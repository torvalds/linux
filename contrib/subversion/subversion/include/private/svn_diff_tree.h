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
 * @file svn_diff_tree.h
 * @brief Generic diff handler. Replacing the old svn_wc_diff_callbacks4_t
 * infrastructure
 */

#ifndef SVN_DIFF_TREE_H
#define SVN_DIFF_TREE_H

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 *                   About the diff tree processor.
 *
 * Subversion uses two kinds of editors to describe changes. One to
 * describe changes on how to *exactly* transform one tree to another tree,
 * as efficiently as possible and one to describe the difference between trees
 * in order to review the changes, or to allow applying them on a third tree
 * which is similar to those other trees.
 *
 * The first case was originally handled by svn_delta_editor_t and might be
 * replaced by svn_editor_t in a future version. This diff processor handles
 * the other case and as such forms the layer below our diff and merge
 * handling.
 *
 * The major difference between this and the other editors is that this diff
 * always provides access to the full text and/or properties in the left and
 * right tree when applicable to allow processor implementers to decide how
 * to interpret changes.
 *
 * Originally this diff processor was not formalized explicitly, but
 * informally handled by the working copy diff callbacks. These callbacks just
 * provided the information to drive a unified diff and a textual merge. To go
 * one step further and allow full tree conflict detection we needed a better
 * defined diff handling. Instead of adding yet a few more functions and
 * arguments to the already overloaded diff callbacks the api was completely
 * redesigned with a few points in mind.
 *
 *   * It must be able to drive the old callbacks interface without users
 *     noticing the difference (100% compatible).
 *     (Implemented as svn_wc__wrap_diff_callbacks())
 *
 *   * It should provide the information that was missing in the old interface,
 *     but required to close existing issues.
 *
 *     E.g. - properties and children on deleted directories.
 *          - revision numbers and copyfrom information on directories.
 *
 * To cleanup the implementation and make it easier on diff processors to
 * handle the results I also added the following constraints.
 *
 *   * Diffs should be fully reversable: anything that is deleted should be
 *     available, just like something that is added.
 *     (Proven via svn_diff__tree_processor_reverse_create)
 *     ### Still in doubt if *_deleted() needs a copy_to argument, for the
 *     ### 99% -> 100%.
 *
 *   * Diff processors should have an easy way to communicate that they are
 *     not interrested in certain expensive to obtain results.
 *
 *   * Directories should have clear open and close events to allow adding them
 *     before their children, but still allowing property changes to have
 *     defined behavior.
 *
 *   * Files and directories should be handled as similar as possible as in
 *     many cases they are just nodes in a tree.
 *
 *   * It should be easy to create diff wrappers to apply certain transforms.
 *
 * During the creation an additional requirement of knowing about 'some
 * absent' nodes was added, to allow the merge to work on just this processor
 * api.
 *
 * The api describes a clean open-close walk through a tree, depending on the
 * driver multiple siblings can be described at the same time, but when a
 * directory is closed all descendants are done.
 *
 * Note that it is possible for nodes to be described as a delete followed by
 * an add at the same place within one parent. (Iff the diff is reversed you
 * can see an add followed by a delete!)
 *   ### "An add followed by a delete" sounds wrong.
 *
 * The directory batons live between the open and close events of a directory
 * and are thereby guaranteed to outlive the batons of their descendants.
 */

/* Describes the source of a merge */
/* ### You mean a diff?
 * ### How come many users don't set the 'repos_relpath' field? */
typedef struct svn_diff_source_t
{
  /* Always available
     In case of copyfrom: the revision copied from
   */
  svn_revnum_t revision;

  /* In case of copyfrom: the repository relative path copied from.

     NULL if the node wasn't copied or moved, or when the driver doesn't
     have this information */
  const char *repos_relpath;

  /* In case of copyfrom: the relative path of source location before the
     move. This path is relative WITHIN THE DIFF. The repository path is
     typically in repos_relpath

     NULL if the node wasn't moved or if the driver doesn't have this
     information. */
  const char *moved_from_relpath;
} svn_diff_source_t;

