/* dag.h : DAG-like interface filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_DAG_H
#define SVN_LIBSVN_FS_DAG_H

#include "svn_fs.h"

#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The interface in this file provides all the essential filesystem
   operations, but exposes the filesystem's DAG structure.  This makes
   it simpler to implement than the public interface, since a client
   of this interface has to understand and cope with shared structure
   directly as it appears in the database.  However, it's still a
   self-consistent set of invariants to maintain, making it
   (hopefully) a useful interface boundary.

   In other words:

   - The dag_node_t interface exposes the internal DAG structure of
     the filesystem, while the svn_fs.h interface does any cloning
     necessary to make the filesystem look like a tree.

   - The dag_node_t interface exposes the existence of copy nodes,
     whereas the svn_fs.h handles them transparently.

   - dag_node_t's must be explicitly cloned, whereas the svn_fs.h
     operations make clones implicitly.

   - Callers of the dag_node_t interface use Berkeley DB transactions
     to ensure consistency between operations, while callers of the
     svn_fs.h interface use Subversion transactions.  */


/* Initializing a filesystem.  */


/* Given a filesystem FS, which contains all the necessary tables,
   create the initial revision 0, and the initial root directory.  */
svn_error_t *svn_fs_base__dag_init_fs(svn_fs_t *fs);



/* Generic DAG node stuff.  */

typedef struct dag_node_t dag_node_t;


/* Fill *NODE with a dag_node_t representing node revision ID in FS,
   allocating in POOL.  */
svn_error_t *svn_fs_base__dag_get_node(dag_node_t **node,
                                       svn_fs_t *fs,
                                       const svn_fs_id_t *id,
                                       trail_t *trail,
                                       apr_pool_t *pool);


/* Return a new dag_node_t object referring to the same node as NODE,
   allocated in POOL.  */
dag_node_t *svn_fs_base__dag_dup(const dag_node_t *node,
                                 apr_pool_t *pool);


/* Return the filesystem containing NODE.  */
svn_fs_t *svn_fs_base__dag_get_fs(dag_node_t *node);


/* Set *REV to NODE's revision number, as part of TRAIL.  If NODE has
   never been committed as part of a revision, set *REV to
   SVN_INVALID_REVNUM.  */
svn_error_t *svn_fs_base__dag_get_revision(svn_revnum_t *rev,
                                           dag_node_t *node,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Return the node revision ID of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const svn_fs_id_t *svn_fs_base__dag_get_id(dag_node_t *node);


/* Return the created path of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const char *svn_fs_base__dag_get_created_path(dag_node_t *node);


/* Set *ID_P to the node revision ID of NODE's immediate predecessor,
   or NULL if NODE has no predecessor, as part of TRAIL.  The returned
   ID will be allocated in POOL.  */
svn_error_t *svn_fs_base__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                                 dag_node_t *node,
                                                 trail_t *trail,
                                                 apr_pool_t *pool);


/* Set *COUNT to the number of predecessors NODE has (recursively), or
   -1 if not known, as part of TRAIL.  */
svn_error_t *svn_fs_base__dag_get_predecessor_count(int *count,
                                                    dag_node_t *node,
                                                    trail_t *trail,
                                                    apr_pool_t *pool);


/* Return non-zero IFF NODE is currently mutable under Subversion
   transaction TXN_ID.  */
svn_boolean_t svn_fs_base__dag_check_mutable(dag_node_t *node,
                                             const char *txn_id);

/* Return the node kind of NODE. */
svn_node_kind_t svn_fs_base__dag_node_kind(dag_node_t *node);

/* Set *PROPLIST_P to a PROPLIST hash representing the entire property
   list of NODE, as part of TRAIL.  The hash has const char * names
   (the property names) and svn_string_t * values (the property values).

   If properties do not exist on NODE, *PROPLIST_P will be set to NULL.

   The returned property list is allocated in POOL.  */
svn_error_t *svn_fs_base__dag_get_proplist(apr_hash_t **proplist_p,
                                           dag_node_t *node,
                                           trail_t *trail,
                                           apr_pool_t *pool);

/* Set the property list of NODE to PROPLIST, as part of TRAIL.  The
   node being changed must be mutable.  TXN_ID is the Subversion
   transaction under which this occurs.  */
