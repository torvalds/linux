/*
 * depth_filter_editor.c -- provide a svn_delta_editor_t which wraps
 *                          another editor and provides depth-based filtering
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

#include "svn_delta.h"


/*** Batons, and the Toys That Create Them ***/

struct edit_baton
{
  /* The editor/baton we're wrapping. */
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;

  /* The depth to which we are limiting the drive of the wrapped
     editor/baton. */
  svn_depth_t requested_depth;

  /* Does the wrapped editor/baton have an explicit target (in the
     anchor/target sense of the word)? */
  svn_boolean_t has_target;
};

struct node_baton
{
  /* TRUE iff this node was filtered out -- that is, not allowed to
     pass through to the wrapped editor -- by virtue of not appearing
     at a depth in the tree that was "inside" the requested depth.  Of
     course, any children of this node will be deeper still, and so
     will also be filtered out for the same reason. */
  svn_boolean_t filtered;

  /* Pointer to the edit_baton. */
  void *edit_baton;

  /* The real node baton we're wrapping.  May be a directory or file
     baton; we don't care. */
  void *wrapped_baton;

  /* The calculated depth (in terms of counted, stacked, integral
     deepnesses) of this node.  If the node is a directory, this value
     is 1 greater than the value of the same on its parent directory;
     if a file, it is equal to its parent directory's depth value. */
  int dir_depth;
};

/* Allocate and return a new node_baton structure, populated via the
   the input to this helper function. */
static struct node_baton *
make_node_baton(void *edit_baton,
                svn_boolean_t filtered,
                int dir_depth,
                apr_pool_t *pool)
{
  struct node_baton *b = apr_palloc(pool, sizeof(*b));
  b->edit_baton = edit_baton;
  b->wrapped_baton = NULL;
  b->filtered = filtered;
  b->dir_depth = dir_depth;
  return b;
}

/* Return TRUE iff changes to immediate children of the directory
   identified by PB, when those children are of node kind KIND, are
   allowed by the requested depth which this editor is trying to
   preserve.  EB is the edit baton.  */
