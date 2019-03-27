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

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_shared_split(apr_bucket *a,
                                                         apr_size_t point)
{
    apr_bucket_refcount *r = a->data;
    apr_status_t rv;

    if ((rv = apr_bucket_simple_split(a, point)) != APR_SUCCESS) {
        return rv;
    }
    r->refcount++;

    return APR_SUCCESS;
}

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_shared_copy(apr_bucket *a,
                                                        apr_bucket **b)
{
    apr_bucket_refcount *r = a->data;

    apr_bucket_simple_copy(a, b);
    r->refcount++;

    return APR_SUCCESS;
}

APU_DECLARE(int) apr_bucket_shared_destroy(void *data)
{
    apr_bucket_refcount *r = data;
    r->refcount--;
    return (r->refcount == 0);
}

APU_DECLARE(apr_bucket *) apr_bucket_shared_make(apr_bucket *b, void *data,
                                                 apr_off_t start,
                                                 apr_size_t length)
{
    apr_bucket_refcount *r = data;

    b->data   = r;
    b->start  = start;
    b->length = length;
    /* caller initializes the type field */
    r->refcount = 1;

    return b;
}
