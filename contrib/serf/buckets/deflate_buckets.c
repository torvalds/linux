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

#include <zlib.h>

/* This conditional isn't defined anywhere yet. */
#ifdef HAVE_ZUTIL_H
#include <zutil.h>
#endif

#include "serf.h"
#include "serf_bucket_util.h"

/* magic header */
static char deflate_magic[2] = { '\037', '\213' };
#define DEFLATE_MAGIC_SIZE 10
#define DEFLATE_VERIFY_SIZE 8
#define DEFLATE_BUFFER_SIZE 8096

static const int DEFLATE_WINDOW_SIZE = -15;
static const int DEFLATE_MEMLEVEL = 9;

typedef struct {
    serf_bucket_t *stream;
    serf_bucket_t *inflate_stream;

    int format;                 /* Are we 'deflate' or 'gzip'? */

    enum {
        STATE_READING_HEADER,   /* reading the gzip header */
        STATE_HEADER,           /* read the gzip header */
        STATE_INIT,             /* init'ing zlib functions */
        STATE_INFLATE,          /* inflating the content now */
        STATE_READING_VERIFY,   /* reading the final gzip CRC */
        STATE_VERIFY,           /* verifying the final gzip CRC */
        STATE_FINISH,           /* clean up after reading body */
        STATE_DONE,             /* body is done; we'll return EOF here */
    } state;

    z_stream zstream;
    char hdr_buffer[DEFLATE_MAGIC_SIZE];
    unsigned char buffer[DEFLATE_BUFFER_SIZE];
    unsigned long crc;
    int windowSize;
    int memLevel;
    int bufferSize;

    /* How much of the chunk, or the terminator, do we have left to read? */
    apr_size_t stream_left;

    /* How much are we supposed to read? */
    apr_size_t stream_size;

    int stream_status; /* What was the last status we read? */

} deflate_context_t;

/* Inputs a string and returns a long.  */
static unsigned long getLong(unsigned char *string)
{
    return ((unsigned long)string[0])
          | (((unsigned long)string[1]) << 8)
          | (((unsigned long)string[2]) << 16)
          | (((unsigned long)string[3]) << 24);
}

serf_bucket_t *serf_bucket_deflate_create(
    serf_bucket_t *stream,
    serf_bucket_alloc_t *allocator,
    int format)
{
    deflate_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;
    ctx->stream_status = APR_SUCCESS;
    ctx->inflate_stream = serf_bucket_aggregate_create(allocator);
    ctx->format = format;
    ctx->crc = 0;
    /* zstream must be NULL'd out. */
    memset(&ctx->zstream, 0, sizeof(ctx->zstream));

    switch (ctx->format) {
        case SERF_DEFLATE_GZIP:
            ctx->state = STATE_READING_HEADER;
            break;
        case SERF_DEFLATE_DEFLATE:
            /* deflate doesn't have a header. */
            ctx->state = STATE_INIT;
            break;
        default:
            /* Not reachable */
            return NULL;
    }

    /* Initial size of gzip header. */
    ctx->stream_left = ctx->stream_size = DEFLATE_MAGIC_SIZE;

    ctx->windowSize = DEFLATE_WINDOW_SIZE;
    ctx->memLevel = DEFLATE_MEMLEVEL;
    ctx->bufferSize = DEFLATE_BUFFER_SIZE;

    return serf_bucket_create(&serf_bucket_type_deflate, allocator, ctx);
}

static void serf_deflate_destroy_and_data(serf_bucket_t *bucket)
{
    deflate_context_t *ctx = bucket->data;

    if (ctx->state > STATE_INIT &&
        ctx->state <= STATE_FINISH)
        inflateEnd(&ctx->zstream);

    /* We may have appended inflate_stream into the stream bucket.
     * If so, avoid free'ing it twice.
     */
    if (ctx->inflate_stream) {
        serf_bucket_destroy(ctx->inflate_stream);
    }
    serf_bucket_destroy(ctx->stream);

    serf_default_destroy_and_data(bucket);
}

