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
#include <apr_mmap.h>

#include "serf.h"
#include "serf_bucket_util.h"

#if APR_HAS_MMAP

typedef struct {
    apr_mmap_t *mmap;
    void *current;
    apr_off_t offset;
    apr_off_t remaining;
} mmap_context_t;


serf_bucket_t *serf_bucket_mmap_create(
    apr_mmap_t *file_mmap,
    serf_bucket_alloc_t *allocator)
{
    mmap_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->mmap = file_mmap;
    ctx->current = NULL;
    ctx->offset = 0;
    ctx->remaining = ctx->mmap->size;

    return serf_bucket_create(&serf_bucket_type_mmap, allocator, ctx);
}

static apr_status_t serf_mmap_read(serf_bucket_t *bucket,
                                     apr_size_t requested,
                                     const char **data, apr_size_t *len)
{
    mmap_context_t *ctx = bucket->data;

    if (requested == SERF_READ_ALL_AVAIL || requested > ctx->remaining) {
        *len = ctx->remaining;
    }
    else {
        *len = requested;
    }

    /* ### Would it be faster to call this once and do the offset ourselves? */
    apr_mmap_offset((void**)data, ctx->mmap, ctx->offset);

    /* For the next read... */
    ctx->offset += *len;
    ctx->remaining -= *len;

    if (ctx->remaining == 0) {
        return APR_EOF;
    }
    return APR_SUCCESS;
}

static apr_status_t serf_mmap_readline(serf_bucket_t *bucket,
                                         int acceptable, int *found,
                                         const char **data, apr_size_t *len)
{
    mmap_context_t *ctx = bucket->data;
    const char *end;

    /* ### Would it be faster to call this once and do the offset ourselves? */
    apr_mmap_offset((void**)data, ctx->mmap, ctx->offset);
    end = *data;

    /* XXX An overflow is generated if we pass &ctx->remaining to readline.
     * Not real clear why.
     */
    *len = ctx->remaining;

    serf_util_readline(&end, len, acceptable, found);

    *len = end - *data;

    ctx->offset += *len;
    ctx->remaining -= *len;

    if (ctx->remaining == 0) {
        return APR_EOF;
    }
    return APR_SUCCESS;
}

static apr_status_t serf_mmap_peek(serf_bucket_t *bucket,
                                     const char **data,
                                     apr_size_t *len)
{
    /* Oh, bah. */
    return APR_ENOTIMPL;
}

const serf_bucket_type_t serf_bucket_type_mmap = {
    "MMAP",
    serf_mmap_read,
    serf_mmap_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_mmap_peek,
    serf_default_destroy_and_data,
};

#else /* !APR_HAS_MMAP */

serf_bucket_t *serf_bucket_mmap_create(apr_mmap_t *file_mmap,
                                       serf_bucket_alloc_t *allocator)
{
    return NULL;
}

const serf_bucket_type_t serf_bucket_type_mmap = {
    "MMAP",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

#endif
