/*
 * delta.c:   an editor driver for expressing differences between two trees
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

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_checksum.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_private_config.h"
#include "repos.h"



/* THINGS TODO:  Currently the code herein gives only a slight nod to
   fully supporting directory deltas that involve renames, copies, and
   such.  */


/* Some datatypes and declarations used throughout the file.  */


/* Parameters which remain constant throughout a delta traversal.
   At the top of the recursion, we initialize one of these structures.
   Then we pass it down to every call.  This way, functions invoked
   deep in the recursion can get access to this traversal's global
   parameters, without using global variables.  */
struct context {
  const svn_delta_editor_t *editor;
  const char *edit_base_path;
  svn_fs_root_t *source_root;
  svn_fs_root_t *target_root;
  svn_repos_authz_func_t authz_read_func;
  void *authz_read_baton;
  svn_boolean_t text_deltas;
  svn_boolean_t entry_props;
  svn_boolean_t ignore_ancestry;
};


/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted.  */
typedef svn_error_t *proplist_change_fn_t(struct context *c,
                                          void *object,
                                          const char *name,
                                          const svn_string_t *value,
                                          apr_pool_t *pool);



/* Some prototypes for functions used throughout.  See each individual
   function for information about what it does.  */


/* Retrieving the base revision from the path/revision hash.  */
static svn_revnum_t get_path_revision(svn_fs_root_t *root,
                                      const char *path,
                                      apr_pool_t *pool);


/* proplist_change_fn_t property changing functions.  */
static svn_error_t *change_dir_prop(struct context *c,
                                    void *object,
                                    const char *path,
                                    const svn_string_t *value,
                                    apr_pool_t *pool);

static svn_error_t *change_file_prop(struct context *c,
                                     void *object,
                                     const char *path,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);


/* Constructing deltas for properties of files and directories.  */
static svn_error_t *delta_proplists(struct context *c,
                                    const char *source_path,
                                    const char *target_path,
                                    proplist_change_fn_t *change_fn,
                                    void *object,
                                    apr_pool_t *pool);


/* Constructing deltas for file constents.  */
static svn_error_t *send_text_delta(struct context *c,
                                    void *file_baton,
                                    const char *base_checksum,
                                    svn_txdelta_stream_t *delta_stream,
                                    apr_pool_t *pool);

static svn_error_t *delta_files(struct context *c,
                                void *file_baton,
                                const char *source_path,
                                const char *target_path,
                                apr_pool_t *pool);


/* Generic directory deltafication routines.  */
static svn_error_t *delete(struct context *c,
                           void *dir_baton,
                           const char *edit_path,
                           apr_pool_t *pool);

static svn_error_t *add_file_or_dir(struct context *c,
                                    void *dir_baton,
                                    svn_depth_t depth,
                                    const char *target_path,
                                    const char *edit_path,
                                    svn_node_kind_t tgt_kind,
                                    apr_pool_t *pool);

static svn_error_t *replace_file_or_dir(struct context *c,
                                        void *dir_baton,
                                        svn_depth_t depth,
                                        const char *source_path,
                                        const char *target_path,
                                        const char *edit_path,
                                        svn_node_kind_t tgt_kind,
                                        apr_pool_t *pool);

static svn_error_t *absent_file_or_dir(struct context *c,
                                       void *dir_baton,
                                       const char *edit_path,
                                       svn_node_kind_t tgt_kind,
                                       apr_pool_t *pool);

static svn_error_t *delta_dirs(struct context *c,
                               void *dir_baton,
                               svn_depth_t depth,
                               const char *source_path,
                               const char *target_path,
                               const char *edit_path,
                               apr_pool_t *pool);



#define MAYBE_DEMOTE_DEPTH(depth)                                  \
  (((depth) == svn_depth_immediates || (depth) == svn_depth_files) \
   ? svn_depth_empty                                               \
   : (depth))


