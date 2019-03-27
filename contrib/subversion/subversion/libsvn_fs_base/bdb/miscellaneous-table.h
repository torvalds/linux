/* miscellaneous-table.h : internal interface to ops on `miscellaneous' table
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

#ifndef SVN_LIBSVN_FS_MISCELLANEOUS_TABLE_H
#define SVN_LIBSVN_FS_MISCELLANEOUS_TABLE_H

#include "svn_fs.h"
#include "svn_error.h"
#include "../trail.h"
#include "../fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `miscellaneous' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *MISCELLANEOUS_P to the new table.
   Return a Berkeley DB error code.  */
int
svn_fs_bdb__open_miscellaneous_table(DB **miscellaneous_p,
                                     DB_ENV *env,
                                     svn_boolean_t create);


/* Add data to the `miscellaneous' table in FS, as part of TRAIL.

   KEY and VAL should be NULL-terminated strings.  If VAL is NULL,
   the key is removed from the table. */
svn_error_t *
svn_fs_bdb__miscellaneous_set(svn_fs_t *fs,
                              const char *key,
                              const char *val,
                              trail_t *trail,
                              apr_pool_t *pool);


/* Set *VAL to the value of data cooresponding to KEY in the
   `miscellaneous' table of FS, or to NULL if that key isn't found. */
svn_error_t *
svn_fs_bdb__miscellaneous_get(const char **val,
                              svn_fs_t *fs,
                              const char *key,
                              trail_t *trail,
                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_MISCELLANEOUS_TABLE_H */
