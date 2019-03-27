/*
 * blncache.h: DAV baseline information cache.
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

#ifndef SVN_LIBSVN_RA_SERF_BLNCACHE_H
#define SVN_LIBSVN_RA_SERF_BLNCACHE_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Baseline information cache. Baseline information cache holds information
 * about DAV baseline (bln):
 * 1. URL of the baseline (bln)
 * 2. Revision number associated with baseline
 * 3. URL of baseline collection (bc).
 */
typedef struct svn_ra_serf__blncache_t svn_ra_serf__blncache_t;

/* Creates new instance of baseline cache. Sets BLNCACHE_P with
 * a pointer to new instance, allocated in POOL.
 */
svn_error_t *
svn_ra_serf__blncache_create(svn_ra_serf__blncache_t **blncache_p,
                             apr_pool_t *pool);

/* Add information about baseline. BLNCACHE is a pointer to
 * baseline cache previously created using svn_ra_serf__blncache_create
 * function. BASELINE_URL is URL of baseline (can be NULL if unknown).
 * REVNUM is revision number associated with baseline. Use SVN_INVALID_REVNUM
 * to indicate that revision is unknown.
 * BC_URL is URL of baseline collection (can be NULL if unknwon).
 */
svn_error_t *
svn_ra_serf__blncache_set(svn_ra_serf__blncache_t *blncache,
                          const char *baseline_url,
                          svn_revnum_t revnum,
                          const char *bc_url,
                          apr_pool_t *scratch_pool);

/* Sets *BC_URL_P with a pointer to baseline collection URL for the given
 * REVNUM. *BC_URL_P will be NULL if cache doesn't have information about
 * this baseline.
 */
svn_error_t *
svn_ra_serf__blncache_get_bc_url(const char **bc_url_p,
                                 svn_ra_serf__blncache_t *blncache,
                                 svn_revnum_t revnum,
                                 apr_pool_t *result_pool);

/* Sets *BC_URL_P with pointer to baseline collection URL and *REVISION_P
 * with revision number of baseline BASELINE_URL. *BC_URL_P will be NULL,
 * *REVNUM_P will SVN_INVALID_REVNUM if cache doesn't have such
 * information.
 */
svn_error_t *
svn_ra_serf__blncache_get_baseline_info(const char **bc_url_p,
                                        svn_revnum_t *revnum_p,
                                        svn_ra_serf__blncache_t *blncache,
                                        const char *baseline_url,
                                        apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_SERF_BLNCACHE_H*/
