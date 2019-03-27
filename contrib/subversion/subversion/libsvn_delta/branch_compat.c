/*
 * branch_compat.c : Branching compatibility layer.
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

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_iter.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_branch_impl.h"
#include "private/svn_branch_repos.h"
#include "private/svn_branch_nested.h"
#include "private/svn_delta_private.h"
#include "private/svn_branch_compat.h"

#include "svn_private_config.h"


/* Verify EXPR is true; raise an error if not. */
#define VERIFY(expr) SVN_ERR_ASSERT(expr)


/*
 * ===================================================================
 * Minor data types
 * ===================================================================
 */

/** A location in a committed revision.
 *
 * @a rev shall not be #SVN_INVALID_REVNUM unless the interface using this
 * type specifically allows it and defines its meaning. */
typedef struct svn_pathrev_t
{
  svn_revnum_t rev;
  const char *relpath;
} svn_pathrev_t;

/* Return true iff PEG_PATH1 and PEG_PATH2 are both the same location.
 */
static svn_boolean_t
pathrev_equal(const svn_pathrev_t *p1,
              const svn_pathrev_t *p2)
{
  if (p1->rev != p2->rev)
    return FALSE;
  if (strcmp(p1->relpath, p2->relpath) != 0)
    return FALSE;

  return TRUE;
}

#if 0
/* Return a human-readable string representation of LOC. */
static const char *
pathrev_str(const svn_pathrev_t *loc,
            apr_pool_t *result_pool)
{
  if (! loc)
    return "<nil>";
  return apr_psprintf(result_pool, "%s@%ld",
                      loc->relpath, loc->rev);
}

/* Return a string representation of the (string) keys of HASH. */
static const char *
hash_keys_str(apr_hash_t *hash)
{
  const char *str = NULL;
  apr_pool_t *pool;
  apr_hash_index_t *hi;

  if (! hash)
    return "<nil>";

  pool = apr_hash_pool_get(hash);
  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);

      if (!str)
        str = key;
      else
        str = apr_psprintf(pool, "%s, %s", str, key);
    }
  return apr_psprintf(pool, "{%s}", str);
}
#endif

/**
 * Merge two hash tables into one new hash table. The values of the overlay
 * hash override the values of the base if both have the same key.
 *
 * Unlike apr_hash_overlay(), this doesn't care whether the input hashes use
 * the same hash function, nor about the relationship between the three pools.
 *
 * @param p The pool to use for the new hash table
 * @param overlay The table to add to the initial table
 * @param base The table that represents the initial values of the new table
 * @return A new hash table containing all of the data from the two passed in
 * @remark Makes a shallow copy: keys and values are not copied
 */
static apr_hash_t *
hash_overlay(apr_hash_t *overlay,
             apr_hash_t *base)
{
  apr_pool_t *pool = apr_hash_pool_get(base);
  apr_hash_t *result = apr_hash_copy(pool, base);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, overlay); hi; hi = apr_hash_next(hi))
    {
      svn_hash_sets(result, apr_hash_this_key(hi), apr_hash_this_val(hi));
    }
  return result;
}


/*
 * ========================================================================
 * Configuration Options
 * ========================================================================
 */

/* Features that are not wanted for this commit editor shim but may be
 * wanted in a similar but different shim such as for an update editor. */
/* #define SHIM_WITH_ADD_ABSENT */
/* #define SHIM_WITH_UNLOCK */

/* Whether to support switching from relative to absolute paths in the
 * Ev1 methods. */
/* #define SHIM_WITH_ABS_PATHS */


/*
 * ========================================================================
 * Shim Connector
 * ========================================================================
 *
 * The shim connector enables a more exact round-trip conversion from an
 * Ev1 drive to Ev3 and back to Ev1.
 */
struct svn_branch__compat_shim_connector_t
{
  /* Set to true if and when an Ev1 receiving shim receives an absolute
   * path (prefixed with '/') from the delta edit, and causes the Ev1
   * sending shim to send absolute paths.
   * ### NOT IMPLEMENTED
   */
#ifdef SHIM_WITH_ABS_PATHS
  svn_boolean_t *ev1_absolute_paths;
#endif

  /* The Ev1 set_target_revision and start_edit methods, respectively, will
   * call the TARGET_REVISION_FUNC and START_EDIT_FUNC callbacks, if non-null.
   * Otherwise, default calls will be used.
   *
   * (Possibly more useful for update editors than for commit editors?) */
  svn_branch__compat_set_target_revision_func_t target_revision_func;

  /* If not null, a callback that the Ev3 driver may call to
   * provide the "base revision" of the root directory, even if it is not
   * going to modify that directory. (If it does modify it, then it will
   * pass in the appropriate base revision at that time.) If null
   * or if the driver does not call it, then the Ev1
   * open_root() method will be called with SVN_INVALID_REVNUM as the base
   * revision parameter.
   */
  svn_delta__start_edit_func_t start_edit_func;

#ifdef SHIM_WITH_UNLOCK
  /* A callback which will be called when an unlocking action is received.
     (For update editors?) */
  svn_delta__unlock_func_t unlock_func;
#endif

  void *baton;
};

svn_error_t *
svn_branch__compat_insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
#if 0
  svn_branch__txn_t *edit_txn;
  svn_branch__compat_shim_connector_t *shim_connector;

#ifdef SVN_DEBUG
  /*SVN_ERR(svn_delta__get_debug_editor(&old_deditor, &old_dedit_baton,
                                      old_deditor, old_dedit_baton,
                                      "[OUT] ", result_pool));*/
#endif
  SVN_ERR(svn_branch__compat_txn_from_delta_for_commit(
                        &edit_txn,
                        &shim_connector,
                        old_deditor, old_dedit_baton,
                        branching_txn,
                        repos_root,
                        fetch_func, fetch_baton,
                        NULL, NULL /*cancel*/,
                        result_pool, scratch_pool));
  SVN_ERR(svn_branch__compat_delta_from_txn_for_commit(
                        new_deditor, new_dedit_baton,
                        edit_txn,
                        repos_root, base_relpath,
                        fetch_func, fetch_baton,
                        shim_connector,
                        result_pool, scratch_pool));
#ifdef SVN_DEBUG
  /*SVN_ERR(svn_delta__get_debug_editor(new_deditor, new_dedit_baton,
                                      *new_deditor, *new_dedit_baton,
                                      "[IN]  ", result_pool));*/
#endif
#else
  *new_deditor = old_deditor;
  *new_dedit_baton = old_dedit_baton;
#endif
  return SVN_NO_ERROR;
}


/*
 * ========================================================================
 * Buffering the Delta Editor Changes
 * ========================================================================
 */

/* The kind of Ev1 restructuring operation on a particular path. For each
 * visited path we use exactly one restructuring action. */
enum restructure_action_t
{
  RESTRUCTURE_NONE = 0,
  RESTRUCTURE_ADD,         /* add the node, maybe replacing. maybe copy  */
#ifdef SHIM_WITH_ADD_ABSENT
  RESTRUCTURE_ADD_ABSENT,  /* add an absent node, possibly replacing  */
#endif
  RESTRUCTURE_DELETE       /* delete this node  */
};

/* Records everything about how this node is to be changed, from an Ev1
 * point of view.  */
