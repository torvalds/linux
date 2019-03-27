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
#include "serf_private.h"

serf_bucket_t *serf_bucket_create(
    const serf_bucket_type_t *type,
    serf_bucket_alloc_t *allocator,
    void *data)
{
    serf_bucket_t *bkt = serf_bucket_mem_alloc(allocator, sizeof(*bkt));

    bkt->type = type;
    bkt->data = data;
    bkt->allocator = allocator;

    return bkt;
}


apr_status_t serf_default_read_iovec(
    serf_bucket_t *bucket,
    apr_size_t requested,
    int vecs_size,
    struct iovec *vecs,
    int *vecs_used)
{
    const char *data;
    apr_size_t len;

    /* Read some data from the bucket.
     *
     * Because we're an internal 'helper' to the bucket, we can't call the
     * normal serf_bucket_read() call because the debug allocator tracker will
     * end up marking the bucket as read *twice* - once for us and once for
     * our caller - which is reading the same bucket.  This leads to premature
     * abort()s if we ever see EAGAIN.  Instead, we'll go directly to the
     * vtable and bypass the debug tracker.
     */
    apr_status_t status = bucket->type->read(bucket, requested, &data, &len);

    /* assert that vecs_size >= 1 ? */

    /* Return that data as a single iovec. */
    if (len) {
        vecs[0].iov_base = (void *)data; /* loses the 'const' */
        vecs[0].iov_len = len;
        *vecs_used = 1;
    }
    else {
        *vecs_used = 0;
    }

    return status;
}


apr_status_t serf_default_read_for_sendfile(
    serf_bucket_t *bucket,
    apr_size_t requested,
    apr_hdtr_t *hdtr,
    apr_file_t **file,
    apr_off_t *offset,
    apr_size_t *len)
{
    /* Read a bunch of stuff into the headers.
     *
     * See serf_default_read_iovec as to why we call into the vtable
     * directly.
     */
    apr_status_t status = bucket->type->read_iovec(bucket, requested,
                                                   hdtr->numheaders,
                                                   hdtr->headers,
                                                   &hdtr->numheaders);

    /* There isn't a file, and there are no trailers. */
    *file = NULL;
    hdtr->numtrailers = 0;

    return status;
}


serf_bucket_t *serf_default_read_bucket(
    serf_bucket_t *bucket,
    const serf_bucket_type_t *type)
{
    return NULL;
}


void serf_default_destroy(serf_bucket_t *bucket)
{
#ifdef SERF_DEBUG_BUCKET_USE
    serf_debug__bucket_destroy(bucket);
#endif

    serf_bucket_mem_free(bucket->allocator, bucket);
}


void serf_default_destroy_and_data(serf_bucket_t *bucket)
{
    serf_bucket_mem_free(bucket->allocator, bucket->data);
    serf_default_destroy(bucket);
}


/* ==================================================================== */


char *serf_bstrmemdup(serf_bucket_alloc_t *allocator,
                      const char *str,
                      apr_size_t size)
{
    char *newstr = serf_bucket_mem_alloc(allocator, size + 1);
    memcpy(newstr, str, size);
    newstr[size] = '\0';
    return newstr;
}


void *serf_bmemdup(serf_bucket_alloc_t *allocator,
                   const void *mem,
                   apr_size_t size)
{
    void *newmem = serf_bucket_mem_alloc(allocator, size);
    memcpy(newmem, mem, size);
    return newmem;
}


char *serf_bstrdup(serf_bucket_alloc_t *allocator,
                   const char *str)
{
    apr_size_t size = strlen(str) + 1;
    char *newstr = serf_bucket_mem_alloc(allocator, size);
    memcpy(newstr, str, size);
    return newstr;
}

char *serf_bstrcatv(serf_bucket_alloc_t *allocator, struct iovec *vec,
                    int vecs, apr_size_t *bytes_written)
{
    int i;
    apr_size_t new_len = 0;
    char *c, *newstr;

    for (i = 0; i < vecs; i++) {
        new_len += vec[i].iov_len;
    }

    /* It's up to the caller to free this memory later. */
    newstr = serf_bucket_mem_alloc(allocator, new_len);

    c = newstr;
    for (i = 0; i < vecs; i++) {
        memcpy(c, vec[i].iov_base, vec[i].iov_len);
        c += vec[i].iov_len;
    }

    if (bytes_written) {
        *bytes_written = c - newstr;
    }

    return newstr;
}

