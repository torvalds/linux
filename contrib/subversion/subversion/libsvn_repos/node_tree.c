/*
 * node_tree.c:  an editor for tracking repository deltas changes
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




#include <stdio.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"
#include "svn_private_config.h"
#include "private/svn_fspath.h"

/*** NOTE: This editor is unique in that it currently is hard-coded to
     be anchored at the root directory of the filesystem.  This
     affords us the ability to use the same paths for filesystem
     locations and editor paths.  ***/



/*** Node creation and assembly structures and routines. ***/
static svn_repos_node_t *
create_node(const char *name,
            svn_repos_node_t *parent,
            apr_pool_t *pool)
{
  svn_repos_node_t *node = apr_pcalloc(pool, sizeof(*node));
  node->action = 'R';
  node->kind = svn_node_unknown;
  node->name = apr_pstrdup(pool, name);
  node->parent = parent;
  return node;
}


static svn_repos_node_t *
create_sibling_node(svn_repos_node_t *elder,
                    const char *name,
                    apr_pool_t *pool)
{
  svn_repos_node_t *tmp_node;

  /* No ELDER sibling?  That's just not gonna work out. */
  if (! elder)
    return NULL;

  /* Run to the end of the list of siblings of ELDER. */
  tmp_node = elder;
  while (tmp_node->sibling)
    tmp_node = tmp_node->sibling;

  /* Create a new youngest sibling and return that. */
  return (tmp_node->sibling = create_node(name, elder->parent, pool));
}


static svn_repos_node_t *
create_child_node(svn_repos_node_t *parent,
                  const char *name,
                  apr_pool_t *pool)
{
  /* No PARENT node?  That's just not gonna work out. */
  if (! parent)
    return NULL;

  /* If PARENT has no children, create its first one and return that. */
  if (! parent->child)
    return (parent->child = create_node(name, parent, pool));

  /* If PARENT already has a child, create a new sibling for its first
     child and return that. */
  return create_sibling_node(parent->child, name, pool);
}


static svn_repos_node_t *
find_child_by_name(svn_repos_node_t *parent,
                   const char *name)
{
  svn_repos_node_t *tmp_node;

  /* No PARENT node, or a barren PARENT?  Nothing to find. */
  if ((! parent) || (! parent->child))
    return NULL;

  /* Look through the children for a node with a matching name. */
  tmp_node = parent->child;
  while (1)
    {
      if (! strcmp(tmp_node->name, name))
        {
          return tmp_node;
        }
      else
        {
          if (tmp_node->sibling)
            tmp_node = tmp_node->sibling;
          else
            break;
        }
    }

  return NULL;
}


static void
find_real_base_location(const char **path_p,
                        svn_revnum_t *rev_p,
                        svn_repos_node_t *node,
                        apr_pool_t *pool)
{
  /* If NODE is an add-with-history, then its real base location is
     the copy source. */
  if ((node->action == 'A')
      && node->copyfrom_path
      && SVN_IS_VALID_REVNUM(node->copyfrom_rev))
    {
      *path_p = node->copyfrom_path;
      *rev_p = node->copyfrom_rev;
      return;
    }

  /* Otherwise, if NODE has a parent, we'll recurse, and add NODE's
     name to whatever the parent's real base path turns out to be (and
     pass the base revision on through). */
  if (node->parent)
    {
      const char *path;
      svn_revnum_t rev;

      find_real_base_location(&path, &rev, node->parent, pool);
      *path_p = svn_fspath__join(path, node->name, pool);
      *rev_p = rev;
      return;
    }

  /* Finally, if the node has no parent, then its name is "/", and it
     has no interesting base revision.  */
  *path_p = "/";
  *rev_p = SVN_INVALID_REVNUM;
  return;
}




/*** Editor functions and batons. ***/

struct edit_baton
{
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_fs_root_t *base_root;
  apr_pool_t *node_pool;
  svn_repos_node_t *node;
};


struct node_baton
{
  struct edit_baton *edit_baton;
  struct node_baton *parent_baton;
  svn_repos_node_t *node;
};


