/*
 * conflicts.c:  conflict resolver implementation
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "client.h"

#include "private/svn_diff_tree.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_token.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"

#define ARRAY_LEN(ary) ((sizeof (ary)) / (sizeof ((ary)[0])))


/*** Dealing with conflicts. ***/

/* Describe a tree conflict. */
typedef svn_error_t *(*tree_conflict_get_description_func_t)(
  const char **change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/* Get more information about a tree conflict.
 * This function may contact the repository. */
typedef svn_error_t *(*tree_conflict_get_details_func_t)(
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

struct svn_client_conflict_t
{
  const char *local_abspath;
  apr_hash_t *prop_conflicts;

  /* Indicate which options were chosen to resolve a text or tree conflict
   * on the conflicted node. */
  svn_client_conflict_option_id_t resolution_text;
  svn_client_conflict_option_id_t resolution_tree;

  /* A mapping from const char* property name to pointers to
   * svn_client_conflict_option_t for all properties which had their
   * conflicts resolved. Indicates which options were chosen to resolve
   * the property conflicts. */
  apr_hash_t *resolved_props;

  /* Ask a tree conflict to describe itself. */
  tree_conflict_get_description_func_t
    tree_conflict_get_incoming_description_func;
  tree_conflict_get_description_func_t
    tree_conflict_get_local_description_func;

  /* Ask a tree conflict to find out more information about itself
   * by contacting the repository. */
  tree_conflict_get_details_func_t tree_conflict_get_incoming_details_func;
  tree_conflict_get_details_func_t tree_conflict_get_local_details_func;

  /* Any additional information found can be stored here and may be used
   * when describing a tree conflict. */
  void *tree_conflict_incoming_details;
  void *tree_conflict_local_details;

  /* The pool this conflict was allocated from. */
  apr_pool_t *pool;

  /* Conflict data provided by libsvn_wc. */
  const svn_wc_conflict_description2_t *legacy_text_conflict;
  const char *legacy_prop_conflict_propname;
  const svn_wc_conflict_description2_t *legacy_tree_conflict;

  /* The recommended resolution option's ID. */
  svn_client_conflict_option_id_t recommended_option_id;
};

/* Resolves conflict to OPTION and sets CONFLICT->RESOLUTION accordingly.
 *
 * May raise an error in case the conflict could not be resolved. A common
 * case would be a tree conflict the resolution of which depends on other
 * tree conflicts to be resolved first. */
typedef svn_error_t *(*conflict_option_resolve_func_t)(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

struct svn_client_conflict_option_t
{
  svn_client_conflict_option_id_t id;
  const char *label;
  const char *description;

  svn_client_conflict_t *conflict;
  conflict_option_resolve_func_t do_resolve_func;

  /* The pool this option was allocated from. */
  apr_pool_t *pool;

  /* Data which is specific to particular conflicts and options. */
  union {
    struct {
      /* Indicates the property to resolve in case of a property conflict.
       * If set to "", all properties are resolved to this option. */
      const char *propname;

      /* A merged property value, if supplied by the API user, else NULL. */
      const svn_string_t *merged_propval;
    } prop;
  } type_data;

};

/*
 * Return a legacy conflict choice corresponding to OPTION_ID.
 * Return svn_wc_conflict_choose_undefined if no corresponding
 * legacy conflict choice exists.
 */
static svn_wc_conflict_choice_t
conflict_option_id_to_wc_conflict_choice(
  svn_client_conflict_option_id_t option_id)
{

  switch (option_id)
    {
      case svn_client_conflict_option_undefined:
        return svn_wc_conflict_choose_undefined;

      case svn_client_conflict_option_postpone:
        return svn_wc_conflict_choose_postpone;

      case svn_client_conflict_option_base_text:
        return svn_wc_conflict_choose_base;

      case svn_client_conflict_option_incoming_text:
        return svn_wc_conflict_choose_theirs_full;

      case svn_client_conflict_option_working_text:
        return svn_wc_conflict_choose_mine_full;

      case svn_client_conflict_option_incoming_text_where_conflicted:
        return svn_wc_conflict_choose_theirs_conflict;

      case svn_client_conflict_option_working_text_where_conflicted:
        return svn_wc_conflict_choose_mine_conflict;

      case svn_client_conflict_option_merged_text:
        return svn_wc_conflict_choose_merged;

      case svn_client_conflict_option_unspecified:
        return svn_wc_conflict_choose_unspecified;

      default:
        break;
    }

  return svn_wc_conflict_choose_undefined;
}

static void
add_legacy_desc_to_conflict(const svn_wc_conflict_description2_t *desc,
                            svn_client_conflict_t *conflict,
                            apr_pool_t *result_pool)
{
  switch (desc->kind)
    {
      case svn_wc_conflict_kind_text:
        conflict->legacy_text_conflict = desc;
        break;

      case svn_wc_conflict_kind_property:
        if (conflict->prop_conflicts == NULL)
          conflict->prop_conflicts = apr_hash_make(result_pool);
        svn_hash_sets(conflict->prop_conflicts, desc->property_name, desc);
        conflict->legacy_prop_conflict_propname = desc->property_name;
        break;

      case svn_wc_conflict_kind_tree:
        conflict->legacy_tree_conflict = desc;
        break;

      default:
        SVN_ERR_ASSERT_NO_RETURN(FALSE); /* unknown kind of conflict */
    }
}

/* A map for svn_wc_conflict_action_t values to strings */
static const svn_token_map_t map_conflict_action[] =
{
  { "edit",             svn_wc_conflict_action_edit },
  { "delete",           svn_wc_conflict_action_delete },
  { "add",              svn_wc_conflict_action_add },
  { "replace",          svn_wc_conflict_action_replace },
  { NULL,               0 }
};

/* A map for svn_wc_conflict_reason_t values to strings */
static const svn_token_map_t map_conflict_reason[] =
{
  { "edit",             svn_wc_conflict_reason_edited },
  { "delete",           svn_wc_conflict_reason_deleted },
  { "missing",          svn_wc_conflict_reason_missing },
  { "obstruction",      svn_wc_conflict_reason_obstructed },
  { "add",              svn_wc_conflict_reason_added },
  { "replace",          svn_wc_conflict_reason_replaced },
  { "unversioned",      svn_wc_conflict_reason_unversioned },
  { "moved-away",       svn_wc_conflict_reason_moved_away },
  { "moved-here",       svn_wc_conflict_reason_moved_here },
  { NULL,               0 }
};

/* Describes a server-side move (really a copy+delete within the same
 * revision) which was identified by scanning the revision log.
 * This structure can represent one or more "chains" of moves, i.e.
 * multiple move operations which occurred across a range of revisions. */
struct repos_move_info {
  /* The revision in which this move was committed. */
  svn_revnum_t rev;

  /* The author who commited the revision in which this move was committed. */
  const char *rev_author;

  /* The repository relpath the node was moved from in this revision. */
  const char *moved_from_repos_relpath;

  /* The repository relpath the node was moved to in this revision. */
  const char *moved_to_repos_relpath;

  /* The copyfrom revision of the moved-to path. */
  svn_revnum_t copyfrom_rev;

  /* The node kind of the item being moved. */
  svn_node_kind_t node_kind;

  /* Prev pointer. NULL if no prior move exists in the chain. */
  struct repos_move_info *prev;

  /* An array of struct repos_move_info * elements, each representing
   * a possible way forward in the move chain. NULL if no next move
   * exists in this chain. If the deleted node was copied only once in
   * this revision, then this array has only one element and the move
   * chain does not fork. But if this revision contains multiple copies of
   * the deleted node, each of these copies appears as an element of this
   * array, and each element represents a different path the next move
   * might have taken. */
  apr_array_header_t *next;
};

static svn_revnum_t
rev_below(svn_revnum_t rev)
{
  SVN_ERR_ASSERT_NO_RETURN(rev != SVN_INVALID_REVNUM);
  SVN_ERR_ASSERT_NO_RETURN(rev > 0);

  return rev == 1 ? 1 : rev - 1;
}

/* Set *RELATED to true if the deleted node DELETED_REPOS_RELPATH@DELETED_REV
 * is an ancestor of the copied node COPYFROM_PATH@COPYFROM_REV.
 * If CHECK_LAST_CHANGED_REV is non-zero, also ensure that the copied node
 * is a copy of the deleted node's last-changed revision's content, rather
 * than a copy of some older content. If it's not, set *RELATED to false. */
static svn_error_t *
check_move_ancestry(svn_boolean_t *related,
                    svn_ra_session_t *ra_session,
                    const char *repos_root_url,
                    const char *deleted_repos_relpath,
                    svn_revnum_t deleted_rev,
                    const char *copyfrom_path,
                    svn_revnum_t copyfrom_rev,
                    svn_boolean_t check_last_changed_rev,
                    apr_pool_t *scratch_pool)
{
  apr_hash_t *locations;
  const char *deleted_url;
  const char *deleted_location;
  apr_array_header_t *location_revisions;
  const char *old_session_url;

  location_revisions = apr_array_make(scratch_pool, 1, sizeof(svn_revnum_t));
  APR_ARRAY_PUSH(location_revisions, svn_revnum_t) = copyfrom_rev;
  deleted_url = svn_uri_canonicalize(apr_pstrcat(scratch_pool,
                                                 repos_root_url, "/",
                                                 deleted_repos_relpath,
                                                 NULL),
                                     scratch_pool);
  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                            deleted_url, scratch_pool));
  SVN_ERR(svn_ra_get_locations(ra_session, &locations, "",
                               rev_below(deleted_rev), location_revisions,
                               scratch_pool));

  deleted_location = apr_hash_get(locations, &copyfrom_rev,
                                  sizeof(svn_revnum_t));
  if (deleted_location)
    {
      if (deleted_location[0] == '/')
        deleted_location++;
      if (strcmp(deleted_location, copyfrom_path) != 0)
        {
          *related = FALSE;
          return SVN_NO_ERROR;
        }
    }
  else
    {
      *related = FALSE;
      return SVN_NO_ERROR;
    }

  if (check_last_changed_rev)
    {
      svn_dirent_t *dirent;

      /* Verify that copyfrom_rev >= last-changed revision of the
       * deleted node. */
      SVN_ERR(svn_ra_stat(ra_session, "", rev_below(deleted_rev), &dirent,
                          scratch_pool));
      if (dirent == NULL || copyfrom_rev < dirent->created_rev)
        {
          *related = FALSE;
          return SVN_NO_ERROR;
        }
    }

  *related = TRUE;
  return SVN_NO_ERROR;
}

struct copy_info {
  const char *copyto_path;
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
  svn_node_kind_t node_kind;
};

/* Allocate and return a NEW_MOVE, and update MOVED_PATHS with this new move. */
static svn_error_t *
add_new_move(struct repos_move_info **new_move,
             const char *deleted_repos_relpath,
             const char *copyto_path,
             svn_revnum_t copyfrom_rev,
             svn_node_kind_t node_kind,
             svn_revnum_t revision,
             const char *author,
             apr_hash_t *moved_paths,
             svn_ra_session_t *ra_session,
             const char *repos_root_url, 
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  struct repos_move_info *move;
  struct repos_move_info *next_move;

  move = apr_pcalloc(result_pool, sizeof(*move));
  move->moved_from_repos_relpath = apr_pstrdup(result_pool,
                                               deleted_repos_relpath);
  move->moved_to_repos_relpath = apr_pstrdup(result_pool, copyto_path);
  move->rev = revision;
  move->rev_author = apr_pstrdup(result_pool, author);
  move->copyfrom_rev = copyfrom_rev;
  move->node_kind = node_kind;

  /* Link together multiple moves of the same node.
   * Note that we're traversing history backwards, so moves already
   * present in the list happened in younger revisions. */
  next_move = svn_hash_gets(moved_paths, move->moved_to_repos_relpath);
  if (next_move)
    {
      svn_boolean_t related;

      /* Tracing back history of the delete-half of the next move
       * to the copyfrom-revision of the prior move we must end up
       * at the delete-half of the prior move. */
      SVN_ERR(check_move_ancestry(&related, ra_session, repos_root_url,
                                  next_move->moved_from_repos_relpath,
                                  next_move->rev,
                                  move->moved_from_repos_relpath,
                                  move->copyfrom_rev,
                                  FALSE, scratch_pool));
      if (related)
        {
          SVN_ERR_ASSERT(move->rev < next_move->rev);

          /* Prepend this move to the linked list. */
          if (move->next == NULL)
            move->next = apr_array_make(result_pool, 1,
                                        sizeof (struct repos_move_info *));
          APR_ARRAY_PUSH(move->next, struct repos_move_info *) = next_move;
          next_move->prev = move;
        }
    }

  /* Make this move the head of our next-move linking map. */
  svn_hash_sets(moved_paths, move->moved_from_repos_relpath, move);

  *new_move = move;
  return SVN_NO_ERROR;
}

/* Push a MOVE into the MOVES_TABLE. */
static void
push_move(struct repos_move_info *move, apr_hash_t *moves_table,
          apr_pool_t *result_pool)
{
  apr_array_header_t *moves;

  /* Add this move to the list of moves in the revision. */
  moves = apr_hash_get(moves_table, &move->rev, sizeof(svn_revnum_t));
  if (moves == NULL)
    {
      /* It is the first move in this revision. Create the list. */
      moves = apr_array_make(result_pool, 1, sizeof(struct repos_move_info *));
      apr_hash_set(moves_table, &move->rev, sizeof(svn_revnum_t), moves);
    }
  APR_ARRAY_PUSH(moves, struct repos_move_info *) = move;
}

/* Find the youngest common ancestor of REPOS_RELPATH1@PEG_REV1 and
 * REPOS_RELPATH2@PEG_REV2. Return the result in *YCA_LOC.
 * Set *YCA_LOC to NULL if no common ancestor exists. */
static svn_error_t *
find_yca(svn_client__pathrev_t **yca_loc,
         const char *repos_relpath1,
         svn_revnum_t peg_rev1,
         const char *repos_relpath2,
         svn_revnum_t peg_rev2,
         const char *repos_root_url,
         const char *repos_uuid,
         svn_ra_session_t *ra_session,
         svn_client_ctx_t *ctx,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *loc1;
  svn_client__pathrev_t *loc2;

  *yca_loc = NULL;

  loc1 = svn_client__pathrev_create_with_relpath(repos_root_url, repos_uuid,
                                                 peg_rev1, repos_relpath1,
                                                 scratch_pool);
  loc2 = svn_client__pathrev_create_with_relpath(repos_root_url, repos_uuid,
                                                 peg_rev2, repos_relpath2,
                                                 scratch_pool);
  SVN_ERR(svn_client__get_youngest_common_ancestor(yca_loc, loc1, loc2,
                                                   ra_session, ctx,
                                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Like find_yca, expect that a YCA could also be found via a brute-force
 * search of parents of REPOS_RELPATH1 and REPOS_RELPATH2, if no "direct"
 * YCA exists. An implicit assumption is that some parent of REPOS_RELPATH1
 * is a branch of some parent of REPOS_RELPATH2.
 *
 * This function can guess a "good enough" YCA for 'missing nodes' which do
 * not exist in the working copy, e.g. when a file edit is merged to a path
 * which does not exist in the working copy.
 */
static svn_error_t *
find_nearest_yca(svn_client__pathrev_t **yca_locp,
                 const char *repos_relpath1,
                 svn_revnum_t peg_rev1,
                 const char *repos_relpath2,
                 svn_revnum_t peg_rev2,
                 const char *repos_root_url,
                 const char *repos_uuid,
                 svn_ra_session_t *ra_session,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *yca_loc;
  svn_error_t *err;
  apr_pool_t *iterpool;
  const char *p1, *p2;
  apr_size_t c1, c2;

  *yca_locp = NULL;

  iterpool = svn_pool_create(scratch_pool);

  p1 = repos_relpath1;
  c1 = svn_path_component_count(repos_relpath1);
  while (c1--)
    {
      svn_pool_clear(iterpool);

      p2 = repos_relpath2;
      c2 = svn_path_component_count(repos_relpath2);
      while (c2--)
        {
          err = find_yca(&yca_loc, p1, peg_rev1, p2, peg_rev2,
                         repos_root_url, repos_uuid, ra_session, ctx,
                         result_pool, iterpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                {
                  svn_error_clear(err);
                  yca_loc = NULL;
                }
              else
                return svn_error_trace(err);
            }

          if (yca_loc)
            {
              *yca_locp = yca_loc;
              svn_pool_destroy(iterpool);
              return SVN_NO_ERROR;
            }

          p2 = svn_relpath_dirname(p2, scratch_pool);
        }

      p1 = svn_relpath_dirname(p1, scratch_pool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Check if the copied node described by COPY and the DELETED_PATH@DELETED_REV
 * share a common ancestor. If so, return new repos_move_info in *MOVE which
 * describes a move from the deleted path to that copy's destination. */
static svn_error_t *
find_related_move(struct repos_move_info **move,
                  struct copy_info *copy,
                  const char *deleted_repos_relpath,
                  svn_revnum_t deleted_rev,
                  const char *author,
                  apr_hash_t *moved_paths,
                  const char *repos_root_url,
                  const char *repos_uuid,
                  svn_client_ctx_t *ctx,
                  svn_ra_session_t *ra_session,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *yca_loc;
  svn_error_t *err;

  *move = NULL;
  err = find_yca(&yca_loc, copy->copyfrom_path, copy->copyfrom_rev,
                 deleted_repos_relpath, rev_below(deleted_rev),
                 repos_root_url, repos_uuid, ra_session, ctx,
                 scratch_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          yca_loc = NULL;
        }
      else
        return svn_error_trace(err);
    }

  if (yca_loc)
    SVN_ERR(add_new_move(move, deleted_repos_relpath,
                         copy->copyto_path, copy->copyfrom_rev,
                         copy->node_kind, deleted_rev, author,
                         moved_paths, ra_session, repos_root_url,
                         result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Detect moves by matching DELETED_REPOS_RELPATH@DELETED_REV to the copies
 * in COPIES. Add any moves found to MOVES_TABLE and update MOVED_PATHS. */
static svn_error_t *
match_copies_to_deletion(const char *deleted_repos_relpath,
                         svn_revnum_t deleted_rev,
                         const char *author,
                         apr_hash_t *copies,
                         apr_hash_t *moves_table,
                         apr_hash_t *moved_paths,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         svn_ra_session_t *ra_session,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, copies);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *copyfrom_path = apr_hash_this_key(hi);
      apr_array_header_t *copies_with_same_source_path;
      int i;

      svn_pool_clear(iterpool);

      copies_with_same_source_path = apr_hash_this_val(hi);

      if (strcmp(copyfrom_path, deleted_repos_relpath) == 0)
        {
          /* We found a copyfrom path which matches a deleted node.
           * Check if the deleted node is an ancestor of the copied node. */
          for (i = 0; i < copies_with_same_source_path->nelts; i++)
            {
              struct copy_info *copy;
              svn_boolean_t related;
              struct repos_move_info *move;

              copy = APR_ARRAY_IDX(copies_with_same_source_path, i,
                                   struct copy_info *);
              SVN_ERR(check_move_ancestry(&related,
                                          ra_session, repos_root_url,
                                          deleted_repos_relpath,
                                          deleted_rev,
                                          copy->copyfrom_path,
                                          copy->copyfrom_rev,
                                          TRUE, iterpool));
              if (!related)
                continue;
              
              /* Remember details of this move. */
              SVN_ERR(add_new_move(&move, deleted_repos_relpath,
                                   copy->copyto_path, copy->copyfrom_rev,
                                   copy->node_kind, deleted_rev, author,
                                   moved_paths, ra_session, repos_root_url,
                                   result_pool, iterpool));
              push_move(move, moves_table, result_pool);
            } 
        }
      else
        {
          /* Check if this deleted node is related to any copies in this
           * revision. These could be moves of the deleted node which
           * were merged here from other lines of history. */
          for (i = 0; i < copies_with_same_source_path->nelts; i++)
            {
              struct copy_info *copy;
              struct repos_move_info *move = NULL;

              copy = APR_ARRAY_IDX(copies_with_same_source_path, i,
                                   struct copy_info *);
              SVN_ERR(find_related_move(&move, copy, deleted_repos_relpath,
                                        deleted_rev, author,
                                        moved_paths,
                                        repos_root_url, repos_uuid,
                                        ctx, ra_session,
                                        result_pool, iterpool));
              if (move)
                push_move(move, moves_table, result_pool);
            }
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Update MOVES_TABLE and MOVED_PATHS based on information from
 * revision data in LOG_ENTRY, COPIES, and DELETED_PATHS.
 * Use RA_SESSION to perform the necessary requests. */
static svn_error_t *
find_moves_in_revision(svn_ra_session_t *ra_session,
                       apr_hash_t *moves_table,
                       apr_hash_t *moved_paths,
                       svn_log_entry_t *log_entry,
                       apr_hash_t *copies,
                       apr_array_header_t *deleted_paths,
                       const char *repos_root_url,
                       const char *repos_uuid,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  int i;
  const svn_string_t *author;

  author = svn_hash_gets(log_entry->revprops, SVN_PROP_REVISION_AUTHOR);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < deleted_paths->nelts; i++)
    {
      const char *deleted_repos_relpath;

      svn_pool_clear(iterpool);

      deleted_repos_relpath = APR_ARRAY_IDX(deleted_paths, i, const char *);
      SVN_ERR(match_copies_to_deletion(deleted_repos_relpath,
                                       log_entry->revision,
                                       author ? author->data
                                              : _("unknown author"),
                                       copies, moves_table, moved_paths,
                                       repos_root_url, repos_uuid, ra_session,
                                       ctx, result_pool, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

struct find_deleted_rev_baton
{
  /* Variables below are arguments provided by the caller of
   * svn_ra_get_log2(). */
  const char *deleted_repos_relpath;
  const char *related_repos_relpath;
  svn_revnum_t related_peg_rev;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_client_ctx_t *ctx;
  const char *victim_abspath; /* for notifications */

  /* Variables below are results for the caller of svn_ra_get_log2(). */
  svn_revnum_t deleted_rev;
  const char *deleted_rev_author;
  svn_node_kind_t replacing_node_kind;
  apr_pool_t *result_pool;

  apr_hash_t *moves_table; /* Obtained from find_moves_in_revision(). */
  struct repos_move_info *move; /* Last known move which affected the node. */

  /* Extra RA session that can be used to make additional requests. */
  svn_ra_session_t *extra_ra_session;
};

/* If DELETED_RELPATH matches the moved-from path of a move in MOVES,
 * or if DELETED_RELPATH is a child of a moved-to path in MOVES, return
 * a struct move_info for the corresponding move. Else, return NULL. */
static struct repos_move_info *
map_deleted_path_to_move(const char *deleted_relpath,
                         apr_array_header_t *moves,
                         apr_pool_t *scratch_pool)
{
  struct repos_move_info *closest_move = NULL;
  apr_size_t min_components = 0;
  int i;

  for (i = 0; i < moves->nelts; i++)
    {
      const char *relpath;
      struct repos_move_info *move;
          
      move = APR_ARRAY_IDX(moves, i, struct repos_move_info *);
      if (strcmp(move->moved_from_repos_relpath, deleted_relpath) == 0)
        return move;

      relpath = svn_relpath_skip_ancestor(move->moved_to_repos_relpath,
                                          deleted_relpath);
      if (relpath)
        {
          /* This could be a nested move. Return the path-wise closest move. */
          const apr_size_t c = svn_path_component_count(relpath);
          if (c == 0)
             return move;
          else if (min_components == 0 || c < min_components)
            {
              min_components = c;
              closest_move = move;
            }
        }
    }

  if (closest_move)
    {
      const char *relpath;
      const char *moved_along_path;
      struct repos_move_info *move;
      
      /* See if we can find an even closer move for this moved-along path. */
      relpath = svn_relpath_skip_ancestor(closest_move->moved_to_repos_relpath,
                                          deleted_relpath);
      moved_along_path =
        svn_relpath_join(closest_move->moved_from_repos_relpath, relpath,
                         scratch_pool);
      move = map_deleted_path_to_move(moved_along_path, moves, scratch_pool);
      if (move)
        return move;
    }

  return closest_move;
}

/* Search for nested moves in REVISION, given the already found MOVES,
 * all DELETED_PATHS, and all COPIES, from the same revision.
 * Append any nested moves to the MOVES array. */
static svn_error_t *
find_nested_moves(apr_array_header_t *moves,
                  apr_hash_t *copies,
                  apr_array_header_t *deleted_paths,
                  apr_hash_t *moved_paths,
                  svn_revnum_t revision,
                  const char *author,
                  const char *repos_root_url,
                  const char *repos_uuid,
                  svn_ra_session_t *ra_session,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *nested_moves;
  int i;
  apr_pool_t *iterpool;

  nested_moves = apr_array_make(result_pool, 0,
                                sizeof(struct repos_move_info *));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < deleted_paths->nelts; i++)
    {
      const char *deleted_path;
      const char *child_relpath;
      const char *moved_along_repos_relpath;
      struct repos_move_info *move;
      apr_array_header_t *copies_with_same_source_path;
      int j;
      svn_boolean_t related;

      svn_pool_clear(iterpool);

      deleted_path = APR_ARRAY_IDX(deleted_paths, i, const char *);
      move = map_deleted_path_to_move(deleted_path, moves, iterpool);
      if (move == NULL)
        continue;
      child_relpath = svn_relpath_skip_ancestor(move->moved_to_repos_relpath,
                                                deleted_path);
      if (child_relpath == NULL || child_relpath[0] == '\0')
        continue; /* not a nested move */

      /* Consider: svn mv A B; svn mv B/foo C/foo
       * Copyfrom for C/foo is A/foo, even though C/foo was moved here from
       * B/foo. A/foo was not deleted. It is B/foo which was deleted.
       * We now know about the move A->B and moved-along child_relpath "foo".
       * Try to detect an ancestral relationship between A/foo and the
       * moved-along path. */
      moved_along_repos_relpath =
        svn_relpath_join(move->moved_from_repos_relpath, child_relpath,
                         iterpool);
      copies_with_same_source_path = svn_hash_gets(copies,
                                                   moved_along_repos_relpath);
      if (copies_with_same_source_path == NULL)
        continue; /* not a nested move */

      for (j = 0; j < copies_with_same_source_path->nelts; j++)
        {
          struct copy_info *copy;

          copy = APR_ARRAY_IDX(copies_with_same_source_path, j,
                               struct copy_info *);
          SVN_ERR(check_move_ancestry(&related, ra_session, repos_root_url,
                                      moved_along_repos_relpath,
                                      revision,
                                      copy->copyfrom_path,
                                      copy->copyfrom_rev,
                                      TRUE, iterpool));
          if (related)
            {
              struct repos_move_info *nested_move;

              /* Remember details of this move. */
              SVN_ERR(add_new_move(&nested_move, moved_along_repos_relpath,
                                   copy->copyto_path, copy->copyfrom_rev,
                                   copy->node_kind,
                                   revision, author, moved_paths,
                                   ra_session, repos_root_url,
                                   result_pool, iterpool));

              /* Add this move to the list of nested moves in this revision. */
              APR_ARRAY_PUSH(nested_moves, struct repos_move_info *) =
                nested_move;
            }
        }
    }
  svn_pool_destroy(iterpool);

  /* Add all nested moves found to the list of all moves in this revision. */
  apr_array_cat(moves, nested_moves);

  return SVN_NO_ERROR;
}

/* Make a shallow copy of the copied LOG_ITEM in COPIES. */
static void
cache_copied_item(apr_hash_t *copies, const char *changed_path,
                  svn_log_changed_path2_t *log_item)
{
  apr_pool_t *result_pool = apr_hash_pool_get(copies);
  struct copy_info *copy = apr_palloc(result_pool, sizeof(*copy));
  apr_array_header_t *copies_with_same_source_path;

  copy->copyfrom_path = log_item->copyfrom_path;
  if (log_item->copyfrom_path[0] == '/')
    copy->copyfrom_path++;
  copy->copyto_path = changed_path;
  copy->copyfrom_rev = log_item->copyfrom_rev;
  copy->node_kind = log_item->node_kind;

  copies_with_same_source_path = apr_hash_get(copies, copy->copyfrom_path,
                                              APR_HASH_KEY_STRING);
  if (copies_with_same_source_path == NULL)
    {
      copies_with_same_source_path = apr_array_make(result_pool, 1,
                                                    sizeof(struct copy_info *));
      apr_hash_set(copies, copy->copyfrom_path, APR_HASH_KEY_STRING,
                   copies_with_same_source_path);
    }
  APR_ARRAY_PUSH(copies_with_same_source_path, struct copy_info *) = copy;
}

/* Implements svn_log_entry_receiver_t.
 *
 * Find the revision in which a node, optionally ancestrally related to the
 * node specified via find_deleted_rev_baton, was deleted, When the revision
 * was found, store it in BATON->DELETED_REV and abort the log operation
 * by raising SVN_ERR_CEASE_INVOCATION.
 *
 * If no such revision can be found, leave BATON->DELETED_REV and
 * BATON->REPLACING_NODE_KIND alone.
 *
 * If the node was replaced, set BATON->REPLACING_NODE_KIND to the node
 * kind of the node which replaced the original node. If the node was not
 * replaced, set BATON->REPLACING_NODE_KIND to svn_node_none.
 *
 * This function answers the same question as svn_ra_get_deleted_rev() but
 * works in cases where we do not already know a revision in which the deleted
 * node once used to exist.
 * 
 * If the node was moved, rather than deleted, return move information
 * in BATON->MOVE.
 */
static svn_error_t *
find_deleted_rev(void *baton,
                 svn_log_entry_t *log_entry,
                 apr_pool_t *scratch_pool)
{
  struct find_deleted_rev_baton *b = baton;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_boolean_t deleted_node_found = FALSE;
  svn_node_kind_t replacing_node_kind = svn_node_none;

  if (b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 b->victim_abspath,
                 svn_wc_notify_tree_conflict_details_progress,
                 scratch_pool),
      notify->revision = log_entry->revision;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  /* No paths were changed in this revision.  Nothing to do. */
  if (! log_entry->changed_paths2)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths2);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *changed_path = apr_hash_this_key(hi);
      svn_log_changed_path2_t *log_item = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      /* ### Remove leading slash from paths in log entries. */
      if (changed_path[0] == '/')
          changed_path++;

      /* Check if we already found the deleted node we're looking for. */
      if (!deleted_node_found &&
          svn_path_compare_paths(b->deleted_repos_relpath, changed_path) == 0 &&
          (log_item->action == 'D' || log_item->action == 'R'))
        {
          deleted_node_found = TRUE;

          if (b->related_repos_relpath != NULL &&
              b->related_peg_rev != SVN_INVALID_REVNUM)
            {
              svn_client__pathrev_t *yca_loc;
              svn_error_t *err;

              /* We found a deleted node which occupies the correct path.
               * To be certain that this is the deleted node we're looking for,
               * we must establish whether it is ancestrally related to the
               * "related node" specified in our baton. */
              err = find_yca(&yca_loc,
                             b->related_repos_relpath,
                             b->related_peg_rev,
                             b->deleted_repos_relpath,
                             rev_below(log_entry->revision),
                             b->repos_root_url, b->repos_uuid,
                             b->extra_ra_session, b->ctx, iterpool, iterpool);
              if (err)
                {
                  /* ### Happens for moves within other moves and copies. */
                  if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                    {
                      svn_error_clear(err);
                      yca_loc = NULL;
                    }
                  else
                    return svn_error_trace(err);
                }

              deleted_node_found = (yca_loc != NULL);
            }

          if (deleted_node_found && log_item->action == 'R')
            replacing_node_kind = log_item->node_kind;
        }
    }
  svn_pool_destroy(iterpool);

  if (!deleted_node_found)
    {
      apr_array_header_t *moves;

      moves = apr_hash_get(b->moves_table, &log_entry->revision,
                           sizeof(svn_revnum_t));
      if (moves)
        {
          struct repos_move_info *move;

          move = map_deleted_path_to_move(b->deleted_repos_relpath,
                                          moves, scratch_pool);
          if (move)
            {
              const char *relpath;

              /* The node was moved. Update our search path accordingly. */
              b->move = move;
              relpath = svn_relpath_skip_ancestor(move->moved_to_repos_relpath,
                                                  b->deleted_repos_relpath);
              if (relpath)
                b->deleted_repos_relpath =
                  svn_relpath_join(move->moved_from_repos_relpath, relpath,
                                   b->result_pool);
            }
        }
    }
  else
    {
      svn_string_t *author;

      b->deleted_rev = log_entry->revision;
      author = svn_hash_gets(log_entry->revprops,
                             SVN_PROP_REVISION_AUTHOR);
      if (author)
        b->deleted_rev_author = apr_pstrdup(b->result_pool, author->data);
      else
        b->deleted_rev_author = _("unknown author");
          
      b->replacing_node_kind = replacing_node_kind;

      /* We're done. Abort the log operation. */
      return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

/* Return a localised string representation of the local part of a tree
   conflict on a file. */
static svn_error_t *
describe_local_file_node_change(const char **description,
                                svn_client_conflict_t *conflict,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t local_change;
  svn_wc_operation_t operation;

  local_change = svn_client_conflict_get_local_change(conflict);
  operation = svn_client_conflict_get_operation(conflict);

  switch (local_change)
    {
      case svn_wc_conflict_reason_edited:
        if (operation == svn_wc_operation_update ||
            operation == svn_wc_operation_switch)
          *description = _("A file containing uncommitted changes was "
                           "found in the working copy.");
        else if (operation == svn_wc_operation_merge)
          *description = _("A file which differs from the corresponding "
                           "file on the merge source branch was found "
                           "in the working copy.");
        break;
      case svn_wc_conflict_reason_obstructed:
        *description = _("A file which already occupies this path was found "
                         "in the working copy.");
        break;
      case svn_wc_conflict_reason_unversioned:
        *description = _("An unversioned file was found in the working "
                         "copy.");
        break;
      case svn_wc_conflict_reason_deleted:
        *description = _("A deleted file was found in the working copy.");
        break;
      case svn_wc_conflict_reason_missing:
        if (operation == svn_wc_operation_update ||
            operation == svn_wc_operation_switch)
          *description = _("No such file was found in the working copy.");
        else if (operation == svn_wc_operation_merge)
          {
            /* ### display deleted revision */
            *description = _("No such file was found in the merge target "
                             "working copy.\nPerhaps the file has been "
                             "deleted or moved away in the repository's "
                             "history?");
          }
        break;
      case svn_wc_conflict_reason_added:
      case svn_wc_conflict_reason_replaced:
        {
          /* ### show more details about copies or replacements? */
          *description = _("A file scheduled to be added to the "
                           "repository in the next commit was found in "
                           "the working copy.");
        }
        break;
      case svn_wc_conflict_reason_moved_away:
        {
          const char *moved_to_abspath;
          svn_error_t *err;

          err = svn_wc__node_was_moved_away(&moved_to_abspath, NULL, 
                                            ctx->wc_ctx,
                                            conflict->local_abspath,
                                            scratch_pool,
                                            scratch_pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
                {
                  moved_to_abspath = NULL;
                  svn_error_clear(err);
                }
              else
                return svn_error_trace(err);
            }
          if (operation == svn_wc_operation_update ||
              operation == svn_wc_operation_switch)
            {
              if (moved_to_abspath == NULL)
                {
                  /* The move no longer exists. */
                  *description = _("The file in the working copy had "
                                   "been moved away at the time this "
                                   "conflict was recorded.");
                }
              else
                {
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("The file in the working copy was "
                                     "moved away to\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_to_abspath),
                                     scratch_pool));
                }
            }
          else if (operation == svn_wc_operation_merge)
            {
              if (moved_to_abspath == NULL)
                {
                  /* The move probably happened in branch history.
                   * This case cannot happen until we detect incoming
                   * moves, which we currently don't do. */
                  /* ### find deleted/moved revision? */
                  *description = _("The file in the working copy had "
                                   "been moved away at the time this "
                                   "conflict was recorded.");
                }
              else
                {
                  /* This is a local move in the working copy. */
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("The file in the working copy was "
                                     "moved away to\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_to_abspath),
                                     scratch_pool));
                }
            }
          break;
        }
      case svn_wc_conflict_reason_moved_here:
        {
          const char *moved_from_abspath;

          SVN_ERR(svn_wc__node_was_moved_here(&moved_from_abspath, NULL, 
                                              ctx->wc_ctx,
                                              conflict->local_abspath,
                                              scratch_pool,
                                              scratch_pool));
          if (operation == svn_wc_operation_update ||
              operation == svn_wc_operation_switch)
            {
              if (moved_from_abspath == NULL)
                {
                  /* The move no longer exists. */
                  *description = _("A file had been moved here in the "
                                   "working copy at the time this "
                                   "conflict was recorded.");
                }
              else
                {
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("A file was moved here in the "
                                     "working copy from\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_from_abspath),
                                     scratch_pool));
                }
            }
          else if (operation == svn_wc_operation_merge)
            {
              if (moved_from_abspath == NULL)
                {
                  /* The move probably happened in branch history.
                   * This case cannot happen until we detect incoming
                   * moves, which we currently don't do. */
                  /* ### find deleted/moved revision? */
                  *description = _("A file had been moved here in the "
                                   "working copy at the time this "
                                   "conflict was recorded.");
                }
              else
                {
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  /* This is a local move in the working copy. */
                  *description = apr_psprintf(
                                   result_pool,
                                   _("A file was moved here in the "
                                     "working copy from\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_from_abspath),
                                     scratch_pool));
                }
            }
          break;
        }
    }

  return SVN_NO_ERROR;
}

/* Return a localised string representation of the local part of a tree
   conflict on a directory. */
static svn_error_t *
describe_local_dir_node_change(const char **description,
                               svn_client_conflict_t *conflict,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t local_change;
  svn_wc_operation_t operation;

  local_change = svn_client_conflict_get_local_change(conflict);
  operation = svn_client_conflict_get_operation(conflict);

  switch (local_change)
    {
      case svn_wc_conflict_reason_edited:
        if (operation == svn_wc_operation_update ||
            operation == svn_wc_operation_switch)
          *description = _("A directory containing uncommitted changes "
                           "was found in the working copy.");
        else if (operation == svn_wc_operation_merge)
          *description = _("A directory which differs from the "
                           "corresponding directory on the merge source "
                           "branch was found in the working copy.");
        break;
      case svn_wc_conflict_reason_obstructed:
        *description = _("A directory which already occupies this path was "
                         "found in the working copy.");
        break;
      case svn_wc_conflict_reason_unversioned:
        *description = _("An unversioned directory was found in the "
                         "working copy.");
        break;
      case svn_wc_conflict_reason_deleted:
        *description = _("A deleted directory was found in the "
                         "working copy.");
        break;
      case svn_wc_conflict_reason_missing:
        if (operation == svn_wc_operation_update ||
            operation == svn_wc_operation_switch)
          *description = _("No such directory was found in the working copy.");
        else if (operation == svn_wc_operation_merge)
          {
            /* ### display deleted revision */
            *description = _("No such directory was found in the merge "
                             "target working copy.\nPerhaps the "
                             "directory has been deleted or moved away "
                             "in the repository's history?");
          }
        break;
      case svn_wc_conflict_reason_added:
      case svn_wc_conflict_reason_replaced:
        {
          /* ### show more details about copies or replacements? */
          *description = _("A directory scheduled to be added to the "
                           "repository in the next commit was found in "
                           "the working copy.");
        }
        break;
      case svn_wc_conflict_reason_moved_away:
        {
          const char *moved_to_abspath;
          svn_error_t *err;

          err = svn_wc__node_was_moved_away(&moved_to_abspath, NULL, 
                                            ctx->wc_ctx,
                                            conflict->local_abspath,
                                            scratch_pool,
                                            scratch_pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
                {
                  moved_to_abspath = NULL;
                  svn_error_clear(err);
                }
              else
                return svn_error_trace(err);
            }

          if (operation == svn_wc_operation_update ||
              operation == svn_wc_operation_switch)
            {
              if (moved_to_abspath == NULL)
                {
                  /* The move no longer exists. */
                  *description = _("The directory in the working copy "
                                   "had been moved away at the time "
                                   "this conflict was recorded.");
                }
              else
                {
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("The directory in the working copy "
                                     "was moved away to\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_to_abspath),
                                     scratch_pool));
                }
            }
          else if (operation == svn_wc_operation_merge)
            {
              if (moved_to_abspath == NULL)
                {
                  /* The move probably happened in branch history.
                   * This case cannot happen until we detect incoming
                   * moves, which we currently don't do. */
                  /* ### find deleted/moved revision? */
                  *description = _("The directory had been moved away "
                                   "at the time this conflict was "
                                   "recorded.");
                }
              else
                {
                  /* This is a local move in the working copy. */
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("The directory was moved away to\n"
                                     "'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_to_abspath),
                                     scratch_pool));
                }
            }
          }
          break;
      case svn_wc_conflict_reason_moved_here:
        {
          const char *moved_from_abspath;

          SVN_ERR(svn_wc__node_was_moved_here(&moved_from_abspath, NULL, 
                                              ctx->wc_ctx,
                                              conflict->local_abspath,
                                              scratch_pool,
                                              scratch_pool));
          if (operation == svn_wc_operation_update ||
              operation == svn_wc_operation_switch)
            {
              if (moved_from_abspath == NULL)
                {
                  /* The move no longer exists. */
                  *description = _("A directory had been moved here at "
                                   "the time this conflict was "
                                   "recorded.");
                }
              else
                {
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("A directory was moved here from\n"
                                     "'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_from_abspath),
                                     scratch_pool));
                }
            }
          else if (operation == svn_wc_operation_merge)
            {
              if (moved_from_abspath == NULL)
                {
                  /* The move probably happened in branch history.
                   * This case cannot happen until we detect incoming
                   * moves, which we currently don't do. */
                  /* ### find deleted/moved revision? */
                  *description = _("A directory had been moved here at "
                                   "the time this conflict was "
                                   "recorded.");
                }
              else
                {
                  /* This is a local move in the working copy. */
                  const char *wcroot_abspath;

                  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath,
                                             ctx->wc_ctx,
                                             conflict->local_abspath,
                                             scratch_pool,
                                             scratch_pool));
                  *description = apr_psprintf(
                                   result_pool,
                                   _("A directory was moved here in "
                                     "the working copy from\n'%s'."),
                                   svn_dirent_local_style(
                                     svn_dirent_skip_ancestor(
                                       wcroot_abspath,
                                       moved_from_abspath),
                                     scratch_pool));
                }
            }
        }
    }

  return SVN_NO_ERROR;
}

struct find_moves_baton
{
  /* Variables below are arguments provided by the caller of
   * svn_ra_get_log2(). */
  const char *repos_root_url;
  const char *repos_uuid;
  svn_client_ctx_t *ctx;
  const char *victim_abspath; /* for notifications */
  apr_pool_t *result_pool;

  /* A hash table mapping a revision number to an array of struct
   * repos_move_info * elements, describing moves.
   *
   * Must be allocated in RESULT_POOL by the caller of svn_ra_get_log2().
   *
   * If the node was moved, the DELETED_REV is present in this table,
   * perhaps along with additional revisions.
   *
   * Given a sequence of moves which happened in the repository, such as:
   *   rA: mv x->z
   *   rA: mv a->b
   *   rB: mv b->c
   *   rC: mv c->d
   * we map each revision number to all the moves which happened in the
   * revision, which looks as follows: 
   *   rA : [(x->z), (a->b)]
   *   rB : [(b->c)]
   *   rC : [(c->d)]
   * This allows us to later find relevant moves based on a revision number.
   *
   * Additionally, we embed the number of the revision in which a move was
   * found inside the repos_move_info structure:
   *   rA : [(rA, x->z), (rA, a->b)]
   *   rB : [(rB, b->c)]
   *   rC : [(rC, c->d)]
   * And also, all moves pertaining to the same node are chained into a
   * doubly-linked list via 'next' and 'prev' pointers (see definition of
   * struct repos_move_info). This can be visualized as follows:
   *   rA : [(rA, x->z, prev=>NULL, next=>NULL),
   *         (rA, a->b, prev=>NULL, next=>(rB, b->c))]
   *   rB : [(rB, b->c), prev=>(rA, a->b), next=>(rC, c->d)]
   *   rC : [(rC, c->d), prev=>(rB, c->d), next=>NULL]
   * This way, we can look up all moves relevant to a node, forwards and
   * backwards in history, once we have located one move in the chain.
   *
   * In the above example, the data tells us that within the revision
   * range rA:C, a was moved to d. However, within the revision range
   * rA;B, a was moved to b.
   */
  apr_hash_t *moves_table;

  /* Variables below hold state for find_moves() and are not
   * intended to be used by the caller of svn_ra_get_log2().
   * Like all other variables, they must be initialized, however. */

  /* Temporary map of moved paths to struct repos_move_info.
   * Used to link multiple moves of the same node across revisions. */
  apr_hash_t *moved_paths;

  /* Extra RA session that can be used to make additional requests. */
  svn_ra_session_t *extra_ra_session;
};

/* Implements svn_log_entry_receiver_t. */
static svn_error_t *
find_moves(void *baton, svn_log_entry_t *log_entry, apr_pool_t *scratch_pool)
{
  struct find_moves_baton *b = baton;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  apr_array_header_t *deleted_paths;
  apr_hash_t *copies;
  apr_array_header_t *moves;

  if (b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 b->victim_abspath,
                 svn_wc_notify_tree_conflict_details_progress,
                 scratch_pool),
      notify->revision = log_entry->revision;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  /* No paths were changed in this revision.  Nothing to do. */
  if (! log_entry->changed_paths2)
    return SVN_NO_ERROR;

  copies = apr_hash_make(scratch_pool);
  deleted_paths = apr_array_make(scratch_pool, 0, sizeof(const char *));
  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths2);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *changed_path = apr_hash_this_key(hi);
      svn_log_changed_path2_t *log_item = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      /* ### Remove leading slash from paths in log entries. */
      if (changed_path[0] == '/')
          changed_path++;

      /* For move detection, scan for copied nodes in this revision. */
      if (log_item->action == 'A' && log_item->copyfrom_path)
        cache_copied_item(copies, changed_path, log_item);

      /* For move detection, store all deleted_paths. */
      if (log_item->action == 'D' || log_item->action == 'R')
        APR_ARRAY_PUSH(deleted_paths, const char *) =
          apr_pstrdup(scratch_pool, changed_path);
    }
  svn_pool_destroy(iterpool);

  /* Check for moves in this revision */
  SVN_ERR(find_moves_in_revision(b->extra_ra_session,
                                 b->moves_table, b->moved_paths,
                                 log_entry, copies, deleted_paths,
                                 b->repos_root_url, b->repos_uuid,
                                 b->ctx, b->result_pool, scratch_pool));

  moves = apr_hash_get(b->moves_table, &log_entry->revision,
                       sizeof(svn_revnum_t));
  if (moves)
    {
      const svn_string_t *author;

      author = svn_hash_gets(log_entry->revprops, SVN_PROP_REVISION_AUTHOR);
      SVN_ERR(find_nested_moves(moves, copies, deleted_paths,
                                b->moved_paths, log_entry->revision,
                                author ? author->data : _("unknown author"),
                                b->repos_root_url,
                                b->repos_uuid,
                                b->extra_ra_session, b->ctx,
                                b->result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Find all moves which occured in repository history starting at
 * REPOS_RELPATH@START_REV until END_REV (where START_REV > END_REV).
 * Return results in *MOVES_TABLE (see struct find_moves_baton for details). */
static svn_error_t *
find_moves_in_revision_range(struct apr_hash_t **moves_table,
                             const char *repos_relpath,
                             const char *repos_root_url,
                             const char *repos_uuid,
                             const char *victim_abspath,
                             svn_revnum_t start_rev,
                             svn_revnum_t end_rev,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  apr_array_header_t *paths;
  apr_array_header_t *revprops;
  struct find_moves_baton b = { 0 };

  SVN_ERR_ASSERT(start_rev > end_rev);

  url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL, FALSE, FALSE,
                                               ctx, scratch_pool,
                                               scratch_pool));

  paths = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(paths, const char *) = "";

  revprops = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;

  b.repos_root_url = repos_root_url;
  b.repos_uuid = repos_uuid;
  b.ctx = ctx;
  b.victim_abspath = victim_abspath;
  b.moves_table = apr_hash_make(result_pool);
  b.moved_paths = apr_hash_make(scratch_pool);
  b.result_pool = result_pool;
  SVN_ERR(svn_ra__dup_session(&b.extra_ra_session, ra_session, NULL,
                              scratch_pool, scratch_pool));

  SVN_ERR(svn_ra_get_log2(ra_session, paths, start_rev, end_rev,
                          0, /* no limit */
                          TRUE, /* need the changed paths list */
                          FALSE, /* need to traverse copies */
                          FALSE, /* no need for merged revisions */
                          revprops,
                          find_moves, &b,
                          scratch_pool));

  *moves_table = b.moves_table;

  return SVN_NO_ERROR;
}

/* Return new move information for a moved-along child MOVED_ALONG_RELPATH.
 * Set MOVE->NODE_KIND to MOVED_ALONG_NODE_KIND.
 * Do not copy MOVE->NEXT and MOVE-PREV.
 * If MOVED_ALONG_RELPATH is empty, this effectively copies MOVE to
 * RESULT_POOL with NEXT and PREV pointers cleared. */
static struct repos_move_info *
new_path_adjusted_move(struct repos_move_info *move,
                       const char *moved_along_relpath,
                       svn_node_kind_t moved_along_node_kind,
                       apr_pool_t *result_pool)
{
  struct repos_move_info *new_move;

  new_move = apr_pcalloc(result_pool, sizeof(*new_move));
  new_move->moved_from_repos_relpath =
    svn_relpath_join(move->moved_from_repos_relpath, moved_along_relpath,
                     result_pool);
  new_move->moved_to_repos_relpath =
    svn_relpath_join(move->moved_to_repos_relpath, moved_along_relpath,
                     result_pool);
  new_move->rev = move->rev;
  new_move->rev_author = apr_pstrdup(result_pool, move->rev_author);
  new_move->copyfrom_rev = move->copyfrom_rev;
  new_move->node_kind = moved_along_node_kind;
  /* Ignore prev and next pointers. Caller will set them if needed. */

  return new_move;
}

/* Given a list of MOVES_IN_REVISION, figure out which of these moves again
 * move the node which was already moved by PREV_MOVE in the past . */
static svn_error_t *
find_next_moves_in_revision(apr_array_header_t **next_moves,
                            apr_array_header_t *moves_in_revision,
                            struct repos_move_info *prev_move,
                            svn_ra_session_t *ra_session,
                            const char *repos_root_url,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < moves_in_revision->nelts; i++)
    {
      struct repos_move_info *move;
      const char *relpath;
      const char *deleted_repos_relpath;
      svn_boolean_t related;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      /* Check if this move affects the current known path of our node. */
      move = APR_ARRAY_IDX(moves_in_revision, i, struct repos_move_info *);
      relpath = svn_relpath_skip_ancestor(move->moved_from_repos_relpath,
                                          prev_move->moved_to_repos_relpath);
      if (relpath == NULL)
        continue;

      /* It does. So our node must have been deleted again. */
      deleted_repos_relpath = svn_relpath_join(move->moved_from_repos_relpath,
                                               relpath, iterpool);

      /* Tracing back history of the delete-half of this move to the
       * copyfrom-revision of the prior move we must end up at the
       * delete-half of the prior move. */
      err = check_move_ancestry(&related, ra_session, repos_root_url,
                                deleted_repos_relpath, move->rev,
                                prev_move->moved_from_repos_relpath,
                                prev_move->copyfrom_rev,
                                FALSE, scratch_pool);
      if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          continue;
        }
      else
        SVN_ERR(err);

      if (related)
        {
          struct repos_move_info *new_move;

          /* We have a winner. */
          new_move = new_path_adjusted_move(move, relpath, prev_move->node_kind,
                                            result_pool);
          if (*next_moves == NULL)
            *next_moves = apr_array_make(result_pool, 1,
                                         sizeof(struct repos_move_info *));
          APR_ARRAY_PUSH(*next_moves, struct repos_move_info *) = new_move;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static int
compare_items_as_revs(const svn_sort__item_t *a, const svn_sort__item_t *b)
{
  return svn_sort_compare_revisions(a->key, b->key);
}

/* Starting at MOVE->REV, loop over future revisions which contain moves,
 * and look for matching next moves in each. Once found, return a list of
 * (ambiguous, if more than one) moves in *NEXT_MOVES. */
static svn_error_t *
find_next_moves(apr_array_header_t **next_moves,
                apr_hash_t *moves_table,
                struct repos_move_info *move,
                svn_ra_session_t *ra_session,
                const char *repos_root_url,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *moves;
  apr_array_header_t *revisions;
  apr_pool_t *iterpool;
  int i;

  *next_moves = NULL;
  revisions = svn_sort__hash(moves_table, compare_items_as_revs, scratch_pool);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < revisions->nelts; i++)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(revisions, i, svn_sort__item_t);
      svn_revnum_t rev = *(svn_revnum_t *)item.key;

      svn_pool_clear(iterpool);

      if (rev <= move->rev)
        continue;

      moves = apr_hash_get(moves_table, &rev, sizeof(rev));
      SVN_ERR(find_next_moves_in_revision(next_moves, moves, move,
                                          ra_session, repos_root_url,
                                          result_pool, iterpool));
      if (*next_moves)
        break;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Trace all future moves of the node moved by MOVE.
 * Update MOVE->PREV and MOVE->NEXT accordingly. */
static svn_error_t *
trace_moved_node(apr_hash_t *moves_table,
                 struct repos_move_info *move,
                 svn_ra_session_t *ra_session,
                 const char *repos_root_url,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *next_moves;

  SVN_ERR(find_next_moves(&next_moves, moves_table, move,
                          ra_session, repos_root_url,
                          result_pool, scratch_pool));
  if (next_moves)
    {
      int i;
      apr_pool_t *iterpool;

      move->next = next_moves;
      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < next_moves->nelts; i++)
        {
          struct repos_move_info *next_move;

          svn_pool_clear(iterpool);
          next_move = APR_ARRAY_IDX(next_moves, i, struct repos_move_info *);
          next_move->prev = move;
          SVN_ERR(trace_moved_node(moves_table, next_move,
                                   ra_session, repos_root_url,
                                   result_pool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Given a list of MOVES_IN_REVISION, figure out which of these moves
 * move the node which was later on moved by NEXT_MOVE. */
static svn_error_t *
find_prev_move_in_revision(struct repos_move_info **prev_move,
                           apr_array_header_t *moves_in_revision,
                           struct repos_move_info *next_move,
                           svn_ra_session_t *ra_session,
                           const char *repos_root_url,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool;

  *prev_move = NULL;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < moves_in_revision->nelts; i++)
    {
      struct repos_move_info *move;
      const char *relpath;
      const char *deleted_repos_relpath;
      svn_boolean_t related;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      /* Check if this move affects the current known path of our node. */
      move = APR_ARRAY_IDX(moves_in_revision, i, struct repos_move_info *);
      relpath = svn_relpath_skip_ancestor(next_move->moved_from_repos_relpath,
                                          move->moved_to_repos_relpath);
      if (relpath == NULL)
        continue;

      /* It does. So our node must have been deleted. */
      deleted_repos_relpath = svn_relpath_join(
                                next_move->moved_from_repos_relpath,
                                relpath, iterpool);

      /* Tracing back history of the delete-half of the next move to the
       * copyfrom-revision of the prior move we must end up at the
       * delete-half of the prior move. */
      err = check_move_ancestry(&related, ra_session, repos_root_url,
                                deleted_repos_relpath, next_move->rev,
                                move->moved_from_repos_relpath,
                                move->copyfrom_rev,
                                FALSE, scratch_pool);
      if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          continue;
        }
      else
        SVN_ERR(err);

      if (related)
        {
          /* We have a winner. */
          *prev_move = new_path_adjusted_move(move, relpath,
                                              next_move->node_kind,
                                              result_pool);
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static int
compare_items_as_revs_reverse(const svn_sort__item_t *a,
                              const svn_sort__item_t *b)
{
  int c = svn_sort_compare_revisions(a->key, b->key);
  if (c < 0)
    return 1;
  if (c > 0)
    return -1;
  return c;
}

/* Starting at MOVE->REV, loop over past revisions which contain moves,
 * and look for a matching previous move in each. Once found, return
 * it in *PREV_MOVE */
static svn_error_t *
find_prev_move(struct repos_move_info **prev_move,
               apr_hash_t *moves_table,
               struct repos_move_info *move,
               svn_ra_session_t *ra_session,
               const char *repos_root_url,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  apr_array_header_t *moves;
  apr_array_header_t *revisions;
  apr_pool_t *iterpool;
  int i;

  *prev_move = NULL;
  revisions = svn_sort__hash(moves_table, compare_items_as_revs_reverse,
                             scratch_pool);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < revisions->nelts; i++)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(revisions, i, svn_sort__item_t);
      svn_revnum_t rev = *(svn_revnum_t *)item.key;

      svn_pool_clear(iterpool);

      if (rev >= move->rev)
        continue;

      moves = apr_hash_get(moves_table, &rev, sizeof(rev));
      SVN_ERR(find_prev_move_in_revision(prev_move, moves, move,
                                         ra_session, repos_root_url,
                                         result_pool, iterpool));
      if (*prev_move)
        break;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Trace all past moves of the node moved by MOVE.
 * Update MOVE->PREV and MOVE->NEXT accordingly. */
static svn_error_t *
trace_moved_node_backwards(apr_hash_t *moves_table,
                           struct repos_move_info *move,
                           svn_ra_session_t *ra_session,
                           const char *repos_root_url,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct repos_move_info *prev_move;

  SVN_ERR(find_prev_move(&prev_move, moves_table, move,
                         ra_session, repos_root_url,
                         result_pool, scratch_pool));
  if (prev_move)
    {
      move->prev = prev_move;
      prev_move->next = apr_array_make(result_pool, 1,
                                       sizeof(struct repos_move_info *));
      APR_ARRAY_PUSH(prev_move->next, struct repos_move_info *) = move;

      SVN_ERR(trace_moved_node_backwards(moves_table, prev_move,
                                         ra_session, repos_root_url,
                                         result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
reparent_session_and_fetch_node_kind(svn_node_kind_t *node_kind,
                                     svn_ra_session_t *ra_session,
                                     const char *url,
                                     svn_revnum_t peg_rev,
                                     apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_ra_reparent(ra_session, url, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
        {
          svn_error_clear(err);
          *node_kind = svn_node_unknown;
          return SVN_NO_ERROR;
        }
    
      return svn_error_trace(err);
    }

  SVN_ERR(svn_ra_check_path(ra_session, "", peg_rev, node_kind, scratch_pool));

  return SVN_NO_ERROR;
}

/* Scan MOVES_TABLE for moves which affect a particular deleted node, and
 * build a set of new move information for this node.
 * Return heads of all possible move chains in *MOVES.
 *
 * MOVES_TABLE describes moves which happened at arbitrary paths in the
 * repository. DELETED_REPOS_RELPATH may have been moved directly or it
 * may have been moved along with a parent path. Move information returned
 * from this function represents how DELETED_REPOS_RELPATH itself was moved
 * from one path to another, effectively "zooming in" on the effective move
 * operations which occurred for this particular node. */
static svn_error_t *
find_operative_moves(apr_array_header_t **moves,
                     apr_hash_t *moves_table,
                     const char *deleted_repos_relpath,
                     svn_revnum_t deleted_rev,
                     svn_ra_session_t *ra_session,
                     const char *repos_root_url,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_array_header_t *moves_in_deleted_rev;
  int i;
  apr_pool_t *iterpool;
  const char *session_url, *url = NULL;

  moves_in_deleted_rev = apr_hash_get(moves_table, &deleted_rev,
                                      sizeof(deleted_rev));
  if (moves_in_deleted_rev == NULL)
    {
      *moves = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, scratch_pool));

  /* Look for operative moves in the revision where the node was deleted. */
  *moves = apr_array_make(scratch_pool, 0, sizeof(struct repos_move_info *));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < moves_in_deleted_rev->nelts; i++)
    {
      struct repos_move_info *move;
      const char *relpath;

      svn_pool_clear(iterpool);

      move = APR_ARRAY_IDX(moves_in_deleted_rev, i, struct repos_move_info *);
      relpath = svn_relpath_skip_ancestor(move->moved_from_repos_relpath,
                                          deleted_repos_relpath);
      if (relpath && relpath[0] != '\0')
        {
          svn_node_kind_t node_kind;

          url = svn_path_url_add_component2(repos_root_url,
                                            deleted_repos_relpath,
                                            iterpool);
          SVN_ERR(reparent_session_and_fetch_node_kind(&node_kind,
                                                       ra_session, url,
                                                       rev_below(deleted_rev),
                                                       iterpool));
          move = new_path_adjusted_move(move, relpath, node_kind, result_pool);
        }
      APR_ARRAY_PUSH(*moves, struct repos_move_info *) = move;
    }

  if (url != NULL)
    SVN_ERR(svn_ra_reparent(ra_session, session_url, scratch_pool));

  /* If we didn't find any applicable moves, return NULL. */
  if ((*moves)->nelts == 0)
    {
      *moves = NULL;
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
   }

  /* Figure out what happened to these moves in future revisions. */
  for (i = 0; i < (*moves)->nelts; i++)
    {
      struct repos_move_info *move;

      svn_pool_clear(iterpool);

      move = APR_ARRAY_IDX(*moves, i, struct repos_move_info *);
      SVN_ERR(trace_moved_node(moves_table, move, ra_session, repos_root_url,
                               result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Try to find a revision older than START_REV, and its author, which deleted
 * DELETED_BASENAME in the directory PARENT_REPOS_RELPATH. Assume the deleted
 * node is ancestrally related to RELATED_REPOS_RELPATH@RELATED_PEG_REV.
 * If no such revision can be found, set *DELETED_REV to SVN_INVALID_REVNUM
 * and *DELETED_REV_AUTHOR to NULL.
 * If the node was replaced rather than deleted, set *REPLACING_NODE_KIND to
 * the node kind of the replacing node. Else, set it to svn_node_unknown.
 * Only request the log for revisions up to END_REV from the server.
 * If the deleted node was moved, provide heads of move chains in *MOVES.
 * If the node was not moved,set *MOVES to NULL.
 */
static svn_error_t *
find_revision_for_suspected_deletion(svn_revnum_t *deleted_rev,
                                     const char **deleted_rev_author,
                                     svn_node_kind_t *replacing_node_kind,
                                     struct apr_array_header_t **moves,
                                     svn_client_conflict_t *conflict,
                                     const char *deleted_basename,
                                     const char *parent_repos_relpath,
                                     svn_revnum_t start_rev,
                                     svn_revnum_t end_rev,
                                     const char *related_repos_relpath,
                                     svn_revnum_t related_peg_rev,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  apr_array_header_t *paths;
  apr_array_header_t *revprops;
  const char *repos_root_url;
  const char *repos_uuid;
  struct find_deleted_rev_baton b = { 0 };
  const char *victim_abspath;
  svn_error_t *err;
  apr_hash_t *moves_table;

  SVN_ERR_ASSERT(start_rev > end_rev);

  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, &repos_uuid,
                                             conflict, scratch_pool,
                                             scratch_pool));
  victim_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(find_moves_in_revision_range(&moves_table, parent_repos_relpath,
                                       repos_root_url, repos_uuid,
                                       victim_abspath, start_rev, end_rev,
                                       ctx, result_pool, scratch_pool));

  url = svn_path_url_add_component2(repos_root_url, parent_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL, FALSE, FALSE,
                                               ctx, scratch_pool,
                                               scratch_pool));

  paths = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(paths, const char *) = "";

  revprops = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;

  b.victim_abspath = victim_abspath;
  b.deleted_repos_relpath = svn_relpath_join(parent_repos_relpath,
                                             deleted_basename, scratch_pool);
  b.related_repos_relpath = related_repos_relpath;
  b.related_peg_rev = related_peg_rev;
  b.deleted_rev = SVN_INVALID_REVNUM;
  b.replacing_node_kind = svn_node_unknown;
  b.repos_root_url = repos_root_url;
  b.repos_uuid = repos_uuid;
  b.ctx = ctx;
  b.moves_table = moves_table;
  b.result_pool = result_pool;
  SVN_ERR(svn_ra__dup_session(&b.extra_ra_session, ra_session, NULL,
                              scratch_pool, scratch_pool));

  err = svn_ra_get_log2(ra_session, paths, start_rev, end_rev,
                        0, /* no limit */
                        TRUE, /* need the changed paths list */
                        FALSE, /* need to traverse copies */
                        FALSE, /* no need for merged revisions */
                        revprops,
                        find_deleted_rev, &b,
                        scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CEASE_INVOCATION &&
          b.deleted_rev != SVN_INVALID_REVNUM)

        {
          /* Log operation was aborted because we found deleted rev. */
          svn_error_clear(err);
        }
      else
        return svn_error_trace(err);
    }

  if (b.deleted_rev == SVN_INVALID_REVNUM)
    {
      struct repos_move_info *move = b.move;

      if (move)
        {
          *deleted_rev = move->rev;
          *deleted_rev_author = move->rev_author;
          *replacing_node_kind = b.replacing_node_kind;
          SVN_ERR(find_operative_moves(moves, moves_table,
                                       b.deleted_repos_relpath,
                                       move->rev,
                                       ra_session, repos_root_url,
                                       result_pool, scratch_pool));
        }
      else
        {
          /* We could not determine the revision in which the node was
           * deleted. */
          *deleted_rev = SVN_INVALID_REVNUM;
          *deleted_rev_author = NULL;
          *replacing_node_kind = svn_node_unknown;
          *moves = NULL;
        }
      return SVN_NO_ERROR;
    }
  else
    {
      *deleted_rev = b.deleted_rev;
      *deleted_rev_author = b.deleted_rev_author;
      *replacing_node_kind = b.replacing_node_kind;
      SVN_ERR(find_operative_moves(moves, moves_table,
                                   b.deleted_repos_relpath, b.deleted_rev,
                                   ra_session, repos_root_url,
                                   result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Details for tree conflicts involving a locally missing node. */
struct conflict_tree_local_missing_details
{
  /* If not SVN_INVALID_REVNUM, the node was deleted in DELETED_REV. */
  svn_revnum_t deleted_rev;

  /* Author who committed DELETED_REV. */
  const char *deleted_rev_author;

  /* The path which was deleted relative to the repository root. */
  const char *deleted_repos_relpath;

  /* Move information about the conflict victim. If not NULL, this is an
   * array of repos_move_info elements. Each element is the head of a
   * move chain which starts in DELETED_REV. */
  apr_array_header_t *moves;

  /* Move information about siblings. Siblings are nodes which share
   * a youngest common ancestor with the conflict victim. E.g. in case
   * of a merge operation they are part of the merge source branch.
   * If not NULL, this is an array of repos_move_info elements.
   * Each element is the head of a move chain, which starts at some
   * point in history after siblings and conflict victim forked off
   * their common ancestor. */
  apr_array_header_t *sibling_moves;

  /* If not NULL, this is the move target abspath. */
  const char *moved_to_abspath;
};

static svn_error_t *
find_related_node(const char **related_repos_relpath,
                  svn_revnum_t *related_peg_rev,
                  const char *younger_related_repos_relpath,
                  svn_revnum_t younger_related_peg_rev,
                  const char *older_repos_relpath,
                  svn_revnum_t older_peg_rev,
                  svn_client_conflict_t *conflict,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *repos_root_url;
  const char *related_url;
  const char *corrected_url;
  svn_node_kind_t related_node_kind;
  svn_ra_session_t *ra_session;

  *related_repos_relpath = NULL;
  *related_peg_rev = SVN_INVALID_REVNUM;

  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict,
                                             scratch_pool, scratch_pool));
  related_url = svn_path_url_add_component2(repos_root_url,
                                            younger_related_repos_relpath,
                                            scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                               &corrected_url,
                                               related_url, NULL,
                                               NULL,
                                               FALSE,
                                               FALSE,
                                               ctx,
                                               scratch_pool,
                                               scratch_pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", younger_related_peg_rev,
                            &related_node_kind, scratch_pool));
  if (related_node_kind == svn_node_none)
    {
      svn_revnum_t related_deleted_rev;
      const char *related_deleted_rev_author;
      svn_node_kind_t related_replacing_node_kind;
      const char *related_basename;
      const char *related_parent_repos_relpath;
      apr_array_header_t *related_moves;

      /* Looks like the younger node, which we'd like to use as our
       * 'related node', was deleted. Try to find its deleted revision
       *  so we can calculate a peg revision at which it exists.
       * The younger node is related to the older node, so we can use
       * the older node to guide us in our search. */
      related_basename = svn_relpath_basename(younger_related_repos_relpath,
                                              scratch_pool);
      related_parent_repos_relpath =
        svn_relpath_dirname(younger_related_repos_relpath, scratch_pool);
      SVN_ERR(find_revision_for_suspected_deletion(
                &related_deleted_rev, &related_deleted_rev_author,
                &related_replacing_node_kind, &related_moves,
                conflict, related_basename,
                related_parent_repos_relpath,
                younger_related_peg_rev, 0,
                older_repos_relpath, older_peg_rev,
                ctx, conflict->pool, scratch_pool));

      /* If we can't find a related node, bail. */
      if (related_deleted_rev == SVN_INVALID_REVNUM)
        return SVN_NO_ERROR;

      /* The node should exist in the revision before it was deleted. */
      *related_repos_relpath = younger_related_repos_relpath;
      *related_peg_rev = rev_below(related_deleted_rev);
    }
  else
    {
      *related_repos_relpath = younger_related_repos_relpath;
      *related_peg_rev = younger_related_peg_rev;
    }

  return SVN_NO_ERROR;
}

/* Determine if REPOS_RELPATH@PEG_REV was moved at some point in its history.
 * History's range of interest ends at END_REV which must be older than PEG_REV.
 *
 * VICTIM_ABSPATH is the abspath of a conflict victim in the working copy and
 * will be used in notifications.
 *
 * Return any applicable move chain heads in *MOVES.
 * If no moves can be found, set *MOVES to NULL. */
static svn_error_t *
find_moves_in_natural_history(apr_array_header_t **moves,
                              const char *repos_relpath,
                              svn_revnum_t peg_rev,
                              svn_node_kind_t node_kind,
                              svn_revnum_t end_rev,
                              const char *victim_abspath,
                              const char *repos_root_url,
                              const char *repos_uuid,
                              svn_ra_session_t *ra_session,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  apr_hash_t *moves_table;
  apr_array_header_t *revs;
  apr_array_header_t *most_recent_moves = NULL;
  int i;
  apr_pool_t *iterpool;

  *moves = NULL;

  SVN_ERR(find_moves_in_revision_range(&moves_table, repos_relpath,
                                       repos_root_url, repos_uuid,
                                       victim_abspath, peg_rev, end_rev,
                                       ctx, scratch_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);

  /* Scan the moves table for applicable moves. */
  revs = svn_sort__hash(moves_table, compare_items_as_revs, scratch_pool);
  for (i = revs->nelts - 1; i >= 0; i--)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(revs, i, svn_sort__item_t);
      apr_array_header_t *moves_in_rev = apr_hash_get(moves_table, item.key,
                                                      sizeof(svn_revnum_t));
      int j;

      svn_pool_clear(iterpool);

      /* Was repos relpath moved to its location in this revision? */
      for (j = 0; j < moves_in_rev->nelts; j++)
        {
          struct repos_move_info *move;
          const char *relpath;

          move = APR_ARRAY_IDX(moves_in_rev, j, struct repos_move_info *);
          relpath = svn_relpath_skip_ancestor(move->moved_to_repos_relpath,
                                              repos_relpath);
          if (relpath)
            {
              /* If the move did not happen in our peg revision, make
               * sure this move happened on the same line of history. */
              if (move->rev != peg_rev)
                {
                  svn_client__pathrev_t *yca_loc;
                  svn_error_t *err;

                  err = find_yca(&yca_loc, repos_relpath, peg_rev,
                                 repos_relpath, move->rev,
                                 repos_root_url, repos_uuid,
                                 NULL, ctx, iterpool, iterpool);
                  if (err)
                    {
                      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                        {
                          svn_error_clear(err);
                          yca_loc = NULL;
                        }
                      else
                        return svn_error_trace(err);
                    }

                  if (yca_loc == NULL || yca_loc->rev != move->rev)
                    continue;
                }

              if (most_recent_moves == NULL)
                most_recent_moves =
                  apr_array_make(result_pool, 1,
                                 sizeof(struct repos_move_info *));

              /* Copy the move to result pool (even if relpath is ""). */
              move = new_path_adjusted_move(move, relpath, node_kind,
                                            result_pool);
              APR_ARRAY_PUSH(most_recent_moves,
                             struct repos_move_info *) = move;
            }
        }

      /* If we found one move, or several ambiguous moves, we're done. */
      if (most_recent_moves)
        break;
    }

  if (most_recent_moves && most_recent_moves->nelts > 0)
    {
      *moves = apr_array_make(result_pool, 1,
                              sizeof(struct repos_move_info *));

      /* Figure out what happened to the most recent moves in prior
       * revisions and build move chains. */
      for (i = 0; i < most_recent_moves->nelts; i++)
        {
          struct repos_move_info *move;

          svn_pool_clear(iterpool);

          move = APR_ARRAY_IDX(most_recent_moves, i, struct repos_move_info *);
          SVN_ERR(trace_moved_node_backwards(moves_table, move,
                                             ra_session, repos_root_url,
                                             result_pool, iterpool));
          /* Follow the move chain backwards. */
          while (move->prev)
            move = move->prev;

          /* Return move heads. */
          APR_ARRAY_PUSH(*moves, struct repos_move_info *) = move;
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Implements tree_conflict_get_details_func_t. */
static svn_error_t *
conflict_tree_get_details_local_missing(svn_client_conflict_t *conflict,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *scratch_pool)
{
  const char *old_repos_relpath;
  const char *new_repos_relpath;
  const char *parent_repos_relpath;
  svn_revnum_t parent_peg_rev;
  svn_revnum_t old_rev;
  svn_revnum_t new_rev;
  svn_revnum_t deleted_rev;
  const char *deleted_rev_author;
  svn_node_kind_t replacing_node_kind;
  const char *deleted_basename;
  struct conflict_tree_local_missing_details *details;
  apr_array_header_t *moves = NULL;
  apr_array_header_t *sibling_moves = NULL;
  const char *related_repos_relpath;
  svn_revnum_t related_peg_rev;
  const char *repos_root_url;
  const char *repos_uuid;

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, NULL, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, NULL, conflict,
            scratch_pool, scratch_pool));

  /* Scan the conflict victim's parent's log to find a revision which
   * deleted the node. */
  deleted_basename = svn_dirent_basename(conflict->local_abspath,
                                         scratch_pool);
  SVN_ERR(svn_wc__node_get_repos_info(&parent_peg_rev, &parent_repos_relpath,
                                      &repos_root_url, &repos_uuid,
                                      ctx->wc_ctx,
                                      svn_dirent_dirname(
                                        conflict->local_abspath,
                                        scratch_pool),
                                      scratch_pool,
                                      scratch_pool));

  /* Pick the younger incoming node as our 'related node' which helps
   * pin-pointing the deleted conflict victim in history. */
  related_repos_relpath = 
            (old_rev < new_rev ? new_repos_relpath : old_repos_relpath);
  related_peg_rev = (old_rev < new_rev ? new_rev : old_rev);

  /* Make sure we're going to search the related node in a revision where
   * it exists. The younger incoming node might have been deleted in HEAD. */
  if (related_repos_relpath != NULL && related_peg_rev != SVN_INVALID_REVNUM)
    SVN_ERR(find_related_node(
              &related_repos_relpath, &related_peg_rev,
              related_repos_relpath, related_peg_rev,
              (old_rev < new_rev ? old_repos_relpath : new_repos_relpath),
              (old_rev < new_rev ? old_rev : new_rev),
              conflict, ctx, scratch_pool, scratch_pool));
    
  SVN_ERR(find_revision_for_suspected_deletion(
            &deleted_rev, &deleted_rev_author, &replacing_node_kind, &moves,
            conflict, deleted_basename, parent_repos_relpath,
            parent_peg_rev, 0, related_repos_relpath, related_peg_rev,
            ctx, conflict->pool, scratch_pool));

  /* If the victim was not deleted then check if the related path was moved. */
  if (deleted_rev == SVN_INVALID_REVNUM)
    {
      const char *victim_abspath;
      svn_ra_session_t *ra_session;
      const char *url, *corrected_url;
      svn_client__pathrev_t *yca_loc;
      svn_revnum_t end_rev;
      svn_node_kind_t related_node_kind;

      /* ### The following describes all moves in terms of forward-merges,
       * should do we something else for reverse-merges? */

      victim_abspath = svn_client_conflict_get_local_abspath(conflict);
      url = svn_path_url_add_component2(repos_root_url, related_repos_relpath,
                                        scratch_pool);
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                                   &corrected_url,
                                                   url, NULL, NULL,
                                                   FALSE,
                                                   FALSE,
                                                   ctx,
                                                   scratch_pool,
                                                   scratch_pool));

      /* Set END_REV to our best guess of the nearest YCA revision. */
      SVN_ERR(find_nearest_yca(&yca_loc, related_repos_relpath, related_peg_rev,
                               parent_repos_relpath, parent_peg_rev,
                               repos_root_url, repos_uuid, ra_session, ctx,
                               scratch_pool, scratch_pool));
      if (yca_loc == NULL)
        return SVN_NO_ERROR;
      end_rev = yca_loc->rev;

      /* END_REV must be smaller than RELATED_PEG_REV, else the call
         to find_moves_in_natural_history() below will error out. */
      if (end_rev >= related_peg_rev)
        end_rev = related_peg_rev > 0 ? related_peg_rev - 1 : 0;

      SVN_ERR(svn_ra_check_path(ra_session, "", related_peg_rev,
                                &related_node_kind, scratch_pool));
      SVN_ERR(find_moves_in_natural_history(&sibling_moves,
                                            related_repos_relpath,
                                            related_peg_rev,
                                            related_node_kind,
                                            end_rev,
                                            victim_abspath,
                                            repos_root_url, repos_uuid,
                                            ra_session, ctx,
                                            conflict->pool, scratch_pool));

      if (sibling_moves == NULL)
        return SVN_NO_ERROR;

      /* ## TODO: Find the missing node in the WC. */
    }

  details = apr_pcalloc(conflict->pool, sizeof(*details));
  details->deleted_rev = deleted_rev;
  details->deleted_rev_author = deleted_rev_author;
  if (deleted_rev != SVN_INVALID_REVNUM)
    details->deleted_repos_relpath = svn_relpath_join(parent_repos_relpath,
                                                      deleted_basename,
                                                      conflict->pool); 
  details->moves = moves;
  details->sibling_moves = sibling_moves;
                                         
  conflict->tree_conflict_local_details = details;

  return SVN_NO_ERROR;
}

/* Return a localised string representation of the local part of a tree
   conflict on a non-existent node. */
static svn_error_t *
describe_local_none_node_change(const char **description,
                                svn_client_conflict_t *conflict,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_wc_conflict_reason_t local_change;
  svn_wc_operation_t operation;

  local_change = svn_client_conflict_get_local_change(conflict);
  operation = svn_client_conflict_get_operation(conflict);

  switch (local_change)
    {
    case svn_wc_conflict_reason_edited:
      *description = _("An item containing uncommitted changes was "
                       "found in the working copy.");
      break;
    case svn_wc_conflict_reason_obstructed:
      *description = _("An item which already occupies this path was found in "
                       "the working copy.");
      break;
    case svn_wc_conflict_reason_deleted:
      *description = _("A deleted item was found in the working copy.");
      break;
    case svn_wc_conflict_reason_missing:
      if (operation == svn_wc_operation_update ||
          operation == svn_wc_operation_switch)
        *description = _("No such file or directory was found in the "
                         "working copy.");
      else if (operation == svn_wc_operation_merge)
        {
          /* ### display deleted revision */
          *description = _("No such file or directory was found in the "
                           "merge target working copy.\nThe item may "
                           "have been deleted or moved away in the "
                           "repository's history.");
        }
      break;
    case svn_wc_conflict_reason_unversioned:
      *description = _("An unversioned item was found in the working "
                       "copy.");
      break;
    case svn_wc_conflict_reason_added:
    case svn_wc_conflict_reason_replaced:
      *description = _("An item scheduled to be added to the repository "
                       "in the next commit was found in the working "
                       "copy.");
      break;
    case svn_wc_conflict_reason_moved_away:
      *description = _("The item in the working copy had been moved "
                       "away at the time this conflict was recorded.");
      break;
    case svn_wc_conflict_reason_moved_here:
      *description = _("An item had been moved here in the working copy "
                       "at the time this conflict was recorded.");
      break;
    }

  return SVN_NO_ERROR;
}

/* Append a description of a move chain beginning at NEXT to DESCRIPTION. */
static const char *
append_moved_to_chain_description(const char *description,
                                  apr_array_header_t *next,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  if (next == NULL)
    return description;

  while (next)
    {
      struct repos_move_info *move;

      /* Describe the first possible move chain only. Adding multiple chains
       * to the description would just be confusing. The user may select a
       * different move destination while resolving the conflict. */
      move = APR_ARRAY_IDX(next, 0, struct repos_move_info *);

      description = apr_psprintf(scratch_pool,
                                 _("%s\nAnd then moved away to '^/%s' by "
                                   "%s in r%ld."),
                                 description, move->moved_to_repos_relpath,
                                 move->rev_author, move->rev);
      next = move->next;
    }

  return apr_pstrdup(result_pool, description);
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_local_description_generic(const char **description,
                                            svn_client_conflict_t *conflict,
                                            svn_client_ctx_t *ctx,
                                            apr_pool_t *result_pool,
                                            apr_pool_t *scratch_pool)
{
  svn_node_kind_t victim_node_kind;

  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);

  *description = NULL;

  switch (victim_node_kind)
    {
      case svn_node_file:
      case svn_node_symlink:
        SVN_ERR(describe_local_file_node_change(description, conflict, ctx,
                                                result_pool, scratch_pool));
        break;
      case svn_node_dir:
        SVN_ERR(describe_local_dir_node_change(description, conflict, ctx,
                                               result_pool, scratch_pool));
        break;
      case svn_node_none:
      case svn_node_unknown:
        SVN_ERR(describe_local_none_node_change(description, conflict,
                                                result_pool, scratch_pool));
        break;
    }

  return SVN_NO_ERROR;
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_description_local_missing(const char **description,
                                            svn_client_conflict_t *conflict,
                                            svn_client_ctx_t *ctx,
                                            apr_pool_t *result_pool,
                                            apr_pool_t *scratch_pool)
{
  struct conflict_tree_local_missing_details *details;

  details = conflict->tree_conflict_local_details;
  if (details == NULL)
    return svn_error_trace(conflict_tree_get_local_description_generic(
                             description, conflict, ctx,
                             result_pool, scratch_pool));

  if (details->moves || details->sibling_moves)
    {
      struct repos_move_info *move;
      
      *description = _("No such file or directory was found in the "
                       "merge target working copy.\n");

      if (details->moves)
        {
          move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
          if (move->node_kind == svn_node_file)
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe file was moved to '^/%s' in r%ld by %s."),
                             *description, move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          else if (move->node_kind == svn_node_dir)
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe directory was moved to '^/%s' in "
                               "r%ld by %s."),
                             *description, move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          else
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe item was moved to '^/%s' in r%ld by %s."),
                             *description, move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          *description = append_moved_to_chain_description(*description,
                                                           move->next,
                                                           result_pool,
                                                           scratch_pool);
        }

      if (details->sibling_moves)
        {
          move = APR_ARRAY_IDX(details->sibling_moves, 0,
                               struct repos_move_info *);
          if (move->node_kind == svn_node_file)
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe file '^/%s' was moved to '^/%s' "
                               "in r%ld by %s."),
                             *description, move->moved_from_repos_relpath,
                             move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          else if (move->node_kind == svn_node_dir)
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe directory '^/%s' was moved to '^/%s' "
                               "in r%ld by %s."),
                             *description, move->moved_from_repos_relpath,
                             move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          else
            *description = apr_psprintf(
                             result_pool,
                             _("%sThe item '^/%s' was moved to '^/%s' "
                               "in r%ld by %s."),
                             *description, move->moved_from_repos_relpath,
                             move->moved_to_repos_relpath,
                             move->rev, move->rev_author);
          *description = append_moved_to_chain_description(*description,
                                                           move->next,
                                                           result_pool,
                                                           scratch_pool);
        }
    }
  else
    *description = apr_psprintf(
                     result_pool,
                     _("No such file or directory was found in the "
                       "merge target working copy.\n'^/%s' was deleted "
                       "in r%ld by %s."),
                     details->deleted_repos_relpath,
                     details->deleted_rev, details->deleted_rev_author);

  return SVN_NO_ERROR;
}

/* Return a localised string representation of the incoming part of a
   conflict; NULL for non-localised odd cases. */
static const char *
describe_incoming_change(svn_node_kind_t kind, svn_wc_conflict_action_t action,
                         svn_wc_operation_t operation)
{
  switch (kind)
    {
      case svn_node_file:
      case svn_node_symlink:
        if (operation == svn_wc_operation_update)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("An update operation tried to edit a file.");
                case svn_wc_conflict_action_add:
                  return _("An update operation tried to add a file.");
                case svn_wc_conflict_action_delete:
                  return _("An update operation tried to delete or move "
                           "a file.");
                case svn_wc_conflict_action_replace:
                  return _("An update operation tried to replace a file.");
              }
          }
        else if (operation == svn_wc_operation_switch)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A switch operation tried to edit a file.");
                case svn_wc_conflict_action_add:
                  return _("A switch operation tried to add a file.");
                case svn_wc_conflict_action_delete:
                  return _("A switch operation tried to delete or move "
                           "a file.");
                case svn_wc_conflict_action_replace:
                  return _("A switch operation tried to replace a file.");
              }
          }
        else if (operation == svn_wc_operation_merge)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A merge operation tried to edit a file.");
                case svn_wc_conflict_action_add:
                  return _("A merge operation tried to add a file.");
                case svn_wc_conflict_action_delete:
                  return _("A merge operation tried to delete or move "
                           "a file.");
                case svn_wc_conflict_action_replace:
                  return _("A merge operation tried to replace a file.");
            }
          }
        break;
      case svn_node_dir:
        if (operation == svn_wc_operation_update)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("An update operation tried to change a directory.");
                case svn_wc_conflict_action_add:
                  return _("An update operation tried to add a directory.");
                case svn_wc_conflict_action_delete:
                  return _("An update operation tried to delete or move "
                           "a directory.");
                case svn_wc_conflict_action_replace:
                  return _("An update operation tried to replace a directory.");
              }
          }
        else if (operation == svn_wc_operation_switch)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A switch operation tried to edit a directory.");
                case svn_wc_conflict_action_add:
                  return _("A switch operation tried to add a directory.");
                case svn_wc_conflict_action_delete:
                  return _("A switch operation tried to delete or move "
                           "a directory.");
                case svn_wc_conflict_action_replace:
                  return _("A switch operation tried to replace a directory.");
              }
          }
        else if (operation == svn_wc_operation_merge)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A merge operation tried to edit a directory.");
                case svn_wc_conflict_action_add:
                  return _("A merge operation tried to add a directory.");
                case svn_wc_conflict_action_delete:
                  return _("A merge operation tried to delete or move "
                           "a directory.");
                case svn_wc_conflict_action_replace:
                  return _("A merge operation tried to replace a directory.");
            }
          }
        break;
      case svn_node_none:
      case svn_node_unknown:
        if (operation == svn_wc_operation_update)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("An update operation tried to edit an item.");
                case svn_wc_conflict_action_add:
                  return _("An update operation tried to add an item.");
                case svn_wc_conflict_action_delete:
                  return _("An update operation tried to delete or move "
                           "an item.");
                case svn_wc_conflict_action_replace:
                  return _("An update operation tried to replace an item.");
              }
          }
        else if (operation == svn_wc_operation_switch)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A switch operation tried to edit an item.");
                case svn_wc_conflict_action_add:
                  return _("A switch operation tried to add an item.");
                case svn_wc_conflict_action_delete:
                  return _("A switch operation tried to delete or move "
                           "an item.");
                case svn_wc_conflict_action_replace:
                  return _("A switch operation tried to replace an item.");
              }
          }
        else if (operation == svn_wc_operation_merge)
          {
            switch (action)
              {
                case svn_wc_conflict_action_edit:
                  return _("A merge operation tried to edit an item.");
                case svn_wc_conflict_action_add:
                  return _("A merge operation tried to add an item.");
                case svn_wc_conflict_action_delete:
                  return _("A merge operation tried to delete or move "
                           "an item.");
                case svn_wc_conflict_action_replace:
                  return _("A merge operation tried to replace an item.");
              }
          }
        break;
    }

  return NULL;
}

