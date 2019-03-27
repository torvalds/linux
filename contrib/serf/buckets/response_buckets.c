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

#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_date.h>

#include "serf.h"
#include "serf_bucket_util.h"
#include "serf_private.h"

typedef struct {
    serf_bucket_t *stream;
    serf_bucket_t *body;        /* Pointer to the stream wrapping the body. */
    serf_bucket_t *headers;     /* holds parsed headers */

    enum {
        STATE_STATUS_LINE,      /* reading status line */
        STATE_HEADERS,          /* reading headers */
        STATE_BODY,             /* reading body */
        STATE_TRAILERS,         /* reading trailers */
        STATE_DONE              /* we've sent EOF */
    } state;

    /* Buffer for accumulating a line from the response. */
    serf_linebuf_t linebuf;

    serf_status_line sl;

    int chunked;                /* Do we need to read trailers? */
    int head_req;               /* Was this a HEAD request? */
} response_context_t;

/* Returns 1 if according to RFC2626 this response can have a body, 0 if it
   must not have a body. */
static int expect_body(response_context_t *ctx)
{
    if (ctx->head_req)
        return 0;

    /* 100 Continue and 101 Switching Protocols */
    if (ctx->sl.code >= 100 && ctx->sl.code < 200)
        return 0;

    /* 204 No Content */
    if (ctx->sl.code == 204)
        return 0;

    /* 205? */

    /* 304 Not Modified */
    if (ctx->sl.code == 304)
        return 0;

    return 1;
}

serf_bucket_t *serf_bucket_response_create(
    serf_bucket_t *stream,
    serf_bucket_alloc_t *allocator)
{
    response_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;
    ctx->body = NULL;
    ctx->headers = serf_bucket_headers_create(allocator);
    ctx->state = STATE_STATUS_LINE;
    ctx->chunked = 0;
    ctx->head_req = 0;

    serf_linebuf_init(&ctx->linebuf);

    return serf_bucket_create(&serf_bucket_type_response, allocator, ctx);
}

void serf_bucket_response_set_head(
    serf_bucket_t *bucket)
{
    response_context_t *ctx = bucket->data;

    ctx->head_req = 1;
}

serf_bucket_t *serf_bucket_response_get_headers(
    serf_bucket_t *bucket)
{
    return ((response_context_t *)bucket->data)->headers;
}


static void serf_response_destroy_and_data(serf_bucket_t *bucket)
{
    response_context_t *ctx = bucket->data;

    if (ctx->state != STATE_STATUS_LINE) {
        serf_bucket_mem_free(bucket->allocator, (void*)ctx->sl.reason);
    }

    serf_bucket_destroy(ctx->stream);
    if (ctx->body != NULL)
        serf_bucket_destroy(ctx->body);
    serf_bucket_destroy(ctx->headers);

    serf_default_destroy_and_data(bucket);
}

static apr_status_t fetch_line(response_context_t *ctx, int acceptable)
{
    return serf_linebuf_fetch(&ctx->linebuf, ctx->stream, acceptable);
}

static apr_status_t parse_status_line(response_context_t *ctx,
                                      serf_bucket_alloc_t *allocator)
{
    int res;
    char *reason; /* ### stupid APR interface makes this non-const */

    /* Ensure a valid length, to avoid overflow on the final '\0' */
    if (ctx->linebuf.used >= SERF_LINEBUF_LIMIT) {
       return SERF_ERROR_BAD_HTTP_RESPONSE;
    }

    /* apr_date_checkmask assumes its arguments are valid C strings */
    ctx->linebuf.line[ctx->linebuf.used] = '\0';

    /* ctx->linebuf.line should be of form: 'HTTP/1.1 200 OK',
       but we also explicitly allow the forms 'HTTP/1.1 200' (no reason)
       and 'HTTP/1.1 401.1 Logon failed' (iis extended error codes) */
    res = apr_date_checkmask(ctx->linebuf.line, "HTTP/#.# ###*");
    if (!res) {
        /* Not an HTTP response?  Well, at least we won't understand it. */
        return SERF_ERROR_BAD_HTTP_RESPONSE;
    }

    ctx->sl.version = SERF_HTTP_VERSION(ctx->linebuf.line[5] - '0',
                                        ctx->linebuf.line[7] - '0');
    ctx->sl.code = apr_strtoi64(ctx->linebuf.line + 8, &reason, 10);

    /* Skip leading spaces for the reason string. */
    if (apr_isspace(*reason)) {
        reason++;
    }

    /* Copy the reason value out of the line buffer. */
    ctx->sl.reason = serf_bstrmemdup(allocator, reason,
                                     ctx->linebuf.used
                                     - (reason - ctx->linebuf.line));

    return APR_SUCCESS;
}