/* Return the error 'SVN_ERR_AUTHZ_ROOT_UNREADABLE' if PATH in ROOT is
 * unreadable according to AUTHZ_READ_FUNC with AUTHZ_READ_BATON.
 *
 * PATH should be the implicit root path of an editor drive, that is,
 * the path used by editor->open_root().
 */
static svn_error_t *
authz_root_check(svn_fs_root_t *root,
                 const char *path,
                 svn_repos_authz_func_t authz_read_func,
                 void *authz_read_baton,
                 apr_pool_t *pool)
{
  svn_boolean_t allowed;

  if (authz_read_func)
    {
      SVN_ERR(authz_read_func(&allowed, root, path, authz_read_baton, pool));

      if (! allowed)
        return svn_error_create(SVN_ERR_AUTHZ_ROOT_UNREADABLE, 0,
                                _("Unable to open root of edit"));
    }

  return SVN_NO_ERROR;
}


/* Public interface to computing directory deltas.  */
svn_error_t *
svn_repos_dir_delta2(svn_fs_root_t *src_root,
                     const char *src_parent_dir,
                     const char *src_entry,
                     svn_fs_root_t *tgt_root,
                     const char *tgt_fullpath,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     svn_repos_authz_func_t authz_read_func,
                     void *authz_read_baton,
                     svn_boolean_t text_deltas,
                     svn_depth_t depth,
                     svn_boolean_t entry_props,
                     svn_boolean_t ignore_ancestry,
                     apr_pool_t *pool)
{
  void *root_baton = NULL;
  struct context c;
  const char *src_fullpath;
  svn_node_kind_t src_kind, tgt_kind;
  svn_revnum_t rootrev;
  svn_fs_node_relation_t relation;
  const char *authz_root_path;

  /* SRC_PARENT_DIR must be valid. */
  if (src_parent_dir)
    src_parent_dir = svn_relpath_canonicalize(src_parent_dir, pool);
  else
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, 0,
                            "Invalid source parent directory '(null)'");

  /* TGT_FULLPATH must be valid. */
  if (tgt_fullpath)
    tgt_fullpath = svn_relpath_canonicalize(tgt_fullpath, pool);
  else
    return svn_error_create(SVN_ERR_FS_PATH_SYNTAX, 0,
                            _("Invalid target path"));

  if (depth == svn_depth_exclude)
    return svn_error_create(SVN_ERR_REPOS_BAD_ARGS, NULL,
                            _("Delta depth 'exclude' not supported"));

  /* Calculate the fs path implicitly used for editor->open_root, so
     we can do an authz check on that path first. */
  if (*src_entry)
    authz_root_path = svn_relpath_dirname(tgt_fullpath, pool);
  else
    authz_root_path = tgt_fullpath;

  /* Construct the full path of the source item. */
  src_fullpath = svn_relpath_join(src_parent_dir, src_entry, pool);

  /* Get the node kinds for the source and target paths.  */
  SVN_ERR(svn_fs_check_path(&tgt_kind, tgt_root, tgt_fullpath, pool));
  SVN_ERR(svn_fs_check_path(&src_kind, src_root, src_fullpath, pool));

  /* If neither of our paths exists, we don't really have anything to do. */
  if ((tgt_kind == svn_node_none) && (src_kind == svn_node_none))
    goto cleanup;

  /* If either the source or the target is a non-directory, we
     require that a SRC_ENTRY be supplied. */
  if ((! *src_entry) && ((src_kind != svn_node_dir)
                         || tgt_kind != svn_node_dir))
    return svn_error_create
      (SVN_ERR_FS_PATH_SYNTAX, 0,
       _("Invalid editor anchoring; at least one of the "
         "input paths is not a directory and there was no source entry"));

  /* Don't report / compare stale revprops.  However, revprop changes that
   * are made by a 3rd party outside this delta operation, may not be
   * detected as per our visibility guarantees.  Reset the revprop caches
   * for both roots in case they belong to different svn_fs_t instances. */
  SVN_ERR(svn_fs_refresh_revision_props(svn_fs_root_fs(tgt_root), pool));
  SVN_ERR(svn_fs_refresh_revision_props(svn_fs_root_fs(src_root), pool));

  /* Set the global target revision if one can be determined. */
  if (svn_fs_is_revision_root(tgt_root))
    {
      SVN_ERR(editor->set_target_revision
              (edit_baton, svn_fs_revision_root_revision(tgt_root), pool));
    }
  else if (svn_fs_is_txn_root(tgt_root))
    {
      SVN_ERR(editor->set_target_revision
              (edit_baton, svn_fs_txn_root_base_revision(tgt_root), pool));
    }

  /* Setup our pseudo-global structure here.  We need these variables
     throughout the deltafication process, so pass them around by
     reference to all the helper functions. */
  c.editor = editor;
  c.source_root = src_root;
  c.target_root = tgt_root;
  c.authz_read_func = authz_read_func;
  c.authz_read_baton = authz_read_baton;
  c.text_deltas = text_deltas;
  c.entry_props = entry_props;
  c.ignore_ancestry = ignore_ancestry;

  /* Get our editor root's revision. */
  rootrev = get_path_revision(src_root, src_parent_dir, pool);

  /* If one or the other of our paths doesn't exist, we have to handle
     those cases specially. */
  if (tgt_kind == svn_node_none)
    {
      /* Caller thinks that target still exists, but it doesn't.
         So transform their source path to "nothing" by deleting it. */
      SVN_ERR(authz_root_check(tgt_root, authz_root_path,
                               authz_read_func, authz_read_baton, pool));
      SVN_ERR(editor->open_root(edit_baton, rootrev, pool, &root_baton));
      SVN_ERR(delete(&c, root_baton, src_entry, pool));
      goto cleanup;
    }
  if (src_kind == svn_node_none)
    {
      /* The source path no longer exists, but the target does.
         So transform "nothing" into "something" by adding. */
      SVN_ERR(authz_root_check(tgt_root, authz_root_path,
                               authz_read_func, authz_read_baton, pool));
      SVN_ERR(editor->open_root(edit_baton, rootrev, pool, &root_baton));
      SVN_ERR(add_file_or_dir(&c, root_baton, depth, tgt_fullpath,
                              src_entry, tgt_kind, pool));
      goto cleanup;
    }

  /* Get and compare the node IDs for the source and target. */
  SVN_ERR(svn_fs_node_relation(&relation, tgt_root, tgt_fullpath,
                               src_root, src_fullpath, pool));

  if (relation == svn_fs_node_unchanged)
    {
      /* They are the same node!  No-op (you gotta love those). */
      goto cleanup;
    }
  else if (*src_entry)
    {
      /* If the nodes have different kinds, we must delete the one and
         add the other.  Also, if they are completely unrelated and
         our caller is interested in relatedness, we do the same thing. */
      if ((src_kind != tgt_kind)
          || ((relation == svn_fs_node_unrelated) && (! ignore_ancestry)))
        {
          SVN_ERR(authz_root_check(tgt_root, authz_root_path,
                                   authz_read_func, authz_read_baton, pool));
          SVN_ERR(editor->open_root(edit_baton, rootrev, pool, &root_baton));
          SVN_ERR(delete(&c, root_baton, src_entry, pool));
          SVN_ERR(add_file_or_dir(&c, root_baton, depth, tgt_fullpath,
                                  src_entry, tgt_kind, pool));
        }
      /* Otherwise, we just replace the one with the other. */
      else
        {
          SVN_ERR(authz_root_check(tgt_root, authz_root_path,
                                   authz_read_func, authz_read_baton, pool));
          SVN_ERR(editor->open_root(edit_baton, rootrev, pool, &root_baton));
          SVN_ERR(replace_file_or_dir(&c, root_baton, depth, src_fullpath,
                                      tgt_fullpath, src_entry,
                                      tgt_kind, pool));
        }
    }
  else
    {
      /* There is no entry given, so delta the whole parent directory. */
      SVN_ERR(authz_root_check(tgt_root, authz_root_path,
                               authz_read_func, authz_read_baton, pool));
      SVN_ERR(editor->open_root(edit_baton, rootrev, pool, &root_baton));
      SVN_ERR(delta_dirs(&c, root_baton, depth, src_fullpath,
                         tgt_fullpath, "", pool));
    }

 cleanup:

  /* Make sure we close the root directory if we opened one above. */
  if (root_baton)
    SVN_ERR(editor->close_directory(root_baton, pool));

  /* Close the edit. */
  return editor->close_edit(edit_baton, pool);
}


