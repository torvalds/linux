/* reps-table.h : internal interface to `representations' table
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

#ifndef SVN_LIBSVN_FS_REPS_TABLE_H
#define SVN_LIBSVN_FS_REPS_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_io.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Creating the `representations' table. ***/

/* Open a `representations' table in ENV.  If CREATE is non-zero,
   create one if it doesn't exist.  Set *REPS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_reps_table(DB **reps_p,
                                DB_ENV *env,
                                svn_boolean_t create);



/*** Storing and retrieving reps.  ***/

/* Set *REP_P to point to the representation for the key KEY in
   FS, as part of TRAIL.  Perform all allocations in POOL.

   If KEY is not a representation in FS, the error
   SVN_ERR_FS_NO_SUCH_REPRESENTATION is returned.  */
svn_error_t *svn_fs_bdb__read_rep(representation_t **rep_p,
                                  svn_fs_t *fs,
                                  const char *key,
                                  trail_t *trail,
                                  apr_pool_t *pool);


/* Store REP as the representation for KEY in FS, as part of
   TRAIL.  Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_bdb__write_rep(svn_fs_t *fs,
                                   const char *key,
                                   const representation_t *rep,
                                   trail_t *trail,
                                   apr_pool_t *pool);


/* Store REP as a new representation in FS, and the new rep's key in
   *KEY, as part of trail.  The new key is allocated in POOL.  */
svn_error_t *svn_fs_bdb__write_new_rep(const char **key,
                                       svn_fs_t *fs,
                                       const representation_t *rep,
                                       trail_t *trail,
                                       apr_pool_t *pool);

/* Delete representation KEY from FS, as part of TRAIL.
   WARNING: This does not ensure that no one references this
   representation!  Callers should ensure that themselves.  */
svn_error_t *svn_fs_bdb__delete_rep(svn_fs_t *fs,
                                    const char *key,
                                    trail_t *trail,
                                    apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REPS_TABLE_H */
