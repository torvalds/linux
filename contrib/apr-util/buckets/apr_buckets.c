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

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_setaside_noop(apr_bucket *data,
                                                          apr_pool_t *pool)
{
    return APR_SUCCESS;
}

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_setaside_notimpl(apr_bucket *data,
                                                             apr_pool_t *pool)
{
    return APR_ENOTIMPL;
}

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_split_notimpl(apr_bucket *data,
                                                          apr_size_t point)
{
    return APR_ENOTIMPL;
}

APU_DECLARE_NONSTD(apr_status_t) apr_bucket_copy_notimpl(apr_bucket *e,
                                                         apr_bucket **c)
{
    return APR_ENOTIMPL;
}

APU_DECLARE_NONSTD(void) apr_bucket_destroy_noop(void *data)
{
    return;
}
