/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_buckets.h"

static apr_status_t pipe_bucket_read(apr_bucket *a, const char **str,
                                     apr_size_t *len, apr_read_type_e block)
{
    apr_file_t *p = a->data;
    char *buf;
    apr_status_t rv;
    apr_interval_time_t timeout;

    if (block == APR_NONBLOCK_READ) {
        apr_file_pipe_timeout_get(p, &timeout);
        apr_file_pipe_timeout_set(p, 0);
    }

    *str = NULL;
    *len = APR_BUCKET_BUFF_SIZE;
    buf = apr_bucket_alloc(*len, a->list); /* XXX: check for failure? */

    rv = apr_file_read(p, buf, len);

    if (block == APR_NONBLOCK_READ) {
        apr_file_pipe_timeout_set(p, timeout);
    }

    if (rv != APR_SUCCESS && rv != APR_EOF) {
        apr_bucket_free(buf);
        return rv;
    }
    /*
     * If there's more to read we have to keep the rest of the pipe
     * for later.  Otherwise, we'll close the pipe.
     * XXX: Note that more complicated bucket types that 
     * refer to data not in memory and must therefore have a read()
     * function similar to this one should be wary of copying this
     * code because if they have a destroy function they probably
     * want to migrate the bucket's subordinate structure from the
     * old bucket to a raw new one and adjust it as appropriate,
     * rather than destroying the old one and creating a completely
     * new bucket.
     */
    if (*len > 0) {
        apr_bucket_heap *h;
        /* Change the current bucket to refer to what we read */
        a = apr_bucket_heap_make(a, buf, *len, apr_bucket_free);
        h = a->data;
        h->alloc_len = APR_BUCKET_BUFF_SIZE; /* note the real buffer size */
        *str = buf;
        APR_BUCKET_INSERT_AFTER(a, apr_bucket_pipe_create(p, a->list));
    }
    else {
        apr_bucket_free(buf);
        a = apr_bucket_immortal_make(a, "", 0);
        *str = a->data;
        if (rv == APR_EOF) {
            apr_file_close(p);
        }
    }
    return APR_SUCCESS;
}

APU_DECLARE(apr_bucket *) apr_bucket_pipe_make(apr_bucket *b, apr_file_t *p)
{
    /*
     * A pipe is closed when the end is reached in pipe_bucket_read().  If
     * the pipe isn't read to the end (e.g., error path), the pipe will be
     * closed when its pool goes away.
     *
     * Note that typically the pipe is allocated from the request pool
     * so it will disappear when the request is finished. However the
     * core filter may decide to set aside the tail end of a CGI
     * response if the connection is pipelined. This turns out not to
     * be a problem because the core will have read to the end of the
     * stream so the bucket(s) that it sets aside will be the heap
     * buckets created by pipe_bucket_read() above.
     */
    b->type        = &apr_bucket_type_pipe;
    b->length      = (apr_size_t)(-1);
    b->start       = -1;
    b->data        = p;
    
    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_pipe_create(apr_file_t *p,
                                                 apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_pipe_make(b, p);
}

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_pipe = {
    "PIPE", 5, APR_BUCKET_DATA, 
    apr_bucket_destroy_noop,
    pipe_bucket_read,
    apr_bucket_setaside_notimpl,
    apr_bucket_split_notimpl,
    apr_bucket_copy_notimpl
};
