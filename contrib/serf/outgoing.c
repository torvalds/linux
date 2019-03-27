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
#include <apr_portable.h>

#include "serf.h"
#include "serf_bucket_util.h"

#include "serf_private.h"

/* cleanup for sockets */
static apr_status_t clean_skt(void *data)
{
    serf_connection_t *conn = data;
    apr_status_t status = APR_SUCCESS;

    if (conn->skt) {
        serf__log_skt(SOCK_VERBOSE, __FILE__, conn->skt, "cleanup - ");
        status = apr_socket_close(conn->skt);
        conn->skt = NULL;
        serf__log_nopref(SOCK_VERBOSE, "closed socket, status %d\n", status);
    }

    return status;
}

static apr_status_t clean_resp(void *data)
{
    serf_request_t *request = data;

    /* The request's RESPOOL is being cleared.  */

    /* If the response has allocated some buckets, then destroy them (since
       the bucket may hold resources other than memory in RESPOOL). Also
       make sure to set their fields to NULL so connection closure does
       not attempt to free them again.  */
    if (request->resp_bkt) {
        serf_bucket_destroy(request->resp_bkt);
        request->resp_bkt = NULL;
    }
    if (request->req_bkt) {
        serf_bucket_destroy(request->req_bkt);
        request->req_bkt = NULL;
    }

    /* ### should we worry about debug stuff, like that performed in
       ### destroy_request()? should we worry about calling req->handler
       ### to notify this "cancellation" due to pool clearing?  */

    /* This pool just got cleared/destroyed. Don't try to destroy the pool
       (again) when the request is canceled.  */
    request->respool = NULL;

    return APR_SUCCESS;
}

/* cleanup for conns */
static apr_status_t clean_conn(void *data)
{
    serf_connection_t *conn = data;

    serf__log(CONN_VERBOSE, __FILE__, "cleaning up connection 0x%x\n",
              conn);
    serf_connection_close(conn);

    return APR_SUCCESS;
}

/* Check if there is data waiting to be sent over the socket. This can happen
   in two situations:
   - The connection queue has atleast one request with unwritten data.
   - All requests are written and the ssl layer wrote some data while reading
     the response. This can happen when the server triggers a renegotiation,
     e.g. after the first and only request on that connection was received.
   Returns 1 if data is pending on CONN, NULL if not.
   If NEXT_REQ is not NULL, it will be filled in with the next available request
   with unwritten data. */
static int
request_or_data_pending(serf_request_t **next_req, serf_connection_t *conn)
{
    serf_request_t *request = conn->requests;

    while (request != NULL && request->req_bkt == NULL &&
           request->writing_started)
        request = request->next;

    if (next_req)
        *next_req = request;

    if (request != NULL) {
        return 1;
    } else if (conn->ostream_head) {
        const char *dummy;
        apr_size_t len;
        apr_status_t status;

        status = serf_bucket_peek(conn->ostream_head, &dummy,
                                  &len);
        if (!SERF_BUCKET_READ_ERROR(status) && len) {
            serf__log_skt(CONN_VERBOSE, __FILE__, conn->skt,
                          "All requests written but still data pending.\n");
            return 1;
        }
    }

    return 0;
}

/* Update the pollset for this connection. We tweak the pollset based on
 * whether we want to read and/or write, given conditions within the
 * connection. If the connection is not (yet) in the pollset, then it
 * will be added.
 */
apr_status_t serf__conn_update_pollset(serf_connection_t *conn)
{
    serf_context_t *ctx = conn->ctx;
    apr_status_t status;
    apr_pollfd_t desc = { 0 };

    if (!conn->skt) {
        return APR_SUCCESS;
    }

    /* Remove the socket from the poll set. */
    desc.desc_type = APR_POLL_SOCKET;
    desc.desc.s = conn->skt;
    desc.reqevents = conn->reqevents;

    status = ctx->pollset_rm(ctx->pollset_baton,
                             &desc, &conn->baton);
    if (status && !APR_STATUS_IS_NOTFOUND(status))
        return status;

    /* Now put it back in with the correct read/write values. */
    desc.reqevents = APR_POLLHUP | APR_POLLERR;
    if (conn->requests &&
        conn->state != SERF_CONN_INIT) {
        /* If there are any outstanding events, then we want to read. */
        /* ### not true. we only want to read IF we have sent some data */
        desc.reqevents |= APR_POLLIN;

        /* Don't write if OpenSSL told us that it needs to read data first. */
        if (conn->stop_writing != 1) {

            /* If the connection is not closing down and
             *   has unwritten data or
             *   there are any requests that still have buckets to write out,
             *     then we want to write.
             */
            if (conn->vec_len &&
                conn->state != SERF_CONN_CLOSING)
                desc.reqevents |= APR_POLLOUT;
            else {

                if ((conn->probable_keepalive_limit &&
                     conn->completed_requests > conn->probable_keepalive_limit) ||
                    (conn->max_outstanding_requests &&
                     conn->completed_requests - conn->completed_responses >=
                     conn->max_outstanding_requests)) {
                        /* we wouldn't try to write any way right now. */
                }
                else if (request_or_data_pending(NULL, conn)) {
                    desc.reqevents |= APR_POLLOUT;
                }
            }
        }
    }

    /* If we can have async responses, always look for something to read. */
    if (conn->async_responses) {
        desc.reqevents |= APR_POLLIN;
    }

    /* save our reqevents, so we can pass it in to remove later. */
    conn->reqevents = desc.reqevents;

    /* Note: even if we don't want to read/write this socket, we still
     * want to poll it for hangups and errors.
     */
    return ctx->pollset_add(ctx->pollset_baton,
                            &desc, &conn->baton);
}

#ifdef SERF_DEBUG_BUCKET_USE

/* Make sure all response buckets were drained. */
static void check_buckets_drained(serf_connection_t *conn)
{
    serf_request_t *request = conn->requests;

    for ( ; request ; request = request->next ) {
        if (request->resp_bkt != NULL) {
            /* ### crap. can't do this. this allocator may have un-drained
             * ### REQUEST buckets.
             */
            /* serf_debug__entered_loop(request->resp_bkt->allocator); */
            /* ### for now, pretend we closed the conn (resets the tracking) */
            serf_debug__closed_conn(request->resp_bkt->allocator);
        }
    }
}

#endif

static void destroy_ostream(serf_connection_t *conn)
{
    if (conn->ostream_head != NULL) {
        serf_bucket_destroy(conn->ostream_head);
        conn->ostream_head = NULL;
        conn->ostream_tail = NULL;
    }
}

