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
#include "svn_time.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "dag.h"
#include "err.h"
#include "fs.h"
#include "key-gen.h"
#include "node-rev.h"
#include "trail.h"
#include "reps-strings.h"
#include "revs-txns.h"
#include "id.h"

#include "util/fs_skels.h"

#include "bdb/txn-table.h"
#include "bdb/rev-table.h"
#include "bdb/nodes-table.h"
#include "bdb/copies-table.h"
#include "bdb/reps-table.h"
#include "bdb/strings-table.h"
#include "bdb/checksum-reps-table.h"
#include "bdb/changes-table.h"
#include "bdb/node-origins-table.h"

#include "private/svn_skel.h"
#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/* Initializing a filesystem.  */

struct dag_node_t
{
  /*** NOTE: Keeping in-memory representations of disk data that can
       be changed by other accessors is a nasty business.  Such
       representations are basically a cache with some pretty complex
       invalidation rules.  For example, the "node revision"
       associated with a DAG node ID can look completely different to
       a process that has modified that information as part of a
       Berkeley DB transaction than it does to some other process.
       That said, there are some aspects of a "node revision" which
       never change, like its 'id' or 'kind'.  Our best bet is to
       limit ourselves to exposing outside of this interface only
       those immutable aspects of a DAG node representation.  ***/

  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The node revision ID for this dag node. */
  svn_fs_id_t *id;

  /* The node's type (file, dir, etc.) */
  svn_node_kind_t kind;

  /* the path at which this node was created. */
  const char *created_path;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs_base__dag_node_kind(dag_node_t *node)
{
  return node->kind;
}


const svn_fs_id_t *
svn_fs_base__dag_get_id(dag_node_t *node)
{
  return node->id;
}


const char *
svn_fs_base__dag_get_created_path(dag_node_t *node)
{
  return node->created_path;
}


svn_fs_t *
svn_fs_base__dag_get_fs(dag_node_t *node)
{
  return node->fs;
}


svn_boolean_t svn_fs_base__dag_check_mutable(dag_node_t *node,
                                             const char *txn_id)
{
  return (strcmp(svn_fs_base__id_txn_id(svn_fs_base__dag_get_id(node)),
                 txn_id) == 0);
}


svn_error_t *
svn_fs_base__dag_get_node(dag_node_t **node,
                          svn_fs_t *fs,
                          const svn_fs_id_t *id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  dag_node_t *new_node;
  node_revision_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc(pool, sizeof(*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs_base__id_copy(id, pool);

  /* Grab the contents so we can cache some of the immutable parts of it. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, id, trail, pool));

  /* Initialize the KIND and CREATED_PATH attributes */
  new_node->kind = noderev->kind;
  new_node->created_path = noderev->created_path;

  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_get_revision(svn_revnum_t *rev,
                              dag_node_t *node,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  /* Use the txn ID from the NODE's id to look up the transaction and
     get its revision number.  */
  return svn_fs_base__txn_get_revision
    (rev, svn_fs_base__dag_get_fs(node),
     svn_fs_base__id_txn_id(svn_fs_base__dag_get_id(node)), trail, pool);
}


svn_error_t *
svn_fs_base__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                    dag_node_t *node,
                                    trail_t *trail,
                                    apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id,
                                        trail, pool));
  *id_p = noderev->predecessor_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_get_predecessor_count(int *count,
                                       dag_node_t *node,
                                       trail_t *trail,
                                       apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id,
                                        trail, pool));
  *count = noderev->predecessor_count;
  return SVN_NO_ERROR;
}


/* Trail body for svn_fs_base__dag_init_fs. */
static svn_error_t *
txn_body_dag_init_fs(void *baton,
                     trail_t *trail)
{
  node_revision_t noderev;
  revision_t revision;
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  svn_fs_t *fs = trail->fs;
  svn_string_t date;
  const char *txn_id;
  const char *copy_id;
  svn_fs_id_t *root_id = svn_fs_base__id_create("0", "0", "0", trail->pool);

  /* Create empty root directory with node revision 0.0.0. */
  memset(&noderev, 0, sizeof(noderev));
  noderev.kind = svn_node_dir;
  noderev.created_path = "/";
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, root_id, &noderev,
                                        trail, trail->pool));

  /* Create a new transaction (better have an id of "0") */
  SVN_ERR(svn_fs_bdb__create_txn(&txn_id, fs, root_id, trail, trail->pool));
  if (strcmp(txn_id, "0"))
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0,
       _("Corrupt DB: initial transaction id not '0' in filesystem '%s'"),
       fs->path);

  /* Create a default copy (better have an id of "0") */
  SVN_ERR(svn_fs_bdb__reserve_copy_id(&copy_id, fs, trail, trail->pool));
  if (strcmp(copy_id, "0"))
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0,
       _("Corrupt DB: initial copy id not '0' in filesystem '%s'"), fs->path);
  SVN_ERR(svn_fs_bdb__create_copy(fs, copy_id, NULL, NULL, root_id,
                                  copy_kind_real, trail, trail->pool));

  /* Link it into filesystem revision 0. */
  revision.txn_id = txn_id;
  SVN_ERR(svn_fs_bdb__put_rev(&rev, fs, &revision, trail, trail->pool));
  if (rev != 0)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, 0,
                             _("Corrupt DB: initial revision number "
                               "is not '0' in filesystem '%s'"), fs->path);

  /* Promote our transaction to a "committed" transaction. */
  SVN_ERR(svn_fs_base__txn_make_committed(fs, txn_id, rev,
                                          trail, trail->pool));

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), trail->pool);
  date.len = strlen(date.data);
  return svn_fs_base__set_rev_prop(fs, 0, SVN_PROP_REVISION_DATE, NULL, &date,
                                   trail, trail->pool);
}


