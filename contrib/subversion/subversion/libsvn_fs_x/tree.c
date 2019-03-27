/* tree.c : tree-like filesystem, built on DAG filesystem
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


/* The job of this layer is to take a filesystem with lots of node
   sharing going on --- the real DAG filesystem as it appears in the
   database --- and make it look and act like an ordinary tree
   filesystem, with no sharing.

   We do just-in-time cloning: you can walk from some unfinished
   transaction's root down into directories and files shared with
   committed revisions; as soon as you try to change something, the
   appropriate nodes get cloned (and parent directory entries updated)
   invisibly, behind your back.  Any other references you have to
   nodes that have been cloned by other changes, even made by other
   processes, are automatically updated to point to the right clones.  */


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_sorts.h"

#include "fs.h"
#include "dag.h"
#include "dag_cache.h"
#include "lock.h"
#include "tree.h"
#include "fs_x.h"
#include "fs_id.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "transaction.h"
#include "pack.h"
#include "util.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "../libsvn_fs/fs-loader.h"



/* The root structures.

   Why do they contain different data?  Well, transactions are mutable
   enough that it isn't safe to cache the DAG node for the root
   directory or the hash of copyfrom data: somebody else might modify
   them concurrently on disk!  (Why is the DAG node cache safer than
   the root DAG node?  When cloning transaction DAG nodes in and out
   of the cache, all of the possibly-mutable data from the
   svn_fs_x__noderev_t inside the dag_node_t is dropped.)  Additionally,
   revisions are immutable enough that their DAG node cache can be
   kept in the FS object and shared among multiple revision root
   objects.
*/
typedef dag_node_t fs_rev_root_data_t;

typedef struct fs_txn_root_data_t
{
  /* TXN_ID value from the main struct but as a struct instead of a string */
  svn_fs_x__txn_id_t txn_id;
} fs_txn_root_data_t;

static svn_fs_root_t *
make_revision_root(svn_fs_t *fs,
                   svn_revnum_t rev,
                   apr_pool_t *result_pool);

static svn_error_t *
make_txn_root(svn_fs_root_t **root_p,
              svn_fs_t *fs,
              svn_fs_x__txn_id_t txn_id,
              svn_revnum_t base_rev,
              apr_uint32_t flags,
              apr_pool_t *result_pool);

static svn_error_t *
x_closest_copy(svn_fs_root_t **root_p,
               const char **path_p,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool);


/* Creating transaction and revision root nodes.  */

svn_error_t *
svn_fs_x__txn_root(svn_fs_root_t **root_p,
                   svn_fs_txn_t *txn,
                   apr_pool_t *pool)
{
  apr_uint32_t flags = 0;
  apr_hash_t *txnprops;

  /* Look for the temporary txn props representing 'flags'. */
  SVN_ERR(svn_fs_x__txn_proplist(&txnprops, txn, pool));
  if (txnprops)
    {
      if (svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CHECK_OOD))
        flags |= SVN_FS_TXN_CHECK_OOD;

      if (svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CHECK_LOCKS))
        flags |= SVN_FS_TXN_CHECK_LOCKS;
    }

  return make_txn_root(root_p, txn->fs, svn_fs_x__txn_get_id(txn),
                       txn->base_rev, flags, pool);
}


svn_error_t *
svn_fs_x__revision_root(svn_fs_root_t **root_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, pool));

  *root_p = make_revision_root(fs, rev, pool);

  return SVN_NO_ERROR;
}



/* Getting dag nodes for roots.  */

/* Return the transaction ID to a given transaction ROOT. */
svn_fs_x__txn_id_t
svn_fs_x__root_txn_id(svn_fs_root_t *root)
{
  fs_txn_root_data_t *frd = root->fsap_data;
  assert(root->is_txn_root);

  return frd->txn_id;
}

/* Return the change set to a given ROOT. */
svn_fs_x__change_set_t
svn_fs_x__root_change_set(svn_fs_root_t *root)
{
  if (root->is_txn_root)
    return svn_fs_x__change_set_by_txn(svn_fs_x__root_txn_id(root));

  return svn_fs_x__change_set_by_rev(root->rev);
}




/* Traversing directory paths.  */

/* Return a text string describing the absolute path of parent path
   DAG_PATH.  It will be allocated in POOL. */
static const char *
parent_path_path(svn_fs_x__dag_path_t *dag_path,
                 apr_pool_t *pool)
{
  const char *path_so_far = "/";
  if (dag_path->parent)
    path_so_far = parent_path_path(dag_path->parent, pool);
  return dag_path->entry
    ? svn_fspath__join(path_so_far, dag_path->entry, pool)
    : path_so_far;
}


/* Return the FS path for the parent path chain object CHILD relative
   to its ANCESTOR in the same chain, allocated in POOL.  */
static const char *
parent_path_relpath(svn_fs_x__dag_path_t *child,
                    svn_fs_x__dag_path_t *ancestor,
                    apr_pool_t *pool)
{
  const char *path_so_far = "";
  svn_fs_x__dag_path_t *this_node = child;
  while (this_node != ancestor)
    {
      assert(this_node != NULL);
      path_so_far = svn_relpath_join(this_node->entry, path_so_far, pool);
      this_node = this_node->parent;
    }
  return path_so_far;
}





/* Populating the `changes' table. */

/* Add a change to the changes table in FS, keyed on transaction id
   TXN_ID, and indicated that a change of kind CHANGE_KIND occurred on
   PATH, and optionally that TEXT_MODs, PROP_MODs or MERGEINFO_MODs
   occurred.  If the change resulted from a copy, COPYFROM_REV and
   COPYFROM_PATH specify under which revision and path the node was
   copied from.  If this was not part of a copy, COPYFROM_REV should
   be SVN_INVALID_REVNUM.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
add_change(svn_fs_t *fs,
           svn_fs_x__txn_id_t txn_id,
           const char *path,
           svn_fs_path_change_kind_t change_kind,
           svn_boolean_t text_mod,
           svn_boolean_t prop_mod,
           svn_boolean_t mergeinfo_mod,
           svn_node_kind_t node_kind,
           svn_revnum_t copyfrom_rev,
           const char *copyfrom_path,
           apr_pool_t *scratch_pool)
{
  return svn_fs_x__add_change(fs, txn_id,
                              svn_fs__canonicalize_abspath(path,
                                                           scratch_pool),
                              change_kind, text_mod, prop_mod, mergeinfo_mod,
                              node_kind, copyfrom_rev, copyfrom_path,
                              scratch_pool);
}



/* Generic node operations.  */

/* Get the id of a node referenced by path PATH in ROOT.  Return the
   id in *ID_P allocated in POOL. */
static svn_error_t *
x_node_id(const svn_fs_id_t **id_p,
          svn_fs_root_t *root,
          const char *path,
          apr_pool_t *pool)
{
  svn_fs_x__id_t noderev_id;

  if ((! root->is_txn_root)
      && (path[0] == '\0' || ((path[0] == '/') && (path[1] == '\0'))))
    {
      /* Optimize the case where we don't need any db access at all.
         The root directory ("" or "/") node is stored in the
         svn_fs_root_t object, and never changes when it's a revision
         root, so we can just reach in and grab it directly. */
      svn_fs_x__init_rev_root(&noderev_id, root->rev);
    }
  else
    {
      apr_pool_t *scratch_pool = svn_pool_create(pool);
      dag_node_t *node;

      SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
      noderev_id = *svn_fs_x__dag_get_id(node);
      svn_pool_destroy(scratch_pool);
    }

  *id_p = svn_fs_x__id_create(svn_fs_x__id_create_context(root->fs, pool),
                              &noderev_id, pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
x_node_relation(svn_fs_node_relation_t *relation,
                svn_fs_root_t *root_a,
                const char *path_a,
                svn_fs_root_t *root_b,
                const char *path_b,
                apr_pool_t *scratch_pool)
{
  dag_node_t *node;
  svn_fs_x__id_t noderev_id_a, noderev_id_b, node_id_a, node_id_b;

  /* Root paths are a common special case. */
  svn_boolean_t a_is_root_dir
    = (path_a[0] == '\0') || ((path_a[0] == '/') && (path_a[1] == '\0'));
  svn_boolean_t b_is_root_dir
    = (path_b[0] == '\0') || ((path_b[0] == '/') && (path_b[1] == '\0'));

  /* Path from different repository are always unrelated. */
  if (root_a->fs != root_b->fs)
    {
      *relation = svn_fs_node_unrelated;
      return SVN_NO_ERROR;
    }

  /* Are both (!) root paths? Then, they are related and we only test how
   * direct the relation is. */
  if (a_is_root_dir && b_is_root_dir)
    {
      svn_boolean_t different_txn
        = root_a->is_txn_root && root_b->is_txn_root
            && strcmp(root_a->txn, root_b->txn);

      /* For txn roots, root->REV is the base revision of that TXN. */
      *relation = (   (root_a->rev == root_b->rev)
                   && (root_a->is_txn_root == root_b->is_txn_root)
                   && !different_txn)
                ? svn_fs_node_unchanged
                : svn_fs_node_common_ancestor;
      return SVN_NO_ERROR;
    }

  /* We checked for all separations between ID spaces (repos, txn).
   * Now, we can simply test for the ID values themselves. */
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root_a, path_a, scratch_pool));
  noderev_id_a = *svn_fs_x__dag_get_id(node);
  node_id_a = *svn_fs_x__dag_get_node_id(node);

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root_b, path_b, scratch_pool));
  noderev_id_b = *svn_fs_x__dag_get_id(node);
  node_id_b = *svn_fs_x__dag_get_node_id(node);

  /* In FSX, even in-txn IDs are globally unique.
   * So, we can simply compare them. */
  if (svn_fs_x__id_eq(&noderev_id_a, &noderev_id_b))
    *relation = svn_fs_node_unchanged;
  else if (svn_fs_x__id_eq(&node_id_a, &node_id_b))
    *relation = svn_fs_node_common_ancestor;
  else
    *relation = svn_fs_node_unrelated;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__node_created_rev(svn_revnum_t *revision,
                           svn_fs_root_t *root,
                           const char *path,
                           apr_pool_t *scratch_pool)
{
  dag_node_t *node;

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
  *revision = svn_fs_x__dag_get_revision(node);

  return SVN_NO_ERROR;
}


/* Set *CREATED_PATH to the path at which PATH under ROOT was created.
   Return a string allocated in POOL. */
static svn_error_t *
x_node_created_path(const char **created_path,
                    svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  dag_node_t *node;

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, pool));
  *created_path = apr_pstrdup(pool, svn_fs_x__dag_get_created_path(node));

  return SVN_NO_ERROR;
}


/* Set *KIND_P to the type of node present at PATH under ROOT.  If
   PATH does not exist under ROOT, set *KIND_P to svn_node_none.  Use
   SCRATCH_POOL for temporary allocation. */
svn_error_t *
svn_fs_x__check_path(svn_node_kind_t *kind_p,
                     svn_fs_root_t *root,
                     const char *path,
                     apr_pool_t *scratch_pool)
{
  dag_node_t *node;

  /* Get the node id. */
  svn_error_t *err = svn_fs_x__get_temp_dag_node(&node, root, path,
                                                 scratch_pool);

  /* Use the node id to get the real kind. */
  if (!err)
    *kind_p = svn_fs_x__dag_node_kind(node);

  if (err &&
      ((err->apr_err == SVN_ERR_FS_NOT_FOUND)
       || (err->apr_err == SVN_ERR_FS_NOT_DIRECTORY)))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      *kind_p = svn_node_none;
    }

  return svn_error_trace(err);
}

/* Set *VALUE_P to the value of the property named PROPNAME of PATH in
   ROOT.  If the node has no property by that name, set *VALUE_P to
   zero.  Allocate the result in POOL. */
static svn_error_t *
x_node_prop(svn_string_t **value_p,
            svn_fs_root_t *root,
            const char *path,
            const char *propname,
            apr_pool_t *pool)
{
  dag_node_t *node;
  apr_hash_t *proplist;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
  SVN_ERR(svn_fs_x__dag_get_proplist(&proplist, node, scratch_pool,
                                     scratch_pool));
  *value_p = NULL;
  if (proplist)
    *value_p = svn_string_dup(svn_hash_gets(proplist, propname), pool);

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}


/* Set *TABLE_P to the entire property list of PATH under ROOT, as an
   APR hash table allocated in POOL.  The resulting property table
   maps property names to pointers to svn_string_t objects containing
   the property value. */
