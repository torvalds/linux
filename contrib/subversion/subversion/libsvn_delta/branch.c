/*
 * branch.c : Element-Based Branching and Move Tracking.
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

#include <assert.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_iter.h"
#include "svn_pools.h"

#include "private/svn_element.h"
#include "private/svn_branch.h"
#include "private/svn_branch_impl.h"
#include "private/svn_sorts_private.h"

#include "svn_private_config.h"


/* Is EID allocated (no matter whether an element with this id exists)? */
#define EID_IS_ALLOCATED(branch, eid) \
  ((eid) >= (branch)->txn->priv->first_eid \
   && (eid) < (branch)->txn->priv->next_eid)

#define IS_BRANCH_ROOT_EID(branch, eid) \
  ((eid) == (branch)->priv->element_tree->root_eid)

/* Is BRANCH1 the same branch as BRANCH2? Compare by full branch-ids; don't
   require identical branch objects. */
#define BRANCH_IS_SAME_BRANCH(branch1, branch2, scratch_pool) \
  (strcmp(svn_branch__get_id(branch1, scratch_pool), \
          svn_branch__get_id(branch2, scratch_pool)) == 0)

struct svn_branch__txn_priv_t
{
  /* All branches. */
  apr_array_header_t *branches;

  /* The range of element ids assigned. */
  /* EIDs local to the txn are negative, assigned by decrementing FIRST_EID
   * (skipping -1). */
  int first_eid, next_eid;

};

struct svn_branch__state_priv_t
{
  /* EID -> svn_element__content_t mapping. */
  svn_element__tree_t *element_tree;

  /* Merge history for this branch state. */
  svn_branch__history_t *history;

  svn_boolean_t is_flat;

};

static svn_branch__state_t *
branch_state_create(const char *bid,
                    int root_eid,
                    svn_branch__txn_t *txn,
                    apr_pool_t *result_pool);

static svn_error_t *
branch_instantiate_elements(svn_branch__state_t *to_branch,
                            const svn_element__tree_t *elements,
                            apr_pool_t *scratch_pool);

static svn_error_t *
svn_branch__map_add_subtree(svn_branch__state_t *to_branch,
                            int to_eid,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            svn_element__tree_t *new_subtree,
                            apr_pool_t *scratch_pool);

/*  */
static apr_pool_t *
branch_state_pool_get(svn_branch__state_t *branch)
{
  return apr_hash_pool_get(branch->priv->element_tree->e_map);
}

/* ### Layering: we didn't want to look at the whole repos in here, but
   copying seems to require it. */
svn_error_t *
svn_branch__repos_get_branch_by_id(svn_branch__state_t **branch_p,
                                  const svn_branch__repos_t *repos,
                                  svn_revnum_t revnum,
                                  const char *branch_id,
                                  apr_pool_t *scratch_pool);

