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

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_simple_copy(apr_bucket *a,
                                                        apr_bucket **b)
{
    *b = apr_bucket_alloc(sizeof(**b), a->list); /* XXX: check for failure? */
    **b = *a;

    return APR_SUCCESS;
}

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_simple_split(apr_bucket *a,
                                                         apr_size_t point)
{
    apr_bucket *b;

    if (point > a->length) {
        return APR_EINVAL;
    }

    apr_bucket_simple_copy(a, &b);

    a->length  = point;
    b->length -= point;
    b->start  += point;

    APR_BUCKET_INSERT_AFTER(a, b);

    return APR_SUCCESS;
}

static apr_status_t simple_bucket_read(apr_bucket *b, const char **str, 
                                       apr_size_t *len, apr_read_type_e block)
{
    *str = (char *)b->data + b->start;
    *len = b->length;
    return APR_SUCCESS;
}

APU_DECLARE(apr_bucket *) apr_bucket_immortal_make(apr_bucket *b,
                                                   const char *buf,
                                                   apr_size_t length)
{
    b->data   = (char *)buf;
    b->length = length;
    b->start  = 0;
    b->type   = &apr_bucket_type_immortal;

    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_immortal_create(const char *buf,
                                                     apr_size_t length,
                                                     apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_immortal_make(b, buf, length);
}

/*
 * XXX: This function could do with some tweaking to reduce memory
 * usage in various cases, e.g. share buffers in the heap between all
 * the buckets that are set aside, or even spool set-aside data to
 * disk if it gets too voluminous (but if it does then that's probably
 * a bug elsewhere). There should probably be a apr_brigade_setaside()
 * function that co-ordinates the action of all the bucket setaside
 * functions to improve memory efficiency.
 */
static apr_status_t transient_bucket_setaside(apr_bucket *b, apr_pool_t *pool)
{
    b = apr_bucket_heap_make(b, (char *)b->data + b->start, b->length, NULL);
    if (b == NULL) {
        return APR_ENOMEM;
    }
    return APR_SUCCESS;
}

APU_DECLARE(apr_bucket *) apr_bucket_transient_make(apr_bucket *b,
                                                    const char *buf,
                                                    apr_size_t length)
{
    b->data   = (char *)buf;
    b->length = length;
    b->start  = 0;
    b->type   = &apr_bucket_type_transient;
    return b;
}

APU_DECLARE(apr_bucket *) apr_bucket_transient_create(const char *buf,
                                                      apr_size_t length,
                                                      apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return apr_bucket_transient_make(b, buf, length);
}

const apr_bucket_type_t apr_bucket_type_immortal = {
    "IMMORTAL", 5, APR_BUCKET_DATA,
    apr_bucket_destroy_noop,
    simple_bucket_read,
    apr_bucket_setaside_noop,
    apr_bucket_simple_split,
    apr_bucket_simple_copy
};

APU_DECLARE_DATA const apr_bucket_type_t apr_bucket_type_transient = {
    "TRANSIENT", 5, APR_BUCKET_DATA,
    apr_bucket_destroy_noop, 
    simple_bucket_read,
    transient_bucket_setaside,
    apr_bucket_simple_split,
    apr_bucket_simple_copy
};
