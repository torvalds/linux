/*
 * compat.c :  Wrappers and callbacks for compatibility.
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

#include <stddef.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_sorts.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "svn_private_config.h"

#include "private/svn_delta_private.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"


struct file_rev_handler_wrapper_baton {
  void *baton;
  svn_file_rev_handler_old_t handler;
};

/* This implements svn_file_rev_handler_t. */
static svn_error_t *
file_rev_handler_wrapper(void *baton,
                         const char *path,
                         svn_revnum_t rev,
                         apr_hash_t *rev_props,
                         svn_boolean_t result_of_merge,
                         svn_txdelta_window_handler_t *delta_handler,
                         void **delta_baton,
                         apr_array_header_t *prop_diffs,
                         apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = baton;

  if (fwb->handler)
    return fwb->handler(fwb->baton,
                        path,
                        rev,
                        rev_props,
                        delta_handler,
                        delta_baton,
                        prop_diffs,
                        pool);

  return SVN_NO_ERROR;
}

void
svn_compat_wrap_file_rev_handler(svn_file_rev_handler_t *handler2,
                                 void **handler2_baton,
                                 svn_file_rev_handler_old_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = apr_pcalloc(pool, sizeof(*fwb));

  /* Set the user provided old format callback in the baton. */
  fwb->baton = handler_baton;
  fwb->handler = handler;

  *handler2_baton = fwb;
  *handler2 = file_rev_handler_wrapper;
}


/* The following code maps the calls to a traditional delta editor to an
 * Editorv2 editor.  It does this by keeping track of a lot of state, and
 * then communicating that state to Ev2 upon closure of the file or dir (or
 * edit).  Note that Ev2 calls add_symlink() and alter_symlink() are not
 * present in the delta editor paradigm, so we never call them.
 *
 * The general idea here is that we have to see *all* the actions on a node's
 * parent before we can process that node, which means we need to buffer a
 * large amount of information in the dir batons, and then process it in the
 * close_directory() handler.
 *
 * There are a few ways we alter the callback stream.  One is when unlocking
 * paths.  To tell a client a path should be unlocked, the server sends a
 * prop-del for the SVN_PROP_ENTRY_LOCK_TOKEN property.  This causes problems,
 * since the client doesn't have this property in the first place, but the
 * deletion has side effects (unlike deleting a non-existent regular property
 * would).  To solve this, we introduce *another* function into the API, not
 * a part of the Ev2 callbacks, but a companion which is used to register
 * the unlock of a path.  See ev2_change_file_prop() for implemenation
 * details.
 */

struct ev2_edit_baton
{
  svn_editor_t *editor;

  apr_hash_t *changes;  /* REPOS_RELPATH -> struct change_node  */

  apr_array_header_t *path_order;
  int paths_processed;

  /* For calculating relpaths from Ev1 copyfrom urls. */
  const char *repos_root;
  const char *base_relpath;

  apr_pool_t *edit_pool;
  struct svn_delta__extra_baton *exb;
  svn_boolean_t closed;

  svn_boolean_t *found_abs_paths; /* Did we strip an incoming '/' from the
                                     paths?  */

  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  svn_delta_fetch_base_func_t fetch_base_func;
  void *fetch_base_baton;

  svn_delta__unlock_func_t do_unlock;
  void *unlock_baton;
};

struct ev2_dir_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
  svn_revnum_t base_revision;

  const char *copyfrom_relpath;
  svn_revnum_t copyfrom_rev;
};

struct ev2_file_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
  svn_revnum_t base_revision;
  const char *delta_base;
};

enum restructure_action_t
{
  RESTRUCTURE_NONE = 0,
  RESTRUCTURE_ADD,         /* add the node, maybe replacing. maybe copy  */
  RESTRUCTURE_ADD_ABSENT,  /* add an absent node, possibly replacing  */
  RESTRUCTURE_DELETE       /* delete this node  */
};

/* Records everything about how this node is to be changed.  */
struct change_node
{
  /* what kind of (tree) restructure is occurring at this node?  */
  enum restructure_action_t action;

  svn_node_kind_t kind;  /* the NEW kind of this node  */

  /* We need two revisions: one to specify the revision we are altering,
     and a second to specify the revision to delete/replace. These are
     mutually exclusive, but they need to be separate to ensure we don't
     confuse the operation on this node. For example, we may delete a
     node and replace it we use DELETING for REPLACES_REV, and ignore
     the value placed into CHANGING when properties were set/changed
     on the new node. Or we simply change a node (setting CHANGING),
     and DELETING remains SVN_INVALID_REVNUM, indicating we are not
     attempting to replace a node.  */
  svn_revnum_t changing;
  svn_revnum_t deleting;

  apr_hash_t *props;  /* new/final set of props to apply  */

  svn_boolean_t contents_changed; /* the file contents changed */
  const char *contents_abspath;  /* file containing new fulltext  */
  svn_checksum_t *checksum;  /* checksum of new fulltext  */

  /* If COPYFROM_PATH is not NULL, then copy PATH@REV to this node.
     RESTRUCTURE must be RESTRUCTURE_ADD.  */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

  /* Record whether an incoming propchange unlocked this node.  */
  svn_boolean_t unlock;
};


static struct change_node *
locate_change(struct ev2_edit_baton *eb,
              const char *relpath)
{
  struct change_node *change = svn_hash_gets(eb->changes, relpath);

  if (change != NULL)
    return change;

  /* Shift RELPATH into the proper pool, and record the observed order.  */
  relpath = apr_pstrdup(eb->edit_pool, relpath);
  APR_ARRAY_PUSH(eb->path_order, const char *) = relpath;

  /* Return an empty change. Callers will tweak as needed.  */
  change = apr_pcalloc(eb->edit_pool, sizeof(*change));
  change->changing = SVN_INVALID_REVNUM;
  change->deleting = SVN_INVALID_REVNUM;
  change->kind = svn_node_unknown;

  svn_hash_sets(eb->changes, relpath, change);

  return change;
}