static apr_status_t detect_eof(void *baton, serf_bucket_t *aggregate_bucket)
{
    serf_connection_t *conn = baton;
    conn->hit_eof = 1;
    return APR_EAGAIN;
}

static apr_status_t do_conn_setup(serf_connection_t *conn)
{
    apr_status_t status;
    serf_bucket_t *ostream;

    if (conn->ostream_head == NULL) {
        conn->ostream_head = serf_bucket_aggregate_create(conn->allocator);
    }

    if (conn->ostream_tail == NULL) {
        conn->ostream_tail = serf__bucket_stream_create(conn->allocator,
                                                        detect_eof,
                                                        conn);
    }

    ostream = conn->ostream_tail;

    status = (*conn->setup)(conn->skt,
                            &conn->stream,
                            &ostream,
                            conn->setup_baton,
                            conn->pool);
    if (status) {
        /* extra destroy here since it wasn't added to the head bucket yet. */
        serf_bucket_destroy(conn->ostream_tail);
        destroy_ostream(conn);
        return status;
    }

    serf_bucket_aggregate_append(conn->ostream_head,
                                 ostream);

    return status;
}

/* Set up the input and output stream buckets.
 When a tunnel over an http proxy is needed, create a socket bucket and
 empty aggregate bucket for sending and receiving unencrypted requests
 over the socket.

 After the tunnel is there, or no tunnel was needed, ask the application
 to create the input and output buckets, which should take care of the
 [en/de]cryption.
 */

static apr_status_t prepare_conn_streams(serf_connection_t *conn,
                                         serf_bucket_t **istream,
                                         serf_bucket_t **ostreamt,
                                         serf_bucket_t **ostreamh)
{
    apr_status_t status;

    if (conn->stream == NULL) {
        conn->latency = apr_time_now() - conn->connect_time;
    }

    /* Do we need a SSL tunnel first? */
    if (conn->state == SERF_CONN_CONNECTED) {
        /* If the connection does not have an associated bucket, then
         * call the setup callback to get one.
         */
        if (conn->stream == NULL) {
            status = do_conn_setup(conn);
            if (status) {
                return status;
            }
        }
        *ostreamt = conn->ostream_tail;
        *ostreamh = conn->ostream_head;
        *istream = conn->stream;
    } else {
        /* SSL tunnel needed and not set up yet, get a direct unencrypted
         stream for this socket */
        if (conn->stream == NULL) {
            *istream = serf_bucket_socket_create(conn->skt,
                                                 conn->allocator);
        }
        /* Don't create the ostream bucket chain including the ssl_encrypt
         bucket yet. This ensure the CONNECT request is sent unencrypted
         to the proxy. */
        *ostreamt = *ostreamh = conn->ssltunnel_ostream;
    }

    return APR_SUCCESS;
}

/* Create and connect sockets for any connections which don't have them
 * yet. This is the core of our lazy-connect behavior.
 */
apr_status_t serf__open_connections(serf_context_t *ctx)
{
    int i;

    for (i = ctx->conns->nelts; i--; ) {
        serf_connection_t *conn = GET_CONN(ctx, i);
        serf__authn_info_t *authn_info;
        apr_status_t status;
        apr_socket_t *skt;

        conn->seen_in_pollset = 0;

        if (conn->skt != NULL) {
#ifdef SERF_DEBUG_BUCKET_USE
            check_buckets_drained(conn);
#endif
            continue;
        }

        /* Delay opening until we have something to deliver! */
        if (conn->requests == NULL) {
            continue;
        }

        apr_pool_clear(conn->skt_pool);
        apr_pool_cleanup_register(conn->skt_pool, conn, clean_skt, clean_skt);

        status = apr_socket_create(&skt, conn->address->family,
                                   SOCK_STREAM,
#if APR_MAJOR_VERSION > 0
                                   APR_PROTO_TCP,
#endif
                                   conn->skt_pool);
        serf__log(SOCK_VERBOSE, __FILE__,
                  "created socket for conn 0x%x, status %d\n", conn, status);
        if (status != APR_SUCCESS)
            return status;

        /* Set the socket to be non-blocking */
        if ((status = apr_socket_timeout_set(skt, 0)) != APR_SUCCESS)
            return status;

        /* Disable Nagle's algorithm */
        if ((status = apr_socket_opt_set(skt,
                                         APR_TCP_NODELAY, 1)) != APR_SUCCESS)
            return status;

        /* Configured. Store it into the connection now. */
        conn->skt = skt;

        /* Remember time when we started connecting to server to calculate
           network latency. */
        conn->connect_time = apr_time_now();

        /* Now that the socket is set up, let's connect it. This should
         * return immediately.
         */
        status = apr_socket_connect(skt, conn->address);
        serf__log_skt(SOCK_VERBOSE, __FILE__, skt,
                      "connected socket for conn 0x%x, status %d\n",
                      conn, status);
        if (status != APR_SUCCESS) {
            if (!APR_STATUS_IS_EINPROGRESS(status))
                return status;
        }

        /* Flag our pollset as dirty now that we have a new socket. */
        conn->dirty_conn = 1;
        ctx->dirty_pollset = 1;

        /* If the authentication was already started on another connection,
           prepare this connection (it might be possible to skip some
           part of the handshaking). */
        if (ctx->proxy_address) {
            authn_info = &ctx->proxy_authn_info;
            if (authn_info->scheme) {
                authn_info->scheme->init_conn_func(authn_info->scheme, 407,
                                                   conn, conn->pool);
            }
        }

        authn_info = serf__get_authn_info_for_server(conn);
        if (authn_info->scheme) {
            authn_info->scheme->init_conn_func(authn_info->scheme, 401,
                                               conn, conn->pool);
        }

        /* Does this connection require a SSL tunnel over the proxy? */
        if (ctx->proxy_address && strcmp(conn->host_info.scheme, "https") == 0)
            serf__ssltunnel_connect(conn);
        else {
            serf_bucket_t *dummy1, *dummy2;

            conn->state = SERF_CONN_CONNECTED;

            status = prepare_conn_streams(conn, &conn->stream,
                                          &dummy1, &dummy2);
            if (status) {
                return status;
            }
        }
    }

    return APR_SUCCESS;
}