svn_error_t *svn_fs_base__dag_set_proplist(dag_node_t *node,
                                           const apr_hash_t *proplist,
                                           const char *txn_id,
                                           trail_t *trail,
                                           apr_pool_t *pool);



/* Mergeinfo tracking stuff. */

/* If HAS_MERGEINFO is not null, set *HAS_MERGEINFO to TRUE iff NODE
   records that its property list contains merge tracking information.

   If COUNT is not null, set *COUNT to the number of nodes --
   including NODE itself -- in the subtree rooted at NODE which claim
   to carry merge tracking information.

   Do this as part of TRAIL, and use POOL for necessary allocations.

   NOTE:  No validation against NODE's actual property list is
   performed. */
svn_error_t *svn_fs_base__dag_get_mergeinfo_stats(svn_boolean_t *has_mergeinfo,
                                                  apr_int64_t *count,
                                                  dag_node_t *node,
                                                  trail_t *trail,
                                                  apr_pool_t *pool);

/* If HAS_MERGEINFO is set, record on NODE that its property list
   carries merge tracking information.  Otherwise, record on NODE its
   property list does *not* carry merge tracking information.  NODE
   must be mutable under TXN_ID (the Subversion transaction under
   which this operation occurs).  Set *HAD_MERGEINFO to the previous
   state of this record.

   Update the mergeinfo count on NODE as necessary.

   Do all of this as part of TRAIL, and use POOL for necessary
   allocations.

   NOTE:  No validation against NODE's actual property list is
   performed. */
svn_error_t *svn_fs_base__dag_set_has_mergeinfo(dag_node_t *node,
                                                svn_boolean_t has_mergeinfo,
                                                svn_boolean_t *had_mergeinfo,
                                                const char *txn_id,
                                                trail_t *trail,
                                                apr_pool_t *pool);

/* Record on NODE a change of COUNT_DELTA nodes -- including NODE
   itself -- in the subtree rooted at NODE claim to carry merge
   tracking information.  That is, add COUNT_DELTA to NODE's current
   mergeinfo count (regardless of whether COUNT_DELTA is a positive or
   negative integer).

   NODE must be mutable under TXN_ID (the Subversion transaction under
   which this operation occurs).  Do this as part of TRAIL, and use
   POOL for necessary allocations.

   NOTE:  No validation of these claims is performed. */
svn_error_t *svn_fs_base__dag_adjust_mergeinfo_count(dag_node_t *node,
                                                     apr_int64_t count_delta,
                                                     const char *txn_id,
                                                     trail_t *trail,
                                                     apr_pool_t *pool);


/* Revision and transaction roots.  */


/* Open the root of revision REV of filesystem FS, as part of TRAIL.
   Set *NODE_P to the new node.  Allocate the node in POOL.  */
svn_error_t *svn_fs_base__dag_revision_root(dag_node_t **node_p,
                                            svn_fs_t *fs,
                                            svn_revnum_t rev,
                                            trail_t *trail,
                                            apr_pool_t *pool);


/* Set *NODE_P to the root of transaction TXN_ID in FS, as part
   of TRAIL.  Allocate the node in POOL.

   Note that the root node of TXN_ID is not necessarily mutable.  If no
   changes have been made in the transaction, then it may share its
   root directory with its base revision.  To get a mutable root node
   for a transaction, call svn_fs_base__dag_clone_root.  */
svn_error_t *svn_fs_base__dag_txn_root(dag_node_t **node_p,
                                       svn_fs_t *fs,
                                       const char *txn_id,
                                       trail_t *trail,
                                       apr_pool_t *pool);


/* Set *NODE_P to the base root of transaction TXN_ID in FS, as part
   of TRAIL.  Allocate the node in POOL.  */
svn_error_t *svn_fs_base__dag_txn_base_root(dag_node_t **node_p,
                                            svn_fs_t *fs,
                                            const char *txn_id,
                                            trail_t *trail,
                                            apr_pool_t *pool);


/* Clone the root directory of TXN_ID in FS, and update the
   `transactions' table entry to point to it, unless this has been
   done already.  In either case, set *ROOT_P to a reference to the
   root directory clone.  Do all this as part of TRAIL, and allocate
   *ROOT_P in POOL.  */
