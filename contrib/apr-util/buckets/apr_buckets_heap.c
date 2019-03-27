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

static apr_status_t heap_bucket_read(apr_bucket *b, const char **str, 
                                     apr_size_t *len, apr_read_type_e block)
{
    apr_bucket_heap *h = b->data;

    *str = h->base + b->start;
    *len = b->length;
    return APR_SUCCESS;
}

static void heap_bucket_destroy(void *data)
{
    apr_bucket_heap *h = data;

    if (apr_bucket_shared_destroy(h)) {
        (*h->free_func)(h->base);
        apr_bucket_free(h);
    }
}

/* Warning: if you change this function, be sure to
 * change apr_bucket_pool_make() too! */
APU_DECLARE(apr_bucket *) apr_bucket_heap_make(apr_bucket *b, const char *buf,
                                               apr_size_t length,
                                               void (*free_func)(void *data))
{
    apr_bucket_heap *h;

    h = apr_bucket_alloc(sizeof(*h), b->list);

    if (!free_func) {
        h->alloc_len = length;
        h->base = apr_bucket_alloc(h->alloc_len, b->list);
        if (h->base == NULL) {
            apr_bucket_free(h);
            return NULL;
        }
        h->free_func = apr_bucket_free;
        memcpy(h->base, buf, length);
    }
    else {
        /* XXX: we lose the const qualifier here which indicates
         * there's something screwy with the API...
         */
        h->base = (char *) buf;
        h->alloc_len = length;
        h->free_func = free_func;
    }

    b = apr_bucket_shared_make(b, h, 0, length);
    b->type = &apr_bucket_type_heap;

    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_heap_create(const char *buf,
                                                 apr_size_t length,
                                                 void (*free_func)(void *data),
                                                 apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_heap_make(b, buf, length, free_func);
}

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_heap = {
    "HEAP", 5, APR_BUCKET_DATA,
    heap_bucket_destroy,
    heap_bucket_read,
    apr_bucket_setaside_noop,
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};
