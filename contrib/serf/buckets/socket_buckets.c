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
#include <apr_network_io.h>

#include "serf.h"
#include "serf_private.h"
#include "serf_bucket_util.h"


typedef struct {
    apr_socket_t *skt;

    serf_databuf_t databuf;

    /* Progress callback */
    serf_progress_t progress_func;
    void *progress_baton;
} socket_context_t;


static apr_status_t socket_reader(void *baton, apr_size_t bufsize,
                                  char *buf, apr_size_t *len)
{
    socket_context_t *ctx = baton;
    apr_status_t status;

    *len = bufsize;
    status = apr_socket_recv(ctx->skt, buf, len);

    if (status && !APR_STATUS_IS_EAGAIN(status))
        serf__log_skt(SOCK_VERBOSE, __FILE__, ctx->skt,
                      "socket_recv error %d\n", status);

    if (*len)
        serf__log_skt(SOCK_MSG_VERBOSE, __FILE__, ctx->skt,
                      "--- socket_recv:\n%.*s\n-(%d)-\n",
                      *len, buf, *len);

    if (ctx->progress_func && *len)
        ctx->progress_func(ctx->progress_baton, *len, 0);

    return status;
}

serf_bucket_t *serf_bucket_socket_create(
    apr_socket_t *skt,
    serf_bucket_alloc_t *allocator)
{
    socket_context_t *ctx;

    /* Oh, well. */
    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->skt = skt;

    serf_databuf_init(&ctx->databuf);
    ctx->databuf.read = socket_reader;
    ctx->databuf.read_baton = ctx;

    ctx->progress_func = NULL;
    ctx->progress_baton = NULL;
    return serf_bucket_create(&serf_bucket_type_socket, allocator, ctx);
}

void serf_bucket_socket_set_read_progress_cb(
    serf_bucket_t *bucket,
    const serf_progress_t progress_func,
    void *progress_baton)
{
    socket_context_t *ctx = bucket->data;

    ctx->progress_func = progress_func;
    ctx->progress_baton = progress_baton;
}

static apr_status_t serf_socket_read(serf_bucket_t *bucket,
                                     apr_size_t requested,
                                     const char **data, apr_size_t *len)
{
    socket_context_t *ctx = bucket->data;

    return serf_databuf_read(&ctx->databuf, requested, data, len);
}

static apr_status_t serf_socket_readline(serf_bucket_t *bucket,
                                         int acceptable, int *found,
                                         const char **data, apr_size_t *len)
{
    socket_context_t *ctx = bucket->data;

    return serf_databuf_readline(&ctx->databuf, acceptable, found, data, len);
}

static apr_status_t serf_socket_peek(serf_bucket_t *bucket,
                                     const char **data,
                                     apr_size_t *len)
{
    socket_context_t *ctx = bucket->data;

    return serf_databuf_peek(&ctx->databuf, data, len);
}

const serf_bucket_type_t serf_bucket_type_socket = {
    "SOCKET",
    serf_socket_read,
    serf_socket_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_socket_peek,
    serf_default_destroy_and_data,
};