static apr_status_t no_more_writes(serf_connection_t *conn)
{
    /* Note that we should hold new requests until we open our new socket. */
    conn->state = SERF_CONN_CLOSING;
    serf__log_skt(CONN_VERBOSE, __FILE__, conn->skt,
                  "stop writing on conn 0x%x\n", conn);

    /* Clear our iovec. */
    conn->vec_len = 0;

    /* Update the pollset to know we don't want to write on this socket any
     * more.
     */
    conn->dirty_conn = 1;
    conn->ctx->dirty_pollset = 1;
    return APR_SUCCESS;
}

/* Read the 'Connection' header from the response. Return SERF_ERROR_CLOSING if
 * the header contains value 'close' indicating the server is closing the
 * connection right after this response.
 * Otherwise returns APR_SUCCESS.
 */
static apr_status_t is_conn_closing(serf_bucket_t *response)
{
    serf_bucket_t *hdrs;
    const char *val;

    hdrs = serf_bucket_response_get_headers(response);
    val = serf_bucket_headers_get(hdrs, "Connection");
    if (val && strcasecmp("close", val) == 0)
        {
            return SERF_ERROR_CLOSING;
        }

    return APR_SUCCESS;
}

static void link_requests(serf_request_t **list, serf_request_t **tail,
                          serf_request_t *request)
{
    if (*list == NULL) {
        *list = request;
        *tail = request;
    }
    else {
        (*tail)->next = request;
        *tail = request;
    }
}

static apr_status_t destroy_request(serf_request_t *request)
{
    serf_connection_t *conn = request->conn;

    /* The request and response buckets are no longer needed,
       nor is the request's pool.  */
    if (request->resp_bkt) {
        serf_debug__closed_conn(request->resp_bkt->allocator);
        serf_bucket_destroy(request->resp_bkt);
        request->resp_bkt = NULL;
    }
    if (request->req_bkt) {
        serf_debug__closed_conn(request->req_bkt->allocator);
        serf_bucket_destroy(request->req_bkt);
        request->req_bkt = NULL;
    }

    serf_debug__bucket_alloc_check(request->allocator);
    if (request->respool) {
        /* ### unregister the pool cleanup for self?  */
        apr_pool_destroy(request->respool);
    }

    serf_bucket_mem_free(conn->allocator, request);

    return APR_SUCCESS;
}

static apr_status_t cancel_request(serf_request_t *request,
                                   serf_request_t **list,
                                   int notify_request)
{
    /* If we haven't run setup, then we won't have a handler to call. */
    if (request->handler && notify_request) {
        /* We actually don't care what the handler returns.
         * We have bigger matters at hand.
         */
        (*request->handler)(request, NULL, request->handler_baton,
                            request->respool);
    }

    if (*list == request) {
        *list = request->next;
    }
    else {
        serf_request_t *scan = *list;

        while (scan->next && scan->next != request)
            scan = scan->next;

        if (scan->next) {
            scan->next = scan->next->next;
        }
    }

    return destroy_request(request);
}

static apr_status_t remove_connection(serf_context_t *ctx,
                                      serf_connection_t *conn)
{
    apr_pollfd_t desc = { 0 };

    desc.desc_type = APR_POLL_SOCKET;
    desc.desc.s = conn->skt;
    desc.reqevents = conn->reqevents;

    return ctx->pollset_rm(ctx->pollset_baton,
                           &desc, &conn->baton);
}

/* A socket was closed, inform the application. */
static void handle_conn_closed(serf_connection_t *conn, apr_status_t status)
{
    (*conn->closed)(conn, conn->closed_baton, status,
                    conn->pool);
}

static apr_status_t reset_connection(serf_connection_t *conn,
                                     int requeue_requests)
{
    serf_context_t *ctx = conn->ctx;
    apr_status_t status;
    serf_request_t *old_reqs;

    conn->probable_keepalive_limit = conn->completed_responses;
    conn->completed_requests = 0;
    conn->completed_responses = 0;

    old_reqs = conn->requests;

    conn->requests = NULL;
    conn->requests_tail = NULL;

    /* Handle all outstanding requests. These have either not been written yet,
       or have been written but the expected reply wasn't received yet. */
    while (old_reqs) {
        /* If we haven't started to write the connection, bring it over
         * unchanged to our new socket.
         * Do not copy a CONNECT request to the new connection, the ssl tunnel
         * setup code will create a new CONNECT request already.
         */
        if (requeue_requests && !old_reqs->writing_started &&
            !old_reqs->ssltunnel) {

            serf_request_t *req = old_reqs;
            old_reqs = old_reqs->next;
            req->next = NULL;
            link_requests(&conn->requests, &conn->requests_tail, req);
        }
        else {
            /* Request has been consumed, or we don't want to requeue the
               request. Either way, inform the application that the request
               is cancelled. */
            cancel_request(old_reqs, &old_reqs, requeue_requests);
        }
    }

    /* Requests queue has been prepared for a new socket, close the old one. */
    if (conn->skt != NULL) {
        remove_connection(ctx, conn);
        status = apr_socket_close(conn->skt);
        serf__log_skt(SOCK_VERBOSE, __FILE__, conn->skt,
                      "closed socket, status %d\n", status);
        if (conn->closed != NULL) {
            handle_conn_closed(conn, status);
        }
        conn->skt = NULL;
    }

    if (conn->stream != NULL) {
        serf_bucket_destroy(conn->stream);
        conn->stream = NULL;
    }

    destroy_ostream(conn);

    /* Don't try to resume any writes */
    conn->vec_len = 0;

    conn->dirty_conn = 1;
    conn->ctx->dirty_pollset = 1;
    conn->state = SERF_CONN_INIT;

    conn->hit_eof = 0;
    conn->connect_time = 0;
    conn->latency = -1;
    conn->stop_writing = 0;

    serf__log(CONN_VERBOSE, __FILE__, "reset connection 0x%x\n", conn);

    conn->status = APR_SUCCESS;

    /* Let our context know that we've 'reset' the socket already. */
    conn->seen_in_pollset |= APR_POLLHUP;

    /* Found the connection. Closed it. All done. */
    return APR_SUCCESS;
}