/* Return a localised string representation of the operation part of a
   conflict. */
static const char *
operation_str(svn_wc_operation_t operation)
{
  switch (operation)
    {
    case svn_wc_operation_update: return _("upon update");
    case svn_wc_operation_switch: return _("upon switch");
    case svn_wc_operation_merge:  return _("upon merge");
    case svn_wc_operation_none:   return _("upon none");
    }
  SVN_ERR_MALFUNCTION_NO_RETURN();
  return NULL;
}

svn_error_t *
svn_client_conflict_prop_get_description(const char **description,
                                         svn_client_conflict_t *conflict,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool)
{
  const char *reason_str, *action_str;

  /* We provide separately translatable strings for the values that we
   * know about, and a fall-back in case any other values occur. */
  switch (svn_client_conflict_get_local_change(conflict))
    {
      case svn_wc_conflict_reason_edited:
        reason_str = _("local edit");
        break;
      case svn_wc_conflict_reason_added:
        reason_str = _("local add");
        break;
      case svn_wc_conflict_reason_deleted:
        reason_str = _("local delete");
        break;
      case svn_wc_conflict_reason_obstructed:
        reason_str = _("local obstruction");
        break;
      default:
        reason_str = apr_psprintf(
                       scratch_pool, _("local %s"),
                       svn_token__to_word(
                         map_conflict_reason,
                         svn_client_conflict_get_local_change(conflict)));
        break;
    }
  switch (svn_client_conflict_get_incoming_change(conflict))
    {
      case svn_wc_conflict_action_edit:
        action_str = _("incoming edit");
        break;
      case svn_wc_conflict_action_add:
        action_str = _("incoming add");
        break;
      case svn_wc_conflict_action_delete:
        action_str = _("incoming delete");
        break;
      default:
        action_str = apr_psprintf(
                       scratch_pool, _("incoming %s"),
                       svn_token__to_word(
                         map_conflict_action,
                         svn_client_conflict_get_incoming_change(conflict)));
        break;
    }
  SVN_ERR_ASSERT(reason_str && action_str);

  *description = apr_psprintf(result_pool, _("%s, %s %s"),
                              reason_str, action_str,
                              operation_str(
                                svn_client_conflict_get_operation(conflict)));

  return SVN_NO_ERROR;
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_incoming_description_generic(
  const char **incoming_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const char *action;
  svn_node_kind_t incoming_kind;
  svn_wc_conflict_action_t conflict_action;
  svn_wc_operation_t conflict_operation;

  conflict_action = svn_client_conflict_get_incoming_change(conflict);
  conflict_operation = svn_client_conflict_get_operation(conflict);

  /* Determine the node kind of the incoming change. */
  incoming_kind = svn_node_unknown;
  if (conflict_action == svn_wc_conflict_action_edit ||
      conflict_action == svn_wc_conflict_action_delete)
    {
      /* Change is acting on 'src_left' version of the node. */
      SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
                NULL, NULL, &incoming_kind, conflict, scratch_pool,
                scratch_pool));
    }
  else if (conflict_action == svn_wc_conflict_action_add ||
           conflict_action == svn_wc_conflict_action_replace)
    {
      /* Change is acting on 'src_right' version of the node.
       *
       * ### For 'replace', the node kind is ambiguous. However, src_left
       * ### is NULL for replace, so we must use src_right. */
      SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
                NULL, NULL, &incoming_kind, conflict, scratch_pool,
                scratch_pool));
    }

  action = describe_incoming_change(incoming_kind, conflict_action,
                                    conflict_operation);
  if (action)
    {
      *incoming_change_description = apr_pstrdup(result_pool, action);
    }
  else
    {
      /* A catch-all message for very rare or nominally impossible cases.
         It will not be pretty, but is closer to an internal error than
         an ordinary user-facing string. */
      *incoming_change_description = apr_psprintf(result_pool,
                                       _("incoming %s %s"),
                                       svn_node_kind_to_word(incoming_kind),
                                       svn_token__to_word(map_conflict_action,
                                                          conflict_action));
    }
  return SVN_NO_ERROR;
}

