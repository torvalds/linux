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
#include <apr_lib.h>
#include <apr_date.h>

#include "serf.h"
#include "serf_bucket_util.h"
#include "serf_bucket_types.h"

#include <stdlib.h>

/* This is an implementation of Bidirectional Web Transfer Protocol (BWTP)
 * See:
 *   http://bwtp.wikidot.com/
 */

typedef struct {
    int channel;
    int open;
    int type; /* 0 = header, 1 = message */ /* TODO enum? */
    const char *phrase;
    serf_bucket_t *headers;

    char req_line[1000];
} frame_context_t;

typedef struct {
    serf_bucket_t *stream;
    serf_bucket_t *body;        /* Pointer to the stream wrapping the body. */
    serf_bucket_t *headers;     /* holds parsed headers */

    enum {
        STATE_STATUS_LINE,      /* reading status line */
        STATE_HEADERS,          /* reading headers */
        STATE_BODY,             /* reading body */
        STATE_DONE              /* we've sent EOF */
    } state;

    /* Buffer for accumulating a line from the response. */
    serf_linebuf_t linebuf;

    int type; /* 0 = header, 1 = message */ /* TODO enum? */
    int channel;
    char *phrase;
    apr_size_t length;
} incoming_context_t;


serf_bucket_t *serf_bucket_bwtp_channel_close(
    int channel,
    serf_bucket_alloc_t *allocator)
{
    frame_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->type = 0;
    ctx->open = 0;
    ctx->channel = channel;
    ctx->phrase = "CLOSED";
    ctx->headers = serf_bucket_headers_create(allocator);

    return serf_bucket_create(&serf_bucket_type_bwtp_frame, allocator, ctx);
}

serf_bucket_t *serf_bucket_bwtp_channel_open(
    int channel,
    const char *uri,
    serf_bucket_alloc_t *allocator)
{
    frame_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->type = 0;
    ctx->open = 1;
    ctx->channel = channel;
    ctx->phrase = uri;
    ctx->headers = serf_bucket_headers_create(allocator);

    return serf_bucket_create(&serf_bucket_type_bwtp_frame, allocator, ctx);
}

serf_bucket_t *serf_bucket_bwtp_header_create(
    int channel,
    const char *phrase,
    serf_bucket_alloc_t *allocator)
{
    frame_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->type = 0;
    ctx->open = 0;
    ctx->channel = channel;
    ctx->phrase = phrase;
    ctx->headers = serf_bucket_headers_create(allocator);

    return serf_bucket_create(&serf_bucket_type_bwtp_frame, allocator, ctx);
}

serf_bucket_t *serf_bucket_bwtp_message_create(
    int channel,
    serf_bucket_t *body,
    serf_bucket_alloc_t *allocator)
{
    frame_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->type = 1;
    ctx->open = 0;
    ctx->channel = channel;
    ctx->phrase = "MESSAGE";
    ctx->headers = serf_bucket_headers_create(allocator);

    return serf_bucket_create(&serf_bucket_type_bwtp_frame, allocator, ctx);
}

int serf_bucket_bwtp_frame_get_channel(
    serf_bucket_t *bucket)
{
    if (SERF_BUCKET_IS_BWTP_FRAME(bucket)) {
        frame_context_t *ctx = bucket->data;

        return ctx->channel;
    }
    else if (SERF_BUCKET_IS_BWTP_INCOMING_FRAME(bucket)) {
        incoming_context_t *ctx = bucket->data;

        return ctx->channel;
    }

    return -1;
}

int serf_bucket_bwtp_frame_get_type(
    serf_bucket_t *bucket)
{
    if (SERF_BUCKET_IS_BWTP_FRAME(bucket)) {
        frame_context_t *ctx = bucket->data;

        return ctx->type;
    }
    else if (SERF_BUCKET_IS_BWTP_INCOMING_FRAME(bucket)) {
        incoming_context_t *ctx = bucket->data;

        return ctx->type;
    }

    return -1;
}

const char *serf_bucket_bwtp_frame_get_phrase(
    serf_bucket_t *bucket)
{
    if (SERF_BUCKET_IS_BWTP_FRAME(bucket)) {
        frame_context_t *ctx = bucket->data;

        return ctx->phrase;
    }
    else if (SERF_BUCKET_IS_BWTP_INCOMING_FRAME(bucket)) {
        incoming_context_t *ctx = bucket->data;

        return ctx->phrase;
    }

    return NULL;
}

serf_bucket_t *serf_bucket_bwtp_frame_get_headers(
    serf_bucket_t *bucket)
{
    if (SERF_BUCKET_IS_BWTP_FRAME(bucket)) {
        frame_context_t *ctx = bucket->data;

        return ctx->headers;
    }
    else if (SERF_BUCKET_IS_BWTP_INCOMING_FRAME(bucket)) {
        incoming_context_t *ctx = bucket->data;

        return ctx->headers;
    }

    return NULL;
}