/* This code should be replaced with header buckets. */
static apr_status_t fetch_headers(serf_bucket_t *bkt, response_context_t *ctx)
{
    apr_status_t status;

    /* RFC 2616 says that CRLF is the only line ending, but we can easily
     * accept any kind of line ending.
     */
    status = fetch_line(ctx, SERF_NEWLINE_ANY);
    if (SERF_BUCKET_READ_ERROR(status)) {
        return status;
    }
    /* Something was read. Process it. */

    if (ctx->linebuf.state == SERF_LINEBUF_READY && ctx->linebuf.used) {
        const char *end_key;
        const char *c;

        end_key = c = memchr(ctx->linebuf.line, ':', ctx->linebuf.used);
        if (!c) {
            /* Bad headers? */
            return SERF_ERROR_BAD_HTTP_RESPONSE;
        }

        /* Skip over initial ':' */
        c++;

        /* And skip all whitespaces. */
        for(; c < ctx->linebuf.line + ctx->linebuf.used; c++)
        {
            if (!apr_isspace(*c))
            {
              break;
            }
        }

        /* Always copy the headers (from the linebuf into new mem). */
        /* ### we should be able to optimize some mem copies */
        serf_bucket_headers_setx(
            ctx->headers,
            ctx->linebuf.line, end_key - ctx->linebuf.line, 1,
            c, ctx->linebuf.line + ctx->linebuf.used - c, 1);
    }

    return status;
}

/* Perform one iteration of the state machine.
 *
 * Will return when one the following conditions occurred:
 *  1) a state change
 *  2) an error
 *  3) the stream is not ready or at EOF
 *  4) APR_SUCCESS, meaning the machine can be run again immediately
 */
static apr_status_t run_machine(serf_bucket_t *bkt, response_context_t *ctx)
{
    apr_status_t status = APR_SUCCESS; /* initialize to avoid gcc warnings */

    switch (ctx->state) {
    case STATE_STATUS_LINE:
        /* RFC 2616 says that CRLF is the only line ending, but we can easily
         * accept any kind of line ending.
         */
        status = fetch_line(ctx, SERF_NEWLINE_ANY);
        if (SERF_BUCKET_READ_ERROR(status))
            return status;

        if (ctx->linebuf.state == SERF_LINEBUF_READY) {
            /* The Status-Line is in the line buffer. Process it. */
            status = parse_status_line(ctx, bkt->allocator);
            if (status)
                return status;

            /* Good times ahead: we're switching protocols! */
            if (ctx->sl.code == 101) {
                ctx->body =
                    serf_bucket_barrier_create(ctx->stream, bkt->allocator);
                ctx->state = STATE_DONE;
                break;
            }

            /* Okay... move on to reading the headers. */
            ctx->state = STATE_HEADERS;
        }
        else {
            /* The connection closed before we could get the next
             * response.  Treat the request as lost so that our upper
             * end knows the server never tried to give us a response.
             */
            if (APR_STATUS_IS_EOF(status)) {
                return SERF_ERROR_REQUEST_LOST;
            }
        }
        break;
    case STATE_HEADERS:
        status = fetch_headers(bkt, ctx);
        if (SERF_BUCKET_READ_ERROR(status))
            return status;

        /* If an empty line was read, then we hit the end of the headers.
         * Move on to the body.
         */
        if (ctx->linebuf.state == SERF_LINEBUF_READY && !ctx->linebuf.used) {
            const void *v;

            /* Advance the state. */
            ctx->state = STATE_BODY;

            /* If this is a response to a HEAD request, or code == 1xx,204 or304
               then we don't receive a real body. */
            if (!expect_body(ctx)) {
                ctx->body = serf_bucket_simple_create(NULL, 0, NULL, NULL,
                                                      bkt->allocator);
                ctx->state = STATE_BODY;
                break;
            }

            ctx->body =
                serf_bucket_barrier_create(ctx->stream, bkt->allocator);

            /* Are we C-L, chunked, or conn close? */
            v = serf_bucket_headers_get(ctx->headers, "Content-Length");
            if (v) {
                apr_uint64_t length;
                length = apr_strtoi64(v, NULL, 10);
                if (errno == ERANGE) {
                    return APR_FROM_OS_ERROR(ERANGE);
                }
                ctx->body = serf_bucket_response_body_create(
                              ctx->body, length, bkt->allocator);
            }
            else {
                v = serf_bucket_headers_get(ctx->headers, "Transfer-Encoding");

                /* Need to handle multiple transfer-encoding. */
                if (v && strcasecmp("chunked", v) == 0) {
                    ctx->chunked = 1;
                    ctx->body = serf_bucket_dechunk_create(ctx->body,
                                                           bkt->allocator);
                }
            }
            v = serf_bucket_headers_get(ctx->headers, "Content-Encoding");
            if (v) {
                /* Need to handle multiple content-encoding. */
                if (v && strcasecmp("gzip", v) == 0) {
                    ctx->body =
                        serf_bucket_deflate_create(ctx->body, bkt->allocator,
                                                   SERF_DEFLATE_GZIP);
                }
                else if (v && strcasecmp("deflate", v) == 0) {
                    ctx->body =
                        serf_bucket_deflate_create(ctx->body, bkt->allocator,
                                                   SERF_DEFLATE_DEFLATE);
                }
            }
        }
        break;
    case STATE_BODY:
        /* Don't do anything. */
        break;
    case STATE_TRAILERS:
        status = fetch_headers(bkt, ctx);
        if (SERF_BUCKET_READ_ERROR(status))
            return status;

        /* If an empty line was read, then we're done. */
        if (ctx->linebuf.state == SERF_LINEBUF_READY && !ctx->linebuf.used) {
            ctx->state = STATE_DONE;
            return APR_EOF;
        }
        break;
    case STATE_DONE:
        return APR_EOF;
    default:
        /* Not reachable */
        return APR_EGENERAL;
    }

    return status;
}

