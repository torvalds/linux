/* uuids-table.h : internal interface to `uuids' table
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

#ifndef SVN_LIBSVN_FS_UUIDS_TABLE_H
#define SVN_LIBSVN_FS_UUIDS_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_io.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `uuids' table in @a env.
 *
 * Open a `uuids' table in @a env.  If @a create is non-zero, create
 * one if it doesn't exist.  Set @a *uuids_p to the new table.
 * Return a Berkeley DB error code.
 */
int svn_fs_bdb__open_uuids_table(DB **uuids_p,
                                 DB_ENV *env,
                                 svn_boolean_t create);

/* Get the UUID at index @a idx in the uuids table within @a fs,
 * storing the result in @a *uuid.
 */
svn_error_t *svn_fs_bdb__get_uuid(svn_fs_t *fs,
                                  int idx,
                                  const char **uuid,
                                  trail_t *trail,
                                  apr_pool_t *pool);

/* Set the UUID at index @a idx in the uuids table within @a fs
 * to @a uuid.
 */
svn_error_t *svn_fs_bdb__set_uuid(svn_fs_t *fs,
                                  int idx,
                                  const char *uuid,
                                  trail_t *trail,
                                  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UUIDS_TABLE_H */