static apr_status_t socket_writev(serf_connection_t *conn)
{
    apr_size_t written;
    apr_status_t status;

    status = apr_socket_sendv(conn->skt, conn->vec,
                              conn->vec_len, &written);
    if (status && !APR_STATUS_IS_EAGAIN(status))
        serf__log_skt(SOCK_VERBOSE, __FILE__, conn->skt,
                      "socket_sendv error %d\n", status);

    /* did we write everything? */
    if (written) {
        apr_size_t len = 0;
        int i;

        serf__log_skt(SOCK_MSG_VERBOSE, __FILE__, conn->skt,
                      "--- socket_sendv:\n");

        for (i = 0; i < conn->vec_len; i++) {
            len += conn->vec[i].iov_len;
            if (written < len) {
                serf__log_nopref(SOCK_MSG_VERBOSE, "%.*s",
                                   conn->vec[i].iov_len - (len - written),
                                   conn->vec[i].iov_base);
                if (i) {
                    memmove(conn->vec, &conn->vec[i],
                            sizeof(struct iovec) * (conn->vec_len - i));
                    conn->vec_len -= i;
                }
                conn->vec[0].iov_base = (char *)conn->vec[0].iov_base + (conn->vec[0].iov_len - (len - written));
                conn->vec[0].iov_len = len - written;
                break;
            } else {
                serf__log_nopref(SOCK_MSG_VERBOSE, "%.*s",
                                   conn->vec[i].iov_len, conn->vec[i].iov_base);
            }
        }
        if (len == written) {
            conn->vec_len = 0;
        }
        serf__log_nopref(SOCK_MSG_VERBOSE, "-(%d)-\n", written);

        /* Log progress information */
        serf__context_progress_delta(conn->ctx, 0, written);
    }

    return status;
}

static apr_status_t setup_request(serf_request_t *request)
{
    serf_connection_t *conn = request->conn;
    apr_status_t status;

    /* Now that we are about to serve the request, allocate a pool. */
    apr_pool_create(&request->respool, conn->pool);
    request->allocator = serf_bucket_allocator_create(request->respool,
                                                      NULL, NULL);
    apr_pool_cleanup_register(request->respool, request,
                              clean_resp, clean_resp);

    /* Fill in the rest of the values for the request. */
    status = request->setup(request, request->setup_baton,
                            &request->req_bkt,
                            &request->acceptor,
                            &request->acceptor_baton,
                            &request->handler,
                            &request->handler_baton,
                            request->respool);
    return status;
}

/* write data out to the connection */
static apr_status_t write_to_connection(serf_connection_t *conn)
{
    if (conn->probable_keepalive_limit &&
        conn->completed_requests > conn->probable_keepalive_limit) {

        conn->dirty_conn = 1;
        conn->ctx->dirty_pollset = 1;

        /* backoff for now. */
        return APR_SUCCESS;
    }

    /* Keep reading and sending until we run out of stuff to read, or
     * writing would block.
     */
    while (1) {
        serf_request_t *request;
        int stop_reading = 0;
        apr_status_t status;
        apr_status_t read_status;
        serf_bucket_t *ostreamt;
        serf_bucket_t *ostreamh;
        int max_outstanding_requests = conn->max_outstanding_requests;

        /* If we're setting up an ssl tunnel, we can't send real requests
           at yet, as they need to be encrypted and our encrypt buckets
           aren't created yet as we still need to read the unencrypted
           response of the CONNECT request. */
        if (conn->state != SERF_CONN_CONNECTED)
            max_outstanding_requests = 1;

        if (max_outstanding_requests &&
            conn->completed_requests -
                conn->completed_responses >= max_outstanding_requests) {
            /* backoff for now. */
            return APR_SUCCESS;
        }

        /* If we have unwritten data, then write what we can. */
        while (conn->vec_len) {
            status = socket_writev(conn);

            /* If the write would have blocked, then we're done. Don't try
             * to write anything else to the socket.
             */
            if (APR_STATUS_IS_EAGAIN(status))
                return APR_SUCCESS;
            if (APR_STATUS_IS_EPIPE(status) ||
                APR_STATUS_IS_ECONNRESET(status) ||
                APR_STATUS_IS_ECONNABORTED(status))
                return no_more_writes(conn);
            if (status)
                return status;
        }
        /* ### can we have a short write, yet no EAGAIN? a short write
           ### would imply unwritten_len > 0 ... */
        /* assert: unwritten_len == 0. */

        /* We may need to move forward to a request which has something
         * to write.
         */
        if (!request_or_data_pending(&request, conn)) {
            /* No more requests (with data) are registered with the
             * connection, and no data is pending on the outgoing stream.
             * Let's update the pollset so that we don't try to write to this
             * socket again.
             */
            conn->dirty_conn = 1;
            conn->ctx->dirty_pollset = 1;
            return APR_SUCCESS;
        }

        status = prepare_conn_streams(conn, &conn->stream, &ostreamt, &ostreamh);
        if (status) {
            return status;
        }

        if (request) {
            if (request->req_bkt == NULL) {
                read_status = setup_request(request);
                if (read_status) {
                    /* Something bad happened. Propagate any errors. */
                    return read_status;
                }
            }

            if (!request->writing_started) {
                request->writing_started = 1;
                serf_bucket_aggregate_append(ostreamt, request->req_bkt);
            }
        }

        /* ### optimize at some point by using read_for_sendfile */
        /* TODO: now that read_iovec will effectively try to return as much
           data as available, we probably don't want to read ALL_AVAIL, but
           a lower number, like the size of one or a few TCP packets, the
           available TCP buffer size ... */
        read_status = serf_bucket_read_iovec(ostreamh,
                                             SERF_READ_ALL_AVAIL,
                                             IOV_MAX,
                                             conn->vec,
                                             &conn->vec_len);

        if (!conn->hit_eof) {
            if (APR_STATUS_IS_EAGAIN(read_status)) {
                /* We read some stuff, but should not try to read again. */
                stop_reading = 1;
            }
            else if (read_status == SERF_ERROR_WAIT_CONN) {
                /* The bucket told us that it can't provide more data until
                   more data is read from the socket. This normally happens
                   during a SSL handshake.

                   We should avoid looking for writability for a while so
                   that (hopefully) something will appear in the bucket so
                   we can actually write something. otherwise, we could
                   end up in a CPU spin: socket wants something, but we
                   don't have anything (and keep returning EAGAIN)
                 */
                conn->stop_writing = 1;
                conn->dirty_conn = 1;
                conn->ctx->dirty_pollset = 1;
            }
            else if (read_status && !APR_STATUS_IS_EOF(read_status)) {
                /* Something bad happened. Propagate any errors. */
                return read_status;
            }
        }

        /* If we got some data, then deliver it. */
        /* ### what to do if we got no data?? is that a problem? */
        if (conn->vec_len > 0) {
            status = socket_writev(conn);

            /* If we can't write any more, or an error occurred, then
             * we're done here.
             */
            if (APR_STATUS_IS_EAGAIN(status))
                return APR_SUCCESS;
            if (APR_STATUS_IS_EPIPE(status))
                return no_more_writes(conn);
            if (APR_STATUS_IS_ECONNRESET(status) ||
                APR_STATUS_IS_ECONNABORTED(status)) {
                return no_more_writes(conn);
            }
            if (status)
                return status;
        }

        if (read_status == SERF_ERROR_WAIT_CONN) {
            stop_reading = 1;
            conn->stop_writing = 1;
            conn->dirty_conn = 1;
            conn->ctx->dirty_pollset = 1;
        }
        else if (request && read_status && conn->hit_eof &&
                 conn->vec_len == 0) {
            /* If we hit the end of the request bucket and all of its data has
             * been written, then clear it out to signify that we're done
             * sending the request. On the next iteration through this loop:
             * - if there are remaining bytes they will be written, and as the 
             * request bucket will be completely read it will be destroyed then.
             * - we'll see if there are other requests that need to be sent 
             * ("pipelining").
             */
            conn->hit_eof = 0;
            serf_bucket_destroy(request->req_bkt);
            request->req_bkt = NULL;

            /* If our connection has async responses enabled, we're not
             * going to get a reply back, so kill the request.
             */
            if (conn->async_responses) {
                conn->requests = request->next;
                destroy_request(request);
            }

            conn->completed_requests++;

            if (conn->probable_keepalive_limit &&
                conn->completed_requests > conn->probable_keepalive_limit) {
                /* backoff for now. */
                stop_reading = 1;
            }
        }

        if (stop_reading) {
            return APR_SUCCESS;
        }
    }
    /* NOTREACHED */
}