svn_error_t *svn_fs_base__dag_clone_root(dag_node_t **root_p,
                                         svn_fs_t *fs,
                                         const char *txn_id,
                                         trail_t *trail,
                                         apr_pool_t *pool);


/* Commit the transaction TXN->id in TXN->FS, as part of TRAIL.  Store the
   new revision number in *NEW_REV.  This entails:
   - marking the tree of mutable nodes at TXN->id's root as immutable,
     and marking all their contents as stable
   - creating a new revision, with TXN->id's root as its root directory
   - promoting TXN->id to a "committed" transaction.

   Beware!  This does not make sure that TXN->id is based on the very
   latest revision in TXN->FS.  If the caller doesn't take care of this,
   you may lose people's work!

   Do any necessary temporary allocation in a subpool of POOL.
   Consume temporary space at most proportional to the maximum depth
   of SVN_TXN's tree of mutable nodes.  */
svn_error_t *svn_fs_base__dag_commit_txn(svn_revnum_t *new_rev,
                                         svn_fs_txn_t *txn,
                                         trail_t *trail,
                                         apr_pool_t *pool);


/* Directories.  */


/* Open the node named NAME in the directory PARENT, as part of TRAIL.
   Set *CHILD_P to the new node, allocated in POOL.  NAME must be a
   single path component; it cannot be a slash-separated directory
   path.  */
svn_error_t *svn_fs_base__dag_open(dag_node_t **child_p,
                                   dag_node_t *parent,
                                   const char *name,
                                   trail_t *trail,
                                   apr_pool_t *pool);


/* Set *ENTRIES_P to a hash table of NODE's entries, as part of TRAIL,
   or NULL if NODE has no entries.  The keys of the table are entry
   names, and the values are svn_fs_dirent_t's.

   The returned table is allocated in POOL.

   NOTE: the 'kind' field of the svn_fs_dirent_t's is set to
   svn_node_unknown by this function -- callers that need in
   interesting value in these slots should fill them in using a new
   TRAIL, since the list of entries can be arbitrarily large.  */
svn_error_t *svn_fs_base__dag_dir_entries(apr_hash_t **entries_p,
                                          dag_node_t *node,
                                          trail_t *trail,
                                          apr_pool_t *pool);


/* Set ENTRY_NAME in NODE to point to ID, as part of TRAIL.  NODE must
   be a mutable directory.  ID can refer to a mutable or immutable
   node.  If ENTRY_NAME does not exist, it will be created.  TXN_ID is
   the Subversion transaction under which this occurs.*/
svn_error_t *svn_fs_base__dag_set_entry(dag_node_t *node,
                                        const char *entry_name,
                                        const svn_fs_id_t *id,
                                        const char *txn_id,
                                        trail_t *trail,
                                        apr_pool_t *pool);


/* Make a new mutable clone of the node named NAME in PARENT, and
   adjust PARENT's directory entry to point to it, as part of TRAIL,
   unless NAME in PARENT already refers to a mutable node.  In either
   case, set *CHILD_P to a reference to the new node, allocated in
   POOL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.
   PARENT_PATH must be the canonicalized absolute path of the parent
   directory.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   PATH is the canonicalized absolute path at which this node is being
   created.

   TXN_ID is the Subversion transaction under which this occurs.  */
svn_error_t *svn_fs_base__dag_clone_child(dag_node_t **child_p,
                                          dag_node_t *parent,
                                          const char *parent_path,
                                          const char *name,
                                          const char *copy_id,
                                          const char *txn_id,
                                          trail_t *trail,
                                          apr_pool_t *pool);


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If the
   entry being deleted points to a mutable node revision, also remove
   that node revision and (if it is a directory) all mutable node
   revisions reachable from it.  Also delete the node-origins record
   for each deleted node revision that had no predecessor.

   TXN_ID is the Subversion transaction under which this occurs.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
