/* rev-table.h : internal interface to revision table operations
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

#ifndef SVN_LIBSVN_FS_REV_TABLE_H
#define SVN_LIBSVN_FS_REV_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_fs.h"

#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Creating and opening the `revisions' table.  */

/* Open a `revisions' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *REVS_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs_bdb__open_revisions_table(DB **revisions_p,
                                     DB_ENV *env,
                                     svn_boolean_t create);



/* Storing and retrieving filesystem revisions.  */


/* Set *REVISION_P to point to the revision structure for the
   filesystem revision REV in FS, as part of TRAIL.  Perform all
   allocations in POOL.  */
svn_error_t *svn_fs_bdb__get_rev(revision_t **revision_p,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 trail_t *trail,
                                 apr_pool_t *pool);

/* Store REVISION in FS as revision *REV as part of TRAIL.  If *REV is
   an invalid revision number, create a brand new revision and return
   its revision number as *REV to the caller.  Do any necessary
   temporary allocation in POOL.  */
svn_error_t *svn_fs_bdb__put_rev(svn_revnum_t *rev,
                                 svn_fs_t *fs,
                                 const revision_t *revision,
                                 trail_t *trail,
                                 apr_pool_t *pool);


/* Set *YOUNGEST_P to the youngest revision in filesystem FS,
   as part of TRAIL.  Use POOL for all temporary allocation. */
svn_error_t *svn_fs_bdb__youngest_rev(svn_revnum_t *youngest_p,
                                      svn_fs_t *fs,
                                      trail_t *trail,
                                      apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REV_TABLE_H */