/*  */
static svn_error_t *
branch_in_rev_or_txn(svn_branch__state_t **src_branch,
                     const svn_branch__rev_bid_eid_t *src_el_rev,
                     svn_branch__txn_t *txn,
                     apr_pool_t *result_pool)
{
  if (SVN_IS_VALID_REVNUM(src_el_rev->rev))
    {
      SVN_ERR(svn_branch__repos_get_branch_by_id(src_branch,
                                                 txn->repos,
                                                 src_el_rev->rev,
                                                 src_el_rev->bid,
                                                 result_pool));
    }
  else
    {
      *src_branch
        = svn_branch__txn_get_branch_by_id(txn, src_el_rev->bid, result_pool);
    }

  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static apr_array_header_t *
branch_txn_get_branches(const svn_branch__txn_t *txn,
                        apr_pool_t *result_pool)
{
  return apr_array_copy(result_pool, txn->priv->branches);
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_delete_branch(svn_branch__txn_t *txn,
                         const char *bid,
                         apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < txn->priv->branches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(txn->priv->branches, i, void *);

      if (strcmp(b->bid, bid) == 0)
        {
          svn_sort__array_delete(txn->priv->branches, i, 1);
          break;
        }
    }
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_get_num_new_eids(const svn_branch__txn_t *txn,
                            int *num_new_eids_p,
                            apr_pool_t *scratch_pool)
{
  if (num_new_eids_p)
    *num_new_eids_p = -1 - txn->priv->first_eid;
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_new_eid(svn_branch__txn_t *txn,
                   svn_branch__eid_t *eid_p,
                   apr_pool_t *scratch_pool)
{
  int eid = (txn->priv->first_eid < 0) ? txn->priv->first_eid - 1 : -2;

  txn->priv->first_eid = eid;
  if (eid_p)
    *eid_p = eid;
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_open_branch(svn_branch__txn_t *txn,
                       svn_branch__state_t **new_branch_p,
                       const char *branch_id,
                       int root_eid,
                       svn_branch__rev_bid_eid_t *tree_ref,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_branch__state_t *new_branch;

  /* if the branch already exists, just return it, else create it */
  new_branch
    = svn_branch__txn_get_branch_by_id(txn, branch_id, scratch_pool);
  if (new_branch)
    {
      SVN_ERR_ASSERT(root_eid == svn_branch__root_eid(new_branch));
    }
  else
    {
      SVN_ERR_ASSERT_NO_RETURN(root_eid != -1);

      new_branch = branch_state_create(branch_id, root_eid, txn,
                                       txn->priv->branches->pool);
      APR_ARRAY_PUSH(txn->priv->branches, void *) = new_branch;
    }

  if (tree_ref)
    {
      svn_branch__state_t *from_branch;
      svn_element__tree_t *tree;

      SVN_ERR(branch_in_rev_or_txn(&from_branch, tree_ref, txn, scratch_pool));
      /* Source branch must exist */
      if (! from_branch)
        {
          return svn_error_createf(SVN_BRANCH__ERR, NULL,
                                   _("Cannot branch from r%ld %s e%d: "
                                     "branch does not exist"),
                                   tree_ref->rev, tree_ref->bid, tree_ref->eid);
        }

      SVN_ERR_ASSERT(from_branch->priv->is_flat);

      SVN_ERR(svn_branch__state_get_elements(from_branch, &tree,
                                             scratch_pool));
      tree = svn_element__tree_get_subtree_at_eid(tree, tree_ref->eid,
                                                  scratch_pool);
      /* Source element must exist */
      if (! tree)
        {
          return svn_error_createf(SVN_BRANCH__ERR, NULL,
                                   _("Cannot branch from r%ld %s e%d: "
                                     "element does not exist"),
                                   tree_ref->rev, tree_ref->bid, tree_ref->eid);
        }

      /* Populate the tree from the 'from' source */
      SVN_ERR(branch_instantiate_elements(new_branch, tree, scratch_pool));
    }

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_sequence_point(svn_branch__txn_t *txn,
                          apr_pool_t *scratch_pool)
{
  int i;

  /* purge elements in each branch */
  for (i = 0; i < txn->priv->branches->nelts; i++)
    {
      svn_branch__state_t *b
        = APR_ARRAY_IDX(txn->priv->branches, i, void *);

      SVN_ERR(svn_branch__state_purge(b, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_complete(svn_branch__txn_t *txn,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_abort(svn_branch__txn_t *txn,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Branch Txn Object
 * ========================================================================
 */

apr_array_header_t *
svn_branch__txn_get_branches(const svn_branch__txn_t *txn,
                             apr_pool_t *result_pool)
{
  apr_array_header_t *branches
    = txn->vtable->get_branches(txn,
                                result_pool);
  return branches;
}

svn_error_t *
svn_branch__txn_delete_branch(svn_branch__txn_t *txn,
                              const char *bid,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->delete_branch(txn,
                                    bid,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_get_num_new_eids(const svn_branch__txn_t *txn,
                                 int *num_new_eids_p,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->get_num_new_eids(txn,
                                        num_new_eids_p,
                                        scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_new_eid(svn_branch__txn_t *txn,
                        int *new_eid_p,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->new_eid(txn,
                               new_eid_p,
                               scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_open_branch(svn_branch__txn_t *txn,
                            svn_branch__state_t **new_branch_p,
                            const char *branch_id,
                            int root_eid,
                            svn_branch__rev_bid_eid_t *tree_ref,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->open_branch(txn,
                                   new_branch_p,
                                   branch_id,
                                   root_eid, tree_ref, result_pool,
                                   scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_finalize_eids(svn_branch__txn_t *txn,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->finalize_eids(txn,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_serialize(svn_branch__txn_t *txn,
                          svn_stream_t *stream,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->serialize(txn,
                                 stream,
                                 scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_sequence_point(svn_branch__txn_t *txn,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->sequence_point(txn,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_complete(svn_branch__txn_t *txn,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->complete(txn,
                                scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_abort(svn_branch__txn_t *txn,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->abort(txn,
                             scratch_pool));
  return SVN_NO_ERROR;
}

svn_branch__txn_t *
svn_branch__txn_create(const svn_branch__txn_vtable_t *vtable,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *result_pool)
{
  svn_branch__txn_t *txn = apr_pcalloc(result_pool, sizeof(*txn));

  txn->vtable = apr_pmemdup(result_pool, vtable, sizeof(*vtable));

  txn->vtable->vpriv.cancel_func = cancel_func;
  txn->vtable->vpriv.cancel_baton = cancel_baton;

#ifdef ENABLE_ORDERING_CHECK
  txn->vtable->vpriv.within_callback = FALSE;
  txn->vtable->vpriv.finished = FALSE;
  txn->vtable->vpriv.state_pool = result_pool;
#endif

  return txn;
}

/*
 * ========================================================================
 */

/*  */
static const char *
branch_finalize_bid(const char *bid,
                    int mapping_offset,
                    apr_pool_t *result_pool)
{
  const char *outer_bid;
  int outer_eid;

  svn_branch__id_unnest(&outer_bid, &outer_eid, bid, result_pool);

  if (outer_bid)
    {
      outer_bid = branch_finalize_bid(outer_bid, mapping_offset, result_pool);
    }

  if (outer_eid < -1)
    {
      outer_eid = mapping_offset - outer_eid;
    }

  return svn_branch__id_nest(outer_bid, outer_eid, result_pool);
}

/* Change txn-local EIDs (negative integers) in BRANCH to revision EIDs, by
 * assigning a new revision-EID (positive integer) for each one.
 */
static svn_error_t *
branch_finalize_eids(svn_branch__state_t *branch,
                     int mapping_offset,
                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  branch->bid = branch_finalize_bid(branch->bid, mapping_offset,
                                    branch_state_pool_get(branch));
  if (branch->priv->element_tree->root_eid < -1)
    {
      branch->priv->element_tree->root_eid
        = mapping_offset - branch->priv->element_tree->root_eid;
    }

  for (hi = apr_hash_first(scratch_pool, branch->priv->element_tree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int old_eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *element = apr_hash_this_val(hi);

      if (old_eid < -1)
        {
          int new_eid = mapping_offset - old_eid;

          svn_element__tree_set(branch->priv->element_tree, old_eid, NULL);
          svn_element__tree_set(branch->priv->element_tree, new_eid, element);
        }
      if (element->parent_eid < -1)
        {
          element->parent_eid = mapping_offset - element->parent_eid;
        }
    }
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
branch_txn_finalize_eids(svn_branch__txn_t *txn,
                         apr_pool_t *scratch_pool)
{
  int n_txn_eids = (-1) - txn->priv->first_eid;
  int mapping_offset;
  apr_array_header_t *branches = branch_txn_get_branches(txn, scratch_pool);
  int i;

  if (txn->priv->first_eid == 0)
    return SVN_NO_ERROR;

  /* mapping from txn-local (negative) EID to committed (positive) EID is:
       txn_local_eid == -2  =>  committed_eid := (txn.next_eid + 0)
       txn_local_eid == -3  =>  committed_eid := (txn.next_eid + 1) ... */
  mapping_offset = txn->priv->next_eid - 2;

  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(branches, i, void *);

      SVN_ERR(branch_finalize_eids(b, mapping_offset, scratch_pool));
    }

  txn->priv->next_eid += n_txn_eids;
  txn->priv->first_eid = 0;
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

static svn_error_t *
branch_txn_serialize(svn_branch__txn_t *txn,
                     svn_stream_t *stream,
                     apr_pool_t *scratch_pool)
{
  apr_array_header_t *branches = branch_txn_get_branches(txn, scratch_pool);
  int i;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "r%ld: eids %d %d "
                            "branches %d\n",
                            txn->rev,
                            txn->priv->first_eid, txn->priv->next_eid,
                            branches->nelts));

  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *branch = APR_ARRAY_IDX(branches, i, void *);

      SVN_ERR(svn_branch__state_serialize(stream, branch, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

svn_branch__state_t *
svn_branch__txn_get_branch_by_id(const svn_branch__txn_t *txn,
                                 const char *branch_id,
                                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *branches = svn_branch__txn_get_branches(txn, scratch_pool);
  int i;
  svn_branch__state_t *branch = NULL;

  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(branches, i, void *);

      if (strcmp(svn_branch__get_id(b, scratch_pool), branch_id) == 0)
        {
          branch = b;
          break;
        }
    }
  return branch;
}

/*
 * ========================================================================
 */

/* Create a new branch txn object.
 *
 * It will have no branches.
 */
static svn_branch__txn_t *
branch_txn_create(svn_branch__repos_t *repos,
                  svn_revnum_t rev,
                  svn_revnum_t base_rev,
                  apr_pool_t *result_pool)
{
  static const svn_branch__txn_vtable_t vtable = {
    {0},
    branch_txn_get_branches,
    branch_txn_delete_branch,
    branch_txn_get_num_new_eids,
    branch_txn_new_eid,
    branch_txn_open_branch,
    branch_txn_finalize_eids,
    branch_txn_serialize,
    branch_txn_sequence_point,
    branch_txn_complete,
    branch_txn_abort,
  };
  svn_branch__txn_t *txn
    = svn_branch__txn_create(&vtable, NULL, NULL, result_pool);

  txn->priv = apr_pcalloc(result_pool, sizeof(*txn->priv));
  txn->repos = repos;
  txn->rev = rev;
  txn->base_rev = base_rev;
  txn->priv->branches = apr_array_make(result_pool, 0, sizeof(void *));
  return txn;
}

/*
 * ========================================================================
 */

static void
branch_validate_element(const svn_branch__state_t *branch,
                        int eid,
                        const svn_element__content_t *element);

/* Assert BRANCH satisfies all its invariants.
 */
static void
assert_branch_state_invariants(const svn_branch__state_t *branch,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  assert(branch->bid);
  assert(branch->txn);
  assert(branch->priv->element_tree);
  assert(branch->priv->element_tree->e_map);

  /* Validate elements in the map */
  for (hi = apr_hash_first(scratch_pool, branch->priv->element_tree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      branch_validate_element(branch, svn_eid__hash_this_key(hi),
                              apr_hash_this_val(hi));
    }
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_copy_one(svn_branch__state_t *branch,
                      const svn_branch__rev_bid_eid_t *src_el_rev,
                      svn_branch__eid_t eid,
                      svn_branch__eid_t new_parent_eid,
                      const char *new_name,
                      const svn_element__payload_t *new_payload,
                      apr_pool_t *scratch_pool)
{
  /* New payload shall be the same as the source if NEW_PAYLOAD is null. */
  /* ### if (! new_payload)
    {
      new_payload = branch_map_get(branch, eid)->payload;
    }
   */

  return SVN_NO_ERROR;
}

/* Copy a subtree.
 *
 * Adjust TO_BRANCH and its subbranches (recursively), to reflect a copy
 * of a subtree from FROM_EL_REV to TO_PARENT_EID:TO_NAME.
 *
 * FROM_EL_REV must be an existing element. (It may be a branch root.)
 *
 * ### TODO:
 * If FROM_EL_REV is the root of a subbranch and/or contains nested
 * subbranches, also copy them ...
 * ### What shall we do with a subbranch? Make plain copies of its raw
 *     elements; make a subbranch by branching the source subbranch?
 *
 * TO_PARENT_EID must be a directory element in TO_BRANCH, and TO_NAME a
 * non-existing path in it.
 */
static svn_error_t *
copy_subtree(const svn_branch__el_rev_id_t *from_el_rev,
             svn_branch__state_t *to_branch,
             svn_branch__eid_t to_parent_eid,
             const char *to_name,
             apr_pool_t *scratch_pool)
{
  svn_element__tree_t *new_subtree;

  SVN_ERR_ASSERT(from_el_rev->branch->priv->is_flat);

  SVN_ERR(svn_branch__state_get_elements(from_el_rev->branch, &new_subtree,
                                         scratch_pool));
  new_subtree = svn_element__tree_get_subtree_at_eid(new_subtree,
                                                     from_el_rev->eid,
                                                     scratch_pool);

  /* copy the subtree, assigning new EIDs */
  SVN_ERR(svn_branch__map_add_subtree(to_branch, -1 /*to_eid*/,
                                      to_parent_eid, to_name,
                                      new_subtree,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_copy_tree(svn_branch__state_t *to_branch,
                       const svn_branch__rev_bid_eid_t *src_el_rev,
                       svn_branch__eid_t new_parent_eid,
                       const char *new_name,
                       apr_pool_t *scratch_pool)
{
  svn_branch__txn_t *txn = to_branch->txn;
  svn_branch__state_t *src_branch;
  svn_branch__el_rev_id_t *from_el_rev;

  SVN_ERR(branch_in_rev_or_txn(&src_branch, src_el_rev, txn, scratch_pool));
  from_el_rev = svn_branch__el_rev_id_create(src_branch, src_el_rev->eid,
                                             src_el_rev->rev, scratch_pool);
  SVN_ERR(copy_subtree(from_el_rev,
                       to_branch, new_parent_eid, new_name,
                       scratch_pool));

  return SVN_NO_ERROR;
}

const char *
svn_branch__get_id(const svn_branch__state_t *branch,
                   apr_pool_t *result_pool)
{
  return branch->bid;
}

int
svn_branch__root_eid(const svn_branch__state_t *branch)
{
  svn_element__tree_t *elements;

  svn_error_clear(svn_branch__state_get_elements(branch, &elements,
                                                 NULL/*scratch_pool*/));
  return elements->root_eid;
}

svn_branch__el_rev_id_t *
svn_branch__el_rev_id_create(svn_branch__state_t *branch,
                             int eid,
                             svn_revnum_t rev,
                             apr_pool_t *result_pool)
{
  svn_branch__el_rev_id_t *id = apr_palloc(result_pool, sizeof(*id));

  id->branch = branch;
  id->eid = eid;
  id->rev = rev;
  return id;
}

svn_branch__el_rev_id_t *
svn_branch__el_rev_id_dup(const svn_branch__el_rev_id_t *old_id,
                          apr_pool_t *result_pool)
{
  if (! old_id)
    return NULL;

  return svn_branch__el_rev_id_create(old_id->branch,
                                      old_id->eid,
                                      old_id->rev,
                                      result_pool);
}

svn_branch__rev_bid_eid_t *
svn_branch__rev_bid_eid_create(svn_revnum_t rev,
                               const char *branch_id,
                               int eid,
                               apr_pool_t *result_pool)
{
  svn_branch__rev_bid_eid_t *id = apr_palloc(result_pool, sizeof(*id));

  id->bid = apr_pstrdup(result_pool, branch_id);
  id->eid = eid;
  id->rev = rev;
  return id;
}

svn_branch__rev_bid_eid_t *
svn_branch__rev_bid_eid_dup(const svn_branch__rev_bid_eid_t *old_id,
                            apr_pool_t *result_pool)
{
  svn_branch__rev_bid_eid_t *id;

  if (! old_id)
    return NULL;

  id = apr_pmemdup(result_pool, old_id, sizeof(*id));
  id->bid = apr_pstrdup(result_pool, old_id->bid);
  return id;
}

svn_branch__rev_bid_t *
svn_branch__rev_bid_create(svn_revnum_t rev,
                           const char *branch_id,
                           apr_pool_t *result_pool)
{
  svn_branch__rev_bid_t *id = apr_palloc(result_pool, sizeof(*id));

  id->bid = apr_pstrdup(result_pool, branch_id);
  id->rev = rev;
  return id;
}

svn_branch__rev_bid_t *
svn_branch__rev_bid_dup(const svn_branch__rev_bid_t *old_id,
                        apr_pool_t *result_pool)
{
  svn_branch__rev_bid_t *id;

  if (! old_id)
    return NULL;

  id = apr_pmemdup(result_pool, old_id, sizeof(*id));
  id->bid = apr_pstrdup(result_pool, old_id->bid);
  return id;
}

svn_boolean_t
svn_branch__rev_bid_equal(const svn_branch__rev_bid_t *id1,
                          const svn_branch__rev_bid_t *id2)
{
  return (id1->rev == id2->rev
          && strcmp(id1->bid, id2->bid) == 0);
}

svn_branch__history_t *
svn_branch__history_create_empty(apr_pool_t *result_pool)
{
  svn_branch__history_t *history
    = svn_branch__history_create(NULL, result_pool);

  return history;
}

svn_branch__history_t *
svn_branch__history_create(apr_hash_t *parents,
                           apr_pool_t *result_pool)
{
  svn_branch__history_t *history
    = apr_pcalloc(result_pool, sizeof(*history));

  history->parents = apr_hash_make(result_pool);
  if (parents)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(result_pool, parents);
           hi; hi = apr_hash_next(hi))
        {
          const char *bid = apr_hash_this_key(hi);
          svn_branch__rev_bid_t *val = apr_hash_this_val(hi);

          svn_hash_sets(history->parents,
                        apr_pstrdup(result_pool, bid),
                        svn_branch__rev_bid_dup(val, result_pool));
        }
    }
  return history;
}

svn_branch__history_t *
svn_branch__history_dup(const svn_branch__history_t *old,
                        apr_pool_t *result_pool)
{
  svn_branch__history_t *history = NULL;

  if (old)
    {
      history
        = svn_branch__history_create(old->parents, result_pool);
    }
  return history;
}


/*
 * ========================================================================
 * Branch mappings
 * ========================================================================
 */

/* Validate that ELEMENT is suitable for a mapping of BRANCH:EID.
 * ELEMENT->payload may be null.
 */
static void
branch_validate_element(const svn_branch__state_t *branch,
                        int eid,
                        const svn_element__content_t *element)
{
  SVN_ERR_ASSERT_NO_RETURN(element);

  /* Parent EID must be valid and different from this element's EID, or -1
     iff this is the branch root element. */
  SVN_ERR_ASSERT_NO_RETURN(
    IS_BRANCH_ROOT_EID(branch, eid)
    ? (element->parent_eid == -1)
    : (element->parent_eid != eid
       && EID_IS_ALLOCATED(branch, element->parent_eid)));

  /* Element name must be given, and empty iff EID is the branch root. */
  SVN_ERR_ASSERT_NO_RETURN(
    element->name
    && IS_BRANCH_ROOT_EID(branch, eid) == (*element->name == '\0'));

  SVN_ERR_ASSERT_NO_RETURN(svn_element__payload_invariants(element->payload));
  if (element->payload->is_subbranch_root)
    {
      /* a subbranch root element must not be the branch root element */
      SVN_ERR_ASSERT_NO_RETURN(! IS_BRANCH_ROOT_EID(branch, eid));
    }
}

static svn_error_t *
branch_state_get_elements(const svn_branch__state_t *branch,
                          svn_element__tree_t **element_tree_p,
                          apr_pool_t *result_pool)
{
  *element_tree_p = branch->priv->element_tree;
  return SVN_NO_ERROR;
}

static svn_element__content_t *
branch_get_element(const svn_branch__state_t *branch,
                   int eid)
{
  svn_element__content_t *element;

  element = svn_element__tree_get(branch->priv->element_tree, eid);

  if (element)
    branch_validate_element(branch, eid, element);
  return element;
}

static svn_error_t *
branch_state_get_element(const svn_branch__state_t *branch,
                         svn_element__content_t **element_p,
                         int eid,
                         apr_pool_t *result_pool)
{
  *element_p = branch_get_element(branch, eid);
  return SVN_NO_ERROR;
}

/* In BRANCH, set element EID to ELEMENT.
 *
 * If ELEMENT is null, delete element EID.
 *
 * Assume ELEMENT is already allocated with sufficient lifetime.
 */
static void
branch_map_set(svn_branch__state_t *branch,
               int eid,
               const svn_element__content_t *element)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->priv->element_tree->e_map);

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));
  if (element)
    branch_validate_element(branch, eid, element);

  svn_element__tree_set(branch->priv->element_tree, eid, element);
  branch->priv->is_flat = FALSE;
  assert_branch_state_invariants(branch, map_pool);
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_set_element(svn_branch__state_t *branch,
                         svn_branch__eid_t eid,
                         const svn_element__content_t *element,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->priv->element_tree->e_map);

  /* EID must be a valid element id */
  SVN_ERR_ASSERT(EID_IS_ALLOCATED(branch, eid));

  if (element)
    {
      element = svn_element__content_dup(element, map_pool);

      /* NEW_PAYLOAD must be specified, either in full or by reference */
      SVN_ERR_ASSERT(element->payload);

      if ((element->parent_eid == -1) != IS_BRANCH_ROOT_EID(branch, eid)
          || (*element->name == '\0') != IS_BRANCH_ROOT_EID(branch, eid))
        {
          return svn_error_createf(SVN_BRANCH__ERR, NULL,
                                   _("Cannot set e%d to (parent=e%d, name='%s'): "
                                     "branch root is e%d"),
                                   eid, element->parent_eid, element->name,
                                   branch->priv->element_tree->root_eid);
        }
    }

  /* Insert the new version */
  branch_map_set(branch, eid, element);
  return SVN_NO_ERROR;
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_purge(svn_branch__state_t *branch,
                   apr_pool_t *scratch_pool)
{
  svn_element__tree_purge_orphans(branch->priv->element_tree->e_map,
                                  branch->priv->element_tree->root_eid,
                                  scratch_pool);
  branch->priv->is_flat = TRUE;
  return SVN_NO_ERROR;
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_get_history(svn_branch__state_t *branch,
                         svn_branch__history_t **history_p,
                         apr_pool_t *result_pool)
{
  if (history_p)
    {
      *history_p
        = svn_branch__history_dup(branch->priv->history, result_pool);
    }
  return SVN_NO_ERROR;
}

/* An #svn_branch__state_t method. */
static svn_error_t *
branch_state_set_history(svn_branch__state_t *branch,
                         const svn_branch__history_t *history,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *branch_pool = branch_state_pool_get(branch);

  branch->priv->history
    = svn_branch__history_dup(history, branch_pool);
  return SVN_NO_ERROR;
}

const char *
svn_branch__get_path_by_eid(const svn_branch__state_t *branch,
                            int eid,
                            apr_pool_t *result_pool)
{
  svn_element__tree_t *elements;

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));
  /*SVN_ERR_ASSERT_NO_RETURN(branch->priv->is_flat);*/

  svn_error_clear(svn_branch__state_get_elements(branch, &elements, result_pool));
  return svn_element__tree_get_path_by_eid(elements, eid, result_pool);
}

int
svn_branch__get_eid_by_path(const svn_branch__state_t *branch,
                            const char *path,
                            apr_pool_t *scratch_pool)
{
  svn_element__tree_t *elements;
  apr_hash_index_t *hi;

  /*SVN_ERR_ASSERT_NO_RETURN(branch->priv->is_flat);*/

  /* ### This is a crude, linear search */
  svn_error_clear(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      const char *this_path = svn_element__tree_get_path_by_eid(elements, eid,
                                                                scratch_pool);

      if (! this_path)
        {
          /* Mapping is not complete; this element is in effect not present. */
          continue;
        }
      if (strcmp(path, this_path) == 0)
        {
          return eid;
        }
    }

  return -1;
}

/* Create a copy of NEW_SUBTREE in TO_BRANCH.
 *
 * For each non-root element in NEW_SUBTREE, create a new element with
 * a new EID, no matter what EID is used to represent it in NEW_SUBTREE.
 *
 * For the new subtree root element, if TO_EID is -1, generate a new EID,
 * otherwise alter (if it exists) or instantiate the element TO_EID.
 *
 * Set the new subtree root element's parent to NEW_PARENT_EID and name to
 * NEW_NAME.
 */
static svn_error_t *
svn_branch__map_add_subtree(svn_branch__state_t *to_branch,
                            int to_eid,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            svn_element__tree_t *new_subtree,
                            apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_element__content_t *new_root_content;

  /* Get a new EID for the root element, if not given. */
  if (to_eid == -1)
    {
      SVN_ERR(svn_branch__txn_new_eid(to_branch->txn, &to_eid,
                                      scratch_pool));
    }

  /* Create the new subtree root element */
  new_root_content = svn_element__tree_get(new_subtree, new_subtree->root_eid);
  new_root_content = svn_element__content_create(new_parent_eid, new_name,
                                                 new_root_content->payload,
                                                 scratch_pool);
  SVN_ERR(branch_state_set_element(to_branch, to_eid, new_root_content,
                                   scratch_pool));

  /* Process its immediate children */
  for (hi = apr_hash_first(scratch_pool, new_subtree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_from_eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *from_element = apr_hash_this_val(hi);

      if (from_element->parent_eid == new_subtree->root_eid)
        {
          svn_element__tree_t *this_subtree;

          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          this_subtree
            = svn_element__tree_create(new_subtree->e_map, this_from_eid,
                                       scratch_pool);
          SVN_ERR(svn_branch__map_add_subtree(to_branch, -1 /*to_eid*/,
                                              to_eid, from_element->name,
                                              this_subtree, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Instantiate elements in a branch.
 *
 * In TO_BRANCH, instantiate (or alter, if existing) each element of
 * ELEMENTS, each with its given tree structure (parent, name) and payload.
 */
static svn_error_t *
branch_instantiate_elements(svn_branch__state_t *to_branch,
                            const svn_element__tree_t *elements,
                            apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *this_element = apr_hash_this_val(hi);

      branch_map_set(to_branch, this_eid,
                     svn_element__content_dup(
                       this_element,
                       apr_hash_pool_get(to_branch->priv->element_tree->e_map)));
    }

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Branch State Object
 * ========================================================================
 */

svn_error_t *
svn_branch__state_get_elements(const svn_branch__state_t *branch,
                               svn_element__tree_t **element_tree_p,
                               apr_pool_t *result_pool)
{
  SVN_ERR(branch->vtable->get_elements(branch,
                                       element_tree_p,
                                       result_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_get_element(const svn_branch__state_t *branch,
                              svn_element__content_t **element_p,
                              int eid,
                              apr_pool_t *result_pool)
{
  SVN_ERR(branch->vtable->get_element(branch,
                                      element_p, eid, result_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_set_element(svn_branch__state_t *branch,
                              int eid,
                              const svn_element__content_t *element,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->set_element(branch,
                                      eid, element,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_alter_one(svn_branch__state_t *branch,
                            svn_branch__eid_t eid,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            const svn_element__payload_t *new_payload,
                            apr_pool_t *scratch_pool)
{
  svn_element__content_t *element
    = svn_element__content_create(new_parent_eid, new_name, new_payload,
                                  scratch_pool);

  SVN_ERR(svn_branch__state_set_element(branch, eid, element, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_copy_tree(svn_branch__state_t *branch,
                            const svn_branch__rev_bid_eid_t *src_el_rev,
                            svn_branch__eid_t new_parent_eid,
                            const char *new_name,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->copy_tree(branch,
                                    src_el_rev, new_parent_eid, new_name,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_delete_one(svn_branch__state_t *branch,
                             svn_branch__eid_t eid,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_branch__state_set_element(branch, eid, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_purge(svn_branch__state_t *branch,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->purge(branch,
                                scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_get_history(svn_branch__state_t *branch,
                              svn_branch__history_t **history_p,
                              apr_pool_t *result_pool)
{
  SVN_ERR(branch->vtable->get_history(branch,
                                      history_p,
                                      result_pool));
  SVN_ERR_ASSERT(*history_p);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__state_set_history(svn_branch__state_t *branch,
                              const svn_branch__history_t *history,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(history);
  SVN_ERR(branch->vtable->set_history(branch,
                                      history,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

svn_branch__state_t *
svn_branch__state_create(const svn_branch__state_vtable_t *vtable,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *result_pool)
{
  svn_branch__state_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->vtable = apr_pmemdup(result_pool, vtable, sizeof(*vtable));

  b->vtable->vpriv.cancel_func = cancel_func;
  b->vtable->vpriv.cancel_baton = cancel_baton;

#ifdef ENABLE_ORDERING_CHECK
  b->vtable->vpriv.within_callback = FALSE;
  b->vtable->vpriv.finished = FALSE;
  b->vtable->vpriv.state_pool = result_pool;
#endif

  return b;
}

/* Create a new branch state object.
 *
 * It will have no elements (not even a root element).
 */
static svn_branch__state_t *
branch_state_create(const char *bid,
                    int root_eid,
                    svn_branch__txn_t *txn,
                    apr_pool_t *result_pool)
{
  static const svn_branch__state_vtable_t vtable = {
    {0},
    branch_state_get_elements,
    branch_state_get_element,
    branch_state_set_element,
    branch_state_copy_one,
    branch_state_copy_tree,
    branch_state_purge,
    branch_state_get_history,
    branch_state_set_history,
  };
  svn_branch__state_t *b
    = svn_branch__state_create(&vtable, NULL, NULL, result_pool);

  b->priv = apr_pcalloc(result_pool, sizeof(*b->priv));
  b->bid = apr_pstrdup(result_pool, bid);
  b->txn = txn;
  b->priv->element_tree = svn_element__tree_create(NULL, root_eid, result_pool);
  assert_branch_state_invariants(b, result_pool);
  b->priv->is_flat = TRUE;
  b->priv->history = svn_branch__history_create_empty(result_pool);
  return b;
}

/*
 * ========================================================================
 * Parsing and Serializing
 * ========================================================================
 */

svn_string_t *
svn_branch__get_default_r0_metadata(apr_pool_t *result_pool)
{
  static const char *default_repos_info
    = "r0: eids 0 1 branches 1\n"
      "B0 root-eid 0 num-eids 1\n"
      "history: parents 0\n"
      "e0: normal -1 .\n";

  return svn_string_create(default_repos_info, result_pool);
}

/*  */
static svn_error_t *
parse_branch_line(char *bid_p,
                  int *root_eid_p,
                  int *num_eids_p,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "%s root-eid %d num-eids %d",
             bid_p, root_eid_p, num_eids_p);
  SVN_ERR_ASSERT(n == 3);

  return SVN_NO_ERROR;
}

/* Parse the history metadata for BRANCH.
 */
static svn_error_t *
history_parse(svn_branch__history_t **history_p,
              svn_stream_t *stream,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_branch__history_t *history
    = svn_branch__history_create_empty(result_pool);
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int num_parents;
  int i;

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "history: parents %d",
             &num_parents);
  SVN_ERR_ASSERT(n == 1);

  for (i = 0; i < num_parents; i++)
    {
      svn_revnum_t rev;
      char bid[100];

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
      SVN_ERR_ASSERT(!eof);

      n = sscanf(line->data, "parent: r%ld.%99s",
                 &rev, bid);
      SVN_ERR_ASSERT(n == 2);

      svn_hash_sets(history->parents,
                    apr_pstrdup(result_pool, bid),
                    svn_branch__rev_bid_create(rev, bid, result_pool));
    }

  if (history_p)
    *history_p = history;
  return SVN_NO_ERROR;
}

/* Parse the mapping for one element.
 */
static svn_error_t *
parse_element_line(int *eid_p,
                   svn_boolean_t *is_subbranch_p,
                   int *parent_eid_p,
                   const char **name_p,
                   svn_stream_t *stream,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  char kind[10];
  int n;
  int offset;

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "e%d: %9s %d%n",
             eid_p,
             kind, parent_eid_p, &offset);
  SVN_ERR_ASSERT(n >= 3);  /* C std is unclear on whether '%n' counts */
  SVN_ERR_ASSERT(line->data[offset] == ' ');

  *name_p = apr_pstrdup(result_pool, line->data + offset + 1);
  *is_subbranch_p = (strcmp(kind, "subbranch") == 0);

  if (strcmp(*name_p, "(null)") == 0)
    *name_p = NULL;
  else if (strcmp(*name_p, ".") == 0)
    *name_p = "";

  return SVN_NO_ERROR;
}

const char *
svn_branch__id_nest(const char *outer_bid,
                    int outer_eid,
                    apr_pool_t *result_pool)
{
  if (!outer_bid)
    return apr_psprintf(result_pool, "B%d", outer_eid);

  return apr_psprintf(result_pool, "%s.%d", outer_bid, outer_eid);
}

void
svn_branch__id_unnest(const char **outer_bid,
                      int *outer_eid,
                      const char *bid,
                      apr_pool_t *result_pool)
{
  char *last_dot = strrchr(bid, '.');

  if (last_dot) /* BID looks like "B3.11" or "B3.11.22" etc. */
    {
      *outer_bid = apr_pstrndup(result_pool, bid, last_dot - bid);
      *outer_eid = atoi(last_dot + 1);
    }
  else /* looks like "B0" or B22" (with no dot) */
    {
      *outer_bid = NULL;
      *outer_eid = atoi(bid + 1);
    }
}

/* Create a new branch *NEW_BRANCH, initialized
 * with info parsed from STREAM, allocated in RESULT_POOL.
 */
static svn_error_t *
svn_branch__state_parse(svn_branch__state_t **new_branch,
                       svn_branch__txn_t *txn,
                       svn_stream_t *stream,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  char bid[1000];
  int root_eid, num_eids;
  svn_branch__state_t *branch_state;
  int i;

  SVN_ERR(parse_branch_line(bid, &root_eid, &num_eids,
                            stream, scratch_pool, scratch_pool));

  branch_state = branch_state_create(bid, root_eid, txn,
                                     result_pool);

  /* Read in the merge history. */
  SVN_ERR(history_parse(&branch_state->priv->history,
                        stream, result_pool, scratch_pool));

  /* Read in the structure. Set the payload of each normal element to a
     (branch-relative) reference. */
  for (i = 0; i < num_eids; i++)
    {
      int eid, this_parent_eid;
      const char *this_name;
      svn_boolean_t is_subbranch;

      SVN_ERR(parse_element_line(&eid,
                                 &is_subbranch, &this_parent_eid, &this_name,
                                 stream, scratch_pool, scratch_pool));

      if (this_name)
        {
          svn_element__payload_t *payload;
          svn_element__content_t *element;

          if (! is_subbranch)
            {
              payload = svn_element__payload_create_ref(txn->rev, bid, eid,
                                                        result_pool);
            }
          else
            {
              payload
                = svn_element__payload_create_subbranch(result_pool);
            }
          element = svn_element__content_create(this_parent_eid,
                                                this_name, payload,
                                                scratch_pool);
          SVN_ERR(branch_state_set_element(branch_state, eid, element,
                                           scratch_pool));
        }
    }

  branch_state->priv->is_flat = TRUE;
  *new_branch = branch_state;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__txn_parse(svn_branch__txn_t **txn_p,
                      svn_branch__repos_t *repos,
                      svn_stream_t *stream,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_branch__txn_t *txn;
  svn_revnum_t rev;
  int first_eid, next_eid;
  int num_branches;
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int j;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "r%ld: eids %d %d "
                         "branches %d",
             &rev,
             &first_eid, &next_eid,
             &num_branches);
  SVN_ERR_ASSERT(n == 4);

  txn = branch_txn_create(repos, rev, rev - 1, result_pool);
  txn->priv->first_eid = first_eid;
  txn->priv->next_eid = next_eid;

  /* parse the branches */
  for (j = 0; j < num_branches; j++)
    {
      svn_branch__state_t *branch;

      SVN_ERR(svn_branch__state_parse(&branch, txn, stream,
                                      result_pool, scratch_pool));
      APR_ARRAY_PUSH(txn->priv->branches, void *) = branch;
    }

  *txn_p = txn;
  return SVN_NO_ERROR;
}

/* Serialize the history metadata for BRANCH.
 */
static svn_error_t *
history_serialize(svn_stream_t *stream,
                  svn_branch__history_t *history,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *ancestors_sorted;
  int i;

  /* Write entries in sorted order for stability -- so that for example
     we can test parse-then-serialize by expecting identical output. */
  ancestors_sorted = svn_sort__hash(history->parents,
                                    svn_sort_compare_items_lexically,
                                    scratch_pool);
  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "history: parents %d\n",
                            ancestors_sorted->nelts));
  for (i = 0; i < ancestors_sorted->nelts; i++)
    {
      svn_sort__item_t *item
        = &APR_ARRAY_IDX(ancestors_sorted, i, svn_sort__item_t);
      svn_branch__rev_bid_t *rev_bid = item->value;

      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "parent: r%ld.%s\n",
                                rev_bid->rev, rev_bid->bid));
    }

  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of BRANCH.
 */
svn_error_t *
svn_branch__state_serialize(svn_stream_t *stream,
                            svn_branch__state_t *branch,
                            apr_pool_t *scratch_pool)
{
  svn_eid__hash_iter_t *ei;

  SVN_ERR_ASSERT(branch->priv->is_flat);

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "%s root-eid %d num-eids %d\n",
                            svn_branch__get_id(branch, scratch_pool),
                            branch->priv->element_tree->root_eid,
                            apr_hash_count(branch->priv->element_tree->e_map)));

  SVN_ERR(history_serialize(stream, branch->priv->history,
                                  scratch_pool));

  for (SVN_EID__HASH_ITER_SORTED_BY_EID(ei, branch->priv->element_tree->e_map,
                                        scratch_pool))
    {
      int eid = ei->eid;
      svn_element__content_t *element = branch_get_element(branch, eid);
      int parent_eid;
      const char *name;

      SVN_ERR_ASSERT(element);
      parent_eid = element->parent_eid;
      name = element->name[0] ? element->name : ".";
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "e%d: %s %d %s\n",
                                eid,
                                element ? ((! element->payload->is_subbranch_root)
                                             ? "normal" : "subbranch")
                                     : "none",
                                parent_eid, name));
    }
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