/* A response message was received from the server, so call
   the handler as specified on the original request. */
static apr_status_t handle_response(serf_request_t *request,
                                    apr_pool_t *pool)
{
    apr_status_t status = APR_SUCCESS;
    int consumed_response = 0;

    /* Only enable the new authentication framework if the program has
     * registered an authentication credential callback.
     *
     * This permits older Serf apps to still handle authentication
     * themselves by not registering credential callbacks.
     */
    if (request->conn->ctx->cred_cb) {
      status = serf__handle_auth_response(&consumed_response,
                                          request,
                                          request->resp_bkt,
                                          request->handler_baton,
                                          pool);

      /* If there was an error reading the response (maybe there wasn't
         enough data available), don't bother passing the response to the
         application.

         If the authentication was tried, but failed, pass the response
         to the application, maybe it can do better. */
      if (status) {
          return status;
      }
    }

    if (!consumed_response) {
        return (*request->handler)(request,
                                   request->resp_bkt,
                                   request->handler_baton,
                                   pool);
    }

    return status;
}

/* An async response message was received from the server. */
static apr_status_t handle_async_response(serf_connection_t *conn,
                                          apr_pool_t *pool)
{
    apr_status_t status;

    if (conn->current_async_response == NULL) {
        conn->current_async_response =
            (*conn->async_acceptor)(NULL, conn->stream,
                                    conn->async_acceptor_baton, pool);
    }

    status = (*conn->async_handler)(NULL, conn->current_async_response,
                                    conn->async_handler_baton, pool);

    if (APR_STATUS_IS_EOF(status)) {
        serf_bucket_destroy(conn->current_async_response);
        conn->current_async_response = NULL;
        status = APR_SUCCESS;
    }

    return status;
}


apr_status_t
serf__provide_credentials(serf_context_t *ctx,
                          char **username,
                          char **password,
                          serf_request_t *request, void *baton,
                          int code, const char *authn_type,
                          const char *realm,
                          apr_pool_t *pool)
{
    serf_connection_t *conn = request->conn;
    serf_request_t *authn_req = request;
    apr_status_t status;

    if (request->ssltunnel == 1 &&
        conn->state == SERF_CONN_SETUP_SSLTUNNEL) {
        /* This is a CONNECT request to set up an SSL tunnel over a proxy.
           This request is created by serf, so if the proxy requires
           authentication, we can't ask the application for credentials with
           this request.

           Solution: setup the first request created by the application on
           this connection, and use that request and its handler_baton to
           call back to the application. */

        authn_req = request->next;
        /* assert: app_request != NULL */
        if (!authn_req)
            return APR_EGENERAL;

        if (!authn_req->req_bkt) {
            apr_status_t status;

            status = setup_request(authn_req);
            /* If we can't setup a request, don't bother setting up the
               ssl tunnel. */
            if (status)
                return status;
        }
    }

    /* Ask the application. */
    status = (*ctx->cred_cb)(username, password,
                             authn_req, authn_req->handler_baton,
                             code, authn_type, realm, pool);
    if (status)
        return status;

    return APR_SUCCESS;
}

