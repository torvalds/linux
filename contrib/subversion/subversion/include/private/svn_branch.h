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
 * @file svn_branch.h
 * @brief Operating on a branched version history
 *
 * @since New in ???.
 */

/* Transactions
 *
 * A 'txn' contains a set of changes to the branches/elements.
 *
 * To make changes you say, for example, "for element 5: I want the parent
 * element to be 3 now, and its name to be 'bar', and its content to be
 * {props=... text=...}". That sets up a move and/or rename and/or
 * content-change (or possibly a no-op for all three aspects) for element 5.
 *
 * Before or after (or at the same time, if we make a parallelizable
 * implementation) we can make edits to the other elements, including
 * element 3.
 *
 * So at the time of the edit method 'change e5: let its parent be e3'
 * we might or might not have even created e3, if that happens to be an
 * element that we wish to create rather than one that already existed.
 *
 * We allow this non-ordering because we want the changes to different
 * elements to be totally independent.
 *
 * So at any given 'moment' in time during specifying the changes to a
 * txn, the txn state is not necessarily one that maps directly to a
 * flat tree (single-rooted, no cycles, no clashes of paths, etc.).
 *
 * Once we've finished specifying the edits, then the txn state will be
 * converted to a flat tree, and that's the final result. But we can't
 * query an arbitrary txn (potentially in the middle of making changes
 * to it) by path, because the paths are not fully defined yet.
 *
 * So there are three kinds of operations:
 *
 * - query involving paths
 *   => requires a flat tree state to query, not an in-progress txn
 *
 * - query, not involving paths
 *   => accepts a txn-in-progress or a flat tree
 *
 * - modify (not involving paths)
 *   => requires a txn
 *
 * Currently, a txn is represented by 'svn_branch__txn_t', with
 * 'svn_branch__state_t' for the individual branches in it. A flat tree is
 * represented by 'svn_branch__subtree_t'. But there is currently not a
 * clean separation; there is some overlap and some warts such as the
 * 'svn_branch__txn_sequence_point' method.
 */


#ifndef SVN_BRANCH_H
#define SVN_BRANCH_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#include "private/svn_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* ### */
#define SVN_BRANCH__ERR 123456

/** Element Identifier (EID).
 *
 * An element may appear in any or all branches, and its EID is the same in
 * each branch in which the element appears.
 * 
 * By definition, an element keeps the same EID for its whole lifetime, even
 * if deleted from all branches and later 'resurrected'.
 *
 * In principle, an EID is an arbitrary token and has no intrinsic
 * relationships (except equality) to other EIDs. The current implementation
 * uses integers and allocates them sequentially from a central counter, but
 * the implementation may be changed.
 *
 * ### In most places the code currently says 'int', verbatim.
 */
typedef int svn_branch__eid_t;

typedef struct svn_branch__el_rev_id_t svn_branch__el_rev_id_t;

typedef struct svn_branch__rev_bid_eid_t svn_branch__rev_bid_eid_t;

typedef struct svn_branch__rev_bid_t svn_branch__rev_bid_t;

typedef struct svn_branch__state_t svn_branch__state_t;

/* Per-repository branching info.
 */
typedef struct svn_branch__repos_t svn_branch__repos_t;

/* Methods (conceptually public, but called indirectly) for a transaction.
 */
typedef struct svn_branch__txn_vtable_t svn_branch__txn_vtable_t;

/* Private data for a transaction.
 */
typedef struct svn_branch__txn_priv_t svn_branch__txn_priv_t;

/* A container for all the branching metadata for a specific revision (or
 * an uncommitted transaction).
 */
typedef struct svn_branch__txn_t
{
  /* Methods (conceptually public, but called indirectly). */
  svn_branch__txn_vtable_t *vtable;

  /* Private data. */
  svn_branch__txn_priv_t *priv;

  /* Public data. */

  /* The repository in which this revision exists. */
  svn_branch__repos_t *repos;

  /* If committed, the revision number; else SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* If committed, the previous revision number, else the revision number
     on which this transaction is based. */
  svn_revnum_t base_rev;

} svn_branch__txn_t;

/* Create a new branch txn object.
 */
svn_branch__txn_t *
svn_branch__txn_create(const svn_branch__txn_vtable_t *vtable,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *result_pool);

