/* verify.h : verification interface of the FSX filesystem
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

#ifndef SVN_LIBSVN_FS_X_VERIFY_H
#define SVN_LIBSVN_FS_X_VERIFY_H

#include "fs.h"

/* Verify metadata in fsx filesystem FS.  Limit the checks to revisions
 * START to END where possible.  Indicate progress via the optional
 * NOTIFY_FUNC callback using NOTIFY_BATON.  The optional CANCEL_FUNC
 * will periodically be called with CANCEL_BATON to allow for preemption.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__verify(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool);

#endif