/* Retrieving the base revision from the path/revision hash.  */


static svn_revnum_t
get_path_revision(svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *pool)
{
  svn_revnum_t revision;
  svn_error_t *err;

  /* Easy out -- if ROOT is a revision root, we can use the revision
     that it's a root of. */
  if (svn_fs_is_revision_root(root))
    return svn_fs_revision_root_revision(root);

  /* Else, this must be a transaction root, so ask the filesystem in
     what revision this path was created. */
  if ((err = svn_fs_node_created_rev(&revision, root, path, pool)))
    {
      revision = SVN_INVALID_REVNUM;
      svn_error_clear(err);
    }

  /* If we don't get back a valid revision, this path is mutable in
     the transaction.  We should probably examine the node on which it
     is based, doable by querying for the node-id of the path, and
     then examining that node-id's predecessor.  ### This predecessor
     determination isn't exposed via the FS public API right now, so
     for now, we'll just return the SVN_INVALID_REVNUM. */
  return revision;
}


/* proplist_change_fn_t property changing functions.  */


/* Call the directory property-setting function of C->editor to set
   the property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_dir_prop(struct context *c,
                void *object,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  return c->editor->change_dir_prop(object, name, value, pool);
}


/* Call the file property-setting function of C->editor to set the
   property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_file_prop(struct context *c,
                 void *object,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  return c->editor->change_file_prop(object, name, value, pool);
}




/* Constructing deltas for properties of files and directories.  */