static apr_status_t wait_for_body(serf_bucket_t *bkt, response_context_t *ctx)
{
    apr_status_t status;

    /* Keep reading and moving through states if we aren't at the BODY */
    while (ctx->state != STATE_BODY) {
        status = run_machine(bkt, ctx);

        /* Anything other than APR_SUCCESS means that we cannot immediately
         * read again (for now).
         */
        if (status)
            return status;
    }
    /* in STATE_BODY */

    return APR_SUCCESS;
}

apr_status_t serf_bucket_response_wait_for_headers(
    serf_bucket_t *bucket)
{
    response_context_t *ctx = bucket->data;

    return wait_for_body(bucket, ctx);
}

apr_status_t serf_bucket_response_status(
    serf_bucket_t *bkt,
    serf_status_line *sline)
{
    response_context_t *ctx = bkt->data;
    apr_status_t status;

    if (ctx->state != STATE_STATUS_LINE) {
        /* We already read it and moved on. Just return it. */
        *sline = ctx->sl;
        return APR_SUCCESS;
    }

    /* Running the state machine once will advance the machine, or state
     * that the stream isn't ready with enough data. There isn't ever a
     * need to run the machine more than once to try and satisfy this. We
     * have to look at the state to tell whether it advanced, though, as
     * it is quite possible to advance *and* to return APR_EAGAIN.
     */
    status = run_machine(bkt, ctx);
    if (ctx->state == STATE_HEADERS) {
        *sline = ctx->sl;
    }
    else {
        /* Indicate that we don't have the information yet. */
        sline->version = 0;
    }

    return status;
}

static apr_status_t serf_response_read(serf_bucket_t *bucket,
                                       apr_size_t requested,
                                       const char **data, apr_size_t *len)
{
    response_context_t *ctx = bucket->data;
    apr_status_t rv;

    rv = wait_for_body(bucket, ctx);
    if (rv) {
        /* It's not possible to have read anything yet! */
        if (APR_STATUS_IS_EOF(rv) || APR_STATUS_IS_EAGAIN(rv)) {
            *len = 0;
        }
        return rv;
    }

    rv = serf_bucket_read(ctx->body, requested, data, len);
    if (SERF_BUCKET_READ_ERROR(rv))
        return rv;

    if (APR_STATUS_IS_EOF(rv)) {
        if (ctx->chunked) {
            ctx->state = STATE_TRAILERS;
            /* Mask the result. */
            rv = APR_SUCCESS;
        } else {
            ctx->state = STATE_DONE;
        }
    }
    return rv;
}

static apr_status_t serf_response_readline(serf_bucket_t *bucket,
                                           int acceptable, int *found,
                                           const char **data, apr_size_t *len)
{
    response_context_t *ctx = bucket->data;
    apr_status_t rv;

    rv = wait_for_body(bucket, ctx);
    if (rv) {
        return rv;
    }

    /* Delegate to the stream bucket to do the readline. */
    return serf_bucket_readline(ctx->body, acceptable, found, data, len);
}

apr_status_t serf_response_full_become_aggregate(serf_bucket_t *bucket)
{
    response_context_t *ctx = bucket->data;
    serf_bucket_t *bkt;
    char buf[256];
    int size;

    serf_bucket_aggregate_become(bucket);

    /* Add reconstructed status line. */
    size = apr_snprintf(buf, 256, "HTTP/%d.%d %d ",
                        SERF_HTTP_VERSION_MAJOR(ctx->sl.version),
                        SERF_HTTP_VERSION_MINOR(ctx->sl.version),
                        ctx->sl.code);
    bkt = serf_bucket_simple_copy_create(buf, size,
                                         bucket->allocator);
    serf_bucket_aggregate_append(bucket, bkt);
    bkt = serf_bucket_simple_copy_create(ctx->sl.reason, strlen(ctx->sl.reason),
                                         bucket->allocator);
    serf_bucket_aggregate_append(bucket, bkt);
    bkt = SERF_BUCKET_SIMPLE_STRING_LEN("\r\n", 2,
                                        bucket->allocator);
    serf_bucket_aggregate_append(bucket, bkt);

    /* Add headers and stream buckets in order. */
    serf_bucket_aggregate_append(bucket, ctx->headers);
    serf_bucket_aggregate_append(bucket, ctx->stream);

    serf_bucket_mem_free(bucket->allocator, ctx);

    return APR_SUCCESS;
}

/* ### need to implement */
#define serf_response_peek NULL

const serf_bucket_type_t serf_bucket_type_response = {
    "RESPONSE",
    serf_response_read,
    serf_response_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_response_peek,
    serf_response_destroy_and_data,
};
