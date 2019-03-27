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
 * @file svn_wc_private.h
 * @brief The Subversion Working Copy Library - Internal routines
 *
 * Requires:
 *            - A working copy
 *
 * Provides:
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:
 *            - Clients.
 */

#ifndef SVN_WC_PRIVATE_H
#define SVN_WC_PRIVATE_H

#include "svn_types.h"
#include "svn_wc.h"
#include "private/svn_diff_tree.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return TRUE iff CLHASH (a hash whose keys are const char *
   changelist names) is NULL or if LOCAL_ABSPATH is part of a changelist in
   CLHASH. */
svn_boolean_t
svn_wc__changelist_match(svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         const apr_hash_t *clhash,
                         apr_pool_t *scratch_pool);

/* Like svn_wc_get_update_editorX and svn_wc_get_status_editorX, but only
   allows updating a file external LOCAL_ABSPATH.

   Since this only deals with files, the WCROOT_IPROPS argument in
   svn_wc_get_update_editorX and svn_wc_get_status_editorX (hashes mapping
   const char * absolute working copy paths, which are working copy roots, to
   depth-first ordered arrays of svn_prop_inherited_item_t * structures) is
   simply IPROPS here, a depth-first ordered arrays of
   svn_prop_inherited_item_t * structs. */
svn_error_t *
svn_wc__get_file_external_editor(const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 svn_revnum_t *target_revision,
                                 svn_wc_context_t *wc_ctx,
                                 const char *local_abspath,
                                 const char *wri_abspath,
                                 const char *url,
                                 const char *repos_root_url,
                                 const char *repos_uuid,
                                 apr_array_header_t *iprops,
                                 svn_boolean_t use_commit_times,
                                 const char *diff3_cmd,
                                 const apr_array_header_t *preserved_exts,
                                 const char *record_ancestor_abspath,
                                 const char *recorded_url,
                                 const svn_opt_revision_t *recorded_peg_rev,
                                 const svn_opt_revision_t *recorded_rev,
                                 svn_wc_conflict_resolver_func2_t conflict_func,
                                 void *conflict_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 svn_wc_notify_func2_t notify_func,
                                 void *notify_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Like svn_wc_crawl_revisionsX, but only supports updating a file external
   LOCAL_ABSPATH which may or may not exist yet. */
svn_error_t *
svn_wc__crawl_file_external(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const svn_ra_reporter3_t *reporter,
                            void *report_baton,
                            svn_boolean_t restore_files,
                            svn_boolean_t use_commit_times,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            apr_pool_t *scratch_pool);

/* Check if LOCAL_ABSPATH is an external in the working copy identified
   by WRI_ABSPATH. If not return SVN_ERR_WC_PATH_NOT_FOUND.

   If it is an external return more information on this external.

   If IGNORE_ENOENT, then set *external_kind to svn_node_none, when
   LOCAL_ABSPATH is not an external instead of returning an error.

   Here is an overview of how DEFINING_REVISION and
   DEFINING_OPERATIONAL_REVISION would be set for which kinds of externals
   definitions:

     svn:externals line   DEFINING_REV.       DEFINING_OP._REV.

         ^/foo@2 bar       2                   2
     -r1 ^/foo@2 bar       1                   2
     -r1 ^/foo   bar       1                  SVN_INVALID_REVNUM
         ^/foo   bar      SVN_INVALID_REVNUM  SVN_INVALID_REVNUM
         ^/foo@HEAD bar   SVN_INVALID_REVNUM  SVN_INVALID_REVNUM
     -rHEAD ^/foo bar     -- not a valid externals definition --
*/
svn_error_t *
svn_wc__read_external_info(svn_node_kind_t *external_kind,
                           const char **defining_abspath,
                           const char **defining_url,
                           svn_revnum_t *defining_operational_revision,
                           svn_revnum_t *defining_revision,
                           svn_wc_context_t *wc_ctx,
                           const char *wri_abspath,
                           const char *local_abspath,
                           svn_boolean_t ignore_enoent,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** See svn_wc__committable_externals_below(). */
typedef struct svn_wc__committable_external_info_t {

  /* The local absolute path where the external should be checked out. */
  const char *local_abspath;

  /* The relpath part of the source URL the external should be checked out
   * from. */
  const char *repos_relpath;

  /* The root URL part of the source URL the external should be checked out
   * from. */
  const char *repos_root_url;

  /* Set to either svn_node_file or svn_node_dir. */
  svn_node_kind_t kind;

} svn_wc__committable_external_info_t;

/* Add svn_wc__committable_external_info_t* items to *EXTERNALS, describing
 * 'committable' externals checked out below LOCAL_ABSPATH. Recursively find
 * all nested externals (externals defined inside externals).
 *
 * In this context, a 'committable' external belongs to the same repository as
 * LOCAL_ABSPATH, is not revision-pegged and is currently checked out in the
 * WC. (Local modifications are not tested for.)
 *
 * *EXTERNALS must be initialized either to NULL or to a pointer created with
 * apr_array_make(..., sizeof(svn_wc__committable_external_info_t *)). If
 * *EXTERNALS is initialized to NULL, an array will be allocated from
 * RESULT_POOL as necessary. If no committable externals are found,
 * *EXTERNALS is left unchanged.
 *
 * DEPTH limits the recursion below LOCAL_ABSPATH.
 *
 * This function will not find externals defined in some parent WC above
 * LOCAL_ABSPATH's WC-root.
 *
 * ###TODO: Add a WRI_ABSPATH (wc root indicator) separate from LOCAL_ABSPATH,
 * to allow searching any wc-root for externals under LOCAL_ABSPATH, not only
 * LOCAL_ABSPATH's most immediate wc-root. */
svn_error_t *
svn_wc__committable_externals_below(apr_array_header_t **externals,
                                    svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    svn_depth_t depth,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/* Gets a mapping from const char * local abspaths of externals to the const
   char * local abspath of where they are defined for all externals defined
   at or below LOCAL_ABSPATH.

   ### Returns NULL in *EXTERNALS until we bumped to format 29.

   Allocate the result in RESULT_POOL and perform temporary allocations in
   SCRATCH_POOL. */
svn_error_t *
svn_wc__externals_defined_below(apr_hash_t **externals,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);


/* Registers a new external at LOCAL_ABSPATH in the working copy containing
   DEFINING_ABSPATH.

   The node is registered as defined on DEFINING_ABSPATH (must be an ancestor
   of LOCAL_ABSPATH) of kind KIND.

   The external is registered as from repository REPOS_ROOT_URL with uuid
   REPOS_UUID and the defining relative path REPOS_RELPATH.

   If the revision of the node is locked OPERATIONAL_REVISION and REVISION
   are the peg and normal revision; otherwise their value is
   SVN_INVALID_REVNUM.

   ### Only KIND svn_node_dir is supported.

   Perform temporary allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_wc__external_register(svn_wc_context_t *wc_ctx,
                          const char *defining_abspath,
                          const char *local_abspath,
                          svn_node_kind_t kind,
                          const char *repos_root_url,
                          const char *repos_uuid,
                          const char *repos_relpath,
                          svn_revnum_t operational_revision,
                          svn_revnum_t revision,
                          apr_pool_t *scratch_pool);

/* Remove the external at LOCAL_ABSPATH from the working copy identified by
   WRI_ABSPATH using WC_CTX.

   If DECLARATION_ONLY is TRUE, only remove the registration and leave the
   on-disk structure untouched.

   If not NULL, call CANCEL_FUNC with CANCEL_BATON to allow canceling while
   removing the working copy files.

   ### This function wraps svn_wc_remove_from_revision_control2().
 */
svn_error_t *
svn_wc__external_remove(svn_wc_context_t *wc_ctx,
                        const char *wri_abspath,
                        const char *local_abspath,
                        svn_boolean_t declaration_only,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *scratch_pool);

/* Gather all svn:externals property values from the actual properties on
   directories below LOCAL_ABSPATH as a mapping of const char *local_abspath
   to const char * values.

   Use DEPTH as how it would be used to limit the externals property results
   on update. (So any depth < infinity will only read svn:externals on
   LOCAL_ABSPATH itself)

   If DEPTHS is not NULL, set *depths to an apr_hash_t* mapping the same
   local_abspaths to the const char * ambient depth of the node.

   Allocate the result in RESULT_POOL and perform temporary allocations in
   SCRATCH_POOL. */
svn_error_t *
svn_wc__externals_gather_definitions(apr_hash_t **externals,
                                     apr_hash_t **ambient_depths,
                                     svn_wc_context_t *wc_ctx,
                                     const char *local_abspath,
                                     svn_depth_t depth,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Close the DB for LOCAL_ABSPATH.  Perform temporary allocations in
   SCRATCH_POOL.

   Wraps svn_wc__db_drop_root(). */
svn_error_t *
svn_wc__close_db(const char *external_abspath,
                 svn_wc_context_t *wc_ctx,
                 apr_pool_t *scratch_pool);

/** Set @a *tree_conflict to a newly allocated @c
 * svn_wc_conflict_description_t structure describing the tree
 * conflict state of @a victim_abspath, or to @c NULL if @a victim_abspath
 * is not in a state of tree conflict. @a wc_ctx is a working copy context
 * used to access @a victim_path.  Allocate @a *tree_conflict in @a result_pool,
 * use @a scratch_pool for temporary allocations.
 */
svn_error_t *
svn_wc__get_tree_conflict(const svn_wc_conflict_description2_t **tree_conflict,
                          svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Record the tree conflict described by @a conflict in the WC for
 * @a conflict->local_abspath.  Use @a scratch_pool for all temporary
 * allocations.
 *
 * Returns an SVN_ERR_WC_PATH_UNEXPECTED_STATUS error when
 * CONFLICT->LOCAL_ABSPATH is already tree conflicted.
 *
 * ### This function can't set moved_away, moved_here conflicts for
 *     any operation, except merges.
 */
svn_error_t *
svn_wc__add_tree_conflict(svn_wc_context_t *wc_ctx,
                          const svn_wc_conflict_description2_t *conflict,
                          apr_pool_t *scratch_pool);

/* Remove any tree conflict on victim @a victim_abspath using @a wc_ctx.
 * (If there is no such conflict recorded, do nothing and return success.)
 *
 * Do all temporary allocations in @a scratch_pool.
 */
svn_error_t *
svn_wc__del_tree_conflict(svn_wc_context_t *wc_ctx,
                          const char *victim_abspath,
                          apr_pool_t *scratch_pool);

/** Check whether LOCAL_ABSPATH has a parent directory that knows about its
 * existence. Set *IS_WCROOT to FALSE if a parent is found, and to TRUE
 * if there is no such parent.
 *
 * Like svn_wc_is_wc_root2(), but doesn't consider switched subdirs or
 * deleted entries as working copy roots.
 */
svn_error_t *
svn_wc__is_wcroot(svn_boolean_t *is_wcroot,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool);


/** Set @a *wcroot_abspath to the local abspath of the root of the
 * working copy in which @a local_abspath resides.
 */
svn_error_t *
svn_wc__get_wcroot(const char **wcroot_abspath,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

/** Set @a *dir to the abspath of the directory in which shelved patches
 * are stored, which is inside the WC's administrative directory, and ensure
 * the directory exists.
 *
 * @a local_abspath is any path in the WC, and is used to find the WC root.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_wc__get_shelves_dir(char **dir,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/**
 * The following are temporary APIs to aid in the transition from wc-1 to
 * wc-ng.  Use them for new development now, but they may be disappearing
 * before the 1.7 release.
 */

/*
 * Convert from svn_wc_conflict_description2_t to
 * svn_wc_conflict_description_t. This is needed by some backwards-compat
 * code in libsvn_client/ctx.c
 *
 * Allocate the result in RESULT_POOL.
 */
svn_wc_conflict_description_t *
svn_wc__cd2_to_cd(const svn_wc_conflict_description2_t *conflict,
                  apr_pool_t *result_pool);


/*
 * Convert from svn_wc_status3_t to svn_wc_status2_t.
 * Allocate the result in RESULT_POOL.
 *
 * Deprecated because svn_wc_status2_t is deprecated and the only
 * calls are from other deprecated functions.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc__status2_from_3(svn_wc_status2_t **status,
                       const svn_wc_status3_t *old_status,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/**
 * Set @a *children to a new array of the immediate children of the working
 * node at @a dir_abspath.  The elements of @a *children are (const char *)
 * absolute paths.
 *
 * Include children that are scheduled for deletion, but not those that
 * are excluded, server-excluded or not-present.
 *
 * Return every path that refers to a child of the working node at
 * @a dir_abspath.  Do not include a path just because it was a child of a
 * deleted directory that existed at @a dir_abspath if that directory is now
 * sheduled to be replaced by the working node at @a dir_abspath.
 *
 * Allocate @a *children in @a result_pool.  Use @a wc_ctx to access the
 * working copy, and @a scratch_pool for all temporary allocations.
 */
svn_error_t *
svn_wc__node_get_children_of_working_node(const apr_array_header_t **children,
                                          svn_wc_context_t *wc_ctx,
                                          const char *dir_abspath,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/**
 * Gets the immediate 'not-present' children of a node.
 *
 * #### Needed during 'svn cp WC URL' to handle mixed revision cases
 */
svn_error_t *
svn_wc__node_get_not_present_children(const apr_array_header_t **children,
                                      svn_wc_context_t *wc_ctx,
                                      const char *dir_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/**
 * Fetch the repository information for the working version
 * of the node at @a local_abspath into @a *revision, @a *repos_relpath,
 * @a *repos_root_url and @a *repos_uuid. Use @a wc_ctx to access the working
 * copy. Allocate results in @a result_pool.
 *
 * @a *revision will be set to SVN_INVALID_REVNUM for any shadowed node (including
 * added and deleted nodes). All other output values will be set to the current
 * values or those they would have after a commit.
 *
 * All output argument may be NULL, indicating no interest.
 */
svn_error_t *
svn_wc__node_get_repos_info(svn_revnum_t *revision,
                            const char **repos_relpath,
                            const char **repos_root_url,
                            const char **repos_uuid,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/**
 * Get the changed revision, date and author for @a local_abspath using @a
 * wc_ctx.  Allocate the return values in @a result_pool; use @a scratch_pool
 * for temporary allocations.  Any of the return pointers may be @c NULL, in
 * which case they are not set.
 *
 * If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_changed_info(svn_revnum_t *changed_rev,
                              apr_time_t *changed_date,
                              const char **changed_author,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/**
 * Set @a *url to the corresponding url for @a local_abspath, using @a wc_ctx.
 * If the node is added, return the url it will have in the repository.
 *
 * If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.
 */
svn_error_t *
svn_wc__node_get_url(const char **url,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/**
 * Retrieves the origin of the node as it is known in the repository. For
 * a copied node this retrieves where the node is copied from, for an added
 * node this returns NULL/INVALID outputs, and for any other node this
 * retrieves the repository location.
 *
 * All output arguments may be NULL.
 *
 * If @a is_copy is not NULL, sets @a *is_copy to TRUE if the origin is a copy
 * of the original node.
 *
 * If not NULL, sets @a revision, @a repos_relpath, @a repos_root_url and
 * @a repos_uuid to the original (if a copy) or their current values.
 *
 * If not NULL, set @a depth, to the recorded depth on @a local_abspath.
 *
 * If @a copy_root_abspath is not NULL, and @a *is_copy indicates that the
 * node was copied, set @a *copy_root_abspath to the local absolute path of
 * the root of the copied subtree containing the node. If the copied node is
 * a root by itself, @a *copy_root_abspath will match @a local_abspath (but
 * won't necessarily point to the same string in memory).
 *
 * If @a scan_deleted is TRUE, determine the origin of the deleted node. If
 * @a scan_deleted is FALSE, return NULL, SVN_INVALID_REVNUM or FALSE for
 * deleted nodes.
 *
 * Allocate the result in @a result_pool. Perform temporary allocations in
 * @a scratch_pool */
svn_error_t *
svn_wc__node_get_origin(svn_boolean_t *is_copy,
                        svn_revnum_t *revision,
                        const char **repos_relpath,
                        const char **repos_root_url,
                        const char **repos_uuid,
                        svn_depth_t *depth,
                        const char **copy_root_abspath,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t scan_deleted,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/**
 * Set @a *not_present to TRUE when @a local_abspath has status
 * svn_wc__db_status_not_present. Set @a *user_excluded to TRUE when
 * @a local_abspath has status svn_wc__db_status_excluded. Set
 * @a *server_excluded to TRUE when @a local_abspath has status
 * svn_wc__db_status_server_excluded. Otherwise set these values to FALSE.
 * If @a base_only is TRUE then only the base node will be examined,
 * otherwise the current base or working node will be examined.
 *
 * If a value is not interesting you can pass #NULL.
 *
 * If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 */
svn_error_t *
svn_wc__node_is_not_present(svn_boolean_t *not_present,
                            svn_boolean_t *user_excluded,
                            svn_boolean_t *server_excluded,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            svn_boolean_t base_only,
                            apr_pool_t *scratch_pool);

/**
 * Set @a *is_added to whether @a local_abspath is added, using
 * @a wc_ctx.  If @a local_abspath is not in the working copy, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND.  Use @a scratch_pool for all temporary
 * allocations.
 *
 * NOTE: "added" in this sense, means it was added, copied-here, or
 *   moved-here. This function provides NO information on whether this
 *   addition has replaced another node.
 *
 *   To be clear, this does NOT correspond to svn_wc_schedule_add.
 */
svn_error_t *
svn_wc__node_is_added(svn_boolean_t *is_added,
                      svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool);

/**
 * Set @a *has_working to whether @a local_abspath has a working node (which
 * might shadow BASE nodes)
 *
 * This is a check similar to status = added or status = deleted.
 */
svn_error_t *
svn_wc__node_has_working(svn_boolean_t *has_working,
                         svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool);


/**
 * Get the repository location of the base node at @a local_abspath.
 *
 * Set *REVISION, *REPOS_RELPATH, *REPOS_ROOT_URL *REPOS_UUID and *LOCK_TOKEN
 * to the location that this node was checked out at or last updated/switched
 * to, regardless of any uncommitted changes (delete, replace and/or copy-here/
 * move-here).
 *
 * If there is no BASE node at @a local_abspath or if @a show_hidden is FALSE,
 * no status 'normal' or 'incomplete' BASE node report
 * SVN_ERR_WC_PATH_NOT_FOUND, or if @a ignore_enoent is TRUE, @a kind
 * svn_node_unknown, @a revision SVN_INVALID_REVNUM and all other values NULL.
 *
 * All output arguments may be NULL.
 *
 * Allocate the results in @a result_pool. Perform temporary allocations in
 * @a scratch_pool.
 */
svn_error_t *
svn_wc__node_get_base(svn_node_kind_t *kind,
                      svn_revnum_t *revision,
                      const char **repos_relpath,
                      const char **repos_root_url,
                      const char **repos_uuid,
                      const char **lock_token,
                      svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      svn_boolean_t ignore_enoent,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);


/* Get the working revision of @a local_abspath using @a wc_ctx. If @a
 * local_abspath is not in the working copy, return @c
 * SVN_ERR_WC_PATH_NOT_FOUND.
 *
 * This function is meant as a temporary solution for using the old-style
 * semantics of entries. It will handle any uncommitted changes (delete,
 * replace and/or copy-here/move-here).
 *
 * For a delete the @a revision is the BASE node of the operation root, e.g
 * the path that was deleted. But if the delete is  below an add, the
 * revision is set to SVN_INVALID_REVNUM. For an add, copy or move we return
 * SVN_INVALID_REVNUM. In case of a replacement, we return the BASE
 * revision.
 *
 * The @a changed_rev is set to the latest committed change to @a
 * local_abspath before or equal to @a revision, unless the node is
 * copied-here or moved-here. Then it is the revision of the latest committed
 * change before or equal to the copyfrom_rev.  NOTE, that we use
 * SVN_INVALID_REVNUM for a scheduled copy or move.
 *
 * The @a changed_date and @a changed_author are the ones associated with @a
 * changed_rev.
 */
svn_error_t *
svn_wc__node_get_pre_ng_status_data(svn_revnum_t *revision,
                                    svn_revnum_t *changed_rev,
                                    apr_time_t *changed_date,
                                    const char **changed_author,
                                    svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/**
 * Acquire a recursive write lock for @a local_abspath.  If @a lock_anchor
 * is true, determine if @a local_abspath has an anchor that should be locked
 * instead; otherwise, @a local_abspath must be a versioned directory.
 * Store the obtained lock in @a wc_ctx.
 *
 * If @a lock_root_abspath is not NULL, store the root of the lock in
 * @a *lock_root_abspath. If @a lock_root_abspath is NULL, then @a
 * lock_anchor must be FALSE.
 *
 * Returns @c SVN_ERR_WC_LOCKED if an existing lock is encountered, in
 * which case any locks acquired will have been released.
 *
 * If @a lock_anchor is TRUE and @a lock_root_abspath is not NULL, @a
 * lock_root_abspath will be set even when SVN_ERR_WC_LOCKED is returned.
 */
svn_error_t *
svn_wc__acquire_write_lock(const char **lock_root_abspath,
                           svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           svn_boolean_t lock_anchor,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/**
 * Recursively release write locks for @a local_abspath, using @a wc_ctx
 * for working copy access.  Only locks held by @a wc_ctx are released.
 * Locks are not removed if work queue items are present.
 *
 * If @a local_abspath is not the root of an owned SVN_ERR_WC_NOT_LOCKED
 * is returned.
 */
svn_error_t *
svn_wc__release_write_lock(svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool);

/** A callback invoked by the svn_wc__call_with_write_lock() function.  */
typedef svn_error_t *(*svn_wc__with_write_lock_func_t)(void *baton,
                                                       apr_pool_t *result_pool,
                                                       apr_pool_t *scratch_pool);


/** Call function @a func while holding a write lock on
 * @a local_abspath. The @a baton, and @a result_pool and
 * @a scratch_pool, is passed @a func.
 *
 * If @a lock_anchor is TRUE, determine if @a local_abspath has an anchor
 * that should be locked instead.
 *
 * Use @a wc_ctx for working copy access.
 * The lock is guaranteed to be released after @a func returns.
 */
svn_error_t *
svn_wc__call_with_write_lock(svn_wc__with_write_lock_func_t func,
                             void *baton,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t lock_anchor,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/** Evaluate the expression @a expr while holding a write lock on
 * @a local_abspath.
 *
 * @a expr must yield an (svn_error_t *) error code.  If the error code
 * is not #SVN_NO_ERROR, cause the function using this macro to return
 * the error to its caller.
 *
 * If @a lock_anchor is TRUE, determine if @a local_abspath has an anchor
 * that should be locked instead.
 *
 * Use @a wc_ctx for working copy access.
 *
 * The lock is guaranteed to be released after evaluating @a expr.
 */
#define SVN_WC__CALL_WITH_WRITE_LOCK(expr, wc_ctx, local_abspath,             \
                                     lock_anchor, scratch_pool)               \
  do {                                                                        \
    svn_error_t *svn_wc__err1, *svn_wc__err2;                                 \
    const char *svn_wc__lock_root_abspath;                                    \
    SVN_ERR(svn_wc__acquire_write_lock(&svn_wc__lock_root_abspath, wc_ctx,    \
                                       local_abspath, lock_anchor,            \
                                       scratch_pool, scratch_pool));          \
    svn_wc__err1 = (expr);                                                    \
    svn_wc__err2 = svn_wc__release_write_lock(                                \
                     wc_ctx, svn_wc__lock_root_abspath, scratch_pool);        \
    SVN_ERR(svn_error_compose_create(svn_wc__err1, svn_wc__err2));            \
  } while (0)


/** A callback invoked by svn_wc__prop_list_recursive().
 * It is equivalent to svn_proplist_receiver_t declared in svn_client.h,
 * but kept private within the svn_wc__ namespace because it is used within
 * the bowels of libsvn_wc which don't include svn_client.h.
 *
 * @since New in 1.7. */
typedef svn_error_t *(*svn_wc__proplist_receiver_t)(void *baton,
                                                    const char *local_abspath,
                                                    apr_hash_t *props,
                                                    apr_pool_t *scratch_pool);

/** Call @a receiver_func, passing @a receiver_baton, an absolute path, and
 * a hash table mapping <tt>const char *</tt> names onto <tt>const
 * svn_string_t *</tt> values for all the regular properties of the node
 * at @a local_abspath and any node beneath @a local_abspath within the
 * specified @a depth. @a receiver_fun must not be NULL.
 *
 * If @a propname is not NULL, the passed hash table will only contain
 * the property @a propname.
 *
 * If @a pristine is not @c TRUE, and @a base_props is FALSE show local
 * modifications to the properties.
 *
 * If a node has no properties, @a receiver_func is not called for the node.
 *
 * If @a changelists are non-NULL and non-empty, filter by them.
 *
 * Use @a wc_ctx to access the working copy, and @a scratch_pool for
 * temporary allocations.
 *
 * If the node at @a local_abspath does not exist,
 * #SVN_ERR_WC_PATH_NOT_FOUND is returned.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc__prop_list_recursive(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const char *propname,
                            svn_depth_t depth,
                            svn_boolean_t pristine,
                            const apr_array_header_t *changelists,
                            svn_wc__proplist_receiver_t receiver_func,
                            void *receiver_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool);

/**
 * Set @a *inherited_props to a depth-first ordered array of
 * #svn_prop_inherited_item_t * structures representing the properties
 * inherited by @a local_abspath from the ACTUAL tree above
 * @a local_abspath (looking through to the WORKING or BASE tree as
 * required), up to and including the root of the working copy and
 * any cached inherited properties inherited by the root.
 *
 * The #svn_prop_inherited_item_t->path_or_url members of the
 * #svn_prop_inherited_item_t * structures in @a *inherited_props are
 * paths relative to the repository root URL for cached inherited
 * properties and absolute working copy paths otherwise.
 *
 * Allocate @a *inherited_props in @a result_pool.  Use @a scratch_pool
 * for temporary allocations.
 */
svn_error_t *
svn_wc__get_iprops(apr_array_header_t **inherited_props,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   const char *propname,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

/** Obtain a mapping of const char * local_abspaths to const svn_string_t*
 * property values in *VALUES, of all PROPNAME properties on LOCAL_ABSPATH
 * and its descendants.
 *
 * Allocate the result in RESULT_POOL, and perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__prop_retrieve_recursive(apr_hash_t **values,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                const char *propname,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/**
 * Set @a *iprops_paths to a hash mapping const char * absolute working
 * copy paths to the nodes repository root relative path for each path
 * in the working copy at or below @a local_abspath, limited by @a depth,
 * that has cached inherited properties for the base node of the path.
 *
 * Allocate @a *iprop_paths
 * in @a result_pool.  Use @a scratch_pool for temporary allocations.
 */
svn_error_t *
svn_wc__get_cached_iprop_children(apr_hash_t **iprop_paths,
                                  svn_depth_t depth,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);


/**
 * For use by entries.c and entries-dump.c to read old-format working copies.
 */
svn_error_t *
svn_wc__read_entries_old(apr_hash_t **entries,
                         const char *dir_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/**
 * Recursively clear the dav cache (wcprops) in @a wc_ctx for the tree
 * rooted at @a local_abspath.
 */
svn_error_t *
svn_wc__node_clear_dav_cache_recursive(svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *scratch_pool);

/**
 * Set @a lock_tokens to a hash mapping <tt>const char *</tt> URL
 * to <tt>const char *</tt> lock tokens for every path at or under
 * @a local_abspath in @a wc_ctx which has such a lock token set on it.
 * Allocate the hash and all items therein from @a result_pool.
 */
svn_error_t *
svn_wc__node_get_lock_tokens_recursive(apr_hash_t **lock_tokens,
                                       svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool);

/* Set @a *min_revision and @a *max_revision to the lowest and highest revision
 * numbers found within @a local_abspath, using context @a wc_ctx.
 * If @a committed is TRUE, set @a *min_revision and @a *max_revision
 * to the lowest and highest comitted (i.e. "last changed") revision numbers,
 * respectively. Use @a scratch_pool for temporary allocations.
 *
 * Either of MIN_REVISION and MAX_REVISION may be passed as NULL if
 * the caller doesn't care about that return value.
 *
 * This function provides a subset of the functionality of
 * svn_wc_revision_status2() and is more efficient if the caller
 * doesn't need all information returned by svn_wc_revision_status2(). */
svn_error_t *
svn_wc__min_max_revisions(svn_revnum_t *min_revision,
                          svn_revnum_t *max_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_boolean_t committed,
                          apr_pool_t *scratch_pool);

/* Indicate in @a is_switched whether any node beneath @a local_abspath
 * is switched, using context @a wc_ctx.
 * Use @a scratch_pool for temporary allocations.
 *
 * If @a trail_url is non-NULL, use it to determine if @a local_abspath itself
 * is switched.  It should be any trailing portion of @a local_abspath's
 * expected URL, long enough to include any parts that the caller considers
 * might be changed by a switch.  If it does not match the end of
 * @a local_abspath's actual URL, then report a "switched" status.
 *
 * This function provides a subset of the functionality of
 * svn_wc_revision_status2() and is more efficient if the caller
 * doesn't need all information returned by svn_wc_revision_status2(). */
svn_error_t *
svn_wc__has_switched_subtrees(svn_boolean_t *is_switched,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              const char *trail_url,
                              apr_pool_t *scratch_pool);

/* Set @a *excluded_subtrees to a hash mapping <tt>const char *</tt>
 * local * absolute paths to <tt>const char *</tt> local absolute paths for
 * every path under @a local_abspath in @a wc_ctx which are excluded
 * by the server (e.g. because of authz) or the users.
 * If no excluded paths are found then @a *server_excluded_subtrees
 * is set to @c NULL.
 * Allocate the hash and all items therein from @a result_pool.
 */
svn_error_t *
svn_wc__get_excluded_subtrees(apr_hash_t **server_excluded_subtrees,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Indicate in @a *is_modified whether the working copy has local
 * modifications, using context @a wc_ctx.
 *
 * If IGNORE_UNVERSIONED, unversioned paths inside the tree rooted by
 * LOCAL_ABSPATH are not seen as a change, otherwise they are.
 * (svn:ignored paths are always ignored)
 *
 * Use @a scratch_pool for temporary allocations. */
svn_error_t *
svn_wc__has_local_mods(svn_boolean_t *is_modified,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       svn_boolean_t ignore_unversioned,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool);

/* Renames a working copy from @a from_abspath to @a dst_abspath and makes sure
   open handles are closed to allow this on all platforms.

   Summary: This avoids a file lock problem on wc.db on Windows, that is
            triggered by libsvn_client'ss copy to working copy code. */
svn_error_t *
svn_wc__rename_wc(svn_wc_context_t *wc_ctx,
                  const char *from_abspath,
                  const char *dst_abspath,
                  apr_pool_t *scratch_pool);

/* Set *TMPDIR_ABSPATH to a directory that is suitable for temporary
   files which may need to be moved (atomically and same-device) into
   the working copy indicated by WRI_ABSPATH.  */
svn_error_t *
svn_wc__get_tmpdir(const char **tmpdir_abspath,
                   svn_wc_context_t *wc_ctx,
                   const char *wri_abspath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

/* Gets information needed by the commit harvester.
 *
 * ### Currently this API is work in progress and is designed for just this
 * ### caller. It is certainly possible (and likely) that this function and
 * ### it's caller will eventually move into a wc and maybe wc_db api.
 */
svn_error_t *
svn_wc__node_get_commit_status(svn_boolean_t *added,
                               svn_boolean_t *deleted,
                               svn_boolean_t *is_replace_root,
                               svn_boolean_t *is_op_root,
                               svn_revnum_t *revision,
                               svn_revnum_t *original_revision,
                               const char **original_repos_relpath,
                               svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Gets the md5 checksum for the pristine file identified by a sha1_checksum in the
   working copy identified by wri_abspath.

   Wraps svn_wc__db_pristine_get_md5().
 */
svn_error_t *
svn_wc__node_get_md5_from_sha1(const svn_checksum_t **md5_checksum,
                               svn_wc_context_t *wc_ctx,
                               const char *wri_abspath,
                               const svn_checksum_t *sha1_checksum,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Like svn_wc_get_pristine_contents2(), but keyed on the CHECKSUM
   rather than on the local absolute path of the working file.
   WRI_ABSPATH is any versioned path of the working copy in whose
   pristine database we'll be looking for these contents.  */
svn_error_t *
svn_wc__get_pristine_contents_by_checksum(svn_stream_t **contents,
                                          svn_wc_context_t *wc_ctx,
                                          const char *wri_abspath,
                                          const svn_checksum_t *checksum,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/* Gets an array of const char *repos_relpaths of descendants of LOCAL_ABSPATH,
 * which must be the op root of an addition, copy or move. The descendants
 * returned are at the same op_depth, but are to be deleted by the commit
 * processing because they are not present in the local copy.
 */
svn_error_t *
svn_wc__get_not_present_descendants(const apr_array_header_t **descendants,
                                    svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);


/* Checks a node LOCAL_ABSPATH in WC_CTX for several kinds of obstructions
 * for tasks like merge processing.
 *
 * If a node is not obstructed it sets *OBSTRUCTION_STATE to
 * svn_wc_notify_state_inapplicable. If a node is obstructed or when its
 * direct parent does not exist or is deleted return _state_obstructed. When
 * a node doesn't exist but should exist return svn_wc_notify_state_missing.
 *
 * A node is also obstructed if it is marked excluded or server-excluded or when
 * an unversioned file or directory exists. And if NO_WCROOT_CHECK is FALSE,
 * the root of a working copy is also obstructed; this to allow detecting
 * obstructing working copies.
 *
 * If KIND is not NULL, set *KIND to the kind of node registered in the working
 * copy, or SVN_NODE_NONE if the node doesn't
 *
 * If DELETED is not NULL, set *DELETED to TRUE if the node is marked as
 * deleted in the working copy.
 *
 * If EXCLUDED is not NULL, set *EXCLUDED to TRUE if the node is marked as
 * user or server excluded.
 *
 * If PARENT_DEPTH is not NULL, set *PARENT_DEPTH to the depth stored on the
 * parent. (Set to svn_depth_unknown if LOCAL_ABSPATH itself exists as node)
 *
 * All output arguments except OBSTRUCTION_STATE can be NULL to ommit the
 * result.
 *
 * This function performs temporary allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_wc__check_for_obstructions(svn_wc_notify_state_t *obstruction_state,
                               svn_node_kind_t *kind,
                               svn_boolean_t *deleted,
                               svn_boolean_t *excluded,
                               svn_depth_t *parent_depth,
                               svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               svn_boolean_t no_wcroot_check,
                               apr_pool_t *scratch_pool);


/**
 * A structure which describes various system-generated metadata about
 * a working-copy path or URL.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, users shouldn't allocate structures of this
 * type, to preserve binary compatibility.
 *
 * @since New in 1.7.
 */
typedef struct svn_wc__info2_t
{
  /** Where the item lives in the repository. */
  const char *URL;

  /** The root URL of the repository. */
  const char *repos_root_URL;

  /** The repository's UUID. */
  const char *repos_UUID;

  /** The revision of the object.  If the target is a working-copy
   * path, then this is its current working revision number.  If the target
   * is a URL, then this is the repository revision that it lives in. */
  svn_revnum_t rev;

  /** The node's kind. */
  svn_node_kind_t kind;

  /** The size of the file in the repository (untranslated,
   * e.g. without adjustment of line endings and keyword
   * expansion). Only applicable for file -- not directory -- URLs.
   * For working copy paths, @a size will be #SVN_INVALID_FILESIZE. */
  svn_filesize_t size;

  /** The last revision in which this object changed. */
  svn_revnum_t last_changed_rev;

  /** The date of the last_changed_rev. */
  apr_time_t last_changed_date;

  /** The author of the last_changed_rev. */
  const char *last_changed_author;

  /** An exclusive lock, if present.  Could be either local or remote. */
  svn_lock_t *lock;

  /* Possible information about the working copy, NULL if not valid. */
  struct svn_wc_info_t *wc_info;

} svn_wc__info2_t;

/** The callback invoked by info retrievers.  Each invocation
 * describes @a local_abspath with the information present in @a info.
 * Use @a scratch_pool for all temporary allocation.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_wc__info_receiver2_t)(void *baton,
                                                 const char *local_abspath,
                                                 const svn_wc__info2_t *info,
                                                 apr_pool_t *scratch_pool);

/* Walk the children of LOCAL_ABSPATH and push svn_wc__info2_t's through
   RECEIVER/RECEIVER_BATON.  Honor DEPTH while crawling children, and
   filter the pushed items against CHANGELISTS.

   If FETCH_EXCLUDED is TRUE, also fetch excluded nodes.
   If FETCH_ACTUAL_ONLY is TRUE, also fetch actual-only nodes. */
svn_error_t *
svn_wc__get_info(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 svn_depth_t depth,
                 svn_boolean_t fetch_excluded,
                 svn_boolean_t fetch_actual_only,
                 const apr_array_header_t *changelists,
                 svn_wc__info_receiver2_t receiver,
                 void *receiver_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool);

/* Alternative version of svn_wc_delete4().
 * It can delete multiple TARGETS more efficiently (within a single sqlite
 * transaction per working copy), but lacks support for moves.
 *
 * ### Inconsistency: if DELETE_UNVERSIONED_TARGET is FALSE and a target is
 *     unversioned, svn_wc__delete_many() will continue whereas
 *     svn_wc_delete4() will throw an error.
 */
svn_error_t *
svn_wc__delete_many(svn_wc_context_t *wc_ctx,
                    const apr_array_header_t *targets,
                    svn_boolean_t keep_local,
                    svn_boolean_t delete_unversioned_target,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    svn_wc_notify_func2_t notify_func,
                    void *notify_baton,
                    apr_pool_t *scratch_pool);


/* If the node at LOCAL_ABSPATH was moved away set *MOVED_TO_ABSPATH to
 * the absolute path of the copied move-target node, and *COPY_OP_ROOT_ABSPATH
 * to the absolute path of the root node of the copy operation.
 *
 * If the node was not moved, set *MOVED_TO_ABSPATH and *COPY_OP_ROOT_ABSPATH
 * to NULL.
 *
 * Either MOVED_TO_ABSPATH or OP_ROOT_ABSPATH may be NULL to indicate
 * that the caller is not interested in the result.
 */
svn_error_t *
svn_wc__node_was_moved_away(const char **moved_to_abspath,
                            const char **copy_op_root_abspath,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* If the node at LOCAL_ABSPATH was moved here set *MOVED_FROM_ABSPATH to
 * the absolute path of the deleted move-source node, and set
 * *DELETE_OP_ROOT_ABSPATH to the absolute path of the root node of the
 * delete operation.
 *
 * If the node was not moved, set *MOVED_FROM_ABSPATH and
 * *DELETE_OP_ROOT_ABSPATH to NULL.
 *
 * Either MOVED_FROM_ABSPATH or OP_ROOT_ABSPATH may be NULL to indicate
 * that the caller is not interested in the result.
 */
svn_error_t *
svn_wc__node_was_moved_here(const char **moved_from_abspath,
                            const char **delete_op_root_abspath,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* During an upgrade to wc-ng, supply known details about an existing
 * external.  The working copy will suck in and store the information supplied
 * about the existing external at @a local_abspath. */
svn_error_t *
svn_wc__upgrade_add_external_info(svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  svn_node_kind_t kind,
                                  const char *def_local_abspath,
                                  const char *repos_relpath,
                                  const char *repos_root_url,
                                  const char *repos_uuid,
                                  svn_revnum_t def_peg_revision,
                                  svn_revnum_t def_revision,
                                  apr_pool_t *scratch_pool);

/* If the URL for @a item is relative, then using the repository root
   URL @a repos_root_url and the parent directory URL @parent_dir_url,
   resolve it into an absolute URL and save it in @a *resolved_url.

   Regardless if the URL is absolute or not, if there are no errors,
   the URL returned in @a *resolved_url will be canonicalized.

   The following relative URL formats are supported:

     ../    relative to the parent directory of the external
     ^/     relative to the repository root
     //     relative to the scheme
     /      relative to the server's hostname

   The ../ and ^/ relative URLs may use .. to remove path elements up
   to the server root.

   The external URL should not be canonicalized before calling this function,
   as otherwise the scheme relative URL '//host/some/path' would have been
   canonicalized to '/host/some/path' and we would not be able to match on
   the leading '//'. */
svn_error_t *
svn_wc__resolve_relative_external_url(const char **resolved_url,
                                      const svn_wc_external_item2_t *item,
                                      const char *repos_root_url,
                                      const char *parent_dir_url,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

typedef enum svn_wc__external_description_format_t
{
  /* LOCALPATH [-r PEG] URL */
  svn_wc__external_description_format_1 = 0,

  /* [-r REV] URL[@PEG] LOCALPATH, introduced in Subversion 1.5 */
  svn_wc__external_description_format_2
} svn_wc__external_description_format_t;

/* Additional information about what the external's parser has parsed. */
typedef struct svn_wc__externals_parser_info_t
{
  /* The syntax format used by the external description. */
  svn_wc__external_description_format_t format;

  /* The string used for defining the operative revision, i.e.
     "-rN", "-rHEAD", or "-r{DATE}".
     NULL if revision was not given. */
  const char *rev_str;

  /* The string used for defining the peg revision (equals rev_str in
     format 1, is "@N", or "@HEAD" or "@{DATE}" in format 2).
     NULL if peg revision was not given. */
  const char *peg_rev_str;

} svn_wc__externals_parser_info_t;

/* Like svn_wc_parse_externals_description3() but returns an additional array
 * with elements of type svn_wc__externals_parser_info_t in @a *parser_infos_p.
 * @a parser_infos_p may be NULL if not required by the caller.
 */
svn_error_t *
svn_wc__parse_externals_description(apr_array_header_t **externals_p,
                                    apr_array_header_t **parser_infos_p,
                                    const char *defining_directory,
                                    const char *desc,
                                    svn_boolean_t canonicalize_url,
                                    apr_pool_t *pool);

/**
 * Set @a *editor and @a *edit_baton to an editor that generates
 * #svn_wc_status3_t structures and sends them through @a status_func /
 * @a status_baton.  @a anchor_abspath is a working copy directory
 * directory which will be used as the root of our editor.  If @a
 * target_basename is not "", it represents a node in the @a anchor_abspath
 * which is the subject of the editor drive (otherwise, the @a
 * anchor_abspath is the subject).
 *
 * If @a set_locks_baton is non-@c NULL, it will be set to a baton that can
 * be used in a call to the svn_wc_status_set_repos_locks() function.
 *
 * Callers drive this editor to describe working copy out-of-dateness
 * with respect to the repository.  If this information is not
 * available or not desired, callers should simply call the
 * close_edit() function of the @a editor vtable.
 *
 * If the editor driver calls @a editor's set_target_revision() vtable
 * function, then when the edit drive is completed, @a *edit_revision
 * will contain the revision delivered via that interface.
 *
 * Assuming the target is a directory, then:
 *
 *   - If @a get_all is @c FALSE, then only locally-modified entries will be
 *     returned.  If @c TRUE, then all entries will be returned.
 *
 *   - If @a depth is #svn_depth_empty, a status structure will
 *     be returned for the target only; if #svn_depth_files, for the
 *     target and its immediate file children; if
 *     #svn_depth_immediates, for the target and its immediate
 *     children; if #svn_depth_infinity, for the target and
 *     everything underneath it, fully recursively.
 *
 *     If @a depth is #svn_depth_unknown, take depths from the
 *     working copy and behave as above in each directory's case.
 *
 *     If the given @a depth is incompatible with the depth found in a
 *     working copy directory, the found depth always governs.
 *
 * If @a check_working_copy is not set, do not scan the working copy
 * for local modifications, taking only the BASE tree into account.
 *
 * If @a no_ignore is set, statuses that would typically be ignored
 * will instead be reported.
 *
 * @a ignore_patterns is an array of file patterns matching
 * unversioned files to ignore for the purposes of status reporting,
 * or @c NULL if the default set of ignorable file patterns should be used.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton while building
 * the @a statushash to determine if the client has canceled the operation.
 *
 * If @a depth_as_sticky is set handle @a depth like when depth_is_sticky is
 * passed for updating. This will show excluded nodes show up as added in the
 * repository.
 *
 * If @a server_performs_filtering is TRUE, assume that the server handles
 * the ambient depth filtering, so this doesn't have to be handled in the
 * editor.
 *
 * Allocate the editor itself in @a result_pool, and use @a scratch_pool
 * for temporary allocations. The editor will do its temporary allocations
 * in a subpool of @a result_pool.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__get_status_editor(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t check_working_copy,
                          svn_boolean_t no_ignore,
                          svn_boolean_t depth_as_sticky,
                          svn_boolean_t server_performs_filtering,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func4_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/**
 * Set @a *editor and @a *edit_baton to an editor and baton for updating a
 * working copy.
 *
 * @a anchor_abspath is a local working copy directory, with a fully recursive
 * write lock in @a wc_ctx, which will be used as the root of our editor.
 *
 * @a target_basename is the entry in @a anchor_abspath that will actually be
 * updated, or the empty string if all of @a anchor_abspath should be updated.
 *
 * The editor invokes @a notify_func with @a notify_baton as the update
 * progresses, if @a notify_func is non-NULL.
 *
 * If @a cancel_func is non-NULL, the editor will invoke @a cancel_func with
 * @a cancel_baton as the update progresses to see if it should continue.
 *
 * If @a conflict_func is non-NULL, then invoke it with @a
 * conflict_baton whenever a conflict is encountered, giving the
 * callback a chance to resolve the conflict before the editor takes
 * more drastic measures (such as marking a file conflicted, or
 * bailing out of the update).
 *
 * If @a external_func is non-NULL, then invoke it with @a external_baton
 * whenever external changes are encountered, giving the callback a chance
 * to store the external information for processing.
 *
 * If @a diff3_cmd is non-NULL, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.
 *
 * @a preserved_exts is an array of filename patterns which, when
 * matched against the extensions of versioned files, determine for
 * which such files any related generated conflict files will preserve
 * the original file's extension as their own.  If a file's extension
 * does not match any of the patterns in @a preserved_exts (which is
 * certainly the case if @a preserved_exts is @c NULL or empty),
 * generated conflict files will carry Subversion's custom extensions.
 *
 * @a target_revision is a pointer to a revision location which, after
 * successful completion of the drive of this editor, will be
 * populated with the revision to which the working copy was updated.
 *
 * @a wcroot_iprops is a hash mapping const char * absolute working copy
 * paths which are working copy roots (at or under the target within the
 * constraints dictated by @a depth) to depth-first ordered arrays of
 * svn_prop_inherited_item_t * structures which represent the inherited
 * properties for the base of those paths at @a target_revision.  After a
 * successful drive of this editor, the base nodes for these paths will
 * have their inherited properties cache updated with the values from
 * @a wcroot_iprops.
 *
 * If @a use_commit_times is TRUE, then all edited/added files will
 * have their working timestamp set to the last-committed-time.  If
 * FALSE, the working files will be touched with the 'now' time.
 *
 * If @a allow_unver_obstructions is TRUE, then allow unversioned
 * obstructions when adding a path.
 *
 * If @a adds_as_modification is TRUE, a local addition at the same path
 * as an incoming addition of the same node kind results in a normal node
 * with a possible local modification, instead of a tree conflict.
 *
 * If @a depth is #svn_depth_infinity, update fully recursively.
 * Else if it is #svn_depth_immediates, update the uppermost
 * directory, its file entries, and the presence or absence of
 * subdirectories (but do not descend into the subdirectories).
 * Else if it is #svn_depth_files, update the uppermost directory
 * and its immediate file entries, but not subdirectories.
 * Else if it is #svn_depth_empty, update exactly the uppermost
 * target, and don't touch its entries.
 *
 * If @a depth_is_sticky is set and @a depth is not
 * #svn_depth_unknown, then in addition to updating PATHS, also set
 * their sticky ambient depth value to @a depth.
 *
 * If @a server_performs_filtering is TRUE, assume that the server handles
 * the ambient depth filtering, so this doesn't have to be handled in the
 * editor.
 *
 * If @a clean_checkout is TRUE, assume that we are checking out into an
 * empty directory, and so bypass a number of conflict checks that are
 * unnecessary in this case.
 *
 * If @a fetch_dirents_func is not NULL, the update editor may call this
 * callback, when asked to perform a depth restricted update. It will do this
 * before returning the editor to allow using the primary ra session for this.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__get_update_editor(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          apr_hash_t *wcroot_iprops,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_boolean_t adds_as_modification,
                          svn_boolean_t server_performs_filtering,
                          svn_boolean_t clean_checkout,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          svn_wc_dirents_func_t fetch_dirents_func,
                          void *fetch_dirents_baton,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/**
 * A variant of svn_wc__get_update_editor().
 *
 * Set @a *editor and @a *edit_baton to an editor and baton for "switching"
 * a working copy to a new @a switch_url.  (Right now, this URL must be
 * within the same repository that the working copy already comes
 * from.)  @a switch_url must not be @c NULL.
 *
 * All other parameters behave as for svn_wc__get_update_editor().
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__get_switch_editor(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          const char *switch_url,
                          apr_hash_t *wcroot_iprops,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_boolean_t server_performs_filtering,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          svn_wc_dirents_func_t fetch_dirents_func,
                          void *fetch_dirents_baton,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);



/**
 * Return an @a editor/@a edit_baton for diffing a working copy against the
 * repository. The editor is allocated in @a result_pool; temporary
 * calculations are performed in @a scratch_pool.
 *
 * This editor supports diffing either the actual files and properties in the
 * working copy (when @a use_text_base is #FALSE), or the current pristine
 * information (when @a use_text_base is #TRUE) against the editor driver.
 *
 * @a anchor_abspath/@a target represent the base of the hierarchy to be
 * compared. The diff callback paths will be relative to this path.
 *
 * Diffs will be reported as valid relpaths, with @a anchor_abspath being
 * the root ("").
 *
 * @a diff_processor will retrieve the diff report.
 *
 * If @a depth is #svn_depth_empty, just diff exactly @a target or
 * @a anchor_path if @a target is empty.  If #svn_depth_files then do the same
 * and for top-level file entries as well (if any).  If
 * #svn_depth_immediates, do the same as #svn_depth_files but also diff
 * top-level subdirectories at #svn_depth_empty.  If #svn_depth_infinity,
 * then diff fully recursively. If @a depth is #svn_depth_unknown, then...
 *
 *   ### ... then the @a server_performs_filtering option is meaningful.
 *   ### But what does this depth mean exactly? Something about 'ambient'
 *   ### depth? How does it compare with depth 'infinity'?
 *
 * @a ignore_ancestry determines whether paths that have discontinuous node
 * ancestry are treated as delete/add or as simple modifications.  If
 * @a ignore_ancestry is @c FALSE, then any discontinuous node ancestry will
 * result in the diff given as a full delete followed by an add.
 *
 * @a show_copies_as_adds determines whether paths added with history will
 * appear as a diff against their copy source, or whether such paths will
 * appear as if they were newly added in their entirety.
 *
 * If @a use_git_diff_format is TRUE, copied paths will be treated as added
 * if they weren't modified after being copied. This allows the callbacks
 * to generate appropriate --git diff headers for such files.
 *
 * Normally, the difference from repository->working_copy is shown. If
 * @a reverse_order is TRUE, then we want to show working_copy->repository
 * diffs. Most of the reversal is done by the caller; here we just swap the
 * order of reporting a replacement so that the local addition is reported
 * before the remote delete. (The caller's diff processor can then transform
 * adds into deletes and deletes into adds, but it can't reorder the output.)
 *
 * If @a cancel_func is non-NULL, it will be used along with @a cancel_baton
 * to periodically check if the client has canceled the operation.
 *
 * @a changelist_filter is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose differences are
 * reported; that is, don't generate diffs about any item unless
 * it's a member of one of those changelists.  If @a changelist_filter is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * If @a server_performs_filtering is TRUE, assume that the server handles
 * the ambient depth filtering, so this doesn't have to be handled in the
 * editor.
 *
 *
 * A diagram illustrating how this function is used.
 *
 *   Steps 1 and 2 create the chain; step 3 drives it.
 *
 *   1.                    svn_wc__get_diff_editor(diff_cbs)
 *                                       |           ^
 *   2.         svn_ra_do_diff3(editor)  |           |
 *                    |           ^      |           |
 *                    v           |      v           |
 *           +----------+       +----------+       +----------+
 *           |          |       |          |       |          |
 *      +--> | reporter | ----> |  editor  | ----> | diff_cbs | ----> text
 *      |    |          |       |          |       |          |       out
 *      |    +----------+       +----------+       +----------+
 *      |
 *   3. svn_wc_crawl_revisions5(WC,reporter)
 *
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__get_diff_editor(const svn_delta_editor_t **editor,
                        void **edit_baton,
                        svn_wc_context_t *wc_ctx,
                        const char *anchor_abspath,
                        const char *target,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_boolean_t server_performs_filtering,
                        const apr_array_header_t *changelist_filter,
                        const svn_diff_tree_processor_t *diff_processor,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/** Callback for the svn_diff_tree_processor_t wrapper, to allow handling
 *  notifications like how the repos diff in libsvn_client does.
 *
 * Probably only necessary while transitioning to svn_diff_tree_processor_t
 */
typedef svn_error_t *
        (*svn_wc__diff_state_handle_t)(svn_boolean_t tree_conflicted,
                                       svn_wc_notify_state_t *state,
                                       svn_wc_notify_state_t *prop_state,
                                       const char *relpath,
                                       svn_node_kind_t kind,
                                       svn_boolean_t before_op,
                                       svn_boolean_t for_add,
                                       svn_boolean_t for_delete,
                                       void *state_baton,
                                       apr_pool_t *scratch_pool);

/** Callback for the svn_diff_tree_processor_t wrapper, to allow handling
 *  notifications like how the repos diff in libsvn_client does.
 *
 * Probably only necessary while transitioning to svn_diff_tree_processor_t
 */
typedef svn_error_t *
        (*svn_wc__diff_state_close_t)(const char *relpath,
                                      svn_node_kind_t kind,
                                      void *state_baton,
                                      apr_pool_t *scratch_pool);

/** Callback for the svn_diff_tree_processor_t wrapper, to allow handling
 *  absent nodes.
 *
 * Probably only necessary while transitioning to svn_diff_tree_processor_t
 */
typedef svn_error_t *
        (*svn_wc__diff_state_absent_t)(const char *relpath,
                                       void *state_baton,
                                       apr_pool_t *scratch_pool);

/** Obtains a diff processor that will drive the diff callbacks when it
 * is invoked.
 */
svn_error_t *
svn_wc__wrap_diff_callbacks(const svn_diff_tree_processor_t **diff_processor,
                            const svn_wc_diff_callbacks4_t *callbacks,
                            void *callback_baton,
                            svn_boolean_t walk_deleted_dirs,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


/**
 * Assuming @a local_abspath itself or any of its children are under version
 * control or a tree conflict victim and in a state of conflict, take these
 * nodes out of this state.
 *
 * If @a resolve_text is TRUE then any text conflict is resolved,
 * if @a resolve_tree is TRUE then any tree conflicts are resolved.
 * If @a resolve_prop is set to "" all property conflicts are resolved,
 * if it is set to any other string value, conflicts on that specific
 * property are resolved and when resolve_prop is NULL, no property
 * conflicts are resolved.
 *
 * If @a depth is #svn_depth_empty, act only on @a local_abspath; if
 * #svn_depth_files, resolve @a local_abspath and its conflicted file
 * children (if any); if #svn_depth_immediates, resolve @a local_abspath
 * and all its immediate conflicted children (both files and directories,
 * if any); if #svn_depth_infinity, resolve @a local_abspath and every
 * conflicted file or directory anywhere beneath it.
 *
 * If @a conflict_choice is #svn_wc_conflict_choose_base, resolve the
 * conflict with the old file contents; if
 * #svn_wc_conflict_choose_mine_full, use the original working contents;
 * if #svn_wc_conflict_choose_theirs_full, the new contents; and if
 * #svn_wc_conflict_choose_merged, don't change the contents at all,
 * just remove the conflict status, which is the pre-1.5 behavior.
 *
 * If @a conflict_choice is #svn_wc_conflict_choose_unspecified, invoke the
 * @a conflict_func with the @a conflict_baton argument to obtain a
 * resolution decision for each conflict.
 *
 * #svn_wc_conflict_choose_theirs_conflict and
 * #svn_wc_conflict_choose_mine_conflict are not legal for binary
 * files or properties.
 *
 * @a wc_ctx is a working copy context, with a write lock, for @a
 * local_abspath.
 *
 * The implementation details are opaque, as our "conflicted" criteria
 * might change over time.  (At the moment, this routine removes the
 * three fulltext 'backup' files and any .prej file created in a conflict,
 * and modifies @a local_abspath's entry.)
 *
 * If @a local_abspath is not under version control and not a tree
 * conflict, return #SVN_ERR_ENTRY_NOT_FOUND. If @a path isn't in a
 * state of conflict to begin with, do nothing, and return #SVN_NO_ERROR.
 *
 * If @c local_abspath was successfully taken out of a state of conflict,
 * report this information to @c notify_func (if non-@c NULL.)  If only
 * text, only property, or only tree conflict resolution was requested,
 * and it was successful, then success gets reported.
 *
 * Temporary allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__resolve_conflicts(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool);

/** 
 * Resolve the text conflict at LOCAL_ABSPATH as per CHOICE, and then
 * mark the conflict resolved.
 * The working copy must already be locked for resolving, e.g. by calling
 * svn_wc__acquire_write_lock_for_resolve() first.
 * @since New in 1.10.
 */
svn_error_t *
svn_wc__conflict_text_mark_resolved(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    svn_wc_conflict_choice_t choice,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    apr_pool_t *scratch_pool);

/** 
 * Resolve the conflicted property PROPNAME at LOCAL_ABSPATH as per CHOICE,
 * and then mark the conflict resolved.  If MERGED_VALUE is not NULL, this is
 * the new merged property, used when choosing #svn_wc_conflict_choose_merged.
 *
 * The working copy must already be locked for resolving, e.g. by calling
 * svn_wc__acquire_write_lock_for_resolve() first.
 * @since New in 1.10.
 */
svn_error_t *
svn_wc__conflict_prop_mark_resolved(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    const char *propname,
                                    svn_wc_conflict_choice_t choice,
                                    const svn_string_t *merged_value,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    apr_pool_t *scratch_pool);

/* Resolve a tree conflict where the victim at LOCAL_ABSPATH is a directory
 * which was locally deleted, replaced or moved away, and which received an
 * arbitrary incoming change during an update or switch operation.
 *
 * The conflict is resolved by accepting the current working copy state and
 * breaking the 'moved-here' link for any files or directories which were
 * moved out of the victim directory before the update operation.
 * As a result, any such files or directories become copies (rather than moves)
 * of content which the victim directory contained before it was updated.
 *
 * The tree conflict at LOCAL_ABSPATH must have the following properties or
 * SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE will be returned:
 * 
 * operation: svn_wc_operation_update or svn_wc_operation_switch
 * local change: svn_wc_conflict_reason_deleted or
 *               svn_wc_conflict_reason_replaced or
 *               svn_wc_conflict_reason_moved_away
 * incoming change: any
 *
 * The working copy must already be locked for resolving, e.g. by calling
 * svn_wc__acquire_write_lock_for_resolve() first.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_wc__conflict_tree_update_break_moved_away(svn_wc_context_t *wc_ctx,
                                              const char *local_abspath,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              svn_wc_notify_func2_t notify_func,
                                              void *notify_baton,
                                              apr_pool_t *scratch_pool);


/* Resolve a tree conflict where the victim at LOCAL_ABSPATH is a directory
 * which was locally deleted or replaced, and which received an edit (some
 * change inside the directory, or a change to the direcotory's properties)
 * during an update or switch operation.
 *
 * The conflict is resolved by keeping the victim deleted, and propagating
 * its tree conflict to any children which were moved out of the directory
 * before the update operation.
 * As a result, any such files or directories become victims of the tree
 * conflict as well and must be resolved independently.
 * Additionally, LOCAL_ABSPATH itself may become the victim of a different
 * tree conflict as a result of resolving the existing tree conflict.
 *
 * The tree conflict at LOCAL_ABSPATH must have the following properties or
 * SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE will be returned:
 * 
 * operation: svn_wc_operation_update or svn_wc_operation_switch
 * local change: svn_wc_conflict_reason_deleted or
 *               svn_wc_conflict_reason_replaced
 * incoming change: svn_wc_conflict_action_edit
 *
 * If this conflict cannot be resolved because the conflict cannot be
 * propagated to moved-away children, this function returns
 * SVN_ERR_WC_OBSTRUCTED_UPDATE or SVN_ERR_WC_FOUND_CONFLICT.
 * The caller should continue by resolving other conflicts and attempt to
 * resolve this conflict again later.
 *
 * The working copy must already be locked for resolving, e.g. by calling
 * svn_wc__acquire_write_lock_for_resolve() first.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_wc__conflict_tree_update_raise_moved_away(svn_wc_context_t *wc_ctx,
                                              const char *local_abspath,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              svn_wc_notify_func2_t notify_func,
                                              void *notify_baton,
                                              apr_pool_t *scratch_pool);

/* Resolve a tree conflict where the victim at LOCAL_ABSPATH is a file or
 * directory which was locally moved away, and which received an edit (some
 * change inside the directory or file, or a change to properties) during an
 * update or switch operation.
 *
 * The conflict is resolved by keeping the victim moved-away, and propagating
 * the incoming edits to the victim's moved-to location.
 *
 * The tree conflict at LOCAL_ABSPATH must have the following properties or
 * SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE will be returned:
 * 
 * operation: svn_wc_operation_update or svn_wc_operation_switch
 * local change: svn_wc_conflict_reason_moved_away
 * incoming change: svn_wc_conflict_action_edit
 *
 * If this conflict cannot be resolved this function returns
 * SVN_ERR_WC_OBSTRUCTED_UPDATE or SVN_ERR_WC_FOUND_CONFLICT.
 * The caller should continue by resolving other conflicts and attempt to
 * resolve this conflict again later.
 *
 * The working copy must already be locked for resolving, e.g. by calling
 * svn_wc__acquire_write_lock_for_resolve() first.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_wc__conflict_tree_update_moved_away_node(svn_wc_context_t *wc_ctx,
                                             const char *local_abspath,
                                             svn_cancel_func_t cancel_func,
                                             void *cancel_baton,
                                             svn_wc_notify_func2_t notify_func,
                                             void *notify_baton,
                                             apr_pool_t *scratch_pool);

/* Merge local changes from a tree conflict victim of an incoming deletion
 * to the specified DEST_ABSPATH added during an update. Both LOCAL_ABSPATH
 * and DEST_ABSPATH must be directories.
 *
 * Assuming DEST_ABSPATH is the correct move destination, this function
 * allows local changes to "follow" incoming moves during updates.
 *
 * @since New in 1.10. */
svn_error_t *
svn_wc__conflict_tree_update_incoming_move(svn_wc_context_t *wc_ctx,
                                           const char *local_abspath,
                                           const char *dest_abspath,
                                           svn_cancel_func_t cancel_func,
                                           void *cancel_baton,
                                           svn_wc_notify_func2_t notify_func,
                                           void *notify_baton,
                                           apr_pool_t *scratch_pool);

/* Resolve a 'local dir add vs incoming dir add' tree conflict upon update
 * by merging the locally added directory with the incoming added directory.
 *
 * @since New in 1.10. */
svn_error_t *
svn_wc__conflict_tree_update_local_add(svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       svn_cancel_func_t cancel_func,
                                       void *cancel_baton,
                                       svn_wc_notify_func2_t notify_func,
                                       void *notify_baton,
                                       apr_pool_t *scratch_pool);

/* Find nodes in the working copy which corresponds to the new location
 * MOVED_TO_REPOS_RELPATH of the tree conflict victim at VICTIM_ABSPATH.
 * The nodes must be of the same node kind as VICTIM_NODE_KIND.
 * If no such node can be found, set *POSSIBLE_TARGETS to an empty array.
 *
 * The nodes should be useful for conflict resolution, e.g. it should be
 * possible to merge changes into these nodes to resolve an incoming-move
 * tree conflict. But the exact criteria for selecting a node are left
 * to the implementation of this function.
 * Note that this function may not necessarily return a node which was
 * actually moved. The only hard guarantee is that the node corresponds to
 * the repository relpath MOVED_TO_REPOS_RELPATH specified by the caller.
 * Users should perform a sanity check on the results returned from this
 * function, e.g. establish whether the MOVED_TO_REPOS_RELPATH at its
 * current checked-out revision shares ancestry with the conflict victim.
 */
svn_error_t *
svn_wc__guess_incoming_move_target_nodes(apr_array_header_t **possible_targets,
                                         svn_wc_context_t *wc_ctx,
                                         const char *victim_abspath,
                                         svn_node_kind_t victim_node_kind,
                                         const char *moved_to_repos_relpath,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool);

/**
 * Move @a src_abspath to @a dst_abspath, by scheduling @a dst_abspath
 * for addition to the repository, remembering the history. Mark @a src_abspath
 * as deleted after moving.@a wc_ctx is used for accessing the working copy and
 * must contain a write lock for the parent directory of @a src_abspath and
 * @a dst_abspath.
 *
 * If @a metadata_only is TRUE then this is a database-only operation and
 * the working directories and files are not changed.
 *
 * @a src_abspath must be a file or directory under version control;
 * the parent of @a dst_abspath must be a directory under version control
 * in the same working copy; @a dst_abspath will be the name of the copied
 * item, and it must not exist already if @a metadata_only is FALSE.  Note that
 * when @a src points to a versioned file, the working file doesn't
 * necessarily exist in which case its text-base is used instead.
 *
 * If @a allow_mixed_revisions is @c FALSE, #SVN_ERR_WC_MIXED_REVISIONS
 * will be raised if the move source is a mixed-revision subtree.
 * If @a allow_mixed_revisions is TRUE, a mixed-revision move source is
 * allowed but the move will degrade to a copy and a delete without local
 * move tracking. This parameter should be set to FALSE except where backwards
 * compatibility to svn_wc_move() is required.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * If @a notify_func is non-NULL, call it with @a notify_baton and the path
 * of the root node (only) of the destination.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc__move2(svn_wc_context_t *wc_ctx,
              const char *src_abspath,
              const char *dst_abspath,
              svn_boolean_t metadata_only,
              svn_boolean_t allow_mixed_revisions,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func2_t notify_func,
              void *notify_baton,
              apr_pool_t *scratch_pool);


/* During merge when we encounter added directories, we add them using
   svn_wc_add4(), recording its original location, etc. But at that time
   we don't have its original properties. This function allows updating the
   BASE properties of such a special added node, but only before it receives
   other changes.

   NEW_ORIGINAL_PROPS is a new set of properties, including entry props that
   will be applied to LOCAL_ABSPATH as pristine properties.

   The copyfrom_* arguments are used to verify (some of) the assumptions of
   this function */
svn_error_t *
svn_wc__complete_directory_add(svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               apr_hash_t *new_original_props,
                               const char *copyfrom_url,
                               svn_revnum_t copyfrom_rev,
                               apr_pool_t *scratch_pool);


/* Acquire a write lock on LOCAL_ABSPATH or an ancestor that covers
   all possible paths affected by resolving the conflicts in the tree
   LOCAL_ABSPATH.  Set *LOCK_ROOT_ABSPATH to the path of the lock
   obtained. */
svn_error_t *
svn_wc__acquire_write_lock_for_resolve(const char **lock_root_abspath,
                                       svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool);

/* The implemementation of svn_wc_diff6(), but reporting to a diff processor
 *
 * If ROOT_RELPATH is not NULL, set *ROOT_RELPATH to the target of the diff
 * within the diff namespace. ("" or a single path component).
 *
 * If ROOT_IS_FILE is NOT NULL set it
 * the first processor call. (The anchor is LOCAL_ABSPATH or an ancestor of it)
 */
svn_error_t *
svn_wc__diff7(const char **root_relpath,
              svn_boolean_t *root_is_dir,
              svn_wc_context_t *wc_ctx,
              const char *local_abspath,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              const apr_array_header_t *changelist_filter,
              const svn_diff_tree_processor_t *diff_processor,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool);

/**
 * Read all conflicts at LOCAL_ABSPATH into an array containing pointers to
 * svn_wc_conflict_description2_t data structures alloated in RESULT_POOL.
 */
svn_error_t *
svn_wc__read_conflict_descriptions2_t(const apr_array_header_t **conflicts,
                                      svn_wc_context_t *wc_ctx,
                                      const char *local_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* Internal version of svn_wc_translated_stream(), accepting a working
   copy context. */
svn_error_t *
svn_wc__translated_stream(svn_stream_t **stream,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const char *versioned_abspath,
                          apr_uint32_t flags,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_PRIVATE_H */