typedef struct change_node_t
{
  /* what kind of (tree) restructure is occurring at this node?  */
  enum restructure_action_t action;

  svn_node_kind_t kind;  /* the NEW kind of this node  */

  /* We may need to specify the revision we are altering or the revision
     to delete or replace. These are mutually exclusive, but are separate
     for clarity. */
  /* CHANGING_REV is the base revision of the change if ACTION is 'none',
     else is SVN_INVALID_REVNUM. (If ACTION is 'add' and COPYFROM_PATH
     is non-null, then COPYFROM_REV serves the equivalent purpose for the
     copied node.) */
  /* ### Can also be SVN_INVALID_REVNUM for a pre-existing file/dir,
         meaning the base is the youngest revision. This is probably not
         a good idea -- it is at least confusing -- and we should instead
         resolve to a real revnum when Ev1 passes in SVN_INVALID_REVNUM
         in such cases. */
  svn_revnum_t changing_rev;
  /* If ACTION is 'delete' or if ACTION is 'add' and it is a replacement,
     DELETING is TRUE and DELETING_REV is the revision to delete. */
  /* ### Can also be SVN_INVALID_REVNUM for a pre-existing file/dir,
         meaning the base is the youngest revision. This is probably not
         a good idea -- it is at least confusing -- and we should instead
         resolve to a real revnum when Ev1 passes in SVN_INVALID_REVNUM
         in such cases. */
  svn_boolean_t deleting;
  svn_revnum_t deleting_rev;

  /* new/final set of props to apply; null => no *change*, not no props */
  apr_hash_t *props;

  /* new fulltext; null => no change */
  svn_boolean_t contents_changed;
  svn_stringbuf_t *contents_text;

  /* If COPYFROM_PATH is not NULL, then copy PATH@REV to this node.
     RESTRUCTURE must be RESTRUCTURE_ADD.  */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

#ifdef SHIM_WITH_UNLOCK
  /* Record whether an incoming propchange unlocked this node.  */
  svn_boolean_t unlock;
#endif
} change_node_t;

#if 0
/* Return a string representation of CHANGE. */
static const char *
change_node_str(change_node_t *change,
                apr_pool_t *result_pool)
{
  const char *copyfrom = "<nil>";
  const char *str;

  if (change->copyfrom_path)
    copyfrom = apr_psprintf(result_pool, "'%s'@%ld",
                            change->copyfrom_path, change->copyfrom_rev);
  str = apr_psprintf(result_pool,
                     "action=%d, kind=%s, changing_rev=%ld, "
                     "deleting=%d, deleting_rev=%ld, ..., "
                     "copyfrom=%s",
                     change->action,
                     svn_node_kind_to_word(change->kind),
                     change->changing_rev,
                     change->deleting, change->deleting_rev,
                     copyfrom);
  return str;
}
#endif

/* Check whether RELPATH is known to exist, known to not exist, or unknown. */
static svn_tristate_t
check_existence(apr_hash_t *changes,
                const char *relpath)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);
  svn_tristate_t exists = svn_tristate_unknown;

  if (change && change->action != RESTRUCTURE_DELETE)
    exists = svn_tristate_true;
  else if (change && change->action == RESTRUCTURE_DELETE)
    exists = svn_tristate_false;
  else
    {
      const char *parent_path = relpath;

      /* Find the nearest parent change. If that's a delete or a simple
         (non-recursive) add, this path cannot exist, else we don't know. */
      while ((parent_path = svn_relpath_dirname(parent_path, scratch_pool)),
             *parent_path)
        {
          change = svn_hash_gets(changes, parent_path);
          if (change)
            {
              if ((change->action == RESTRUCTURE_ADD && !change->copyfrom_path)
                  || change->action == RESTRUCTURE_DELETE)
                exists = svn_tristate_false;
              break;
            }
        }
    }

  return exists;
}

/* Insert a new Ev1-style change for RELPATH, or return an existing one.
 *
 * Verify Ev3 rules. Primary differences from Ev1 rules are ...
 *
 * If ACTION is 'delete', elide any previous explicit deletes inside
 * that subtree. (Other changes inside that subtree are not allowed.) We
 * do not store multiple change records per path even with nested moves
 * -- the most complex change is delete + copy which all fits in one
 * record with action='add'.
 */
static svn_error_t *
insert_change(change_node_t **change_p, apr_hash_t *changes,
              const char *relpath,
              enum restructure_action_t action)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  change_node_t *change = svn_hash_gets(changes, relpath);

  /* Check whether this op is allowed. */
  switch (action)
    {
    case RESTRUCTURE_NONE:
      if (change)
        {
          /* A no-restructure change is allowed after add, but not allowed
           * (in Ev3) after another no-restructure change, nor a delete. */
          VERIFY(change->action == RESTRUCTURE_ADD);
        }
      break;

    case RESTRUCTURE_ADD:
      if (change)
        {
          /* Add or copy is allowed after delete (and replaces the delete),
           * but not allowed after an add or a no-restructure change. */
          VERIFY(change->action == RESTRUCTURE_DELETE);
          change->action = action;
        }
      break;

#ifdef SHIM_WITH_ADD_ABSENT
    case RESTRUCTURE_ADD_ABSENT:
      /* ### */
      break;
#endif

    case RESTRUCTURE_DELETE:
      SVN_ERR_MALFUNCTION();
    }

  if (change)
    {
      if (action != RESTRUCTURE_NONE)
        {
          change->action = action;
        }
    }
  else
    {
      /* Create a new change. Callers will set the other fields as needed. */
      change = apr_pcalloc(changes_pool, sizeof(*change));
      change->action = action;
      change->changing_rev = SVN_INVALID_REVNUM;

      svn_hash_sets(changes, apr_pstrdup(changes_pool, relpath), change);
    }

  *change_p = change;
  return SVN_NO_ERROR;
}

/* Modify CHANGES so as to delete the subtree at RELPATH.
 *
 * Insert a new Ev1-style change record for RELPATH (or perhaps remove
 * the existing record if this would have the same effect), and remove
 * any change records for sub-paths under RELPATH.
 *
 * Follow Ev3 rules, although without knowing whether this delete is
 * part of a move. Ev3 (incremental) "rm" operation says each node to
 * be removed "MAY be a child of a copy but otherwise SHOULD NOT have
 * been created or modified in this edit". "mv" operation says ...
 */
static svn_error_t *
delete_subtree(apr_hash_t *changes,
               const char *relpath,
               svn_revnum_t deleting_rev)
{
  apr_pool_t *changes_pool = apr_hash_pool_get(changes);
  apr_pool_t *scratch_pool = changes_pool;
  change_node_t *change = svn_hash_gets(changes, relpath);

  if (change)
    {
      /* If this previous change was a non-replacing addition, there
         is no longer any change to be made at this path. If it was
         a replacement or a modification, it now becomes a delete.
         (If it was a delete, this attempt to delete is an error.) */
       VERIFY(change->action != RESTRUCTURE_DELETE);
       if (change->action == RESTRUCTURE_ADD && !change->deleting)
         {
           svn_hash_sets(changes, relpath, NULL);
           change = NULL;
         }
       else
         {
           change->action = RESTRUCTURE_DELETE;
         }
    }
  else
    {
      /* There was no change recorded at this path. Record a delete. */
      change = apr_pcalloc(changes_pool, sizeof(*change));
      change->action = RESTRUCTURE_DELETE;
      change->changing_rev = SVN_INVALID_REVNUM;
      change->deleting = TRUE;
      change->deleting_rev = deleting_rev;

      svn_hash_sets(changes, apr_pstrdup(changes_pool, relpath), change);
    }

  /* Elide all child ops. */
  {
    apr_hash_index_t *hi;

    for (hi = apr_hash_first(scratch_pool, changes);
         hi; hi = apr_hash_next(hi))
      {
        const char *this_relpath = apr_hash_this_key(hi);
        const char *r = svn_relpath_skip_ancestor(relpath, this_relpath);

        if (r && r[0])
          {
            svn_hash_sets(changes, this_relpath, NULL);
          }
      }
  }

  return SVN_NO_ERROR;
}


/*
 * ===================================================================
 * Commit Editor converter to join a v1 driver to a v3 consumer
 * ===================================================================
 *
 * ...
 */

