/* batch_fsync.h --- efficiently fsync multiple targets
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

#ifndef SVN_LIBSVN_FS_X__BATCH_FSYNC_H
#define SVN_LIBSVN_FS_X__BATCH_FSYNC_H

#include "svn_error.h"

/* Infrastructure for efficiently calling fsync on files and directories.
 *
 * The idea is to have a container of open file handles (including
 * directory handles on POSIX), at most one per file.  During the course
 * of an FS operation that needs to be fsync'ed, all touched files and
 * folders accumulate in the container.
 *
 * At the end of the FS operation, all file changes will be written the
 * physical disk, once per file and folder.  Afterwards, all handles will
 * be closed and the container is ready for reuse.
 *
 * To minimize the delay caused by the batch flush, run all fsync calls
 * concurrently - if the OS supports multi-threading.
 */

/* Opaque container type.
 */
typedef struct svn_fs_x__batch_fsync_t svn_fs_x__batch_fsync_t;

/* Initialize the concurrent fsync infrastructure.  Clean it up when
 * OWNING_POOL gets cleared.
 *
 * This function must be called before using any of the other functions in
 * in this module.  It should only be called once.
 */
svn_error_t *
svn_fs_x__batch_fsync_init(apr_pool_t *owning_pool);

/* Set *RESULT_P to a new batch fsync structure, allocated in RESULT_POOL.
 * If FLUSH_TO_DISK is not set, the resulting struct will not actually use
 * fsync. */
svn_error_t *
svn_fs_x__batch_fsync_create(svn_fs_x__batch_fsync_t **result_p,
                             svn_boolean_t flush_to_disk,
                             apr_pool_t *result_pool);

/* Open the file at FILENAME for read and write access.  Return it in *FILE
 * and schedule it for fsync in BATCH.  If BATCH already contains an open
 * file for FILENAME, return that instead creating a new instance.
 *
 * Use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_fs_x__batch_fsync_open_file(apr_file_t **file,
                                svn_fs_x__batch_fsync_t *batch,
                                const char *filename,
                                apr_pool_t *scratch_pool);

/* Inform the BATCH that a file or directory has been created at PATH.
 * "Created" means either newly created to renamed to PATH - even if another
 * item with the same name existed before.  Depending on the OS, the correct
 * path will scheduled for fsync.
 *
 * Use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_fs_x__batch_fsync_new_path(svn_fs_x__batch_fsync_t *batch,
                               const char *path,
                               apr_pool_t *scratch_pool);

/* For all files and directories in BATCH, flush all changes to disk and
 * close the file handles.  Use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_fs_x__batch_fsync_run(svn_fs_x__batch_fsync_t *batch,
                          apr_pool_t *scratch_pool);

#endif
