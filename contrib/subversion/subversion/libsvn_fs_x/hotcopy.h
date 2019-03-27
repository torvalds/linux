/* hotcopy.h : interface to the hot-copying functionality
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

#ifndef SVN_LIBSVN_FS_X_HOTCOPY_H
#define SVN_LIBSVN_FS_X_HOTCOPY_H

#include "fs.h"

/* Copy the fsfs filesystem SRC_FS at SRC_PATH into a new copy DST_FS at
 * DST_PATH.  If INCREMENTAL is TRUE, do not re-copy data which already
 * exists in DST_FS.  Indicate progress via the optional NOTIFY_FUNC
 * callback using NOTIFY_BATON.  Use COMMON_POOL for process-wide and
 * SCRATCH_POOL for temporary allocations.  Use COMMON_POOL_LOCK to ensure
 * that the initialization of the shared data is serialized. */
svn_error_t *
svn_fs_x__hotcopy(svn_fs_t *src_fs,
                  svn_fs_t *dst_fs,
                  const char *src_path,
                  const char *dst_path,
                  svn_boolean_t incremental,
                  svn_fs_hotcopy_notify_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_mutex__t *common_pool_lock,
                  apr_pool_t *scratch_pool,
                  apr_pool_t *common_pool);

#endif
