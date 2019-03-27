/*
 * fnv1a.h :  routines to create checksums derived from FNV-1a
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_SUBR_FNV1A_H
#define SVN_LIBSVN_SUBR_FNV1A_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Opaque FNV-1a checksum creation context type.
 */
typedef struct svn_fnv1a_32__context_t svn_fnv1a_32__context_t;

/* Return a new FNV-1a checksum creation context allocated in POOL.
 */
svn_fnv1a_32__context_t *
svn_fnv1a_32__context_create(apr_pool_t *pool);

/* Reset the FNV-1a checksum CONTEXT to initial state.
 */
void
svn_fnv1a_32__context_reset(svn_fnv1a_32__context_t *context);

/* Feed LEN bytes from DATA into the FNV-1a checksum creation CONTEXT.
 */
void
svn_fnv1a_32__update(svn_fnv1a_32__context_t *context,
                     const void *data,
                     apr_size_t len);

/* Return the FNV-1a checksum over all data fed into  CONTEXT.
 */
apr_uint32_t
svn_fnv1a_32__finalize(svn_fnv1a_32__context_t *context);


/* Opaque modified FNV-1a checksum creation context type.
 */
typedef struct svn_fnv1a_32x4__context_t svn_fnv1a_32x4__context_t;

/* Return a new modified FNV-1a checksum creation context allocated in POOL.
 */
svn_fnv1a_32x4__context_t *
svn_fnv1a_32x4__context_create(apr_pool_t *pool);

/* Reset the modified FNV-1a checksum CONTEXT to initial state.
 */
void
svn_fnv1a_32x4__context_reset(svn_fnv1a_32x4__context_t *context);

/* Feed LEN bytes from DATA into the modified FNV-1a checksum creation
 * CONTEXT.
 */
void
svn_fnv1a_32x4__update(svn_fnv1a_32x4__context_t *context,
                       const void *data,
                       apr_size_t len);

/* Return the modified FNV-1a checksum over all data fed into  CONTEXT.
 */
apr_uint32_t
svn_fnv1a_32x4__finalize(svn_fnv1a_32x4__context_t *context);

/* Set HASHES to the 4 partial hash sums produced by the modified FVN-1a
 * over INPUT of LEN bytes.
 */
void
svn__fnv1a_32x4_raw(apr_uint32_t hashes[4],
                    const void *input,
                    apr_size_t len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_FNV1A_H */
