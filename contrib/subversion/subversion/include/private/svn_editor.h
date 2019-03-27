/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_editor.h
 * @brief Tree editing functions and structures
 */

#ifndef SVN_EDITOR_H
#define SVN_EDITOR_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Temporarily private stuff (should move to svn_delta.h when Editor
     V2 is made public) ***/

/** Callback to retrieve a node's entire set of properties.  This is
 * needed by the various editor shims in order to effect backwards
 * compatibility.
 *
 * Implementations should set @a *props to the hash of properties
 * associated with @a path in @a base_revision, allocating that hash
 * and its contents in @a result_pool, and should use @a scratch_pool
 * for temporary allocations.
 *
 * @a baton is an implementation-specific closure.
 */
typedef svn_error_t *(*svn_delta_fetch_props_func_t)(
  apr_hash_t **props,
  void *baton,
  const char *path,
  svn_revnum_t base_revision,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool
  );

/** Callback to retrieve a node's kind.  This is needed by the various
 * editor shims in order to effect backwards compatibility.
 *
 * Implementations should set @a *kind to the node kind of @a path in
 * @a base_revision, using @a scratch_pool for temporary allocations.
 *
 * @a baton is an implementation-specific closure.
 */
typedef svn_error_t *(*svn_delta_fetch_kind_func_t)(
  svn_node_kind_t *kind,
  void *baton,
  const char *path,
  svn_revnum_t base_revision,
  apr_pool_t *scratch_pool
  );

/** Callback to fetch the name of a file to use as a delta base.
 *
 * Implementations should set @a *filename to the name of a file
 * suitable for use as a delta base for @a path in @a base_revision
 * (allocating @a *filename from @a result_pool), or to @c NULL if the
 * base stream is empty.  @a scratch_pool is provided for temporary
 * allocations.
 *
 * @a baton is an implementation-specific closure.
 */
typedef svn_error_t *(*svn_delta_fetch_base_func_t)(
  const char **filename,
  void *baton,
  const char *path,
  svn_revnum_t base_revision,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool
  );

/** Collection of callbacks used for the shim code.  This structure
 * may grow additional fields in the future.  Therefore, always use
 * svn_delta_shim_callbacks_default() to allocate new instances of it.
 */
typedef struct svn_delta_shim_callbacks_t
{
  svn_delta_fetch_props_func_t fetch_props_func;
  svn_delta_fetch_kind_func_t fetch_kind_func;
  svn_delta_fetch_base_func_t fetch_base_func;
  void *fetch_baton;
} svn_delta_shim_callbacks_t;

/** Return a collection of default shim functions in @a result_pool.
 */
svn_delta_shim_callbacks_t *
svn_delta_shim_callbacks_default(apr_pool_t *result_pool);



