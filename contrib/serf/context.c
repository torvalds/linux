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

/**
 * Callback function (implements serf_progress_t). Takes a number of bytes
 * read @a read and bytes written @a written, adds those to the total for this
 * context and notifies an interested party (if any).
 */
void serf__context_progress_delta(
    void *progress_baton,
    apr_off_t read,
    apr_off_t written)
{
    serf_context_t *ctx = progress_baton;

    ctx->progress_read += read;
    ctx->progress_written += written;

    if (ctx->progress_func)
        ctx->progress_func(ctx->progress_baton,
                           ctx->progress_read,
                           ctx->progress_written);
}


/* Check for dirty connections and update their pollsets accordingly. */
static apr_status_t check_dirty_pollsets(serf_context_t *ctx)
{
    int i;

    /* if we're not dirty, return now. */
    if (!ctx->dirty_pollset) {
        return APR_SUCCESS;
    }

    for (i = ctx->conns->nelts; i--; ) {
        serf_connection_t *conn = GET_CONN(ctx, i);
        apr_status_t status;

        /* if this connection isn't dirty, skip it. */
        if (!conn->dirty_conn) {
            continue;
        }

        /* reset this connection's flag before we update. */
        conn->dirty_conn = 0;

        if ((status = serf__conn_update_pollset(conn)) != APR_SUCCESS)
            return status;
    }

    /* reset our context flag now */
    ctx->dirty_pollset = 0;

    return APR_SUCCESS;
}


static apr_status_t pollset_add(void *user_baton,
                                apr_pollfd_t *pfd,
                                void *serf_baton)
{
    serf_pollset_t *s = (serf_pollset_t*)user_baton;
    pfd->client_data = serf_baton;
    return apr_pollset_add(s->pollset, pfd);
}

static apr_status_t pollset_rm(void *user_baton,
                               apr_pollfd_t *pfd,
                               void *serf_baton)
{
    serf_pollset_t *s = (serf_pollset_t*)user_baton;
    pfd->client_data = serf_baton;
    return apr_pollset_remove(s->pollset, pfd);
}


void serf_config_proxy(serf_context_t *ctx,
                       apr_sockaddr_t *address)
{
    ctx->proxy_address = address;
}


void serf_config_credentials_callback(serf_context_t *ctx,
                                      serf_credentials_callback_t cred_cb)
{
    ctx->cred_cb = cred_cb;
}


void serf_config_authn_types(serf_context_t *ctx,
                             int authn_types)
{
    ctx->authn_types = authn_types;
}


serf_context_t *serf_context_create_ex(
    void *user_baton,
    serf_socket_add_t addf,
    serf_socket_remove_t rmf,
    apr_pool_t *pool)
{
    serf_context_t *ctx = apr_pcalloc(pool, sizeof(*ctx));

    ctx->pool = pool;

    if (user_baton != NULL) {
        ctx->pollset_baton = user_baton;
        ctx->pollset_add = addf;
        ctx->pollset_rm = rmf;
    }
    else {
        /* build the pollset with a (default) number of connections */
        serf_pollset_t *ps = apr_pcalloc(pool, sizeof(*ps));

        /* ### TODO: As of APR 1.4.x apr_pollset_create_ex can return a status
           ### other than APR_SUCCESS, so we should handle it.
           ### Probably move creation of the pollset to later when we have
           ### the possibility of returning status to the caller.
         */
#ifdef BROKEN_WSAPOLL
        /* APR 1.4.x switched to using WSAPoll() on Win32, but it does not
         * properly handle errors on a non-blocking sockets (such as
         * connecting to a server where no listener is active).
         *
         * So, sadly, we must force using select() on Win32.
         *
         * http://mail-archives.apache.org/mod_mbox/apr-dev/201105.mbox/%3CBANLkTin3rBCecCBRvzUA5B-14u-NWxR_Kg@mail.gmail.com%3E
         */
        (void) apr_pollset_create_ex(&ps->pollset, MAX_CONN, pool, 0,
                                     APR_POLLSET_SELECT);
#else
        (void) apr_pollset_create(&ps->pollset, MAX_CONN, pool, 0);
#endif
        ctx->pollset_baton = ps;
        ctx->pollset_add = pollset_add;
        ctx->pollset_rm = pollset_rm;
    }

    /* default to a single connection since that is the typical case */
    ctx->conns = apr_array_make(pool, 1, sizeof(serf_connection_t *));

    /* Initialize progress status */
    ctx->progress_read = 0;
    ctx->progress_written = 0;

    ctx->authn_types = SERF_AUTHN_ALL;
    ctx->server_authn_info = apr_hash_make(pool);

    return ctx;
}


serf_context_t *serf_context_create(apr_pool_t *pool)
{
    return serf_context_create_ex(NULL, NULL, NULL, pool);
}

apr_status_t serf_context_prerun(serf_context_t *ctx)
{
    apr_status_t status = APR_SUCCESS;
    if ((status = serf__open_connections(ctx)) != APR_SUCCESS)
        return status;

    if ((status = check_dirty_pollsets(ctx)) != APR_SUCCESS)
        return status;
    return status;
}


