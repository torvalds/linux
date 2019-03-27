/*
 * branch_nested.c : Nested Branches
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

#include "private/svn_branch_nested.h"
#include "private/svn_branch_impl.h"
#include "private/svn_branch_repos.h"

#include "svn_private_config.h"


void
svn_branch__get_outer_branch_and_eid(svn_branch__state_t **outer_branch_p,
                                     int *outer_eid_p,
                                     const svn_branch__state_t *branch,
                                     apr_pool_t *scratch_pool)
{
  const char *outer_bid;

  svn_branch__id_unnest(&outer_bid, outer_eid_p, branch->bid, scratch_pool);
  *outer_branch_p = NULL;
  if (outer_bid)
    {
      *outer_branch_p
        = svn_branch__txn_get_branch_by_id(branch->txn, outer_bid,
                                           scratch_pool);
    }
}

const char *
svn_branch__get_root_rrpath(const svn_branch__state_t *branch,
                            apr_pool_t *result_pool)
{
  svn_branch__state_t *outer_branch;
  int outer_eid;
  const char *root_rrpath;

  svn_branch__get_outer_branch_and_eid(&outer_branch, &outer_eid, branch,
                                       result_pool);
  if (outer_branch)
    {
      root_rrpath
        = svn_branch__get_rrpath_by_eid(outer_branch, outer_eid, result_pool);
    }
  else
    {
      root_rrpath = "";
    }

  SVN_ERR_ASSERT_NO_RETURN(root_rrpath);
  return root_rrpath;
}

const char *
svn_branch__get_rrpath_by_eid(const svn_branch__state_t *branch,
                              int eid,
                              apr_pool_t *result_pool)
{
  const char *path = svn_branch__get_path_by_eid(branch, eid, result_pool);
  const char *rrpath = NULL;

  if (path)
    {
      rrpath = svn_relpath_join(svn_branch__get_root_rrpath(branch, result_pool),
                                path, result_pool);
    }
  return rrpath;
}

svn_error_t *
svn_branch__get_subbranch_at_eid(svn_branch__state_t *branch,
                                 svn_branch__state_t **subbranch_p,
                                 int eid,
                                 apr_pool_t *scratch_pool)
{
  svn_element__content_t *element;

  SVN_ERR(svn_branch__state_get_element(branch, &element, eid, scratch_pool));
  if (element && element->payload->is_subbranch_root)
    {
      const char *branch_id = svn_branch__get_id(branch, scratch_pool);
      const char *subbranch_id = svn_branch__id_nest(branch_id, eid,
                                                     scratch_pool);

      *subbranch_p = svn_branch__txn_get_branch_by_id(branch->txn, subbranch_id,
                                                      scratch_pool);
    }
  else
    {
      *subbranch_p = NULL;
    }
  return SVN_NO_ERROR;
}

/* Set *SUBBRANCH_EIDS_P an array of EIDs of the subbranch-root elements in
 * BRANCH.
 */
static svn_error_t *
svn_branch__get_immediate_subbranch_eids(svn_branch__state_t *branch,
                                         apr_array_header_t **subbranch_eids_p,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranch_eids
    = apr_array_make(result_pool, 0, sizeof(int));
  svn_element__tree_t *elements;
  apr_hash_index_t *hi;

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *element = apr_hash_this_val(hi);

      if (element->payload->is_subbranch_root)
        {
          APR_ARRAY_PUSH(subbranch_eids, int) = eid;
        }
    }
  *subbranch_eids_p = subbranch_eids;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__get_immediate_subbranches(svn_branch__state_t *branch,
                                      apr_array_header_t **subbranches_p,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranch_eids;
  apr_array_header_t *subbranches
    = apr_array_make(result_pool, 0, sizeof(void *));
  const char *branch_id = svn_branch__get_id(branch, scratch_pool);
  int i;

  SVN_ERR(svn_branch__get_immediate_subbranch_eids(branch, &subbranch_eids,
                                                   scratch_pool, scratch_pool));
  for (i = 0; i < subbranch_eids->nelts; i++)
    {
      int eid = APR_ARRAY_IDX(subbranch_eids, i, int);
      const char *subbranch_id
        = svn_branch__id_nest(branch_id, eid, scratch_pool);
      svn_branch__state_t *subbranch
        = svn_branch__txn_get_branch_by_id(branch->txn, subbranch_id,
                                           scratch_pool);

      SVN_ERR_ASSERT_NO_RETURN(subbranch);
      APR_ARRAY_PUSH(subbranches, void *) = subbranch;
    }
  *subbranches_p = subbranches;
  return SVN_NO_ERROR;
}

