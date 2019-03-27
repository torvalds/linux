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

typedef struct {
    apr_file_t *file;

    serf_databuf_t databuf;

} file_context_t;


static apr_status_t file_reader(void *baton, apr_size_t bufsize,
                                char *buf, apr_size_t *len)
{
    file_context_t *ctx = baton;

    *len = bufsize;
    return apr_file_read(ctx->file, buf, len);
}

serf_bucket_t *serf_bucket_file_create(
    apr_file_t *file,
    serf_bucket_alloc_t *allocator)
{
    file_context_t *ctx;
#if APR_HAS_MMAP
    apr_finfo_t finfo;
    const char *file_path;

    /* See if we'd be better off mmap'ing this file instead.
     *
     * Note that there is a failure case here that we purposely fall through:
     * if a file is buffered, apr_mmap will reject it.  However, on older
     * versions of APR, we have no way of knowing this - but apr_mmap_create
     * will check for this and return APR_EBADF.
     */
    apr_file_name_get(&file_path, file);
    apr_stat(&finfo, file_path, APR_FINFO_SIZE,
             serf_bucket_allocator_get_pool(allocator));
    if (APR_MMAP_CANDIDATE(finfo.size)) {
        apr_status_t status;
        apr_mmap_t *file_mmap;
        status = apr_mmap_create(&file_mmap, file, 0, finfo.size,
                                 APR_MMAP_READ,
                                 serf_bucket_allocator_get_pool(allocator));

        if (status == APR_SUCCESS) {
            return serf_bucket_mmap_create(file_mmap, allocator);
        }
    }
#endif

    /* Oh, well. */
    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->file = file;

    serf_databuf_init(&ctx->databuf);
    ctx->databuf.read = file_reader;
    ctx->databuf.read_baton = ctx;

    return serf_bucket_create(&serf_bucket_type_file, allocator, ctx);
}

static apr_status_t serf_file_read(serf_bucket_t *bucket,
                                   apr_size_t requested,
                                   const char **data, apr_size_t *len)
{
    file_context_t *ctx = bucket->data;

    return serf_databuf_read(&ctx->databuf, requested, data, len);
}

static apr_status_t serf_file_readline(serf_bucket_t *bucket,
                                       int acceptable, int *found,
                                       const char **data, apr_size_t *len)
{
    file_context_t *ctx = bucket->data;

    return serf_databuf_readline(&ctx->databuf, acceptable, found, data, len);
}

static apr_status_t serf_file_peek(serf_bucket_t *bucket,
                                   const char **data,
                                   apr_size_t *len)
{
    file_context_t *ctx = bucket->data;

    return serf_databuf_peek(&ctx->databuf, data, len);
}

const serf_bucket_type_t serf_bucket_type_file = {
    "FILE",
    serf_file_read,
    serf_file_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_file_peek,
    serf_default_destroy_and_data,
};
