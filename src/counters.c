/*
 * Copyright (C) 2017 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <time.h>
#include <libubox/ulog.h>
#include <libubox/utils.h>
#include <libubox/blobmsg_json.h>

#include "auth.h"
#include "http.h"
#include "auth.h"
#include "utils.h"
#include "config.h"
#include "term.h"

enum {
    COUNTERS_RESP,
    _COUNTERS_MAX
};

static const struct blobmsg_policy counters_pol[] = {
    [COUNTERS_RESP] = {
        .name = "resp",
        .type = BLOBMSG_TYPE_ARRAY
    }
};

enum {
    COUNTERS_RESP_MAC,
    COUNTERS_RESP_AUTH,
    _COUNTERS_RESP_MAX
};

static const struct blobmsg_policy resp_pol[] = {
   [COUNTERS_RESP_MAC] = {
       .name = "mac",
       .type = BLOBMSG_TYPE_STRING
   },
   [COUNTERS_RESP_AUTH] = {
       .name = "auth",
       .type = BLOBMSG_TYPE_INT32
   }
};

static void counters_cb(void *data, char *body)
{
    static struct blob_buf b;
    struct blob_attr *tb[_COUNTERS_RESP_MAX];

    if (!body)
        return;

    blobmsg_buf_init(&b);

    if (!blobmsg_add_json_from_string(&b, body)) {
        ULOG_ERR("counters: invalid resp format\n");
        blob_buf_free(&b);
        return;
    }

    blobmsg_parse(counters_pol, _COUNTERS_MAX, tb, blob_data(b.head), blob_len(b.head));

    if (tb[COUNTERS_RESP]) {
        int rem;
        struct blob_attr *item;

        blobmsg_for_each_attr(item, tb[COUNTERS_RESP], rem) {
            blobmsg_parse(resp_pol, _COUNTERS_RESP_MAX, tb, blobmsg_data(item), blobmsg_data_len(item));

            if (tb[COUNTERS_RESP_MAC]) {
                if (tb[COUNTERS_RESP_AUTH] && !blobmsg_get_u32(tb[COUNTERS_RESP_AUTH])) {
                    const char *mac = blobmsg_data(tb[COUNTERS_RESP_MAC]);
                    ULOG_INFO("Auth server resp deny for %s\n", mac);
                    del_term_by_mac(mac);
                }
            }
        }
    }

    blob_buf_free(&b);
}

static void counters(struct uloop_timeout *t)
{
    struct config *conf = get_config();
    struct terminal *term, *ptr;
    struct blob_buf b = { };
    time_t now = time(NULL);
    void *tbl, *array;
    char *p;

    uloop_timeout_set(t, 1000 * conf->checkinterval);

    blobmsg_buf_init(&b);

    array = blobmsg_open_array(&b, "counters");

    avl_for_each_element_safe(&term_tree, term, avl, ptr) {
        if (!(term->flag & TERM_FLAG_AUTHED))
            continue;

        tbl = blobmsg_open_table(&b, "");
        blobmsg_add_string(&b, "ip", term->ip);
        blobmsg_add_string(&b, "mac", term->mac);
        blobmsg_add_string(&b, "token", term->token);
        blobmsg_add_u32(&b, "uptime", now - term->auth_time);
        blobmsg_add_u32(&b, "incoming", term->rx);
        blobmsg_add_u32(&b, "outgoing", term->tx);
        blobmsg_close_table(&b, tbl);

        if (term->flag & TERM_FLAG_TIMEOUT) {
            authserver_request(NULL, AUTH_REQUEST_TYPE_LOGOUT, term->ip, term->mac, term->token);
            del_term(term);
        }
    }

    blobmsg_close_table(&b, array);
    p = blobmsg_format_json(b.head, true);
    httppost(counters_cb, NULL, p, "%s&stage=counters", conf->auth_url);

    free(p);
    blob_buf_free(&b);
}

static struct uloop_timeout timeout = {
    .cb = counters
};

void start_counters()
{
    uloop_timeout_set(&timeout, 0);
}

void stop_counters()
{
    uloop_timeout_cancel(&timeout);
}