svn_error_t *
svn_branch__compat_delta_from_txn_for_commit(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_branch__txn_t *edit_txn,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        const svn_branch__compat_shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  /* ### ... */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__compat_delta_from_txn_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_branch__compat_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_branch__compat_shim_connector_t *shim_connector
    = apr_pcalloc(result_pool, sizeof(*shim_connector));

  shim_connector->target_revision_func = update_editor->set_target_revision_func;
  shim_connector->baton = update_editor->set_target_revision_baton;
#ifdef SHIM_WITH_ABS_PATHS
  shim_connector->ev1_absolute_paths /*...*/;
#endif

  SVN_ERR(svn_branch__compat_delta_from_txn_for_commit(
                        deditor, dedit_baton,
                        update_editor->edit_txn,
                        repos_root_url, base_repos_relpath,
                        fetch_func, fetch_baton,
                        shim_connector,
                        result_pool, scratch_pool));
  /*SVN_ERR(svn_delta__get_debug_editor(deditor, dedit_baton,
                                      *deditor, *dedit_baton,
                                      "[UP>1] ", result_pool));*/

  return SVN_NO_ERROR;
}


/*
 * ===================================================================
 * Commit Editor converter to join a v3 driver to a v1 consumer
 * ===================================================================
 *
 * This editor buffers all the changes before driving the Ev1 at the end,
 * since it needs to do a single depth-first traversal of the path space
 * and this cannot be started until all moves are known.
 *
 * Moves are converted to copy-and-delete, with the copy being from
 * the source peg rev. (### Should it request copy-from revision "-1"?)
 *
 * It works like this:
 *
 *                +------+--------+
 *                | path | change |
 *      Ev3  -->  +------+--------+  -->  Ev1
 *                | ...  | ...    |
 *                | ...  | ...    |
 *
 *   1. Ev3 changes are accumulated in a per-path table, EB->changes.
 *
 *   2. On Ev3 close-edit, walk through the table in a depth-first order,
 *      sending the equivalent Ev1 action for each change.
 *
 * TODO
 *
 * ### For changes inside a copied subtree, the calls to the "open dir"
 *     and "open file" Ev1 methods may be passing the wrong revision
 *     number: see comment in apply_change().
 *
 * ### Have we got our rel-paths in order? Ev1, Ev3 and callbacks may
 *     all expect different paths. Are they relative to repos root or to
 *     some base path? Leading slash (unimplemented 'send_abs_paths'
 *     feature), etc.
 *
 * ### May be tidier for OPEN_ROOT_FUNC callback (see open_root_ev3())
 *     not to actually open the root in advance, but instead just to
 *     remember the base revision that the driver wants us to specify
 *     when we do open it.
 */



/*
 * ========================================================================
 * Driving the Delta Editor
 * ========================================================================
 */

/* Information needed for driving the delta editor. */
struct svn_branch__txn_priv_t
{
  /* The Ev1 "delta editor" */
  const svn_delta_editor_t *deditor;
  void *dedit_baton;

  /* Callbacks */
  svn_branch__compat_fetch_func_t fetch_func;
  void *fetch_baton;

  /* The Ev1 root directory baton if we have opened the root, else null. */
  void *ev1_root_dir_baton;

#ifdef SHIM_WITH_ABS_PATHS
  const svn_boolean_t *make_abs_paths;
#endif

  /* Repository root URL
     ### Some code allows this to be null -- but is that valid? */
  const char *repos_root_url;

  /* Ev1 changes recorded so far: REPOS_RELPATH -> change_node_ev3_t */
  apr_hash_t *changes;

  /* The branching state on which the per-element API is working */
  svn_branch__txn_t *txn;

  apr_pool_t *edit_pool;
};

/* Get all the (Ev1) paths that have changes.
 */
static const apr_array_header_t *
get_unsorted_paths(apr_hash_t *changes,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths = apr_array_make(scratch_pool, 0, sizeof(void *));
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, changes); hi; hi = apr_hash_next(hi))
    {
      const char *this_path = apr_hash_this_key(hi);
      APR_ARRAY_PUSH(paths, const char *) = this_path;
    }

  return paths;
}

#if 0 /* needed only for shim connector */
/*  */
static svn_error_t *
set_target_revision_ev3(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  svn_branch__txn_priv_t *eb = edit_baton;

  SVN_ERR(eb->deditor->set_target_revision(eb->dedit_baton, target_revision,
                                           scratch_pool));

  return SVN_NO_ERROR;
}
#endif

/*  */
static svn_error_t *
open_root_ev3(void *baton,
              svn_revnum_t base_revision)
{
  svn_branch__txn_priv_t *eb = baton;

  SVN_ERR(eb->deditor->open_root(eb->dedit_baton, base_revision,
                                 eb->edit_pool, &eb->ev1_root_dir_baton));
  return SVN_NO_ERROR;
}

/* If RELPATH is a child of a copy, return the path of the copy root,
 * else return NULL.
 */
static const char *
find_enclosing_copy(apr_hash_t *changes,
                    const char *relpath,
                    apr_pool_t *result_pool)
{
  while (*relpath)
    {
      const change_node_t *change = svn_hash_gets(changes, relpath);

      if (change)
        {
          if (change->copyfrom_path)
            return relpath;
          if (change->action != RESTRUCTURE_NONE)
            return NULL;
        }
      relpath = svn_relpath_dirname(relpath, result_pool);
    }

  return NULL;
}

/* Set *BASE_PROPS to the 'base' properties, against which any changes
 * will be described, for the changed path described in CHANGES at
 * REPOS_RELPATH.
 *
 * For a copied path, including a copy child path, fetch from the copy
 * source path. For a plain add, return an empty set. For a delete,
 * return NULL.
 */