/* Details for tree conflicts involving incoming deletions and replacements. */
struct conflict_tree_incoming_delete_details
{
  /* If not SVN_INVALID_REVNUM, the node was deleted in DELETED_REV. */
  svn_revnum_t deleted_rev;

  /* If not SVN_INVALID_REVNUM, the node was added in ADDED_REV. The incoming
   * delete is the result of a reverse application of this addition. */
  svn_revnum_t added_rev;

  /* The path which was deleted/added relative to the repository root. */
  const char *repos_relpath;

  /* Author who committed DELETED_REV/ADDED_REV. */
  const char *rev_author;

  /* New node kind for a replaced node. This is svn_node_none for deletions. */
  svn_node_kind_t replacing_node_kind;

  /* Move information. If not NULL, this is an array of repos_move_info *
   * elements. Each element is the head of a move chain which starts in
   * DELETED_REV or in ADDED_REV (in which case moves should be interpreted
   * in reverse). */
  apr_array_header_t *moves;

  /* A map of repos_relpaths and working copy nodes for an incoming move.
   *
   * Each key is a "const char *" repository relpath corresponding to a
   * possible repository-side move destination node in the revision which
   * is the target revision in case of update and switch, or the merge-right
   * revision in case of a merge.
   *
   * Each value is an apr_array_header_t *.
   * Each array consists of "const char *" absolute paths to working copy
   * nodes which correspond to the repository node selected by the map key.
   * Each such working copy node is a potential local move target which can
   * be chosen to "follow" the incoming move when resolving a tree conflict.
   *
   * This may be an empty hash map in case if there is no move target path
   * in the working copy. */
  apr_hash_t *wc_move_targets;

  /* The preferred move target repository relpath. This is our key into
   * the WC_MOVE_TARGETS map above (can be overridden by the user). */
  const char *move_target_repos_relpath;

  /* The current index into the list of working copy nodes corresponding to
   * MOVE_TARGET_REPOS_REPLATH (can be overridden by the user). */
  int wc_move_target_idx;
};

/* Get the currently selected repository-side move target path.
 * If none was selected yet, determine and return a default one. */
static const char *
get_moved_to_repos_relpath(
  struct conflict_tree_incoming_delete_details *details,
  apr_pool_t *scratch_pool)
{
  struct repos_move_info *move;

  if (details->move_target_repos_relpath)
    return details->move_target_repos_relpath;

  if (details->wc_move_targets && apr_hash_count(details->wc_move_targets) > 0)
    {
      svn_sort__item_t item;
      apr_array_header_t *repos_relpaths;

      repos_relpaths = svn_sort__hash(details->wc_move_targets,
                                      svn_sort_compare_items_as_paths,
                                      scratch_pool);
      item = APR_ARRAY_IDX(repos_relpaths, 0, svn_sort__item_t);
      return (const char *)item.key;
    }

  move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
  return move->moved_to_repos_relpath;
}

static const char *
describe_incoming_deletion_upon_update(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  svn_revnum_t old_rev,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Directory updated from r%ld to r%ld was "
                           "replaced with a file by %s in r%ld."),
                         old_rev, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved to "
                               "'^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File updated from r%ld to r%ld was replaced "
                           "with a file from another line of history by "
                           "%s in r%ld."),
                         old_rev, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Item updated from r%ld to r%ld was replaced "
                           "with a file by %s in r%ld."), old_rev, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced item was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                          _("Directory updated from r%ld to r%ld was "
                            "replaced with a directory from another line "
                            "of history by %s in r%ld."),
                          old_rev, new_rev,
                          details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved to "
                               "'^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File updated from r%ld to r%ld was "
                           "replaced with a directory by %s in r%ld."),
                         old_rev, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Item updated from r%ld to r%ld was replaced "
                           "by %s in r%ld."), old_rev, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced item was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        {
          if (details->moves)
            {
              const char *description;
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("Directory updated from r%ld to r%ld was "
                               "moved to '^/%s' by %s in r%ld."),
                             old_rev, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Directory updated from r%ld to r%ld was "
                                  "deleted by %s in r%ld."),
                                old_rev, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("File updated from r%ld to r%ld was moved "
                               "to '^/%s' by %s in r%ld."), old_rev, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("File updated from r%ld to r%ld was "
                                  "deleted by %s in r%ld."), old_rev, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else
        {
          if (details->moves)
            {
              const char *description;
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description = 
                apr_psprintf(result_pool,
                             _("Item updated from r%ld to r%ld was moved "
                               "to '^/%s' by %s in r%ld."), old_rev, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Item updated from r%ld to r%ld was "
                                  "deleted by %s in r%ld."), old_rev, new_rev,
                                details->rev_author, details->deleted_rev);
        }
    }
}

static const char *
describe_incoming_reverse_addition_upon_update(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  svn_revnum_t old_rev,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory updated backwards from r%ld to r%ld "
                              "was a file before the replacement made by %s "
                              "in r%ld."), old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File updated backwards from r%ld to r%ld was a "
                              "file from another line of history before the "
                              "replacement made by %s in r%ld."),
                            old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item updated backwards from r%ld to r%ld was "
                              "replaced with a file by %s in r%ld."),
                            old_rev, new_rev,
                            details->rev_author, details->added_rev);
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory updated backwards from r%ld to r%ld "
                              "was a directory from another line of history "
                              "before the replacement made by %s in "
                              "r%ld."), old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File updated backwards from r%ld to r%ld was a "
                              "directory before the replacement made by %s "
                              "in r%ld."), old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item updated backwards from r%ld to r%ld was "
                              "replaced with a directory by %s in r%ld."),
                            old_rev, new_rev,
                            details->rev_author, details->added_rev);
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory updated backwards from r%ld to r%ld "
                              "did not exist before it was added by %s in "
                              "r%ld."), old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File updated backwards from r%ld to r%ld did "
                              "not exist before it was added by %s in r%ld."),
                            old_rev, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item updated backwards from r%ld to r%ld did "
                              "not exist before it was added by %s in r%ld."),
                            old_rev, new_rev,
                            details->rev_author, details->added_rev);
    }
}

static const char *
describe_incoming_deletion_upon_switch(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Directory switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a file by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved "
                               "to '^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;    
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                           "replaced with a file from another line of "
                           "history by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Item switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                           "replaced with a file by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced item was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Directory switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a directory from another "
                           "line of history by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved to "
                               "'^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a directory by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Item switched from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                           "replaced with a directory by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced item was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;
              
              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("Directory switched from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                               "was moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Directory switched from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                                  "was deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("File switched from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                               "moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("File switched from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                                  "deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("Item switched from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                               "moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Item switched from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                                  "deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
    }
}

static const char *
describe_incoming_reverse_addition_upon_switch(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was a file before the replacement made by %s "
                              "in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas a "
                              "file from another line of history before the "
                              "replacement made by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                              "replaced with a file by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was a directory from another line of history "
                              "before the replacement made by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("Directory switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was a file before the replacement made by %s "
                              "in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                              "replaced with a directory by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "did not exist before it was added by %s in "
                              "r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\ndid "
                              "not exist before it was added by %s in "
                              "r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item switched from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\ndid "
                              "not exist before it was added by %s in "
                              "r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
}