/* ==================================================================== */


static void find_crlf(const char **data, apr_size_t *len, int *found)
{
    const char *start = *data;
    const char *end = start + *len;

    while (start < end) {
        const char *cr = memchr(start, '\r', *len);

        if (cr == NULL) {
            break;
        }
        ++cr;

        if (cr < end && cr[0] == '\n') {
            *len -= cr + 1 - start;
            *data = cr + 1;
            *found = SERF_NEWLINE_CRLF;
            return;
        }
        if (cr == end) {
            *len = 0;
            *data = end;
            *found = SERF_NEWLINE_CRLF_SPLIT;
            return;
        }

        /* It was a bare CR without an LF. Just move past it. */
        *len -= cr - start;
        start = cr;
    }

    *data = start + *len;
    *len -= *data - start;
    *found = SERF_NEWLINE_NONE;
}


void serf_util_readline(
    const char **data,
    apr_size_t *len,
    int acceptable,
    int *found)
{
    const char *start;
    const char *cr;
    const char *lf;
    int want_cr;
    int want_crlf;
    int want_lf;

    /* If _only_ CRLF is acceptable, then the scanning needs a loop to
     * skip false hits on CR characters. Use a separate function.
     */
    if (acceptable == SERF_NEWLINE_CRLF) {
        find_crlf(data, len, found);
        return;
    }

    start = *data;
    cr = lf = NULL;
    want_cr = acceptable & SERF_NEWLINE_CR;
    want_crlf = acceptable & SERF_NEWLINE_CRLF;
    want_lf = acceptable & SERF_NEWLINE_LF;

    if (want_cr || want_crlf) {
        cr = memchr(start, '\r', *len);
    }
    if (want_lf) {
        lf = memchr(start, '\n', *len);
    }

    if (cr != NULL) {
        if (lf != NULL) {
            if (cr + 1 == lf)
                *found = want_crlf ? SERF_NEWLINE_CRLF : SERF_NEWLINE_CR;
            else if (want_cr && cr < lf)
                *found = SERF_NEWLINE_CR;
            else
                *found = SERF_NEWLINE_LF;
        }
        else if (cr == start + *len - 1) {
            /* the CR occurred in the last byte of the buffer. this could be
             * a CRLF split across the data boundary.
             * ### FIX THIS LOGIC? does caller need to detect?
             */
            *found = want_crlf ? SERF_NEWLINE_CRLF_SPLIT : SERF_NEWLINE_CR;
        }
        else if (want_cr)
            *found = SERF_NEWLINE_CR;
        else /* want_crlf */
            *found = SERF_NEWLINE_NONE;
    }
    else if (lf != NULL)
        *found = SERF_NEWLINE_LF;
    else
        *found = SERF_NEWLINE_NONE;

    switch (*found) {
      case SERF_NEWLINE_LF:
        *data = lf + 1;
        break;
      case SERF_NEWLINE_CR:
      case SERF_NEWLINE_CRLF:
      case SERF_NEWLINE_CRLF_SPLIT:
        *data = cr + 1 + (*found == SERF_NEWLINE_CRLF);
        break;
      case SERF_NEWLINE_NONE:
        *data += *len;
        break;
      default:
        /* Not reachable */
        return;
    }

    *len -= *data - start;
}


/* ==================================================================== */


void serf_databuf_init(serf_databuf_t *databuf)
{
    /* nothing is sitting in the buffer */
    databuf->remaining = 0;

    /* avoid thinking we have hit EOF */
    databuf->status = APR_SUCCESS;
}

/* Ensure the buffer is prepared for reading. Will return APR_SUCCESS,
 * APR_EOF, or some failure code. *len is only set for EOF. */
static apr_status_t common_databuf_prep(serf_databuf_t *databuf,
                                        apr_size_t *len)
{
    apr_size_t readlen;
    apr_status_t status;

    /* if there is data in the buffer, then we're happy. */
    if (databuf->remaining > 0)
        return APR_SUCCESS;

    /* if we already hit EOF, then keep returning that. */
    if (APR_STATUS_IS_EOF(databuf->status)) {
        /* *data = NULL;   ?? */
        *len = 0;
        return APR_EOF;
    }

    /* refill the buffer */
    status = (*databuf->read)(databuf->read_baton, sizeof(databuf->buf),
                              databuf->buf, &readlen);
    if (SERF_BUCKET_READ_ERROR(status)) {
        return status;
    }

    databuf->current = databuf->buf;
    databuf->remaining = readlen;
    databuf->status = status;

    return APR_SUCCESS;
}


