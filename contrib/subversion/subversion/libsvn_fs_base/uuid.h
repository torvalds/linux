/* uuid.h : internal interface to uuid functions
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

#ifndef SVN_LIBSVN_FS_UUID_H
#define SVN_LIBSVN_FS_UUID_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Set FS->UUID to the value read from the database, allocated
   in FS->POOL.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *svn_fs_base__populate_uuid(svn_fs_t *fs,
                                        apr_pool_t *scratch_pool);


/* These functions implement some of the calls in the FS loader
   library's fs vtable. */

svn_error_t *svn_fs_base__set_uuid(svn_fs_t *fs, const char *uuid,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UUID_H */