static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct node_baton *d = parent_baton;
  struct edit_baton *eb = d->edit_baton;
  svn_repos_node_t *node;
  const char *name;
  const char *base_path;
  svn_revnum_t base_rev;
  svn_fs_root_t *base_root;
  svn_node_kind_t kind;

  /* Get (or create) the change node and update it. */
  name = svn_relpath_basename(path, pool);
  node = find_child_by_name(d->node, name);
  if (! node)
    node = create_child_node(d->node, name, eb->node_pool);
  node->action = 'D';

  /* We need to look up this node's parents to see what its original
     path in the filesystem was.  Why?  Because if this deletion
     occurred underneath a copied path, the thing that was deleted
     probably lived at a different location (relative to the copy
     source). */
  find_real_base_location(&base_path, &base_rev, node, pool);
  if (! SVN_IS_VALID_REVNUM(base_rev))
    {
      /* No interesting base revision?  We'll just look for the path
         in our base root.  */
      base_root = eb->base_root;
    }
  else
    {
      /* Oh.  Perhaps some copy goodness happened somewhere? */
      SVN_ERR(svn_fs_revision_root(&base_root, eb->fs, base_rev, pool));
    }

  /* Now figure out if this thing was a file or a dir. */
  SVN_ERR(svn_fs_check_path(&kind, base_root, base_path, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("'%s' not found in filesystem"), path);
  node->kind = kind;

  return SVN_NO_ERROR;
}



static svn_error_t *
add_open_helper(const char *path,
                char action,
                svn_node_kind_t kind,
                void *parent_baton,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_rev,
                apr_pool_t *pool,
                void **child_baton)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *nb = apr_pcalloc(pool, sizeof(*nb));

  SVN_ERR_ASSERT(parent_baton && path);

  nb->edit_baton = eb;
  nb->parent_baton = pb;

  /* Create and populate the node. */
  nb->node = create_child_node(pb->node, svn_relpath_basename(path, NULL),
                               eb->node_pool);
  nb->node->kind = kind;
  nb->node->action = action;
  nb->node->copyfrom_rev = copyfrom_rev;
  nb->node->copyfrom_path =
    copyfrom_path ? apr_pstrdup(eb->node_pool, copyfrom_path) : NULL;

  *child_baton = nb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct node_baton *d = apr_pcalloc(pool, sizeof(*d));

  d->edit_baton = eb;
  d->parent_baton = NULL;
  d->node = (eb->node = create_node("", NULL, eb->node_pool));
  d->node->kind = svn_node_dir;
  d->node->action = 'R';
  *root_baton = d;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  return add_open_helper(path, 'R', svn_node_dir, parent_baton,
                         NULL, SVN_INVALID_REVNUM,
                         pool, child_baton);
}


static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  return add_open_helper(path, 'A', svn_node_dir, parent_baton,
                         copyfrom_path, copyfrom_revision,
                         pool, child_baton);
}


static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  return add_open_helper(path, 'R', svn_node_file, parent_baton,
                         NULL, SVN_INVALID_REVNUM,
                         pool, file_baton);
}


static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  return add_open_helper(path, 'A', svn_node_file, parent_baton,
                         copyfrom_path, copyfrom_revision,
                         pool, file_baton);
}


static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct node_baton *fb = file_baton;
  fb->node->text_mod = TRUE;
  *handler = svn_delta_noop_window_handler;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}



static svn_error_t *
change_node_prop(void *node_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct node_baton *nb = node_baton;
  nb->node->prop_mod = TRUE;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_node_editor(const svn_delta_editor_t **editor,
                      void **edit_baton,
                      svn_repos_t *repos,
                      svn_fs_root_t *base_root,
                      svn_fs_root_t *root,
                      apr_pool_t *node_pool,
                      apr_pool_t *pool)
{
  svn_delta_editor_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_default_editor(pool);
  my_editor->open_root           = open_root;
  my_editor->delete_entry        = delete_entry;
  my_editor->add_directory       = add_directory;
  my_editor->open_directory      = open_directory;
  my_editor->add_file            = add_file;
  my_editor->open_file           = open_file;
  my_editor->apply_textdelta     = apply_textdelta;
  my_editor->change_file_prop    = change_node_prop;
  my_editor->change_dir_prop     = change_node_prop;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc(pool, sizeof(*my_edit_baton));
  my_edit_baton->node_pool = node_pool;
  my_edit_baton->fs = repos->fs;
  my_edit_baton->root = root;
  my_edit_baton->base_root = base_root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}



svn_repos_node_t *
svn_repos_node_from_baton(void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  return eb->node;
}
