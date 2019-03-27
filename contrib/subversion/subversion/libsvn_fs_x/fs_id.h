/* fs_id.h : FSX's implementation of svn_fs_id_t
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

#ifndef SVN_LIBSVN_FS_X_FS_ID_H
#define SVN_LIBSVN_FS_X_FS_ID_H

#include "id.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Transparent FS access object to be used with FSX's implementation for
   svn_fs_id_t.  It allows the ID object query data from the respective FS
   to check for node relationships etc.  It also allows to re-open the repo
   after the original svn_fs_t object got cleaned up, i.e. the ID object's
   functionality does not depend on any other object's lifetime.

   For efficiency, multiple svn_fs_id_t should share the same context.
 */
typedef struct svn_fs_x__id_context_t svn_fs_x__id_context_t;

/* Return a context object for filesystem FS; construct it in RESULT_POOL. */
svn_fs_x__id_context_t *
svn_fs_x__id_create_context(svn_fs_t *fs,
                            apr_pool_t *result_pool);

/* Create a permanent ID based on NODEREV_ID, allocated in RESULT_POOL.
   For complex requests, access the filesystem provided with CONTEXT.

   For efficiency, CONTEXT should have been created in RESULT_POOL and be
   shared between multiple ID instances allocated in the same pool.
 */
svn_fs_id_t *
svn_fs_x__id_create(svn_fs_x__id_context_t *context,
                    const svn_fs_x__id_t *noderev_id,
                    apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_FS_ID_H */
