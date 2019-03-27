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

#include <stdlib.h>

#include "apr_buckets.h"
#include "apr_allocator.h"

#define ALLOC_AMT (8192 - APR_MEMNODE_T_SIZE)

typedef struct node_header_t {
    apr_size_t size;
    apr_bucket_alloc_t *alloc;
    apr_memnode_t *memnode;
    struct node_header_t *next;
} node_header_t;

#define SIZEOF_NODE_HEADER_T  APR_ALIGN_DEFAULT(sizeof(node_header_t))
#define SMALL_NODE_SIZE       (APR_BUCKET_ALLOC_SIZE + SIZEOF_NODE_HEADER_T)

/** A list of free memory from which new buckets or private bucket
 *  structures can be allocated.
 */
struct apr_bucket_alloc_t {
    apr_pool_t *pool;
    apr_allocator_t *allocator;
    node_header_t *freelist;
    apr_memnode_t *blocks;
};

static apr_status_t alloc_cleanup(void *data)
{
    apr_bucket_alloc_t *list = data;

    apr_allocator_free(list->allocator, list->blocks);

#if APR_POOL_DEBUG
    if (list->pool && list->allocator != apr_pool_allocator_get(list->pool)) {
        apr_allocator_destroy(list->allocator);
    }
#endif

    return APR_SUCCESS;
}

APU_DECLARE_NONSTD(apr_bucket_alloc_t *) apr_bucket_alloc_create(apr_pool_t *p)
{
    apr_allocator_t *allocator = apr_pool_allocator_get(p);
    apr_bucket_alloc_t *list;

#if APR_POOL_DEBUG
    /* may be NULL for debug mode. */
    if (allocator == NULL) {
        if (apr_allocator_create(&allocator) != APR_SUCCESS) {
            apr_abortfunc_t fn = apr_pool_abort_get(p);
            if (fn)
                (fn)(APR_ENOMEM);
            abort();
        }
    }
#endif
    list = apr_bucket_alloc_create_ex(allocator);
    if (list == NULL) {
            apr_abortfunc_t fn = apr_pool_abort_get(p);
            if (fn)
                (fn)(APR_ENOMEM);
            abort();
    }
    list->pool = p;
    apr_pool_cleanup_register(list->pool, list, alloc_cleanup,
                              apr_pool_cleanup_null);

    return list;
}

APU_DECLARE_NONSTD(apr_bucket_alloc_t *) apr_bucket_alloc_create_ex(
                                             apr_allocator_t *allocator)
{
    apr_bucket_alloc_t *list;
    apr_memnode_t *block;

    block = apr_allocator_alloc(allocator, ALLOC_AMT);
    if (!block) {
        return NULL;
    }
    list = (apr_bucket_alloc_t *)block->first_avail;
    list->pool = NULL;
    list->allocator = allocator;
    list->freelist = NULL;
    list->blocks = block;
    block->first_avail += APR_ALIGN_DEFAULT(sizeof(*list));

    return list;
}

APU_DECLARE_NONSTD(void) apr_bucket_alloc_destroy(apr_bucket_alloc_t *list)
{
    if (list->pool) {
        apr_pool_cleanup_kill(list->pool, list, alloc_cleanup);
    }

    apr_allocator_free(list->allocator, list->blocks);

#if APR_POOL_DEBUG
    if (list->pool && list->allocator != apr_pool_allocator_get(list->pool)) {
        apr_allocator_destroy(list->allocator);
    }
#endif
}

APU_DECLARE_NONSTD(void *) apr_bucket_alloc(apr_size_t size, 
                                            apr_bucket_alloc_t *list)
{
    node_header_t *node;
    apr_memnode_t *active = list->blocks;
    char *endp;

    size += SIZEOF_NODE_HEADER_T;
    if (size <= SMALL_NODE_SIZE) {
        if (list->freelist) {
            node = list->freelist;
            list->freelist = node->next;
        }
        else {
            endp = active->first_avail + SMALL_NODE_SIZE;
            if (endp >= active->endp) {
                list->blocks = apr_allocator_alloc(list->allocator, ALLOC_AMT);
                if (!list->blocks) {
                    list->blocks = active;
                    return NULL;
                }
                list->blocks->next = active;
                active = list->blocks;
                endp = active->first_avail + SMALL_NODE_SIZE;
            }
            node = (node_header_t *)active->first_avail;
            node->alloc = list;
            node->memnode = active;
            node->size = SMALL_NODE_SIZE;
            active->first_avail = endp;
        }
    }
    else {
        apr_memnode_t *memnode = apr_allocator_alloc(list->allocator, size);
        if (!memnode) {
            return NULL;
        }
        node = (node_header_t *)memnode->first_avail;
        node->alloc = list;
        node->memnode = memnode;
        node->size = size;
    }
    return ((char *)node) + SIZEOF_NODE_HEADER_T;
}

#ifdef APR_BUCKET_DEBUG
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
static void check_not_already_free(node_header_t *node)
{
    apr_bucket_alloc_t *list = node->alloc;
    node_header_t *curr = list->freelist;

    while (curr) {
        if (node == curr) {
            abort();
        }
        curr = curr->next;
    }
}
#else
#define check_not_already_free(node)
#endif

APU_DECLARE_NONSTD(void) apr_bucket_free(void *mem)
{
    node_header_t *node = (node_header_t *)((char *)mem - SIZEOF_NODE_HEADER_T);
    apr_bucket_alloc_t *list = node->alloc;

    if (node->size == SMALL_NODE_SIZE) {
        check_not_already_free(node);
        node->next = list->freelist;
        list->freelist = node;
    }
    else {
        apr_allocator_free(list->allocator, node->memnode);
    }
}
