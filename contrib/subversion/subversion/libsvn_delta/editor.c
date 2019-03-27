/*
 * editor.c :  editing trees of versioned resources
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
#include "svn_dirent_uri.h"

#include "private/svn_editor.h"

#ifdef SVN_DEBUG
/* This enables runtime checks of the editor API constraints.  This may
   introduce additional memory and runtime overhead, and should not be used
   in production builds.

   ### Remove before release?

   ### Disabled for now.  If I call svn_editor_alter_directory(A) then
       svn_editor_add_file(A/f) the latter fails on SHOULD_ALLOW_ADD.
       If I modify svn_editor_alter_directory to MARK_ALLOW_ADD(child)
       then if I call svn_editor_alter_directory(A) followed by
       svn_editor_alter_directory(A/B/C) the latter fails on
       VERIFY_PARENT_MAY_EXIST. */
#if 0
#define ENABLE_ORDERING_CHECK
#endif
#endif


struct svn_editor_t
{
  void *baton;

  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* Our callback functions match that of the set-many structure, so
     just use that.  */
  svn_editor_cb_many_t funcs;

  /* This pool is used as the scratch_pool for all callbacks.  */
  apr_pool_t *scratch_pool;

#ifdef ENABLE_ORDERING_CHECK
  svn_boolean_t within_callback;

  apr_hash_t *pending_incomplete_children;
  apr_hash_t *completed_nodes;
  svn_boolean_t finished;

  apr_pool_t *state_pool;
#endif
};


#ifdef ENABLE_ORDERING_CHECK

#define START_CALLBACK(editor)                       \
  do {                                               \
    svn_editor_t *editor__tmp_e = (editor);          \
    SVN_ERR_ASSERT(!editor__tmp_e->within_callback); \
    editor__tmp_e->within_callback = TRUE;           \
  } while (0)
#define END_CALLBACK(editor) ((editor)->within_callback = FALSE)

/* Marker to indicate no further changes are allowed on this node.  */
static const int marker_done = 0;
#define MARKER_DONE (&marker_done)

/* Marker indicating that add_* may be called for this path, or that it
   can be the destination of a copy or move. For copy/move, the path
   will switch to MARKER_ALLOW_ALTER, to enable further tweaks.  */
static const int marker_allow_add = 0;
#define MARKER_ALLOW_ADD (&marker_allow_add)

/* Marker indicating that alter_* may be called for this path.  */
static const int marker_allow_alter = 0;
#define MARKER_ALLOW_ALTER (&marker_allow_alter)

/* Just like MARKER_DONE, but also indicates that the node was created
   via add_directory(). This allows us to verify that the CHILDREN param
   was comprehensive.  */
static const int marker_added_dir = 0;
#define MARKER_ADDED_DIR (&marker_added_dir)

#define MARK_FINISHED(editor) ((editor)->finished = TRUE)
#define SHOULD_NOT_BE_FINISHED(editor)  SVN_ERR_ASSERT(!(editor)->finished)

#define CLEAR_INCOMPLETE(editor, relpath) \
  svn_hash_sets((editor)->pending_incomplete_children, relpath, NULL);

#define MARK_RELPATH(editor, relpath, value) \
  svn_hash_sets((editor)->completed_nodes, \
                apr_pstrdup((editor)->state_pool, relpath), value)

#define MARK_COMPLETED(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_DONE)
#define SHOULD_NOT_BE_COMPLETED(editor, relpath) \
  SVN_ERR_ASSERT(svn_hash_gets((editor)->completed_nodes, relpath) == NULL)

#define MARK_ALLOW_ADD(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ALLOW_ADD)
#define SHOULD_ALLOW_ADD(editor, relpath) \
  SVN_ERR_ASSERT(allow_either(editor, relpath, MARKER_ALLOW_ADD, NULL))

