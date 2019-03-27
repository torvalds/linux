/*
 * editor.c:  Editor for modifying FS transactions
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_path.h"

#include "svn_private_config.h"

#include "fs-loader.h"

#include "private/svn_fspath.h"
#include "private/svn_fs_private.h"
#include "private/svn_editor.h"


struct edit_baton {
  /* The transaction associated with this editor.  */
  svn_fs_txn_t *txn;

  /* Has this editor been completed?  */
  svn_boolean_t completed;

  /* We sometimes need the cancellation beyond what svn_editor_t provides  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The pool that the txn lives within. When we create a ROOT, it will
     be allocated within a subpool of this. The root will be closed in
     complete/abort and that subpool will be destroyed.

     This pool SHOULD NOT be used for any allocations.  */
  apr_pool_t *txn_pool;

  /* This is the root from the txn. Use get_root() to fetch/create this
     member as appropriate.  */
  svn_fs_root_t *root;
};

#define FSPATH(relpath, pool) apr_pstrcat(pool, "/", relpath, SVN_VA_NULL)

static svn_error_t *
get_root(svn_fs_root_t **root,
         struct edit_baton *eb)
{
  if (eb->root == NULL)
    SVN_ERR(svn_fs_txn_root(&eb->root, eb->txn, eb->txn_pool));
  *root = eb->root;
  return SVN_NO_ERROR;
}