static const char *
describe_incoming_deletion_upon_merge(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Directory merged from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a file by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved to "
                               "'^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File merged from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                           "replaced with a file from another line of "
                           "history by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        return apr_psprintf(result_pool,
                            _("Item merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                              "replaced with a file by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->deleted_rev);
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Directory merged from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a directory from another "
                           "line of history by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced directory was moved to "
                               "'^/%s'."), description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("File merged from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                           "was replaced with a directory by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced file was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
      else
        {
          const char *description =
            apr_psprintf(result_pool,
                         _("Item merged from\n"
                           "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                           "replaced with a directory by %s in r%ld."),
                         old_repos_relpath, old_rev,
                         new_repos_relpath, new_rev,
                         details->rev_author, details->deleted_rev);
          if (details->moves)
            {
              struct repos_move_info *move;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("%s\nThe replaced item was moved to '^/%s'."),
                             description,
                             get_moved_to_repos_relpath(details, scratch_pool));
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          return description;
        }
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("Directory merged from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                               "moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Directory merged from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                                  "deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("File merged from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                               "moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("File merged from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                                  "deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
      else
        {
          if (details->moves)
            {
              struct repos_move_info *move;
              const char *description;

              move = APR_ARRAY_IDX(details->moves, 0, struct repos_move_info *);
              description =
                apr_psprintf(result_pool,
                             _("Item merged from\n"
                               "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                               "moved to '^/%s' by %s in r%ld."),
                             old_repos_relpath, old_rev,
                             new_repos_relpath, new_rev,
                             get_moved_to_repos_relpath(details, scratch_pool),
                             details->rev_author, details->deleted_rev);
              return append_moved_to_chain_description(description,
                                                       move->next,
                                                       result_pool,
                                                       scratch_pool);
            }
          else
            return apr_psprintf(result_pool,
                                _("Item merged from\n"
                                  "'^/%s@%ld'\nto\n'^/%s@%ld'\nwas "
                                  "deleted by %s in r%ld."),
                                old_repos_relpath, old_rev,
                                new_repos_relpath, new_rev,
                                details->rev_author, details->deleted_rev);
        }
    }
}

static const char *
describe_incoming_reverse_addition_upon_merge(
  struct conflict_tree_incoming_delete_details *details,
  svn_node_kind_t victim_node_kind,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (details->replacing_node_kind == svn_node_file ||
      details->replacing_node_kind == svn_node_symlink)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory reverse-merged from\n'^/%s@%ld'\nto "
                              "^/%s@%ld was a file before the replacement "
                              "made by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File reverse-merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was a file from another line of history before "
                              "the replacement made by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item reverse-merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was replaced with a file by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
  else if (details->replacing_node_kind == svn_node_dir)
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory reverse-merged from\n'^/%s@%ld'\nto "
                              "^/%s@%ld was a directory from another line "
                              "of history before the replacement made by %s "
                              "in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("Directory reverse-merged from\n'^/%s@%ld'\nto "
                              "^/%s@%ld was a file before the replacement "
                              "made by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item reverse-merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "was replaced with a directory by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
  else
    {
      if (victim_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Directory reverse-merged from\n'^/%s@%ld'\nto "
                              "^/%s@%ld did not exist before it was added "
                              "by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else if (victim_node_kind == svn_node_file ||
               victim_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("File reverse-merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "did not exist before it was added by %s in "
                              "r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("Item reverse-merged from\n"
                              "'^/%s@%ld'\nto\n'^/%s@%ld'\n"
                              "did not exist before it was added by %s in "
                              "r%ld."),
                            old_repos_relpath, old_rev,
                            new_repos_relpath, new_rev,
                            details->rev_author, details->added_rev);
    }
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_description_incoming_delete(
  const char **incoming_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const char *action;
  svn_node_kind_t victim_node_kind;
  svn_wc_operation_t conflict_operation;
  const char *old_repos_relpath;
  svn_revnum_t old_rev;
  const char *new_repos_relpath;
  svn_revnum_t new_rev;
  struct conflict_tree_incoming_delete_details *details;

  if (conflict->tree_conflict_incoming_details == NULL)
    return svn_error_trace(conflict_tree_get_incoming_description_generic(
                             incoming_change_description,
                             conflict, ctx, result_pool, scratch_pool));

  conflict_operation = svn_client_conflict_get_operation(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, NULL, conflict, scratch_pool,
            scratch_pool));

  details = conflict->tree_conflict_incoming_details;

  if (conflict_operation == svn_wc_operation_update)
    {
      if (details->deleted_rev != SVN_INVALID_REVNUM)
        {
          action = describe_incoming_deletion_upon_update(details,
                                                          victim_node_kind,
                                                          old_rev,
                                                          new_rev,
                                                          result_pool,
                                                          scratch_pool);
        }
      else /* details->added_rev != SVN_INVALID_REVNUM */
        {
          /* This deletion is really the reverse change of an addition. */
          action = describe_incoming_reverse_addition_upon_update(
                     details, victim_node_kind, old_rev, new_rev, result_pool);
        }
    }
  else if (conflict_operation == svn_wc_operation_switch)
    {
      if (details->deleted_rev != SVN_INVALID_REVNUM)
        {
          action = describe_incoming_deletion_upon_switch(details,
                                                          victim_node_kind,
                                                          old_repos_relpath,
                                                          old_rev,
                                                          new_repos_relpath,
                                                          new_rev,
                                                          result_pool,
                                                          scratch_pool);
        }
      else /* details->added_rev != SVN_INVALID_REVNUM */
        {
          /* This deletion is really the reverse change of an addition. */
          action = describe_incoming_reverse_addition_upon_switch(
                     details, victim_node_kind, old_repos_relpath, old_rev,
                     new_repos_relpath, new_rev, result_pool);
            
        }
      }
  else if (conflict_operation == svn_wc_operation_merge)
    {
      if (details->deleted_rev != SVN_INVALID_REVNUM)
        {
          action = describe_incoming_deletion_upon_merge(details,
                                                         victim_node_kind,
                                                         old_repos_relpath,
                                                         old_rev,
                                                         new_repos_relpath,
                                                         new_rev,
                                                         result_pool,
                                                         scratch_pool);
        }
      else /* details->added_rev != SVN_INVALID_REVNUM */
        {
          /* This deletion is really the reverse change of an addition. */
          action = describe_incoming_reverse_addition_upon_merge(
                     details, victim_node_kind, old_repos_relpath, old_rev,
                     new_repos_relpath, new_rev, result_pool);
        }
      }

  *incoming_change_description = apr_pstrdup(result_pool, action);

  return SVN_NO_ERROR;
}

/* Baton for find_added_rev(). */
struct find_added_rev_baton
{
  const char *victim_abspath;
  svn_client_ctx_t *ctx;
  svn_revnum_t added_rev;
  const char *repos_relpath;
  const char *parent_repos_relpath;
  apr_pool_t *pool;
};

/* Implements svn_location_segment_receiver_t.
 * Finds the revision in which a node was added by tracing 'start'
 * revisions in location segments reported for the node.
 * If the PARENT_REPOS_RELPATH in the baton is not NULL, only consider
 * segments in which the node existed somwhere beneath this path. */
static svn_error_t *
find_added_rev(svn_location_segment_t *segment,
               void *baton,
               apr_pool_t *scratch_pool)
{
  struct find_added_rev_baton *b = baton;

  if (b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 b->victim_abspath,
                 svn_wc_notify_tree_conflict_details_progress,
                 scratch_pool),
      notify->revision = segment->range_start;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  if (segment->path) /* not interested in gaps */
    {
      if (b->parent_repos_relpath == NULL ||
          svn_relpath_skip_ancestor(b->parent_repos_relpath,
                                    segment->path) != NULL)
        {
          b->added_rev = segment->range_start;
          b->repos_relpath = apr_pstrdup(b->pool, segment->path);
        }
    }

  return SVN_NO_ERROR;
}

/* Find conflict details in the case where a revision which added a node was
 * applied in reverse, resulting in an incoming deletion. */
static svn_error_t *
get_incoming_delete_details_for_reverse_addition(
  struct conflict_tree_incoming_delete_details **details,
  const char *repos_root_url,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  svn_revnum_t new_rev,
  svn_client_ctx_t *ctx,
  const char *victim_abspath,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  svn_string_t *author_revprop;
  struct find_added_rev_baton b = { 0 };

  url = svn_path_url_add_component2(repos_root_url, old_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                               &corrected_url,
                                               url, NULL, NULL,
                                               FALSE,
                                               FALSE,
                                               ctx,
                                               scratch_pool,
                                               scratch_pool));

  *details = apr_pcalloc(result_pool, sizeof(**details));
  b.ctx = ctx;
  b.victim_abspath = victim_abspath;
  b.added_rev = SVN_INVALID_REVNUM;
  b.repos_relpath = NULL;
  b.parent_repos_relpath = NULL;
  b.pool = scratch_pool;

  /* Figure out when this node was added. */
  SVN_ERR(svn_ra_get_location_segments(ra_session, "", old_rev,
                                       old_rev, new_rev,
                                       find_added_rev, &b,
                                       scratch_pool));

  SVN_ERR(svn_ra_rev_prop(ra_session, b.added_rev,
                          SVN_PROP_REVISION_AUTHOR,
                          &author_revprop, scratch_pool));
  (*details)->deleted_rev = SVN_INVALID_REVNUM;
  (*details)->added_rev = b.added_rev;
  (*details)->repos_relpath = apr_pstrdup(result_pool, b.repos_relpath);
  if (author_revprop)
    (*details)->rev_author = apr_pstrdup(result_pool, author_revprop->data);
  else
    (*details)->rev_author = _("unknown author");

  /* Check for replacement. */
  (*details)->replacing_node_kind = svn_node_none;
  if ((*details)->added_rev > 0)
    {
      svn_node_kind_t replaced_node_kind;

      SVN_ERR(svn_ra_check_path(ra_session, "",
                                rev_below((*details)->added_rev),
                                &replaced_node_kind, scratch_pool));
      if (replaced_node_kind != svn_node_none)
        SVN_ERR(svn_ra_check_path(ra_session, "", (*details)->added_rev,
                                  &(*details)->replacing_node_kind,
                                  scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Follow each move chain starting a MOVE all the way to the end to find
 * the possible working copy locations for VICTIM_ABSPATH which corresponds
 * to VICTIM_REPOS_REPLATH@VICTIM_REVISION.
 * Add each such location to the WC_MOVE_TARGETS hash table, keyed on the
 * repos_relpath which is the corresponding move destination in the repository.
 * This function is recursive. */
static svn_error_t *
follow_move_chains(apr_hash_t *wc_move_targets,
                   struct repos_move_info *move,
                   svn_client_ctx_t *ctx,
                   const char *victim_abspath,
                   svn_node_kind_t victim_node_kind,
                   const char *victim_repos_relpath,
                   svn_revnum_t victim_revision,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  /* If this is the end of a move chain, look for matching paths in
   * the working copy and add them to our collection if found. */
  if (move->next == NULL)
    {
      apr_array_header_t *candidate_abspaths;

      /* Gather candidate nodes which represent this moved_to_repos_relpath. */
      SVN_ERR(svn_wc__guess_incoming_move_target_nodes(
                &candidate_abspaths, ctx->wc_ctx,
                victim_abspath, victim_node_kind,
                move->moved_to_repos_relpath,
                scratch_pool, scratch_pool));
      if (candidate_abspaths->nelts > 0)
        {
          apr_array_header_t *moved_to_abspaths;
          int i;
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          moved_to_abspaths = apr_array_make(result_pool, 1,
                                             sizeof (const char *));

          for (i = 0; i < candidate_abspaths->nelts; i++)
            {
              const char *candidate_abspath;
              const char *repos_root_url;
              const char *repos_uuid;
              const char *candidate_repos_relpath;
              svn_revnum_t candidate_revision;

              svn_pool_clear(iterpool);

              candidate_abspath = APR_ARRAY_IDX(candidate_abspaths, i,
                                                const char *);
              SVN_ERR(svn_wc__node_get_origin(NULL, &candidate_revision,
                                              &candidate_repos_relpath,
                                              &repos_root_url,
                                              &repos_uuid,
                                              NULL, NULL,
                                              ctx->wc_ctx,
                                              candidate_abspath,
                                              FALSE,
                                              iterpool, iterpool));

              if (candidate_revision == SVN_INVALID_REVNUM)
                continue;

              /* If the conflict victim and the move target candidate
               * are not from the same revision we must ensure that
               * they are related. */
               if (candidate_revision != victim_revision)
                {
                  svn_client__pathrev_t *yca_loc;
                  svn_error_t *err;

                  err = find_yca(&yca_loc, victim_repos_relpath,
                                 victim_revision,
                                 candidate_repos_relpath,
                                 candidate_revision,
                                 repos_root_url, repos_uuid,
                                 NULL, ctx, iterpool, iterpool);
                  if (err)
                    {
                      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                        {
                          svn_error_clear(err);
                          yca_loc = NULL;
                        }
                      else
                        return svn_error_trace(err);
                    }

                  if (yca_loc == NULL)
                    continue;
                }

              APR_ARRAY_PUSH(moved_to_abspaths, const char *) =
                apr_pstrdup(result_pool, candidate_abspath);
            }
          svn_pool_destroy(iterpool);

          svn_hash_sets(wc_move_targets, move->moved_to_repos_relpath,
                        moved_to_abspaths);
        }
    }
  else
    {
      int i;
      apr_pool_t *iterpool;

      /* Recurse into each of the possible move chains. */
      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < move->next->nelts; i++)
        {
          struct repos_move_info *next_move;

          svn_pool_clear(iterpool);

          next_move = APR_ARRAY_IDX(move->next, i, struct repos_move_info *);
          SVN_ERR(follow_move_chains(wc_move_targets, next_move,
                                     ctx, victim_abspath, victim_node_kind,
                                     victim_repos_relpath, victim_revision,
                                     result_pool, iterpool));
                                        
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
init_wc_move_targets(struct conflict_tree_incoming_delete_details *details,
                     svn_client_conflict_t *conflict,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  int i;
  const char *victim_abspath;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_wc_operation_t operation;

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  operation = svn_client_conflict_get_operation(conflict);
  /* ### Should we get the old location in case of reverse-merges? */
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict,
            scratch_pool, scratch_pool));
  details->wc_move_targets = apr_hash_make(conflict->pool);
  for (i = 0; i < details->moves->nelts; i++)
    {
      struct repos_move_info *move;

      move = APR_ARRAY_IDX(details->moves, i, struct repos_move_info *);
      SVN_ERR(follow_move_chains(details->wc_move_targets, move,
                                 ctx, victim_abspath,
                                 victim_node_kind,
                                 incoming_new_repos_relpath,
                                 incoming_new_pegrev,
                                 conflict->pool, scratch_pool));
    }

  /* Initialize to the first possible move target. Hopefully,
   * in most cases there will only be one candidate anyway. */
  details->move_target_repos_relpath =
    get_moved_to_repos_relpath(details, scratch_pool);
  details->wc_move_target_idx = 0;

  /* If only one move target exists after an update or switch,
   * recommend a resolution option which follows the incoming move. */
  if (apr_hash_count(details->wc_move_targets) == 1 &&
      (operation == svn_wc_operation_update ||
       operation == svn_wc_operation_switch))
    {
      apr_array_header_t *wc_abspaths;

      wc_abspaths = svn_hash_gets(details->wc_move_targets,
                                  details->move_target_repos_relpath);
      if (wc_abspaths->nelts == 1)
        {
          svn_client_conflict_option_id_t recommended[] =
            {
              /* Only one of these will be present for any given conflict. */
              svn_client_conflict_option_incoming_move_file_text_merge,
              svn_client_conflict_option_incoming_move_dir_merge,
              svn_client_conflict_option_local_move_file_text_merge
            };
          apr_array_header_t *options;

          SVN_ERR(svn_client_conflict_tree_get_resolution_options(
                    &options, conflict, ctx, scratch_pool, scratch_pool));
          for (i = 0; i < (sizeof(recommended) / sizeof(recommended[0])); i++)
            {
              svn_client_conflict_option_id_t option_id = recommended[i];

              if (svn_client_conflict_option_find_by_id(options, option_id))
                {
                  conflict->recommended_option_id = option_id;
                  break;
                }
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Implements tree_conflict_get_details_func_t.
 * Find the revision in which the victim was deleted in the repository. */
static svn_error_t *
conflict_tree_get_details_incoming_delete(svn_client_conflict_t *conflict,
                                          svn_client_ctx_t *ctx,
                                          apr_pool_t *scratch_pool)
{
  const char *old_repos_relpath;
  const char *new_repos_relpath;
  const char *repos_root_url;
  svn_revnum_t old_rev;
  svn_revnum_t new_rev;
  svn_node_kind_t old_kind;
  svn_node_kind_t new_kind;
  struct conflict_tree_incoming_delete_details *details;
  svn_wc_operation_t operation;

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, &old_kind, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, &new_kind, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict,
                                             scratch_pool, scratch_pool));
  operation = svn_client_conflict_get_operation(conflict);

  if (operation == svn_wc_operation_update)
    {
      if (old_rev < new_rev)
        {
          const char *parent_repos_relpath;
          svn_revnum_t parent_peg_rev;
          svn_revnum_t deleted_rev;
          const char *deleted_rev_author;
          svn_node_kind_t replacing_node_kind;
          apr_array_header_t *moves;
          const char *related_repos_relpath;
          svn_revnum_t related_peg_rev;

          /* The update operation went forward in history. */
          SVN_ERR(svn_wc__node_get_repos_info(&parent_peg_rev,
                                              &parent_repos_relpath,
                                              NULL, NULL,
                                              ctx->wc_ctx,
                                              svn_dirent_dirname(
                                                conflict->local_abspath,
                                                scratch_pool),
                                              scratch_pool,
                                              scratch_pool));
          if (new_kind == svn_node_none)
            {
              SVN_ERR(find_related_node(&related_repos_relpath,
                                        &related_peg_rev,
                                        new_repos_relpath, new_rev,
                                        old_repos_relpath, old_rev,
                                        conflict, ctx,
                                        scratch_pool, scratch_pool));
            }
          else
            {
              /* related to self */
              related_repos_relpath = NULL;
              related_peg_rev = SVN_INVALID_REVNUM;
            }

          SVN_ERR(find_revision_for_suspected_deletion(
                    &deleted_rev, &deleted_rev_author, &replacing_node_kind,
                    &moves, conflict,
                    svn_dirent_basename(conflict->local_abspath, scratch_pool),
                    parent_repos_relpath, parent_peg_rev,
                    new_kind == svn_node_none ? 0 : old_rev,
                    related_repos_relpath, related_peg_rev,
                    ctx, conflict->pool, scratch_pool));
          if (deleted_rev == SVN_INVALID_REVNUM)
            {
              /* We could not determine the revision in which the node was
               * deleted. We cannot provide the required details so the best
               * we can do is fall back to the default description. */
              return SVN_NO_ERROR;
            }

          details = apr_pcalloc(conflict->pool, sizeof(*details));
          details->deleted_rev = deleted_rev;
          details->added_rev = SVN_INVALID_REVNUM;
          details->repos_relpath = apr_pstrdup(conflict->pool,
                                               new_repos_relpath);
          details->rev_author = deleted_rev_author;
          details->replacing_node_kind = replacing_node_kind;
          details->moves = moves;
        }
      else /* new_rev < old_rev */
        {
          /* The update operation went backwards in history.
           * Figure out when this node was added. */
          SVN_ERR(get_incoming_delete_details_for_reverse_addition(
                    &details, repos_root_url, old_repos_relpath,
                    old_rev, new_rev, ctx,
                    svn_client_conflict_get_local_abspath(conflict),
                    conflict->pool, scratch_pool));
        }
    }
  else if (operation == svn_wc_operation_switch ||
           operation == svn_wc_operation_merge)
    {
      if (old_rev < new_rev)
        {
          svn_revnum_t deleted_rev;
          const char *deleted_rev_author;
          svn_node_kind_t replacing_node_kind;
          apr_array_header_t *moves;

          /* The switch/merge operation went forward in history.
           *
           * The deletion of the node happened on the branch we switched to
           * or merged from. Scan new_repos_relpath's parent's log to find
           * the revision which deleted the node. */
          SVN_ERR(find_revision_for_suspected_deletion(
                    &deleted_rev, &deleted_rev_author, &replacing_node_kind,
                    &moves, conflict,
                    svn_relpath_basename(new_repos_relpath, scratch_pool),
                    svn_relpath_dirname(new_repos_relpath, scratch_pool),
                    new_rev, old_rev, old_repos_relpath, old_rev, ctx,
                    conflict->pool, scratch_pool));
          if (deleted_rev == SVN_INVALID_REVNUM)
            {
              /* We could not determine the revision in which the node was
               * deleted. We cannot provide the required details so the best
               * we can do is fall back to the default description. */
              return SVN_NO_ERROR;
            }

          details = apr_pcalloc(conflict->pool, sizeof(*details));
          details->deleted_rev = deleted_rev;
          details->added_rev = SVN_INVALID_REVNUM;
          details->repos_relpath = apr_pstrdup(conflict->pool,
                                               new_repos_relpath);
          details->rev_author = apr_pstrdup(conflict->pool,
                                            deleted_rev_author);
          details->replacing_node_kind = replacing_node_kind;
          details->moves = moves;
        }
      else /* new_rev < old_rev */
        {
          /* The switch/merge operation went backwards in history.
           * Figure out when the node we switched away from, or merged
           * from another branch, was added. */
          SVN_ERR(get_incoming_delete_details_for_reverse_addition(
                    &details, repos_root_url, old_repos_relpath,
                    old_rev, new_rev, ctx,
                    svn_client_conflict_get_local_abspath(conflict),
                    conflict->pool, scratch_pool));
        }
    }
  else
    {
      details = NULL;
    }

  conflict->tree_conflict_incoming_details = details;

  if (details && details->moves)
    SVN_ERR(init_wc_move_targets(details, conflict, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* Details for tree conflicts involving incoming additions. */
struct conflict_tree_incoming_add_details
{
  /* If not SVN_INVALID_REVNUM, the node was added in ADDED_REV. */
  svn_revnum_t added_rev;

  /* If not SVN_INVALID_REVNUM, the node was deleted in DELETED_REV.
   * Note that both ADDED_REV and DELETED_REV may be valid for update/switch.
   * See comment in conflict_tree_get_details_incoming_add() for details. */
  svn_revnum_t deleted_rev;

  /* The path which was added/deleted relative to the repository root. */
  const char *repos_relpath;

  /* Authors who committed ADDED_REV/DELETED_REV. */
  const char *added_rev_author;
  const char *deleted_rev_author;

  /* Move information. If not NULL, this is an array of repos_move_info *
   * elements. Each element is the head of a move chain which starts in
   * ADDED_REV or in DELETED_REV (in which case moves should be interpreted
   * in reverse). */
  apr_array_header_t *moves;
};

/* Implements tree_conflict_get_details_func_t.
 * Find the revision in which the victim was added in the repository. */
static svn_error_t *
conflict_tree_get_details_incoming_add(svn_client_conflict_t *conflict,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *scratch_pool)
{
  const char *old_repos_relpath;
  const char *new_repos_relpath;
  const char *repos_root_url;
  svn_revnum_t old_rev;
  svn_revnum_t new_rev;
  struct conflict_tree_incoming_add_details *details;
  svn_wc_operation_t operation;

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict,
                                             scratch_pool, scratch_pool));
  operation = svn_client_conflict_get_operation(conflict);

  if (operation == svn_wc_operation_update ||
      operation == svn_wc_operation_switch)
    {
      /* Only the new repository location is recorded for the node which
       * caused an incoming addition. There is no pre-update/pre-switch
       * revision to be recorded for the node since it does not exist in
       * the repository at that revision.
       * The implication is that we cannot know whether the operation went
       * forward or backwards in history. So always try to find an added
       * and a deleted revision for the node. Users must figure out by whether
       * the addition or deletion caused the conflict. */
      const char *url;
      const char *corrected_url;
      svn_string_t *author_revprop;
      struct find_added_rev_baton b = { 0 };
      svn_ra_session_t *ra_session;
      svn_revnum_t deleted_rev;
      svn_revnum_t head_rev;

      url = svn_path_url_add_component2(repos_root_url, new_repos_relpath,
                                        scratch_pool);
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                                   &corrected_url,
                                                   url, NULL, NULL,
                                                   FALSE,
                                                   FALSE,
                                                   ctx,
                                                   scratch_pool,
                                                   scratch_pool));

      details = apr_pcalloc(conflict->pool, sizeof(*details));
      b.ctx = ctx,
      b.victim_abspath = svn_client_conflict_get_local_abspath(conflict),
      b.added_rev = SVN_INVALID_REVNUM;
      b.repos_relpath = NULL;
      b.parent_repos_relpath = NULL;
      b.pool = scratch_pool;

      /* Figure out when this node was added. */
      SVN_ERR(svn_ra_get_location_segments(ra_session, "", new_rev,
                                           new_rev, SVN_INVALID_REVNUM,
                                           find_added_rev, &b,
                                           scratch_pool));

      SVN_ERR(svn_ra_rev_prop(ra_session, b.added_rev,
                              SVN_PROP_REVISION_AUTHOR,
                              &author_revprop, scratch_pool));
      details->repos_relpath = apr_pstrdup(conflict->pool, b.repos_relpath);
      details->added_rev = b.added_rev;
      if (author_revprop)
        details->added_rev_author = apr_pstrdup(conflict->pool,
                                          author_revprop->data);
      else
        details->added_rev_author = _("unknown author");
      details->deleted_rev = SVN_INVALID_REVNUM;
      details->deleted_rev_author = NULL;

      /* Figure out whether this node was deleted later.
       * ### Could probably optimize by infering both addition and deletion
       * ### from svn_ra_get_location_segments() call above. */
      SVN_ERR(svn_ra_get_latest_revnum(ra_session, &head_rev, scratch_pool));
      if (new_rev < head_rev)
        {
          SVN_ERR(svn_ra_get_deleted_rev(ra_session, "", new_rev, head_rev,
                                         &deleted_rev, scratch_pool));
          if (SVN_IS_VALID_REVNUM(deleted_rev))
           {
              SVN_ERR(svn_ra_rev_prop(ra_session, deleted_rev,
                                      SVN_PROP_REVISION_AUTHOR,
                                      &author_revprop, scratch_pool));
              details->deleted_rev = deleted_rev;
              if (author_revprop)
                details->deleted_rev_author = apr_pstrdup(conflict->pool,
                                                          author_revprop->data);
              else
                details->deleted_rev_author = _("unknown author");
            }
        }
    }
  else if (operation == svn_wc_operation_merge)
    {
      if (old_rev < new_rev)
        {
          /* The merge operation went forwards in history.
           * The addition of the node happened on the branch we merged form.
           * Scan the nodes's history to find the revision which added it. */
          const char *url;
          const char *corrected_url;
          svn_string_t *author_revprop;
          struct find_added_rev_baton b = { 0 };
          svn_ra_session_t *ra_session;

          url = svn_path_url_add_component2(repos_root_url, new_repos_relpath,
                                            scratch_pool);
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                                       &corrected_url,
                                                       url, NULL, NULL,
                                                       FALSE,
                                                       FALSE,
                                                       ctx,
                                                       scratch_pool,
                                                       scratch_pool));

          details = apr_pcalloc(conflict->pool, sizeof(*details));
          b.victim_abspath = svn_client_conflict_get_local_abspath(conflict);
          b.ctx = ctx;
          b.added_rev = SVN_INVALID_REVNUM;
          b.repos_relpath = NULL;
          b.parent_repos_relpath = NULL;
          b.pool = scratch_pool;

          /* Figure out when this node was added. */
          SVN_ERR(svn_ra_get_location_segments(ra_session, "", new_rev,
                                               new_rev, old_rev,
                                               find_added_rev, &b,
                                               scratch_pool));

          SVN_ERR(svn_ra_rev_prop(ra_session, b.added_rev,
                                  SVN_PROP_REVISION_AUTHOR,
                                  &author_revprop, scratch_pool));
          details->repos_relpath = apr_pstrdup(conflict->pool, b.repos_relpath);
          details->added_rev = b.added_rev;
          if (author_revprop)
            details->added_rev_author = apr_pstrdup(conflict->pool,
                                                    author_revprop->data);
          else
            details->added_rev_author = _("unknown author");
          details->deleted_rev = SVN_INVALID_REVNUM;
          details->deleted_rev_author = NULL;
        }
      else
        {
          /* The merge operation was a reverse-merge.
           * This addition is in fact a deletion, applied in reverse,
           * which happened on the branch we merged from.
           * Find the revision which deleted the node. */
          svn_revnum_t deleted_rev;
          const char *deleted_rev_author;
          svn_node_kind_t replacing_node_kind;
          apr_array_header_t *moves;

          SVN_ERR(find_revision_for_suspected_deletion(
                    &deleted_rev, &deleted_rev_author, &replacing_node_kind,
                    &moves, conflict,
                    svn_relpath_basename(old_repos_relpath, scratch_pool),
                    svn_relpath_dirname(old_repos_relpath, scratch_pool),
                    old_rev, new_rev,
                    NULL, SVN_INVALID_REVNUM, /* related to self */
                    ctx,
                    conflict->pool, scratch_pool));
          if (deleted_rev == SVN_INVALID_REVNUM)
            {
              /* We could not determine the revision in which the node was
               * deleted. We cannot provide the required details so the best
               * we can do is fall back to the default description. */
              return SVN_NO_ERROR;
            }

          details = apr_pcalloc(conflict->pool, sizeof(*details));
          details->repos_relpath = apr_pstrdup(conflict->pool,
                                               new_repos_relpath);
          details->deleted_rev = deleted_rev;
          details->deleted_rev_author = apr_pstrdup(conflict->pool,
                                                    deleted_rev_author);

          details->added_rev = SVN_INVALID_REVNUM;
          details->added_rev_author = NULL;
          details->moves = moves;
        }
    }
  else
    {
      details = NULL;
    }

  conflict->tree_conflict_incoming_details = details;

  return SVN_NO_ERROR;
}

static const char *
describe_incoming_add_upon_update(
  struct conflict_tree_incoming_add_details *details,
  svn_node_kind_t new_node_kind,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (new_node_kind == svn_node_dir)
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new directory appeared during update to r%ld; "
                              "it was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new directory appeared during update to r%ld; "
                              "it was added by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new directory appeared during update to r%ld; "
                              "it was deleted by %s in r%ld."), new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
  else if (new_node_kind == svn_node_file ||
           new_node_kind == svn_node_symlink)
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new file appeared during update to r%ld; "
                              "it was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new file appeared during update to r%ld; "
                              "it was added by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new file appeared during update to r%ld; "
                              "it was deleted by %s in r%ld."), new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
  else
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new item appeared during update to r%ld; "
                              "it was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new item appeared during update to r%ld; "
                              "it was added by %s in r%ld."), new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new item appeared during update to r%ld; "
                              "it was deleted by %s in r%ld."), new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
}

static const char *
describe_incoming_add_upon_switch(
  struct conflict_tree_incoming_add_details *details,
  svn_node_kind_t victim_node_kind,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (victim_node_kind == svn_node_dir)
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new directory appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new directory appeared during switch to\n"
                             "'^/%s@%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new directory appeared during switch to\n"
                              "'^/%s@%ld'.\nIt was deleted by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
  else if (victim_node_kind == svn_node_file ||
           victim_node_kind == svn_node_symlink)
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new file appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new file appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new file appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was deleted by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
  else
    {
      if (SVN_IS_VALID_REVNUM(details->added_rev) &&
          SVN_IS_VALID_REVNUM(details->deleted_rev))
        return apr_psprintf(result_pool,
                            _("A new item appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was added by %s in r%ld and later deleted "
                              "by %s in r%ld."), new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev,
                            details->deleted_rev_author, details->deleted_rev);
      else if (SVN_IS_VALID_REVNUM(details->added_rev))
        return apr_psprintf(result_pool,
                            _("A new item appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new item appeared during switch to\n"
                              "'^/%s@%ld'.\n"
                              "It was deleted by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->deleted_rev_author, details->deleted_rev);
    }
}

static const char *
describe_incoming_add_upon_merge(
  struct conflict_tree_incoming_add_details *details,
  svn_node_kind_t new_node_kind,
  svn_revnum_t old_rev,
  const char *new_repos_relpath,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (new_node_kind == svn_node_dir)
    {
      if (old_rev + 1 == new_rev)
        return apr_psprintf(result_pool,
                            _("A new directory appeared during merge of\n"
                              "'^/%s:%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new directory appeared during merge of\n"
                              "'^/%s:%ld-%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, old_rev + 1, new_rev,
                            details->added_rev_author, details->added_rev);
    }
  else if (new_node_kind == svn_node_file ||
           new_node_kind == svn_node_symlink)
    {
      if (old_rev + 1 == new_rev)
        return apr_psprintf(result_pool,
                            _("A new file appeared during merge of\n"
                              "'^/%s:%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new file appeared during merge of\n"
                              "'^/%s:%ld-%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, old_rev + 1, new_rev,
                            details->added_rev_author, details->added_rev);
    }
  else
    {
      if (old_rev + 1 == new_rev)
        return apr_psprintf(result_pool,
                            _("A new item appeared during merge of\n"
                              "'^/%s:%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, new_rev,
                            details->added_rev_author, details->added_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new item appeared during merge of\n"
                              "'^/%s:%ld-%ld'.\nIt was added by %s in r%ld."),
                            new_repos_relpath, old_rev + 1, new_rev,
                            details->added_rev_author, details->added_rev);
    }
}

static const char *
describe_incoming_reverse_deletion_upon_merge(
  struct conflict_tree_incoming_add_details *details,
  svn_node_kind_t new_node_kind,
  const char *old_repos_relpath,
  svn_revnum_t old_rev,
  svn_revnum_t new_rev,
  apr_pool_t *result_pool)
{
  if (new_node_kind == svn_node_dir)
    {
      if (new_rev + 1 == old_rev)
        return apr_psprintf(result_pool,
                            _("A new directory appeared during reverse-merge of"
                              "\n'^/%s:%ld'.\nIt was deleted by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            details->deleted_rev_author,
                            details->deleted_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new directory appeared during reverse-merge "
                              "of\n'^/%s:%ld-%ld'.\n"
                              "It was deleted by %s in r%ld."),
                            old_repos_relpath, new_rev, rev_below(old_rev),
                            details->deleted_rev_author,
                            details->deleted_rev);
    }
  else if (new_node_kind == svn_node_file ||
           new_node_kind == svn_node_symlink)
    {
      if (new_rev + 1 == old_rev)
        return apr_psprintf(result_pool,
                            _("A new file appeared during reverse-merge of\n"
                              "'^/%s:%ld'.\nIt was deleted by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            details->deleted_rev_author,
                            details->deleted_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new file appeared during reverse-merge of\n"
                              "'^/%s:%ld-%ld'.\nIt was deleted by %s in r%ld."),
                            old_repos_relpath, new_rev + 1, old_rev,
                            details->deleted_rev_author,
                            details->deleted_rev);
    }
  else
    {
      if (new_rev + 1 == old_rev)
        return apr_psprintf(result_pool,
                            _("A new item appeared during reverse-merge of\n"
                              "'^/%s:%ld'.\nIt was deleted by %s in r%ld."),
                            old_repos_relpath, old_rev,
                            details->deleted_rev_author,
                            details->deleted_rev);
      else
        return apr_psprintf(result_pool,
                            _("A new item appeared during reverse-merge of\n"
                              "'^/%s:%ld-%ld'.\nIt was deleted by %s in r%ld."),
                            old_repos_relpath, new_rev + 1, old_rev,
                            details->deleted_rev_author,
                            details->deleted_rev);
    }
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_description_incoming_add(
  const char **incoming_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const char *action;
  svn_node_kind_t victim_node_kind;
  svn_wc_operation_t conflict_operation;
  const char *old_repos_relpath;
  svn_revnum_t old_rev;
  svn_node_kind_t old_node_kind;
  const char *new_repos_relpath;
  svn_revnum_t new_rev;
  svn_node_kind_t new_node_kind;
  struct conflict_tree_incoming_add_details *details;

  if (conflict->tree_conflict_incoming_details == NULL)
    return svn_error_trace(conflict_tree_get_incoming_description_generic(
                             incoming_change_description, conflict, ctx,
                             result_pool, scratch_pool));

  conflict_operation = svn_client_conflict_get_operation(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, &old_node_kind, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, &new_node_kind, conflict,
            scratch_pool, scratch_pool));

  details = conflict->tree_conflict_incoming_details;

  if (conflict_operation == svn_wc_operation_update)
    {
      action = describe_incoming_add_upon_update(details,
                                                 new_node_kind,
                                                 new_rev,
                                                 result_pool);
    }
  else if (conflict_operation == svn_wc_operation_switch)
    {
      action = describe_incoming_add_upon_switch(details,
                                                 victim_node_kind,
                                                 new_repos_relpath,
                                                 new_rev,
                                                 result_pool);
    }
  else if (conflict_operation == svn_wc_operation_merge)
    {
      if (old_rev < new_rev)
        action = describe_incoming_add_upon_merge(details,
                                                  new_node_kind,
                                                  old_rev,
                                                  new_repos_relpath,
                                                  new_rev,
                                                  result_pool);
      else
        action = describe_incoming_reverse_deletion_upon_merge(
                   details, new_node_kind, old_repos_relpath,
                   old_rev, new_rev, result_pool);
    }

  *incoming_change_description = apr_pstrdup(result_pool, action);

  return SVN_NO_ERROR;
}

/* Details for tree conflicts involving incoming edits.
 * Note that we store an array of these. Each element corresponds to a
 * revision within the old/new range in which a modification occured. */
struct conflict_tree_incoming_edit_details
{
  /* The revision in which the edit ocurred. */
  svn_revnum_t rev;

  /* The author of the revision. */
  const char *author;

  /** Is the text modified? May be svn_tristate_unknown. */
  svn_tristate_t text_modified;

  /** Are properties modified? May be svn_tristate_unknown. */
  svn_tristate_t props_modified;

  /** For directories, are children modified?
   * May be svn_tristate_unknown. */
  svn_tristate_t children_modified;

  /* The path which was edited, relative to the repository root. */
  const char *repos_relpath;
};

/* Baton for find_modified_rev(). */
struct find_modified_rev_baton {
  const char *victim_abspath;
  svn_client_ctx_t *ctx;
  apr_array_header_t *edits;
  const char *repos_relpath;
  svn_node_kind_t node_kind;
  apr_pool_t *result_pool;
  apr_pool_t *scratch_pool;
};

/* Implements svn_log_entry_receiver_t. */
static svn_error_t *
find_modified_rev(void *baton,
                  svn_log_entry_t *log_entry,
                  apr_pool_t *scratch_pool)
{
  struct find_modified_rev_baton *b = baton;
  struct conflict_tree_incoming_edit_details *details = NULL;
  svn_string_t *author;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  if (b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 b->victim_abspath,
                 svn_wc_notify_tree_conflict_details_progress,
                 scratch_pool),
      notify->revision = log_entry->revision;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  /* No paths were changed in this revision.  Nothing to do. */
  if (! log_entry->changed_paths2)
    return SVN_NO_ERROR;

  details = apr_pcalloc(b->result_pool, sizeof(*details));
  details->rev = log_entry->revision;
  author = svn_hash_gets(log_entry->revprops, SVN_PROP_REVISION_AUTHOR);
  if (author)
    details->author = apr_pstrdup(b->result_pool, author->data);
  else
    details->author = _("unknown author");

  details->text_modified = svn_tristate_unknown;
  details->props_modified = svn_tristate_unknown;
  details->children_modified = svn_tristate_unknown;

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths2);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      void *val;
      const char *path;
      svn_log_changed_path2_t *log_item;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, (void *) &path, NULL, &val);
      log_item = val;

      /* ### Remove leading slash from paths in log entries. */
      if (path[0] == '/')
          path = svn_relpath_canonicalize(path, iterpool);

      if (svn_path_compare_paths(b->repos_relpath, path) == 0 &&
          (log_item->action == 'M' || log_item->action == 'A'))
        {
          details->text_modified = log_item->text_modified;
          details->props_modified = log_item->props_modified;
          details->repos_relpath = apr_pstrdup(b->result_pool, path);

          if (log_item->copyfrom_path)
            b->repos_relpath = apr_pstrdup(b->scratch_pool,
                                           log_item->copyfrom_path);
        }
      else if (b->node_kind == svn_node_dir &&
               svn_relpath_skip_ancestor(b->repos_relpath, path) != NULL)
        details->children_modified = svn_tristate_true;
    }

  if (b->node_kind == svn_node_dir &&
      details->children_modified == svn_tristate_unknown)
        details->children_modified = svn_tristate_false;

  APR_ARRAY_PUSH(b->edits, struct conflict_tree_incoming_edit_details *) =
    details;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Implements tree_conflict_get_details_func_t.
 * Find one or more revisions in which the victim was modified in the
 * repository. */
static svn_error_t *
conflict_tree_get_details_incoming_edit(svn_client_conflict_t *conflict,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *scratch_pool)
{
  const char *old_repos_relpath;
  const char *new_repos_relpath;
  const char *repos_root_url;
  svn_revnum_t old_rev;
  svn_revnum_t new_rev;
  svn_node_kind_t old_node_kind;
  svn_node_kind_t new_node_kind;
  svn_wc_operation_t operation;
  const char *url;
  const char *corrected_url;
  svn_ra_session_t *ra_session;
  apr_array_header_t *paths;
  apr_array_header_t *revprops;
  struct find_modified_rev_baton b = { 0 };

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, &old_node_kind, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, &new_node_kind, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict,
                                             scratch_pool, scratch_pool));
  operation = svn_client_conflict_get_operation(conflict);
  if (operation == svn_wc_operation_update)
    {
      b.node_kind = old_rev < new_rev ? new_node_kind : old_node_kind;

      /* If there is no node then we cannot find any edits. */
      if (b.node_kind == svn_node_none)
        return SVN_NO_ERROR;

      url = svn_path_url_add_component2(repos_root_url,
                                        old_rev < new_rev ? new_repos_relpath
                                                          : old_repos_relpath,
                                        scratch_pool);

      b.repos_relpath = old_rev < new_rev ? new_repos_relpath
                                          : old_repos_relpath;
    }
  else if (operation == svn_wc_operation_switch ||
           operation == svn_wc_operation_merge)
    {
      url = svn_path_url_add_component2(repos_root_url, new_repos_relpath,
                                        scratch_pool);

      b.repos_relpath = new_repos_relpath;
      b.node_kind = new_node_kind;
    }

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                               &corrected_url,
                                               url, NULL, NULL,
                                               FALSE,
                                               FALSE,
                                               ctx,
                                               scratch_pool,
                                               scratch_pool));

  paths = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(paths, const char *) = "";

  revprops = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;

  b.ctx = ctx;
  b.victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  b.result_pool = conflict->pool;
  b.scratch_pool = scratch_pool;
  b.edits = apr_array_make(
               conflict->pool, 0,
               sizeof(struct conflict_tree_incoming_edit_details *));

  SVN_ERR(svn_ra_get_log2(ra_session, paths,
                          old_rev < new_rev ? old_rev : new_rev,
                          old_rev < new_rev ? new_rev : old_rev,
                          0, /* no limit */
                          TRUE, /* need the changed paths list */
                          FALSE, /* need to traverse copies */
                          FALSE, /* no need for merged revisions */
                          revprops,
                          find_modified_rev, &b,
                          scratch_pool));

  conflict->tree_conflict_incoming_details = b.edits;

  return SVN_NO_ERROR;
}