svn_branch__subtree_t *
svn_branch__subtree_create(apr_hash_t *e_map,
                           int root_eid,
                           apr_pool_t *result_pool)
{
  svn_branch__subtree_t *subtree = apr_pcalloc(result_pool, sizeof(*subtree));

  subtree->tree = svn_element__tree_create(e_map, root_eid, result_pool);
  subtree->subbranches = apr_hash_make(result_pool);
  return subtree;
}

svn_error_t *
svn_branch__get_subtree(svn_branch__state_t *branch,
                        svn_branch__subtree_t **subtree_p,
                        int eid,
                        apr_pool_t *result_pool)
{
  svn_element__tree_t *element_tree;
  svn_branch__subtree_t *new_subtree;
  apr_array_header_t *subbranch_eids;
  int i;
  apr_pool_t *iterpool = result_pool;  /* ### not a proper iterpool */

  SVN_ERR(svn_branch__state_get_elements(branch, &element_tree, result_pool));
  element_tree = svn_element__tree_get_subtree_at_eid(element_tree, eid,
                                                      result_pool);
  new_subtree
    = svn_branch__subtree_create(element_tree->e_map, eid, result_pool);

  /* Add subbranches */
  SVN_ERR(svn_branch__get_immediate_subbranch_eids(branch, &subbranch_eids,
                                                   result_pool, result_pool));
  for (i = 0; i < subbranch_eids->nelts; i++)
    {
      int outer_eid = APR_ARRAY_IDX(subbranch_eids, i, int);
      const char *subbranch_relpath_in_subtree;

      subbranch_relpath_in_subtree
        = svn_element__tree_get_path_by_eid(new_subtree->tree, outer_eid,
                                            iterpool);

      /* Is it pathwise at or below EID? If so, add it into the subtree. */
      if (subbranch_relpath_in_subtree)
        {
          svn_branch__state_t *subbranch;
          svn_branch__subtree_t *this_subtree;

          SVN_ERR(svn_branch__get_subbranch_at_eid(branch, &subbranch,
                                                   outer_eid, iterpool));
          if (subbranch)
            {
              SVN_ERR(svn_branch__get_subtree(subbranch, &this_subtree,
                                              svn_branch__root_eid(subbranch),
                                              result_pool));
              svn_eid__hash_set(new_subtree->subbranches, outer_eid,
                                this_subtree);
            }
        }
    }
  *subtree_p = new_subtree;
  return SVN_NO_ERROR;
}

svn_branch__subtree_t *
svn_branch__subtree_get_subbranch_at_eid(svn_branch__subtree_t *subtree,
                                         int eid,
                                         apr_pool_t *result_pool)
{
  subtree = svn_eid__hash_get(subtree->subbranches, eid);

  return subtree;
}

/* Instantiate ELEMENTS in TO_BRANCH.
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

      SVN_ERR(svn_branch__state_set_element(to_branch, this_eid,
                                            this_element, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__instantiate_elements_r(svn_branch__state_t *to_branch,
                                   svn_branch__subtree_t elements,
                                   apr_pool_t *scratch_pool)
{
  SVN_ERR(branch_instantiate_elements(to_branch, elements.tree,
                                      scratch_pool));

  /* branch any subbranches */
  {
    apr_hash_index_t *hi;

    for (hi = apr_hash_first(scratch_pool, elements.subbranches);
         hi; hi = apr_hash_next(hi))
      {
        int this_outer_eid = svn_eid__hash_this_key(hi);
        svn_branch__subtree_t *this_subtree = apr_hash_this_val(hi);
        const char *new_branch_id;
        svn_branch__state_t *new_branch;
        /*### svn_branch__history_t *history;*/

        /* branch this subbranch into NEW_BRANCH (recursing) */
        new_branch_id = svn_branch__id_nest(to_branch->bid, this_outer_eid,
                                            scratch_pool);
        SVN_ERR(svn_branch__txn_open_branch(to_branch->txn, &new_branch,
                                            new_branch_id,
                                            this_subtree->tree->root_eid,
                                            NULL /*tree_ref*/,
                                            scratch_pool, scratch_pool));
        /*### SVN_ERR(svn_branch__state_set_history(new_branch, history,
                                              scratch_pool));*/

        SVN_ERR(svn_branch__instantiate_elements_r(new_branch, *this_subtree,
                                                   scratch_pool));
      }
  }

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