/** Transforming trees ("editing").
 *
 * In Subversion, we have a number of occasions where we transform a tree
 * from one state into another. This process is called "editing" a tree.
 *
 * In processing a `commit' command:
 * - The client examines its working copy data to determine the set of
 *   changes necessary to transform its base tree into the desired target.
 * - The client networking library delivers that set of changes/operations
 *   across the wire as an equivalent series of network requests (for
 *   example, to svnserve as an ra_svn protocol stream, or to an
 *   Apache httpd server as WebDAV commands)
 * - The server receives those requests and applies the sequence of
 *   operations on a revision, producing a transaction representing the
 *   desired target.
 * - The Subversion server then commits the transaction to the filesystem.
 *
 * In processing an `update' command, the process is reversed:
 * - The Subversion server module talks to the filesystem and computes a
 *   set of changes necessary to bring the client's working copy up to date.
 * - The server serializes this description of changes, and delivers it to
 *   the client.
 * - The client networking library receives that reply, producing a set
 *   of changes/operations to alter the working copy into the revision
 *   requested by the update command.
 * - The working copy library applies those operations to the working copy
 *   to align it with the requested update target.
 *
 * The series of changes (or operations) necessary to transform a tree from
 * one state into another is passed between subsystems using this "editor"
 * interface. The "receiver" edits its tree according to the operations
 * described by the "driver".
 *
 * Note that the driver must have a perfect understanding of the tree which
 * the receiver will be applying edits upon. There is no room for error here,
 * and the interface embodies assumptions/requirements that the driver has
 * about the targeted tree. As a result, this interface is a standardized
 * mechanism of *describing* those change operations, but the intimate
 * knowledge between the driver and the receiver implies some level of
 * coupling between those subsystems.
 *
 * The set of changes, and the data necessary to describe it entirely, is
 * completely unbounded. An addition of one simple 20 GB file might be well
 * past the available memory of a machine processing these operations.
 * As a result, the API to describe the changes is designed to be applied
 * in a sequential (and relatively random-access) model. The operations
 * can be streamed from the driver to the receiver, resulting in the
 * receiver editing its tree to the target state defined by the driver.
 *
 *
 * <h3>History</h3>
 *
 * Classically, Subversion had a notion of a "tree delta" which could be
 * passed around as an independent entity. Theory implied this delta was an
 * entity in its own right, to be used when and where necessary.
 * Unfortunately, this theory did not work well in practice. The producer
 * and consumer of these tree deltas were (and are) tightly coupled. As noted
 * above, the tree delta producer needed to be *totally* aware of the tree
 * that it needed to edit. So rather than telling the delta consumer how to
 * edit its tree, the classic #svn_delta_editor_t interface focused
 * entirely on the tree delta, an intermediate (logical) data structure
 * which was unusable outside of the particular, coupled pairing of producer
 * and consumer. This generation of the API forgoes the logical tree delta
 * entity and directly passes the necessary edits/changes/operations from
 * the producer to the consumer. In our new parlance, one subsystem "drives"
 * a set of operations describing the change, and a "receiver" accepts and
 * applies them to its tree.
 *
 * The classic interface was named #svn_delta_editor_t and was described
 * idiomatically as the "editor interface". This generation of the interface
 * retains the "editor" name for that reason. All notions of a "tree delta"
 * structure are no longer part of this interface.
 *
 * The old interface was purely vtable-based and used a number of special
 * editors which could be interposed between the driver and receiver. Those
 * editors provided cancellation, debugging, and other various operations.
 * While the "interposition" pattern is still possible with this interface,
 * the most common functionality (cancellation and debugging) have been
 * integrated directly into this new editor system.
 *
 *
 * <h3>Implementation Plan</h3>
 * @note This section can be removed after Ev2 is fully implemented.
 *
 * The delta editor is pretty engrained throughout Subversion, so attempting
 * to replace it in situ is somewhat akin to performing open heart surgery
 * while the patient is running a marathon.  However, a viable plan should
 * make things a bit easier, and help parallelize the work.
 *
 * In short, the following items need to be done:
 *  -# Implement backward compatibility wrappers ("shims")
 *  -# Use shims to update editor consumers to Ev2
 *  -# Update editor producers to drive Ev2
 *     - This will largely involve rewriting the RA layers to accept and
 *       send Ev2 commands
 *  -# Optimize consumers and producers to leverage the features of Ev2
 *
 * The shims are largely self-contained, and as of this writing, are almost
 * complete.  They can be released without much ado.  However, they do add
 * <em>significant</em> performance regressions, which make releasing code
 * which is half-delta-editor and half-Ev2 inadvisable.  As such, the updating
 * of producers and consumers to Ev2 will probably need to wait until 1.9,
 * though it could be largely parallelized.
 *
 *
 * @defgroup svn_editor The editor interface
 * @{
 */