svn_error_t *
svn_fs_base__dag_init_fs(svn_fs_t *fs)
{
  return svn_fs_base__retry_txn(fs, txn_body_dag_init_fs, NULL,
                                TRUE, fs->pool);
}



/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Given directory NODEREV in FS, set *ENTRIES_P to its entries list
   hash, as part of TRAIL, or to NULL if NODEREV has no entries.  The
   entries list will be allocated in POOL, and the entries in that
   list will not have interesting value in their 'kind' fields.  If
   NODEREV is not a directory, return the error SVN_ERR_FS_NOT_DIRECTORY. */
static svn_error_t *
get_dir_entries(apr_hash_t **entries_p,
                svn_fs_t *fs,
                node_revision_t *noderev,
                trail_t *trail,
                apr_pool_t *pool)
{
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;
  svn_string_t entries_raw;
  svn_skel_t *entries_skel;

  /* Error if this is not a directory. */
  if (noderev->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to get entries of a non-directory node"));

  /* If there's a DATA-KEY, there might be entries to fetch. */
  if (noderev->data_key)
    {
      /* Now we have a rep, follow through to get the entries. */
      SVN_ERR(svn_fs_base__rep_contents(&entries_raw, fs, noderev->data_key,
                                        trail, pool));
      entries_skel = svn_skel__parse(entries_raw.data, entries_raw.len, pool);

      /* Were there entries?  Make a hash from them. */
      if (entries_skel)
        SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel,
                                                pool));
    }

  /* No hash?  No problem.  */
  *entries_p = NULL;
  if (! entries)
    return SVN_NO_ERROR;

  /* Else, convert the hash from a name->id mapping to a name->dirent one.  */
  *entries_p = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent = apr_palloc(pool, sizeof(*dirent));

      /* KEY will be the entry name in ancestor, VAL the id.  */
      apr_hash_this(hi, &key, &klen, &val);
      dirent->name = key;
      dirent->id = val;
      dirent->kind = svn_node_unknown;
      apr_hash_set(*entries_p, key, klen, dirent);
    }

  /* Return our findings. */
  return SVN_NO_ERROR;
}


/* Set *ID_P to the node-id for entry NAME in PARENT, as part of
   TRAIL.  If no such entry, set *ID_P to NULL but do not error.  The
   entry is allocated in POOL or in the same pool as PARENT;
   the caller should copy if it cares.  */
static svn_error_t *
dir_entry_id_from_node(const svn_fs_id_t **id_p,
                       dag_node_t *parent,
                       const char *name,
                       trail_t *trail,
                       apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *dirent;

  SVN_ERR(svn_fs_base__dag_dir_entries(&entries, parent, trail, pool));
  if (entries)
    dirent = svn_hash_gets(entries, name);
  else
    dirent = NULL;

  *id_p = dirent ? dirent->id : NULL;
  return SVN_NO_ERROR;
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Allocations are done in TRAIL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry(dag_node_t *parent,
          const char *name,
          const svn_fs_id_t *id,
          const char *txn_id,
          trail_t *trail,
          apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  svn_stream_t *wstream;
  apr_size_t len;
  svn_string_t raw_entries;
  svn_stringbuf_t *raw_entries_buf;
  svn_skel_t *entries_skel;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(parent);

  /* Get the parent's node-revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&parent_noderev, fs, parent->id,
                                        trail, pool));
  rep_key = parent_noderev->data_key;
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));

  /* If the parent node already pointed at a mutable representation,
     we don't need to do anything.  But if it didn't, either because
     the parent didn't refer to any rep yet or because it referred to
     an immutable one, we must make the parent refer to the mutable
     rep we just created. */
  if (! svn_fs_base__same_keys(rep_key, mutable_rep_key))
    {
      parent_noderev->data_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, parent->id, parent_noderev,
                                            trail, pool));
    }

  /* If the new representation inherited nothing, start a new entries
     list for it.  Else, go read its existing entries list. */
  if (rep_key)
    {
      SVN_ERR(svn_fs_base__rep_contents(&raw_entries, fs, rep_key,
                                        trail, pool));
      entries_skel = svn_skel__parse(raw_entries.data, raw_entries.len, pool);
      if (entries_skel)
        SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel,
                                                pool));
    }

  /* If we still have no ENTRIES hash, make one here.  */
  if (! entries)
    entries = apr_hash_make(pool);

  /* Now, add our new entry to the entries list. */
  svn_hash_sets(entries, name, id);

  /* Finally, replace the old entries list with the new one. */
  SVN_ERR(svn_fs_base__unparse_entries_skel(&entries_skel, entries,
                                            pool));
  raw_entries_buf = svn_skel__unparse(entries_skel, pool);
  SVN_ERR(svn_fs_base__rep_contents_write_stream(&wstream, fs,
                                                 mutable_rep_key, txn_id,
                                                 TRUE, trail, pool));
  len = raw_entries_buf->len;
  SVN_ERR(svn_stream_write(wstream, raw_entries_buf->data, &len));
  return svn_stream_close(wstream);
}


/* Make a new entry named NAME in PARENT, as part of TRAIL.  If IS_DIR
   is true, then the node revision the new entry points to will be a
   directory, else it will be a file.  The new node will be allocated
   in POOL.  PARENT must be mutable, and must not have an entry
   named NAME.  */