static svn_error_t *
x_node_proplist(apr_hash_t **table_p,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  dag_node_t *node;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
  SVN_ERR(svn_fs_x__dag_get_proplist(table_p, node, pool, scratch_pool));

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
x_node_has_props(svn_boolean_t *has_props,
                 svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *scratch_pool)
{
  apr_hash_t *props;

  SVN_ERR(x_node_proplist(&props, root, path, scratch_pool));

  *has_props = (0 < apr_hash_count(props));

  return SVN_NO_ERROR;
}

static svn_error_t *
increment_mergeinfo_up_tree(svn_fs_x__dag_path_t *pp,
                            apr_int64_t increment,
                            apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  for (; pp; pp = pp->parent)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_x__dag_increment_mergeinfo_count(pp->node,
                                                      increment,
                                                      iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Change, add, or delete a node's property value.  The affected node
   is PATH under ROOT, the property value to modify is NAME, and VALUE
   points to either a string value to set the new contents to, or NULL
   if the property should be deleted.  Perform temporary allocations
   in SCRATCH_POOL. */
static svn_error_t *
x_change_node_prop(svn_fs_root_t *root,
                   const char *path,
                   const char *name,
                   const svn_string_t *value,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__dag_path_t *dag_path;
  apr_hash_t *proplist;
  svn_fs_x__txn_id_t txn_id;
  svn_boolean_t mergeinfo_mod = FALSE;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  if (! root->is_txn_root)
    return SVN_FS__NOT_TXN(root);
  txn_id = svn_fs_x__root_txn_id(root);

  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path, 0, TRUE, subpool,
                                 subpool));

  /* Check (non-recursively) to see if path is locked; if so, check
     that we can use it. */
  if (root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(path, root->fs, FALSE, FALSE,
                                             subpool));

  SVN_ERR(svn_fs_x__make_path_mutable(root, dag_path, path, subpool,
                                      subpool));
  SVN_ERR(svn_fs_x__dag_get_proplist(&proplist, dag_path->node, subpool,
                                     subpool));

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! proplist)
    proplist = apr_hash_make(subpool);

  if (strcmp(name, SVN_PROP_MERGEINFO) == 0)
    {
      apr_int64_t increment = 0;
      svn_boolean_t had_mergeinfo
        = svn_fs_x__dag_has_mergeinfo(dag_path->node);

      if (value && !had_mergeinfo)
        increment = 1;
      else if (!value && had_mergeinfo)
        increment = -1;

      if (increment != 0)
        {
          SVN_ERR(increment_mergeinfo_up_tree(dag_path, increment, subpool));
          SVN_ERR(svn_fs_x__dag_set_has_mergeinfo(dag_path->node,
                                                  (value != NULL), subpool));
        }

      mergeinfo_mod = TRUE;
    }

  /* Set the property. */
  svn_hash_sets(proplist, name, value);

  /* Overwrite the node's proplist. */
  SVN_ERR(svn_fs_x__dag_set_proplist(dag_path->node, proplist,
                                     subpool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_path_change_modify, FALSE, TRUE, mergeinfo_mod,
                     svn_fs_x__dag_node_kind(dag_path->node),
                     SVN_INVALID_REVNUM, NULL, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Determine if the properties of two path/root combinations are
   different.  Set *CHANGED_P to TRUE if the properties at PATH1 under
   ROOT1 differ from those at PATH2 under ROOT2, or FALSE otherwise.
   Both roots must be in the same filesystem. */
static svn_error_t *
x_props_changed(svn_boolean_t *changed_p,
                svn_fs_root_t *root1,
                const char *path1,
                svn_fs_root_t *root2,
                const char *path2,
                svn_boolean_t strict,
                apr_pool_t *scratch_pool)
{
  dag_node_t *node1, *node2;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Check that roots are in the same fs. */
  if (root1->fs != root2->fs)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       _("Cannot compare property value between two different filesystems"));

  SVN_ERR(svn_fs_x__get_dag_node(&node1, root1, path1, subpool, subpool));
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node2, root2, path2, subpool));
  SVN_ERR(svn_fs_x__dag_things_different(changed_p, NULL, node1, node2,
                                         strict, subpool));
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Merges and commits. */

/* Set *NODE to the root node of ROOT.  */
static svn_error_t *
get_root(dag_node_t **node,
         svn_fs_root_t *root,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  return svn_fs_x__get_dag_node(node, root, "/", result_pool, scratch_pool);
}


/* Set the contents of CONFLICT_PATH to PATH, and return an
   SVN_ERR_FS_CONFLICT error that indicates that there was a conflict
   at PATH.  Perform all allocations in POOL (except the allocation of
   CONFLICT_PATH, which should be handled outside this function).  */
static svn_error_t *
conflict_err(svn_stringbuf_t *conflict_path,
             const char *path)
{
  svn_stringbuf_set(conflict_path, path);
  return svn_error_createf(SVN_ERR_FS_CONFLICT, NULL,
                           _("Conflict at '%s'"), path);
}

/* Compare the directory representations at nodes LHS and RHS in FS and set
 * *CHANGED to TRUE, if at least one entry has been added or removed them.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
compare_dir_structure(svn_boolean_t *changed,
                      svn_fs_t *fs,
                      dag_node_t *lhs,
                      dag_node_t *rhs,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *lhs_entries;
  apr_array_header_t *rhs_entries;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_fs_x__dag_dir_entries(&lhs_entries, lhs, scratch_pool,
                                    iterpool));
  SVN_ERR(svn_fs_x__dag_dir_entries(&rhs_entries, rhs, scratch_pool,
                                    iterpool));

  /* different number of entries -> some addition / removal */
  if (lhs_entries->nelts != rhs_entries->nelts)
    {
      svn_pool_destroy(iterpool);
      *changed = TRUE;

      return SVN_NO_ERROR;
    }

  /* Since directories are sorted by name, we can simply compare their
     entries one-by-one without binary lookup etc. */
  for (i = 0; i < lhs_entries->nelts; ++i)
    {
      svn_fs_x__dirent_t *lhs_entry
        = APR_ARRAY_IDX(lhs_entries, i, svn_fs_x__dirent_t *);
      svn_fs_x__dirent_t *rhs_entry
        = APR_ARRAY_IDX(rhs_entries, i, svn_fs_x__dirent_t *);

      if (strcmp(lhs_entry->name, rhs_entry->name) == 0)
        {
          dag_node_t *lhs_node, *rhs_node;

          /* Unchanged entry? */
          if (!svn_fs_x__id_eq(&lhs_entry->id, &rhs_entry->id))
            continue;

          /* We get here rarely. */
          svn_pool_clear(iterpool);

          /* Modified but not copied / replaced or anything? */
          SVN_ERR(svn_fs_x__dag_get_node(&lhs_node, fs, &lhs_entry->id,
                                         iterpool, iterpool));
          SVN_ERR(svn_fs_x__dag_get_node(&rhs_node, fs, &rhs_entry->id,
                                         iterpool, iterpool));
          if (svn_fs_x__dag_same_line_of_history(lhs_node, rhs_node))
            continue;
        }

      /* This is a different entry. */
      *changed = TRUE;
      svn_pool_destroy(iterpool);

      return SVN_NO_ERROR;
    }

  svn_pool_destroy(iterpool);
  *changed = FALSE;

  return SVN_NO_ERROR;
}

/* Merge changes between ANCESTOR and SOURCE into TARGET.  ANCESTOR
 * and TARGET must be distinct node revisions.  TARGET_PATH should
 * correspond to TARGET's full path in its filesystem, and is used for
 * reporting conflict location.
 *
 * SOURCE, TARGET, and ANCESTOR are generally directories; this
 * function recursively merges the directories' contents.  If any are
 * files, this function simply returns an error whenever SOURCE,
 * TARGET, and ANCESTOR are all distinct node revisions.
 *
 * If there are differences between ANCESTOR and SOURCE that conflict
 * with changes between ANCESTOR and TARGET, this function returns an
 * SVN_ERR_FS_CONFLICT error, and updates CONFLICT_P to the name of the
 * conflicting node in TARGET, with TARGET_PATH prepended as a path.
 *
 * If there are no conflicting differences, CONFLICT_P is updated to
 * the empty string.
 *
 * CONFLICT_P must point to a valid svn_stringbuf_t.
 *
 * Do any necessary temporary allocation in POOL.
 */
static svn_error_t *
merge(svn_stringbuf_t *conflict_p,
      const char *target_path,
      dag_node_t *target,
      dag_node_t *source,
      dag_node_t *ancestor,
      svn_fs_x__txn_id_t txn_id,
      apr_int64_t *mergeinfo_increment_out,
      apr_pool_t *pool)
{
  const svn_fs_x__id_t *source_id, *target_id, *ancestor_id;
  apr_array_header_t *s_entries, *t_entries, *a_entries;
  int i, s_idx = -1, t_idx = -1;
  svn_fs_t *fs;
  apr_pool_t *iterpool;
  apr_int64_t mergeinfo_increment = 0;

  /* Make sure everyone comes from the same filesystem. */
  fs = svn_fs_x__dag_get_fs(ancestor);
  if ((fs != svn_fs_x__dag_get_fs(source))
      || (fs != svn_fs_x__dag_get_fs(target)))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, NULL,
         _("Bad merge; ancestor, source, and target not all in same fs"));
    }

  /* We have the same fs, now check it. */
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  source_id   = svn_fs_x__dag_get_id(source);
  target_id   = svn_fs_x__dag_get_id(target);
  ancestor_id = svn_fs_x__dag_get_id(ancestor);

  /* It's improper to call this function with ancestor == target. */
  if (svn_fs_x__id_eq(ancestor_id, target_id))
    {
      svn_string_t *id_str = svn_fs_x__id_unparse(target_id, pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         _("Bad merge; target '%s' has id '%s', same as ancestor"),
         target_path, id_str->data);
    }

  svn_stringbuf_setempty(conflict_p);

  /* Base cases:
   * Either no change made in source, or same change as made in target.
   * Both mean nothing to merge here.
   */
  if (svn_fs_x__id_eq(ancestor_id, source_id)
      || (svn_fs_x__id_eq(source_id, target_id)))
    return SVN_NO_ERROR;

  /* Else proceed, knowing all three are distinct node revisions.
   *
   * How to merge from this point:
   *
   * if (not all 3 are directories)
   *   {
   *     early exit with conflict;
   *   }
   *
   * // Property changes may only be made to up-to-date
   * // directories, because once the client commits the prop
   * // change, it bumps the directory's revision, and therefore
   * // must be able to depend on there being no other changes to
   * // that directory in the repository.
   * if (target's property list differs from ancestor's)
   *    conflict;
   *
   * For each entry NAME in the directory ANCESTOR:
   *
   *   Let ANCESTOR-ENTRY, SOURCE-ENTRY, and TARGET-ENTRY be the IDs of
   *   the name within ANCESTOR, SOURCE, and TARGET respectively.
   *   (Possibly null if NAME does not exist in SOURCE or TARGET.)
   *
   *   If ANCESTOR-ENTRY == SOURCE-ENTRY, then:
   *     No changes were made to this entry while the transaction was in
   *     progress, so do nothing to the target.
   *
   *   Else if ANCESTOR-ENTRY == TARGET-ENTRY, then:
   *     A change was made to this entry while the transaction was in
   *     process, but the transaction did not touch this entry.  Replace
   *     TARGET-ENTRY with SOURCE-ENTRY.
   *
   *   Else:
   *     Changes were made to this entry both within the transaction and
   *     to the repository while the transaction was in progress.  They
   *     must be merged or declared to be in conflict.
   *
   *     If SOURCE-ENTRY and TARGET-ENTRY are both null, that's a
   *     double delete; flag a conflict.
   *
   *     If any of the three entries is of type file, declare a conflict.
   *
   *     If either SOURCE-ENTRY or TARGET-ENTRY is not a direct
   *     modification of ANCESTOR-ENTRY (determine by comparing the
   *     node-id fields), declare a conflict.  A replacement is
   *     incompatible with a modification or other replacement--even
   *     an identical replacement.
   *
   *     Direct modifications were made to the directory ANCESTOR-ENTRY
   *     in both SOURCE and TARGET.  Recursively merge these
   *     modifications.
   *
   * For each leftover entry NAME in the directory SOURCE:
   *
   *   If NAME exists in TARGET, declare a conflict.  Even if SOURCE and
   *   TARGET are adding exactly the same thing, two additions are not
   *   auto-mergeable with each other.
   *
   *   Add NAME to TARGET with the entry from SOURCE.
   *
   * Now that we are done merging the changes from SOURCE into the
   * directory TARGET, update TARGET's predecessor to be SOURCE.
   */

  if ((svn_fs_x__dag_node_kind(source) != svn_node_dir)
      || (svn_fs_x__dag_node_kind(target) != svn_node_dir)
      || (svn_fs_x__dag_node_kind(ancestor) != svn_node_dir))
    {
      return conflict_err(conflict_p, target_path);
    }


  /* Possible early merge failure: if target and ancestor have
     different property lists, then the merge should fail.
     Propchanges can *only* be committed on an up-to-date directory.
     ### TODO: see issue #418 about the inelegance of this.

     Another possible, similar, early merge failure: if source and
     ancestor have different property lists (meaning someone else
     changed directory properties while our commit transaction was
     happening), the merge should fail.  See issue #2751.
  */
  {
    svn_fs_x__noderev_t *tgt_nr, *anc_nr, *src_nr;
    svn_boolean_t same;
    apr_pool_t *scratch_pool;

    /* Get node revisions for our id's. */
    scratch_pool = svn_pool_create(pool);
    SVN_ERR(svn_fs_x__get_node_revision(&tgt_nr, fs, target_id,
                                        pool, scratch_pool));
    svn_pool_clear(scratch_pool);
    SVN_ERR(svn_fs_x__get_node_revision(&anc_nr, fs, ancestor_id,
                                        pool, scratch_pool));
    svn_pool_clear(scratch_pool);
    SVN_ERR(svn_fs_x__get_node_revision(&src_nr, fs, source_id,
                                        pool, scratch_pool));
    svn_pool_destroy(scratch_pool);

    /* Now compare the prop-keys of the skels.  Note that just because
       the keys are different -doesn't- mean the proplists have
       different contents. */
    SVN_ERR(svn_fs_x__prop_rep_equal(&same, fs, src_nr, anc_nr, TRUE, pool));
    if (! same)
      return conflict_err(conflict_p, target_path);

    /* The directory entries got changed in the repository but the directory
       properties did not. */
    SVN_ERR(svn_fs_x__prop_rep_equal(&same, fs, tgt_nr, anc_nr, TRUE, pool));
    if (! same)
      {
        /* There is an incoming prop change for this directory.
           We will accept it only if the directory changes were mere updates
           to its entries, i.e. there were no additions or removals.
           Those could cause update problems to the working copy. */
        svn_boolean_t changed;
        SVN_ERR(compare_dir_structure(&changed, fs, source, ancestor, pool));

        if (changed)
          return conflict_err(conflict_p, target_path);
      }
  }

  /* ### todo: it would be more efficient to simply check for a NULL
     entries hash where necessary below than to allocate an empty hash
     here, but another day, another day... */
  iterpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_x__dag_dir_entries(&s_entries, source, pool, iterpool));
  SVN_ERR(svn_fs_x__dag_dir_entries(&t_entries, target, pool, iterpool));
  SVN_ERR(svn_fs_x__dag_dir_entries(&a_entries, ancestor, pool, iterpool));

  /* for each entry E in a_entries... */
  for (i = 0; i < a_entries->nelts; ++i)
    {
      svn_fs_x__dirent_t *s_entry, *t_entry, *a_entry;
      svn_pool_clear(iterpool);

      a_entry = APR_ARRAY_IDX(a_entries, i, svn_fs_x__dirent_t *);
      s_entry = svn_fs_x__find_dir_entry(s_entries, a_entry->name, &s_idx);
      t_entry = svn_fs_x__find_dir_entry(t_entries, a_entry->name, &t_idx);

      /* No changes were made to this entry while the transaction was
         in progress, so do nothing to the target. */
      if (s_entry && svn_fs_x__id_eq(&a_entry->id, &s_entry->id))
        continue;

      /* A change was made to this entry while the transaction was in
         process, but the transaction did not touch this entry. */
      else if (t_entry && svn_fs_x__id_eq(&a_entry->id, &t_entry->id))
        {
          dag_node_t *t_ent_node;
          SVN_ERR(svn_fs_x__dag_get_node(&t_ent_node, fs, &t_entry->id,
                                         iterpool, iterpool));
          mergeinfo_increment
            -= svn_fs_x__dag_get_mergeinfo_count(t_ent_node);

          if (s_entry)
            {
              dag_node_t *s_ent_node;
              SVN_ERR(svn_fs_x__dag_get_node(&s_ent_node, fs, &s_entry->id,
                                             iterpool, iterpool));

              mergeinfo_increment
                += svn_fs_x__dag_get_mergeinfo_count(s_ent_node);

              SVN_ERR(svn_fs_x__dag_set_entry(target, a_entry->name,
                                              &s_entry->id,
                                              s_entry->kind,
                                              txn_id,
                                              iterpool));
            }
          else
            {
              SVN_ERR(svn_fs_x__dag_delete(target, a_entry->name, txn_id,
                                           iterpool));
            }
        }

      /* Changes were made to this entry both within the transaction
         and to the repository while the transaction was in progress.
         They must be merged or declared to be in conflict. */
      else
        {
          dag_node_t *s_ent_node, *t_ent_node, *a_ent_node;
          const char *new_tpath;
          apr_int64_t sub_mergeinfo_increment;

          /* If SOURCE-ENTRY and TARGET-ENTRY are both null, that's a
             double delete; if one of them is null, that's a delete versus
             a modification. In any of these cases, flag a conflict. */
          if (s_entry == NULL || t_entry == NULL)
            return conflict_err(conflict_p,
                                svn_fspath__join(target_path,
                                                 a_entry->name,
                                                 iterpool));

          /* If any of the three entries is of type file, flag a conflict. */
          if (s_entry->kind == svn_node_file
              || t_entry->kind == svn_node_file
              || a_entry->kind == svn_node_file)
            return conflict_err(conflict_p,
                                svn_fspath__join(target_path,
                                                 a_entry->name,
                                                 iterpool));

          /* Fetch DAG nodes to efficiently access ID parts. */
          SVN_ERR(svn_fs_x__dag_get_node(&s_ent_node, fs, &s_entry->id,
                                         iterpool, iterpool));
          SVN_ERR(svn_fs_x__dag_get_node(&t_ent_node, fs, &t_entry->id,
                                         iterpool, iterpool));
          SVN_ERR(svn_fs_x__dag_get_node(&a_ent_node, fs, &a_entry->id,
                                         iterpool, iterpool));

          /* If either SOURCE-ENTRY or TARGET-ENTRY is not a direct
             modification of ANCESTOR-ENTRY, declare a conflict. */
          if (   !svn_fs_x__dag_same_line_of_history(s_ent_node, a_ent_node)
              || !svn_fs_x__dag_same_line_of_history(t_ent_node, a_ent_node))
            return conflict_err(conflict_p,
                                svn_fspath__join(target_path,
                                                 a_entry->name,
                                                 iterpool));

          /* Direct modifications were made to the directory
             ANCESTOR-ENTRY in both SOURCE and TARGET.  Recursively
             merge these modifications. */
          new_tpath = svn_fspath__join(target_path, t_entry->name, iterpool);
          SVN_ERR(merge(conflict_p, new_tpath,
                        t_ent_node, s_ent_node, a_ent_node,
                        txn_id,
                        &sub_mergeinfo_increment,
                        iterpool));
          mergeinfo_increment += sub_mergeinfo_increment;
        }
    }

  /* For each entry E in source but not in ancestor */
  for (i = 0; i < s_entries->nelts; ++i)
    {
      svn_fs_x__dirent_t *a_entry, *s_entry, *t_entry;
      dag_node_t *s_ent_node;

      svn_pool_clear(iterpool);

      s_entry = APR_ARRAY_IDX(s_entries, i, svn_fs_x__dirent_t *);
      a_entry = svn_fs_x__find_dir_entry(a_entries, s_entry->name, &s_idx);
      t_entry = svn_fs_x__find_dir_entry(t_entries, s_entry->name, &t_idx);

      /* Process only entries in source that are NOT in ancestor. */
      if (a_entry)
        continue;

      /* If NAME exists in TARGET, declare a conflict. */
      if (t_entry)
        return conflict_err(conflict_p,
                            svn_fspath__join(target_path,
                                             t_entry->name,
                                             iterpool));

      SVN_ERR(svn_fs_x__dag_get_node(&s_ent_node, fs, &s_entry->id,
                                     iterpool, iterpool));
      mergeinfo_increment += svn_fs_x__dag_get_mergeinfo_count(s_ent_node);

      SVN_ERR(svn_fs_x__dag_set_entry
              (target, s_entry->name, &s_entry->id, s_entry->kind,
               txn_id, iterpool));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_fs_x__dag_update_ancestry(target, source, pool));

  SVN_ERR(svn_fs_x__dag_increment_mergeinfo_count(target,
                                                  mergeinfo_increment,
                                                  pool));

  if (mergeinfo_increment_out)
    *mergeinfo_increment_out = mergeinfo_increment;

  return SVN_NO_ERROR;
}

