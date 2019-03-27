/* dag.h : DAG-like interface filesystem
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

#ifndef SVN_LIBSVN_FS_X_DAG_H
#define SVN_LIBSVN_FS_X_DAG_H

#include "svn_fs.h"
#include "svn_delta.h"
#include "private/svn_cache.h"

#include "fs.h"
#include "id.h"

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


/* Generic DAG node stuff.  */

typedef struct dag_node_t dag_node_t;

/* Fill *NODE with a dag_node_t representing node revision ID in FS,
   allocating in RESULT_POOL.  Use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_fs_x__dag_get_node(dag_node_t **node,
                       svn_fs_t *fs,
                       const svn_fs_x__id_t *id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


/* Return a new dag_node_t object referring to the same node as NODE,
   allocated in RESULT_POOL.  If you're trying to build a structure in a
   pool that wants to refer to dag nodes that may have been allocated
   elsewhere, you can call this function and avoid inter-pool pointers. */
dag_node_t *
svn_fs_x__dag_dup(const dag_node_t *node,
                  apr_pool_t *result_pool);

/* Return the filesystem containing NODE.  */
svn_fs_t *
svn_fs_x__dag_get_fs(dag_node_t *node);

/* Changes the filesystem containing NODE to FS.  (Used when pulling
   nodes out of a shared cache, say.) */
void
svn_fs_x__dag_set_fs(dag_node_t *node,
                     svn_fs_t *fs);


/* Return NODE's revision number.  If NODE has never been committed as
   part of a revision, set *REV to SVN_INVALID_REVNUM.  */
svn_revnum_t
svn_fs_x__dag_get_revision(const dag_node_t *node);


/* Return the node revision ID of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const svn_fs_x__id_t *
svn_fs_x__dag_get_id(const dag_node_t *node);

/* Return the node ID of NODE.  The value returned is shared with NODE,
   and will be deallocated when NODE is.  */
const svn_fs_x__id_t *
svn_fs_x__dag_get_node_id(dag_node_t *node);

/* Return the copy ID of NODE.  The value returned is shared with NODE,
   and will be deallocated when NODE is.  */
const svn_fs_x__id_t *
svn_fs_x__dag_get_copy_id(dag_node_t *node);

/* Return TRUE, iff nodes LHS and RHS have the same node ID. */
svn_boolean_t
svn_fs_x__dag_related_node(dag_node_t *lhs,
                           dag_node_t *rhs);

/* Return TRUE, iff nodes LHS and RHS have the same node and copy IDs.
 */
svn_boolean_t
svn_fs_x__dag_same_line_of_history(dag_node_t *lhs,
                                   dag_node_t *rhs);

/* Return the created path of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const char *
svn_fs_x__dag_get_created_path(dag_node_t *node);


/* Return the node revision ID of NODE's immediate predecessor.
 */
const svn_fs_x__id_t *
svn_fs_x__dag_get_predecessor_id(dag_node_t *node);

/* Return the number of predecessors NODE has (recursively).
 */
int
svn_fs_x__dag_get_predecessor_count(dag_node_t *node);

/* Return the number of node under NODE (inclusive) with svn:mergeinfo
   properties.
 */
apr_int64_t
svn_fs_x__dag_get_mergeinfo_count(dag_node_t *node);

/* Return TRUE, iff NODE is a directory with at least one descendant (not
   including itself) with svn:mergeinfo.
 */
svn_boolean_t
svn_fs_x__dag_has_descendants_with_mergeinfo(dag_node_t *node);

/* Return TRUE, iff NODE itself has svn:mergeinfo set on it.  */
svn_boolean_t
svn_fs_x__dag_has_mergeinfo(dag_node_t *node);

/* Return non-zero IFF NODE is currently mutable. */
svn_boolean_t
svn_fs_x__dag_check_mutable(const dag_node_t *node);

/* Return the node kind of NODE. */
svn_node_kind_t
svn_fs_x__dag_node_kind(dag_node_t *node);

/* Set *PROPLIST_P to a PROPLIST hash representing the entire property
   list of NODE, allocating from POOL.  The hash has const char *
   names (the property names) and svn_string_t * values (the property
   values).

   If properties do not exist on NODE, *PROPLIST_P will be set to
   NULL.

   Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
svn_error_t *
svn_fs_x__dag_get_proplist(apr_hash_t **proplist_p,
                           dag_node_t *node,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Set the property list of NODE to PROPLIST, allocating from POOL.
   The node being changed must be mutable.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_set_proplist(dag_node_t *node,
                           apr_hash_t *proplist,
                           apr_pool_t *scratch_pool);

/* Increment the mergeinfo_count field on NODE by INCREMENT.  The node
   being changed must be mutable.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_increment_mergeinfo_count(dag_node_t *node,
                                        apr_int64_t increment,
                                        apr_pool_t *scratch_pool);

/* Set the has-mergeinfo flag on NODE to HAS_MERGEINFO.  The node
   being changed must be mutable.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_set_has_mergeinfo(dag_node_t *node,
                                svn_boolean_t has_mergeinfo,
                                apr_pool_t *scratch_pool);



/* Revision and transaction roots.  */