static svn_error_t *
apply_propedit(struct ev2_edit_baton *eb,
               const char *relpath,
               svn_node_kind_t kind,
               svn_revnum_t base_revision,
               const char *name,
               const svn_string_t *value,
               apr_pool_t *scratch_pool)
{
  struct change_node *change = locate_change(eb, relpath);

  SVN_ERR_ASSERT(change->kind == svn_node_unknown || change->kind == kind);
  change->kind = kind;

  /* We're now changing the node. Record the revision.  */
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(change->changing)
                 || change->changing == base_revision);
  change->changing = base_revision;

  if (change->props == NULL)
    {
      /* Fetch the original set of properties. We'll apply edits to create
         the new/target set of properties.

         If this is a copied/moved now, then the original properties come
         from there. If the node has been added, it starts with empty props.
         Otherwise, we get the properties from BASE.  */

      if (change->copyfrom_path)
        SVN_ERR(eb->fetch_props_func(&change->props,
                                     eb->fetch_props_baton,
                                     change->copyfrom_path,
                                     change->copyfrom_rev,
                                     eb->edit_pool, scratch_pool));
      else if (change->action == RESTRUCTURE_ADD)
        change->props = apr_hash_make(eb->edit_pool);
      else
        SVN_ERR(eb->fetch_props_func(&change->props,
                                     eb->fetch_props_baton,
                                     relpath, base_revision,
                                     eb->edit_pool, scratch_pool));
    }

  if (value == NULL)
    svn_hash_sets(change->props, name, NULL);
  else
    svn_hash_sets(change->props,
                  apr_pstrdup(eb->edit_pool, name),
                  svn_string_dup(value, eb->edit_pool));

  return SVN_NO_ERROR;
}


/* Find all the paths which are immediate children of PATH and return their
   basenames in a list. */
static apr_array_header_t *
get_children(struct ev2_edit_baton *eb,
             const char *path,
             apr_pool_t *pool)
{
  apr_array_header_t *children = apr_array_make(pool, 1, sizeof(const char *));
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, eb->changes); hi; hi = apr_hash_next(hi))
    {
      const char *repos_relpath = apr_hash_this_key(hi);
      const char *child;

      /* Find potential children. */
      child = svn_relpath_skip_ancestor(path, repos_relpath);
      if (!child || !*child)
        continue;

      /* If we have a path separator, it's a deep child, so just ignore it.
         ### Is there an API we should be using for this? */
      if (strchr(child, '/') != NULL)
        continue;

      APR_ARRAY_PUSH(children, const char *) = child;
    }

  return children;
}


