/*
 * path_driver.c -- drive an editor across a set of paths
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
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "private/svn_fspath.h"
#include "private/svn_sorts_private.h"


/*** Helper functions. ***/

typedef struct dir_stack_t
{
  void *dir_baton;   /* the dir baton. */
  apr_pool_t *pool;  /* the pool associated with the dir baton. */

} dir_stack_t;


/* Call EDITOR's open_directory() function with the PATH argument, then
 * add the resulting dir baton to the dir baton stack.
 */
static svn_error_t *
open_dir(apr_array_header_t *db_stack,
         const svn_delta_editor_t *editor,
         const char *path,
         apr_pool_t *pool)
{
  void *parent_db, *db;
  dir_stack_t *item;
  apr_pool_t *subpool;

  /* Assert that we are in a stable state. */
  SVN_ERR_ASSERT(db_stack && db_stack->nelts);

  /* Get the parent dir baton. */
  item = APR_ARRAY_IDX(db_stack, db_stack->nelts - 1, void *);
  parent_db = item->dir_baton;

  /* Call the EDITOR's open_directory function to get a new directory
     baton. */
  subpool = svn_pool_create(pool);
  SVN_ERR(editor->open_directory(path, parent_db, SVN_INVALID_REVNUM, subpool,
                                 &db));

  /* Now add the dir baton to the stack. */
  item = apr_pcalloc(subpool, sizeof(*item));
  item->dir_baton = db;
  item->pool = subpool;
  APR_ARRAY_PUSH(db_stack, dir_stack_t *) = item;

  return SVN_NO_ERROR;
}


/* Pop a directory from the dir baton stack and update the stack
 * pointer.
 *
 * This function calls the EDITOR's close_directory() function.
 */
static svn_error_t *
pop_stack(apr_array_header_t *db_stack,
          const svn_delta_editor_t *editor)
{
  dir_stack_t *item;

  /* Assert that we are in a stable state. */
  SVN_ERR_ASSERT(db_stack && db_stack->nelts);

  /* Close the most recent directory pushed to the stack. */
  item = APR_ARRAY_IDX(db_stack, db_stack->nelts - 1, dir_stack_t *);
  (void) apr_array_pop(db_stack);
  SVN_ERR(editor->close_directory(item->dir_baton, item->pool));
  svn_pool_destroy(item->pool);

  return SVN_NO_ERROR;
}


/* Count the number of path components in PATH. */
static int
count_components(const char *path)
{
  int count = 1;
  const char *instance = path;

  if ((strlen(path) == 1) && (path[0] == '/'))
    return 0;

  do
    {
      instance++;
      instance = strchr(instance, '/');
      if (instance)
        count++;
    }
  while (instance);

  return count;
}