static svn_error_t *
make_entry(dag_node_t **child_p,
           dag_node_t *parent,
           const char *parent_path,
           const char *name,
           svn_boolean_t is_dir,
           const char *txn_id,
           trail_t *trail,
           apr_pool_t *pool)
{
  const svn_fs_id_t *new_node_id;
  node_revision_t new_noderev;

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
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Check that parent does not already have an entry named NAME. */
  SVN_ERR(dir_entry_id_from_node(&new_node_id, parent, name, trail, pool));
  if (new_node_id)
    return svn_error_createf
      (SVN_ERR_FS_ALREADY_EXISTS, NULL,
       _("Attempted to create entry that already exists"));

  /* Create the new node's NODE-REVISION */
  memset(&new_noderev, 0, sizeof(new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  new_noderev.created_path = svn_fspath__join(parent_path, name, pool);
  SVN_ERR(svn_fs_base__create_node
          (&new_node_id, svn_fs_base__dag_get_fs(parent), &new_noderev,
           svn_fs_base__id_copy_id(svn_fs_base__dag_get_id(parent)),
           txn_id, trail, pool));

  /* Create a new dag_node_t for our new node */
  SVN_ERR(svn_fs_base__dag_get_node(child_p,
                                    svn_fs_base__dag_get_fs(parent),
                                    new_node_id, trail, pool));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  return set_entry(parent, name, svn_fs_base__dag_get_id(*child_p),
                   txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_dir_entries(apr_hash_t **entries,
                             dag_node_t *node,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id,
                                        trail, pool));
  return get_dir_entries(entries, node->fs, noderev, trail, pool);
}


svn_error_t *
svn_fs_base__dag_set_entry(dag_node_t *node,
                           const char *entry_name,
                           const svn_fs_id_t *id,
                           const char *txn_id,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  /* Check it's a directory. */
  if (node->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to set entry in non-directory node"));

  /* Check it's mutable. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_create
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set entry in immutable node"));

  return set_entry(node, entry_name, id, txn_id, trail, pool);
}



/*** Proplists. ***/

svn_error_t *
svn_fs_base__dag_get_proplist(apr_hash_t **proplist_p,
                              dag_node_t *node,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_hash_t *proplist = NULL;
  svn_string_t raw_proplist;
  svn_skel_t *proplist_skel;

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id,
                                        trail, pool));

  /* Get property key (returning early if there isn't one) . */
  if (! noderev->prop_key)
    {
      *proplist_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the string associated with the property rep, parsing it as a
     skel, and then attempt to parse *that* into a property hash.  */
  SVN_ERR(svn_fs_base__rep_contents(&raw_proplist,
                                    svn_fs_base__dag_get_fs(node),
                                    noderev->prop_key, trail, pool));
  proplist_skel = svn_skel__parse(raw_proplist.data, raw_proplist.len, pool);
  if (proplist_skel)
    SVN_ERR(svn_skel__parse_proplist(&proplist, proplist_skel, pool));

  *proplist_p = proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_set_proplist(dag_node_t *node,
                              const apr_hash_t *proplist,
                              const char *txn_id,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;
  const char *rep_key, *mutable_rep_key;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(node);
  svn_stream_t *wstream;
  apr_size_t len;
  svn_skel_t *proplist_skel;
  svn_stringbuf_t *raw_proplist_buf;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    {
      svn_string_t *idstr = svn_fs_base__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         _("Can't set proplist on *immutable* node-revision %s"),
         idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, node->id,
                                        trail, pool));
  rep_key = noderev->prop_key;

  /* Flatten the proplist into a string. */
  SVN_ERR(svn_skel__unparse_proplist(&proplist_skel, proplist, pool));
  raw_proplist_buf = svn_skel__unparse(proplist_skel, pool);

  /* If this repository supports representation sharing, and the
     resulting property list is exactly the same as another string in
     the database, just use the previously existing string and get
     outta here. */
  if (bfd->format >= SVN_FS_BASE__MIN_REP_SHARING_FORMAT)
    {
      svn_error_t *err;
      const char *dup_rep_key;
      svn_checksum_t *checksum;

      SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1, raw_proplist_buf->data,
                           raw_proplist_buf->len, pool));

      err = svn_fs_bdb__get_checksum_rep(&dup_rep_key, fs, checksum,
                                         trail, pool);
      if (! err)
        {
          if (noderev->prop_key)
            SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->prop_key,
                                                       txn_id, trail, pool));
          noderev->prop_key = dup_rep_key;
          return svn_fs_bdb__put_node_revision(fs, node->id, noderev,
                                               trail, pool);
        }
      else if (err)
        {
          if (err->apr_err != SVN_ERR_FS_NO_SUCH_CHECKSUM_REP)
            return svn_error_trace(err);

          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }
    }

  /* Get a mutable version of this rep (updating the node revision if
     this isn't a NOOP)  */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));
  if (! svn_fs_base__same_keys(mutable_rep_key, rep_key))
    {
      noderev->prop_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, node->id, noderev,
                                            trail, pool));
    }

  /* Replace the old property list with the new one. */
  SVN_ERR(svn_fs_base__rep_contents_write_stream(&wstream, fs,
                                                 mutable_rep_key, txn_id,
                                                 TRUE, trail, pool));
  len = raw_proplist_buf->len;
  SVN_ERR(svn_stream_write(wstream, raw_proplist_buf->data, &len));
  SVN_ERR(svn_stream_close(wstream));

  return SVN_NO_ERROR;
}



/*** Roots. ***/