static svn_error_t *
process_actions(struct ev2_edit_baton *eb,
                const char *repos_relpath,
                const struct change_node *change,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *props = NULL;
  svn_stream_t *contents = NULL;
  svn_checksum_t *checksum = NULL;
  svn_node_kind_t kind = svn_node_unknown;

  SVN_ERR_ASSERT(change != NULL);

  if (change->unlock)
    SVN_ERR(eb->do_unlock(eb->unlock_baton, repos_relpath, scratch_pool));

  if (change->action == RESTRUCTURE_DELETE)
    {
      /* If the action was left as RESTRUCTURE_DELETE, then a
         replacement is not occurring. Just do the delete and bail.  */
      SVN_ERR(svn_editor_delete(eb->editor, repos_relpath,
                                change->deleting));

      /* No further work possible on this node.  */
      return SVN_NO_ERROR;
    }
  if (change->action == RESTRUCTURE_ADD_ABSENT)
    {
      SVN_ERR(svn_editor_add_absent(eb->editor, repos_relpath,
                                    change->kind, change->deleting));

      /* No further work possible on this node.  */
      return SVN_NO_ERROR;
    }

  if (change->contents_changed)
    {
      /* We can only set text on files. */
      /* ### validate we aren't overwriting KIND?  */
      kind = svn_node_file;

      if (change->contents_abspath)
        {
          /* ### the checksum might be in CHANGE->CHECKSUM  */
          SVN_ERR(svn_io_file_checksum2(&checksum, change->contents_abspath,
                                        svn_checksum_sha1, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&contents, change->contents_abspath,
                                           scratch_pool, scratch_pool));
        }
      else
        {
          contents = svn_stream_empty(scratch_pool);
          checksum = svn_checksum_empty_checksum(svn_checksum_sha1,
                                                 scratch_pool);
        }
    }

  if (change->props != NULL)
    {
      /* ### validate we aren't overwriting KIND?  */
      kind = change->kind;
      props = change->props;
    }

  if (change->action == RESTRUCTURE_ADD)
    {
      /* An add might be a replace. Grab the revnum we're replacing.  */
      svn_revnum_t replaces_rev = change->deleting;

      kind = change->kind;

      if (change->copyfrom_path != NULL)
        {
          SVN_ERR(svn_editor_copy(eb->editor, change->copyfrom_path,
                                  change->copyfrom_rev,
                                  repos_relpath, replaces_rev));
          /* Fall through to possibly make changes post-copy.  */
        }
      else
        {
          /* If no properties were defined, then use an empty set.  */
          if (props == NULL)
            props = apr_hash_make(scratch_pool);

          if (kind == svn_node_dir)
            {
              const apr_array_header_t *children;

              children = get_children(eb, repos_relpath, scratch_pool);
              SVN_ERR(svn_editor_add_directory(eb->editor, repos_relpath,
                                               children, props,
                                               replaces_rev));
            }
          else
            {
              /* If this file was added, but apply_txdelta() was not
                 called (i.e., CONTENTS_CHANGED is FALSE), then we're adding
                 an empty file.  */
              if (change->contents_abspath == NULL)
                {
                  contents = svn_stream_empty(scratch_pool);
                  checksum = svn_checksum_empty_checksum(svn_checksum_sha1,
                                                         scratch_pool);
                }

              SVN_ERR(svn_editor_add_file(eb->editor, repos_relpath,
                                          checksum, contents, props,
                                          replaces_rev));
            }

          /* No further work possible on this node.  */
          return SVN_NO_ERROR;
        }
    }

#if 0
  /* There *should* be work for this node. But it seems that isn't true
     in some cases. Future investigation...  */
  SVN_ERR_ASSERT(props || contents);
#endif
  if (props || contents)
    {
      /* Changes to properties or content should have indicated the revision
         it was intending to change.

         Oop. Not true. The node may be locally-added.  */
#if 0
      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(change->changing));
#endif

      /* ### we need to gather up the target set of children  */

      if (kind == svn_node_dir)
        SVN_ERR(svn_editor_alter_directory(eb->editor, repos_relpath,
                                           change->changing, NULL, props));
      else
        SVN_ERR(svn_editor_alter_file(eb->editor, repos_relpath,
                                      change->changing,
                                      checksum, contents, props));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
run_ev2_actions(struct ev2_edit_baton *eb,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);

  /* Possibly pick up where we left off. Ocassionally, we do some of these
     as part of close_edit() and then some more as part of abort_edit()  */
  for (; eb->paths_processed < eb->path_order->nelts; ++eb->paths_processed)
    {
      const char *repos_relpath = APR_ARRAY_IDX(eb->path_order,
                                                eb->paths_processed,
                                                const char *);
      const struct change_node *change = svn_hash_gets(eb->changes,
                                                       repos_relpath);

      svn_pool_clear(iterpool);

      SVN_ERR(process_actions(eb, repos_relpath, change, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static const char *
map_to_repos_relpath(struct ev2_edit_baton *eb,
                     const char *path_or_url,
                     apr_pool_t *result_pool)
{
  if (svn_path_is_url(path_or_url))
    {
      return svn_uri_skip_ancestor(eb->repos_root, path_or_url, result_pool);
    }
  else
    {
      return svn_relpath_join(eb->base_relpath,
                              path_or_url[0] == '/'
                                    ? path_or_url + 1 : path_or_url,
                              result_pool);
    }
}


static svn_error_t *
ev2_set_target_revision(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  if (eb->exb->target_revision)
    SVN_ERR(eb->exb->target_revision(eb->exb->baton, target_revision,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_root(void *edit_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **root_baton)
{
  struct ev2_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));
  struct ev2_edit_baton *eb = edit_baton;

  db->eb = eb;
  db->path = apr_pstrdup(eb->edit_pool, eb->base_relpath);
  db->base_revision = base_revision;

  *root_baton = db;

  if (eb->exb->start_edit)
    SVN_ERR(eb->exb->start_edit(eb->exb->baton, base_revision));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_delete_entry(const char *path,
                 svn_revnum_t revision,
                 void *parent_baton,
                 apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  svn_revnum_t base_revision;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  struct change_node *change = locate_change(pb->eb, relpath);

  if (SVN_IS_VALID_REVNUM(revision))
    base_revision = revision;
  else
    base_revision = pb->base_revision;

  SVN_ERR_ASSERT(change->action == RESTRUCTURE_NONE);
  change->action = RESTRUCTURE_DELETE;

  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(change->deleting)
                 || change->deleting == base_revision);
  change->deleting = base_revision;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_add_directory(const char *path,
                  void *parent_baton,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_revision,
                  apr_pool_t *result_pool,
                  void **child_baton)
{
  /* ### fix this?  */
  apr_pool_t *scratch_pool = result_pool;
  struct ev2_dir_baton *pb = parent_baton;
  struct ev2_dir_baton *cb = apr_pcalloc(result_pool, sizeof(*cb));
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  struct change_node *change = locate_change(pb->eb, relpath);

  /* ### assert that RESTRUCTURE is NONE or DELETE?  */
  change->action = RESTRUCTURE_ADD;
  change->kind = svn_node_dir;

  cb->eb = pb->eb;
  cb->path = apr_pstrdup(result_pool, relpath);
  cb->base_revision = pb->base_revision;
  *child_baton = cb;

  if (!copyfrom_path)
    {
      if (pb->copyfrom_relpath)
        {
          const char *name = svn_relpath_basename(relpath, scratch_pool);
          cb->copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath, name,
                                                  result_pool);
          cb->copyfrom_rev = pb->copyfrom_rev;
        }
    }
  else
    {
      /* A copy */

      change->copyfrom_path = map_to_repos_relpath(pb->eb, copyfrom_path,
                                                   pb->eb->edit_pool);
      change->copyfrom_rev = copyfrom_revision;

      cb->copyfrom_relpath = change->copyfrom_path;
      cb->copyfrom_rev = change->copyfrom_rev;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_directory(const char *path,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *result_pool,
                   void **child_baton)
{
  /* ### fix this?  */
  apr_pool_t *scratch_pool = result_pool;
  struct ev2_dir_baton *pb = parent_baton;
  struct ev2_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);

  db->eb = pb->eb;
  db->path = apr_pstrdup(result_pool, relpath);
  db->base_revision = base_revision;

  if (pb->copyfrom_relpath)
    {
      /* We are inside a copy. */
      const char *name = svn_relpath_basename(relpath, scratch_pool);

      db->copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath, name,
                                              result_pool);
      db->copyfrom_rev = pb->copyfrom_rev;
    }

  *child_baton = db;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_change_dir_prop(void *dir_baton,
                    const char *name,
                    const svn_string_t *value,
                    apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *db = dir_baton;

  SVN_ERR(apply_propedit(db->eb, db->path, svn_node_dir, db->base_revision,
                         name, value, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_directory(void *dir_baton,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_directory(const char *path,
                     void *parent_baton,
                     apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  struct change_node *change = locate_change(pb->eb, relpath);

  /* ### assert that RESTRUCTURE is NONE or DELETE?  */
  change->action = RESTRUCTURE_ADD_ABSENT;
  change->kind = svn_node_dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_add_file(const char *path,
             void *parent_baton,
             const char *copyfrom_path,
             svn_revnum_t copyfrom_revision,
             apr_pool_t *result_pool,
             void **file_baton)
{
  /* ### fix this?  */
  apr_pool_t *scratch_pool = result_pool;
  struct ev2_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  struct change_node *change = locate_change(pb->eb, relpath);

  /* ### assert that RESTRUCTURE is NONE or DELETE?  */
  change->action = RESTRUCTURE_ADD;
  change->kind = svn_node_file;

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, relpath);
  fb->base_revision = pb->base_revision;
  *file_baton = fb;

  if (!copyfrom_path)
    {
      /* Don't bother fetching the base, as in an add we don't have a base. */
      fb->delta_base = NULL;
    }
  else
    {
      /* A copy */

      change->copyfrom_path = map_to_repos_relpath(fb->eb, copyfrom_path,
                                                   fb->eb->edit_pool);
      change->copyfrom_rev = copyfrom_revision;

      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      change->copyfrom_path,
                                      change->copyfrom_rev,
                                      result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_file(const char *path,
              void *parent_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **file_baton)
{
  /* ### fix this?  */
  apr_pool_t *scratch_pool = result_pool;
  struct ev2_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, relpath);
  fb->base_revision = base_revision;

  if (pb->copyfrom_relpath)
    {
      /* We're in a copied directory, so the delta base is going to be
         based up on the copy source. */
      const char *name = svn_relpath_basename(relpath, scratch_pool);
      const char *copyfrom_relpath = svn_relpath_join(pb->copyfrom_relpath,
                                                      name,
                                                      scratch_pool);

      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      copyfrom_relpath, pb->copyfrom_rev,
                                      result_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      relpath, base_revision,
                                      result_pool, scratch_pool));
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  svn_stream_t *source;

  apr_pool_t *pool;
};

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  svn_error_t *err;

  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_close(hb->source));

  svn_pool_destroy(hb->pool);

  return svn_error_trace(err);
}

/* Lazy-open handler for getting a read-only stream of the delta base. */
static svn_error_t *
open_delta_base(svn_stream_t **stream, void *baton,
                apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  const char *const delta_base = baton;
  return svn_stream_open_readonly(stream, delta_base,
                                  result_pool, scratch_pool);
}

/* Lazy-open handler for opening a stream for the delta result. */
static svn_error_t *
open_delta_target(svn_stream_t **stream, void *baton,
                apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  struct change_node *change = baton;
  return svn_stream_open_unique(stream, &change->contents_abspath,
                                NULL,
                                svn_io_file_del_on_pool_cleanup,
                                result_pool, scratch_pool);
}

static svn_error_t *
ev2_apply_textdelta(void *file_baton,
                    const char *base_checksum,
                    apr_pool_t *result_pool,
                    svn_txdelta_window_handler_t *handler,
                    void **handler_baton)
{
  struct ev2_file_baton *fb = file_baton;
  apr_pool_t *handler_pool = svn_pool_create(fb->eb->edit_pool);
  struct handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
  struct change_node *change;
  svn_stream_t *target;

  change = locate_change(fb->eb, fb->path);
  SVN_ERR_ASSERT(!change->contents_changed);
  SVN_ERR_ASSERT(change->contents_abspath == NULL);
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(change->changing)
                 || change->changing == fb->base_revision);
  change->changing = fb->base_revision;

  if (! fb->delta_base)
    hb->source = svn_stream_empty(handler_pool);
  else
    hb->source = svn_stream_lazyopen_create(open_delta_base,
                                            (char*)fb->delta_base,
                                            FALSE, handler_pool);

  change->contents_changed = TRUE;
  target = svn_stream_lazyopen_create(open_delta_target, change,
                                      FALSE, fb->eb->edit_pool);

  svn_txdelta_apply(hb->source, target,
                    NULL, NULL,
                    handler_pool,
                    &hb->apply_handler, &hb->apply_baton);

  hb->pool = handler_pool;

  *handler_baton = hb;
  *handler = window_handler;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_change_file_prop(void *file_baton,
                     const char *name,
                     const svn_string_t *value,
                     apr_pool_t *scratch_pool)
{
  struct ev2_file_baton *fb = file_baton;

  if (!strcmp(name, SVN_PROP_ENTRY_LOCK_TOKEN) && value == NULL)
    {
      /* We special case the lock token propery deletion, which is the
         server's way of telling the client to unlock the path. */

      /* ### this duplicates much of apply_propedit(). fix in future.  */
      const char *relpath = map_to_repos_relpath(fb->eb, fb->path,
                                                 scratch_pool);
      struct change_node *change = locate_change(fb->eb, relpath);

      change->unlock = TRUE;
    }

  SVN_ERR(apply_propedit(fb->eb, fb->path, svn_node_file, fb->base_revision,
                         name, value, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_file(void *file_baton,
               const char *text_checksum,
               apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_file(const char *path,
                void *parent_baton,
                apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  const char *relpath = map_to_repos_relpath(pb->eb, path, scratch_pool);
  struct change_node *change = locate_change(pb->eb, relpath);

  /* ### assert that RESTRUCTURE is NONE or DELETE?  */
  change->action = RESTRUCTURE_ADD_ABSENT;
  change->kind = svn_node_file;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  SVN_ERR(run_ev2_actions(edit_baton, scratch_pool));
  eb->closed = TRUE;
  return svn_error_trace(svn_editor_complete(eb->editor));
}

static svn_error_t *
ev2_abort_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  SVN_ERR(run_ev2_actions(edit_baton, scratch_pool));
  if (!eb->closed)
    return svn_error_trace(svn_editor_abort(eb->editor));
  else
    return SVN_NO_ERROR;
}

/* Return a svn_delta_editor_t * in DEDITOR, with an accompanying baton in
 * DEDITOR_BATON, which will drive EDITOR.  These will both be
 * allocated in RESULT_POOL, which may become large and long-lived;
 * SCRATCH_POOL is used for temporary allocations.
 *
 * The other parameters are as follows:
 *  - UNLOCK_FUNC / UNLOCK_BATON: A callback / baton which will be called
 *         when an unlocking action is received.
 *  - FOUND_ABS_PATHS: A pointer to a boolean flag which will be set if
 *         this shim determines that it is receiving absolute paths.
 *  - FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback / baton pair which
 *         will be used by the shim handlers if they need to determine the
 *         existing properties on a  path.
 *  - FETCH_BASE_FUNC / FETCH_BASE_BATON: A callback / baton pair which will
 *         be used by the shims handlers if they need to determine the base
 *         text of a path.  It should only be invoked for files.
 *  - EXB: An 'extra baton' which is used to communicate between the shims.
 *         Its callbacks should be invoked at the appropriate time by this
 *         shim.
 */
svn_error_t *
svn_delta__delta_from_editor(const svn_delta_editor_t **deditor,
                  void **dedit_baton,
                  svn_editor_t *editor,
                  svn_delta__unlock_func_t unlock_func,
                  void *unlock_baton,
                  svn_boolean_t *found_abs_paths,
                  const char *repos_root,
                  const char *base_relpath,
                  svn_delta_fetch_props_func_t fetch_props_func,
                  void *fetch_props_baton,
                  svn_delta_fetch_base_func_t fetch_base_func,
                  void *fetch_base_baton,
                  struct svn_delta__extra_baton *exb,
                  apr_pool_t *pool)
{
  /* Static 'cause we don't want it to be on the stack. */
  static svn_delta_editor_t delta_editor = {
      ev2_set_target_revision,
      ev2_open_root,
      ev2_delete_entry,
      ev2_add_directory,
      ev2_open_directory,
      ev2_change_dir_prop,
      ev2_close_directory,
      ev2_absent_directory,
      ev2_add_file,
      ev2_open_file,
      ev2_apply_textdelta,
      ev2_change_file_prop,
      ev2_close_file,
      ev2_absent_file,
      ev2_close_edit,
      ev2_abort_edit
    };
  struct ev2_edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));

  if (!base_relpath)
    base_relpath = "";
  else if (base_relpath[0] == '/')
    base_relpath += 1;

  eb->editor = editor;
  eb->changes = apr_hash_make(pool);
  eb->path_order = apr_array_make(pool, 1, sizeof(const char *));
  eb->edit_pool = pool;
  eb->found_abs_paths = found_abs_paths;
  *eb->found_abs_paths = FALSE;
  eb->exb = exb;
  eb->repos_root = apr_pstrdup(pool, repos_root);
  eb->base_relpath = apr_pstrdup(pool, base_relpath);

  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->fetch_base_func = fetch_base_func;
  eb->fetch_base_baton = fetch_base_baton;

  eb->do_unlock = unlock_func;
  eb->unlock_baton = unlock_baton;

  *dedit_baton = eb;
  *deditor = &delta_editor;

  return SVN_NO_ERROR;
}


/* ### note the similarity to struct change_node. these structures will
   ### be combined in the future.  */
struct operation {
  /* ### leave these two here for now. still used.  */
  svn_revnum_t base_revision;
  void *baton;
};

struct editor_baton
{
  const svn_delta_editor_t *deditor;
  void *dedit_baton;

  svn_delta_fetch_kind_func_t fetch_kind_func;
  void *fetch_kind_baton;

  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  struct operation root;
  svn_boolean_t *make_abs_paths;
  const char *repos_root;
  const char *base_relpath;

  /* REPOS_RELPATH -> struct change_node *  */
  apr_hash_t *changes;

  apr_pool_t *edit_pool;
};


/* Insert a new change for RELPATH, or return an existing one.  */
static struct change_node *
insert_change(const char *relpath,
              apr_hash_t *changes)
{
  apr_pool_t *result_pool;
  struct change_node *change;

  change = svn_hash_gets(changes, relpath);
  if (change != NULL)
    return change;

  result_pool = apr_hash_pool_get(changes);

  /* Return an empty change. Callers will tweak as needed.  */
  change = apr_pcalloc(result_pool, sizeof(*change));
  change->changing = SVN_INVALID_REVNUM;
  change->deleting = SVN_INVALID_REVNUM;

  svn_hash_sets(changes, apr_pstrdup(result_pool, relpath), change);

  return change;
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
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(relpath, eb->changes);

  change->action = RESTRUCTURE_ADD;
  change->kind = svn_node_dir;
  change->deleting = replaces_rev;
  change->props = svn_prop_hash_dup(props, eb->edit_pool);

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
  struct editor_baton *eb = baton;
  const char *tmp_filename;
  svn_stream_t *tmp_stream;
  svn_checksum_t *md5_checksum;
  struct change_node *change = insert_change(relpath, eb->changes);

  /* We may need to re-checksum these contents */
  if (!(checksum && checksum->kind == svn_checksum_md5))
    contents = svn_stream_checksummed2(contents, &md5_checksum, NULL,
                                       svn_checksum_md5, TRUE, scratch_pool);
  else
    md5_checksum = (svn_checksum_t *)checksum;

  /* Spool the contents to a tempfile, and provide that to the driver. */
  SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 eb->edit_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, tmp_stream, NULL, NULL, scratch_pool));

  change->action = RESTRUCTURE_ADD;
  change->kind = svn_node_file;
  change->deleting = replaces_rev;
  change->props = svn_prop_hash_dup(props, eb->edit_pool);
  change->contents_changed = TRUE;
  change->contents_abspath = tmp_filename;
  change->checksum = svn_checksum_dup(md5_checksum, eb->edit_pool);

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
#if 0
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(relpath, eb->changes);

  change->action = RESTRUCTURE_ADD;
  change->kind = svn_node_symlink;
  change->deleting = replaces_rev;
  change->props = svn_prop_hash_dup(props, eb->edit_pool);
  /* ### target  */
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
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(relpath, eb->changes);

  change->action = RESTRUCTURE_ADD_ABSENT;
  change->kind = kind;
  change->deleting = replaces_rev;

  return SVN_NO_ERROR;
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
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(relpath, eb->changes);

  /* ### should we verify the kind is truly a directory?  */

  /* ### do we need to do anything with CHILDREN?  */

  /* Note: this node may already have information in CHANGE as a result
     of an earlier copy/move operation.  */
  change->kind = svn_node_dir;
  change->changing = revision;
  change->props = svn_prop_hash_dup(props, eb->edit_pool);

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
  struct editor_baton *eb = baton;
  svn_stream_t *tmp_stream;
  struct change_node *change = insert_change(relpath, eb->changes);

  /* Note: this node may already have information in CHANGE as a result
     of an earlier copy/move operation.  */

  /* ### should we verify the kind is truly a file?  */
  change->kind = svn_node_file;
  change->changing = revision;
  if (props != NULL)
    change->props = svn_prop_hash_dup(props, eb->edit_pool);

  if (contents)
    {
      const char *tmp_filename;
      svn_checksum_t *md5_checksum;

      /* We may need to re-checksum these contents */
      if (checksum && checksum->kind == svn_checksum_md5)
        md5_checksum = (svn_checksum_t *)checksum;
      else
        contents = svn_stream_checksummed2(contents, &md5_checksum, NULL,
                                           svn_checksum_md5, TRUE,
                                           scratch_pool);

      /* Spool the contents to a tempfile, and provide that to the driver. */
      SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     eb->edit_pool, scratch_pool));
      SVN_ERR(svn_stream_copy3(contents, tmp_stream, NULL, NULL,
                               scratch_pool));

      change->contents_changed = TRUE;
      change->contents_abspath = tmp_filename;
      change->checksum = svn_checksum_dup(md5_checksum, eb->edit_pool);
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
  /* ### should we verify the kind is truly a symlink?  */

  /* ### do something  */

  SVN__NOT_IMPLEMENTED();
}

/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(relpath, eb->changes);

  change->action = RESTRUCTURE_DELETE;
  /* change->kind = svn_node_unknown;  */
  change->deleting = revision;

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
  struct editor_baton *eb = baton;
  struct change_node *change = insert_change(dst_relpath, eb->changes);

  change->action = RESTRUCTURE_ADD;
  /* change->kind = svn_node_unknown;  */
  change->deleting = replaces_rev;
  change->copyfrom_path = apr_pstrdup(eb->edit_pool, src_relpath);
  change->copyfrom_rev = src_revision;

  /* We need the source's kind to know whether to call add_directory()
     or add_file() later on.  */
  SVN_ERR(eb->fetch_kind_func(&change->kind, eb->fetch_kind_baton,
                              change->copyfrom_path,
                              change->copyfrom_rev,
                              scratch_pool));

  /* Note: this node may later have alter_*() called on it.  */

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
  struct editor_baton *eb = baton;
  struct change_node *change;

  /* Remap a move into a DELETE + COPY.  */

  change = insert_change(src_relpath, eb->changes);
  change->action = RESTRUCTURE_DELETE;
  /* change->kind = svn_node_unknown;  */
  change->deleting = src_revision;

  change = insert_change(dst_relpath, eb->changes);
  change->action = RESTRUCTURE_ADD;
  /* change->kind = svn_node_unknown;  */
  change->deleting = replaces_rev;
  change->copyfrom_path = apr_pstrdup(eb->edit_pool, src_relpath);
  change->copyfrom_rev = src_revision;

  /* We need the source's kind to know whether to call add_directory()
     or add_file() later on.  */
  SVN_ERR(eb->fetch_kind_func(&change->kind, eb->fetch_kind_baton,
                              change->copyfrom_path,
                              change->copyfrom_rev,
                              scratch_pool));

  /* Note: this node may later have alter_*() called on it.  */

  return SVN_NO_ERROR;
}

static int
count_components(const char *relpath)
{
  int count = 1;
  const char *slash = strchr(relpath, '/');

  while (slash != NULL)
    {
      ++count;
      slash = strchr(slash + 1, '/');
    }
  return count;
}


static int
sort_deletes_first(const svn_sort__item_t *item1,
                   const svn_sort__item_t *item2)
{
  const char *relpath1 = item1->key;
  const char *relpath2 = item2->key;
  const struct change_node *change1 = item1->value;
  const struct change_node *change2 = item2->value;
  const char *slash1;
  const char *slash2;
  ptrdiff_t len1;
  ptrdiff_t len2;

  /* Force the root to always sort first. Otherwise, it may look like a
     sibling of its children (no slashes), and could get sorted *after*
     any children that get deleted.  */
  if (*relpath1 == '\0')
    return -1;
  if (*relpath2 == '\0')
    return 1;

  /* Are these two items siblings? The 'if' statement tests if they are
     siblings in the root directory, or that slashes were found in both
     paths, that the length of the paths to those slashes match, and that
     the path contents up to those slashes also match.  */
  slash1 = strrchr(relpath1, '/');
  slash2 = strrchr(relpath2, '/');
  if ((slash1 == NULL && slash2 == NULL)
      || (slash1 != NULL
          && slash2 != NULL
          && (len1 = slash1 - relpath1) == (len2 = slash2 - relpath2)
          && memcmp(relpath1, relpath2, len1) == 0))
    {
      if (change1->action == RESTRUCTURE_DELETE)
        {
          if (change2->action == RESTRUCTURE_DELETE)
            {
              /* If both items are being deleted, then we don't care about
                 the order. State they are equal.  */
              return 0;
            }

          /* ITEM1 is being deleted. Sort it before the surviving item.  */
          return -1;
        }
      if (change2->action == RESTRUCTURE_DELETE)
        /* ITEM2 is being deleted. Sort it before the surviving item.  */
        return 1;

      /* Normally, we don't care about the ordering of two siblings. However,
         if these siblings are directories, then we need to provide an
         ordering so that the quicksort algorithm will further sort them
         relative to the maybe-directory's children.

         Without this additional ordering, we could see that A/B/E and A/B/F
         are equal. And then A/B/E/child is sorted before A/B/F. But since
         E and F are "equal", A/B/E could arrive *after* A/B/F and after the
         A/B/E/child node.  */

      /* FALLTHROUGH */
    }

  /* Paths-to-be-deleted with fewer components always sort earlier.

     For example, gamma will sort before E/alpha.

     Without this test, E/alpha lexicographically sorts before gamma,
     but gamma sorts before E when gamma is to be deleted. This kind of
     ordering would place E/alpha before E. Not good.

     With this test, gamma sorts before E/alpha. E and E/alpha are then
     sorted by svn_path_compare_paths() (which places E before E/alpha).  */
  if (change1->action == RESTRUCTURE_DELETE
      || change2->action == RESTRUCTURE_DELETE)
    {
      int count1 = count_components(relpath1);
      int count2 = count_components(relpath2);

      if (count1 < count2 && change1->action == RESTRUCTURE_DELETE)
        return -1;
      if (count1 > count2 && change2->action == RESTRUCTURE_DELETE)
        return 1;
    }

  /* Use svn_path_compare_paths() to get correct depth-based ordering.  */
  return svn_path_compare_paths(relpath1, relpath2);
}


static const apr_array_header_t *
get_sorted_paths(apr_hash_t *changes,
                 const char *base_relpath,
                 apr_pool_t *scratch_pool)
{
  const apr_array_header_t *items;
  apr_array_header_t *paths;
  int i;

  /* Construct a sorted array of svn_sort__item_t structs. Within a given
     directory, nodes that are to be deleted will appear first.  */
  items = svn_sort__hash(changes, sort_deletes_first, scratch_pool);

  /* Build a new array with just the paths, trimmed to relative paths for
     the Ev1 drive.  */
  paths = apr_array_make(scratch_pool, items->nelts, sizeof(const char *));
  for (i = items->nelts; i--; )
    {
      const svn_sort__item_t *item;

      item = &APR_ARRAY_IDX(items, i, const svn_sort__item_t);
      APR_ARRAY_IDX(paths, i, const char *)
        = svn_relpath_skip_ancestor(base_relpath, item->key);
    }

  /* We didn't use PUSH, so set the proper number of elements.  */
  paths->nelts = items->nelts;

  return paths;
}


static svn_error_t *
drive_ev1_props(const struct editor_baton *eb,
                const char *repos_relpath,
                const struct change_node *change,
                void *node_baton,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *old_props;
  apr_array_header_t *propdiffs;
  int i;

  /* If there are no properties to install, then just exit.  */
  if (change->props == NULL)
    return SVN_NO_ERROR;

  if (change->copyfrom_path)
    {
      /* The pristine properties are from the copy/move source.  */
      SVN_ERR(eb->fetch_props_func(&old_props, eb->fetch_props_baton,
                                   change->copyfrom_path,
                                   change->copyfrom_rev,
                                   scratch_pool, iterpool));
    }
  else if (change->action == RESTRUCTURE_ADD)
    {
      /* Locally-added nodes have no pristine properties.

         Note: we can use iterpool; this hash only needs to survive to
         the propdiffs call, and there are no contents to preserve.  */
      old_props = apr_hash_make(iterpool);
    }
  else
    {
      /* Fetch the pristine properties for whatever we're editing.  */
      SVN_ERR(eb->fetch_props_func(&old_props, eb->fetch_props_baton,
                                   repos_relpath, change->changing,
                                   scratch_pool, iterpool));
    }

  SVN_ERR(svn_prop_diffs(&propdiffs, change->props, old_props, scratch_pool));

  for (i = 0; i < propdiffs->nelts; i++)
    {
      /* Note: the array returned by svn_prop_diffs() is an array of
         actual structures, not pointers to them. */
      const svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->change_dir_prop(node_baton,
                                             prop->name, prop->value,
                                             iterpool));
      else
        SVN_ERR(eb->deditor->change_file_prop(node_baton,
                                              prop->name, prop->value,
                                              iterpool));
    }

  /* Handle the funky unlock protocol. Note: only possibly on files.  */
  if (change->unlock)
    {
      SVN_ERR_ASSERT(change->kind == svn_node_file);
      SVN_ERR(eb->deditor->change_file_prop(node_baton,
                                            SVN_PROP_ENTRY_LOCK_TOKEN, NULL,
                                            iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Conforms to svn_delta_path_driver_cb_func_t  */
static svn_error_t *
apply_change(void **dir_baton,
             void *parent_baton,
             void *callback_baton,
             const char *ev1_relpath,
             apr_pool_t *result_pool)
{
  /* ### fix this?  */
  apr_pool_t *scratch_pool = result_pool;
  const struct editor_baton *eb = callback_baton;
  const struct change_node *change;
  const char *relpath;
  void *file_baton = NULL;

  /* Typically, we are not creating new directory batons.  */
  *dir_baton = NULL;

  relpath = svn_relpath_join(eb->base_relpath, ev1_relpath, scratch_pool);
  change = svn_hash_gets(eb->changes, relpath);

  /* The callback should only be called for paths in CHANGES.  */
  SVN_ERR_ASSERT(change != NULL);

  /* Are we editing the root of the tree?  */
  if (parent_baton == NULL)
    {
      /* The root was opened in start_edit_func()  */
      *dir_baton = eb->root.baton;

      /* Only property edits are allowed on the root.  */
      SVN_ERR_ASSERT(change->action == RESTRUCTURE_NONE);
      SVN_ERR(drive_ev1_props(eb, relpath, change, *dir_baton, scratch_pool));

      /* No further action possible for the root.  */
      return SVN_NO_ERROR;
    }

  if (change->action == RESTRUCTURE_DELETE)
    {
      SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting,
                                        parent_baton, scratch_pool));

      /* No futher action possible for this node.  */
      return SVN_NO_ERROR;
    }

  /* If we're not deleting this node, then we should know its kind.  */
  SVN_ERR_ASSERT(change->kind != svn_node_unknown);

  if (change->action == RESTRUCTURE_ADD_ABSENT)
    {
      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->absent_directory(ev1_relpath, parent_baton,
                                              scratch_pool));
      else
        SVN_ERR(eb->deditor->absent_file(ev1_relpath, parent_baton,
                                         scratch_pool));

      /* No further action possible for this node.  */
      return SVN_NO_ERROR;
    }
  /* RESTRUCTURE_NONE or RESTRUCTURE_ADD  */

  if (change->action == RESTRUCTURE_ADD)
    {
      const char *copyfrom_url = NULL;
      svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

      /* Do we have an old node to delete first?  */
      if (SVN_IS_VALID_REVNUM(change->deleting))
        SVN_ERR(eb->deditor->delete_entry(ev1_relpath, change->deleting,
                                          parent_baton, scratch_pool));

      /* Are we copying the node from somewhere?  */
      if (change->copyfrom_path)
        {
          if (eb->repos_root)
            copyfrom_url = svn_path_url_add_component2(eb->repos_root,
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
      else
        SVN_ERR(eb->deditor->add_file(ev1_relpath, parent_baton,
                                      copyfrom_url, copyfrom_rev,
                                      result_pool, &file_baton));
    }
  else
    {
      if (change->kind == svn_node_dir)
        SVN_ERR(eb->deditor->open_directory(ev1_relpath, parent_baton,
                                            change->changing,
                                            result_pool, dir_baton));
      else
        SVN_ERR(eb->deditor->open_file(ev1_relpath, parent_baton,
                                       change->changing,
                                       result_pool, &file_baton));
    }

  /* Apply any properties in CHANGE to the node.  */
  if (change->kind == svn_node_dir)
    SVN_ERR(drive_ev1_props(eb, relpath, change, *dir_baton, scratch_pool));
  else
    SVN_ERR(drive_ev1_props(eb, relpath, change, file_baton, scratch_pool));

  if (change->contents_changed && change->contents_abspath)
    {
      svn_txdelta_window_handler_t handler;
      void *handler_baton;
      svn_stream_t *contents;

      /* ### would be nice to have a BASE_CHECKSUM, but hey: this is the
         ### shim code...  */
      SVN_ERR(eb->deditor->apply_textdelta(file_baton, NULL, scratch_pool,
                                           &handler, &handler_baton));
      SVN_ERR(svn_stream_open_readonly(&contents, change->contents_abspath,
                                       scratch_pool, scratch_pool));
      /* ### it would be nice to send a true txdelta here, but whatever.  */
      SVN_ERR(svn_txdelta_send_stream(contents, handler, handler_baton,
                                      NULL, scratch_pool));
      SVN_ERR(svn_stream_close(contents));
    }

  if (file_baton)
    {
      const char *digest = svn_checksum_to_cstring(change->checksum,
                                                   scratch_pool);

      SVN_ERR(eb->deditor->close_file(file_baton, digest, scratch_pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
drive_changes(const struct editor_baton *eb,
              apr_pool_t *scratch_pool)
{
  struct change_node *change;
  const apr_array_header_t *paths;

  /* If we never opened a root baton, then the caller aborted the editor
     before it even began. There is nothing to do. Bail.  */
  if (eb->root.baton == NULL)
    return SVN_NO_ERROR;

  /* We need to make the path driver believe we want to make changes to
     the root. Otherwise, it will attempt an open_root(), which we already
     did in start_edit_func(). We can forge up a change record, if one
     does not already exist.  */
  change = insert_change(eb->base_relpath, eb->changes);
  change->kind = svn_node_dir;
  /* No property changes (tho they might exist from a real change).  */

  /* Get a sorted list of Ev1-relative paths.  */
  paths = get_sorted_paths(eb->changes, eb->base_relpath, scratch_pool);
  SVN_ERR(svn_delta_path_driver2(eb->deditor, eb->dedit_baton, paths,
                                 FALSE, apply_change, (void *)eb,
                                 scratch_pool));

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  svn_error_t *err;

  /* Drive the tree we've created. */
  err = drive_changes(eb, scratch_pool);
  if (!err)
     {
       err = svn_error_compose_create(err, eb->deditor->close_edit(
                                                            eb->dedit_baton,
                                                            scratch_pool));
     }

  if (err)
    svn_error_clear(eb->deditor->abort_edit(eb->dedit_baton, scratch_pool));

  return svn_error_trace(err);
}

/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  svn_error_t *err;
  svn_error_t *err2;

  /* We still need to drive anything we collected in the editor to this
     point. */

  /* Drive the tree we've created. */
  err = drive_changes(eb, scratch_pool);

  err2 = eb->deditor->abort_edit(eb->dedit_baton, scratch_pool);

  if (err2)
    {
      if (err)
        svn_error_clear(err2);
      else
        err = err2;
    }

  return svn_error_trace(err);
}

static svn_error_t *
start_edit_func(void *baton,
                svn_revnum_t base_revision)
{
  struct editor_baton *eb = baton;

  eb->root.base_revision = base_revision;

  /* For some Ev1 editors (such as the repos commit editor), the root must
     be open before can invoke any callbacks. The open_root() call sets up
     stuff (eg. open an FS txn) which will be needed.  */
  SVN_ERR(eb->deditor->open_root(eb->dedit_baton, eb->root.base_revision,
                                 eb->edit_pool, &eb->root.baton));

  return SVN_NO_ERROR;
}

static svn_error_t *
target_revision_func(void *baton,
                     svn_revnum_t target_revision,
                     apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  SVN_ERR(eb->deditor->set_target_revision(eb->dedit_baton, target_revision,
                                           scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
do_unlock(void *baton,
          const char *path,
          apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  {
    /* PATH is REPOS_RELPATH  */
    struct change_node *change = insert_change(path, eb->changes);

    /* We will need to propagate a deletion of SVN_PROP_ENTRY_LOCK_TOKEN  */
    change->unlock = TRUE;
  }

  return SVN_NO_ERROR;
}

/* Return an svn_editor_t * in EDITOR_P which will drive
 * DEDITOR/DEDIT_BATON.  EDITOR_P is allocated in RESULT_POOL, which may
 * become large and long-lived; SCRATCH_POOL is used for temporary
 * allocations.
 *
 * The other parameters are as follows:
 *  - EXB: An 'extra_baton' used for passing information between the coupled
 *         shims.  This includes actions like 'start edit' and 'set target'.
 *         As this shim receives these actions, it provides the extra baton
 *         to its caller.
 *  - UNLOCK_FUNC / UNLOCK_BATON: A callback / baton pair which a caller
 *         can use to notify this shim that a path should be unlocked (in the
 *         'svn lock' sense).  As this shim receives this action, it provides
 *         this callback / baton to its caller.
 *  - SEND_ABS_PATHS: A pointer which will be set prior to this edit (but
 *         not necessarily at the invocation of editor_from_delta()),and
 *         which indicates whether incoming paths should be expected to
 *         be absolute or relative.
 *  - CANCEL_FUNC / CANCEL_BATON: The usual; folded into the produced editor.
 *  - FETCH_KIND_FUNC / FETCH_KIND_BATON: A callback / baton pair which will
 *         be used by the shim handlers if they need to determine the kind of
 *         a path.
 *  - FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback / baton pair which
 *         will be used by the shim handlers if they need to determine the
 *         existing properties on a path.
 */
svn_error_t *
svn_delta__editor_from_delta(svn_editor_t **editor_p,
                  struct svn_delta__extra_baton **exb,
                  svn_delta__unlock_func_t *unlock_func,
                  void **unlock_baton,
                  const svn_delta_editor_t *deditor,
                  void *dedit_baton,
                  svn_boolean_t *send_abs_paths,
                  const char *repos_root,
                  const char *base_relpath,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_delta_fetch_kind_func_t fetch_kind_func,
                  void *fetch_kind_baton,
                  svn_delta_fetch_props_func_t fetch_props_func,
                  void *fetch_props_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_editor_t *editor;
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
  struct editor_baton *eb = apr_pcalloc(result_pool, sizeof(*eb));
  struct svn_delta__extra_baton *extra_baton = apr_pcalloc(result_pool,
                                                sizeof(*extra_baton));

  if (!base_relpath)
    base_relpath = "";
  else if (base_relpath[0] == '/')
    base_relpath += 1;

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;
  eb->edit_pool = result_pool;
  eb->repos_root = apr_pstrdup(result_pool, repos_root);
  eb->base_relpath = apr_pstrdup(result_pool, base_relpath);

  eb->changes = apr_hash_make(result_pool);

  eb->fetch_kind_func = fetch_kind_func;
  eb->fetch_kind_baton = fetch_kind_baton;
  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->root.base_revision = SVN_INVALID_REVNUM;

  eb->make_abs_paths = send_abs_paths;

  SVN_ERR(svn_editor_create(&editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(editor, &editor_cbs, scratch_pool));

  *editor_p = editor;

  *unlock_func = do_unlock;
  *unlock_baton = eb;

  extra_baton->start_edit = start_edit_func;
  extra_baton->target_revision = target_revision_func;
  extra_baton->baton = eb;

  *exb = extra_baton;

  return SVN_NO_ERROR;
}

svn_delta_shim_callbacks_t *
svn_delta_shim_callbacks_default(apr_pool_t *result_pool)
{
  svn_delta_shim_callbacks_t *shim_callbacks = apr_pcalloc(result_pool,
                                                     sizeof(*shim_callbacks));
  return shim_callbacks;
}

/* To enable editor shims throughout Subversion, ENABLE_EV2_SHIMS should be
 * defined.  This can be done manually, or by providing `--enable-ev2-shims'
 * to `configure'.  */

svn_error_t *
svn_editor__insert_shims(const svn_delta_editor_t **deditor_out,
                         void **dedit_baton_out,
                         const svn_delta_editor_t *deditor_in,
                         void *dedit_baton_in,
                         const char *repos_root,
                         const char *base_relpath,
                         svn_delta_shim_callbacks_t *shim_callbacks,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
#ifndef ENABLE_EV2_SHIMS
  /* Shims disabled, just copy the editor and baton directly. */
  *deditor_out = deditor_in;
  *dedit_baton_out = dedit_baton_in;
#else
  /* Use our shim APIs to create an intermediate svn_editor_t, and then
     wrap that again back into a svn_delta_editor_t.  This introduces
     a lot of overhead. */
  svn_editor_t *editor;

  /* The "extra baton" is a set of functions and a baton which allows the
     shims to communicate additional events to each other.
     svn_delta__editor_from_delta() returns a pointer to this baton, which
     svn_delta__delta_from_editor() should then store. */
  struct svn_delta__extra_baton *exb;

  /* The reason this is a pointer is that we don't know the appropriate
     value until we start receiving paths.  So process_actions() sets the
     flag, which drive_tree() later consumes. */
  svn_boolean_t *found_abs_paths = apr_palloc(result_pool,
                                              sizeof(*found_abs_paths));

  svn_delta__unlock_func_t unlock_func;
  void *unlock_baton;

  SVN_ERR_ASSERT(shim_callbacks->fetch_kind_func != NULL);
  SVN_ERR_ASSERT(shim_callbacks->fetch_props_func != NULL);
  SVN_ERR_ASSERT(shim_callbacks->fetch_base_func != NULL);

  SVN_ERR(svn_delta__editor_from_delta(&editor, &exb,
                            &unlock_func, &unlock_baton,
                            deditor_in, dedit_baton_in,
                            found_abs_paths, repos_root, base_relpath,
                            NULL, NULL,
                            shim_callbacks->fetch_kind_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_delta__delta_from_editor(deditor_out, dedit_baton_out, editor,
                            unlock_func, unlock_baton,
                            found_abs_paths,
                            repos_root, base_relpath,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_base_func,
                            shim_callbacks->fetch_baton,
                            exb, result_pool));

#endif
  return SVN_NO_ERROR;
}
