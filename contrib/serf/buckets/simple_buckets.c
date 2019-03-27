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
    const char *original;
    const char *current;
    apr_size_t remaining;

    serf_simple_freefunc_t freefunc;
    void *baton;

} simple_context_t;


static void free_copied_data(void *baton, const char *data)
{
    serf_bucket_mem_free(baton, (char*)data);
}

serf_bucket_t *serf_bucket_simple_create(
    const char *data,
    apr_size_t len,
    serf_simple_freefunc_t freefunc,
    void *freefunc_baton,
    serf_bucket_alloc_t *allocator)
{
    simple_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->original = ctx->current = data;
    ctx->remaining = len;
    ctx->freefunc = freefunc;
    ctx->baton = freefunc_baton;

    return serf_bucket_create(&serf_bucket_type_simple, allocator, ctx);
}

serf_bucket_t *serf_bucket_simple_copy_create(
    const char *data, apr_size_t len,
    serf_bucket_alloc_t *allocator)
{
    simple_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));

    ctx->original = ctx->current = serf_bucket_mem_alloc(allocator, len);
    memcpy((char*)ctx->original, data, len);

    ctx->remaining = len;
    ctx->freefunc = free_copied_data;
    ctx->baton = allocator;

    return serf_bucket_create(&serf_bucket_type_simple, allocator, ctx);
}

serf_bucket_t *serf_bucket_simple_own_create(
    const char *data, apr_size_t len,
    serf_bucket_alloc_t *allocator)
{
    simple_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));

    ctx->original = ctx->current = data;

    ctx->remaining = len;
    ctx->freefunc = free_copied_data;
    ctx->baton = allocator;

    return serf_bucket_create(&serf_bucket_type_simple, allocator, ctx);
}

static apr_status_t serf_simple_read(serf_bucket_t *bucket,
                                     apr_size_t requested,
                                     const char **data, apr_size_t *len)
{
    simple_context_t *ctx = bucket->data;

    if (requested == SERF_READ_ALL_AVAIL || requested > ctx->remaining)
        requested = ctx->remaining;

    *data = ctx->current;
    *len = requested;

    ctx->current += requested;
    ctx->remaining -= requested;

    return ctx->remaining ? APR_SUCCESS : APR_EOF;
}

static apr_status_t serf_simple_readline(serf_bucket_t *bucket,
                                         int acceptable, int *found,
                                         const char **data, apr_size_t *len)
{
    simple_context_t *ctx = bucket->data;

    /* Returned data will be from current position. */
    *data = ctx->current;
    serf_util_readline(&ctx->current, &ctx->remaining, acceptable, found);

    /* See how much ctx->current moved forward. */
    *len = ctx->current - *data;

    return ctx->remaining ? APR_SUCCESS : APR_EOF;
}

static apr_status_t serf_simple_peek(serf_bucket_t *bucket,
                                     const char **data,
                                     apr_size_t *len)
{
    simple_context_t *ctx = bucket->data;

    /* return whatever we have left */
    *data = ctx->current;
    *len = ctx->remaining;

    /* we returned everything this bucket will ever hold */
    return APR_EOF;
}

static void serf_simple_destroy(serf_bucket_t *bucket)
{
    simple_context_t *ctx = bucket->data;

    if (ctx->freefunc)
        (*ctx->freefunc)(ctx->baton, ctx->original);

    serf_default_destroy_and_data(bucket);
}


const serf_bucket_type_t serf_bucket_type_simple = {
    "SIMPLE",
    serf_simple_read,
    serf_simple_readline,
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_simple_peek,
    serf_simple_destroy,
};