/* read data from the connection */
static apr_status_t read_from_connection(serf_connection_t *conn)
{
    apr_status_t status;
    apr_pool_t *tmppool;
    int close_connection = FALSE;

    /* Whatever is coming in on the socket corresponds to the first request
     * on our chain.
     */
    serf_request_t *request = conn->requests;

    /* If the stop_writing flag was set on the connection, reset it now because
       there is some data to read. */
    if (conn->stop_writing) {
        conn->stop_writing = 0;
        conn->dirty_conn = 1;
        conn->ctx->dirty_pollset = 1;
    }

    /* assert: request != NULL */

    if ((status = apr_pool_create(&tmppool, conn->pool)) != APR_SUCCESS)
        goto error;

    /* Invoke response handlers until we have no more work. */
    while (1) {
        serf_bucket_t *dummy1, *dummy2;

        apr_pool_clear(tmppool);

        /* Only interested in the input stream here. */
        status = prepare_conn_streams(conn, &conn->stream, &dummy1, &dummy2);
        if (status) {
            goto error;
        }

        /* We have a different codepath when we can have async responses. */
        if (conn->async_responses) {
            /* TODO What about socket errors? */
            status = handle_async_response(conn, tmppool);
            if (APR_STATUS_IS_EAGAIN(status)) {
                status = APR_SUCCESS;
                goto error;
            }
            if (status) {
                goto error;
            }
            continue;
        }

        /* We are reading a response for a request we haven't
         * written yet!
         *
         * This shouldn't normally happen EXCEPT:
         *
         * 1) when the other end has closed the socket and we're
         *    pending an EOF return.
         * 2) Doing the initial SSL handshake - we'll get EAGAIN
         *    as the SSL buckets will hide the handshake from us
         *    but not return any data.
         * 3) When the server sends us an SSL alert.
         *
         * In these cases, we should not receive any actual user data.
         *
         * 4) When the server sends a error response, like 408 Request timeout.
         *    This response should be passed to the application.
         *
         * If we see an EOF (due to either an expired timeout or the server
         * sending the SSL 'close notify' shutdown alert), we'll reset the
         * connection and open a new one.
         */
        if (request->req_bkt || !request->writing_started) {
            const char *data;
            apr_size_t len;

            status = serf_bucket_peek(conn->stream, &data, &len);

            if (APR_STATUS_IS_EOF(status)) {
                reset_connection(conn, 1);
                status = APR_SUCCESS;
                goto error;
            }
            else if (APR_STATUS_IS_EAGAIN(status) && !len) {
                status = APR_SUCCESS;
                goto error;
            } else if (status && !APR_STATUS_IS_EAGAIN(status)) {
                /* Read error */
                goto error;
            }

            /* Unexpected response from the server */

        }

        /* If the request doesn't have a response bucket, then call the
         * acceptor to get one created.
         */
        if (request->resp_bkt == NULL) {
            request->resp_bkt = (*request->acceptor)(request, conn->stream,
                                                     request->acceptor_baton,
                                                     tmppool);
            apr_pool_clear(tmppool);
        }

        status = handle_response(request, tmppool);

        /* Some systems will not generate a HUP poll event so we have to
         * handle the ECONNRESET issue and ECONNABORT here.
         */
        if (APR_STATUS_IS_ECONNRESET(status) ||
            APR_STATUS_IS_ECONNABORTED(status) ||
            status == SERF_ERROR_REQUEST_LOST) {
            /* If the connection had ever been good, be optimistic & try again.
             * If it has never tried again (incl. a retry), fail.
             */
            if (conn->completed_responses) {
                reset_connection(conn, 1);
                status = APR_SUCCESS;
            }
            else if (status == SERF_ERROR_REQUEST_LOST) {
                status = SERF_ERROR_ABORTED_CONNECTION;
            }
            goto error;
        }

        /* If our response handler says it can't do anything more, we now
         * treat that as a success.
         */
        if (APR_STATUS_IS_EAGAIN(status)) {
            /* It is possible that while reading the response, the ssl layer
               has prepared some data to send. If this was the last request,
               serf will not check for socket writability, so force this here.
             */
            if (request_or_data_pending(&request, conn) && !request) {
                conn->dirty_conn = 1;
                conn->ctx->dirty_pollset = 1;
            }
            status = APR_SUCCESS;
            goto error;
        }

        /* If we received APR_SUCCESS, run this loop again. */
        if (!status) {
            continue;
        }

        close_connection = is_conn_closing(request->resp_bkt);

        if (!APR_STATUS_IS_EOF(status) &&
            close_connection != SERF_ERROR_CLOSING) {
            /* Whether success, or an error, there is no more to do unless
             * this request has been completed.
             */
            goto error;
        }

        /* The response has been fully-read, so that means the request has
         * either been fully-delivered (most likely), or that we don't need to
         * write the rest of it anymore, e.g. when a 408 Request timeout was
         $ received.
         * Remove it from our queue and loop to read another response.
         */
        conn->requests = request->next;

        destroy_request(request);

        request = conn->requests;

        /* If we're truly empty, update our tail. */
        if (request == NULL) {
            conn->requests_tail = NULL;
        }

        conn->completed_responses++;

        /* We've to rebuild pollset since completed_responses is changed. */
        conn->dirty_conn = 1;
        conn->ctx->dirty_pollset = 1;

        /* This means that we're being advised that the connection is done. */
        if (close_connection == SERF_ERROR_CLOSING) {
            reset_connection(conn, 1);
            if (APR_STATUS_IS_EOF(status))
                status = APR_SUCCESS;
            goto error;
        }

        /* The server is suddenly deciding to serve more responses than we've
         * seen before.
         *
         * Let our requests go.
         */
        if (conn->probable_keepalive_limit &&
            conn->completed_responses > conn->probable_keepalive_limit) {
            conn->probable_keepalive_limit = 0;
        }

        /* If we just ran out of requests or have unwritten requests, then
         * update the pollset. We don't want to read from this socket any
         * more. We are definitely done with this loop, too.
         */
        if (request == NULL || !request->writing_started) {
            conn->dirty_conn = 1;
            conn->ctx->dirty_pollset = 1;
            status = APR_SUCCESS;
            goto error;
        }
    }

error:
    apr_pool_destroy(tmppool);
    return status;
}

/* process all events on the connection */
apr_status_t serf__process_connection(serf_connection_t *conn,
                                      apr_int16_t events)
{
    apr_status_t status;

    /* POLLHUP/ERR should come after POLLIN so if there's an error message or
     * the like sitting on the connection, we give the app a chance to read
     * it before we trigger a reset condition.
     */
    if ((events & APR_POLLIN) != 0) {
        if ((status = read_from_connection(conn)) != APR_SUCCESS)
            return status;

        /* If we decided to reset our connection, return now as we don't
         * want to write.
         */
        if ((conn->seen_in_pollset & APR_POLLHUP) != 0) {
            return APR_SUCCESS;
        }
    }
    if ((events & APR_POLLHUP) != 0) {
        /* The connection got reset by the server. On Windows this can happen
           when all data is read, so just cleanup the connection and open
           a new one.
           If we haven't had any successful responses on this connection,
           then error out as it is likely a server issue. */
        if (conn->completed_responses) {
            return reset_connection(conn, 1);
        }
        return SERF_ERROR_ABORTED_CONNECTION;
    }
    if ((events & APR_POLLERR) != 0) {
        /* We might be talking to a buggy HTTP server that doesn't
         * do lingering-close.  (httpd < 2.1.8 does this.)
         *
         * See:
         *
         * http://issues.apache.org/bugzilla/show_bug.cgi?id=35292
         */
        if (conn->completed_requests && !conn->probable_keepalive_limit) {
            return reset_connection(conn, 1);
        }
#ifdef SO_ERROR
        /* If possible, get the error from the platform's socket layer and
           convert it to an APR status code. */
        {
            apr_os_sock_t osskt;
            if (!apr_os_sock_get(&osskt, conn->skt)) {
                int error;
                apr_socklen_t l = sizeof(error);

                if (!getsockopt(osskt, SOL_SOCKET, SO_ERROR, (char*)&error,
                                &l)) {
                    status = APR_FROM_OS_ERROR(error);

                    /* Handle fallback for multi-homed servers.
                     
                       ### Improve algorithm to find better than just 'next'?

                       Current Windows versions already handle re-ordering for
                       api users by using statistics on the recently failed
                       connections to order the list of addresses. */
                    if (conn->completed_requests == 0
                        && conn->address->next != NULL
                        && (APR_STATUS_IS_ECONNREFUSED(status)
                            || APR_STATUS_IS_TIMEUP(status)
                            || APR_STATUS_IS_ENETUNREACH(status))) {

                        conn->address = conn->address->next;
                        return reset_connection(conn, 1);
                    }

                    return status;
                  }
            }
        }
#endif
        return APR_EGENERAL;
    }
    if ((events & APR_POLLOUT) != 0) {
        if ((status = write_to_connection(conn)) != APR_SUCCESS)
            return status;
    }
    return APR_SUCCESS;
}

