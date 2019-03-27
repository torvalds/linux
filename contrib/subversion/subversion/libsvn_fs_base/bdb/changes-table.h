/* changes-table.h : internal interface to `changes' table
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

#ifndef SVN_LIBSVN_FS_CHANGES_TABLE_H
#define SVN_LIBSVN_FS_CHANGES_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_io.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `changes' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *CHANGES_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs_bdb__open_changes_table(DB **changes_p,
                                   DB_ENV *env,
                                   svn_boolean_t create);


/* Add CHANGE as a record to the `changes' table in FS as part of
   TRAIL, keyed on KEY.

   CHANGE->path is expected to be a canonicalized filesystem path (see
   svn_fs__canonicalize_abspath).

   Note that because the `changes' table uses duplicate keys, this
   function will not overwrite prior additions that have the KEY
   key, but simply adds this new record alongside previous ones.  */
svn_error_t *svn_fs_bdb__changes_add(svn_fs_t *fs,
                                     const char *key,
                                     change_t *change,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Remove all changes associated with KEY from the `changes' table in
   FS, as part of TRAIL. */
svn_error_t *svn_fs_bdb__changes_delete(svn_fs_t *fs,
                                        const char *key,
                                        trail_t *trail,
                                        apr_pool_t *pool);

/* Return a hash *CHANGES_P, keyed on const char * paths, and
   containing svn_fs_path_change2_t * values representing summarized
   changed records associated with KEY in FS, as part of TRAIL.
   Allocate the array and its items in POOL.  */
svn_error_t *svn_fs_bdb__changes_fetch(apr_hash_t **changes_p,
                                       svn_fs_t *fs,
                                       const char *key,
                                       trail_t *trail,
                                       apr_pool_t *pool);

/* Return an array *CHANGES_P of change_t * items representing
   all the change records associated with KEY in FS, as part of TRAIL.
   Allocate the array and its items in POOL.  */
svn_error_t *svn_fs_bdb__changes_fetch_raw(apr_array_header_t **changes_p,
                                           svn_fs_t *fs,
                                           const char *key,
                                           trail_t *trail,
                                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_CHANGES_TABLE_H */