/** An abstract object that edits a target tree.
 *
 * @note The term "follow" means at any later time in the editor drive.
 * Terms such as "must", "must not", "required", "shall", "shall not",
 * "should", "should not", "recommended", "may", and "optional" in this
 * document are to be interpreted as described in RFC 2119.
 *
 * @note The editor objects are *not* reentrant. The receiver should not
 * directly or indirectly invoke an editor API with the same object unless
 * it has been marked as explicitly supporting reentrancy during a
 * receiver's callback. This limitation extends to the cancellation
 * callback, too. (This limitation is due to the scratch_pool shared by
 * all callbacks, and cleared after each callback; a reentrant call could
 * clear the outer call's pool). Note that the code itself is reentrant, so
 * there is no problem using the APIs on different editor objects.
 *
 * \n
 * <h3>Life-Cycle</h3>
 *
 * - @b Create: A receiver uses svn_editor_create() to create an
 *    "empty" svn_editor_t.  It cannot be used yet, since it still lacks
 *    actual callback functions.  svn_editor_create() sets the
 *    #svn_editor_t's callback baton and scratch pool that the callback
 *    functions receive, as well as a cancellation callback and baton
 *    (see "Cancellation" below).
 *
 * - @b Set callbacks: The receiver calls svn_editor_setcb_many() or a
 *    succession of the other svn_editor_setcb_*() functions to tell
 *    #svn_editor_t which functions to call when driven by the various
 *    operations.  Callback functions are implemented by the receiver and must
 *    adhere to the @c svn_editor_cb_*_t function types as expected by the
 *    svn_editor_setcb_*() functions. See: \n
 *      svn_editor_cb_many_t \n
 *      svn_editor_setcb_many() \n
 *      or \n
 *      svn_editor_setcb_add_directory() \n
 *      svn_editor_setcb_add_file() \n
 *      svn_editor_setcb_add_symlink() \n
 *      svn_editor_setcb_add_absent() \n
 *      svn_editor_setcb_alter_directory() \n
 *      svn_editor_setcb_alter_file() \n
 *      svn_editor_setcb_alter_symlink() \n
 *      svn_editor_setcb_delete() \n
 *      svn_editor_setcb_copy() \n
 *      svn_editor_setcb_move() \n
 *      svn_editor_setcb_complete() \n
 *      svn_editor_setcb_abort()
 *
 * - @b Drive: The driver is provided with the completed #svn_editor_t
 *    instance. (It is typically passed to a generic driving
 *    API, which could receive the driving editor calls over the network
 *    by providing a proxy #svn_editor_t on the remote side.)
 *    The driver invokes the #svn_editor_t instance's callback functions
 *    according to the restrictions defined below, in order to describe the
 *    entire set of operations necessary to transform the receiver's tree
 *    into the desired target. The callbacks can be invoked using the
 *    svn_editor_*() functions, i.e.: \n
 *      svn_editor_add_directory() \n
 *      svn_editor_add_file() \n
 *      svn_editor_add_symlink() \n
 *      svn_editor_add_absent() \n
 *      svn_editor_alter_directory() \n
 *      svn_editor_alter_file() \n
 *      svn_editor_alter_symlink() \n
 *      svn_editor_delete() \n
 *      svn_editor_copy() \n
 *      svn_editor_move() \n
 *    \n\n
 *    Just before each callback invocation is carried out, the @a cancel_func
 *    that was passed to svn_editor_create() is invoked to poll any
 *    external reasons to cancel the sequence of operations.  Unless it
 *    overrides the cancellation (denoted by #SVN_ERR_CANCELLED), the driver
 *    aborts the transmission by invoking the svn_editor_abort() callback.
 *    Exceptions to this are calls to svn_editor_complete() and
 *    svn_editor_abort(), which cannot be canceled externally.
 *
 * - @b Receive: While the driver invokes operations upon the editor, the
 *    receiver finds its callback functions called with the information
 *    to operate on its tree. Each actual callback function receives those
 *    arguments that the driver passed to the "driving" functions, plus these:
 *    -  @a baton: This is the @a editor_baton pointer originally passed to
 *       svn_editor_create().  It may be freely used by the callback
 *       implementation to store information across all callbacks.
 *    -  @a scratch_pool: This temporary pool is cleared directly after
 *       each callback returns.  See "Pool Usage".
 *    \n\n
 *    If the receiver encounters an error within a callback, it returns an
 *    #svn_error_t*. The driver receives this and aborts transmission.
 *
 * - @b Complete/Abort: The driver will end transmission by calling \n
 *    svn_editor_complete() if successful, or \n
 *    svn_editor_abort() if an error or cancellation occurred.
 * \n\n
 *
 * <h3>Driving Order Restrictions</h3>
 * In order to reduce complexity of callback receivers, the editor callbacks
 * must be driven in adherence to these rules:
 *
 * - If any path is added (with add_*) or deleted/moved, then
 *   an svn_editor_alter_directory() call must be made for its parent
 *   directory with the target/eventual set of children.
 *
 * - svn_editor_add_directory() -- Another svn_editor_add_*() call must
 *   follow for each child mentioned in the @a children argument of any
 *   svn_editor_add_directory() call.
 *
 * - For each node created with add_*, if its parent was created using
 *   svn_editor_add_directory(), then the new child node MUST have been
 *   mentioned in the @a children parameter of the parent's creation.
 *   This allows the parent directory to properly mark the child as
 *   "incomplete" until the child's add_* call arrives.
 *
 * - A path should never be referenced more than once by the add_*, alter_*,
 *   and delete operations (the "Once Rule"). The source path of a copy (and
 *   its children, if a directory) may be copied many times, and are
 *   otherwise subject to the Once Rule. The destination path of a copy
 *   or move may have alter_* operations applied, but not add_* or delete.
 *   If the destination path of a copy or move is a directory,
 *   then its children are subject to the Once Rule. The source path of
 *   a move (and its child paths) may be referenced in add_*, or as the
 *   destination of a copy (where these new or copied nodes are subject
 *   to the Once Rule).
 *
 * - The ancestor of an added, copied-here, moved-here, or
 *   modified node may not be deleted. The ancestor may not be moved
 *   (instead: perform the move, *then* the edits).
 *
 * - svn_editor_delete() must not be used to replace a path -- i.e.
 *   svn_editor_delete() must not be followed by an svn_editor_add_*() on
 *   the same path, nor by an svn_editor_copy() or svn_editor_move() with
 *   the same path as the copy/move target.
 *
 *   Instead of a prior delete call, the add/copy/move callbacks should be
 *   called with the @a replaces_rev argument set to the revision number of
 *   the node at this path that is being replaced.  Note that the path and
 *   revision number are the key to finding any other information about the
 *   replaced node, like node kind, etc.
 *   @todo say which function(s) to use.
 *
 * - svn_editor_delete() must not be used to move a path -- i.e.
 *   svn_editor_delete() must not delete the source path of a previous
 *   svn_editor_copy() call. Instead, svn_editor_move() must be used.
 *   Note: if the desired semantics is one (or more) copies, followed
 *   by a delete... that is fine. It is simply that svn_editor_move()
 *   should be used to describe a semantic move.
 *
 * - One of svn_editor_complete() or svn_editor_abort() must be called
 *   exactly once, which must be the final call the driver invokes.
 *   Invoking svn_editor_complete() must imply that the set of changes has
 *   been transmitted completely and without errors, and invoking
 *   svn_editor_abort() must imply that the transformation was not completed
 *   successfully.
 *
 * - If any callback invocation (besides svn_editor_complete()) returns
 *   with an error, the driver must invoke svn_editor_abort() and stop
 *   transmitting operations.
 * \n\n
 *
 * <h3>Receiving Restrictions</h3>
 *
 * All callbacks must complete their handling of a path before they return.
 * Since future callbacks will never reference this path again (due to the
 * Once Rule), the changes can and should be completed.
 *
 * This restriction is not recursive -- a directory's children may remain
 * incomplete until later callback calls are received.
 *
 * For example, an svn_editor_add_directory() call during an 'update'
 * operation will create the directory itself, including its properties,
 * and will complete any client notification for the directory itself.
 * The immediate children of the added directory, given in @a children,
 * will be recorded in the WC as 'incomplete' and will be completed in the
 * course of the same operation sequence, when the corresponding callbacks
 * for these items are invoked.
 * \n\n
 *
 * <h3>Timing and State</h3>
 * The calls made by the driver to alter the state in the receiver are
 * based on the receiver's *current* state, which includes all prior changes
 * made during the edit.
 *
 * Example: copy A to B; set-props on A; copy A to C. The props on C
 * should reflect the updated properties of A.
 *
 * Example: mv A@N to B; mv C@M to A. The second move cannot be marked as
 * a "replacing" move since it is not replacing A. The node at A was moved
 * away. The second operation is simply moving C to the now-empty path
 * known as A.
 *
 * <h3>Paths</h3>
 * Each driver/receiver implementation of this editor interface must
 * establish the expected root for all the paths sent and received via
 * the callbacks' @a relpath arguments.
 *
 * For example, during an "update", the driver is the repository, as a
 * whole. The receiver may have just a portion of that repository. Here,
 * the receiver could tell the driver which repository URL the working
 * copy refers to, and thus the driver could send @a relpath arguments
 * that are relative to the receiver's working copy.
 *
 * @note Because the source of a copy may be located *anywhere* in the
 * repository, editor drives should typically use the repository root
 * as the negotiated root. This allows the @a src_relpath argument in
 * svn_editor_copy() to specify any possible source.
 * \n\n
 *
 * <h3>Pool Usage</h3>
 * The @a result_pool passed to svn_editor_create() is used to allocate
 * the #svn_editor_t instance, and thus it must not be cleared before the
 * driver has finished driving the editor.
 *
 * The @a scratch_pool passed to each callback invocation is derived from
 * the @a result_pool that was passed to svn_editor_create(). It is
 * cleared directly after each single callback invocation.
 * To allocate memory with a longer lifetime from within a callback
 * function, you may use your own pool kept in the @a editor_baton.
 *
 * The @a scratch_pool passed to svn_editor_create() may be used to help
 * during construction of the #svn_editor_t instance, but it is assumed to
 * live only until svn_editor_create() returns.
 * \n\n
 *
 * <h3>Cancellation</h3>
 * To allow graceful interruption by external events (like a user abort),
 * svn_editor_create() can be passed an #svn_cancel_func_t that is
 * polled every time the driver invokes a callback, just before the
 * actual editor callback implementation is invoked.  If this function
 * decides to return with an error, the driver will receive this error
 * as if the callback function had returned it, i.e. as the result from
 * calling any of the driving functions (e.g. svn_editor_add_directory()).
 * As with any other error, the driver must then invoke svn_editor_abort()
 * and abort the transformation sequence. See #svn_cancel_func_t.
 *
 * The @a cancel_baton argument to svn_editor_create() is passed
 * unchanged to each poll of @a cancel_func.
 *
 * The cancellation function and baton are typically provided by the client
 * context.
 *
 *
 * @todo ### TODO anything missing?
 *
 * @since New in 1.8.
 */