static svn_error_t *
fetch_base_props(apr_hash_t **base_props,
                 apr_hash_t *changes,
                 const char *repos_relpath,
                 svn_branch__compat_fetch_func_t fetch_func,
                 void *fetch_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const change_node_t *change = svn_hash_gets(changes, repos_relpath);
  const char *source_path = NULL;
  svn_revnum_t source_rev;

  if (change->action == RESTRUCTURE_DELETE)
    {
      *base_props = NULL;
    }

  else if (change->action == RESTRUCTURE_ADD && ! change->copyfrom_path)
    {
      *base_props = apr_hash_make(result_pool);
    }
  else if (change->copyfrom_path)
    {
      source_path = change->copyfrom_path;
      source_rev = change->copyfrom_rev;
    }
  else /* RESTRUCTURE_NONE */
    {
      /* It's an edit, but possibly to a copy child. Discover if it's a
         copy child, & find the copy-from source. */

      const char *copy_path
        = find_enclosing_copy(changes, repos_relpath, scratch_pool);

      if (copy_path)
        {
          const change_node_t *enclosing_copy
            = svn_hash_gets(changes, copy_path);
          const char *remainder
            = svn_relpath_skip_ancestor(copy_path, repos_relpath);

          source_path = svn_relpath_join(enclosing_copy->copyfrom_path,
                                         remainder, scratch_pool);
          source_rev = enclosing_copy->copyfrom_rev;
        }
      else
        {
          /* It's a plain edit (not a copy child path). */
          source_path = repos_relpath;
          source_rev = change->changing_rev;
        }
    }

  if (source_path)
    {
      SVN_ERR(fetch_func(NULL, base_props, NULL, NULL,
                         fetch_baton, source_path, source_rev,
                         result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Send property changes to Ev1 for the CHANGE at REPOS_RELPATH.
 *
 * Ev1 requires exactly one prop-change call for each prop whose value
 * has changed. Therefore we *have* to fetch the original props from the
 * repository, provide them as OLD_PROPS, and calculate the changes.
 */
static svn_error_t *
drive_ev1_props(const char *repos_relpath,
                const change_node_t *change,
                apr_hash_t *old_props,
                const svn_delta_editor_t *deditor,
                void *node_baton,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *propdiffs;
  int i;

  SVN_ERR_ASSERT(change->action != RESTRUCTURE_DELETE);

  /* If there are no property changes, then just exit. */
  if (change->props == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_prop_diffs(&propdiffs, change->props, old_props, scratch_pool));

  /* Apply property changes. These should be changes against the empty set
     for a new node, or changes against the source node for a copied node. */
  for (i = 0; i < propdiffs->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (change->kind == svn_node_dir)
        SVN_ERR(deditor->change_dir_prop(node_baton,
                                         prop->name, prop->value,
                                         iterpool));
      else
        SVN_ERR(deditor->change_file_prop(node_baton,
                                          prop->name, prop->value,
                                          iterpool));
    }

#ifdef SHIM_WITH_UNLOCK
  /* Handle the funky unlock protocol. Note: only possibly on files.  */
  if (change->unlock)
    {
      SVN_ERR_ASSERT(change->kind == svn_node_file);
      SVN_ERR(deditor->change_file_prop(node_baton,
                                            SVN_PROP_ENTRY_LOCK_TOKEN, NULL,
                                            iterpool));
    }
#endif

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Drive the Ev1 editor with the change recorded in EB->changes for the
 * path EV1_RELPATH.
 *
 * Conforms to svn_delta_path_driver_cb_func_t.
 */
static svn_error_t *
apply_change(void **dir_baton,
             void *parent_baton,
             void *callback_baton,
             const char *ev1_relpath,
             apr_pool_t *result_pool)
{
  apr_pool_t *scratch_pool = result_pool;
  const svn_branch__txn_priv_t *eb = callback_baton;
  const change_node_t *change = svn_hash_gets(eb->changes, ev1_relpath);
  void *file_baton = NULL;
  apr_hash_t *base_props;

  /* The callback should only be called for paths in CHANGES.  */
  SVN_ERR_ASSERT(change != NULL);

  /* Typically, we are not creating new directory batons.  */
  *dir_baton = NULL;

  SVN_ERR(fetch_base_props(&base_props, eb->changes, ev1_relpath,
                           eb->fetch_func, eb->fetch_baton,
                           scratch_pool, scratch_pool));

  /* Are we editing the root of the tree?  */
  if (parent_baton == NULL)
    {
      /* The root dir was already opened. */
      *dir_baton = eb->ev1_root_dir_baton;

      /* Only property edits are allowed on the root.  */
      SVN_ERR_ASSERT(change->action == RESTRUCTURE_NONE);
      SVN_ERR(drive_ev1_props(ev1_relpath, change, base_props,
                              eb->deditor, *dir_baton, scratch_pool));

      /* No further action possible for the root.  */
      return SVN_NO_ERROR;
    }

  if (change->action == RESTRUCTURE_DELETE)
    {
      SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting_rev,
                                        parent_baton, scratch_pool));

      /* No futher action possible for this node.  */
      return SVN_NO_ERROR;
    }

  /* If we're not deleting this node, then we should know its kind.  */
  SVN_ERR_ASSERT(change->kind != svn_node_unknown);

#ifdef SHIM_WITH_ADD_ABSENT
  if (change->action == RESTRUCTURE_ADD_ABSENT)
    {
      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->absent_directory(ev1_relpath, parent_baton,
                                              scratch_pool));
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->absent_file(ev1_relpath, parent_baton,
                                         scratch_pool));
      else
        SVN_ERR_MALFUNCTION();

      /* No further action possible for this node.  */
      return SVN_NO_ERROR;
    }
#endif
  /* RESTRUCTURE_NONE or RESTRUCTURE_ADD  */

  if (change->action == RESTRUCTURE_ADD)
    {
      const char *copyfrom_url = NULL;
      svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

      /* Do we have an old node to delete first? If so, delete it. */
      if (change->deleting)
        SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting_rev,
                                          parent_baton, scratch_pool));

      /* If it's a copy, determine the copy source location. */
      if (change->copyfrom_path)
        {
          /* ### What's this about URL vs. fspath? REPOS_ROOT_URL isn't
             optional, is it, at least in a commit editor? */
          if (eb->repos_root_url)
            copyfrom_url = svn_path_url_add_component2(eb->repos_root_url,
                                                       change->copyfrom_path,
                                                       scratch_pool);
          else
            {
              copyfrom_url = change->copyfrom_path;

              /* Make this an FS path by prepending "/" */
              if (copyfrom_url[0] != '/')
                copyfrom_url = apr_pstrcat(scratch_pool, "/",
                                           copyfrom_url, SVN_VA_NULL);
            }

          copyfrom_rev = change->copyfrom_rev;
        }

      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->add_directory(ev1_relpath, parent_baton,
                                           copyfrom_url, copyfrom_rev,
                                           result_pool, dir_baton));
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->add_file(ev1_relpath, parent_baton,
                                      copyfrom_url, copyfrom_rev,
                                      result_pool, &file_baton));
      else
        SVN_ERR_MALFUNCTION();
    }
  else /* RESTRUCTURE_NONE */
    {
      /* ### The code that inserts a "plain edit" change record sets
         'changing_rev' to the peg rev of the pegged part of the path,
         even when the full path refers to a child of a copy. Should we
         instead be using the copy source rev here, in that case? (Like
         when we fetch the base properties.) */

      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->open_directory(ev1_relpath, parent_baton,
                                            change->changing_rev,
                                            result_pool, dir_baton));
      else if (change->kind == svn_node_file)
        SVN_ERR(eb->deditor->open_file(ev1_relpath, parent_baton,
                                       change->changing_rev,
                                       result_pool, &file_baton));
      else
        SVN_ERR_MALFUNCTION();
    }

  /* Apply any properties in CHANGE to the node.  */
  if (change->kind == svn_node_dir)
    SVN_ERR(drive_ev1_props(ev1_relpath, change, base_props,
                            eb->deditor, *dir_baton, scratch_pool));
  else
    SVN_ERR(drive_ev1_props(ev1_relpath, change, base_props,
                            eb->deditor, file_baton, scratch_pool));

  /* Send the text content delta, if new text content is provided. */
  if (change->contents_text)
    {
      svn_stream_t *read_stream;
      svn_txdelta_window_handler_t handler;
      void *handler_baton;

      read_stream = svn_stream_from_stringbuf(change->contents_text,
                                              scratch_pool);
      /* ### would be nice to have a BASE_CHECKSUM, but hey: this is the
         ### shim code...  */
      SVN_ERR(eb->deditor->apply_textdelta(file_baton, NULL, scratch_pool,
                                           &handler, &handler_baton));
      /* ### it would be nice to send a true txdelta here, but whatever.  */
      SVN_ERR(svn_txdelta_send_stream(read_stream, handler, handler_baton,
                                      NULL, scratch_pool));
      SVN_ERR(svn_stream_close(read_stream));
    }

  if (file_baton)
    {
      SVN_ERR(eb->deditor->close_file(file_baton, NULL, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Old-repository storage paths for branch elements
 * ========================================================================
 *
 * To support top-level branches, we map each top-level branch to its own
 * directory in the old repository, with each nested branch in a subdirectory:
 *
 *   B0  =>  ^/top0/...
 *           ^/top0/.../trunk/...  <= B0.12
 *   B1  =>  ^/top1/...
 *
 * It may be better to put each branch in its own directory:
 *
 *   B0     =>  ^/B0/...
 *   B0.12  =>  ^/B0.12/...
 *   B1     =>  ^/B1/...
 *
 * (A branch root is not necessarily a directory, it could be a file.)
 */

/* Get the old-repository path for the storage of the root element of BRANCH.
 *
 * Currently, this is the same as the nested-branching hierarchical path
 * (and thus assumes there is only one top-level branch).
 */
static const char *
branch_get_storage_root_rrpath(const svn_branch__state_t *branch,
                               apr_pool_t *result_pool)
{
  int top_branch_num = atoi(branch->bid + 1);
  const char *top_path = apr_psprintf(result_pool, "top%d", top_branch_num);
  const char *nested_path = svn_branch__get_root_rrpath(branch, result_pool);

  return svn_relpath_join(top_path, nested_path, result_pool);
}

/* Get the old-repository path for the storage of element EID of BRANCH.
 *
 * If the element EID doesn't exist in BRANCH, return NULL.
 */
static const char *
branch_get_storage_rrpath_by_eid(const svn_branch__state_t *branch,
                                 int eid,
                                 apr_pool_t *result_pool)
{
  const char *path = svn_branch__get_path_by_eid(branch, eid, result_pool);
  const char *rrpath = NULL;

  if (path)
    {
      rrpath = svn_relpath_join(branch_get_storage_root_rrpath(branch,
                                                               result_pool),
                                path, result_pool);
    }
  return rrpath;
}

/* Return, in *STORAGE_PATHREV_P, the storage-rrpath-rev for BRANCH_REF.
 */
static svn_error_t *
storage_pathrev_from_branch_ref(svn_pathrev_t *storage_pathrev_p,
                                svn_element__branch_ref_t branch_ref,
                                svn_branch__repos_t *repos,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_branch__el_rev_id_t *el_rev;

  SVN_ERR(svn_branch__repos_find_el_rev_by_id(&el_rev,
                                              repos,
                                              branch_ref.rev,
                                              branch_ref.branch_id,
                                              branch_ref.eid,
                                              scratch_pool, scratch_pool));

  storage_pathrev_p->rev = el_rev->rev;
  storage_pathrev_p->relpath
    = branch_get_storage_rrpath_by_eid(el_rev->branch, el_rev->eid,
                                       result_pool);

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Editor for Commit (independent per-element changes; element-id addressing)
 * ========================================================================
 */

/*  */
#define PAYLOAD_IS_ONLY_BY_REFERENCE(payload) \
    ((payload)->kind == svn_node_unknown)

/* Fetch a payload as *PAYLOAD_P from its storage-pathrev PATH_REV.
 * Fetch names of immediate children of PATH_REV as *CHILDREN_NAMES.
 * Either of the outputs may be null if not wanted.
 */
static svn_error_t *
payload_fetch(svn_element__payload_t **payload_p,
              apr_hash_t **children_names,
              svn_branch__txn_priv_t *eb,
              const svn_pathrev_t *path_rev,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_element__payload_t *payload
    = apr_pcalloc(result_pool, sizeof (*payload));

  SVN_ERR(eb->fetch_func(&payload->kind,
                         &payload->props,
                         &payload->text,
                         children_names,
                         eb->fetch_baton,
                         path_rev->relpath, path_rev->rev,
                         result_pool, scratch_pool));

  SVN_ERR_ASSERT(svn_element__payload_invariants(payload));
  SVN_ERR_ASSERT(payload->kind == svn_node_dir
                 || payload->kind == svn_node_file);
  if (payload_p)
    *payload_p = payload;
  return SVN_NO_ERROR;
}

/* Return the storage-pathrev of PAYLOAD as *STORAGE_PATHREV_P.
 *
 * Find the storage-pathrev from PAYLOAD->branch_ref.
 */
static svn_error_t *
payload_get_storage_pathrev(svn_pathrev_t *storage_pathrev_p,
                            svn_element__payload_t *payload,
                            svn_branch__repos_t *repos,
                            apr_pool_t *result_pool)
{
  SVN_ERR_ASSERT(payload->branch_ref.branch_id /* && ... */);

  SVN_ERR(storage_pathrev_from_branch_ref(storage_pathrev_p,
                                          payload->branch_ref, repos,
                                          result_pool, result_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__compat_fetch(svn_element__payload_t **payload_p,
                         svn_branch__txn_t *txn,
                         svn_element__branch_ref_t branch_ref,
                         svn_branch__compat_fetch_func_t fetch_func,
                         void *fetch_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_branch__txn_priv_t eb;
  svn_pathrev_t storage_pathrev;

  /* Simulate the existence of /top0 in r0. */
  if (branch_ref.rev == 0 && branch_ref.eid == 0)
    {
      *payload_p = svn_element__payload_create_dir(apr_hash_make(result_pool),
                                                   result_pool);
      return SVN_NO_ERROR;
    }

  eb.txn = txn;
  eb.fetch_func = fetch_func;
  eb.fetch_baton = fetch_baton;

  SVN_ERR(storage_pathrev_from_branch_ref(&storage_pathrev,
                                          branch_ref, txn->repos,
                                          scratch_pool, scratch_pool));

  SVN_ERR(payload_fetch(payload_p, NULL,
                        &eb, &storage_pathrev, result_pool, scratch_pool));
  (*payload_p)->branch_ref = branch_ref;
  return SVN_NO_ERROR;
}

/* Fill in the actual payload, from its reference, if not already done.
 */
static svn_error_t *
payload_resolve(svn_element__payload_t *payload,
                svn_branch__txn_priv_t *eb,
                apr_pool_t *scratch_pool)
{
  svn_pathrev_t storage;

  SVN_ERR_ASSERT(svn_element__payload_invariants(payload));

  if (! PAYLOAD_IS_ONLY_BY_REFERENCE(payload))
    return SVN_NO_ERROR;

  SVN_ERR(payload_get_storage_pathrev(&storage, payload,
                                      eb->txn->repos,
                                      scratch_pool));

  SVN_ERR(eb->fetch_func(&payload->kind,
                         &payload->props,
                         &payload->text,
                         NULL,
                         eb->fetch_baton,
                         storage.relpath, storage.rev,
                         payload->pool, scratch_pool));

  SVN_ERR_ASSERT(svn_element__payload_invariants(payload));
  SVN_ERR_ASSERT(! PAYLOAD_IS_ONLY_BY_REFERENCE(payload));
  return SVN_NO_ERROR;
}

/* Update *PATHS, a hash of (storage_rrpath -> svn_branch__el_rev_id_t),
 * creating or filling in entries for all elements in BRANCH.
 */
static svn_error_t *
convert_branch_to_paths(apr_hash_t *paths,
                        svn_branch__state_t *branch,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_element__tree_t *elements;

  /* assert(branch is at a sequence point); */

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *element = apr_hash_this_val(hi);
      const char *rrpath
        = branch_get_storage_rrpath_by_eid(branch, eid, result_pool);
      svn_branch__el_rev_id_t *ba;

      /* A subbranch-root element carries no payload; the corresponding
         inner branch root element will provide the payload for this path. */
      if (element->payload->is_subbranch_root)
        continue;

      /* No other element should exist at this path, given that we avoid
         storing anything for a subbranch-root element. */
      SVN_ERR_ASSERT(! svn_hash_gets(paths, rrpath));

      /* Fill in the details. */
      ba = svn_branch__el_rev_id_create(branch, eid, branch->txn->rev,
                                        result_pool);
      svn_hash_sets(paths, rrpath, ba);
      /*SVN_DBG(("branch-to-path[%d]: b%s e%d -> %s",
               i, svn_branch__get_id(branch, scratch_pool), eid, rrpath));*/
    }
  return SVN_NO_ERROR;
}

/* Produce a mapping from paths to element ids, covering all elements in
 * BRANCH and all its sub-branches, recursively.
 *
 * Update *PATHS_UNION, a hash of (storage_rrpath -> svn_branch__el_rev_id_t),
 * creating or filling in entries for all elements in all branches at and
 * under BRANCH, recursively.
 */
static svn_error_t *
convert_branch_to_paths_r(apr_hash_t *paths_union,
                          svn_branch__state_t *branch,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches;
  int i;

  /*SVN_DBG(("[%d] branch={b%s e%d at '%s'}", idx,
           svn_branch__get_id(branch, scratch_pool), branch->root_eid,
           svn_branch__get_root_rrpath(branch, scratch_pool)));*/
  SVN_ERR(convert_branch_to_paths(paths_union, branch,
                                  result_pool, scratch_pool));

  SVN_ERR(svn_branch__get_immediate_subbranches(branch, &subbranches,
                                               scratch_pool, scratch_pool));
  /* Rercurse into sub-branches */
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch__state_t *b = APR_ARRAY_IDX(subbranches, i, void *);

      SVN_ERR(convert_branch_to_paths_r(paths_union, b, result_pool,
                                        scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* Return TRUE iff INITIAL_PAYLOAD and FINAL_PAYLOAD are both non-null
 * and have the same properties.
 */
static svn_boolean_t
props_equal(svn_element__payload_t *initial_payload,
            svn_element__payload_t *final_payload,
            apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  if (!initial_payload || !final_payload)
    return FALSE;

  svn_error_clear(svn_prop_diffs(&prop_diffs,
                                 initial_payload->props,
                                 final_payload->props,
                                 scratch_pool));
  return (prop_diffs->nelts == 0);
}

/* Return TRUE iff INITIAL_PAYLOAD and FINAL_PAYLOAD are both file payload
 * and have the same text.
 */
static svn_boolean_t
text_equal(svn_element__payload_t *initial_payload,
           svn_element__payload_t *final_payload)
{
  if (!initial_payload || !final_payload
      || initial_payload->kind != svn_node_file
      || final_payload->kind != svn_node_file)
    {
      return FALSE;
    }

  return svn_stringbuf_compare(initial_payload->text,
                               final_payload->text);
}

/* Return the copy-from location to be used if this is to be a copy;
 * otherwise return NULL.
 *
 * ### Currently this is indicated by payload-by-reference, which is
 *     an inadequate indication.
 */
static svn_error_t *
get_copy_from(svn_pathrev_t *copyfrom_pathrev_p,
              svn_element__payload_t *final_payload,
              svn_branch__txn_priv_t *eb,
              apr_pool_t *result_pool)
{
  if (final_payload->branch_ref.branch_id)
    {
      SVN_ERR(payload_get_storage_pathrev(copyfrom_pathrev_p, final_payload,
                                          eb->txn->repos,
                                          result_pool));
    }
  else
    {
      copyfrom_pathrev_p->relpath = NULL;
      copyfrom_pathrev_p->rev = SVN_INVALID_REVNUM;
    }

  return SVN_NO_ERROR;
}

/* Return a hash whose keys are the names of the immediate children of
 * RRPATH in PATHS.
 */
static apr_hash_t *
get_immediate_children_names(apr_hash_t *paths,
                             const char *parent_rrpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  apr_hash_t *children = apr_hash_make(result_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, paths); hi; hi = apr_hash_next(hi))
    {
      const char *this_rrpath = apr_hash_this_key(hi);

      if (this_rrpath[0]
          && strcmp(parent_rrpath, svn_relpath_dirname(this_rrpath,
                                                       scratch_pool)) == 0)
        {
          svn_hash_sets(children,
                        svn_relpath_basename(this_rrpath, result_pool), "");
        }
    }

  return children;
}

/* Generate Ev1 instructions to edit from a current state to a final state
 * at RRPATH, recursing for child paths of RRPATH.
 *
 * The current state at RRPATH might not be the initial state because,
 * although neither RRPATH nor any sub-paths have been explicitly visited
 * before, the current state at RRPATH and its sub-paths might be the
 * result of a copy.
 *
 * PRED_LOC is the predecessor location of the node currently at RRPATH in
 * the Ev1 transaction, or NULL if there is no node currently at RRPATH.
 * If the node is copied, including a child of a copy, this is its copy-from
 * location, otherwise this is its location in the txn base revision.
 * (The current node cannot be a plain added node on entry to this function,
 * as the function must be called only once for each path and there is no
 * recursive add operation.) PRED_LOC identifies the node content that the
 * that the Ev1 edit needs to delete, replace, update or leave unchanged.
 *
 * Process a single hierarchy of nested branches, rooted in the top-level
 * branch TOP_BRANCH_NUM.
 */
static svn_error_t *
drive_changes_r(const char *rrpath,
                svn_pathrev_t *pred_loc,
                apr_hash_t *paths_final,
                const char *top_branch_id,
                svn_branch__txn_priv_t *eb,
                apr_pool_t *scratch_pool)
{
  /* The el-rev-id of the element that will finally exist at RRPATH. */
  svn_branch__el_rev_id_t *final_el_rev = svn_hash_gets(paths_final, rrpath);
  svn_element__payload_t *final_payload;
  svn_pathrev_t final_copy_from;
  svn_boolean_t succession;

  /*SVN_DBG(("rrpath '%s' current=%s, final=e%d)",
           rrpath,
           pred_loc ? pathrev_str(*pred_loc, scratch_pool) : "<nil>",
           final_el_rev ? final_el_rev->eid : -1));*/

  SVN_ERR_ASSERT(!pred_loc
                 || (pred_loc->relpath && SVN_IS_VALID_REVNUM(pred_loc->rev)));

  if (final_el_rev)
    {
      svn_element__content_t *final_element;

      SVN_ERR(svn_branch__state_get_element(final_el_rev->branch, &final_element,
                                            final_el_rev->eid, scratch_pool));
      /* A non-null FINAL address means an element exists there. */
      SVN_ERR_ASSERT(final_element);

      final_payload = final_element->payload;

      /* Decide whether the state at this path should be a copy (incl. a
         copy-child) */
      SVN_ERR(get_copy_from(&final_copy_from, final_payload, eb, scratch_pool));
      /* It doesn't make sense to have a non-copy inside a copy */
      /*SVN_ERR_ASSERT(!(parent_is_copy && !final_copy_from));*/
   }
  else
    {
      final_payload = NULL;
      final_copy_from.relpath = NULL;
    }

  /* Succession means:
       for a copy (inc. child)  -- copy-from same place as natural predecessor
       otherwise                -- it's succession if it's the same element
                                   (which also implies the same kind) */
  if (pred_loc && final_copy_from.relpath)
    {
      succession = pathrev_equal(pred_loc, &final_copy_from);
    }
  else if (pred_loc && final_el_rev)
    {
      svn_branch__el_rev_id_t *pred_el_rev;

      SVN_ERR(svn_branch__repos_find_el_rev_by_path_rev(&pred_el_rev,
                                            eb->txn->repos,
                                            pred_loc->rev,
                                            top_branch_id,
                                            pred_loc->relpath,
                                            scratch_pool, scratch_pool));

      succession = (pred_el_rev->eid == final_el_rev->eid);
    }
  else
    {
      succession = FALSE;
    }

  /* If there's an initial node that isn't also going to be the final
     node at this path, then it's being deleted or replaced: delete it. */
  if (pred_loc && !succession)
    {
      /* Issue an Ev1 delete unless this path is inside a path at which
         we've already issued a delete. */
      if (check_existence(eb->changes, rrpath) != svn_tristate_false)
        {
          /*SVN_DBG(("ev1:del(%s)", rrpath));*/
          /* ### We don't need "delete_subtree", we only need to insert a
             single delete operation, as we know we haven't
             inserted any changes inside this subtree. */
          SVN_ERR(delete_subtree(eb->changes, rrpath, pred_loc->rev));
        }
      else
        {
          /*SVN_DBG(("ev1:del(%s): parent is already deleted", rrpath))*/
        }
    }

  /* If there's a final node, it's being added or modified.
     Or it's unchanged -- we do nothing in that case. */
  if (final_el_rev)
    {
      svn_element__payload_t *current_payload = NULL;
      apr_hash_t *current_children = NULL;
      change_node_t *change = NULL;

      /* Get the full payload of the final node. If we have
         only a reference to the payload, fetch it in full. */
      SVN_ERR_ASSERT(final_payload);
      SVN_ERR(payload_resolve(final_payload, eb, scratch_pool));

      /* If the final node was also the initial node, it's being
         modified, otherwise it's being added (perhaps a replacement). */
      if (succession)
        {
          /* Get full payload of the current node */
          SVN_ERR(payload_fetch(&current_payload, &current_children,
                                eb, pred_loc,
                                scratch_pool, scratch_pool));

          /* If no changes to make, then skip this path */
          if (svn_element__payload_equal(current_payload,
                                         final_payload, scratch_pool))
            {
              /*SVN_DBG(("ev1:no-op(%s)", rrpath));*/
            }
          else
            {
              /*SVN_DBG(("ev1:mod(%s)", rrpath));*/
              SVN_ERR(insert_change(&change, eb->changes, rrpath,
                                    RESTRUCTURE_NONE));
              change->changing_rev = pred_loc->rev;
            }
        }
      else /* add or copy/move */
        {
          /*SVN_DBG(("ev1:add(%s)", rrpath));*/
          SVN_ERR(insert_change(&change, eb->changes, rrpath,
                                RESTRUCTURE_ADD));

          /* If the node is to be copied (and possibly modified) ... */
          if (final_copy_from.relpath)
            {
              change->copyfrom_rev = final_copy_from.rev;
              change->copyfrom_path = final_copy_from.relpath;

              /* Get full payload of the copy source node */
              SVN_ERR(payload_fetch(&current_payload, &current_children,
                                    eb, &final_copy_from,
                                    scratch_pool, scratch_pool));
            }
        }

      if (change)
        {
          /* Copy the required content into the change record. Avoid no-op
             changes of props / text, not least to minimize clutter when
             debugging Ev1 operations. */
          SVN_ERR_ASSERT(final_payload->kind == svn_node_dir
                         || final_payload->kind == svn_node_file);
          change->kind = final_payload->kind;
          if (!props_equal(current_payload, final_payload, scratch_pool))
            {
              change->props = final_payload->props;
            }
          if (final_payload->kind == svn_node_file
              && !text_equal(current_payload, final_payload))
            {
              change->contents_text = final_payload->text;
            }
        }

      /* Recurse to process this directory's children */
      if (final_payload->kind == svn_node_dir)
        {
          apr_hash_t *final_children;
          apr_hash_t *union_children;
          apr_hash_index_t *hi;

          final_children = get_immediate_children_names(paths_final, rrpath,
                                                        scratch_pool,
                                                        scratch_pool);
          union_children = (current_children
                            ? hash_overlay(current_children, final_children)
                            : final_children);
          for (hi = apr_hash_first(scratch_pool, union_children);
               hi; hi = apr_hash_next(hi))
            {
              const char *name = apr_hash_this_key(hi);
              const char *this_rrpath = svn_relpath_join(rrpath, name,
                                                         scratch_pool);
              svn_boolean_t child_in_current
                = current_children && svn_hash_gets(current_children, name);
              svn_pathrev_t *child_pred = NULL;

              if (child_in_current)
                {
                 /* If the parent dir is copied, then this child has been
                    copied along with it: predecessor is parent's copy-from
                    location extended by the child's name. */
                  child_pred = apr_palloc(scratch_pool, sizeof(*child_pred));
                  if (final_copy_from.relpath)
                    {
                      child_pred->rev = final_copy_from.rev;
                      child_pred->relpath
                        = svn_relpath_join(final_copy_from.relpath, name,
                                           scratch_pool);
                    }
                  else
                    {
                      child_pred->rev = pred_loc->rev;
                      child_pred->relpath = this_rrpath;
                    }
               }

              SVN_ERR(drive_changes_r(this_rrpath,
                                      child_pred,
                                      paths_final, top_branch_id,
                                      eb, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/*
 * Drive svn_delta_editor_t (actions: add/copy/delete/modify) from
 * a before-and-after element mapping.
 */
static svn_error_t *
drive_changes(svn_branch__txn_priv_t *eb,
              apr_pool_t *scratch_pool)
{
  apr_array_header_t *branches;
  int i;
  const apr_array_header_t *paths;

  /* Convert the element mappings to an svn_delta_editor_t traversal.

        1. find union of paths in initial and final states, across all branches.
        2. traverse paths in depth-first order.
        3. modify/delete/add/replace as needed at each path.
   */

  /* Process one hierarchy of nested branches at a time. */
  branches = svn_branch__txn_get_branches(eb->txn, scratch_pool);
  for (i = 0; i < branches->nelts; i++)
    {
      svn_branch__state_t *root_branch = APR_ARRAY_IDX(branches, i, void *);
      apr_hash_t *paths_final;

      const char *top_path = branch_get_storage_root_rrpath(root_branch,
                                                            scratch_pool);
      svn_pathrev_t current;
      svn_branch__state_t *base_root_branch;
      svn_boolean_t branch_is_new;

      if (strchr(root_branch->bid, '.'))
        continue;  /* that's not a root branch */

      SVN_ERR(svn_branch__repos_get_branch_by_id(&base_root_branch,
                                                 eb->txn->repos,
                                                 eb->txn->base_rev,
                                                 root_branch->bid, scratch_pool));
      branch_is_new = !base_root_branch;

      paths_final = apr_hash_make(scratch_pool);
      SVN_ERR(convert_branch_to_paths_r(paths_final,
                                        root_branch,
                                        scratch_pool, scratch_pool));

      current.rev = eb->txn->base_rev;
      current.relpath = top_path;

      /* Create the top-level storage node if the branch is new, or if this is
         the first commit to branch B0 which was created in r0 but had no
         storage node there. */
      if (branch_is_new || current.rev == 0)
        {
          change_node_t *change;

          SVN_ERR(insert_change(&change, eb->changes, top_path, RESTRUCTURE_ADD));
          change->kind = svn_node_dir;
        }

      SVN_ERR(drive_changes_r(top_path, &current,
                              paths_final, svn_branch__get_id(root_branch,
                                                              scratch_pool),
                              eb, scratch_pool));
    }

  /* If the driver has not explicitly opened the root directory via the
     start_edit (aka open_root) callback, do so now. */
  if (eb->ev1_root_dir_baton == NULL)
    SVN_ERR(open_root_ev3(eb, SVN_INVALID_REVNUM));

  /* Make the path driver visit the root dir of the edit. Otherwise, it
     will attempt an open_root() instead, which we already did. */
  if (! svn_hash_gets(eb->changes, ""))
    {
      change_node_t *change;

      SVN_ERR(insert_change(&change, eb->changes, "", RESTRUCTURE_NONE));
      change->kind = svn_node_dir;
    }

  /* Apply the appropriate Ev1 change to each Ev1-relative path. */
  paths = get_unsorted_paths(eb->changes, scratch_pool);
  SVN_ERR(svn_delta_path_driver2(eb->deditor, eb->dedit_baton,
                                 paths, TRUE /*sort*/,
                                 apply_change, (void *)eb,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static apr_array_header_t *
compat_branch_txn_get_branches(const svn_branch__txn_t *txn,
                               apr_pool_t *result_pool)
{
  /* Just forwarding: nothing more is needed. */
  apr_array_header_t *branches
    = svn_branch__txn_get_branches(txn->priv->txn,
                                   result_pool);

  return branches;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_delete_branch(svn_branch__txn_t *txn,
                                const char *bid,
                                apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_delete_branch(txn->priv->txn,
                                        bid,
                                        scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_get_num_new_eids(const svn_branch__txn_t *txn,
                                   int *num_new_eids_p,
                                   apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_get_num_new_eids(txn->priv->txn,
                                           num_new_eids_p,
                                           scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_new_eid(svn_branch__txn_t *txn,
                          svn_branch__eid_t *eid_p,
                          apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_new_eid(txn->priv->txn,
                                  eid_p,
                                  scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_finalize_eids(svn_branch__txn_t *txn,
                                apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_finalize_eids(txn->priv->txn,
                                        scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_open_branch(svn_branch__txn_t *txn,
                              svn_branch__state_t **new_branch_p,
                              const char *new_branch_id,
                              int root_eid,
                              svn_branch__rev_bid_eid_t *tree_ref,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_open_branch(txn->priv->txn,
                                      new_branch_p,
                                      new_branch_id, root_eid, tree_ref,
                                      result_pool,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_serialize(svn_branch__txn_t *txn,
                            svn_stream_t *stream,
                            apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_serialize(txn->priv->txn,
                                    stream,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_sequence_point(svn_branch__txn_t *txn,
                                 apr_pool_t *scratch_pool)
{
  /* Just forwarding: nothing more is needed. */
  SVN_ERR(svn_branch__txn_sequence_point(txn->priv->txn,
                                         scratch_pool));
  return SVN_NO_ERROR;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_complete(svn_branch__txn_t *txn,
                           apr_pool_t *scratch_pool)
{
  svn_branch__txn_priv_t *eb = txn->priv;
  svn_error_t *err;

  /* Convert the transaction to a revision */
  SVN_ERR(svn_branch__txn_sequence_point(txn->priv->txn, scratch_pool));
  SVN_ERR(svn_branch__txn_finalize_eids(txn->priv->txn, scratch_pool));

  err = drive_changes(eb, scratch_pool);

  if (!err)
     {
       err = svn_error_compose_create(err, eb->deditor->close_edit(
                                                            eb->dedit_baton,
                                                            scratch_pool));
     }

  if (err)
    svn_error_clear(eb->deditor->abort_edit(eb->dedit_baton, scratch_pool));

  SVN_ERR(svn_branch__txn_complete(txn->priv->txn, scratch_pool));

  return err;
}

/* An #svn_branch__txn_t method. */
static svn_error_t *
compat_branch_txn_abort(svn_branch__txn_t *txn,
                        apr_pool_t *scratch_pool)
{
  svn_branch__txn_priv_t *eb = txn->priv;

  SVN_ERR(eb->deditor->abort_edit(eb->dedit_baton, scratch_pool));

  SVN_ERR(svn_branch__txn_abort(txn->priv->txn,
                                scratch_pool));
  return SVN_NO_ERROR;
}

/* Baton for wrap_fetch_func. */
typedef struct wrap_fetch_baton_t
{
  /* Wrapped fetcher */
  svn_branch__compat_fetch_func_t fetch_func;
  void *fetch_baton;
} wrap_fetch_baton_t;

/* The purpose of this fetcher-wrapper is to make it appear that B0
 * was created (as an empty dir) in r0.
 */
static svn_error_t *
wrap_fetch_func(svn_node_kind_t *kind,
                apr_hash_t **props,
                svn_stringbuf_t **file_text,
                apr_hash_t **children_names,
                void *baton,
                const char *repos_relpath,
                svn_revnum_t revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  wrap_fetch_baton_t *b = baton;

  if (revision == 0 && strcmp(repos_relpath, "top0") == 0)
    {
      if (kind)
        *kind = svn_node_dir;
      if (props)
        *props = apr_hash_make(result_pool);
      if (file_text)
        *file_text = NULL;
      if (children_names)
        *children_names = apr_hash_make(result_pool);
    }
  else
    {
      SVN_ERR(b->fetch_func(kind, props, file_text, children_names,
                            b->fetch_baton,
                            repos_relpath, revision,
                            result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__compat_txn_from_delta_for_commit(
                        svn_branch__txn_t **txn_p,
                        svn_branch__compat_shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch__txn_t *branching_txn,
                        const char *repos_root_url,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  static const svn_branch__txn_vtable_t vtable = {
    {0},
    compat_branch_txn_get_branches,
    compat_branch_txn_delete_branch,
    compat_branch_txn_get_num_new_eids,
    compat_branch_txn_new_eid,
    compat_branch_txn_open_branch,
    compat_branch_txn_finalize_eids,
    compat_branch_txn_serialize,
    compat_branch_txn_sequence_point,
    compat_branch_txn_complete,
    compat_branch_txn_abort
  };
  svn_branch__txn_t *txn;
  svn_branch__txn_priv_t *eb = apr_pcalloc(result_pool, sizeof(*eb));
  wrap_fetch_baton_t *wb = apr_pcalloc(result_pool, sizeof(*wb));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;

  eb->repos_root_url = apr_pstrdup(result_pool, repos_root_url);

  eb->changes = apr_hash_make(result_pool);

  wb->fetch_func = fetch_func;
  wb->fetch_baton = fetch_baton;
  eb->fetch_func = wrap_fetch_func;
  eb->fetch_baton = wb;

  eb->edit_pool = result_pool;

  branching_txn = svn_branch__nested_txn_create(branching_txn, result_pool);

  eb->txn = branching_txn;

  txn = svn_branch__txn_create(&vtable, NULL, NULL, result_pool);
  txn->priv = eb;
  txn->repos = branching_txn->repos;
  txn->rev = branching_txn->rev;
  txn->base_rev = branching_txn->base_rev;
  *txn_p = txn;

  if (shim_connector)
    {
#if 0
      *shim_connector = apr_palloc(result_pool, sizeof(**shim_connector));
#ifdef SHIM_WITH_ABS_PATHS
      (*shim_connector)->ev1_absolute_paths
        = apr_palloc(result_pool, sizeof(svn_boolean_t));
      eb->make_abs_paths = (*shim_connector)->ev1_absolute_paths;
#endif
      (*shim_connector)->target_revision_func = set_target_revision_ev3;
      (*shim_connector)->start_edit_func = open_root_ev3;
#ifdef SHIM_WITH_UNLOCK
      (*shim_connector)->unlock_func = do_unlock;
#endif
      (*shim_connector)->baton = eb;
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch__compat_txn_from_delta_for_update(
                        svn_branch__compat_update_editor3_t **update_editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch__txn_t *branching_txn,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_branch__compat_update_editor3_t *update_editor
    = apr_pcalloc(result_pool, sizeof(*update_editor));
  svn_branch__compat_shim_connector_t *shim_connector;

  /*(("svn_delta__ev3_from_delta_for_update(base='%s')...",
           base_repos_relpath));*/

  /*SVN_ERR(svn_delta__get_debug_editor(&deditor, &dedit_baton,
                                      deditor, dedit_baton,
                                      "[1>UP] ", result_pool));*/
  SVN_ERR(svn_branch__compat_txn_from_delta_for_commit(
                        &update_editor->edit_txn,
                        &shim_connector,
                        deditor, dedit_baton,
                        branching_txn, repos_root_url,
                        fetch_func, fetch_baton,
                        cancel_func, cancel_baton,
                        result_pool, scratch_pool));

  update_editor->set_target_revision_func = shim_connector->target_revision_func;
  update_editor->set_target_revision_baton = shim_connector->baton;
  /* shim_connector->start_edit_func = open_root_ev3; */
#ifdef SHIM_WITH_ABS_PATHS
  update_editor->ev1_absolute_paths /*...*/;
#endif
#ifdef SHIM_WITH_UNLOCK
  update_editor->unlock_func = do_unlock;
#endif

  *update_editor_p = update_editor;
  return SVN_NO_ERROR;
}