/* Merge changes between an ancestor and SOURCE_NODE into
   TXN.  The ancestor is either ANCESTOR_NODE, or if
   that is null, TXN's base node.

   If the merge is successful, TXN's base will become
   SOURCE_NODE, and its root node will have a new ID, a
   successor of SOURCE_NODE.

   If a conflict results, update *CONFLICT to the path in the txn that
   conflicted; see the CONFLICT_P parameter of merge() for details. */
static svn_error_t *
merge_changes(dag_node_t *ancestor_node,
              dag_node_t *source_node,
              svn_fs_txn_t *txn,
              svn_stringbuf_t *conflict,
              apr_pool_t *scratch_pool)
{
  dag_node_t *txn_root_node;
  svn_fs_t *fs = txn->fs;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__txn_get_id(txn);

  SVN_ERR(svn_fs_x__dag_root(&txn_root_node, fs,
                             svn_fs_x__change_set_by_txn(txn_id),
                             scratch_pool, scratch_pool));

  if (ancestor_node == NULL)
    {
      svn_revnum_t base_rev;
      SVN_ERR(svn_fs_x__get_base_rev(&base_rev, fs, txn_id, scratch_pool));
      SVN_ERR(svn_fs_x__dag_root(&ancestor_node, fs,
                                 svn_fs_x__change_set_by_rev(base_rev),
                                 scratch_pool, scratch_pool));
    }

  if (!svn_fs_x__dag_related_node(ancestor_node, txn_root_node))
    {
      /* If no changes have been made in TXN since its current base,
         then it can't conflict with any changes since that base.
         The caller isn't supposed to call us in that case. */
      SVN_ERR_MALFUNCTION();
    }
  else
    SVN_ERR(merge(conflict, "/", txn_root_node,
                  source_node, ancestor_node, txn_id, NULL, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__commit_txn(const char **conflict_p,
                     svn_revnum_t *new_rev,
                     svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  /* How do commits work in Subversion?
   *
   * When you're ready to commit, here's what you have:
   *
   *    1. A transaction, with a mutable tree hanging off it.
   *    2. A base revision, against which TXN_TREE was made.
   *    3. A latest revision, which may be newer than the base rev.
   *
   * The problem is that if latest != base, then one can't simply
   * attach the txn root as the root of the new revision, because that
   * would lose all the changes between base and latest.  It is also
   * not acceptable to insist that base == latest; in a busy
   * repository, commits happen too fast to insist that everyone keep
   * their entire tree up-to-date at all times.  Non-overlapping
   * changes should not interfere with each other.
   *
   * The solution is to merge the changes between base and latest into
   * the txn tree [see the function merge()].  The txn tree is the
   * only one of the three trees that is mutable, so it has to be the
   * one to adjust.
   *
   * You might have to adjust it more than once, if a new latest
   * revision gets committed while you were merging in the previous
   * one.  For example:
   *
   *    1. Jane starts txn T, based at revision 6.
   *    2. Someone commits (or already committed) revision 7.
   *    3. Jane's starts merging the changes between 6 and 7 into T.
   *    4. Meanwhile, someone commits revision 8.
   *    5. Jane finishes the 6-->7 merge.  T could now be committed
   *       against a latest revision of 7, if only that were still the
   *       latest.  Unfortunately, 8 is now the latest, so...
   *    6. Jane starts merging the changes between 7 and 8 into T.
   *    7. Meanwhile, no one commits any new revisions.  Whew.
   *    8. Jane commits T, creating revision 9, whose tree is exactly
   *       T's tree, except immutable now.
   *
   * Lather, rinse, repeat.
   */

  svn_error_t *err = SVN_NO_ERROR;
  svn_stringbuf_t *conflict = svn_stringbuf_create_empty(pool);
  svn_fs_t *fs = txn->fs;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  /* Limit memory usage when the repository has a high commit rate and
     needs to run the following while loop multiple times.  The memory
     growth without an iteration pool is very noticeable when the
     transaction modifies a node that has 20,000 sibling nodes. */
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Initialize output params. */
  *new_rev = SVN_INVALID_REVNUM;
  if (conflict_p)
    *conflict_p = NULL;

  while (1729)
    {
      svn_revnum_t youngish_rev;
      svn_fs_root_t *youngish_root;
      dag_node_t *youngish_root_node;

      svn_pool_clear(iterpool);

      /* Get the *current* youngest revision.  We call it "youngish"
         because new revisions might get committed after we've
         obtained it. */

      SVN_ERR(svn_fs_x__youngest_rev(&youngish_rev, fs, iterpool));
      SVN_ERR(svn_fs_x__revision_root(&youngish_root, fs, youngish_rev,
                                      iterpool));

      /* Get the dag node for the youngest revision.  Later we'll use
         it as the SOURCE argument to a merge, and if the merge
         succeeds, this youngest root node will become the new base
         root for the svn txn that was the target of the merge (but
         note that the youngest rev may have changed by then -- that's
         why we're careful to get this root in its own bdb txn
         here). */
      SVN_ERR(get_root(&youngish_root_node, youngish_root, iterpool,
                       iterpool));

      /* Try to merge.  If the merge succeeds, the base root node of
         TARGET's txn will become the same as youngish_root_node, so
         any future merges will only be between that node and whatever
         the root node of the youngest rev is by then. */
      err = merge_changes(NULL, youngish_root_node, txn, conflict, iterpool);
      if (err)
        {
          if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
            *conflict_p = conflict->data;
          goto cleanup;
        }
      txn->base_rev = youngish_rev;

      /* Try to commit. */
      err = svn_fs_x__commit(new_rev, fs, txn, iterpool);
      if (err && (err->apr_err == SVN_ERR_FS_TXN_OUT_OF_DATE))
        {
          /* Did someone else finish committing a new revision while we
             were in mid-merge or mid-commit?  If so, we'll need to
             loop again to merge the new changes in, then try to
             commit again.  Or if that's not what happened, then just
             return the error. */
          svn_revnum_t youngest_rev;
          SVN_ERR(svn_fs_x__youngest_rev(&youngest_rev, fs, iterpool));
          if (youngest_rev == youngish_rev)
            goto cleanup;
          else
            svn_error_clear(err);
        }
      else if (err)
        {
          goto cleanup;
        }
      else
        {
          err = SVN_NO_ERROR;
          goto cleanup;
        }
    }

 cleanup:

  svn_pool_destroy(iterpool);

  SVN_ERR(err);

  if (ffd->pack_after_commit)
    {
      SVN_ERR(svn_fs_x__pack(fs, 0, NULL, NULL, NULL, NULL, pool));
    }

  return SVN_NO_ERROR;
}


/* Merge changes between two nodes into a third node.  Given nodes
   SOURCE_PATH under SOURCE_ROOT, TARGET_PATH under TARGET_ROOT and
   ANCESTOR_PATH under ANCESTOR_ROOT, modify target to contain all the
   changes between the ancestor and source.  If there are conflicts,
   return SVN_ERR_FS_CONFLICT and set *CONFLICT_P to a textual
   description of the offending changes.  Perform any temporary
   allocations in POOL. */
static svn_error_t *
x_merge(const char **conflict_p,
        svn_fs_root_t *source_root,
        const char *source_path,
        svn_fs_root_t *target_root,
        const char *target_path,
        svn_fs_root_t *ancestor_root,
        const char *ancestor_path,
        apr_pool_t *pool)
{
  dag_node_t *source, *ancestor;
  svn_fs_txn_t *txn;
  svn_error_t *err;
  svn_stringbuf_t *conflict = svn_stringbuf_create_empty(pool);

  if (! target_root->is_txn_root)
    return SVN_FS__NOT_TXN(target_root);

  /* Paranoia. */
  if ((source_root->fs != ancestor_root->fs)
      || (target_root->fs != ancestor_root->fs))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, NULL,
         _("Bad merge; ancestor, source, and target not all in same fs"));
    }

  /* ### kff todo: is there any compelling reason to get the nodes in
     one db transaction?  Right now we don't; txn_body_get_root() gets
     one node at a time.  This will probably need to change:

     Jim Blandy <jimb@zwingli.cygnus.com> writes:
     > svn_fs_merge needs to be a single transaction, to protect it against
     > people deleting parents of nodes it's working on, etc.
  */

  /* Get the ancestor node. */
  SVN_ERR(get_root(&ancestor, ancestor_root, pool, pool));

  /* Get the source node. */
  SVN_ERR(get_root(&source, source_root, pool, pool));

  /* Open a txn for the txn root into which we're merging. */
  SVN_ERR(svn_fs_x__open_txn(&txn, ancestor_root->fs, target_root->txn,
                             pool));

  /* Merge changes between ANCESTOR and SOURCE into TXN. */
  err = merge_changes(ancestor, source, txn, conflict, pool);
  if (err)
    {
      if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
        *conflict_p = conflict->data;
      return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__deltify(svn_fs_t *fs,
                  svn_revnum_t revision,
                  apr_pool_t *scratch_pool)
{
  /* Deltify is a no-op for fs_x. */

  return SVN_NO_ERROR;
}



/* Directories.  */

/* Set *TABLE_P to a newly allocated APR hash table containing the
   entries of the directory at PATH in ROOT.  The keys of the table
   are entry names, as byte strings, excluding the final null
   character; the table's values are pointers to svn_fs_svn_fs_x__dirent_t
   structures.  Allocate the table and its contents in POOL. */
static svn_error_t *
x_dir_entries(apr_hash_t **table_p,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool)
{
  dag_node_t *node;
  apr_hash_t *hash = svn_hash__make(pool);
  apr_array_header_t *table;
  int i;
  svn_fs_x__id_context_t *context = NULL;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  /* Get the entries for this path in the caller's pool. */
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
  SVN_ERR(svn_fs_x__dag_dir_entries(&table, node, scratch_pool,
                                    scratch_pool));

  if (table->nelts)
    context = svn_fs_x__id_create_context(root->fs, pool);

  /* Convert directory array to hash. */
  for (i = 0; i < table->nelts; ++i)
    {
      svn_fs_x__dirent_t *entry
        = APR_ARRAY_IDX(table, i, svn_fs_x__dirent_t *);
      apr_size_t len = strlen(entry->name);

      svn_fs_dirent_t *api_dirent = apr_pcalloc(pool, sizeof(*api_dirent));
      api_dirent->name = apr_pstrmemdup(pool, entry->name, len);
      api_dirent->kind = entry->kind;
      api_dirent->id = svn_fs_x__id_create(context, &entry->id, pool);

      apr_hash_set(hash, api_dirent->name, len, api_dirent);
    }

  *table_p = hash;
  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
x_dir_optimal_order(apr_array_header_t **ordered_p,
                    svn_fs_root_t *root,
                    apr_hash_t *entries,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  *ordered_p = svn_fs_x__order_dir_entries(root->fs, entries, result_pool,
                                           scratch_pool);

  return SVN_NO_ERROR;
}

/* Create a new directory named PATH in ROOT.  The new directory has
   no entries, and no properties.  ROOT must be the root of a
   transaction, not a revision.  Do any necessary temporary allocation
   in SCRATCH_POOL.  */
static svn_error_t *
x_make_dir(svn_fs_root_t *root,
           const char *path,
           apr_pool_t *scratch_pool)
{
  svn_fs_x__dag_path_t *dag_path;
  dag_node_t *sub_dir;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(root);
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path,
                                 svn_fs_x__dag_path_last_optional,
                                 TRUE, subpool, subpool));

  /* Check (recursively) to see if some lock is 'reserving' a path at
     that location, or even some child-path; if so, check that we can
     use it. */
  if (root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(path, root->fs, TRUE, FALSE,
                                             subpool));

  /* If there's already a sub-directory by that name, complain.  This
     also catches the case of trying to make a subdirectory named `/'.  */
  if (dag_path->node)
    return SVN_FS__ALREADY_EXISTS(root, path);

  /* Create the subdirectory.  */
  SVN_ERR(svn_fs_x__make_path_mutable(root, dag_path->parent, path, subpool,
                                      subpool));
  SVN_ERR(svn_fs_x__dag_make_dir(&sub_dir,
                                 dag_path->parent->node,
                                 parent_path_path(dag_path->parent,
                                                  subpool),
                                 dag_path->entry,
                                 txn_id,
                                 subpool, subpool));

  /* Add this directory to the path cache. */
  svn_fs_x__update_dag_cache(sub_dir);

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_path_change_add, FALSE, FALSE, FALSE,
                     svn_node_dir, SVN_INVALID_REVNUM, NULL, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Delete the node at PATH under ROOT.  ROOT must be a transaction
   root.  Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
x_delete_node(svn_fs_root_t *root,
              const char *path,
              apr_pool_t *scratch_pool)
{
  svn_fs_x__dag_path_t *dag_path;
  svn_fs_x__txn_id_t txn_id;
  apr_int64_t mergeinfo_count = 0;
  svn_node_kind_t kind;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  if (! root->is_txn_root)
    return SVN_FS__NOT_TXN(root);

  txn_id = svn_fs_x__root_txn_id(root);
  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path, 0, TRUE, subpool,
                                 subpool));
  kind = svn_fs_x__dag_node_kind(dag_path->node);

  /* We can't remove the root of the filesystem.  */
  if (! dag_path->parent)
    return svn_error_create(SVN_ERR_FS_ROOT_DIR, NULL,
                            _("The root directory cannot be deleted"));

  /* Check to see if path (or any child thereof) is locked; if so,
     check that we can use the existing lock(s). */
  if (root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(path, root->fs, TRUE, FALSE,
                                             subpool));

  /* Make the parent directory mutable, and do the deletion.  */
  SVN_ERR(svn_fs_x__make_path_mutable(root, dag_path->parent, path, subpool,
                                      subpool));
  mergeinfo_count = svn_fs_x__dag_get_mergeinfo_count(dag_path->node);
  SVN_ERR(svn_fs_x__dag_delete(dag_path->parent->node,
                               dag_path->entry,
                               txn_id, subpool));

  /* Remove this node and any children from the path cache. */
  svn_fs_x__invalidate_dag_cache(root, parent_path_path(dag_path, subpool));

  /* Update mergeinfo counts for parents */
  if (mergeinfo_count > 0)
    SVN_ERR(increment_mergeinfo_up_tree(dag_path->parent,
                                        -mergeinfo_count,
                                        subpool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_path_change_delete, FALSE, FALSE, FALSE, kind,
                     SVN_INVALID_REVNUM, NULL, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Set *SAME_P to TRUE if FS1 and FS2 have the same UUID, else set to FALSE.
   Use SCRATCH_POOL for temporary allocation only.
   Note: this code is duplicated between libsvn_fs_x and libsvn_fs_base. */
static svn_error_t *
x_same_p(svn_boolean_t *same_p,
         svn_fs_t *fs1,
         svn_fs_t *fs2,
         apr_pool_t *scratch_pool)
{
  *same_p = ! strcmp(fs1->uuid, fs2->uuid);
  return SVN_NO_ERROR;
}

/* Copy the node at FROM_PATH under FROM_ROOT to TO_PATH under
   TO_ROOT.  If PRESERVE_HISTORY is set, then the copy is recorded in
   the copies table.  Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
copy_helper(svn_fs_root_t *from_root,
            const char *from_path,
            svn_fs_root_t *to_root,
            const char *to_path,
            svn_boolean_t preserve_history,
            apr_pool_t *scratch_pool)
{
  dag_node_t *from_node;
  svn_fs_x__dag_path_t *to_dag_path;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(to_root);
  svn_boolean_t same_p;

  /* Use an error check, not an assert, because even the caller cannot
     guarantee that a filesystem's UUID has not changed "on the fly". */
  SVN_ERR(x_same_p(&same_p, from_root->fs, to_root->fs, scratch_pool));
  if (! same_p)
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy between two different filesystems ('%s' and '%s')"),
       from_root->fs->path, to_root->fs->path);

  /* more things that we can't do ATM */
  if (from_root->is_txn_root)
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Copy from mutable tree not currently supported"));

  if (! to_root->is_txn_root)
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Copy immutable tree not supported"));

  /* Get the NODE for FROM_PATH in FROM_ROOT.*/
  SVN_ERR(svn_fs_x__get_dag_node(&from_node, from_root, from_path,
                                 scratch_pool, scratch_pool));

  /* Build up the parent path from TO_PATH in TO_ROOT.  If the last
     component does not exist, it's not that big a deal.  We'll just
     make one there. */
  SVN_ERR(svn_fs_x__get_dag_path(&to_dag_path, to_root, to_path,
                                 svn_fs_x__dag_path_last_optional, TRUE,
                                 scratch_pool, scratch_pool));

  /* Check to see if path (or any child thereof) is locked; if so,
     check that we can use the existing lock(s). */
  if (to_root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(to_path, to_root->fs,
                                             TRUE, FALSE, scratch_pool));

  /* If the destination node already exists as the same node as the
     source (in other words, this operation would result in nothing
     happening at all), just do nothing an return successfully,
     proud that you saved yourself from a tiresome task. */
  if (to_dag_path->node &&
      svn_fs_x__id_eq(svn_fs_x__dag_get_id(from_node),
                      svn_fs_x__dag_get_id(to_dag_path->node)))
    return SVN_NO_ERROR;

  if (! from_root->is_txn_root)
    {
      svn_fs_path_change_kind_t kind;
      dag_node_t *new_node;
      const char *from_canonpath;
      apr_int64_t mergeinfo_start;
      apr_int64_t mergeinfo_end;

      /* If TO_PATH already existed prior to the copy, note that this
         operation is a replacement, not an addition. */
      if (to_dag_path->node)
        {
          kind = svn_fs_path_change_replace;
          mergeinfo_start
            = svn_fs_x__dag_get_mergeinfo_count(to_dag_path->node);
        }
      else
        {
          kind = svn_fs_path_change_add;
          mergeinfo_start = 0;
        }

      mergeinfo_end = svn_fs_x__dag_get_mergeinfo_count(from_node);

      /* Make sure the target node's parents are mutable.  */
      SVN_ERR(svn_fs_x__make_path_mutable(to_root, to_dag_path->parent,
                                          to_path, scratch_pool,
                                          scratch_pool));

      /* Canonicalize the copyfrom path. */
      from_canonpath = svn_fs__canonicalize_abspath(from_path, scratch_pool);

      SVN_ERR(svn_fs_x__dag_copy(to_dag_path->parent->node,
                                 to_dag_path->entry,
                                 from_node,
                                 preserve_history,
                                 from_root->rev,
                                 from_canonpath,
                                 txn_id, scratch_pool));

      if (kind != svn_fs_path_change_add)
        svn_fs_x__invalidate_dag_cache(to_root,
                                       parent_path_path(to_dag_path,
                                                        scratch_pool));

      if (mergeinfo_start != mergeinfo_end)
        SVN_ERR(increment_mergeinfo_up_tree(to_dag_path->parent,
                                            mergeinfo_end - mergeinfo_start,
                                            scratch_pool));

      /* Make a record of this modification in the changes table. */
      SVN_ERR(svn_fs_x__get_dag_node(&new_node, to_root, to_path,
                                     scratch_pool, scratch_pool));
      SVN_ERR(add_change(to_root->fs, txn_id, to_path, kind, FALSE,
                         FALSE, FALSE, svn_fs_x__dag_node_kind(from_node),
                         from_root->rev, from_canonpath, scratch_pool));
    }
  else
    {
      /* See IZ Issue #436 */
      /* Copying from transaction roots not currently available.

         ### cmpilato todo someday: make this not so. :-) Note that
         when copying from mutable trees, you have to make sure that
         you aren't creating a cyclic graph filesystem, and a simple
         referencing operation won't cut it.  Currently, we should not
         be able to reach this clause, and the interface reports that
         this only works from immutable trees anyway, but JimB has
         stated that this requirement need not be necessary in the
         future. */

      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}


/* Create a copy of FROM_PATH in FROM_ROOT named TO_PATH in TO_ROOT.
   If FROM_PATH is a directory, copy it recursively.  Temporary
   allocations are from SCRATCH_POOL.*/
static svn_error_t *
x_copy(svn_fs_root_t *from_root,
       const char *from_path,
       svn_fs_root_t *to_root,
       const char *to_path,
       apr_pool_t *scratch_pool)
{
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  SVN_ERR(copy_helper(from_root,
                      svn_fs__canonicalize_abspath(from_path, subpool),
                      to_root,
                      svn_fs__canonicalize_abspath(to_path, subpool),
                      TRUE, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Create a copy of FROM_PATH in FROM_ROOT named TO_PATH in TO_ROOT.
   If FROM_PATH is a directory, copy it recursively.  No history is
   preserved.  Temporary allocations are from SCRATCH_POOL. */
static svn_error_t *
x_revision_link(svn_fs_root_t *from_root,
                svn_fs_root_t *to_root,
                const char *path,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *subpool;

  if (! to_root->is_txn_root)
    return SVN_FS__NOT_TXN(to_root);

  subpool = svn_pool_create(scratch_pool);

  path = svn_fs__canonicalize_abspath(path, subpool);
  SVN_ERR(copy_helper(from_root, path, to_root, path, FALSE, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Discover the copy ancestry of PATH under ROOT.  Return a relevant
   ancestor/revision combination in *PATH_P and *REV_P.  Temporary
   allocations are in POOL. */
static svn_error_t *
x_copied_from(svn_revnum_t *rev_p,
              const char **path_p,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool)
{
  dag_node_t *node;

  /* There is no cached entry, look it up the old-fashioned way. */
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, pool));
  *rev_p = svn_fs_x__dag_get_copyfrom_rev(node);
  *path_p = svn_fs_x__dag_get_copyfrom_path(node);

  return SVN_NO_ERROR;
}



/* Files.  */

/* Create the empty file PATH under ROOT.  Temporary allocations are
   in SCRATCH_POOL. */
static svn_error_t *
x_make_file(svn_fs_root_t *root,
            const char *path,
            apr_pool_t *scratch_pool)
{
  svn_fs_x__dag_path_t *dag_path;
  dag_node_t *child;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(root);
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path,
                                 svn_fs_x__dag_path_last_optional,
                                 TRUE, subpool, subpool));

  /* If there's already a file by that name, complain.
     This also catches the case of trying to make a file named `/'.  */
  if (dag_path->node)
    return SVN_FS__ALREADY_EXISTS(root, path);

  /* Check (non-recursively) to see if path is locked;  if so, check
     that we can use it. */
  if (root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(path, root->fs, FALSE, FALSE,
                                             subpool));

  /* Create the file.  */
  SVN_ERR(svn_fs_x__make_path_mutable(root, dag_path->parent, path, subpool,
                                      subpool));
  SVN_ERR(svn_fs_x__dag_make_file(&child,
                                  dag_path->parent->node,
                                  parent_path_path(dag_path->parent,
                                                   subpool),
                                  dag_path->entry,
                                  txn_id,
                                  subpool, subpool));

  /* Add this file to the path cache. */
  svn_fs_x__update_dag_cache(child);

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_path_change_add, TRUE, FALSE, FALSE,
                     svn_node_file, SVN_INVALID_REVNUM, NULL, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Set *LENGTH_P to the size of the file PATH under ROOT.  Temporary
   allocations are in SCRATCH_POOL. */
static svn_error_t *
x_file_length(svn_filesize_t *length_p,
              svn_fs_root_t *root,
              const char *path,
              apr_pool_t *scratch_pool)
{
  dag_node_t *file;

  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR(svn_fs_x__get_temp_dag_node(&file, root, path, scratch_pool));

  /* Now fetch its length */
  return svn_fs_x__dag_file_length(length_p, file);
}


/* Set *CHECKSUM to the checksum of type KIND for PATH under ROOT, or
   NULL if that information isn't available.  Temporary allocations
   are from POOL. */
static svn_error_t *
x_file_checksum(svn_checksum_t **checksum,
                svn_checksum_kind_t kind,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  dag_node_t *file;

  SVN_ERR(svn_fs_x__get_temp_dag_node(&file, root, path, pool));
  return svn_fs_x__dag_file_checksum(checksum, file, kind, pool);
}


/* --- Machinery for svn_fs_file_contents() ---  */

/* Set *CONTENTS to a readable stream that will return the contents of
   PATH under ROOT.  The stream is allocated in POOL. */
static svn_error_t *
x_file_contents(svn_stream_t **contents,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  dag_node_t *node;
  svn_stream_t *file_stream;

  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, pool));

  /* Then create a readable stream from the dag_node_t. */
  SVN_ERR(svn_fs_x__dag_get_contents(&file_stream, node, pool));

  *contents = file_stream;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_file_contents() ---  */


/* --- Machinery for svn_fs_try_process_file_contents() ---  */

static svn_error_t *
x_try_process_file_contents(svn_boolean_t *success,
                            svn_fs_root_t *root,
                            const char *path,
                            svn_fs_process_contents_func_t processor,
                            void* baton,
                            apr_pool_t *pool)
{
  dag_node_t *node;
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, pool));

  return svn_fs_x__dag_try_process_file_contents(success, node,
                                                 processor, baton, pool);
}

/* --- End machinery for svn_fs_try_process_file_contents() ---  */


/* --- Machinery for svn_fs_apply_textdelta() ---  */


/* Local baton type for all the helper functions below. */
typedef struct txdelta_baton_t
{
  /* This is the custom-built window consumer given to us by the delta
     library;  it uniquely knows how to read data from our designated
     "source" stream, interpret the window, and write data to our
     designated "target" stream (in this case, our repos file.) */
  svn_txdelta_window_handler_t interpreter;
  void *interpreter_baton;

  /* The original file info */
  svn_fs_root_t *root;
  const char *path;

  /* Derived from the file info */
  dag_node_t *node;

  svn_stream_t *source_stream;
  svn_stream_t *target_stream;

  /* MD5 digest for the base text against which a delta is to be
     applied, and for the resultant fulltext, respectively.  Either or
     both may be null, in which case ignored. */
  svn_checksum_t *base_checksum;
  svn_checksum_t *result_checksum;

  /* Pool used by db txns */
  apr_pool_t *pool;

} txdelta_baton_t;


/* The main window handler returned by svn_fs_apply_textdelta. */
static svn_error_t *
window_consumer(svn_txdelta_window_t *window, void *baton)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;

  /* Send the window right through to the custom window interpreter.
     In theory, the interpreter will then write more data to
     cb->target_string. */
  SVN_ERR(tb->interpreter(window, tb->interpreter_baton));

  /* Is the window NULL?  If so, we're done.  The stream has already been
     closed by the interpreter. */
  if (! window)
    SVN_ERR(svn_fs_x__dag_finalize_edits(tb->node, tb->result_checksum,
                                         tb->pool));

  return SVN_NO_ERROR;
}

/* Helper function for fs_apply_textdelta.  BATON is of type
   txdelta_baton_t. */
static svn_error_t *
apply_textdelta(void *baton,
                apr_pool_t *scratch_pool)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  svn_fs_x__dag_path_t *dag_path;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(tb->root);

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, tb->root, tb->path, 0, TRUE,
                                 scratch_pool, scratch_pool));

  /* Check (non-recursively) to see if path is locked; if so, check
     that we can use it. */
  if (tb->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(tb->path, tb->root->fs,
                                             FALSE, FALSE, scratch_pool));

  /* Now, make sure this path is mutable. */
  SVN_ERR(svn_fs_x__make_path_mutable(tb->root, dag_path, tb->path,
                                      scratch_pool, scratch_pool));
  tb->node = svn_fs_x__dag_dup(dag_path->node, tb->pool);

  if (tb->base_checksum)
    {
      svn_checksum_t *checksum;

      /* Until we finalize the node, its data_key points to the old
         contents, in other words, the base text. */
      SVN_ERR(svn_fs_x__dag_file_checksum(&checksum, tb->node,
                                          tb->base_checksum->kind,
                                          scratch_pool));
      if (!svn_checksum_match(tb->base_checksum, checksum))
        return svn_checksum_mismatch_err(tb->base_checksum, checksum,
                                         scratch_pool,
                                         _("Base checksum mismatch on '%s'"),
                                         tb->path);
    }

  /* Make a readable "source" stream out of the current contents of
     ROOT/PATH; obviously, this must done in the context of a db_txn.
     The stream is returned in tb->source_stream. */
  SVN_ERR(svn_fs_x__dag_get_contents(&(tb->source_stream),
                                     tb->node, tb->pool));

  /* Make a writable "target" stream */
  SVN_ERR(svn_fs_x__dag_get_edit_stream(&(tb->target_stream), tb->node,
                                        tb->pool));

  /* Now, create a custom window handler that uses our two streams. */
  svn_txdelta_apply(tb->source_stream,
                    tb->target_stream,
                    NULL,
                    tb->path,
                    tb->pool,
                    &(tb->interpreter),
                    &(tb->interpreter_baton));

  /* Make a record of this modification in the changes table. */
  return add_change(tb->root->fs, txn_id, tb->path,
                    svn_fs_path_change_modify, TRUE, FALSE, FALSE,
                    svn_node_file, SVN_INVALID_REVNUM, NULL, scratch_pool);
}