typedef struct svn_editor_t svn_editor_t;

/** The kind of the checksum to be used throughout the #svn_editor_t APIs.
 *
 * @note ### This may change before Ev2 is official released, so just like
 * everything else in this file, please don't rely upon it until then.
 */
#define SVN_EDITOR_CHECKSUM_KIND svn_checksum_sha1


/** These function types define the callback functions a tree delta consumer
 * implements.
 *
 * Each of these "receiving" function types matches a "driving" function,
 * which has the same arguments with these differences:
 *
 * - These "receiving" functions have a @a baton argument, which is the
 *   @a editor_baton originally passed to svn_editor_create(), as well as
 *   a @a scratch_pool argument.
 *
 * - The "driving" functions have an #svn_editor_t* argument, in order to
 *   call the implementations of the function types defined here that are
 *   registered with the given #svn_editor_t instance.
 *
 * Note that any remaining arguments for these function types are explained
 * in the comment for the "driving" functions. Each function type links to
 * its corresponding "driver".
 *
 * @see svn_editor_t, svn_editor_cb_many_t.
 *
 * @defgroup svn_editor_callbacks Editor callback definitions
 * @{
 */

/** @see svn_editor_add_directory(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_add_directory_t)(
  void *baton,
  const char *relpath,
  const apr_array_header_t *children,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_add_file(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_add_file_t)(
  void *baton,
  const char *relpath,
  const svn_checksum_t *checksum,
  svn_stream_t *contents,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_add_symlink(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_add_symlink_t)(
  void *baton,
  const char *relpath,
  const char *target,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_add_absent(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_add_absent_t)(
  void *baton,
  const char *relpath,
  svn_node_kind_t kind,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_alter_directory(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_alter_directory_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  const apr_array_header_t *children,
  apr_hash_t *props,
  apr_pool_t *scratch_pool);

/** @see svn_editor_alter_file(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_alter_file_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  const svn_checksum_t *checksum,
  svn_stream_t *contents,
  apr_hash_t *props,
  apr_pool_t *scratch_pool);

/** @see svn_editor_alter_symlink(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_alter_symlink_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  const char *target,
  apr_hash_t *props,
  apr_pool_t *scratch_pool);

/** @see svn_editor_delete(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_delete_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  apr_pool_t *scratch_pool);

/** @see svn_editor_copy(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_copy_t)(
  void *baton,
  const char *src_relpath,
  svn_revnum_t src_revision,
  const char *dst_relpath,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_move(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_move_t)(
  void *baton,
  const char *src_relpath,
  svn_revnum_t src_revision,
  const char *dst_relpath,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

/** @see svn_editor_complete(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor_abort(), svn_editor_t.
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_editor_cb_abort_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @} */


