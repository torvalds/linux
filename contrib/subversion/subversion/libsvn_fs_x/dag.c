/* dag.c : DAG-like interface filesystem
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

#include <string.h>

#include "svn_path.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "dag.h"
#include "fs.h"
#include "fs_x.h"
#include "fs_id.h"
#include "cached_data.h"
#include "transaction.h"

#include "../libsvn_fs/fs-loader.h"

#include "private/svn_fspath.h"
#include "svn_private_config.h"
#include "private/svn_temp_serializer.h"
#include "temp_serializer.h"
#include "dag_cache.h"


/* Initializing a filesystem.  */

struct dag_node_t
{
  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The node's NODE-REVISION. */
  svn_fs_x__noderev_t *node_revision;

  /* The pool to allocate NODE_REVISION in. */
  apr_pool_t *node_pool;

  /* Directory entry lookup hint to speed up consecutive calls to
     svn_fs_x__rep_contents_dir_entry(). Only used for directory nodes.
     Any value is legal but should default to APR_SIZE_MAX. */
  apr_size_t hint;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t
svn_fs_x__dag_node_kind(dag_node_t *node)
{
  return node->node_revision->kind;
}

const svn_fs_x__id_t *
svn_fs_x__dag_get_id(const dag_node_t *node)
{
  return &node->node_revision->noderev_id;
}


const char *
svn_fs_x__dag_get_created_path(dag_node_t *node)
{
  return node->node_revision->created_path;
}


svn_fs_t *
svn_fs_x__dag_get_fs(dag_node_t *node)
{
  return node->fs;
}

void
svn_fs_x__dag_set_fs(dag_node_t *node,
                     svn_fs_t *fs)
{
  node->fs = fs;
}


/* Dup NODEREV and all associated data into RESULT_POOL.
   Leaves the id and is_fresh_txn_root fields as zero bytes. */
static svn_fs_x__noderev_t *
copy_node_revision(svn_fs_x__noderev_t *noderev,
                   apr_pool_t *result_pool)
{
  svn_fs_x__noderev_t *nr = apr_pmemdup(result_pool, noderev,
                                        sizeof(*noderev));

  if (noderev->copyfrom_path)
    nr->copyfrom_path = apr_pstrdup(result_pool, noderev->copyfrom_path);

  nr->copyroot_path = apr_pstrdup(result_pool, noderev->copyroot_path);
  nr->data_rep = svn_fs_x__rep_copy(noderev->data_rep, result_pool);
  nr->prop_rep = svn_fs_x__rep_copy(noderev->prop_rep, result_pool);

  if (noderev->created_path)
    nr->created_path = apr_pstrdup(result_pool, noderev->created_path);

  return nr;
}


const svn_fs_x__id_t *
svn_fs_x__dag_get_node_id(dag_node_t *node)
{
  return &node->node_revision->node_id;
}

const svn_fs_x__id_t *
svn_fs_x__dag_get_copy_id(dag_node_t *node)
{
  return &node->node_revision->copy_id;
}

svn_boolean_t
svn_fs_x__dag_related_node(dag_node_t *lhs,
                           dag_node_t *rhs)
{
  return svn_fs_x__id_eq(&lhs->node_revision->node_id,
                         &rhs->node_revision->node_id);
}

svn_boolean_t
svn_fs_x__dag_same_line_of_history(dag_node_t *lhs,
                                   dag_node_t *rhs)
{
  svn_fs_x__noderev_t *lhs_noderev = lhs->node_revision;
  svn_fs_x__noderev_t *rhs_noderev = rhs->node_revision;

  return svn_fs_x__id_eq(&lhs_noderev->node_id, &rhs_noderev->node_id)
      && svn_fs_x__id_eq(&lhs_noderev->copy_id, &rhs_noderev->copy_id);
}

svn_boolean_t
svn_fs_x__dag_check_mutable(const dag_node_t *node)
{
  return svn_fs_x__is_txn(svn_fs_x__dag_get_id(node)->change_set);
}

svn_error_t *
svn_fs_x__dag_get_node(dag_node_t **node,
                       svn_fs_t *fs,
                       const svn_fs_x__id_t *id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  dag_node_t *new_node;
  svn_fs_x__noderev_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc(result_pool, sizeof(*new_node));
  new_node->fs = fs;
  new_node->hint = APR_SIZE_MAX;

  /* Grab the contents so we can inspect the node's kind and created path. */
  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, id,
                                      result_pool, scratch_pool));
  new_node->node_pool = result_pool;
  new_node->node_revision = noderev;

  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_revnum_t