/* Apply each property in PROPS to the node at FSPATH in ROOT.  */
static svn_error_t *
add_new_props(svn_fs_root_t *root,
              const char *fspath,
              apr_hash_t *props,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  /* ### it would be nice to have svn_fs_set_node_props(). but since we
     ### don't... add each property to the node. this is a new node, so
     ### we don't need to worry about deleting props. just adding.  */

  for (hi = apr_hash_first(scratch_pool, props); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      const svn_string_t *value = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_change_node_prop(root, fspath, name, value, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
alter_props(svn_fs_root_t *root,
            const char *fspath,
            apr_hash_t *props,
            apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *old_props;
  apr_array_header_t *propdiffs;
  int i;

  SVN_ERR(svn_fs_node_proplist(&old_props, root, fspath, scratch_pool));

  SVN_ERR(svn_prop_diffs(&propdiffs, props, old_props, scratch_pool));

  for (i = 0; i < propdiffs->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);

      svn_pool_clear(iterpool);

      /* Add, change, or delete properties.  */
      SVN_ERR(svn_fs_change_node_prop(root, fspath, prop->name, prop->value,
                                      iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
set_text(svn_fs_root_t *root,
         const char *fspath,
         const svn_checksum_t *checksum,
         svn_stream_t *contents,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *scratch_pool)
{
  svn_stream_t *fs_contents;

  /* ### We probably don't have an MD5 checksum, so no digest is available
     ### for svn_fs_apply_text() to validate. It would be nice to have an
     ### FS API that takes our CHECKSUM/CONTENTS pair (and PROPS!).  */
  SVN_ERR(svn_fs_apply_text(&fs_contents, root, fspath,
                            NULL /* result_checksum */,
                            scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, fs_contents,
                           cancel_func, cancel_baton,
                           scratch_pool));

  return SVN_NO_ERROR;
}


/* The caller wants to modify REVISION of FSPATH. Is that allowed?  */
static svn_error_t *
can_modify(svn_fs_root_t *txn_root,
           const char *fspath,
           svn_revnum_t revision,
           apr_pool_t *scratch_pool)
{
  svn_revnum_t created_rev;

  /* Out-of-dateness check:  compare the created-rev of the node
     in the txn against the created-rev of FSPATH.  */
  SVN_ERR(svn_fs_node_created_rev(&created_rev, txn_root, fspath,
                                  scratch_pool));

  /* Uncommitted nodes (eg. a descendant of a copy/move destination)
     have no (committed) revision number. Let the caller go ahead and
     modify these nodes.

     Note: strictly speaking, they might be performing an "illegal" edit
     in certain cases, but let's just assume they're Good Little Boys.

     If CREATED_REV is invalid, that means it's already mutable in the
     txn, which means it has already passed this out-of-dateness check.
     (Usually, this happens when looking at a parent directory of an
     already-modified node)  */
  if (!SVN_IS_VALID_REVNUM(created_rev))
    return SVN_NO_ERROR;

  /* If the node is immutable (has a revision), then the caller should
     have supplied a valid revision number [that they expect to change].
     The checks further below will determine the out-of-dateness of the
     specified revision.  */
  /* ### ugh. descendants of copy/move destinations carry along
     ### their original immutable state and (thus) a valid CREATED_REV.
     ### but they are logically uncommitted, so the caller will pass
     ### SVN_INVALID_REVNUM. (technically, the caller could provide
     ### ORIGINAL_REV, but that is semantically incorrect for the Ev2
     ### API).
     ###
     ### for now, we will assume the caller knows what they are doing
     ### and an invalid revision implies such a descendant. in the
     ### future, we could examine the ancestor chain looking for a
     ### copy/move-here node and allow the modification (and the
     ### converse: if no such ancestor, the caller must specify the
     ### correct/intended revision to modify).
  */
#if 1
  if (!SVN_IS_VALID_REVNUM(revision))
    return SVN_NO_ERROR;
#else
  if (!SVN_IS_VALID_REVNUM(revision))
    /* ### use a custom error code?  */
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Revision for modifying '%s' is required"),
                             fspath);
#endif

  if (revision < created_rev)
    {
      /* We asked to change a node that is *older* than what we found
         in the transaction. The client is out of date.  */
      return svn_error_createf(SVN_ERR_FS_OUT_OF_DATE, NULL,
                               _("'%s' is out of date; try updating"),
                               fspath);
    }

  if (revision > created_rev)
    {
      /* We asked to change a node that is *newer* than what we found
         in the transaction. Given that the transaction was based off
         of 'youngest', then either:
         - the caller asked to modify a future node
         - the caller has committed more revisions since this txn
         was constructed, and is asking to modify a node in one
         of those new revisions.
         In either case, the node may not have changed in those new
         revisions; use the node's ID to determine this case.  */
      svn_fs_root_t *rev_root;
      svn_fs_node_relation_t relation;

      /* Get the ID from the future/new revision.  */
      SVN_ERR(svn_fs_revision_root(&rev_root, svn_fs_root_fs(txn_root),
                                   revision, scratch_pool));
      SVN_ERR(svn_fs_node_relation(&relation, txn_root, fspath, rev_root,
                                   fspath, scratch_pool));
      svn_fs_close_root(rev_root);

      /* Has the target node changed in the future?  */
      if (relation != svn_fs_node_unchanged)
        {
          /* Restarting the commit will base the txn on the future/new
             revision, allowing the modification at REVISION.  */
          /* ### use a custom error code  */
          return svn_error_createf(SVN_ERR_FS_CONFLICT, NULL,
                                   _("'%s' has been modified since the "
                                     "commit began (restart the commit)"),
                                   fspath);
        }
    }

  return SVN_NO_ERROR;
}


/* Can we create a node at FSPATH in TXN_ROOT? If something already exists
   at that path, then the client MAY be out of date. We then have to see if
   the path was created/modified in this transaction. IOW, it is new and
   can be replaced without problem.

   Note: the editor protocol disallows double-modifications. This is to
   ensure somebody does not accidentally overwrite another file due to
   being out-of-date.  */
static svn_error_t *
can_create(svn_fs_root_t *txn_root,
           const char *fspath,
           apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *cur_fspath;

  SVN_ERR(svn_fs_check_path(&kind, txn_root, fspath, scratch_pool));
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* ### I'm not sure if this works perfectly. We might have an ancestor
     ### that was modified as a result of a change on a cousin. We might
     ### misinterpret that as a *-here node which brought along this
     ### child. Need to write a test to verify. We may also be able to
     ### test the ancestor to determine if it has been *-here in this
     ### txn, or just a simple modification.  */

  /* Are any of the parents copied/moved-here?  */
  for (cur_fspath = fspath;
       strlen(cur_fspath) > 1;  /* not the root  */
       cur_fspath = svn_fspath__dirname(cur_fspath, scratch_pool))
    {
      svn_revnum_t created_rev;

      SVN_ERR(svn_fs_node_created_rev(&created_rev, txn_root, cur_fspath,
                                      scratch_pool));
      if (!SVN_IS_VALID_REVNUM(created_rev))
        {
          /* The node has no created revision, meaning it is uncommitted.
             Thus, it was created in this transaction, or it has already
             been modified in some way (implying it has already passed a
             modification check.  */
          /* ### verify the node has been *-here ??  */
          return SVN_NO_ERROR;
        }
    }

  return svn_error_createf(SVN_ERR_FS_OUT_OF_DATE, NULL,
                           _("'%s' already exists, so may be out"
                             " of date; try updating"),
                           fspath);
}


/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  /* Note: we ignore CHILDREN. We have no "incomplete" state to worry about,
     so we don't need to be aware of what children will be created.  */

  SVN_ERR(get_root(&root, eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      SVN_ERR(can_modify(root, fspath, replaces_rev, scratch_pool));
      SVN_ERR(svn_fs_delete(root, fspath, scratch_pool));
    }
  else
    {
      SVN_ERR(can_create(root, fspath, scratch_pool));
    }

  SVN_ERR(svn_fs_make_dir(root, fspath, scratch_pool));
  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
            apr_hash_t *props,
            svn_revnum_t replaces_rev,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  SVN_ERR(get_root(&root, eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      SVN_ERR(can_modify(root, fspath, replaces_rev, scratch_pool));
      SVN_ERR(svn_fs_delete(root, fspath, scratch_pool));
    }
  else
    {
      SVN_ERR(can_create(root, fspath, scratch_pool));
    }

  SVN_ERR(svn_fs_make_file(root, fspath, scratch_pool));

  SVN_ERR(set_text(root, fspath, checksum, contents,
                   eb->cancel_func, eb->cancel_baton, scratch_pool));
  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_cb(void *baton,
               const char *relpath,
               const char *target,
               apr_hash_t *props,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  SVN_ERR(get_root(&root, eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      SVN_ERR(can_modify(root, fspath, replaces_rev, scratch_pool));
      SVN_ERR(svn_fs_delete(root, fspath, scratch_pool));
    }
  else
    {
      SVN_ERR(can_create(root, fspath, scratch_pool));
    }

  /* ### we probably need to construct a file with specific contents
     ### (until the FS grows some symlink APIs)  */
#if 0
  SVN_ERR(svn_fs_make_file(root, fspath, scratch_pool));
  SVN_ERR(svn_fs_apply_text(&fs_contents, root, fspath,
                            NULL /* result_checksum */,
                            scratch_pool));
  /* ### SVN_ERR(svn_stream_printf(fs_contents, ..., scratch_pool));  */
  apr_hash_set(props, SVN_PROP_SPECIAL, APR_HASH_KEY_STRING,
               SVN_PROP_SPECIAL_VALUE);

  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));
#endif

  SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_node_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  /* This is a programming error. Code should not attempt to create these
     kinds of nodes within the FS.  */
  /* ### use a custom error code  */
  return svn_error_create(
           SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("The filesystem does not support 'absent' nodes"));
}


/* This implements svn_editor_cb_alter_directory_t */
static svn_error_t *
alter_directory_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   const apr_array_header_t *children,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  /* Note: we ignore CHILDREN. We have no "incomplete" state to worry about,
     so we don't need to be aware of what children will be created.  */

  SVN_ERR(get_root(&root, eb));
  SVN_ERR(can_modify(root, fspath, revision, scratch_pool));

  if (props)
    SVN_ERR(alter_props(root, fspath, props, scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_alter_file_t */
static svn_error_t *
alter_file_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              const svn_checksum_t *checksum,
              svn_stream_t *contents,
              apr_hash_t *props,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  SVN_ERR(get_root(&root, eb));
  SVN_ERR(can_modify(root, fspath, revision, scratch_pool));

  if (contents != NULL)
    {
      SVN_ERR_ASSERT(checksum != NULL);
      SVN_ERR(set_text(root, fspath, checksum, contents,
                       eb->cancel_func, eb->cancel_baton, scratch_pool));
    }

  if (props != NULL)
    {
      SVN_ERR(alter_props(root, fspath, props, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_alter_symlink_t */
static svn_error_t *
alter_symlink_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 const char *target,
                 apr_hash_t *props,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  SVN_UNUSED(eb);
  SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  SVN_ERR(get_root(&root, eb));
  SVN_ERR(can_modify(root, fspath, revision, scratch_pool));

  SVN_ERR(svn_fs_delete(root, fspath, scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *src_fspath = FSPATH(src_relpath, scratch_pool);
  const char *dst_fspath = FSPATH(dst_relpath, scratch_pool);
  svn_fs_root_t *root;
  svn_fs_root_t *src_root;

  SVN_ERR(get_root(&root, eb));

  /* Check if we can we replace the maybe-specified destination (revision).  */
  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      SVN_ERR(can_modify(root, dst_fspath, replaces_rev, scratch_pool));
      SVN_ERR(svn_fs_delete(root, dst_fspath, scratch_pool));
    }
  else
    {
      SVN_ERR(can_create(root, dst_fspath, scratch_pool));
    }

  SVN_ERR(svn_fs_revision_root(&src_root, svn_fs_root_fs(root), src_revision,
                               scratch_pool));
  SVN_ERR(svn_fs_copy(src_root, src_fspath, root, dst_fspath, scratch_pool));
  svn_fs_close_root(src_root);

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  const char *src_fspath = FSPATH(src_relpath, scratch_pool);
  const char *dst_fspath = FSPATH(dst_relpath, scratch_pool);
  svn_fs_root_t *root;
  svn_fs_root_t *src_root;

  SVN_ERR(get_root(&root, eb));

  /* Check if we delete the specified source (revision), and can we replace
     the maybe-specified destination (revision).  */
  SVN_ERR(can_modify(root, src_fspath, src_revision, scratch_pool));
  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      SVN_ERR(can_modify(root, dst_fspath, replaces_rev, scratch_pool));
      SVN_ERR(svn_fs_delete(root, dst_fspath, scratch_pool));
    }
  else
    {
      SVN_ERR(can_create(root, dst_fspath, scratch_pool));
    }

  /* ### would be nice to have svn_fs_move()  */

  /* Copy the src to the dst. */
  SVN_ERR(svn_fs_revision_root(&src_root, svn_fs_root_fs(root), src_revision,
                               scratch_pool));
  SVN_ERR(svn_fs_copy(src_root, src_fspath, root, dst_fspath, scratch_pool));
  svn_fs_close_root(src_root);

  /* Notice: we're deleting the src repos path from the dst root. */
  SVN_ERR(svn_fs_delete(root, src_fspath, scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  /* Watch out for a following call to svn_fs_editor_commit(). Note that
     we are likely here because svn_fs_editor_commit() was called, and it
     invoked svn_editor_complete().  */
  eb->completed = TRUE;

  if (eb->root != NULL)
    {
      svn_fs_close_root(eb->root);
      eb->root = NULL;
    }

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_error_t *err;

  /* Don't allow a following call to svn_fs_editor_commit().  */
  eb->completed = TRUE;

  if (eb->root != NULL)
    {
      svn_fs_close_root(eb->root);
      eb->root = NULL;
    }

  /* ### should we examine the error and attempt svn_fs_purge_txn() ?  */
  err = svn_fs_abort_txn(eb->txn, scratch_pool);

  /* For safety, clear the now-useless txn.  */
  eb->txn = NULL;

  return svn_error_trace(err);
}


static svn_error_t *
make_editor(svn_editor_t **editor,
            svn_fs_txn_t *txn,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  static const svn_editor_cb_many_t editor_cbs = {
    add_directory_cb,
    add_file_cb,
    add_symlink_cb,
    add_absent_cb,
    alter_directory_cb,
    alter_file_cb,
    alter_symlink_cb,
    delete_cb,
    copy_cb,
    move_cb,
    complete_cb,
    abort_cb
  };
  struct edit_baton *eb = apr_pcalloc(result_pool, sizeof(*eb));

  eb->txn = txn;
  eb->cancel_func = cancel_func;
  eb->cancel_baton = cancel_baton;
  eb->txn_pool = result_pool;

  SVN_ERR(svn_editor_create(editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__editor_create(svn_editor_t **editor,
                      const char **txn_name,
                      svn_fs_t *fs,
                      apr_uint32_t flags,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_revnum_t revision;
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_youngest_rev(&revision, fs, scratch_pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, revision, flags, result_pool));
  SVN_ERR(svn_fs_txn_name(txn_name, txn, result_pool));
  return svn_error_trace(make_editor(editor, txn,
                                     cancel_func, cancel_baton,
                                     result_pool, scratch_pool));
}


svn_error_t *
svn_fs__editor_create_for(svn_editor_t **editor,
                          svn_fs_t *fs,
                          const char *txn_name,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, result_pool));
  return svn_error_trace(make_editor(editor, txn,
                                     cancel_func, cancel_baton,
                                     result_pool, scratch_pool));
}


svn_error_t *
svn_fs__editor_commit(svn_revnum_t *revision,
                      svn_error_t **post_commit_err,
                      const char **conflict_path,
                      svn_editor_t *editor,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = svn_editor_get_baton(editor);
  const char *inner_conflict_path;
  svn_error_t *err = NULL;

  /* make sure people are using the correct sequencing.  */
  if (eb->completed)
    return svn_error_create(SVN_ERR_FS_INCORRECT_EDITOR_COMPLETION,
                            NULL, NULL);

  *revision = SVN_INVALID_REVNUM;
  *post_commit_err = NULL;
  *conflict_path = NULL;

  /* Clean up internal resources (eg. eb->root). This also allows the
     editor infrastructure to know this editor is "complete".  */
  err = svn_editor_complete(editor);
  if (err)
    {
      svn_fs_txn_t *txn = eb->txn;

      eb->txn = NULL;
      return svn_error_trace(svn_error_compose_create(
                  err,
                  svn_fs_abort_txn(txn, scratch_pool)));
    }

  /* Note: docco for svn_fs_commit_txn() states that CONFLICT_PATH will
     be allocated in the txn's pool. But it lies. Regardless, we want
     it placed into RESULT_POOL.  */

  err = svn_fs_commit_txn(&inner_conflict_path,
                          revision,
                          eb->txn,
                          scratch_pool);
  if (SVN_IS_VALID_REVNUM(*revision))
    {
      if (err)
        {
          /* Case 3. ERR is a post-commit (cleanup) error.  */

          /* Pass responsibility via POST_COMMIT_ERR.  */
          *post_commit_err = err;
          err = SVN_NO_ERROR;
        }
      /* else: Case 1.  */
    }
  else
    {
      SVN_ERR_ASSERT(err != NULL);
      if (err->apr_err == SVN_ERR_FS_CONFLICT)
        {
          /* Case 2.  */

          /* Copy this into the correct pool (see note above).  */
          *conflict_path = apr_pstrdup(result_pool, inner_conflict_path);

          /* Return success. The caller should inspect CONFLICT_PATH to
             determine this particular case.  */
          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }
      /* else: Case 4.  */

      /* Abort the TXN. Nobody wants to use it.  */
      /* ### should we examine the error and attempt svn_fs_purge_txn() ?  */
      err = svn_error_compose_create(
        err,
        svn_fs_abort_txn(eb->txn, scratch_pool));
    }

  /* For safety, clear the now-useless txn.  */
  eb->txn = NULL;

  return svn_error_trace(err);
}
