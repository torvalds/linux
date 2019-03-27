/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_branch_impl.h
 * @brief Declarations needed by implementators of branch classes
 *
 * @since New in ???.
 */

#ifndef SVN_BRANCH_IMPL_H
#define SVN_BRANCH_IMPL_H

#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Common aspects od a txn/branch 'editor' class (derived from Ev2) */
typedef struct svn_branch__vtable_priv_t
{
  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

#ifdef ENABLE_ORDERING_CHECK
  svn_boolean_t within_callback;
  svn_boolean_t finished;
  apr_pool_t *state_pool;
#endif

} svn_branch__vtable_priv_t;

/* The methods of svn_branch__txn_t.
 * See the corresponding public API functions for details.
 */

typedef apr_array_header_t *(*svn_branch__txn_v_get_branches_t)(
  const svn_branch__txn_t *txn,
  apr_pool_t *result_pool);

typedef svn_error_t *(*svn_branch__txn_v_delete_branch_t)(
  svn_branch__txn_t *txn,
  const char *bid,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_get_num_new_eids_t)(
  const svn_branch__txn_t *txn,
  int *num_new_eids_p,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_new_eid_t)(
  svn_branch__txn_t *txn,
  svn_branch__eid_t *eid_p,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_open_branch_t)(
  svn_branch__txn_t *txn,
  svn_branch__state_t **new_branch_p,
  const char *new_branch_id,
  int root_eid,
  svn_branch__rev_bid_eid_t *from,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_finalize_eids_t)(
  svn_branch__txn_t *txn,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_serialize_t)(
  svn_branch__txn_t *txn,
  svn_stream_t *stream,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_sequence_point_t)(
  svn_branch__txn_t *txn,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_complete_t)(
  svn_branch__txn_t *txn,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__txn_v_abort_t)(
  svn_branch__txn_t *txn,
  apr_pool_t *scratch_pool);

struct svn_branch__txn_vtable_t
{
  svn_branch__vtable_priv_t vpriv;

  /* Methods. */
  svn_branch__txn_v_get_branches_t get_branches;
  svn_branch__txn_v_delete_branch_t delete_branch;
  svn_branch__txn_v_get_num_new_eids_t get_num_new_eids;
  svn_branch__txn_v_new_eid_t new_eid;
  svn_branch__txn_v_open_branch_t open_branch;
  svn_branch__txn_v_finalize_eids_t finalize_eids;
  svn_branch__txn_v_serialize_t serialize;
  svn_branch__txn_v_sequence_point_t sequence_point;
  svn_branch__txn_v_complete_t complete;
  svn_branch__txn_v_complete_t abort;

};

/* The methods of svn_branch__state_t.
 * See the corresponding public API functions for details.
 */

typedef svn_error_t *(*svn_branch__state_v_get_elements_t)(
  const svn_branch__state_t *branch,
  svn_element__tree_t **element_tree_p,
  apr_pool_t *result_pool);

typedef svn_error_t *(*svn_branch__state_v_get_element_t)(
  const svn_branch__state_t *branch,
  svn_element__content_t **element_p,
  int eid,
  apr_pool_t *result_pool);

typedef svn_error_t *(*svn_branch__state_v_set_element_t)(
  svn_branch__state_t *branch,
  svn_branch__eid_t eid,
  const svn_element__content_t *element,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__state_v_copy_one_t)(
  svn_branch__state_t *branch,
  const svn_branch__rev_bid_eid_t *src_el_rev,
  svn_branch__eid_t local_eid,
  svn_branch__eid_t new_parent_eid,
  const char *new_name,
  const svn_element__payload_t *new_payload,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__state_v_copy_tree_t)(
  svn_branch__state_t *branch,
  const svn_branch__rev_bid_eid_t *src_el_rev,
  svn_branch__eid_t new_parent_eid,
  const char *new_name,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__state_v_purge_t)(
  svn_branch__state_t *branch,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__state_v_get_history_t)(
  svn_branch__state_t *branch,
  svn_branch__history_t **history_p,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_branch__state_v_set_history_t)(
  svn_branch__state_t *branch,
  const svn_branch__history_t *history,
  apr_pool_t *scratch_pool);

struct svn_branch__state_vtable_t
{
  svn_branch__vtable_priv_t vpriv;

  svn_branch__state_v_get_elements_t get_elements;
  svn_branch__state_v_get_element_t get_element;
  svn_branch__state_v_set_element_t set_element;
  svn_branch__state_v_copy_one_t copy_one;
  svn_branch__state_v_copy_tree_t copy_tree;
  svn_branch__state_v_purge_t purge;
  svn_branch__state_v_get_history_t get_history;
  svn_branch__state_v_set_history_t set_history;

};


#ifdef __cplusplus
}
#endif

#endif /* SVN_BRANCH_IMPL_H */

