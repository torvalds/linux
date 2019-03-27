/*
 * wc_db_update_move.c :  updating moves during tree-conflict resolution
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

/* This implements editors and edit drivers which are used to resolve
 * "incoming edit, local move-away", "incoming move, local edit", and
 * "incoming add, local add" tree conflicts resulting from an update
 * (or switch).
 *
 * Our goal is to be able to resolve conflicts such that the end result
 * is just the same as if the user had run the update *before* the local
 * (or incoming) move or local add.
 *
 * -- Updating local moves --
 *
 * When an update (or switch) produces incoming changes for a locally
 * moved-away subtree, it updates the base nodes of the moved-away tree
 * and flags a tree-conflict on the moved-away root node.
 * This editor transfers these changes from the moved-away part of the
 * working copy to the corresponding moved-here part of the working copy.
 *
 * Both the driver and receiver components of the editor are implemented
 * in this file.
 *
 * The driver sees two NODES trees: the move source tree and the move
 * destination tree.  When the move is initially made these trees are
 * equivalent, the destination is a copy of the source.  The source is
 * a single-op-depth, single-revision, deleted layer [1] and the
 * destination has an equivalent single-op-depth, single-revision
 * layer. The destination may have additional higher op-depths
 * representing adds, deletes, moves within the move destination. [2]
 *
 * After the initial move an update has modified the NODES in the move
 * source and may have introduced a tree-conflict since the source and
 * destination trees are no longer equivalent.  The source is a
 * different revision and may have text, property and tree changes
 * compared to the destination.  The driver will compare the two NODES
 * trees and drive an editor to change the destination tree so that it
 * once again matches the source tree.  Changes made to the
 * destination NODES tree to achieve this match will be merged into
 * the working files/directories.
 *
 * The whole drive occurs as one single wc.db transaction.  At the end
 * of the transaction the destination NODES table should have a layer
 * that is equivalent to the source NODES layer, there should be
 * workqueue items to make any required changes to working
 * files/directories in the move destination, and there should be
 * tree-conflicts in the move destination where it was not possible to
 * update the working files/directories.
 *
 * [1] The move source tree is single-revision because we currently do
 *     not allow a mixed-rev move, and therefore it is single op-depth
 *     regardless whether it is a base layer or a nested move.
 *
 * [2] The source tree also may have additional higher op-depths,
 *     representing a replacement, but this editor only reads from the
 *     single-op-depth layer of it, and makes no changes of any kind
 *     within the source tree.
 *
 * -- Updating incoming moves --
 *
 * When an update (or switch) produces an incoming move, it deletes the
 * moved node at the old location from the BASE tree and adds a node at
 * the new location to the BASE tree. If the old location contains local
 * changes, a tree conflict is raised, and the former BASE tree which
 * the local changes were based on (the tree conflict victim) is re-added
 * as a copy which contains these local changes.
 *
 * The driver sees two NODES trees: The op-root of the copy, and the
 * WORKING layer on top of this copy which represents the local changes.
 * The driver will compare the two NODES trees and drive an editor to
 * change the move destination's WORKING tree so that it now contains
 * the local changes seen in the copy of the victim's tree.
 *
 * We require that no local changes exist at the destination, in order
 * to avoid tree conflicts where the "incoming" and "local" change both
 * originated in the working copy, because the resolver code cannot handle
 * such tree conflicts at present.
 * 
 * The whole drive occurs as one single wc.db transaction.  At the end
 * of the transaction the destination NODES table should have a WORKING
 * layer that is equivalent to the WORKING layer found in the copied victim
 * tree, and there should be workqueue items to make any required changes
 * to working files/directories in the move destination, and there should be
 * tree-conflicts in the move destination where it was not possible to
 * update the working files/directories.
 *
 * -- Updating local adds --
 *
 * When an update (or switch) adds a directory tree it creates corresponding
 * nodes in the BASE tree. Any existing locally added nodes are bumped to a
 * higher layer with the top-most locally added directory as op-root.
 * In-between, the update inserts a base-deleted layer, i.e. it schedules the
 * directory in the BASE tree for removal upon the next commit, to be replaced
 * by the locally added directory.
 *
 * The driver sees two NODES trees: The BASE layer, and the WORKING layer
 * which represents the locally added tree.
 * The driver will compare the two NODES trees and drive an editor to
 * merge WORKING tree nodes with the nodes in the BASE tree.
 *
 * The whole drive occurs as one single wc.db transaction.
 * Directories which exist in both trees become part of the BASE tree, with
 * properties merged.
 * Files which exist in both trees are merged (there is no common ancestor,
 * so the common ancestor in this merge is the empty file).
 * Files and directories which exist only in the WORKING layer become
 * local-add op-roots of their own.
 * Mismatching node kinds produce new 'incoming add vs local add upon update'
 * tree conflicts which must be resolved individually later on.
 */

#define SVN_WC__I_AM_WC_DB

#include <assert.h>

#include "svn_checksum.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "private/svn_skel.h"
#include "private/svn_sorts_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"

#include "wc.h"
#include "props.h"
#include "wc_db_private.h"
#include "wc-queries.h"
#include "conflicts.h"
#include "workqueue.h"
#include "token-map.h"

/* Helper functions */
/* Return the absolute path, in local path style, of LOCAL_RELPATH
   in WCROOT.  */
static const char *
path_for_error_message(const svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool)
{
  const char *local_abspath
    = svn_dirent_join(wcroot->abspath, local_relpath, result_pool);

  return svn_dirent_local_style(local_abspath, result_pool);
}

/* Ensure that there is a working copy lock for LOCAL_RELPATH in WCROOT */
static svn_error_t *
verify_write_lock(svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  svn_boolean_t locked;

  SVN_ERR(svn_wc__db_wclock_owns_lock_internal(&locked, wcroot, local_relpath,
                                               FALSE, scratch_pool));
  if (!locked)
    {
      return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                               _("No write-lock in '%s'"),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* In our merge conflicts we record the move_op_src path, which is essentially
   the depth at which what was moved is marked deleted. The problem is that
   this depth is not guaranteed to be stable, because somebody might just
   remove another ancestor, or revert one.

   To work around this problem we locate the layer below this path, and use
   that to pinpoint whatever is moved.

   For a path SRC_RELPATH that was deleted by an operation rooted at
   DELETE_OP_DEPTH find the op-depth at which the node was originally added.
   */
static svn_error_t *
find_src_op_depth(int *src_op_depth,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *src_relpath,
                  int delete_op_depth,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_HIGHEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                            src_relpath, delete_op_depth));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    *src_op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                              _("'%s' is not deleted"),
                              path_for_error_message(wcroot, src_relpath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

/*
 * Receiver code.
 *
 * The receiver is an editor that, when driven with a certain change, will
 * merge the edits into the working/actual state of the move destination
 * at MOVE_ROOT_DST_RELPATH (in struct tc_editor_baton), perhaps raising
 * conflicts if necessary.
 *
 * The receiver should not need to refer directly to the move source, as
 * the driver should provide all relevant information about the change to
 * be made at the move destination.
 */

typedef struct update_move_baton_t {
  svn_wc__db_t *db;
  svn_wc__db_wcroot_t *wcroot;

  int src_op_depth;
  int dst_op_depth;

  svn_wc_operation_t operation;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;

  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} update_move_baton_t;

/* Per node flags for tree conflict collection */
typedef struct node_move_baton_t
{
  svn_boolean_t skip;
  svn_boolean_t shadowed;
  svn_boolean_t edited;

  const char *src_relpath;
  const char *dst_relpath;

  update_move_baton_t *umb;
  struct node_move_baton_t *pb;
} node_move_baton_t;

/*
 * Notifications are delayed until the entire update-move transaction
 * completes. These functions provide the necessary support by storing
 * notification information in a temporary db table (the "update_move_list")
 * and spooling notifications out of that table after the transaction.
 */

/* Add an entry to the notification list, and at the same time install
   a conflict and/or work items. */
static svn_error_t *
update_move_list_add(svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     svn_wc__db_t *db,
                     svn_wc_notify_action_t action,
                     svn_node_kind_t kind,
                     svn_wc_notify_state_t content_state,
                     svn_wc_notify_state_t prop_state,
                     svn_skel_t *conflict,
                     svn_skel_t *work_item,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  if (conflict)
    {
      svn_boolean_t tree_conflict;

      SVN_ERR(svn_wc__conflict_read_info(NULL, NULL, NULL, NULL,
                                         &tree_conflict,
                                         db, wcroot->abspath, conflict,
                                         scratch_pool, scratch_pool));
      if (tree_conflict)
        {
          action = svn_wc_notify_tree_conflict;
          content_state = svn_wc_notify_state_inapplicable;
          prop_state = svn_wc_notify_state_inapplicable;
        }
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_INSERT_UPDATE_MOVE_LIST));
  SVN_ERR(svn_sqlite__bindf(stmt, "sdtdd", local_relpath,
                            action, kind_map_none, kind,
                            content_state, prop_state));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath, conflict,
                                              scratch_pool));

  if (work_item)
    SVN_ERR(svn_wc__db_wq_add_internal(wcroot, work_item, scratch_pool));

  return SVN_NO_ERROR;
}

/* Send all notifications stored in the notification list, and then
 * remove the temporary database table. */
svn_error_t *
svn_wc__db_update_move_list_notify(svn_wc__db_wcroot_t *wcroot,
                                   svn_revnum_t old_revision,
                                   svn_revnum_t new_revision,
                                   svn_wc_notify_func2_t notify_func,
                                   void *notify_baton,
                                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  if (notify_func)
    {
      apr_pool_t *iterpool;
      svn_boolean_t have_row;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_UPDATE_MOVE_LIST));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      iterpool = svn_pool_create(scratch_pool);
      while (have_row)
        {
          const char *local_relpath;
          svn_wc_notify_action_t action;
          svn_wc_notify_t *notify;

          svn_pool_clear(iterpool);

          local_relpath = svn_sqlite__column_text(stmt, 0, NULL);
          action = svn_sqlite__column_int(stmt, 1);
          notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                        local_relpath,
                                                        iterpool),
                                        action, iterpool);
          notify->kind = svn_sqlite__column_token(stmt, 2, kind_map_none);
          notify->content_state = svn_sqlite__column_int(stmt, 3);
          notify->prop_state = svn_sqlite__column_int(stmt, 4);
          notify->old_revision = old_revision;
          notify->revision = new_revision;
          notify_func(notify_baton, notify, scratch_pool);

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      svn_pool_destroy(iterpool);
      SVN_ERR(svn_sqlite__reset(stmt));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_FINALIZE_UPDATE_MOVE));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* Create a tree-conflict for recording on LOCAL_RELPATH if such
   a tree-conflict does not already exist. */
static svn_error_t *
create_tree_conflict(svn_skel_t **conflict_p,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     const char *dst_op_root_relpath,
                     svn_wc__db_t *db,
                     const svn_wc_conflict_version_t *old_version,
                     const svn_wc_conflict_version_t *new_version,
                     svn_wc_operation_t operation,
                     svn_node_kind_t old_kind,
                     svn_node_kind_t new_kind,
                     const char *old_repos_relpath,
                     svn_wc_conflict_reason_t reason,
                     svn_wc_conflict_action_t action,
                     const char *move_src_op_root_relpath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_skel_t *conflict;
  svn_wc_conflict_version_t *conflict_old_version, *conflict_new_version;
  const char *move_src_op_root_abspath
    = move_src_op_root_relpath
    ? svn_dirent_join(wcroot->abspath,
                      move_src_op_root_relpath, scratch_pool)
    : NULL;
  const char *old_repos_relpath_part
    = old_repos_relpath && old_version
    ? svn_relpath_skip_ancestor(old_version->path_in_repos,
                                old_repos_relpath)
    : NULL;
  const char *new_repos_relpath
    = old_repos_relpath_part
    ? svn_relpath_join(new_version->path_in_repos, old_repos_relpath_part,
                       scratch_pool)
    : NULL;

  if (!new_repos_relpath)
    {
      const char *child_relpath = svn_relpath_skip_ancestor(
                                            dst_op_root_relpath,
                                            local_relpath);
      SVN_ERR_ASSERT(child_relpath != NULL);
      new_repos_relpath = svn_relpath_join(new_version->path_in_repos,
                                           child_relpath, scratch_pool);
    }

  err = svn_wc__db_read_conflict_internal(&conflict, NULL, NULL,
                                          wcroot, local_relpath,
                                          result_pool, scratch_pool);
  if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
    return svn_error_trace(err);
  else if (err)
    {
      svn_error_clear(err);
      conflict = NULL;
    }

  if (conflict)
    {
      svn_wc_operation_t conflict_operation;
      svn_boolean_t tree_conflicted;

      SVN_ERR(svn_wc__conflict_read_info(&conflict_operation, NULL, NULL, NULL,
                                         &tree_conflicted,
                                         db, wcroot->abspath, conflict,
                                         scratch_pool, scratch_pool));

      if (conflict_operation != svn_wc_operation_update
          && conflict_operation != svn_wc_operation_switch)
        return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                                 _("'%s' already in conflict"),
                                 path_for_error_message(wcroot, local_relpath,
                                                        scratch_pool));

      if (tree_conflicted)
        {
          svn_wc_conflict_reason_t existing_reason;
          svn_wc_conflict_action_t existing_action;
          const char *existing_abspath;

          SVN_ERR(svn_wc__conflict_read_tree_conflict(&existing_reason,
                                                      &existing_action,
                                                      &existing_abspath,
                                                      db, wcroot->abspath,
                                                      conflict,
                                                      scratch_pool,
                                                      scratch_pool));
          if (reason != existing_reason
              || action != existing_action
              || (reason == svn_wc_conflict_reason_moved_away
                  && strcmp(move_src_op_root_relpath,
                            svn_dirent_skip_ancestor(wcroot->abspath,
                                                     existing_abspath))))
            return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                                     _("'%s' already in conflict"),
                                     path_for_error_message(wcroot,
                                                            local_relpath,
                                                            scratch_pool));

          /* Already a suitable tree-conflict. */
          *conflict_p = conflict;
          return SVN_NO_ERROR;
        }
    }
  else
    conflict = svn_wc__conflict_skel_create(result_pool);

  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(
                     conflict, db,
                     svn_dirent_join(wcroot->abspath, local_relpath,
                                     scratch_pool),
                     reason,
                     action,
                     move_src_op_root_abspath,
                     result_pool,
                     scratch_pool));

  if (old_version)
    conflict_old_version = svn_wc_conflict_version_create2(
                                 old_version->repos_url,
                                 old_version->repos_uuid,
                                 old_repos_relpath, old_version->peg_rev,
                                 old_kind, scratch_pool);
  else
    conflict_old_version = NULL;

  conflict_new_version = svn_wc_conflict_version_create2(
                           new_version->repos_url, new_version->repos_uuid,
                           new_repos_relpath, new_version->peg_rev,
                           new_kind, scratch_pool);

  if (operation == svn_wc_operation_update)
    {
      SVN_ERR(svn_wc__conflict_skel_set_op_update(
                conflict, conflict_old_version, conflict_new_version,
                result_pool, scratch_pool));
    }
  else
    {
      assert(operation == svn_wc_operation_switch);
      SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                  conflict, conflict_old_version, conflict_new_version,
                  result_pool, scratch_pool));
    }

  *conflict_p = conflict;
  return SVN_NO_ERROR;
}

static svn_error_t *
create_node_tree_conflict(svn_skel_t **conflict_p,
                          node_move_baton_t *nmb,
                          const char *dst_local_relpath,
                          svn_node_kind_t old_kind,
                          svn_node_kind_t new_kind,
                          svn_wc_conflict_reason_t reason,
                          svn_wc_conflict_action_t action,
                          const char *move_src_op_root_relpath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  update_move_baton_t *umb = nmb->umb;
  const char *dst_repos_relpath;
  const char *dst_root_relpath = svn_relpath_prefix(nmb->dst_relpath,
                                                    umb->dst_op_depth,
                                                    scratch_pool);

  dst_repos_relpath =
            svn_relpath_join(nmb->umb->old_version->path_in_repos,
                             svn_relpath_skip_ancestor(dst_root_relpath,
                                                       nmb->dst_relpath),
                             scratch_pool);

  return svn_error_trace(
            create_tree_conflict(conflict_p, umb->wcroot, dst_local_relpath,
                                 svn_relpath_prefix(dst_local_relpath,
                                                    umb->dst_op_depth,
                                                    scratch_pool),
                                 umb->db,
                                 umb->old_version, umb->new_version,
                                 umb->operation, old_kind, new_kind,
                                 dst_repos_relpath,
                                 reason, action, move_src_op_root_relpath,
                                 result_pool, scratch_pool));
}