/* Generate the appropriate property editing calls to turn the
   properties of SOURCE_PATH into those of TARGET_PATH.  If
   SOURCE_PATH is NULL, this is an add, so assume the target starts
   with no properties.  Pass OBJECT on to the editor function wrapper
   CHANGE_FN. */
static svn_error_t *
delta_proplists(struct context *c,
                const char *source_path,
                const char *target_path,
                proplist_change_fn_t *change_fn,
                void *object,
                apr_pool_t *pool)
{
  apr_hash_t *s_props = 0;
  apr_hash_t *t_props = 0;
  apr_pool_t *subpool;
  apr_array_header_t *prop_diffs;
  int i;

  SVN_ERR_ASSERT(target_path);

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create(pool);

  /* If we're supposed to send entry props for all non-deleted items,
     here we go! */
  if (c->entry_props)
    {
      svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
      svn_string_t *cr_str = NULL;
      svn_string_t *committed_date = NULL;
      svn_string_t *last_author = NULL;

      /* Get the CR and two derivative props. ### check for error returns. */
      SVN_ERR(svn_fs_node_created_rev(&committed_rev, c->target_root,
                                      target_path, subpool));
      if (SVN_IS_VALID_REVNUM(committed_rev))
        {
          svn_fs_t *fs = svn_fs_root_fs(c->target_root);
          apr_hash_t *r_props;
          const char *uuid;

          /* Transmit the committed-rev. */
          cr_str = svn_string_createf(subpool, "%ld",
                                      committed_rev);
          SVN_ERR(change_fn(c, object, SVN_PROP_ENTRY_COMMITTED_REV,
                            cr_str, subpool));

          SVN_ERR(svn_fs_revision_proplist2(&r_props, fs, committed_rev,
                                            FALSE, pool, subpool));

          /* Transmit the committed-date. */
          committed_date = svn_hash_gets(r_props, SVN_PROP_REVISION_DATE);
          if (committed_date || source_path)
            {
              SVN_ERR(change_fn(c, object, SVN_PROP_ENTRY_COMMITTED_DATE,
                                committed_date, subpool));
            }

          /* Transmit the last-author. */
          last_author = svn_hash_gets(r_props, SVN_PROP_REVISION_AUTHOR);
          if (last_author || source_path)
            {
              SVN_ERR(change_fn(c, object, SVN_PROP_ENTRY_LAST_AUTHOR,
                                last_author, subpool));
            }

          /* Transmit the UUID. */
          SVN_ERR(svn_fs_get_uuid(fs, &uuid, subpool));
          SVN_ERR(change_fn(c, object, SVN_PROP_ENTRY_UUID,
                            svn_string_create(uuid, subpool),
                            subpool));
        }
    }

  if (source_path)
    {
      svn_boolean_t changed;

      /* Is this deltification worth our time? */
      SVN_ERR(svn_fs_props_different(&changed, c->target_root, target_path,
                                     c->source_root, source_path, subpool));
      if (! changed)
        goto cleanup;

      /* If so, go ahead and get the source path's properties. */
      SVN_ERR(svn_fs_node_proplist(&s_props, c->source_root,
                                   source_path, subpool));
    }
  else
    {
      s_props = apr_hash_make(subpool);
    }

  /* Get the target path's properties */
  SVN_ERR(svn_fs_node_proplist(&t_props, c->target_root,
                               target_path, subpool));

  /* Now transmit the differences. */
  SVN_ERR(svn_prop_diffs(&prop_diffs, t_props, s_props, subpool));
  for (i = 0; i < prop_diffs->nelts; i++)
    {
      const svn_prop_t *pc = &APR_ARRAY_IDX(prop_diffs, i, svn_prop_t);
      SVN_ERR(change_fn(c, object, pc->name, pc->value, subpool));
    }

 cleanup:
  /* Destroy local subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}




/* Constructing deltas for file contents.  */


/* Change the contents of FILE_BATON in C->editor, according to the
   text delta from DELTA_STREAM.  Pass BASE_CHECKSUM along to
   C->editor->apply_textdelta. */
static svn_error_t *
send_text_delta(struct context *c,
                void *file_baton,
                const char *base_checksum,
                svn_txdelta_stream_t *delta_stream,
                apr_pool_t *pool)
{
  svn_txdelta_window_handler_t delta_handler;
  void *delta_handler_baton;

  /* Get a handler that will apply the delta to the file.  */
  SVN_ERR(c->editor->apply_textdelta
          (file_baton, base_checksum, pool,
           &delta_handler, &delta_handler_baton));

  if (c->text_deltas && delta_stream)
    {
      /* Deliver the delta stream to the file.  */
      return svn_txdelta_send_txstream(delta_stream,
                                       delta_handler,
                                       delta_handler_baton,
                                       pool);
    }
  else
    {
      /* The caller doesn't want text delta data.  Just send a single
         NULL window. */
      return delta_handler(NULL, delta_handler_baton);
    }
}

/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those in SOURCE_PATH to those in TARGET_PATH. */
static svn_error_t *
delta_files(struct context *c,
            void *file_baton,
            const char *source_path,
            const char *target_path,
            apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_boolean_t changed = TRUE;

  SVN_ERR_ASSERT(target_path);

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create(pool);

  /* Compare the files' property lists.  */
  SVN_ERR(delta_proplists(c, source_path, target_path,
                          change_file_prop, file_baton, subpool));

  if (source_path)
    {
      SVN_ERR(svn_fs_contents_different(&changed,
                                        c->target_root, target_path,
                                        c->source_root, source_path,
                                        subpool));
    }
  else
    {
      /* If there isn't a source path, this is an add, which
         necessarily has textual mods. */
    }

  /* If there is a change, and the context indicates that we should
     care about it, then hand it off to a delta stream.  */
  if (changed)
    {
      svn_txdelta_stream_t *delta_stream = NULL;
      svn_checksum_t *source_checksum;
      const char *source_hex_digest = NULL;

      if (c->text_deltas)
        {
          /* Get a delta stream turning an empty file into one having
             TARGET_PATH's contents.  */
          SVN_ERR(svn_fs_get_file_delta_stream
                  (&delta_stream,
                   source_path ? c->source_root : NULL,
                   source_path ? source_path : NULL,
                   c->target_root, target_path, subpool));
        }

      if (source_path)
        {
          SVN_ERR(svn_fs_file_checksum(&source_checksum, svn_checksum_md5,
                                       c->source_root, source_path, TRUE,
                                       subpool));

          source_hex_digest = svn_checksum_to_cstring(source_checksum,
                                                      subpool);
        }

      SVN_ERR(send_text_delta(c, file_baton, source_hex_digest,
                              delta_stream, subpool));
    }

  /* Cleanup. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}




/* Generic directory deltafication routines.  */


/* Emit a delta to delete the entry named TARGET_ENTRY from DIR_BATON.  */
static svn_error_t *
delete(struct context *c,
       void *dir_baton,
       const char *edit_path,
       apr_pool_t *pool)
{
  return c->editor->delete_entry(edit_path, SVN_INVALID_REVNUM,
                                 dir_baton, pool);
}


/* If authorized, emit a delta to create the entry named TARGET_ENTRY
   at the location EDIT_PATH.  If not authorized, indicate that
   EDIT_PATH is absent.  Pass DIR_BATON through to editor functions
   that require it.  DEPTH is the depth from this point downward. */
static svn_error_t *
add_file_or_dir(struct context *c, void *dir_baton,
                svn_depth_t depth,
                const char *target_path,
                const char *edit_path,
                svn_node_kind_t tgt_kind,
                apr_pool_t *pool)
{
  struct context *context = c;
  svn_boolean_t allowed;

  SVN_ERR_ASSERT(target_path && edit_path);

  if (c->authz_read_func)
    {
      SVN_ERR(c->authz_read_func(&allowed, c->target_root, target_path,
                                 c->authz_read_baton, pool));
      if (!allowed)
        return absent_file_or_dir(c, dir_baton, edit_path, tgt_kind, pool);
    }

  if (tgt_kind == svn_node_dir)
    {
      void *subdir_baton;

      SVN_ERR(context->editor->add_directory(edit_path, dir_baton, NULL,
                                             SVN_INVALID_REVNUM, pool,
                                             &subdir_baton));
      SVN_ERR(delta_dirs(context, subdir_baton, MAYBE_DEMOTE_DEPTH(depth),
                         NULL, target_path, edit_path, pool));
      return context->editor->close_directory(subdir_baton, pool);
    }
  else
    {
      void *file_baton;
      svn_checksum_t *checksum;

      SVN_ERR(context->editor->add_file(edit_path, dir_baton,
                                        NULL, SVN_INVALID_REVNUM, pool,
                                        &file_baton));
      SVN_ERR(delta_files(context, file_baton, NULL, target_path, pool));
      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                   context->target_root, target_path,
                                   TRUE, pool));
      return context->editor->close_file
             (file_baton, svn_checksum_to_cstring(checksum, pool), pool);
    }
}