/** These functions create an editor instance so that it can be driven.
 *
 * @defgroup svn_editor_create Editor creation
 * @{
 */

/** Allocate an #svn_editor_t instance from @a result_pool, store
 * @a editor_baton, @a cancel_func and @a cancel_baton in the new instance
 * and return it in @a editor.
 * @a scratch_pool is used for temporary allocations (if any). Note that
 * this is NOT the same @a scratch_pool that is passed to callback functions.
 * @see svn_editor_t
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_create(svn_editor_t **editor,
                  void *editor_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);


/** Return an editor's private baton.
 *
 * In some cases, the baton is required outside of the callbacks. This
 * function returns the private baton for use.
 *
 * @since New in 1.8.
 */
void *
svn_editor_get_baton(const svn_editor_t *editor);


/** Sets the #svn_editor_cb_add_directory_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_add_directory(svn_editor_t *editor,
                               svn_editor_cb_add_directory_t callback,
                               apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_add_file_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_add_file(svn_editor_t *editor,
                          svn_editor_cb_add_file_t callback,
                          apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_add_symlink_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_add_symlink(svn_editor_t *editor,
                             svn_editor_cb_add_symlink_t callback,
                             apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_add_absent_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_add_absent(svn_editor_t *editor,
                            svn_editor_cb_add_absent_t callback,
                            apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_alter_directory_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_alter_directory(svn_editor_t *editor,
                                 svn_editor_cb_alter_directory_t callback,
                                 apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_alter_file_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_alter_file(svn_editor_t *editor,
                            svn_editor_cb_alter_file_t callback,
                            apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_alter_symlink_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_alter_symlink(svn_editor_t *editor,
                               svn_editor_cb_alter_symlink_t callback,
                               apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_delete_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_delete(svn_editor_t *editor,
                        svn_editor_cb_delete_t callback,
                        apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_copy_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_copy(svn_editor_t *editor,
                      svn_editor_cb_copy_t callback,
                      apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_move_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_move(svn_editor_t *editor,
                      svn_editor_cb_move_t callback,
                      apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_complete_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_complete(svn_editor_t *editor,
                          svn_editor_cb_complete_t callback,
                          apr_pool_t *scratch_pool);

/** Sets the #svn_editor_cb_abort_t callback in @a editor
 * to @a callback.
 * @a scratch_pool is used for temporary allocations (if any).
 * @see also svn_editor_setcb_many().
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_abort(svn_editor_t *editor,
                       svn_editor_cb_abort_t callback,
                       apr_pool_t *scratch_pool);


/** Lists a complete set of editor callbacks.
 * This is a convenience structure.
 * @see svn_editor_setcb_many(), svn_editor_create(), svn_editor_t.
 * @since New in 1.8.
 */