svn_fs_x__dag_get_revision(const dag_node_t *node)
{
  svn_fs_x__noderev_t *noderev = node->node_revision;
  return (  svn_fs_x__is_fresh_txn_root(noderev)
          ? svn_fs_x__get_revnum(noderev->predecessor_id.change_set)
          : svn_fs_x__get_revnum(noderev->noderev_id.change_set));
}

const svn_fs_x__id_t *
svn_fs_x__dag_get_predecessor_id(dag_node_t *node)
{
  return &node->node_revision->predecessor_id;
}

int
svn_fs_x__dag_get_predecessor_count(dag_node_t *node)
{
  return node->node_revision->predecessor_count;
}

apr_int64_t
svn_fs_x__dag_get_mergeinfo_count(dag_node_t *node)
{
  return node->node_revision->mergeinfo_count;
}

svn_boolean_t
svn_fs_x__dag_has_mergeinfo(dag_node_t *node)
{
  return node->node_revision->has_mergeinfo;
}

svn_boolean_t
svn_fs_x__dag_has_descendants_with_mergeinfo(dag_node_t *node)
{
  svn_fs_x__noderev_t *noderev = node->node_revision;

  if (noderev->kind != svn_node_dir)
      return FALSE;

  if (noderev->mergeinfo_count > 1)
    return TRUE;
  else if (noderev->mergeinfo_count == 1 && !noderev->has_mergeinfo)
    return TRUE;

  return FALSE;
}


/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Set *ID_P to the noderev-id for entry NAME in PARENT.  If no such
   entry, set *ID_P to NULL but do not error. */
