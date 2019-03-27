/*
 * replay.c:   an editor driver for changes made in a given revision
 *             or transaction
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


#include <apr_hash.h>

#include "svn_types.h"
#include "svn_delta.h"
#include "svn_hash.h"
#include "svn_fs.h"
#include "svn_checksum.h"
#include "svn_repos.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "private/svn_fspath.h"
#include "private/svn_repos_private.h"
#include "private/svn_delta_private.h"
#include "private/svn_sorts_private.h"


/*** Backstory ***/

/* The year was 2003.  Subversion usage was rampant in the world, and
   there was a rapidly growing issues database to prove it.  To make
   matters worse, svn_repos_dir_delta() had simply outgrown itself.
   No longer content to simply describe the differences between two
   trees, the function had been slowly bearing the added
   responsibility of representing the actions that had been taken to
   cause those differences -- a burden it was never meant to bear.
   Now grown into a twisted mess of razor-sharp metal and glass, and
   trembling with a sort of momentarily stayed spring force,
   svn_repos_dir_delta was a timebomb poised for total annihilation of
   the American Midwest.

   Subversion needed a change.

   Changes, in fact.  And not just in the literary segue sense.  What
   Subversion desperately needed was a new mechanism solely
   responsible for replaying repository actions back to some
   interested party -- to translate and retransmit the contents of the
   Berkeley 'changes' database file. */

/*** Overview ***/

/* The filesystem keeps a record of high-level actions that affect the
   files and directories in itself.  The 'changes' table records
   additions, deletions, textual and property modifications, and so
   on.  The goal of the functions in this file is to examine those
   change records, and use them to drive an editor interface in such a
   way as to effectively replay those actions.

   This is critically different than what svn_repos_dir_delta() was
   designed to do.  That function describes, in the simplest way it
   can, how to transform one tree into another.  It doesn't care
   whether or not this was the same way a user might have done this
   transformation.  More to the point, it doesn't care if this is how
   those differences *did* come into being.  And it is for this reason
   that it cannot be relied upon for tasks such as the repository
   dumpfile-generation code, which is supposed to represent not
   changes, but actions that cause changes.

   So, what's the plan here?

   First, we fetch the changes for a particular revision or
   transaction.  We get these as an array, sorted chronologically.
   From this array we will build a hash, keyed on the path associated
   with each change item, and whose values are arrays of changes made
   to that path, again preserving the chronological ordering.

   Once our hash is built, we then sort all the keys of the hash (the
   paths) using a depth-first directory sort routine.

   Finally, we drive an editor, moving down our list of sorted paths,
   and manufacturing any intermediate editor calls (directory openings
   and closures) needed to navigate between each successive path.  For
   each path, we replay the sorted actions that occurred at that path.

   When we've finished the editor drive, we should have fully replayed
   the filesystem events that occurred in that revision or transaction
   (though not necessarily in the same order in which they
   occurred). */

/* #define USE_EV2_IMPL */


/*** Helper functions. ***/


/* Information for an active copy, that is a directory which we are currently
   working on and which was added with history. */
struct copy_info
{
  /* Destination relpath (relative to the root of the  . */
  const char *path;

  /* Copy source path (expressed as an absolute FS path) or revision.
     NULL and SVN_INVALID_REVNUM if this is an add without history,
     nested inside an add with history. */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
};

struct path_driver_cb_baton
{
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* The root of the revision we're replaying. */
  svn_fs_root_t *root;

  /* The root of the previous revision.  If this is non-NULL it means that
     we are supposed to generate props and text deltas relative to it. */
  svn_fs_root_t *compare_root;

  apr_hash_t *changed_paths;

  svn_repos_authz_func_t authz_read_func;
  void *authz_read_baton;

  const char *base_path; /* relpath */

  svn_revnum_t low_water_mark;
  /* Stack of active copy operations. */
  apr_array_header_t *copies;

  /* The global pool for this replay operation. */
  apr_pool_t *pool;
};

/* Recursively traverse EDIT_PATH (as it exists under SOURCE_ROOT) emitting
   the appropriate editor calls to add it and its children without any
   history.  This is meant to be used when either a subset of the tree
   has been ignored and we need to copy something from that subset to
   the part of the tree we do care about, or if a subset of the tree is
   unavailable because of authz and we need to use it as the source of
   a copy. */
