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

#ifdef USE_ATOMICS_S390

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

static APR_INLINE apr_uint32_t atomic_add(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t prev = *mem, temp;

    asm volatile ("loop_%=:\n"
                  "    lr  %1,%0\n"
                  "    alr %1,%3\n"
                  "    cs  %0,%1,%2\n"
                  "    jl  loop_%=\n"
                  : "+d" (prev), "+d" (temp), "=Q" (*mem)
                  : "d" (val), "m" (*mem)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(apr_uint32_t) apr_atomic_add32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    return atomic_add(mem, val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    return atomic_add(mem, 1);
}

static APR_INLINE apr_uint32_t atomic_sub(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t prev = *mem, temp;

    asm volatile ("loop_%=:\n"
                  "    lr  %1,%0\n"
                  "    slr %1,%3\n"
                  "    cs  %0,%1,%2\n"
                  "    jl  loop_%=\n"
                  : "+d" (prev), "+d" (temp), "=Q" (*mem)
                  : "d" (val), "m" (*mem)
                  : "cc", "memory");

    return temp;
}

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    atomic_sub(mem, val);
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    return atomic_sub(mem, 1);
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    asm volatile ("    cs  %0,%2,%1\n"
                  : "+d" (cmp), "=Q" (*mem)
                  : "d" (with), "m" (*mem)
                  : "cc", "memory");

    return cmp;
}

APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t prev = *mem;

    asm volatile ("loop_%=:\n"
                  "    cs  %0,%2,%1\n"
                  "    jl  loop_%=\n"
                  : "+d" (prev), "=Q" (*mem)
                  : "d" (val), "m" (*mem)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(void*) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp)
{
    void *prev = (void *) cmp;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("    cs  %0,%2,%1\n"
                  : "+d" (prev), "=Q" (*mem)
                  : "d" (with), "m" (*mem)
                  : "cc", "memory");
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("    csg %0,%2,%1\n"
                  : "+d" (prev), "=Q" (*mem)
                  : "d" (with), "m" (*mem)
                  : "cc", "memory");
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
    void *prev = (void *) *mem;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("loop_%=:\n"
                  "    cs  %0,%2,%1\n"
                  "    jl  loop_%=\n"
                  : "+d" (prev), "=Q" (*mem)
                  : "d" (with), "m" (*mem)
                  : "cc", "memory");
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("loop_%=:\n"
                  "    csg %0,%2,%1\n"
                  "    jl  loop_%=\n"
                  : "+d" (prev), "=Q" (*mem)
                  : "d" (with), "m" (*mem)
                  : "cc", "memory");
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

#endif /* USE_ATOMICS_S390 */
