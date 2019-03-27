/*
 * workqueue.h :  manipulating work queue items
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
 *
 *
 * Greg says:
 *
 * I think the current items are misdirected
 * work items should NOT touch the DB
 * the work items should be inserted into WORK_QUEUE by wc_db,
 * meaning: workqueue.[ch] should return work items for passing to the wc_db API,
 * which installs them during a transaction with the other work,
 * and those items should *only* make the on-disk state match what is in the database
 * before you rejoined the chan, I was discussing with Bert that I might rejigger the postcommit work,
 * in order to do the prop file handling as work items,
 * and pass those to db_global_commit for insertion as part of its transaction
 * so that once we switch to in-db props, those work items just get deleted,
 * (where they're simple things like: move this file to there, or delete that file)
 * i.e. workqueue should be seriously dumb
 * */

#ifndef SVN_WC_WORKQUEUE_H
#define SVN_WC_WORKQUEUE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_wc.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Returns TRUE if WI refers to a single work item. Returns FALSE if
   WI is a list of work items. WI must not be NULL.

   A work item looks like: (OP_CODE arg1 arg2 ...)

   If we see OP_CODE (an atom) as WI's first child, then this is a
   single work item. Otherwise, it is a list of work items.  */
#define SVN_WC__SINGLE_WORK_ITEM(wi) ((wi)->children->is_atom)


/* Combine WORK_ITEM1 and WORK_ITEM2 into a single, resulting work item.

   Each of the WORK_ITEM parameters may have one of three values:

     NULL                          no work item
     (OPCODE arg1 arg2 ...)        single work item
     ((OPCODE ...) (OPCODE ...))   multiple work items

   These will be combined as appropriate, and returned in one of the
   above three styles.

   The resulting list will be ordered: WORK_ITEM1 first, then WORK_ITEM2.

   The result contains a shallow copy of the inputs.  Allocate any
   additional storage needed in RESULT_POOL.
 */
svn_skel_t *
svn_wc__wq_merge(svn_skel_t *work_item1,
                 svn_skel_t *work_item2,
                 apr_pool_t *result_pool);


/* For the WCROOT identified by the DB and WRI_ABSPATH pair, run any
   work items that may be present in its workqueue.  */
svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will install the working
   copy file at LOCAL_ABSPATH. If USE_COMMIT_TIMES is TRUE, then the newly
   installed file will use the nodes CHANGE_DATE for the file timestamp.
   If RECORD_FILEINFO is TRUE, then the resulting RECORDED_TIME and
   RECORDED_SIZE will be recorded in the database.

   If SOURCE_ABSPATH is NULL, then the pristine contents will be installed
   (with appropriate translation). If SOURCE_ABSPATH is not NULL, then it
   specifies a source file for the translation. The file must exist for as
   long as *WORK_ITEM exists (and is queued). Typically, it will be a
   temporary file, and an OP_FILE_REMOVE will be queued to later remove it.
*/
svn_error_t *
svn_wc__wq_build_file_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *source_abspath,
                              svn_boolean_t use_commit_times,
                              svn_boolean_t record_fileinfo,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will remove a single
   file LOCAL_ABSPATH from the working copy identified by the pair DB,
   WRI_ABSPATH.  */
svn_error_t *
svn_wc__wq_build_file_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *wri_abspath,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that will remove a single
   directory or if RECURSIVE is TRUE a directory with all its
   descendants.  */
svn_error_t *
svn_wc__wq_build_dir_remove(svn_skel_t **work_item,
                            svn_wc__db_t *db,
                            const char *wri_abspath,
                            const char *local_abspath,
                            svn_boolean_t recursive,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that describes a move of
   a file or directory from SRC_ABSPATH to DST_ABSPATH, ready for
   storing in the working copy managing DST_ABSPATH.

   Perform temporary allocations in SCRATCH_POOL and *WORK_ITEM in
   RESULT_POOL.
*/
svn_error_t *
svn_wc__wq_build_file_move(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const char *src_abspath,
                           const char *dst_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that describes a copy from
   SRC_ABSPATH to DST_ABSPATH, while translating the stream using
   the information from LOCAL_ABSPATH. */
svn_error_t *
svn_wc__wq_build_file_copy_translated(svn_skel_t **work_item,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      const char *src_abspath,
                                      const char *dst_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will synchronize the
   target node's readonly and executable flags with the values defined
   by its properties and lock status.  */
svn_error_t *
svn_wc__wq_build_sync_file_flags(svn_skel_t **work_item,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will install a property reject
   file for LOCAL_ABSPATH into the working copy.
 */
svn_error_t *
svn_wc__wq_build_prej_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Handle the final post-commit step of retranslating and recording the
   working copy state of a committed file.

   If PROP_MODS is false, assume that properties are not changed.

   (Property modifications are read when svn_wc__wq_build_file_commit
    is called and processed when the working queue is being evaluated)

    Allocate *work_item in RESULT_POOL. Perform temporary allocations
    in SCRATCH_POOL.
   */
svn_error_t *
svn_wc__wq_build_file_commit(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t prop_mods,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that will install the working
   copy directory at LOCAL_ABSPATH. */
svn_error_t *
svn_wc__wq_build_dir_install(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

svn_error_t *
svn_wc__wq_build_postupgrade(svn_skel_t **work_item,
                             apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