#define MARK_ALLOW_ALTER(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ALLOW_ALTER)
#define SHOULD_ALLOW_ALTER(editor, relpath) \
  SVN_ERR_ASSERT(allow_either(editor, relpath, MARKER_ALLOW_ALTER, NULL))

#define MARK_ADDED_DIR(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ADDED_DIR)
#define CHECK_UNKNOWN_CHILD(editor, relpath) \
  SVN_ERR_ASSERT(check_unknown_child(editor, relpath))

/* When a child is changed in some way, mark the parent directory as needing
   to be "stable" (no future structural changes). IOW, only allow "alter" on
   the parent. Prevents parent-add/delete/move after any child operation.  */
#define MARK_PARENT_STABLE(editor, relpath) \
  mark_parent_stable(editor, relpath)

/* If the parent is MARKER_ALLOW_ADD, then it has been moved-away, and we
   know it does not exist. All other cases: it might exist.  */
#define VERIFY_PARENT_MAY_EXIST(editor, relpath) \
  SVN_ERR_ASSERT(svn_hash_gets((editor)->completed_nodes, \
                               svn_relpath_dirname(relpath, \
                                                   (editor)->scratch_pool)) \
                 != MARKER_ALLOW_ADD)

/* If the parent is MARKER_ADDED_DIR, then we should not be deleting
   children(*). If the parent is MARKER_ALLOW_ADD, then it has been
   moved-away, so children cannot exist. That leaves MARKER_DONE,
   MARKER_ALLOW_ALTER, and NULL as possible values. Just assert that
   we didn't get either of the bad ones.

   (*) if the child as added via add_*(), then it would have been marked
   as completed and delete/move-away already test against completed nodes.
   This test is to beware of trying to delete "children" that are not
   actually (and can't possibly be) present.  */
#define CHILD_DELETIONS_ALLOWED(editor, relpath) \
  SVN_ERR_ASSERT(!allow_either(editor, \
                               svn_relpath_dirname(relpath, \
                                                   (editor)->scratch_pool), \
                               MARKER_ADDED_DIR, MARKER_ALLOW_ADD))

static svn_boolean_t
allow_either(const svn_editor_t *editor,
             const char *relpath,
             const void *marker1,
             const void *marker2)
{
  void *value = svn_hash_gets(editor->completed_nodes, relpath);
  return value == marker1 || value == marker2;
}

static svn_boolean_t
check_unknown_child(const svn_editor_t *editor,
                    const char *relpath)
{
  const char *parent;

  /* If we already know about the new child, then exit early.  */
  if (svn_hash_gets(editor->pending_incomplete_children, relpath) != NULL)
    return TRUE;

  parent = svn_relpath_dirname(relpath, editor->scratch_pool);

  /* Was this parent created via svn_editor_add_directory() ?  */
  if (svn_hash_gets(editor->completed_nodes, parent)
      == MARKER_ADDED_DIR)
    {
      /* Whoops. This child should have been listed in that add call,
         and placed into ->pending_incomplete_children.  */
      return FALSE;
    }

  /* The parent was not added in this drive.  */
  return TRUE;
}

static void
mark_parent_stable(const svn_editor_t *editor,
                   const char *relpath)
{
  const char *parent = svn_relpath_dirname(relpath, editor->scratch_pool);
  const void *marker = svn_hash_gets(editor->completed_nodes, parent);

  /* If RELPATH has already been marked (to disallow adds, or that it
     has been fully-completed), then do nothing.  */
  if (marker == MARKER_ALLOW_ALTER
      || marker == MARKER_DONE
      || marker == MARKER_ADDED_DIR)
    return;

  /* If the marker is MARKER_ALLOW_ADD, then that means the parent was
     moved away. There is no way to work on a child. That should have
     been tested before we got here by VERIFY_PARENT_MAY_EXIST().  */
  SVN_ERR_ASSERT_NO_RETURN(marker != MARKER_ALLOW_ADD);

  /* MARKER is NULL. Upgrade it to MARKER_ALLOW_ALTER.  */
  MARK_RELPATH(editor, parent, MARKER_ALLOW_ALTER);
}