serf_connection_t *serf_connection_create(
    serf_context_t *ctx,
    apr_sockaddr_t *address,
    serf_connection_setup_t setup,
    void *setup_baton,
    serf_connection_closed_t closed,
    void *closed_baton,
    apr_pool_t *pool)
{
    serf_connection_t *conn = apr_pcalloc(pool, sizeof(*conn));

    conn->ctx = ctx;
    conn->status = APR_SUCCESS;
    /* Ignore server address if proxy was specified. */
    conn->address = ctx->proxy_address ? ctx->proxy_address : address;
    conn->setup = setup;
    conn->setup_baton = setup_baton;
    conn->closed = closed;
    conn->closed_baton = closed_baton;
    conn->pool = pool;
    conn->allocator = serf_bucket_allocator_create(pool, NULL, NULL);
    conn->stream = NULL;
    conn->ostream_head = NULL;
    conn->ostream_tail = NULL;
    conn->baton.type = SERF_IO_CONN;
    conn->baton.u.conn = conn;
    conn->hit_eof = 0;
    conn->state = SERF_CONN_INIT;
    conn->latency = -1; /* unknown */

    /* Create a subpool for our connection. */
    apr_pool_create(&conn->skt_pool, conn->pool);

    /* register a cleanup */
    apr_pool_cleanup_register(conn->pool, conn, clean_conn,
                              apr_pool_cleanup_null);

    /* Add the connection to the context. */
    *(serf_connection_t **)apr_array_push(ctx->conns) = conn;

    serf__log(CONN_VERBOSE, __FILE__, "created connection 0x%x\n",
              conn);

    return conn;
}

apr_status_t serf_connection_create2(
    serf_connection_t **conn,
    serf_context_t *ctx,
    apr_uri_t host_info,
    serf_connection_setup_t setup,
    void *setup_baton,
    serf_connection_closed_t closed,
    void *closed_baton,
    apr_pool_t *pool)
{
    apr_status_t status = APR_SUCCESS;
    serf_connection_t *c;
    apr_sockaddr_t *host_address = NULL;

    /* Set the port number explicitly, needed to create the socket later. */
    if (!host_info.port) {
        host_info.port = apr_uri_port_of_scheme(host_info.scheme);
    }

    /* Only lookup the address of the server if no proxy server was
       configured. */
    if (!ctx->proxy_address) {
        status = apr_sockaddr_info_get(&host_address,
                                       host_info.hostname,
                                       APR_UNSPEC, host_info.port, 0, pool);
        if (status)
            return status;
    }

    c = serf_connection_create(ctx, host_address, setup, setup_baton,
                               closed, closed_baton, pool);

    /* We're not interested in the path following the hostname. */
    c->host_url = apr_uri_unparse(c->pool,
                                  &host_info,
                                  APR_URI_UNP_OMITPATHINFO |
                                  APR_URI_UNP_OMITUSERINFO);

    /* Store the host info without the path on the connection. */
    (void)apr_uri_parse(c->pool, c->host_url, &(c->host_info));
    if (!c->host_info.port) {
        c->host_info.port = apr_uri_port_of_scheme(c->host_info.scheme);
    }

    *conn = c;

    return status;
}

apr_status_t serf_connection_reset(
    serf_connection_t *conn)
{
    return reset_connection(conn, 0);
}


apr_status_t serf_connection_close(
    serf_connection_t *conn)
{
    int i;
    serf_context_t *ctx = conn->ctx;
    apr_status_t status;

    for (i = ctx->conns->nelts; i--; ) {
        serf_connection_t *conn_seq = GET_CONN(ctx, i);

        if (conn_seq == conn) {
            while (conn->requests) {
                serf_request_cancel(conn->requests);
            }
            if (conn->skt != NULL) {
                remove_connection(ctx, conn);
                status = apr_socket_close(conn->skt);
                serf__log_skt(SOCK_VERBOSE, __FILE__, conn->skt,
                              "closed socket, status %d\n",
                              status);
                if (conn->closed != NULL) {
                    handle_conn_closed(conn, status);
                }
                conn->skt = NULL;
            }
            if (conn->stream != NULL) {
                serf_bucket_destroy(conn->stream);
                conn->stream = NULL;
            }

            destroy_ostream(conn);

            /* Remove the connection from the context. We don't want to
             * deal with it any more.
             */
            if (i < ctx->conns->nelts - 1) {
                /* move later connections over this one. */
                memmove(
                    &GET_CONN(ctx, i),
                    &GET_CONN(ctx, i + 1),
                    (ctx->conns->nelts - i - 1) * sizeof(serf_connection_t *));
            }
            --ctx->conns->nelts;

            serf__log(CONN_VERBOSE, __FILE__, "closed connection 0x%x\n",
                      conn);

            /* Found the connection. Closed it. All done. */
            return APR_SUCCESS;
        }
    }

    /* We didn't find the specified connection. */
    /* ### doc talks about this w.r.t poll structures. use something else? */
    return APR_NOTFOUND;
}


void serf_connection_set_max_outstanding_requests(
    serf_connection_t *conn,
    unsigned int max_requests)
{
    if (max_requests == 0)
        serf__log_skt(CONN_VERBOSE, __FILE__, conn->skt,
                      "Set max. nr. of outstanding requests for this "
                      "connection to unlimited.\n");
    else
        serf__log_skt(CONN_VERBOSE, __FILE__, conn->skt,
                      "Limit max. nr. of outstanding requests for this "
                      "connection to %u.\n", max_requests);

    conn->max_outstanding_requests = max_requests;
}


void serf_connection_set_async_responses(
    serf_connection_t *conn,
    serf_response_acceptor_t acceptor,
    void *acceptor_baton,
    serf_response_handler_t handler,
    void *handler_baton)
{
    conn->async_responses = 1;
    conn->async_acceptor = acceptor;
    conn->async_acceptor_baton = acceptor_baton;
    conn->async_handler = handler;
    conn->async_handler_baton = handler_baton;
}

