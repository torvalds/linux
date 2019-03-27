/*
 * debug_editor.c :  An editor that writes the operations it does to stderr.
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

#include "svn_io.h"

#include "private/svn_delta_private.h"

struct edit_baton
{
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;

  int indent_level;

  svn_stream_t *out;
  const char *prefix;
};

struct dir_baton
{
  void *edit_baton;
  void *wrapped_dir_baton;
};

struct file_baton
{
  void *edit_baton;
  void *wrapped_file_baton;
};

static svn_error_t *
write_indent(struct edit_baton *eb, apr_pool_t *pool)
{
  int i;

  SVN_ERR(svn_stream_puts(eb->out, eb->prefix));
  for (i = 0; i < eb->indent_level; ++i)
    SVN_ERR(svn_stream_puts(eb->out, " "));

  return SVN_NO_ERROR;
}

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "set_target_revision : %ld\n",
                            target_revision));

  return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                 target_revision,
                                                 pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *dir_baton = apr_palloc(pool, sizeof(*dir_baton));

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "open_root : %ld\n",
                            base_revision));
  eb->indent_level++;

  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                        base_revision,
                                        pool,
                                        &dir_baton->wrapped_dir_baton));

  dir_baton->edit_baton = edit_baton;

  *root_baton = dir_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "delete_entry : %s:%ld\n",
                            path, base_revision));

  return eb->wrapped_editor->delete_entry(path,
                                          base_revision,
                                          pb->wrapped_dir_baton,
                                          pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool,
                            "add_directory : '%s' [from '%s':%ld]\n",
                            path, copyfrom_path, copyfrom_revision));
  eb->indent_level++;

  SVN_ERR(eb->wrapped_editor->add_directory(path,
                                            pb->wrapped_dir_baton,
                                            copyfrom_path,
                                            copyfrom_revision,
                                            pool,
                                            &b->wrapped_dir_baton));

  b->edit_baton = eb;
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
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *db = apr_palloc(pool, sizeof(*db));

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "open_directory : '%s':%ld\n",
                            path, base_revision));
  eb->indent_level++;

  SVN_ERR(eb->wrapped_editor->open_directory(path,
                                             pb->wrapped_dir_baton,
                                             base_revision,
                                             pool,
                                             &db->wrapped_dir_baton));

  db->edit_baton = eb;
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool,
                            "add_file : '%s' [from '%s':%ld]\n",
                            path, copyfrom_path, copyfrom_revision));

  eb->indent_level++;

  SVN_ERR(eb->wrapped_editor->add_file(path,
                                       pb->wrapped_dir_baton,
                                       copyfrom_path,
                                       copyfrom_revision,
                                       pool,
                                       &fb->wrapped_file_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "open_file : '%s':%ld\n",
                            path, base_revision));

  eb->indent_level++;

  SVN_ERR(eb->wrapped_editor->open_file(path,
                                        pb->wrapped_dir_baton,
                                        base_revision,
                                        pool,
                                        &fb->wrapped_file_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "apply_textdelta : %s\n",
                            base_checksum));

  SVN_ERR(eb->wrapped_editor->apply_textdelta(fb->wrapped_file_baton,
                                              base_checksum,
                                              pool,
                                              handler,
                                              handler_baton));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  eb->indent_level--;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "close_file : %s\n",
                            text_checksum));

  SVN_ERR(eb->wrapped_editor->close_file(fb->wrapped_file_baton,
                                         text_checksum, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_file(const char *path,
            void *file_baton,
            apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "absent_file : %s\n", path));

  SVN_ERR(eb->wrapped_editor->absent_file(path, fb->wrapped_file_baton,
                                          pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  eb->indent_level--;
  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "close_directory\n"));

  SVN_ERR(eb->wrapped_editor->close_directory(db->wrapped_dir_baton,
                                              pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_directory(const char *path,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "absent_directory : %s\n",
                            path));

  SVN_ERR(eb->wrapped_editor->absent_directory(path, db->wrapped_dir_baton,
                                               pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "change_file_prop : %s -> %s\n",
                            name, value ? value->data : "<deleted>"));

  SVN_ERR(eb->wrapped_editor->change_file_prop(fb->wrapped_file_baton,
                                               name,
                                               value,
                                               pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "change_dir_prop : %s -> %s\n",
                            name, value ? value->data : "<deleted>"));

  SVN_ERR(eb->wrapped_editor->change_dir_prop(db->wrapped_dir_baton,
                                              name,
                                              value,
                                              pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "close_edit\n"));

  SVN_ERR(eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR(write_indent(eb, pool));
  SVN_ERR(svn_stream_printf(eb->out, pool, "abort_edit\n"));

  SVN_ERR(eb->wrapped_editor->abort_edit(eb->wrapped_edit_baton, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta__get_debug_editor(const svn_delta_editor_t **editor,
                            void **edit_baton,
                            const svn_delta_editor_t *wrapped_editor,
                            void *wrapped_edit_baton,
                            const char *prefix,
                            apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = apr_palloc(pool, sizeof(*tree_editor));
  struct edit_baton *eb = apr_palloc(pool, sizeof(*eb));
  apr_file_t *errfp;
  svn_stream_t *out;

  apr_status_t apr_err = apr_file_open_stdout(&errfp, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Problem opening stderr");

  out = svn_stream_from_aprfile2(errfp, TRUE, pool);

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;
  tree_editor->abort_edit = abort_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->out = out;
  eb->indent_level = 0;
  /* This is DBG_FLAG from ../libsvn_subr/debug.c */
  eb->prefix = apr_pstrcat(pool, "DBG: ", prefix, SVN_VA_NULL);

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}
