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

#include "apr_arch_atomic.h"

#ifdef USE_ATOMICS_BUILTINS

APR_DECLARE(apr_status_t) apr_atomic_init(apr_pool_t *p)
{
    return APR_SUCCESS;
}

APR_DECLARE(apr_uint32_t) apr_atomic_read32(volatile apr_uint32_t *mem)
{
    return *mem;
}

APR_DECLARE(void) apr_atomic_set32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    *mem = val;
}

APR_DECLARE(apr_uint32_t) apr_atomic_add32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    return __sync_fetch_and_add(mem, val);
}

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    __sync_fetch_and_sub(mem, val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    return __sync_fetch_and_add(mem, 1);
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    return __sync_sub_and_fetch(mem, 1);
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    return __sync_val_compare_and_swap(mem, cmp, with);
}

APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    __sync_synchronize();

    return __sync_lock_test_and_set(mem, val);
}

APR_DECLARE(void*) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp)
{
    return (void*) __sync_val_compare_and_swap(mem, cmp, with);
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
    __sync_synchronize();

    return (void*) __sync_lock_test_and_set(mem, with);
}

#endif /* USE_ATOMICS_BUILTINS */
