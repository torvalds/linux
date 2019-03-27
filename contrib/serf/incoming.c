/* ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_pools.h>
#include <apr_poll.h>
#include <apr_version.h>

#include "serf.h"
#include "serf_bucket_util.h"

#include "serf_private.h"

static apr_status_t read_from_client(serf_incoming_t *client)
{
    return APR_ENOTIMPL;
}

static apr_status_t write_to_client(serf_incoming_t *client)
{
    return APR_ENOTIMPL;
}

apr_status_t serf__process_client(serf_incoming_t *client, apr_int16_t events)
{
    apr_status_t rv;
    if ((events & APR_POLLIN) != 0) {
        rv = read_from_client(client);
        if (rv) {
            return rv;
        }
    }

    if ((events & APR_POLLHUP) != 0) {
        return APR_ECONNRESET;
    }

    if ((events & APR_POLLERR) != 0) {
        return APR_EGENERAL;
    }

    if ((events & APR_POLLOUT) != 0) {
        rv = write_to_client(client);
        if (rv) {
            return rv;
        }
    }

    return APR_SUCCESS;
}

apr_status_t serf__process_listener(serf_listener_t *l)
{
    apr_status_t rv;
    apr_socket_t *in;
    apr_pool_t *p;
    /* THIS IS NOT OPTIMAL */
    apr_pool_create(&p, l->pool);

    rv = apr_socket_accept(&in, l->skt, p);

    if (rv) {
        apr_pool_destroy(p);
        return rv;
    }

    rv = l->accept_func(l->ctx, l, l->accept_baton, in, p);

    if (rv) {
        apr_pool_destroy(p);
        return rv;
    }

    return rv;
}


apr_status_t serf_incoming_create(
    serf_incoming_t **client,
    serf_context_t *ctx,
    apr_socket_t *insock,
    void *request_baton,
    serf_incoming_request_cb_t request,
    apr_pool_t *pool)
{
    apr_status_t rv;
    serf_incoming_t *ic = apr_palloc(pool, sizeof(*ic));

    ic->ctx = ctx;
    ic->baton.type = SERF_IO_CLIENT;
    ic->baton.u.client = ic;
    ic->request_baton =  request_baton;
    ic->request = request;
    ic->skt = insock;
    ic->desc.desc_type = APR_POLL_SOCKET;
    ic->desc.desc.s = ic->skt;
    ic->desc.reqevents = APR_POLLIN;

    rv = ctx->pollset_add(ctx->pollset_baton,
                         &ic->desc, &ic->baton);
    *client = ic;

    return rv;
}


apr_status_t serf_listener_create(
    serf_listener_t **listener,
    serf_context_t *ctx,
    const char *host,
    apr_uint16_t port,
    void *accept_baton,
    serf_accept_client_t accept,
    apr_pool_t *pool)
{
    apr_sockaddr_t *sa;
    apr_status_t rv;
    serf_listener_t *l = apr_palloc(pool, sizeof(*l));

    l->ctx = ctx;
    l->baton.type = SERF_IO_LISTENER;
    l->baton.u.listener = l;
    l->accept_func = accept;
    l->accept_baton = accept_baton;

    apr_pool_create(&l->pool, pool);

    rv = apr_sockaddr_info_get(&sa, host, APR_UNSPEC, port, 0, l->pool);
    if (rv)
        return rv;

    rv = apr_socket_create(&l->skt, sa->family,
                           SOCK_STREAM,
#if APR_MAJOR_VERSION > 0
                           APR_PROTO_TCP,
#endif
                           l->pool);
    if (rv)
        return rv;

    rv = apr_socket_opt_set(l->skt, APR_SO_REUSEADDR, 1);
    if (rv)
        return rv;

    rv = apr_socket_bind(l->skt, sa);
    if (rv)
        return rv;

    rv = apr_socket_listen(l->skt, 5);
    if (rv)
        return rv;

    l->desc.desc_type = APR_POLL_SOCKET;
    l->desc.desc.s = l->skt;
    l->desc.reqevents = APR_POLLIN;

    rv = ctx->pollset_add(ctx->pollset_baton,
                            &l->desc, &l->baton);
    if (rv)
        return rv;

    *listener = l;

    return APR_SUCCESS;
}