svn_error_t *
svn_fs_base__dag_revision_root(dag_node_t **node_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  const svn_fs_id_t *root_id;

  SVN_ERR(svn_fs_base__rev_get_root(&root_id, fs, rev, trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_txn_root(dag_node_t **node_p,
                          svn_fs_t *fs,
                          const char *txn_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  const svn_fs_id_t *root_id, *ignored;

  SVN_ERR(svn_fs_base__get_txn_ids(&root_id, &ignored, fs, txn_id,
                                   trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_txn_base_root(dag_node_t **node_p,
                               svn_fs_t *fs,
                               const char *txn_id,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *ignored;

  SVN_ERR(svn_fs_base__get_txn_ids(&ignored, &base_root_id, fs, txn_id,
                                   trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, base_root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_clone_child(dag_node_t **child_p,
                             dag_node_t *parent,
                             const char *parent_path,
                             const char *name,
                             const char *copy_id,
                             const char *txn_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs_base__dag_get_fs(parent);

  /* First check that the parent is mutable. */
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to make a child clone with an illegal name '%s'"), name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR(svn_fs_base__dag_open(&cur_entry, parent, name, trail, pool));

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs_base__dag_check_mutable(cur_entry, txn_id))
    {
      /* This has already been cloned */
      new_node_id = cur_entry->id;
    }
  else
    {
      node_revision_t *noderev;

      /* Go get a fresh NODE-REVISION for current child node. */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, cur_entry->id,
                                            trail, pool));

      /* Do the clone thingy here. */
      noderev->predecessor_id = cur_entry->id;
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      noderev->created_path = svn_fspath__join(parent_path, name, pool);
      SVN_ERR(svn_fs_base__create_successor(&new_node_id, fs, cur_entry->id,
                                            noderev, copy_id, txn_id,
                                            trail, pool));

      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR(set_entry(parent, name, new_node_id, txn_id, trail, pool));
    }

  /* Initialize the youngster. */
  return svn_fs_base__dag_get_node(child_p, fs, new_node_id, trail, pool);
}



svn_error_t *
svn_fs_base__dag_clone_root(dag_node_t **root_p,
                            svn_fs_t *fs,
                            const char *txn_id,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *root_id;
  node_revision_t *noderev;

  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR(svn_fs_base__get_txn_ids(&root_id, &base_root_id, fs, txn_id,
                                   trail, pool));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs_base__id_eq(root_id, base_root_id))
    {
      const char *base_copy_id = svn_fs_base__id_copy_id(base_root_id);

      /* Of my own flesh and bone...
         (Get the NODE-REVISION for the base node, and then write
         it back out as the clone.) */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, base_root_id,
                                            trail, pool));

      /* With its Y-chromosome changed to X...
         (Store it with an updated predecessor count.) */
      /* ### TODO: Does it even makes sense to have a different copy id for
         the root node?  That is, does this function need a copy_id
         passed in?  */
      noderev->predecessor_id = svn_fs_base__id_copy(base_root_id, pool);
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      SVN_ERR(svn_fs_base__create_successor(&root_id, fs, base_root_id,
                                            noderev, base_copy_id,
                                            txn_id, trail, pool));

      /* ... And when it is grown
       *      Then my own little clone
       *        Will be of the opposite sex!
       */
      SVN_ERR(svn_fs_base__set_txn_root(fs, txn_id, root_id, trail, pool));
    }

  /*
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  /* One way or another, root_id now identifies a cloned root node. */
  return svn_fs_base__dag_get_node(root_p, fs, root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_delete(dag_node_t *parent,
                        const char *name,
                        const char *txn_id,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  svn_skel_t *entries_skel;
  svn_fs_t *fs = parent->fs;
  svn_string_t str;
  svn_fs_id_t *id = NULL;
  dag_node_t *node;

  /* Make sure parent is a directory. */
  if (parent->kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to delete entry '%s' from *non*-directory node"), name);

  /* Make sure parent is mutable. */
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to delete entry '%s' from immutable directory node"),
       name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to delete a node with an illegal name '%s'"), name);

  /* Get a fresh NODE-REVISION for the parent node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&parent_noderev, fs, parent->id,
                                        trail, pool));

  /* Get the key for the parent's entries list (data) representation. */
  rep_key = parent_noderev->data_key;

  /* No REP_KEY means no representation, and no representation means
     no data, and no data means no entries...there's nothing here to
     delete! */
  if (! rep_key)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       _("Delete failed: directory has no entry '%s'"), name);

  /* Ensure we have a key to a mutable representation of the entries
     list.  We'll have to update the NODE-REVISION if it points to an
     immutable version.  */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));
  if (! svn_fs_base__same_keys(mutable_rep_key, rep_key))
    {
      parent_noderev->data_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, parent->id, parent_noderev,
                                            trail, pool));
    }

  /* Read the representation, then use it to get the string that holds
     the entries list.  Parse that list into a skel, and parse *that*
     into a hash. */

  SVN_ERR(svn_fs_base__rep_contents(&str, fs, rep_key, trail, pool));
  entries_skel = svn_skel__parse(str.data, str.len, pool);
  if (entries_skel)
    SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel, pool));

  /* Find NAME in the ENTRIES skel.  */
  if (entries)
    id = svn_hash_gets(entries, name);

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! id)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       _("Delete failed: directory has no entry '%s'"), name);

  /* Use the ID of this ENTRY to get the entry's node.  */
  SVN_ERR(svn_fs_base__dag_get_node(&node, svn_fs_base__dag_get_fs(parent),
                                    id, trail, pool));

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR(svn_fs_base__dag_delete_if_mutable(parent->fs, id, txn_id,
                                             trail, pool));

  /* Remove this entry from its parent's entries list. */
  svn_hash_sets(entries, name, NULL);

  /* Replace the old entries list with the new one. */
  {
    svn_stream_t *ws;
    svn_stringbuf_t *unparsed_entries;
    apr_size_t len;

    SVN_ERR(svn_fs_base__unparse_entries_skel(&entries_skel, entries, pool));
    unparsed_entries = svn_skel__unparse(entries_skel, pool);
    SVN_ERR(svn_fs_base__rep_contents_write_stream(&ws, fs, mutable_rep_key,
                                                   txn_id, TRUE, trail,
                                                   pool));
    len = unparsed_entries->len;
    SVN_ERR(svn_stream_write(ws, unparsed_entries->data, &len));
    SVN_ERR(svn_stream_close(ws));
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_remove_node(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             const char *txn_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  dag_node_t *node;
  node_revision_t *noderev;

  /* Fetch the node. */
  SVN_ERR(svn_fs_base__dag_get_node(&node, fs, id, trail, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             _("Attempted removal of immutable node"));

  /* Get a fresh node-revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, id, trail, pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->prop_key,
                                               txn_id, trail, pool));

  /* Delete any mutable data representation. */
  if (noderev->data_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->data_key,
                                               txn_id, trail, pool));

  /* Delete any mutable edit representation (files only). */
  if (noderev->edit_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->edit_key,
                                               txn_id, trail, pool));

  /* Delete the node revision itself. */
  return svn_fs_base__delete_node_revision(fs, id,
                                           noderev->predecessor_id == NULL,
                                           trail, pool);
}


