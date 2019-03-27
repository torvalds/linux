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

#ifdef USE_ATOMICS_IA32

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
    asm volatile ("lock; xaddl %0,%1"
                  : "=r" (val), "=m" (*mem)
                  : "0" (val), "m" (*mem)
                  : "memory", "cc");
    return val;
}

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    asm volatile ("lock; subl %1, %0"
                  : /* no output */
                  : "m" (*(mem)), "r" (val)
                  : "memory", "cc");
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    return apr_atomic_add32(mem, 1);
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    unsigned char prev;

    asm volatile ("lock; decl %0; setnz %1"
                  : "=m" (*mem), "=qm" (prev)
                  : "m" (*mem)
                  : "memory");

    return prev;
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    apr_uint32_t prev;

    asm volatile ("lock; cmpxchgl %1, %2"
                  : "=a" (prev)
                  : "r" (with), "m" (*(mem)), "0"(cmp)
                  : "memory", "cc");
    return prev;
}

APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t prev = val;

    asm volatile ("xchgl %0, %1"
                  : "=r" (prev), "+m" (*mem)
                  : "0" (prev));
    return prev;
}

APR_DECLARE(void*) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp)
{
    void *prev;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("lock; cmpxchgl %2, %1"
                  : "=a" (prev), "=m" (*mem)
                  : "r" (with), "m" (*mem), "0" (cmp));
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("lock; cmpxchgq %q2, %1"
                  : "=a" (prev), "=m" (*mem)
                  : "r" ((unsigned long)with), "m" (*mem),
                    "0" ((unsigned long)cmp));
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
    void *prev;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("xchgl %2, %1"
                  : "=a" (prev), "+m" (*mem)
                  : "0" (with));
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("xchgq %q2, %1"
                  : "=a" (prev), "+m" (*mem)
                  : "0" (with));
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

#endif /* USE_ATOMICS_IA32 */