static svn_error_t *
add_subdir(svn_fs_root_t *source_root,
           svn_fs_root_t *target_root,
           const svn_delta_editor_t *editor,
           void *edit_baton,
           const char *edit_path,
           void *parent_baton,
           const char *source_fspath,
           svn_repos_authz_func_t authz_read_func,
           void *authz_read_baton,
           apr_hash_t *changed_paths,
           apr_pool_t *pool,
           void **dir_baton)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi, *phi;
  apr_hash_t *dirents;
  apr_hash_t *props;

  SVN_ERR(editor->add_directory(edit_path, parent_baton, NULL,
                                SVN_INVALID_REVNUM, pool, dir_baton));

  SVN_ERR(svn_fs_node_proplist(&props, target_root, edit_path, pool));

  for (phi = apr_hash_first(pool, props); phi; phi = apr_hash_next(phi))
    {
      const char *key = apr_hash_this_key(phi);
      svn_string_t *val = apr_hash_this_val(phi);

      svn_pool_clear(subpool);
      SVN_ERR(editor->change_dir_prop(*dir_baton, key, val, subpool));
    }

  /* We have to get the dirents from the source path, not the target,
     because we want nested copies from *readable* paths to be handled by
     path_driver_cb_func, not add_subdir (in order to preserve history). */
  SVN_ERR(svn_fs_dir_entries(&dirents, source_root, source_fspath, pool));

  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      svn_fs_path_change3_t *change;
      svn_boolean_t readable = TRUE;
      svn_fs_dirent_t *dent = apr_hash_this_val(hi);
      const char *copyfrom_path = NULL;
      svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;
      const char *new_edit_path;

      svn_pool_clear(subpool);

      new_edit_path = svn_relpath_join(edit_path, dent->name, subpool);

      /* If a file or subdirectory of the copied directory is listed as a
         changed path (because it was modified after the copy but before the
         commit), we remove it from the changed_paths hash so that future
         calls to path_driver_cb_func will ignore it. */
      change = svn_hash_gets(changed_paths, new_edit_path);
      if (change)
        {
          svn_hash_sets(changed_paths, new_edit_path, NULL);

          /* If it's a delete, skip this entry. */
          if (change->change_kind == svn_fs_path_change_delete)
            continue;

          /* If it's a replacement, check for copyfrom info (if we
             don't have it already. */
          if (change->change_kind == svn_fs_path_change_replace)
            {
              if (! change->copyfrom_known)
                {
                  SVN_ERR(svn_fs_copied_from(&change->copyfrom_rev,
                                             &change->copyfrom_path,
                                             target_root, new_edit_path, pool));
                  change->copyfrom_known = TRUE;
                }
              copyfrom_path = change->copyfrom_path;
              copyfrom_rev = change->copyfrom_rev;
            }
        }

      if (authz_read_func)
        SVN_ERR(authz_read_func(&readable, target_root, new_edit_path,
                                authz_read_baton, pool));

      if (! readable)
        continue;

      if (dent->kind == svn_node_dir)
        {
          svn_fs_root_t *new_source_root;
          const char *new_source_fspath;
          void *new_dir_baton;

          if (copyfrom_path)
            {
              svn_fs_t *fs = svn_fs_root_fs(source_root);
              SVN_ERR(svn_fs_revision_root(&new_source_root, fs,
                                           copyfrom_rev, pool));
              new_source_fspath = copyfrom_path;
            }
          else
            {
              new_source_root = source_root;
              new_source_fspath = svn_fspath__join(source_fspath, dent->name,
                                                   subpool);
            }

          /* ### authz considerations?
           *
           * I think not; when path_driver_cb_func() calls add_subdir(), it
           * passes SOURCE_ROOT and SOURCE_FSPATH that are unreadable.
           */
          if (change && change->change_kind == svn_fs_path_change_replace
              && copyfrom_path == NULL)
            {
              SVN_ERR(editor->add_directory(new_edit_path, *dir_baton,
                                            NULL, SVN_INVALID_REVNUM,
                                            subpool, &new_dir_baton));
            }
          else
            {
              SVN_ERR(add_subdir(new_source_root, target_root,
                                 editor, edit_baton, new_edit_path,
                                 *dir_baton, new_source_fspath,
                                 authz_read_func, authz_read_baton,
                                 changed_paths, subpool, &new_dir_baton));
            }

          SVN_ERR(editor->close_directory(new_dir_baton, subpool));
        }
      else if (dent->kind == svn_node_file)
        {
          svn_txdelta_window_handler_t delta_handler;
          void *delta_handler_baton, *file_baton;
          svn_txdelta_stream_t *delta_stream;
          svn_checksum_t *checksum;

          SVN_ERR(editor->add_file(new_edit_path, *dir_baton, NULL,
                                   SVN_INVALID_REVNUM, pool, &file_baton));

          SVN_ERR(svn_fs_node_proplist(&props, target_root,
                                       new_edit_path, subpool));

          for (phi = apr_hash_first(pool, props); phi; phi = apr_hash_next(phi))
            {
              const char *key = apr_hash_this_key(phi);
              svn_string_t *val = apr_hash_this_val(phi);

              SVN_ERR(editor->change_file_prop(file_baton, key, val, subpool));
            }

          SVN_ERR(editor->apply_textdelta(file_baton, NULL, pool,
                                          &delta_handler,
                                          &delta_handler_baton));

          SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream, NULL, NULL,
                                               target_root, new_edit_path,
                                               pool));

          SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                            delta_handler,
                                            delta_handler_baton,
                                            pool));

          SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5, target_root,
                                       new_edit_path, TRUE, pool));
          SVN_ERR(editor->close_file(file_baton,
                                     svn_checksum_to_cstring(checksum, pool),
                                     pool));
        }
      else
        SVN_ERR_MALFUNCTION();
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Given PATH deleted under ROOT, return in READABLE whether the path was
   readable prior to the deletion.  Consult COPIES (a stack of 'struct
   copy_info') and AUTHZ_READ_FUNC. */
static svn_error_t *
was_readable(svn_boolean_t *readable,
             svn_fs_root_t *root,
             const char *path,
             apr_array_header_t *copies,
             svn_repos_authz_func_t authz_read_func,
             void *authz_read_baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_fs_root_t *inquire_root;
  const char *inquire_path;
  struct copy_info *info = NULL;
  const char *relpath;

  /* Short circuit. */
  if (! authz_read_func)
    {
      *readable = TRUE;
      return SVN_NO_ERROR;
    }

  if (copies->nelts != 0)
    info = APR_ARRAY_IDX(copies, copies->nelts - 1, struct copy_info *);

  /* Are we under a copy? */
  if (info && (relpath = svn_relpath_skip_ancestor(info->path, path)))
    {
      SVN_ERR(svn_fs_revision_root(&inquire_root, svn_fs_root_fs(root),
                                   info->copyfrom_rev, scratch_pool));
      inquire_path = svn_fspath__join(info->copyfrom_path, relpath,
                                      scratch_pool);
    }
  else
    {
      /* Compute the revision that ROOT is based on.  (Note that ROOT is not
         r0's root, since this function is only called for deletions.)
         ### Need a more succinct way to express this */
      svn_revnum_t inquire_rev = SVN_INVALID_REVNUM;
      if (svn_fs_is_txn_root(root))
        inquire_rev = svn_fs_txn_root_base_revision(root);
      if (svn_fs_is_revision_root(root))
        inquire_rev =  svn_fs_revision_root_revision(root)-1;
      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(inquire_rev));

      SVN_ERR(svn_fs_revision_root(&inquire_root, svn_fs_root_fs(root),
                                   inquire_rev, scratch_pool));
      inquire_path = path;
    }

  SVN_ERR(authz_read_func(readable, inquire_root, inquire_path,
                          authz_read_baton, result_pool));

  return SVN_NO_ERROR;
}

