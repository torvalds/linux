/* dag.c : DAG-like interface filesystem, private to libsvn_fs
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

#include "cached_data.h"
#include "dag.h"
#include "fs.h"
#include "fs_fs.h"
#include "id.h"
#include "transaction.h"

#include "../libsvn_fs/fs-loader.h"

#include "private/svn_fspath.h"
#include "svn_private_config.h"
#include "private/svn_temp_serializer.h"
#include "temp_serializer.h"


/* Initializing a filesystem.  */

struct dag_node_t
{
  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The node revision ID for this dag node, allocated in POOL.  */
  svn_fs_id_t *id;

  /* In the special case that this node is the root of a transaction
     that has not yet been modified, the node revision ID for this dag
     node's predecessor; otherwise NULL. (Used in
     svn_fs_node_created_rev.) */
  const svn_fs_id_t *fresh_root_predecessor_id;

  /* The node's type (file, dir, etc.) */
  svn_node_kind_t kind;

  /* The node's NODE-REVISION, or NULL if we haven't read it in yet.
     This is allocated in this node's POOL.

     If you're willing to respect all the rules above, you can munge
     this yourself, but you're probably better off just calling
     `get_node_revision' and `set_node_revision', which take care of
     things for you.  */
  node_revision_t *node_revision;

  /* The pool to allocate NODE_REVISION in. */
  apr_pool_t *node_pool;

  /* the path at which this node was created. */
  const char *created_path;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs_fs__dag_node_kind(dag_node_t *node)
{
  return node->kind;
}


const svn_fs_id_t *
svn_fs_fs__dag_get_id(const dag_node_t *node)
{
  return node->id;
}


const char *
svn_fs_fs__dag_get_created_path(dag_node_t *node)
{
  return node->created_path;
}


svn_fs_t *
svn_fs_fs__dag_get_fs(dag_node_t *node)
{
  return node->fs;
}

void
svn_fs_fs__dag_set_fs(dag_node_t *node, svn_fs_t *fs)
{
  node->fs = fs;
}


/* Dup NODEREV and all associated data into POOL.
   Leaves the id and is_fresh_txn_root fields as zero bytes. */
static node_revision_t *
copy_node_revision(node_revision_t *noderev,
                   apr_pool_t *pool)
{
  node_revision_t *nr = apr_pcalloc(pool, sizeof(*nr));
  nr->kind = noderev->kind;
  if (noderev->predecessor_id)
    nr->predecessor_id = svn_fs_fs__id_copy(noderev->predecessor_id, pool);
  nr->predecessor_count = noderev->predecessor_count;
  if (noderev->copyfrom_path)
    nr->copyfrom_path = apr_pstrdup(pool, noderev->copyfrom_path);
  nr->copyfrom_rev = noderev->copyfrom_rev;
  nr->copyroot_path = apr_pstrdup(pool, noderev->copyroot_path);
  nr->copyroot_rev = noderev->copyroot_rev;
  nr->data_rep = svn_fs_fs__rep_copy(noderev->data_rep, pool);
  nr->prop_rep = svn_fs_fs__rep_copy(noderev->prop_rep, pool);
  nr->mergeinfo_count = noderev->mergeinfo_count;
  nr->has_mergeinfo = noderev->has_mergeinfo;

  if (noderev->created_path)
    nr->created_path = apr_pstrdup(pool, noderev->created_path);
  return nr;
}


/* Set *NODEREV_P to the cached node-revision for NODE.
   If the node-revision was not already cached in NODE, read it in,
   allocating the cache in NODE->NODE_POOL.

   If you plan to change the contents of NODE, be careful!  We're
   handing you a pointer directly to our cached node-revision, not
   your own copy.  If you change it as part of some operation, but
   then some Berkeley DB function deadlocks or gets an error, you'll
   need to back out your changes, or else the cache will reflect
   changes that never got committed.  It's probably best not to change
   the structure at all.  */
static svn_error_t *
get_node_revision(node_revision_t **noderev_p,
                  dag_node_t *node)
{
  /* If we've already got a copy, there's no need to read it in.  */
  if (! node->node_revision)
    {
      node_revision_t *noderev;
      apr_pool_t *scratch_pool = svn_pool_create(node->node_pool);

      SVN_ERR(svn_fs_fs__get_node_revision(&noderev, node->fs,
                                           node->id, node->node_pool,
                                           scratch_pool));
      node->node_revision = noderev;
      svn_pool_destroy(scratch_pool);
    }

  /* Now NODE->node_revision is set.  */
  *noderev_p = node->node_revision;
  return SVN_NO_ERROR;
}


svn_boolean_t svn_fs_fs__dag_check_mutable(const dag_node_t *node)
{
  return svn_fs_fs__id_is_txn(svn_fs_fs__dag_get_id(node));
}


svn_error_t *
svn_fs_fs__dag_get_node(dag_node_t **node,
                        svn_fs_t *fs,
                        const svn_fs_id_t *id,
                        apr_pool_t *pool)
{
  dag_node_t *new_node;
  node_revision_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc(pool, sizeof(*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs_fs__id_copy(id, pool);

  /* Grab the contents so we can inspect the node's kind and created path. */
  new_node->node_pool = pool;
  SVN_ERR(get_node_revision(&noderev, new_node));

  /* Initialize the KIND and CREATED_PATH attributes */
  new_node->kind = noderev->kind;
  new_node->created_path = apr_pstrdup(pool, noderev->created_path);

  if (noderev->is_fresh_txn_root)
    new_node->fresh_root_predecessor_id = noderev->predecessor_id;
  else
    new_node->fresh_root_predecessor_id = NULL;

  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_revision(svn_revnum_t *rev,
                            dag_node_t *node,
                            apr_pool_t *pool)
{
  /* In the special case that this is an unmodified transaction root,
     we need to actually get the revision of the noderev's predecessor
     (the revision root); see Issue #2608. */
  const svn_fs_id_t *correct_id = node->fresh_root_predecessor_id
    ? node->fresh_root_predecessor_id : node->id;

  /* Look up the committed revision from the Node-ID. */
  *rev = svn_fs_fs__id_rev(correct_id);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                  dag_node_t *node)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));
  *id_p = noderev->predecessor_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_predecessor_count(int *count,
                                     dag_node_t *node)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));
  *count = noderev->predecessor_count;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_mergeinfo_count(apr_int64_t *count,
                                   dag_node_t *node)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));
  *count = noderev->mergeinfo_count;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_has_mergeinfo(svn_boolean_t *has_mergeinfo,
                             dag_node_t *node)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));
  *has_mergeinfo = noderev->has_mergeinfo;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_has_descendants_with_mergeinfo(svn_boolean_t *do_they,
                                              dag_node_t *node)
{
  node_revision_t *noderev;

  if (node->kind != svn_node_dir)
    {
      *do_they = FALSE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_node_revision(&noderev, node));
  if (noderev->mergeinfo_count > 1)
    *do_they = TRUE;
  else if (noderev->mergeinfo_count == 1 && !noderev->has_mergeinfo)
    *do_they = TRUE;
  else
    *do_they = FALSE;
  return SVN_NO_ERROR;
}