svn_error_t *
svn_fs_base__dag_delete_if_mutable(svn_fs_t *fs,
                                   const svn_fs_id_t *id,
                                   const char *txn_id,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_base__dag_get_node(&node, fs, id, trail, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (node->kind == svn_node_dir)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      /* Loop over hash entries */
      SVN_ERR(svn_fs_base__dag_dir_entries(&entries, node, trail, pool));
      if (entries)
        {
          apr_pool_t *subpool = svn_pool_create(pool);
          for (hi = apr_hash_first(pool, entries);
               hi;
               hi = apr_hash_next(hi))
            {
              void *val;
              svn_fs_dirent_t *dirent;

              svn_pool_clear(subpool);
              apr_hash_this(hi, NULL, NULL, &val);
              dirent = val;
              SVN_ERR(svn_fs_base__dag_delete_if_mutable(fs, dirent->id,
                                                         txn_id, trail,
                                                         subpool));
            }
          svn_pool_destroy(subpool);
        }
    }

  /* ... then delete the node itself, any mutable representations and
     strings it points to, and possibly its node-origins record. */
  return svn_fs_base__dag_remove_node(fs, id, txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_make_file(dag_node_t **child_p,
                           dag_node_t *parent,
                           const char *parent_path,
                           const char *name,
                           const char *txn_id,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, FALSE,
                    txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_make_dir(dag_node_t **child_p,
                          dag_node_t *parent,
                          const char *parent_path,
                          const char *name,
                          const char *txn_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, TRUE,
                    txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_get_contents(svn_stream_t **contents,
                              dag_node_t *file,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get textual contents of a *non*-file node"));

  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id,
                                        trail, pool));

  /* Our job is to _return_ a stream on the file's contents, so the
     stream has to be trail-independent.  Here, we pass NULL to tell
     the stream that we're not providing it a trail that lives across
     reads.  This means the stream will do each read in a one-off,
     temporary trail.  */
  return svn_fs_base__rep_contents_read_stream(contents, file->fs,
                                               noderev->data_key,
                                               FALSE, trail, pool);

  /* Note that we're not registering any `close' func, because there's
     nothing to cleanup outside of our trail.  When the trail is
     freed, the stream/baton will be too. */
}


svn_error_t *
svn_fs_base__dag_file_length(svn_filesize_t *length,
                             dag_node_t *file,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get length of a *non*-file node"));

  /* Go get a fresh node-revision for FILE, and . */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id,
                                        trail, pool));
  if (noderev->data_key)
    SVN_ERR(svn_fs_base__rep_contents_size(length, file->fs,
                                           noderev->data_key, trail, pool));
  else
    *length = 0;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_file_checksum(svn_checksum_t **checksum,
                               svn_checksum_kind_t checksum_kind,
                               dag_node_t *file,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  node_revision_t *noderev;

  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get checksum of a *non*-file node"));

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id,
                                        trail, pool));
  if (! noderev->data_key)
    {
      *checksum = NULL;
      return SVN_NO_ERROR;
    }

  if (checksum_kind == svn_checksum_md5)
    return svn_fs_base__rep_contents_checksums(checksum, NULL, file->fs,
                                               noderev->data_key,
                                               trail, pool);
  else if (checksum_kind == svn_checksum_sha1)
    return svn_fs_base__rep_contents_checksums(NULL, checksum, file->fs,
                                               noderev->data_key,
                                               trail, pool);
  else
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
}