/* Set *CONTENTS_P and *CONTENTS_BATON_P to a window handler and baton
   that will accept text delta windows to modify the contents of PATH
   under ROOT.  Allocations are in POOL. */
static svn_error_t *
x_apply_textdelta(svn_txdelta_window_handler_t *contents_p,
                  void **contents_baton_p,
                  svn_fs_root_t *root,
                  const char *path,
                  svn_checksum_t *base_checksum,
                  svn_checksum_t *result_checksum,
                  apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  txdelta_baton_t *tb = apr_pcalloc(pool, sizeof(*tb));

  tb->root = root;
  tb->path = svn_fs__canonicalize_abspath(path, pool);
  tb->pool = pool;
  tb->base_checksum = svn_checksum_dup(base_checksum, pool);
  tb->result_checksum = svn_checksum_dup(result_checksum, pool);

  SVN_ERR(apply_textdelta(tb, scratch_pool));

  *contents_p = window_consumer;
  *contents_baton_p = tb;

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_textdelta() ---  */

/* --- Machinery for svn_fs_apply_text() ---  */

/* Baton for svn_fs_apply_text(). */
typedef struct text_baton_t
{
  /* The original file info */
  svn_fs_root_t *root;
  const char *path;

  /* Derived from the file info */
  dag_node_t *node;

  /* The returned stream that will accept the file's new contents. */
  svn_stream_t *stream;

  /* The actual fs stream that the returned stream will write to. */
  svn_stream_t *file_stream;

  /* MD5 digest for the final fulltext written to the file.  May
     be null, in which case ignored. */
  svn_checksum_t *result_checksum;

  /* Pool used by db txns */
  apr_pool_t *pool;
} text_baton_t;


/* A wrapper around svn_fs_x__dag_finalize_edits, but for
 * fulltext data, not text deltas.  Closes BATON->file_stream.
 *
 * Note: If you're confused about how this function relates to another
 * of similar name, think of it this way:
 *
 * svn_fs_apply_textdelta() ==> ... ==> txn_body_txdelta_finalize_edits()
 * svn_fs_apply_text()      ==> ... ==> txn_body_fulltext_finalize_edits()
 */

/* Write function for the publically returned stream. */
static svn_error_t *
text_stream_writer(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  text_baton_t *tb = baton;

  /* Psst, here's some data.  Pass it on to the -real- file stream. */
  return svn_stream_write(tb->file_stream, data, len);
}

/* Close function for the publically returned stream. */
static svn_error_t *
text_stream_closer(void *baton)
{
  text_baton_t *tb = baton;

  /* Close the internal-use stream.  ### This used to be inside of
     txn_body_fulltext_finalize_edits(), but that invoked a nested
     Berkeley DB transaction -- scandalous! */
  SVN_ERR(svn_stream_close(tb->file_stream));

  /* Need to tell fs that we're done sending text */
  return svn_fs_x__dag_finalize_edits(tb->node, tb->result_checksum,
                                       tb->pool);
}


/* Helper function for fs_apply_text.  BATON is of type
   text_baton_t. */
static svn_error_t *
apply_text(void *baton,
           apr_pool_t *scratch_pool)
{
  text_baton_t *tb = baton;
  svn_fs_x__dag_path_t *dag_path;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(tb->root);

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, tb->root, tb->path, 0, TRUE,
                                 scratch_pool, scratch_pool));

  /* Check (non-recursively) to see if path is locked; if so, check
     that we can use it. */
  if (tb->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_x__allow_locked_operation(tb->path, tb->root->fs,
                                             FALSE, FALSE, scratch_pool));

  /* Now, make sure this path is mutable. */
  SVN_ERR(svn_fs_x__make_path_mutable(tb->root, dag_path, tb->path,
                                      scratch_pool, scratch_pool));
  tb->node = svn_fs_x__dag_dup(dag_path->node, tb->pool);

  /* Make a writable stream for replacing the file's text. */
  SVN_ERR(svn_fs_x__dag_get_edit_stream(&(tb->file_stream), tb->node,
                                        tb->pool));

  /* Create a 'returnable' stream which writes to the file_stream. */
  tb->stream = svn_stream_create(tb, tb->pool);
  svn_stream_set_write(tb->stream, text_stream_writer);
  svn_stream_set_close(tb->stream, text_stream_closer);

  /* Make a record of this modification in the changes table. */
  return add_change(tb->root->fs, txn_id, tb->path,
                    svn_fs_path_change_modify, TRUE, FALSE, FALSE,
                    svn_node_file, SVN_INVALID_REVNUM, NULL, scratch_pool);
}