#else

/* Be wary with the definition of these macros so that we don't
   end up with "statement with no effect" warnings. Obviously, this
   depends upon particular usage, which is easy to verify.  */

#define START_CALLBACK(editor)  /* empty */
#define END_CALLBACK(editor)  /* empty */

#define MARK_FINISHED(editor)  /* empty */
#define SHOULD_NOT_BE_FINISHED(editor)  /* empty */

#define CLEAR_INCOMPLETE(editor, relpath)  /* empty */

#define MARK_COMPLETED(editor, relpath)  /* empty */
#define SHOULD_NOT_BE_COMPLETED(editor, relpath)  /* empty */

#define MARK_ALLOW_ADD(editor, relpath)  /* empty */
#define SHOULD_ALLOW_ADD(editor, relpath)  /* empty */

#define MARK_ALLOW_ALTER(editor, relpath)  /* empty */
#define SHOULD_ALLOW_ALTER(editor, relpath)  /* empty */

#define MARK_ADDED_DIR(editor, relpath)  /* empty */
#define CHECK_UNKNOWN_CHILD(editor, relpath)  /* empty */

#define MARK_PARENT_STABLE(editor, relpath)  /* empty */
#define VERIFY_PARENT_MAY_EXIST(editor, relpath)  /* empty */
#define CHILD_DELETIONS_ALLOWED(editor, relpath)  /* empty */

#endif /* ENABLE_ORDERING_CHECK */


svn_error_t *
svn_editor_create(svn_editor_t **editor,
                  void *editor_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  *editor = apr_pcalloc(result_pool, sizeof(**editor));

  (*editor)->baton = editor_baton;
  (*editor)->cancel_func = cancel_func;
  (*editor)->cancel_baton = cancel_baton;
  (*editor)->scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  (*editor)->pending_incomplete_children = apr_hash_make(result_pool);
  (*editor)->completed_nodes = apr_hash_make(result_pool);
  (*editor)->finished = FALSE;
  (*editor)->state_pool = result_pool;
#endif

  return SVN_NO_ERROR;
}


void *
svn_editor_get_baton(const svn_editor_t *editor)
{
  return editor->baton;
}


