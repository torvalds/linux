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
    serf_bucket_t *stream;
} barrier_context_t;


serf_bucket_t *serf_bucket_barrier_create(
    serf_bucket_t *stream,
    serf_bucket_alloc_t *allocator)
{
    barrier_context_t *ctx;

    ctx = serf_bucket_mem_alloc(allocator, sizeof(*ctx));
    ctx->stream = stream;

    return serf_bucket_create(&serf_bucket_type_barrier, allocator, ctx);
}

static apr_status_t serf_barrier_read(serf_bucket_t *bucket,
                                     apr_size_t requested,
                                     const char **data, apr_size_t *len)
{
    barrier_context_t *ctx = bucket->data;

    return serf_bucket_read(ctx->stream, requested, data, len);
}

static apr_status_t serf_barrier_read_iovec(serf_bucket_t *bucket,
                                            apr_size_t requested,
                                            int vecs_size, struct iovec *vecs,
                                            int *vecs_used)
{
    barrier_context_t *ctx = bucket->data;

    return serf_bucket_read_iovec(ctx->stream, requested, vecs_size, vecs,
                                  vecs_used);
}

static apr_status_t serf_barrier_readline(serf_bucket_t *bucket,
                                         int acceptable, int *found,
                                         const char **data, apr_size_t *len)
{
    barrier_context_t *ctx = bucket->data;

    return serf_bucket_readline(ctx->stream, acceptable, found, data, len);
}

static apr_status_t serf_barrier_peek(serf_bucket_t *bucket,
                                     const char **data,
                                     apr_size_t *len)
{
    barrier_context_t *ctx = bucket->data;

    return serf_bucket_peek(ctx->stream, data, len);
}

static void serf_barrier_destroy(serf_bucket_t *bucket)
{
    /* The intent of this bucket is not to let our wrapped buckets be
     * destroyed. */

    /* The option is for us to go ahead and 'eat' this bucket now,
     * or just ignore the deletion entirely.
     */
    serf_default_destroy_and_data(bucket);
}

const serf_bucket_type_t serf_bucket_type_barrier = {
    "BARRIER",
    serf_barrier_read,
    serf_barrier_readline,
    serf_barrier_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    serf_barrier_peek,
    serf_barrier_destroy,
};
