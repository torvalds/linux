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

#ifndef APR_ATOMIC_H
#define APR_ATOMIC_H

/**
 * @file apr_atomic.h
 * @brief APR Atomic Operations
 */

#include "apr.h"
#include "apr_pools.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup apr_atomic Atomic Operations
 * @ingroup APR 
 * @{
 */

/**
 * this function is required on some platforms to initialize the
 * atomic operation's internal structures
 * @param p pool
 * @return APR_SUCCESS on successful completion
 * @remark Programs do NOT need to call this directly. APR will call this
 *         automatically from apr_initialize.
 * @internal
 */
APR_DECLARE(apr_status_t) apr_atomic_init(apr_pool_t *p);

/*
 * Atomic operations on 32-bit values
 * Note: Each of these functions internally implements a memory barrier
 * on platforms that require it
 */

/**
 * atomically read an apr_uint32_t from memory
 * @param mem the pointer
 */
APR_DECLARE(apr_uint32_t) apr_atomic_read32(volatile apr_uint32_t *mem);

/**
 * atomically set an apr_uint32_t in memory
 * @param mem pointer to the object
 * @param val value that the object will assume
 */
APR_DECLARE(void) apr_atomic_set32(volatile apr_uint32_t *mem, apr_uint32_t val);

/**
 * atomically add 'val' to an apr_uint32_t
 * @param mem pointer to the object
 * @param val amount to add
 * @return old value pointed to by mem
 */
APR_DECLARE(apr_uint32_t) apr_atomic_add32(volatile apr_uint32_t *mem, apr_uint32_t val);

/**
 * atomically subtract 'val' from an apr_uint32_t
 * @param mem pointer to the object
 * @param val amount to subtract
 */
APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val);

/**
 * atomically increment an apr_uint32_t by 1
 * @param mem pointer to the object
 * @return old value pointed to by mem
 */
APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem);

/**
 * atomically decrement an apr_uint32_t by 1
 * @param mem pointer to the atomic value
 * @return zero if the value becomes zero on decrement, otherwise non-zero
 */
APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem);

/**
 * compare an apr_uint32_t's value with 'cmp'.
 * If they are the same swap the value with 'with'
 * @param mem pointer to the value
 * @param with what to swap it with
 * @param cmp the value to compare it to
 * @return the old value of *mem
 */
APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                              apr_uint32_t cmp);

/**
 * exchange an apr_uint32_t's value with 'val'.
 * @param mem pointer to the value
 * @param val what to swap it with
 * @return the old value of *mem
 */
APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val);

/**
 * compare the pointer's value with cmp.
 * If they are the same swap the value with 'with'
 * @param mem pointer to the pointer
 * @param with what to swap it with
 * @param cmp the value to compare it to
 * @return the old value of the pointer
 */
APR_DECLARE(void*) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp);

/**
 * exchange a pair of pointer values
 * @param mem pointer to the pointer
 * @param with what to swap it with
 * @return the old value of the pointer
 */
APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with);

/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* !APR_ATOMIC_H */