/* Return a writable stream that will set the contents of PATH under
   ROOT.  RESULT_CHECKSUM is the MD5 checksum of the final result.
   Temporary allocations are in POOL. */
static svn_error_t *
x_apply_text(svn_stream_t **contents_p,
             svn_fs_root_t *root,
             const char *path,
             svn_checksum_t *result_checksum,
             apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  text_baton_t *tb = apr_pcalloc(pool, sizeof(*tb));

  tb->root = root;
  tb->path = svn_fs__canonicalize_abspath(path, pool);
  tb->pool = pool;
  tb->result_checksum = svn_checksum_dup(result_checksum, pool);

  SVN_ERR(apply_text(tb, scratch_pool));

  *contents_p = tb->stream;

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_text() ---  */


/* Check if the contents of PATH1 under ROOT1 are different from the
   contents of PATH2 under ROOT2.  If they are different set
   *CHANGED_P to TRUE, otherwise set it to FALSE. */
static svn_error_t *
x_contents_changed(svn_boolean_t *changed_p,
                   svn_fs_root_t *root1,
                   const char *path1,
                   svn_fs_root_t *root2,
                   const char *path2,
                   svn_boolean_t strict,
                   apr_pool_t *scratch_pool)
{
  dag_node_t *node1, *node2;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Check that roots are in the same fs. */
  if (root1->fs != root2->fs)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       _("Cannot compare file contents between two different filesystems"));

  SVN_ERR(svn_fs_x__get_dag_node(&node1, root1, path1, subpool, subpool));
  /* Make sure that path is file. */
  if (svn_fs_x__dag_node_kind(node1) != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL, _("'%s' is not a file"), path1);

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node2, root2, path2, subpool));
  /* Make sure that path is file. */
  if (svn_fs_x__dag_node_kind(node2) != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL, _("'%s' is not a file"), path2);

  SVN_ERR(svn_fs_x__dag_things_different(NULL, changed_p, node1, node2,
                                         strict, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}



/* Public interface to computing file text deltas.  */

static svn_error_t *
x_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                        svn_fs_root_t *source_root,
                        const char *source_path,
                        svn_fs_root_t *target_root,
                        const char *target_path,
                        apr_pool_t *pool)
{
  dag_node_t *source_node, *target_node;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  if (source_root && source_path)
    SVN_ERR(svn_fs_x__get_dag_node(&source_node, source_root, source_path,
                                   scratch_pool, scratch_pool));
  else
    source_node = NULL;
  SVN_ERR(svn_fs_x__get_temp_dag_node(&target_node, target_root, target_path,
                                      scratch_pool));

  /* Create a delta stream that turns the source into the target.  */
  SVN_ERR(svn_fs_x__dag_get_file_delta_stream(stream_p, source_node,
                                              target_node, pool,
                                              scratch_pool));

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}