static const char *
describe_incoming_edit_upon_update(svn_revnum_t old_rev,
                                   svn_revnum_t new_rev,
                                   svn_node_kind_t old_node_kind,
                                   svn_node_kind_t new_node_kind,
                                   apr_pool_t *result_pool)
{
  if (old_rev < new_rev)
    {
      if (new_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Changes destined for a directory arrived "
                              "via the following revisions during update "
                              "from r%ld to r%ld."), old_rev, new_rev);
      else if (new_node_kind == svn_node_file ||
               new_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("Changes destined for a file arrived "
                              "via the following revisions during update "
                              "from r%ld to r%ld"), old_rev, new_rev);
      else
        return apr_psprintf(result_pool,
                            _("Changes from the following revisions arrived "
                              "during update from r%ld to r%ld"),
                            old_rev, new_rev);
    }
  else
    {
      if (new_node_kind == svn_node_dir)
        return apr_psprintf(result_pool,
                            _("Changes destined for a directory arrived "
                              "via the following revisions during backwards "
                              "update from r%ld to r%ld"),
                            old_rev, new_rev);
      else if (new_node_kind == svn_node_file ||
               new_node_kind == svn_node_symlink)
        return apr_psprintf(result_pool,
                            _("Changes destined for a file arrived "
                              "via the following revisions during backwards "
                              "update from r%ld to r%ld"),
                            old_rev, new_rev);
      else
        return apr_psprintf(result_pool,
                            _("Changes from the following revisions arrived "
                              "during backwards update from r%ld to r%ld"),
                            old_rev, new_rev);
    }
}

static const char *
describe_incoming_edit_upon_switch(const char *new_repos_relpath,
                                   svn_revnum_t new_rev,
                                   svn_node_kind_t new_node_kind,
                                   apr_pool_t *result_pool)
{
  if (new_node_kind == svn_node_dir)
    return apr_psprintf(result_pool,
                        _("Changes destined for a directory arrived via "
                          "the following revisions during switch to\n"
                          "'^/%s@r%ld'"),
                        new_repos_relpath, new_rev);
  else if (new_node_kind == svn_node_file ||
           new_node_kind == svn_node_symlink)
    return apr_psprintf(result_pool,
                        _("Changes destined for a directory arrived via "
                          "the following revisions during switch to\n"
                          "'^/%s@r%ld'"),
                        new_repos_relpath, new_rev);
  else
    return apr_psprintf(result_pool,
                        _("Changes from the following revisions arrived "
                          "during switch to\n'^/%s@r%ld'"),
                        new_repos_relpath, new_rev);
}

/* Return a string showing the list of revisions in EDITS, ensuring
 * the string won't grow too large for display. */
static const char *
describe_incoming_edit_list_modified_revs(apr_array_header_t *edits,
                                          apr_pool_t *result_pool)
{
  int num_revs_to_skip;
  static const int min_revs_for_skipping = 5;
  static const int max_revs_to_display = 8;
  const char *s = "";
  int i;

  if (edits->nelts <= max_revs_to_display)
    num_revs_to_skip = 0;
  else
    {
      /* Check if we should insert a placeholder for some revisions because
       * the string would grow too long for display otherwise. */
      num_revs_to_skip = edits->nelts - max_revs_to_display;
      if (num_revs_to_skip < min_revs_for_skipping)
        {
          /* Don't bother with the placeholder. Just list all revisions. */
          num_revs_to_skip = 0;
        }
    }

  for (i = 0; i < edits->nelts; i++)
    {
      struct conflict_tree_incoming_edit_details *details;

      details = APR_ARRAY_IDX(edits, i,
                              struct conflict_tree_incoming_edit_details *);
      if (num_revs_to_skip > 0)
        {
          /* Insert a placeholder for revisions falling into the middle of
           * the range so we'll get something that looks like:
           * 1, 2, 3, 4, 5 [ placeholder ] 95, 96, 97, 98, 99 */
          if (i < max_revs_to_display / 2)
            s = apr_psprintf(result_pool, _("%s r%ld by %s%s"), s,
                             details->rev, details->author,
                             i < edits->nelts - 1 ? "," : "");
          else if (i >= max_revs_to_display / 2 &&
                   i < edits->nelts - (max_revs_to_display / 2))
              continue;
          else
            {
              if (i == edits->nelts - (max_revs_to_display / 2))
                  s = apr_psprintf(result_pool,
                                   _("%s\n [%d revisions omitted for "
                                     "brevity],\n"),
                                   s, num_revs_to_skip);

              s = apr_psprintf(result_pool, _("%s r%ld by %s%s"), s,
                               details->rev, details->author,
                               i < edits->nelts - 1 ? "," : "");
            }
        } 
      else
        s = apr_psprintf(result_pool, _("%s r%ld by %s%s"), s,
                         details->rev, details->author,
                         i < edits->nelts - 1 ? "," : "");
    }

  return s;
}

/* Implements tree_conflict_get_description_func_t. */
static svn_error_t *
conflict_tree_get_description_incoming_edit(
  const char **incoming_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const char *action;
  svn_wc_operation_t conflict_operation;
  const char *old_repos_relpath;
  svn_revnum_t old_rev;
  svn_node_kind_t old_node_kind;
  const char *new_repos_relpath;
  svn_revnum_t new_rev;
  svn_node_kind_t new_node_kind;
  apr_array_header_t *edits;

  if (conflict->tree_conflict_incoming_details == NULL)
    return svn_error_trace(conflict_tree_get_incoming_description_generic(
                             incoming_change_description, conflict, ctx,
                             result_pool, scratch_pool));

  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &old_repos_relpath, &old_rev, &old_node_kind, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &new_repos_relpath, &new_rev, &new_node_kind, conflict,
            scratch_pool, scratch_pool));

  conflict_operation = svn_client_conflict_get_operation(conflict);

  edits = conflict->tree_conflict_incoming_details;

  if (conflict_operation == svn_wc_operation_update)
    action = describe_incoming_edit_upon_update(old_rev, new_rev,
                                                old_node_kind, new_node_kind,
                                                scratch_pool);
  else if (conflict_operation == svn_wc_operation_switch)
    action = describe_incoming_edit_upon_switch(new_repos_relpath, new_rev,
                                                new_node_kind, scratch_pool);
  else if (conflict_operation == svn_wc_operation_merge)
    {
      /* Handle merge inline because it returns early sometimes. */
      if (old_rev < new_rev)
        {
          if (old_rev + 1 == new_rev)
            {
              if (new_node_kind == svn_node_dir)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a directory "
                                        "arrived during merge of\n"
                                        "'^/%s:%ld'."),
                                        new_repos_relpath, new_rev);
              else if (new_node_kind == svn_node_file ||
                       new_node_kind == svn_node_symlink)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a file "
                                        "arrived during merge of\n"
                                        "'^/%s:%ld'."),
                                      new_repos_relpath, new_rev);
              else
                action = apr_psprintf(scratch_pool,
                                      _("Changes arrived during merge of\n"
                                        "'^/%s:%ld'."),
                                      new_repos_relpath, new_rev);

              *incoming_change_description = apr_pstrdup(result_pool, action);

              return SVN_NO_ERROR;
            }
          else
            {
              if (new_node_kind == svn_node_dir)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a directory "
                                        "arrived via the following revisions "
                                        "during merge of\n'^/%s:%ld-%ld'"),
                                      new_repos_relpath, old_rev + 1, new_rev);
              else if (new_node_kind == svn_node_file ||
                       new_node_kind == svn_node_symlink)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a file "
                                        "arrived via the following revisions "
                                        "during merge of\n'^/%s:%ld-%ld'"),
                                      new_repos_relpath, old_rev + 1, new_rev);
              else
                action = apr_psprintf(scratch_pool,
                                      _("Changes from the following revisions "
                                        "arrived during merge of\n"
                                        "'^/%s:%ld-%ld'"),
                                      new_repos_relpath, old_rev + 1, new_rev);
            }
        }
      else
        {
          if (new_rev + 1 == old_rev)
            {
              if (new_node_kind == svn_node_dir)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a directory "
                                        "arrived during reverse-merge of\n"
                                        "'^/%s:%ld'."),
                                      new_repos_relpath, old_rev);
              else if (new_node_kind == svn_node_file ||
                       new_node_kind == svn_node_symlink)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a file "
                                        "arrived during reverse-merge of\n"
                                        "'^/%s:%ld'."),
                                      new_repos_relpath, old_rev);
              else
                action = apr_psprintf(scratch_pool,
                                      _("Changes arrived during reverse-merge "
                                        "of\n'^/%s:%ld'."),
                                      new_repos_relpath, old_rev);

              *incoming_change_description = apr_pstrdup(result_pool, action);

              return SVN_NO_ERROR;
            }
          else
            {
              if (new_node_kind == svn_node_dir)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a directory "
                                        "arrived via the following revisions "
                                        "during reverse-merge of\n"
                                        "'^/%s:%ld-%ld'"),
                                      new_repos_relpath, new_rev + 1, old_rev);
              else if (new_node_kind == svn_node_file ||
                       new_node_kind == svn_node_symlink)
                action = apr_psprintf(scratch_pool,
                                      _("Changes destined for a file "
                                        "arrived via the following revisions "
                                        "during reverse-merge of\n"
                                        "'^/%s:%ld-%ld'"),
                                      new_repos_relpath, new_rev + 1, old_rev);
                
              else
                action = apr_psprintf(scratch_pool,
                                      _("Changes from the following revisions "
                                        "arrived during reverse-merge of\n"
                                        "'^/%s:%ld-%ld'"),
                                      new_repos_relpath, new_rev + 1, old_rev);
            }
        }
    }

  action = apr_psprintf(scratch_pool, "%s:\n%s", action,
                        describe_incoming_edit_list_modified_revs(
                          edits, scratch_pool));
  *incoming_change_description = apr_pstrdup(result_pool, action);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_tree_get_description(
  const char **incoming_change_description,
  const char **local_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  SVN_ERR(conflict->tree_conflict_get_incoming_description_func(
            incoming_change_description,
            conflict, ctx, result_pool, scratch_pool));

  SVN_ERR(conflict->tree_conflict_get_local_description_func(
            local_change_description,
            conflict, ctx, result_pool, scratch_pool));
  
  return SVN_NO_ERROR;
}

void
svn_client_conflict_option_set_merged_propval(
  svn_client_conflict_option_t *option,
  const svn_string_t *merged_propval)
{
  option->type_data.prop.merged_propval = svn_string_dup(merged_propval,
                                                         option->pool);
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_postpone(svn_client_conflict_option_t *option,
                 svn_client_conflict_t *conflict,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR; /* Nothing to do. */
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_text_conflict(svn_client_conflict_option_t *option,
                      svn_client_conflict_t *conflict,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *lock_abspath;
  svn_wc_conflict_choice_t conflict_choice;
  svn_error_t *err;

  option_id = svn_client_conflict_option_get_id(option);
  conflict_choice = conflict_option_id_to_wc_conflict_choice(option_id);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__conflict_text_mark_resolved(ctx->wc_ctx,
                                            local_abspath,
                                            conflict_choice,
                                            ctx->cancel_func,
                                            ctx->cancel_baton,
                                            ctx->notify_func2,
                                            ctx->notify_baton2,
                                            scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  conflict->resolution_text = option_id;

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_prop_conflict(svn_client_conflict_option_t *option,
                      svn_client_conflict_t *conflict,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  svn_wc_conflict_choice_t conflict_choice;
  const char *local_abspath;
  const char *lock_abspath;
  const char *propname = option->type_data.prop.propname;
  svn_error_t *err;
  const svn_string_t *merged_value;

  option_id = svn_client_conflict_option_get_id(option);
  conflict_choice = conflict_option_id_to_wc_conflict_choice(option_id);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  if (option_id == svn_client_conflict_option_merged_text)
    merged_value = option->type_data.prop.merged_propval;
  else
    merged_value = NULL;

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__conflict_prop_mark_resolved(ctx->wc_ctx, local_abspath,
                                            propname, conflict_choice,
                                            merged_value,
                                            ctx->notify_func2,
                                            ctx->notify_baton2,
                                            scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  if (propname[0] == '\0')
    {
      apr_hash_index_t *hi;

      /* All properties have been resolved to the same option. */
      for (hi = apr_hash_first(scratch_pool, conflict->prop_conflicts);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *this_propname = apr_hash_this_key(hi);

          svn_hash_sets(conflict->resolved_props,
                        apr_pstrdup(apr_hash_pool_get(conflict->resolved_props),
                                    this_propname),
                        option);
          svn_hash_sets(conflict->prop_conflicts, this_propname, NULL);
        }

      conflict->legacy_prop_conflict_propname = NULL;
    }
  else
    {
      svn_hash_sets(conflict->resolved_props,
                    apr_pstrdup(apr_hash_pool_get(conflict->resolved_props),
                                propname),
                   option);
      svn_hash_sets(conflict->prop_conflicts, propname, NULL);

      if (apr_hash_count(conflict->prop_conflicts) > 0)
        conflict->legacy_prop_conflict_propname =
            apr_hash_this_key(apr_hash_first(scratch_pool,
                                             conflict->prop_conflicts));
      else
        conflict->legacy_prop_conflict_propname = NULL;
    }

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_accept_current_wc_state(svn_client_conflict_option_t *option,
                                svn_client_conflict_t *conflict,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  if (option_id != svn_client_conflict_option_accept_current_wc_state)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Tree conflict on '%s' can only be resolved "
                               "to the current working copy state"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  /* Resolve to current working copy state. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);

  /* svn_wc__del_tree_conflict doesn't handle notification for us */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved_tree,
                                           scratch_pool),
                      scratch_pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = option_id;

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_update_break_moved_away(svn_client_conflict_option_t *option,
                                svn_client_conflict_t *conflict,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__conflict_tree_update_break_moved_away(ctx->wc_ctx,
                                                      local_abspath,
                                                      ctx->cancel_func,
                                                      ctx->cancel_baton,
                                                      ctx->notify_func2,
                                                      ctx->notify_baton2,
                                                      scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_update_raise_moved_away(svn_client_conflict_option_t *option,
                                svn_client_conflict_t *conflict,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__conflict_tree_update_raise_moved_away(ctx->wc_ctx,
                                                      local_abspath,
                                                      ctx->cancel_func,
                                                      ctx->cancel_baton,
                                                      ctx->notify_func2,
                                                      ctx->notify_baton2,
                                                      scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_update_moved_away_node(svn_client_conflict_option_t *option,
                               svn_client_conflict_t *conflict,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__conflict_tree_update_moved_away_node(ctx->wc_ctx,
                                                     local_abspath,
                                                     ctx->cancel_func,
                                                     ctx->cancel_baton,
                                                     ctx->notify_func2,
                                                     ctx->notify_baton2,
                                                     scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Verify the local working copy state matches what we expect when an
 * incoming add vs add tree conflict exists after an update operation.
 * We assume the update operation leaves the working copy in a state which
 * prefers the local change and cancels the incoming addition.
 * Run a quick sanity check and error out if it looks as if the
 * working copy was modified since, even though it's not easy to make
 * such modifications without also clearing the conflict marker. */
static svn_error_t *
verify_local_state_for_incoming_add_upon_update(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_t *option,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_client_conflict_option_id_t option_id;
  const char *wcroot_abspath;
  svn_wc_operation_t operation;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;
  const char *base_repos_relpath;
  svn_revnum_t base_rev;
  svn_node_kind_t base_kind;
  const char *local_style_relpath;
  svn_boolean_t is_added;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  option_id = svn_client_conflict_option_get_id(option);
  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                             local_abspath, scratch_pool,
                             scratch_pool));
  operation = svn_client_conflict_get_operation(conflict);
  SVN_ERR_ASSERT(operation == svn_wc_operation_update);

  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  local_style_relpath = svn_dirent_local_style(
                          svn_dirent_skip_ancestor(wcroot_abspath,
                                                   local_abspath),
                          scratch_pool);

  /* Check if a local addition addition replaces the incoming new node. */
  err = svn_wc__node_get_base(&base_kind, &base_rev, &base_repos_relpath,
                              NULL, NULL, NULL, ctx->wc_ctx, local_abspath,
                              FALSE, scratch_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      if (option_id == svn_client_conflict_option_incoming_add_ignore)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected a base node but found none)"),
                                 local_style_relpath);
      else if (option_id ==
               svn_client_conflict_option_incoming_added_dir_replace)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected a base node but found none)"),
                                 local_style_relpath);
      else
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                                 _("Unexpected option id '%d'"), option_id);
    }
  else if (err)
    return svn_error_trace(err);

  if (base_kind != incoming_new_kind)
    {
      if (option_id == svn_client_conflict_option_incoming_add_ignore)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected base node kind '%s', "
                                   "but found '%s')"),
                                 local_style_relpath,
                                 svn_node_kind_to_word(incoming_new_kind),
                                 svn_node_kind_to_word(base_kind));
      else if (option_id ==
               svn_client_conflict_option_incoming_added_dir_replace)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected base node kind '%s', "
                                   "but found '%s')"),
                                  local_style_relpath,
                                 svn_node_kind_to_word(incoming_new_kind),
                                 svn_node_kind_to_word(base_kind));
      else
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Unexpected option id '%d'"), option_id);
    }

  if (strcmp(base_repos_relpath, incoming_new_repos_relpath) != 0 ||
      base_rev != incoming_new_pegrev)
    {
      if (option_id == svn_client_conflict_option_incoming_add_ignore)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected base node from '^/%s@%ld', "
                                   "but found '^/%s@%ld')"),
                                 local_style_relpath,
                                 incoming_new_repos_relpath,
                                 incoming_new_pegrev,
                                 base_repos_relpath, base_rev);
      else if (option_id ==
               svn_client_conflict_option_incoming_added_dir_replace)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected base node from '^/%s@%ld', "
                                   "but found '^/%s@%ld')"),
                                 local_style_relpath,
                                 incoming_new_repos_relpath,
                                 incoming_new_pegrev,
                                 base_repos_relpath, base_rev);
      else
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Unexpected option id '%d'"), option_id);
    }

  SVN_ERR(svn_wc__node_is_added(&is_added, ctx->wc_ctx, local_abspath,
                                scratch_pool));
  if (!is_added)
    {
      if (option_id == svn_client_conflict_option_incoming_add_ignore)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected an added item, but the item "
                                   "is not added)"),
                                 local_style_relpath);

      else if (option_id ==
               svn_client_conflict_option_incoming_added_dir_replace)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected an added item, but the item "
                                   "is not added)"),
                                 local_style_relpath);
      else
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Unexpected option id '%d'"), option_id);
    }

  return SVN_NO_ERROR;
}


