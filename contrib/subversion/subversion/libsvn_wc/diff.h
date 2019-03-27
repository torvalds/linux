/*
 * lock.h:  routines for diffing local files and directories.
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

#ifndef SVN_LIBSVN_WC_DIFF_H
#define SVN_LIBSVN_WC_DIFF_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#include "wc_db.h"
#include "private/svn_diff_tree.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* A function to diff locally added and locally copied files.

   Reports the file LOCAL_ABSPATH as ADDED file with relpath RELPATH to
   PROCESSOR with as parent baton PROCESSOR_PARENT_BATON.

   The node is expected to have status svn_wc__db_status_normal, or
   svn_wc__db_status_added. When DIFF_PRISTINE is TRUE, report the pristine
   version of LOCAL_ABSPATH as ADDED. In this case an
   svn_wc__db_status_deleted may shadow an added or deleted node.
 */
svn_error_t *
svn_wc__diff_local_only_file(svn_wc__db_t *db,
                             const char *local_abspath,
                             const char *relpath,
                             const char *moved_from_relpath,
                             const svn_diff_tree_processor_t *processor,
                             void *processor_parent_baton,
                             svn_boolean_t diff_pristine,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool);

/* A function to diff locally added and locally copied directories.

   Reports the directory LOCAL_ABSPATH and everything below it (limited by
   DEPTH) as added with relpath RELPATH to PROCESSOR with as parent baton
   PROCESSOR_PARENT_BATON.

   The node is expected to have status svn_wc__db_status_normal, or
   svn_wc__db_status_added. When DIFF_PRISTINE is TRUE, report the pristine
   version of LOCAL_ABSPATH as ADDED. In this case an
   svn_wc__db_status_deleted may shadow an added or deleted node.
 */
svn_error_t *
svn_wc__diff_local_only_dir(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *relpath,
                            svn_depth_t depth,
                            const char *moved_from_relpath,
                            const svn_diff_tree_processor_t *processor,
                            void *processor_parent_baton,
                            svn_boolean_t diff_pristine,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool);

/* Reports the BASE-file LOCAL_ABSPATH as deleted to PROCESSOR with relpath
   RELPATH, revision REVISION and parent baton PROCESSOR_PARENT_BATON.

   If REVISION is invalid, the revision as stored in BASE is used.

   The node is expected to have status svn_wc__db_status_normal in BASE. */
svn_error_t *
svn_wc__diff_base_only_file(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *relpath,
                            svn_revnum_t revision,
                            const svn_diff_tree_processor_t *processor,
                            void *processor_parent_baton,
                            apr_pool_t *scratch_pool);

/* Reports the BASE-directory LOCAL_ABSPATH and everything below it (limited
   by DEPTH) as deleted to PROCESSOR with relpath RELPATH and parent baton
   PROCESSOR_PARENT_BATON.

   If REVISION is invalid, the revision as stored in BASE is used.

   The node is expected to have status svn_wc__db_status_normal in BASE. */
svn_error_t *
svn_wc__diff_base_only_dir(svn_wc__db_t *db,
                           const char *local_abspath,
                           const char *relpath,
                           svn_revnum_t revision,
                           svn_depth_t depth,
                           const svn_diff_tree_processor_t *processor,
                           void *processor_parent_baton,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool);

/* Diff the file PATH against the text base of its BASE layer.  At this
 * stage we are dealing with a file that does exist in the working copy.
 */
svn_error_t *
svn_wc__diff_base_working_diff(svn_wc__db_t *db,
                               const char *local_abspath,
                               const char *relpath,
                               svn_revnum_t revision,
                               const svn_diff_tree_processor_t *processor,
                               void *processor_dir_baton,
                               svn_boolean_t diff_pristine,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool);

/* Return a tree processor filter that filters by changelist membership.
 *
 * This filter only passes on the changes for a file if the file's path
 * (in the WC) is assigned to one of the changelists in @a changelist_hash.
 * It also passes on the opening and closing of each directory that contains
 * such a change, and possibly also of other directories, but not addition
 * or deletion or changes to a directory.
 *
 * If @a changelist_hash is null then no filtering is performed and the
 * returned diff processor is driven exactly like the input @a processor.
 *
 * @a wc_ctx is the WC context and @a root_local_abspath is the WC path of
 * the root of the diff (for which relpath = "" in the diff processor).
 *
 * Allocate the returned diff processor in @a result_pool, or if no
 * filtering is required then the input pointer @a processor itself may be
 * returned.
 */
const svn_diff_tree_processor_t *
svn_wc__changelist_filter_tree_processor_create(
                                const svn_diff_tree_processor_t *processor,
                                svn_wc_context_t *wc_ctx,
                                const char *root_local_abspath,
                                apr_hash_t *changelist_hash,
                                apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_DIFF_H */