static int count_size(void *baton, const char *key, const char *value)
{
    apr_size_t *c = baton;
    /* TODO Deal with folding.  Yikes. */

    /* Add in ": " and CRLF - so an extra four bytes. */
    *c += strlen(key) + strlen(value) + 4;

    return 0;
}

static apr_size_t calc_header_size(serf_bucket_t *hdrs)
{
    apr_size_t size = 0;

    serf_bucket_headers_do(hdrs, count_size, &size);

    return size;
}

static void serialize_data(serf_bucket_t *bucket)
{
    frame_context_t *ctx = bucket->data;
    serf_bucket_t *new_bucket;
    apr_size_t req_len;

    /* Serialize the request-line and headers into one mother string,
     * and wrap a bucket around it.
     */
    req_len = apr_snprintf(ctx->req_line, sizeof(ctx->req_line),
                           "%s %d " "%" APR_UINT64_T_HEX_FMT " %s%s\r\n",
                           (ctx->type ? "BWM" : "BWH"),
                           ctx->channel, calc_header_size(ctx->headers),
                           (ctx->open ? "OPEN " : ""),
                           ctx->phrase);
    new_bucket = serf_bucket_simple_copy_create(ctx->req_line, req_len,
                                                bucket->allocator);

    /* Build up the new bucket structure.
     *
     * Note that self needs to become an aggregate bucket so that a
     * pointer to self still represents the "right" data.
     */
    serf_bucket_aggregate_become(bucket);

    /* Insert the two buckets. */
    serf_bucket_aggregate_append(bucket, new_bucket);
    serf_bucket_aggregate_append(bucket, ctx->headers);

    /* Our private context is no longer needed, and is not referred to by
     * any existing bucket. Toss it.
     */
    serf_bucket_mem_free(bucket->allocator, ctx);
}

static apr_status_t serf_bwtp_frame_read(serf_bucket_t *bucket,
                                         apr_size_t requested,
                                         const char **data, apr_size_t *len)
{
    /* Seralize our private data into a new aggregate bucket. */
    serialize_data(bucket);

    /* Delegate to the "new" aggregate bucket to do the read. */
    return serf_bucket_read(bucket, requested, data, len);
}

static apr_status_t serf_bwtp_frame_readline(serf_bucket_t *bucket,
                                             int acceptable, int *found,
                                             const char **data, apr_size_t *len)
{
    /* Seralize our private data into a new aggregate bucket. */
    serialize_data(bucket);

    /* Delegate to the "new" aggregate bucket to do the readline. */
    return serf_bucket_readline(bucket, acceptable, found, data, len);
}

static apr_status_t serf_bwtp_frame_read_iovec(serf_bucket_t *bucket,
                                               apr_size_t requested,
                                               int vecs_size,
                                               struct iovec *vecs,
                                               int *vecs_used)
{
    /* Seralize our private data into a new aggregate bucket. */
    serialize_data(bucket);

    /* Delegate to the "new" aggregate bucket to do the read. */
    return serf_bucket_read_iovec(bucket, requested,
                                  vecs_size, vecs, vecs_used);
}

static apr_status_t serf_bwtp_frame_peek(serf_bucket_t *bucket,
                                         const char **data,
                                         apr_size_t *len)
{
    /* Seralize our private data into a new aggregate bucket. */
    serialize_data(bucket);

    /* Delegate to the "new" aggregate bucket to do the peek. */
    return serf_bucket_peek(bucket, data, len);
}

const serf_bucket_type_t serf_bucket_type_bwtp_frame = {
    "BWTP-FRAME",
    serf_bwtp_frame_read,
    serf_bwtp_frame_readline,
    serf_bwtp_frame_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_bwtp_frame_peek,
    serf_default_destroy_and_data,
};


serf_bucket_t *serf_bucket_bwtp_incoming_frame_create(
    serf_bucket_t *stream,
    serf_bucket_alloc_t *allocator)
{
    incoming_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;
    ctx->body = NULL;
    ctx->headers = serf_bucket_headers_create(allocator);
    ctx->state = STATE_STATUS_LINE;
    ctx->length = 0;
    ctx->channel = -1;
    ctx->phrase = NULL;

    serf_linebuf_init(&ctx->linebuf);

    return serf_bucket_create(&serf_bucket_type_bwtp_incoming_frame, allocator, ctx);
}

static void bwtp_incoming_destroy_and_data(serf_bucket_t *bucket)
{
    incoming_context_t *ctx = bucket->data;

    if (ctx->state != STATE_STATUS_LINE && ctx->phrase) {
        serf_bucket_mem_free(bucket->allocator, (void*)ctx->phrase);
    }

    serf_bucket_destroy(ctx->stream);
    if (ctx->body != NULL)
        serf_bucket_destroy(ctx->body);
    serf_bucket_destroy(ctx->headers);

    serf_default_destroy_and_data(bucket);
}