/* Return all the branches in TXN.
 *
 * These branches are available for reading. (Some of them may also be
 * mutable.)
 *
 * ### Rename to 'list_branches' & return only their ids?
 *
 * Return an empty array if there are none.
 */
apr_array_header_t *
svn_branch__txn_get_branches(const svn_branch__txn_t *txn,
                             apr_pool_t *result_pool);

/* Return the branch whose id is BRANCH_ID in TXN.
 *
 * Return NULL if not found.
 *
 * Note: a branch id is, in behavioural terms, an arbitrary token. In the
 * current implementation it is constructed from the hierarchy of subbranch
 * root EIDs leading to the branch, but that may be changed in future.
 *
 * See also: svn_branch__get_id().
 */
svn_branch__state_t *
svn_branch__txn_get_branch_by_id(const svn_branch__txn_t *txn,
                                 const char *branch_id,
                                 apr_pool_t *scratch_pool);

svn_error_t *
svn_branch__txn_get_num_new_eids(const svn_branch__txn_t *txn,
                                 int *num_new_eids_p,
                                 apr_pool_t *scratch_pool);

/* Assign a new txn-scope element id in TXN.
 */
svn_error_t *
svn_branch__txn_new_eid(svn_branch__txn_t *txn,
                        int *new_eid_p,
                        apr_pool_t *scratch_pool);

/** Open for writing, either a new branch or an existing branch.
 *
 * When creating a new branch, declare its root element id to be ROOT_EID. Do
 * not instantiate the root element, nor any other elements.
 *
 * TREE_REF specifies the initial tree content, by reference to a committed
 * tree. It overwrites any existing tree, even if the branch was already
 * mutable in the txn.
 *
 * If TREE_REF is null, then the initial tree is empty for a new branch
 * (not already present in the txn), or the branch's current tree if the
 * branch was already present (readable or mutable) in the txn.
 *
 * ### TODO: Take a 'history' parameter; 'none' is a valid option.
 *
 * We use a common 'open subbranch' method for both 'find' and 'add'
 * cases, according to the principle that 'editing' a txn should dictate
 * the new state without reference to the old state.
 *
 * This method returns a mutable 'branch state' object which is a part of
 * the txn.
 *
 * ### When opening ('finding') an existing branch, ROOT_EID should match
 *     it. (Should we check, and throw an error if not?)
 */