/* If authorized, emit a delta to modify EDIT_PATH with the changes
   from SOURCE_PATH to TARGET_PATH.  If not authorized, indicate that
   EDIT_PATH is absent.  Pass DIR_BATON through to editor functions
   that require it.  DEPTH is the depth from this point downward. */
static svn_error_t *
replace_file_or_dir(struct context *c,
                    void *dir_baton,
                    svn_depth_t depth,
                    const char *source_path,
                    const char *target_path,
                    const char *edit_path,
                    svn_node_kind_t tgt_kind,
                    apr_pool_t *pool)
{
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;
  svn_boolean_t allowed;

  SVN_ERR_ASSERT(target_path && source_path && edit_path);

  if (c->authz_read_func)
    {
      SVN_ERR(c->authz_read_func(&allowed, c->target_root, target_path,
                                 c->authz_read_baton, pool));
      if (!allowed)
        return absent_file_or_dir(c, dir_baton, edit_path, tgt_kind, pool);
    }

  /* Get the base revision for the entry from the hash. */
  base_revision = get_path_revision(c->source_root, source_path, pool);

  if (tgt_kind == svn_node_dir)
    {
      void *subdir_baton;

      SVN_ERR(c->editor->open_directory(edit_path, dir_baton,
                                        base_revision, pool,
                                        &subdir_baton));
      SVN_ERR(delta_dirs(c, subdir_baton, MAYBE_DEMOTE_DEPTH(depth),
                         source_path, target_path, edit_path, pool));
      return c->editor->close_directory(subdir_baton, pool);
    }
  else
    {
      void *file_baton;
      svn_checksum_t *checksum;

      SVN_ERR(c->editor->open_file(edit_path, dir_baton, base_revision,
                                   pool, &file_baton));
      SVN_ERR(delta_files(c, file_baton, source_path, target_path, pool));
      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                   c->target_root, target_path, TRUE,
                                   pool));
      return c->editor->close_file
             (file_baton, svn_checksum_to_cstring(checksum, pool), pool);
    }
}