/**
 * A callback vtable invoked by our diff-editors, as they receive diffs
 * from the server. 'svn diff' and 'svn merge' implement their own versions
 * of this vtable.
 *
 * All callbacks receive the processor and at least a parent baton. Forwarding
 * the processor allows future extensions to call into the old functions without
 * revving the entire API.
 *
 * Users must call svn_diff__tree_processor_create() to allow adding new
 * callbacks later. (E.g. when we decide how to add move support) These
 * extensions can then just call into other callbacks.
 *
 * @since New in 1.8.
 */
typedef struct svn_diff_tree_processor_t
{
  /** The value passed to svn_diff__tree_processor_create() as BATON.
   */
  void *baton; /* To avoid an additional in some places
                * ### What? */

  /* Called before a directory's children are processed.
   *
   * Set *SKIP_CHILDREN to TRUE, to skip calling callbacks for all
   * children.
   *
   * Set *SKIP to TRUE to skip calling the added, deleted, changed
   * or closed callback for this node only.
   */
  svn_error_t *
  (*dir_opened)(void **new_dir_baton,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                const svn_diff_source_t *copyfrom_source,
                void *parent_dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);

  /* Called after a directory and all its children are added
   */
  svn_error_t *
  (*dir_added)(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool);

  /* Called after all children of this node are reported as deleted.
   *
   * The default implementation calls dir_closed().
   */
  svn_error_t *
  (*dir_deleted)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called instead of dir_closed() if the properties on the directory
   *  were modified.
   *
   * The default implementation calls dir_closed().
   */
  svn_error_t *
  (*dir_changed)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called when a directory is closed without applying changes to
   * the directory itself.
   *
   * When dir_changed or dir_deleted are handled by the default implementation
   * they call dir_closed()
   */
  svn_error_t *
  (*dir_closed)(const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                void *dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool);

  /* Called before file_added(), file_deleted(), file_changed() and
     file_closed()
   */
  svn_error_t *
  (*file_opened)(void **new_file_baton,
                 svn_boolean_t *skip,
                 const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 const svn_diff_source_t *copyfrom_source,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool);

  /* Called after file_opened() for newly added and copied files */
  svn_error_t *
  (*file_added)(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool);

  /* Called after file_opened() for deleted or moved away files */
  svn_error_t *
  (*file_deleted)(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool);

  /* Called after file_opened() for changed files */
  svn_error_t *
  (*file_changed)(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool);

  /* Called after file_opened() for unmodified files */
  svn_error_t *
  (*file_closed)(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 void *file_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);

  /* Called when encountering a marker for an absent file or directory */
  svn_error_t *
  (*node_absent)(const char *relpath,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool);
} svn_diff_tree_processor_t;

/**
 * Create a new svn_diff_tree_processor_t instance with all functions
 * set to a callback doing nothing but copying the parent baton to
 * the new baton.
 *
 * @since New in 1.8.
 */
svn_diff_tree_processor_t *
svn_diff__tree_processor_create(void *baton,
                                apr_pool_t *result_pool);

/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to call into another svn_diff_tree_processor_t processor, but with all
 * adds and deletes inverted.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_reverse_create(const svn_diff_tree_processor_t * processor,
                                        const char *prefix_relpath,
                                        apr_pool_t *result_pool);

/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to call into processor for all paths equal to and below prefix_relpath.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_filter_create(const svn_diff_tree_processor_t *processor,
                                       const char *prefix_relpath,
                                       apr_pool_t *result_pool);

/**
 * Create a new svn_diff_tree_processor_t instance with all function setup
 * to call into processor with all adds with copyfrom information transformed
 * to simple node changes.
 *
 * @since New in 1.8.
 */ /* Used by libsvn_wc diff editor */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_copy_as_changed_create(
                                const svn_diff_tree_processor_t *processor,
                                apr_pool_t *result_pool);


/**
 * Create a new svn_diff_tree_processor_t instance with all functions setup
 * to first call into processor1 and then processor2.
 *
 * This function is mostly a debug and migration helper.
 *
 * @since New in 1.8.
 */ /* Used by libsvn clients repository diff */
const svn_diff_tree_processor_t *
svn_diff__tree_processor_tee_create(const svn_diff_tree_processor_t *processor1,
                                    const svn_diff_tree_processor_t *processor2,
                                    apr_pool_t *result_pool);


svn_diff_source_t *
svn_diff__source_create(svn_revnum_t revision,
                        apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_DIFF_TREE_H */

