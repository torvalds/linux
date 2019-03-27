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

#include "serf.h"
#include "serf_bucket_util.h"

/* Older versions of APR do not have this macro.  */
#ifdef APR_SIZE_MAX
#define REQUESTED_MAX APR_SIZE_MAX
#else
#define REQUESTED_MAX (~((apr_size_t)0))
#endif


typedef struct {
    serf_bucket_t *stream;
    apr_uint64_t remaining;
} body_context_t;

serf_bucket_t *serf_bucket_response_body_create(
    serf_bucket_t *stream, apr_uint64_t len, serf_bucket_alloc_t *allocator)
{
    body_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;
    ctx->remaining = len;

    return serf_bucket_create(&serf_bucket_type_response_body, allocator, ctx);
}

static apr_status_t serf_response_body_read(serf_bucket_t *bucket,
                                            apr_size_t requested,
                                            const char **data,
                                            apr_size_t *len)
{
    body_context_t *ctx = bucket->data;
    apr_status_t status;

    if (!ctx->remaining) {
        *len = 0;
        return APR_EOF;
    }

    if (requested == SERF_READ_ALL_AVAIL || requested > ctx->remaining) {
        if (ctx->remaining <= REQUESTED_MAX) {
            requested = (apr_size_t) ctx->remaining;
        } else {
            requested = REQUESTED_MAX;
        }
    }

    status = serf_bucket_read(ctx->stream, requested, data, len);

    if (!SERF_BUCKET_READ_ERROR(status)) {
        ctx->remaining -= *len;
    }

    if (APR_STATUS_IS_EOF(status) && ctx->remaining > 0) {
        /* The server sent less data than expected. */
        status = SERF_ERROR_TRUNCATED_HTTP_RESPONSE;
    }

    return status;
}

static apr_status_t serf_response_body_readline(serf_bucket_t *bucket,
                                                int acceptable, int *found,
                                                const char **data,
                                                apr_size_t *len)
{
    body_context_t *ctx = bucket->data;
    apr_status_t status;

    if (!ctx->remaining) {
        *len = 0;
        return APR_EOF;
    }

    status = serf_bucket_readline(ctx->stream, acceptable, found, data, len);

    if (!SERF_BUCKET_READ_ERROR(status)) {
        ctx->remaining -= *len;
    }

    if (APR_STATUS_IS_EOF(status) && ctx->remaining > 0) {
        /* The server sent less data than expected. */
        status = SERF_ERROR_TRUNCATED_HTTP_RESPONSE;
    }

    return status;
}

static apr_status_t serf_response_body_peek(serf_bucket_t *bucket,
                                            const char **data,
                                            apr_size_t *len)
{
    body_context_t *ctx = bucket->data;

    return serf_bucket_peek(ctx->stream, data, len);
}

static void serf_response_body_destroy(serf_bucket_t *bucket)
{
    body_context_t *ctx = bucket->data;

    serf_bucket_destroy(ctx->stream);

    serf_default_destroy_and_data(bucket);
}

const serf_bucket_type_t serf_bucket_type_response_body = {
    "RESPONSE_BODY",
    serf_response_body_read,
    serf_response_body_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_response_body_peek,
    serf_response_body_destroy,
};