svn_error_t *
svn_fs_base__dag_get_edit_stream(svn_stream_t **contents,
                                 dag_node_t *file,
                                 const char *txn_id,
                                 trail_t *trail,
                                 apr_pool_t *pool)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  node_revision_t *noderev;
  const char *mutable_rep_key;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to set textual contents of a *non*-file node"));

  /* Make sure our node is mutable. */
  if (! svn_fs_base__dag_check_mutable(file, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set textual contents of an immutable node"));

  /* Get the node revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, file->id,
                                        trail, pool));

  /* If this node already has an EDIT-DATA-KEY, destroy the data
     associated with that key.  */
  if (noderev->edit_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->edit_key,
                                               txn_id, trail, pool));

  /* Now, let's ensure that we have a new EDIT-DATA-KEY available for
     use. */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, NULL, fs,
                                       txn_id, trail, pool));

  /* We made a new rep, so update the node revision. */
  noderev->edit_key = mutable_rep_key;
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, file->id, noderev,
                                        trail, pool));

  /* Return a writable stream with which to set new contents. */
  SVN_ERR(svn_fs_base__rep_contents_write_stream(&ws, fs, mutable_rep_key,
                                                 txn_id, FALSE, trail,
                                                 pool));
  *contents = ws;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__dag_finalize_edits(dag_node_t *file,
                                const svn_checksum_t *checksum,
                                const char *txn_id,
                                trail_t *trail,
                                apr_pool_t *pool)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  node_revision_t *noderev;
  const char *old_data_key, *new_data_key, *useless_data_key = NULL;
  const char *data_key_uniquifier = NULL;
  svn_checksum_t *md5_checksum, *sha1_checksum;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to set textual contents of a *non*-file node"));

  /* Make sure our node is mutable. */
  if (! svn_fs_base__dag_check_mutable(file, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set textual contents of an immutable node"));

  /* Get the node revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, file->id,
                                        trail, pool));

  /* If this node has no EDIT-DATA-KEY, this is a no-op. */
  if (! noderev->edit_key)
    return SVN_NO_ERROR;

  /* Get our representation's checksums. */
  SVN_ERR(svn_fs_base__rep_contents_checksums(&md5_checksum, &sha1_checksum,
                                              fs, noderev->edit_key,
                                              trail, pool));

  /* If our caller provided a checksum of the right kind to compare, do so. */
  if (checksum)
    {
      svn_checksum_t *test_checksum;

      if (checksum->kind == svn_checksum_md5)
        test_checksum = md5_checksum;
      else if (checksum->kind == svn_checksum_sha1)
        test_checksum = sha1_checksum;
      else
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);

      if (! svn_checksum_match(checksum, test_checksum))
        return svn_checksum_mismatch_err(checksum, test_checksum, pool,
                        _("Checksum mismatch on representation '%s'"),
                        noderev->edit_key);
    }

  /* Now, we want to delete the old representation and replace it with
     the new.  Of course, we don't actually delete anything until
     everything is being properly referred to by the node-revision
     skel.

     Now, if the result of all this editing is that we've created a
     representation that describes content already represented
     immutably in our database, we don't even need to keep these edits.
     We can simply point our data_key at that pre-existing
     representation and throw away our work!  In this situation,
     though, we'll need a unique ID to help other code distinguish
     between "the contents weren't touched" and "the contents were
     touched but still look the same" (to state it oversimply).  */
  old_data_key = noderev->data_key;
  if (sha1_checksum && bfd->format >= SVN_FS_BASE__MIN_REP_SHARING_FORMAT)
    {
      svn_error_t *err = svn_fs_bdb__get_checksum_rep(&new_data_key, fs,
                                                      sha1_checksum,
                                                      trail, pool);
      if (! err)
        {
          useless_data_key = noderev->edit_key;
          err = svn_fs_bdb__reserve_rep_reuse_id(&data_key_uniquifier,
                                                 trail->fs, trail, pool);
        }
      else if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_CHECKSUM_REP))
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          new_data_key = noderev->edit_key;
        }
      SVN_ERR(err);
    }
  else
    {
      new_data_key = noderev->edit_key;
    }

  noderev->data_key = new_data_key;
  noderev->data_key_uniquifier = data_key_uniquifier;
  noderev->edit_key = NULL;

  SVN_ERR(svn_fs_bdb__put_node_revision(fs, file->id, noderev, trail, pool));

  /* Only *now* can we safely destroy the old representation (if it
     even existed in the first place). */
  if (old_data_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, old_data_key, txn_id,
                                               trail, pool));

  /* If we've got a discardable rep (probably because we ended up
     re-using a preexisting one), throw out the discardable rep. */
  if (useless_data_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, useless_data_key,
                                               txn_id, trail, pool));

  return SVN_NO_ERROR;
}



dag_node_t *
svn_fs_base__dag_dup(const dag_node_t *node,
                     apr_pool_t *pool)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc(pool, sizeof(*new_node));

  new_node->fs = node->fs;
  new_node->id = svn_fs_base__id_copy(node->id, pool);
  new_node->kind = node->kind;
  new_node->created_path = apr_pstrdup(pool, node->created_path);
  return new_node;
}


svn_error_t *
svn_fs_base__dag_open(dag_node_t **child_p,
                      dag_node_t *parent,
                      const char *name,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  const svn_fs_id_t *node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR(dir_entry_id_from_node(&node_id, parent, name, trail, pool));
  if (! node_id)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("Attempted to open non-existent child node '%s'"), name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to open node with an illegal name '%s'"), name);

  /* Now get the node that was requested. */
  return svn_fs_base__dag_get_node(child_p, svn_fs_base__dag_get_fs(parent),
                                   node_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_copy(dag_node_t *to_node,
                      const char *entry,
                      dag_node_t *from_node,
                      svn_boolean_t preserve_history,
                      svn_revnum_t from_rev,
                      const char *from_path,
                      const char *txn_id,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (preserve_history)
    {
      node_revision_t *noderev;
      const char *copy_id;
      svn_fs_t *fs = svn_fs_base__dag_get_fs(from_node);
      const svn_fs_id_t *src_id = svn_fs_base__dag_get_id(from_node);
      const char *from_txn_id = NULL;

      /* Make a copy of the original node revision. */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, from_node->id,
                                            trail, pool));

      /* Reserve a copy ID for this new copy. */
      SVN_ERR(svn_fs_bdb__reserve_copy_id(&copy_id, fs, trail, pool));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      noderev->predecessor_id = svn_fs_base__id_copy(src_id, pool);
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      noderev->created_path = svn_fspath__join
        (svn_fs_base__dag_get_created_path(to_node), entry, pool);
      SVN_ERR(svn_fs_base__create_successor(&id, fs, src_id, noderev,
                                            copy_id, txn_id, trail, pool));

      /* Translate FROM_REV into a transaction ID. */
      SVN_ERR(svn_fs_base__rev_get_txn_id(&from_txn_id, fs, from_rev,
                                          trail, pool));

      /* Now that we've done the copy, we need to add the information
         about the copy to the `copies' table, using the COPY_ID we
         reserved above.  */
      SVN_ERR(svn_fs_bdb__create_copy
              (fs, copy_id,
               svn_fs__canonicalize_abspath(from_path, pool),
               from_txn_id, id, copy_kind_real, trail, pool));

      /* Finally, add the COPY_ID to the transaction's list of copies
         so that, if this transaction is aborted, the `copies' table
         entry we added above will be cleaned up. */
      SVN_ERR(svn_fs_base__add_txn_copy(fs, txn_id, copy_id, trail, pool));
    }
  else  /* don't preserve history */
    {
      id = svn_fs_base__dag_get_id(from_node);
    }

  /* Set the entry in to_node to the new id. */
  return svn_fs_base__dag_set_entry(to_node, entry, id, txn_id,
                                    trail, pool);
}