/* In directory DIR_BATON, indicate that EDIT_PATH  (relative to the
   edit root) is absent by invoking C->editor->absent_directory or
   C->editor->absent_file (depending on TGT_KIND). */
static svn_error_t *
absent_file_or_dir(struct context *c,
                   void *dir_baton,
                   const char *edit_path,
                   svn_node_kind_t tgt_kind,
                   apr_pool_t *pool)
{
  SVN_ERR_ASSERT(edit_path);

  if (tgt_kind == svn_node_dir)
    return c->editor->absent_directory(edit_path, dir_baton, pool);
  else
    return c->editor->absent_file(edit_path, dir_baton, pool);
}


/* Emit deltas to turn SOURCE_PATH into TARGET_PATH.  Assume that
   DIR_BATON represents the directory we're constructing to the editor
   in the context C.  */
static svn_error_t *
delta_dirs(struct context *c,
           void *dir_baton,
           svn_depth_t depth,
           const char *source_path,
           const char *target_path,
           const char *edit_path,
           apr_pool_t *pool)
{
  apr_hash_t *s_entries = 0, *t_entries = 0;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  SVN_ERR_ASSERT(target_path);

  /* Compare the property lists.  */
  SVN_ERR(delta_proplists(c, source_path, target_path,
                          change_dir_prop, dir_baton, pool));

  /* Get the list of entries in each of source and target.  */
  SVN_ERR(svn_fs_dir_entries(&t_entries, c->target_root,
                             target_path, pool));
  if (source_path)
    SVN_ERR(svn_fs_dir_entries(&s_entries, c->source_root,
                               source_path, pool));

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create(pool);

  /* Loop over the hash of entries in the target, searching for its
     partner in the source.  If we find the matching partner entry,
     use editor calls to replace the one in target with a new version
     if necessary, then remove that entry from the source entries
     hash.  If we can't find a related node in the source, we use
     editor calls to add the entry as a new item in the target.
     Having handled all the entries that exist in target, any entries
     still remaining the source entries hash represent entries that no
     longer exist in target.  Use editor calls to delete those entries
     from the target tree. */
  for (hi = apr_hash_first(pool, t_entries); hi; hi = apr_hash_next(hi))
    {
      const void *key = apr_hash_this_key(hi);
      apr_ssize_t klen = apr_hash_this_key_len(hi);
      const svn_fs_dirent_t *t_entry = apr_hash_this_val(hi);
      const svn_fs_dirent_t *s_entry;
      const char *t_fullpath;
      const char *e_fullpath;
      const char *s_fullpath;
      svn_node_kind_t tgt_kind;

      /* Clear out our subpool for the next iteration... */
      svn_pool_clear(subpool);

      tgt_kind = t_entry->kind;
      t_fullpath = svn_relpath_join(target_path, t_entry->name, subpool);
      e_fullpath = svn_relpath_join(edit_path, t_entry->name, subpool);

      /* Can we find something with the same name in the source
         entries hash? */
      if (s_entries && ((s_entry = apr_hash_get(s_entries, key, klen)) != 0))
        {
          svn_node_kind_t src_kind;

          s_fullpath = svn_relpath_join(source_path, t_entry->name, subpool);
          src_kind = s_entry->kind;

          if (depth == svn_depth_infinity
              || src_kind != svn_node_dir
              || (src_kind == svn_node_dir
                  && depth == svn_depth_immediates))
            {
              /* Use svn_fs_compare_ids() to compare our current
                 source and target ids.

                    0: means they are the same id, and this is a noop.
                   -1: means they are unrelated, so we have to delete the
                       old one and add the new one.
                    1: means the nodes are related through ancestry, so go
                       ahead and do the replace directly.  */
              int distance = svn_fs_compare_ids(s_entry->id, t_entry->id);
              if (distance == 0)
                {
                  /* no-op */
                }
              else if ((src_kind != tgt_kind)
                       || ((distance == -1) && (! c->ignore_ancestry)))
                {
                  SVN_ERR(delete(c, dir_baton, e_fullpath, subpool));
                  SVN_ERR(add_file_or_dir(c, dir_baton,
                                          MAYBE_DEMOTE_DEPTH(depth),
                                          t_fullpath, e_fullpath, tgt_kind,
                                          subpool));
                }
              else
                {
                  SVN_ERR(replace_file_or_dir(c, dir_baton,
                                              MAYBE_DEMOTE_DEPTH(depth),
                                              s_fullpath, t_fullpath,
                                              e_fullpath, tgt_kind,
                                              subpool));
                }
            }

          /*  Remove the entry from the source_hash. */
          svn_hash_sets(s_entries, key, NULL);
        }
      else
        {
          if (depth == svn_depth_infinity
              || tgt_kind != svn_node_dir
              || (tgt_kind == svn_node_dir
                  && depth == svn_depth_immediates))
            {
              SVN_ERR(add_file_or_dir(c, dir_baton,
                                      MAYBE_DEMOTE_DEPTH(depth),
                                      t_fullpath, e_fullpath, tgt_kind,
                                      subpool));
            }
        }
    }

  /* All that is left in the source entries hash are things that need
     to be deleted.  Delete them.  */
  if (s_entries)
    {
      for (hi = apr_hash_first(pool, s_entries); hi; hi = apr_hash_next(hi))
        {
          const svn_fs_dirent_t *s_entry = apr_hash_this_val(hi);
          const char *e_fullpath;
          svn_node_kind_t src_kind;

          /* Clear out our subpool for the next iteration... */
          svn_pool_clear(subpool);

          src_kind = s_entry->kind;
          e_fullpath = svn_relpath_join(edit_path, s_entry->name, subpool);

          /* Do we actually want to delete the dir if we're non-recursive? */
          if (depth == svn_depth_infinity
              || src_kind != svn_node_dir
              || (src_kind == svn_node_dir
                  && depth == svn_depth_immediates))
            {
              SVN_ERR(delete(c, dir_baton, e_fullpath, subpool));
            }
        }
    }

  /* Destroy local allocation subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