static svn_boolean_t
okay_to_edit(struct edit_baton *eb,
             struct node_baton *pb,
             svn_node_kind_t kind)
{
  int effective_depth;

  /* If we've already filter out the parent directory, we necessarily
     are filtering out its children, too.  */
  if (pb->filtered)
    return FALSE;

  /* Calculate the effective depth of the parent directory.

     NOTE:  "Depth" in this sense is not the same as the Subversion
     magic depth keywords.  Here, we're talking about a literal,
     integral, stacked depth of directories.

     The root of the edit is generally depth=1, subdirectories thereof
     depth=2, and so on.  But if we have an edit target -- which means
     that the real target of the edit operation isn't the root
     directory, but is instead some immediate child thereof -- we have
     to adjust our calculated effected depth such that the target
     itself is depth=1 (as are its siblings, which we trust aren't
     present in the edit at all), immediate subdirectories thereof are
     depth=2, and so on.
  */
  effective_depth = pb->dir_depth - (eb->has_target ? 1 : 0);
  switch (eb->requested_depth)
    {
    case svn_depth_empty:
      return (effective_depth <= 0);
    case svn_depth_files:
      return ((effective_depth <= 0)
              || (kind == svn_node_file && effective_depth == 1));
    case svn_depth_immediates:
      return (effective_depth <= 1);
    case svn_depth_unknown:
    case svn_depth_exclude:
    case svn_depth_infinity:
      /* Shouldn't reach; see svn_delta_depth_filter_editor() */
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


/*** Editor Functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* Nothing depth-y to filter here. */
 return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct node_baton *b;

  /* The root node always gets through cleanly. */
  b = make_node_baton(edit_baton, FALSE, 1, pool);
  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton, base_revision,
                                        pool, &b->wrapped_baton));

  *root_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* ### FIXME: We don't know the type of the entry, which ordinarily
     doesn't matter, but is a key (*the* key, in fact) distinction
     between depth "files" and depths "immediates".  If the server is
     telling us to delete a subdirectory and our requested depth was
     "immediates", that's fine; if our requested depth was "files",
     though, this deletion shouldn't survive filtering.  For now,
     we'll claim to our helper function that the to-be-deleted thing
     is a file because that's the conservative route to take. */
  if (okay_to_edit(eb, pb, svn_node_file))
    SVN_ERR(eb->wrapped_editor->delete_entry(path, base_revision,
                                             pb->wrapped_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *b = NULL;

  /* Check for sufficient depth. */
  if (okay_to_edit(eb, pb, svn_node_dir))
    {
      b = make_node_baton(eb, FALSE, pb->dir_depth + 1, pool);
      SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_baton,
                                                copyfrom_path,
                                                copyfrom_revision,
                                                pool, &b->wrapped_baton));
    }
  else
    {
      b = make_node_baton(eb, TRUE, pb->dir_depth + 1, pool);
    }

  *child_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *b;

  /* Check for sufficient depth. */
  if (okay_to_edit(eb, pb, svn_node_dir))
    {
      b = make_node_baton(eb, FALSE, pb->dir_depth + 1, pool);
      SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_baton,
                                                 base_revision, pool,
                                                 &b->wrapped_baton));
    }
  else
    {
      b = make_node_baton(eb, TRUE, pb->dir_depth + 1, pool);
    }

  *child_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **child_baton)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *b = NULL;

  /* Check for sufficient depth. */
  if (okay_to_edit(eb, pb, svn_node_file))
    {
      b = make_node_baton(eb, FALSE, pb->dir_depth, pool);
      SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_baton,
                                           copyfrom_path, copyfrom_revision,
                                           pool, &b->wrapped_baton));
    }
  else
    {
      b = make_node_baton(eb, TRUE, pb->dir_depth, pool);
    }

  *child_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **child_baton)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *b;

  /* Check for sufficient depth. */
  if (okay_to_edit(eb, pb, svn_node_file))
    {
      b = make_node_baton(eb, FALSE, pb->dir_depth, pool);
      SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_baton,
                                            base_revision, pool,
                                            &b->wrapped_baton));
    }
  else
    {
      b = make_node_baton(eb, TRUE, pb->dir_depth, pool);
    }

  *child_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct node_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* For filtered files, we just consume the textdelta. */
  if (fb->filtered)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
    }
  else
    {
      SVN_ERR(eb->wrapped_editor->apply_textdelta(fb->wrapped_baton,
                                                  base_checksum, pool,
                                                  handler, handler_baton));
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct node_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* Don't close filtered files. */
  if (! fb->filtered)
    SVN_ERR(eb->wrapped_editor->close_file(fb->wrapped_baton,
                                           text_checksum, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Don't report absent items in filtered directories. */
  if (! pb->filtered)
    SVN_ERR(eb->wrapped_editor->absent_file(path, pb->wrapped_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct node_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  /* Don't close filtered directories. */
  if (! db->filtered)
    SVN_ERR(eb->wrapped_editor->close_directory(db->wrapped_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Don't report absent items in filtered directories. */
  if (! pb->filtered)
    SVN_ERR(eb->wrapped_editor->absent_directory(path, pb->wrapped_baton,
                                                 pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct node_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* No propchanges on filtered files. */
  if (! fb->filtered)
    SVN_ERR(eb->wrapped_editor->change_file_prop(fb->wrapped_baton,
                                                 name, value, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct node_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  /* No propchanges on filtered nodes. */
  if (! db->filtered)
    SVN_ERR(eb->wrapped_editor->change_dir_prop(db->wrapped_baton,
                                                name, value, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

svn_error_t *
svn_delta_depth_filter_editor(const svn_delta_editor_t **editor,
                              void **edit_baton,
                              const svn_delta_editor_t *wrapped_editor,
                              void *wrapped_edit_baton,
                              svn_depth_t requested_depth,
                              svn_boolean_t has_target,
                              apr_pool_t *pool)
{
  svn_delta_editor_t *depth_filter_editor;
  struct edit_baton *eb;

  /* Easy out: if the caller wants infinite depth, there's nothing to
     filter, so just return the editor we were supposed to wrap.  And
     if they've asked for an unknown depth, we can't possibly know
     what that means, so why bother?  */
  if ((requested_depth == svn_depth_unknown)
      || (requested_depth == svn_depth_infinity))
    {
      *editor = wrapped_editor;
      *edit_baton = wrapped_edit_baton;
      return SVN_NO_ERROR;
    }

  depth_filter_editor = svn_delta_default_editor(pool);
  depth_filter_editor->set_target_revision = set_target_revision;
  depth_filter_editor->open_root = open_root;
  depth_filter_editor->delete_entry = delete_entry;
  depth_filter_editor->add_directory = add_directory;
  depth_filter_editor->open_directory = open_directory;
  depth_filter_editor->change_dir_prop = change_dir_prop;
  depth_filter_editor->close_directory = close_directory;
  depth_filter_editor->absent_directory = absent_directory;
  depth_filter_editor->add_file = add_file;
  depth_filter_editor->open_file = open_file;
  depth_filter_editor->apply_textdelta = apply_textdelta;
  depth_filter_editor->change_file_prop = change_file_prop;
  depth_filter_editor->close_file = close_file;
  depth_filter_editor->absent_file = absent_file;
  depth_filter_editor->close_edit = close_edit;

  eb = apr_palloc(pool, sizeof(*eb));
  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->has_target = has_target;
  eb->requested_depth = requested_depth;

  *editor = depth_filter_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}