/* Open the root of change set CHANGE_SET of filesystem FS, allocating from
   RESULT_POOL.  Set *NODE_P to the new node.  Use SCRATCH_POOL for
   temporary allocations.*/
svn_error_t *
svn_fs_x__dag_root(dag_node_t **node_p,
                   svn_fs_t *fs,
                   svn_fs_x__change_set_t change_set,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);


/* Directories.  */


/* Open the node named NAME in the directory PARENT.  Set *CHILD_P to
   the new node, allocated in RESULT_POOL.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If NAME does
   not exist within PARENT, set *CHILD_P to NULL.
 */
svn_error_t *
svn_fs_x__dag_open(dag_node_t **child_p,
                   dag_node_t *parent,
                   const char *name,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);


/* Set *ID_P to the noderev-id for entry NAME in PARENT.  If no such
   entry exists, set *ID_P to "unused" but do not error. */
svn_error_t *
svn_fs_x__dir_entry_id(svn_fs_x__id_t *id_p,
                       dag_node_t *parent,
                       const char *name,
                       apr_pool_t *scratch_pool);

/* Set *ENTRIES_P to an array of NODE's entries, sorted by entry names,
   and the values are svn_fs_x__dirent_t. The returned table (and elements)
   is allocated in RESULT_POOL, temporaries in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__dag_dir_entries(apr_array_header_t **entries_p,
                          dag_node_t *node,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Set ENTRY_NAME in NODE to point to ID (with kind KIND), allocating
   from POOL.  NODE must be a mutable directory.  ID can refer to a
   mutable or immutable node.  If ENTRY_NAME does not exist, it will
   be created.  TXN_ID is the Subversion transaction under which this
   occurs.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_set_entry(dag_node_t *node,
                        const char *entry_name,
                        const svn_fs_x__id_t *id,
                        svn_node_kind_t kind,
                        svn_fs_x__txn_id_t txn_id,
                        apr_pool_t *scratch_pool);


/* Make a new mutable clone of the node named NAME in PARENT, and
   adjust PARENT's directory entry to point to it, unless NAME in
   PARENT already refers to a mutable node.  In either case, set
   *CHILD_P to a reference to the new node, allocated in POOL.  PARENT
   must be mutable.  NAME must be a single path component; it cannot
   be a slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   PATH is the canonicalized absolute path at which this node is being
   created.

   TXN_ID is the Subversion transaction under which this occurs.

   Allocate *CHILD_P in RESULT_POOL and use SCRATCH_POOL for temporaries.
 */