/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_incoming_add_ignore(svn_client_conflict_option_t *option,
                            svn_client_conflict_t *conflict,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *lock_abspath;
  svn_wc_operation_t operation;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  operation = svn_client_conflict_get_operation(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  if (operation == svn_wc_operation_update)
    {
      err = verify_local_state_for_incoming_add_upon_update(conflict, option,
                                                            ctx, scratch_pool);
      if (err)
        goto unlock_wc;
    }

  /* All other options for this conflict actively fetch the incoming
   * new node. We can ignore the incoming new node by doing nothing. */

  /* Resolve to current working copy state. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);

  /* svn_wc__del_tree_conflict doesn't handle notification for us */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved_tree,
                                           scratch_pool),
                      scratch_pool);

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Delete entry and wc props from a set of properties. */
static void
filter_props(apr_hash_t *props, apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, props);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);

      if (!svn_wc_is_normal_prop(propname))
        svn_hash_sets(props, propname, NULL);
    }
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_file_text_update(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  const char *wc_tmpdir;
  const char *local_abspath;
  const char *lock_abspath;
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  const char *empty_file_abspath;
  const char *working_file_tmp_abspath;
  svn_stream_t *working_file_stream;
  svn_stream_t *working_file_tmp_stream;
  apr_hash_t *working_props;
  apr_array_header_t *propdiffs;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Set up tempory storage for the working version of file. */
  SVN_ERR(svn_wc__get_tmpdir(&wc_tmpdir, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(&working_file_tmp_stream,
                                 &working_file_tmp_abspath, wc_tmpdir,
                                 /* Don't delete automatically! */
                                 svn_io_file_del_none,
                                 scratch_pool, scratch_pool));

  /* Copy the detranslated working file to temporary storage. */
  SVN_ERR(svn_wc__translated_stream(&working_file_stream, ctx->wc_ctx,
                                    local_abspath, local_abspath,
                                    SVN_WC_TRANSLATE_TO_NF,
                                    scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(working_file_stream, working_file_tmp_stream,
                           ctx->cancel_func, ctx->cancel_baton,
                           scratch_pool));

  /* Get a copy of the working file's properties. */
  SVN_ERR(svn_wc_prop_list2(&working_props, ctx->wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));
  filter_props(working_props, scratch_pool);

  /* Create an empty file as fake "merge-base" for the two added files.
   * The files are not ancestrally related so this is the best we can do. */
  SVN_ERR(svn_io_open_unique_file3(NULL, &empty_file_abspath, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  /* Create a property diff which shows all props as added. */
  SVN_ERR(svn_prop_diffs(&propdiffs, working_props,
                         apr_hash_make(scratch_pool), scratch_pool));

  /* ### The following WC modifications should be atomic. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  /* Revert the path in order to restore the repository's line of
   * history, which is part of the BASE tree. This revert operation
   * is why are being careful about not losing the temporary copy. */
  err = svn_wc_revert5(ctx->wc_ctx, local_abspath, svn_depth_empty,
                       FALSE, NULL, TRUE, FALSE,
                       NULL, NULL, /* no cancellation */
                       ctx->notify_func2, ctx->notify_baton2,
                       scratch_pool);
  if (err)
    goto unlock_wc;

  /* Perform the file merge. ### Merge into tempfile and then rename on top? */
  err = svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                      ctx->wc_ctx, empty_file_abspath,
                      working_file_tmp_abspath, local_abspath,
                      NULL, NULL, NULL, /* labels */
                      NULL, NULL, /* conflict versions */
                      FALSE, /* dry run */
                      NULL, NULL, /* diff3_cmd, merge_options */
                      NULL, propdiffs,
                      NULL, NULL, /* conflict func/baton */
                      NULL, NULL, /* don't allow user to cancel here */
                      scratch_pool);

unlock_wc:
  if (err)
      err = svn_error_quick_wrapf(
              err, _("If needed, a backup copy of '%s' can be found at '%s'"),
              svn_dirent_local_style(local_abspath, scratch_pool),
              svn_dirent_local_style(working_file_tmp_abspath, scratch_pool));
  err = svn_error_compose_create(err,
                                 svn_wc__release_write_lock(ctx->wc_ctx,
                                                            lock_abspath,
                                                            scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);
  
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      /* Tell the world about the file merge that just happened. */
      notify = svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);

      /* And also about the successfully resolved tree conflict. */
      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_resolved_tree,
                                    scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  /* All is good -- remove temporary copy of the working file. */
  SVN_ERR(svn_io_remove_file2(working_file_tmp_abspath, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_file_text_merge(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  const char *repos_root_url;
  const char *wc_tmpdir;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *local_abspath;
  const char *lock_abspath;
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  apr_file_t *incoming_new_file;
  const char *incoming_new_tmp_abspath;
  const char *empty_file_abspath;
  svn_stream_t *incoming_new_stream;
  apr_hash_t *incoming_new_props;
  apr_array_header_t *propdiffs;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Set up temporary storage for the repository version of file. */
  SVN_ERR(svn_wc__get_tmpdir(&wc_tmpdir, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));
  SVN_ERR(svn_io_open_unique_file3(&incoming_new_file,
                                   &incoming_new_tmp_abspath, wc_tmpdir,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));
  incoming_new_stream = svn_stream_from_aprfile2(incoming_new_file, TRUE,
                                                 scratch_pool);

  /* Fetch the incoming added file from the repository. */
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  url = svn_path_url_add_component2(repos_root_url, incoming_new_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL, FALSE, FALSE,
                                               ctx, scratch_pool,
                                               scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, "", incoming_new_pegrev,
                          incoming_new_stream, NULL, /* fetched_rev */
                          &incoming_new_props, scratch_pool));

  /* Flush file to disk. */
  SVN_ERR(svn_stream_close(incoming_new_stream));
  SVN_ERR(svn_io_file_flush(incoming_new_file, scratch_pool));

  filter_props(incoming_new_props, scratch_pool);

  /* Create an empty file as fake "merge-base" for the two added files.
   * The files are not ancestrally related so this is the best we can do. */
  SVN_ERR(svn_io_open_unique_file3(NULL, &empty_file_abspath, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  /* Create a property diff which shows all props as added. */
  SVN_ERR(svn_prop_diffs(&propdiffs, incoming_new_props,
                         apr_hash_make(scratch_pool), scratch_pool));

  /* ### The following WC modifications should be atomic. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  /* Resolve to current working copy state. svn_wc_merge5() requires this. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);
  if (err)
    return svn_error_compose_create(err,
                                    svn_wc__release_write_lock(ctx->wc_ctx,
                                                               lock_abspath,
                                                               scratch_pool));
  /* Perform the file merge. ### Merge into tempfile and then rename on top? */
  err = svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                      ctx->wc_ctx, empty_file_abspath,
                      incoming_new_tmp_abspath, local_abspath,
                      NULL, NULL, NULL, /* labels */
                      NULL, NULL, /* conflict versions */
                      FALSE, /* dry run */
                      NULL, NULL, /* diff3_cmd, merge_options */
                      NULL, propdiffs,
                      NULL, NULL, /* conflict func/baton */
                      NULL, NULL, /* don't allow user to cancel here */
                      scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      /* Tell the world about the file merge that just happened. */
      notify = svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);

      /* And also about the successfully resolved tree conflict. */
      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_resolved_tree,
                                    scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_file_replace_and_merge(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  const char *repos_root_url;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  apr_file_t *incoming_new_file;
  svn_stream_t *incoming_new_stream;
  apr_hash_t *incoming_new_props;
  const char *local_abspath;
  const char *lock_abspath;
  const char *wc_tmpdir;
  svn_stream_t *working_file_tmp_stream;
  const char *working_file_tmp_abspath;
  svn_stream_t *working_file_stream;
  apr_hash_t *working_props;
  svn_error_t *err;
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  apr_file_t *empty_file;
  const char *empty_file_abspath;
  apr_array_header_t *propdiffs;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Set up tempory storage for the working version of file. */
  SVN_ERR(svn_wc__get_tmpdir(&wc_tmpdir, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(&working_file_tmp_stream,
                                 &working_file_tmp_abspath, wc_tmpdir,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));

  /* Copy the detranslated working file to temporary storage. */
  SVN_ERR(svn_wc__translated_stream(&working_file_stream, ctx->wc_ctx,
                                    local_abspath, local_abspath,
                                    SVN_WC_TRANSLATE_TO_NF,
                                    scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(working_file_stream, working_file_tmp_stream,
                           ctx->cancel_func, ctx->cancel_baton,
                           scratch_pool));

  /* Get a copy of the working file's properties. */
  SVN_ERR(svn_wc_prop_list2(&working_props, ctx->wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));

  /* Fetch the incoming added file from the repository. */
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  url = svn_path_url_add_component2(repos_root_url, incoming_new_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL, FALSE, FALSE,
                                               ctx, scratch_pool,
                                               scratch_pool));
  if (corrected_url)
    url = corrected_url;
  SVN_ERR(svn_io_open_unique_file3(&incoming_new_file, NULL, wc_tmpdir,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));
  incoming_new_stream = svn_stream_from_aprfile2(incoming_new_file, TRUE,
                                                 scratch_pool);
  SVN_ERR(svn_ra_get_file(ra_session, "", incoming_new_pegrev,
                          incoming_new_stream, NULL, /* fetched_rev */
                          &incoming_new_props, scratch_pool));
  /* Flush file to disk. */
  SVN_ERR(svn_io_file_flush(incoming_new_file, scratch_pool));

  /* Reset the stream in preparation for adding its content to WC. */
  SVN_ERR(svn_stream_reset(incoming_new_stream));

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  /* ### The following WC modifications should be atomic. */

  /* Replace the working file with the file from the repository. */
  err = svn_wc_delete4(ctx->wc_ctx, local_abspath, FALSE, FALSE,
                       NULL, NULL, /* don't allow user to cancel here */
                       ctx->notify_func2, ctx->notify_baton2,
                       scratch_pool);
  if (err)
    goto unlock_wc;
  err = svn_wc_add_repos_file4(ctx->wc_ctx, local_abspath,
                               incoming_new_stream,
                               NULL, /* ### could we merge first, then set
                                        ### the merged content here? */
                               incoming_new_props,
                               NULL, /* ### merge props first, set here? */
                               url, incoming_new_pegrev,
                               NULL, NULL, /* don't allow user to cancel here */
                               scratch_pool);
  if (err)
    goto unlock_wc;

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_add,
                                                     scratch_pool);
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  /* Resolve to current working copy state. svn_wc_merge5() requires this. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);
  if (err)
    goto unlock_wc;

  /* Create an empty file as fake "merge-base" for the two added files.
   * The files are not ancestrally related so this is the best we can do. */
  err = svn_io_open_unique_file3(&empty_file, &empty_file_abspath, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;

  filter_props(incoming_new_props, scratch_pool);

  /* Create a property diff for the files. */
  err = svn_prop_diffs(&propdiffs, incoming_new_props,
                       working_props, scratch_pool);
  if (err)
    goto unlock_wc;

  /* Perform the file merge. */
  err = svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                      ctx->wc_ctx, empty_file_abspath,
                      working_file_tmp_abspath, local_abspath,
                      NULL, NULL, NULL, /* labels */
                      NULL, NULL, /* conflict versions */
                      FALSE, /* dry run */
                      NULL, NULL, /* diff3_cmd, merge_options */
                      NULL, propdiffs,
                      NULL, NULL, /* conflict func/baton */
                      NULL, NULL, /* don't allow user to cancel here */
                      scratch_pool);
  if (err)
    goto unlock_wc;

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                   local_abspath,
                                   svn_wc_notify_update_update,
                                   scratch_pool);

      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  SVN_ERR(svn_stream_close(incoming_new_stream));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                  local_abspath,
                                  svn_wc_notify_resolved_tree,
                                  scratch_pool);

      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

static svn_error_t *
raise_tree_conflict(const char *local_abspath,
                    svn_wc_conflict_action_t incoming_change,
                    svn_wc_conflict_reason_t local_change,
                    svn_node_kind_t local_node_kind,
                    svn_node_kind_t merge_left_kind,
                    svn_node_kind_t merge_right_kind,
                    const char *repos_root_url,
                    const char *repos_uuid,
                    const char *repos_relpath,
                    svn_revnum_t merge_left_rev,
                    svn_revnum_t merge_right_rev,
                    svn_wc_context_t *wc_ctx,
                    svn_wc_notify_func2_t notify_func2,
                    void *notify_baton2,
                    apr_pool_t *scratch_pool)
{
  svn_wc_conflict_description2_t *conflict;
  const svn_wc_conflict_version_t *left_version;
  const svn_wc_conflict_version_t *right_version;

  left_version = svn_wc_conflict_version_create2(repos_root_url,
                                                 repos_uuid,
                                                 repos_relpath,
                                                 merge_left_rev,
                                                 merge_left_kind,
                                                 scratch_pool);
  right_version = svn_wc_conflict_version_create2(repos_root_url,
                                                  repos_uuid,
                                                  repos_relpath,
                                                  merge_right_rev,
                                                  merge_right_kind,
                                                  scratch_pool);
  conflict = svn_wc_conflict_description_create_tree2(local_abspath,
                                                      local_node_kind,
                                                      svn_wc_operation_merge,
                                                      left_version,
                                                      right_version,
                                                      scratch_pool);
  conflict->action = incoming_change;
  conflict->reason = local_change;

  SVN_ERR(svn_wc__add_tree_conflict(wc_ctx, conflict, scratch_pool));

  if (notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_tree_conflict,
                                    scratch_pool);
      notify->kind = local_node_kind;
      notify_func2(notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

struct merge_newly_added_dir_baton {
  const char *target_abspath;
  svn_client_ctx_t *ctx;
  const char *repos_root_url;
  const char *repos_uuid;
  const char *added_repos_relpath;
  svn_revnum_t merge_left_rev;
  svn_revnum_t merge_right_rev;
};

static svn_error_t *
merge_added_dir_props(const char *target_abspath,
                      const char *added_repos_relpath,
                      apr_hash_t *added_props,
                      const char *repos_root_url,
                      const char *repos_uuid,
                      svn_revnum_t merge_left_rev,
                      svn_revnum_t merge_right_rev,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *scratch_pool)
{
  svn_wc_notify_state_t property_state;
  apr_array_header_t *propchanges;
  const svn_wc_conflict_version_t *left_version;
  const svn_wc_conflict_version_t *right_version;
  apr_hash_index_t *hi;

  left_version = svn_wc_conflict_version_create2(
                   repos_root_url, repos_uuid, added_repos_relpath,
                   merge_left_rev, svn_node_none, scratch_pool);

  right_version = svn_wc_conflict_version_create2(
                    repos_root_url, repos_uuid, added_repos_relpath,
                    merge_right_rev, svn_node_dir, scratch_pool);

  propchanges = apr_array_make(scratch_pool, apr_hash_count(added_props),
                               sizeof(svn_prop_t));
  for (hi = apr_hash_first(scratch_pool, added_props);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_prop_t prop;

      prop.name = apr_hash_this_key(hi);
      prop.value = apr_hash_this_val(hi);

      if (svn_wc_is_normal_prop(prop.name))
        APR_ARRAY_PUSH(propchanges, svn_prop_t) = prop;
    }

  SVN_ERR(svn_wc_merge_props3(&property_state, ctx->wc_ctx,
                              target_abspath,
                              left_version, right_version,
                              apr_hash_make(scratch_pool),
                              propchanges,
                              FALSE, /* not a dry-run */
                              NULL, NULL, NULL, NULL,
                              scratch_pool));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(target_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      notify->kind = svn_node_dir;
      notify->content_state = svn_wc_notify_state_unchanged;;
      notify->prop_state = property_state;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               apr_hash_t *copyfrom_props,
               apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;
  const char *local_abspath;
  const char *copyfrom_url;
  svn_node_kind_t db_kind;
  svn_node_kind_t on_disk_kind;
  apr_hash_index_t *hi;

  /* Handle the root of the added directory tree. */
  if (relpath[0] == '\0')
    {
      /* ### svn_wc_merge_props3() requires this... */
      SVN_ERR(svn_wc__del_tree_conflict(b->ctx->wc_ctx, b->target_abspath,
                                        scratch_pool));
      SVN_ERR(merge_added_dir_props(b->target_abspath,
                                    b->added_repos_relpath, right_props,
                                    b->repos_root_url, b->repos_uuid,
                                    b->merge_left_rev, b->merge_right_rev,
                                    b->ctx, scratch_pool));
      return SVN_NO_ERROR;

    }

  local_abspath = svn_dirent_join(b->target_abspath, relpath, scratch_pool);

  SVN_ERR(svn_wc_read_kind2(&db_kind, b->ctx->wc_ctx, local_abspath,
                            FALSE, FALSE, scratch_pool));
  SVN_ERR(svn_io_check_path(local_abspath, &on_disk_kind, scratch_pool));

  if (db_kind == svn_node_dir && on_disk_kind == svn_node_dir)
    {
      SVN_ERR(merge_added_dir_props(svn_dirent_join(b->target_abspath, relpath,
                                                    scratch_pool),
                                    b->added_repos_relpath, right_props,
                                    b->repos_root_url, b->repos_uuid,
                                    b->merge_left_rev, b->merge_right_rev,
                                    b->ctx, scratch_pool));
      return SVN_NO_ERROR;
    }

  if (db_kind != svn_node_none && db_kind != svn_node_unknown)
    {
      SVN_ERR(raise_tree_conflict(
                local_abspath, svn_wc_conflict_action_add,
                svn_wc_conflict_reason_obstructed,
                db_kind, svn_node_none, svn_node_dir,
                b->repos_root_url, b->repos_uuid,
                svn_relpath_join(b->added_repos_relpath, relpath, scratch_pool),
                b->merge_left_rev, b->merge_right_rev,
                b->ctx->wc_ctx, b->ctx->notify_func2, b->ctx->notify_baton2,
                scratch_pool));
      return SVN_NO_ERROR;
    }

  if (on_disk_kind != svn_node_none)
    {
      SVN_ERR(raise_tree_conflict(
                local_abspath, svn_wc_conflict_action_add,
                svn_wc_conflict_reason_obstructed, db_kind,
                svn_node_none, svn_node_dir, b->repos_root_url, b->repos_uuid,
                svn_relpath_join(b->added_repos_relpath, relpath, scratch_pool),
                b->merge_left_rev, b->merge_right_rev,
                b->ctx->wc_ctx, b->ctx->notify_func2, b->ctx->notify_baton2,
                scratch_pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));
  copyfrom_url = apr_pstrcat(scratch_pool, b->repos_root_url, "/",
                             right_source->repos_relpath, SVN_VA_NULL);
  SVN_ERR(svn_wc_add4(b->ctx->wc_ctx, local_abspath, svn_depth_infinity,
                      copyfrom_url, right_source->revision,
                      NULL, NULL, /* cancel func/baton */
                      b->ctx->notify_func2, b->ctx->notify_baton2,
                      scratch_pool));

  for (hi = apr_hash_first(scratch_pool, right_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);
      const svn_string_t *propval = apr_hash_this_val(hi);

      SVN_ERR(svn_wc_prop_set4(b->ctx->wc_ctx, local_abspath,
                               propname, propval, svn_depth_empty,
                               FALSE, NULL /* do not skip checks */,
                               NULL, NULL, /* cancel func/baton */
                               b->ctx->notify_func2, b->ctx->notify_baton2,
                               scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
merge_added_files(const char *local_abspath,
                  const char *incoming_added_file_abspath,
                  apr_hash_t *incoming_added_file_props,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  apr_file_t *empty_file;
  const char *empty_file_abspath;
  apr_array_header_t *propdiffs;
  apr_hash_t *working_props;

  /* Create an empty file as fake "merge-base" for the two added files.
   * The files are not ancestrally related so this is the best we can do. */
  SVN_ERR(svn_io_open_unique_file3(&empty_file, &empty_file_abspath, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  /* Get a copy of the working file's properties. */
  SVN_ERR(svn_wc_prop_list2(&working_props, ctx->wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));

  /* Create a property diff for the files. */
  SVN_ERR(svn_prop_diffs(&propdiffs, incoming_added_file_props,
                         working_props, scratch_pool));

  /* Perform the file merge. */
  SVN_ERR(svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                        ctx->wc_ctx, empty_file_abspath,
                        incoming_added_file_abspath, local_abspath,
                        NULL, NULL, NULL, /* labels */
                        NULL, NULL, /* conflict versions */
                        FALSE, /* dry run */
                        NULL, NULL, /* diff3_cmd, merge_options */
                        NULL, propdiffs,
                        NULL, NULL, /* conflict func/baton */
                        NULL, NULL, /* don't allow user to cancel here */
                        scratch_pool));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                   local_abspath,
                                   svn_wc_notify_update_update,
                                   scratch_pool);

      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                apr_hash_t *copyfrom_props,
                apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct merge_newly_added_dir_baton *b = processor->baton;
  const char *local_abspath;
  svn_node_kind_t db_kind;
  svn_node_kind_t on_disk_kind;
  apr_array_header_t *propsarray;
  apr_array_header_t *regular_props;

  local_abspath = svn_dirent_join(b->target_abspath, relpath, scratch_pool);

  SVN_ERR(svn_wc_read_kind2(&db_kind, b->ctx->wc_ctx, local_abspath,
                            FALSE, FALSE, scratch_pool));
  SVN_ERR(svn_io_check_path(local_abspath, &on_disk_kind, scratch_pool));

  if (db_kind == svn_node_file && on_disk_kind == svn_node_file)
    {
      propsarray = svn_prop_hash_to_array(right_props, scratch_pool);
      SVN_ERR(svn_categorize_props(propsarray, NULL, NULL, &regular_props,
                                   scratch_pool));
      SVN_ERR(merge_added_files(local_abspath, right_file,
                                svn_prop_array_to_hash(regular_props,
                                                       scratch_pool),
                                b->ctx, scratch_pool));
      return SVN_NO_ERROR;
    }

  if (db_kind != svn_node_none && db_kind != svn_node_unknown)
    {
      SVN_ERR(raise_tree_conflict(
                local_abspath, svn_wc_conflict_action_add,
                svn_wc_conflict_reason_obstructed,
                db_kind, svn_node_none, svn_node_file,
                b->repos_root_url, b->repos_uuid,
                svn_relpath_join(b->added_repos_relpath, relpath, scratch_pool),
                b->merge_left_rev, b->merge_right_rev,
                b->ctx->wc_ctx, b->ctx->notify_func2, b->ctx->notify_baton2,
                scratch_pool));
      return SVN_NO_ERROR;
    }

  if (on_disk_kind != svn_node_none)
    {
      SVN_ERR(raise_tree_conflict(
                local_abspath, svn_wc_conflict_action_add,
                svn_wc_conflict_reason_obstructed, db_kind,
                svn_node_none, svn_node_file, b->repos_root_url, b->repos_uuid,
                svn_relpath_join(b->added_repos_relpath, relpath, scratch_pool),
                b->merge_left_rev, b->merge_right_rev,
                b->ctx->wc_ctx, b->ctx->notify_func2, b->ctx->notify_baton2,
                scratch_pool));
      return SVN_NO_ERROR;
    }

  propsarray = svn_prop_hash_to_array(right_props, scratch_pool);
  SVN_ERR(svn_categorize_props(propsarray, NULL, NULL, &regular_props,
                               scratch_pool));
  SVN_ERR(svn_io_copy_file(right_file, local_abspath, FALSE, scratch_pool));
  SVN_ERR(svn_wc_add_from_disk3(b->ctx->wc_ctx, local_abspath,
                                svn_prop_array_to_hash(regular_props,
                                                       scratch_pool),
                                FALSE, b->ctx->notify_func2,
                                b->ctx->notify_baton2, scratch_pool));

  return SVN_NO_ERROR;
}

/* Merge a newly added directory into TARGET_ABSPATH in the working copy.
 *
 * This uses a diff-tree processor because our standard merge operation
 * is not set up for merges where the merge-source anchor is itself an
 * added directory (i.e. does not exist on one side of the diff).
 * The standard merge will only merge additions of children of a path
 * that exists across the entire revision range being merged.
 * But in our case, SOURCE1 does not yet exist in REV1, but SOURCE2
 * does exist in REV2. Thus we use a diff processor.
 */
static svn_error_t *
merge_newly_added_dir(const char *added_repos_relpath,
                      const char *source1,
                      svn_revnum_t rev1,
                      const char *source2,
                      svn_revnum_t rev2,
                      const char *target_abspath,
                      svn_boolean_t reverse_merge,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_diff_tree_processor_t *processor;
  struct merge_newly_added_dir_baton baton = { 0 };
  const svn_diff_tree_processor_t *diff_processor;
  svn_ra_session_t *ra_session;
  const char *corrected_url;
  svn_ra_session_t *extra_ra_session;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;

  svn_uri_split(&anchor1, &target1, source1, scratch_pool);
  svn_uri_split(&anchor2, &target2, source2, scratch_pool);

  baton.target_abspath = target_abspath;
  baton.ctx = ctx;
  baton.added_repos_relpath = added_repos_relpath;
  SVN_ERR(svn_wc__node_get_repos_info(NULL, NULL,
                                      &baton.repos_root_url, &baton.repos_uuid,
                                      ctx->wc_ctx, target_abspath,
                                      scratch_pool, scratch_pool));
  baton.merge_left_rev = rev1;
  baton.merge_right_rev = rev2;

  processor = svn_diff__tree_processor_create(&baton, scratch_pool);
  processor->dir_added = diff_dir_added;
  processor->file_added = diff_file_added;

  diff_processor = processor;
  if (reverse_merge)
    diff_processor = svn_diff__tree_processor_reverse_create(diff_processor,
                                                             NULL,
                                                             scratch_pool);

  /* Filter the first path component using a filter processor, until we fixed
     the diff processing to handle this directly */
  diff_processor = svn_diff__tree_processor_filter_create(
                     diff_processor, target1, scratch_pool);

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               anchor2, NULL, NULL, FALSE,
                                               FALSE, ctx,
                                               scratch_pool, scratch_pool));
  if (corrected_url)
    anchor2 = corrected_url;

  /* Extra RA session is used during the editor calls to fetch file contents. */
  SVN_ERR(svn_ra__dup_session(&extra_ra_session, ra_session, anchor2,
                              scratch_pool, scratch_pool));

  /* Create a repos-repos diff editor. */
  SVN_ERR(svn_client__get_diff_editor2(
                &diff_editor, &diff_edit_baton,
                extra_ra_session, svn_depth_infinity, rev1, TRUE,
                diff_processor, ctx->cancel_func, ctx->cancel_baton,
                scratch_pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3(ra_session, &reporter, &reporter_baton,
                          rev2, target1, svn_depth_infinity, TRUE, TRUE,
                          source2, diff_editor, diff_edit_baton, scratch_pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             scratch_pool));

  SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_dir_merge(svn_client_conflict_option_t *option,
                                       svn_client_conflict_t *conflict,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *scratch_pool)
{
  const char *repos_root_url;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *local_abspath;
  const char *lock_abspath;
  struct conflict_tree_incoming_add_details *details;
  const char *added_repos_relpath;
  const char *source1;
  svn_revnum_t rev1;
  const char *source2;
  svn_revnum_t rev2;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  details = conflict->tree_conflict_incoming_details;
  if (details == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Conflict resolution option '%d' requires "
                               "details for tree conflict at '%s' to be "
                               "fetched from the repository"),
                            option->id,
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));

  /* Set up merge sources to merge the entire incoming added directory tree. */
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  source1 = svn_path_url_add_component2(repos_root_url,
                                        details->repos_relpath,
                                        scratch_pool);
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            NULL, conflict, scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool, scratch_pool));
  if (incoming_old_pegrev < incoming_new_pegrev) /* forward merge */
    {
      if (details->added_rev == SVN_INVALID_REVNUM)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Could not determine when '%s' was "
                                   "added the repository"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
      rev1 = rev_below(details->added_rev);
      source2 = svn_path_url_add_component2(repos_root_url,
                                            incoming_new_repos_relpath,
                                            scratch_pool);
      rev2 = incoming_new_pegrev;
      added_repos_relpath = incoming_new_repos_relpath;
    }
  else /* reverse-merge */
    {
      if (details->deleted_rev == SVN_INVALID_REVNUM)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Could not determine when '%s' was "
                                   "deleted from the repository"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
      rev1 = details->deleted_rev;
      source2 = svn_path_url_add_component2(repos_root_url,
                                            incoming_old_repos_relpath,
                                            scratch_pool);
      rev2 = incoming_old_pegrev;
      added_repos_relpath = incoming_new_repos_relpath;
    }

  /* ### The following WC modifications should be atomic. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  /* ### wrap in a transaction */
  err = merge_newly_added_dir(added_repos_relpath,
                              source1, rev1, source2, rev2,
                              local_abspath,
                              (incoming_old_pegrev > incoming_new_pegrev),
                              ctx, scratch_pool, scratch_pool);
  if (!err)
    err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved_tree,
                                           scratch_pool),
                      scratch_pool);

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_update_incoming_added_dir_merge(svn_client_conflict_option_t *option,
                                       svn_client_conflict_t *conflict,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(
            &lock_abspath, ctx->wc_ctx, local_abspath,
            scratch_pool, scratch_pool));

  err = svn_wc__conflict_tree_update_local_add(ctx->wc_ctx,
                                               local_abspath,
                                               ctx->cancel_func,
                                               ctx->cancel_baton,
                                               ctx->notify_func2,
                                               ctx->notify_baton2,
                                               scratch_pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* A baton for notification_adjust_func(). */
struct notification_adjust_baton
{
  svn_wc_notify_func2_t inner_func;
  void *inner_baton;
  const char *checkout_abspath;
  const char *final_abspath;
};

/* A svn_wc_notify_func2_t function that wraps BATON->inner_func (whose
 * baton is BATON->inner_baton) and adjusts the notification paths that
 * start with BATON->checkout_abspath to start instead with
 * BATON->final_abspath. */
static void
notification_adjust_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  struct notification_adjust_baton *nb = baton;
  svn_wc_notify_t *inner_notify = svn_wc_dup_notify(notify, pool);
  const char *relpath;

  relpath = svn_dirent_skip_ancestor(nb->checkout_abspath, notify->path);
  inner_notify->path = svn_dirent_join(nb->final_abspath, relpath, pool);

  if (nb->inner_func)
    nb->inner_func(nb->inner_baton, inner_notify, pool);
}

/* Resolve a dir/dir "incoming add vs local obstruction" tree conflict by
 * replacing the local directory with the incoming directory.
 * If MERGE_DIRS is set, also merge the directories after replacing. */
static svn_error_t *
merge_incoming_added_dir_replace(svn_client_conflict_option_t *option,
                                 svn_client_conflict_t *conflict,
                                 svn_client_ctx_t *ctx,
                                 svn_boolean_t merge_dirs,
                                 apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  const char *url;
  const char *corrected_url;
  const char *repos_root_url;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *local_abspath;
  const char *lock_abspath;
  const char *tmpdir_abspath, *tmp_abspath;
  svn_error_t *err;
  svn_revnum_t copy_src_revnum;
  svn_opt_revision_t copy_src_peg_revision;
  svn_boolean_t timestamp_sleep;
  svn_wc_notify_func2_t old_notify_func2 = ctx->notify_func2;
  void *old_notify_baton2 = ctx->notify_baton2;
  struct notification_adjust_baton nb;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Find the URL of the incoming added directory in the repository. */
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  url = svn_path_url_add_component2(repos_root_url, incoming_new_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL, FALSE, FALSE,
                                               ctx, scratch_pool,
                                               scratch_pool));
  if (corrected_url)
    url = corrected_url;


  /* Find a temporary location in which to check out the copy source. */
  SVN_ERR(svn_wc__get_tmpdir(&tmpdir_abspath, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));

  SVN_ERR(svn_io_open_unique_file3(NULL, &tmp_abspath, tmpdir_abspath,
                                   svn_io_file_del_on_close,
                                   scratch_pool, scratch_pool));

  /* Make a new checkout of the requested source. While doing so,
   * resolve copy_src_revnum to an actual revision number in case it
   * was until now 'invalid' meaning 'head'.  Ask this function not to
   * sleep for timestamps, by passing a sleep_needed output param.
   * Send notifications for all nodes except the root node, and adjust
   * them to refer to the destination rather than this temporary path. */

  nb.inner_func = ctx->notify_func2;
  nb.inner_baton = ctx->notify_baton2;
  nb.checkout_abspath = tmp_abspath;
  nb.final_abspath = local_abspath;
  ctx->notify_func2 = notification_adjust_func;
  ctx->notify_baton2 = &nb;

  copy_src_peg_revision.kind = svn_opt_revision_number;
  copy_src_peg_revision.value.number = incoming_new_pegrev;

  err = svn_client__checkout_internal(&copy_src_revnum, &timestamp_sleep,
                                      url, tmp_abspath,
                                      &copy_src_peg_revision,
                                      &copy_src_peg_revision,
                                      svn_depth_infinity,
                                      TRUE, /* we want to ignore externals */
                                      FALSE, /* we don't allow obstructions */
                                      ra_session, ctx, scratch_pool);

  ctx->notify_func2 = old_notify_func2;
  ctx->notify_baton2 = old_notify_baton2;

  SVN_ERR(err);

  /* ### The following WC modifications should be atomic. */

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 svn_dirent_dirname(
                                                   local_abspath,
                                                   scratch_pool),
                                                 scratch_pool, scratch_pool));

  /* Remove the working directory. */
  err = svn_wc_delete4(ctx->wc_ctx, local_abspath, FALSE, FALSE,
                       NULL, NULL, /* don't allow user to cancel here */
                       ctx->notify_func2, ctx->notify_baton2,
                       scratch_pool);
  if (err)
    goto unlock_wc;

  /* Schedule dst_path for addition in parent, with copy history.
     Don't send any notification here.
     Then remove the temporary checkout's .svn dir in preparation for
     moving the rest of it into the final destination. */
  err = svn_wc_copy3(ctx->wc_ctx, tmp_abspath, local_abspath,
                     TRUE /* metadata_only */,
                     NULL, NULL, /* don't allow user to cancel here */
                     NULL, NULL, scratch_pool);
  if (err)
    goto unlock_wc;

  err = svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, tmp_abspath,
                                   FALSE, scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;
  err = svn_wc_remove_from_revision_control2(ctx->wc_ctx,
                                             tmp_abspath,
                                             FALSE, FALSE,
                                             NULL, NULL, /* don't cancel */
                                             scratch_pool);
  if (err)
    goto unlock_wc;

  /* Move the temporary disk tree into place. */
  err = svn_io_file_rename2(tmp_abspath, local_abspath, FALSE, scratch_pool);
  if (err)
    goto unlock_wc;

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_add,
                                                     scratch_pool);
      notify->kind = svn_node_dir;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  /* Resolve to current working copy state.
   * svn_client__merge_locked() requires this. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);
  if (err)
    goto unlock_wc;

  if (merge_dirs)
    {
      svn_revnum_t base_revision;
      const char *base_repos_relpath;
      struct find_added_rev_baton b = { 0 };

      /* Find the URL and revision of the directory we have just replaced. */
      err = svn_wc__node_get_base(NULL, &base_revision, &base_repos_relpath,
                                  NULL, NULL, NULL, ctx->wc_ctx, local_abspath,
                                  FALSE, scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;

      url = svn_path_url_add_component2(repos_root_url, base_repos_relpath,
                                        scratch_pool);

      /* Trace the replaced directory's history to its origin. */
      err = svn_ra_reparent(ra_session, url, scratch_pool);
      if (err)
        goto unlock_wc;
      b.victim_abspath = local_abspath;
      b.ctx = ctx;
      b.added_rev = SVN_INVALID_REVNUM;
      b.repos_relpath = NULL;
      b.parent_repos_relpath = svn_relpath_dirname(base_repos_relpath,
                                                   scratch_pool);
      b.pool = scratch_pool;

      err = svn_ra_get_location_segments(ra_session, "", base_revision,
                                         base_revision, SVN_INVALID_REVNUM,
                                         find_added_rev, &b,
                                         scratch_pool);
      if (err)
        goto unlock_wc;

      if (b.added_rev == SVN_INVALID_REVNUM)
        {
          err = svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                  _("Could not determine the revision in "
                                    "which '^/%s' was added to the "
                                    "repository.\n"),
                                  base_repos_relpath);
          goto unlock_wc;
        }

      /* Merge the replaced directory into the directory which replaced it.
       * We do not need to consider a reverse-merge here since the source of
       * this merge was part of the merge target working copy, not a branch
       * in the repository. */
      err = merge_newly_added_dir(base_repos_relpath,
                                  url, rev_below(b.added_rev), url,
                                  base_revision, local_abspath, FALSE,
                                  ctx, scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;
    }

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);
  SVN_ERR(err);

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                  local_abspath,
                                  svn_wc_notify_resolved_tree,
                                  scratch_pool);

      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_dir_replace(svn_client_conflict_option_t *option,
                                         svn_client_conflict_t *conflict,
                                         svn_client_ctx_t *ctx,
                                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(merge_incoming_added_dir_replace(option,
                                                          conflict,
                                                          ctx,
                                                          FALSE,
                                                          scratch_pool));
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_merge_incoming_added_dir_replace_and_merge(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  return svn_error_trace(merge_incoming_added_dir_replace(option,
                                                          conflict,
                                                          ctx,
                                                          TRUE,
                                                          scratch_pool));
}

/* Verify the local working copy state matches what we expect when an
 * incoming deletion tree conflict exists.
 * We assume update/merge/switch operations leave the working copy in a
 * state which prefers the local change and cancels the deletion.
 * Run a quick sanity check and error out if it looks as if the
 * working copy was modified since, even though it's not easy to make
 * such modifications without also clearing the conflict marker. */
static svn_error_t *
verify_local_state_for_incoming_delete(svn_client_conflict_t *conflict,
                                       svn_client_conflict_option_t *option,
                                        svn_client_ctx_t *ctx,
                                       apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  const char *wcroot_abspath;
  svn_wc_operation_t operation;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                             local_abspath, scratch_pool,
                             scratch_pool));
  operation = svn_client_conflict_get_operation(conflict);

  if (operation == svn_wc_operation_update ||
      operation == svn_wc_operation_switch)
    {
      struct conflict_tree_incoming_delete_details *details;
      svn_boolean_t is_copy;
      svn_revnum_t copyfrom_rev;
      const char *copyfrom_repos_relpath;

      details = conflict->tree_conflict_incoming_details;
      if (details == NULL)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Conflict resolution option '%d' requires "
                                   "details for tree conflict at '%s' to be "
                                   "fetched from the repository."),
                                option->id,
                                svn_dirent_local_style(local_abspath,
                                                       scratch_pool));

      /* Ensure that the item is a copy of itself from before it was deleted.
       * Update and switch are supposed to set this up when flagging the
       * conflict. */
      SVN_ERR(svn_wc__node_get_origin(&is_copy, &copyfrom_rev,
                                      &copyfrom_repos_relpath,
                                      NULL, NULL, NULL, NULL,
                                      ctx->wc_ctx, local_abspath, FALSE,
                                      scratch_pool, scratch_pool));
      if (!is_copy)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected a copied item, but the item "
                                   "is not a copy)"),
                                 svn_dirent_local_style(
                                   svn_dirent_skip_ancestor(
                                     wcroot_abspath,
                                     conflict->local_abspath),
                                 scratch_pool));
      else if (details->deleted_rev == SVN_INVALID_REVNUM &&
               details->added_rev == SVN_INVALID_REVNUM)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Could not find the revision in which '%s' "
                                   "was deleted from the repository"),
                                 svn_dirent_local_style(
                                   svn_dirent_skip_ancestor(
                                     wcroot_abspath,
                                     conflict->local_abspath),
                                   scratch_pool));
      else if (details->deleted_rev != SVN_INVALID_REVNUM &&
               copyfrom_rev >= details->deleted_rev)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected an item copied from a revision "
                                   "smaller than r%ld, but the item was "
                                   "copied from r%ld)"),
                                 svn_dirent_local_style(
                                   svn_dirent_skip_ancestor(
                                     wcroot_abspath, conflict->local_abspath),
                                   scratch_pool),
                                 details->deleted_rev, copyfrom_rev);

      else if (details->added_rev != SVN_INVALID_REVNUM &&
               copyfrom_rev < details->added_rev)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected an item copied from a revision "
                                   "larger than r%ld, but the item was "
                                   "copied from r%ld)"),
                                 svn_dirent_local_style(
                                   svn_dirent_skip_ancestor(
                                     wcroot_abspath, conflict->local_abspath),
                                   scratch_pool),
                                  details->added_rev, copyfrom_rev);
      else if (operation == svn_wc_operation_update)
        {
          const char *old_repos_relpath;

          SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
                    &old_repos_relpath, NULL, NULL, conflict,
                    scratch_pool, scratch_pool));
          if (strcmp(copyfrom_repos_relpath, details->repos_relpath) != 0 &&
              strcmp(copyfrom_repos_relpath, old_repos_relpath) != 0)
            return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                     _("Cannot resolve tree conflict on '%s' "
                                       "(expected an item copied from '^/%s' "
                                       "or from '^/%s' but the item was "
                                       "copied from '^/%s@%ld')"),
                                     svn_dirent_local_style(
                                       svn_dirent_skip_ancestor(
                                         wcroot_abspath, conflict->local_abspath),
                                       scratch_pool),
                                     details->repos_relpath,
                                     old_repos_relpath,
                                     copyfrom_repos_relpath, copyfrom_rev);
        }
      else if (operation == svn_wc_operation_switch)
        {
          const char *old_repos_relpath;

          SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
                    &old_repos_relpath, NULL, NULL, conflict,
                    scratch_pool, scratch_pool));

          if (strcmp(copyfrom_repos_relpath, old_repos_relpath) != 0)
            return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                     _("Cannot resolve tree conflict on '%s' "
                                       "(expected an item copied from '^/%s', "
                                       "but the item was copied from "
                                        "'^/%s@%ld')"),
                                     svn_dirent_local_style(
                                       svn_dirent_skip_ancestor(
                                         wcroot_abspath,
                                         conflict->local_abspath),
                                       scratch_pool),
                                     old_repos_relpath,
                                     copyfrom_repos_relpath, copyfrom_rev);
        }
    }
  else if (operation == svn_wc_operation_merge)
    {
      svn_node_kind_t victim_node_kind;
      svn_node_kind_t on_disk_kind;

      /* For merge, all we can do is ensure that the item still exists. */
      victim_node_kind =
        svn_client_conflict_tree_get_victim_node_kind(conflict);
      SVN_ERR(svn_io_check_path(local_abspath, &on_disk_kind, scratch_pool));

      if (victim_node_kind != on_disk_kind)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("Cannot resolve tree conflict on '%s' "
                                   "(expected node kind '%s' but found '%s')"),
                                 svn_dirent_local_style(
                                   svn_dirent_skip_ancestor(
                                     wcroot_abspath, conflict->local_abspath),
                                   scratch_pool),
                                 svn_node_kind_to_word(victim_node_kind),
                                 svn_node_kind_to_word(on_disk_kind));
    }

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_incoming_delete_ignore(svn_client_conflict_option_t *option,
                               svn_client_conflict_t *conflict,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));

  err = verify_local_state_for_incoming_delete(conflict, option, ctx,
                                               scratch_pool);
  if (err)
    goto unlock_wc;

  /* Resolve to the current working copy state. */
  err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath, scratch_pool);

  /* svn_wc__del_tree_conflict doesn't handle notification for us */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved_tree,
                                           scratch_pool),
                      scratch_pool);

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = option_id;

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_incoming_delete_accept(svn_client_conflict_option_t *option,
                               svn_client_conflict_t *conflict,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *parent_abspath;
  const char *lock_abspath;
  svn_error_t *err;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);

  /* Deleting a node requires a lock on the node's parent. */
  parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 parent_abspath,
                                                 scratch_pool, scratch_pool));

  err = verify_local_state_for_incoming_delete(conflict, option, ctx,
                                               scratch_pool);
  if (err)
    goto unlock_wc;

  /* Delete the tree conflict victim. Marks the conflict resolved. */
  err = svn_wc_delete4(ctx->wc_ctx, local_abspath, FALSE, FALSE,
                       NULL, NULL, /* don't allow user to cancel here */
                       ctx->notify_func2, ctx->notify_baton2,
                       scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          /* Not a versioned path. This can happen if the victim has already
           * been deleted in our branche's history, for example. Either way,
           * the item is gone, which is what we want, so don't treat this as
           * a fatal error. */
          svn_error_clear(err);

          /* Resolve to current working copy state. */
          err = svn_wc__del_tree_conflict(ctx->wc_ctx, local_abspath,
                                          scratch_pool);
        }

      if (err)
        goto unlock_wc;
    }

  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved_tree,
                                           scratch_pool),
                      scratch_pool);

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  conflict->resolution_tree = option_id;

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_incoming_move_file_text_merge(svn_client_conflict_option_t *option,
                                      svn_client_conflict_t *conflict,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  svn_wc_operation_t operation;
  const char *lock_abspath;
  svn_error_t *err;
  const char *repos_root_url;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *wc_tmpdir;
  const char *ancestor_abspath;
  svn_stream_t *ancestor_stream;
  apr_hash_t *ancestor_props;
  apr_hash_t *victim_props;
  apr_hash_t *move_target_props;
  const char *ancestor_url;
  const char *corrected_url;
  svn_ra_session_t *ra_session;
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  apr_array_header_t *propdiffs;
  struct conflict_tree_incoming_delete_details *details;
  apr_array_header_t *possible_moved_to_abspaths;
  const char *moved_to_abspath;
  const char *incoming_abspath = NULL;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  operation = svn_client_conflict_get_operation(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->moves == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("The specified conflict resolution option "
                               "requires details for tree conflict at '%s' "
                               "to be fetched from the repository first."),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
  if (operation == svn_wc_operation_none)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Invalid operation code '%d' recorded for "
                               "conflict at '%s'"), operation,
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  option_id = svn_client_conflict_option_get_id(option);
  SVN_ERR_ASSERT(option_id ==
                 svn_client_conflict_option_incoming_move_file_text_merge ||
                 option_id ==
                 svn_client_conflict_option_incoming_move_dir_merge);
                  
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  /* Set up temporary storage for the common ancestor version of the file. */
  SVN_ERR(svn_wc__get_tmpdir(&wc_tmpdir, ctx->wc_ctx, local_abspath,
                             scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_open_unique(&ancestor_stream,
                                 &ancestor_abspath, wc_tmpdir,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));

  /* Fetch the ancestor file's content. */
  ancestor_url = svn_path_url_add_component2(repos_root_url,
                                             incoming_old_repos_relpath,
                                             scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               ancestor_url, NULL, NULL,
                                               FALSE, FALSE, ctx,
                                               scratch_pool, scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, "", incoming_old_pegrev,
                          ancestor_stream, NULL, /* fetched_rev */
                          &ancestor_props, scratch_pool));
  filter_props(ancestor_props, scratch_pool);

  /* Close stream to flush ancestor file to disk. */
  SVN_ERR(svn_stream_close(ancestor_stream));

  possible_moved_to_abspaths =
    svn_hash_gets(details->wc_move_targets,
                  get_moved_to_repos_relpath(details, scratch_pool));
  moved_to_abspath = APR_ARRAY_IDX(possible_moved_to_abspaths,
                                   details->wc_move_target_idx,
                                   const char *);

  /* ### The following WC modifications should be atomic. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(
            &lock_abspath, ctx->wc_ctx,
            svn_dirent_get_longest_ancestor(local_abspath,
                                            moved_to_abspath,
                                            scratch_pool),
            scratch_pool, scratch_pool));

  err = verify_local_state_for_incoming_delete(conflict, option, ctx,
                                               scratch_pool);
  if (err)
    goto unlock_wc;

   /* Get a copy of the conflict victim's properties. */
  err = svn_wc_prop_list2(&victim_props, ctx->wc_ctx, local_abspath,
                          scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;

  /* Get a copy of the move target's properties. */
  err = svn_wc_prop_list2(&move_target_props, ctx->wc_ctx,
                          moved_to_abspath,
                          scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;

  /* Create a property diff for the files. */
  err = svn_prop_diffs(&propdiffs, move_target_props, victim_props,
                       scratch_pool);
  if (err)
    goto unlock_wc;

  if (operation == svn_wc_operation_update ||
      operation == svn_wc_operation_switch)
    {
      svn_stream_t *working_stream;
      svn_stream_t *incoming_stream;

      /* Create a temporary copy of the working file in repository-normal form.
       * Set up this temporary file to be automatically removed. */
      err = svn_stream_open_unique(&incoming_stream,
                                   &incoming_abspath, wc_tmpdir,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;

      err = svn_wc__translated_stream(&working_stream, ctx->wc_ctx,
                                      local_abspath, local_abspath,
                                      SVN_WC_TRANSLATE_TO_NF,
                                      scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;

      err = svn_stream_copy3(working_stream, incoming_stream,
                             NULL, NULL, /* no cancellation */
                             scratch_pool);
      if (err)
        goto unlock_wc;
    }
  else if (operation == svn_wc_operation_merge)
    {
      svn_stream_t *incoming_stream;
      svn_stream_t *move_target_stream;

      /* Set aside the current move target file. This is required to apply
       * the move, and only then perform a three-way text merge between
       * the ancestor's file, our working file (which we would move to
       * the destination), and the file that we have set aside, which
       * contains the incoming fulltext.
       * Set up this temporary file to NOT be automatically removed. */
      err = svn_stream_open_unique(&incoming_stream,
                                   &incoming_abspath, wc_tmpdir,
                                   svn_io_file_del_none,
                                   scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;

      err = svn_wc__translated_stream(&move_target_stream, ctx->wc_ctx,
                                      moved_to_abspath, moved_to_abspath,
                                      SVN_WC_TRANSLATE_TO_NF,
                                      scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;

      err = svn_stream_copy3(move_target_stream, incoming_stream,
                             NULL, NULL, /* no cancellation */
                             scratch_pool);
      if (err)
        goto unlock_wc;

      /* Apply the incoming move. */
      err = svn_io_remove_file2(moved_to_abspath, FALSE, scratch_pool);
      if (err)
        goto unlock_wc;
      err = svn_wc__move2(ctx->wc_ctx, local_abspath, moved_to_abspath,
                          FALSE, /* ordinary (not meta-data only) move */
                          FALSE, /* mixed-revisions don't apply to files */
                          NULL, NULL, /* don't allow user to cancel here */
                          NULL, NULL, /* no extra notification */
                          scratch_pool);
      if (err)
        goto unlock_wc;
    }
  else
    SVN_ERR_MALFUNCTION();

  /* Perform the file merge. */
  err = svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                      ctx->wc_ctx, ancestor_abspath,
                      incoming_abspath, moved_to_abspath,
                      NULL, NULL, NULL, /* labels */
                      NULL, NULL, /* conflict versions */
                      FALSE, /* dry run */
                      NULL, NULL, /* diff3_cmd, merge_options */
                      apr_hash_count(ancestor_props) ? ancestor_props : NULL,
                      propdiffs,
                      NULL, NULL, /* conflict func/baton */
                      NULL, NULL, /* don't allow user to cancel here */
                      scratch_pool);
  svn_io_sleep_for_timestamps(moved_to_abspath, scratch_pool);
  if (err)
    goto unlock_wc;

  if (operation == svn_wc_operation_merge && incoming_abspath)
    {
      err = svn_io_remove_file2(incoming_abspath, TRUE, scratch_pool);
      if (err)
        goto unlock_wc;
      incoming_abspath = NULL;
    }
  
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      /* Tell the world about the file merge that just happened. */
      notify = svn_wc_create_notify(moved_to_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  if (operation == svn_wc_operation_update ||
      operation == svn_wc_operation_switch)
    {
      /* Delete the tree conflict victim (clears the tree conflict marker). */
      err = svn_wc_delete4(ctx->wc_ctx, local_abspath, FALSE, FALSE,
                           NULL, NULL, /* don't allow user to cancel here */
                           NULL, NULL, /* no extra notification */
                           scratch_pool);
      if (err)
        goto unlock_wc;
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_resolved_tree,
                                    scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = option_id;

unlock_wc:
  if (err && operation == svn_wc_operation_merge && incoming_abspath)
      err = svn_error_quick_wrapf(
              err, _("If needed, a backup copy of '%s' can be found at '%s'"),
              svn_dirent_local_style(moved_to_abspath, scratch_pool),
              svn_dirent_local_style(incoming_abspath, scratch_pool));
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_incoming_move_dir_merge(svn_client_conflict_option_t *option,
                                svn_client_conflict_t *conflict,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  svn_wc_operation_t operation;
  const char *lock_abspath;
  svn_error_t *err;
  const char *repos_root_url;
  const char *repos_uuid;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *victim_repos_relpath;
  svn_revnum_t victim_peg_rev;
  const char *moved_to_repos_relpath;
  svn_revnum_t moved_to_peg_rev;
  struct conflict_tree_incoming_delete_details *details;
  apr_array_header_t *possible_moved_to_abspaths;
  const char *moved_to_abspath;
  svn_client__pathrev_t *yca_loc;
  svn_opt_revision_t yca_opt_rev;
  svn_client__conflict_report_t *conflict_report;
  svn_boolean_t is_copy;
  svn_boolean_t is_modified;

  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  operation = svn_client_conflict_get_operation(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->moves == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("The specified conflict resolution option "
                               "requires details for tree conflict at '%s' "
                               "to be fetched from the repository first."),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));

  option_id = svn_client_conflict_option_get_id(option);
  SVN_ERR_ASSERT(option_id ==
                 svn_client_conflict_option_incoming_move_dir_merge);
                  
  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, &repos_uuid,
                                             conflict, scratch_pool,
                                             scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  /* Get repository location of the moved-away node (the conflict victim). */
  if (operation == svn_wc_operation_update ||
      operation == svn_wc_operation_switch)
    {
      victim_repos_relpath = incoming_old_repos_relpath;
      victim_peg_rev = incoming_old_pegrev;
    }
  else if (operation == svn_wc_operation_merge)
    SVN_ERR(svn_wc__node_get_repos_info(&victim_peg_rev, &victim_repos_relpath,
                                        NULL, NULL, ctx->wc_ctx, local_abspath,
                                        scratch_pool, scratch_pool));

  /* Get repository location of the moved-here node (incoming move). */
  possible_moved_to_abspaths =
    svn_hash_gets(details->wc_move_targets,
                  get_moved_to_repos_relpath(details, scratch_pool));
  moved_to_abspath = APR_ARRAY_IDX(possible_moved_to_abspaths,
                                   details->wc_move_target_idx,
                                   const char *);

  /* ### The following WC modifications should be atomic. */

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(
            &lock_abspath, ctx->wc_ctx,
            svn_dirent_get_longest_ancestor(local_abspath,
                                            moved_to_abspath,
                                            scratch_pool),
            scratch_pool, scratch_pool));

  err = svn_wc__node_get_origin(&is_copy, &moved_to_peg_rev,
                                &moved_to_repos_relpath,
                                NULL, NULL, NULL, NULL,
                                ctx->wc_ctx, moved_to_abspath, FALSE,
                                scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;
  if (!is_copy && operation == svn_wc_operation_merge)
    {
      err = svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                              _("Cannot resolve tree conflict on '%s' "
                                "(expected a copied item at '%s', but the "
                                "item is not a copy)"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool),
                              svn_dirent_local_style(moved_to_abspath,
                                                     scratch_pool));
      goto unlock_wc;
    }

  if (moved_to_repos_relpath == NULL || moved_to_peg_rev == SVN_INVALID_REVNUM)
    {
      err = svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                              _("Cannot resolve tree conflict on '%s' "
                                "(could not determine origin of '%s')"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool),
                              svn_dirent_local_style(moved_to_abspath,
                                                     scratch_pool));
      goto unlock_wc;
    }

  /* Now find the youngest common ancestor of these nodes. */
  err = find_yca(&yca_loc, victim_repos_relpath, victim_peg_rev,
                 moved_to_repos_relpath, moved_to_peg_rev,
                 repos_root_url, repos_uuid,
                 NULL, ctx, scratch_pool, scratch_pool);
  if (err)
    goto unlock_wc;

  if (yca_loc == NULL)
    {
      err = svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                              _("Cannot resolve tree conflict on '%s' "
                                "(could not find common ancestor of '^/%s@%ld' "
                                " and '^/%s@%ld')"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool),
                              victim_repos_relpath, victim_peg_rev,
                              moved_to_repos_relpath, moved_to_peg_rev);
      goto unlock_wc;
    }

  yca_opt_rev.kind = svn_opt_revision_number;
  yca_opt_rev.value.number = yca_loc->rev;

  err = verify_local_state_for_incoming_delete(conflict, option, ctx,
                                               scratch_pool);
  if (err)
    goto unlock_wc;

  if (operation == svn_wc_operation_merge)
    {
      const char *move_target_url;
      svn_opt_revision_t incoming_new_opt_rev;

      /* Revert the incoming move target directory. */
      SVN_ERR(svn_wc_revert5(ctx->wc_ctx, moved_to_abspath, svn_depth_infinity,
                             FALSE, NULL, TRUE, FALSE,
                             NULL, NULL, /* no cancellation */
                             ctx->notify_func2, ctx->notify_baton2,
                             scratch_pool));

      /* The move operation is not part of natural history. We must replicate
       * this move in our history. Record a move in the working copy. */
      err = svn_wc__move2(ctx->wc_ctx, local_abspath, moved_to_abspath,
                          FALSE, /* this is not a meta-data only move */
                          TRUE, /* allow mixed-revisions just in case */
                          NULL, NULL, /* don't allow user to cancel here */
                          ctx->notify_func2, ctx->notify_baton2,
                          scratch_pool);
      if (err)
        goto unlock_wc;

      /* Merge YCA_URL@YCA_REV->MOVE_TARGET_URL@MERGE_RIGHT into move target. */
      move_target_url = apr_pstrcat(scratch_pool, repos_root_url, "/",
                                    get_moved_to_repos_relpath(details,
                                                               scratch_pool),
                                    SVN_VA_NULL);
      incoming_new_opt_rev.kind = svn_opt_revision_number;
      incoming_new_opt_rev.value.number = incoming_new_pegrev;
      err = svn_client__merge_locked(&conflict_report,
                                     yca_loc->url, &yca_opt_rev,
                                     move_target_url, &incoming_new_opt_rev,
                                     moved_to_abspath, svn_depth_infinity,
                                     TRUE, TRUE, /* do a no-ancestry merge */
                                     FALSE, FALSE, FALSE,
                                     TRUE, /* Allow mixed-rev just in case,
                                            * since conflict victims can't be
                                            * updated to straighten out
                                            * mixed-rev trees. */
                                     NULL, ctx, scratch_pool, scratch_pool);
      if (err)
        goto unlock_wc;
    }
  else
    {
      SVN_ERR_ASSERT(operation == svn_wc_operation_update ||
                     operation == svn_wc_operation_switch);

      /* Merge local modifications into the incoming move target dir. */
      err = svn_wc__has_local_mods(&is_modified, ctx->wc_ctx, local_abspath,
                                   TRUE, ctx->cancel_func, ctx->cancel_baton,
                                   scratch_pool);
      if (err)
        goto unlock_wc;

      if (is_modified)
        {
          err = svn_wc__conflict_tree_update_incoming_move(ctx->wc_ctx,
                                                           local_abspath,
                                                           moved_to_abspath,
                                                           ctx->cancel_func,
                                                           ctx->cancel_baton,
                                                           ctx->notify_func2,
                                                           ctx->notify_baton2,
                                                           scratch_pool);
          if (err)
            goto unlock_wc;
        }

      /* The move operation is part of our natural history.
       * Delete the tree conflict victim (clears the tree conflict marker). */
      err = svn_wc_delete4(ctx->wc_ctx, local_abspath, FALSE, FALSE,
                           NULL, NULL, /* don't allow user to cancel here */
                           NULL, NULL, /* no extra notification */
                           scratch_pool);
      if (err)
        goto unlock_wc;
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_resolved_tree,
                                    scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = option_id;

unlock_wc:
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_local_move_file_merge(svn_client_conflict_option_t *option,
                              svn_client_conflict_t *conflict,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  const char *lock_abspath;
  svn_error_t *err;
  const char *repos_root_url;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  const char *wc_tmpdir;
  const char *ancestor_tmp_abspath;
  const char *incoming_tmp_abspath;
  apr_hash_t *ancestor_props;
  apr_hash_t *incoming_props;
  svn_stream_t *stream;
  const char *url;
  const char *corrected_url;
  const char *old_session_url;
  svn_ra_session_t *ra_session;
  svn_wc_merge_outcome_t merge_content_outcome;
  svn_wc_notify_state_t merge_props_outcome;
  apr_array_header_t *propdiffs;
  struct conflict_tree_local_missing_details *details;

  details = conflict->tree_conflict_local_details;

  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  SVN_ERR(svn_wc__get_tmpdir(&wc_tmpdir, ctx->wc_ctx,
                             details->moved_to_abspath,
                             scratch_pool, scratch_pool));

  /* Fetch the common ancestor file's content. */
  SVN_ERR(svn_stream_open_unique(&stream, &ancestor_tmp_abspath, wc_tmpdir,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  url = svn_path_url_add_component2(repos_root_url,
                                    incoming_old_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               url, NULL, NULL,
                                               FALSE, FALSE, ctx,
                                               scratch_pool, scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, "", incoming_old_pegrev, stream, NULL,
                          &ancestor_props, scratch_pool));
  filter_props(ancestor_props, scratch_pool);

  /* Close stream to flush the file to disk. */
  SVN_ERR(svn_stream_close(stream));

  /* Do the same for the incoming file's content. */
  SVN_ERR(svn_stream_open_unique(&stream, &incoming_tmp_abspath, wc_tmpdir,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  url = svn_path_url_add_component2(repos_root_url,
                                    incoming_new_repos_relpath,
                                    scratch_pool);
  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                            url, scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, "", incoming_new_pegrev, stream, NULL,
                          &incoming_props, scratch_pool));
  /* Close stream to flush the file to disk. */
  SVN_ERR(svn_stream_close(stream));

  filter_props(incoming_props, scratch_pool);

  /* Create a property diff for the files. */
  SVN_ERR(svn_prop_diffs(&propdiffs, incoming_props, ancestor_props,
                         scratch_pool));

  /* ### The following WC modifications should be atomic. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(
            &lock_abspath, ctx->wc_ctx,
            svn_dirent_get_longest_ancestor(conflict->local_abspath,
                                            details->moved_to_abspath,
                                            scratch_pool),
            scratch_pool, scratch_pool));

  /* Perform the file merge. */
  err = svn_wc_merge5(&merge_content_outcome, &merge_props_outcome,
                      ctx->wc_ctx,
                      ancestor_tmp_abspath, incoming_tmp_abspath,
                      details->moved_to_abspath,
                      NULL, NULL, NULL, /* labels */
                      NULL, NULL, /* conflict versions */
                      FALSE, /* dry run */
                      NULL, NULL, /* diff3_cmd, merge_options */
                      apr_hash_count(ancestor_props) ? ancestor_props : NULL,
                      propdiffs,
                      NULL, NULL, /* conflict func/baton */
                      NULL, NULL, /* don't allow user to cancel here */
                      scratch_pool);
  svn_io_sleep_for_timestamps(details->moved_to_abspath, scratch_pool);
  if (err)
    return svn_error_compose_create(err,
                                    svn_wc__release_write_lock(ctx->wc_ctx,
                                                               lock_abspath,
                                                               scratch_pool));

  err = svn_wc__del_tree_conflict(ctx->wc_ctx, conflict->local_abspath,
                                  scratch_pool);
  err = svn_error_compose_create(err,
                                 svn_wc__release_write_lock(ctx->wc_ctx,
                                                            lock_abspath,
                                                            scratch_pool));
  if (err)
    return svn_error_trace(err);

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      /* Tell the world about the file merge that just happened. */
      notify = svn_wc_create_notify(details->moved_to_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      if (merge_content_outcome == svn_wc_merge_conflict)
        notify->content_state = svn_wc_notify_state_conflicted;
      else
        notify->content_state = svn_wc_notify_state_merged;
      notify->prop_state = merge_props_outcome;
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);

      /* And also about the successfully resolved tree conflict. */
      notify = svn_wc_create_notify(conflict->local_abspath,
                                    svn_wc_notify_resolved_tree,
                                    scratch_pool);
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  conflict->resolution_tree = svn_client_conflict_option_get_id(option);

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_text_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted, NULL, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));

  SVN_ERR_ASSERT(text_conflicted); /* ### return proper error? */

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_prop_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  apr_array_header_t *props_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(NULL, &props_conflicted, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));

  /* ### return proper error? */
  SVN_ERR_ASSERT(props_conflicted && props_conflicted->nelts > 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_tree_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  svn_boolean_t tree_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, scratch_pool,
                                             scratch_pool));

  SVN_ERR_ASSERT(tree_conflicted); /* ### return proper error? */

  return SVN_NO_ERROR;
}

/* Helper to add to conflict resolution option to array of OPTIONS.
 * Resolution option object will be allocated from OPTIONS->POOL
 * and DESCRIPTION will be copied to this pool.
 * Returns pointer to the created conflict resolution option. */
static svn_client_conflict_option_t *
add_resolution_option(apr_array_header_t *options,
                      svn_client_conflict_t *conflict,
                      svn_client_conflict_option_id_t id,
                      const char *label,
                      const char *description,
                      conflict_option_resolve_func_t resolve_func)
{
    svn_client_conflict_option_t *option;

    option = apr_pcalloc(options->pool, sizeof(*option));
    option->pool = options->pool;
    option->id = id;
    option->label = apr_pstrdup(option->pool, label);
    option->description = apr_pstrdup(option->pool, description);
    option->conflict = conflict;
    option->do_resolve_func = resolve_func;

    APR_ARRAY_PUSH(options, const svn_client_conflict_option_t *) = option;

    return option;
}

svn_error_t *
svn_client_conflict_text_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  const char *mime_type;

  SVN_ERR(assert_text_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, 7,
                            sizeof(svn_client_conflict_option_t *));

  add_resolution_option(*options, conflict,
      svn_client_conflict_option_postpone,
      _("Postpone"),
      _("skip this conflict and leave it unresolved"),
      resolve_postpone);

  mime_type = svn_client_conflict_text_get_mime_type(conflict);
  if (mime_type && svn_mime_type_is_binary(mime_type))
    {
      /* Resolver options for a binary file conflict. */
      add_resolution_option(*options, conflict,
        svn_client_conflict_option_base_text,
        _("Accept base"),
        _("discard local and incoming changes for this binary file"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_incoming_text,
        _("Accept incoming"),
        _("accept incoming version of binary file"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_working_text,
        _("Mark as resolved"),
        _("accept binary file as it appears in the working copy"),
        resolve_text_conflict);
  }
  else
    {
      /* Resolver options for a text file conflict. */
      add_resolution_option(*options, conflict,
        svn_client_conflict_option_base_text,
        _("Accept base"),
        _("discard local and incoming changes for this file"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_incoming_text,
        _("Accept incoming"),
        _("accept incoming version of entire file"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_working_text,
        _("Reject incoming"),
        _("reject all incoming changes for this file"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_incoming_text_where_conflicted,
        _("Accept incoming for conflicts"),
        _("accept changes only where they conflict"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_working_text_where_conflicted,
        _("Reject conflicts"),
        _("reject changes which conflict and accept the rest"),
        resolve_text_conflict);

      add_resolution_option(*options, conflict,
        svn_client_conflict_option_merged_text,
        _("Mark as resolved"),
        _("accept the file as it appears in the working copy"),
        resolve_text_conflict);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_prop_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_prop_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, 7,
                            sizeof(svn_client_conflict_option_t *));

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_postpone,
    _("Postpone"),
    _("skip this conflict and leave it unresolved"),
    resolve_postpone);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_base_text,
    _("Accept base"),
    _("discard local and incoming changes for this property"),
    resolve_prop_conflict);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_incoming_text,
    _("Accept incoming"),
    _("accept incoming version of entire property value"),
    resolve_prop_conflict);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_working_text,
    _("Mark as resolved"),
    _("accept working copy version of entire property value"),
    resolve_prop_conflict);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_incoming_text_where_conflicted,
    _("Accept incoming for conflicts"),
    _("accept incoming changes only where they conflict"),
    resolve_prop_conflict);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_working_text_where_conflicted,
    _("Reject conflicts"),
    _("reject changes which conflict and accept the rest"),
    resolve_prop_conflict);

  add_resolution_option(*options, conflict,
    svn_client_conflict_option_merged_text,
    _("Accept merged"),
    _("accept merged version of property value"),
    resolve_prop_conflict);

  return SVN_NO_ERROR;
}

/* Configure 'accept current wc state' resolution option for a tree conflict. */
static svn_error_t *
configure_option_accept_current_wc_state(svn_client_conflict_t *conflict,
                                         apr_array_header_t *options)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  conflict_option_resolve_func_t do_resolve_func;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);

  if ((operation == svn_wc_operation_update ||
       operation == svn_wc_operation_switch) &&
      (local_change == svn_wc_conflict_reason_moved_away ||
       local_change == svn_wc_conflict_reason_deleted ||
       local_change == svn_wc_conflict_reason_replaced) &&
      incoming_change == svn_wc_conflict_action_edit)
    {
      /* We must break moves if the user accepts the current working copy
       * state instead of updating a moved-away node or updating children
       * moved outside of deleted or replaced directory nodes.
       * Else such moves would be left in an invalid state. */
      do_resolve_func = resolve_update_break_moved_away;
    }
  else
    do_resolve_func = resolve_accept_current_wc_state;

  add_resolution_option(options, conflict,
                        svn_client_conflict_option_accept_current_wc_state,
                        _("Mark as resolved"),
                        _("accept current working copy state"),
                        do_resolve_func);

  return SVN_NO_ERROR;
}