/*** Deltification ***/

/* Maybe change the representation identified by TARGET_REP_KEY to be
   a delta against the representation identified by SOURCE_REP_KEY.
   Some reasons why we wouldn't include:

      - TARGET_REP_KEY and SOURCE_REP_KEY are the same key.

      - TARGET_REP_KEY's representation isn't mutable in TXN_ID (if
        TXN_ID is non-NULL).

      - The delta provides less space savings that a fulltext (this is
        a detail handled by lower logic layers, not this function).

   Do this work in TRAIL, using POOL for necessary allocations.
*/
static svn_error_t *
maybe_deltify_mutable_rep(const char *target_rep_key,
                          const char *source_rep_key,
                          const char *txn_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  if (! (target_rep_key && source_rep_key
         && (strcmp(target_rep_key, source_rep_key) != 0)))
    return SVN_NO_ERROR;

  if (txn_id)
    {
      representation_t *target_rep;
      SVN_ERR(svn_fs_bdb__read_rep(&target_rep, trail->fs, target_rep_key,
                                   trail, pool));
      if (strcmp(target_rep->txn_id, txn_id) != 0)
        return SVN_NO_ERROR;
    }

  return svn_fs_base__rep_deltify(trail->fs, target_rep_key, source_rep_key,
                                  trail, pool);
}


svn_error_t *
svn_fs_base__dag_deltify(dag_node_t *target,
                         dag_node_t *source,
                         svn_boolean_t props_only,
                         const char *txn_id,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  node_revision_t *source_nr, *target_nr;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(target);

  /* Get node revisions for the two nodes.  */
  SVN_ERR(svn_fs_bdb__get_node_revision(&target_nr, fs, target->id,
                                        trail, pool));
  SVN_ERR(svn_fs_bdb__get_node_revision(&source_nr, fs, source->id,
                                        trail, pool));

  /* If TARGET and SOURCE both have properties, and are not sharing a
     property key, deltify TARGET's properties.  */
  SVN_ERR(maybe_deltify_mutable_rep(target_nr->prop_key, source_nr->prop_key,
                                    txn_id, trail, pool));

  /* If we are not only attending to properties, and if TARGET and
     SOURCE both have data, and are not sharing a data key, deltify
     TARGET's data.  */
  if (! props_only)
    SVN_ERR(maybe_deltify_mutable_rep(target_nr->data_key, source_nr->data_key,
                                      txn_id, trail, pool));

  return SVN_NO_ERROR;
}

/* Maybe store a `checksum-reps' index record for the representation whose
   key is REP.  (If there's already a rep for this checksum, we don't
   bother overwriting it.)  */
static svn_error_t *
maybe_store_checksum_rep(const char *rep,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_fs_t *fs = trail->fs;
  svn_checksum_t *sha1_checksum;

  /* We want the SHA1 checksum, if any. */
  SVN_ERR(svn_fs_base__rep_contents_checksums(NULL, &sha1_checksum,
                                              fs, rep, trail, pool));
  if (sha1_checksum)
    {
      err = svn_fs_bdb__set_checksum_rep(fs, sha1_checksum, rep, trail, pool);
      if (err && (err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }
    }
  return svn_error_trace(err);
}

svn_error_t *
svn_fs_base__dag_index_checksums(dag_node_t *node,
                                 trail_t *trail,
                                 apr_pool_t *pool)
{
  node_revision_t *node_rev;

  SVN_ERR(svn_fs_bdb__get_node_revision(&node_rev, trail->fs, node->id,
                                        trail, pool));
  if ((node_rev->kind == svn_node_file) && node_rev->data_key)
    SVN_ERR(maybe_store_checksum_rep(node_rev->data_key, trail, pool));
  if (node_rev->prop_key)
    SVN_ERR(maybe_store_checksum_rep(node_rev->prop_key, trail, pool));

  return SVN_NO_ERROR;
}



/*** Committing ***/

svn_error_t *
svn_fs_base__dag_commit_txn(svn_revnum_t *new_rev,
                            svn_fs_txn_t *txn,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  revision_t revision;
  apr_hash_t *txnprops;
  svn_fs_t *fs = txn->fs;
  const char *txn_id = txn->id;
  const svn_string_t *client_date;

  /* Remove any temporary transaction properties initially created by
     begin_txn().  */
  SVN_ERR(svn_fs_base__txn_proplist_in_trail(&txnprops, txn_id, trail));

  /* Add new revision entry to `revisions' table. */
  revision.txn_id = txn_id;
  *new_rev = SVN_INVALID_REVNUM;
  SVN_ERR(svn_fs_bdb__put_rev(new_rev, fs, &revision, trail, pool));

  if (svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CHECK_OOD))
    SVN_ERR(svn_fs_base__set_txn_prop
            (fs, txn_id, SVN_FS__PROP_TXN_CHECK_OOD, NULL, trail, pool));

  if (svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CHECK_LOCKS))
    SVN_ERR(svn_fs_base__set_txn_prop
            (fs, txn_id, SVN_FS__PROP_TXN_CHECK_LOCKS, NULL, trail, pool));

  client_date = svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CLIENT_DATE);
  if (client_date)
    SVN_ERR(svn_fs_base__set_txn_prop
            (fs, txn_id, SVN_FS__PROP_TXN_CLIENT_DATE, NULL, trail, pool));

  /* Promote the unfinished transaction to a committed one. */
  SVN_ERR(svn_fs_base__txn_make_committed(fs, txn_id, *new_rev,
                                          trail, pool));

  if (!client_date || strcmp(client_date->data, "1"))
    {
      /* Set a date on the commit if requested.  We wait until now to fetch the
         date, so it's definitely newer than any previous revision's date. */
      svn_string_t date;
      date.data = svn_time_to_cstring(apr_time_now(), pool);
      date.len = strlen(date.data);
      SVN_ERR(svn_fs_base__set_rev_prop(fs, *new_rev, SVN_PROP_REVISION_DATE,
                                        NULL, &date, trail, pool));
    }

  return SVN_NO_ERROR;
}