apr_status_t serf_event_trigger(
    serf_context_t *s,
    void *serf_baton,
    const apr_pollfd_t *desc)
{
    apr_pollfd_t tdesc = { 0 };
    apr_status_t status = APR_SUCCESS;
    serf_io_baton_t *io = serf_baton;

    if (io->type == SERF_IO_CONN) {
        serf_connection_t *conn = io->u.conn;
        serf_context_t *ctx = conn->ctx;

        /* If this connection has already failed, return the error again, and try
         * to remove it from the pollset again
         */
        if (conn->status) {
            tdesc.desc_type = APR_POLL_SOCKET;
            tdesc.desc.s = conn->skt;
            tdesc.reqevents = conn->reqevents;
            ctx->pollset_rm(ctx->pollset_baton,
                            &tdesc, &conn->baton);
            return conn->status;
        }
        /* apr_pollset_poll() can return a conn multiple times... */
        if ((conn->seen_in_pollset & desc->rtnevents) != 0 ||
            (conn->seen_in_pollset & APR_POLLHUP) != 0) {
            return APR_SUCCESS;
        }

        conn->seen_in_pollset |= desc->rtnevents;

        if ((conn->status = serf__process_connection(conn,
                                         desc->rtnevents)) != APR_SUCCESS) {

            /* it's possible that the connection was already reset and thus the
               socket cleaned up. */
            if (conn->skt) {
                tdesc.desc_type = APR_POLL_SOCKET;
                tdesc.desc.s = conn->skt;
                tdesc.reqevents = conn->reqevents;
                ctx->pollset_rm(ctx->pollset_baton,
                                &tdesc, &conn->baton);
            }
            return conn->status;
        }
    }
    else if (io->type == SERF_IO_LISTENER) {
        serf_listener_t *l = io->u.listener;

        status = serf__process_listener(l);

        if (status) {
            return status;
        }
    }
    else if (io->type == SERF_IO_CLIENT) {
        serf_incoming_t *c = io->u.client;

        status = serf__process_client(c, desc->rtnevents);

        if (status) {
            return status;
        }
    }
    return status;
}


apr_status_t serf_context_run(
    serf_context_t *ctx,
    apr_short_interval_time_t duration,
    apr_pool_t *pool)
{
    apr_status_t status;
    apr_int32_t num;
    const apr_pollfd_t *desc;
    serf_pollset_t *ps = (serf_pollset_t*)ctx->pollset_baton;

    if ((status = serf_context_prerun(ctx)) != APR_SUCCESS) {
        return status;
    }

    if ((status = apr_pollset_poll(ps->pollset, duration, &num,
                                   &desc)) != APR_SUCCESS) {
        /* EINTR indicates a handled signal happened during the poll call,
           ignore, the application can safely retry. */
        if (APR_STATUS_IS_EINTR(status))
            return APR_SUCCESS;

        /* ### do we still need to dispatch stuff here?
           ### look at the potential return codes. map to our defined
           ### return values? ...
        */

        /* Use the strict documented error for poll timeouts, to allow proper
           handling of the other timeout types when returned from
           serf_event_trigger */
        if (APR_STATUS_IS_TIMEUP(status))
            return APR_TIMEUP; /* Return the documented error */
        return status;
    }

    while (num--) {
        serf_io_baton_t *io  = desc->client_data;

        status = serf_event_trigger(ctx, io, desc);
        if (status) {
            return status;
        }

        desc++;
    }

    return APR_SUCCESS;
}


void serf_context_set_progress_cb(
    serf_context_t *ctx,
    const serf_progress_t progress_func,
    void *progress_baton)
{
    ctx->progress_func = progress_func;
    ctx->progress_baton = progress_baton;
}


serf_bucket_t *serf_context_bucket_socket_create(
    serf_context_t *ctx,
    apr_socket_t *skt,
    serf_bucket_alloc_t *allocator)
{
    serf_bucket_t *bucket = serf_bucket_socket_create(skt, allocator);

    /* Use serf's default bytes read/written callback */
    serf_bucket_socket_set_read_progress_cb(bucket,
                                            serf__context_progress_delta,
                                            ctx);

    return bucket;
}


/* ### this really ought to go somewhere else, but... meh.  */
void serf_lib_version(int *major, int *minor, int *patch)
{
    *major = SERF_MAJOR_VERSION;
    *minor = SERF_MINOR_VERSION;
    *patch = SERF_PATCH_VERSION;
}


const char *serf_error_string(apr_status_t errcode)
{
    switch (errcode)
    {
    case SERF_ERROR_CLOSING:
        return "The connection is closing";
    case SERF_ERROR_REQUEST_LOST:
        return "A request has been lost";
    case SERF_ERROR_WAIT_CONN:
        return "The connection is blocked, pending further action";
    case SERF_ERROR_DECOMPRESSION_FAILED:
        return "An error occurred during decompression";
    case SERF_ERROR_BAD_HTTP_RESPONSE:
        return "The server sent an improper HTTP response";
    case SERF_ERROR_TRUNCATED_HTTP_RESPONSE:
        return "The server sent a truncated HTTP response body.";
    case SERF_ERROR_ABORTED_CONNECTION:
        return "The server unexpectedly closed the connection.";
    case SERF_ERROR_SSL_COMM_FAILED:
        return "An error occurred during SSL communication";
    case SERF_ERROR_SSL_CERT_FAILED:
        return "An SSL certificate related error occurred ";
    case SERF_ERROR_AUTHN_FAILED:
        return "An error occurred during authentication";
    case SERF_ERROR_AUTHN_NOT_SUPPORTED:
        return "The requested authentication type(s) are not supported";
    case SERF_ERROR_AUTHN_MISSING_ATTRIBUTE:
        return "An authentication attribute is missing";
    case SERF_ERROR_AUTHN_INITALIZATION_FAILED:
        return "Initialization of an authentication type failed";
    case SERF_ERROR_SSLTUNNEL_SETUP_FAILED:
        return "The proxy server returned an error while setting up the "
               "SSL tunnel.";
    default:
        return NULL;
    }

    /* NOTREACHED  */
}