/* Initialize COPYFROM_ROOT, COPYFROM_PATH, and COPYFROM_REV with the
   revision root, fspath, and revnum of the copyfrom of CHANGE, which
   corresponds to PATH under ROOT.  If the copyfrom info is valid
   (i.e., is not (NULL, SVN_INVALID_REVNUM)), then initialize SRC_READABLE
   too, consulting AUTHZ_READ_FUNC and AUTHZ_READ_BATON if provided.

   NOTE: If the copyfrom information in CHANGE is marked as unknown
   (meaning, its ->copyfrom_rev and ->copyfrom_path cannot be
   trusted), this function will also update those members of the
   CHANGE structure to carry accurate copyfrom information.  */
static svn_error_t *
fill_copyfrom(svn_fs_root_t **copyfrom_root,
              const char **copyfrom_path,
              svn_revnum_t *copyfrom_rev,
              svn_boolean_t *src_readable,
              svn_fs_root_t *root,
              svn_fs_path_change3_t *change,
              svn_repos_authz_func_t authz_read_func,
              void *authz_read_baton,
              const char *path,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  if (! change->copyfrom_known)
    {
      SVN_ERR(svn_fs_copied_from(&(change->copyfrom_rev),
                                 &(change->copyfrom_path),
                                 root, path, result_pool));
      change->copyfrom_known = TRUE;
    }
  *copyfrom_rev = change->copyfrom_rev;
  *copyfrom_path = change->copyfrom_path;

  if (*copyfrom_path && SVN_IS_VALID_REVNUM(*copyfrom_rev))
    {
      SVN_ERR(svn_fs_revision_root(copyfrom_root,
                                   svn_fs_root_fs(root),
                                   *copyfrom_rev, result_pool));

      if (authz_read_func)
        {
          SVN_ERR(authz_read_func(src_readable, *copyfrom_root,
                                  *copyfrom_path,
                                  authz_read_baton, result_pool));
        }
      else
        *src_readable = TRUE;
    }
  else
    {
      *copyfrom_root = NULL;
      /* SRC_READABLE left uninitialized */
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *edit_path,
                    apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb = callback_baton;
  const svn_delta_editor_t *editor = cb->editor;
  void *edit_baton = cb->edit_baton;
  svn_fs_root_t *root = cb->root;
  svn_fs_path_change3_t *change;
  svn_boolean_t do_add = FALSE, do_delete = FALSE;
  void *file_baton = NULL;
  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;
  svn_fs_root_t *source_root = cb->compare_root;
  const char *source_fspath = NULL;
  const char *base_path = cb->base_path;

  *dir_baton = NULL;

  /* Initialize SOURCE_FSPATH. */
  if (source_root)
    source_fspath = svn_fspath__canonicalize(edit_path, pool);

  /* First, flush the copies stack so it only contains ancestors of path. */
  while (cb->copies->nelts > 0
         && ! svn_dirent_is_ancestor(APR_ARRAY_IDX(cb->copies,
                                                   cb->copies->nelts - 1,
                                                   struct copy_info *)->path,
                                     edit_path))
    apr_array_pop(cb->copies);

  change = svn_hash_gets(cb->changed_paths, edit_path);
  if (! change)
    {
      /* This can only happen if the path was removed from cb->changed_paths
         by an earlier call to add_subdir, which means the path was already
         handled and we should simply ignore it. */
      return SVN_NO_ERROR;
    }
  switch (change->change_kind)
    {
    case svn_fs_path_change_add:
      do_add = TRUE;
      break;

    case svn_fs_path_change_delete:
      do_delete = TRUE;
      break;

    case svn_fs_path_change_replace:
      do_add = TRUE;
      do_delete = TRUE;
      break;

    case svn_fs_path_change_modify:
    default:
      /* do nothing */
      break;
    }

  /* Handle any deletions. */
  if (do_delete)
    {
      svn_boolean_t readable;

      /* Issue #4121: delete under under a copy, of a path that was unreadable
         at its pre-copy location. */
      SVN_ERR(was_readable(&readable, root, edit_path, cb->copies,
                            cb->authz_read_func, cb->authz_read_baton,
                            pool, pool));
      if (readable)
        SVN_ERR(editor->delete_entry(edit_path, SVN_INVALID_REVNUM,
                                     parent_baton, pool));
    }

  /* Fetch the node kind if it makes sense to do so. */
  if (! do_delete || do_add)
    {
      if (change->node_kind == svn_node_unknown)
        SVN_ERR(svn_fs_check_path(&(change->node_kind), root, edit_path, pool));
      if ((change->node_kind != svn_node_dir) &&
          (change->node_kind != svn_node_file))
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Filesystem path '%s' is neither a file "
                                   "nor a directory"), edit_path);
    }

  /* Handle any adds/opens. */
  if (do_add)
    {
      svn_boolean_t src_readable;
      svn_fs_root_t *copyfrom_root;

      /* E.g. when verifying corrupted repositories, their changed path
         lists may contain an ADD for "/".  The delta path driver will
         call us with a NULL parent in that case. */
      if (*edit_path == 0)
        return svn_error_create(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                _("Root directory already exists."));

      /* Was this node copied? */
      SVN_ERR(fill_copyfrom(&copyfrom_root, &copyfrom_path, &copyfrom_rev,
                            &src_readable, root, change,
                            cb->authz_read_func, cb->authz_read_baton,
                            edit_path, pool, pool));

      /* If we have a copyfrom path, and we can't read it or we're just
         ignoring it, or the copyfrom rev is prior to the low water mark
         then we just null them out and do a raw add with no history at
         all. */
      if (copyfrom_path
          && ((! src_readable)
              || (svn_relpath_skip_ancestor(base_path, copyfrom_path + 1) == NULL)
              || (cb->low_water_mark > copyfrom_rev)))
        {
          copyfrom_path = NULL;
          copyfrom_rev = SVN_INVALID_REVNUM;
        }

      /* Do the right thing based on the path KIND. */
      if (change->node_kind == svn_node_dir)
        {
          /* If this is a copy, but we can't represent it as such,
             then we just do a recursive add of the source path
             contents. */
          if (change->copyfrom_path && ! copyfrom_path)
            {
              SVN_ERR(add_subdir(copyfrom_root, root, editor, edit_baton,
                                 edit_path, parent_baton, change->copyfrom_path,
                                 cb->authz_read_func, cb->authz_read_baton,
                                 cb->changed_paths, pool, dir_baton));
            }
          else
            {
              SVN_ERR(editor->add_directory(edit_path, parent_baton,
                                            copyfrom_path, copyfrom_rev,
                                            pool, dir_baton));
            }
        }
      else
        {
          SVN_ERR(editor->add_file(edit_path, parent_baton, copyfrom_path,
                                   copyfrom_rev, pool, &file_baton));
        }

      /* If we represent this as a copy... */
      if (copyfrom_path)
        {
          /* If it is a directory, make sure descendants get the correct
             delta source by remembering that we are operating inside a
             (possibly nested) copy operation. */
          if (change->node_kind == svn_node_dir)
            {
              struct copy_info *info = apr_pcalloc(cb->pool, sizeof(*info));

              info->path = apr_pstrdup(cb->pool, edit_path);
              info->copyfrom_path = apr_pstrdup(cb->pool, copyfrom_path);
              info->copyfrom_rev = copyfrom_rev;

              APR_ARRAY_PUSH(cb->copies, struct copy_info *) = info;
            }

          /* Save the source so that we can use it later, when we
             need to generate text and prop deltas. */
          source_root = copyfrom_root;
          source_fspath = copyfrom_path;
        }
      else
        /* Else, we are an add without history... */
        {
          /* If an ancestor is added with history, we need to forget about
             that here, go on with life and repeat all the mistakes of our
             past... */
          if (change->node_kind == svn_node_dir && cb->copies->nelts > 0)
            {
              struct copy_info *info = apr_pcalloc(cb->pool, sizeof(*info));

              info->path = apr_pstrdup(cb->pool, edit_path);
              info->copyfrom_path = NULL;
              info->copyfrom_rev = SVN_INVALID_REVNUM;

              APR_ARRAY_PUSH(cb->copies, struct copy_info *) = info;
            }
          source_root = NULL;
          source_fspath = NULL;
        }
    }
  else if (! do_delete)
    {
      /* Do the right thing based on the path KIND (and the presence
         of a PARENT_BATON). */
      if (change->node_kind == svn_node_dir)
        {
          if (parent_baton)
            {
              SVN_ERR(editor->open_directory(edit_path, parent_baton,
                                             SVN_INVALID_REVNUM,
                                             pool, dir_baton));
            }
          else
            {
              SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                                        pool, dir_baton));
            }
        }
      else
        {
          SVN_ERR(editor->open_file(edit_path, parent_baton, SVN_INVALID_REVNUM,
                                    pool, &file_baton));
        }
      /* If we are inside an add with history, we need to adjust the
         delta source. */
      if (cb->copies->nelts > 0)
        {
          struct copy_info *info = APR_ARRAY_IDX(cb->copies,
                                                 cb->copies->nelts - 1,
                                                 struct copy_info *);
          if (info->copyfrom_path)
            {
              const char *relpath = svn_relpath_skip_ancestor(info->path,
                                                              edit_path);
              SVN_ERR_ASSERT(relpath && *relpath);
              SVN_ERR(svn_fs_revision_root(&source_root,
                                           svn_fs_root_fs(root),
                                           info->copyfrom_rev, pool));
              source_fspath = svn_fspath__join(info->copyfrom_path,
                                               relpath, pool);
            }
          else
            {
              /* This is an add without history, nested inside an
                 add with history.  We have no delta source in this case. */
              source_root = NULL;
              source_fspath = NULL;
            }
        }
    }

  if (! do_delete || do_add)
    {
      /* Is this a copy that was downgraded to a raw add?  (If so,
         we'll need to transmit properties and file contents and such
         for it regardless of what the CHANGE structure's text_mod
         and prop_mod flags say.)  */
      svn_boolean_t downgraded_copy = (change->copyfrom_known
                                       && change->copyfrom_path
                                       && (! copyfrom_path));

      /* Handle property modifications. */
      if (change->prop_mod || downgraded_copy)
        {
          if (cb->compare_root)
            {
              apr_array_header_t *prop_diffs;
              apr_hash_t *old_props;
              apr_hash_t *new_props;
              int i;

              if (source_root)
                SVN_ERR(svn_fs_node_proplist(&old_props, source_root,
                                             source_fspath, pool));
              else
                old_props = apr_hash_make(pool);

              SVN_ERR(svn_fs_node_proplist(&new_props, root, edit_path, pool));

              SVN_ERR(svn_prop_diffs(&prop_diffs, new_props, old_props,
                                     pool));

              for (i = 0; i < prop_diffs->nelts; ++i)
                {
                  svn_prop_t *pc = &APR_ARRAY_IDX(prop_diffs, i, svn_prop_t);
                   if (change->node_kind == svn_node_dir)
                     SVN_ERR(editor->change_dir_prop(*dir_baton, pc->name,
                                                     pc->value, pool));
                   else if (change->node_kind == svn_node_file)
                     SVN_ERR(editor->change_file_prop(file_baton, pc->name,
                                                      pc->value, pool));
                }
            }
          else
            {
              /* Just do a dummy prop change to signal that there are *any*
                 propmods. */
              if (change->node_kind == svn_node_dir)
                SVN_ERR(editor->change_dir_prop(*dir_baton, "", NULL,
                                                pool));
              else if (change->node_kind == svn_node_file)
                SVN_ERR(editor->change_file_prop(file_baton, "", NULL,
                                                 pool));
            }
        }

      /* Handle textual modifications. */
      if (change->node_kind == svn_node_file
          && (change->text_mod || downgraded_copy))
        {
          svn_txdelta_window_handler_t delta_handler;
          void *delta_handler_baton;
          const char *hex_digest = NULL;

          if (cb->compare_root && source_root && source_fspath)
            {
              svn_checksum_t *checksum;
              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                           source_root, source_fspath, TRUE,
                                           pool));
              hex_digest = svn_checksum_to_cstring(checksum, pool);
            }

          SVN_ERR(editor->apply_textdelta(file_baton, hex_digest, pool,
                                          &delta_handler,
                                          &delta_handler_baton));
          if (cb->compare_root)
            {
              svn_txdelta_stream_t *delta_stream;

              SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream, source_root,
                                                   source_fspath, root,
                                                   edit_path, pool));
              SVN_ERR(svn_txdelta_send_txstream(delta_stream, delta_handler,
                                                delta_handler_baton, pool));
            }
          else
            SVN_ERR(delta_handler(NULL, delta_handler_baton));
        }
    }

  /* Close the file baton if we opened it. */
  if (file_baton)
    {
      svn_checksum_t *checksum;
      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5, root, edit_path,
                                   TRUE, pool));
      SVN_ERR(editor->close_file(file_baton,
                                 svn_checksum_to_cstring(checksum, pool),
                                 pool));
    }

  return SVN_NO_ERROR;
}