svn_error_t *
svn_branch__find_nested_branch_element_by_relpath(
                                svn_branch__state_t **branch_p,
                                int *eid_p,
                                svn_branch__state_t *root_branch,
                                const char *relpath,
                                apr_pool_t *scratch_pool)
{
  /* The path we're looking for is (path-wise) in this branch. See if it
     is also in a sub-branch. */
  /* Loop invariants: RELPATH is the path we're looking for, relative to
     ROOT_BRANCH which is the current level of nesting that we've descended
     into. */
  while (TRUE)
    {
      apr_array_header_t *subbranch_eids;
      int i;
      svn_boolean_t found = FALSE;

      SVN_ERR(svn_branch__get_immediate_subbranch_eids(
                root_branch, &subbranch_eids, scratch_pool, scratch_pool));
      for (i = 0; i < subbranch_eids->nelts; i++)
        {
          int outer_eid = APR_ARRAY_IDX(subbranch_eids, i, int);
          const char *relpath_to_subbranch;
          const char *relpath_in_subbranch;

          /* Check whether the RELPATH we're looking for is within this
             subbranch at OUTER_EID. If it is, recurse in the subbranch. */
          relpath_to_subbranch
            = svn_branch__get_path_by_eid(root_branch, outer_eid, scratch_pool);
          relpath_in_subbranch
            = svn_relpath_skip_ancestor(relpath_to_subbranch, relpath);
          if (relpath_in_subbranch)
            {
              svn_branch__state_t *subbranch;

              SVN_ERR(svn_branch__get_subbranch_at_eid(
                        root_branch, &subbranch, outer_eid, scratch_pool));
              /* If the branch hierarchy is not 'flat' then we might find
                 there is no actual branch where the subbranch-root element
                 says there should be one. In that case, ignore it. */
              if (subbranch)
                {
                  root_branch = subbranch;
                  relpath = relpath_in_subbranch;
                  found = TRUE;
                  break;
                }
            }
        }
      if (! found)
        {
          break;
        }
    }

  *branch_p = root_branch;
  if (eid_p)
    *eid_p = svn_branch__get_eid_by_path(root_branch, relpath, scratch_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__repos_find_el_rev_by_path_rev(svn_branch__el_rev_id_t **el_rev_p,
                                const svn_branch__repos_t *repos,
                                svn_revnum_t revnum,
                                const char *branch_id,
                                const char *relpath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_branch__el_rev_id_t *el_rev = apr_palloc(result_pool, sizeof(*el_rev));
  svn_branch__state_t *branch;

  SVN_ERR(svn_branch__repos_get_branch_by_id(&branch,
                                             repos, revnum, branch_id,
                                             scratch_pool));
  el_rev->rev = revnum;
  SVN_ERR(svn_branch__find_nested_branch_element_by_relpath(&el_rev->branch,
                                                            &el_rev->eid,
                                                            branch, relpath,
                                                            scratch_pool));

  /* Any relpath must at least be within the originally given branch */
  SVN_ERR_ASSERT_NO_RETURN(el_rev->branch);
  *el_rev_p = el_rev;
  return SVN_NO_ERROR;
}

/* Set *BRANCH_P to the branch found in the repository of TXN, at the
 * location (in a revision or in this txn) SRC_EL_REV.
 *
 * Return an error if REVNUM or BRANCH_ID is not found.
 */
static svn_error_t *
branch_in_rev_or_txn(svn_branch__state_t **branch_p,
                     const svn_branch__rev_bid_eid_t *src_el_rev,
                     svn_branch__txn_t *txn,
                     apr_pool_t *result_pool)
{
  if (SVN_IS_VALID_REVNUM(src_el_rev->rev))
    {
      SVN_ERR(svn_branch__repos_get_branch_by_id(branch_p,
                                                 txn->repos,
                                                 src_el_rev->rev,
                                                 src_el_rev->bid,
                                                 result_pool));
    }
  else
    {
      *branch_p
        = svn_branch__txn_get_branch_by_id(
            txn, src_el_rev->bid, result_pool);
      if (! *branch_p)
        return svn_error_createf(SVN_BRANCH__ERR, NULL,
                                 _("Branch %s not found"),
                                 src_el_rev->bid);
    }

  return SVN_NO_ERROR;
}

struct svn_branch__txn_priv_t
{
  /* The underlying branch-txn that supports only non-nested branching. */
  svn_branch__txn_t *wrapped_txn;

};

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static apr_array_header_t *
nested_branch_txn_get_branches(const svn_branch__txn_t *txn,
                               apr_pool_t *result_pool)
{
  /* Just forwarding: nothing more is needed. */
  apr_array_header_t *branches
    = svn_branch__txn_get_branches(txn->priv->wrapped_txn,
                                   result_pool);

  return branches;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_delete_branch(svn_branch__txn_t *txn,
                                const char *bid,
                                apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_delete_branch(txn->priv->wrapped_txn,
                                        bid,
                                        scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_get_num_new_eids(const svn_branch__txn_t *txn,
                                   int *num_new_eids_p,
                                   apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_get_num_new_eids(txn->priv->wrapped_txn,
                                           num_new_eids_p,
                                           scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_new_eid(svn_branch__txn_t *txn,
                          svn_branch__eid_t *eid_p,
                          apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_new_eid(txn->priv->wrapped_txn,
                                  eid_p,
                                  scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_open_branch(svn_branch__txn_t *txn,
                              svn_branch__state_t **new_branch_p,
                              const char *new_branch_id,
                              int root_eid,
                              svn_branch__rev_bid_eid_t *tree_ref,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_branch__state_t *new_branch;

  SVN_ERR(svn_branch__txn_open_branch(txn->priv->wrapped_txn,
                                      &new_branch,
                                      new_branch_id, root_eid, tree_ref,
                                      result_pool,
                                      scratch_pool));

  /* Recursively branch any nested branches */
  if (tree_ref)
    {
      svn_branch__state_t *from_branch;
      svn_branch__subtree_t *from_subtree;

      /* (The way we're doing it here also redundantly re-instantiates all the
         elements in NEW_BRANCH.) */
      SVN_ERR(branch_in_rev_or_txn(&from_branch, tree_ref,
                                   txn->priv->wrapped_txn, scratch_pool));
      SVN_ERR(svn_branch__get_subtree(from_branch, &from_subtree,
                                      tree_ref->eid, scratch_pool));
      SVN_ERR(svn_branch__instantiate_elements_r(new_branch, *from_subtree,
                                                 scratch_pool));
    }

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_finalize_eids(svn_branch__txn_t *txn,
                                apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_finalize_eids(txn->priv->wrapped_txn,
                                        scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_serialize(svn_branch__txn_t *txn,
                            svn_stream_t *stream,
                            apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_serialize(txn->priv->wrapped_txn,
                                    stream,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements nested branching.
 * An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_sequence_point(svn_branch__txn_t *txn,
                                 apr_pool_t *scratch_pool)
{
  svn_branch__txn_t *wrapped_txn = txn->priv->wrapped_txn;
  apr_array_header_t *branches;
  int i;

  /* first, purge elements in each branch */
  SVN_ERR(svn_branch__txn_sequence_point(wrapped_txn, scratch_pool));

  /* second, purge branches that are no longer nested */
  branches = svn_branch__txn_get_branches(wrapped_txn, scratch_pool);
  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(branches, i, void *);
      svn_branch__state_t *outer_branch;
      int outer_eid;

      svn_branch__get_outer_branch_and_eid(&outer_branch, &outer_eid,
                                           b, scratch_pool);
      if (outer_branch)
        {
          svn_element__content_t *element;

          SVN_ERR(svn_branch__state_get_element(outer_branch, &element,
                                                outer_eid, scratch_pool));
          if (! element)
            SVN_ERR(svn_branch__txn_delete_branch(wrapped_txn, b->bid,
                                                  scratch_pool));
        }
    }
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_complete(svn_branch__txn_t *txn,
                           apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_complete(txn->priv->wrapped_txn,
                                   scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
nested_branch_txn_abort(svn_branch__txn_t *txn,
                        apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_abort(txn->priv->wrapped_txn,
                                scratch_pool));
  return SVN_NO_ERROR;
}

svn_branch__txn_t *
svn_branch__nested_txn_create(svn_branch__txn_t *wrapped_txn,
                              apr_pool_t *result_pool)
{
  static const svn_branch__txn_vtable_t vtable = {
    {0},
    nested_branch_txn_get_branches,
    nested_branch_txn_delete_branch,
    nested_branch_txn_get_num_new_eids,
    nested_branch_txn_new_eid,
    nested_branch_txn_open_branch,
    nested_branch_txn_finalize_eids,
    nested_branch_txn_serialize,
    nested_branch_txn_sequence_point,
    nested_branch_txn_complete,
    nested_branch_txn_abort,
  };
  svn_branch__txn_t *txn
    = svn_branch__txn_create(&vtable, NULL, NULL, result_pool);

  txn->priv = apr_pcalloc(result_pool, sizeof(*txn->priv));
  txn->priv->wrapped_txn = wrapped_txn;
  txn->repos = wrapped_txn->repos;
  txn->rev = wrapped_txn->rev;
  txn->base_rev = wrapped_txn->base_rev;
  return txn;
}

