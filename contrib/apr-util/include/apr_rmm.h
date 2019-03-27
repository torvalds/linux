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

#ifndef APR_RMM_H
#define APR_RMM_H
/** 
 * @file apr_rmm.h
 * @brief APR-UTIL Relocatable Memory Management Routines
 */
/**
 * @defgroup APR_Util_RMM Relocatable Memory Management Routines
 * @ingroup APR_Util
 * @{
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"
#include "apu.h"
#include "apr_anylock.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Structure to access Relocatable, Managed Memory */
typedef struct apr_rmm_t apr_rmm_t;

/** Fundamental allocation unit, within a specific apr_rmm_t */
typedef apr_size_t   apr_rmm_off_t;

/**
 * Initialize a relocatable memory block to be managed by the apr_rmm API.
 * @param rmm The relocatable memory block
 * @param lock An apr_anylock_t of the appropriate type of lock, or NULL
 *             if no locking is required.
 * @param membuf The block of relocatable memory to be managed
 * @param memsize The size of relocatable memory block to be managed
 * @param cont The pool to use for local storage and management
 * @remark Both @param membuf and @param memsize must be aligned
 * (for instance using APR_ALIGN_DEFAULT).
 */
APU_DECLARE(apr_status_t) apr_rmm_init(apr_rmm_t **rmm, apr_anylock_t *lock,
                                       void *membuf, apr_size_t memsize, 
                                       apr_pool_t *cont);

/**
 * Destroy a managed memory block.
 * @param rmm The relocatable memory block to destroy
 */
APU_DECLARE(apr_status_t) apr_rmm_destroy(apr_rmm_t *rmm);

/**
 * Attach to a relocatable memory block already managed by the apr_rmm API.
 * @param rmm The relocatable memory block
 * @param lock An apr_anylock_t of the appropriate type of lock
 * @param membuf The block of relocatable memory already under management
 * @param cont The pool to use for local storage and management
 */
APU_DECLARE(apr_status_t) apr_rmm_attach(apr_rmm_t **rmm, apr_anylock_t *lock,
                                         void *membuf, apr_pool_t *cont);

/**
 * Detach from the managed block of memory.
 * @param rmm The relocatable memory block to detach from
 */
APU_DECLARE(apr_status_t) apr_rmm_detach(apr_rmm_t *rmm);

/**
 * Allocate memory from the block of relocatable memory.
 * @param rmm The relocatable memory block
 * @param reqsize How much memory to allocate
 */
APU_DECLARE(apr_rmm_off_t) apr_rmm_malloc(apr_rmm_t *rmm, apr_size_t reqsize);

/**
 * Realloc memory from the block of relocatable memory.
 * @param rmm The relocatable memory block
 * @param entity The memory allocation to realloc
 * @param reqsize The new size
 */
APU_DECLARE(apr_rmm_off_t) apr_rmm_realloc(apr_rmm_t *rmm, void *entity, apr_size_t reqsize);

/**
 * Allocate memory from the block of relocatable memory and initialize it to zero.
 * @param rmm The relocatable memory block
 * @param reqsize How much memory to allocate
 */
APU_DECLARE(apr_rmm_off_t) apr_rmm_calloc(apr_rmm_t *rmm, apr_size_t reqsize);

/**
 * Free allocation returned by apr_rmm_malloc or apr_rmm_calloc.
 * @param rmm The relocatable memory block
 * @param entity The memory allocation to free
 */
APU_DECLARE(apr_status_t) apr_rmm_free(apr_rmm_t *rmm, apr_rmm_off_t entity);

/**
 * Retrieve the physical address of a relocatable allocation of memory
 * @param rmm The relocatable memory block
 * @param entity The memory allocation to free
 * @return address The address, aligned with APR_ALIGN_DEFAULT.
 */
APU_DECLARE(void *) apr_rmm_addr_get(apr_rmm_t *rmm, apr_rmm_off_t entity);

/**
 * Compute the offset of a relocatable allocation of memory
 * @param rmm The relocatable memory block
 * @param entity The physical address to convert to an offset
 */
APU_DECLARE(apr_rmm_off_t) apr_rmm_offset_get(apr_rmm_t *rmm, void *entity);

/**
 * Compute the required overallocation of memory needed to fit n allocs
 * @param n The number of alloc/calloc regions desired
 */
APU_DECLARE(apr_size_t) apr_rmm_overhead_get(int n);

#ifdef __cplusplus
}
#endif
/** @} */
#endif  /* ! APR_RMM_H */