svn_error_t *
svn_branch__txn_open_branch(svn_branch__txn_t *txn,
                            svn_branch__state_t **new_branch_p,
                            const char *branch_id,
                            int root_eid,
                            svn_branch__rev_bid_eid_t *tree_ref,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/** Register a sequence point.
 *
 * At a sequence point, elements are arranged in a tree hierarchy: each
 * element has exactly one parent element, except the root, and so on.
 * Translation between paths and element addressing is defined only at
 * a sequence point.
 *
 * The other edit operations -- add, alter, delete, etc. -- result in a
 * state that is not a sequence point.
 *
 * The new transaction begins at a sequence point. Completion of editing
 * (svn_branch__txn_complete()) also creates a sequence point.
 */
svn_error_t *
svn_branch__txn_sequence_point(svn_branch__txn_t *txn,
                               apr_pool_t *scratch_pool);

/** Finalize this transaction.
 *
 * Notify that the edit has been completed successfully.
 */
svn_error_t *
svn_branch__txn_complete(svn_branch__txn_t *txn,
                         apr_pool_t *scratch_pool);

/** Abandon this transaction.
 *
 * Notify that editing this transaction was not successful.
 */
svn_error_t *
svn_branch__txn_abort(svn_branch__txn_t *txn,
                      apr_pool_t *scratch_pool);

/* Change txn-local EIDs (negative integers) in TXN to revision EIDs, by
 * assigning a new revision-EID (positive integer) for each one.
 *
 * Rewrite TXN->first_eid and TXN->next_eid accordingly.
 */
svn_error_t *
svn_branch__txn_finalize_eids(svn_branch__txn_t *txn,
                              apr_pool_t *scratch_pool);

/* Often, branches have the same root element. For example,
 * branching /trunk to /branches/br1 results in:
 *
 *      branch 1: (root-EID=100)
 *          EID 100 => /trunk
 *          ...
 *      branch 2: (root-EID=100)
 *          EID 100 => /branches/br1
 *          ...
 *
 * However, the root element of one branch may correspond to a non-root
 * element of another branch.
 *
 * Continuing the same example, branching from the trunk subtree
 * /trunk/D (which is not itself a branch root) results in:
 *
 *      branch 3: (root-EID=104)
 *          EID 100 => (nil)
 *          ...
 *          EID 104 => /branches/branch-of-trunk-subtree-D
 *          ...
 */

/* Methods (conceptually public, but called indirectly) for a branch state.
 */
typedef struct svn_branch__state_vtable_t svn_branch__state_vtable_t;

/* Private data for a branch state.
 */
typedef struct svn_branch__state_priv_t svn_branch__state_priv_t;

/* A branch state.
 *
 * A branch state object describes one version of one branch.
 */
struct svn_branch__state_t
{
  /* Methods (conceptually public, but called indirectly). */
  svn_branch__state_vtable_t *vtable;

  /* Private data. */
  svn_branch__state_priv_t *priv;

  /* Public data. */

  /* The branch identifier (starting with 'B') */
  const char *bid;

  /* The revision to which this branch state belongs */
  /* ### Later we should remove this and let a single state be sharable
     by multiple txns. */
  svn_branch__txn_t *txn;

};

/* Create a new branch state object.
 */
svn_branch__state_t *
svn_branch__state_create(const svn_branch__state_vtable_t *vtable,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *result_pool);

/* Get the full id of branch BRANCH.
 *
 * Branch id format:
 *      B<top-level-branch-num>[.<1st-level-eid>[.<2nd-level-eid>[...]]]
 *
 * Note: a branch id is, in behavioural terms, an arbitrary token. In the
 * current implementation it is constructed from the hierarchy of subbranch
 * root EIDs leading to the branch, but that may be changed in future.
 *
 * See also: svn_branch__txn_get_branch_by_id().
 */
const char *
svn_branch__get_id(const svn_branch__state_t *branch,
                   apr_pool_t *result_pool);

/* Return the element id of the root element of BRANCH.
 */
int
svn_branch__root_eid(const svn_branch__state_t *branch);

/* Return the id of the branch nested in OUTER_BID at element OUTER_EID.
 *
 * For a top-level branch, OUTER_BID is null and OUTER_EID is the
 * top-level branch number.
 *
 * (Such branches need not exist. This works purely with ids, making use
 * of the fact that nested branch ids are predictable based on the nesting
 * element id.)
 */
const char *
svn_branch__id_nest(const char *outer_bid,
                    int outer_eid,
                    apr_pool_t *result_pool);

/* Given a nested branch id BID, set *OUTER_BID to the outer branch's id
 * and *OUTER_EID to the nesting element in the outer branch.
 *
 * For a top-level branch, set *OUTER_BID to NULL and *OUTER_EID to the
 * top-level branch number.
 *
 * (Such branches need not exist. This works purely with ids, making use
 * of the fact that nested branch ids are predictable based on the nesting
 * element id.)
 */
void
svn_branch__id_unnest(const char **outer_bid,
                      int *outer_eid,
                      const char *bid,
                      apr_pool_t *result_pool);

/* Remove the branch with id BID from the list of branches in TXN.
 */
svn_error_t *
svn_branch__txn_delete_branch(svn_branch__txn_t *txn,
                              const char *bid,
                              apr_pool_t *scratch_pool);

/* Branch-Element-Revision */
struct svn_branch__el_rev_id_t
{
  /* The branch state that applies to REV. */
  svn_branch__state_t *branch;
  /* Element. */
  int eid;
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'.
     ### Do we need this if BRANCH refers to a particular branch-revision? */
  svn_revnum_t rev;

};

/* Revision-branch-element id. */
struct svn_branch__rev_bid_eid_t
{
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'. */
  svn_revnum_t rev;
  /* The branch id in revision REV. */
  const char *bid;
  /* Element id. */
  int eid;

};

/* Revision-branch id. */
struct svn_branch__rev_bid_t
{
  /* Revision. SVN_INVALID_REVNUM means 'in this transaction', not 'head'. */
  svn_revnum_t rev;
  /* The branch id in revision REV. */
  const char *bid;

};

/* Return a new el_rev_id object constructed with *shallow* copies of BRANCH,
 * EID and REV, allocated in RESULT_POOL.
 */
svn_branch__el_rev_id_t *
svn_branch__el_rev_id_create(svn_branch__state_t *branch,
                             int eid,
                             svn_revnum_t rev,
                             apr_pool_t *result_pool);

/* Return a new id object constructed with a deep copy of OLD_ID,
 * allocated in RESULT_POOL. */
svn_branch__el_rev_id_t *
svn_branch__el_rev_id_dup(const svn_branch__el_rev_id_t *old_id,
                          apr_pool_t *result_pool);

/* Return a new id object constructed with deep copies of REV, BRANCH_ID
 * and EID, allocated in RESULT_POOL.
 */
svn_branch__rev_bid_eid_t *
svn_branch__rev_bid_eid_create(svn_revnum_t rev,
                               const char *branch_id,
                               int eid,
                               apr_pool_t *result_pool);
svn_branch__rev_bid_t *
svn_branch__rev_bid_create(svn_revnum_t rev,
                           const char *branch_id,
                           apr_pool_t *result_pool);

/* Return a new id object constructed with a deep copy of OLD_ID,
 * allocated in RESULT_POOL. */
svn_branch__rev_bid_eid_t *
svn_branch__rev_bid_eid_dup(const svn_branch__rev_bid_eid_t *old_id,
                            apr_pool_t *result_pool);
svn_branch__rev_bid_t *
svn_branch__rev_bid_dup(const svn_branch__rev_bid_t *old_id,
                        apr_pool_t *result_pool);

svn_boolean_t
svn_branch__rev_bid_equal(const svn_branch__rev_bid_t *id1,
                          const svn_branch__rev_bid_t *id2);

typedef struct svn_branch__history_t
{
  /* The immediate parents of this state in the branch/merge graph.
     Hash of (BID -> svn_branch__rev_bid_t). */
  apr_hash_t *parents;
} svn_branch__history_t;

svn_branch__history_t *
svn_branch__history_create_empty(apr_pool_t *result_pool);

svn_branch__history_t *
svn_branch__history_create(apr_hash_t *parents,
                           apr_pool_t *result_pool);

svn_branch__history_t *
svn_branch__history_dup(const svn_branch__history_t *old,
                        apr_pool_t *result_pool);

/* Return the mapping of elements in branch BRANCH.
 */
svn_error_t *
svn_branch__state_get_elements(const svn_branch__state_t *branch,
                               svn_element__tree_t **element_tree_p,
                               apr_pool_t *result_pool);

/* In BRANCH, get element EID (parent, name, payload).
 *
 * If element EID is not present, return null.
 */
svn_error_t *
svn_branch__state_get_element(const svn_branch__state_t *branch,
                              svn_element__content_t **element_p,
                              int eid,
                              apr_pool_t *result_pool);

/** Equivalent to
 *    alter_one(..., element->parent_eid, element->name, element->payload),
 * or, if @a element is null, to
 *    delete_one(...).
 */
svn_error_t *
svn_branch__state_set_element(svn_branch__state_t *branch,
                              int eid,
                              const svn_element__content_t *element,
                              apr_pool_t *result_pool);

/** Specify that the element of @a branch identified by @a eid shall not
 * be present.
 *
 * The delete is not explicitly recursive. However, as an effect of the
 * final 'flattening' of a branch state into a single tree, each element
 * in the final state that still has this element as its parent will also
 * be deleted, recursively.
 *
 * The element @a eid must not be the root element of @a branch.
 *
 * ### Options for Out-Of-Date Checking on Rebase
 *
 *   We may want to specify what kind of OOD check takes place. The
 *   following two options differ in what happens to an element that is
 *   added, on the other side, as a child of this deleted element.
 *
 *   Rebase option 1: The rebase checks for changes in the whole subtree,
 *   excluding any portions of the subtree for which an explicit delete or
 *   move-away has been issued. The check includes checking that the other
 *   side has not added any child. In other words, the deletion is
 *   interpreted as an action affecting a subtree (dynamically rooted at
 *   this element), rather than as an action affecting a single element or
 *   a fixed set of elements that was explicitly or implicitly specified
 *   by the sender.
 *
 *   To delete a mixed-rev subtree, the client sends an explicit delete for
 *   each subtree that has a different base revision from its parent.
 *
 *   Rebase option 2: The rebase checks for changes to this element only.
 *   The sender can send an explicit delete for each existing child element
 *   that it requires to be checked as well. However, there is no way for
 *   the sender to specify whether a child element added by the other side
 *   should be considered an out-of-date error or silently deleted.
 *
 *   It would also be possible to let the caller specify, at some suitable
 *   granularity, which option to use.
 */
svn_error_t *
svn_branch__state_delete_one(svn_branch__state_t *branch,
                             svn_branch__eid_t eid,
                             apr_pool_t *scratch_pool);

/** Specify the tree position and payload of the element of @a branch
 * identified by @a eid.
 *
 * Set the element's parent EID, name and payload to @a new_parent_eid,
 * @a new_name and @a new_payload respectively.
 *
 * This may create a new element or alter an existing element.
 *
 * If the element ...                   we can describe the effect as ...
 *
 *   exists in the branch               =>  altering it;
 *   previously existed in the branch   =>  resurrecting it;
 *   only existed in other branches     =>  branching it;
 *   never existed anywhere             =>  creating or adding it.
 *
 * However, these are imprecise descriptions and not mutually exclusive.
 * For example, if it existed previously in this branch and another, then
 * we may describe the result as 'resurrecting' and/or as 'branching'.
 *
 * Duplicate @a new_name and @a new_payload into the branch's pool.
 */
svn_error_t *
svn_branch__state_alter_one(svn_branch__state_t *branch,
                            svn_branch__eid_t eid,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            const svn_element__payload_t *new_payload,
                            apr_pool_t *scratch_pool);

svn_error_t *
svn_branch__state_copy_tree(svn_branch__state_t *branch,
                            const svn_branch__rev_bid_eid_t *src_el_rev,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            apr_pool_t *scratch_pool);

/* Purge orphaned elements in BRANCH.
 */
svn_error_t *
svn_branch__state_purge(svn_branch__state_t *branch,
                        apr_pool_t *scratch_pool);

/* Get the merge history of BRANCH.
 */
svn_error_t *
svn_branch__state_get_history(svn_branch__state_t *branch,
                              svn_branch__history_t **merge_history_p,
                              apr_pool_t *result_pool);

/* Set the merge history of BRANCH.
 */
svn_error_t *
svn_branch__state_set_history(svn_branch__state_t *branch,
                              const svn_branch__history_t *merge_history,
                              apr_pool_t *scratch_pool);

/* Return the branch-relative path of element EID in BRANCH.
 *
 * If the element EID does not currently exist in BRANCH, return NULL.
 *
 * ### TODO: Clarify sequencing requirements.
 */
const char *
svn_branch__get_path_by_eid(const svn_branch__state_t *branch,
                            int eid,
                            apr_pool_t *result_pool);

/* Return the EID for the branch-relative path PATH in BRANCH.
 *
 * If no element of BRANCH is at this path, return -1.
 *
 * ### TODO: Clarify sequencing requirements.
 */
int
svn_branch__get_eid_by_path(const svn_branch__state_t *branch,
                            const char *path,
                            apr_pool_t *scratch_pool);

/* Get the default branching metadata for r0 of a new repository.
 */
svn_string_t *
svn_branch__get_default_r0_metadata(apr_pool_t *result_pool);

/* Create a new txn object *TXN_P, initialized with info
 * parsed from STREAM, allocated in RESULT_POOL.
 */
svn_error_t *
svn_branch__txn_parse(svn_branch__txn_t **txn_p,
                      svn_branch__repos_t *repos,
                      svn_stream_t *stream,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/* Write to STREAM a parseable representation of TXN.
 */
svn_error_t *
svn_branch__txn_serialize(svn_branch__txn_t *txn,
                          svn_stream_t *stream,
                          apr_pool_t *scratch_pool);

/* Write to STREAM a parseable representation of BRANCH.
 */
svn_error_t *
svn_branch__state_serialize(svn_stream_t *stream,
                            svn_branch__state_t *branch,
                            apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_H */