/* Configure 'update move destination' resolution option for a tree conflict. */
static svn_error_t *
configure_option_update_move_destination(svn_client_conflict_t *conflict,
                                         apr_array_header_t *options)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);

  if ((operation == svn_wc_operation_update ||
       operation == svn_wc_operation_switch) &&
      incoming_change == svn_wc_conflict_action_edit &&
      local_change == svn_wc_conflict_reason_moved_away)
    {
      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_update_move_destination,
        _("Update move destination"),
        _("apply incoming changes to move destination"),
        resolve_update_moved_away_node);
    }

  return SVN_NO_ERROR;
}

/* Configure 'update raise moved away children' resolution option for a tree
 * conflict. */
static svn_error_t *
configure_option_update_raise_moved_away_children(
  svn_client_conflict_t *conflict,
  apr_array_header_t *options)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);

  if ((operation == svn_wc_operation_update ||
       operation == svn_wc_operation_switch) &&
      incoming_change == svn_wc_conflict_action_edit &&
      (local_change == svn_wc_conflict_reason_deleted ||
       local_change == svn_wc_conflict_reason_replaced) &&
      victim_node_kind == svn_node_dir)
    {
      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_update_any_moved_away_children,
        _("Update any moved-away children"),
        _("prepare for updating moved-away children, if any"),
        resolve_update_raise_moved_away);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming add ignore' resolution option for a tree conflict. */
static svn_error_t *
configure_option_incoming_add_ignore(svn_client_conflict_t *conflict,
                                     svn_client_ctx_t *ctx,
                                     apr_array_header_t *options,
                                     apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t victim_node_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  /* This option is only available for directories. */
  if (victim_node_kind == svn_node_dir &&
      incoming_change == svn_wc_conflict_action_add &&
      (local_change == svn_wc_conflict_reason_obstructed ||
       local_change == svn_wc_conflict_reason_added))
    {
      const char *description;
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));
      if (operation == svn_wc_operation_merge)
        description =
          apr_psprintf(scratch_pool,
                       _("ignore and do not add '^/%s@%ld' here"),
                       incoming_new_repos_relpath, incoming_new_pegrev);
      else if (operation == svn_wc_operation_update ||
               operation == svn_wc_operation_switch)
        {
          if (victim_node_kind == svn_node_file)
            description =
              apr_psprintf(scratch_pool,
                           _("replace '^/%s@%ld' with the locally added file"),
                           incoming_new_repos_relpath, incoming_new_pegrev);
          else if (victim_node_kind == svn_node_dir)
            description =
              apr_psprintf(scratch_pool,
                           _("replace '^/%s@%ld' with the locally added "
                             "directory"),
                           incoming_new_repos_relpath, incoming_new_pegrev);
          else
            description =
              apr_psprintf(scratch_pool,
                           _("replace '^/%s@%ld' with the locally added item"),
                           incoming_new_repos_relpath, incoming_new_pegrev);
        }
      else
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("unexpected operation code '%d'"),
                                 operation);
      add_resolution_option(
        options, conflict, svn_client_conflict_option_incoming_add_ignore,
        _("Ignore incoming addition"), description, resolve_incoming_add_ignore);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming added file text merge' resolution option for a tree
 * conflict. */
static svn_error_t *
configure_option_incoming_added_file_text_merge(svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_array_header_t *options,
                                                apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (victim_node_kind == svn_node_file &&
      incoming_new_kind == svn_node_file &&
      incoming_change == svn_wc_conflict_action_add &&
      (local_change == svn_wc_conflict_reason_obstructed ||
       local_change == svn_wc_conflict_reason_added))
    {
      const char *description;
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));

      if (operation == svn_wc_operation_merge)
        description =
          apr_psprintf(scratch_pool, _("merge '^/%s@%ld' into '%s'"),
            incoming_new_repos_relpath, incoming_new_pegrev,
            svn_dirent_local_style(
              svn_dirent_skip_ancestor(wcroot_abspath,
                                       conflict->local_abspath),
              scratch_pool));
      else
        description =
          apr_psprintf(scratch_pool, _("merge local '%s' and '^/%s@%ld'"),
            svn_dirent_local_style(
              svn_dirent_skip_ancestor(wcroot_abspath,
                                       conflict->local_abspath),
              scratch_pool),
            incoming_new_repos_relpath, incoming_new_pegrev);

      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_incoming_added_file_text_merge,
        _("Merge the files"), description,
        operation == svn_wc_operation_merge
          ? resolve_merge_incoming_added_file_text_merge
          : resolve_merge_incoming_added_file_text_update);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming added file replace and merge' resolution option for a
 * tree conflict. */
static svn_error_t *
configure_option_incoming_added_file_replace_and_merge(
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_array_header_t *options,
  apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (operation == svn_wc_operation_merge &&
      victim_node_kind == svn_node_file &&
      incoming_new_kind == svn_node_file &&
      incoming_change == svn_wc_conflict_action_add &&
      local_change == svn_wc_conflict_reason_obstructed)
    {
      const char *wcroot_abspath;
      const char *description;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));
      description =
        apr_psprintf(scratch_pool,
          _("delete '%s', copy '^/%s@%ld' here, and merge the files"),
          svn_dirent_local_style(
            svn_dirent_skip_ancestor(wcroot_abspath,
                                     conflict->local_abspath),
            scratch_pool),
          incoming_new_repos_relpath, incoming_new_pegrev);

      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_incoming_added_file_replace_and_merge,
        _("Replace and merge"),
        description, resolve_merge_incoming_added_file_replace_and_merge);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming added dir merge' resolution option for a tree
 * conflict. */
static svn_error_t *
configure_option_incoming_added_dir_merge(svn_client_conflict_t *conflict,
                                          svn_client_ctx_t *ctx,
                                          apr_array_header_t *options,
                                          apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (victim_node_kind == svn_node_dir &&
      incoming_new_kind == svn_node_dir &&
      incoming_change == svn_wc_conflict_action_add &&
      (local_change == svn_wc_conflict_reason_added ||
       (operation == svn_wc_operation_merge &&
       local_change == svn_wc_conflict_reason_obstructed)))

    {
      const char *description;
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));
      if (operation == svn_wc_operation_merge)
        description =
          apr_psprintf(scratch_pool, _("merge '^/%s@%ld' into '%s'"),
            incoming_new_repos_relpath, incoming_new_pegrev,
            svn_dirent_local_style(
              svn_dirent_skip_ancestor(wcroot_abspath,
                                       conflict->local_abspath),
              scratch_pool));
      else
        description =
          apr_psprintf(scratch_pool, _("merge local '%s' and '^/%s@%ld'"),
            svn_dirent_local_style(
              svn_dirent_skip_ancestor(wcroot_abspath,
                                       conflict->local_abspath),
              scratch_pool),
            incoming_new_repos_relpath, incoming_new_pegrev);

      add_resolution_option(options, conflict,
                            svn_client_conflict_option_incoming_added_dir_merge,
                            _("Merge the directories"), description,
                            operation == svn_wc_operation_merge
                              ? resolve_merge_incoming_added_dir_merge
                              : resolve_update_incoming_added_dir_merge);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming added dir replace' resolution option for a tree
 * conflict. */
static svn_error_t *
configure_option_incoming_added_dir_replace(svn_client_conflict_t *conflict,
                                            svn_client_ctx_t *ctx,
                                            apr_array_header_t *options,
                                            apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (operation == svn_wc_operation_merge &&
      victim_node_kind == svn_node_dir &&
      incoming_new_kind == svn_node_dir &&
      incoming_change == svn_wc_conflict_action_add &&
      local_change == svn_wc_conflict_reason_obstructed)
    {
      const char *description;
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));
      description =
        apr_psprintf(scratch_pool, _("delete '%s' and copy '^/%s@%ld' here"),
          svn_dirent_local_style(
            svn_dirent_skip_ancestor(wcroot_abspath,
                                     conflict->local_abspath),
            scratch_pool),
          incoming_new_repos_relpath, incoming_new_pegrev);
      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_incoming_added_dir_replace,
        _("Delete my directory and replace it with incoming directory"),
        description, resolve_merge_incoming_added_dir_replace);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming added dir replace and merge' resolution option
 * for a tree conflict. */
static svn_error_t *
configure_option_incoming_added_dir_replace_and_merge(
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_array_header_t *options,
  apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  svn_node_kind_t victim_node_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (operation == svn_wc_operation_merge &&
      victim_node_kind == svn_node_dir &&
      incoming_new_kind == svn_node_dir &&
      incoming_change == svn_wc_conflict_action_add &&
      local_change == svn_wc_conflict_reason_obstructed)
    {
      const char *description;
      const char *wcroot_abspath;

      SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                 conflict->local_abspath, scratch_pool,
                                 scratch_pool));
      description =
        apr_psprintf(scratch_pool,
          _("delete '%s', copy '^/%s@%ld' here, and merge the directories"),
          svn_dirent_local_style(
            svn_dirent_skip_ancestor(wcroot_abspath,
                                     conflict->local_abspath),
            scratch_pool),
          incoming_new_repos_relpath, incoming_new_pegrev);

      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_incoming_added_dir_replace_and_merge,
        _("Replace and merge"),
        description, resolve_merge_incoming_added_dir_replace_and_merge);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming delete ignore' resolution option for a tree conflict. */
static svn_error_t *
configure_option_incoming_delete_ignore(svn_client_conflict_t *conflict,
                                        svn_client_ctx_t *ctx,
                                        apr_array_header_t *options,
                                        apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  if (incoming_change == svn_wc_conflict_action_delete)
    {
      const char *description;
      struct conflict_tree_incoming_delete_details *incoming_details;
      svn_boolean_t is_incoming_move;

      incoming_details = conflict->tree_conflict_incoming_details;
      is_incoming_move = (incoming_details != NULL &&
                          incoming_details->moves != NULL);
      if (local_change == svn_wc_conflict_reason_moved_away ||
          local_change == svn_wc_conflict_reason_edited)
        {
          /* An option which ignores the incoming deletion makes no sense
           * if we know there was a local move and/or an incoming move. */
          if (is_incoming_move)
            return SVN_NO_ERROR;
        }
      else if (local_change == svn_wc_conflict_reason_deleted)
        {
          /* If the local item was deleted and conflict details were fetched
           * and indicate that there was no move, then this is an actual
           * 'delete vs delete' situation. An option which ignores the incoming
           * deletion makes no sense in that case because there is no local
           * node to preserve. */
          if (!is_incoming_move)
            return SVN_NO_ERROR;
        }
      else if (local_change == svn_wc_conflict_reason_missing &&
               operation == svn_wc_operation_merge)
        {
          struct conflict_tree_local_missing_details *local_details;
          svn_boolean_t is_local_move; /* "local" to branch history */

          local_details = conflict->tree_conflict_local_details;
          is_local_move = (local_details != NULL &&
                           local_details->moves != NULL);

          if (!is_incoming_move && !is_local_move)
            return SVN_NO_ERROR;
        }

      description =
        apr_psprintf(scratch_pool, _("ignore the deletion of '^/%s@%ld'"),
          incoming_new_repos_relpath, incoming_new_pegrev);

      add_resolution_option(options, conflict,
                            svn_client_conflict_option_incoming_delete_ignore,
                            _("Ignore incoming deletion"), description,
                            resolve_incoming_delete_ignore);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming delete accept' resolution option for a tree conflict. */
static svn_error_t *
configure_option_incoming_delete_accept(svn_client_conflict_t *conflict,
                                        svn_client_ctx_t *ctx,
                                        apr_array_header_t *options,
                                        apr_pool_t *scratch_pool)
{
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;

  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  if (incoming_change == svn_wc_conflict_action_delete)
    {
      struct conflict_tree_incoming_delete_details *incoming_details;
      svn_boolean_t is_incoming_move;

      incoming_details = conflict->tree_conflict_incoming_details;
      is_incoming_move = (incoming_details != NULL &&
                          incoming_details->moves != NULL);
      if (is_incoming_move &&
          (local_change == svn_wc_conflict_reason_edited ||
          local_change == svn_wc_conflict_reason_moved_away))
        {
          /* An option which accepts the incoming deletion makes no sense
           * if we know there was a local move and/or an incoming move. */
          return SVN_NO_ERROR;
        }
      else
        {
          const char *description;
          const char *wcroot_abspath;
          const char *local_abspath;

          SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                     conflict->local_abspath, scratch_pool,
                                     scratch_pool));
          local_abspath = svn_client_conflict_get_local_abspath(conflict);
          description =
            apr_psprintf(scratch_pool, _("accept the deletion of '%s'"),
              svn_dirent_local_style(svn_dirent_skip_ancestor(wcroot_abspath,
                                                              local_abspath),
                                     scratch_pool));
          add_resolution_option(
            options, conflict,
            svn_client_conflict_option_incoming_delete_accept,
            _("Accept incoming deletion"), description,
            resolve_incoming_delete_accept);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
describe_incoming_move_merge_conflict_option(
  const char **description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  struct conflict_tree_incoming_delete_details *details,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *move_target_wc_abspaths;
  svn_wc_operation_t operation;
  const char *victim_abspath;
  const char *moved_to_abspath;
  const char *wcroot_abspath;

  move_target_wc_abspaths =
    svn_hash_gets(details->wc_move_targets,
                  get_moved_to_repos_relpath(details, scratch_pool));
  moved_to_abspath = APR_ARRAY_IDX(move_target_wc_abspaths,
                                   details->wc_move_target_idx,
                                   const char *);

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                             victim_abspath, scratch_pool,
                             scratch_pool));

  operation = svn_client_conflict_get_operation(conflict);
  if (operation == svn_wc_operation_merge)
    *description =
      apr_psprintf(
        result_pool, _("move '%s' to '%s' and merge"),
        svn_dirent_local_style(svn_dirent_skip_ancestor(wcroot_abspath,
                                                        victim_abspath),
                               scratch_pool),
        svn_dirent_local_style(svn_dirent_skip_ancestor(wcroot_abspath,
                                                        moved_to_abspath),
                               scratch_pool));
  else
    *description =
      apr_psprintf(
        result_pool, _("move and merge local changes from '%s' into '%s'"),
        svn_dirent_local_style(svn_dirent_skip_ancestor(wcroot_abspath,
                                                        victim_abspath),
                               scratch_pool),
        svn_dirent_local_style(svn_dirent_skip_ancestor(wcroot_abspath,
                                                        moved_to_abspath),
                               scratch_pool));

  return SVN_NO_ERROR;
}

/* Configure 'incoming move file merge' resolution option for
 * a tree conflict. */
static svn_error_t *
configure_option_incoming_move_file_merge(svn_client_conflict_t *conflict,
                                          svn_client_ctx_t *ctx,
                                          apr_array_header_t *options,
                                          apr_pool_t *scratch_pool)
{
  svn_node_kind_t victim_node_kind;
  svn_wc_conflict_action_t incoming_change;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  svn_node_kind_t incoming_old_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            &incoming_old_kind, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (victim_node_kind == svn_node_file &&
      incoming_old_kind == svn_node_file &&
      incoming_new_kind == svn_node_none &&
      incoming_change == svn_wc_conflict_action_delete)
    {
      struct conflict_tree_incoming_delete_details *details;
      const char *description;

      details = conflict->tree_conflict_incoming_details;
      if (details == NULL || details->moves == NULL)
        return SVN_NO_ERROR;

      if (apr_hash_count(details->wc_move_targets) == 0)
        return SVN_NO_ERROR;

      SVN_ERR(describe_incoming_move_merge_conflict_option(&description,
                                                           conflict, ctx,
                                                           details,
                                                           scratch_pool,
                                                           scratch_pool));
      add_resolution_option(
        options, conflict,
        svn_client_conflict_option_incoming_move_file_text_merge,
        _("Move and merge"), description,
        resolve_incoming_move_file_text_merge);
    }

  return SVN_NO_ERROR;
}

/* Configure 'incoming move dir merge' resolution option for
 * a tree conflict. */
static svn_error_t *
configure_option_incoming_dir_merge(svn_client_conflict_t *conflict,
                                    svn_client_ctx_t *ctx,
                                    apr_array_header_t *options,
                                    apr_pool_t *scratch_pool)
{
  svn_node_kind_t victim_node_kind;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  const char *incoming_old_repos_relpath;
  svn_revnum_t incoming_old_pegrev;
  svn_node_kind_t incoming_old_kind;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;
  svn_node_kind_t incoming_new_kind;

  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  victim_node_kind = svn_client_conflict_tree_get_victim_node_kind(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
            &incoming_old_repos_relpath, &incoming_old_pegrev,
            &incoming_old_kind, conflict, scratch_pool,
            scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            &incoming_new_kind, conflict, scratch_pool,
            scratch_pool));

  if (victim_node_kind == svn_node_dir &&
      incoming_old_kind == svn_node_dir &&
      incoming_new_kind == svn_node_none &&
      incoming_change == svn_wc_conflict_action_delete &&
      local_change == svn_wc_conflict_reason_edited)
    {
      struct conflict_tree_incoming_delete_details *details;
      const char *description;

      details = conflict->tree_conflict_incoming_details;
      if (details == NULL || details->moves == NULL)
        return SVN_NO_ERROR;

      if (apr_hash_count(details->wc_move_targets) == 0)
        return SVN_NO_ERROR;

      SVN_ERR(describe_incoming_move_merge_conflict_option(&description,
                                                           conflict, ctx,
                                                           details,
                                                           scratch_pool,
                                                           scratch_pool));
      add_resolution_option(options, conflict,
                            svn_client_conflict_option_incoming_move_dir_merge,
                            _("Move and merge"), description,
                            resolve_incoming_move_dir_merge);
    }

  return SVN_NO_ERROR;
}