svn_error_t *svn_fs_base__dag_delete(dag_node_t *parent,
                                     const char *name,
                                     const char *txn_id,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Delete the node revision assigned to node ID from FS's `nodes'
   table, as part of TRAIL.  Also delete any mutable representations
   and strings associated with that node revision.  Also delete the
   node-origins record for this node revision's node id, if this node
   revision had no predecessor.

   ID may refer to a file or directory, which must be mutable.  TXN_ID
   is the Subversion transaction under which this occurs.

   NOTE: If ID represents a directory, and that directory has mutable
   children, you risk orphaning those children by leaving them
   dangling, disconnected from all DAG trees.  It is assumed that
   callers of this interface know what in the world they are doing.  */
svn_error_t *svn_fs_base__dag_remove_node(svn_fs_t *fs,
                                          const svn_fs_id_t *id,
                                          const char *txn_id,
                                          trail_t *trail,
                                          apr_pool_t *pool);


/* Delete all mutable node revisions reachable from node ID, including
   ID itself, from FS's `nodes' table, as part of TRAIL.  Also delete
   any mutable representations and strings associated with that node
   revision.  Also delete the node-origins record for each deleted
   node revision that had no predecessor.

   ID may refer to a file or directory, which may be mutable or
   immutable.  TXN_ID is the Subversion transaction under which this
   occurs.  */
svn_error_t *svn_fs_base__dag_delete_if_mutable(svn_fs_t *fs,
                                                const svn_fs_id_t *id,
                                                const char *txn_id,
                                                trail_t *trail,
                                                apr_pool_t *pool);


/* Create a new mutable directory named NAME in PARENT, as part of
   TRAIL.  Set *CHILD_P to a reference to the new node, allocated in
   POOL.  The new directory has no contents, and no properties.
   PARENT must be mutable.  NAME must be a single path component; it
   cannot be a slash-separated directory path.  PARENT_PATH must be
   the canonicalized absolute path of the parent directory.  PARENT
   must not currently have an entry named NAME.  Do any temporary
   allocation in POOL.  TXN_ID is the Subversion transaction
   under which this occurs.  */
svn_error_t *svn_fs_base__dag_make_dir(dag_node_t **child_p,
                                       dag_node_t *parent,
                                       const char *parent_path,
                                       const char *name,
                                       const char *txn_id,
                                       trail_t *trail,
                                       apr_pool_t *pool);



/* Files.  */


/* Set *CONTENTS to a readable generic stream which yields the
   contents of FILE, as part of TRAIL.  Allocate the stream in POOL.
   If FILE is not a file, return SVN_ERR_FS_NOT_FILE.  */
svn_error_t *svn_fs_base__dag_get_contents(svn_stream_t **contents,
                                           dag_node_t *file,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Return a generic writable stream in *CONTENTS with which to set the
   contents of FILE as part of TRAIL.  Allocate the stream in POOL.
   TXN_ID is the Subversion transaction under which this occurs.  Any
   previous edits on the file will be deleted, and a new edit stream
   will be constructed.  */
svn_error_t *svn_fs_base__dag_get_edit_stream(svn_stream_t **contents,
                                              dag_node_t *file,
                                              const char *txn_id,
                                              trail_t *trail,
                                              apr_pool_t *pool);


/* Signify the completion of edits to FILE made using the stream
   returned by svn_fs_base__dag_get_edit_stream, as part of TRAIL.  TXN_ID
   is the Subversion transaction under which this occurs.

   If CHECKSUM is non-null, it must match the checksum for FILE's
   contents (note: this is not recalculated, the recorded checksum is
   used), else the error SVN_ERR_CHECKSUM_MISMATCH is returned.

   This operation is a no-op if no edits are present.  */
svn_error_t *svn_fs_base__dag_finalize_edits(dag_node_t *file,
                                             const svn_checksum_t *checksum,
                                             const char *txn_id,
                                             trail_t *trail,
                                             apr_pool_t *pool);


/* Set *LENGTH to the length of the contents of FILE, as part of TRAIL. */
svn_error_t *svn_fs_base__dag_file_length(svn_filesize_t *length,
                                          dag_node_t *file,
                                          trail_t *trail,
                                          apr_pool_t *pool);

/* Put the checksum of type CHECKSUM_KIND recorded for FILE into
 * CHECKSUM, as part of TRAIL.
 *
 * If no stored checksum of the requested kind is available, do not
 * calculate the checksum, just put NULL into CHECKSUM.
 */
svn_error_t *svn_fs_base__dag_file_checksum(svn_checksum_t **checksum,
                                            svn_checksum_kind_t checksum_kind,
                                            dag_node_t *file,
                                            trail_t *trail,
                                            apr_pool_t *pool);

/* Create a new mutable file named NAME in PARENT, as part of TRAIL.
   Set *CHILD_P to a reference to the new node, allocated in
   POOL.  The new file's contents are the empty string, and it
   has no properties.  PARENT must be mutable.  NAME must be a single
   path component; it cannot be a slash-separated directory path.
   PARENT_PATH must be the canonicalized absolute path of the parent
   directory.  TXN_ID is the Subversion transaction under which this
   occurs.  */
svn_error_t *svn_fs_base__dag_make_file(dag_node_t **child_p,
                                        dag_node_t *parent,
                                        const char *parent_path,
                                        const char *name,
                                        const char *txn_id,
                                        trail_t *trail,
                                        apr_pool_t *pool);



/* Copies */

/* Make ENTRY in TO_NODE be a copy of FROM_NODE, as part of TRAIL.
   TO_NODE must be mutable.  TXN_ID is the Subversion transaction
   under which this occurs.

   If PRESERVE_HISTORY is true, the new node will record that it was
   copied from FROM_PATH in FROM_REV; therefore, FROM_NODE should be
   the node found at FROM_PATH in FROM_REV, although this is not
   checked.

   If PRESERVE_HISTORY is false, FROM_PATH and FROM_REV are ignored.  */
svn_error_t *svn_fs_base__dag_copy(dag_node_t *to_node,
                                   const char *entry,
                                   dag_node_t *from_node,
                                   svn_boolean_t preserve_history,
                                   svn_revnum_t from_rev,
                                   const char *from_path,
                                   const char *txn_id,
                                   trail_t *trail,
                                   apr_pool_t *pool);



/* Deltification */

/* Change TARGET's representation to be a delta against SOURCE, as
   part of TRAIL.  If TARGET or SOURCE does not exist, do nothing and
   return success.  If PROPS_ONLY is non-zero, only the node property
   portion of TARGET will be deltified.

   If TXN_ID is non-NULL, it is the transaction ID in which TARGET's
   representation(s) must have been created (otherwise deltification
   is silently not attempted).

   WARNING WARNING WARNING: Do *NOT* call this with a mutable SOURCE
   node.  Things will go *very* sour if you deltify TARGET against a
   node that might just disappear from the filesystem in the (near)
   future.  */
svn_error_t *svn_fs_base__dag_deltify(dag_node_t *target,
                                      dag_node_t *source,
                                      svn_boolean_t props_only,
                                      const char *txn_id,
                                      trail_t *trail,
                                      apr_pool_t *pool);

/* Index NODE's backing data representations by their checksum.  Do
   this as part of TRAIL.  Use POOL for allocations. */
svn_error_t *svn_fs_base__dag_index_checksums(dag_node_t *node,
                                              trail_t *trail,
                                              apr_pool_t *pool);


/* Comparison */

/* Find out what is the same between two nodes.

   If PROPS_CHANGED is non-null, set *PROPS_CHANGED to 1 if the two
   nodes have different property lists, or to 0 if same.

   If CONTENTS_CHANGED is non-null, set *CONTENTS_CHANGED to 1 if the
   two nodes have different contents, or to 0 if same.  For files,
   file contents are compared; for directories, the entries lists are
   compared.  If one is a file and the other is a directory, the one's
   contents will be compared to the other's entries list.  (Not
   terribly useful, I suppose, but that's the caller's business.)

   ### todo: This function only compares rep keys at the moment.  This
   may leave us with a slight chance of a false positive, though I
   don't really see how that would happen in practice.  Nevertheless,
   it should probably be fixed.  */
svn_error_t *svn_fs_base__things_different(svn_boolean_t *props_changed,
                                           svn_boolean_t *contents_changed,
                                           dag_node_t *node1,
                                           dag_node_t *node2,
                                           trail_t *trail,
                                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_DAG_H */