svn_error_t *
svn_fs_x__dag_clone_child(dag_node_t **child_p,
                          dag_node_t *parent,
                          const char *parent_path,
                          const char *name,
                          const svn_fs_x__id_t *copy_id,
                          svn_fs_x__txn_id_t txn_id,
                          svn_boolean_t is_parent_copyroot,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/* Delete the directory entry named NAME from PARENT, allocating from
   POOL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If the
   node being deleted is a mutable directory, remove all mutable nodes
   reachable from it.  TXN_ID is the Subversion transaction under
   which this occurs.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_delete(dag_node_t *parent,
                     const char *name,
                     svn_fs_x__txn_id_t txn_id,
                     apr_pool_t *scratch_pool);


/* Create a new mutable directory named NAME in PARENT.  Set *CHILD_P
   to a reference to the new node, allocated in RESULT_POOL.  The new
   directory has no contents, and no properties.  PARENT must be
   mutable.  NAME must be a single path component; it cannot be a
   slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.  PARENT must
   not currently have an entry named NAME.  TXN_ID is the Subversion
   transaction under which this occurs.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_make_dir(dag_node_t **child_p,
                       dag_node_t *parent,
                       const char *parent_path,
                       const char *name,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);



/* Files.  */


/* Set *CONTENTS to a readable generic stream which yields the
   contents of FILE.  Allocate the stream in RESULT_POOL.

   If FILE is not a file, return SVN_ERR_FS_NOT_FILE.
 */
svn_error_t *
svn_fs_x__dag_get_contents(svn_stream_t **contents,
                           dag_node_t *file,
                           apr_pool_t *result_pool);

/* Attempt to fetch the contents of NODE and pass it along with the BATON
   to the PROCESSOR.   Set *SUCCESS only of the data could be provided
   and the processor had been called.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_try_process_file_contents(svn_boolean_t *success,
                                        dag_node_t *node,
                                        svn_fs_process_contents_func_t processor,
                                        void* baton,
                                        apr_pool_t *scratch_pool);


/* Set *STREAM_P to a delta stream that will turn the contents of SOURCE into
   the contents of TARGET, allocated in RESULT_POOL.  If SOURCE is null, the
   empty string will be used is its stead.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                    dag_node_t *source,
                                    dag_node_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/* Return a generic writable stream in *CONTENTS with which to set the
   contents of FILE.  Allocate the stream in RESULT_POOL.

   Any previous edits on the file will be deleted, and a new edit
   stream will be constructed.
 */
svn_error_t *
svn_fs_x__dag_get_edit_stream(svn_stream_t **contents,
                              dag_node_t *file,
                              apr_pool_t *result_pool);


/* Signify the completion of edits to FILE made using the stream
   returned by svn_fs_x__dag_get_edit_stream.

   If CHECKSUM is non-null, it must match the checksum for FILE's
   contents (note: this is not recalculated, the recorded checksum is
   used), else the error SVN_ERR_CHECKSUM_MISMATCH is returned.

   This operation is a no-op if no edits are present.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_finalize_edits(dag_node_t *file,
                             const svn_checksum_t *checksum,
                             apr_pool_t *scratch_pool);


/* Set *LENGTH to the length of the contents of FILE.
 */
svn_error_t *
svn_fs_x__dag_file_length(svn_filesize_t *length,
                          dag_node_t *file);

/* Put the recorded checksum of type KIND for FILE into CHECKSUM, allocating
   from RESULT_POOL.

   If no stored checksum is available, do not calculate the checksum,
   just put NULL into CHECKSUM.
 */
svn_error_t *
svn_fs_x__dag_file_checksum(svn_checksum_t **checksum,
                            dag_node_t *file,
                            svn_checksum_kind_t kind,
                            apr_pool_t *result_pool);

/* Create a new mutable file named NAME in PARENT.  Set *CHILD_P to a
   reference to the new node, allocated in RESULT_POOL.  The new file's
   contents are the empty string, and it has no properties.  PARENT
   must be mutable.  NAME must be a single path component; it cannot
   be a slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.  TXN_ID is the
   Subversion transaction under which this occurs.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_make_file(dag_node_t **child_p,
                        dag_node_t *parent,
                        const char *parent_path,
                        const char *name,
                        svn_fs_x__txn_id_t txn_id,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);



/* Copies */

/* Make ENTRY in TO_NODE be a copy of FROM_NODE.  TO_NODE must be mutable.
   TXN_ID is the Subversion transaction under which this occurs.

   If PRESERVE_HISTORY is true, the new node will record that it was
   copied from FROM_PATH in FROM_REV; therefore, FROM_NODE should be
   the node found at FROM_PATH in FROM_REV, although this is not
   checked.  FROM_PATH should be canonicalized before being passed
   here.

   If PRESERVE_HISTORY is false, FROM_PATH and FROM_REV are ignored.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_copy(dag_node_t *to_node,
                   const char *entry,
                   dag_node_t *from_node,
                   svn_boolean_t preserve_history,
                   svn_revnum_t from_rev,
                   const char *from_path,
                   svn_fs_x__txn_id_t txn_id,
                   apr_pool_t *scratch_pool);


/* Comparison */

/* Find out what is the same between two nodes.  If STRICT is FALSE,
   this function may report false positives, i.e. report changes even
   if the resulting contents / props are equal.

   If PROPS_CHANGED is non-null, set *PROPS_CHANGED to 1 if the two
   nodes have different property lists, or to 0 if same.

   If CONTENTS_CHANGED is non-null, set *CONTENTS_CHANGED to 1 if the
   two nodes have different contents, or to 0 if same.  NODE1 and NODE2
   must refer to files from the same filesystem.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_things_different(svn_boolean_t *props_changed,
                               svn_boolean_t *contents_changed,
                               dag_node_t *node1,
                               dag_node_t *node2,
                               svn_boolean_t strict,
                               apr_pool_t *scratch_pool);


/* Set *REV and *PATH to the copyroot revision and path of node NODE, or
   to SVN_INVALID_REVNUM and NULL if no copyroot exists.
 */
void
svn_fs_x__dag_get_copyroot(svn_revnum_t *rev,
                           const char **path,
                           dag_node_t *node);

/* Return the copyfrom revision associated with NODE.
 */
svn_revnum_t
svn_fs_x__dag_get_copyfrom_rev(dag_node_t *node);

/* Return the copyfrom path associated with NODE.
 */
const char *
svn_fs_x__dag_get_copyfrom_path(dag_node_t *node);

/* Update *TARGET so that SOURCE is it's predecessor.

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__dag_update_ancestry(dag_node_t *target,
                              dag_node_t *source,
                              apr_pool_t *scratch_pool);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_DAG_H */