static serf_request_t *
create_request(serf_connection_t *conn,
               serf_request_setup_t setup,
               void *setup_baton,
               int priority,
               int ssltunnel)
{
    serf_request_t *request;

    request = serf_bucket_mem_alloc(conn->allocator, sizeof(*request));
    request->conn = conn;
    request->setup = setup;
    request->setup_baton = setup_baton;
    request->handler = NULL;
    request->respool = NULL;
    request->req_bkt = NULL;
    request->resp_bkt = NULL;
    request->priority = priority;
    request->writing_started = 0;
    request->ssltunnel = ssltunnel;
    request->next = NULL;
    request->auth_baton = NULL;

    return request;
}

serf_request_t *serf_connection_request_create(
    serf_connection_t *conn,
    serf_request_setup_t setup,
    void *setup_baton)
{
    serf_request_t *request;

    request = create_request(conn, setup, setup_baton,
                             0, /* priority */
                             0  /* ssl tunnel */);

    /* Link the request to the end of the request chain. */
    link_requests(&conn->requests, &conn->requests_tail, request);
    
    /* Ensure our pollset becomes writable in context run */
    conn->ctx->dirty_pollset = 1;
    conn->dirty_conn = 1;

    return request;
}

static serf_request_t *
priority_request_create(serf_connection_t *conn,
                        int ssltunnelreq,
                        serf_request_setup_t setup,
                        void *setup_baton)
{
    serf_request_t *request;
    serf_request_t *iter, *prev;

    request = create_request(conn, setup, setup_baton,
                             1, /* priority */
                             ssltunnelreq);

    /* Link the new request after the last written request. */
    iter = conn->requests;
    prev = NULL;

    /* Find a request that has data which needs to be delivered. */
    while (iter != NULL && iter->req_bkt == NULL && iter->writing_started) {
        prev = iter;
        iter = iter->next;
    }

    /* A CONNECT request to setup an ssltunnel has absolute priority over all
       other requests on the connection, so:
       a. add it first to the queue 
       b. ensure that other priority requests are added after the CONNECT
          request */
    if (!request->ssltunnel) {
        /* Advance to next non priority request */
        while (iter != NULL && iter->priority) {
            prev = iter;
            iter = iter->next;
        }
    }

    if (prev) {
        request->next = iter;
        prev->next = request;
    } else {
        request->next = iter;
        conn->requests = request;
    }

    /* Ensure our pollset becomes writable in context run */
    conn->ctx->dirty_pollset = 1;
    conn->dirty_conn = 1;

    return request;
}

serf_request_t *serf_connection_priority_request_create(
    serf_connection_t *conn,
    serf_request_setup_t setup,
    void *setup_baton)
{
    return priority_request_create(conn,
                                   0, /* not a ssltunnel CONNECT request */
                                   setup, setup_baton);
}

serf_request_t *serf__ssltunnel_request_create(serf_connection_t *conn,
                                               serf_request_setup_t setup,
                                               void *setup_baton)
{
    return priority_request_create(conn,
                                   1, /* This is a ssltunnel CONNECT request */
                                   setup, setup_baton);
}

apr_status_t serf_request_cancel(serf_request_t *request)
{
    return cancel_request(request, &request->conn->requests, 0);
}

apr_status_t serf_request_is_written(serf_request_t *request)
{
    if (request->writing_started && !request->req_bkt)
        return APR_SUCCESS;

    return APR_EBUSY;
}

apr_pool_t *serf_request_get_pool(const serf_request_t *request)
{
    return request->respool;
}


serf_bucket_alloc_t *serf_request_get_alloc(
    const serf_request_t *request)
{
    return request->allocator;
}


serf_connection_t *serf_request_get_conn(
    const serf_request_t *request)
{
    return request->conn;
}


void serf_request_set_handler(
    serf_request_t *request,
    const serf_response_handler_t handler,
    const void **handler_baton)
{
    request->handler = handler;
    request->handler_baton = handler_baton;
}


serf_bucket_t *serf_request_bucket_request_create(
    serf_request_t *request,
    const char *method,
    const char *uri,
    serf_bucket_t *body,
    serf_bucket_alloc_t *allocator)
{
    serf_bucket_t *req_bkt, *hdrs_bkt;
    serf_connection_t *conn = request->conn;
    serf_context_t *ctx = conn->ctx;
    int ssltunnel;

    ssltunnel = ctx->proxy_address &&
                (strcmp(conn->host_info.scheme, "https") == 0);

    req_bkt = serf_bucket_request_create(method, uri, body, allocator);
    hdrs_bkt = serf_bucket_request_get_headers(req_bkt);

    /* Use absolute uri's in requests to a proxy. USe relative uri's in
       requests directly to a server or sent through an SSL tunnel. */
    if (ctx->proxy_address && conn->host_url &&
        !(ssltunnel && !request->ssltunnel)) {

        serf_bucket_request_set_root(req_bkt, conn->host_url);
    }

    if (conn->host_info.hostinfo)
        serf_bucket_headers_setn(hdrs_bkt, "Host",
                                 conn->host_info.hostinfo);

    /* Setup server authorization headers, unless this is a CONNECT request. */
    if (!request->ssltunnel) {
        serf__authn_info_t *authn_info;
        authn_info = serf__get_authn_info_for_server(conn);
        if (authn_info->scheme)
            authn_info->scheme->setup_request_func(HOST, 0, conn, request,
                                                   method, uri,
                                                   hdrs_bkt);
    }

    /* Setup proxy authorization headers.
       Don't set these headers on the requests to the server if we're using
       an SSL tunnel, only on the CONNECT request to setup the tunnel. */
    if (ctx->proxy_authn_info.scheme) {
        if (strcmp(conn->host_info.scheme, "https") == 0) {
            if (request->ssltunnel)
                ctx->proxy_authn_info.scheme->setup_request_func(PROXY, 0, conn,
                                                                 request,
                                                                 method, uri,
                                                                 hdrs_bkt);
        } else {
            ctx->proxy_authn_info.scheme->setup_request_func(PROXY, 0, conn,
                                                             request,
                                                             method, uri,
                                                             hdrs_bkt);
        }
    }

    return req_bkt;
}

apr_interval_time_t serf_connection_get_latency(serf_connection_t *conn)
{
    if (conn->ctx->proxy_address) {
        /* Detecting network latency for proxied connection is not implemented
           yet. */
        return -1;
    }

    return conn->latency;
}
