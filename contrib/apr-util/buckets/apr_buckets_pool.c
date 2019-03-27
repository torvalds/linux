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
#define APR_WANT_MEMFUNC
#include "apr_want.h"

static apr_status_t pool_bucket_cleanup(void *data)
{
    apr_bucket_pool *p = data;

    /*
     * If the pool gets cleaned up, we have to copy the data out
     * of the pool and onto the heap.  But the apr_buckets out there
     * that point to this pool bucket need to be notified such that
     * they can morph themselves into a regular heap bucket the next
     * time they try to read.  To avoid having to manipulate
     * reference counts and b->data pointers, the apr_bucket_pool
     * actually _contains_ an apr_bucket_heap as its first element,
     * so the two share their apr_bucket_refcount member, and you
     * can typecast a pool bucket struct to make it look like a
     * regular old heap bucket struct.
     */
    p->heap.base = apr_bucket_alloc(p->heap.alloc_len, p->list);
    memcpy(p->heap.base, p->base, p->heap.alloc_len);
    p->base = NULL;
    p->pool = NULL;

    return APR_SUCCESS;
}

static apr_status_t pool_bucket_read(apr_bucket *b, const char **str, 
                                     apr_size_t *len, apr_read_type_e block)
{
    apr_bucket_pool *p = b->data;
    const char *base = p->base;

    if (p->pool == NULL) {
        /*
         * pool has been cleaned up... masquerade as a heap bucket from now
         * on. subsequent bucket operations will use the heap bucket code.
         */
        b->type = &apr_bucket_type_heap;
        base = p->heap.base;
    }
    *str = base + b->start;
    *len = b->length;
    return APR_SUCCESS;
}

static void pool_bucket_destroy(void *data)
{
    apr_bucket_pool *p = data;

    /* If the pool is cleaned up before the last reference goes
     * away, the data is really now on the heap; heap_destroy() takes
     * over.  free() in heap_destroy() thinks it's freeing
     * an apr_bucket_heap, when in reality it's freeing the whole
     * apr_bucket_pool for us.
     */
    if (p->pool) {
        /* the shared resource is still in the pool
         * because the pool has not been cleaned up yet
         */
        if (apr_bucket_shared_destroy(p)) {
            apr_pool_cleanup_kill(p->pool, p, pool_bucket_cleanup);
            apr_bucket_free(p);
        }
    }
    else {
        /* the shared resource is no longer in the pool, it's
         * on the heap, but this reference still thinks it's a pool
         * bucket.  we should just go ahead and pass control to
         * heap_destroy() for it since it doesn't know any better.
         */
        apr_bucket_type_heap.destroy(p);
    }
}

APU_DECLARE(apr_bucket *) apr_bucket_pool_make(apr_bucket *b,
                      const char *buf, apr_size_t length, apr_pool_t *pool)
{
    apr_bucket_pool *p;

    p = apr_bucket_alloc(sizeof(*p), b->list);

    /* XXX: we lose the const qualifier here which indicates
     * there's something screwy with the API...
     */
    /* XXX: why is this?  buf is const, p->base is const... what's
     * the problem?  --jcw */
    p->base = (char *) buf;
    p->pool = pool;
    p->list = b->list;

    b = apr_bucket_shared_make(b, p, 0, length);
    b->type = &apr_bucket_type_pool;

    /* pre-initialize heap bucket member */
    p->heap.alloc_len = length;
    p->heap.base      = NULL;
    p->heap.free_func = apr_bucket_free;

    apr_pool_cleanup_register(p->pool, p, pool_bucket_cleanup,
                              apr_pool_cleanup_null);
    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_pool_create(const char *buf,
                                                 apr_size_t length,
                                                 apr_pool_t *pool,
                                                 apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_pool_make(b, buf, length, pool);
}

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_pool = {
    "POOL", 5, APR_BUCKET_DATA,
    pool_bucket_destroy,
    pool_bucket_read,
    apr_bucket_setaside_noop, /* don't need to setaside thanks to the cleanup*/
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};
