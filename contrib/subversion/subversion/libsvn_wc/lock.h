/*
 * lock.h:  routines for locking working copy subdirectories.
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


#ifndef SVN_LIBSVN_WC_LOCK_H
#define SVN_LIBSVN_WC_LOCK_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** General utilities that may get moved upstairs at some point. */

/* Store ENTRIES in the cache in ADM_ACCESS.  ENTRIES may be NULL. */
void svn_wc__adm_access_set_entries(svn_wc_adm_access_t *adm_access,
                                    apr_hash_t *entries);

/* Return the entries hash cached in ADM_ACCESS.  The returned hash may
   be NULL.  */
apr_hash_t *svn_wc__adm_access_entries(svn_wc_adm_access_t *adm_access);

/* Same as svn_wc__adm_retrieve_internal, but takes a DB and an absolute
   directory path.  */
svn_wc_adm_access_t *
svn_wc__adm_retrieve_internal2(svn_wc__db_t *db,
                               const char *abspath,
                               apr_pool_t *scratch_pool);

/* ### this is probably bunk. but I dunna want to trace backwards-compat
   ### users of svn_wc_check_wc(). probably gonna be rewritten for wc-ng
   ### in any case.

   If CHECK_PATH is TRUE, a not-existing directory is not a working copy */
svn_error_t *
svn_wc__internal_check_wc(int *wc_format,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_boolean_t check_path,
                          apr_pool_t *scratch_pool);

/* Return the working copy database associated with this access baton. */
svn_wc__db_t *
svn_wc__adm_get_db(const svn_wc_adm_access_t *adm_access);


/* Get a reference to the baton's internal ABSPATH.  */
const char *
svn_wc__adm_access_abspath(const svn_wc_adm_access_t *adm_access);

/* Return the pool used by access baton ADM_ACCESS.
 * Note: This is a non-deprecated variant of svn_wc_adm_access_pool for
 * libsvn_wc internal usage only.
 */
apr_pool_t *
svn_wc__adm_access_pool_internal(const svn_wc_adm_access_t *adm_access);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_LOCK_H */