svn_error_t *
svn_fs_x__dir_entry_id(svn_fs_x__id_t *id_p,
                       dag_node_t *parent,
                       const char *name,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__dirent_t *dirent;
  svn_fs_x__noderev_t *noderev = parent->node_revision;

  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to open node with an illegal name '%s'", name);

  /* Get a dirent hash for this directory. */
  SVN_ERR(svn_fs_x__rep_contents_dir_entry(&dirent, parent->fs, noderev,
                                           name, &parent->hint,
                                           scratch_pool, scratch_pool));
  if (dirent)
    *id_p = dirent->id;
  else
    svn_fs_x__id_reset(id_p);

  return SVN_NO_ERROR;
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Temporary allocations are done in SCRATCH_POOL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry(dag_node_t *parent,
          const char *name,
          const svn_fs_x__id_t *id,
          svn_node_kind_t kind,
          svn_fs_x__txn_id_t txn_id,
          apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *parent_noderev = parent->node_revision;

  /* Set the new entry. */
  SVN_ERR(svn_fs_x__set_entry(parent->fs, txn_id, parent_noderev, name, id,
                              kind, parent->node_pool, scratch_pool));

  /* Update cached data. */
  svn_fs_x__update_dag_cache(parent);

  return SVN_NO_ERROR;
}


/* Make a new entry named NAME in PARENT.  If IS_DIR is true, then the
   node revision the new entry points to will be a directory, else it
   will be a file.  The new node will be allocated in RESULT_POOL.  PARENT
   must be mutable, and must not have an entry named NAME.

   Use SCRATCH_POOL for all temporary allocations.
 */
static svn_error_t *
make_entry(dag_node_t **child_p,
           dag_node_t *parent,
           const char *parent_path,
           const char *name,
           svn_boolean_t is_dir,
           svn_fs_x__txn_id_t txn_id,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t new_noderev;
  svn_fs_x__noderev_t *parent_noderev = parent->node_revision;

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to create a node with an illegal name '%s'"), name);

  /* Make sure that parent is a directory */
  if (parent_noderev->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to create entry in non-directory parent"));

  /* Check that the parent is mutable. */
  if (! svn_fs_x__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Create the new node's NODE-REVISION */
  memset(&new_noderev, 0, sizeof(new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  new_noderev.created_path = svn_fspath__join(parent_path, name, result_pool);

  new_noderev.copyroot_path = apr_pstrdup(result_pool,
                                          parent_noderev->copyroot_path);
  new_noderev.copyroot_rev = parent_noderev->copyroot_rev;
  new_noderev.copyfrom_rev = SVN_INVALID_REVNUM;
  new_noderev.copyfrom_path = NULL;
  svn_fs_x__id_reset(&new_noderev.predecessor_id);

  SVN_ERR(svn_fs_x__create_node
          (svn_fs_x__dag_get_fs(parent), &new_noderev,
           &parent_noderev->copy_id, txn_id, scratch_pool));

  /* Create a new dag_node_t for our new node */
  SVN_ERR(svn_fs_x__dag_get_node(child_p, svn_fs_x__dag_get_fs(parent),
                                 &new_noderev.noderev_id, result_pool,
                                 scratch_pool));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  return set_entry(parent, name, &new_noderev.noderev_id,
                   new_noderev.kind, txn_id, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_dir_entries(apr_array_header_t **entries,
                          dag_node_t *node,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev = node->node_revision;

  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  return svn_fs_x__rep_contents_dir(entries, node->fs, noderev, result_pool,
                                    scratch_pool);
}


svn_error_t *
svn_fs_x__dag_set_entry(dag_node_t *node,
                        const char *entry_name,
                        const svn_fs_x__id_t *id,
                        svn_node_kind_t kind,
                        svn_fs_x__txn_id_t txn_id,
                        apr_pool_t *scratch_pool)
{
  /* Check it's a directory. */
  if (node->node_revision->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to set entry in non-directory node"));

  /* Check it's mutable. */
  if (! svn_fs_x__dag_check_mutable(node))
    return svn_error_create
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set entry in immutable node"));

  return set_entry(node, entry_name, id, kind, txn_id, scratch_pool);
}



/*** Proplists. ***/

svn_error_t *
svn_fs_x__dag_get_proplist(apr_hash_t **proplist_p,
                           dag_node_t *node,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_fs_x__get_proplist(proplist_p, node->fs, node->node_revision,
                                 result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__dag_set_proplist(dag_node_t *node,
                           apr_hash_t *proplist,
                           apr_pool_t *scratch_pool)
{
  /* Sanity check: this node better be mutable! */
  if (! svn_fs_x__dag_check_mutable(node))
    {
      svn_string_t *idstr
        = svn_fs_x__id_unparse(&node->node_revision->noderev_id,
                               scratch_pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't set proplist on *immutable* node-revision %s",
         idstr->data);
    }

  /* Set the new proplist. */
  SVN_ERR(svn_fs_x__set_proplist(node->fs, node->node_revision, proplist,
                                 scratch_pool));
  svn_fs_x__update_dag_cache(node);

  return SVN_NO_ERROR;
}

/* Write NODE's NODEREV element to disk.  Update the DAG cache.
   Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
noderev_changed(dag_node_t *node,
                apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_fs_x__put_node_revision(node->fs, node->node_revision,
                                      scratch_pool));
  svn_fs_x__update_dag_cache(node);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__dag_increment_mergeinfo_count(dag_node_t *node,
                                        apr_int64_t increment,
                                        apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev = node->node_revision;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_x__dag_check_mutable(node))
    {
      svn_string_t *idstr = svn_fs_x__id_unparse(&noderev->noderev_id,
                                                 scratch_pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't increment mergeinfo count on *immutable* node-revision %s",
         idstr->data);
    }

  if (increment == 0)
    return SVN_NO_ERROR;

  noderev->mergeinfo_count += increment;
  if (noderev->mergeinfo_count < 0)
    {
      svn_string_t *idstr = svn_fs_x__id_unparse(&noderev->noderev_id,
                                                 scratch_pool);
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, NULL,
         apr_psprintf(scratch_pool,
                      _("Can't increment mergeinfo count on node-revision %%s "
                        "to negative value %%%s"),
                      APR_INT64_T_FMT),
         idstr->data, noderev->mergeinfo_count);
    }
  if (noderev->mergeinfo_count > 1 && noderev->kind == svn_node_file)
    {
      svn_string_t *idstr = svn_fs_x__id_unparse(&noderev->noderev_id,
                                                 scratch_pool);
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, NULL,
         apr_psprintf(scratch_pool,
                      _("Can't increment mergeinfo count on *file* "
                        "node-revision %%s to %%%s (> 1)"),
                      APR_INT64_T_FMT),
         idstr->data, noderev->mergeinfo_count);
    }

  /* Flush it out. */
  return noderev_changed(node, scratch_pool);
}

svn_error_t *
svn_fs_x__dag_set_has_mergeinfo(dag_node_t *node,
                                svn_boolean_t has_mergeinfo,
                                apr_pool_t *scratch_pool)
{
  /* Sanity check: this node better be mutable! */
  if (! svn_fs_x__dag_check_mutable(node))
    {
      svn_string_t *idstr
        = svn_fs_x__id_unparse(&node->node_revision->noderev_id,
                               scratch_pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't set mergeinfo flag on *immutable* node-revision %s",
         idstr->data);
    }

  node->node_revision->has_mergeinfo = has_mergeinfo;

  /* Flush it out. */
  return noderev_changed(node, scratch_pool);
}


/*** Roots. ***/

svn_error_t *
svn_fs_x__dag_root(dag_node_t **node_p,
                   svn_fs_t *fs,
                   svn_fs_x__change_set_t change_set,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__id_t root_id;
  root_id.change_set = change_set;
  root_id.number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;

  return svn_fs_x__dag_get_node(node_p, fs, &root_id, result_pool,
                                scratch_pool);
}


svn_error_t *
svn_fs_x__dag_clone_child(dag_node_t **child_p,
                          dag_node_t *parent,
                          const char *parent_path,
                          const char *name,
                          const svn_fs_x__id_t *copy_id,
                          svn_fs_x__txn_id_t txn_id,
                          svn_boolean_t is_parent_copyroot,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_x__id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs_x__dag_get_fs(parent);

  /* First check that the parent is mutable. */
  if (! svn_fs_x__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to clone child of non-mutable node");

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to make a child clone with an illegal name '%s'", name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR(svn_fs_x__dag_open(&cur_entry, parent, name, scratch_pool,
                             scratch_pool));
  if (! cur_entry)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       "Attempted to open non-existent child node '%s'", name);

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs_x__dag_check_mutable(cur_entry))
    {
      /* This has already been cloned */
      new_node_id = svn_fs_x__dag_get_id(cur_entry);
    }
  else
    {
      svn_fs_x__noderev_t *noderev = cur_entry->node_revision;

      if (is_parent_copyroot)
        {
          svn_fs_x__noderev_t *parent_noderev = parent->node_revision;
          noderev->copyroot_rev = parent_noderev->copyroot_rev;
          noderev->copyroot_path = apr_pstrdup(scratch_pool,
                                               parent_noderev->copyroot_path);
        }

      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;

      noderev->predecessor_id = noderev->noderev_id;
      noderev->predecessor_count++;
      noderev->created_path = svn_fspath__join(parent_path, name,
                                               scratch_pool);

      if (copy_id == NULL)
        copy_id = &noderev->copy_id;

      SVN_ERR(svn_fs_x__create_successor(fs, noderev, copy_id, txn_id,
                                         scratch_pool));
      new_node_id = &noderev->noderev_id;

      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR(set_entry(parent, name, new_node_id, noderev->kind, txn_id,
                        scratch_pool));
    }

  /* Initialize the youngster. */
  return svn_fs_x__dag_get_node(child_p, fs, new_node_id, result_pool,
                                scratch_pool);
}


/* Delete all mutable node revisions reachable from node ID, including
   ID itself, from FS's `nodes' table.  Also delete any mutable
   representations and strings associated with that node revision.
   ID may refer to a file or directory, which may be mutable or immutable.

   Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
delete_if_mutable(svn_fs_t *fs,
                  const svn_fs_x__id_t *id,
                  apr_pool_t *scratch_pool)
{
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_x__dag_get_node(&node, fs, id, scratch_pool, scratch_pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_x__dag_check_mutable(node))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (node->node_revision->kind == svn_node_dir)
    {
      apr_array_header_t *entries;
      int i;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      /* Loop over directory entries */
      SVN_ERR(svn_fs_x__dag_dir_entries(&entries, node, scratch_pool,
                                        iterpool));
      for (i = 0; i < entries->nelts; ++i)
        {
          const svn_fs_x__id_t *noderev_id
            = &APR_ARRAY_IDX(entries, i, svn_fs_x__dirent_t *)->id;

          svn_pool_clear(iterpool);
          SVN_ERR(delete_if_mutable(fs, noderev_id, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */
  return svn_fs_x__delete_node_revision(fs, id, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_delete(dag_node_t *parent,
                     const char *name,
                     svn_fs_x__txn_id_t txn_id,
                     apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *parent_noderev = parent->node_revision;
  svn_fs_t *fs = parent->fs;
  svn_fs_x__dirent_t *dirent;
  apr_pool_t *subpool;

  /* Make sure parent is a directory. */
  if (parent_noderev->kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       "Attempted to delete entry '%s' from *non*-directory node", name);

  /* Make sure parent is mutable. */
  if (! svn_fs_x__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to delete entry '%s' from immutable directory node", name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to delete a node with an illegal name '%s'", name);

  /* We allocate a few potentially heavy temporary objects (file buffers
     and directories).  Make sure we don't keep them around for longer
     than necessary. */
  subpool = svn_pool_create(scratch_pool);

  /* Search this directory for a dirent with that NAME. */
  SVN_ERR(svn_fs_x__rep_contents_dir_entry(&dirent, fs, parent_noderev,
                                           name, &parent->hint,
                                           subpool, subpool));

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! dirent)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       "Delete failed--directory has no entry '%s'", name);

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR(delete_if_mutable(parent->fs, &dirent->id, subpool));

  /* Remove this entry from its parent's entries list. */
  SVN_ERR(set_entry(parent, name, NULL, svn_node_unknown, txn_id, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__dag_make_file(dag_node_t **child_p,
                        dag_node_t *parent,
                        const char *parent_path,
                        const char *name,
                        svn_fs_x__txn_id_t txn_id,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, FALSE, txn_id,
                    result_pool, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_make_dir(dag_node_t **child_p,
                       dag_node_t *parent,
                       const char *parent_path,
                       const char *name,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, TRUE, txn_id,
                    result_pool, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_get_contents(svn_stream_t **contents_p,
                           dag_node_t *file,
                           apr_pool_t *result_pool)
{
  /* Make sure our node is a file. */
  if (file->node_revision->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");

  /* Get a stream to the contents. */
  SVN_ERR(svn_fs_x__get_contents(contents_p, file->fs,
                                 file->node_revision->data_rep, TRUE,
                                 result_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__dag_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                    dag_node_t *source,
                                    dag_node_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *src_noderev = source ? source->node_revision : NULL;
  svn_fs_x__noderev_t *tgt_noderev = target->node_revision;

  /* Make sure our nodes are files. */
  if ((source && src_noderev->kind != svn_node_file)
      || tgt_noderev->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");

  /* Get the delta stream. */
  return svn_fs_x__get_file_delta_stream(stream_p, target->fs,
                                         src_noderev, tgt_noderev,
                                         result_pool, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_try_process_file_contents(svn_boolean_t *success,
                                        dag_node_t *node,
                                        svn_fs_process_contents_func_t processor,
                                        void* baton,
                                        apr_pool_t *scratch_pool)
{
  return svn_fs_x__try_process_file_contents(success, node->fs,
                                             node->node_revision,
                                             processor, baton, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_file_length(svn_filesize_t *length,
                          dag_node_t *file)
{
  /* Make sure our node is a file. */
  if (file->node_revision->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get length of a *non*-file node");

  return svn_fs_x__file_length(length, file->node_revision);
}


svn_error_t *
svn_fs_x__dag_file_checksum(svn_checksum_t **checksum,
                            dag_node_t *file,
                            svn_checksum_kind_t kind,
                            apr_pool_t *result_pool)
{
  if (file->node_revision->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get checksum of a *non*-file node");

  return svn_fs_x__file_checksum(checksum, file->node_revision, kind,
                                 result_pool);
}


svn_error_t *
svn_fs_x__dag_get_edit_stream(svn_stream_t **contents,
                              dag_node_t *file,
                              apr_pool_t *result_pool)
{
  /* Make sure our node is a file. */
  if (file->node_revision->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to set textual contents of a *non*-file node");

  /* Make sure our node is mutable. */
  if (! svn_fs_x__dag_check_mutable(file))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to set textual contents of an immutable node");

  SVN_ERR(svn_fs_x__set_contents(contents, file->fs, file->node_revision,
                                 result_pool));
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_x__dag_finalize_edits(dag_node_t *file,
                             const svn_checksum_t *checksum,
                             apr_pool_t *scratch_pool)
{
  if (checksum)
    {
      svn_checksum_t *file_checksum;

      SVN_ERR(svn_fs_x__dag_file_checksum(&file_checksum, file,
                                          checksum->kind, scratch_pool));
      if (!svn_checksum_match(checksum, file_checksum))
        return svn_checksum_mismatch_err(checksum, file_checksum,
                                         scratch_pool,
                                         _("Checksum mismatch for '%s'"),
                                         file->node_revision->created_path);
    }

  svn_fs_x__update_dag_cache(file);
  return SVN_NO_ERROR;
}


dag_node_t *
svn_fs_x__dag_dup(const dag_node_t *node,
                  apr_pool_t *result_pool)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pmemdup(result_pool, node, sizeof(*new_node));

  /* Copy sub-structures. */
  new_node->node_revision = copy_node_revision(node->node_revision,
                                               result_pool);
  new_node->node_pool = result_pool;

  return new_node;
}


svn_error_t *
svn_fs_x__dag_open(dag_node_t **child_p,
                   dag_node_t *parent,
                   const char *name,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__id_t node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR(svn_fs_x__dir_entry_id(&node_id, parent, name, scratch_pool));
  if (! svn_fs_x__id_used(&node_id))
    {
      *child_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Now get the node that was requested. */
  return svn_fs_x__dag_get_node(child_p, svn_fs_x__dag_get_fs(parent),
                                &node_id, result_pool, scratch_pool);
}


svn_error_t *
svn_fs_x__dag_copy(dag_node_t *to_node,
                   const char *entry,
                   dag_node_t *from_node,
                   svn_boolean_t preserve_history,
                   svn_revnum_t from_rev,
                   const char *from_path,
                   svn_fs_x__txn_id_t txn_id,
                   apr_pool_t *scratch_pool)
{
  const svn_fs_x__id_t *id;

  if (preserve_history)
    {
      svn_fs_x__noderev_t *to_noderev;
      svn_fs_x__id_t copy_id;
      svn_fs_t *fs = svn_fs_x__dag_get_fs(from_node);

      /* Make a copy of the original node revision. */
      to_noderev = copy_node_revision(from_node->node_revision, scratch_pool);

      /* Reserve a copy ID for this new copy. */
      SVN_ERR(svn_fs_x__reserve_copy_id(&copy_id, fs, txn_id, scratch_pool));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      to_noderev->predecessor_id = to_noderev->noderev_id;
      to_noderev->predecessor_count++;
      to_noderev->created_path =
        svn_fspath__join(svn_fs_x__dag_get_created_path(to_node), entry,
                         scratch_pool);
      to_noderev->copyfrom_path = apr_pstrdup(scratch_pool, from_path);
      to_noderev->copyfrom_rev = from_rev;

      /* Set the copyroot equal to our own id. */
      to_noderev->copyroot_path = NULL;

      SVN_ERR(svn_fs_x__create_successor(fs, to_noderev,
                                         &copy_id, txn_id, scratch_pool));
      id = &to_noderev->noderev_id;
    }
  else  /* don't preserve history */
    {
      id = svn_fs_x__dag_get_id(from_node);
    }

  /* Set the entry in to_node to the new id. */
  return svn_fs_x__dag_set_entry(to_node, entry, id,
                                 from_node->node_revision->kind,
                                 txn_id, scratch_pool);
}



/*** Comparison. ***/

svn_error_t *
svn_fs_x__dag_things_different(svn_boolean_t *props_changed,
                               svn_boolean_t *contents_changed,
                               dag_node_t *node1,
                               dag_node_t *node2,
                               svn_boolean_t strict,
                               apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev1 = node1->node_revision;
  svn_fs_x__noderev_t *noderev2 = node2->node_revision;
  svn_fs_t *fs;
  svn_boolean_t same;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  fs = svn_fs_x__dag_get_fs(node1);

  /* Compare property keys. */
  if (props_changed != NULL)
    {
      SVN_ERR(svn_fs_x__prop_rep_equal(&same, fs, noderev1, noderev2,
                                       strict, scratch_pool));
      *props_changed = !same;
    }

  /* Compare contents keys. */
  if (contents_changed != NULL)
    *contents_changed = !svn_fs_x__file_text_rep_equal(noderev1->data_rep,
                                                       noderev2->data_rep);

  return SVN_NO_ERROR;
}

void
svn_fs_x__dag_get_copyroot(svn_revnum_t *rev,
                           const char **path,
                           dag_node_t *node)
{
  *rev = node->node_revision->copyroot_rev;
  *path = node->node_revision->copyroot_path;
}

svn_revnum_t
svn_fs_x__dag_get_copyfrom_rev(dag_node_t *node)
{
  return node->node_revision->copyfrom_rev;
}

const char *
svn_fs_x__dag_get_copyfrom_path(dag_node_t *node)
{
  return node->node_revision->copyfrom_path;
}

svn_error_t *
svn_fs_x__dag_update_ancestry(dag_node_t *target,
                              dag_node_t *source,
                              apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *source_noderev = source->node_revision;
  svn_fs_x__noderev_t *target_noderev = target->node_revision;

  if (! svn_fs_x__dag_check_mutable(target))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to update ancestry of non-mutable node"));

  target_noderev->predecessor_id = source_noderev->noderev_id;
  target_noderev->predecessor_count = source_noderev->predecessor_count;
  target_noderev->predecessor_count++;

  return noderev_changed(target, scratch_pool);
}