/* Configure 'local move file merge' resolution option for
 * a tree conflict. */
static svn_error_t *
configure_option_local_move_file_merge(svn_client_conflict_t *conflict,
                                       svn_client_ctx_t *ctx,
                                       apr_array_header_t *options,
                                       apr_pool_t *scratch_pool)
{
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;
  const char *incoming_new_repos_relpath;
  svn_revnum_t incoming_new_pegrev;

  operation = svn_client_conflict_get_operation(conflict);
  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);
  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
            &incoming_new_repos_relpath, &incoming_new_pegrev,
            NULL, conflict, scratch_pool,
            scratch_pool));

  if (operation == svn_wc_operation_merge &&
      incoming_change == svn_wc_conflict_action_edit &&
      local_change == svn_wc_conflict_reason_missing)
    {
      struct conflict_tree_local_missing_details *details;

      details = conflict->tree_conflict_local_details;
      if (details != NULL && details->moves != NULL)
        {
          apr_hash_t *wc_move_targets = apr_hash_make(scratch_pool);
          apr_pool_t *iterpool;
          int i;

          iterpool = svn_pool_create(scratch_pool);
          for (i = 0; i < details->moves->nelts; i++)
            {
              struct repos_move_info *move;

              svn_pool_clear(iterpool);
              move = APR_ARRAY_IDX(details->moves, i, struct repos_move_info *);
              SVN_ERR(follow_move_chains(wc_move_targets, move, ctx,
                                         conflict->local_abspath,
                                         svn_node_file,
                                         incoming_new_repos_relpath,
                                         incoming_new_pegrev,
                                         scratch_pool, iterpool));
            }
          svn_pool_destroy(iterpool);

          if (apr_hash_count(wc_move_targets) > 0)
            {
              apr_array_header_t *move_target_repos_relpaths;
              const svn_sort__item_t *item;
              apr_array_header_t *moved_to_abspaths;
              const char *description;
              const char *wcroot_abspath;

              /* Initialize to the first possible move target. Hopefully,
               * in most cases there will only be one candidate anyway. */
              move_target_repos_relpaths = svn_sort__hash(
                                             wc_move_targets,
                                             svn_sort_compare_items_as_paths,
                                             scratch_pool);
              item = &APR_ARRAY_IDX(move_target_repos_relpaths,
                                    0, svn_sort__item_t);
              moved_to_abspaths = item->value;
              details->moved_to_abspath =
                apr_pstrdup(conflict->pool,
                            APR_ARRAY_IDX(moved_to_abspaths, 0, const char *));

              SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                         conflict->local_abspath,
                                         scratch_pool, scratch_pool));
              description =
                apr_psprintf(
                  scratch_pool, _("apply changes to move destination '%s'"),
                  svn_dirent_local_style(
                    svn_dirent_skip_ancestor(wcroot_abspath,
                                             details->moved_to_abspath),
                    scratch_pool));

              add_resolution_option(
                options, conflict,
                svn_client_conflict_option_local_move_file_text_merge,
                _("Apply to move destination"),
                description, resolve_local_move_file_merge);
            }
          else
            details->moved_to_abspath = NULL;
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_option_get_moved_to_repos_relpath_candidates(
  apr_array_header_t **possible_moved_to_repos_relpaths,
  svn_client_conflict_option_t *option,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict = option->conflict;
  struct conflict_tree_incoming_delete_details *details;
  const char *victim_abspath;
  apr_array_header_t *sorted_repos_relpaths;
  int i;

  SVN_ERR_ASSERT(svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_file_text_merge ||
                 svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_dir_merge);

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->wc_move_targets == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Getting a list of possible move targets "
                               "requires details for tree conflict at '%s' "
                               "to be fetched from the repository first"),
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  /* Return a copy of the repos replath candidate list. */
  sorted_repos_relpaths = svn_sort__hash(details->wc_move_targets,
                                         svn_sort_compare_items_as_paths,
                                         scratch_pool);

  *possible_moved_to_repos_relpaths = apr_array_make(
                                        result_pool,
                                        sorted_repos_relpaths->nelts,
                                        sizeof (const char *));
  for (i = 0; i < sorted_repos_relpaths->nelts; i++)
    {
      svn_sort__item_t item;
      const char *repos_relpath;

      item = APR_ARRAY_IDX(sorted_repos_relpaths, i, svn_sort__item_t);
      repos_relpath = item.key;
      APR_ARRAY_PUSH(*possible_moved_to_repos_relpaths, const char *) =
        apr_pstrdup(result_pool, repos_relpath);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_option_set_moved_to_repos_relpath(
  svn_client_conflict_option_t *option,
  int preferred_move_target_idx,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict = option->conflict;
  struct conflict_tree_incoming_delete_details *details;
  const char *victim_abspath;
  apr_array_header_t *move_target_repos_relpaths;
  svn_sort__item_t item;
  const char *move_target_repos_relpath;
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_file_text_merge ||
                 svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_dir_merge);

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->wc_move_targets == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Setting a move target requires details "
                               "for tree conflict at '%s' to be fetched "
                               "from the repository first"),
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  if (preferred_move_target_idx < 0 ||
      preferred_move_target_idx >= apr_hash_count(details->wc_move_targets))
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Index '%d' is out of bounds of the possible "
                               "move target list for '%s'"),
                            preferred_move_target_idx,
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  /* Translate the index back into a hash table key. */
  move_target_repos_relpaths =
    svn_sort__hash(details->wc_move_targets,
                   svn_sort_compare_items_as_paths,
                   scratch_pool);
  item = APR_ARRAY_IDX(move_target_repos_relpaths, preferred_move_target_idx,
                       svn_sort__item_t);
  move_target_repos_relpath = item.key;
  /* Find our copy of the hash key and remember the user's preference. */
  for (hi = apr_hash_first(scratch_pool, details->wc_move_targets);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *repos_relpath = apr_hash_this_key(hi);

      if (strcmp(move_target_repos_relpath, repos_relpath) == 0)
        {
          details->move_target_repos_relpath = repos_relpath;
          /* Update option description. */
          SVN_ERR(describe_incoming_move_merge_conflict_option(
                    &option->description,
                    conflict, ctx,
                    details,
                    conflict->pool,
                    scratch_pool));

          return SVN_NO_ERROR;
        }
    }

  return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                           _("Repository path '%s' not found in list of "
                             "possible move targets for '%s'"),
                           move_target_repos_relpath,
                           svn_dirent_local_style(victim_abspath,
                                                  scratch_pool));
}

svn_error_t *
svn_client_conflict_option_get_moved_to_abspath_candidates(
  apr_array_header_t **possible_moved_to_abspaths,
  svn_client_conflict_option_t *option,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict = option->conflict;
  struct conflict_tree_incoming_delete_details *details;
  const char *victim_abspath;
  apr_array_header_t *move_target_wc_abspaths;
  int i;

  SVN_ERR_ASSERT(svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_file_text_merge ||
                 svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_dir_merge);

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->wc_move_targets == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Getting a list of possible move targets "
                               "requires details for tree conflict at '%s' "
                               "to be fetched from the repository first"),
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  move_target_wc_abspaths =
    svn_hash_gets(details->wc_move_targets,
                  get_moved_to_repos_relpath(details, scratch_pool));

  /* Return a copy of the option's move target candidate list. */
  *possible_moved_to_abspaths =
    apr_array_make(result_pool, move_target_wc_abspaths->nelts,
                   sizeof (const char *));
  for (i = 0; i < move_target_wc_abspaths->nelts; i++)
    {
      const char *moved_to_abspath;

      moved_to_abspath = APR_ARRAY_IDX(move_target_wc_abspaths, i,
                                       const char *);
      APR_ARRAY_PUSH(*possible_moved_to_abspaths, const char *) =
        apr_pstrdup(result_pool, moved_to_abspath);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_option_set_moved_to_abspath(
  svn_client_conflict_option_t *option,
  int preferred_move_target_idx,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict = option->conflict;
  struct conflict_tree_incoming_delete_details *details;
  const char *victim_abspath;
  apr_array_header_t *move_target_wc_abspaths;

  SVN_ERR_ASSERT(svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_file_text_merge ||
                 svn_client_conflict_option_get_id(option) ==
                 svn_client_conflict_option_incoming_move_dir_merge);

  victim_abspath = svn_client_conflict_get_local_abspath(conflict);
  details = conflict->tree_conflict_incoming_details;
  if (details == NULL || details->wc_move_targets == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Setting a move target requires details "
                               "for tree conflict at '%s' to be fetched "
                               "from the repository first"),
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  move_target_wc_abspaths =
    svn_hash_gets(details->wc_move_targets,
                  get_moved_to_repos_relpath(details, scratch_pool));

  if (preferred_move_target_idx < 0 ||
      preferred_move_target_idx > move_target_wc_abspaths->nelts)
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Index '%d' is out of bounds of the possible "
                               "move target list for '%s'"),
                            preferred_move_target_idx,
                            svn_dirent_local_style(victim_abspath,
                                                   scratch_pool));

  /* Record the user's preference. */
  details->wc_move_target_idx = preferred_move_target_idx;

  /* Update option description. */
  SVN_ERR(describe_incoming_move_merge_conflict_option(&option->description,
                                                       conflict, ctx,
                                                       details,
                                                       conflict->pool,
                                                       scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_tree_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_tree_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, 2,
                            sizeof(svn_client_conflict_option_t *));

  /* Add postpone option. */
  add_resolution_option(*options, conflict,
                        svn_client_conflict_option_postpone,
                        _("Postpone"),
                        _("skip this conflict and leave it unresolved"),
                        resolve_postpone);

  /* Add an option which marks the conflict resolved. */
  SVN_ERR(configure_option_accept_current_wc_state(conflict, *options));

  /* Configure options which offer automatic resolution. */
  SVN_ERR(configure_option_update_move_destination(conflict, *options));
  SVN_ERR(configure_option_update_raise_moved_away_children(conflict,
                                                            *options));
  SVN_ERR(configure_option_incoming_add_ignore(conflict, ctx, *options,
                                               scratch_pool));
  SVN_ERR(configure_option_incoming_added_file_text_merge(conflict, ctx,
                                                          *options,
                                                          scratch_pool));
  SVN_ERR(configure_option_incoming_added_file_replace_and_merge(conflict,
                                                                 ctx,
                                                                 *options,
                                                                 scratch_pool));
  SVN_ERR(configure_option_incoming_added_dir_merge(conflict, ctx,
                                                    *options,
                                                    scratch_pool));
  SVN_ERR(configure_option_incoming_added_dir_replace(conflict, ctx,
                                                      *options,
                                                      scratch_pool));
  SVN_ERR(configure_option_incoming_added_dir_replace_and_merge(conflict,
                                                                ctx,
                                                                *options,
                                                                scratch_pool));
  SVN_ERR(configure_option_incoming_delete_ignore(conflict, ctx, *options,
                                                  scratch_pool));
  SVN_ERR(configure_option_incoming_delete_accept(conflict, ctx, *options,
                                                  scratch_pool));
  SVN_ERR(configure_option_incoming_move_file_merge(conflict, ctx, *options,
                                                    scratch_pool));
  SVN_ERR(configure_option_incoming_dir_merge(conflict, ctx, *options,
                                              scratch_pool));
  SVN_ERR(configure_option_local_move_file_merge(conflict, ctx, *options,
                                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* Swallow authz failures and return SVN_NO_ERROR in that case.
 * Otherwise, return ERR unchanged. */
static svn_error_t *
ignore_authz_failures(svn_error_t *err)
{
  if (err && (   svn_error_find_cause(err, SVN_ERR_AUTHZ_UNREADABLE)
              || svn_error_find_cause(err, SVN_ERR_RA_NOT_AUTHORIZED)
              || svn_error_find_cause(err, SVN_ERR_RA_DAV_FORBIDDEN)))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }

  return err;
}

svn_error_t *
svn_client_conflict_tree_get_details(svn_client_conflict_t *conflict,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_tree_conflict(conflict, scratch_pool));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 svn_client_conflict_get_local_abspath(conflict),
                 svn_wc_notify_begin_search_tree_conflict_details,
                 scratch_pool),
      ctx->notify_func2(ctx->notify_baton2, notify,
                                  scratch_pool);
    }

  /* Collecting conflict details may fail due to insufficient access rights.
   * This is not a failure but simply restricts our future options. */
  if (conflict->tree_conflict_get_incoming_details_func)
    SVN_ERR(ignore_authz_failures(
      conflict->tree_conflict_get_incoming_details_func(conflict, ctx,
                                                        scratch_pool)));


  if (conflict->tree_conflict_get_local_details_func)
    SVN_ERR(ignore_authz_failures(
      conflict->tree_conflict_get_local_details_func(conflict, ctx,
                                                    scratch_pool)));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(
                 svn_client_conflict_get_local_abspath(conflict),
                 svn_wc_notify_end_search_tree_conflict_details,
                 scratch_pool),
      ctx->notify_func2(ctx->notify_baton2, notify,
                                  scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_option_get_id(svn_client_conflict_option_t *option)
{
  return option->id;
}

const char *
svn_client_conflict_option_get_label(svn_client_conflict_option_t *option,
                                     apr_pool_t *result_pool)
{
  return apr_pstrdup(result_pool, option->label);
}

const char *
svn_client_conflict_option_get_description(svn_client_conflict_option_t *option,
                                           apr_pool_t *result_pool)
{
  return apr_pstrdup(result_pool, option->description);
}

svn_client_conflict_option_id_t
svn_client_conflict_get_recommended_option_id(svn_client_conflict_t *conflict)
{
  return conflict->recommended_option_id;
}
                                    
svn_error_t *
svn_client_conflict_text_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_text_conflict(conflict, scratch_pool));
  SVN_ERR(option->do_resolve_func(option, conflict, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_t *
svn_client_conflict_option_find_by_id(apr_array_header_t *options,
                                      svn_client_conflict_option_id_t option_id)
{
  int i;

  for (i = 0; i < options->nelts; i++)
    {
      svn_client_conflict_option_t *this_option;
      svn_client_conflict_option_id_t this_option_id;
      
      this_option = APR_ARRAY_IDX(options, i, svn_client_conflict_option_t *);
      this_option_id = svn_client_conflict_option_get_id(this_option);

      if (this_option_id == option_id)
        return this_option;
    }

  return NULL;
}

svn_error_t *
svn_client_conflict_text_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_text_get_resolution_options(
            &resolution_options, conflict, ctx,
            scratch_pool, scratch_pool));
  option = svn_client_conflict_option_find_by_id(resolution_options,
                                                 option_id);
  if (option == NULL)
    return svn_error_createf(SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE,
                             NULL,
                             _("Inapplicable conflict resolution option "
                               "given for conflicted path '%s'"),
                             svn_dirent_local_style(conflict->local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_client_conflict_text_resolve(conflict, option, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_text_get_resolution(svn_client_conflict_t *conflict)
{
  return conflict->resolution_text;
}

svn_error_t *
svn_client_conflict_prop_resolve(svn_client_conflict_t *conflict,
                                 const char *propname,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_prop_conflict(conflict, scratch_pool));
  option->type_data.prop.propname = propname;
  SVN_ERR(option->do_resolve_func(option, conflict, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_prop_resolve_by_id(
  svn_client_conflict_t *conflict,
  const char *propname,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(
            &resolution_options, conflict, ctx,
            scratch_pool, scratch_pool));
  option = svn_client_conflict_option_find_by_id(resolution_options,
                                                 option_id);
  if (option == NULL)
    return svn_error_createf(SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE,
                             NULL,
                             _("Inapplicable conflict resolution option "
                               "given for conflicted path '%s'"),
                             svn_dirent_local_style(conflict->local_abspath,
                                                    scratch_pool));
  SVN_ERR(svn_client_conflict_prop_resolve(conflict, propname, option, ctx,
                                           scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_prop_get_resolution(svn_client_conflict_t *conflict,
                                        const char *propname)
{
  svn_client_conflict_option_t *option;

  option = svn_hash_gets(conflict->resolved_props, propname);
  if (option == NULL)
    return svn_client_conflict_option_unspecified;

  return svn_client_conflict_option_get_id(option);
}

svn_error_t *
svn_client_conflict_tree_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_tree_conflict(conflict, scratch_pool));
  SVN_ERR(option->do_resolve_func(option, conflict, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_tree_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_tree_get_resolution_options(
            &resolution_options, conflict, ctx,
            scratch_pool, scratch_pool));
  option = svn_client_conflict_option_find_by_id(resolution_options,
                                                 option_id);
  if (option == NULL)
    return svn_error_createf(SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE,
                             NULL,
                             _("Inapplicable conflict resolution option "
                               "given for conflicted path '%s'"),
                             svn_dirent_local_style(conflict->local_abspath,
                                                    scratch_pool));
  SVN_ERR(svn_client_conflict_tree_resolve(conflict, option, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_tree_get_resolution(svn_client_conflict_t *conflict)
{
  return conflict->resolution_tree;
}

/* Return the legacy conflict descriptor which is wrapped by CONFLICT. */
static const svn_wc_conflict_description2_t *
get_conflict_desc2_t(svn_client_conflict_t *conflict)
{
  if (conflict->legacy_text_conflict)
    return conflict->legacy_text_conflict;

  if (conflict->legacy_tree_conflict)
    return conflict->legacy_tree_conflict;

  if (conflict->prop_conflicts && conflict->legacy_prop_conflict_propname)
    return svn_hash_gets(conflict->prop_conflicts,
                         conflict->legacy_prop_conflict_propname);

  return NULL;
}

svn_error_t *
svn_client_conflict_get_conflicted(svn_boolean_t *text_conflicted,
                                   apr_array_header_t **props_conflicted,
                                   svn_boolean_t *tree_conflicted,
                                   svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if (text_conflicted)
    *text_conflicted = (conflict->legacy_text_conflict != NULL);

  if (props_conflicted)
    {
      if (conflict->prop_conflicts)
        SVN_ERR(svn_hash_keys(props_conflicted, conflict->prop_conflicts,
                              result_pool));
      else
        *props_conflicted = apr_array_make(result_pool, 0,
                                           sizeof(const char*));
    }

  if (tree_conflicted)
    *tree_conflicted = (conflict->legacy_tree_conflict != NULL);

  return SVN_NO_ERROR;
}

const char *
svn_client_conflict_get_local_abspath(svn_client_conflict_t *conflict)
{
  return conflict->local_abspath;
}

svn_wc_operation_t
svn_client_conflict_get_operation(svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->operation;
}

svn_wc_conflict_action_t
svn_client_conflict_get_incoming_change(svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->action;
}

svn_wc_conflict_reason_t
svn_client_conflict_get_local_change(svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->reason;
}

svn_error_t *
svn_client_conflict_get_repos_info(const char **repos_root_url,
                                   const char **repos_uuid,
                                   svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if (repos_root_url)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *repos_root_url =
          get_conflict_desc2_t(conflict)->src_left_version->repos_url;
      else if (get_conflict_desc2_t(conflict)->src_right_version)
        *repos_root_url =
          get_conflict_desc2_t(conflict)->src_right_version->repos_url;
      else
        *repos_root_url = NULL;
    }

  if (repos_uuid)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *repos_uuid =
          get_conflict_desc2_t(conflict)->src_left_version->repos_uuid;
      else if (get_conflict_desc2_t(conflict)->src_right_version)
        *repos_uuid =
          get_conflict_desc2_t(conflict)->src_right_version->repos_uuid;
      else
        *repos_uuid = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_old_repos_location(
  const char **incoming_old_repos_relpath,
  svn_revnum_t *incoming_old_pegrev,
  svn_node_kind_t *incoming_old_node_kind,
  svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_old_repos_relpath)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_repos_relpath =
          get_conflict_desc2_t(conflict)->src_left_version->path_in_repos;
      else
        *incoming_old_repos_relpath = NULL;
    }

  if (incoming_old_pegrev)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_pegrev =
          get_conflict_desc2_t(conflict)->src_left_version->peg_rev;
      else
        *incoming_old_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_old_node_kind)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_node_kind =
          get_conflict_desc2_t(conflict)->src_left_version->node_kind;
      else
        *incoming_old_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_new_repos_location(
  const char **incoming_new_repos_relpath,
  svn_revnum_t *incoming_new_pegrev,
  svn_node_kind_t *incoming_new_node_kind,
  svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_new_repos_relpath)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_repos_relpath =
          get_conflict_desc2_t(conflict)->src_right_version->path_in_repos;
      else
        *incoming_new_repos_relpath = NULL;
    }

  if (incoming_new_pegrev)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_pegrev =
          get_conflict_desc2_t(conflict)->src_right_version->peg_rev;
      else
        *incoming_new_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_new_node_kind)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_node_kind =
          get_conflict_desc2_t(conflict)->src_right_version->node_kind;
      else
        *incoming_new_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_node_kind_t
svn_client_conflict_tree_get_victim_node_kind(svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(assert_tree_conflict(conflict, conflict->pool)
                           == SVN_NO_ERROR);

  return get_conflict_desc2_t(conflict)->node_kind;
}

svn_error_t *
svn_client_conflict_prop_get_propvals(const svn_string_t **base_propval,
                                      const svn_string_t **working_propval,
                                      const svn_string_t **incoming_old_propval,
                                      const svn_string_t **incoming_new_propval,
                                      svn_client_conflict_t *conflict,
                                      const char *propname,
                                      apr_pool_t *result_pool)
{
  const svn_wc_conflict_description2_t *desc;

  SVN_ERR(assert_prop_conflict(conflict, conflict->pool));

  desc = svn_hash_gets(conflict->prop_conflicts, propname);
  if (desc == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Property '%s' is not in conflict."), propname);

  if (base_propval)
    *base_propval =
      svn_string_dup(desc->prop_value_base, result_pool);

  if (working_propval)
    *working_propval =
      svn_string_dup(desc->prop_value_working, result_pool);

  if (incoming_old_propval)
    *incoming_old_propval =
      svn_string_dup(desc->prop_value_incoming_old, result_pool);

  if (incoming_new_propval)
    *incoming_new_propval =
      svn_string_dup(desc->prop_value_incoming_new, result_pool);

  return SVN_NO_ERROR;
}

const char *
svn_client_conflict_prop_get_reject_abspath(svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(assert_prop_conflict(conflict, conflict->pool)
                           == SVN_NO_ERROR);

  /* svn_wc_conflict_description2_t stores this path in 'their_abspath' */
  return get_conflict_desc2_t(conflict)->their_abspath;
}

const char *
svn_client_conflict_text_get_mime_type(svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(assert_text_conflict(conflict, conflict->pool)
                           == SVN_NO_ERROR);

  return get_conflict_desc2_t(conflict)->mime_type;
}

svn_error_t *
svn_client_conflict_text_get_contents(const char **base_abspath,
                                      const char **working_abspath,
                                      const char **incoming_old_abspath,
                                      const char **incoming_new_abspath,
                                      svn_client_conflict_t *conflict,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_text_conflict(conflict, scratch_pool));

  if (base_abspath)
    {
      if (svn_client_conflict_get_operation(conflict) ==
          svn_wc_operation_merge)
        *base_abspath = NULL; /* ### WC base contents not available yet */
      else /* update/switch */
        *base_abspath = get_conflict_desc2_t(conflict)->base_abspath;
    }

  if (working_abspath)
    *working_abspath = get_conflict_desc2_t(conflict)->my_abspath;

  if (incoming_old_abspath)
    *incoming_old_abspath = get_conflict_desc2_t(conflict)->base_abspath;

  if (incoming_new_abspath)
    *incoming_new_abspath = get_conflict_desc2_t(conflict)->their_abspath;

  return SVN_NO_ERROR;
}

/* Set up type-specific data for a new conflict object. */
static svn_error_t *
conflict_type_specific_setup(svn_client_conflict_t *conflict,
                             apr_pool_t *scratch_pool)
{
  svn_boolean_t tree_conflicted;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_reason_t local_change;

  /* For now, we only deal with tree conflicts here. */
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, scratch_pool,
                                             scratch_pool));
  if (!tree_conflicted)
    return SVN_NO_ERROR;

  /* Set a default description function. */
  conflict->tree_conflict_get_incoming_description_func =
    conflict_tree_get_incoming_description_generic;
  conflict->tree_conflict_get_local_description_func =
    conflict_tree_get_local_description_generic;

  incoming_change = svn_client_conflict_get_incoming_change(conflict);
  local_change = svn_client_conflict_get_local_change(conflict);

  /* Set type-specific description and details functions. */
  if (incoming_change == svn_wc_conflict_action_delete ||
      incoming_change == svn_wc_conflict_action_replace)
    {
      conflict->tree_conflict_get_incoming_description_func =
        conflict_tree_get_description_incoming_delete;
      conflict->tree_conflict_get_incoming_details_func =
        conflict_tree_get_details_incoming_delete;
    }
  else if (incoming_change == svn_wc_conflict_action_add)
    {
      conflict->tree_conflict_get_incoming_description_func =
        conflict_tree_get_description_incoming_add;
      conflict->tree_conflict_get_incoming_details_func =
        conflict_tree_get_details_incoming_add;
    }
  else if (incoming_change == svn_wc_conflict_action_edit)
    {
      conflict->tree_conflict_get_incoming_description_func =
        conflict_tree_get_description_incoming_edit;
      conflict->tree_conflict_get_incoming_details_func =
        conflict_tree_get_details_incoming_edit;
    }

  if (local_change == svn_wc_conflict_reason_missing)
    {
      conflict->tree_conflict_get_local_description_func =
        conflict_tree_get_description_local_missing;
      conflict->tree_conflict_get_local_details_func =
        conflict_tree_get_details_local_missing;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get(svn_client_conflict_t **conflict,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const apr_array_header_t *descs;
  int i;

  *conflict = apr_pcalloc(result_pool, sizeof(**conflict));

  (*conflict)->local_abspath = apr_pstrdup(result_pool, local_abspath);
  (*conflict)->resolution_text = svn_client_conflict_option_unspecified;
  (*conflict)->resolution_tree = svn_client_conflict_option_unspecified;
  (*conflict)->resolved_props = apr_hash_make(result_pool);
  (*conflict)->recommended_option_id = svn_client_conflict_option_unspecified;
  (*conflict)->pool = result_pool;

  /* Add all legacy conflict descriptors we can find. Eventually, this code
   * path should stop relying on svn_wc_conflict_description2_t entirely. */
  SVN_ERR(svn_wc__read_conflict_descriptions2_t(&descs, ctx->wc_ctx,
                                                local_abspath,
                                                result_pool, scratch_pool));
  for (i = 0; i < descs->nelts; i++)
    {
      const svn_wc_conflict_description2_t *desc;

      desc = APR_ARRAY_IDX(descs, i, const svn_wc_conflict_description2_t *);
      add_legacy_desc_to_conflict(desc, *conflict, result_pool);
    }

  SVN_ERR(conflict_type_specific_setup(*conflict, scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton for conflict_status_walker */
struct conflict_status_walker_baton
{
  svn_client_conflict_walk_func_t conflict_walk_func;
  void *conflict_walk_func_baton;
  svn_client_ctx_t *ctx;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_boolean_t resolved_a_tree_conflict;
  apr_hash_t *unresolved_tree_conflicts;
};

/* Implements svn_wc_notify_func2_t to collect new conflicts caused by
   resolving a tree conflict. */
static void
tree_conflict_collector(void *baton,
                        const svn_wc_notify_t *notify,
                        apr_pool_t *pool)
{
  struct conflict_status_walker_baton *cswb = baton;

  if (cswb->notify_func)
    cswb->notify_func(cswb->notify_baton, notify, pool);

  if (cswb->unresolved_tree_conflicts
      && (notify->action == svn_wc_notify_tree_conflict
          || notify->prop_state == svn_wc_notify_state_conflicted
          || notify->content_state == svn_wc_notify_state_conflicted))
    {
      if (!svn_hash_gets(cswb->unresolved_tree_conflicts, notify->path))
        {
          const char *tc_abspath;
          apr_pool_t *hash_pool;
 
          hash_pool = apr_hash_pool_get(cswb->unresolved_tree_conflicts);
          tc_abspath = apr_pstrdup(hash_pool, notify->path);
          svn_hash_sets(cswb->unresolved_tree_conflicts, tc_abspath, "");
        }
    }
}

/* 
 * Record a tree conflict resolution failure due to error condition ERR
 * in the RESOLVE_LATER hash table. If the hash table is not available
 * (meaning the caller does not wish to retry resolution later), or if
 * the error condition does not indicate circumstances where another
 * existing tree conflict is blocking the resolution attempt, then
 * return the error ERR itself.
 */
static svn_error_t *
handle_tree_conflict_resolution_failure(const char *local_abspath,
                                        svn_error_t *err,
                                        apr_hash_t *unresolved_tree_conflicts)
{
  const char *tc_abspath;

  if (!unresolved_tree_conflicts
      || (err->apr_err != SVN_ERR_WC_OBSTRUCTED_UPDATE
          && err->apr_err != SVN_ERR_WC_FOUND_CONFLICT))
    return svn_error_trace(err); /* Give up. Do not retry resolution later. */

  svn_error_clear(err);
  tc_abspath = apr_pstrdup(apr_hash_pool_get(unresolved_tree_conflicts),
                           local_abspath);

  svn_hash_sets(unresolved_tree_conflicts, tc_abspath, "");

  return SVN_NO_ERROR; /* Caller may retry after resolving other conflicts. */
}

/* Implements svn_wc_status4_t to walk all conflicts to resolve.
 */
static svn_error_t *
conflict_status_walker(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton *cswb = baton;
  svn_client_conflict_t *conflict;
  svn_error_t *err;
  svn_boolean_t tree_conflicted;

  if (!status->conflicted)
    return SVN_NO_ERROR;

  SVN_ERR(svn_client_conflict_get(&conflict, local_abspath, cswb->ctx,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, scratch_pool,
                                             scratch_pool));
  err = cswb->conflict_walk_func(cswb->conflict_walk_func_baton,
                                 conflict, scratch_pool);
  if (err)
    {
      if (tree_conflicted)
        SVN_ERR(handle_tree_conflict_resolution_failure(
                  local_abspath, err, cswb->unresolved_tree_conflicts));

      else
        return svn_error_trace(err);
    }

  if (tree_conflicted)
    {
      svn_client_conflict_option_id_t resolution;

      resolution = svn_client_conflict_tree_get_resolution(conflict);
      if (resolution != svn_client_conflict_option_unspecified &&
          resolution != svn_client_conflict_option_postpone)
        cswb->resolved_a_tree_conflict = TRUE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_walk(const char *local_abspath,
                         svn_depth_t depth,
                         svn_client_conflict_walk_func_t conflict_walk_func,
                         void *conflict_walk_func_baton,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton cswb;
  apr_pool_t *iterpool = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  cswb.conflict_walk_func = conflict_walk_func;
  cswb.conflict_walk_func_baton = conflict_walk_func_baton;
  cswb.ctx = ctx;
  cswb.resolved_a_tree_conflict = FALSE;
  cswb.unresolved_tree_conflicts = apr_hash_make(scratch_pool);

  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(
                        local_abspath,
                        svn_wc_notify_conflict_resolver_starting,
                        scratch_pool),
                      scratch_pool);

  /* Swap in our notify_func wrapper. We must revert this before returning! */
  cswb.notify_func = ctx->notify_func2;
  cswb.notify_baton = ctx->notify_baton2;
  ctx->notify_func2 = tree_conflict_collector;
  ctx->notify_baton2 = &cswb;

  err = svn_wc_walk_status(ctx->wc_ctx,
                           local_abspath,
                           depth,
                           FALSE /* get_all */,
                           FALSE /* no_ignore */,
                           TRUE /* ignore_text_mods */,
                           NULL /* ignore_patterns */,
                           conflict_status_walker, &cswb,
                           ctx->cancel_func, ctx->cancel_baton,
                           scratch_pool);

  /* If we got new tree conflicts (or delayed conflicts) during the initial
     walk, we now walk them one by one as closure. */
  while (!err && cswb.unresolved_tree_conflicts &&
         apr_hash_count(cswb.unresolved_tree_conflicts))
    {
      apr_hash_index_t *hi;
      svn_wc_status3_t *status = NULL;
      const char *tc_abspath = NULL;

      if (iterpool)
        svn_pool_clear(iterpool);
      else
        iterpool = svn_pool_create(scratch_pool);

      hi = apr_hash_first(scratch_pool, cswb.unresolved_tree_conflicts);
      cswb.unresolved_tree_conflicts = apr_hash_make(scratch_pool);
      cswb.resolved_a_tree_conflict = FALSE;

      for (; hi && !err; hi = apr_hash_next(hi))
        {
          svn_pool_clear(iterpool);

          tc_abspath = apr_hash_this_key(hi);

          if (ctx->cancel_func)
            {
              err = ctx->cancel_func(ctx->cancel_baton);
              if (err)
                break;
            }

          err = svn_error_trace(svn_wc_status3(&status, ctx->wc_ctx,
                                               tc_abspath,
                                               iterpool, iterpool));
          if (err)
            break;

          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));
          if (err)
            break;
        }
  
      if (!err && !cswb.resolved_a_tree_conflict && tc_abspath &&
          apr_hash_count(cswb.unresolved_tree_conflicts))
        {
          /* None of the remaining conflicts got resolved, without any error.
           * Disable the 'unresolved_tree_conflicts' cache and try again. */
          cswb.unresolved_tree_conflicts = NULL;

          /* Run the most recent resolve operation again.
           * We still have status and tc_abspath for that one.
           * This should uncover the error which prevents resolution. */
          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));
          SVN_ERR_ASSERT(err != NULL);

          err = svn_error_createf(
                    SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                    _("Unable to resolve pending conflict on '%s'"),
                    svn_dirent_local_style(tc_abspath, scratch_pool));
          break;
        }
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  ctx->notify_func2 = cswb.notify_func;
  ctx->notify_baton2 = cswb.notify_baton;

  if (!err && ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                          svn_wc_notify_conflict_resolver_done,
                                          scratch_pool),
                      scratch_pool);

  return svn_error_trace(err);
}
