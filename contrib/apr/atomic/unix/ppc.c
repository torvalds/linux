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

#ifdef USE_ATOMICS_PPC

#ifdef PPC405_ERRATA
#   define PPC405_ERR77_SYNC   "    sync\n"
#else
#   define PPC405_ERR77_SYNC
#endif

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
    apr_uint32_t prev, temp;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%3\n"      /* load and reserve     */
                  "    add     %1,%0,%4\n"     /* add val and prev     */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %1,0,%3\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  : "=&r" (prev), "=&r" (temp), "=m" (*mem)
                  : "b" (mem), "r" (val)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t temp;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%2\n"      /* load and reserve     */
                  "    subf    %0,%3,%0\n"     /* subtract val         */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %0,0,%2\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  : "=&r" (temp), "=m" (*mem)
                  : "b" (mem), "r" (val)
                  : "cc", "memory");
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    apr_uint32_t prev;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%2\n"      /* load and reserve     */
                  "    addi    %0,%0,1\n"      /* add immediate        */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %0,0,%2\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "    subi    %0,%0,1\n"      /* return old value     */
                  : "=&b" (prev), "=m" (*mem)
                  : "b" (mem), "m" (*mem)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    apr_uint32_t prev;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%2\n"      /* load and reserve     */
                  "    subi    %0,%0,1\n"      /* subtract immediate   */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %0,0,%2\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  : "=&b" (prev), "=m" (*mem)
                  : "b" (mem), "m" (*mem)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    apr_uint32_t prev;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%1\n"      /* load and reserve     */
                  "    cmpw    %0,%3\n"        /* compare operands     */
                  "    bne-    exit_%=\n"      /* skip if not equal    */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "exit_%=:\n"                 /* not equal            */
                  : "=&r" (prev)
                  : "b" (mem), "r" (with), "r" (cmp)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    apr_uint32_t prev;

    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%1\n"      /* load and reserve     */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b"             /* loop if lost         */
                  : "=&r" (prev)
                  : "b" (mem), "r" (val)
                  : "cc", "memory");

    return prev;
}

APR_DECLARE(void*) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp)
{
    void *prev;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%1\n"      /* load and reserve     */
                  "    cmpw    %0,%3\n"        /* compare operands     */
                  "    bne-    2f\n"           /* skip if not equal    */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "2:\n"                       /* not equal            */
                  : "=&r" (prev)
                  : "b" (mem), "r" (with), "r" (cmp)
                  : "cc", "memory");
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("1:\n"                       /* lost reservation     */
                  "    ldarx   %0,0,%1\n"      /* load and reserve     */
                  "    cmpd    %0,%3\n"        /* compare operands     */
                  "    bne-    2f\n"           /* skip if not equal    */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stdcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "2:\n"                       /* not equal            */
                  : "=&r" (prev)
                  : "b" (mem), "r" (with), "r" (cmp)
                  : "cc", "memory");
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
    void *prev;
#if APR_SIZEOF_VOIDP == 4
    asm volatile ("1:\n"                       /* lost reservation     */
                  "    lwarx   %0,0,%1\n"      /* load and reserve     */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stwcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "    isync\n"                /* memory barrier       */
                  : "=&r" (prev)
                  : "b" (mem), "r" (with)
                  : "cc", "memory");
#elif APR_SIZEOF_VOIDP == 8
    asm volatile ("1:\n"                       /* lost reservation     */
                  "    ldarx   %0,0,%1\n"      /* load and reserve     */
                  PPC405_ERR77_SYNC            /* ppc405 Erratum 77    */
                  "    stdcx.  %2,0,%1\n"      /* store new value      */
                  "    bne-    1b\n"           /* loop if lost         */
                  "    isync\n"                /* memory barrier       */
                  : "=&r" (prev)
                  : "b" (mem), "r" (with)
                  : "cc", "memory");
#else
#error APR_SIZEOF_VOIDP value not supported
#endif
    return prev;
}

#endif /* USE_ATOMICS_PPC */
