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
#include <apr_strings.h>

#include "serf.h"
#include "serf_bucket_util.h"


typedef struct {
    enum {
        STATE_FETCH,
        STATE_CHUNK,
        STATE_EOF
    } state;

    apr_status_t last_status;

    serf_bucket_t *chunk;
    serf_bucket_t *stream;

    char chunk_hdr[20];
} chunk_context_t;


serf_bucket_t *serf_bucket_chunk_create(
    serf_bucket_t *stream, serf_bucket_alloc_t *allocator)
{
    chunk_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->state = STATE_FETCH;
    ctx->chunk = serf_bucket_aggregate_create(allocator);
    ctx->stream = stream;

    return serf_bucket_create(&serf_bucket_type_chunk, allocator, ctx);
}

#define CRLF "\r\n"

static apr_status_t create_chunk(serf_bucket_t *bucket)
{
    chunk_context_t *ctx = bucket->data;
    serf_bucket_t *simple_bkt;
    apr_size_t chunk_len;
    apr_size_t stream_len;
    struct iovec vecs[66]; /* 64 + chunk trailer + EOF trailer = 66 */
    int vecs_read;
    int i;

    if (ctx->state != STATE_FETCH) {
        return APR_SUCCESS;
    }

    ctx->last_status =
        serf_bucket_read_iovec(ctx->stream, SERF_READ_ALL_AVAIL,
                               64, vecs, &vecs_read);

    if (SERF_BUCKET_READ_ERROR(ctx->last_status)) {
        /* Uh-oh. */
        return ctx->last_status;
    }

    /* Count the length of the data we read. */
    stream_len = 0;
    for (i = 0; i < vecs_read; i++) {
        stream_len += vecs[i].iov_len;
    }

    /* assert: stream_len in hex < sizeof(ctx->chunk_hdr) */

    /* Inserting a 0 byte chunk indicates a terminator, which already happens
     * during the EOF handler below.  Adding another one here will cause the
     * EOF chunk to be interpreted by the server as a new request.  So,
     * we'll only do this if we have something to write.
     */
    if (stream_len) {
        /* Build the chunk header. */
        chunk_len = apr_snprintf(ctx->chunk_hdr, sizeof(ctx->chunk_hdr),
                                 "%" APR_UINT64_T_HEX_FMT CRLF,
                                 (apr_uint64_t)stream_len);

        /* Create a copy of the chunk header so we can have multiple chunks
         * in the pipeline at the same time.
         */
        simple_bkt = serf_bucket_simple_copy_create(ctx->chunk_hdr, chunk_len,
                                                    bucket->allocator);
        serf_bucket_aggregate_append(ctx->chunk, simple_bkt);

        /* Insert the chunk footer. */
        vecs[vecs_read].iov_base = CRLF;
        vecs[vecs_read++].iov_len = sizeof(CRLF) - 1;
    }

    /* We've reached the end of the line for the stream. */
    if (APR_STATUS_IS_EOF(ctx->last_status)) {
        /* Insert the chunk footer. */
        vecs[vecs_read].iov_base = "0" CRLF CRLF;
        vecs[vecs_read++].iov_len = sizeof("0" CRLF CRLF) - 1;

        ctx->state = STATE_EOF;
    }
    else {
        /* Okay, we can return data.  */
        ctx->state = STATE_CHUNK;
    }

    serf_bucket_aggregate_append_iovec(ctx->chunk, vecs, vecs_read);

    return APR_SUCCESS;
}

static apr_status_t serf_chunk_read(serf_bucket_t *bucket,
                                    apr_size_t requested,
                                    const char **data, apr_size_t *len)
{
    chunk_context_t *ctx = bucket->data;
    apr_status_t status;

    /* Before proceeding, we need to fetch some data from the stream. */
    if (ctx->state == STATE_FETCH) {
        status = create_chunk(bucket);
        if (status) {
            return status;
        }
    }

    status = serf_bucket_read(ctx->chunk, requested, data, len);

    /* Mask EOF from aggregate bucket. */
    if (APR_STATUS_IS_EOF(status) && ctx->state == STATE_CHUNK) {
        status = ctx->last_status;
        ctx->state = STATE_FETCH;
    }

    return status;
}

static apr_status_t serf_chunk_readline(serf_bucket_t *bucket,
                                         int acceptable, int *found,
                                         const char **data, apr_size_t *len)
{
    chunk_context_t *ctx = bucket->data;
    apr_status_t status;

    status = serf_bucket_readline(ctx->chunk, acceptable, found, data, len);

    /* Mask EOF from aggregate bucket. */
    if (APR_STATUS_IS_EOF(status) && ctx->state == STATE_CHUNK) {
        status = APR_EAGAIN;
        ctx->state = STATE_FETCH;
    }

    return status;
}

static apr_status_t serf_chunk_read_iovec(serf_bucket_t *bucket,
                                          apr_size_t requested,
                                          int vecs_size,
                                          struct iovec *vecs,
                                          int *vecs_used)
{
    chunk_context_t *ctx = bucket->data;
    apr_status_t status;

    /* Before proceeding, we need to fetch some data from the stream. */
    if (ctx->state == STATE_FETCH) {
        status = create_chunk(bucket);
        if (status) {
            return status;
        }
    }

    status = serf_bucket_read_iovec(ctx->chunk, requested, vecs_size, vecs,
                                    vecs_used);

    /* Mask EOF from aggregate bucket. */
    if (APR_STATUS_IS_EOF(status) && ctx->state == STATE_CHUNK) {
        status = ctx->last_status;
        ctx->state = STATE_FETCH;
    }

    return status;
}

static apr_status_t serf_chunk_peek(serf_bucket_t *bucket,
                                     const char **data,
                                     apr_size_t *len)
{
    chunk_context_t *ctx = bucket->data;
    apr_status_t status;

    status = serf_bucket_peek(ctx->chunk, data, len);

    /* Mask EOF from aggregate bucket. */
    if (APR_STATUS_IS_EOF(status) && ctx->state == STATE_CHUNK) {
        status = APR_EAGAIN;
    }

    return status;
}

static void serf_chunk_destroy(serf_bucket_t *bucket)
{
    chunk_context_t *ctx = bucket->data;

    serf_bucket_destroy(ctx->stream);
    serf_bucket_destroy(ctx->chunk);

    serf_default_destroy_and_data(bucket);
}

const serf_bucket_type_t serf_bucket_type_chunk = {
    "CHUNK",
    serf_chunk_read,
    serf_chunk_readline,
    serf_chunk_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_chunk_peek,
    serf_chunk_destroy,
};