/* Checks if a specific local path is shadowed as seen from the move root.
   Helper for update_moved_away_node() */
static svn_error_t *
check_node_shadowed(svn_boolean_t *shadowed,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    int move_root_dst_op_depth,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      int op_depth = svn_sqlite__column_int(stmt, 0);

      *shadowed = (op_depth > move_root_dst_op_depth);
    }
  else
    *shadowed = FALSE;
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Set a tree conflict for the shadowed node LOCAL_RELPATH, which is
   the ROOT OF THE OBSTRUCTION if such a tree-conflict does not
   already exist.  KIND is the kind of the incoming LOCAL_RELPATH. */
static svn_error_t *
mark_tc_on_op_root(node_move_baton_t *nmb,
                   svn_node_kind_t old_kind,
                   svn_node_kind_t new_kind,
                   svn_wc_conflict_action_t action,
                   apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  const char *move_dst_relpath;
  svn_skel_t *conflict;

  SVN_ERR_ASSERT(nmb->shadowed && !nmb->pb->shadowed);

  nmb->skip = TRUE;

  if (old_kind == svn_node_none)
    move_dst_relpath = NULL;
  else
    SVN_ERR(svn_wc__db_scan_moved_to_internal(NULL, &move_dst_relpath, NULL,
                                              b->wcroot, nmb->dst_relpath,
                                              b->dst_op_depth,
                                              scratch_pool, scratch_pool));

  SVN_ERR(create_node_tree_conflict(&conflict, nmb, nmb->dst_relpath,
                                    old_kind, new_kind,
                                    (move_dst_relpath
                                     ? svn_wc_conflict_reason_moved_away
                                     : svn_wc_conflict_reason_deleted),
                                    action, move_dst_relpath
                                              ? nmb->dst_relpath
                                              : NULL,
                                    scratch_pool, scratch_pool));

  SVN_ERR(update_move_list_add(b->wcroot, nmb->dst_relpath, b->db,
                               svn_wc_notify_tree_conflict,
                               new_kind,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, NULL, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
mark_node_edited(node_move_baton_t *nmb,
                 apr_pool_t *scratch_pool)
{
  if (nmb->edited)
    return SVN_NO_ERROR;

  if (nmb->pb)
    {
      SVN_ERR(mark_node_edited(nmb->pb, scratch_pool));

      if (nmb->pb->skip)
        nmb->skip = TRUE;
    }

  nmb->edited = TRUE;

  if (nmb->skip)
    return SVN_NO_ERROR;

  if (nmb->shadowed && !(nmb->pb && nmb->pb->shadowed))
    {
      svn_node_kind_t dst_kind, src_kind;

      SVN_ERR(svn_wc__db_depth_get_info(NULL, &dst_kind, NULL,
                                        NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        nmb->umb->wcroot, nmb->dst_relpath,
                                        nmb->umb->dst_op_depth,
                                        scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_depth_get_info(NULL, &src_kind, NULL, NULL,
                                        NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        nmb->umb->wcroot, nmb->src_relpath,
                                        nmb->umb->src_op_depth,
                                        scratch_pool, scratch_pool));

      SVN_ERR(mark_tc_on_op_root(nmb,
                                 dst_kind, src_kind,
                                 svn_wc_conflict_action_edit,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
mark_parent_edited(node_move_baton_t *nmb,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(nmb && nmb->pb);

  SVN_ERR(mark_node_edited(nmb->pb, scratch_pool));

  if (nmb->pb->skip)
    nmb->skip = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_add_directory(node_move_baton_t *nmb,
                        const char *relpath,
                        svn_node_kind_t old_kind,
                        apr_hash_t *props,
                        apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  const char *local_abspath;
  svn_node_kind_t wc_kind;
  svn_skel_t *work_item = NULL;
  svn_skel_t *conflict = NULL;
  svn_wc_conflict_reason_t reason = svn_wc_conflict_reason_unversioned;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  if (nmb->shadowed)
    {
      svn_wc__db_status_t status;

      SVN_ERR(svn_wc__db_read_info_internal(&status, &wc_kind, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL,
                                            b->wcroot, relpath,
                                            scratch_pool, scratch_pool));

      if (status == svn_wc__db_status_deleted)
        reason = svn_wc_conflict_reason_deleted;
      else if (status != svn_wc__db_status_added)
        wc_kind = svn_node_none;
      else if (old_kind == svn_node_none)
        reason = svn_wc_conflict_reason_added;
      else
        reason = svn_wc_conflict_reason_replaced;
    }
  else
    wc_kind = svn_node_none;

  local_abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);

  if (wc_kind == svn_node_none)
    {
      /* Check for unversioned tree-conflict */
      SVN_ERR(svn_io_check_path(local_abspath, &wc_kind, scratch_pool));
    }

  if (!nmb->shadowed && wc_kind == old_kind)
    wc_kind = svn_node_none; /* Node will be gone once we install */

  if (wc_kind != svn_node_none
      && (nmb->shadowed || wc_kind != old_kind)) /* replace */
    {
      SVN_ERR(create_node_tree_conflict(&conflict, nmb, relpath,
                                        old_kind, svn_node_dir,
                                        reason,
                                        (old_kind == svn_node_none)
                                          ? svn_wc_conflict_action_add
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
    }
  else
    {
      SVN_ERR(svn_wc__wq_build_dir_install(&work_item, b->db, local_abspath,
                                           scratch_pool, scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, relpath, b->db,
                               (old_kind == svn_node_none)
                                  ? svn_wc_notify_update_add
                                  : svn_wc_notify_update_replace,
                               svn_node_dir,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, work_item, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_working_node(const char *src_relpath,
                  const char *dst_relpath,
                  svn_wc__db_wcroot_t *wcroot,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *dst_parent_relpath = svn_relpath_dirname(dst_relpath,
                                                       scratch_pool);

  /* Add a WORKING row for the new node, based on the source. */
  SVN_ERR(svn_sqlite__get_statement(&stmt,wcroot->sdb,
                                    STMT_INSERT_WORKING_NODE_COPY_FROM));
  SVN_ERR(svn_sqlite__bindf(stmt, "issdst", wcroot->wc_id, src_relpath,
                            dst_relpath, relpath_depth(dst_relpath),
                            dst_parent_relpath, presence_map,
                            svn_wc__db_status_normal));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Copy properties over.  ### This loses changelist association. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, src_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      apr_size_t props_size;
      const char *properties;

      properties = svn_sqlite__column_blob(stmt, 1, &props_size,
                                           scratch_pool);
      SVN_ERR(svn_sqlite__reset(stmt));
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "issbs",
                                wcroot->wc_id, dst_relpath,
                                svn_relpath_dirname(dst_relpath,
                                                    scratch_pool),
                                properties, props_size, NULL));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_incoming_add_directory(node_move_baton_t *nmb,
                                 const char *dst_relpath,
                                 svn_node_kind_t old_kind,
                                 apr_hash_t *props,
                                 const char *src_relpath,
                                 apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  const char *dst_abspath;
  svn_node_kind_t wc_kind;
  svn_skel_t *work_item = NULL;
  svn_skel_t *conflict = NULL;
  svn_wc_conflict_reason_t reason = svn_wc_conflict_reason_unversioned;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  dst_abspath = svn_dirent_join(b->wcroot->abspath, dst_relpath, scratch_pool);

  /* Check for unversioned tree-conflict */
  SVN_ERR(svn_io_check_path(dst_abspath, &wc_kind, scratch_pool));

  if (wc_kind == old_kind)
    wc_kind = svn_node_none; /* Node will be gone once we install */

  if (wc_kind != svn_node_none && wc_kind != old_kind) /* replace */
    {
      SVN_ERR(create_node_tree_conflict(&conflict, nmb, dst_relpath,
                                        old_kind, svn_node_dir,
                                        reason,
                                        (old_kind == svn_node_none)
                                          ? svn_wc_conflict_action_add
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
    }
  else
    {
      SVN_ERR(copy_working_node(src_relpath, dst_relpath, b->wcroot,
                                scratch_pool));
      SVN_ERR(svn_wc__wq_build_dir_install(&work_item, b->db, dst_abspath,
                                           scratch_pool, scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, dst_relpath, b->db,
                               (old_kind == svn_node_none)
                                  ? svn_wc_notify_update_add
                                  : svn_wc_notify_update_replace,
                               svn_node_dir,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, work_item, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_add_file(node_move_baton_t *nmb,
                   const char *relpath,
                   svn_node_kind_t old_kind,
                   const svn_checksum_t *checksum,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_wc_conflict_reason_t reason = svn_wc_conflict_reason_unversioned;
  svn_node_kind_t wc_kind;
  const char *local_abspath;
  svn_skel_t *work_item = NULL;
  svn_skel_t *conflict = NULL;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  if (nmb->shadowed)
    {
      svn_wc__db_status_t status;

      SVN_ERR(svn_wc__db_read_info_internal(&status, &wc_kind, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL,
                                            b->wcroot, relpath,
                                            scratch_pool, scratch_pool));

      if (status == svn_wc__db_status_deleted)
        reason = svn_wc_conflict_reason_deleted;
      else if (status != svn_wc__db_status_added)
        wc_kind = svn_node_none;
      else if (old_kind == svn_node_none)
        reason = svn_wc_conflict_reason_added;
      else
        reason = svn_wc_conflict_reason_replaced;
    }
  else
    wc_kind = svn_node_none;

  local_abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);

  if (wc_kind == svn_node_none)
    {
      /* Check for unversioned tree-conflict */
      SVN_ERR(svn_io_check_path(local_abspath, &wc_kind, scratch_pool));
    }

  if (wc_kind != svn_node_none
      && (nmb->shadowed || wc_kind != old_kind)) /* replace */
    {
      SVN_ERR(create_node_tree_conflict(&conflict, nmb, relpath,
                                        old_kind, svn_node_file,
                                        reason,
                                        (old_kind == svn_node_none)
                                          ? svn_wc_conflict_action_add
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
    }
  else
    {
      /* Update working file. */
      SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db,
                                            svn_dirent_join(b->wcroot->abspath,
                                                            relpath,
                                                            scratch_pool),
                                            NULL,
                                            FALSE /*FIXME: use_commit_times?*/,
                                            TRUE  /* record_file_info */,
                                            scratch_pool, scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, relpath, b->db,
                               (old_kind == svn_node_none)
                                  ? svn_wc_notify_update_add
                                  : svn_wc_notify_update_replace,
                               svn_node_file,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, work_item, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_incoming_add_file(node_move_baton_t *nmb,
                            const char *dst_relpath,
                            svn_node_kind_t old_kind,
                            const svn_checksum_t *checksum,
                            apr_hash_t *props,
                            const char *src_relpath,
                            const char *content_abspath,
                            apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_wc_conflict_reason_t reason = svn_wc_conflict_reason_unversioned;
  svn_node_kind_t wc_kind;
  const char *dst_abspath;
  svn_skel_t *work_items = NULL;
  svn_skel_t *work_item = NULL;
  svn_skel_t *conflict = NULL;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    {
      SVN_ERR(svn_io_remove_file2(content_abspath, TRUE, scratch_pool));
      return SVN_NO_ERROR;
    }

  dst_abspath = svn_dirent_join(b->wcroot->abspath, dst_relpath, scratch_pool);

  /* Check for unversioned tree-conflict */
  SVN_ERR(svn_io_check_path(dst_abspath, &wc_kind, scratch_pool));

  if (wc_kind != svn_node_none && wc_kind != old_kind) /* replace */
    {
      SVN_ERR(create_node_tree_conflict(&conflict, nmb, dst_relpath,
                                        old_kind, svn_node_file,
                                        reason,
                                        (old_kind == svn_node_none)
                                          ? svn_wc_conflict_action_add
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
      SVN_ERR(svn_io_remove_file2(content_abspath, TRUE, scratch_pool));
    }
  else
    {
      const char *src_abspath;

      SVN_ERR(copy_working_node(src_relpath, dst_relpath, b->wcroot,
                                scratch_pool));

      /* Update working file. */
      src_abspath = svn_dirent_join(b->wcroot->abspath, src_relpath,
                                    scratch_pool);
      SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db, dst_abspath,
                                            src_abspath,
                                            FALSE /* FIXME: use_commit_times?*/,
                                            TRUE  /* record_file_info */,
                                            scratch_pool, scratch_pool));
      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

      /* Queue removal of temporary content copy. */
      SVN_ERR(svn_wc__wq_build_file_remove(&work_item, b->db,
                                           b->wcroot->abspath, src_abspath,
                                           scratch_pool, scratch_pool));
    
      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }

  SVN_ERR(update_move_list_add(b->wcroot, dst_relpath, b->db,
                               (old_kind == svn_node_none)
                                  ? svn_wc_notify_update_add
                                  : svn_wc_notify_update_replace,
                               svn_node_file,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, work_items, scratch_pool));
  return SVN_NO_ERROR;
}

/* All the info we need about one version of a working node. */
typedef struct working_node_version_t
{
  svn_wc_conflict_version_t *location_and_kind;
  apr_hash_t *props;
  const svn_checksum_t *checksum; /* for files only */
} working_node_version_t;

/* Return *WORK_ITEMS to create a conflict on LOCAL_ABSPATH. */
static svn_error_t *
create_conflict_markers(svn_skel_t **work_items,
                        const char *local_abspath,
                        svn_wc__db_t *db,
                        const char *repos_relpath,
                        svn_skel_t *conflict_skel,
                        svn_wc_operation_t operation,
                        const working_node_version_t *old_version,
                        const working_node_version_t *new_version,
                        svn_node_kind_t kind,
                        svn_boolean_t set_operation,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc_conflict_version_t *original_version;
  svn_wc_conflict_version_t *conflicted_version;
  const char *part;

  original_version = svn_wc_conflict_version_dup(
                       old_version->location_and_kind, scratch_pool);
  original_version->node_kind = kind;
  conflicted_version = svn_wc_conflict_version_dup(
                         new_version->location_and_kind, scratch_pool);
  conflicted_version->node_kind = kind;

  part = svn_relpath_skip_ancestor(original_version->path_in_repos,
                                   repos_relpath);
  if (part == NULL)
    part = svn_relpath_skip_ancestor(conflicted_version->path_in_repos,
                                     repos_relpath);
  SVN_ERR_ASSERT(part != NULL);

  conflicted_version->path_in_repos
    = svn_relpath_join(conflicted_version->path_in_repos, part, scratch_pool);
  original_version->path_in_repos = repos_relpath;

  if (set_operation)
    {
      if (operation == svn_wc_operation_update)
        {
          SVN_ERR(svn_wc__conflict_skel_set_op_update(
                    conflict_skel, original_version,
                    conflicted_version,
                    scratch_pool, scratch_pool));
        }
      else if (operation == svn_wc_operation_merge)
        {
          SVN_ERR(svn_wc__conflict_skel_set_op_merge(
                    conflict_skel, original_version,
                    conflicted_version,
                    scratch_pool, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                    conflict_skel, original_version,
                    conflicted_version,
                    scratch_pool, scratch_pool));
        }
    }

  /* According to this func's doc string, it is "Currently only used for
   * property conflicts as text conflict markers are just in-wc files." */
  SVN_ERR(svn_wc__conflict_create_markers(work_items, db,
                                          local_abspath,
                                          conflict_skel,
                                          result_pool,
                                          scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
update_working_props(svn_wc_notify_state_t *prop_state,
                     svn_skel_t **conflict_skel,
                     apr_array_header_t **propchanges,
                     apr_hash_t **actual_props,
                     update_move_baton_t *b,
                     const char *local_relpath,
                     const struct working_node_version_t *old_version,
                     const struct working_node_version_t *new_version,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *new_actual_props;
  apr_array_header_t *new_propchanges;

  /*
   * Run a 3-way prop merge to update the props, using the pre-update
   * props as the merge base, the post-update props as the
   * merge-left version, and the current props of the
   * moved-here working file as the merge-right version.
   */
  SVN_ERR(svn_wc__db_read_props_internal(actual_props,
                                         b->wcroot, local_relpath,
                                         result_pool, scratch_pool));
  SVN_ERR(svn_prop_diffs(propchanges, new_version->props, old_version->props,
                         result_pool));
  SVN_ERR(svn_wc__merge_props(conflict_skel, prop_state,
                              &new_actual_props,
                              b->db, svn_dirent_join(b->wcroot->abspath,
                                                     local_relpath,
                                                     scratch_pool),
                              old_version->props, old_version->props,
                              *actual_props, *propchanges,
                              result_pool, scratch_pool));

  /* Setting properties in ACTUAL_NODE with svn_wc__db_op_set_props_internal
     relies on NODES row being updated via a different route .

     This extra property diff makes sure we clear the actual row when
     the final result is unchanged properties. */
  SVN_ERR(svn_prop_diffs(&new_propchanges, new_actual_props, new_version->props,
                         scratch_pool));
  if (!new_propchanges->nelts)
    new_actual_props = NULL;

  /* Install the new actual props. */
  SVN_ERR(svn_wc__db_op_set_props_internal(b->wcroot, local_relpath,
                                           new_actual_props,
                                           svn_wc__has_magic_property(
                                                    *propchanges),
                                           scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_alter_directory(node_move_baton_t *nmb,
                          const char *dst_relpath,
                          apr_hash_t *old_props,
                          apr_hash_t *new_props,
                          apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  working_node_version_t old_version, new_version;
  svn_skel_t *work_items = NULL;
  svn_skel_t *conflict_skel = NULL;
  const char *local_abspath = svn_dirent_join(b->wcroot->abspath, dst_relpath,
                                              scratch_pool);
  svn_wc_notify_state_t prop_state;
  apr_hash_t *actual_props;
  apr_array_header_t *propchanges;
  svn_node_kind_t wc_kind;
  svn_boolean_t obstructed = FALSE;

  SVN_ERR(mark_node_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  SVN_ERR(svn_io_check_path(local_abspath, &wc_kind, scratch_pool));
  if (wc_kind != svn_node_none && wc_kind != svn_node_dir)
    {
      SVN_ERR(create_node_tree_conflict(&conflict_skel, nmb, dst_relpath,
                                        svn_node_dir, svn_node_dir,
                                        svn_wc_conflict_reason_obstructed,
                                        svn_wc_conflict_action_edit,
                                        NULL,
                                        scratch_pool, scratch_pool));
      obstructed = TRUE;
    }

  old_version.location_and_kind = b->old_version;
  new_version.location_and_kind = b->new_version;

  old_version.checksum = NULL; /* not a file */
  old_version.props = old_props;
  new_version.checksum = NULL; /* not a file */
  new_version.props = new_props;

  SVN_ERR(update_working_props(&prop_state, &conflict_skel,
                                &propchanges, &actual_props,
                                b, dst_relpath,
                                &old_version, &new_version,
                                scratch_pool, scratch_pool));

  if (prop_state == svn_wc_notify_state_conflicted)
    {
      const char *move_dst_repos_relpath;

      SVN_ERR(svn_wc__db_depth_get_info(NULL, NULL, NULL,
                                        &move_dst_repos_relpath, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL,
                                        b->wcroot, dst_relpath,
                                        b->dst_op_depth,
                                        scratch_pool, scratch_pool));

      SVN_ERR(create_conflict_markers(&work_items, local_abspath,
                                      b->db, move_dst_repos_relpath,
                                      conflict_skel, b->operation,
                                      &old_version, &new_version,
                                      svn_node_dir, !obstructed,
                                      scratch_pool, scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, dst_relpath, b->db,
                               svn_wc_notify_update_update,
                               svn_node_dir,
                               svn_wc_notify_state_inapplicable,
                               prop_state,
                               conflict_skel, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

/* Edit the file found at the move destination, which is initially at
 * the old state.  Merge the changes into the "working"/"actual" file.
 *
 * Merge the difference between OLD_VERSION and NEW_VERSION into
 * the working file at LOCAL_RELPATH.
 *
 * The term 'old' refers to the pre-update state, which is the state of
 * (some layer of) LOCAL_RELPATH while this function runs; and 'new'
 * refers to the post-update state, as found at the (base layer of) the
 * move source path while this function runs.
 *
 * LOCAL_RELPATH is a file in the working copy at WCROOT in DB, and
 * REPOS_RELPATH is the repository path it would be committed to.
 *
 * Use NOTIFY_FUNC and NOTIFY_BATON for notifications.
 * Set *WORK_ITEMS to any required work items, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
tc_editor_alter_file(node_move_baton_t *nmb,
                     const char *dst_relpath,
                     const svn_checksum_t *old_checksum,
                     const svn_checksum_t *new_checksum,
                     apr_hash_t *old_props,
                     apr_hash_t *new_props,
                     apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  working_node_version_t old_version, new_version;
  const char *local_abspath = svn_dirent_join(b->wcroot->abspath,
                                              dst_relpath,
                                              scratch_pool);
  const char *old_pristine_abspath;
  const char *new_pristine_abspath;
  svn_skel_t *conflict_skel = NULL;
  apr_hash_t *actual_props;
  apr_array_header_t *propchanges;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_wc_notify_state_t prop_state, content_state;
  svn_skel_t *work_item, *work_items = NULL;
  svn_node_kind_t wc_kind;
  svn_boolean_t obstructed = FALSE;

  SVN_ERR(mark_node_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  SVN_ERR(svn_io_check_path(local_abspath, &wc_kind, scratch_pool));
  if (wc_kind != svn_node_none && wc_kind != svn_node_file)
    {
      SVN_ERR(create_node_tree_conflict(&conflict_skel, nmb, dst_relpath,
                                        svn_node_file, svn_node_file,
                                        svn_wc_conflict_reason_obstructed,
                                        svn_wc_conflict_action_edit,
                                        NULL,
                                        scratch_pool, scratch_pool));
      obstructed = TRUE;
    }

  old_version.location_and_kind = b->old_version;
  new_version.location_and_kind = b->new_version;

  old_version.checksum = old_checksum;
  old_version.props = old_props;
  new_version.checksum = new_checksum;
  new_version.props = new_props;

  /* ### TODO: Only do this when there is no higher WORKING layer */
  SVN_ERR(update_working_props(&prop_state, &conflict_skel, &propchanges,
                               &actual_props, b, dst_relpath,
                               &old_version, &new_version,
                               scratch_pool, scratch_pool));

  if (!obstructed
      && !svn_checksum_match(new_version.checksum, old_version.checksum))
    {
      svn_boolean_t is_locally_modified;

      SVN_ERR(svn_wc__internal_file_modified_p(&is_locally_modified,
                                               b->db, local_abspath,
                                               FALSE /* exact_comparison */,
                                               scratch_pool));
      if (!is_locally_modified)
        {
          SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db,
                                                local_abspath,
                                                NULL,
                                                FALSE /* FIXME: use_commit_times? */,
                                                TRUE  /* record_file_info */,
                                                scratch_pool, scratch_pool));

          work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

          content_state = svn_wc_notify_state_changed;
        }
      else
        {
          /*
           * Run a 3-way merge to update the file, using the pre-update
           * pristine text as the merge base, the post-update pristine
           * text as the merge-left version, and the current content of the
           * moved-here working file as the merge-right version.
           */
          SVN_ERR(svn_wc__db_pristine_get_path(&old_pristine_abspath,
                                               b->db, b->wcroot->abspath,
                                               old_version.checksum,
                                               scratch_pool, scratch_pool));
          SVN_ERR(svn_wc__db_pristine_get_path(&new_pristine_abspath,
                                               b->db, b->wcroot->abspath,
                                               new_version.checksum,
                                               scratch_pool, scratch_pool));
          SVN_ERR(svn_wc__internal_merge(&work_item, &conflict_skel,
                                         &merge_outcome, b->db,
                                         old_pristine_abspath,
                                         new_pristine_abspath,
                                         local_abspath,
                                         local_abspath,
                                         NULL, NULL, NULL, /* diff labels */
                                         actual_props,
                                         FALSE, /* dry-run */
                                         NULL, /* diff3-cmd */
                                         NULL, /* merge options */
                                         propchanges,
                                         b->cancel_func, b->cancel_baton,
                                         scratch_pool, scratch_pool));

          work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

          if (merge_outcome == svn_wc_merge_conflict)
            content_state = svn_wc_notify_state_conflicted;
          else
            content_state = svn_wc_notify_state_merged;
        }
    }
  else
    content_state = svn_wc_notify_state_unchanged;

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      const char *move_dst_repos_relpath;

      SVN_ERR(svn_wc__db_depth_get_info(NULL, NULL, NULL,
                                        &move_dst_repos_relpath, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL,
                                        b->wcroot, dst_relpath,
                                        b->dst_op_depth,
                                        scratch_pool, scratch_pool));

      SVN_ERR(create_conflict_markers(&work_item, local_abspath, b->db,
                                      move_dst_repos_relpath, conflict_skel,
                                      b->operation, &old_version, &new_version,
                                      svn_node_file, !obstructed,
                                      scratch_pool, scratch_pool));

      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }

  SVN_ERR(update_move_list_add(b->wcroot, dst_relpath, b->db,
                               svn_wc_notify_update_update,
                               svn_node_file,
                               content_state,
                               prop_state,
                               conflict_skel, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_update_incoming_moved_file(node_move_baton_t *nmb,
                                     const char *dst_relpath,
                                     const char *src_relpath,
                                     const svn_checksum_t *src_checksum,
                                     const svn_checksum_t *dst_checksum,
                                     apr_hash_t *dst_props,
                                     apr_hash_t *src_props,
                                     svn_boolean_t do_text_merge,
                                     apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  working_node_version_t old_version, new_version;
  const char *dst_abspath = svn_dirent_join(b->wcroot->abspath,
                                            dst_relpath,
                                            scratch_pool);
  svn_skel_t *conflict_skel = NULL;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unchanged;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unchanged;
  svn_skel_t *work_item, *work_items = NULL;
  svn_node_kind_t dst_kind_on_disk;
  const char *dst_repos_relpath;
  svn_boolean_t tree_conflict = FALSE;
  svn_node_kind_t dst_db_kind;
  svn_error_t *err;

  SVN_ERR(mark_node_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  err = svn_wc__db_base_get_info_internal(NULL, &dst_db_kind, NULL,
                                          &dst_repos_relpath,
                                          NULL, NULL, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL, NULL, NULL,
                                          b->wcroot, dst_relpath,
                                          scratch_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      const char *dst_parent_relpath;
      const char *dst_parent_repos_relpath;
      const char *src_abspath;

      /* If the file cannot be found, it was either deleted at the
       * move destination, or it was moved after its parent was moved.
       * We cannot deal with this problem right now. Instead, we will
       * raise a new tree conflict at the location where this file should
       * have been, and let another run of the resolver deal with the
       * new conflict later on. */

      svn_error_clear(err);

      /* Create a WORKING node for this file at the move destination. */
      SVN_ERR(copy_working_node(src_relpath, dst_relpath, b->wcroot,
                                scratch_pool));

      /* Raise a tree conflict at the new WORKING node. */
      dst_db_kind = svn_node_none;
      SVN_ERR(create_node_tree_conflict(&conflict_skel, nmb, dst_relpath,
                                        svn_node_file, dst_db_kind,
                                        svn_wc_conflict_reason_edited,
                                        svn_wc_conflict_action_delete,
                                        NULL, scratch_pool, scratch_pool));
      dst_parent_relpath = svn_relpath_dirname(dst_relpath, scratch_pool);
      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                                &dst_parent_repos_relpath,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, b->wcroot,
                                                dst_parent_relpath,
                                                scratch_pool, scratch_pool));
      dst_repos_relpath = svn_relpath_join(dst_parent_repos_relpath,
                                           svn_relpath_basename(dst_relpath,
                                                                scratch_pool),
                                           scratch_pool);
      tree_conflict = TRUE;

      /* Schedule a copy of the victim's file content to the new node's path. */
      src_abspath = svn_dirent_join(b->wcroot->abspath, src_relpath,
                                    scratch_pool);
      SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db,
                                            dst_abspath,
                                            src_abspath,
                                            FALSE /*FIXME: use_commit_times?*/,
                                            TRUE  /* record_file_info */,
                                            scratch_pool, scratch_pool));
      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }
  else
    SVN_ERR(err);

  if ((dst_db_kind == svn_node_none || dst_db_kind != svn_node_file) &&
      conflict_skel == NULL)
    {
      SVN_ERR(create_node_tree_conflict(&conflict_skel, nmb, dst_relpath,
                                        svn_node_file, dst_db_kind,
                                        dst_db_kind == svn_node_none
                                          ? svn_wc_conflict_reason_missing
                                          : svn_wc_conflict_reason_obstructed,
                                        svn_wc_conflict_action_edit,
                                        NULL,
                                        scratch_pool, scratch_pool));
      tree_conflict = TRUE;
    }

  SVN_ERR(svn_io_check_path(dst_abspath, &dst_kind_on_disk, scratch_pool));
  if ((dst_kind_on_disk == svn_node_none || dst_kind_on_disk != svn_node_file)
      && conflict_skel == NULL)
    {
      SVN_ERR(create_node_tree_conflict(&conflict_skel, nmb, dst_relpath,
                                        svn_node_file, dst_kind_on_disk,
                                        dst_kind_on_disk == svn_node_none
                                          ? svn_wc_conflict_reason_missing
                                          : svn_wc_conflict_reason_obstructed,
                                        svn_wc_conflict_action_edit,
                                        NULL,
                                        scratch_pool, scratch_pool));
      tree_conflict = TRUE;
    }

  old_version.location_and_kind = b->old_version;
  new_version.location_and_kind = b->new_version;

  old_version.checksum = src_checksum;
  old_version.props = src_props;
  new_version.checksum = dst_checksum;
  new_version.props = dst_props;

  /* Merge properties and text content if there is no tree conflict. */
  if (conflict_skel == NULL)
    {
      apr_hash_t *actual_props;
      apr_array_header_t *propchanges;

      SVN_ERR(update_working_props(&prop_state, &conflict_skel, &propchanges,
                                   &actual_props, b, dst_relpath,
                                   &old_version, &new_version,
                                   scratch_pool, scratch_pool));
      if (do_text_merge)
        {
          const char *old_pristine_abspath;
          const char *src_abspath;
          const char *label_left;
          const char *label_target;

          /*
           * Run a 3-way merge to update the file at its post-move location,
           * using the pre-move file's pristine text as the merge base, the
           * post-move content as the merge-right version, and the current
           * content of the working file at the pre-move location as the
           * merge-left version.
           */
          SVN_ERR(svn_wc__db_pristine_get_path(&old_pristine_abspath,
                                               b->db, b->wcroot->abspath,
                                               src_checksum,
                                               scratch_pool, scratch_pool));
          src_abspath = svn_dirent_join(b->wcroot->abspath, src_relpath,
                                        scratch_pool);
          label_left = apr_psprintf(scratch_pool, ".r%ld",
                                    b->old_version->peg_rev);
          label_target = apr_psprintf(scratch_pool, ".r%ld",
                                      b->new_version->peg_rev);
          SVN_ERR(svn_wc__internal_merge(&work_item, &conflict_skel,
                                         &merge_outcome, b->db,
                                         old_pristine_abspath,
                                         src_abspath,
                                         dst_abspath,
                                         dst_abspath,
                                         label_left,
                                         _(".working"),
                                         label_target,
                                         actual_props,
                                         FALSE, /* dry-run */
                                         NULL, /* diff3-cmd */
                                         NULL, /* merge options */
                                         propchanges,
                                         b->cancel_func, b->cancel_baton,
                                         scratch_pool, scratch_pool));

          work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

          if (merge_outcome == svn_wc_merge_conflict)
            content_state = svn_wc_notify_state_conflicted;
          else
            content_state = svn_wc_notify_state_merged;
        }
    }

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      SVN_ERR(create_conflict_markers(&work_item, dst_abspath, b->db,
                                      dst_repos_relpath, conflict_skel,
                                      b->operation, &old_version, &new_version,
                                      svn_node_file, !tree_conflict,
                                      scratch_pool, scratch_pool));

      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }

  SVN_ERR(update_move_list_add(b->wcroot, dst_relpath, b->db,
                               svn_wc_notify_update_update,
                               svn_node_file,
                               content_state,
                               prop_state,
                               conflict_skel, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_delete(node_move_baton_t *nmb,
                 const char *relpath,
                 svn_node_kind_t old_kind,
                 svn_node_kind_t new_kind,
                 apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_sqlite__stmt_t *stmt;
  const char *local_abspath;
  svn_boolean_t is_modified, is_all_deletes;
  svn_skel_t *work_items = NULL;
  svn_skel_t *conflict = NULL;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  /* Check before retracting delete to catch delete-delete
     conflicts. This catches conflicts on the node itself; deleted
     children are caught as local modifications below.*/
  if (nmb->shadowed)
    {
      SVN_ERR(mark_tc_on_op_root(nmb,
                                 old_kind, new_kind,
                                 svn_wc_conflict_action_delete,
                                 scratch_pool));
      return SVN_NO_ERROR;
    }

  local_abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);
  SVN_ERR(svn_wc__node_has_local_mods(&is_modified, &is_all_deletes,
                                      nmb->umb->db, local_abspath, FALSE,
                                      NULL, NULL, scratch_pool));
  if (is_modified)
    {
      svn_wc_conflict_reason_t reason;

      /* No conflict means no NODES rows at the relpath op-depth
         so it's easy to convert the modified tree into a copy.

         Note the following assumptions for relpath:
            * it is not shadowed
            * it is not the/an op-root. (or we can't make us a copy)
       */

      SVN_ERR(svn_wc__db_op_make_copy_internal(b->wcroot, relpath, FALSE,
                                               NULL, NULL, scratch_pool));

      reason = svn_wc_conflict_reason_edited;

      SVN_ERR(create_node_tree_conflict(&conflict, nmb, relpath,
                                        old_kind, new_kind, reason,
                                        (new_kind == svn_node_none)
                                          ? svn_wc_conflict_action_delete
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
    }
  else
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      const char *del_abspath;
      svn_boolean_t have_row;

      /* Get all descendants of the node in reverse order (so children are
         handled before their parents, but not strictly depth first) */
      SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                        STMT_SELECT_DESCENDANTS_OP_DEPTH_RV));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, relpath,
                                b->dst_op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      while (have_row)
        {
          svn_error_t *err;
          svn_skel_t *work_item;
          svn_node_kind_t del_kind;

          svn_pool_clear(iterpool);

          del_kind = svn_sqlite__column_token(stmt, 1, kind_map);
          del_abspath = svn_dirent_join(b->wcroot->abspath,
                                        svn_sqlite__column_text(stmt, 0, NULL),
                                        iterpool);
          if (del_kind == svn_node_dir)
            err = svn_wc__wq_build_dir_remove(&work_item, b->db,
                                              b->wcroot->abspath, del_abspath,
                                              FALSE /* recursive */,
                                              iterpool, iterpool);
          else
            err = svn_wc__wq_build_file_remove(&work_item, b->db,
                                               b->wcroot->abspath, del_abspath,
                                               iterpool, iterpool);
          if (!err)
            err = svn_wc__db_wq_add_internal(b->wcroot, work_item, iterpool);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      SVN_ERR(svn_sqlite__reset(stmt));

      if (old_kind == svn_node_dir)
        SVN_ERR(svn_wc__wq_build_dir_remove(&work_items, b->db,
                                            b->wcroot->abspath, local_abspath,
                                            FALSE /* recursive */,
                                            scratch_pool, iterpool));
      else
        SVN_ERR(svn_wc__wq_build_file_remove(&work_items, b->db,
                                             b->wcroot->abspath, local_abspath,
                                             scratch_pool, iterpool));

      svn_pool_destroy(iterpool);
    }

  /* Only notify if add_file/add_dir is not going to notify */
  if (conflict || (new_kind == svn_node_none))
    SVN_ERR(update_move_list_add(b->wcroot, relpath, b->db,
                                 svn_wc_notify_update_delete,
                                 new_kind,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable,
                                 conflict, work_items, scratch_pool));
  else if (work_items)
    SVN_ERR(svn_wc__db_wq_add_internal(b->wcroot, work_items,
                                       scratch_pool));

  return SVN_NO_ERROR;
}

/* Handle node deletion for an incoming move. */
static svn_error_t *
tc_incoming_editor_delete(node_move_baton_t *nmb,
                          const char *relpath,
                          svn_node_kind_t old_kind,
                          svn_node_kind_t new_kind,
                          apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_sqlite__stmt_t *stmt;
  const char *local_abspath;
  svn_boolean_t is_modified, is_all_deletes;
  svn_skel_t *work_items = NULL;
  svn_skel_t *conflict = NULL;

  SVN_ERR(mark_parent_edited(nmb, scratch_pool));
  if (nmb->skip)
    return SVN_NO_ERROR;

  /* Check before retracting delete to catch delete-delete
     conflicts. This catches conflicts on the node itself; deleted
     children are caught as local modifications below.*/
  if (nmb->shadowed)
    {
      SVN_ERR(mark_tc_on_op_root(nmb,
                                 old_kind, new_kind,
                                 svn_wc_conflict_action_delete,
                                 scratch_pool));
      return SVN_NO_ERROR;
    }

  local_abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);
  SVN_ERR(svn_wc__node_has_local_mods(&is_modified, &is_all_deletes,
                                      nmb->umb->db, local_abspath, FALSE,
                                      NULL, NULL, scratch_pool));
  if (is_modified)
    {
      svn_wc_conflict_reason_t reason;

      /* No conflict means no NODES rows at the relpath op-depth
         so it's easy to convert the modified tree into a copy.

         Note the following assumptions for relpath:
            * it is not shadowed
            * it is not the/an op-root. (or we can't make us a copy)
       */

      SVN_ERR(svn_wc__db_op_make_copy_internal(b->wcroot, relpath, FALSE,
                                               NULL, NULL, scratch_pool));

      reason = svn_wc_conflict_reason_edited;

      SVN_ERR(create_node_tree_conflict(&conflict, nmb, relpath,
                                        old_kind, new_kind, reason,
                                        (new_kind == svn_node_none)
                                          ? svn_wc_conflict_action_delete
                                          : svn_wc_conflict_action_replace,
                                        NULL,
                                        scratch_pool, scratch_pool));
      nmb->skip = TRUE;
    }
  else
    {
      /* Delete the WORKING node at DST_RELPATH. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                 STMT_INSERT_DELETE_FROM_NODE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isdd",
                                b->wcroot->wc_id, relpath,
                                0, relpath_depth(relpath)));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Only notify if add_file/add_dir is not going to notify */
  if (conflict || (new_kind == svn_node_none))
    SVN_ERR(update_move_list_add(b->wcroot, relpath, b->db,
                                 svn_wc_notify_update_delete,
                                 new_kind,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable,
                                 conflict, work_items, scratch_pool));
  else if (work_items)
    SVN_ERR(svn_wc__db_wq_add_internal(b->wcroot, work_items,
                                       scratch_pool));

  return SVN_NO_ERROR;
}

/*
 * Driver code.
 *
 * The scenario is that a subtree has been locally moved, and then the base
 * layer on the source side of the move has received an update to a new
 * state.  The destination subtree has not yet been updated, and still
 * matches the pre-update state of the source subtree.
 *
 * The edit driver drives the receiver with the difference between the
 * pre-update state (as found now at the move-destination) and the
 * post-update state (found now at the move-source).
 *
 * We currently assume that both the pre-update and post-update states are
 * single-revision.
 */

/* Return *PROPS, *CHECKSUM, *CHILDREN and *KIND for LOCAL_RELPATH at
   OP_DEPTH provided the row exists.  Return *KIND of svn_node_none if
   the row does not exist, or only describes a delete of a lower op-depth.
   *CHILDREN is a sorted array of basenames of type 'const char *', rather
   than a hash, to allow the driver to process children in a defined order. */
static svn_error_t *
get_info(apr_hash_t **props,
         const svn_checksum_t **checksum,
         apr_array_header_t **children,
         svn_node_kind_t *kind,
         const char *local_relpath,
         int op_depth,
         svn_wc__db_wcroot_t *wcroot,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  const char *repos_relpath;
  svn_node_kind_t db_kind;
  svn_error_t *err;

  err = svn_wc__db_depth_get_info(&status, &db_kind, NULL, &repos_relpath, NULL,
                                  NULL, NULL, NULL, NULL, checksum, NULL,
                                  NULL, props,
                                  wcroot, local_relpath, op_depth,
                                  result_pool, scratch_pool);

  /* If there is no node at this depth, or only a node that describes a delete
     of a lower layer we report this node as not existing. */
  if ((err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      || (!err && status != svn_wc__db_status_added
               && status != svn_wc__db_status_normal))
    {
      svn_error_clear(err);

      if (kind)
        *kind = svn_node_none;
      if (checksum)
        *checksum = NULL;
      if (props)
        *props = NULL;
      if (children)
        *children = apr_array_make(result_pool, 0, sizeof(const char *));

      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  if (kind)
    *kind = db_kind;

  if (children && db_kind == svn_node_dir)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;

      *children = apr_array_make(result_pool, 16, sizeof(const char *));
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_OP_DEPTH_CHILDREN_EXISTS));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      while (have_row)
        {
          const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

          APR_ARRAY_PUSH(*children, const char *)
              = svn_relpath_basename(child_relpath, result_pool);

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else if (children)
    *children = apr_array_make(result_pool, 0, sizeof(const char *));

  return SVN_NO_ERROR;
}

/* Return TRUE if SRC_PROPS and DST_PROPS contain the same properties,
   FALSE otherwise. SRC_PROPS and DST_PROPS are standard property
   hashes. */
static svn_error_t *
props_match(svn_boolean_t *match,
            apr_hash_t *src_props,
            apr_hash_t *dst_props,
            apr_pool_t *scratch_pool)
{
  if (!src_props && !dst_props)
    *match = TRUE;
  else if (!src_props || ! dst_props)
    *match = FALSE;
  else
    {
      apr_array_header_t *propdiffs;

      SVN_ERR(svn_prop_diffs(&propdiffs, src_props, dst_props, scratch_pool));
      *match = propdiffs->nelts ? FALSE : TRUE;
    }
  return SVN_NO_ERROR;
}

/* ### Drive TC_EDITOR so as to ...
 */
static svn_error_t *
update_moved_away_node(node_move_baton_t *nmb,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *src_relpath,
                       const char *dst_relpath,
                       apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_node_kind_t src_kind, dst_kind;
  const svn_checksum_t *src_checksum, *dst_checksum;
  apr_hash_t *src_props, *dst_props;
  apr_array_header_t *src_children, *dst_children;

  if (b->cancel_func)
    SVN_ERR(b->cancel_func(b->cancel_baton));

  SVN_ERR(get_info(&src_props, &src_checksum, &src_children, &src_kind,
                   src_relpath, b->src_op_depth,
                   wcroot, scratch_pool, scratch_pool));

  SVN_ERR(get_info(&dst_props, &dst_checksum, &dst_children, &dst_kind,
                   dst_relpath, b->dst_op_depth,
                   wcroot, scratch_pool, scratch_pool));

  if (src_kind == svn_node_none
      || (dst_kind != svn_node_none && src_kind != dst_kind))
    {
      SVN_ERR(tc_editor_delete(nmb, dst_relpath, dst_kind, src_kind,
                               scratch_pool));
    }

  if (nmb->skip)
    return SVN_NO_ERROR;

  if (src_kind != svn_node_none && src_kind != dst_kind)
    {
      if (src_kind == svn_node_file || src_kind == svn_node_symlink)
        {
          SVN_ERR(tc_editor_add_file(nmb, dst_relpath, dst_kind,
                                     src_checksum, src_props, scratch_pool));
        }
      else if (src_kind == svn_node_dir)
        {
          SVN_ERR(tc_editor_add_directory(nmb, dst_relpath, dst_kind,
                                          src_props, scratch_pool));
        }
    }
  else if (src_kind != svn_node_none)
    {
      svn_boolean_t props_equal;

      SVN_ERR(props_match(&props_equal, src_props, dst_props, scratch_pool));

      if (src_kind == svn_node_file || src_kind == svn_node_symlink)
        {
          if (!props_equal || !svn_checksum_match(src_checksum, dst_checksum))
            SVN_ERR(tc_editor_alter_file(nmb, dst_relpath,
                                         dst_checksum, src_checksum,
                                         dst_props, src_props, scratch_pool));
        }
      else if (src_kind == svn_node_dir)
        {
          if (!props_equal)
            SVN_ERR(tc_editor_alter_directory(nmb, dst_relpath,
                                              dst_props, src_props,
                                              scratch_pool));
        }
    }

  if (nmb->skip)
    return SVN_NO_ERROR;

  if (src_kind == svn_node_dir)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i = 0, j = 0;

      while (i < src_children->nelts || j < dst_children->nelts)
        {
          const char *child_name;
          svn_boolean_t src_only = FALSE, dst_only = FALSE;
          node_move_baton_t cnmb = { 0 };

          cnmb.pb = nmb;
          cnmb.umb = nmb->umb;
          cnmb.shadowed = nmb->shadowed;

          svn_pool_clear(iterpool);
          if (i >= src_children->nelts)
            {
              dst_only = TRUE;
              child_name = APR_ARRAY_IDX(dst_children, j, const char *);
            }
          else if (j >= dst_children->nelts)
            {
              src_only = TRUE;
              child_name = APR_ARRAY_IDX(src_children, i, const char *);
            }
          else
            {
              const char *src_name = APR_ARRAY_IDX(src_children, i,
                                                   const char *);
              const char *dst_name = APR_ARRAY_IDX(dst_children, j,
                                                   const char *);
              int cmp = strcmp(src_name, dst_name);

              if (cmp > 0)
                dst_only = TRUE;
              else if (cmp < 0)
                src_only = TRUE;

              child_name = dst_only ? dst_name : src_name;
            }

          cnmb.src_relpath = svn_relpath_join(src_relpath, child_name,
                                              iterpool);
          cnmb.dst_relpath = svn_relpath_join(dst_relpath, child_name,
                                              iterpool);

          if (!cnmb.shadowed)
            SVN_ERR(check_node_shadowed(&cnmb.shadowed, wcroot,
                                        cnmb.dst_relpath, b->dst_op_depth,
                                        iterpool));

          SVN_ERR(update_moved_away_node(&cnmb, wcroot, cnmb.src_relpath,
                                         cnmb.dst_relpath, iterpool));

          if (!dst_only)
            ++i;
          if (!src_only)
            ++j;

          if (nmb->skip) /* Does parent now want a skip? */
            break;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
suitable_for_move(svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_revnum_t revision;
  const char *repos_relpath;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_trace(svn_sqlite__reset(stmt));

  revision = svn_sqlite__column_revnum(stmt, 4);
  repos_relpath = svn_sqlite__column_text(stmt, 1, scratch_pool);

  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REPOS_PATH_REVISION));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      svn_revnum_t node_revision = svn_sqlite__column_revnum(stmt, 2);
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *relpath;

      svn_pool_clear(iterpool);

      relpath = svn_relpath_skip_ancestor(local_relpath, child_relpath);
      relpath = svn_relpath_join(repos_relpath, relpath, iterpool);

      if (strcmp(relpath, svn_sqlite__column_text(stmt, 1, NULL)))
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                 svn_sqlite__reset(stmt),
                                 _("Cannot apply update because '%s' is a "
                                   "switched path (please switch it back to "
                                   "its original URL and try again)"),
                                 path_for_error_message(wcroot, child_relpath,
                                                        scratch_pool));

      if (revision != node_revision)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                 svn_sqlite__reset(stmt),
                                 _("Cannot apply update because '%s' is a "
                                   "mixed-revision working copy (please "
                                   "update and try again)"),
                                 path_for_error_message(wcroot, local_relpath,
                                                        scratch_pool));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_update_moved_away_conflict_victim(), which see.
 */
static svn_error_t *
update_moved_away_conflict_victim(svn_revnum_t *old_rev,
                                  svn_revnum_t *new_rev,
                                  svn_wc__db_t *db,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *local_relpath,
                                  const char *delete_relpath,
                                  svn_wc_operation_t operation,
                                  svn_wc_conflict_action_t action,
                                  svn_wc_conflict_reason_t reason,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *scratch_pool)
{
  update_move_baton_t umb = { NULL };
  const char *src_relpath, *dst_relpath;
  svn_wc_conflict_version_t old_version;
  svn_wc_conflict_version_t new_version;
  apr_int64_t repos_id;
  node_move_baton_t nmb = { 0 };

  SVN_ERR_ASSERT(svn_relpath_skip_ancestor(delete_relpath, local_relpath));

  /* Construct editor baton. */

  SVN_ERR(find_src_op_depth(&umb.src_op_depth, wcroot,
                            local_relpath, relpath_depth(delete_relpath),
                            scratch_pool));

  SVN_ERR(svn_wc__db_scan_moved_to_internal(&src_relpath, &dst_relpath, NULL,
                                            wcroot, local_relpath,
                                            umb.src_op_depth,
                                            scratch_pool, scratch_pool));

  if (dst_relpath == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("The node '%s' has not been moved away"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  umb.dst_op_depth = relpath_depth(dst_relpath);

  SVN_ERR(verify_write_lock(wcroot, src_relpath, scratch_pool));
  SVN_ERR(verify_write_lock(wcroot, dst_relpath, scratch_pool));


  SVN_ERR(svn_wc__db_depth_get_info(NULL, &new_version.node_kind,
                                    &new_version.peg_rev,
                                    &new_version.path_in_repos, &repos_id,
                                    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                    NULL,
                                    wcroot, src_relpath, umb.src_op_depth,
                                    scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_fetch_repos_info(&new_version.repos_url,
                                      &new_version.repos_uuid,
                                      wcroot, repos_id,
                                      scratch_pool));

  SVN_ERR(svn_wc__db_depth_get_info(NULL, &old_version.node_kind,
                                    &old_version.peg_rev,
                                    &old_version.path_in_repos, &repos_id,
                                    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                    NULL,
                                    wcroot, dst_relpath, umb.dst_op_depth,
                                    scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_fetch_repos_info(&old_version.repos_url,
                                      &old_version.repos_uuid,
                                      wcroot, repos_id,
                                      scratch_pool));
  *old_rev = old_version.peg_rev;
  *new_rev = new_version.peg_rev;

  umb.operation = operation;
  umb.old_version= &old_version;
  umb.new_version= &new_version;
  umb.db = db;
  umb.wcroot = wcroot;
  umb.cancel_func = cancel_func;
  umb.cancel_baton = cancel_baton;

  if (umb.src_op_depth == 0)
    SVN_ERR(suitable_for_move(wcroot, src_relpath, scratch_pool));

  /* Create a new, and empty, list for notification information. */
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  /* Drive the editor... */

  nmb.umb = &umb;
  nmb.src_relpath = src_relpath;
  nmb.dst_relpath = dst_relpath;
  /* nmb.shadowed = FALSE; */
  /* nmb.edited = FALSE; */
  /* nmb.skip_children = FALSE; */

  /* We walk the move source (i.e. the post-update tree), comparing each node
    * with the equivalent node at the move destination and applying the update
    * to nodes at the move destination. */
  SVN_ERR(update_moved_away_node(&nmb, wcroot, src_relpath, dst_relpath,
                                 scratch_pool));

  SVN_ERR(svn_wc__db_op_copy_layer_internal(wcroot, src_relpath,
                                            umb.src_op_depth,
                                            dst_relpath, NULL, NULL,
                                            scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_update_moved_away_conflict_victim(svn_wc__db_t *db,
                                             const char *local_abspath,
                                             const char *delete_op_abspath,
                                             svn_wc_operation_t operation,
                                             svn_wc_conflict_action_t action,
                                             svn_wc_conflict_reason_t reason,
                                             svn_cancel_func_t cancel_func,
                                             void *cancel_baton,
                                             svn_wc_notify_func2_t notify_func,
                                             void *notify_baton,
                                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_revnum_t old_rev, new_rev;
  const char *local_relpath;
  const char *delete_relpath;

  /* ### Check for mixed-rev src or dst? */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  delete_relpath
    = svn_dirent_skip_ancestor(wcroot->abspath, delete_op_abspath);

  SVN_WC__DB_WITH_TXN(
    update_moved_away_conflict_victim(
      &old_rev, &new_rev,
      db, wcroot, local_relpath, delete_relpath,
      operation, action, reason,
      cancel_func, cancel_baton,
      scratch_pool),
    wcroot);

  /* Send all queued up notifications. */
  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot, old_rev, new_rev,
                                             notify_func, notify_baton,
                                             scratch_pool));
  if (notify_func)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                    local_relpath,
                                                    scratch_pool),
                                    svn_wc_notify_update_completed,
                                    scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = svn_wc_notify_state_inapplicable;
      notify->prop_state = svn_wc_notify_state_inapplicable;
      notify->revision = new_rev;
      notify_func(notify_baton, notify, scratch_pool);
    }


  return SVN_NO_ERROR;
}

static svn_error_t *
get_working_info(apr_hash_t **props,
                 const svn_checksum_t **checksum,
                 apr_array_header_t **children,
                 svn_node_kind_t *kind,
                 const char *local_relpath,
                 svn_wc__db_wcroot_t *wcroot,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  const char *repos_relpath;
  svn_node_kind_t db_kind;
  svn_error_t *err;

  err = svn_wc__db_read_info_internal(&status, &db_kind, NULL, &repos_relpath,
                                      NULL, NULL, NULL, NULL, NULL,
                                      checksum,
                                      NULL, NULL, NULL, NULL, NULL,
                                      NULL, NULL, NULL, NULL, NULL,
                                      NULL, NULL, NULL, NULL, NULL,
                                      wcroot, local_relpath,
                                      result_pool, scratch_pool);

  /* If there is no node, or only a node that describes a delete
     of a lower layer we report this node as not existing. */
  if ((err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      || (!err && status != svn_wc__db_status_added
               && status != svn_wc__db_status_normal))
    {
      svn_error_clear(err);

      if (kind)
        *kind = svn_node_none;
      if (checksum)
        *checksum = NULL;
      if (props)
        *props = NULL;
      if (children)
        *children = apr_array_make(result_pool, 0, sizeof(const char *));

      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  SVN_ERR(svn_wc__db_read_props_internal(props, wcroot, local_relpath,
                                         result_pool, scratch_pool));

  if (kind)
    *kind = db_kind;

  if (children && db_kind == svn_node_dir)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;

      *children = apr_array_make(result_pool, 16, sizeof(const char *));
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_WORKING_CHILDREN));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      while (have_row)
        {
          const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

          APR_ARRAY_PUSH(*children, const char *)
              = svn_relpath_basename(child_relpath, result_pool);

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else if (children)
    *children = apr_array_make(result_pool, 0, sizeof(const char *));

  return SVN_NO_ERROR;
}

/* Apply changes found in the victim node at SRC_RELPATH to the incoming
 * move at DST_RELPATH. */
static svn_error_t *
update_incoming_moved_node(node_move_baton_t *nmb,
                           svn_wc__db_wcroot_t *wcroot,
                           const char *src_relpath,
                           const char *dst_relpath,
                           apr_pool_t *scratch_pool)
{
  update_move_baton_t *b = nmb->umb;
  svn_node_kind_t orig_kind, working_kind;
  const char *victim_relpath = src_relpath;
  const svn_checksum_t *orig_checksum, *working_checksum;
  apr_hash_t *orig_props, *working_props;
  apr_array_header_t *orig_children, *working_children;

  if (b->cancel_func)
    SVN_ERR(b->cancel_func(b->cancel_baton));

  /* Compare the tree conflict victim's copied layer (the "original") with
   * the working layer, i.e. look for changes layered on top of the copy. */
  SVN_ERR(get_info(&orig_props, &orig_checksum, &orig_children, &orig_kind,
                   victim_relpath, b->src_op_depth, wcroot, scratch_pool,
                   scratch_pool));
  SVN_ERR(get_working_info(&working_props, &working_checksum,
                           &working_children, &working_kind, victim_relpath,
                           wcroot, scratch_pool, scratch_pool));

  if (working_kind == svn_node_none
      || (orig_kind != svn_node_none && orig_kind != working_kind))
    {
      SVN_ERR(tc_incoming_editor_delete(nmb, dst_relpath, orig_kind,
                                        working_kind, scratch_pool));
    }

  if (nmb->skip)
    return SVN_NO_ERROR;

  if (working_kind != svn_node_none && orig_kind != working_kind)
    {
      if (working_kind == svn_node_file || working_kind == svn_node_symlink)
        {
          const char *victim_abspath;
          const char *wctemp_abspath;
          svn_stream_t *working_stream;
          svn_stream_t *temp_stream;
          const char *temp_abspath;
          svn_error_t *err;

          /* Copy the victim's content to a safe place and add it from there. */
          SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&wctemp_abspath, b->db,
                                                 b->wcroot->abspath,
                                                 scratch_pool,
                                                 scratch_pool));
          victim_abspath = svn_dirent_join(b->wcroot->abspath,
                                           victim_relpath, scratch_pool);
          SVN_ERR(svn_stream_open_readonly(&working_stream, victim_abspath,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_open_unique(&temp_stream, &temp_abspath,
                                         wctemp_abspath, svn_io_file_del_none,
                                         scratch_pool, scratch_pool));
          err = svn_stream_copy3(working_stream, temp_stream, 
                                 b->cancel_func, b->cancel_baton,
                                 scratch_pool);
          if (err && err->apr_err == SVN_ERR_CANCELLED)
            {
              svn_error_t *err2;

              err2 = svn_io_remove_file2(temp_abspath, TRUE, scratch_pool);
              return svn_error_compose_create(err, err2);
            }
          else
            SVN_ERR(err);

          SVN_ERR(tc_editor_incoming_add_file(nmb, dst_relpath, orig_kind,
                                              working_checksum, working_props,
                                              victim_relpath, temp_abspath,
                                              scratch_pool));
        }
      else if (working_kind == svn_node_dir)
        {
          SVN_ERR(tc_editor_incoming_add_directory(nmb, dst_relpath,
                                                   orig_kind, working_props,
                                                   victim_relpath,
                                                   scratch_pool));
        }
    }
  else if (working_kind != svn_node_none)
    {
      svn_boolean_t props_equal;

      SVN_ERR(props_match(&props_equal, orig_props, working_props,
                          scratch_pool));

      if (working_kind == svn_node_file || working_kind == svn_node_symlink)
        {
          svn_boolean_t is_modified;

          SVN_ERR(svn_wc__internal_file_modified_p(&is_modified, b->db,
                                                   svn_dirent_join(
                                                     b->wcroot->abspath,
                                                     victim_relpath,
                                                     scratch_pool),
                                                   FALSE /* exact_comparison */,
                                                   scratch_pool));
          if (!props_equal || is_modified)
            SVN_ERR(tc_editor_update_incoming_moved_file(nmb, dst_relpath,
                                                         victim_relpath,
                                                         working_checksum,
                                                         orig_checksum,
                                                         orig_props,
                                                         working_props,
                                                         is_modified,
                                                         scratch_pool));
        }
      else if (working_kind == svn_node_dir)
        {
          if (!props_equal)
            SVN_ERR(tc_editor_alter_directory(nmb, dst_relpath,
                                              orig_props, working_props,
                                              scratch_pool));
        }
    }

  if (nmb->skip)
    return SVN_NO_ERROR;

  if (working_kind == svn_node_dir)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i = 0, j = 0;

      while (i < orig_children->nelts || j < working_children->nelts)
        {
          const char *child_name;
          svn_boolean_t orig_only = FALSE, working_only = FALSE;
          node_move_baton_t cnmb = { 0 };

          cnmb.pb = nmb;
          cnmb.umb = nmb->umb;
          cnmb.shadowed = nmb->shadowed;

          svn_pool_clear(iterpool);
          if (i >= orig_children->nelts)
            {
              working_only = TRUE;
              child_name = APR_ARRAY_IDX(working_children, j, const char *);
            }
          else if (j >= working_children->nelts)
            {
              orig_only = TRUE;
              child_name = APR_ARRAY_IDX(orig_children, i, const char *);
            }
          else
            {
              const char *orig_name = APR_ARRAY_IDX(orig_children, i,
                                                    const char *);
              const char *working_name = APR_ARRAY_IDX(working_children, j,
                                                       const char *);
              int cmp = strcmp(orig_name, working_name);

              if (cmp > 0)
                working_only = TRUE;
              else if (cmp < 0)
                orig_only = TRUE;

              child_name = working_only ? working_name : orig_name;
            }

          cnmb.src_relpath = svn_relpath_join(src_relpath, child_name,
                                              iterpool);
          cnmb.dst_relpath = svn_relpath_join(dst_relpath, child_name,
                                              iterpool);

          SVN_ERR(update_incoming_moved_node(&cnmb, wcroot, cnmb.src_relpath,
                                             cnmb.dst_relpath, iterpool));

          if (!working_only)
            ++i;
          if (!orig_only)
            ++j;

          if (nmb->skip) /* Does parent now want a skip? */
            break;
        }
    }

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_update_incoming_move(). */
static svn_error_t *
update_incoming_move(svn_revnum_t *old_rev,
                    svn_revnum_t *new_rev,
                    svn_wc__db_t *db,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    const char *dst_relpath,
                    svn_wc_operation_t operation,
                    svn_wc_conflict_action_t action,
                    svn_wc_conflict_reason_t reason,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  update_move_baton_t umb = { NULL };
  svn_wc_conflict_version_t old_version;
  svn_wc_conflict_version_t new_version;
  apr_int64_t repos_id;
  node_move_baton_t nmb = { 0 };
  svn_boolean_t is_modified;

  SVN_ERR_ASSERT(svn_relpath_skip_ancestor(dst_relpath, local_relpath) == NULL);

  /* For incoming moves during update/switch, the move source is a copied
   * tree which was copied from the pre-update BASE revision while raising
   * the tree conflict, when the update attempted to delete the move source.
   * This copy is our "original" state (SRC of the diff) and the local changes
   * on top of this copy at the top-most WORKING layer are used to drive the
   * editor (DST of the diff).
   *
   * The move destination, where changes are applied to, is now in the BASE
   * tree at DST_RELPATH. This repository-side move is the "incoming change"
   * recorded for any tree conflicts created during the editor drive.
   * We assume this path contains no local changes, and create local changes
   * in DST_RELPATH corresponding to changes contained in the conflict victim.
   * 
   * DST_OP_DEPTH is used to infer the "op-root" of the incoming move. This
   * "op-root" is virtual because all nodes belonging to the incoming move
   * live in the BASE tree. It is used for constructing repository paths
   * when new tree conflicts need to be raised.
   */
  umb.src_op_depth = relpath_depth(local_relpath); /* SRC of diff */
  umb.dst_op_depth = relpath_depth(dst_relpath); /* virtual DST op-root */

  SVN_ERR(verify_write_lock(wcroot, local_relpath, scratch_pool));
  SVN_ERR(verify_write_lock(wcroot, dst_relpath, scratch_pool));

  /* Make sure there are no local modifications in the move destination. */
  SVN_ERR(svn_wc__node_has_local_mods(&is_modified, NULL, db,
                                      svn_dirent_join(wcroot->abspath,
                                                      dst_relpath,
                                                      scratch_pool),
                                      TRUE, cancel_func, cancel_baton,
                                      scratch_pool));
  if (is_modified)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Cannot merge local changes from '%s' because "
                               "'%s' already contains other local changes "
                               "(please commit or revert these other changes "
                               "and try again)"),
                             svn_dirent_local_style(
                               svn_dirent_join(wcroot->abspath, local_relpath,
                                               scratch_pool),
                               scratch_pool),
                             svn_dirent_local_style(
                               svn_dirent_join(wcroot->abspath, dst_relpath,
                                               scratch_pool),
                               scratch_pool));

  /* Check for switched subtrees and mixed-revision working copy. */
  SVN_ERR(suitable_for_move(wcroot, dst_relpath, scratch_pool));

  /* Read version info from the updated incoming post-move location. */
  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &new_version.node_kind,
                                            &new_version.peg_rev,
                                            &new_version.path_in_repos,
                                            &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, dst_relpath,
                                            scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_fetch_repos_info(&new_version.repos_url,
                                      &new_version.repos_uuid,
                                      wcroot, repos_id,
                                      scratch_pool));

  /* Read version info from the victim's location. */
  SVN_ERR(svn_wc__db_depth_get_info(NULL, &old_version.node_kind,
                                    &old_version.peg_rev,
                                    &old_version.path_in_repos, &repos_id,
                                    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                    NULL, wcroot,
                                    local_relpath, umb.src_op_depth,
                                    scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_fetch_repos_info(&old_version.repos_url,
                                      &old_version.repos_uuid,
                                      wcroot, repos_id,
                                      scratch_pool));
  *old_rev = old_version.peg_rev;
  *new_rev = new_version.peg_rev;

  umb.operation = operation;
  umb.old_version= &old_version;
  umb.new_version= &new_version;
  umb.db = db;
  umb.wcroot = wcroot;
  umb.cancel_func = cancel_func;
  umb.cancel_baton = cancel_baton;

  /* Create a new, and empty, list for notification information. */
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  /* Drive the editor... */

  nmb.umb = &umb;
  nmb.src_relpath = local_relpath;
  nmb.dst_relpath = dst_relpath;
  /* nmb.shadowed = FALSE; */
  /* nmb.edited = FALSE; */
  /* nmb.skip_children = FALSE; */

  /* We walk the conflict victim, comparing each node with the equivalent node
   * at the WORKING layer, applying any local changes to nodes at the move
   * destination. */
  SVN_ERR(update_incoming_moved_node(&nmb, wcroot, local_relpath, dst_relpath,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_update_incoming_move(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *dest_abspath,
                                svn_wc_operation_t operation,
                                svn_wc_conflict_action_t action,
                                svn_wc_conflict_reason_t reason,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                svn_wc_notify_func2_t notify_func,
                                void *notify_baton,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_revnum_t old_rev, new_rev;
  const char *local_relpath;
  const char *dest_relpath;

  /* ### Check for mixed-rev src or dst? */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  dest_relpath
    = svn_dirent_skip_ancestor(wcroot->abspath, dest_abspath);

  SVN_WC__DB_WITH_TXN(update_incoming_move(&old_rev, &new_rev, db, wcroot,
                                           local_relpath, dest_relpath,
                                           operation, action, reason,
                                           cancel_func, cancel_baton,
                                           scratch_pool),
                      wcroot);

  /* Send all queued up notifications. */
  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot, old_rev, new_rev,
                                             notify_func, notify_baton,
                                             scratch_pool));
  if (notify_func)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                    local_relpath,
                                                    scratch_pool),
                                    svn_wc_notify_update_completed,
                                    scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = svn_wc_notify_state_inapplicable;
      notify->prop_state = svn_wc_notify_state_inapplicable;
      notify->revision = new_rev;
      notify_func(notify_baton, notify, scratch_pool);
    }


  return SVN_NO_ERROR;
}

typedef struct update_local_add_baton_t {
  int add_op_depth;
  svn_wc__db_t *db;
  svn_wc__db_wcroot_t *wcroot;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* We refer to these if raising new tree conflicts. */
  const svn_wc_conflict_version_t *new_version;
} update_local_add_baton_t;

typedef struct added_node_baton_t {
  struct update_local_add_baton_t *b;
  struct added_node_baton_t *pb;
  const char *local_relpath;
  svn_boolean_t skip;
  svn_boolean_t edited;
} added_node_baton_t;


static svn_error_t *
update_local_add_mark_node_edited(added_node_baton_t *nb,
                                  apr_pool_t *scratch_pool)
{
  if (nb->edited)
    return SVN_NO_ERROR;

  if (nb->pb)
    {
      SVN_ERR(update_local_add_mark_node_edited(nb->pb, scratch_pool));

      if (nb->pb->skip)
        nb->skip = TRUE;
    }

  nb->edited = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
update_local_add_mark_parent_edited(added_node_baton_t *nb,
                                    apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(nb && nb->pb);

  SVN_ERR(update_local_add_mark_node_edited(nb->pb, scratch_pool));

  if (nb->pb->skip)
    nb->skip = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
mark_update_add_add_tree_conflict(added_node_baton_t *nb,
                                  svn_node_kind_t base_kind,
                                  svn_node_kind_t working_kind,
                                  svn_wc_conflict_reason_t local_change,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)

{
  svn_wc__db_t *db = nb->b->db;
  svn_wc__db_wcroot_t *wcroot = nb->b->wcroot;
  svn_wc_conflict_version_t *new_version;
  svn_skel_t *conflict;

  new_version = svn_wc_conflict_version_dup(nb->b->new_version, result_pool);

  /* Fill in conflict info templates with info for this node. */
  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, &new_version->peg_rev,
                                            &new_version->path_in_repos,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, nb->local_relpath,
                                            scratch_pool, scratch_pool));
  new_version->node_kind = base_kind;

  SVN_ERR(create_tree_conflict(&conflict, wcroot, nb->local_relpath,
                               nb->local_relpath, db, NULL, new_version,
                               svn_wc_operation_update,
                               svn_node_none, base_kind, NULL,
                               local_change, svn_wc_conflict_action_add,
                               NULL, scratch_pool, scratch_pool));

  SVN_ERR(update_move_list_add(wcroot, nb->local_relpath, db,
                               svn_wc_notify_tree_conflict, working_kind,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
update_local_add_notify_obstructed_or_missing(added_node_baton_t *nb,
                                              svn_node_kind_t working_kind,
                                              svn_node_kind_t kind_on_disk,
                                              apr_pool_t *scratch_pool)
{
  svn_wc_notify_state_t content_state;

  if (kind_on_disk == svn_node_none)
      content_state = svn_wc_notify_state_missing;
  else
      content_state = svn_wc_notify_state_obstructed;

  SVN_ERR(update_move_list_add(nb->b->wcroot, nb->local_relpath, nb->b->db,
                               svn_wc_notify_skip, working_kind,
                               content_state, svn_wc_notify_state_inapplicable,
                               NULL, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_update_add_new_file(added_node_baton_t *nb,
                              svn_node_kind_t base_kind,
                              const svn_checksum_t *base_checksum,
                              apr_hash_t *base_props,
                              svn_node_kind_t working_kind,
                              const svn_checksum_t *working_checksum,
                              apr_hash_t *working_props,
                              apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_node_kind_t kind_on_disk;

  SVN_ERR(update_local_add_mark_parent_edited(nb, scratch_pool));
  if (nb->skip)
    return SVN_NO_ERROR;

  if (base_kind != svn_node_none)
    {
      SVN_ERR(mark_update_add_add_tree_conflict(nb, base_kind, svn_node_file,
                                                svn_wc_conflict_reason_added,
                                                scratch_pool, scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }
  
  /* Check for obstructions. */
  local_abspath = svn_dirent_join(nb->b->wcroot->abspath, nb->local_relpath,
                                  scratch_pool);
  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  if (kind_on_disk != svn_node_file)
    {
      SVN_ERR(update_local_add_notify_obstructed_or_missing(nb, working_kind,
                                                            kind_on_disk,
                                                            scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  /* Nothing else to do. Locally added files are an op-root in NODES. */

  SVN_ERR(update_move_list_add(nb->b->wcroot, nb->local_relpath, nb->b->db,
                               svn_wc_notify_update_add, svn_node_file,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               NULL, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_update_add_new_directory(added_node_baton_t *nb,
                                   svn_node_kind_t base_kind,
                                   apr_hash_t *base_props,
                                   apr_hash_t *working_props,
                                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_node_kind_t kind_on_disk;

  SVN_ERR(update_local_add_mark_parent_edited(nb, scratch_pool));
  if (nb->skip)
    return SVN_NO_ERROR;

  if (base_kind != svn_node_none)
    {
      SVN_ERR(mark_update_add_add_tree_conflict(nb, base_kind, svn_node_dir,
                                                svn_wc_conflict_reason_added,
                                                scratch_pool, scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  /* Check for obstructions. */
  local_abspath = svn_dirent_join(nb->b->wcroot->abspath, nb->local_relpath,
                                  scratch_pool);
  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  if (kind_on_disk != svn_node_dir)
    {
      SVN_ERR(update_local_add_notify_obstructed_or_missing(nb, svn_node_dir,
                                                            kind_on_disk,
                                                            scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  /* Nothing else to do. Locally added directories are an op-root in NODES. */

  SVN_ERR(update_move_list_add(nb->b->wcroot, nb->local_relpath, nb->b->db,
                               svn_wc_notify_update_add, svn_node_dir,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               NULL, NULL, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
update_incoming_add_merge_props(svn_wc_notify_state_t *prop_state,
                                svn_skel_t **conflict_skel,
                                const char *local_relpath,
                                apr_hash_t *base_props,
                                apr_hash_t *working_props,
                                svn_wc__db_t *db,
                                svn_wc__db_wcroot_t *wcroot,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  apr_hash_t *new_actual_props;
  apr_array_header_t *propchanges;
  const char *local_abspath = svn_dirent_join(wcroot->abspath,
                                              local_relpath,
                                              scratch_pool);

  /*
   * Run a 3-way prop merge to update the props, using the empty props
   * as the merge base, the post-update props as the merge-left version, and
   * the current props of the added working file as the merge-right version.
   */
  SVN_ERR(svn_prop_diffs(&propchanges, working_props,
                         apr_hash_make(scratch_pool), scratch_pool));
  SVN_ERR(svn_wc__merge_props(conflict_skel, prop_state, &new_actual_props,
                              db, local_abspath,
                              apr_hash_make(scratch_pool),
                              base_props, working_props, propchanges,
                              result_pool, scratch_pool));

  /* Install the new actual props. */
  if (apr_hash_count(new_actual_props) > 0)
    SVN_ERR(svn_wc__db_op_set_props_internal(wcroot, local_relpath,
                                             new_actual_props,
                                             svn_wc__has_magic_property(
                                                      propchanges),
                                             scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_update_add_merge_files(added_node_baton_t *nb,
                                 const svn_checksum_t *working_checksum,
                                 const svn_checksum_t *base_checksum,
                                 apr_hash_t *working_props,
                                 apr_hash_t *base_props,
                                 apr_pool_t *scratch_pool)
{
  update_local_add_baton_t *b = nb->b;
  apr_array_header_t *propchanges;
  svn_boolean_t is_modified;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_skel_t *conflict_skel = NULL;
  svn_wc_notify_state_t prop_state, content_state;
  svn_skel_t *work_items = NULL;
  svn_node_kind_t kind_on_disk;
  const char *local_abspath = svn_dirent_join(b->wcroot->abspath,
                                              nb->local_relpath,
                                              scratch_pool);

  SVN_ERR(update_local_add_mark_node_edited(nb, scratch_pool));
  if (nb->skip)
    return SVN_NO_ERROR;

  /* Check for on-disk obstructions or missing files. */
  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  if (kind_on_disk != svn_node_file)
    {
      SVN_ERR(update_local_add_notify_obstructed_or_missing(nb, svn_node_file,
                                                            kind_on_disk,
                                                            scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(update_incoming_add_merge_props(&prop_state, &conflict_skel,
                                          nb->local_relpath,
                                          base_props, working_props,
                                          b->db, b->wcroot,
                                          scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__internal_file_modified_p(&is_modified,
                                           b->db, local_abspath,
                                           FALSE /* exact_comparison */,
                                           scratch_pool));
  if (!is_modified)
    {
      svn_skel_t *work_item = NULL;

      SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db,
                                            local_abspath, NULL,
                                            /* FIXME: use_commit_times? */
                                            FALSE,
                                            TRUE,  /* record_file_info */
                                            scratch_pool, scratch_pool));
      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
      content_state = svn_wc_notify_state_changed;
    }
  else
    {
      const char *empty_file_abspath;
      const char *pristine_abspath;
      svn_skel_t *work_item = NULL;

      /*
       * Run a 3-way merge to update the file, using the empty file
       * merge base, the post-update pristine text as the merge-left version,
       * and the locally added content of the working file as the merge-right
       * version.
       */
      SVN_ERR(svn_io_open_unique_file3(NULL, &empty_file_abspath, NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       scratch_pool, scratch_pool));
      SVN_ERR(svn_wc__db_pristine_get_path(&pristine_abspath, b->db,
                                           b->wcroot->abspath, base_checksum,
                                           scratch_pool, scratch_pool));

      /* Create a property diff which shows all props as added. */
      SVN_ERR(svn_prop_diffs(&propchanges, working_props,
                             apr_hash_make(scratch_pool), scratch_pool));

      SVN_ERR(svn_wc__internal_merge(&work_item, &conflict_skel,
                                     &merge_outcome, b->db,
                                     empty_file_abspath,
                                     pristine_abspath,
                                     local_abspath,
                                     local_abspath,
                                     NULL, NULL, NULL, /* diff labels */
                                     apr_hash_make(scratch_pool),
                                     FALSE, /* dry-run */
                                     NULL, /* diff3-cmd */
                                     NULL, /* merge options */
                                     propchanges,
                                     b->cancel_func, b->cancel_baton,
                                     scratch_pool, scratch_pool));

      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);

      if (merge_outcome == svn_wc_merge_conflict)
        content_state = svn_wc_notify_state_conflicted;
      else
        content_state = svn_wc_notify_state_merged;
    }

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      svn_wc_conflict_version_t *new_version;
      svn_node_kind_t new_kind;
      svn_revnum_t new_rev;
      const char *repos_relpath;

      new_version = svn_wc_conflict_version_dup(nb->b->new_version,
                                                scratch_pool);
      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &new_kind, &new_rev,
                                                &repos_relpath, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL,
                                                b->wcroot, nb->local_relpath,
                                                scratch_pool, scratch_pool));
      /* Fill in conflict info templates with info for this node. */
      new_version->path_in_repos = repos_relpath;
      new_version->node_kind = new_kind;
      new_version->peg_rev = new_rev;

      /* Create conflict markers. */
      SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel, NULL,
                                                  new_version, scratch_pool,
                                                  scratch_pool));
      if (prop_state == svn_wc_notify_state_conflicted)
        SVN_ERR(svn_wc__conflict_create_markers(&work_items, b->db,
                                                local_abspath,
                                                conflict_skel,
                                                scratch_pool,
                                                scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, nb->local_relpath, b->db,
                               svn_wc_notify_update_update,
                               svn_node_file, content_state, prop_state,
                               conflict_skel, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_update_add_merge_dirprops(added_node_baton_t *nb,
                                    apr_hash_t *working_props,
                                    apr_hash_t *base_props,
                                    apr_pool_t *scratch_pool)
{
  update_local_add_baton_t *b = nb->b;
  svn_skel_t *conflict_skel = NULL;
  svn_wc_notify_state_t prop_state;
  svn_skel_t *work_items = NULL;
  svn_node_kind_t kind_on_disk;
  const char *local_abspath = svn_dirent_join(b->wcroot->abspath,
                                              nb->local_relpath,
                                              scratch_pool);

  SVN_ERR(update_local_add_mark_node_edited(nb, scratch_pool));
  if (nb->skip)
    return SVN_NO_ERROR;

  /* Check for on-disk obstructions or missing files. */
  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  if (kind_on_disk != svn_node_dir)
    {
      SVN_ERR(update_local_add_notify_obstructed_or_missing(nb, svn_node_dir,
                                                            kind_on_disk,
                                                            scratch_pool));
      nb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(update_incoming_add_merge_props(&prop_state, &conflict_skel,
                                          nb->local_relpath,
                                          base_props, working_props,
                                          b->db, b->wcroot,
                                          scratch_pool, scratch_pool));

  /* If there are any conflicts to be stored, convert them into work items. */
  if (conflict_skel && prop_state == svn_wc_notify_state_conflicted)
    {
      svn_wc_conflict_version_t *new_version;
      svn_node_kind_t new_kind;
      svn_revnum_t new_rev;
      const char *repos_relpath;

      new_version = svn_wc_conflict_version_dup(nb->b->new_version,
                                                scratch_pool);
      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &new_kind, &new_rev,
                                                &repos_relpath, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL,
                                                b->wcroot, nb->local_relpath,
                                                scratch_pool, scratch_pool));
      /* Fill in conflict info templates with info for this node. */
      new_version->path_in_repos = repos_relpath;
      new_version->node_kind = new_kind;
      new_version->peg_rev = new_rev;

      /* Create conflict markers. */
      SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel, NULL,
                                                  new_version, scratch_pool,
                                                  scratch_pool));
      SVN_ERR(svn_wc__conflict_create_markers(&work_items, b->db,
                                              local_abspath,
                                              conflict_skel,
                                              scratch_pool,
                                              scratch_pool));
    }

  SVN_ERR(update_move_list_add(b->wcroot, nb->local_relpath, b->db,
                               svn_wc_notify_update_update, svn_node_dir,
                               svn_wc_notify_state_inapplicable, prop_state,
                               conflict_skel, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
update_locally_added_node(added_node_baton_t *nb,
                          apr_pool_t *scratch_pool)
{
  update_local_add_baton_t *b = nb->b;
  svn_wc__db_wcroot_t *wcroot = b->wcroot;
  svn_wc__db_t *db = b->db;
  svn_node_kind_t base_kind, working_kind;
  const svn_checksum_t *base_checksum;
  apr_hash_t *base_props, *working_props;
  apr_array_header_t *base_children, *working_children;
  const char *local_abspath = svn_dirent_join(wcroot->abspath,
                                              nb->local_relpath,
                                              scratch_pool);

  if (b->cancel_func)
    SVN_ERR(b->cancel_func(b->cancel_baton));

  if (nb->skip)
    return SVN_NO_ERROR;

  /* Compare the tree conflict victim's BASE layer to the working layer. */
  SVN_ERR(get_info(&base_props, &base_checksum, &base_children, &base_kind,
                   nb->local_relpath, 0, wcroot, scratch_pool, scratch_pool));
  SVN_ERR(get_working_info(&working_props, NULL, &working_children,
                           &working_kind, nb->local_relpath, wcroot,
                           scratch_pool, scratch_pool));
  if (working_kind == svn_node_none)
    {
      svn_node_kind_t kind_on_disk;
      svn_skel_t *work_item = NULL;

      /* Skip obstructed nodes. */
      SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk,
                                scratch_pool));
      if (kind_on_disk != base_kind && kind_on_disk != svn_node_none)
        {
          SVN_ERR(update_move_list_add(nb->b->wcroot, nb->local_relpath,
                                       nb->b->db,
                                       svn_wc_notify_skip,
                                       base_kind,
                                       svn_wc_notify_state_obstructed,
                                       svn_wc_notify_state_inapplicable,
                                       NULL, NULL, scratch_pool));
          nb->skip = TRUE;
          return SVN_NO_ERROR;
        }

      /* The working tree has no node here. The working copy of this node
       * is currently not installed because the base tree is shadowed.
       * Queue an installation of this node into the working copy. */
      if (base_kind == svn_node_file || base_kind == svn_node_symlink)
        SVN_ERR(svn_wc__wq_build_file_install(&work_item, db, local_abspath,
                                              NULL,
                                              /* FIXME: use_commit_times? */
                                              FALSE,
                                              TRUE,  /* record_file_info */
                                              scratch_pool, scratch_pool));
      else if (base_kind == svn_node_dir)
        SVN_ERR(svn_wc__wq_build_dir_install(&work_item, db, local_abspath,
                                             scratch_pool, scratch_pool));

      if (work_item)
        SVN_ERR(update_move_list_add(wcroot, nb->local_relpath, db,
                                     svn_wc_notify_update_add,
                                     base_kind,
                                     svn_wc_notify_state_inapplicable,
                                     svn_wc_notify_state_inapplicable,
                                     NULL, work_item, scratch_pool));
      return SVN_NO_ERROR;
    }

  if (base_kind != working_kind)
    {
      if (working_kind == svn_node_file || working_kind == svn_node_symlink)
        {
          svn_checksum_t *working_checksum = NULL;

          if (base_checksum)
            SVN_ERR(svn_io_file_checksum2(&working_checksum, local_abspath,
                                          base_checksum->kind, scratch_pool));
          SVN_ERR(tc_editor_update_add_new_file(nb, base_kind, base_checksum,
                                                base_props, working_kind,
                                                working_checksum, working_props,
                                                scratch_pool));
        }
      else if (working_kind == svn_node_dir)
        SVN_ERR(tc_editor_update_add_new_directory(nb, base_kind, base_props,
                                                   working_props,
                                                   scratch_pool));
    }
  else
    {
      svn_boolean_t props_equal;

      SVN_ERR(props_match(&props_equal, base_props, working_props,
                          scratch_pool));

      if (working_kind == svn_node_file || working_kind == svn_node_symlink)
        {
          svn_checksum_t *working_checksum;

          SVN_ERR_ASSERT(base_checksum);
          SVN_ERR(svn_io_file_checksum2(&working_checksum, local_abspath,
                                        base_checksum->kind, scratch_pool));
          if (!props_equal || !svn_checksum_match(base_checksum,
                                                  working_checksum))
            SVN_ERR(tc_editor_update_add_merge_files(nb, working_checksum,
                                                     base_checksum,
                                                     working_props, base_props,
                                                     scratch_pool));
        }
      else if (working_kind == svn_node_dir && !props_equal)
        SVN_ERR(tc_editor_update_add_merge_dirprops(nb, working_props,
                                                    base_props,
                                                    scratch_pool));
    }

  if (nb->skip)
    return SVN_NO_ERROR;

  if (working_kind == svn_node_dir)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i = 0, j = 0;

      while (i < base_children->nelts || j < working_children->nelts)
        {
          const char *child_name;
          svn_boolean_t base_only = FALSE, working_only = FALSE;
          added_node_baton_t cnb = { 0 };

          cnb.pb = nb;
          cnb.b = nb->b;
          cnb.skip = FALSE;

          svn_pool_clear(iterpool);
          if (i >= base_children->nelts)
            {
              working_only = TRUE;
              child_name = APR_ARRAY_IDX(working_children, j, const char *);
            }
          else if (j >= working_children->nelts)
            {
              base_only = TRUE;
              child_name = APR_ARRAY_IDX(base_children, i, const char *);
            }
          else
            {
              const char *base_name = APR_ARRAY_IDX(base_children, i,
                                                    const char *);
              const char *working_name = APR_ARRAY_IDX(working_children, j,
                                                       const char *);
              int cmp = strcmp(base_name, working_name);

              if (cmp > 0)
                working_only = TRUE;
              else if (cmp < 0)
                base_only = TRUE;

              child_name = working_only ? working_name : base_name;
            }

          cnb.local_relpath = svn_relpath_join(nb->local_relpath, child_name,
                                               iterpool);

          SVN_ERR(update_locally_added_node(&cnb, iterpool));

          if (!working_only)
            ++i;
          if (!base_only)
            ++j;

          if (nb->skip) /* Does parent now want a skip? */
            break;
        }
    }

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_update_local_add(). */
static svn_error_t *
update_local_add(svn_revnum_t *new_rev,
                svn_wc__db_t *db,
                svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  update_local_add_baton_t b = { 0 };
  added_node_baton_t nb = { 0 };
  const char *repos_root_url;
  const char *repos_uuid;
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_node_kind_t new_kind;
  svn_sqlite__stmt_t *stmt;

  b.add_op_depth = relpath_depth(local_relpath); /* DST op-root */

  SVN_ERR(verify_write_lock(wcroot, local_relpath, scratch_pool));

  b.db = db;
  b.wcroot = wcroot;
  b.cancel_func = cancel_func;
  b.cancel_baton = cancel_baton;

  /* Read new version info from the updated BASE node. */
  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &new_kind, new_rev,
                                            &repos_relpath, &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_fetch_repos_info(&repos_root_url, &repos_uuid, wcroot,
                                      repos_id, scratch_pool));
  b.new_version = svn_wc_conflict_version_create2(repos_root_url, repos_uuid,
                                                  repos_relpath, *new_rev,
                                                  new_kind, scratch_pool);

  /* Create a new, and empty, list for notification information. */
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  /* Drive the editor... */
  nb.b = &b;
  nb.local_relpath = local_relpath;
  nb.skip = FALSE;
  SVN_ERR(update_locally_added_node(&nb, scratch_pool));

  /* The conflict victim is now part of the base tree.
   * Remove the locally added version of the conflict victim and its children.
   * Any children we want to retain are at a higher op-depth so they won't
   * be deleted by this statement. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WORKING_OP_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            relpath_depth(local_relpath)));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  /* Remove the tree conflict marker. */
  SVN_ERR(svn_wc__db_op_mark_resolved_internal(wcroot, local_relpath, db,
                                               FALSE, FALSE, TRUE,
                                               NULL, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_update_local_add(svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_revnum_t new_rev;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(update_local_add(&new_rev, db, wcroot,
                                       local_relpath, 
                                       cancel_func, cancel_baton,
                                       scratch_pool),
                      wcroot);

  /* Send all queued up notifications. */
  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot, new_rev, new_rev,
                                             notify_func, notify_baton,
                                             scratch_pool));
  if (notify_func)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                    local_relpath,
                                                    scratch_pool),
                                    svn_wc_notify_update_completed,
                                    scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = svn_wc_notify_state_inapplicable;
      notify->prop_state = svn_wc_notify_state_inapplicable;
      notify->revision = new_rev;
      notify_func(notify_baton, notify, scratch_pool);
    }


  return SVN_NO_ERROR;
}
/* Set *CAN_BUMP to TRUE if DEPTH is sufficient to cover the entire
   tree  LOCAL_RELPATH at OP_DEPTH, to FALSE otherwise. */
static svn_error_t *
depth_sufficient_to_bump(svn_boolean_t *can_bump,
                         svn_wc__db_wcroot_t *wcroot,
                         const char *local_relpath,
                         int op_depth,
                         svn_depth_t depth,
                         apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  switch (depth)
    {
    case svn_depth_infinity:
      *can_bump = TRUE;
      return SVN_NO_ERROR;

    case svn_depth_empty:
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_OP_DEPTH_CHILDREN));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath, op_depth));
      break;

    case svn_depth_files:
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_HAS_NON_FILE_CHILDREN));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath, op_depth));
      break;

    case svn_depth_immediates:
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_HAS_GRANDCHILDREN));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath, op_depth));
      break;
    default:
      SVN_ERR_MALFUNCTION();
    }
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  *can_bump = !have_row;
  return SVN_NO_ERROR;
}

/* Mark a move-edit conflict on MOVE_SRC_ROOT_RELPATH. */
static svn_error_t *
bump_mark_tree_conflict(svn_wc__db_wcroot_t *wcroot,
                        const char *move_src_root_relpath,
                        int src_op_depth,
                        const char *move_src_op_root_relpath,
                        const char *move_dst_op_root_relpath,
                        svn_wc__db_t *db,
                        apr_pool_t *scratch_pool)
{
  apr_int64_t repos_id;
  const char *repos_root_url;
  const char *repos_uuid;
  const char *old_repos_relpath;
  const char *new_repos_relpath;
  svn_revnum_t old_rev;
  svn_revnum_t new_rev;
  svn_node_kind_t old_kind;
  svn_node_kind_t new_kind;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_skel_t *conflict;

  /* Verify precondition: We are allowed to set a tree conflict here. */
  SVN_ERR(verify_write_lock(wcroot, move_src_root_relpath, scratch_pool));

  /* Read new (post-update) information from the new move source BASE node. */
  SVN_ERR(svn_wc__db_depth_get_info(NULL, &new_kind, &new_rev,
                                    &new_repos_relpath, &repos_id,
                                    NULL, NULL, NULL, NULL, NULL,
                                    NULL, NULL, NULL,
                                    wcroot, move_src_op_root_relpath,
                                    src_op_depth, scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_fetch_repos_info(&repos_root_url, &repos_uuid,
                                      wcroot, repos_id, scratch_pool));

  /* Read old (pre-update) information from the move destination node.

     This potentially touches nodes that aren't locked by us, but that is not
     a problem because we have a SQLite write lock here, and all sqlite
     operations that affect move stability use a sqlite lock as well.
     (And affecting the move itself requires a write lock on the node that
      we do own the lock for: the move source)
  */
  SVN_ERR(svn_wc__db_depth_get_info(NULL, &old_kind, &old_rev,
                                    &old_repos_relpath, NULL, NULL, NULL,
                                    NULL, NULL, NULL, NULL, NULL, NULL,
                                    wcroot, move_dst_op_root_relpath,
                                    relpath_depth(move_dst_op_root_relpath),
                                    scratch_pool, scratch_pool));

  if (strcmp(move_src_root_relpath, move_src_op_root_relpath))
    {
      /* We have information for the op-root, but need it for the node that
         we are putting the tree conflict on. Luckily we know that we have
         a clean BASE */

      const char *rpath = svn_relpath_skip_ancestor(move_src_op_root_relpath,
                                                    move_src_root_relpath);

      old_repos_relpath = svn_relpath_join(old_repos_relpath, rpath,
                                           scratch_pool);
      new_repos_relpath = svn_relpath_join(new_repos_relpath, rpath,
                                           scratch_pool);
    }

  old_version = svn_wc_conflict_version_create2(
                  repos_root_url, repos_uuid, old_repos_relpath, old_rev,
                  old_kind, scratch_pool);
  new_version = svn_wc_conflict_version_create2(
                  repos_root_url, repos_uuid, new_repos_relpath, new_rev,
                  new_kind, scratch_pool);

  SVN_ERR(create_tree_conflict(&conflict, wcroot, move_src_root_relpath,
                               move_dst_op_root_relpath,
                               db, old_version, new_version,
                               svn_wc_operation_update,
                               old_kind, new_kind,
                               old_repos_relpath,
                               svn_wc_conflict_reason_moved_away,
                               svn_wc_conflict_action_edit,
                               move_src_op_root_relpath,
                               scratch_pool, scratch_pool));

  SVN_ERR(update_move_list_add(wcroot, move_src_root_relpath, db,
                               svn_wc_notify_tree_conflict,
                               new_kind,
                               svn_wc_notify_state_inapplicable,
                               svn_wc_notify_state_inapplicable,
                               conflict, NULL, scratch_pool));

  return SVN_NO_ERROR;
}

/* Checks if SRC_RELPATH is within BUMP_DEPTH from BUMP_ROOT. Sets
 * *SKIP to TRUE if the node should be skipped, otherwise to FALSE.
 * Sets *SRC_DEPTH to the remaining depth at SRC_RELPATH.
 */
static svn_error_t *
check_bump_layer(svn_boolean_t *skip,
                 svn_depth_t *src_depth,
                 const char *bump_root,
                 svn_depth_t bump_depth,
                 const char *src_relpath,
                 svn_node_kind_t src_kind,
                 apr_pool_t *scratch_pool)
{
  const char *relpath;

  *skip = FALSE;
  *src_depth = bump_depth;

  relpath = svn_relpath_skip_ancestor(bump_root, src_relpath);

  if (!relpath)
    *skip = TRUE;

  if (bump_depth == svn_depth_infinity)
    return SVN_NO_ERROR;

  if (relpath && *relpath == '\0')
    return SVN_NO_ERROR;

  switch (bump_depth)
    {
      case svn_depth_empty:
        *skip = TRUE;
        break;

      case svn_depth_files:
        if (src_kind != svn_node_file)
          {
            *skip = TRUE;
            break;
          }
        /* Fallthrough */
      case svn_depth_immediates:
        if (!relpath || relpath_depth(relpath) > 1)
          *skip = TRUE;

        *src_depth = svn_depth_empty;
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

/* The guts of bump_moved_away: Determines if a move can be bumped to match
 * the move origin and if so performs this bump.
 */
static svn_error_t *
bump_moved_layer(svn_boolean_t *recurse,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 int op_depth,
                 const char *src_relpath,
                 int src_del_depth,
                 svn_depth_t src_depth,
                 const char *dst_relpath,
                 svn_wc__db_t *db,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_skel_t *conflict;
  svn_boolean_t can_bump;
  const char *src_root_relpath;

  SVN_ERR(verify_write_lock(wcroot, local_relpath, scratch_pool));

  *recurse = FALSE;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_HAS_LAYER_BETWEEN));

  SVN_ERR(svn_sqlite__bindf(stmt, "isdd", wcroot->wc_id, local_relpath,
                            op_depth, src_del_depth));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    return SVN_NO_ERROR;

  if (op_depth == 0)
    SVN_ERR(depth_sufficient_to_bump(&can_bump, wcroot, src_relpath,
                                     op_depth, src_depth, scratch_pool));
  else
    /* Having chosen to bump an entire BASE tree move we
       always have sufficient depth to bump subtree moves. */
    can_bump = TRUE;

  /* Are we allowed to bump */
  if (can_bump)
    {
      svn_boolean_t locked;

      SVN_ERR(svn_wc__db_wclock_owns_lock_internal(&locked, wcroot,
                                                   dst_relpath,
                                                   FALSE, scratch_pool));

      if (!locked)
        can_bump = FALSE;
    }

  src_root_relpath = svn_relpath_prefix(src_relpath, src_del_depth,
                                        scratch_pool);

  if (!can_bump)
    {
      SVN_ERR(bump_mark_tree_conflict(wcroot, src_relpath, op_depth,
                                      src_root_relpath, dst_relpath,
                                      db, scratch_pool));

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__db_read_conflict_internal(&conflict, NULL, NULL,
                                            wcroot, src_root_relpath,
                                            scratch_pool, scratch_pool));

  /* ### TODO: check this is the right sort of tree-conflict? */
  if (!conflict)
    {
      /* ### TODO: verify moved_here? */

      SVN_ERR(verify_write_lock(wcroot, src_relpath, scratch_pool));

      SVN_ERR(svn_wc__db_op_copy_layer_internal(wcroot,
                                                src_relpath, op_depth,
                                                dst_relpath, NULL, NULL,
                                                scratch_pool));

      *recurse = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Internal storage for bump_moved_away() */
struct bump_pair_t
{
  const char *src_relpath;
  const char *dst_relpath;
  int src_del_op_depth;
  svn_node_kind_t src_kind;
};

/* Bump moves of LOCAL_RELPATH and all its descendants that were
   originally below LOCAL_RELPATH at op-depth OP_DEPTH.
 */
static svn_error_t *
bump_moved_away(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                int op_depth,
                svn_depth_t depth,
                svn_wc__db_t *db,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;
  int i;
  apr_array_header_t *pairs = apr_array_make(scratch_pool, 32,
                                             sizeof(struct bump_pair_t*));

  /* Build an array, as we can't execute the same Sqlite query recursively */
  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_PAIR3));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while(have_row)
    {
      struct bump_pair_t *bp = apr_pcalloc(scratch_pool, sizeof(*bp));

      bp->src_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
      bp->dst_relpath = svn_sqlite__column_text(stmt, 1, scratch_pool);
      bp->src_del_op_depth = svn_sqlite__column_int(stmt, 2);
      bp->src_kind = svn_sqlite__column_token(stmt, 3, kind_map);

      APR_ARRAY_PUSH(pairs, struct bump_pair_t *) = bp;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  for (i = 0; i < pairs->nelts; i++)
    {
      struct bump_pair_t *bp = APR_ARRAY_IDX(pairs, i, struct bump_pair_t *);
      svn_boolean_t skip;
      svn_depth_t src_wc_depth;

      svn_pool_clear(iterpool);


      SVN_ERR(check_bump_layer(&skip, &src_wc_depth, local_relpath, depth,
                               bp->src_relpath, bp->src_kind, iterpool));

      if (!skip)
        {
          svn_boolean_t recurse;

          SVN_ERR(bump_moved_layer(&recurse, wcroot,
                                   local_relpath, op_depth,
                                   bp->src_relpath, bp->src_del_op_depth,
                                   src_wc_depth, bp->dst_relpath,
                                   db, iterpool));

          if (recurse)
            SVN_ERR(bump_moved_away(wcroot, bp->dst_relpath,
                                    relpath_depth(bp->dst_relpath),
                                    depth, db, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_bump_moved_away(svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           svn_depth_t depth,
                           svn_wc__db_t *db,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  if (local_relpath[0] != '\0')
    {
      const char *move_dst_op_root_relpath;
      const char *move_src_root_relpath, *delete_relpath;
      svn_error_t *err;

      /* Is the root of the update moved away? (Impossible for the wcroot) */

      err = svn_wc__db_scan_moved_to_internal(&move_src_root_relpath,
                                              &move_dst_op_root_relpath,
                                              &delete_relpath,
                                              wcroot, local_relpath,
                                              0 /* BASE */,
                                              scratch_pool, scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
        }
      else if (move_src_root_relpath)
        {
          if (strcmp(move_src_root_relpath, local_relpath))
            {
              /* An ancestor of the path that was updated is moved away.

                 If we have a lock on that ancestor, we can mark a tree
                 conflict on it, if we don't we ignore this case. A future
                 update of the ancestor will handle this. */
              svn_boolean_t locked;

              SVN_ERR(svn_wc__db_wclock_owns_lock_internal(
                                &locked, wcroot,
                                move_src_root_relpath,
                                FALSE, scratch_pool));

              if (locked)
                {
                  SVN_ERR(bump_mark_tree_conflict(wcroot,
                                                  move_src_root_relpath, 0,
                                                  delete_relpath,
                                                  move_dst_op_root_relpath,
                                                  db, scratch_pool));
                }
              return SVN_NO_ERROR;
            }
        }
    }

  SVN_ERR(bump_moved_away(wcroot, local_relpath, 0, depth, db, scratch_pool));

  return SVN_NO_ERROR;
}

/* Set *OPERATION, *LOCAL_CHANGE, *INCOMING_CHANGE, *OLD_VERSION, *NEW_VERSION
 * to reflect the tree conflict on the victim SRC_ABSPATH in DB.
 *
 * If SRC_ABSPATH is not a tree-conflict victim, return an error.
 */
static svn_error_t *
fetch_conflict_details(int *src_op_depth,
                       svn_wc_operation_t *operation,
                       svn_wc_conflict_action_t *action,
                       svn_wc_conflict_version_t **left_version,
                       svn_wc_conflict_version_t **right_version,
                       svn_wc__db_wcroot_t *wcroot,
                       svn_wc__db_t *db,
                       const char *local_relpath,
                       const svn_skel_t *conflict_skel,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const apr_array_header_t *locations;
  svn_boolean_t text_conflicted;
  svn_boolean_t prop_conflicted;
  svn_boolean_t tree_conflicted;
  const char *move_src_op_root_abspath;
  svn_wc_conflict_reason_t reason;
  const char *local_abspath = svn_dirent_join(wcroot->abspath, local_relpath,
                                              scratch_pool);

  if (!conflict_skel)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("'%s' is not in conflict"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(operation, &locations,
                                     &text_conflicted, &prop_conflicted,
                                     &tree_conflicted,
                                     db, local_abspath,
                                     conflict_skel, result_pool,
                                     scratch_pool));

  if (text_conflicted || prop_conflicted || !tree_conflicted)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("'%s' is not a valid tree-conflict victim"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason,
                                              action,
                                              &move_src_op_root_abspath,
                                              db, local_abspath,
                                              conflict_skel, result_pool,
                                              scratch_pool));

  if (reason == svn_wc_conflict_reason_moved_away)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("'%s' is already a moved away tree-conflict"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  if (left_version)
    {
      if (locations && locations->nelts > 0)
        *left_version = APR_ARRAY_IDX(locations, 0,
                                     svn_wc_conflict_version_t *);
      else
        *left_version = NULL;
    }

  if (right_version)
    {
      if (locations && locations->nelts > 1)
        *right_version = APR_ARRAY_IDX(locations, 1,
                                     svn_wc_conflict_version_t *);
      else
        *right_version = NULL;
    }

  {
    int del_depth = relpath_depth(local_relpath);

    if (move_src_op_root_abspath)
      del_depth = relpath_depth(
                      svn_dirent_skip_ancestor(wcroot->abspath,
                                               move_src_op_root_abspath));

    SVN_ERR(find_src_op_depth(src_op_depth, wcroot, local_relpath, del_depth,
                              scratch_pool));
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_raise_moved_away_internal(
                        svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        int src_op_depth,
                        svn_wc__db_t *db,
                        svn_wc_operation_t operation,
                        svn_wc_conflict_action_t action,
                        const svn_wc_conflict_version_t *old_version,
                        const svn_wc_conflict_version_t *new_version,
                        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_DESCENDANTS_SRC));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            src_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while(have_row)
    {
      svn_error_t *err;
      int delete_op_depth = svn_sqlite__column_int(stmt, 0);
      const char *src_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      svn_node_kind_t src_kind = svn_sqlite__column_token(stmt, 2, kind_map);
      const char *src_repos_relpath = svn_sqlite__column_text(stmt, 3, NULL);
      const char *dst_relpath = svn_sqlite__column_text(stmt, 4, NULL);
      svn_skel_t *conflict;
      svn_pool_clear(iterpool);

      SVN_ERR_ASSERT(src_repos_relpath != NULL);

      err = create_tree_conflict(&conflict, wcroot, src_relpath, dst_relpath,
                                 db, old_version, new_version, operation,
                                 src_kind /* ### old kind */,
                                 src_kind /* ### new kind */,
                                 src_repos_relpath,
                                 svn_wc_conflict_reason_moved_away,
                                 action,
                                 svn_relpath_prefix(src_relpath,
                                                    delete_op_depth,
                                                    iterpool),
                                 iterpool, iterpool);

      if (!err)
        err = update_move_list_add(wcroot, src_relpath, db,
                                   svn_wc_notify_tree_conflict,
                                   src_kind,
                                   svn_wc_notify_state_inapplicable,
                                   svn_wc_notify_state_inapplicable,
                                   conflict, NULL, scratch_pool);

      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_raise_moved_away(svn_wc__db_t *db,
                               const char *local_abspath,
                               svn_wc_notify_func2_t notify_func,
                               void *notify_baton,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc_operation_t operation;
  svn_wc_conflict_action_t action;
  svn_wc_conflict_version_t *left_version, *right_version;
  int move_src_op_depth;
  svn_skel_t *conflict;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN4(
    svn_wc__db_read_conflict_internal(&conflict, NULL, NULL,
                                      wcroot, local_relpath,
                                      scratch_pool, scratch_pool),
    fetch_conflict_details(&move_src_op_depth,
                           &operation, &action,
                           &left_version, &right_version,
                           wcroot, db, local_relpath, conflict,
                           scratch_pool, scratch_pool),
    svn_wc__db_op_mark_resolved_internal(wcroot, local_relpath, db,
                                         FALSE, FALSE, TRUE,
                                         NULL, scratch_pool),
    svn_wc__db_op_raise_moved_away_internal(wcroot, local_relpath,
                                            move_src_op_depth,
                                            db, operation, action,
                                            left_version, right_version,
                                            scratch_pool),
    wcroot);

  /* These version numbers are valid for update/switch notifications 
     only! */
  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot,
                                             (left_version
                                              ? left_version->peg_rev
                                              : SVN_INVALID_REVNUM),
                                             (right_version
                                              ? right_version->peg_rev
                                              : SVN_INVALID_REVNUM),
                                             notify_func, notify_baton,
                                             scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
break_moved_away(svn_wc__db_wcroot_t *wcroot,
                 svn_wc__db_t *db,
                 const char *local_relpath,
                 int parent_src_op_depth,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;
  svn_error_t *err = NULL;

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_DESCENDANTS_SRC));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            parent_src_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
    {
      int src_op_depth = svn_sqlite__column_int(stmt, 0);
      const char *src_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      svn_node_kind_t src_kind = svn_sqlite__column_token(stmt, 2, kind_map);
      const char *dst_relpath = svn_sqlite__column_text(stmt, 4, NULL);

      svn_pool_clear(iterpool);

      err = verify_write_lock(wcroot, src_relpath, iterpool);

      if (!err)
        err = verify_write_lock(wcroot, dst_relpath, iterpool);

      if (err)
        break;

      err = svn_error_trace(
              svn_wc__db_op_break_move_internal(wcroot,
                                                src_relpath, src_op_depth,
                                                dst_relpath, NULL, iterpool));

      if (err)
        break;

      err = svn_error_trace(
              update_move_list_add(wcroot, src_relpath, db,
                                   svn_wc_notify_move_broken,
                                   src_kind,
                                   svn_wc_notify_state_inapplicable,
                                   svn_wc_notify_state_inapplicable,
                                   NULL, NULL, scratch_pool));

      if (err)
        break;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  svn_pool_destroy(iterpool);

  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_op_break_moved_away(svn_wc__db_t *db,
                               const char *local_abspath,
                               const char *del_op_root_abspath,
                               svn_boolean_t mark_tc_resolved,
                               svn_wc_notify_func2_t notify_func,
                               void *notify_baton,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *del_relpath;
  int src_op_depth;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (del_op_root_abspath)
    del_relpath = svn_dirent_skip_ancestor(wcroot->abspath,
                                           del_op_root_abspath);
  else
    del_relpath = NULL;


  SVN_WC__DB_WITH_TXN4(
    find_src_op_depth(&src_op_depth, wcroot, local_relpath,
                      del_relpath ? relpath_depth(del_relpath)
                                 : relpath_depth(local_relpath),
                      scratch_pool),
    break_moved_away(wcroot, db, local_relpath, src_op_depth,
                     scratch_pool),
    mark_tc_resolved
        ? svn_wc__db_op_mark_resolved_internal(wcroot, local_relpath, db,
                                               FALSE, FALSE, TRUE,
                                               NULL, scratch_pool)
        : SVN_NO_ERROR,
    SVN_NO_ERROR,
    wcroot);

  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot,
                                             SVN_INVALID_REVNUM,
                                             SVN_INVALID_REVNUM,
                                             notify_func, notify_baton,
                                             scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
required_lock_for_resolve(const char **required_relpath,
                          svn_wc__db_wcroot_t *wcroot,
                          const char *local_relpath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *required_relpath = local_relpath;

  /* This simply looks for all moves out of the LOCAL_RELPATH tree. We
     could attempt to limit it to only those moves that are going to
     be resolved but that would require second guessing the resolver.
     This simple algorithm is sufficient although it may give a
     strictly larger/deeper lock than necessary. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_OUTSIDE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath, 0));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      const char *move_dst_relpath = svn_sqlite__column_text(stmt, 1,
                                                             NULL);

      *required_relpath
        = svn_relpath_get_longest_ancestor(*required_relpath,
                                           move_dst_relpath,
                                           scratch_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  *required_relpath = apr_pstrdup(result_pool, *required_relpath);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__required_lock_for_resolve(const char **required_abspath,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *required_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    required_lock_for_resolve(&required_relpath, wcroot, local_relpath,
                              scratch_pool, scratch_pool),
    wcroot);

  *required_abspath = svn_dirent_join(wcroot->abspath, required_relpath,
                                      result_pool);

  return SVN_NO_ERROR;
}