/* Finding Changes */

/* Implement changes_iterator_vtable_t.get for in-txn change lists.
   There is no specific FSAP data type, a simple APR hash iterator
   to the underlying collection is sufficient. */
static svn_error_t *
x_txn_changes_iterator_get(svn_fs_path_change3_t **change,
                           svn_fs_path_change_iterator_t *iterator)
{
  apr_hash_index_t *hi = iterator->fsap_data;

  if (hi)
    {
      *change = apr_hash_this_val(hi);
      iterator->fsap_data = apr_hash_next(hi);
    }
  else
    {
      *change = NULL;
    }

  return SVN_NO_ERROR;
}

static changes_iterator_vtable_t txn_changes_iterator_vtable =
{
  x_txn_changes_iterator_get
};

/* FSAP data structure for in-revision changes list iterators. */
typedef struct fs_revision_changes_iterator_data_t
{
  /* Context that tells the lower layers from where to fetch the next
     block of changes. */
  svn_fs_x__changes_context_t *context;

  /* Changes to send. */
  apr_array_header_t *changes;

  /* Current indexes within CHANGES. */
  int idx;

  /* A cleanable scratch pool in case we need one.
     No further sub-pool creation necessary. */
  apr_pool_t *scratch_pool;
} fs_revision_changes_iterator_data_t;

static svn_error_t *
x_revision_changes_iterator_get(svn_fs_path_change3_t **change,
                                svn_fs_path_change_iterator_t *iterator)
{
  fs_revision_changes_iterator_data_t *data = iterator->fsap_data;

  /* If we exhausted our block of changes and did not reach the end of the
     list, yet, fetch the next block.  Note that that block may be empty. */
  if ((data->idx >= data->changes->nelts) && !data->context->eol)
    {
      apr_pool_t *changes_pool = data->changes->pool;

      /* Drop old changes block, read new block. */
      svn_pool_clear(changes_pool);
      SVN_ERR(svn_fs_x__get_changes(&data->changes, data->context,
                                    changes_pool, data->scratch_pool));
      data->idx = 0;

      /* Immediately release any temporary data. */
      svn_pool_clear(data->scratch_pool);
    }

  if (data->idx < data->changes->nelts)
    {
      *change = APR_ARRAY_IDX(data->changes, data->idx,
                              svn_fs_x__change_t *);
      ++data->idx;
    }
  else
    {
      *change = NULL;
    }

  return SVN_NO_ERROR;
}

static changes_iterator_vtable_t rev_changes_iterator_vtable =
{
  x_revision_changes_iterator_get
};

static svn_error_t *
x_report_changes(svn_fs_path_change_iterator_t **iterator,
                 svn_fs_root_t *root,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_fs_path_change_iterator_t *result = apr_pcalloc(result_pool,
                                                      sizeof(*result));
  if (root->is_txn_root)
    {
      apr_hash_t *changed_paths;
      SVN_ERR(svn_fs_x__txn_changes_fetch(&changed_paths, root->fs,
                                          svn_fs_x__root_txn_id(root),
                                          result_pool));

      result->fsap_data = apr_hash_first(result_pool, changed_paths);
      result->vtable = &txn_changes_iterator_vtable;
    }
  else
    {
      /* The block of changes that we retrieve need to live in a separately
         cleanable pool. */
      apr_pool_t *changes_pool = svn_pool_create(result_pool);

      /* Our iteration context info. */
      fs_revision_changes_iterator_data_t *data = apr_pcalloc(result_pool,
                                                              sizeof(*data));

      /* This pool must remain valid as long as ITERATOR lives but will
         be used only for temporary allocations and will be cleaned up
         frequently.  So, this must be a sub-pool of RESULT_POOL. */
      data->scratch_pool = svn_pool_create(result_pool);

      /* Fetch the first block of data. */
      SVN_ERR(svn_fs_x__create_changes_context(&data->context,
                                               root->fs, root->rev,
                                               result_pool, scratch_pool));
      SVN_ERR(svn_fs_x__get_changes(&data->changes, data->context,
                                    changes_pool, scratch_pool));

      /* Return the fully initialized object. */
      result->fsap_data = data;
      result->vtable = &rev_changes_iterator_vtable;
    }

  *iterator = result;

  return SVN_NO_ERROR;
}


/* Our coolio opaque history object. */
typedef struct fs_history_data_t
{
  /* filesystem object */
  svn_fs_t *fs;

  /* path and revision of historical location */
  const char *path;
  svn_revnum_t revision;

  /* internal-use hints about where to resume the history search. */
  const char *path_hint;
  svn_revnum_t rev_hint;

  /* FALSE until the first call to svn_fs_history_prev(). */
  svn_boolean_t is_interesting;

  /* If not SVN_INVALID_REVISION, we know that the next copy operation
     is at this revision. */
  svn_revnum_t next_copy;

  /* If used, see svn_fs_x__id_used, this is the noderev ID of
     PATH@REVISION. */
  svn_fs_x__id_t current_id;

} fs_history_data_t;

static svn_fs_history_t *
assemble_history(svn_fs_t *fs,
                 const char *path,
                 svn_revnum_t revision,
                 svn_boolean_t is_interesting,
                 const char *path_hint,
                 svn_revnum_t rev_hint,
                 svn_revnum_t next_copy,
                 const svn_fs_x__id_t *current_id,
                 apr_pool_t *result_pool);


/* Set *HISTORY_P to an opaque node history object which represents
   PATH under ROOT.  ROOT must be a revision root.  Use POOL for all
   allocations. */
static svn_error_t *
x_node_history(svn_fs_history_t **history_p,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  /* And we require that the path exist in the root. */
  SVN_ERR(svn_fs_x__check_path(&kind, root, path, scratch_pool));
  if (kind == svn_node_none)
    return SVN_FS__NOT_FOUND(root, path);

  /* Okay, all seems well.  Build our history object and return it. */
  *history_p = assemble_history(root->fs, path, root->rev, FALSE, NULL,
                                SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                NULL, result_pool);
  return SVN_NO_ERROR;
}

/* Find the youngest copyroot for path DAG_PATH or its parents in
   filesystem FS, and store the copyroot in *REV_P and *PATH_P. */
static svn_error_t *
find_youngest_copyroot(svn_revnum_t *rev_p,
                       const char **path_p,
                       svn_fs_t *fs,
                       svn_fs_x__dag_path_t *dag_path)
{
  svn_revnum_t rev_mine;
  svn_revnum_t rev_parent = SVN_INVALID_REVNUM;
  const char *path_mine;
  const char *path_parent = NULL;

  /* First find our parent's youngest copyroot. */
  if (dag_path->parent)
    SVN_ERR(find_youngest_copyroot(&rev_parent, &path_parent, fs,
                                   dag_path->parent));

  /* Find our copyroot. */
  svn_fs_x__dag_get_copyroot(&rev_mine, &path_mine, dag_path->node);

  /* If a parent and child were copied to in the same revision, prefer
     the child copy target, since it is the copy relevant to the
     history of the child. */
  if (rev_mine >= rev_parent)
    {
      *rev_p = rev_mine;
      *path_p = path_mine;
    }
  else
    {
      *rev_p = rev_parent;
      *path_p = path_parent;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
x_closest_copy(svn_fs_root_t **root_p,
               const char **path_p,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  svn_fs_t *fs = root->fs;
  svn_fs_x__dag_path_t *dag_path, *copy_dst_dag_path;
  svn_revnum_t copy_dst_rev, created_rev;
  const char *copy_dst_path;
  svn_fs_root_t *copy_dst_root;
  dag_node_t *copy_dst_node;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  /* Initialize return values. */
  *root_p = NULL;
  *path_p = NULL;

  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path, 0, FALSE,
                                 scratch_pool, scratch_pool));

  /* Find the youngest copyroot in the path of this node-rev, which
     will indicate the target of the innermost copy affecting the
     node-rev. */
  SVN_ERR(find_youngest_copyroot(&copy_dst_rev, &copy_dst_path,
                                 fs, dag_path));
  if (copy_dst_rev == 0)  /* There are no copies affecting this node-rev. */
    {
      svn_pool_destroy(scratch_pool);
      return SVN_NO_ERROR;
    }

  /* It is possible that this node was created from scratch at some
     revision between COPY_DST_REV and REV.  Make sure that PATH
     exists as of COPY_DST_REV and is related to this node-rev. */
  SVN_ERR(svn_fs_x__revision_root(&copy_dst_root, fs, copy_dst_rev, pool));
  SVN_ERR(svn_fs_x__get_dag_path(&copy_dst_dag_path, copy_dst_root, path,
                                 svn_fs_x__dag_path_allow_null, FALSE,
                                 scratch_pool, scratch_pool));
  if (copy_dst_dag_path == NULL)
    {
      svn_pool_destroy(scratch_pool);
      return SVN_NO_ERROR;
    }

  copy_dst_node = copy_dst_dag_path->node;
  if (!svn_fs_x__dag_related_node(copy_dst_node, dag_path->node))
    {
      svn_pool_destroy(scratch_pool);
      return SVN_NO_ERROR;
    }

  /* One final check must be done here.  If you copy a directory and
     create a new entity somewhere beneath that directory in the same
     txn, then we can't claim that the copy affected the new entity.
     For example, if you do:

        copy dir1 dir2
        create dir2/new-thing
        commit

     then dir2/new-thing was not affected by the copy of dir1 to dir2.
     We detect this situation by asking if PATH@COPY_DST_REV's
     created-rev is COPY_DST_REV, and that node-revision has no
     predecessors, then there is no relevant closest copy.
  */
  created_rev = svn_fs_x__dag_get_revision(copy_dst_node);
  if (created_rev == copy_dst_rev)
    if (!svn_fs_x__id_used(svn_fs_x__dag_get_predecessor_id(copy_dst_node)))
      {
        svn_pool_destroy(scratch_pool);
        return SVN_NO_ERROR;
      }

  /* The copy destination checks out.  Return it. */
  *root_p = copy_dst_root;
  *path_p = apr_pstrdup(pool, copy_dst_path);

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
x_node_origin_rev(svn_revnum_t *revision,
                  svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *scratch_pool)
{
  svn_fs_x__id_t node_id;
  dag_node_t *node;

  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));
  node_id = *svn_fs_x__dag_get_node_id(node);

  *revision = svn_fs_x__get_revnum(node_id.change_set);

  return SVN_NO_ERROR;
}