static apr_status_t serf_deflate_read(serf_bucket_t *bucket,
                                      apr_size_t requested,
                                      const char **data, apr_size_t *len)
{
    deflate_context_t *ctx = bucket->data;
    apr_status_t status;
    const char *private_data;
    apr_size_t private_len;
    int zRC;

    while (1) {
        switch (ctx->state) {
        case STATE_READING_HEADER:
        case STATE_READING_VERIFY:
            status = serf_bucket_read(ctx->stream, ctx->stream_left,
                                      &private_data, &private_len);

            if (SERF_BUCKET_READ_ERROR(status)) {
                return status;
            }

            memcpy(ctx->hdr_buffer + (ctx->stream_size - ctx->stream_left),
                   private_data, private_len);

            ctx->stream_left -= private_len;

            if (ctx->stream_left == 0) {
                ctx->state++;
                if (APR_STATUS_IS_EAGAIN(status)) {
                    *len = 0;
                    return status;
                }
            }
            else if (status) {
                *len = 0;
                return status;
            }
            break;
        case STATE_HEADER:
            if (ctx->hdr_buffer[0] != deflate_magic[0] ||
                ctx->hdr_buffer[1] != deflate_magic[1]) {
                return SERF_ERROR_DECOMPRESSION_FAILED;
            }
            if (ctx->hdr_buffer[3] != 0) {
                return SERF_ERROR_DECOMPRESSION_FAILED;
            }
            ctx->state++;
            break;
        case STATE_VERIFY:
        {
            unsigned long compCRC, compLen, actualLen;

            /* Do the checksum computation. */
            compCRC = getLong((unsigned char*)ctx->hdr_buffer);
            if (ctx->crc != compCRC) {
                return SERF_ERROR_DECOMPRESSION_FAILED;
            }
            compLen = getLong((unsigned char*)ctx->hdr_buffer + 4);
            /* The length in the trailer is module 2^32, so do the same for
               the actual length. */
            actualLen = ctx->zstream.total_out;
            actualLen &= 0xFFFFFFFF;
            if (actualLen != compLen) {
                return SERF_ERROR_DECOMPRESSION_FAILED;
            }
            ctx->state++;
            break;
        }
        case STATE_INIT:
            zRC = inflateInit2(&ctx->zstream, ctx->windowSize);
            if (zRC != Z_OK) {
                return SERF_ERROR_DECOMPRESSION_FAILED;
            }
            ctx->zstream.next_out = ctx->buffer;
            ctx->zstream.avail_out = ctx->bufferSize;
            ctx->state++;
            break;
        case STATE_FINISH:
            inflateEnd(&ctx->zstream);
            serf_bucket_aggregate_prepend(ctx->stream, ctx->inflate_stream);
            ctx->inflate_stream = 0;
            ctx->state++;
            break;
        case STATE_INFLATE:
            /* Do we have anything already uncompressed to read? */
            status = serf_bucket_read(ctx->inflate_stream, requested, data,
                                      len);
            if (SERF_BUCKET_READ_ERROR(status)) {
                return status;
            }
            /* Hide EOF. */
            if (APR_STATUS_IS_EOF(status)) {
                status = ctx->stream_status;
                if (APR_STATUS_IS_EOF(status)) {
                    /* We've read all of the data from our stream, but we
                     * need to continue to iterate until we flush
                     * out the zlib buffer.
                     */
                    status = APR_SUCCESS;
                }
            }
            if (*len != 0) {
                return status;
            }

            /* We tried; but we have nothing buffered. Fetch more. */

            /* It is possible that we maxed out avail_out before
             * exhausting avail_in; therefore, continue using the
             * previous buffer.  Otherwise, fetch more data from
             * our stream bucket.
             */
            if (ctx->zstream.avail_in == 0) {
                /* When we empty our inflated stream, we'll return this
                 * status - this allow us to eventually pass up EAGAINs.
                 */
                ctx->stream_status = serf_bucket_read(ctx->stream,
                                                      ctx->bufferSize,
                                                      &private_data,
                                                      &private_len);

                if (SERF_BUCKET_READ_ERROR(ctx->stream_status)) {
                    return ctx->stream_status;
                }

                if (!private_len && APR_STATUS_IS_EAGAIN(ctx->stream_status)) {
                    *len = 0;
                    status = ctx->stream_status;
                    ctx->stream_status = APR_SUCCESS;
                    return status;
                }

                ctx->zstream.next_in = (unsigned char*)private_data;
                ctx->zstream.avail_in = private_len;
            }

            while (1) {

                zRC = inflate(&ctx->zstream, Z_NO_FLUSH);

                /* We're full or zlib requires more space. Either case, clear
                   out our buffer, reset, and return. */
                if (zRC == Z_BUF_ERROR || ctx->zstream.avail_out == 0) {
                    serf_bucket_t *tmp;
                    ctx->zstream.next_out = ctx->buffer;
                    private_len = ctx->bufferSize - ctx->zstream.avail_out;

                    ctx->crc = crc32(ctx->crc, (const Bytef *)ctx->buffer,
                                     private_len);

                    /* FIXME: There probably needs to be a free func. */
                    tmp = SERF_BUCKET_SIMPLE_STRING_LEN((char *)ctx->buffer,
                                                        private_len,
                                                        bucket->allocator);
                    serf_bucket_aggregate_append(ctx->inflate_stream, tmp);
                    ctx->zstream.avail_out = ctx->bufferSize;
                    break;
                }

                if (zRC == Z_STREAM_END) {
                    serf_bucket_t *tmp;

                    private_len = ctx->bufferSize - ctx->zstream.avail_out;
                    ctx->crc = crc32(ctx->crc, (const Bytef *)ctx->buffer,
                                     private_len);
                    /* FIXME: There probably needs to be a free func. */
                    tmp = SERF_BUCKET_SIMPLE_STRING_LEN((char *)ctx->buffer,
                                                        private_len,
                                                        bucket->allocator);
                    serf_bucket_aggregate_append(ctx->inflate_stream, tmp);

                    ctx->zstream.avail_out = ctx->bufferSize;

                    /* Push back the remaining data to be read. */
                    tmp = serf_bucket_aggregate_create(bucket->allocator);
                    serf_bucket_aggregate_prepend(tmp, ctx->stream);
                    ctx->stream = tmp;

                    /* We now need to take the remaining avail_in and
                     * throw it in ctx->stream so our next read picks it up.
                     */
                    tmp = SERF_BUCKET_SIMPLE_STRING_LEN(
                                        (const char*)ctx->zstream.next_in,
                                                     ctx->zstream.avail_in,
                                                     bucket->allocator);
                    serf_bucket_aggregate_prepend(ctx->stream, tmp);

                    switch (ctx->format) {
                    case SERF_DEFLATE_GZIP:
                        ctx->stream_left = ctx->stream_size =
                            DEFLATE_VERIFY_SIZE;
                        ctx->state++;
                        break;
                    case SERF_DEFLATE_DEFLATE:
                        /* Deflate does not have a verify footer. */
                        ctx->state = STATE_FINISH;
                        break;
                    default:
                        /* Not reachable */
                        return APR_EGENERAL;
                    }

                    break;
                }

                /* Any other error? */
                if (zRC != Z_OK) {
                    return SERF_ERROR_DECOMPRESSION_FAILED;
                }

                /* As long as zRC == Z_OK, just keep looping. */
            }
            /* Okay, we've inflated.  Try to read. */
            status = serf_bucket_read(ctx->inflate_stream, requested, data,
                                      len);
            /* Hide EOF. */
            if (APR_STATUS_IS_EOF(status)) {
                status = ctx->stream_status;

                /* If the inflation wasn't finished, return APR_SUCCESS. */
                if (zRC != Z_STREAM_END)
                    return APR_SUCCESS;

                /* If our stream is finished too and all data was inflated,
                 * return SUCCESS so we'll iterate one more time.
                 */
                if (APR_STATUS_IS_EOF(status)) {
                    /* No more data to read from the stream, and everything
                       inflated. If all data was received correctly, state
                       should have been advanced to STATE_READING_VERIFY or
                       STATE_FINISH. If not, then the data was incomplete
                       and we have an error. */
                    if (ctx->state != STATE_INFLATE)
                        return APR_SUCCESS;
                    else
                        return SERF_ERROR_DECOMPRESSION_FAILED;
                }
            }
            return status;
        case STATE_DONE:
            /* We're done inflating.  Use our finished buffer. */
            return serf_bucket_read(ctx->stream, requested, data, len);
        default:
            /* Not reachable */
            return APR_EGENERAL;
        }
    }

    /* NOTREACHED */
}

/* ### need to implement */
#define serf_deflate_readline NULL
#define serf_deflate_peek NULL

const serf_bucket_type_t serf_bucket_type_deflate = {
    "DEFLATE",
    serf_deflate_read,
    serf_deflate_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_deflate_peek,
    serf_deflate_destroy_and_data,
};