svn_error_t *
svn_editor_setcb_add_directory(svn_editor_t *editor,
                               svn_editor_cb_add_directory_t callback,
                               apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_directory = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_file(svn_editor_t *editor,
                          svn_editor_cb_add_file_t callback,
                          apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_file = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_symlink(svn_editor_t *editor,
                             svn_editor_cb_add_symlink_t callback,
                             apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_symlink = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_absent(svn_editor_t *editor,
                            svn_editor_cb_add_absent_t callback,
                            apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_absent = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_directory(svn_editor_t *editor,
                                 svn_editor_cb_alter_directory_t callback,
                                 apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_directory = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_file(svn_editor_t *editor,
                            svn_editor_cb_alter_file_t callback,
                            apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_file = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_symlink(svn_editor_t *editor,
                               svn_editor_cb_alter_symlink_t callback,
                               apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_symlink = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_delete(svn_editor_t *editor,
                        svn_editor_cb_delete_t callback,
                        apr_pool_t *scratch_pool)
{
  editor->funcs.cb_delete = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_copy(svn_editor_t *editor,
                      svn_editor_cb_copy_t callback,
                      apr_pool_t *scratch_pool)
{
  editor->funcs.cb_copy = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_move(svn_editor_t *editor,
                      svn_editor_cb_move_t callback,
                      apr_pool_t *scratch_pool)
{
  editor->funcs.cb_move = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_complete(svn_editor_t *editor,
                          svn_editor_cb_complete_t callback,
                          apr_pool_t *scratch_pool)
{
  editor->funcs.cb_complete = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_abort(svn_editor_t *editor,
                       svn_editor_cb_abort_t callback,
                       apr_pool_t *scratch_pool)
{
  editor->funcs.cb_abort = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_many(svn_editor_t *editor,
                      const svn_editor_cb_many_t *many,
                      apr_pool_t *scratch_pool)
{
#define COPY_CALLBACK(NAME) if (many->NAME) editor->funcs.NAME = many->NAME

  COPY_CALLBACK(cb_add_directory);
  COPY_CALLBACK(cb_add_file);
  COPY_CALLBACK(cb_add_symlink);
  COPY_CALLBACK(cb_add_absent);
  COPY_CALLBACK(cb_alter_directory);
  COPY_CALLBACK(cb_alter_file);
  COPY_CALLBACK(cb_alter_symlink);
  COPY_CALLBACK(cb_delete);
  COPY_CALLBACK(cb_copy);
  COPY_CALLBACK(cb_move);
  COPY_CALLBACK(cb_complete);
  COPY_CALLBACK(cb_abort);

#undef COPY_CALLBACK

  return SVN_NO_ERROR;
}


static svn_error_t *
check_cancel(svn_editor_t *editor)
{
  svn_error_t *err = NULL;

  if (editor->cancel_func)
    {
      START_CALLBACK(editor);
      err = editor->cancel_func(editor->cancel_baton);
      END_CALLBACK(editor);
    }

  return svn_error_trace(err);
}


svn_error_t *
svn_editor_add_directory(svn_editor_t *editor,
                         const char *relpath,
                         const apr_array_header_t *children,
                         apr_hash_t *props,
                         svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT(children != NULL);
  SVN_ERR_ASSERT(props != NULL);
  /* ### validate children are just basenames?  */
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_add_directory)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_add_directory(editor->baton, relpath, children,
                                           props, replaces_rev,
                                           editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_ADDED_DIR(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

#ifdef ENABLE_ORDERING_CHECK
  {
    int i;
    for (i = 0; i < children->nelts; i++)
      {
        const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
        const char *child = svn_relpath_join(relpath, child_basename,
                                             editor->state_pool);

        svn_hash_sets(editor->pending_incomplete_children, child, "");
      }
  }
#endif

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_add_file(svn_editor_t *editor,
                    const char *relpath,
                    const svn_checksum_t *checksum,
                    svn_stream_t *contents,
                    apr_hash_t *props,
                    svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT(checksum != NULL
                    && checksum->kind == SVN_EDITOR_CHECKSUM_KIND);
  SVN_ERR_ASSERT(contents != NULL);
  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_add_file)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_add_file(editor->baton, relpath,
                                      checksum, contents, props,
                                      replaces_rev, editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_add_symlink(svn_editor_t *editor,
                       const char *relpath,
                       const char *target,
                       apr_hash_t *props,
                       svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_add_symlink)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_add_symlink(editor->baton, relpath, target, props,
                                         replaces_rev, editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_add_absent(svn_editor_t *editor,
                      const char *relpath,
                      svn_node_kind_t kind,
                      svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_add_absent)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_add_absent(editor->baton, relpath, kind,
                                        replaces_rev, editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_alter_directory(svn_editor_t *editor,
                           const char *relpath,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           apr_hash_t *props)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT(children != NULL || props != NULL);
  /* ### validate children are just basenames?  */
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_alter_directory)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_alter_directory(editor->baton,
                                             relpath, revision,
                                             children, props,
                                             editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);

#ifdef ENABLE_ORDERING_CHECK
  /* ### this is not entirely correct. we probably need to adjust the
     ### check_unknown_child() function for this scenario.  */
#if 0
  {
    int i;
    for (i = 0; i < children->nelts; i++)
      {
        const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
        const char *child = svn_relpath_join(relpath, child_basename,
                                             editor->state_pool);

        apr_hash_set(editor->pending_incomplete_children, child,
                     APR_HASH_KEY_STRING, "");
        /* Perhaps MARK_ALLOW_ADD(editor, child); ? */
      }
  }
#endif
#endif

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_alter_file(svn_editor_t *editor,
                      const char *relpath,
                      svn_revnum_t revision,
                      const svn_checksum_t *checksum,
                      svn_stream_t *contents,
                      apr_hash_t *props)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT((checksum != NULL && contents != NULL)
                 || (checksum == NULL && contents == NULL));
  SVN_ERR_ASSERT(props != NULL || checksum != NULL);
  if (checksum)
    SVN_ERR_ASSERT(checksum->kind == SVN_EDITOR_CHECKSUM_KIND);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_alter_file)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_alter_file(editor->baton,
                                        relpath, revision,
                                        checksum, contents, props,
                                        editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_alter_symlink(svn_editor_t *editor,
                         const char *relpath,
                         svn_revnum_t revision,
                         const char *target,
                         apr_hash_t *props)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SVN_ERR_ASSERT(props != NULL || target != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_alter_symlink)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_alter_symlink(editor->baton,
                                           relpath, revision,
                                           target, props,
                                           editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_delete(svn_editor_t *editor,
                  const char *relpath,
                  svn_revnum_t revision)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_NOT_BE_COMPLETED(editor, relpath);
  VERIFY_PARENT_MAY_EXIST(editor, relpath);
  CHILD_DELETIONS_ALLOWED(editor, relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_delete)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_delete(editor->baton, relpath, revision,
                                    editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_COMPLETED(editor, relpath);
  MARK_PARENT_STABLE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_copy(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(src_relpath));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(dst_relpath));
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, dst_relpath);
  VERIFY_PARENT_MAY_EXIST(editor, src_relpath);
  VERIFY_PARENT_MAY_EXIST(editor, dst_relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_copy)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_copy(editor->baton, src_relpath, src_revision,
                                  dst_relpath, replaces_rev,
                                  editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_ALLOW_ALTER(editor, dst_relpath);
  MARK_PARENT_STABLE(editor, dst_relpath);
  CLEAR_INCOMPLETE(editor, dst_relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_move(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_relpath_is_canonical(src_relpath));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(dst_relpath));
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_NOT_BE_COMPLETED(editor, src_relpath);
  SHOULD_ALLOW_ADD(editor, dst_relpath);
  VERIFY_PARENT_MAY_EXIST(editor, src_relpath);
  CHILD_DELETIONS_ALLOWED(editor, src_relpath);
  VERIFY_PARENT_MAY_EXIST(editor, dst_relpath);

  SVN_ERR(check_cancel(editor));

  if (editor->funcs.cb_move)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_move(editor->baton, src_relpath, src_revision,
                                  dst_relpath, replaces_rev,
                                  editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_ALLOW_ADD(editor, src_relpath);
  MARK_PARENT_STABLE(editor, src_relpath);
  MARK_ALLOW_ALTER(editor, dst_relpath);
  MARK_PARENT_STABLE(editor, dst_relpath);
  CLEAR_INCOMPLETE(editor, dst_relpath);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_complete(svn_editor_t *editor)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
#ifdef ENABLE_ORDERING_CHECK
  SVN_ERR_ASSERT(apr_hash_count(editor->pending_incomplete_children) == 0);
#endif

  if (editor->funcs.cb_complete)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_complete(editor->baton, editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_FINISHED(editor);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}


svn_error_t *
svn_editor_abort(svn_editor_t *editor)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);

  if (editor->funcs.cb_abort)
    {
      START_CALLBACK(editor);
      err = editor->funcs.cb_abort(editor->baton, editor->scratch_pool);
      END_CALLBACK(editor);
    }

  MARK_FINISHED(editor);

  svn_pool_clear(editor->scratch_pool);
  return svn_error_trace(err);
}
