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

#ifndef APR_SHM_H
#define APR_SHM_H

/**
 * @file apr_shm.h
 * @brief APR Shared Memory Routines
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_shm Shared Memory Routines
 * @ingroup APR 
 * @{
 */

/**
 * Private, platform-specific data struture representing a shared memory
 * segment.
 */
typedef struct apr_shm_t apr_shm_t;

/**
 * Create and make accessible a shared memory segment with default
 * properties.
 * @param m The shared memory structure to create.
 * @param reqsize The desired size of the segment.
 * @param filename The file to use for shared memory on platforms that
 *        require it.
 * @param pool the pool from which to allocate the shared memory
 *        structure.
 * @remark A note about Anonymous vs. Named shared memory segments:
 *         Not all plaforms support anonymous shared memory segments, but in
 *         some cases it is prefered over other types of shared memory
 *         implementations. Passing a NULL 'file' parameter to this function
 *         will cause the subsystem to use anonymous shared memory segments.
 *         If such a system is not available, APR_ENOTIMPL is returned.
 * @remark A note about allocation sizes:
 *         On some platforms it is necessary to store some metainformation
 *         about the segment within the actual segment. In order to supply
 *         the caller with the requested size it may be necessary for the
 *         implementation to request a slightly greater segment length
 *         from the subsystem. In all cases, the apr_shm_baseaddr_get()
 *         function will return the first usable byte of memory.
 * 
 */
APR_DECLARE(apr_status_t) apr_shm_create(apr_shm_t **m,
                                         apr_size_t reqsize,
                                         const char *filename,
                                         apr_pool_t *pool);

/**
 * Special processing flags for apr_shm_create_ex() and apr_shm_attach_ex().
 */
#define APR_SHM_NS_LOCAL    1 /* Create or attach to named shared memory
                               * segment in the "Local" namespace on
                               * Windows.  (Ignored on other platforms.)
                               * By default, the "Global" namespace is
                               * used for privileged processes and the
                               * "Local" namespace is used otherwise.
                               */
#define APR_SHM_NS_GLOBAL   2 /* Create or attach to named shared memory
                               * segment in the "Global" namespace on
                               * Windows.  (Ignored on other platforms.)
                               */

/**
 * Create and make accessible a shared memory segment with platform-
 * specific processing.
 * @param m The shared memory structure to create.
 * @param reqsize The desired size of the segment.
 * @param filename The file to use for shared memory on platforms that
 *        require it.
 * @param pool the pool from which to allocate the shared memory
 *        structure.
 * @param flags mask of APR_SHM_* (defined above)
 * @remark A note about Anonymous vs. Named shared memory segments:
 *         Not all plaforms support anonymous shared memory segments, but in
 *         some cases it is prefered over other types of shared memory
 *         implementations. Passing a NULL 'file' parameter to this function
 *         will cause the subsystem to use anonymous shared memory segments.
 *         If such a system is not available, APR_ENOTIMPL is returned.
 * @remark A note about allocation sizes:
 *         On some platforms it is necessary to store some metainformation
 *         about the segment within the actual segment. In order to supply
 *         the caller with the requested size it may be necessary for the
 *         implementation to request a slightly greater segment length
 *         from the subsystem. In all cases, the apr_shm_baseaddr_get()
 *         function will return the first usable byte of memory.
 * 
 */
APR_DECLARE(apr_status_t) apr_shm_create_ex(apr_shm_t **m,
                                            apr_size_t reqsize,
                                            const char *filename,
                                            apr_pool_t *pool,
                                            apr_int32_t flags);

/**
 * Remove named resource associated with a shared memory segment,
 * preventing attachments to the resource, but not destroying it.
 * @param filename The filename associated with shared-memory segment which
 *        needs to be removed
 * @param pool The pool used for file operations
 * @remark This function is only supported on platforms which support
 * name-based shared memory segments, and will return APR_ENOTIMPL on
 * platforms without such support.  Removing the file while the shm
 * is in use is not entirely portable, caller may use this to enhance
 * obscurity of the resource, but be prepared for the call to fail,
 * and for concurrent attempts to create a resource of the same name
 * to also fail.  The pool cleanup of apr_shm_create (apr_shm_destroy)
 * also removes the named resource.
 */
APR_DECLARE(apr_status_t) apr_shm_remove(const char *filename,
                                         apr_pool_t *pool);

/**
 * Destroy a shared memory segment and associated memory.
 * @param m The shared memory segment structure to destroy.
 */
APR_DECLARE(apr_status_t) apr_shm_destroy(apr_shm_t *m);

/**
 * Attach to a shared memory segment that was created
 * by another process.
 * @param m The shared memory structure to create.
 * @param filename The file used to create the original segment.
 *        (This MUST match the original filename.)
 * @param pool the pool from which to allocate the shared memory
 *        structure for this process.
 */
APR_DECLARE(apr_status_t) apr_shm_attach(apr_shm_t **m,
                                         const char *filename,
                                         apr_pool_t *pool);

/**
 * Attach to a shared memory segment that was created
 * by another process, with platform-specific processing.
 * @param m The shared memory structure to create.
 * @param filename The file used to create the original segment.
 *        (This MUST match the original filename.)
 * @param pool the pool from which to allocate the shared memory
 *        structure for this process.
 * @param flags mask of APR_SHM_* (defined above)
 */
APR_DECLARE(apr_status_t) apr_shm_attach_ex(apr_shm_t **m,
                                            const char *filename,
                                            apr_pool_t *pool,
                                            apr_int32_t flags);

/**
 * Detach from a shared memory segment without destroying it.
 * @param m The shared memory structure representing the segment
 *        to detach from.
 */
APR_DECLARE(apr_status_t) apr_shm_detach(apr_shm_t *m);

/**
 * Retrieve the base address of the shared memory segment.
 * NOTE: This address is only usable within the callers address
 * space, since this API does not guarantee that other attaching
 * processes will maintain the same address mapping.
 * @param m The shared memory segment from which to retrieve
 *        the base address.
 * @return address, aligned by APR_ALIGN_DEFAULT.
 */
APR_DECLARE(void *) apr_shm_baseaddr_get(const apr_shm_t *m);

/**
 * Retrieve the length of a shared memory segment in bytes.
 * @param m The shared memory segment from which to retrieve
 *        the segment length.
 */
APR_DECLARE(apr_size_t) apr_shm_size_get(const apr_shm_t *m);

/**
 * Get the pool used by this shared memory segment.
 */
APR_POOL_DECLARE_ACCESSOR(shm);

/** @} */ 

#ifdef __cplusplus
}
#endif

#endif  /* APR_SHM_T */