typedef struct svn_editor_cb_many_t
{
  svn_editor_cb_add_directory_t cb_add_directory;
  svn_editor_cb_add_file_t cb_add_file;
  svn_editor_cb_add_symlink_t cb_add_symlink;
  svn_editor_cb_add_absent_t cb_add_absent;
  svn_editor_cb_alter_directory_t cb_alter_directory;
  svn_editor_cb_alter_file_t cb_alter_file;
  svn_editor_cb_alter_symlink_t cb_alter_symlink;
  svn_editor_cb_delete_t cb_delete;
  svn_editor_cb_copy_t cb_copy;
  svn_editor_cb_move_t cb_move;
  svn_editor_cb_complete_t cb_complete;
  svn_editor_cb_abort_t cb_abort;

} svn_editor_cb_many_t;

/** Sets all the callback functions in @a editor at once, according to the
 * callback functions stored in @a many.
 * @a scratch_pool is used for temporary allocations (if any).
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_setcb_many(svn_editor_t *editor,
                      const svn_editor_cb_many_t *many,
                      apr_pool_t *scratch_pool);

/** @} */


/** These functions are called by the tree delta driver to edit the target.
 *
 * @see svn_editor_t.
 *
 * @defgroup svn_editor_drive Driving the editor
 * @{
 */