/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Set *ID_P to the node-id for entry NAME in PARENT.  If no such
   entry, set *ID_P to NULL but do not error.  The node-id is
   allocated in POOL. */
static svn_error_t *
dir_entry_id_from_node(const svn_fs_id_t **id_p,
                       dag_node_t *parent,
                       const char *name,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_fs_dirent_t *dirent;

  SVN_ERR(svn_fs_fs__dag_dir_entry(&dirent, parent, name, result_pool,
                                   scratch_pool));
  *id_p = dirent ? dirent->id : NULL;

  return SVN_NO_ERROR;
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Allocations are done in POOL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry(dag_node_t *parent,
          const char *name,
          const svn_fs_id_t *id,
          svn_node_kind_t kind,
          const svn_fs_fs__id_part_t *txn_id,
          apr_pool_t *pool)
{
  node_revision_t *parent_noderev;

  /* Get the parent's node-revision. */
  SVN_ERR(get_node_revision(&parent_noderev, parent));

  /* Set the new entry. */
  return svn_fs_fs__set_entry(parent->fs, txn_id, parent_noderev, name, id,
                              kind, pool);
}


/* Make a new entry named NAME in PARENT.  If IS_DIR is true, then the
   node revision the new entry points to will be a directory, else it
   will be a file.  The new node will be allocated in POOL.  PARENT
   must be mutable, and must not have an entry named NAME.

   Use POOL for all allocations, except caching the node_revision in PARENT.
 */
static svn_error_t *
make_entry(dag_node_t **child_p,
           dag_node_t *parent,
           const char *parent_path,
           const char *name,
           svn_boolean_t is_dir,
           const svn_fs_fs__id_part_t *txn_id,
           apr_pool_t *pool)
{
  const svn_fs_id_t *new_node_id;
  node_revision_t new_noderev, *parent_noderev;

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to create a node with an illegal name '%s'"), name);

  /* Make sure that parent is a directory */
  if (parent->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to create entry in non-directory parent"));

  /* Check that the parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Create the new node's NODE-REVISION */
  memset(&new_noderev, 0, sizeof(new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  new_noderev.created_path = svn_fspath__join(parent_path, name, pool);

  SVN_ERR(get_node_revision(&parent_noderev, parent));
  new_noderev.copyroot_path = apr_pstrdup(pool,
                                          parent_noderev->copyroot_path);
  new_noderev.copyroot_rev = parent_noderev->copyroot_rev;
  new_noderev.copyfrom_rev = SVN_INVALID_REVNUM;
  new_noderev.copyfrom_path = NULL;

  SVN_ERR(svn_fs_fs__create_node
          (&new_node_id, svn_fs_fs__dag_get_fs(parent), &new_noderev,
           svn_fs_fs__id_copy_id(svn_fs_fs__dag_get_id(parent)),
           txn_id, pool));

  /* Create a new dag_node_t for our new node */
  SVN_ERR(svn_fs_fs__dag_get_node(child_p, svn_fs_fs__dag_get_fs(parent),
                                  new_node_id, pool));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  return set_entry(parent, name, svn_fs_fs__dag_get_id(*child_p),
                   new_noderev.kind, txn_id, pool);
}


svn_error_t *
svn_fs_fs__dag_dir_entries(apr_array_header_t **entries,
                           dag_node_t *node,
                           apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));

  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  return svn_fs_fs__rep_contents_dir(entries, node->fs, noderev, pool, pool);
}

svn_error_t *
svn_fs_fs__dag_dir_entry(svn_fs_dirent_t **dirent,
                         dag_node_t *node,
                         const char* name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  node_revision_t *noderev;
  SVN_ERR(get_node_revision(&noderev, node));

  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  /* Get a dirent hash for this directory. */
  return svn_fs_fs__rep_contents_dir_entry(dirent, node->fs, noderev, name,
                                           result_pool, scratch_pool);
}


svn_error_t *
svn_fs_fs__dag_set_entry(dag_node_t *node,
                         const char *entry_name,
                         const svn_fs_id_t *id,
                         svn_node_kind_t kind,
                         const svn_fs_fs__id_part_t *txn_id,
                         apr_pool_t *pool)
{
  /* Check it's a directory. */
  if (node->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to set entry in non-directory node"));

  /* Check it's mutable. */
  if (! svn_fs_fs__dag_check_mutable(node))
    return svn_error_create
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set entry in immutable node"));

  return set_entry(node, entry_name, id, kind, txn_id, pool);
}



/*** Proplists. ***/

svn_error_t *
svn_fs_fs__dag_get_proplist(apr_hash_t **proplist_p,
                            dag_node_t *node,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_hash_t *proplist = NULL;

  SVN_ERR(get_node_revision(&noderev, node));

  SVN_ERR(svn_fs_fs__get_proplist(&proplist, node->fs,
                                  noderev, pool));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_has_props(svn_boolean_t *has_props,
                         dag_node_t *node,
                         apr_pool_t *scratch_pool)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node));

  if (! noderev->prop_rep)
    {
      *has_props = FALSE; /* Easy out */
      return SVN_NO_ERROR;
    }

  if (svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    {
      /* We are in a commit or something. Check actual properties */
      apr_hash_t *proplist;

      SVN_ERR(svn_fs_fs__get_proplist(&proplist, node->fs,
                                      noderev, scratch_pool));

      *has_props = proplist ? (0 < apr_hash_count(proplist)) : FALSE;
    }
  else
    {
      /* Properties are stored as a standard hash stream,
         always ending with "END\n" (4 bytes) */
      *has_props = noderev->prop_rep->expanded_size > 4;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_set_proplist(dag_node_t *node,
                            apr_hash_t *proplist,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_fs__dag_check_mutable(node))
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't set proplist on *immutable* node-revision %s",
         idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(get_node_revision(&noderev, node));

  /* Set the new proplist. */
  return svn_fs_fs__set_proplist(node->fs, noderev, proplist, pool);
}


svn_error_t *
svn_fs_fs__dag_increment_mergeinfo_count(dag_node_t *node,
                                         apr_int64_t increment,
                                         apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_fs__dag_check_mutable(node))
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't increment mergeinfo count on *immutable* node-revision %s",
         idstr->data);
    }

  if (increment == 0)
    return SVN_NO_ERROR;

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(get_node_revision(&noderev, node));

  noderev->mergeinfo_count += increment;
  if (noderev->mergeinfo_count < 0)
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, NULL,
         apr_psprintf(pool,
                      _("Can't increment mergeinfo count on node-revision %%s "
                        "to negative value %%%s"),
                      APR_INT64_T_FMT),
         idstr->data, noderev->mergeinfo_count);
    }
  if (noderev->mergeinfo_count > 1 && noderev->kind == svn_node_file)
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, NULL,
         apr_psprintf(pool,
                      _("Can't increment mergeinfo count on *file* "
                        "node-revision %%s to %%%s (> 1)"),
                      APR_INT64_T_FMT),
         idstr->data, noderev->mergeinfo_count);
    }

  /* Flush it out. */
  return svn_fs_fs__put_node_revision(node->fs, noderev->id,
                                      noderev, FALSE, pool);
}