static svn_error_t *
history_prev(svn_fs_history_t **prev_history,
             svn_fs_history_t *history,
             svn_boolean_t cross_copies,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  fs_history_data_t *fhd = history->fsap_data;
  const char *commit_path, *src_path, *path = fhd->path;
  svn_revnum_t commit_rev, src_rev, dst_rev;
  svn_revnum_t revision = fhd->revision;
  svn_fs_t *fs = fhd->fs;
  svn_fs_x__dag_path_t *dag_path;
  dag_node_t *node;
  svn_fs_root_t *root;
  svn_boolean_t reported = fhd->is_interesting;
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;
  svn_fs_x__id_t pred_id;

  /* Initialize our return value. */
  *prev_history = NULL;

  /* When following history, there tend to be long sections of linear
     history where there are no copies at PATH or its parents.  Within
     these sections, we only need to follow the node history. */
  if (   SVN_IS_VALID_REVNUM(fhd->next_copy)
      && revision > fhd->next_copy
      && svn_fs_x__id_used(&fhd->current_id))
    {
      /* We know the last reported node (CURRENT_ID) and the NEXT_COPY
         revision is somewhat further in the past. */
      svn_fs_x__noderev_t *noderev;
      assert(reported);

      /* Get the previous node change.  If there is none, then we already
         reported the initial addition and this history traversal is done. */
      SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, &fhd->current_id,
                                          scratch_pool, scratch_pool));
      if (! svn_fs_x__id_used(&noderev->predecessor_id))
        return SVN_NO_ERROR;

      /* If the previous node change is younger than the next copy, it is
         part of the linear history section. */
      commit_rev = svn_fs_x__get_revnum(noderev->predecessor_id.change_set);
      if (commit_rev > fhd->next_copy)
        {
          /* Within the linear history, simply report all node changes and
             continue with the respective predecessor. */
          *prev_history = assemble_history(fs, noderev->created_path,
                                           commit_rev, TRUE, NULL,
                                           SVN_INVALID_REVNUM,
                                           fhd->next_copy,
                                           &noderev->predecessor_id,
                                           result_pool);

          return SVN_NO_ERROR;
        }

     /* We hit a copy. Fall back to the standard code path. */
    }

  /* If our last history report left us hints about where to pickup
     the chase, then our last report was on the destination of a
     copy.  If we are crossing copies, start from those locations,
     otherwise, we're all done here.  */
  if (fhd->path_hint && SVN_IS_VALID_REVNUM(fhd->rev_hint))
    {
      reported = FALSE;
      if (! cross_copies)
        return SVN_NO_ERROR;
      path = fhd->path_hint;
      revision = fhd->rev_hint;
    }

  /* Construct a ROOT for the current revision. */
  SVN_ERR(svn_fs_x__revision_root(&root, fs, revision, scratch_pool));

  /* Open PATH/REVISION, and get its node and a bunch of other
     goodies.  */
  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, root, path, 0, FALSE,
                                 scratch_pool, scratch_pool));
  node = dag_path->node;
  commit_path = svn_fs_x__dag_get_created_path(node);
  commit_rev = svn_fs_x__dag_get_revision(node);
  svn_fs_x__id_reset(&pred_id);

  /* The Subversion filesystem is written in such a way that a given
     line of history may have at most one interesting history point
     per filesystem revision.  Either that node was edited (and
     possibly copied), or it was copied but not edited.  And a copy
     source cannot be from the same revision as its destination.  So,
     if our history revision matches its node's commit revision, we
     know that ... */
  if (revision == commit_rev)
    {
      if (! reported)
        {
          /* ... we either have not yet reported on this revision (and
             need now to do so) ... */
          *prev_history = assemble_history(fs, commit_path,
                                           commit_rev, TRUE, NULL,
                                           SVN_INVALID_REVNUM,
                                           SVN_INVALID_REVNUM, NULL,
                                           result_pool);
          return SVN_NO_ERROR;
        }
      else
        {
          /* ... or we *have* reported on this revision, and must now
             progress toward this node's predecessor (unless there is
             no predecessor, in which case we're all done!). */
          pred_id = *svn_fs_x__dag_get_predecessor_id(node);
          if (!svn_fs_x__id_used(&pred_id))
            return SVN_NO_ERROR;

          /* Replace NODE and friends with the information from its
             predecessor. */
          SVN_ERR(svn_fs_x__dag_get_node(&node, fs, &pred_id, scratch_pool,
                                         scratch_pool));
          commit_path = svn_fs_x__dag_get_created_path(node);
          commit_rev = svn_fs_x__dag_get_revision(node);
        }
    }

  /* Find the youngest copyroot in the path of this node, including
     itself. */
  SVN_ERR(find_youngest_copyroot(&copyroot_rev, &copyroot_path, fs,
                                 dag_path));

  /* Initialize some state variables. */
  src_path = NULL;
  src_rev = SVN_INVALID_REVNUM;
  dst_rev = SVN_INVALID_REVNUM;

  if (copyroot_rev > commit_rev)
    {
      const char *remainder_path;
      const char *copy_dst, *copy_src;
      svn_fs_root_t *copyroot_root;

      SVN_ERR(svn_fs_x__revision_root(&copyroot_root, fs, copyroot_rev,
                                       scratch_pool));
      SVN_ERR(svn_fs_x__get_temp_dag_node(&node, copyroot_root,
                                          copyroot_path, scratch_pool));
      copy_dst = svn_fs_x__dag_get_created_path(node);

      /* If our current path was the very destination of the copy,
         then our new current path will be the copy source.  If our
         current path was instead the *child* of the destination of
         the copy, then figure out its previous location by taking its
         path relative to the copy destination and appending that to
         the copy source.  Finally, if our current path doesn't meet
         one of these other criteria ... ### for now just fallback to
         the old copy hunt algorithm. */
      remainder_path = svn_fspath__skip_ancestor(copy_dst, path);

      if (remainder_path)
        {
          /* If we get here, then our current path is the destination
             of, or the child of the destination of, a copy.  Fill
             in the return values and get outta here.  */
          src_rev = svn_fs_x__dag_get_copyfrom_rev(node);
          copy_src = svn_fs_x__dag_get_copyfrom_path(node);

          dst_rev = copyroot_rev;
          src_path = svn_fspath__join(copy_src, remainder_path, scratch_pool);
        }
    }

  /* If we calculated a copy source path and revision, we'll make a
     'copy-style' history object. */
  if (src_path && SVN_IS_VALID_REVNUM(src_rev))
    {
      svn_boolean_t retry = FALSE;

      /* It's possible for us to find a copy location that is the same
         as the history point we've just reported.  If that happens,
         we simply need to take another trip through this history
         search. */
      if ((dst_rev == revision) && reported)
        retry = TRUE;

      *prev_history = assemble_history(fs, path, dst_rev, ! retry,
                                       src_path, src_rev,
                                       SVN_INVALID_REVNUM, NULL,
                                       result_pool);
    }
  else
    {
      /* We know the next copy revision.  If we are not at the copy rev
         itself, we will also know the predecessor node ID and the next
         invocation will use the optimized "linear history" code path. */
      *prev_history = assemble_history(fs, commit_path, commit_rev, TRUE,
                                       NULL, SVN_INVALID_REVNUM,
                                       copyroot_rev, &pred_id, result_pool);
    }

  return SVN_NO_ERROR;
}


/* Implement svn_fs_history_prev, set *PREV_HISTORY_P to a new
   svn_fs_history_t object that represents the predecessory of
   HISTORY.  If CROSS_COPIES is true, *PREV_HISTORY_P may be related
   only through a copy operation.  Perform all allocations in POOL. */
static svn_error_t *
fs_history_prev(svn_fs_history_t **prev_history_p,
                svn_fs_history_t *history,
                svn_boolean_t cross_copies,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_fs_history_t *prev_history = NULL;
  fs_history_data_t *fhd = history->fsap_data;
  svn_fs_t *fs = fhd->fs;

  /* Special case: the root directory changes in every single
     revision, no exceptions.  And, the root can't be the target (or
     child of a target -- duh) of a copy.  So, if that's our path,
     then we need only decrement our revision by 1, and there you go. */
  if (strcmp(fhd->path, "/") == 0)
    {
      if (! fhd->is_interesting)
        prev_history = assemble_history(fs, "/", fhd->revision,
                                        1, NULL, SVN_INVALID_REVNUM,
                                        SVN_INVALID_REVNUM, NULL,
                                        result_pool);
      else if (fhd->revision > 0)
        prev_history = assemble_history(fs, "/", fhd->revision - 1,
                                        1, NULL, SVN_INVALID_REVNUM,
                                        SVN_INVALID_REVNUM, NULL,
                                        result_pool);
    }
  else
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      prev_history = history;

      while (1)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(history_prev(&prev_history, prev_history, cross_copies,
                               result_pool, iterpool));

          if (! prev_history)
            break;
          fhd = prev_history->fsap_data;
          if (fhd->is_interesting)
            break;
        }

      svn_pool_destroy(iterpool);
    }

  *prev_history_p = prev_history;
  return SVN_NO_ERROR;
}


/* Set *PATH and *REVISION to the path and revision for the HISTORY
   object.  Allocate *PATH in RESULT_POOL. */
static svn_error_t *
fs_history_location(const char **path,
                    svn_revnum_t *revision,
                    svn_fs_history_t *history,
                    apr_pool_t *result_pool)
{
  fs_history_data_t *fhd = history->fsap_data;

  *path = apr_pstrdup(result_pool, fhd->path);
  *revision = fhd->revision;
  return SVN_NO_ERROR;
}

static history_vtable_t history_vtable = {
  fs_history_prev,
  fs_history_location
};

/* Return a new history object (marked as "interesting") for PATH and
   REVISION, allocated in RESULT_POOL, and with its members set to the
   values of the parameters provided.  Note that PATH and PATH_HINT get
   normalized and duplicated in RESULT_POOL. */
static svn_fs_history_t *
assemble_history(svn_fs_t *fs,
                 const char *path,
                 svn_revnum_t revision,
                 svn_boolean_t is_interesting,
                 const char *path_hint,
                 svn_revnum_t rev_hint,
                 svn_revnum_t next_copy,
                 const svn_fs_x__id_t *current_id,
                 apr_pool_t *result_pool)
{
  svn_fs_history_t *history = apr_pcalloc(result_pool, sizeof(*history));
  fs_history_data_t *fhd = apr_pcalloc(result_pool, sizeof(*fhd));
  fhd->path = svn_fs__canonicalize_abspath(path, result_pool);
  fhd->revision = revision;
  fhd->is_interesting = is_interesting;
  fhd->path_hint = path_hint
                 ? svn_fs__canonicalize_abspath(path_hint, result_pool)
                 : NULL;
  fhd->rev_hint = rev_hint;
  fhd->next_copy = next_copy;
  fhd->fs = fs;

  if (current_id)
    fhd->current_id = *current_id;
  else
    svn_fs_x__id_reset(&fhd->current_id);

  history->vtable = &history_vtable;
  history->fsap_data = fhd;
  return history;
}


/* mergeinfo queries */


/* DIR_DAG is a directory DAG node which has mergeinfo in its
   descendants.  This function iterates over its children.  For each
   child with immediate mergeinfo, call RECEIVER with it and BATON.
   For each child with descendants with mergeinfo, it recurses.  Note
   that it does *not* call the action on the path for DIR_DAG itself.

   SCRATCH_POOL is used for temporary allocations, including the mergeinfo
   hashes passed to actions.
 */