/** Drive @a editor's #svn_editor_cb_add_directory_t callback.
 *
 * Create a new directory at @a relpath. The immediate parent of @a relpath
 * is expected to exist.
 *
 * For descriptions of @a props and @a replaces_rev, see
 * svn_editor_add_file().
 *
 * A complete listing of the immediate children of @a relpath that will be
 * added subsequently is given in @a children. @a children is an array of
 * const char*s, each giving the basename of an immediate child. It is an
 * error to pass NULL for @a children; use an empty array to indicate
 * the new directory will have no children.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 */
svn_error_t *
svn_editor_add_directory(svn_editor_t *editor,
                         const char *relpath,
                         const apr_array_header_t *children,
                         apr_hash_t *props,
                         svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_add_file_t callback.
 *
 * Create a new file at @a relpath. The immediate parent of @a relpath
 * is expected to exist.
 *
 * The file's contents are specified in @a contents which has a checksum
 * matching @a checksum. Both values must be non-NULL.
 *
 * Set the properties of the new file to @a props, which is an
 * apr_hash_t holding key-value pairs. Each key is a const char* of a
 * property name, each value is a const svn_string_t*. If no properties are
 * being set on the new file, @a props must be the empty hash. It is an
 * error to pass NULL for @a props.
 *
 * If this add is expected to replace a previously existing file, symlink or
 * directory at @a relpath, the revision number of the node to be replaced
 * must be given in @a replaces_rev. Otherwise, @a replaces_rev must be
 * #SVN_INVALID_REVNUM.  Note: it is not allowed to call a "delete" followed
 * by an "add" on the same path. Instead, an "add" with @a replaces_rev set
 * accordingly MUST be used.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_add_file(svn_editor_t *editor,
                    const char *relpath,
                    const svn_checksum_t *checksum,
                    svn_stream_t *contents,
                    apr_hash_t *props,
                    svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_add_symlink_t callback.
 *
 * Create a new symbolic link at @a relpath, with a link target of @a
 * target. The immediate parent of @a relpath is expected to exist.
 *
 * For descriptions of @a props and @a replaces_rev, see
 * svn_editor_add_file().
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_add_symlink(svn_editor_t *editor,
                       const char *relpath,
                       const char *target,
                       apr_hash_t *props,
                       svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_add_absent_t callback.
 *
 * Create an "absent" node of kind @a kind at @a relpath. The immediate
 * parent of @a relpath is expected to exist.
 * ### TODO @todo explain "absent".
 * ### JAF: What are the allowed values of 'kind'?
 *
 * For a description of @a replaces_rev, see svn_editor_add_file().
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_add_absent(svn_editor_t *editor,
                      const char *relpath,
                      svn_node_kind_t kind,
                      svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_alter_directory_t callback.
 *
 * Alter the properties of the directory at @a relpath.
 *
 * @a revision specifies the revision at which the receiver should
 * expect to find this node. That is, @a relpath at the start of the
 * whole edit and @a relpath at @a revision must lie within the same
 * node-rev (aka location history segment). This information may be used
 * to catch an attempt to alter and out-of-date directory. If the
 * directory does not have a corresponding revision in the repository
 * (e.g. it has not yet been committed), then @a revision should be
 * #SVN_INVALID_REVNUM.
 *
 * If any changes to the set of children will be made in the future of
 * the edit drive, then @a children MUST specify the resulting set of
 * children. See svn_editor_add_directory() for the format of @a children.
 * If not changes will be made, then NULL may be specified.
 *
 * For a description of @a props, see svn_editor_add_file(). If no changes
 * to the properties will be made (ie. only future changes to the set of
 * children), then @a props may be NULL.
 *
 * It is an error to pass NULL for both @a children and @a props.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_alter_directory(svn_editor_t *editor,
                           const char *relpath,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           apr_hash_t *props);

/** Drive @a editor's #svn_editor_cb_alter_file_t callback.
 *
 * Alter the properties and/or the contents of the file at @a relpath
 * with @a revision as its expected revision. See svn_editor_alter_directory()
 * for more information about @a revision.
 *
 * If @a props is non-NULL, then the properties will be applied.
 *
 * If @a contents is non-NULL, then the stream will be copied to
 * the file, and its checksum must match @a checksum (which must also
 * be non-NULL). If @a contents is NULL, then @a checksum must also
 * be NULL, and no change will be applied to the file's contents.
 *
 * The properties and/or the contents must be changed. It is an error to
 * pass NULL for @a props, @a checksum, and @a contents.
 *
 * For a description of @a checksum and @a contents see
 * svn_editor_add_file(). This function allows @a props to be NULL, but
 * the parameter is otherwise described by svn_editor_add_file().
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_alter_file(svn_editor_t *editor,
                      const char *relpath,
                      svn_revnum_t revision,
                      const svn_checksum_t *checksum,
                      svn_stream_t *contents,
                      apr_hash_t *props);

/** Drive @a editor's #svn_editor_cb_alter_symlink_t callback.
 *
 * Alter the properties and/or the target of the symlink at @a relpath
 * with @a revision as its expected revision. See svn_editor_alter_directory()
 * for more information about @a revision.
 *
 * If @a props is non-NULL, then the properties will be applied.
 *
 * If @a target is non-NULL, then the symlink's target will be updated.
 *
 * The properties and/or the target must be changed. It is an error to
 * pass NULL for @a props and @a target.
 *
 * This function allows @a props to be NULL, but the parameter is
 * otherwise described by svn_editor_add_file().
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_alter_symlink(svn_editor_t *editor,
                         const char *relpath,
                         svn_revnum_t revision,
                         const char *target,
                         apr_hash_t *props);

/** Drive @a editor's #svn_editor_cb_delete_t callback.
 *
 * Delete the existing node at @a relpath, expected to be identical to
 * revision @a revision of that path.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_delete(svn_editor_t *editor,
                  const char *relpath,
                  svn_revnum_t revision);

/** Drive @a editor's #svn_editor_cb_copy_t callback.
 *
 * Copy the node at @a src_relpath, expected to be identical to revision @a
 * src_revision of that path, to @a dst_relpath.
 *
 * For a description of @a replaces_rev, see svn_editor_add_file().
 *
 * @note See the general instructions on paths for this API. Since the
 * @a src_relpath argument must generally be able to reference any node
 * in the repository, the implication is that the editor's root must be
 * the repository root.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_copy(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_move_t callback.
 *
 * Move the node at @a src_relpath to @a dst_relpath.
 *
 * @a src_revision specifies the revision at which the receiver should
 * expect to find this node.  That is, @a src_relpath at the start of
 * the whole edit and @a src_relpath at @a src_revision must lie within
 * the same node-rev (aka history-segment).  This is just like the
 * revisions specified to svn_editor_delete().
 *
 * For a description of @a replaces_rev, see svn_editor_add_file().
 *
 * ### what happens if one side of this move is not "within" the receiver's
 * ### set of paths?
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_move(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev);

/** Drive @a editor's #svn_editor_cb_complete_t callback.
 *
 * Send word that the edit has been completed successfully.
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_complete(svn_editor_t *editor);

/** Drive @a editor's #svn_editor_cb_abort_t callback.
 *
 * Notify that the edit transmission was not successful.
 * ### TODO @todo Shouldn't we add a reason-for-aborting argument?
 *
 * For all restrictions on driving the editor, see #svn_editor_t.
 * @since New in 1.8.
 */
svn_error_t *
svn_editor_abort(svn_editor_t *editor);

/** @} */

/** @} */

/** A temporary API which conditionally inserts a double editor shim
 * into the chain of delta editors.  Used for testing Editor v2.
 *
 * Whether or not the shims are inserted is controlled by a compile-time
 * option in libsvn_delta/compat.c.
 *
 * @note The use of these shims and this API will likely cause all kinds
 * of performance degredation.  (Which is actually a moot point since they
 * don't even work properly yet anyway.)
 */
svn_error_t *
svn_editor__insert_shims(const svn_delta_editor_t **deditor_out,
                         void **dedit_baton_out,
                         const svn_delta_editor_t *deditor_in,
                         void *dedit_baton_in,
                         const char *repos_root,
                         const char *base_dir,
                         svn_delta_shim_callbacks_t *shim_callbacks,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR_H */