apr_status_t serf_databuf_read(
    serf_databuf_t *databuf,
    apr_size_t requested,
    const char **data,
    apr_size_t *len)
{
    apr_status_t status = common_databuf_prep(databuf, len);
    if (status)
        return status;

    /* peg the requested amount to what we have remaining */
    if (requested == SERF_READ_ALL_AVAIL || requested > databuf->remaining)
        requested = databuf->remaining;

    /* return the values */
    *data = databuf->current;
    *len = requested;

    /* adjust our internal state to note we've consumed some data */
    databuf->current += requested;
    databuf->remaining -= requested;

    /* If we read everything, then we need to return whatever the data
     * read returned to us. This is going to be APR_EOF or APR_EGAIN.
     * If we have NOT read everything, then return APR_SUCCESS to indicate
     * that we're ready to return some more if asked.
     */
    return databuf->remaining ? APR_SUCCESS : databuf->status;
}


apr_status_t serf_databuf_readline(
    serf_databuf_t *databuf,
    int acceptable,
    int *found,
    const char **data,
    apr_size_t *len)
{
    apr_status_t status = common_databuf_prep(databuf, len);
    if (status)
        return status;

    /* the returned line will start at the current position. */
    *data = databuf->current;

    /* read a line from the buffer, and adjust the various pointers. */
    serf_util_readline(&databuf->current, &databuf->remaining, acceptable,
                       found);

    /* the length matches the amount consumed by the readline */
    *len = databuf->current - *data;

    /* see serf_databuf_read's return condition */
    return databuf->remaining ? APR_SUCCESS : databuf->status;
}


apr_status_t serf_databuf_peek(
    serf_databuf_t *databuf,
    const char **data,
    apr_size_t *len)
{
    apr_status_t status = common_databuf_prep(databuf, len);
    if (status)
        return status;

    /* return everything we have */
    *data = databuf->current;
    *len = databuf->remaining;

    /* If the last read returned EOF, then the peek should return the same.
     * The other possibility in databuf->status is APR_EAGAIN, which we
     * should never return. Thus, just return APR_SUCCESS for non-EOF cases.
     */
    if (APR_STATUS_IS_EOF(databuf->status))
        return APR_EOF;
    return APR_SUCCESS;
}


/* ==================================================================== */


void serf_linebuf_init(serf_linebuf_t *linebuf)
{
    linebuf->state = SERF_LINEBUF_EMPTY;
    linebuf->used = 0;
}