#ifdef USE_EV2_IMPL
static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  svn_fs_root_t *root = baton;
  svn_fs_root_t *prev_root;
  svn_fs_t *fs = svn_fs_root_fs(root);

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = svn_fs_revision_root_revision(root) - 1;

  SVN_ERR(svn_fs_revision_root(&prev_root, fs, base_revision, scratch_pool));
  SVN_ERR(svn_fs_check_path(kind, prev_root, path, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_fs_root_t *root = baton;
  svn_fs_root_t *prev_root;
  svn_fs_t *fs = svn_fs_root_fs(root);

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = svn_fs_revision_root_revision(root) - 1;

  SVN_ERR(svn_fs_revision_root(&prev_root, fs, base_revision, scratch_pool));
  SVN_ERR(svn_fs_node_proplist(props, prev_root, path, result_pool));

  return SVN_NO_ERROR;
}
#endif




/* Retrieve the path changes under ROOT, filter them with AUTHZ_READ_FUNC
   and AUTHZ_READ_BATON and return those that intersect with BASE_RELPATH.

   The svn_fs_path_change3_t* will be returned in *CHANGED_PATHS, keyed by
   their path.  The paths themselves are additionally returned in *PATHS.

   Allocate the returned data in RESULT_POOL and use SCRATCH_POOL for
   temporary allocations.
 */
static svn_error_t *
get_relevant_changes(apr_hash_t **changed_paths,
                     apr_array_header_t **paths,
                     svn_fs_root_t *root,
                     const char *base_relpath,
                     svn_repos_authz_func_t authz_read_func,
                     void *authz_read_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_fs_path_change_iterator_t *iterator;
  svn_fs_path_change3_t *change;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Fetch the paths changed under ROOT. */
  SVN_ERR(svn_fs_paths_changed3(&iterator, root, scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_path_change_get(&change, iterator));

  /* Make an array from the keys of our CHANGED_PATHS hash, and copy
     the values into a new hash whose keys have no leading slashes. */
  *paths = apr_array_make(result_pool, 16, sizeof(const char *));
  *changed_paths = apr_hash_make(result_pool);
  while (change)
    {
      const char *path = change->path.data;
      apr_ssize_t keylen = change->path.len;
      svn_boolean_t allowed = TRUE;

      svn_pool_clear(iterpool);
      if (authz_read_func)
        SVN_ERR(authz_read_func(&allowed, root, path, authz_read_baton,
                                iterpool));

      if (allowed)
        {
          if (path[0] == '/')
            {
              path++;
              keylen--;
            }

          /* If the base_path doesn't match the top directory of this path
             we don't want anything to do with it... 
             ...unless this was a change to one of the parent directories of
             base_path. */
          if (   svn_relpath_skip_ancestor(base_relpath, path)
              || svn_relpath_skip_ancestor(path, base_relpath))
            {
              change = svn_fs_path_change3_dup(change, result_pool);
              path = change->path.data;
              if (path[0] == '/')
                path++;

              APR_ARRAY_PUSH(*paths, const char *) = path;
              apr_hash_set(*changed_paths, path, keylen, change);
            }
        }

      SVN_ERR(svn_fs_path_change_get(&change, iterator));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_replay2(svn_fs_root_t *root,
                  const char *base_path,
                  svn_revnum_t low_water_mark,
                  svn_boolean_t send_deltas,
                  const svn_delta_editor_t *editor,
                  void *edit_baton,
                  svn_repos_authz_func_t authz_read_func,
                  void *authz_read_baton,
                  apr_pool_t *pool)
{
#ifndef USE_EV2_IMPL
  apr_hash_t *changed_paths;
  apr_array_header_t *paths;
  struct path_driver_cb_baton cb_baton;

  /* Special-case r0, which we know is an empty revision; if we don't
     special-case it we might end up trying to compare it to "r-1". */
  if (svn_fs_is_revision_root(root) && svn_fs_revision_root_revision(root) == 0)
    {
      SVN_ERR(editor->set_target_revision(edit_baton, 0, pool));
      return SVN_NO_ERROR;
    }

  if (! base_path)
    base_path = "";
  else if (base_path[0] == '/')
    ++base_path;

  /* Fetch the paths changed under ROOT. */
  SVN_ERR(get_relevant_changes(&changed_paths, &paths, root, base_path,
                               authz_read_func, authz_read_baton,
                               pool, pool));

  /* If we were not given a low water mark, assume that everything is there,
     all the way back to revision 0. */
  if (! SVN_IS_VALID_REVNUM(low_water_mark))
    low_water_mark = 0;

  /* Initialize our callback baton. */
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.root = root;
  cb_baton.changed_paths = changed_paths;
  cb_baton.authz_read_func = authz_read_func;
  cb_baton.authz_read_baton = authz_read_baton;
  cb_baton.base_path = base_path;
  cb_baton.low_water_mark = low_water_mark;
  cb_baton.compare_root = NULL;

  if (send_deltas)
    {
      SVN_ERR(svn_fs_revision_root(&cb_baton.compare_root,
                                   svn_fs_root_fs(root),
                                   svn_fs_is_revision_root(root)
                                     ? svn_fs_revision_root_revision(root) - 1
                                     : svn_fs_txn_root_base_revision(root),
                                   pool));
    }

  cb_baton.copies = apr_array_make(pool, 4, sizeof(struct copy_info *));
  cb_baton.pool = pool;

  /* Determine the revision to use throughout the edit, and call
     EDITOR's set_target_revision() function.  */
  if (svn_fs_is_revision_root(root))
    {
      svn_revnum_t revision = svn_fs_revision_root_revision(root);
      SVN_ERR(editor->set_target_revision(edit_baton, revision, pool));
    }

  /* Call the path-based editor driver. */
  return svn_delta_path_driver2(editor, edit_baton,
                                paths, TRUE,
                                path_driver_cb_func, &cb_baton, pool);
#else
  svn_editor_t *editorv2;
  struct svn_delta__extra_baton *exb;
  svn_delta__unlock_func_t unlock_func;
  svn_boolean_t send_abs_paths;
  const char *repos_root = "";
  void *unlock_baton;

  /* If we were not given a low water mark, assume that everything is there,
     all the way back to revision 0. */
  if (! SVN_IS_VALID_REVNUM(low_water_mark))
    low_water_mark = 0;

  /* Special-case r0, which we know is an empty revision; if we don't
     special-case it we might end up trying to compare it to "r-1". */
  if (svn_fs_is_revision_root(root)
        && svn_fs_revision_root_revision(root) == 0)
    {
      SVN_ERR(editor->set_target_revision(edit_baton, 0, pool));
      return SVN_NO_ERROR;
    }

  /* Determine the revision to use throughout the edit, and call
     EDITOR's set_target_revision() function.  */
  if (svn_fs_is_revision_root(root))
    {
      svn_revnum_t revision = svn_fs_revision_root_revision(root);
      SVN_ERR(editor->set_target_revision(edit_baton, revision, pool));
    }

  if (! base_path)
    base_path = "";
  else if (base_path[0] == '/')
    ++base_path;

  /* Use the shim to convert our editor to an Ev2 editor, and pass it down
     the stack. */
  SVN_ERR(svn_delta__editor_from_delta(&editorv2, &exb,
                                       &unlock_func, &unlock_baton,
                                       editor, edit_baton,
                                       &send_abs_paths,
                                       repos_root, "",
                                       NULL, NULL,
                                       fetch_kind_func, root,
                                       fetch_props_func, root,
                                       pool, pool));

  /* Tell the shim that we're starting the process. */
  SVN_ERR(exb->start_edit(exb->baton, svn_fs_revision_root_revision(root)));

  /* ### We're ignoring SEND_DELTAS here. */
  SVN_ERR(svn_repos__replay_ev2(root, base_path, low_water_mark,
                                editorv2, authz_read_func, authz_read_baton,
                                pool));

  return SVN_NO_ERROR;
#endif
}


/*****************************************************************
 *                      Ev2 Implementation                       *
 *****************************************************************/

/* Recursively traverse EDIT_PATH (as it exists under SOURCE_ROOT) emitting
   the appropriate editor calls to add it and its children without any
   history.  This is meant to be used when either a subset of the tree
   has been ignored and we need to copy something from that subset to
   the part of the tree we do care about, or if a subset of the tree is
   unavailable because of authz and we need to use it as the source of
   a copy. */
static svn_error_t *
add_subdir_ev2(svn_fs_root_t *source_root,
               svn_fs_root_t *target_root,
               svn_editor_t *editor,
               const char *repos_relpath,
               const char *source_fspath,
               svn_repos_authz_func_t authz_read_func,
               void *authz_read_baton,
               apr_hash_t *changed_paths,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  apr_hash_t *dirents;
  apr_hash_t *props = NULL;
  apr_array_header_t *children = NULL;

  SVN_ERR(svn_fs_node_proplist(&props, target_root, repos_relpath,
                               scratch_pool));

  SVN_ERR(svn_editor_add_directory(editor, repos_relpath, children,
                                   props, SVN_INVALID_REVNUM));

  /* We have to get the dirents from the source path, not the target,
     because we want nested copies from *readable* paths to be handled by
     path_driver_cb_func, not add_subdir (in order to preserve history). */
  SVN_ERR(svn_fs_dir_entries(&dirents, source_root, source_fspath,
                             scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      svn_fs_path_change3_t *change;
      svn_boolean_t readable = TRUE;
      svn_fs_dirent_t *dent = apr_hash_this_val(hi);
      const char *copyfrom_path = NULL;
      svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;
      const char *child_relpath;

      svn_pool_clear(iterpool);

      child_relpath = svn_relpath_join(repos_relpath, dent->name, iterpool);

      /* If a file or subdirectory of the copied directory is listed as a
         changed path (because it was modified after the copy but before the
         commit), we remove it from the changed_paths hash so that future
         calls to path_driver_cb_func will ignore it. */
      change = svn_hash_gets(changed_paths, child_relpath);
      if (change)
        {
          svn_hash_sets(changed_paths, child_relpath, NULL);

          /* If it's a delete, skip this entry. */
          if (change->change_kind == svn_fs_path_change_delete)
            continue;

          /* If it's a replacement, check for copyfrom info (if we
             don't have it already. */
          if (change->change_kind == svn_fs_path_change_replace)
            {
              if (! change->copyfrom_known)
                {
                  SVN_ERR(svn_fs_copied_from(&change->copyfrom_rev,
                                             &change->copyfrom_path,
                                             target_root, child_relpath,
                                             result_pool));
                  change->copyfrom_known = TRUE;
                }
              copyfrom_path = change->copyfrom_path;
              copyfrom_rev = change->copyfrom_rev;
            }
        }

      if (authz_read_func)
        SVN_ERR(authz_read_func(&readable, target_root, child_relpath,
                                authz_read_baton, iterpool));

      if (! readable)
        continue;

      if (dent->kind == svn_node_dir)
        {
          svn_fs_root_t *new_source_root;
          const char *new_source_fspath;

          if (copyfrom_path)
            {
              svn_fs_t *fs = svn_fs_root_fs(source_root);
              SVN_ERR(svn_fs_revision_root(&new_source_root, fs,
                                           copyfrom_rev, result_pool));
              new_source_fspath = copyfrom_path;
            }
          else
            {
              new_source_root = source_root;
              new_source_fspath = svn_fspath__join(source_fspath, dent->name,
                                                   iterpool);
            }

          /* ### authz considerations?
           *
           * I think not; when path_driver_cb_func() calls add_subdir(), it
           * passes SOURCE_ROOT and SOURCE_FSPATH that are unreadable.
           */
          if (change && change->change_kind == svn_fs_path_change_replace
              && copyfrom_path == NULL)
            {
              SVN_ERR(svn_editor_add_directory(editor, child_relpath,
                                               children, props,
                                               SVN_INVALID_REVNUM));
            }
          else
            {
              SVN_ERR(add_subdir_ev2(new_source_root, target_root,
                                     editor, child_relpath,
                                     new_source_fspath,
                                     authz_read_func, authz_read_baton,
                                     changed_paths, result_pool, iterpool));
            }
        }
      else if (dent->kind == svn_node_file)
        {
          svn_checksum_t *checksum;
          svn_stream_t *contents;

          SVN_ERR(svn_fs_node_proplist(&props, target_root,
                                       child_relpath, iterpool));

          SVN_ERR(svn_fs_file_contents(&contents, target_root,
                                       child_relpath, iterpool));

          SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1,
                                       target_root,
                                       child_relpath, TRUE, iterpool));

          SVN_ERR(svn_editor_add_file(editor, child_relpath, checksum,
                                      contents, props, SVN_INVALID_REVNUM));
        }
      else
        SVN_ERR_MALFUNCTION();
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
replay_node(svn_fs_root_t *root,
            const char *repos_relpath,
            svn_editor_t *editor,
            svn_revnum_t low_water_mark,
            const char *base_repos_relpath,
            apr_array_header_t *copies,
            apr_hash_t *changed_paths,
            svn_repos_authz_func_t authz_read_func,
            void *authz_read_baton,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_fs_path_change3_t *change;
  svn_boolean_t do_add = FALSE;
  svn_boolean_t do_delete = FALSE;
  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;
  svn_revnum_t replaces_rev;

  /* First, flush the copies stack so it only contains ancestors of path. */
  while (copies->nelts > 0
         && (svn_relpath_skip_ancestor(APR_ARRAY_IDX(copies,
                                                    copies->nelts - 1,
                                                     struct copy_info *)->path,
                                       repos_relpath) == NULL) )
    apr_array_pop(copies);

  change = svn_hash_gets(changed_paths, repos_relpath);
  if (! change)
    {
      /* This can only happen if the path was removed from changed_paths
         by an earlier call to add_subdir, which means the path was already
         handled and we should simply ignore it. */
      return SVN_NO_ERROR;
    }
  switch (change->change_kind)
    {
    case svn_fs_path_change_add:
      do_add = TRUE;
      break;

    case svn_fs_path_change_delete:
      do_delete = TRUE;
      break;

    case svn_fs_path_change_replace:
      do_add = TRUE;
      do_delete = TRUE;
      break;

    case svn_fs_path_change_modify:
    default:
      /* do nothing */
      break;
    }

  /* Handle any deletions. */
  if (do_delete && ! do_add)
    {
      svn_boolean_t readable;

      /* Issue #4121: delete under under a copy, of a path that was unreadable
         at its pre-copy location. */
      SVN_ERR(was_readable(&readable, root, repos_relpath, copies,
                            authz_read_func, authz_read_baton,
                            scratch_pool, scratch_pool));
      if (readable)
        SVN_ERR(svn_editor_delete(editor, repos_relpath, SVN_INVALID_REVNUM));

      return SVN_NO_ERROR;
    }

  /* Handle replacements. */
  if (do_delete && do_add)
    replaces_rev = svn_fs_revision_root_revision(root);
  else
    replaces_rev = SVN_INVALID_REVNUM;

  /* Fetch the node kind if it makes sense to do so. */
  if (! do_delete || do_add)
    {
      if (change->node_kind == svn_node_unknown)
        SVN_ERR(svn_fs_check_path(&(change->node_kind), root, repos_relpath,
                                  scratch_pool));
      if ((change->node_kind != svn_node_dir) &&
          (change->node_kind != svn_node_file))
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Filesystem path '%s' is neither a file "
                                   "nor a directory"), repos_relpath);
    }

  /* Handle any adds/opens. */
  if (do_add)
    {
      svn_boolean_t src_readable;
      svn_fs_root_t *copyfrom_root;

      /* Was this node copied? */
      SVN_ERR(fill_copyfrom(&copyfrom_root, &copyfrom_path, &copyfrom_rev,
                            &src_readable, root, change,
                            authz_read_func, authz_read_baton,
                            repos_relpath, scratch_pool, scratch_pool));

      /* If we have a copyfrom path, and we can't read it or we're just
         ignoring it, or the copyfrom rev is prior to the low water mark
         then we just null them out and do a raw add with no history at
         all. */
      if (copyfrom_path
          && ((! src_readable)
              || (svn_relpath_skip_ancestor(base_repos_relpath,
                                            copyfrom_path + 1) == NULL)
              || (low_water_mark > copyfrom_rev)))
        {
          copyfrom_path = NULL;
          copyfrom_rev = SVN_INVALID_REVNUM;
        }

      /* Do the right thing based on the path KIND. */
      if (change->node_kind == svn_node_dir)
        {
          /* If this is a copy, but we can't represent it as such,
             then we just do a recursive add of the source path
             contents. */
          if (change->copyfrom_path && ! copyfrom_path)
            {
              SVN_ERR(add_subdir_ev2(copyfrom_root, root, editor,
                                     repos_relpath, change->copyfrom_path,
                                     authz_read_func, authz_read_baton,
                                     changed_paths, result_pool,
                                     scratch_pool));
            }
          else
            {
              if (copyfrom_path)
                {
                  if (copyfrom_path[0] == '/')
                    ++copyfrom_path;
                  SVN_ERR(svn_editor_copy(editor, copyfrom_path, copyfrom_rev,
                                          repos_relpath, replaces_rev));
                }
              else
                {
                  apr_array_header_t *children;
                  apr_hash_t *props;
                  apr_hash_t *dirents;

                  SVN_ERR(svn_fs_dir_entries(&dirents, root, repos_relpath,
                                             scratch_pool));
                  SVN_ERR(svn_hash_keys(&children, dirents, scratch_pool));

                  SVN_ERR(svn_fs_node_proplist(&props, root, repos_relpath,
                                               scratch_pool));

                  SVN_ERR(svn_editor_add_directory(editor, repos_relpath,
                                                   children, props,
                                                   replaces_rev));
                }
            }
        }
      else
        {
          if (copyfrom_path)
            {
              if (copyfrom_path[0] == '/')
                ++copyfrom_path;
              SVN_ERR(svn_editor_copy(editor, copyfrom_path, copyfrom_rev,
                                      repos_relpath, replaces_rev));
            }
          else
            {
              apr_hash_t *props;
              svn_checksum_t *checksum;
              svn_stream_t *contents;

              SVN_ERR(svn_fs_node_proplist(&props, root, repos_relpath,
                                           scratch_pool));

              SVN_ERR(svn_fs_file_contents(&contents, root, repos_relpath,
                                           scratch_pool));

              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1, root,
                                           repos_relpath, TRUE, scratch_pool));

              SVN_ERR(svn_editor_add_file(editor, repos_relpath, checksum,
                                          contents, props, replaces_rev));
            }
        }

      /* If we represent this as a copy... */
      if (copyfrom_path)
        {
          /* If it is a directory, make sure descendants get the correct
             delta source by remembering that we are operating inside a
             (possibly nested) copy operation. */
          if (change->node_kind == svn_node_dir)
            {
              struct copy_info *info = apr_pcalloc(result_pool, sizeof(*info));

              info->path = apr_pstrdup(result_pool, repos_relpath);
              info->copyfrom_path = apr_pstrdup(result_pool, copyfrom_path);
              info->copyfrom_rev = copyfrom_rev;

              APR_ARRAY_PUSH(copies, struct copy_info *) = info;
            }
        }
      else
        /* Else, we are an add without history... */
        {
          /* If an ancestor is added with history, we need to forget about
             that here, go on with life and repeat all the mistakes of our
             past... */
          if (change->node_kind == svn_node_dir && copies->nelts > 0)
            {
              struct copy_info *info = apr_pcalloc(result_pool, sizeof(*info));

              info->path = apr_pstrdup(result_pool, repos_relpath);
              info->copyfrom_path = NULL;
              info->copyfrom_rev = SVN_INVALID_REVNUM;

              APR_ARRAY_PUSH(copies, struct copy_info *) = info;
            }
        }
    }
  else if (! do_delete)
    {
      /* If we are inside an add with history, we need to adjust the
         delta source. */
      if (copies->nelts > 0)
        {
          struct copy_info *info = APR_ARRAY_IDX(copies,
                                                 copies->nelts - 1,
                                                 struct copy_info *);
          if (info->copyfrom_path)
            {
              const char *relpath = svn_relpath_skip_ancestor(info->path,
                                                              repos_relpath);
              SVN_ERR_ASSERT(relpath && *relpath);
              repos_relpath = svn_relpath_join(info->copyfrom_path,
                                               relpath, scratch_pool);
            }
        }
    }

  if (! do_delete && !do_add)
    {
      apr_hash_t *props = NULL;

      /* Is this a copy that was downgraded to a raw add?  (If so,
         we'll need to transmit properties and file contents and such
         for it regardless of what the CHANGE structure's text_mod
         and prop_mod flags say.)  */
      svn_boolean_t downgraded_copy = (change->copyfrom_known
                                       && change->copyfrom_path
                                       && (! copyfrom_path));

      /* Handle property modifications. */
      if (change->prop_mod || downgraded_copy)
        {
          SVN_ERR(svn_fs_node_proplist(&props, root, repos_relpath,
                                       scratch_pool));
        }

      /* Handle textual modifications. */
      if (change->node_kind == svn_node_file
          && (change->text_mod || change->prop_mod || downgraded_copy))
        {
          svn_checksum_t *checksum = NULL;
          svn_stream_t *contents = NULL;

          if (change->text_mod)
            {
              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1,
                                           root, repos_relpath, TRUE,
                                           scratch_pool));

              SVN_ERR(svn_fs_file_contents(&contents, root, repos_relpath,
                                           scratch_pool));
            }

          SVN_ERR(svn_editor_alter_file(editor, repos_relpath,
                                        SVN_INVALID_REVNUM,
                                        checksum, contents, props));
        }

      if (change->node_kind == svn_node_dir
          && (change->prop_mod || downgraded_copy))
        {
          apr_array_header_t *children = NULL;

          SVN_ERR(svn_editor_alter_directory(editor, repos_relpath,
                                             SVN_INVALID_REVNUM, children,
                                             props));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__replay_ev2(svn_fs_root_t *root,
                      const char *base_repos_relpath,
                      svn_revnum_t low_water_mark,
                      svn_editor_t *editor,
                      svn_repos_authz_func_t authz_read_func,
                      void *authz_read_baton,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *changed_paths;
  apr_array_header_t *paths;
  apr_array_header_t *copies;
  apr_pool_t *iterpool;
  svn_error_t *err = SVN_NO_ERROR;
  int i;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(base_repos_relpath));

  /* Special-case r0, which we know is an empty revision; if we don't
     special-case it we might end up trying to compare it to "r-1". */
  if (svn_fs_is_revision_root(root)
        && svn_fs_revision_root_revision(root) == 0)
    {
      return SVN_NO_ERROR;
    }

  /* Fetch the paths changed under ROOT. */
  SVN_ERR(get_relevant_changes(&changed_paths, &paths, root,
                               base_repos_relpath,
                               authz_read_func, authz_read_baton,
                               scratch_pool, scratch_pool));

  /* If we were not given a low water mark, assume that everything is there,
     all the way back to revision 0. */
  if (! SVN_IS_VALID_REVNUM(low_water_mark))
    low_water_mark = 0;

  copies = apr_array_make(scratch_pool, 4, sizeof(struct copy_info *));

  /* Sort the paths.  Although not strictly required by the API, this has
     the pleasant side effect of maintaining a consistent ordering of
     dumpfile contents. */
  svn_sort__array(paths, svn_sort_compare_paths);

  /* Now actually handle the various paths. */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < paths->nelts; i++)
    {
      const char *repos_relpath = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(iterpool);
      err = replay_node(root, repos_relpath, editor, low_water_mark,
                        base_repos_relpath, copies, changed_paths,
                        authz_read_func, authz_read_baton,
                        scratch_pool, iterpool);
      if (err)
        break;
    }

  if (err)
    return svn_error_compose_create(err, svn_editor_abort(editor));
  else
    SVN_ERR(svn_editor_complete(editor));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}
