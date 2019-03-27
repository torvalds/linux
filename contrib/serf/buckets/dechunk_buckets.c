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

#include <apr_strings.h>

#include "serf.h"
#include "serf_bucket_util.h"

typedef struct {
    serf_bucket_t *stream;

    enum {
        STATE_SIZE,     /* reading the chunk size */
        STATE_CHUNK,    /* reading the chunk */
        STATE_TERM,     /* reading the chunk terminator */
        STATE_DONE      /* body is done; we've returned EOF */
    } state;

    /* Buffer for accumulating a chunk size. */
    serf_linebuf_t linebuf;

    /* How much of the chunk, or the terminator, do we have left to read? */
    apr_int64_t body_left;

} dechunk_context_t;


serf_bucket_t *serf_bucket_dechunk_create(
    serf_bucket_t *stream,
    serf_bucket_alloc_t *allocator)
{
    dechunk_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;
    ctx->state = STATE_SIZE;

    serf_linebuf_init(&ctx->linebuf);

    return serf_bucket_create(&serf_bucket_type_dechunk, allocator, ctx);
}

static void serf_dechunk_destroy_and_data(serf_bucket_t *bucket)
{
    dechunk_context_t *ctx = bucket->data;

    serf_bucket_destroy(ctx->stream);

    serf_default_destroy_and_data(bucket);
}

static apr_status_t serf_dechunk_read(serf_bucket_t *bucket,
                                      apr_size_t requested,
                                      const char **data, apr_size_t *len)
{
    dechunk_context_t *ctx = bucket->data;
    apr_status_t status;

    while (1) {
        switch (ctx->state) {
        case STATE_SIZE:

            /* fetch a line terminated by CRLF */
            status = serf_linebuf_fetch(&ctx->linebuf, ctx->stream,
                                        SERF_NEWLINE_CRLF);
            if (SERF_BUCKET_READ_ERROR(status))
                return status;

            /* if a line was read, then parse it. */
            if (ctx->linebuf.state == SERF_LINEBUF_READY) {
                /* NUL-terminate the line. if it filled the entire buffer,
                   then just assume the thing is too large. */
                if (ctx->linebuf.used == sizeof(ctx->linebuf.line))
                    return APR_FROM_OS_ERROR(ERANGE);
                ctx->linebuf.line[ctx->linebuf.used] = '\0';

                /* convert from HEX digits. */
                ctx->body_left = apr_strtoi64(ctx->linebuf.line, NULL, 16);
                if (errno == ERANGE) {
                    return APR_FROM_OS_ERROR(ERANGE);
                }

                if (ctx->body_left == 0) {
                    /* Just read the last-chunk marker. We're DONE. */
                    ctx->state = STATE_DONE;
                    status = APR_EOF;
                }
                else {
                    /* Got a size, so we'll start reading the chunk now. */
                    ctx->state = STATE_CHUNK;
                }

                /* If we can read more, then go do so. */
                if (!status)
                    continue;
            }
            /* assert: status != 0 */

            /* Note that we didn't actually read anything, so our callers
             * don't get confused.
             */
            *len = 0;

            return status;

        case STATE_CHUNK:

            if (requested > ctx->body_left) {
                requested = ctx->body_left;
            }

            /* Delegate to the stream bucket to do the read. */
            status = serf_bucket_read(ctx->stream, requested, data, len);
            if (SERF_BUCKET_READ_ERROR(status))
                return status;

            /* Some data was read, so decrement the amount left and see
             * if we're done reading this chunk.
             */
            ctx->body_left -= *len;
            if (!ctx->body_left) {
                ctx->state = STATE_TERM;
                ctx->body_left = 2;     /* CRLF */
            }

            /* We need more data but there is no more available. */
            if (ctx->body_left && APR_STATUS_IS_EOF(status)) {
                return SERF_ERROR_TRUNCATED_HTTP_RESPONSE;
            }

            /* Return the data we just read. */
            return status;

        case STATE_TERM:
            /* Delegate to the stream bucket to do the read. */
            status = serf_bucket_read(ctx->stream, ctx->body_left, data, len);
            if (SERF_BUCKET_READ_ERROR(status))
                return status;

            /* Some data was read, so decrement the amount left and see
             * if we're done reading the chunk terminator.
             */
            ctx->body_left -= *len;

            /* We need more data but there is no more available. */
            if (ctx->body_left && APR_STATUS_IS_EOF(status))
                return SERF_ERROR_TRUNCATED_HTTP_RESPONSE;

            if (!ctx->body_left) {
                ctx->state = STATE_SIZE;
            }

            /* Don't return the CR of CRLF to the caller! */
            *len = 0;

            if (status)
                return status;

            break;

        case STATE_DONE:
            /* Just keep returning EOF */
            *len = 0;
            return APR_EOF;

        default:
            /* Not reachable */
            return APR_EGENERAL;
        }
    }
    /* NOTREACHED */
}

/* ### need to implement */
#define serf_dechunk_readline NULL
#define serf_dechunk_peek NULL

const serf_bucket_type_t serf_bucket_type_dechunk = {
    "DECHUNK",
    serf_dechunk_read,
    serf_dechunk_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_dechunk_peek,
    serf_dechunk_destroy_and_data,
};