static svn_error_t *
crawl_directory_dag_for_mergeinfo(svn_fs_root_t *root,
                                  const char *this_path,
                                  dag_node_t *dir_dag,
                                  svn_fs_mergeinfo_receiver_t receiver,
                                  void *baton,
                                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *entries;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_fs_x__dag_dir_entries(&entries, dir_dag, scratch_pool,
                                    iterpool));
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_x__dirent_t *dirent
        = APR_ARRAY_IDX(entries, i, svn_fs_x__dirent_t *);
      const char *kid_path;
      dag_node_t *kid_dag;

      svn_pool_clear(iterpool);

      kid_path = svn_fspath__join(this_path, dirent->name, iterpool);
      SVN_ERR(svn_fs_x__get_temp_dag_node(&kid_dag, root, kid_path,
                                          iterpool));

      if (svn_fs_x__dag_has_mergeinfo(kid_dag))
        {
          /* Save this particular node's mergeinfo. */
          apr_hash_t *proplist;
          svn_mergeinfo_t kid_mergeinfo;
          svn_string_t *mergeinfo_string;
          svn_error_t *err;

          SVN_ERR(svn_fs_x__dag_get_proplist(&proplist, kid_dag, iterpool,
                                             iterpool));
          mergeinfo_string = svn_hash_gets(proplist, SVN_PROP_MERGEINFO);
          if (!mergeinfo_string)
            {
              svn_string_t *idstr
                = svn_fs_x__id_unparse(&dirent->id, iterpool);
              return svn_error_createf
                (SVN_ERR_FS_CORRUPT, NULL,
                 _("Node-revision #'%s' claims to have mergeinfo but doesn't"),
                 idstr->data);
            }

          /* Issue #3896: If a node has syntactically invalid mergeinfo, then
             treat it as if no mergeinfo is present rather than raising a parse
             error. */
          err = svn_mergeinfo_parse(&kid_mergeinfo,
                                    mergeinfo_string->data,
                                    iterpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
                svn_error_clear(err);
              else
                return svn_error_trace(err);
              }
          else
            {
              SVN_ERR(receiver(kid_path, kid_mergeinfo, baton, iterpool));
            }
        }

      if (svn_fs_x__dag_has_descendants_with_mergeinfo(kid_dag))
        SVN_ERR(crawl_directory_dag_for_mergeinfo(root,
                                                  kid_path,
                                                  kid_dag,
                                                  receiver,
                                                  baton,
                                                  iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Calculates the mergeinfo for PATH under REV_ROOT using inheritance
   type INHERIT.  Returns it in *MERGEINFO, or NULL if there is none.
   The result is allocated in RESULT_POOL; SCRATCH_POOL is
   used for temporary allocations.
 */
static svn_error_t *
get_mergeinfo_for_path(svn_mergeinfo_t *mergeinfo,
                       svn_fs_root_t *rev_root,
                       const char *path,
                       svn_mergeinfo_inheritance_t inherit,
                       svn_boolean_t adjust_inherited_mergeinfo,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__dag_path_t *dag_path, *nearest_ancestor;
  apr_hash_t *proplist;
  svn_string_t *mergeinfo_string;

  *mergeinfo = NULL;
  SVN_ERR(svn_fs_x__get_dag_path(&dag_path, rev_root, path, 0, FALSE,
                                 scratch_pool, scratch_pool));

  if (inherit == svn_mergeinfo_nearest_ancestor && ! dag_path->parent)
    return SVN_NO_ERROR;

  if (inherit == svn_mergeinfo_nearest_ancestor)
    nearest_ancestor = dag_path->parent;
  else
    nearest_ancestor = dag_path;

  while (TRUE)
    {
      if (svn_fs_x__dag_has_mergeinfo(nearest_ancestor->node))
        break;

      /* No need to loop if we're looking for explicit mergeinfo. */
      if (inherit == svn_mergeinfo_explicit)
        {
          return SVN_NO_ERROR;
        }

      nearest_ancestor = nearest_ancestor->parent;

      /* Run out?  There's no mergeinfo. */
      if (!nearest_ancestor)
        {
          return SVN_NO_ERROR;
        }
    }

  SVN_ERR(svn_fs_x__dag_get_proplist(&proplist, nearest_ancestor->node,
                                     scratch_pool, scratch_pool));
  mergeinfo_string = svn_hash_gets(proplist, SVN_PROP_MERGEINFO);
  if (!mergeinfo_string)
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, NULL,
       _("Node-revision '%s@%ld' claims to have mergeinfo but doesn't"),
       parent_path_path(nearest_ancestor, scratch_pool), rev_root->rev);

  /* Parse the mergeinfo; store the result in *MERGEINFO. */
  {
    /* Issue #3896: If a node has syntactically invalid mergeinfo, then
       treat it as if no mergeinfo is present rather than raising a parse
       error. */
    svn_error_t *err = svn_mergeinfo_parse(mergeinfo,
                                           mergeinfo_string->data,
                                           result_pool);
    if (err)
      {
        if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
          {
            svn_error_clear(err);
            err = NULL;
            *mergeinfo = NULL;
          }
        return svn_error_trace(err);
      }
  }

  /* If our nearest ancestor is the very path we inquired about, we
     can return the mergeinfo results directly.  Otherwise, we're
     inheriting the mergeinfo, so we need to a) remove non-inheritable
     ranges and b) telescope the merged-from paths. */
  if (adjust_inherited_mergeinfo && (nearest_ancestor != dag_path))
    {
      svn_mergeinfo_t tmp_mergeinfo;

      SVN_ERR(svn_mergeinfo_inheritable2(&tmp_mergeinfo, *mergeinfo,
                                         NULL, SVN_INVALID_REVNUM,
                                         SVN_INVALID_REVNUM, TRUE,
                                         scratch_pool, scratch_pool));
      SVN_ERR(svn_fs__append_to_merged_froms(mergeinfo, tmp_mergeinfo,
                                             parent_path_relpath(
                                               dag_path, nearest_ancestor,
                                               scratch_pool),
                                             result_pool));
    }

  return SVN_NO_ERROR;
}

/* Invoke RECEIVER with BATON for each mergeinfo found on descendants of
   PATH (but not PATH itself).  Use SCRATCH_POOL for temporary values. */
static svn_error_t *
add_descendant_mergeinfo(svn_fs_root_t *root,
                         const char *path,
                         svn_fs_mergeinfo_receiver_t receiver,
                         void *baton,
                         apr_pool_t *scratch_pool)
{
  dag_node_t *this_dag;

  SVN_ERR(svn_fs_x__get_temp_dag_node(&this_dag, root, path, scratch_pool));
  if (svn_fs_x__dag_has_descendants_with_mergeinfo(this_dag))
    SVN_ERR(crawl_directory_dag_for_mergeinfo(root,
                                              path,
                                              this_dag,
                                              receiver,
                                              baton,
                                              scratch_pool));
  return SVN_NO_ERROR;
}


/* Find all the mergeinfo for a set of PATHS under ROOT and report it
   through RECEIVER with BATON.  INHERITED, INCLUDE_DESCENDANTS and
   ADJUST_INHERITED_MERGEINFO are the same as in the FS API.

   Allocate temporary values are allocated in SCRATCH_POOL. */
static svn_error_t *
get_mergeinfos_for_paths(svn_fs_root_t *root,
                         const apr_array_header_t *paths,
                         svn_mergeinfo_inheritance_t inherit,
                         svn_boolean_t include_descendants,
                         svn_boolean_t adjust_inherited_mergeinfo,
                         svn_fs_mergeinfo_receiver_t receiver,
                         void *baton,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  for (i = 0; i < paths->nelts; i++)
    {
      svn_error_t *err;
      svn_mergeinfo_t path_mergeinfo;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(iterpool);

      err = get_mergeinfo_for_path(&path_mergeinfo, root, path,
                                   inherit, adjust_inherited_mergeinfo,
                                   iterpool, iterpool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
              err = NULL;
              path_mergeinfo = NULL;
            }
          else
            {
              return svn_error_trace(err);
            }
        }

      if (path_mergeinfo)
        SVN_ERR(receiver(path, path_mergeinfo, baton, iterpool));
      if (include_descendants)
        SVN_ERR(add_descendant_mergeinfo(root, path, receiver, baton,
                                         iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Implements svn_fs_get_mergeinfo. */
static svn_error_t *
x_get_mergeinfo(svn_fs_root_t *root,
                const apr_array_header_t *paths,
                svn_mergeinfo_inheritance_t inherit,
                svn_boolean_t include_descendants,
                svn_boolean_t adjust_inherited_mergeinfo,
                svn_fs_mergeinfo_receiver_t receiver,
                void *baton,
                apr_pool_t *scratch_pool)
{
  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  /* Retrieve a path -> mergeinfo hash mapping. */
  return get_mergeinfos_for_paths(root, paths, inherit,
                                  include_descendants,
                                  adjust_inherited_mergeinfo,
                                  receiver, baton,
                                  scratch_pool);
}


/* The vtable associated with root objects. */
static root_vtable_t root_vtable = {
  NULL,
  x_report_changes,
  svn_fs_x__check_path,
  x_node_history,
  x_node_id,
  x_node_relation,
  svn_fs_x__node_created_rev,
  x_node_origin_rev,
  x_node_created_path,
  x_delete_node,
  x_copy,
  x_revision_link,
  x_copied_from,
  x_closest_copy,
  x_node_prop,
  x_node_proplist,
  x_node_has_props,
  x_change_node_prop,
  x_props_changed,
  x_dir_entries,
  x_dir_optimal_order,
  x_make_dir,
  x_file_length,
  x_file_checksum,
  x_file_contents,
  x_try_process_file_contents,
  x_make_file,
  x_apply_textdelta,
  x_apply_text,
  x_contents_changed,
  x_get_file_delta_stream,
  x_merge,
  x_get_mergeinfo,
};

/* Construct a new root object in FS, allocated from RESULT_POOL.  */
static svn_fs_root_t *
make_root(svn_fs_t *fs,
          apr_pool_t *result_pool)
{
  svn_fs_root_t *root = apr_pcalloc(result_pool, sizeof(*root));

  root->fs = fs;
  root->pool = result_pool;
  root->vtable = &root_vtable;

  return root;
}


/* Construct a root object referring to the root of revision REV in FS.
   Create the new root in RESULT_POOL.  */
static svn_fs_root_t *
make_revision_root(svn_fs_t *fs,
                   svn_revnum_t rev,
                   apr_pool_t *result_pool)
{
  svn_fs_root_t *root = make_root(fs, result_pool);

  root->is_txn_root = FALSE;
  root->rev = rev;

  return root;
}


/* Construct a root object referring to the root of the transaction
   named TXN and based on revision BASE_REV in FS, with FLAGS to
   describe transaction's behavior.  Create the new root in RESULT_POOL.  */
static svn_error_t *
make_txn_root(svn_fs_root_t **root_p,
              svn_fs_t *fs,
              svn_fs_x__txn_id_t txn_id,
              svn_revnum_t base_rev,
              apr_uint32_t flags,
              apr_pool_t *result_pool)
{
  svn_fs_root_t *root = make_root(fs, result_pool);
  fs_txn_root_data_t *frd = apr_pcalloc(root->pool, sizeof(*frd));
  frd->txn_id = txn_id;

  root->is_txn_root = TRUE;
  root->txn = svn_fs_x__txn_name(txn_id, root->pool);
  root->txn_flags = flags;
  root->rev = base_rev;
  root->fsap_data = frd;

  *root_p = root;
  return SVN_NO_ERROR;
}



/* Verify. */
static const char *
stringify_node(dag_node_t *node,
               apr_pool_t *result_pool)
{
  /* ### TODO: print some PATH@REV to it, too. */
  return svn_fs_x__id_unparse(svn_fs_x__dag_get_id(node), result_pool)->data;
}

/* Check metadata sanity on NODE, and on its children.  Manually verify
   information for DAG nodes in revision REV, and trust the metadata
   accuracy for nodes belonging to older revisions.  To detect cycles,
   provide all parent dag_node_t * in PARENT_NODES. */
static svn_error_t *
verify_node(dag_node_t *node,
            svn_revnum_t rev,
            apr_array_header_t *parent_nodes,
            apr_pool_t *scratch_pool)
{
  svn_boolean_t has_mergeinfo;
  apr_int64_t mergeinfo_count;
  svn_fs_x__id_t pred_id;
  svn_fs_t *fs = svn_fs_x__dag_get_fs(node);
  int pred_count;
  svn_node_kind_t kind;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* Detect (non-)DAG cycles. */
  for (i = 0; i < parent_nodes->nelts; ++i)
    {
      dag_node_t *parent = APR_ARRAY_IDX(parent_nodes, i, dag_node_t *);
      if (svn_fs_x__id_eq(svn_fs_x__dag_get_id(parent),
                          svn_fs_x__dag_get_id(node)))
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                "Node is its own direct or indirect parent '%s'",
                                stringify_node(node, iterpool));
    }

  /* Fetch some data. */
  has_mergeinfo = svn_fs_x__dag_has_mergeinfo(node);
  mergeinfo_count = svn_fs_x__dag_get_mergeinfo_count(node);
  pred_id = *svn_fs_x__dag_get_predecessor_id(node);
  pred_count = svn_fs_x__dag_get_predecessor_count(node);
  kind = svn_fs_x__dag_node_kind(node);

  /* Sanity check. */
  if (mergeinfo_count < 0)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "Negative mergeinfo-count %" APR_INT64_T_FMT
                             " on node '%s'",
                             mergeinfo_count, stringify_node(node, iterpool));

  /* Issue #4129. (This check will explicitly catch non-root instances too.) */
  if (svn_fs_x__id_used(&pred_id))
    {
      dag_node_t *pred;
      int pred_pred_count;
      SVN_ERR(svn_fs_x__dag_get_node(&pred, fs, &pred_id, iterpool,
                                     iterpool));
      pred_pred_count = svn_fs_x__dag_get_predecessor_count(pred);
      if (pred_pred_count+1 != pred_count)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 "Predecessor count mismatch: "
                                 "%s has %d, but %s has %d",
                                 stringify_node(node, iterpool), pred_count,
                                 stringify_node(pred, iterpool),
                                 pred_pred_count);
    }

  /* Kind-dependent verifications. */
  if (kind == svn_node_none)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               "Node '%s' has kind 'none'",
                               stringify_node(node, iterpool));
    }
  if (kind == svn_node_file)
    {
      if (has_mergeinfo != mergeinfo_count) /* comparing int to bool */
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 "File node '%s' has inconsistent mergeinfo: "
                                 "has_mergeinfo=%d, "
                                 "mergeinfo_count=%" APR_INT64_T_FMT,
                                 stringify_node(node, iterpool),
                                 has_mergeinfo, mergeinfo_count);
    }
  if (kind == svn_node_dir)
    {
      apr_array_header_t *entries;
      apr_int64_t children_mergeinfo = 0;
      APR_ARRAY_PUSH(parent_nodes, dag_node_t*) = node;

      SVN_ERR(svn_fs_x__dag_dir_entries(&entries, node, scratch_pool,
                                        iterpool));

      /* Compute CHILDREN_MERGEINFO. */
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_x__dirent_t *dirent
            = APR_ARRAY_IDX(entries, i, svn_fs_x__dirent_t *);
          dag_node_t *child;
          apr_int64_t child_mergeinfo;

          svn_pool_clear(iterpool);

          /* Compute CHILD_REV. */
          if (svn_fs_x__get_revnum(dirent->id.change_set) == rev)
            {
              SVN_ERR(svn_fs_x__dag_get_node(&child, fs, &dirent->id,
                                             iterpool, iterpool));
              SVN_ERR(verify_node(child, rev, parent_nodes, iterpool));
              child_mergeinfo = svn_fs_x__dag_get_mergeinfo_count(child);
            }
          else
            {
              SVN_ERR(svn_fs_x__get_mergeinfo_count(&child_mergeinfo, fs,
                                                    &dirent->id, iterpool));
            }

          children_mergeinfo += child_mergeinfo;
        }

      /* Side-effect of issue #4129. */
      if (children_mergeinfo+has_mergeinfo != mergeinfo_count)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 "Mergeinfo-count discrepancy on '%s': "
                                 "expected %" APR_INT64_T_FMT "+%d, "
                                 "counted %" APR_INT64_T_FMT,
                                 stringify_node(node, iterpool),
                                 mergeinfo_count, has_mergeinfo,
                                 children_mergeinfo);

      /* If we don't make it here, there was an error / corruption.
       * In that case, nobody will need PARENT_NODES anymore. */
      apr_array_pop(parent_nodes);
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__verify_root(svn_fs_root_t *root,
                      apr_pool_t *scratch_pool)
{
  dag_node_t *root_dir;
  apr_array_header_t *parent_nodes;

  /* Issue #4129: bogus pred-counts and minfo-cnt's on the root node-rev
     (and elsewhere).  This code makes more thorough checks than the
     commit-time checks in validate_root_noderev(). */

  /* Callers should disable caches by setting SVN_FS_CONFIG_FSX_CACHE_NS;
     see r1462436.

     When this code is called in the library, we want to ensure we
     use the on-disk data --- rather than some data that was read
     in the possibly-distance past and cached since. */
  SVN_ERR(svn_fs_x__dag_root(&root_dir, root->fs,
                             svn_fs_x__root_change_set(root),
                             scratch_pool, scratch_pool));

  /* Recursively verify ROOT_DIR. */
  parent_nodes = apr_array_make(scratch_pool, 16, sizeof(dag_node_t *));
  SVN_ERR(verify_node(root_dir, root->rev, parent_nodes, scratch_pool));

  /* Verify explicitly the predecessor of the root. */
  {
    svn_fs_x__id_t pred_id;
    svn_boolean_t has_predecessor;

    /* Only r0 should have no predecessor. */
    pred_id = *svn_fs_x__dag_get_predecessor_id(root_dir);
    has_predecessor = svn_fs_x__id_used(&pred_id);
    if (!root->is_txn_root && has_predecessor != !!root->rev)
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               "r%ld's root node's predecessor is "
                               "unexpectedly '%s'",
                               root->rev,
                               (has_predecessor
                                 ? svn_fs_x__id_unparse(&pred_id,
                                                        scratch_pool)->data
                                 : "(null)"));
    if (root->is_txn_root && !has_predecessor)
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               "Transaction '%s''s root node's predecessor is "
                               "unexpectedly NULL",
                               root->txn);

    /* Check the predecessor's revision. */
    if (has_predecessor)
      {
        svn_revnum_t pred_rev = svn_fs_x__get_revnum(pred_id.change_set);
        if (! root->is_txn_root && pred_rev+1 != root->rev)
          /* Issue #4129. */
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   "r%ld's root node's predecessor is r%ld"
                                   " but should be r%ld",
                                   root->rev, pred_rev, root->rev - 1);
        if (root->is_txn_root && pred_rev != root->rev)
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   "Transaction '%s''s root node's predecessor"
                                   " is r%ld"
                                   " but should be r%ld",
                                   root->txn, pred_rev, root->rev);
      }
  }

  return SVN_NO_ERROR;
}