/*** Public interfaces ***/
svn_error_t *
svn_delta_path_driver2(const svn_delta_editor_t *editor,
                       void *edit_baton,
                       const apr_array_header_t *paths,
                       svn_boolean_t sort_paths,
                       svn_delta_path_driver_cb_func_t callback_func,
                       void *callback_baton,
                       apr_pool_t *pool)
{
  apr_array_header_t *db_stack = apr_array_make(pool, 4, sizeof(void *));
  const char *last_path = NULL;
  int i = 0;
  void *parent_db = NULL, *db = NULL;
  const char *path;
  apr_pool_t *subpool, *iterpool;
  dir_stack_t *item;

  /* Do nothing if there are no paths. */
  if (! paths->nelts)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  iterpool = svn_pool_create(pool);

  /* sort paths if necessary */
  if (sort_paths && paths->nelts > 1)
    {
      apr_array_header_t *sorted = apr_array_copy(subpool, paths);
      svn_sort__array(sorted, svn_sort_compare_paths);
      paths = sorted;
    }

  item = apr_pcalloc(subpool, sizeof(*item));

  /* If the root of the edit is also a target path, we want to call
     the callback function to let the user open the root directory and
     do what needs to be done.  Otherwise, we'll do the open_root()
     ourselves. */
  path = APR_ARRAY_IDX(paths, 0, const char *);
  if (svn_path_is_empty(path))
    {
      SVN_ERR(callback_func(&db, NULL, callback_baton, path, subpool));
      last_path = path;
      i++;
    }
  else
    {
      SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM, subpool, &db));
    }
  item->pool = subpool;
  item->dir_baton = db;
  APR_ARRAY_PUSH(db_stack, void *) = item;

  /* Now, loop over the commit items, traversing the URL tree and
     driving the editor. */
  for (; i < paths->nelts; i++)
    {
      const char *pdir;
      const char *common = "";
      size_t common_len;

      /* Clear the iteration pool. */
      svn_pool_clear(iterpool);

      /* Get the next path. */
      path = APR_ARRAY_IDX(paths, i, const char *);

      /*** Step A - Find the common ancestor of the last path and the
           current one.  For the first iteration, this is just the
           empty string. ***/
      if (i > 0)
        common = (last_path[0] == '/')
          ? svn_fspath__get_longest_ancestor(last_path, path, iterpool)
          : svn_relpath_get_longest_ancestor(last_path, path, iterpool);
      common_len = strlen(common);

      /*** Step B - Close any directories between the last path and
           the new common ancestor, if any need to be closed.
           Sometimes there is nothing to do here (like, for the first
           iteration, or when the last path was an ancestor of the
           current one). ***/
      if ((i > 0) && (strlen(last_path) > common_len))
        {
          const char *rel = last_path + (common_len ? (common_len + 1) : 0);
          int count = count_components(rel);
          while (count--)
            {
              SVN_ERR(pop_stack(db_stack, editor));
            }
        }

      /*** Step C - Open any directories between the common ancestor
           and the parent of the current path. ***/
      if (*path == '/')
        pdir = svn_fspath__dirname(path, iterpool);
      else
        pdir = svn_relpath_dirname(path, iterpool);

      if (strlen(pdir) > common_len)
        {
          const char *piece = pdir + common_len + 1;

          while (1)
            {
              const char *rel = pdir;

              /* Find the first separator. */
              piece = strchr(piece, '/');

              /* Calculate REL as the portion of PDIR up to (but not
                 including) the location to which PIECE is pointing. */
              if (piece)
                rel = apr_pstrmemdup(iterpool, pdir, piece - pdir);

              /* Open the subdirectory. */
              SVN_ERR(open_dir(db_stack, editor, rel, pool));

              /* If we found a '/', advance our PIECE pointer to
                 character just after that '/'.  Otherwise, we're
                 done.  */
              if (piece)
                piece++;
              else
                break;
            }
        }

      /*** Step D - Tell our caller to handle the current path. ***/
      item = APR_ARRAY_IDX(db_stack, db_stack->nelts - 1, void *);
      parent_db = item->dir_baton;
      subpool = svn_pool_create(pool);
      SVN_ERR(callback_func(&db, parent_db, callback_baton, path, subpool));
      if (db)
        {
          item = apr_pcalloc(subpool, sizeof(*item));
          item->dir_baton = db;
          item->pool = subpool;
          APR_ARRAY_PUSH(db_stack, void *) = item;
        }
      else
        {
          svn_pool_destroy(subpool);
        }

      /*** Step E - Save our state for the next iteration.  If our
           caller opened or added PATH as a directory, that becomes
           our LAST_PATH.  Otherwise, we use PATH's parent
           directory. ***/

      /* NOTE:  The variable LAST_PATH needs to outlive the loop. */
      if (db)
        last_path = path; /* lives in a pool outside our control. */
      else
        last_path = apr_pstrdup(pool, pdir); /* duping into POOL. */
    }

  /* Destroy the iteration subpool. */
  svn_pool_destroy(iterpool);

  /* Close down any remaining open directory batons. */
  while (db_stack->nelts)
    {
      SVN_ERR(pop_stack(db_stack, editor));
    }

  return SVN_NO_ERROR;
}