static apr_status_t fetch_line(incoming_context_t *ctx, int acceptable)
{
    return serf_linebuf_fetch(&ctx->linebuf, ctx->stream, acceptable);
}

static apr_status_t parse_status_line(incoming_context_t *ctx,
                                      serf_bucket_alloc_t *allocator)
{
    int res;
    char *reason; /* ### stupid APR interface makes this non-const */

    /* ctx->linebuf.line should be of form: BW* */
    res = apr_date_checkmask(ctx->linebuf.line, "BW*");
    if (!res) {
        /* Not an BWTP response?  Well, at least we won't understand it. */
        return APR_EGENERAL;
    }

    if (ctx->linebuf.line[2] == 'H') {
        ctx->type = 0;
    }
    else if (ctx->linebuf.line[2] == 'M') {
        ctx->type = 1;
    }
    else {
        ctx->type = -1;
    }

    ctx->channel = apr_strtoi64(ctx->linebuf.line + 3, &reason, 16);

    /* Skip leading spaces for the reason string. */
    if (apr_isspace(*reason)) {
        reason++;
    }

    ctx->length = apr_strtoi64(reason, &reason, 16);

    /* Skip leading spaces for the reason string. */
    if (reason - ctx->linebuf.line < ctx->linebuf.used) {
        if (apr_isspace(*reason)) {
            reason++;
        }

        ctx->phrase = serf_bstrmemdup(allocator, reason,
                                      ctx->linebuf.used
                                      - (reason - ctx->linebuf.line));
    } else {
        ctx->phrase = NULL;
    }

    return APR_SUCCESS;
}

/* This code should be replaced with header buckets. */
static apr_status_t fetch_headers(serf_bucket_t *bkt, incoming_context_t *ctx)
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
            return APR_EGENERAL;
        }

        /* Skip over initial : and spaces. */
        while (apr_isspace(*++c))
            continue;

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
static apr_status_t run_machine(serf_bucket_t *bkt, incoming_context_t *ctx)
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

        if (ctx->linebuf.state == SERF_LINEBUF_READY && ctx->linebuf.used) {
            /* The Status-Line is in the line buffer. Process it. */
            status = parse_status_line(ctx, bkt->allocator);
            if (status)
                return status;

            if (ctx->length) {
                ctx->body =
                    serf_bucket_barrier_create(ctx->stream, bkt->allocator);
                ctx->body = serf_bucket_limit_create(ctx->body, ctx->length,
                                                     bkt->allocator);
                if (!ctx->type) {
                    ctx->state = STATE_HEADERS;
                } else {
                    ctx->state = STATE_BODY;
                }
            } else {
                ctx->state = STATE_DONE;
            }
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
        status = fetch_headers(ctx->body, ctx);
        if (SERF_BUCKET_READ_ERROR(status))
            return status;

        /* If an empty line was read, then we hit the end of the headers.
         * Move on to the body.
         */
        if (ctx->linebuf.state == SERF_LINEBUF_READY && !ctx->linebuf.used) {
            /* Advance the state. */
            ctx->state = STATE_DONE;
        }
        break;
    case STATE_BODY:
        /* Don't do anything. */
        break;
    case STATE_DONE:
        return APR_EOF;
    default:
        /* Not reachable */
        return APR_EGENERAL;
    }

    return status;
}

static apr_status_t wait_for_body(serf_bucket_t *bkt, incoming_context_t *ctx)
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

apr_status_t serf_bucket_bwtp_incoming_frame_wait_for_headers(
    serf_bucket_t *bucket)
{
    incoming_context_t *ctx = bucket->data;

    return wait_for_body(bucket, ctx);
}

static apr_status_t bwtp_incoming_read(serf_bucket_t *bucket,
                                       apr_size_t requested,
                                       const char **data, apr_size_t *len)
{
    incoming_context_t *ctx = bucket->data;
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
    if (APR_STATUS_IS_EOF(rv)) {
        ctx->state = STATE_DONE;
    }
    return rv;
}

static apr_status_t bwtp_incoming_readline(serf_bucket_t *bucket,
                                           int acceptable, int *found,
                                           const char **data, apr_size_t *len)
{
    incoming_context_t *ctx = bucket->data;
    apr_status_t rv;

    rv = wait_for_body(bucket, ctx);
    if (rv) {
        return rv;
    }

    /* Delegate to the stream bucket to do the readline. */
    return serf_bucket_readline(ctx->body, acceptable, found, data, len);
}

/* ### need to implement */
#define bwtp_incoming_peek NULL

const serf_bucket_type_t serf_bucket_type_bwtp_incoming_frame = {
    "BWTP-INCOMING",
    bwtp_incoming_read,
    bwtp_incoming_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    bwtp_incoming_peek,
    bwtp_incoming_destroy_and_data,
};