/*** Comparison. ***/

svn_error_t *
svn_fs_base__things_different(svn_boolean_t *props_changed,
                              svn_boolean_t *contents_changed,
                              dag_node_t *node1,
                              dag_node_t *node2,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev1, *noderev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The node revision skels for these two nodes. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev1, node1->fs, node1->id,
                                        trail, pool));
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev2, node2->fs, node2->id,
                                        trail, pool));

  /* Compare property keys. */
  if (props_changed != NULL)
    *props_changed = (! svn_fs_base__same_keys(noderev1->prop_key,
                                               noderev2->prop_key));

  /* Compare contents keys and their (optional) uniquifiers. */
  if (contents_changed != NULL)
    *contents_changed =
      (! (svn_fs_base__same_keys(noderev1->data_key,
                                 noderev2->data_key)
          /* Technically, these uniquifiers aren't used and "keys",
             but keys are base-36 stringified numbers, so we'll take
             this liberty. */
          && (svn_fs_base__same_keys(noderev1->data_key_uniquifier,
                                     noderev2->data_key_uniquifier))));

  return SVN_NO_ERROR;
}



/*** Mergeinfo tracking stuff ***/

svn_error_t *
svn_fs_base__dag_get_mergeinfo_stats(svn_boolean_t *has_mergeinfo,
                                     apr_int64_t *count,
                                     dag_node_t *node,
                                     trail_t *trail,
                                     apr_pool_t *pool)
{
  node_revision_t *node_rev;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(node);
  const svn_fs_id_t *id = svn_fs_base__dag_get_id(node);

  SVN_ERR(svn_fs_bdb__get_node_revision(&node_rev, fs, id, trail, pool));
  if (has_mergeinfo)
    *has_mergeinfo = node_rev->has_mergeinfo;
  if (count)
    *count = node_rev->mergeinfo_count;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_set_has_mergeinfo(dag_node_t *node,
                                   svn_boolean_t has_mergeinfo,
                                   svn_boolean_t *had_mergeinfo,
                                   const char *txn_id,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  node_revision_t *node_rev;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(node);
  const svn_fs_id_t *id = svn_fs_base__dag_get_id(node);

  SVN_ERR(svn_fs_base__test_required_feature_format
          (trail->fs, "mergeinfo", SVN_FS_BASE__MIN_MERGEINFO_FORMAT));

  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             _("Attempted merge tracking info change on "
                               "immutable node"));

  SVN_ERR(svn_fs_bdb__get_node_revision(&node_rev, fs, id, trail, pool));
  *had_mergeinfo = node_rev->has_mergeinfo;

  /* Are we changing the node? */
  if ((! has_mergeinfo) != (! *had_mergeinfo))
    {
      /* Note the new has-mergeinfo state. */
      node_rev->has_mergeinfo = has_mergeinfo;

      /* Increment or decrement the mergeinfo count as necessary. */
      if (has_mergeinfo)
        node_rev->mergeinfo_count++;
      else
        node_rev->mergeinfo_count--;

      SVN_ERR(svn_fs_bdb__put_node_revision(fs, id, node_rev, trail, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_adjust_mergeinfo_count(dag_node_t *node,
                                        apr_int64_t count_delta,
                                        const char *txn_id,
                                        trail_t *trail,
                                        apr_pool_t *pool)
{
  node_revision_t *node_rev;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(node);
  const svn_fs_id_t *id = svn_fs_base__dag_get_id(node);

  SVN_ERR(svn_fs_base__test_required_feature_format
          (trail->fs, "mergeinfo", SVN_FS_BASE__MIN_MERGEINFO_FORMAT));

  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             _("Attempted mergeinfo count change on "
                               "immutable node"));

  if (count_delta == 0)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_bdb__get_node_revision(&node_rev, fs, id, trail, pool));
  node_rev->mergeinfo_count = node_rev->mergeinfo_count + count_delta;
  if ((node_rev->mergeinfo_count < 0)
      || ((node->kind == svn_node_file) && (node_rev->mergeinfo_count > 1)))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             apr_psprintf(pool,
                                          _("Invalid value (%%%s) for node "
                                            "revision mergeinfo count"),
                                          APR_INT64_T_FMT),
                             node_rev->mergeinfo_count);

  return svn_fs_bdb__put_node_revision(fs, id, node_rev, trail, pool);
}