apr_status_t serf_linebuf_fetch(
    serf_linebuf_t *linebuf,
    serf_bucket_t *bucket,
    int acceptable)
{
    /* If we had a complete line, then assume the caller has used it, so
     * we can now reset the state.
     */
    if (linebuf->state == SERF_LINEBUF_READY) {
        linebuf->state = SERF_LINEBUF_EMPTY;

        /* Reset the line_used, too, so we don't have to test the state
         * before using this value.
         */
        linebuf->used = 0;
    }

    while (1) {
        apr_status_t status;
        const char *data;
        apr_size_t len;

        if (linebuf->state == SERF_LINEBUF_CRLF_SPLIT) {
            /* On the previous read, we received just a CR. The LF might
             * be present, but the bucket couldn't see it. We need to
             * examine a single character to determine how to handle the
             * split CRLF.
             */

            status = serf_bucket_peek(bucket, &data, &len);
            if (SERF_BUCKET_READ_ERROR(status))
                return status;

            if (len > 0) {
                if (*data == '\n') {
                    /* We saw the second part of CRLF. We don't need to
                     * save that character, so do an actual read to suck
                     * up that character.
                     */
                    /* ### check status */
                    (void) serf_bucket_read(bucket, 1, &data, &len);
                }
                /* else:
                 *   We saw the first character of the next line. Thus,
                 *   the current line is terminated by the CR. Just
                 *   ignore whatever we peeked at. The next reader will
                 *   see it and handle it as appropriate.
                 */

                /* Whatever was read, the line is now ready for use. */
                linebuf->state = SERF_LINEBUF_READY;
            } else {
                /* no data available, try again later. */
                return APR_EAGAIN;
            }
        }
        else {
            int found;

            status = serf_bucket_readline(bucket, acceptable, &found,
                                          &data, &len);
            if (SERF_BUCKET_READ_ERROR(status)) {
                return status;
            }
            /* Some bucket types (socket) might need an extra read to find
               out EOF state, so they'll return no data in that read. This
               means we're done reading, return what we got. */
            if (APR_STATUS_IS_EOF(status) && len == 0) {
	        return status;
            }
            if (linebuf->used + len > sizeof(linebuf->line)) {
                /* ### need a "line too long" error */
                return APR_EGENERAL;
            }

            /* Note: our logic doesn't change for SERF_LINEBUF_PARTIAL. That
             * only affects how we fill the buffer. It is a communication to
             * our caller on whether the line is ready or not.
             */

            /* If we didn't see a newline, then we should mark the line
             * buffer as partially complete.
             */
            if (found == SERF_NEWLINE_NONE) {
                linebuf->state = SERF_LINEBUF_PARTIAL;
            }
            else if (found == SERF_NEWLINE_CRLF_SPLIT) {
                linebuf->state = SERF_LINEBUF_CRLF_SPLIT;

                /* Toss the partial CR. We won't ever need it. */
                --len;
            }
            else {
                /* We got a newline (of some form). We don't need it
                 * in the line buffer, so back up the length. Then
                 * mark the line as ready.
                 */
                len -= 1 + (found == SERF_NEWLINE_CRLF);

                linebuf->state = SERF_LINEBUF_READY;
            }

            /* ### it would be nice to avoid this copy if at all possible,
               ### and just return the a data/len pair to the caller. we're
               ### keeping it simple for now. */
            memcpy(&linebuf->line[linebuf->used], data, len);
            linebuf->used += len;
        }

        /* If we saw anything besides "success. please read again", then
         * we should return that status. If the line was completed, then
         * we should also return.
         */
        if (status || linebuf->state == SERF_LINEBUF_READY)
            return status;

        /* We got APR_SUCCESS and the line buffer is not complete. Let's
         * loop to read some more data.
         */
    }
    /* NOTREACHED */
}

/* Logging functions.
   Use with one of the [COMP]_VERBOSE defines so that the compiler knows to
   optimize this code out when no logging is needed. */
static void log_time()
{
    apr_time_exp_t tm;

    apr_time_exp_lt(&tm, apr_time_now());
    fprintf(stderr, "[%d-%02d-%02dT%02d:%02d:%02d.%06d%+03d] ",
            1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_usec,
            tm.tm_gmtoff/3600);
}

void serf__log(int verbose_flag, const char *filename, const char *fmt, ...)
{
    va_list argp;

    if (verbose_flag) {
        log_time();

        if (filename)
            fprintf(stderr, "%s: ", filename);

        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
    }
}

void serf__log_nopref(int verbose_flag, const char *fmt, ...)
{
    va_list argp;

    if (verbose_flag) {
        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
    }
}

void serf__log_skt(int verbose_flag, const char *filename, apr_socket_t *skt,
                   const char *fmt, ...)
{
    va_list argp;

    if (verbose_flag) {
        apr_sockaddr_t *sa;
        log_time();

        if (skt) {
            /* Log local and remote ip address:port */
            fprintf(stderr, "[l:");
            if (apr_socket_addr_get(&sa, APR_LOCAL, skt) == APR_SUCCESS) {
                char buf[32];
                apr_sockaddr_ip_getbuf(buf, 32, sa);
                fprintf(stderr, "%s:%d", buf, sa->port);
            }
            fprintf(stderr, " r:");
            if (apr_socket_addr_get(&sa, APR_REMOTE, skt) == APR_SUCCESS) {
                char buf[32];
                apr_sockaddr_ip_getbuf(buf, 32, sa);
                fprintf(stderr, "%s:%d", buf, sa->port);
            }
            fprintf(stderr, "] ");
        }

        if (filename)
            fprintf(stderr, "%s: ", filename);

        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
    }
}