svn_error_t *
svn_fs_fs__dag_set_has_mergeinfo(dag_node_t *node,
                                 svn_boolean_t has_mergeinfo,
                                 apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_fs__dag_check_mutable(node))
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't set mergeinfo flag on *immutable* node-revision %s",
         idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(get_node_revision(&noderev, node));

  noderev->has_mergeinfo = has_mergeinfo;

  /* Flush it out. */
  return svn_fs_fs__put_node_revision(node->fs, noderev->id,
                                      noderev, FALSE, pool);
}


/*** Roots. ***/

svn_error_t *
svn_fs_fs__dag_revision_root(dag_node_t **node_p,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  dag_node_t *new_node;

  /* Construct the node. */
  new_node = apr_pcalloc(pool, sizeof(*new_node));
  new_node->fs = fs;
  SVN_ERR(svn_fs_fs__rev_get_root(&new_node->id, fs, rev, pool, pool));

  /* Grab the contents so we can inspect the node's kind and created path. */
  new_node->node_pool = pool;

  /* Initialize the KIND and CREATED_PATH attributes */
  new_node->kind = svn_node_dir;
  new_node->created_path = "/";
  new_node->fresh_root_predecessor_id = NULL;

  /* Return a fresh new node */
  *node_p = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_txn_root(dag_node_t **node_p,
                        svn_fs_t *fs,
                        const svn_fs_fs__id_part_t *txn_id,
                        apr_pool_t *pool)
{
  const svn_fs_id_t *root_id, *ignored;

  SVN_ERR(svn_fs_fs__get_txn_ids(&root_id, &ignored, fs, txn_id, pool));
  return svn_fs_fs__dag_get_node(node_p, fs, root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_txn_base_root(dag_node_t **node_p,
                             svn_fs_t *fs,
                             const svn_fs_fs__id_part_t *txn_id,
                             apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *ignored;

  SVN_ERR(svn_fs_fs__get_txn_ids(&ignored, &base_root_id, fs, txn_id, pool));
  return svn_fs_fs__dag_get_node(node_p, fs, base_root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_clone_child(dag_node_t **child_p,
                           dag_node_t *parent,
                           const char *parent_path,
                           const char *name,
                           const svn_fs_fs__id_part_t *copy_id,
                           const svn_fs_fs__id_part_t *txn_id,
                           svn_boolean_t is_parent_copyroot,
                           apr_pool_t *pool)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs_fs__dag_get_fs(parent);
  apr_pool_t *subpool = svn_pool_create(pool);

  /* First check that the parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to clone child of non-mutable node");

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to make a child clone with an illegal name '%s'", name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR(svn_fs_fs__dag_open(&cur_entry, parent, name, pool, subpool));
  if (! cur_entry)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       "Attempted to open non-existent child node '%s'", name);

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs_fs__dag_check_mutable(cur_entry))
    {
      /* This has already been cloned */
      new_node_id = cur_entry->id;
    }
  else
    {
      node_revision_t *noderev, *parent_noderev;

      /* Go get a fresh NODE-REVISION for current child node. */
      SVN_ERR(get_node_revision(&noderev, cur_entry));

      if (is_parent_copyroot)
        {
          SVN_ERR(get_node_revision(&parent_noderev, parent));
          noderev->copyroot_rev = parent_noderev->copyroot_rev;
          noderev->copyroot_path = apr_pstrdup(pool,
                                               parent_noderev->copyroot_path);
        }

      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;

      noderev->predecessor_id = svn_fs_fs__id_copy(cur_entry->id, pool);
      noderev->predecessor_count++;
      noderev->created_path = svn_fspath__join(parent_path, name, pool);

      SVN_ERR(svn_fs_fs__create_successor(&new_node_id, fs, cur_entry->id,
                                          noderev, copy_id, txn_id, pool));

      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR(set_entry(parent, name, new_node_id, noderev->kind, txn_id,
                        pool));
    }

  /* Initialize the youngster. */
  svn_pool_destroy(subpool);
  return svn_fs_fs__dag_get_node(child_p, fs, new_node_id, pool);
}



svn_error_t *
svn_fs_fs__dag_clone_root(dag_node_t **root_p,
                          svn_fs_t *fs,
                          const svn_fs_fs__id_part_t *txn_id,
                          apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *root_id;

  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR(svn_fs_fs__get_txn_ids(&root_id, &base_root_id, fs, txn_id, pool));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  SVN_ERR_ASSERT(!svn_fs_fs__id_eq(root_id, base_root_id));

  /*
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  /* One way or another, root_id now identifies a cloned root node. */
  return svn_fs_fs__dag_get_node(root_p, fs, root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_delete(dag_node_t *parent,
                      const char *name,
                      const svn_fs_fs__id_part_t *txn_id,
                      apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  svn_fs_t *fs = parent->fs;
  svn_fs_dirent_t *dirent;
  svn_fs_id_t *id;
  apr_pool_t *subpool;

  /* Make sure parent is a directory. */
  if (parent->kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       "Attempted to delete entry '%s' from *non*-directory node", name);

  /* Make sure parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to delete entry '%s' from immutable directory node", name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to delete a node with an illegal name '%s'", name);

  /* Get a fresh NODE-REVISION for the parent node. */
  SVN_ERR(get_node_revision(&parent_noderev, parent));

  subpool = svn_pool_create(pool);

  /* Search this directory for a dirent with that NAME. */
  SVN_ERR(svn_fs_fs__rep_contents_dir_entry(&dirent, fs, parent_noderev,
                                            name, subpool, subpool));

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! dirent)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       "Delete failed--directory has no entry '%s'", name);

  /* Copy the ID out of the subpool and release the rest of the
     directory listing. */
  id = svn_fs_fs__id_copy(dirent->id, pool);
  svn_pool_destroy(subpool);

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR(svn_fs_fs__dag_delete_if_mutable(parent->fs, id, pool));

  /* Remove this entry from its parent's entries list. */
  return svn_fs_fs__set_entry(parent->fs, txn_id, parent_noderev, name,
                              NULL, svn_node_unknown, pool);
}


svn_error_t *
svn_fs_fs__dag_remove_node(svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           apr_pool_t *pool)
{
  dag_node_t *node;

  /* Fetch the node. */
  SVN_ERR(svn_fs_fs__dag_get_node(&node, fs, id, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_fs__dag_check_mutable(node))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             "Attempted removal of immutable node");

  /* Delete the node revision. */
  return svn_fs_fs__delete_node_revision(fs, id, pool);
}


svn_error_t *
svn_fs_fs__dag_delete_if_mutable(svn_fs_t *fs,
                                 const svn_fs_id_t *id,
                                 apr_pool_t *pool)
{
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_fs__dag_get_node(&node, fs, id, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_fs__dag_check_mutable(node))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (node->kind == svn_node_dir)
    {
      apr_array_header_t *entries;
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      /* Loop over directory entries */
      SVN_ERR(svn_fs_fs__dag_dir_entries(&entries, node, pool));
      if (entries)
        for (i = 0; i < entries->nelts; ++i)
          {
            svn_pool_clear(iterpool);
            SVN_ERR(svn_fs_fs__dag_delete_if_mutable(fs,
                          APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *)->id,
                          iterpool));
          }

      svn_pool_destroy(iterpool);
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */
  return svn_fs_fs__dag_remove_node(fs, id, pool);
}

svn_error_t *
svn_fs_fs__dag_make_file(dag_node_t **child_p,
                         dag_node_t *parent,
                         const char *parent_path,
                         const char *name,
                         const svn_fs_fs__id_part_t *txn_id,
                         apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, FALSE, txn_id, pool);
}


svn_error_t *
svn_fs_fs__dag_make_dir(dag_node_t **child_p,
                        dag_node_t *parent,
                        const char *parent_path,
                        const char *name,
                        const svn_fs_fs__id_part_t *txn_id,
                        apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, TRUE, txn_id, pool);
}


svn_error_t *
svn_fs_fs__dag_get_contents(svn_stream_t **contents_p,
                            dag_node_t *file,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;
  svn_stream_t *contents;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");

  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(get_node_revision(&noderev, file));

  /* Get a stream to the contents. */
  SVN_ERR(svn_fs_fs__get_contents(&contents, file->fs,
                                  noderev->data_rep, TRUE, pool));

  *contents_p = contents;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                     dag_node_t *source,
                                     dag_node_t *target,
                                     apr_pool_t *pool)
{
  node_revision_t *src_noderev;
  node_revision_t *tgt_noderev;

  /* Make sure our nodes are files. */
  if ((source && source->kind != svn_node_file)
      || target->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");

  /* Go get fresh node-revisions for the nodes. */
  if (source)
    SVN_ERR(get_node_revision(&src_noderev, source));
  else
    src_noderev = NULL;
  SVN_ERR(get_node_revision(&tgt_noderev, target));

  /* Get the delta stream. */
  return svn_fs_fs__get_file_delta_stream(stream_p, target->fs,
                                          src_noderev, tgt_noderev, pool);
}


svn_error_t *
svn_fs_fs__dag_try_process_file_contents(svn_boolean_t *success,
                                         dag_node_t *node,
                                         svn_fs_process_contents_func_t processor,
                                         void* baton,
                                         apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Go get fresh node-revisions for the nodes. */
  SVN_ERR(get_node_revision(&noderev, node));

  return svn_fs_fs__try_process_file_contents(success, node->fs,
                                              noderev,
                                              processor, baton, pool);
}


svn_error_t *
svn_fs_fs__dag_file_length(svn_filesize_t *length,
                           dag_node_t *file,
                           apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get length of a *non*-file node");

  /* Go get a fresh node-revision for FILE, and . */
  SVN_ERR(get_node_revision(&noderev, file));

  return svn_fs_fs__file_length(length, noderev, pool);
}


svn_error_t *
svn_fs_fs__dag_file_checksum(svn_checksum_t **checksum,
                             dag_node_t *file,
                             svn_checksum_kind_t kind,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;

  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get checksum of a *non*-file node");

  SVN_ERR(get_node_revision(&noderev, file));

  return svn_fs_fs__file_checksum(checksum, noderev, kind, pool);
}


svn_error_t *
svn_fs_fs__dag_get_edit_stream(svn_stream_t **contents,
                               dag_node_t *file,
                               apr_pool_t *pool)
{
  node_revision_t *noderev;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to set textual contents of a *non*-file node");

  /* Make sure our node is mutable. */
  if (! svn_fs_fs__dag_check_mutable(file))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to set textual contents of an immutable node");

  /* Get the node revision. */
  SVN_ERR(get_node_revision(&noderev, file));

  SVN_ERR(svn_fs_fs__set_contents(&ws, file->fs, noderev, pool));

  *contents = ws;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__dag_finalize_edits(dag_node_t *file,
                              const svn_checksum_t *checksum,
                              apr_pool_t *pool)
{
  if (checksum)
    {
      svn_checksum_t *file_checksum;

      SVN_ERR(svn_fs_fs__dag_file_checksum(&file_checksum, file,
                                           checksum->kind, pool));
      if (!svn_checksum_match(checksum, file_checksum))
        return svn_checksum_mismatch_err(checksum, file_checksum, pool,
                                         _("Checksum mismatch for '%s'"),
                                         file->created_path);
    }

  return SVN_NO_ERROR;
}


dag_node_t *
svn_fs_fs__dag_dup(const dag_node_t *node,
                   apr_pool_t *pool)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc(pool, sizeof(*new_node));

  new_node->fs = node->fs;
  new_node->id = svn_fs_fs__id_copy(node->id, pool);
  new_node->kind = node->kind;
  new_node->created_path = apr_pstrdup(pool, node->created_path);

  /* Only copy cached node_revision_t for immutable nodes. */
  if (node->node_revision && !svn_fs_fs__dag_check_mutable(node))
    {
      new_node->node_revision = copy_node_revision(node->node_revision, pool);
      new_node->node_revision->id =
          svn_fs_fs__id_copy(node->node_revision->id, pool);
      new_node->node_revision->is_fresh_txn_root =
          node->node_revision->is_fresh_txn_root;
    }
  new_node->node_pool = pool;

  return new_node;
}

svn_error_t *
svn_fs_fs__dag_serialize(void **data,
                         apr_size_t *data_len,
                         void *in,
                         apr_pool_t *pool)
{
  dag_node_t *node = in;
  svn_stringbuf_t *serialized;

  /* create an serialization context and serialize the dag node as root */
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(node,
                                sizeof(*node),
                                1024 - SVN_TEMP_SERIALIZER__OVERHEAD,
                                pool);

  /* for mutable nodes, we will _never_ cache the noderev */
  if (node->node_revision && !svn_fs_fs__dag_check_mutable(node))
    svn_fs_fs__noderev_serialize(context, &node->node_revision);
  else
    svn_temp_serializer__set_null(context,
                                  (const void * const *)&node->node_revision);

  /* The deserializer will use its own pool. */
  svn_temp_serializer__set_null(context,
                                (const void * const *)&node->node_pool);

  /* serialize other sub-structures */
  svn_fs_fs__id_serialize(context, (const svn_fs_id_t **)&node->id);
  svn_fs_fs__id_serialize(context, &node->fresh_root_predecessor_id);
  svn_temp_serializer__add_string(context, &node->created_path);

  /* return serialized data */
  serialized = svn_temp_serializer__get(context);
  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_deserialize(void **out,
                           void *data,
                           apr_size_t data_len,
                           apr_pool_t *pool)
{
  dag_node_t *node = (dag_node_t *)data;
  if (data_len == 0)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Empty noderev in cache"));

  /* Copy the _full_ buffer as it also contains the sub-structures. */
  node->fs = NULL;

  /* fixup all references to sub-structures */
  svn_fs_fs__id_deserialize(node, &node->id);
  svn_fs_fs__id_deserialize(node,
                            (svn_fs_id_t **)&node->fresh_root_predecessor_id);
  svn_fs_fs__noderev_deserialize(node, &node->node_revision);
  node->node_pool = pool;

  svn_temp_deserializer__resolve(node, (void**)&node->created_path);

  /* return result */
  *out = node;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_open(dag_node_t **child_p,
                    dag_node_t *parent,
                    const char *name,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const svn_fs_id_t *node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR(dir_entry_id_from_node(&node_id, parent, name,
                                 scratch_pool, scratch_pool));
  if (! node_id)
    {
      *child_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to open node with an illegal name '%s'", name);

  /* Now get the node that was requested. */
  return svn_fs_fs__dag_get_node(child_p, svn_fs_fs__dag_get_fs(parent),
                                 node_id, result_pool);
}


svn_error_t *
svn_fs_fs__dag_copy(dag_node_t *to_node,
                    const char *entry,
                    dag_node_t *from_node,
                    svn_boolean_t preserve_history,
                    svn_revnum_t from_rev,
                    const char *from_path,
                    const svn_fs_fs__id_part_t *txn_id,
                    apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (preserve_history)
    {
      node_revision_t *from_noderev, *to_noderev;
      svn_fs_fs__id_part_t copy_id;
      const svn_fs_id_t *src_id = svn_fs_fs__dag_get_id(from_node);
      svn_fs_t *fs = svn_fs_fs__dag_get_fs(from_node);

      /* Make a copy of the original node revision. */
      SVN_ERR(get_node_revision(&from_noderev, from_node));
      to_noderev = copy_node_revision(from_noderev, pool);

      /* Reserve a copy ID for this new copy. */
      SVN_ERR(svn_fs_fs__reserve_copy_id(&copy_id, fs, txn_id, pool));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      to_noderev->predecessor_id = svn_fs_fs__id_copy(src_id, pool);
      to_noderev->predecessor_count++;
      to_noderev->created_path =
        svn_fspath__join(svn_fs_fs__dag_get_created_path(to_node), entry,
                     pool);
      to_noderev->copyfrom_path = apr_pstrdup(pool, from_path);
      to_noderev->copyfrom_rev = from_rev;

      /* Set the copyroot equal to our own id. */
      to_noderev->copyroot_path = NULL;

      SVN_ERR(svn_fs_fs__create_successor(&id, fs, src_id, to_noderev,
                                          &copy_id, txn_id, pool));

    }
  else  /* don't preserve history */
    {
      id = svn_fs_fs__dag_get_id(from_node);
    }

  /* Set the entry in to_node to the new id. */
  return svn_fs_fs__dag_set_entry(to_node, entry, id, from_node->kind,
                                  txn_id, pool);
}



/*** Comparison. ***/

svn_error_t *
svn_fs_fs__dag_things_different(svn_boolean_t *props_changed,
                                svn_boolean_t *contents_changed,
                                dag_node_t *node1,
                                dag_node_t *node2,
                                svn_boolean_t strict,
                                apr_pool_t *pool)
{
  node_revision_t *noderev1, *noderev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The node revision skels for these two nodes. */
  SVN_ERR(get_node_revision(&noderev1, node1));
  SVN_ERR(get_node_revision(&noderev2, node2));

  if (strict)
    {
      /* In strict mode, compare text and property representations in the
         svn_fs_contents_different() / svn_fs_props_different() manner.

         See the "No-op changes no longer dumped by 'svnadmin dump' in 1.9"
         discussion (http://svn.haxx.se/dev/archive-2015-09/0269.shtml) and
         issue #4598 (https://issues.apache.org/jira/browse/SVN-4598). */
      svn_fs_t *fs = svn_fs_fs__dag_get_fs(node1);
      svn_boolean_t same;

      /* Compare property keys. */
      if (props_changed != NULL)
        {
          SVN_ERR(svn_fs_fs__prop_rep_equal(&same, fs, noderev1,
                                            noderev2, pool));
          *props_changed = !same;
        }

      /* Compare contents keys. */
      if (contents_changed != NULL)
        {
          SVN_ERR(svn_fs_fs__file_text_rep_equal(&same, fs, noderev1,
                                                 noderev2, pool));
          *contents_changed = !same;
        }
    }
  else
    {
      /* Otherwise, compare representation keys -- as in Subversion 1.8. */

      /* Compare property keys. */
      if (props_changed != NULL)
        *props_changed =
          !svn_fs_fs__noderev_same_rep_key(noderev1->prop_rep,
                                           noderev2->prop_rep);

      /* Compare contents keys. */
      if (contents_changed != NULL)
        *contents_changed =
          !svn_fs_fs__noderev_same_rep_key(noderev1->data_rep,
                                           noderev2->data_rep);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyroot(svn_revnum_t *rev,
                            const char **path,
                            dag_node_t *node)
{
  node_revision_t *noderev;

  /* Go get a fresh node-revision for NODE. */
  SVN_ERR(get_node_revision(&noderev, node));

  *rev = noderev->copyroot_rev;
  *path = noderev->copyroot_path;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyfrom_rev(svn_revnum_t *rev,
                                dag_node_t *node)
{
  node_revision_t *noderev;

  /* Go get a fresh node-revision for NODE. */
  SVN_ERR(get_node_revision(&noderev, node));

  *rev = noderev->copyfrom_rev;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyfrom_path(const char **path,
                                 dag_node_t *node)
{
  node_revision_t *noderev;

  /* Go get a fresh node-revision for NODE. */
  SVN_ERR(get_node_revision(&noderev, node));

  *path = noderev->copyfrom_path;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_update_ancestry(dag_node_t *target,
                               dag_node_t *source,
                               apr_pool_t *pool)
{
  node_revision_t *source_noderev, *target_noderev;

  if (! svn_fs_fs__dag_check_mutable(target))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to update ancestry of non-mutable node"));

  SVN_ERR(get_node_revision(&source_noderev, source));
  SVN_ERR(get_node_revision(&target_noderev, target));

  target_noderev->predecessor_id = source->id;
  target_noderev->predecessor_count = source_noderev->predecessor_count;
  target_noderev->predecessor_count++;

  return svn_fs_fs__put_node_revision(target->fs, target->id, target_noderev,
                                      FALSE, pool);
}
