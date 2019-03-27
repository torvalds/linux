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
 * @file svn_wc.h
 * @brief Subversion's working copy library
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
 *
 * Notes:
 *            The 'path' parameters to most of the older functions can be
 *            absolute or relative (relative to current working
 *            directory).  If there are any cases where they are
 *            relative to the path associated with the
 *            'svn_wc_adm_access_t *adm_access' baton passed along with the
 *            path, those cases should be explicitly documented, and if they
 *            are not, please fix it. All new functions introduced since
 *            Subversion 1.7 require absolute paths, unless explicitly
 *            documented otherwise.
 *
 *            Starting with Subversion 1.7, several arguments are re-ordered
 *            to be more consistent through the api. The common ordering used
 *            is:
 *
 *            Firsts:
 *              - Output arguments
 *            Then:
 *              - Working copy context
 *              - Local abspath
 *            Followed by:
 *              - Function specific arguments
 *              - Specific callbacks with their batons
 *            Finally:
 *              - Generic callbacks (with baton) from directly functional to
 *                just observing:
 *                  - svn_wc_conflict_resolver_func2_t
 *                  - svn_wc_external_update_t
 *                  - svn_cancel_func_t
 *                  - svn_wc_notify_func2_t
 *              - Result pool
 *              - Scratch pool.
 */

#ifndef SVN_WC_H
#define SVN_WC_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <apr_time.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_checksum.h"
#include "svn_io.h"
#include "svn_delta.h"     /* for svn_stream_t */
#include "svn_opt.h"
#include "svn_ra.h"        /* for svn_ra_reporter_t type */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Get libsvn_wc version information.
 *
 * @since New in 1.1.
 */
const svn_version_t *
svn_wc_version(void);


/**
 * @defgroup svn_wc  Working copy management
 * @{
 */


/** Flags for use with svn_wc_translated_file2() and svn_wc_translated_stream().
 *
 * @defgroup translate_flags Translation flags
 * @{
 */

  /** Translate from Normal Form.
   *
   * The working copy text bases and repository files are stored
   * in normal form.  Some files' contents - or ever representation -
   * differs between the working copy and the normal form.  This flag
   * specifies to take the latter form as input and transform it
   * to the former.
   *
   * Either this flag or #SVN_WC_TRANSLATE_TO_NF should be specified,
   * but not both.
   */
#define SVN_WC_TRANSLATE_FROM_NF                 0x00000000

  /** Translate to Normal Form.
   *
   * Either this flag or #SVN_WC_TRANSLATE_FROM_NF should be specified,
   * but not both.
   */
#define SVN_WC_TRANSLATE_TO_NF                   0x00000001

  /** Force repair of eol styles, making sure the output file consistently
   * contains the one eol style as specified by the svn:eol-style
   * property and the required translation direction.
   *
   */
#define SVN_WC_TRANSLATE_FORCE_EOL_REPAIR        0x00000002

  /** Don't register a pool cleanup to delete the output file */
#define SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP       0x00000004

  /** Guarantee a new file is created on successful return.
   * The default shortcuts translation by returning the path
   * of the untranslated file when no translation is required.
   */
#define SVN_WC_TRANSLATE_FORCE_COPY              0x00000008

  /** Use a non-wc-local tmp directory for creating output files,
   * instead of in the working copy admin tmp area which is the default.
   *
   * @since New in 1.4.
   */
#define SVN_WC_TRANSLATE_USE_GLOBAL_TMP          0x00000010

/** @} */


/**
 * @defgroup svn_wc_context  Working copy context
 * @{
 */

/** The context for all working copy interactions.
 *
 * This is the client-facing datastructure API consumers are required
 * to create and use when interacting with a working copy.  Multiple
 * contexts can be created for the same working copy simultaneously, within
 * the same process or different processes.  Context mutexing will be handled
 * internally by the working copy library.
 *
 * @note: #svn_wc_context_t should be passed by non-const pointer in all
 * APIs, even for read-only operations, as it contains mutable data (caching,
 * etc.).
 *
 * @since New in 1.7.
 */
typedef struct svn_wc_context_t svn_wc_context_t;

/** Create a context for the working copy, and return it in @a *wc_ctx.  This
 * context is not associated with a particular working copy, but as operations
 * are performed, will load the appropriate working copy information.
 *
 * @a config should hold the various configuration options that may apply to
 * this context.  It should live at least as long as @a result_pool.  It may
 * be @c NULL.
 *
 * The context will be allocated in @a result_pool, and will use @a
 * result_pool for any internal allocations requiring the same longevity as
 * the context.  The context will be automatically destroyed, and its
 * resources released, when @a result_pool is cleared, or it may be manually
 * destroyed by invoking svn_wc_context_destroy().
 *
 * Use @a scratch_pool for temporary allocations.  It may be cleared
 * immediately upon returning from this function.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_context_create(svn_wc_context_t **wc_ctx,
                      const svn_config_t *config,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);


/** Destroy the working copy context described by @a wc_ctx, releasing any
 * acquired resources.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_context_destroy(svn_wc_context_t *wc_ctx);


/** @} */


/**
 * Locking/Opening/Closing using adm access batons.
 *
 * @defgroup svn_wc_adm_access Adm access batons (deprecated)
 * @{
 */

/** Baton for access to a working copy administrative area.
 *
 * Access batons can be grouped into sets, by passing an existing open
 * baton when opening a new baton.  Given one baton in a set, other batons
 * may be retrieved.  This allows an entire hierarchy to be locked, and
 * then the set of batons can be passed around by passing a single baton.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *    New code should use a #svn_wc_context_t object to access the working
 *    copy.
 */
typedef struct svn_wc_adm_access_t svn_wc_adm_access_t;


/**
 * Return, in @a *adm_access, a pointer to a new access baton for the working
 * copy administrative area associated with the directory @a path.  If
 * @a write_lock is TRUE the baton will include a write lock, otherwise the
 * baton can only be used for read access.  If @a path refers to a directory
 * that is already write locked then the error #SVN_ERR_WC_LOCKED will be
 * returned.  The error #SVN_ERR_WC_NOT_DIRECTORY will be returned if
 * @a path is not a versioned directory.
 *
 * If @a associated is an open access baton then @a adm_access will be added
 * to the set containing @a associated.  @a associated can be @c NULL, in
 * which case @a adm_access is the start of a new set.
 *
 * @a levels_to_lock specifies how far to lock.  Zero means just the specified
 * directory.  Any negative value means to lock the entire working copy
 * directory hierarchy under @a path.  A positive value indicates the number of
 * levels of directories to lock -- 1 means just immediate subdirectories, 2
 * means immediate subdirectories and their subdirectories, etc.  All the
 * access batons will become part of the set containing @a adm_access.  This
 * is an all-or-nothing option, if it is not possible to lock all the
 * requested directories then an error will be returned and @a adm_access will
 * be invalid, with the exception that subdirectories of @a path that are
 * missing from the physical filesystem will not be locked and will not cause
 * an error.  The error #SVN_ERR_WC_LOCKED will be returned if a
 * subdirectory of @a path is already write locked.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * @a pool will be used to allocate memory for the baton and any subsequently
 * cached items.  If @a adm_access has not been closed when the pool is
 * cleared, it will be closed automatically at that point, and removed from
 * its set.  A baton closed in this way will not remove physical locks from
 * the working copy if cleanup is required.
 *
 * The first baton in a set, with @a associated passed as @c NULL, must have
 * the longest lifetime of all the batons in the set.  This implies it must be
 * the root of the hierarchy.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *    Callers should use a #svn_wc_context_t object to access the working
 *    copy.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_open3(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_open3(), but without cancellation support.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_open2(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_open2(), but with @a tree_lock instead of
 * @a levels_to_lock.  @a levels_to_lock is set to -1 if @a tree_lock
 * is @c TRUE, else 0.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_open(svn_wc_adm_access_t **adm_access,
                svn_wc_adm_access_t *associated,
                const char *path,
                svn_boolean_t write_lock,
                svn_boolean_t tree_lock,
                apr_pool_t *pool);

/**
 * Checks the working copy to determine the node type of @a path.  If
 * @a path is a versioned directory then the behaviour is like that of
 * svn_wc_adm_open3(), otherwise, if @a path is a file or does not
 * exist, then the behaviour is like that of svn_wc_adm_open3() with
 * @a path replaced by the parent directory of @a path.  If @a path is
 * an unversioned directory, the behaviour is also like that of
 * svn_wc_adm_open3() on the parent, except that if the open fails,
 * then the returned #SVN_ERR_WC_NOT_DIRECTORY error refers to @a path,
 * not to @a path's parent.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *    Callers should use a #svn_wc_context_t object to access the working
 *    copy.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_open3(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_probe_open3() without the cancel
 * functionality.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_open2(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_probe_open2(), but with @a tree_lock instead of
 * @a levels_to_lock.  @a levels_to_lock is set to -1 if @a tree_lock
 * is @c TRUE, else 0.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_open(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      svn_boolean_t tree_lock,
                      apr_pool_t *pool);

/**
 * Open access batons for @a path and return in @a *anchor_access and
 * @a *target the anchor and target required to drive an editor.  Return
 * in @a *target_access the access baton for the target, which may be the
 * same as @a *anchor_access (in which case @a *target is the empty
 * string, never NULL).  All the access batons will be in the
 * @a *anchor_access set.
 *
 * @a levels_to_lock determines the levels_to_lock used when opening
 * @a path if @a path is a versioned directory, @a levels_to_lock is
 * ignored otherwise.  If @a write_lock is @c TRUE the access batons
 * will hold write locks.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * This function is essentially a combination of svn_wc_adm_open3() and
 * svn_wc_get_actual_target(), with the emphasis on reducing physical IO.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *    Callers should use a #svn_wc_context_t object to access the working
 *    copy.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_open_anchor(svn_wc_adm_access_t **anchor_access,
                       svn_wc_adm_access_t **target_access,
                       const char **target,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool);

/** Return, in @a *adm_access, a pointer to an existing access baton associated
 * with @a path.  @a path must be a directory that is locked as part of the
 * set containing the @a associated access baton.
 *
 * If the requested access baton is marked as missing in, or is simply
 * absent from, @a associated, return #SVN_ERR_WC_NOT_LOCKED.
 *
 * @a pool is used only for local processing, it is not used for the batons.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_retrieve(svn_wc_adm_access_t **adm_access,
                    svn_wc_adm_access_t *associated,
                    const char *path,
                    apr_pool_t *pool);

/** Check the working copy to determine the node type of @a path.  If
 * @a path is a versioned directory then the behaviour is like that of
 * svn_wc_adm_retrieve(), otherwise, if @a path is a file, an unversioned
 * directory, or does not exist, then the behaviour is like that of
 * svn_wc_adm_retrieve() with @a path replaced by the parent directory of
 * @a path.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_retrieve(svn_wc_adm_access_t **adm_access,
                          svn_wc_adm_access_t *associated,
                          const char *path,
                          apr_pool_t *pool);

/**
 * Try various ways to obtain an access baton for @a path.
 *
 * First, try to obtain @a *adm_access via svn_wc_adm_probe_retrieve(),
 * but if this fails because @a associated can't give a baton for
 * @a path or @a path's parent, then try svn_wc_adm_probe_open3(),
 * this time passing @a write_lock and @a levels_to_lock.  If there is
 * still no access because @a path is not a versioned directory, then
 * just set @a *adm_access to NULL and return success.  But if it is
 * because @a path is locked, then return the error #SVN_ERR_WC_LOCKED,
 * and the effect on @a *adm_access is undefined.  (Or if the attempt
 * fails for any other reason, return the corresponding error, and the
 * effect on @a *adm_access is also undefined.)
 *
 * If svn_wc_adm_probe_open3() succeeds, then add @a *adm_access to
 * @a associated.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * Use @a pool only for local processing, not to allocate @a *adm_access.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_try3(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      int levels_to_lock,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_probe_try3() without the cancel
 * functionality.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_try2(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      int levels_to_lock,
                      apr_pool_t *pool);

/**
 * Similar to svn_wc_adm_probe_try2(), but with @a tree_lock instead of
 * @a levels_to_lock.  @a levels_to_lock is set to -1 if @a tree_lock
 * is @c TRUE, else 0.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_probe_try(svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *associated,
                     const char *path,
                     svn_boolean_t write_lock,
                     svn_boolean_t tree_lock,
                     apr_pool_t *pool);


/** Give up the access baton @a adm_access, and its lock if any. This will
 * recursively close any batons in the same set that are direct
 * subdirectories of @a adm_access.  Any physical locks will be removed from
 * the working copy.  Lock removal is unconditional, there is no check to
 * determine if cleanup is required.
 *
 * Any temporary allocations are performed using @a scratch_pool.
 *
 * @since New in 1.6
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_close2(svn_wc_adm_access_t *adm_access,
                  apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_adm_close2(), but with the internal pool of @a adm_access
 * used for temporary allocations.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_adm_close(svn_wc_adm_access_t *adm_access);

/** Return the path used to open the access baton @a adm_access.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
const char *
svn_wc_adm_access_path(const svn_wc_adm_access_t *adm_access);

/** Return the pool used by access baton @a adm_access.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
apr_pool_t *
svn_wc_adm_access_pool(const svn_wc_adm_access_t *adm_access);

/** Return @c TRUE is the access baton @a adm_access has a write lock,
 * @c FALSE otherwise. Compared to svn_wc_locked() this is a cheap, fast
 * function that doesn't access the filesystem.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * New code should use svn_wc_locked2() instead.
 */
SVN_DEPRECATED
svn_boolean_t
svn_wc_adm_locked(const svn_wc_adm_access_t *adm_access);

/** @} */


/** Gets up to two booleans indicating whether a path is locked for
 * writing.
 *
 * @a locked_here is set to TRUE when a write lock on @a local_abspath
 * exists in @a wc_ctx. @a locked is set to TRUE when there is a
 * write_lock on @a local_abspath
 *
 * @a locked_here and/or @a locked can be NULL when you are not
 * interested in a specific value
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_locked2(svn_boolean_t *locked_here,
               svn_boolean_t *locked,
               svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               apr_pool_t *scratch_pool);

/** Set @a *locked to non-zero if @a path is locked, else set it to zero.
 *
 * New code should use svn_wc_locked2() instead.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_locked(svn_boolean_t *locked,
              const char *path,
              apr_pool_t *pool);


/**
 * @defgroup svn_wc_adm_dir_name Name of Subversion's admin dir
 * @{
 */

/** The default name of the administrative subdirectory.
 *
 * Ideally, this would be completely private to wc internals (in fact,
 * it used to be that adm_subdir() in adm_files.c was the only function
 * who knew the adm subdir's name).  However, import wants to protect
 * against importing administrative subdirs, so now the name is a
 * matter of public record.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
#define SVN_WC_ADM_DIR_NAME   ".svn"


/**
 * Return @c TRUE if @a name is the name of the WC administrative
 * directory.  Use @a pool for any temporary allocations.  Only works
 * with base directory names, not paths or URIs.
 *
 * For compatibility, the default name (.svn) will always be treated
 * as an admin dir name, even if the working copy is actually using an
 * alternative name.
 *
 * @since New in 1.3.
 */
svn_boolean_t
svn_wc_is_adm_dir(const char *name, apr_pool_t *pool);


/**
 * Return the name of the administrative directory.
 * Use @a pool for any temporary allocations.
 *
 * The returned pointer will refer to either a statically allocated
 * string, or to a string allocated in @a pool.
 *
 * @since New in 1.3.
 */
const char *
svn_wc_get_adm_dir(apr_pool_t *pool);


/**
 * Use @a name for the administrative directory in the working copy.
 * Use @a pool for any temporary allocations.
 *
 * The list of valid names is limited.  Currently only ".svn" (the
 * default) and "_svn" are allowed.
 *
 * @note This function changes global (per-process) state and must be
 * called in a single-threaded context during the initialization of a
 * Subversion client.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_wc_set_adm_dir(const char *name,
                   apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_externals Externals
 * @{
 */

/** Callback for external definitions updates
 *
 * @a local_abspath is the path on which the external definition was found.
 * @a old_val and @a new_val are the before and after values of the
 * SVN_PROP_EXTERNALS property.  @a depth is the ambient depth of the
 * working copy directory at @a local_abspath.
 *
 * @since New in 1.7. */
typedef svn_error_t *(*svn_wc_external_update_t)(void *baton,
                                                 const char *local_abspath,
                                                 const svn_string_t *old_val,
                                                 const svn_string_t *new_val,
                                                 svn_depth_t depth,
                                                 apr_pool_t *scratch_pool);

/** Traversal information is information gathered by a working copy
 * crawl or update.  For example, the before and after values of the
 * svn:externals property are important after an update, and since
 * we're traversing the working tree anyway (a complete traversal
 * during the initial crawl, and a traversal of changed paths during
 * the checkout/update/switch), it makes sense to gather the
 * property's values then instead of making a second pass.
 *
 * New code should use the svn_wc_external_update_t callback instead.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef struct svn_wc_traversal_info_t svn_wc_traversal_info_t;


/** Return a new, empty traversal info object, allocated in @a pool.
 *
 * New code should use the svn_wc_external_update_t callback instead.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_traversal_info_t *
svn_wc_init_traversal_info(apr_pool_t *pool);

/** Set @a *externals_old and @a *externals_new to hash tables representing
 * changes to values of the svn:externals property on directories
 * traversed by @a traversal_info.
 *
 * @a traversal_info is obtained from svn_wc_init_traversal_info(), but is
 * only useful after it has been passed through another function, such
 * as svn_wc_crawl_revisions(), svn_wc_get_update_editor(),
 * svn_wc_get_switch_editor(), etc.
 *
 * Each hash maps <tt>const char *</tt> directory names onto
 * <tt>const char *</tt> values of the externals property for that directory.
 * The dir names are full paths -- that is, anchor plus target, not target
 * alone. The values are not parsed, they are simply copied raw, and are
 * never NULL: directories that acquired or lost the property are
 * simply omitted from the appropriate table.  Directories whose value
 * of the property did not change show the same value in each hash.
 *
 * The hashes, keys, and values have the same lifetime as @a traversal_info.
 *
 * New code should use the svn_wc_external_update_t callback instead.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
void
svn_wc_edited_externals(apr_hash_t **externals_old,
                        apr_hash_t **externals_new,
                        svn_wc_traversal_info_t *traversal_info);


/** Set @a *depths to a hash table mapping <tt>const char *</tt>
 * directory names (directories traversed by @a traversal_info) to
 * <tt>const char *</tt> values (the depths of those directories, as
 * converted by svn_depth_to_word()).
 *
 * @a traversal_info is obtained from svn_wc_init_traversal_info(), but is
 * only useful after it has been passed through another function, such
 * as svn_wc_crawl_revisions(), svn_wc_get_update_editor(),
 * svn_wc_get_switch_editor(), etc.
 *
 * The dir names are full paths -- that is, anchor plus target, not target
 * alone.  The values are not allocated, they are static constant strings.
 * Although the values are never NULL, not all directories traversed
 * are necessarily listed.  For example, directories which did not
 * have an svn:externals property set or modified are not included.
 *
 * The hashes and keys have the same lifetime as @a traversal_info.
 *
 * New code should use the svn_wc_external_update_t callback instead.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
void
svn_wc_traversed_depths(apr_hash_t **depths,
                        svn_wc_traversal_info_t *traversal_info);


/** One external item.  This usually represents one line from an
 * svn:externals description but with the path and URL
 * canonicalized.
 *
 * In order to avoid backwards compatibility problems clients should use
 * svn_wc_external_item2_create() to allocate and initialize this structure
 * instead of doing so themselves.
 *
 * @since New in 1.5.
 */
typedef struct svn_wc_external_item2_t
{
  /** The name of the subdirectory into which this external should be
      checked out.  This is relative to the parent directory that
      holds this external item.  (Note that these structs are often
      stored in hash tables with the target dirs as keys, so this
      field will often be redundant.) */
  const char *target_dir;

  /** Where to check out from. This is possibly a relative external URL, as
   * allowed in externals definitions, but without the peg revision. */
  const char *url;

  /** What revision to check out.  The only valid kinds for this are
      svn_opt_revision_number, svn_opt_revision_date, and
      svn_opt_revision_head. */
  svn_opt_revision_t revision;

  /** The peg revision to use when checking out.  The only valid kinds are
      svn_opt_revision_number, svn_opt_revision_date, and
      svn_opt_revision_head. */
  svn_opt_revision_t peg_revision;

} svn_wc_external_item2_t;

/**
 * Initialize an external item.
 * Set @a *item to an external item object, allocated in @a pool.
 *
 * In order to avoid backwards compatibility problems, this function
 * is used to initialize and allocate the #svn_wc_external_item2_t
 * structure rather than doing so explicitly, as the size of this
 * structure may change in the future.
 *
 * The current implementation never returns error, but callers should
 * still check for error, for compatibility with future versions.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc_external_item2_create(svn_wc_external_item2_t **item,
                             apr_pool_t *pool);

/** Same as svn_wc_external_item2_create() except the pointer to the new
 * empty item is 'const' which is stupid since the next thing you need to do
 * is fill in its fields.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_external_item_create(const svn_wc_external_item2_t **item,
                            apr_pool_t *pool);

/**
 * Return a duplicate of @a item, allocated in @a pool.  No part of the new
 * item will be shared with @a item.
 *
 * @since New in 1.5.
 */
svn_wc_external_item2_t *
svn_wc_external_item2_dup(const svn_wc_external_item2_t *item,
                          apr_pool_t *pool);

/**
 * One external item.  Similar to svn_wc_external_item2_t, except
 * @a revision is interpreted as both the operational revision and the
 * peg revision.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
typedef struct svn_wc_external_item_t
{
  /** Same as #svn_wc_external_item2_t.target_dir */
  const char *target_dir;

  /** Same as #svn_wc_external_item2_t.url */
  const char *url;

  /** Same as #svn_wc_external_item2_t.revision */
  svn_opt_revision_t revision;

} svn_wc_external_item_t;

/**
 * Return a duplicate of @a item, allocated in @a pool.  No part of the new
 * item will be shared with @a item.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_wc_external_item_t *
svn_wc_external_item_dup(const svn_wc_external_item_t *item,
                         apr_pool_t *pool);

/**
 * If @a externals_p is non-NULL, set @a *externals_p to an array of
 * #svn_wc_external_item2_t * objects based on @a desc.
 *
 * If the format of @a desc is invalid, don't touch @a *externals_p and
 * return #SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION.  Thus, if
 * you just want to check the validity of an externals description,
 * and don't care about the parsed result, pass NULL for @a externals_p.
 *
 * The format of @a desc is the same as for values of the directory
 * property #SVN_PROP_EXTERNALS.  Look there for more details.
 *
 * If @a canonicalize_url is @c TRUE, canonicalize the @a url member
 * of those objects.  If the @a url member refers to an absolute URL,
 * it will be canonicalized as URL consistent with the way URLs are
 * canonicalized throughout the Subversion API.  If, however, the
 * @a url member makes use of the recognized (SVN-specific) relative
 * URL syntax for svn:externals, "canonicalization" is an ill-defined
 * concept which may even result in munging the relative URL syntax
 * beyond recognition.  You've been warned.
 *
 * Allocate the table, keys, and values in @a pool.
 *
 * @a defining_directory is the path or URL of the directory on which
 * the svn:externals property corresponding to @a desc is set.
 * @a defining_directory is only used when constructing error strings.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc_parse_externals_description3(apr_array_header_t **externals_p,
                                    const char *defining_directory,
                                    const char *desc,
                                    svn_boolean_t canonicalize_url,
                                    apr_pool_t *pool);

/**
 * Similar to svn_wc_parse_externals_description3() with @a
 * canonicalize_url set to @c TRUE, but returns an array of
 * #svn_wc_external_item_t * objects instead of
 * #svn_wc_external_item2_t * objects
 *
 * @since New in 1.1.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_parse_externals_description2(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    apr_pool_t *pool);

/**
 * Similar to svn_wc_parse_externals_description2(), but returns the
 * parsed externals in a hash instead of an array.  This function
 * should not be used, as storing the externals in a hash causes their
 * order of evaluation to be not easily identifiable.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_parse_externals_description(apr_hash_t **externals_p,
                                   const char *parent_directory,
                                   const char *desc,
                                   apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_notifications Notification callback handling
 * @{
 *
 * In many cases, the WC library will scan a working copy and make
 * changes. The caller usually wants to know when each of these changes
 * has been made, so that it can display some kind of notification to
 * the user.
 *
 * These notifications have a standard callback function type, which
 * takes the path of the file that was affected, and a caller-
 * supplied baton.
 *
 * @note The callback is a 'void' return -- this is a simple
 * reporting mechanism, rather than an opportunity for the caller to
 * alter the operation of the WC library.
 *
 * @note Some of the actions are used across several
 * different Subversion commands.  For example, the update actions are
 * also used for checkouts, switches, and merges.
 */

/** The type of action occurring. */
typedef enum svn_wc_notify_action_t
{
  /** Adding a path to revision control. */
  svn_wc_notify_add = 0,

  /** Copying a versioned path. */
  svn_wc_notify_copy,

  /** Deleting a versioned path. */
  svn_wc_notify_delete,

  /** Restoring a missing path from the pristine text-base. */
  svn_wc_notify_restore,

  /** Reverting a modified path. */
  svn_wc_notify_revert,

  /** A revert operation has failed. */
  svn_wc_notify_failed_revert,

  /** All conflicts on a path were marked as resolved.
   * @note As of 1.10, separate notifications are sent for individually
   * resolved text, property, and tree conflicts. This notification is used
   * only if all conflicts on a path were marked resolved at once. */
  svn_wc_notify_resolved,

  /** Skipping a path. */
  svn_wc_notify_skip,

  /** Got a delete in an update. */
  svn_wc_notify_update_delete,

  /** Got an add in an update. */
  svn_wc_notify_update_add,

  /** Got any other action in an update. */
  svn_wc_notify_update_update,

  /** The last notification in an update (including updates of externals). */
  svn_wc_notify_update_completed,

  /** Updating an external module. */
  svn_wc_notify_update_external,

  /** The last notification in a status (including status on externals). */
  svn_wc_notify_status_completed,

  /** Running status on an external module. */
  svn_wc_notify_status_external,

  /** Committing a modification. */
  svn_wc_notify_commit_modified,

  /** Committing an addition. */
  svn_wc_notify_commit_added,

  /** Committing a deletion. */
  svn_wc_notify_commit_deleted,

  /** Committing a replacement. */
  svn_wc_notify_commit_replaced,

  /** Transmitting post-fix text-delta data for a file. */
  svn_wc_notify_commit_postfix_txdelta,

  /** Processed a single revision's blame. */
  svn_wc_notify_blame_revision,

  /** Locking a path. @since New in 1.2. */
  svn_wc_notify_locked,

  /** Unlocking a path. @since New in 1.2. */
  svn_wc_notify_unlocked,

  /** Failed to lock a path. @since New in 1.2. */
  svn_wc_notify_failed_lock,

  /** Failed to unlock a path. @since New in 1.2. */
  svn_wc_notify_failed_unlock,

  /** Tried adding a path that already exists. @since New in 1.5. */
  svn_wc_notify_exists,

  /** Changelist name set. @since New in 1.5. */
  svn_wc_notify_changelist_set,

  /** Changelist name cleared. @since New in 1.5. */
  svn_wc_notify_changelist_clear,

  /** Warn user that a path has moved from one changelist to another.
      @since New in 1.5.
      @deprecated As of 1.7, separate clear and set notifications are sent. */
  svn_wc_notify_changelist_moved,

  /** A merge operation (to path) has begun.  See #svn_wc_notify_t.merge_range.
      @since New in 1.5. */
  svn_wc_notify_merge_begin,

  /** A merge operation (to path) from a foreign repository has begun.
      See #svn_wc_notify_t.merge_range.  @since New in 1.5. */
  svn_wc_notify_foreign_merge_begin,

  /** Replace notification. @since New in 1.5. */
  svn_wc_notify_update_replace,

  /** Property added. @since New in 1.6. */
  svn_wc_notify_property_added,

  /** Property updated. @since New in 1.6. */
  svn_wc_notify_property_modified,

  /** Property deleted. @since New in 1.6. */
  svn_wc_notify_property_deleted,

  /** Nonexistent property deleted. @since New in 1.6. */
  svn_wc_notify_property_deleted_nonexistent,

  /** Revprop set. @since New in 1.6. */
  svn_wc_notify_revprop_set,

  /** Revprop deleted. @since New in 1.6. */
  svn_wc_notify_revprop_deleted,

  /** The last notification in a merge. @since New in 1.6. */
  svn_wc_notify_merge_completed,

  /** The path is a tree-conflict victim of the intended action (*not*
   * a persistent tree-conflict from an earlier operation, but *this*
   * operation caused the tree-conflict). @since New in 1.6. */
  svn_wc_notify_tree_conflict,

  /** The path is a subdirectory referenced in an externals definition
   * which is unable to be operated on.  @since New in 1.6. */
  svn_wc_notify_failed_external,

  /** Starting an update operation.  @since New in 1.7. */
  svn_wc_notify_update_started,

  /** An update tried to add a file or directory at a path where
   * a separate working copy was found.  @since New in 1.7. */
  svn_wc_notify_update_skip_obstruction,

  /** An explicit update tried to update a file or directory that
   * doesn't live in the repository and can't be brought in.
   * @since New in 1.7. */
  svn_wc_notify_update_skip_working_only,

  /** An update tried to update a file or directory to which access could
   * not be obtained. @since New in 1.7. */
  svn_wc_notify_update_skip_access_denied,

  /** An update operation removed an external working copy.
   * @since New in 1.7. */
  svn_wc_notify_update_external_removed,

  /** A node below an existing node was added during update.
   * @since New in 1.7. */
  svn_wc_notify_update_shadowed_add,

  /** A node below an existing node was updated during update.
   * @since New in 1.7. */
  svn_wc_notify_update_shadowed_update,

  /** A node below an existing node was deleted during update.
   * @since New in 1.7. */
  svn_wc_notify_update_shadowed_delete,

  /** The mergeinfo on path was updated.  @since New in 1.7. */
  svn_wc_notify_merge_record_info,

  /** A working copy directory was upgraded to the latest format.
   * @since New in 1.7. */
  svn_wc_notify_upgraded_path,

  /** Mergeinfo describing a merge was recorded.
   * @since New in 1.7. */
  svn_wc_notify_merge_record_info_begin,

  /** Mergeinfo was removed due to elision.
   * @since New in 1.7. */
  svn_wc_notify_merge_elide_info,

  /** A file in the working copy was patched.
   * @since New in 1.7. */
  svn_wc_notify_patch,

  /** A hunk from a patch was applied.
   * @since New in 1.7. */
  svn_wc_notify_patch_applied_hunk,

  /** A hunk from a patch was rejected.
   * @since New in 1.7. */
  svn_wc_notify_patch_rejected_hunk,

  /** A hunk from a patch was found to already be applied.
   * @since New in 1.7. */
  svn_wc_notify_patch_hunk_already_applied,

  /** Committing a non-overwriting copy (path is the target of the
   * copy, not the source).
   * @since New in 1.7. */
  svn_wc_notify_commit_copied,

  /** Committing an overwriting (replace) copy (path is the target of
   * the copy, not the source).
   * @since New in 1.7. */
  svn_wc_notify_commit_copied_replaced,

  /** The server has instructed the client to follow a URL
   * redirection.
   * @since New in 1.7. */
  svn_wc_notify_url_redirect,

  /** The operation was attempted on a path which doesn't exist.
   * @since New in 1.7. */
  svn_wc_notify_path_nonexistent,

  /** Removing a path by excluding it.
   * @since New in 1.7. */
  svn_wc_notify_exclude,

  /** Operation failed because the node remains in conflict
   * @since New in 1.7. */
  svn_wc_notify_failed_conflict,

  /** Operation failed because an added node is missing
   * @since New in 1.7. */
  svn_wc_notify_failed_missing,

  /** Operation failed because a node is out of date
   * @since New in 1.7. */
  svn_wc_notify_failed_out_of_date,

  /** Operation failed because an added parent is not selected
   * @since New in 1.7. */
  svn_wc_notify_failed_no_parent,

  /** Operation failed because a node is locked by another user and/or
   * working copy.  @since New in 1.7. */
  svn_wc_notify_failed_locked,

  /** Operation failed because the operation was forbidden by the server.
   * @since New in 1.7. */
  svn_wc_notify_failed_forbidden_by_server,

  /** The operation skipped the path because it was conflicted.
   * @since New in 1.7. */
  svn_wc_notify_skip_conflicted,

  /** Just the lock on a file was removed during update.
   * @since New in 1.8. */
  svn_wc_notify_update_broken_lock,

  /** Operation failed because a node is obstructed.
   * @since New in 1.8. */
  svn_wc_notify_failed_obstruction,

  /** Conflict resolver is starting.
   * This can be used by clients to detect when to display conflict summary
   * information, for example.
   * @since New in 1.8. */
  svn_wc_notify_conflict_resolver_starting,

  /** Conflict resolver is done.
   * This can be used by clients to detect when to display conflict summary
   * information, for example.
   * @since New in 1.8. */
  svn_wc_notify_conflict_resolver_done,

  /** The current operation left local changes of something that was deleted
   * The changes are available on (and below) the notified path
   * @since New in 1.8. */
  svn_wc_notify_left_local_modifications,

  /** A copy from a foreign repository has started
   * @since New in 1.8. */
  svn_wc_notify_foreign_copy_begin,

  /** A move in the working copy has been broken, i.e. degraded into a
   * copy + delete. The notified path is the move source (the deleted path).
   * ### TODO: Provide path to move destination as well?
   * @since New in 1.8. */
  svn_wc_notify_move_broken,

  /** Running cleanup on an external module.
   * @since New in 1.9. */
  svn_wc_notify_cleanup_external,

  /** The operation failed because the operation (E.g. commit) is only valid
   * if the operation includes this path.
   * @since New in 1.9. */
  svn_wc_notify_failed_requires_target,

  /** Running info on an external module.
   * @since New in 1.9. */
  svn_wc_notify_info_external,

  /** Finalizing commit.
   * @since New in 1.9. */
  svn_wc_notify_commit_finalizing,

  /** All text conflicts in a file were marked as resolved.
   * @since New in 1.10. */
  svn_wc_notify_resolved_text,

  /** A property conflict on a path was marked as resolved.
   * The name of the property is specified in #svn_wc_notify_t.prop_name.
   * @since New in 1.10. */
  svn_wc_notify_resolved_prop,

  /** A tree conflict on a path was marked as resolved.
   * @since New in 1.10. */
  svn_wc_notify_resolved_tree,

  /** Starting to search the repository for details about a tree conflict.
   * @since New in 1.10. */
  svn_wc_notify_begin_search_tree_conflict_details,

  /** Progressing in search of repository for details about a tree conflict.
   * The revision being searched is specified in #svn_wc_notify_t.revision.
   * @since New in 1.10. */
  svn_wc_notify_tree_conflict_details_progress,

  /** Done searching the repository for details about a conflict.
   * @since New in 1.10. */
  svn_wc_notify_end_search_tree_conflict_details

} svn_wc_notify_action_t;


/** The type of notification that is occurring. */
typedef enum svn_wc_notify_state_t
{
  svn_wc_notify_state_inapplicable = 0,

  /** Notifier doesn't know or isn't saying. */
  svn_wc_notify_state_unknown,

  /** The state did not change. */
  svn_wc_notify_state_unchanged,

  /** The item wasn't present. */
  svn_wc_notify_state_missing,

  /** An unversioned item obstructed work. */
  svn_wc_notify_state_obstructed,

  /** Pristine state was modified. */
  svn_wc_notify_state_changed,

  /** Modified state had mods merged in. */
  svn_wc_notify_state_merged,

  /** Modified state got conflicting mods. */
  svn_wc_notify_state_conflicted,

  /** The source to copy the file from is missing. */
  svn_wc_notify_state_source_missing

} svn_wc_notify_state_t;

/**
 * What happened to a lock during an operation.
 *
 * @since New in 1.2.
 */
typedef enum svn_wc_notify_lock_state_t
{
  svn_wc_notify_lock_state_inapplicable = 0,

  svn_wc_notify_lock_state_unknown,

  /** The lock wasn't changed. */
  svn_wc_notify_lock_state_unchanged,

  /** The item was locked. */
  svn_wc_notify_lock_state_locked,

  /** The item was unlocked. */
  svn_wc_notify_lock_state_unlocked

} svn_wc_notify_lock_state_t;

/**
 * Structure used in the #svn_wc_notify_func2_t function.
 *
 * @c kind, @c content_state, @c prop_state and @c lock_state are from
 * after @c action, not before.
 *
 * @note If @c action is #svn_wc_notify_update_completed, then @c path has
 * already been installed, so it is legitimate for an implementation of
 * #svn_wc_notify_func2_t to examine @c path in the working copy.
 *
 * @note The purpose of the @c kind, @c mime_type, @c content_state, and
 * @c prop_state fields is to provide "for free" information that an
 * implementation is likely to want, and which it would otherwise be
 * forced to deduce via expensive operations such as reading entries
 * and properties.  However, if the caller does not have this
 * information, it will simply pass the corresponding `*_unknown'
 * values, and it is up to the implementation how to handle that
 * (i.e., whether to attempt deduction, or just to punt and
 * give a less informative notification).
 *
 * @note Callers of notification functions should use svn_wc_create_notify()
 * or svn_wc_create_notify_url() to create structures of this type to allow
 * for extensibility.
 *
 * @since New in 1.2.
 */
typedef struct svn_wc_notify_t {

  /** Path, either absolute or relative to the current working directory
   * (i.e., not relative to an anchor).  @c path is "." or another valid path
   * value for compatibility reasons when the real target is a url that
   * is available in @c url. */
  const char *path;

  /** Action that describes what happened to #svn_wc_notify_t.path. */
  svn_wc_notify_action_t action;

  /** Node kind of @c path. */
  svn_node_kind_t kind;

  /** If non-NULL, indicates the mime-type of @c path.
   * It is always @c NULL for directories. */
  const char *mime_type;

  /** Points to the lock structure received from the repository when
   * @c action is #svn_wc_notify_locked.  For other actions, it is
   * @c NULL. */
  const svn_lock_t *lock;

  /** Points to an error describing the reason for the failure when @c
   * action is one of the following: #svn_wc_notify_failed_lock,
   * #svn_wc_notify_failed_unlock, #svn_wc_notify_failed_external.
   * Is @c NULL otherwise. */
  svn_error_t *err;

  /** The type of notification that is occurring about node content. */
  svn_wc_notify_state_t content_state;

  /** The type of notification that is occurring about node properties. */
  svn_wc_notify_state_t prop_state;

  /** Reflects the addition or removal of a lock token in the working copy. */
  svn_wc_notify_lock_state_t lock_state;

  /** When @c action is #svn_wc_notify_update_completed, target revision
   * of the update, or #SVN_INVALID_REVNUM if not available; when @c
   * action is #svn_wc_notify_blame_revision, processed revision; Since
   * Subversion 1.7 when action is #svn_wc_notify_update_update or
   * #svn_wc_notify_update_add, the target revision.
   * In all other cases, it is #SVN_INVALID_REVNUM.
   */
  svn_revnum_t revision;

  /** If @c action pertains to a changelist, this is the changelist name.
   * In all other cases, it is @c NULL.  @since New in 1.5 */
  const char *changelist_name;

  /** When @c action is #svn_wc_notify_merge_begin or
   * #svn_wc_notify_foreign_merge_begin or
   * #svn_wc_notify_merge_record_info_begin, and both the
   * left and right sides of the merge are from the same URL.  In all
   * other cases, it is @c NULL.  @since New in 1.5 */
  svn_merge_range_t *merge_range;

  /** Similar to @c path, but if non-NULL the notification is about a url.
   * @since New in 1.6 */
  const char *url;

  /** If non-NULL, specifies an absolute path prefix that can be subtracted
   * from the start of the absolute path in @c path or @c url.  Its purpose
   * is to allow notification to remove a common prefix from all the paths
   * displayed for an operation.  @since New in 1.6 */
  const char *path_prefix;

  /** If @c action relates to properties, specifies the name of the property.
   * @since New in 1.6 */
  const char *prop_name;

  /** If @c action is #svn_wc_notify_blame_revision, contains a list of
   * revision properties for the specified revision
   * @since New in 1.6 */
  apr_hash_t *rev_props;

  /** If @c action is #svn_wc_notify_update_update or
   * #svn_wc_notify_update_add, contains the revision before the update.
   * In all other cases, it is #SVN_INVALID_REVNUM.
   * @since New in 1.7 */
  svn_revnum_t old_revision;

  /** These fields are used by svn patch to identify the
   * hunk the notification is for. They are line-based
   * offsets and lengths parsed from the unidiff hunk header.
   * @since New in 1.7. */
  /* @{ */
  svn_linenum_t hunk_original_start;
  svn_linenum_t hunk_original_length;
  svn_linenum_t hunk_modified_start;
  svn_linenum_t hunk_modified_length;
  /* @} */

  /** The line at which a hunk was matched (and applied).
   * @since New in 1.7. */
  svn_linenum_t hunk_matched_line;

  /** The fuzz factor the hunk was applied with.
   * @since New in 1.7 */
  svn_linenum_t hunk_fuzz;

  /* NOTE: Add new fields at the end to preserve binary compatibility.
     Also, if you add fields here, you have to update svn_wc_create_notify
     and svn_wc_dup_notify. */
} svn_wc_notify_t;

/**
 * Allocate an #svn_wc_notify_t structure in @a pool, initialize and return
 * it.
 *
 * Set the @c path field of the created struct to @a path, and @c action to
 * @a action.  Set all other fields to their @c _unknown, @c NULL or
 * invalid value, respectively. Make only a shallow copy of the pointer
 * @a path.
 *
 * @since New in 1.2.
 */
svn_wc_notify_t *
svn_wc_create_notify(const char *path,
                     svn_wc_notify_action_t action,
                     apr_pool_t *pool);

/**
 * Allocate an #svn_wc_notify_t structure in @a pool, initialize and return
 * it.
 *
 * Set the @c url field of the created struct to @a url, @c path to "." and @c
 * action to @a action.  Set all other fields to their @c _unknown, @c NULL or
 * invalid value, respectively. Make only a shallow copy of the pointer
 * @a url.
 *
 * @since New in 1.6.
 */
svn_wc_notify_t *
svn_wc_create_notify_url(const char *url,
                         svn_wc_notify_action_t action,
                         apr_pool_t *pool);

/**
 * Return a deep copy of @a notify, allocated in @a pool.
 *
 * @since New in 1.2.
 */
svn_wc_notify_t *
svn_wc_dup_notify(const svn_wc_notify_t *notify,
                  apr_pool_t *pool);

/**
 * Notify the world that @a notify->action has happened to @a notify->path.
 *
 * Recommendation: callers of #svn_wc_notify_func2_t should avoid
 * invoking it multiple times on the same path within a given
 * operation, and implementations should not bother checking for such
 * duplicate calls.  For example, in an update, the caller should not
 * invoke the notify func on receiving a prop change and then again
 * on receiving a text change.  Instead, wait until all changes have
 * been received, and then invoke the notify func once (from within
 * an #svn_delta_editor_t's close_file(), for example), passing
 * the appropriate @a notify->content_state and @a notify->prop_state flags.
 *
 * @since New in 1.2.
 */
typedef void (*svn_wc_notify_func2_t)(void *baton,
                                      const svn_wc_notify_t *notify,
                                      apr_pool_t *pool);

/**
 * Similar to #svn_wc_notify_func2_t, but takes the information as arguments
 * instead of struct fields.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef void (*svn_wc_notify_func_t)(void *baton,
                                     const char *path,
                                     svn_wc_notify_action_t action,
                                     svn_node_kind_t kind,
                                     const char *mime_type,
                                     svn_wc_notify_state_t content_state,
                                     svn_wc_notify_state_t prop_state,
                                     svn_revnum_t revision);

/** @} */


/**
 * Interactive conflict handling
 *
 * @defgroup svn_wc_conflict Conflict callback functionality
 * @{
 *
 * This API gives a Subversion client application the opportunity to
 * define a callback that allows the user to resolve conflicts
 * interactively during updates and merges.
 *
 * If a conflict is discovered, libsvn_wc invokes the callback with an
 * #svn_wc_conflict_description_t.  This structure describes the
 * path in conflict, whether it's a text or property conflict, and may
 * also present up to three files that can be used to resolve the
 * conflict (perhaps by launching an editor or 3rd-party merging
 * tool).  The structure also provides a possible fourth file (@c
 * merged_file) which, if not NULL, represents libsvn_wc's attempt to
 * contextually merge the first three files.  (Note that libsvn_wc
 * will not attempt to merge a file that it believes is binary, and it
 * will only attempt to merge property values it believes to be a
 * series of multi-line text.)
 *
 * When the callback is finished interacting with the user, it
 * responds by returning a #svn_wc_conflict_result_t.  This
 * structure indicates whether the user wants to postpone the conflict
 * for later (allowing libsvn_wc to mark the path "conflicted" as
 * usual), or whether the user wants libsvn_wc to use one of the four
 * files as the "final" state for resolving the conflict immediately.
 *
 * Note that the callback is at liberty (and encouraged) to merge the
 * three files itself.  If it does so, it signals this to libsvn_wc by
 * returning a choice of #svn_wc_conflict_choose_merged.  To return
 * the 'final' merged file to libsvn_wc, the callback has the option of
 * either:
 *
 *    - editing the original @c merged_file in-place
 *
 *        or, if libsvn_wc never supplied a merged_file in the
 *        description structure (i.e. passed NULL for that field),
 *
 *    - return the merged file in the #svn_wc_conflict_result_t.
 *
 */

/** The type of action being attempted on an object.
 *
 * @since New in 1.5.
 */
typedef enum svn_wc_conflict_action_t
{
  svn_wc_conflict_action_edit,    /**< attempting to change text or props */
  svn_wc_conflict_action_add,     /**< attempting to add object */
  svn_wc_conflict_action_delete,  /**< attempting to delete object */
  svn_wc_conflict_action_replace  /**< attempting to replace object,
                                       @since New in 1.7 */
} svn_wc_conflict_action_t;


/** The pre-existing condition which is causing a state of conflict.
 *
 * @since New in 1.5.
 */
typedef enum svn_wc_conflict_reason_t
{
  /** Local edits are already present */
  svn_wc_conflict_reason_edited,
  /** Another object is in the way */
  svn_wc_conflict_reason_obstructed,
  /** Object is already schedule-delete */
  svn_wc_conflict_reason_deleted,
  /** Object is unknown or missing */
  svn_wc_conflict_reason_missing,
  /** Object is unversioned */
  svn_wc_conflict_reason_unversioned,
  /** Object is already added or schedule-add. @since New in 1.6. */
  svn_wc_conflict_reason_added,
  /** Object is already replaced. @since New in 1.7. */
  svn_wc_conflict_reason_replaced,
  /** Object is moved away. @since New in 1.8. */
  svn_wc_conflict_reason_moved_away,
  /** Object is moved here. @since New in 1.8. */
  svn_wc_conflict_reason_moved_here

} svn_wc_conflict_reason_t;


/** The type of conflict being described by an
 * #svn_wc_conflict_description2_t (see below).
 *
 * @since New in 1.5.
 */
typedef enum svn_wc_conflict_kind_t
{
  /** textual conflict (on a file) */
  svn_wc_conflict_kind_text,
  /** property conflict (on a file or dir) */
  svn_wc_conflict_kind_property,
  /** tree conflict (on a dir) @since New in 1.6. */
  svn_wc_conflict_kind_tree
} svn_wc_conflict_kind_t;


/** The user operation that exposed a conflict.
 *
 * @since New in 1.6.
 */
typedef enum svn_wc_operation_t
{
  svn_wc_operation_none = 0,
  svn_wc_operation_update,
  svn_wc_operation_switch,
  svn_wc_operation_merge

} svn_wc_operation_t;


/** Info about one of the conflicting versions of a node. Each field may
 * have its respective null/invalid/unknown value if the corresponding
 * information is not relevant or not available.
 *
 * @todo Consider making some or all of the info mandatory, to reduce
 * complexity.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type.
 *
 * @see svn_wc_conflict_version_create()
 * @see svn_wc_conflict_version_dup()
 *
 * @since New in 1.6.
*/
typedef struct svn_wc_conflict_version_t
{
  /** @name Where to find this node version in a repository */
  /**@{*/

  /** URL of repository root */
  const char *repos_url;

  /** revision at which to look up path_in_repos */
  svn_revnum_t peg_rev;

  /** path within repos; must not start with '/' */
   /* ### should have been called repos_relpath, but we can't change this. */
  const char *path_in_repos;
  /** @} */

  /** The node kind.  Can be any kind, including 'none' or 'unknown'. */
  svn_node_kind_t node_kind;

  /** UUID of the repository (or NULL if unknown.)
   * @since New in 1.8. */
  const char *repos_uuid;

  /* @todo Add metadata about a local copy of the node, if and when
   * we store one. */

  /* Remember to update svn_wc_conflict_version_create() and
   * svn_wc_conflict_version_dup() in case you add fields to this struct. */
} svn_wc_conflict_version_t;

/**
 * Allocate an #svn_wc_conflict_version_t structure in @a pool,
 * initialize to contain a conflict origin, and return it.
 *
 * Set the @c repos_url field of the created struct to @a repos_root_url,
 * the @c path_in_repos field to @a repos_relpath, the @c peg_rev field to
 * @a revision and the @c node_kind to @a kind. Make only shallow
 * copies of the pointer arguments.
 *
 * @a repos_root_url, @a repos_relpath and @a revision must be valid,
 * non-null values. @a repos_uuid should be a valid UUID, but can be
 * NULL if unknown. @a kind can be any kind, even 'none' or 'unknown'.
 *
 * @since New in 1.8.
 */
svn_wc_conflict_version_t *
svn_wc_conflict_version_create2(const char *repos_root_url,
                                const char *repos_uuid,
                                const char *repos_relpath,
                                svn_revnum_t revision,
                                svn_node_kind_t kind,
                                apr_pool_t *result_pool);

/** Similar to svn_wc_conflict_version_create2(), but doesn't set all
 * required values.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_wc_conflict_version_t *
svn_wc_conflict_version_create(const char *repos_url,
                               const char *path_in_repos,
                               svn_revnum_t peg_rev,
                               svn_node_kind_t node_kind,
                               apr_pool_t *pool);

/** Return a duplicate of @a version, allocated in @a pool.
 * No part of the new version will be shared with @a version.
 *
 * @since New in 1.6.
 */
svn_wc_conflict_version_t *
svn_wc_conflict_version_dup(const svn_wc_conflict_version_t *version,
                            apr_pool_t *pool);


/** A struct that describes a conflict that has occurred in the
 * working copy.
 *
 * The conflict described by this structure is one of:
 *   - a conflict on the content of the file node @a local_abspath
 *   - a conflict on the property @a property_name of @a local_abspath
 *   - a tree conflict, of which @a local_abspath is the victim
 * Be aware that the victim of a tree conflict can be a non-existent node.
 * The three kinds of conflict are distinguished by @a kind.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type but should use
 * svn_wc_conflict_description_create_text2() or
 * svn_wc_conflict_description_create_prop2() or
 * svn_wc_conflict_description_create_tree2() instead.
 *
 * @since New in 1.7.
 */
typedef struct svn_wc_conflict_description2_t
{
  /** The path that is in conflict (for a tree conflict, it is the victim) */
  const char *local_abspath;

  /** The node type of the local node involved in this conflict.
   * For a tree conflict, this is the node kind of the tree conflict victim.
   * For the left/right node kinds of the incoming conflicting change see
   * src_left_version->node_kind and src_right_version->node_kind. */
  svn_node_kind_t node_kind;

  /** What sort of conflict are we describing? */
  svn_wc_conflict_kind_t kind;

  /** The name of the property whose conflict is being described.
   *  (Only if @a kind is 'property'; else undefined.) */
  const char *property_name;

  /** Whether svn thinks ('my' version of) @c path is a 'binary' file.
   *  (Only if @c kind is 'text', else undefined.) */
  svn_boolean_t is_binary;

  /** The svn:mime-type property of ('my' version of) @c path, if available,
   *  else NULL.
   *  (Only if @c kind is 'text', else undefined.) */
  const char *mime_type;

  /** The incoming action being attempted on the conflicted node or property.
   *  When @c kind is 'text', this action must be 'edit', but generally it can
   *  be any kind of possible change. */
  svn_wc_conflict_action_t action;

  /** The local change or state of the target node or property, relative
   *  to its merge-left source, that conflicts with the incoming action.
   *  When @c kind is 'text', this must be 'edited', but generally it can
   *  be any kind of possible change.
   *  Note that 'local' does not always refer to a working copy. A change
   *  can be local to the target branch of a merge operation, for example,
   *  and is not necessarily visible in a working copy of the target branch
   *  at any given revision. */
  svn_wc_conflict_reason_t reason;

  /** If this is text-conflict and involves the merging of two files
   * descended from a common ancestor, here are the paths of up to
   * four fulltext files that can be used to interactively resolve the
   * conflict.
   *
   * @a base_abspath, @a their_abspath and @a my_abspath are absolute
   * paths.
   *
   * ### Is @a merged_file relative to some directory, or absolute?
   *
   * All four files will be in repository-normal form -- LF
   * line endings and contracted keywords.  (If any of these files are
   * not available, they default to NULL.)
   *
   * On the other hand, if this is a property-conflict, then these
   * paths represent temporary files that contain the three different
   * property-values in conflict.  The fourth path (@c merged_file)
   * may or may not be NULL;  if set, it represents libsvn_wc's
   * attempt to merge the property values together.  (Remember that
   * property values are technically binary values, and thus can't
   * always be merged.)
   */
  const char *base_abspath;  /* common ancestor of the two files being merged */

  /** their version of the file */
  /* ### BH: For properties this field contains the reference to
             the property rejection (.prej) file */
  const char *their_abspath;

  /** my locally-edited version of the file */
  const char *my_abspath;

  /** merged version; may contain conflict markers
   * ### For property conflicts, this contains 'their_abspath'. */
  const char *merged_file;

  /** The operation that exposed the conflict.
   * Used only for tree conflicts.
   */
  svn_wc_operation_t operation;

  /** Info on the "merge-left source" or "older" version of incoming change. */
  const svn_wc_conflict_version_t *src_left_version;

  /** Info on the "merge-right source" or "their" version of incoming change. */
  const svn_wc_conflict_version_t *src_right_version;

  /** For property conflicts, the absolute path to the .prej file.
   * @since New in 1.9. */
  const char *prop_reject_abspath;

  /** For property conflicts, the local base value of the property, i.e. the
   * value of the property as of the BASE revision of the working copy.
   * For conflicts created during update/switch this contains the
   * post-update/switch property value. The pre-update/switch value can
   * be found in prop_value_incoming_old.
   * Only set if available, so might be @c NULL.
   * @since New in 1.9. */
  const svn_string_t *prop_value_base;

  /** For property conflicts, the local working value of the property,
   * i.e. the value of the property in the working copy, possibly with
   * local modiciations.
   * Only set if available, so might be @c NULL.
   * @since New in 1.9. */
  const svn_string_t *prop_value_working;

  /** For property conflicts, the incoming old value of the property,
   * i.e. the value the property had at @c src_left_version.
   * Only set if available, so might be @c NULL.
   * @since New in 1.9 */
  const svn_string_t *prop_value_incoming_old;

  /** For property conflicts, the incoming new value of the property,
   * i.e. the value the property had at @c src_right_version.
   * Only set if available, so might be @c NULL.
   * @since New in 1.9 */
  const svn_string_t *prop_value_incoming_new;

/* NOTE: Add new fields at the end to preserve binary compatibility.
     Also, if you add fields here, you have to update
     svn_wc_conflict_description2_dup and perhaps
     svn_wc_conflict_description_create_text2,
     svn_wc_conflict_description_create_prop2, and
     svn_wc_conflict_description_create_tree2. */
} svn_wc_conflict_description2_t;


/** Similar to #svn_wc_conflict_description2_t, but with relative paths and
 * adm_access batons.  Passed to #svn_wc_conflict_resolver_func_t.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef struct svn_wc_conflict_description_t
{
  /** The path that is in conflict (for a tree conflict, it is the victim) */
  const char *path;

  /** The local node type of the path being operated on (for a tree conflict,
   *  this specifies the local node kind, which may be (and typically is)
   *  different than the left and right kind) */
  svn_node_kind_t node_kind;

  /** What sort of conflict are we describing? */
  svn_wc_conflict_kind_t kind;

  /** The name of the property whose conflict is being described.
   *  (Only if @a kind is 'property'; else undefined.) */
  const char *property_name;

  /** Whether svn thinks ('my' version of) @c path is a 'binary' file.
   *  (Only if @c kind is 'text', else undefined.) */
  svn_boolean_t is_binary;

  /** The svn:mime-type property of ('my' version of) @c path, if available,
   *  else NULL.
   *  (Only if @c kind is 'text', else undefined.) */
  const char *mime_type;

  /** If not NULL, an open working copy access baton to either the
   *  path itself (if @c path is a directory), or to the parent
   *  directory (if @c path is a file.)
   *  For a tree conflict, this will always be an access baton
   *  to the parent directory of the path, even if the path is
   *  a directory. */
  svn_wc_adm_access_t *access;

  /** The action being attempted on the conflicted node or property.
   *  (When @c kind is 'text', this action must be 'edit'.) */
  svn_wc_conflict_action_t action;

  /** The state of the target node or property, relative to its merge-left
   *  source, that is the reason for the conflict.
   *  (When @c kind is 'text', this reason must be 'edited'.) */
  svn_wc_conflict_reason_t reason;

  /** If this is text-conflict and involves the merging of two files
   * descended from a common ancestor, here are the paths of up to
   * four fulltext files that can be used to interactively resolve the
   * conflict.  All four files will be in repository-normal form -- LF
   * line endings and contracted keywords.  (If any of these files are
   * not available, they default to NULL.)
   *
   * On the other hand, if this is a property-conflict, then these
   * paths represent temporary files that contain the three different
   * property-values in conflict.  The fourth path (@c merged_file)
   * may or may not be NULL;  if set, it represents libsvn_wc's
   * attempt to merge the property values together.  (Remember that
   * property values are technically binary values, and thus can't
   * always be merged.)
   */
  const char *base_file;     /* common ancestor of the two files being merged */

  /** their version of the file */
  const char *their_file;

  /** my locally-edited version of the file */
  const char *my_file;

  /** merged version; may contain conflict markers */
  const char *merged_file;

  /** The operation that exposed the conflict.
   * Used only for tree conflicts.
   *
   * @since New in 1.6.
   */
  svn_wc_operation_t operation;

  /** Info on the "merge-left source" or "older" version of incoming change.
   * @since New in 1.6. */
  svn_wc_conflict_version_t *src_left_version;

  /** Info on the "merge-right source" or "their" version of incoming change.
   * @since New in 1.6. */
  svn_wc_conflict_version_t *src_right_version;

} svn_wc_conflict_description_t;

/**
 * Allocate an #svn_wc_conflict_description2_t structure in @a result_pool,
 * initialize to represent a text conflict, and return it.
 *
 * Set the @c local_abspath field of the created struct to @a local_abspath
 * (which must be an absolute path), the @c kind field to
 * #svn_wc_conflict_kind_text, the @c node_kind to #svn_node_file,
 * the @c action to #svn_wc_conflict_action_edit, and the @c reason to
 * #svn_wc_conflict_reason_edited.
 *
 * @note It is the caller's responsibility to set the other required fields
 * (such as the four file names and @c mime_type and @c is_binary).
 *
 * @since New in 1.7.
 */
svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_text2(const char *local_abspath,
                                         apr_pool_t *result_pool);


/** Similar to svn_wc_conflict_description_create_text2(), but returns
 * a #svn_wc_conflict_description_t *.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_conflict_description_t *
svn_wc_conflict_description_create_text(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        apr_pool_t *pool);

/**
 * Allocate an #svn_wc_conflict_description2_t structure in @a result_pool,
 * initialize to represent a property conflict, and return it.
 *
 * Set the @c local_abspath field of the created struct to @a local_abspath
 * (which must be an absolute path), the @c kind field
 * to #svn_wc_conflict_kind_property, the @c node_kind to @a node_kind, and
 * the @c property_name to @a property_name.
 *
 * @note: It is the caller's responsibility to set the other required fields
 * (such as the four file names and @c action and @c reason).
 *
 * @since New in 1.7.
 */
svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_prop2(const char *local_abspath,
                                         svn_node_kind_t node_kind,
                                         const char *property_name,
                                         apr_pool_t *result_pool);

/** Similar to svn_wc_conflict_descriptor_create_prop(), but returns
 * a #svn_wc_conflict_description_t *.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_conflict_description_t *
svn_wc_conflict_description_create_prop(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        svn_node_kind_t node_kind,
                                        const char *property_name,
                                        apr_pool_t *pool);

/**
 * Allocate an #svn_wc_conflict_description2_t structure in @a pool,
 * initialize to represent a tree conflict, and return it.
 *
 * Set the @c local_abspath field of the created struct to @a local_abspath
 * (which must be an absolute path), the @c kind field to
 * #svn_wc_conflict_kind_tree, the @c node_kind to @a node_kind,
 * the @c operation to @a operation, the @c src_left_version field to
 * @a src_left_version, and the @c src_right_version field to
 * @a src_right_version.
 *
 * @note: It is the caller's responsibility to set the other required fields
 * (such as the four file names and @c action and @c reason).
 *
 * @since New in 1.7.
 */
svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_tree2(
  const char *local_abspath,
  svn_node_kind_t node_kind,
  svn_wc_operation_t operation,
  const svn_wc_conflict_version_t *src_left_version,
  const svn_wc_conflict_version_t *src_right_version,
  apr_pool_t *result_pool);


/** Similar to svn_wc_conflict_description_create_tree(), but returns
 * a #svn_wc_conflict_description_t *.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_conflict_description_t *
svn_wc_conflict_description_create_tree(
  const char *path,
  svn_wc_adm_access_t *adm_access,
  svn_node_kind_t node_kind,
  svn_wc_operation_t operation,
  /* non-const */ svn_wc_conflict_version_t *src_left_version,
  /* non-const */ svn_wc_conflict_version_t *src_right_version,
  apr_pool_t *pool);


/** Return a duplicate of @a conflict, allocated in @a result_pool.
 * A deep copy of all members will be made.
 *
 * @since New in 1.9.
 */
svn_wc_conflict_description2_t *
svn_wc_conflict_description2_dup(
  const svn_wc_conflict_description2_t *conflict,
  apr_pool_t *result_pool);


/** Like svn_wc_conflict_description2_dup(), but is improperly named
 * as a private function when it is intended to be a public API.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_wc_conflict_description2_t *
svn_wc__conflict_description2_dup(
  const svn_wc_conflict_description2_t *conflict,
  apr_pool_t *result_pool);


/** The way in which the conflict callback chooses a course of action.
 *
 * @since New in 1.5.
 */
typedef enum svn_wc_conflict_choice_t
{
  /** Undefined; for private use only.
      This value must never be returned in svn_wc_conflict_result_t,
      but a separate value, unequal to all other pre-defined values may
      be useful in conflict resolver implementations to signal that no
      choice is made yet.
   * @since New in 1.9
   */
  svn_wc_conflict_choose_undefined = -1,

  /** Don't resolve the conflict now.  Let libsvn_wc mark the path
     'conflicted', so user can run 'svn resolved' later. */
  svn_wc_conflict_choose_postpone = 0,

  /** If there were files to choose from, select one as a way of
     resolving the conflict here and now.  libsvn_wc will then do the
     work of "installing" the chosen file.
  */
  svn_wc_conflict_choose_base,            /**< original version */
  svn_wc_conflict_choose_theirs_full,     /**< incoming version */
  svn_wc_conflict_choose_mine_full,       /**< own version */
  svn_wc_conflict_choose_theirs_conflict, /**< incoming (for conflicted hunks) */
  svn_wc_conflict_choose_mine_conflict,   /**< own (for conflicted hunks) */
  svn_wc_conflict_choose_merged,          /**< merged version */

  /** @since New in 1.8. */
  svn_wc_conflict_choose_unspecified      /**< undecided */

} svn_wc_conflict_choice_t;


/** The final result returned by #svn_wc_conflict_resolver_func_t.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type.  Instead,
 * construct this structure using svn_wc_create_conflict_result()
 * below.
 *
 * @since New in 1.5.
 */
typedef struct svn_wc_conflict_result_t
{
  /** A choice to either delay the conflict resolution or select a
      particular file to resolve the conflict. */
  svn_wc_conflict_choice_t choice;

  /** If not NULL, this is a path to a file which contains the client's
      (or more likely, the user's) merging of the three values in
      conflict.  libsvn_wc accepts this file if (and only if) @c choice
      is set to #svn_wc_conflict_choose_merged.*/
  const char *merged_file;

  /** If true, save a backup copy of merged_file (or the original
      merged_file from the conflict description, if merged_file is
      NULL) in the user's working copy. */
  svn_boolean_t save_merged;

  /** If not NULL, this is the new merged property, used when choosing
   * #svn_wc_conflict_choose_merged. This value is prefered over using
   * merged_file.
   *
   * @since New in 1.9.
   */
  const svn_string_t *merged_value;

} svn_wc_conflict_result_t;


/**
 * Allocate an #svn_wc_conflict_result_t structure in @a pool,
 * initialize and return it.
 *
 * Set the @c choice field of the structure to @a choice, @c merged_file
 * to @a merged_file, and @c save_merged to false.  Make only a shallow
 * copy of the pointer argument @a merged_file. @a merged_file may be
 * NULL if setting merged_file is not needed.
 *
 * @since New in 1.5.
 */
svn_wc_conflict_result_t *
svn_wc_create_conflict_result(svn_wc_conflict_choice_t choice,
                              const char *merged_file,
                              apr_pool_t *pool);


/** A callback used in merge, update and switch for resolving conflicts
 * during the application of a tree delta to a working copy.
 *
 * @a description describes the exact nature of the conflict, and
 * provides information to help resolve it.  @a baton is a closure
 * object; it should be provided by the implementation, and passed by
 * the caller.  When finished, the callback signals its resolution by
 * returning a structure in @a *result, which should be allocated in
 * @a result_pool.  (See #svn_wc_conflict_result_t.)  @a scratch_pool
 * should be used for any temporary allocations.
 *
 * The values #svn_wc_conflict_choose_mine_conflict and
 * #svn_wc_conflict_choose_theirs_conflict are not legal for conflicts
 * in binary files or binary properties.
 *
 * Implementations of this callback are free to present the conflict
 * using any user interface.  This may include simple contextual
 * conflicts in a file's text or properties, or more complex
 * 'tree'-based conflicts related to obstructed additions, deletions,
 * and edits.  The callback implementation is free to decide which
 * sorts of conflicts to handle; it's also free to decide which types
 * of conflicts are automatically resolvable and which require user
 * interaction.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_wc_conflict_resolver_func2_t)(
  svn_wc_conflict_result_t **result,
  const svn_wc_conflict_description2_t *description,
  void *baton,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);


/** Similar to #svn_wc_conflict_resolver_func2_t, but using
 * #svn_wc_conflict_description_t instead of
 * #svn_wc_conflict_description2_t
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef svn_error_t *(*svn_wc_conflict_resolver_func_t)(
  svn_wc_conflict_result_t **result,
  const svn_wc_conflict_description_t *description,
  void *baton,
  apr_pool_t *pool);

/** @} */



/**
 * A callback vtable invoked by our diff-editors, as they receive diffs
 * from the server. 'svn diff' and 'svn merge' implement their own versions
 * of this vtable.
 *
 * Common parameters:
 *
 * If @a state is non-NULL, set @a *state to the state of the item
 * after the operation has been performed.  (In practice, this is only
 * useful with merge, not diff; diff callbacks will probably set
 * @a *state to #svn_wc_notify_state_unknown, since they do not change
 * the state and therefore do not bother to know the state after the
 * operation.)  By default, @a state refers to the item's content
 * state.  Functions concerned with property state have separate
 * @a contentstate and @a propstate arguments.
 *
 * If @a tree_conflicted is non-NULL, set @a *tree_conflicted to true if
 * this operation caused a tree conflict, else to false. (Like with @a
 * state, this is only useful with merge, not diff; diff callbacks
 * should set this to false.)
 *
 * @since New in 1.7.
 */
typedef struct svn_wc_diff_callbacks4_t
{
  /**
   * This function is called before @a file_changed to allow callbacks to
   * skip the most expensive processing of retrieving the file data.
   *
   */
  svn_error_t *(*file_opened)(svn_boolean_t *tree_conflicted,
                              svn_boolean_t *skip,
                              const char *path,
                              svn_revnum_t rev,
                              void *diff_baton,
                              apr_pool_t *scratch_pool);

  /**
   * A file @a path has changed.  If @a tmpfile2 is non-NULL, the
   * contents have changed and those changes can be seen by comparing
   * @a tmpfile1 and @a tmpfile2, which represent @a rev1 and @a rev2 of
   * the file, respectively.
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   *
   * @a propchanges is an array of (#svn_prop_t) structures. If it contains
   * any elements, the original list of properties is provided in
   * @a originalprops, which is a hash of #svn_string_t values, keyed on the
   * property name.
   *
   */
  svn_error_t *(*file_changed)(svn_wc_notify_state_t *contentstate,
                               svn_wc_notify_state_t *propstate,
                               svn_boolean_t *tree_conflicted,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               svn_revnum_t rev1,
                               svn_revnum_t rev2,
                               const char *mimetype1,
                               const char *mimetype2,
                               const apr_array_header_t *propchanges,
                               apr_hash_t *originalprops,
                               void *diff_baton,
                               apr_pool_t *scratch_pool);

  /**
   * A file @a path was added.  The contents can be seen by comparing
   * @a tmpfile1 and @a tmpfile2, which represent @a rev1 and @a rev2
   * of the file, respectively.  (If either file is empty, the rev
   * will be 0.)
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   *
   * @a propchanges is an array of (#svn_prop_t) structures.  If it contains
   * any elements, the original list of properties is provided in
   * @a originalprops, which is a hash of #svn_string_t values, keyed on the
   * property name.
   * If @a copyfrom_path is non-@c NULL, this add has history (i.e., is a
   * copy), and the origin of the copy may be recorded as
   * @a copyfrom_path under @a copyfrom_revision.
   */
  svn_error_t *(*file_added)(svn_wc_notify_state_t *contentstate,
                             svn_wc_notify_state_t *propstate,
                             svn_boolean_t *tree_conflicted,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             svn_revnum_t rev1,
                             svn_revnum_t rev2,
                             const char *mimetype1,
                             const char *mimetype2,
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_revision,
                             const apr_array_header_t *propchanges,
                             apr_hash_t *originalprops,
                             void *diff_baton,
                             apr_pool_t *scratch_pool);

  /**
   * A file @a path was deleted.  The [loss of] contents can be seen by
   * comparing @a tmpfile1 and @a tmpfile2.  @a originalprops provides
   * the properties of the file.
   * ### Some existing callers include WC "entry props" in @a originalprops.
   *
   * If known, the @c svn:mime-type value of each file is passed into
   * @a mimetype1 and @a mimetype2;  either or both of the values can
   * be NULL.  The implementor can use this information to decide if
   * (or how) to generate differences.
   */
  svn_error_t *(*file_deleted)(svn_wc_notify_state_t *state,
                               svn_boolean_t *tree_conflicted,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               const char *mimetype1,
                               const char *mimetype2,
                               apr_hash_t *originalprops,
                               void *diff_baton,
                               apr_pool_t *scratch_pool);

  /**
   * A directory @a path was deleted.
   */
  svn_error_t *(*dir_deleted)(svn_wc_notify_state_t *state,
                              svn_boolean_t *tree_conflicted,
                              const char *path,
                              void *diff_baton,
                              apr_pool_t *scratch_pool);
  /**
   * A directory @a path has been opened.  @a rev is the revision that the
   * directory came from.
   *
   * This function is called for any existing directory @a path before any
   * of the callbacks are called for a child of @a path.
   *
   * If the callback returns @c TRUE in @a *skip_children, children
   * of this directory will be skipped.
   */
  svn_error_t *(*dir_opened)(svn_boolean_t *tree_conflicted,
                             svn_boolean_t *skip,
                             svn_boolean_t *skip_children,
                             const char *path,
                             svn_revnum_t rev,
                             void *diff_baton,
                             apr_pool_t *scratch_pool);

  /**
   * A directory @a path was added.  @a rev is the revision that the
   * directory came from.
   *
   * This function is called for any new directory @a path before any
   * of the callbacks are called for a child of @a path.
   *
   * If @a copyfrom_path is non-@c NULL, this add has history (i.e., is a
   * copy), and the origin of the copy may be recorded as
   * @a copyfrom_path under @a copyfrom_revision.
   */
  svn_error_t *(*dir_added)(svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            svn_boolean_t *skip,
                            svn_boolean_t *skip_children,
                            const char *path,
                            svn_revnum_t rev,
                            const char *copyfrom_path,
                            svn_revnum_t copyfrom_revision,
                            void *diff_baton,
                            apr_pool_t *scratch_pool);

  /**
   * A list of property changes (@a propchanges) was applied to the
   * directory @a path.
   *
   * The array is a list of (#svn_prop_t) structures.
   *
   * @a dir_was_added is set to #TRUE if the directory was added, and
   * to #FALSE if the directory pre-existed.
   */
  svn_error_t *(*dir_props_changed)(svn_wc_notify_state_t *propstate,
                                    svn_boolean_t *tree_conflicted,
                                    const char *path,
                                    svn_boolean_t dir_was_added,
                                    const apr_array_header_t *propchanges,
                                    apr_hash_t *original_props,
                                    void *diff_baton,
                                    apr_pool_t *scratch_pool);

  /**
   * A directory @a path which has been opened with @a dir_opened or @a
   * dir_added has been closed.
   *
   * @a dir_was_added is set to #TRUE if the directory was added, and
   * to #FALSE if the directory pre-existed.
   */
  svn_error_t *(*dir_closed)(svn_wc_notify_state_t *contentstate,
                             svn_wc_notify_state_t *propstate,
                             svn_boolean_t *tree_conflicted,
                             const char *path,
                             svn_boolean_t dir_was_added,
                             void *diff_baton,
                             apr_pool_t *scratch_pool);

} svn_wc_diff_callbacks4_t;


/**
 * Similar to #svn_wc_diff_callbacks4_t, but without @a copyfrom_path and
 * @a copyfrom_revision arguments to @c file_added and @c dir_added functions.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef struct svn_wc_diff_callbacks3_t
{
  /** The same as #svn_wc_diff_callbacks4_t.file_changed. */
  svn_error_t *(*file_changed)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *contentstate,
                               svn_wc_notify_state_t *propstate,
                               svn_boolean_t *tree_conflicted,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               svn_revnum_t rev1,
                               svn_revnum_t rev2,
                               const char *mimetype1,
                               const char *mimetype2,
                               const apr_array_header_t *propchanges,
                               apr_hash_t *originalprops,
                               void *diff_baton);

  /** Similar to #svn_wc_diff_callbacks4_t.file_added but without
   * @a copyfrom_path and @a copyfrom_revision arguments. */
  svn_error_t *(*file_added)(svn_wc_adm_access_t *adm_access,
                             svn_wc_notify_state_t *contentstate,
                             svn_wc_notify_state_t *propstate,
                             svn_boolean_t *tree_conflicted,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             svn_revnum_t rev1,
                             svn_revnum_t rev2,
                             const char *mimetype1,
                             const char *mimetype2,
                             const apr_array_header_t *propchanges,
                             apr_hash_t *originalprops,
                             void *diff_baton);

  /** The same as #svn_wc_diff_callbacks4_t.file_deleted. */
  svn_error_t *(*file_deleted)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *state,
                               svn_boolean_t *tree_conflicted,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               const char *mimetype1,
                               const char *mimetype2,
                               apr_hash_t *originalprops,
                               void *diff_baton);

  /** Similar to #svn_wc_diff_callbacks4_t.dir_added but without
   * @a copyfrom_path and @a copyfrom_revision arguments. */
  svn_error_t *(*dir_added)(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            svn_revnum_t rev,
                            void *diff_baton);

  /** The same as #svn_wc_diff_callbacks4_t.dir_deleted. */
  svn_error_t *(*dir_deleted)(svn_wc_adm_access_t *adm_access,
                              svn_wc_notify_state_t *state,
                              svn_boolean_t *tree_conflicted,
                              const char *path,
                              void *diff_baton);

  /** The same as #svn_wc_diff_callbacks4_t.dir_props_changed. */
  svn_error_t *(*dir_props_changed)(svn_wc_adm_access_t *adm_access,
                                    svn_wc_notify_state_t *propstate,
                                    svn_boolean_t *tree_conflicted,
                                    const char *path,
                                    const apr_array_header_t *propchanges,
                                    apr_hash_t *original_props,
                                    void *diff_baton);

  /** The same as #svn_wc_diff_callbacks4_t.dir_opened. */
  svn_error_t *(*dir_opened)(svn_wc_adm_access_t *adm_access,
                             svn_boolean_t *tree_conflicted,
                             const char *path,
                             svn_revnum_t rev,
                             void *diff_baton);

  /** The same as #svn_wc_diff_callbacks4_t.dir_closed. */
  svn_error_t *(*dir_closed)(svn_wc_adm_access_t *adm_access,
                             svn_wc_notify_state_t *contentstate,
                             svn_wc_notify_state_t *propstate,
                             svn_boolean_t *tree_conflicted,
                             const char *path,
                             void *diff_baton);

} svn_wc_diff_callbacks3_t;

/**
 * Similar to #svn_wc_diff_callbacks3_t, but without the @c dir_opened
 * and @c dir_closed functions, and without the @a tree_conflicted argument
 * to the functions.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef struct svn_wc_diff_callbacks2_t
{
  /** The same as @c file_changed in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*file_changed)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *contentstate,
                               svn_wc_notify_state_t *propstate,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               svn_revnum_t rev1,
                               svn_revnum_t rev2,
                               const char *mimetype1,
                               const char *mimetype2,
                               const apr_array_header_t *propchanges,
                               apr_hash_t *originalprops,
                               void *diff_baton);

  /** The same as @c file_added in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*file_added)(svn_wc_adm_access_t *adm_access,
                             svn_wc_notify_state_t *contentstate,
                             svn_wc_notify_state_t *propstate,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             svn_revnum_t rev1,
                             svn_revnum_t rev2,
                             const char *mimetype1,
                             const char *mimetype2,
                             const apr_array_header_t *propchanges,
                             apr_hash_t *originalprops,
                             void *diff_baton);

  /** The same as @c file_deleted in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*file_deleted)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *state,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               const char *mimetype1,
                               const char *mimetype2,
                               apr_hash_t *originalprops,
                               void *diff_baton);

  /** The same as @c dir_added in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*dir_added)(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            const char *path,
                            svn_revnum_t rev,
                            void *diff_baton);

  /** The same as @c dir_deleted in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*dir_deleted)(svn_wc_adm_access_t *adm_access,
                              svn_wc_notify_state_t *state,
                              const char *path,
                              void *diff_baton);

  /** The same as @c dir_props_changed in #svn_wc_diff_callbacks3_t. */
  svn_error_t *(*dir_props_changed)(svn_wc_adm_access_t *adm_access,
                                    svn_wc_notify_state_t *state,
                                    const char *path,
                                    const apr_array_header_t *propchanges,
                                    apr_hash_t *original_props,
                                    void *diff_baton);

} svn_wc_diff_callbacks2_t;

/**
 * Similar to #svn_wc_diff_callbacks2_t, but with file additions/content
 * changes and property changes split into different functions.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef struct svn_wc_diff_callbacks_t
{
  /** Similar to @c file_changed in #svn_wc_diff_callbacks2_t, but without
   * property change information.  @a tmpfile2 is never NULL. @a state applies
   * to the file contents. */
  svn_error_t *(*file_changed)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *state,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               svn_revnum_t rev1,
                               svn_revnum_t rev2,
                               const char *mimetype1,
                               const char *mimetype2,
                               void *diff_baton);

  /** Similar to @c file_added in #svn_wc_diff_callbacks2_t, but without
   * property change information.  @a *state applies to the file contents. */
  svn_error_t *(*file_added)(svn_wc_adm_access_t *adm_access,
                             svn_wc_notify_state_t *state,
                             const char *path,
                             const char *tmpfile1,
                             const char *tmpfile2,
                             svn_revnum_t rev1,
                             svn_revnum_t rev2,
                             const char *mimetype1,
                             const char *mimetype2,
                             void *diff_baton);

  /** Similar to @c file_deleted in #svn_wc_diff_callbacks2_t, but without
   * the properties. */
  svn_error_t *(*file_deleted)(svn_wc_adm_access_t *adm_access,
                               svn_wc_notify_state_t *state,
                               const char *path,
                               const char *tmpfile1,
                               const char *tmpfile2,
                               const char *mimetype1,
                               const char *mimetype2,
                               void *diff_baton);

  /** The same as @c dir_added in #svn_wc_diff_callbacks2_t. */
  svn_error_t *(*dir_added)(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            const char *path,
                            svn_revnum_t rev,
                            void *diff_baton);

  /** The same as @c dir_deleted in #svn_wc_diff_callbacks2_t. */
  svn_error_t *(*dir_deleted)(svn_wc_adm_access_t *adm_access,
                              svn_wc_notify_state_t *state,
                              const char *path,
                              void *diff_baton);

  /** Similar to @c dir_props_changed in #svn_wc_diff_callbacks2_t, but this
   * function is called for files as well as directories. */
  svn_error_t *(*props_changed)(svn_wc_adm_access_t *adm_access,
                                svn_wc_notify_state_t *state,
                                const char *path,
                                const apr_array_header_t *propchanges,
                                apr_hash_t *original_props,
                                void *diff_baton);

} svn_wc_diff_callbacks_t;


/* Asking questions about a working copy. */

/** Set @a *wc_format to @a local_abspath's working copy format version
 * number if @a local_abspath is a valid working copy directory, else set it
 * to 0.
 *
 * Return error @c APR_ENOENT if @a local_abspath does not exist at all.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_check_wc2(int *wc_format,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_check_wc2(), but with a relative path and no supplied
 * working copy context.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_check_wc(const char *path,
                int *wc_format,
                apr_pool_t *pool);


/** Set @a *has_binary_prop to @c TRUE iff @a path has been marked
 * with a property indicating that it is non-text (in other words, binary).
 * @a adm_access is an access baton set that contains @a path.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API. As a
 * replacement for this functionality, @see svn_mime_type_is_binary and
 * #SVN_PROP_MIME_TYPE.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_has_binary_prop(svn_boolean_t *has_binary_prop,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool);


/* Detecting modification. */

/** Set @a *modified_p to non-zero if @a local_abspath's text is modified
 * with regard to the base revision, else set @a *modified_p to zero.
 * @a local_abspath is the absolute path to the file.
 *
 * This function uses some heuristics to avoid byte-by-byte comparisons
 * against the base text (eg. file size and its modification time).
 *
 * If @a local_abspath does not exist, consider it unmodified.  If it exists
 * but is not under revision control (not even scheduled for
 * addition), return the error #SVN_ERR_ENTRY_NOT_FOUND.
 *
 * @a unused is ignored.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_text_modified_p2(svn_boolean_t *modified_p,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t unused,
                        apr_pool_t *scratch_pool);

/** Similar to svn_wc_text_modified_p2(), but with a relative path and
 * adm_access baton?
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_text_modified_p(svn_boolean_t *modified_p,
                       const char *filename,
                       svn_boolean_t force_comparison,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool);

/** Set @a *modified_p to non-zero if @a path's properties are modified
 * with regard to the base revision, else set @a modified_p to zero.
 * @a adm_access must be an access baton for @a path.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_props_modified_p2(svn_boolean_t *modified_p,
                         svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool);

/** Similar to svn_wc_props_modified_p2(), but with a relative path and
 * adm_access baton.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_props_modified_p(svn_boolean_t *modified_p,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool);


/**
* @defgroup svn_wc_entries Entries and status (deprecated)
 * @{
 */

/** The schedule states an entry can be in.
 * @deprecated Provided for backward compatibility with the 1.6 API. */
typedef enum svn_wc_schedule_t
{
  /** Nothing special here */
  svn_wc_schedule_normal,

  /** Slated for addition */
  svn_wc_schedule_add,

  /** Slated for deletion */
  svn_wc_schedule_delete,

  /** Slated for replacement (delete + add) */
  svn_wc_schedule_replace

} svn_wc_schedule_t;


/**
 * Values for the working_size field in svn_wc_entry_t
 * when it isn't set to the actual size value of the unchanged
 * working file.
 *
 *  The value of the working size is unknown (hasn't been
 *  calculated and stored in the past for whatever reason).
 *
 * @since New in 1.5
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
#define SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN (-1)

/** A working copy entry -- that is, revision control information about
 * one versioned entity.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
/* SVN_DEPRECATED */
typedef struct svn_wc_entry_t
{
  /* IMPORTANT: If you extend this structure, add new fields to the end. */

  /* General Attributes */

  /** entry's name */
  const char *name;

  /** base revision */
  svn_revnum_t revision;

  /** url in repository */
  const char *url;

  /** canonical repository URL or NULL if not known */
  const char *repos;

  /** repository uuid */
  const char *uuid;

  /** node kind (file, dir, ...) */
  svn_node_kind_t kind;

  /* State information */

  /** scheduling (add, delete, replace ...) */
  svn_wc_schedule_t schedule;

  /** in a copied state (possibly because the entry is a child of a
   *  path that is #svn_wc_schedule_add or #svn_wc_schedule_replace,
   *  when the entry itself is #svn_wc_schedule_normal).
   *  COPIED is true for nodes under a directory that was copied, but
   *  COPYFROM_URL is null there. They are both set for the root
   *  destination of the copy.
   */
  svn_boolean_t copied;

  /** The directory containing this entry had a versioned child of this
   * name, but this entry represents a different revision or a switched
   * path at which no item exists in the repository. This typically
   * arises from committing or updating to a deletion of this entry
   * without committing or updating the parent directory.
   *
   * The schedule can be 'normal' or 'add'. */
  svn_boolean_t deleted;

  /** absent -- we know an entry of this name exists, but that's all
      (usually this happens because of authz restrictions)  */
  svn_boolean_t absent;

  /** for THIS_DIR entry, implies whole entries file is incomplete */
  svn_boolean_t incomplete;

  /** copyfrom location */
  const char *copyfrom_url;

  /** copyfrom revision */
  svn_revnum_t copyfrom_rev;

  /** old version of conflicted file. A file basename, relative to the
   * user's directory that the THIS_DIR entry refers to. */
  const char *conflict_old;

  /** new version of conflicted file. A file basename, relative to the
   * user's directory that the THIS_DIR entry refers to. */
  const char *conflict_new;

  /** working version of conflicted file. A file basename, relative to the
   * user's directory that the THIS_DIR entry refers to. */
  const char *conflict_wrk;

  /** property reject file. A file basename, relative to the user's
   * directory that the THIS_DIR entry refers to. */
  const char *prejfile;

  /** last up-to-date time for text contents (0 means no information available)
   */
  apr_time_t text_time;

  /** last up-to-date time for properties (0 means no information available)
   *
   * @deprecated This value will always be 0 in version 1.4 and later.
   */
  apr_time_t prop_time;

  /** Hex MD5 checksum for the untranslated text base file,
   * can be @c NULL for backwards compatibility.
   */
  const char *checksum;

  /* "Entry props" */

  /** last revision this was changed */
  svn_revnum_t cmt_rev;

  /** last date this was changed */
  apr_time_t cmt_date;

  /** last commit author of this item */
  const char *cmt_author;

  /** lock token or NULL if path not locked in this WC
   * @since New in 1.2.
   */
  const char *lock_token;

  /** lock owner, or NULL if not locked in this WC
   * @since New in 1.2.
   */
  const char *lock_owner;

  /** lock comment or NULL if not locked in this WC or no comment
   * @since New in 1.2.
   */
  const char *lock_comment;

  /** Lock creation date or 0 if not locked in this WC
   * @since New in 1.2.
   */
  apr_time_t lock_creation_date;

  /** Whether this entry has any working properties.
   * False if this information is not stored in the entry.
   *
   * @since New in 1.4. */
  svn_boolean_t has_props;

  /** Whether this entry has property modifications.
   *
   * @note For working copies in older formats, this flag is not valid.
   *
   * @see svn_wc_props_modified_p().
   *
   * @since New in 1.4. */
  svn_boolean_t has_prop_mods;

  /** A space-separated list of all properties whose presence/absence is cached
   * in this entry.
   *
   * @see @c present_props.
   *
   * @since New in 1.4.
   * @deprecated This value will always be "" in version 1.7 and later. */
  const char *cachable_props;

  /** Cached property existence for this entry.
   * This is a space-separated list of property names.  If a name exists in
   * @c cachable_props but not in this list, this entry does not have that
   * property.  If a name exists in both lists, the property is present on this
   * entry.
   *
   * @since New in 1.4.
   * @deprecated This value will always be "" in version 1.7 and later. */
  const char *present_props;

  /** which changelist this item is part of, or NULL if not part of any.
   * @since New in 1.5.
   */
  const char *changelist;

  /** Size of the file after being translated into local
   * representation, or #SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN if
   * unknown.
   *
   * @since New in 1.5.
   */
  apr_off_t working_size;

  /** Whether a local copy of this entry should be kept in the working copy
   * after a deletion has been committed,  Only valid for the this-dir entry
   * when it is scheduled for deletion.
   *
   * @since New in 1.5. */
  svn_boolean_t keep_local;

  /** The depth of this entry.
   *
   * ### It's a bit annoying that we only use this on this_dir
   * ### entries, yet it will exist (with value svn_depth_infinity) on
   * ### all entries.  Maybe some future extensibility would make this
   * ### field meaningful on entries besides this_dir.
   *
   * @since New in 1.5. */
  svn_depth_t depth;

  /** Serialized data for all of the tree conflicts detected in this_dir.
   *
   * @since New in 1.6. */
  const char *tree_conflict_data;

  /** The entry is a intra-repository file external and this is the
   * repository root relative path to the file specified in the
   * externals definition, otherwise NULL if the entry is not a file
   * external.
   *
   * @since New in 1.6. */
  const char *file_external_path;

  /** The entry is a intra-repository file external and this is the
   * peg revision number specified in the externals definition.  This
   * field is only valid when the file_external_path field is
   * non-NULL.  The only permissible values are
   * svn_opt_revision_unspecified if the entry is not an external,
   * svn_opt_revision_head if the external revision is unspecified or
   * specified with -r HEAD or svn_opt_revision_number for a specific
   * revision number.
   *
   * @since New in 1.6. */
  svn_opt_revision_t file_external_peg_rev;

  /** The entry is an intra-repository file external and this is the
   * operative revision number specified in the externals definition.
   * This field is only valid when the file_external_path field is
   * non-NULL.  The only permissible values are
   * svn_opt_revision_unspecified if the entry is not an external,
   * svn_opt_revision_head if the external revision is unspecified or
   * specified with -r HEAD or svn_opt_revision_number for a specific
   * revision number.
   *
   * @since New in 1.6. */
  svn_opt_revision_t file_external_rev;

  /* IMPORTANT: If you extend this structure, check the following functions in
   * subversion/libsvn_wc/entries.c, to see if you need to extend them as well.
   *
   * svn_wc__atts_to_entry()
   * svn_wc_entry_dup()
   * alloc_entry()
   * read_entry()
   * write_entry()
   * fold_entry()
   */
} svn_wc_entry_t;


/** How an entries file's owner dir is named in the entries file.
 * @deprecated Provided for backward compatibility with the 1.6 API. */
#define SVN_WC_ENTRY_THIS_DIR  ""


/** Set @a *entry to an entry for @a path, allocated in the access baton pool.
 * If @a show_hidden is TRUE, return the entry even if it's in 'excluded',
 * 'deleted' or 'absent' state. Excluded entries are those with their depth
 * set to #svn_depth_exclude. If @a path is not under revision control, or
 * if entry is hidden, not scheduled for re-addition, and @a show_hidden is @c
 * FALSE, then set @a *entry to @c NULL.
 *
 * @a *entry should not be modified, since doing so modifies the entries
 * cache in @a adm_access without changing the entries file on disk.
 *
 * If @a path is not a directory then @a adm_access must be an access baton
 * for the parent directory of @a path.  To avoid needing to know whether
 * @a path is a directory or not, if @a path is a directory @a adm_access
 * can still be an access baton for the parent of @a path so long as the
 * access baton for @a path itself is in the same access baton set.
 *
 * @a path can be relative or absolute but must share the same base used
 * to open @a adm_access.
 *
 * Note that it is possible for @a path to be absent from disk but still
 * under revision control; and conversely, it is possible for @a path to
 * be present, but not under revision control.
 *
 * Use @a pool only for local processing.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_entry(const svn_wc_entry_t **entry,
             const char *path,
             svn_wc_adm_access_t *adm_access,
             svn_boolean_t show_hidden,
             apr_pool_t *pool);


/** Parse the `entries' file for @a adm_access and return a hash @a entries,
 * whose keys are (<tt>const char *</tt>) entry names and values are
 * (<tt>svn_wc_entry_t *</tt>).  The hash @a entries, and its keys and
 * values, are allocated from the pool used to open the @a adm_access
 * baton (that's how the entries caching works).  @a pool is used for
 * transient allocations.
 *
 * Entries that are in a 'excluded', 'deleted' or 'absent' state (and not
 * scheduled for re-addition) are not returned in the hash, unless
 * @a show_hidden is TRUE. Excluded entries are those with their depth set to
 * #svn_depth_exclude.
 *
 * @par Important:
 * The @a entries hash is the entries cache in @a adm_access
 * and so usually the hash itself, the keys and the values should be treated
 * as read-only.  If any of these are modified then it is the caller's
 * responsibility to ensure that the entries file on disk is updated.  Treat
 * the hash values as type (<tt>const svn_wc_entry_t *</tt>) if you wish to
 * avoid accidental modification.  Modifying the schedule member is a
 * particularly bad idea, as the entries writing process relies on having
 * access to the original schedule.  Use a duplicate entry to modify the
 * schedule.
 *
 * @par Important:
 * Only the entry structures representing files and
 * #SVN_WC_ENTRY_THIS_DIR contain complete information.  The entry
 * structures representing subdirs have only the `kind' and `state'
 * fields filled in.  If you want info on a subdir, you must use this
 * routine to open its @a path and read the #SVN_WC_ENTRY_THIS_DIR
 * structure, or call svn_wc_entry() on its @a path.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_entries_read(apr_hash_t **entries,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool);


/** Return a duplicate of @a entry, allocated in @a pool.  No part of the new
 * entry will be shared with @a entry.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_entry_t *
svn_wc_entry_dup(const svn_wc_entry_t *entry,
                 apr_pool_t *pool);

/** @} */


/**
 * This struct contains information about a working copy node.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, users shouldn't allocate structures of this
 * type, to preserve binary compatibility.
 *
 * @since New in 1.7.
 */
typedef struct svn_wc_info_t
{
  /** The schedule of this item
   * ### Do we still need schedule? */
  svn_wc_schedule_t schedule;

  /** If copied, the URL from which the copy was made, else @c NULL. */
  const char *copyfrom_url;

  /** If copied, the revision from which the copy was made,
   * else #SVN_INVALID_REVNUM. */
  svn_revnum_t copyfrom_rev;

  /** The checksum of the node, if it is a file. */
  const svn_checksum_t *checksum;

  /** A changelist the item is in, @c NULL if this node is not in a
   * changelist. */
  const char *changelist;

  /** The depth of the item, see #svn_depth_t */
  svn_depth_t depth;

  /**
   * The size of the file after being translated into its local
   * representation, or #SVN_INVALID_FILESIZE if unknown.
   * Not applicable for directories.
   */
  svn_filesize_t recorded_size;

  /**
   * The time at which the file had the recorded size recorded_size and was
   * considered unmodified. */
  apr_time_t recorded_time;

  /** Array of const svn_wc_conflict_description2_t * which contains info
   * on any conflict of which this node is a victim. Otherwise NULL.  */
  const apr_array_header_t *conflicts;

  /** The local absolute path of the working copy root.  */
  const char *wcroot_abspath;

  /** The path the node was moved from, if it was moved here. Else NULL.
   * @since New in 1.8. */
  const char *moved_from_abspath;

  /** The path the node was moved to, if it was moved away. Else NULL.
   * @since New in 1.8. */
  const char *moved_to_abspath;
} svn_wc_info_t;

/**
 * Return a duplicate of @a info, allocated in @a pool. No part of the new
 * structure will be shared with @a info.
 *
 * @since New in 1.7.
 */
svn_wc_info_t *
svn_wc_info_dup(const svn_wc_info_t *info,
                apr_pool_t *pool);


/** Given @a local_abspath in a dir under version control, decide if it is
 * in a state of conflict; return the answers in @a *text_conflicted_p, @a
 * *prop_conflicted_p, and @a *tree_conflicted_p.  If one or two of the
 * answers are uninteresting, simply pass @c NULL pointers for those.
 *
 * If @a local_abspath is unversioned or does not exist, return
 * #SVN_ERR_WC_PATH_NOT_FOUND.
 *
 * If the @a local_abspath has corresponding text conflict files (with suffix
 * .mine, .theirs, etc.) that cannot be found, assume that the text conflict
 * has been resolved by the user and return @c FALSE in @a
 * *text_conflicted_p.
 *
 * Similarly, if a property conflicts file (.prej suffix) is said to exist,
 * but it cannot be found, assume that the property conflicts have been
 * resolved by the user and return @c FALSE in @a *prop_conflicted_p.
 *
 * @a *tree_conflicted_p can't be auto-resolved in this fashion.  An
 * explicit `resolved' is needed.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_conflicted_p3(svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_boolean_t *tree_conflicted_p,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool);

/** Similar to svn_wc_conflicted_p3(), but with a path/adm_access parameter
 * pair in place of a wc_ctx/local_abspath pair.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_conflicted_p2(svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_boolean_t *tree_conflicted_p,
                     const char *path,
                     svn_wc_adm_access_t *adm_access,
                     apr_pool_t *pool);

/** Given a @a dir_path under version control, decide if one of its entries
 * (@a entry) is in a state of conflict; return the answers in @a
 * text_conflicted_p and @a prop_conflicted_p. These pointers must not be
 * null.
 *
 * If the @a entry mentions that text conflict files (with suffix .mine,
 * .theirs, etc.) exist, but they cannot be found, assume the text conflict
 * has been resolved by the user and return FALSE in @a *text_conflicted_p.
 *
 * Similarly, if the @a entry mentions that a property conflicts file (.prej
 * suffix) exists, but it cannot be found, assume the property conflicts
 * have been resolved by the user and return FALSE in @a *prop_conflicted_p.
 *
 * The @a entry is not updated.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_conflicted_p(svn_boolean_t *text_conflicted_p,
                    svn_boolean_t *prop_conflicted_p,
                    const char *dir_path,
                    const svn_wc_entry_t *entry,
                    apr_pool_t *pool);


/** Set @a *url and @a *rev to the ancestor URL and revision for @a path,
 * allocating in @a pool.  @a adm_access must be an access baton for @a path.
 *
 * If @a url or @a rev is NULL, then ignore it (just don't return the
 * corresponding information).
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_ancestry(char **url,
                    svn_revnum_t *rev,
                    const char *path,
                    svn_wc_adm_access_t *adm_access,
                    apr_pool_t *pool);


/** A callback vtable invoked by the generic entry-walker function.
 * @since New in 1.5.
 */
typedef struct svn_wc_entry_callbacks2_t
{
  /** An @a entry was found at @a path. */
  svn_error_t *(*found_entry)(const char *path,
                              const svn_wc_entry_t *entry,
                              void *walk_baton,
                              apr_pool_t *pool);

  /** Handle the error @a err encountered while processing @a path.
   * Wrap or squelch @a err as desired, and return an #svn_error_t
   * *, or #SVN_NO_ERROR.
   */
  svn_error_t *(*handle_error)(const char *path,
                               svn_error_t *err,
                               void *walk_baton,
                               apr_pool_t *pool);

} svn_wc_entry_callbacks2_t;

/** @deprecated Provided for backward compatibility with the 1.4 API. */
typedef struct svn_wc_entry_callbacks_t
{
  /** An @a entry was found at @a path. */
  svn_error_t *(*found_entry)(const char *path,
                              const svn_wc_entry_t *entry,
                              void *walk_baton,
                              apr_pool_t *pool);

} svn_wc_entry_callbacks_t;

/**
 * A generic entry-walker.
 *
 * Do a potentially recursive depth-first entry-walk beginning on
 * @a path, which can be a file or dir.  Call callbacks in
 * @a walk_callbacks, passing @a walk_baton to each.  Use @a pool for
 * looping, recursion, and to allocate all entries returned.
 * @a adm_access must be an access baton for @a path.  The pool
 * passed to @a walk_callbacks is a temporary subpool of @a pool.
 *
 * If @a depth is #svn_depth_empty, invoke the callbacks on @a path
 * and return without recursing further.  If #svn_depth_files, do
 * the same and invoke the callbacks on file children (if any) of
 * @a path, then return.  If #svn_depth_immediates, do the preceding
 * but also invoke callbacks on immediate subdirectories, then return.
 * If #svn_depth_infinity, recurse fully starting from @a path.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * Like our other entries interfaces, entries that are in a 'excluded',
 * 'deleted' or 'absent' state (and not scheduled for re-addition) are not
 * discovered, unless @a show_hidden is TRUE. Excluded entries are those with
 * their depth set to #svn_depth_exclude.
 *
 * When a new directory is entered, #SVN_WC_ENTRY_THIS_DIR will always
 * be returned first.
 *
 * @note Callers should be aware that each directory will be
 * returned *twice*:  first as an entry within its parent, and
 * subsequently as the '.' entry within itself.  The two calls can be
 * distinguished by looking for #SVN_WC_ENTRY_THIS_DIR in the 'name'
 * field of the entry.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_walk_entries3(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks2_t *walk_callbacks,
                     void *walk_baton,
                     svn_depth_t depth,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool);

/**
 * Similar to svn_wc_walk_entries3(), but without cancellation support
 * or error handling from @a walk_callbacks, and with @a depth always
 * set to #svn_depth_infinity.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_walk_entries2(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks_t *walk_callbacks,
                     void *walk_baton,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool);

/**
 * Similar to svn_wc_walk_entries2(), but without cancellation support.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_walk_entries(const char *path,
                    svn_wc_adm_access_t *adm_access,
                    const svn_wc_entry_callbacks_t *walk_callbacks,
                    void *walk_baton,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool);


/** Mark missing @a path as 'deleted' in its @a parent's list of
 * entries.  @a path should be a directory that is both deleted (via
 * svn_wc_delete4) and removed (via a system call).  This function
 * should only be called during post-commit processing following a
 * successful commit editor drive.
 *
 * Return #SVN_ERR_WC_PATH_FOUND if @a path isn't actually missing.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_mark_missing_deleted(const char *path,
                            svn_wc_adm_access_t *parent,
                            apr_pool_t *pool);


/** Ensure that an administrative area exists for @a local_abspath, so
 * that @a local_abspath is a working copy subdir based on @a url at @a
 * revision, with depth @a depth, and with repository UUID @a repos_uuid
 * and repository root URL @a repos_root_url.
 *
 * @a depth must be a definite depth, it cannot be #svn_depth_unknown.
 * @a repos_uuid and @a repos_root_url MUST NOT be @c NULL, and
 * @a repos_root_url must be a prefix of @a url.
 *
 * If the administrative area does not exist, then create it and
 * initialize it to an unlocked state.
 *
 * If the administrative area already exists then the given @a url
 * must match the URL in the administrative area and the given
 * @a revision must match the BASE of the working copy dir unless
 * the admin directory is scheduled for deletion or the
 * #SVN_ERR_WC_OBSTRUCTED_UPDATE error will be returned.
 *
 * Do not ensure existence of @a local_abspath itself; if @a local_abspath
 * does not exist, return error.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_ensure_adm4(svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   const char *url,
                   const char *repos_root_url,
                   const char *repos_uuid,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_ensure_adm4(), but without the wc context parameter.
 *
 * @note the @a uuid and @a repos parameters were documented as allowing
 * @c NULL to be passed. Beginning with 1.7, this will return an error,
 * contrary to prior documented behavior: see 'notes/api-errata/1.7/wc005.txt'.
 *
 * @since New in 1.5.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_ensure_adm3(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   apr_pool_t *pool);


/**
 * Similar to svn_wc_ensure_adm3(), but with @a depth set to
 * #svn_depth_infinity.
 *
 * See the note on svn_wc_ensure_adm3() regarding the @a repos and @a uuid
 * parameters.
 *
 * @since New in 1.3.
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_ensure_adm2(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   apr_pool_t *pool);


/**
 * Similar to svn_wc_ensure_adm2(), but with @a repos set to @c NULL.
 *
 * @note as of 1.7, this function always returns #SVN_ERR_BAD_URL since
 * the @a repos parameter may not be @c NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_ensure_adm(const char *path,
                  const char *uuid,
                  const char *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool);


/** Set the repository root URL of @a path to @a repos, if possible.
 *
 * Before Subversion 1.7 there could be working copy directories that
 * didn't have a stored repository root in some specific circumstances.
 * This function allowed setting this root later.
 *
 * Since Subversion 1.7 this function just returns #SVN_NO_ERROR.
 *
 * @since New in 1.3.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_maybe_set_repos_root(svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *repos,
                            apr_pool_t *pool);


/**
 * @defgroup svn_wc_status Working copy status.
 * @{
 *
 * We have three functions for getting working copy status: one function
 * for getting the status of exactly one thing, another for
 * getting the statuses of (potentially) multiple things and a third for
 * getting the working copy out-of-dateness with respect to the repository.
 *
 * Why do we have two different functions for getting working copy status?
 * The concept of depth, as explained in the documentation for
 * svn_depth_t, may be useful in understanding this.  Suppose we're
 * getting the status of directory D:
 *
 * To offer all three levels, we could have one unified function,
 * taking a `depth' parameter.  Unfortunately, because this function
 * would have to handle multiple return values as well as the single
 * return value case, getting the status of just one entity would
 * become cumbersome: you'd have to roll through a hash to find one
 * lone status.
 *
 * So we have svn_wc_status3() for depth-empty (just D itself), and
 * svn_wc_walk_status() for depth-immediates and depth-infinity,
 * since the latter two involve multiple return values. And for
 * out-of-dateness information we have svn_wc_get_status_editor5().
 */

/** The type of status for the working copy. */
enum svn_wc_status_kind
{
    /** does not exist */
    svn_wc_status_none = 1,

    /** is not a versioned thing in this wc */
    svn_wc_status_unversioned,

    /** exists, but uninteresting */
    svn_wc_status_normal,

    /** is scheduled for addition */
    svn_wc_status_added,

    /** under v.c., but is missing */
    svn_wc_status_missing,

    /** scheduled for deletion */
    svn_wc_status_deleted,

    /** was deleted and then re-added */
    svn_wc_status_replaced,

    /** text or props have been modified */
    svn_wc_status_modified,

    /** local mods received repos mods (### unused) */
    svn_wc_status_merged,

    /** local mods received conflicting repos mods */
    svn_wc_status_conflicted,

    /** is unversioned but configured to be ignored */
    svn_wc_status_ignored,

    /** an unversioned resource is in the way of the versioned resource */
    svn_wc_status_obstructed,

    /** an unversioned directory path populated by an svn:externals
        property; this status is not used for file externals */
    svn_wc_status_external,

    /** a directory doesn't contain a complete entries list */
    svn_wc_status_incomplete
};

/**
 * Structure for holding the "status" of a working copy item.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type.
 *
 * @since New in 1.7.
 */
typedef struct svn_wc_status3_t
{
  /** The kind of node as recorded in the working copy */
  svn_node_kind_t kind;

  /** The depth of the node as recorded in the working copy
   * (#svn_depth_unknown for files or when no depth is set) */
  svn_depth_t depth;

  /** The actual size of the working file on disk, or SVN_INVALID_FILESIZE
   * if unknown (or if the item isn't a file at all). */
  svn_filesize_t filesize;

  /** If the path is under version control, versioned is TRUE, otherwise
   * FALSE. */
  svn_boolean_t versioned;

  /** Set to TRUE if the item is the victim of a conflict. */
  svn_boolean_t conflicted;

  /** The status of the node itself. In order of precedence: Obstructions,
   * structural changes, text changes. */
  enum svn_wc_status_kind node_status;

  /** The status of the entry's text. */
  enum svn_wc_status_kind text_status;

  /** The status of the entry's properties. */
  enum svn_wc_status_kind prop_status;

  /** a file or directory can be 'copied' if it's scheduled for
   * addition-with-history (or part of a subtree that is scheduled as such.).
   */
  svn_boolean_t copied;

  /** Base revision. */
  svn_revnum_t revision;

  /** Last revision this was changed */
  svn_revnum_t changed_rev;

  /** Date of last commit. */
  apr_time_t changed_date;

  /** Last commit author of this item */
  const char *changed_author;

  /** The URL of the repository */
  const char *repos_root_url;

  /** The UUID of the repository */
  const char *repos_uuid;

  /** The in-repository path relative to the repository root. */
  const char *repos_relpath;

  /** a file or directory can be 'switched' if the switch command has been
   * used.  If this is TRUE, then file_external will be FALSE.
   */
  svn_boolean_t switched;

  /** This directory has a working copy lock */
  svn_boolean_t locked;

  /** The repository file lock. (Values of path, token, owner, comment
   * and are available if a lock is present) */
  const svn_lock_t *lock;

  /** Which changelist this item is part of, or NULL if not part of any. */
  const char *changelist;

  /**
   * @defgroup svn_wc_status_ood WC out-of-date info from the repository
   * @{
   *
   * When the working copy item is out-of-date compared to the
   * repository, the following fields represent the state of the
   * youngest revision of the item in the repository.  If the working
   * copy is not out of date, the fields are initialized as described
   * below.
   */

  /** Set to the node kind of the youngest commit, or #svn_node_none
   * if not out of date. */
  svn_node_kind_t ood_kind;

  /** The status of the node, based on the text status if the node has no
   * restructuring changes */
  enum svn_wc_status_kind repos_node_status;

  /** The entry's text status in the repository. */
  enum svn_wc_status_kind repos_text_status;

  /** The entry's property status in the repository. */
  enum svn_wc_status_kind repos_prop_status;

  /** The entry's lock in the repository, if any. */
  const svn_lock_t *repos_lock;

  /** Set to the youngest committed revision, or #SVN_INVALID_REVNUM
   * if not out of date. */
  svn_revnum_t ood_changed_rev;

  /** Set to the most recent commit date, or @c 0 if not out of date. */
  apr_time_t ood_changed_date;

  /** Set to the user name of the youngest commit, or @c NULL if not
   * out of date or non-existent.  Because a non-existent @c
   * svn:author property has the same behavior as an out-of-date
   * working copy, examine @c ood_last_cmt_rev to determine whether
   * the working copy is out of date. */
  const char *ood_changed_author;

  /** @} */

  /** Set to the local absolute path that this node was moved from, if this
   * file or directory has been moved here locally and is the root of that
   * move. Otherwise set to NULL.
   *
   * This will be NULL for moved-here nodes that are just part of a subtree
   * that was moved along (and are not themselves a root of a different move
   * operation).
   *
   * @since New in 1.8. */
  const char *moved_from_abspath;

  /** Set to the local absolute path that this node was moved to, if this file
   * or directory has been moved away locally and corresponds to the root
   * of the destination side of the move. Otherwise set to NULL.
   *
   * Note: Saying just "root" here could be misleading. For example:
   *   svn mv A AA;
   *   svn mv AA/B BB;
   * creates a situation where A/B is moved-to BB, but one could argue that
   * the move source's root actually was AA/B. Note that, as far as the
   * working copy is concerned, above case is exactly identical to:
   *   svn mv A/B BB;
   *   svn mv A AA;
   * In both situations, @a moved_to_abspath would be set for nodes A (moved
   * to AA) and A/B (moved to BB), only.
   *
   * This will be NULL for moved-away nodes that were just part of a subtree
   * that was moved along (and are not themselves a root of a different move
   * operation).
   *
   * @since New in 1.8. */
  const char *moved_to_abspath;

  /** @c TRUE iff the item is a file brought in by an svn:externals definition.
   * @since New in 1.8. */
  svn_boolean_t file_external;


  /** The actual kind of the node in the working copy. May differ from
   * @a kind on obstructions, deletes, etc. #svn_node_unknown if unavailable.
   *
   * @since New in 1.9 */
  svn_node_kind_t actual_kind;

  /* NOTE! Please update svn_wc_dup_status3() when adding new fields here. */
} svn_wc_status3_t;

/**
 * ### All diffs are not yet known.
 * Same as svn_wc_status3_t, but without the #svn_boolean_t 'versioned'
 * field. Instead an item that is not versioned has the 'entry' field set to
 * @c NULL.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef struct svn_wc_status2_t
{
  /** Can be @c NULL if not under version control. */
  const svn_wc_entry_t *entry;

  /** The status of the entry itself, including its text if it is a file. */
  enum svn_wc_status_kind text_status;

  /** The status of the entry's properties. */
  enum svn_wc_status_kind prop_status;

  /** a directory can be 'locked' if a working copy update was interrupted. */
  svn_boolean_t locked;

  /** a file or directory can be 'copied' if it's scheduled for
   * addition-with-history (or part of a subtree that is scheduled as such.).
   */
  svn_boolean_t copied;

  /** a file or directory can be 'switched' if the switch command has been
   * used.  If this is TRUE, then file_external will be FALSE.
   */
  svn_boolean_t switched;

  /** The entry's text status in the repository. */
  enum svn_wc_status_kind repos_text_status;

  /** The entry's property status in the repository. */
  enum svn_wc_status_kind repos_prop_status;

  /** The entry's lock in the repository, if any. */
  svn_lock_t *repos_lock;

  /** Set to the URI (actual or expected) of the item.
   * @since New in 1.3
   */
  const char *url;

  /**
   * @defgroup svn_wc_status_ood WC out-of-date info from the repository
   * @{
   *
   * When the working copy item is out-of-date compared to the
   * repository, the following fields represent the state of the
   * youngest revision of the item in the repository.  If the working
   * copy is not out of date, the fields are initialized as described
   * below.
   */

  /** Set to the youngest committed revision, or #SVN_INVALID_REVNUM
   * if not out of date.
   * @since New in 1.3
   */
  svn_revnum_t ood_last_cmt_rev;

  /** Set to the most recent commit date, or @c 0 if not out of date.
   * @since New in 1.3
   */
  apr_time_t ood_last_cmt_date;

  /** Set to the node kind of the youngest commit, or #svn_node_none
   * if not out of date.
   * @since New in 1.3
   */
  svn_node_kind_t ood_kind;

  /** Set to the user name of the youngest commit, or @c NULL if not
   * out of date or non-existent.  Because a non-existent @c
   * svn:author property has the same behavior as an out-of-date
   * working copy, examine @c ood_last_cmt_rev to determine whether
   * the working copy is out of date.
   * @since New in 1.3
   */
  const char *ood_last_cmt_author;

  /** @} */

  /** Non-NULL if the entry is the victim of a tree conflict.
   * @since New in 1.6
   */
  svn_wc_conflict_description_t *tree_conflict;

  /** If the item is a file that was added to the working copy with an
   * svn:externals; if file_external is TRUE, then switched is always
   * FALSE.
   * @since New in 1.6
   */
  svn_boolean_t file_external;

  /** The actual status of the text compared to the pristine base of the
   * file. This value isn't masked by other working copy statuses.
   * @c pristine_text_status is #svn_wc_status_none if this value was
   * not calculated during the status walk.
   * @since New in 1.6
   */
  enum svn_wc_status_kind pristine_text_status;

  /** The actual status of the properties compared to the pristine base of
   * the node. This value isn't masked by other working copy statuses.
   * @c pristine_prop_status is #svn_wc_status_none if this value was
   * not calculated during the status walk.
   * @since New in 1.6
   */
  enum svn_wc_status_kind pristine_prop_status;

} svn_wc_status2_t;



/**
 * Same as #svn_wc_status2_t, but without the #svn_lock_t 'repos_lock', const char 'url', #svn_revnum_t 'ood_last_cmt_rev', apr_time_t 'ood_last_cmt_date', #svn_node_kind_t 'ood_kind', const char 'ood_last_cmt_author', #svn_wc_conflict_description_t 'tree_conflict', #svn_boolean_t 'file_external', #svn_wc_status_kind 'pristine_text_status', and #svn_wc_status_kind 'pristine_prop_status' fields.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef struct svn_wc_status_t
{
  /** Can be @c NULL if not under version control. */
  const svn_wc_entry_t *entry;

  /** The status of the entries text. */
  enum svn_wc_status_kind text_status;

  /** The status of the entries properties. */
  enum svn_wc_status_kind prop_status;

  /** a directory can be 'locked' if a working copy update was interrupted. */
  svn_boolean_t locked;

  /** a file or directory can be 'copied' if it's scheduled for
   * addition-with-history (or part of a subtree that is scheduled as such.).
   */
  svn_boolean_t copied;

  /** a file or directory can be 'switched' if the switch command has been
   * used.
   */
  svn_boolean_t switched;

  /** The entry's text status in the repository. */
  enum svn_wc_status_kind repos_text_status;

  /** The entry's property status in the repository. */
  enum svn_wc_status_kind repos_prop_status;

} svn_wc_status_t;


/**
 * Return a deep copy of the @a orig_stat status structure, allocated
 * in @a pool.
 *
 * @since New in 1.7.
 */
svn_wc_status3_t *
svn_wc_dup_status3(const svn_wc_status3_t *orig_stat,
                   apr_pool_t *pool);

/**
 * Same as svn_wc_dup_status3(), but for older svn_wc_status_t structures.
 *
 * @since New in 1.2
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_wc_status2_t *
svn_wc_dup_status2(const svn_wc_status2_t *orig_stat,
                   apr_pool_t *pool);


/**
 * Same as svn_wc_dup_status2(), but for older svn_wc_status_t structures.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_wc_status_t *
svn_wc_dup_status(const svn_wc_status_t *orig_stat,
                  apr_pool_t *pool);


/**
 * Fill @a *status for @a local_abspath, allocating in @a result_pool.
 * Use @a scratch_pool for temporary allocations.
 *
 * Here are some things to note about the returned structure.  A quick
 * examination of the @c status->text_status after a successful return of
 * this function can reveal the following things:
 *
 *    - #svn_wc_status_none : @a local_abspath is not versioned, and is
 *                            not present on disk
 *
 *    - #svn_wc_status_missing : @a local_abspath is versioned, but is
 *                               missing from the working copy.
 *
 *    - #svn_wc_status_unversioned : @a local_abspath is not versioned,
 *                                   but is present on disk and not being
 *                                   ignored (see above).
 *
 * The other available results for the @c text_status field are more
 * straightforward in their meanings.  See the comments on the
 * #svn_wc_status_kind structure for some hints.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_status3(svn_wc_status3_t **status,
               svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool);

/** Similar to svn_wc_status3(), but with a adm_access baton and absolute
 * path.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_status2(svn_wc_status2_t **status,
               const char *path,
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool);


/**
 *  Same as svn_wc_status2(), but for older svn_wc_status_t structures.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_status(svn_wc_status_t **status,
              const char *path,
              svn_wc_adm_access_t *adm_access,
              apr_pool_t *pool);




/**
 * A callback for reporting a @a status about @a local_abspath.
 *
 * @a baton is a closure object; it should be provided by the
 * implementation, and passed by the caller.
 *
 * @a scratch_pool will be cleared between invocations to the callback.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_wc_status_func4_t)(void *baton,
                                              const char *local_abspath,
                                              const svn_wc_status3_t *status,
                                              apr_pool_t *scratch_pool);

/**
 * Same as svn_wc_status_func4_t, but with a non-const status and a relative
 * path.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef svn_error_t *(*svn_wc_status_func3_t)(void *baton,
                                              const char *path,
                                              svn_wc_status2_t *status,
                                              apr_pool_t *pool);

/**
 * Same as svn_wc_status_func3_t, but without a provided pool or
 * the ability to propagate errors.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
typedef void (*svn_wc_status_func2_t)(void *baton,
                                      const char *path,
                                      svn_wc_status2_t *status);

/**
 *  Same as svn_wc_status_func2_t, but for older svn_wc_status_t structures.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef void (*svn_wc_status_func_t)(void *baton,
                                     const char *path,
                                     svn_wc_status_t *status);

/**
 * Walk the working copy status of @a local_abspath using @a wc_ctx, by
 * creating #svn_wc_status3_t structures and sending these through
 * @a status_func / @a status_baton.
 *
 *  * Assuming the target is a directory, then:
 *
 *   - If @a get_all is FALSE, then only locally-modified entries will be
 *     returned.  If TRUE, then all entries will be returned.
 *
 *   - If @a ignore_text_mods is TRUE, then the walk will not check for
 *     modified files.  Any #svn_wc_status3_t structures returned for files
 *     will always have a text_status field set to svn_wc_status_normal.
 *     If @a ignore_text_mods is FALSE, the walk checks for text changes
 *     and returns #svn_wc_status3_t structures describing any changes.
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
 * If @a no_ignore is set, statuses that would typically be ignored
 * will instead be reported.
 *
 * @a ignore_patterns is an array of file patterns matching
 * unversioned files to ignore for the purposes of status reporting,
 * or @c NULL if the default set of ignorable file patterns should be used.
 * Patterns from #SVN_PROP_IGNORE (and, as of 1.8,
 * #SVN_PROP_INHERITABLE_IGNORES) properties are always used, even if not
 * specified in @a ignore_patterns.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton while walking
 * to determine if the client has canceled the operation.
 *
 * This function uses @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_walk_status(svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_text_mods,
                   const apr_array_header_t *ignore_patterns,
                   svn_wc_status_func4_t status_func,
                   void *status_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool);

/**
 * DEPRECATED -- please use APIs from svn_client.h
 *
 * ---
 *
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
 *   - If @a get_all is FALSE, then only locally-modified entries will be
 *     returned.  If TRUE, then all entries will be returned.
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
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_status_editor5(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
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
 * Same as svn_wc_get_status_editor5, but using #svn_wc_status_func3_t
 * instead of #svn_wc_status_func4_t. And @a server_performs_filtering
 * always set to #TRUE.
 *
 * This also uses a single pool parameter, stating that all temporary
 * allocations are performed in manually constructed/destroyed subpool.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_status_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func3_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool);

/**
 * Same as svn_wc_get_status_editor4(), but using #svn_wc_status_func2_t
 * instead of #svn_wc_status_func3_t.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_status_editor3(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool);

/**
 * Like svn_wc_get_status_editor3(), but with @a ignore_patterns
 * provided from the corresponding value in @a config, and @a recurse
 * instead of @a depth.  If @a recurse is TRUE, behave as if for
 * #svn_depth_infinity; else if @a recurse is FALSE, behave as if for
 * #svn_depth_immediates.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_status_editor2(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          apr_hash_t *config,
                          svn_boolean_t recurse,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool);

/**
 * Same as svn_wc_get_status_editor2(), but with @a set_locks_baton set
 * to @c NULL, and taking a deprecated svn_wc_status_func_t argument.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_status_editor(const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_revnum_t *edit_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         apr_hash_t *config,
                         svn_boolean_t recurse,
                         svn_boolean_t get_all,
                         svn_boolean_t no_ignore,
                         svn_wc_status_func_t status_func,
                         void *status_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool);


/**
 * Associate @a locks, a hash table mapping <tt>const char*</tt>
 * absolute repository paths to <tt>svn_lock_t</tt> objects, with a
 * @a set_locks_baton returned by an earlier call to
 * svn_wc_get_status_editor3().  @a repos_root is the repository root URL.
 * Perform all allocations in @a pool.
 *
 * @note @a locks will not be copied, so it must be valid throughout the
 * edit.  @a pool must also not be destroyed or cleared before the edit is
 * finished.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_wc_status_set_repos_locks(void *set_locks_baton,
                              apr_hash_t *locks,
                              const char *repos_root,
                              apr_pool_t *pool);

/** @} */


/**
 * Copy @a src_abspath to @a dst_abspath, and schedule @a dst_abspath
 * for addition to the repository, remembering the copy history. @a wc_ctx
 * is used for accessing the working copy and must contain a write lock for
 * the parent directory of @a dst_abspath,
 *
 * If @a metadata_only is TRUE then this is a database-only operation and
 * the working directories and files are not copied.
 *
 * @a src_abspath must be a file or directory under version control;
 * the parent of @a dst_abspath must be a directory under version control
 * in the same working copy; @a dst_abspath will be the name of the copied
 * item, and it must not exist already if @a metadata_only is FALSE.  Note that
 * when @a src points to a versioned file, the working file doesn't
 * necessarily exist in which case its text-base is used instead.
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
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_copy3(svn_wc_context_t *wc_ctx,
             const char *src_abspath,
             const char *dst_abspath,
             svn_boolean_t metadata_only,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool);

/** Similar to svn_wc_copy3(), but takes access batons and a relative path
 * and a basename instead of absolute paths and a working copy context.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_copy2(const char *src,
             svn_wc_adm_access_t *dst_parent,
             const char *dst_basename,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *pool);

/**
 * Similar to svn_wc_copy2(), but takes an #svn_wc_notify_func_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_copy(const char *src,
            svn_wc_adm_access_t *dst_parent,
            const char *dst_basename,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool);

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
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * If @a notify_func is non-NULL, call it with @a notify_baton and the path
 * of the root node (only) of the destination.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @see svn_client_move7()
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_move(svn_wc_context_t *wc_ctx,
            const char *src_abspath,
            const char *dst_abspath,
            svn_boolean_t metadata_only,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *scratch_pool);

/**
 * Schedule @a local_abspath for deletion.  It will be deleted from the
 * repository on the next commit.  If @a local_abspath refers to a
 * directory, then a recursive deletion will occur. @a wc_ctx must hold
 * a write lock for the parent of @a local_abspath, @a local_abspath itself
 * and everything below @a local_abspath.
 *
 * If @a keep_local is FALSE, this function immediately deletes all files,
 * modified and unmodified, versioned and if @a delete_unversioned is TRUE,
 * unversioned from the working copy.
 * It also immediately deletes unversioned directories and directories that
 * are scheduled to be added below @a local_abspath.  Only versioned may
 * remain in the working copy, these get deleted by the update following
 * the commit.
 *
 * If @a keep_local is TRUE, all files and directories will be kept in the
 * working copy (and will become unversioned on the next commit).
 *
 * If @a delete_unversioned_target is TRUE and @a local_abspath is not
 * versioned, @a local_abspath will be handled as an added files without
 * history. So it will be deleted if @a keep_local is FALSE. If @a
 * delete_unversioned is FALSE and @a local_abspath is not versioned a
 * #SVN_ERR_WC_PATH_NOT_FOUND error will be returned.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * For each path marked for deletion, @a notify_func will be called with
 * the @a notify_baton and that path. The @a notify_func callback may be
 * @c NULL if notification is not needed.
 *
 * Use @a scratch_pool for temporary allocations.  It may be cleared
 * immediately upon returning from this function.
 *
 * @since New in 1.7.
 */
 /* ### BH: Maybe add a delete_switched flag that allows deny switched
            nodes like file externals? */
svn_error_t *
svn_wc_delete4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t keep_local,
               svn_boolean_t delete_unversioned_target,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_delete4, but uses an access baton and relative path
 * instead of a working copy context and absolute path. @a adm_access
 * must hold a write lock for the parent of @a path.
 *
 * @c delete_unversioned_target will always be set to TRUE.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_delete3(const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               svn_boolean_t keep_local,
               apr_pool_t *pool);

/**
 * Similar to svn_wc_delete3(), but with @a keep_local always set to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_delete2(const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool);

/**
 * Similar to svn_wc_delete2(), but takes an #svn_wc_notify_func_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_delete(const char *path,
              svn_wc_adm_access_t *adm_access,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool);


/**
 * Schedule the single node that exists on disk at @a local_abspath for
 * addition to the working copy.  The added node will have the properties
 * provided in @a props, or none if that is NULL.
 *
 * Unless @a skip_checks is TRUE, check and canonicalize the properties in the
 * same way as svn_wc_prop_set4().  Return an error and don't add the node if
 * the properties are not valid on this node.
 *
 * ### The error code on validity check failure should be specified, and
 *     preferably should be a single code.
 *
 * The versioned state of the parent path must be a modifiable directory,
 * and the versioned state of @a local_abspath must be either nonexistent or
 * deleted; if deleted, the new node will be a replacement.
 *
 * If @a local_abspath does not exist as file, directory or symlink, return
 * #SVN_ERR_WC_PATH_NOT_FOUND.
 *
 * If @a notify_func is non-NULL, invoke it with @a notify_baton to report
 * the item being added.
 *
 * ### TODO: Split into add_dir, add_file, add_symlink?
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_wc_add_from_disk3(svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      const apr_hash_t *props,
                      svn_boolean_t skip_checks,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_add_from_disk3(), but always passes FALSE for
 * @a skip_checks
 *
 * @since New in 1.8.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_from_disk2(svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      const apr_hash_t *props,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      apr_pool_t *scratch_pool);


/**
 * Similar to svn_wc_add_from_disk2(), but always passes NULL for @a
 * props.
 *
 * This is a replacement for svn_wc_add4() case 2a (which see for
 * details).

 * @see svn_wc_add4()
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_from_disk(svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool);


/**
 * Put @a local_abspath under version control by registering it as addition
 * or copy in the database containing its parent. The new node is scheduled
 * for addition to the repository below its parent node.
 *
 * 1) If the node is already versioned, it MUST BE the root of a separate
 * working copy from the same repository as the parent WC. The new node
 * and anything below it will be scheduled for addition inside the parent
 * working copy as a copy of the original location. The separate working
 * copy will be integrated by this step. In this case, which is only used
 * by code like that of "svn cp URL@rev path" @a copyfrom_url and
 * @a copyfrom_rev MUST BE the url and revision of @a local_abspath
 * in the separate working copy.
 *
 * 2a) If the node was not versioned before it will be scheduled as a local
 * addition or 2b) if @a copyfrom_url and @a copyfrom_rev are set as a copy
 * of that location. In this last case the function doesn't set the pristine
 * version (of a file) and/or pristine properties, which callers should
 * handle via different APIs. Usually it is easier to call
 * svn_wc_add_repos_file4() (### or a possible svn_wc_add_repos_dir()) than
 * using this variant.
 *
 * If @a local_abspath does not exist as file, directory or symlink, return
 * #SVN_ERR_WC_PATH_NOT_FOUND.
 *
 * If @a local_abspath is an unversioned directory, record @a depth on it;
 * otherwise, ignore @a depth. (Use #svn_depth_infinity unless you exactly
 * know what you are doing, or you may create an unexpected sparse working
 * copy)
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * When the @a local_abspath has been added, then @a notify_func will be
 * called (if it is not @c NULL) with the @a notify_baton and the path.
 *
 * @note Case 1 is deprecated. Consider doing a WC-to-WC copy instead.
 * @note For case 2a, prefer svn_wc_add_from_disk().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_add4(svn_wc_context_t *wc_ctx,
            const char *local_abspath,
            svn_depth_t depth,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_add4(), but with an access baton
 * and relative path instead of a context and absolute path.
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add3(const char *path,
            svn_wc_adm_access_t *parent_access,
            svn_depth_t depth,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *pool);

/**
 * Similar to svn_wc_add3(), but with the @a depth parameter always
 * #svn_depth_infinity.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add2(const char *path,
            svn_wc_adm_access_t *parent_access,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *pool);

/**
 * Similar to svn_wc_add2(), but takes an #svn_wc_notify_func_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add(const char *path,
           svn_wc_adm_access_t *parent_access,
           const char *copyfrom_url,
           svn_revnum_t copyfrom_rev,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           svn_wc_notify_func_t notify_func,
           void *notify_baton,
           apr_pool_t *pool);

/** Add a file to a working copy at @a local_abspath, obtaining the
 * text-base's contents from @a new_base_contents, the wc file's
 * content from @a new_contents, its unmodified properties from @a
 * new_base_props and its actual properties from @a new_props. Use
 * @a wc_ctx for accessing the working copy.
 *
 * The unmodified text and props normally come from the repository
 * file represented by the copyfrom args, see below.  The new file
 * will be marked as copy.
 *
 * @a new_contents and @a new_props may be NULL, in which case
 * the working copy text and props are taken from the base files with
 * appropriate translation of the file's content.
 *
 * @a new_contents must be provided in Normal Form. This is required
 * in order to pass both special and non-special files through a stream.
 *
 * @a wc_ctx must contain a write lock for the parent of @a local_abspath.
 *
 * If @a copyfrom_url is non-NULL, then @a copyfrom_rev must be a
 * valid revision number, and together they are the copyfrom history
 * for the new file.
 *
 * The @a cancel_func and @a cancel_baton are a standard cancellation
 * callback, or NULL if no callback is needed. @a notify_func and
 * @a notify_baton are a notification callback, and (if not NULL)
 * will be notified of the addition of this file.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * ### This function is very redundant with svn_wc_add().  Ideally,
 * we'd merge them, so that svn_wc_add() would just take optional
 * new_props and optional copyfrom information.  That way it could be
 * used for both 'svn add somefilesittingonmydisk' and for adding
 * files from repositories, with or without copyfrom history.
 *
 * The problem with this Ideal Plan is that svn_wc_add() also takes
 * care of recursive URL-rewriting.  There's a whole comment in its
 * doc string about how that's really weird, outside its core mission,
 * etc, etc.  So another part of the Ideal Plan is that that
 * functionality of svn_wc_add() would move into a separate function.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_add_repos_file4(svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       svn_stream_t *new_base_contents,
                       svn_stream_t *new_contents,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool);

/** Similar to svn_wc_add_repos_file4, but uses access batons and a
 * relative path instead of a working copy context and absolute path.
 *
 * ### NOTE: the notification callback/baton is not yet used.
 *
 * @since New in 1.6.
 * @deprecated Provided for compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_repos_file3(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       svn_stream_t *new_base_contents,
                       svn_stream_t *new_contents,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       svn_wc_notify_func2_t notify_func,
                       void *notify_baton,
                       apr_pool_t *scratch_pool);


/** Same as svn_wc_add_repos_file3(), except that it has pathnames rather
 * than streams for the text base, and actual text, and has no cancellation.
 *
 * @since New in 1.4.
 * @deprecated Provided for compatibility with the 1.5 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_repos_file2(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       const char *new_text_base_path,
                       const char *new_text_path,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       apr_pool_t *pool);

/** Same as svn_wc_add_repos_file3(), except that it doesn't have the
 * BASE arguments or cancellation.
 *
 * @deprecated Provided for compatibility with the 1.3 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_repos_file(const char *dst_path,
                      svn_wc_adm_access_t *adm_access,
                      const char *new_text_path,
                      apr_hash_t *new_props,
                      const char *copyfrom_url,
                      svn_revnum_t copyfrom_rev,
                      apr_pool_t *pool);


/** Remove @a local_abspath from revision control.  @a wc_ctx must
 * hold a write lock on the parent of @a local_abspath, or if that is a
 * WC root then on @a local_abspath itself.
 *
 * If @a local_abspath is a file, all its info will be removed from the
 * administrative area.  If @a local_abspath is a directory, then the
 * administrative area will be deleted, along with *all* the administrative
 * areas anywhere in the tree below @a adm_access.
 *
 * Normally, only administrative data is removed.  However, if
 * @a destroy_wf is TRUE, then all working file(s) and dirs are deleted
 * from disk as well.  When called with @a destroy_wf, any locally
 * modified files will *not* be deleted, and the special error
 * #SVN_ERR_WC_LEFT_LOCAL_MOD might be returned.  (Callers only need to
 * check for this special return value if @a destroy_wf is TRUE.)
 *
 * If @a instant_error is TRUE, then return
 * #SVN_ERR_WC_LEFT_LOCAL_MOD the instant a locally modified file is
 * encountered.  Otherwise, leave locally modified files in place and
 * return the error only after all the recursion is complete.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the removal.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * WARNING:  This routine is exported for careful, measured use by
 * libsvn_client.  Do *not* call this routine unless you really
 * understand what the heck you're doing.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_remove_from_revision_control2(svn_wc_context_t *wc_ctx,
                                     const char *local_abspath,
                                     svn_boolean_t destroy_wf,
                                     svn_boolean_t instant_error,
                                     svn_cancel_func_t cancel_func,
                                     void *cancel_baton,
                                     apr_pool_t *pool);

/**
 * Similar to svn_wc_remove_from_revision_control2() but with a name
 * and access baton.
 *
 * WARNING:  This routine was exported for careful, measured use by
 * libsvn_client.  Do *not* call this routine unless you really
 * understand what the heck you're doing.
 *
 * @deprecated Provided for compatibility with the 1.6 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_remove_from_revision_control(svn_wc_adm_access_t *adm_access,
                                    const char *name,
                                    svn_boolean_t destroy_wf,
                                    svn_boolean_t instant_error,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *pool);


/**
 * Assuming @a local_abspath is under version control or a tree conflict
 * victim and in a state of conflict, then take @a local_abspath *out*
 * of this state.  If @a resolve_text is TRUE then any text conflict is
 * resolved, if @a resolve_tree is TRUE then any tree conflicts are
 * resolved. If @a resolve_prop is set to "" all property conflicts are
 * resolved, if it is set to any other string value, conflicts on that
 * specific property are resolved and when resolve_prop is NULL, no
 * property conflicts are resolved.
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
 * #svn_wc_conflict_choose_theirs_conflict and
 * #svn_wc_conflict_choose_mine_conflict are not legal for binary
 * files or properties.
 *
 * @a wc_ctx is a working copy context, with a write lock, for @a
 * local_abspath.
 *
 * Needless to say, this function doesn't touch conflict markers or
 * anything of that sort -- only a human can semantically resolve a
 * conflict.  Instead, this function simply marks a file as "having
 * been resolved", clearing the way for a commit.
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
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.9 API.
 * Use svn_client_conflict_text_resolve(), svn_client_conflict_prop_resolve(),
 * and svn_client_conflict_tree_resolve() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_resolved_conflict5(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool);

/** Similar to svn_wc_resolved_conflict5, but takes an absolute path
 * and an access baton. This version doesn't support resolving a specific
 * property.conflict.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_resolved_conflict4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t resolve_tree,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool);


/**
 * Similar to svn_wc_resolved_conflict4(), but without tree-conflict
 * resolution support.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_resolved_conflict3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool);


/**
 * Similar to svn_wc_resolved_conflict3(), but without automatic conflict
 * resolution support, and with @a depth set according to @a recurse:
 * if @a recurse is TRUE, @a depth is #svn_depth_infinity, else it is
 * #svn_depth_files.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_resolved_conflict2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool);

/**
 * Similar to svn_wc_resolved_conflict2(), but takes an
 * svn_wc_notify_func_t and doesn't have cancellation support.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_resolved_conflict(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool);


/* Commits. */


/**
 * Storage type for queued post-commit data.
 *
 * @since New in 1.5.
 */
typedef struct svn_wc_committed_queue_t svn_wc_committed_queue_t;


/**
 * Create a queue for use with svn_wc_queue_committed() and
 * svn_wc_process_committed_queue().
 *
 * The returned queue and all further allocations required for queuing
 * new items will also be done from @a pool.
 *
 * @since New in 1.5.
 */
svn_wc_committed_queue_t *
svn_wc_committed_queue_create(apr_pool_t *pool);


/**
 * Queue committed items to be processed later by
 * svn_wc_process_committed_queue2().
 *
 * Record in @a queue that @a local_abspath will need to be bumped
 * after a commit succeeds.
 *
 * If non-NULL, @a wcprop_changes is an array of <tt>svn_prop_t *</tt>
 * changes to wc properties; if an #svn_prop_t->value is NULL, then
 * that property is deleted.
 *   ### [JAF]  No, a prop whose value is NULL is ignored, not deleted.  This
 *   ### seems to be not a set of changes but rather the new complete set of
 *   ### props.  And it's renamed to 'new_dav_cache' inside; why?
 *
 * If @a is_committed is @c TRUE, the node will be processed as committed. This
 * turns the node and its implied descendants as the new unmodified state at
 * the new specified revision. Unless @a recurse is TRUE, changes on
 * descendants are not committed as changes directly. In this case they should
 * be queueud as their own changes.
 *
 * If @a remove_lock is @c TRUE, any entryprops related to a repository
 * lock will be removed.
 *
 * If @a remove_changelist is @c TRUE, any association with a
 * changelist will be removed.
 *
 *
 * If @a sha1_checksum is non-NULL, use it to identify the node's pristine
 * text.
 *
 * If @a recurse is TRUE and @a local_abspath is a directory, then bump every
 * versioned object at or under @a local_abspath.  This is usually done for
 * copied trees.
 *
 * ### In the present implementation, if a recursive directory item is in
 *     the queue, then any children (at any depth) of that directory that
 *     are also in the queue as separate items will get:
 *       'wcprop_changes' = NULL;
 *       'remove_lock' = FALSE;
 *       'remove_changelist' from the recursive parent item;
 *     and any children (at any depth) of that directory that are NOT in
 *     the queue as separate items will get:
 *       'wcprop_changes' = NULL;
 *       'remove_lock' = FALSE;
 *       'remove_changelist' from the recursive parent item;
 *
 * @note the @a recurse parameter should be used with extreme care since
 * it will bump ALL nodes under the directory, regardless of their
 * actual inclusion in the new revision.
 *
 * All pointer data passed to this function (@a local_abspath,
 * @a wcprop_changes and the checksums) should remain valid until the
 * queue has been processed by svn_wc_process_committed_queue2().
 *
 * Temporary allocations will be performed in @a scratch_pool, and persistent
 * allocations will use the same pool as @a queue used when it was created.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_wc_queue_committed4(svn_wc_committed_queue_t *queue,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t recurse,
                        svn_boolean_t is_committed,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *sha1_checksum,
                        apr_pool_t *scratch_pool);

/** Similar to svn_wc_queue_committed4, but with is_committed always
 * TRUE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 */
svn_error_t *
svn_wc_queue_committed3(svn_wc_committed_queue_t *queue,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t recurse,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *sha1_checksum,
                        apr_pool_t *scratch_pool);

/** Same as svn_wc_queue_committed3() except @a path doesn't have to be an
 * abspath and @a adm_access is unused and a SHA-1 checksum cannot be
 * specified.
 *
 * @since New in 1.6.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_queue_committed2(svn_wc_committed_queue_t *queue,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t recurse,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *md5_checksum,
                        apr_pool_t *scratch_pool);


/** Same as svn_wc_queue_committed2() but the @a queue parameter has an
 * extra indirection and @a digest is supplied instead of a checksum type.
 *
 * @note despite the extra indirection, this function does NOT allocate
 *   the queue for you. svn_wc_committed_queue_create() must be called.
 *
 * @since New in 1.5
 *
 * @deprecated Provided for backwards compatibility with 1.5
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_queue_committed(svn_wc_committed_queue_t **queue,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t recurse,
                       const apr_array_header_t *wcprop_changes,
                       svn_boolean_t remove_lock,
                       svn_boolean_t remove_changelist,
                       const unsigned char *digest,
                       apr_pool_t *pool);


/**
 * Bump all items in @a queue to @a new_revnum after a commit succeeds.
 * @a rev_date and @a rev_author are the (server-side) date and author
 * of the new revision; one or both may be @c NULL.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client wants to cancel the operation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_process_committed_queue2(svn_wc_committed_queue_t *queue,
                                svn_wc_context_t *wc_ctx,
                                svn_revnum_t new_revnum,
                                const char *rev_date,
                                const char *rev_author,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *scratch_pool);

/** @see svn_wc_process_committed_queue2()
 *
 * @since New in 1.5.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_process_committed_queue(svn_wc_committed_queue_t *queue,
                               svn_wc_adm_access_t *adm_access,
                               svn_revnum_t new_revnum,
                               const char *rev_date,
                               const char *rev_author,
                               apr_pool_t *pool);


/**
 * @note this function has improper expectations around the operation and
 *   execution of other parts of the Subversion WC library. The resulting
 *   coupling makes this interface near-impossible to support. Documentation
 *   has been removed, as a result.
 *
 * @deprecated Use the svn_wc_committed_queue_* functions instead. Provided
 *   for backwards compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_process_committed4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          svn_boolean_t remove_changelist,
                          const unsigned char *digest,
                          apr_pool_t *pool);

/** @see svn_wc_process_committed4()
 *
 * @deprecated Use the svn_wc_committed_queue_* functions instead. Provided
 *   for backwards compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_process_committed3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          const unsigned char *digest,
                          apr_pool_t *pool);

/** @see svn_wc_process_committed4()
 *
 * @deprecated Use the svn_wc_committed_queue_* functions instead. Provided
 *   for backwards compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_process_committed2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          apr_pool_t *pool);

/** @see svn_wc_process_committed4()
 *
 * @deprecated Use the svn_wc_committed_queue_* functions instead. Provided
 *   for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_process_committed(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t recurse,
                         svn_revnum_t new_revnum,
                         const char *rev_date,
                         const char *rev_author,
                         const apr_array_header_t *wcprop_changes,
                         apr_pool_t *pool);





/**
 * Do a depth-first crawl in a working copy, beginning at @a local_abspath,
 * using @a wc_ctx for accessing the working copy.
 *
 * Communicate the `state' of the working copy's revisions and depths
 * to @a reporter/@a report_baton.  Obviously, if @a local_abspath is a
 * file instead of a directory, this depth-first crawl will be a short one.
 *
 * No locks or logs are created, nor are any animals harmed in the
 * process unless @a restore_files is TRUE.  No cleanup is necessary.
 *
 * After all revisions are reported, @a reporter->finish_report() is
 * called, which immediately causes the RA layer to update the working
 * copy.  Thus the return value may very well reflect the result of
 * the update!
 *
 * If @a depth is #svn_depth_empty, then report state only for
 * @a path itself.  If #svn_depth_files, do the same and include
 * immediate file children of @a path.  If #svn_depth_immediates,
 * then behave as if for #svn_depth_files but also report the
 * property states of immediate subdirectories.  If @a depth is
 * #svn_depth_infinity, then report state fully recursively.  All
 * descents are only as deep as @a path's own depth permits, of
 * course.  If @a depth is #svn_depth_unknown, then just use
 * #svn_depth_infinity, which in practice means depth of @a path.
 *
 * Iff @a honor_depth_exclude is TRUE, the crawler will report paths
 * whose ambient depth is #svn_depth_exclude as being excluded, and
 * thus prevent the server from pushing update data for those paths;
 * therefore, don't set this flag if you wish to pull in excluded paths.
 * Note that #svn_depth_exclude on the target @a path is never
 * honored, even if @a honor_depth_exclude is TRUE, because we need to
 * be able to explicitly pull in a target.  For example, if this is
 * the working copy...
 *
 *    svn co greek_tree_repos wc_dir
 *    svn up --set-depth exclude wc_dir/A/B/E  # now A/B/E is excluded
 *
 * ...then 'svn up wc_dir/A/B' would report E as excluded (assuming
 * @a honor_depth_exclude is TRUE), but 'svn up wc_dir/A/B/E' would
 * not, because the latter is trying to explicitly pull in E.  In
 * general, we never report the update target as excluded.
 *
 * Iff @a depth_compatibility_trick is TRUE, then set the @c start_empty
 * flag on @a reporter->set_path() and @a reporter->link_path() calls
 * as necessary to trick a pre-1.5 (i.e., depth-unaware) server into
 * sending back all the items the client might need to upgrade a
 * working copy from a shallower depth to a deeper one.
 *
 * If @a restore_files is TRUE, then unexpectedly missing working files
 * will be restored from the administrative directory's cache. For each
 * file restored, the @a notify_func function will be called with the
 * @a notify_baton and the path of the restored file. @a notify_func may
 * be @c NULL if this notification is not required.  If @a
 * use_commit_times is TRUE, then set restored files' timestamps to
 * their last-commit-times.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_crawl_revisions5(svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t honor_depth_exclude,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_crawl_revisions5, but with a relative path and
 * access baton instead of an absolute path and wc_ctx.
 *
 * Passes NULL for @a cancel_func and @a cancel_baton.
 *
 * @since New in 1.6.
 * @deprecated Provided for compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_crawl_revisions4(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t honor_depth_exclude,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool);


/**
 * Similar to svn_wc_crawl_revisions4, but with @a honor_depth_exclude always
 * set to false.
 *
 * @deprecated Provided for compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_crawl_revisions3(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool);

/**
 * Similar to svn_wc_crawl_revisions3, but taking svn_ra_reporter2_t
 * instead of svn_ra_reporter3_t, and therefore only able to report
 * #svn_depth_infinity for depths; and taking @a recurse instead of @a
 * depth; and with @a depth_compatibility_trick always false.
 *
 * @deprecated Provided for compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_crawl_revisions2(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter2_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool);

/**
 * Similar to svn_wc_crawl_revisions2(), but takes an #svn_wc_notify_func_t
 * and a #svn_ra_reporter_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_crawl_revisions(const char *path,
                       svn_wc_adm_access_t *adm_access,
                       const svn_ra_reporter_t *reporter,
                       void *report_baton,
                       svn_boolean_t restore_files,
                       svn_boolean_t recurse,
                       svn_boolean_t use_commit_times,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       svn_wc_traversal_info_t *traversal_info,
                       apr_pool_t *pool);


/**
 * @defgroup svn_wc_roots Working copy roots
 * @{
 */

/** If @a is_wcroot is not @c NULL, set @a *is_wcroot to @c TRUE if @a
 * local_abspath is the root of the working copy, otherwise to @c FALSE.
 *
 * If @a is_switched is not @c NULL, set @a *is_switched to @c TRUE if @a
 * local_abspath is not the root of the working copy, and switched against its
 * parent.
 *
 * If @a kind is not @c NULL, set @a *kind to the node kind of @a
 * local_abspath.
 *
 * Use @a scratch_pool for any temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc_check_root(svn_boolean_t *is_wcroot,
                  svn_boolean_t *is_switched,
                  svn_node_kind_t *kind,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool);

/** Set @a *wc_root to @c TRUE if @a local_abspath represents a "working copy
 * root", @c FALSE otherwise. Here, @a local_abspath is a "working copy root"
 * if its parent directory is not a WC or if it is switched. Also, a deleted
 * tree-conflict victim is considered a "working copy root" because it has no
 * URL.
 *
 * If @a local_abspath is not found, return the error #SVN_ERR_ENTRY_NOT_FOUND.
 *
 * Use @a scratch_pool for any temporary allocations.
 *
 * @note For legacy reasons only a directory can be a wc-root. However, this
 * function will also set wc_root to @c TRUE for a switched file.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API. Consider
 * using svn_wc_check_root() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_is_wc_root2(svn_boolean_t *wc_root,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   apr_pool_t *scratch_pool);


/**
 * Similar to svn_wc_is_wc_root2(), but with an access baton and relative
 * path.
 *
 * @note If @a path is '', this function will always return @c TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_is_wc_root(svn_boolean_t *wc_root,
                  const char *path,
                  svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool);

/** @} */


/* Updates. */

/** Conditionally split @a path into an @a anchor and @a target for the
 * purpose of updating and committing.
 *
 * @a anchor is the directory at which the update or commit editor
 * should be rooted.
 *
 * @a target is the actual subject (relative to the @a anchor) of the
 * update/commit, or "" if the @a anchor itself is the subject.
 *
 * Allocate @a anchor and @a target in @a result_pool; @a scratch_pool
 * is used for temporary allocations.
 *
 * @note Even though this API uses a #svn_wc_context_t, it accepts a
 * (possibly) relative path and returns a (possibly) relative path in
 * @a *anchor.  The reason being that the outputs are generally used to
 * open access batons, and such opening currently requires relative paths.
 * In the long-run, I expect this API to be removed from 1.7, due to the
 * remove of access batons, but for the time being, the #svn_wc_context_t
 * parameter allows us to avoid opening a duplicate database, just for this
 * function.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_get_actual_target2(const char **anchor,
                          const char **target,
                          svn_wc_context_t *wc_ctx,
                          const char *path,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/** Similar to svn_wc_get_actual_target2(), but without the wc context, and
 * with a absolute path.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_actual_target(const char *path,
                         const char **anchor,
                         const char **target,
                         apr_pool_t *pool);


/**
 * @defgroup svn_wc_update_switch Update and switch (update-like functionality)
 * @{
 */

/**
 * A simple callback type to wrap svn_ra_get_file();  see that
 * docstring for more information.
 *
 * This technique allows libsvn_client to 'wrap' svn_ra_get_file() and
 * pass it down into libsvn_wc functions, thus allowing the WC layer
 * to legally call the RA function via (blind) callback.
 *
 * @since New in 1.5
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
typedef svn_error_t *(*svn_wc_get_file_t)(void *baton,
                                          const char *path,
                                          svn_revnum_t revision,
                                          svn_stream_t *stream,
                                          svn_revnum_t *fetched_rev,
                                          apr_hash_t **props,
                                          apr_pool_t *pool);

/**
 * A simple callback type to wrap svn_ra_get_dir2() for avoiding issue #3569,
 * where a directory is updated to a revision without some of its children
 * recorded in the working copy. A future update won't bring these files in
 * because the repository assumes they are already there.
 *
 * We really only need the names of the dirents for a not-present marking,
 * but we also store the node-kind if we receive one.
 *
 * @a *dirents should be set to a hash mapping <tt>const char *</tt> child
 * names, to <tt>const svn_dirent_t *</tt> instances.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_wc_dirents_func_t)(void *baton,
                                              apr_hash_t **dirents,
                                              const char *repos_root_url,
                                              const char *repos_relpath,
                                              apr_pool_t *result_pool,
                                              apr_pool_t *scratch_pool);


/**
 * DEPRECATED -- please use APIs from svn_client.h
 *
 * ---
 *
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
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_update_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
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

/** Similar to svn_wc_get_update_editor4, but uses access batons and relative
 * path instead of a working copy context-abspath pair and
 * svn_wc_traversal_info_t instead of an externals callback.  Also,
 * @a fetch_func and @a fetch_baton are ignored.
 *
 * If @a ti is non-NULL, record traversal info in @a ti, for use by
 * post-traversal accessors such as svn_wc_edited_externals().
 *
 * All locks, both those in @a anchor and newly acquired ones, will be
 * released when the editor driver calls @c close_edit.
 *
 * Always sets @a adds_as_modification to TRUE, @a server_performs_filtering
 * and @a clean_checkout to FALSE.
 *
 * Uses a svn_wc_conflict_resolver_func_t conflict resolver instead of a
 * svn_wc_conflict_resolver_func2_t.
 *
 * This function assumes that @a diff3_cmd is path encoded. Later versions
 * assume utf-8.
 *
 * Always passes a null dirent function.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_update_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          svn_wc_get_file_t fetch_func,
                          void *fetch_baton,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *ti,
                          apr_pool_t *pool);


/**
 * Similar to svn_wc_get_update_editor3() but with the @a
 * allow_unver_obstructions parameter always set to FALSE, @a
 * conflict_func and baton set to NULL, @a fetch_func and baton set to
 * NULL, @a preserved_exts set to NULL, @a depth_is_sticky set to
 * FALSE, and @a depth set according to @a recurse: if @a recurse is
 * TRUE, pass #svn_depth_infinity, if FALSE, pass #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_update_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *ti,
                          apr_pool_t *pool);

/**
 * Similar to svn_wc_get_update_editor2(), but takes an svn_wc_notify_func_t
 * instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_update_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *ti,
                         apr_pool_t *pool);

/**
 * DEPRECATED -- please use APIs from svn_client.h
 *
 * ---
 *
 * A variant of svn_wc_get_update_editor4().
 *
 * Set @a *editor and @a *edit_baton to an editor and baton for "switching"
 * a working copy to a new @a switch_url.  (Right now, this URL must be
 * within the same repository that the working copy already comes
 * from.)  @a switch_url must not be @c NULL.
 *
 * All other parameters behave as for svn_wc_get_update_editor4().
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_switch_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          const char *switch_url,
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

/** Similar to svn_wc_get_switch_editor4, but uses access batons and relative
 * path instead of a working copy context and svn_wc_traversal_info_t instead
 * of an externals callback.
 *
 * If @a ti is non-NULL, record traversal info in @a ti, for use by
 * post-traversal accessors such as svn_wc_edited_externals().
 *
 * All locks, both those in @a anchor and newly acquired ones, will be
 * released when the editor driver calls @c close_edit.
 *
 * Always sets @a server_performs_filtering to FALSE.
 *
 * Uses a svn_wc_conflict_resolver_func_t conflict resolver instead of a
 * svn_wc_conflict_resolver_func2_t.
 *
 * This function assumes that @a diff3_cmd is path encoded. Later versions
 * assume utf-8.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_switch_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *ti,
                          apr_pool_t *pool);

/**
 * Similar to svn_wc_get_switch_editor3() but with the
 * @a allow_unver_obstructions parameter always set to FALSE,
 * @a preserved_exts set to NULL, @a conflict_func and baton set to NULL,
 * @a depth_is_sticky set to FALSE, and @a depth set according to @a
 * recurse: if @a recurse is TRUE, pass #svn_depth_infinity, if
 * FALSE, pass #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_switch_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *ti,
                          apr_pool_t *pool);

/**
 * Similar to svn_wc_get_switch_editor2(), but takes an
 * #svn_wc_notify_func_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_switch_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         const char *switch_url,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *ti,
                         apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_properties Properties
 * @{
 */

/** Set @a *props to a hash table mapping <tt>char *</tt> names onto
 * <tt>svn_string_t *</tt> values for all the regular properties of
 * @a local_abspath.  Allocate the table, names, and values in
 * @a result_pool.  If the node has no properties, then an empty hash
 * is returned.  Use @a wc_ctx to access the working copy, and @a
 * scratch_pool for temporary allocations.
 *
 * If the node does not exist, #SVN_ERR_WC_PATH_NOT_FOUND is returned.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_prop_list2(apr_hash_t **props,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);

/** Similar to svn_wc_prop_list2() but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_prop_list(apr_hash_t **props,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 apr_pool_t *pool);


/** Return the set of "pristine" properties for @a local_abspath.
 *
 * There are node states where properties do not make sense. For these
 * cases, NULL will be returned in @a *props. Otherwise, a hash table
 * will always be returned (but may be empty, indicating no properties).
 *
 * If the node is locally-added, then @a *props will be set to NULL since
 * pristine properties are undefined. Note: if this addition is replacing a
 * previously-deleted node, then the replaced node's properties are not
 * available until the addition is reverted.
 *
 * If the node has been copied (from another node in the repository), then
 * the pristine properties will correspond to those original properties.
 *
 * If the node is locally-deleted, these properties will correspond to
 * the BASE node's properties, as checked-out from the repository. Note: if
 * this deletion is a child of a copy, then the pristine properties will
 * correspond to that copy's properties, not any potential BASE node. The
 * BASE node's properties will not be accessible until the copy is reverted.
 *
 * Nodes that are incomplete, excluded, absent, or not present at the
 * node's revision will return NULL in @a props.
 *
 * If the node is not versioned, SVN_ERR_WC_PATH_NOT_FOUND will be returned.
 *
 * @a props will be allocated in @a result_pool, and all temporary
 * allocations will be performed in @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_get_pristine_props(apr_hash_t **props,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/** Set @a *value to the value of property @a name for @a local_abspath,
 * allocating @a *value in @a result_pool.  If no such prop, set @a *value
 * to @c NULL. @a name may be a regular or wc property; if it is an
 * entry property, return the error #SVN_ERR_BAD_PROP_KIND.  @a wc_ctx
 * is used to access the working copy.
 *
 * If @a local_abspath is not a versioned path, return
 * #SVN_ERR_WC_PATH_NOT_FOUND
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_prop_get2(const svn_string_t **value,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool);

/** Similar to svn_wc_prop_get2(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * When @a path is not versioned, set @a *value to NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_prop_get(const svn_string_t **value,
                const char *name,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool);

/**
 * Set property @a name to @a value for @a local_abspath, or if @a value is
 * NULL, remove property @a name from @a local_abspath.  Use @a wc_ctx to
 * access @a local_abspath.
 *
 * @a name may be a regular property or a "wc property".  If @a name is
 * an "entry property", return the error #SVN_ERR_BAD_PROP_KIND (even if
 * @a skip_checks is TRUE).
 *
 * If @a name is a "wc property", then just update the WC DAV cache for
 * @a local_abspath with @a name and @a value.  In this case, @a depth
 * must be #svn_depth_empty.
 *
 * The rest of this description applies when @a name is a regular property.
 *
 * If @a name is a name in the reserved "svn:" name space, and @a value is
 * non-null, then canonicalize the property value and check the property
 * name and value as documented for svn_wc_canonicalize_svn_prop().
 * @a skip_checks controls the level of checking as documented there.
 *
 * Return an error if the canonicalization or the check fails.
 * The error will be either #SVN_ERR_ILLEGAL_TARGET (if the
 * property is not appropriate for @a path), or
 * #SVN_ERR_BAD_MIME_TYPE (if @a name is "svn:mime-type", but @a value
 * is not a valid mime-type).
 * ### That is not currently right -- several other errors can be raised.
 *
 * @a depth follows the usual semantics for depth.
 *
 * @a changelist_filter is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose properties are
 * set; that is, don't set properties on any item unless it's a member
 * of one of those changelists.  If @a changelist_filter is empty (or
 * altogether @c NULL), no changelist filtering occurs.
 *
 * If @a cancel_func is non-NULL, then it will be invoked (with the
 * @a cancel_baton value passed) during the processing of the property
 * set (i.e. when @a depth indicates some amount of recursion).
 *
 * For each file or directory operated on, @a notify_func will be called
 * with its path and the @a notify_baton.  @a notify_func may be @c NULL
 * if you are not interested in this information.
 *
 * Use @a scratch_pool for temporary allocation.
 *
 * @note If the caller is setting both svn:mime-type and svn:eol-style in
 * separate calls, and @a skip_checks is false, there is an ordering
 * dependency between them, as the validity check for svn:eol-style makes
 * use of the current value of svn:mime-type.
 *
 * ### The error code on validity check failure should be specified, and
 *     should be a single code or a very small set of possibilities.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_prop_set4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 const svn_string_t *value,
                 svn_depth_t depth,
                 svn_boolean_t skip_checks,
                 const apr_array_header_t *changelist_filter,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool);

/** Similar to svn_wc_prop_set4(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair, no @a depth parameter, no changelist
 * filtering (for the depth-based property setting), and no cancellation.
 *
 * @since New in 1.6.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_prop_set3(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool);


/**
 * Like svn_wc_prop_set3(), but without the notification callbacks.
 *
 * @since New in 1.2.
 * @deprecated Provided for backwards compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_prop_set2(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 apr_pool_t *pool);


/**
 * Like svn_wc_prop_set2(), but with @a skip_checks always FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_prop_set(const char *name,
                const svn_string_t *value,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool);


/** Return TRUE iff @a name is a 'normal' property name.  'Normal' is
 * defined as a user-visible and user-tweakable property that shows up
 * when you fetch a proplist.
 *
 * The function currently parses the namespace like so:
 *
 *   - 'svn:wc:'  ==>  a wcprop, stored/accessed separately via different API.
 *
 *   - 'svn:entry:' ==> an "entry" prop, shunted into the 'entries' file.
 *
 * If these patterns aren't found, then the property is assumed to be
 * Normal.
 */
svn_boolean_t
svn_wc_is_normal_prop(const char *name);



/** Return TRUE iff @a name is a 'wc' property name. */
svn_boolean_t
svn_wc_is_wc_prop(const char *name);

/** Return TRUE iff @a name is a 'entry' property name. */
svn_boolean_t
svn_wc_is_entry_prop(const char *name);

/** Callback type used by #svn_wc_canonicalize_svn_prop.
 *
 * If @a mime_type is non-null, it sets @a *mime_type to the value of
 * #SVN_PROP_MIME_TYPE for the path passed to
 * #svn_wc_canonicalize_svn_prop (allocated from @a pool).  If @a
 * stream is non-null, it writes the contents of the file to @a
 * stream.
 *
 * (Currently, this is used if you are attempting to set the
 * #SVN_PROP_EOL_STYLE property, to make sure that the value matches
 * the mime type and contents.)
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_wc_canonicalize_svn_prop_get_file_t)(
  const svn_string_t **mime_type,
  svn_stream_t *stream,
  void *baton,
  apr_pool_t *pool);


/** Canonicalize the value of an svn:* property @a propname with
 * value @a propval.
 *
 * If the property is not appropriate for a node of kind @a kind, or
 * is otherwise invalid, throw an error.  Otherwise, set @a *propval_p
 * to a canonicalized version of the property value.
 *
 * The exact set of canonicalizations and checks may vary across different
 * versions of this API.  Currently:
 *
 *   - svn:executable
 *   - svn:needs-lock
 *   - svn:special
 *     - set the value to '*'
 *
 *   - svn:keywords
 *     - strip leading and trailing white space
 *
 *   - svn:ignore
 *   - svn:global-ignores
 *   - svn:auto-props
 *     - add a final a newline character if missing
 *
 *   - svn:externals
 *     - add a final a newline character if missing
 *     - check for valid syntax
 *     - check for no duplicate entries
 *
 *   - svn:mergeinfo
 *     - canonicalize
 *     - check for validity
 *
 * Also, unless @a skip_some_checks is TRUE:
 *
 *   - svn:eol-style
 *     - strip leading and trailing white space
 *     - check value is recognized
 *     - check file content has a self-consistent EOL style
 *       (but not necessarily that it matches @a propval)
 *
 *   - svn:mime-type
 *     - strip white space
 *     - check for reasonable syntax
 *
 * The EOL-style check (if not skipped) requires access to the contents and
 * MIME type of the target if it is a file.  It will call @a prop_getter with
 * @a getter_baton.  The callback must set the MIME type and/or write the
 * contents of the file to the given stream.  If @a skip_some_checks is true,
 * then @a prop_getter is not used and may be NULL.
 *
 * @a path should be the path of the file in question; it is only used
 * for error messages.
 *
 * ### The error code on validity check failure should be specified, and
 *     should be a single code or a very small set of possibilities.
 *
 * ### This is not actually related to the WC, but it does need to call
 * ### svn_wc_parse_externals_description3.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_wc_canonicalize_svn_prop(const svn_string_t **propval_p,
                             const char *propname,
                             const svn_string_t *propval,
                             const char *path,
                             svn_node_kind_t kind,
                             svn_boolean_t skip_some_checks,
                             svn_wc_canonicalize_svn_prop_get_file_t prop_getter,
                             void *getter_baton,
                             apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_diffs Diffs
 * @{
 */

/**
 * DEPRECATED -- please use APIs from svn_client.h
 *
 * ---
 *
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
 * @a callbacks/@a callback_baton is the callback table to use.
 *
 * If @a depth is #svn_depth_empty, just diff exactly @a target or
 * @a anchor_path if @a target is empty.  If #svn_depth_files then do the same
 * and for top-level file entries as well (if any).  If
 * #svn_depth_immediates, do the same as #svn_depth_files but also diff
 * top-level subdirectories at #svn_depth_empty.  If #svn_depth_infinity,
 * then diff fully recursively.
 *
 * @a ignore_ancestry determines whether paths that have discontinuous node
 * ancestry are treated as delete/add or as simple modifications.  If
 * @a ignore_ancestry is @c FALSE, then any discontinuous node ancestry will
 * result in the diff given as a full delete followed by an add.
 *
 * @a show_copies_as_adds determines whether paths added with history will
 * appear as a diff against their copy source, or whether such paths will
 * appear as if they were newly added in their entirety.
 * @a show_copies_as_adds implies not @a ignore_ancestry.
 *
 * If @a use_git_diff_format is TRUE, copied paths will be treated as added
 * if they weren't modified after being copied. This allows the callbacks
 * to generate appropriate --git diff headers for such files.
 * @a use_git_diff_format implies @a show_copies_as_adds, and as such implies
 * not @a ignore_ancestry.
 *
 * Normally, the difference from repository->working_copy is shown.
 * If @a reverse_order is TRUE, then show working_copy->repository diffs.
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
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor6(const svn_delta_editor_t **editor,
                        void **edit_baton,
                        svn_wc_context_t *wc_ctx,
                        const char *anchor_abspath,
                        const char *target,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t show_copies_as_adds,
                        svn_boolean_t use_git_diff_format,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_boolean_t server_performs_filtering,
                        const apr_array_header_t *changelist_filter,
                        const svn_wc_diff_callbacks4_t *callbacks,
                        void *callback_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_get_diff_editor6(), but with an access baton and relative
 * path. @a server_performs_filtering always true and with a
 * #svn_wc_diff_callbacks3_t instead of #svn_wc_diff_callbacks4_t,
 * @a show_copies_as_adds, and @a use_git_diff_format set to @c FALSE.
 *
 * Diffs will be reported as below the relative path stored in @a anchor.
 *
 * @since New in 1.6.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor5(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks3_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelist_filter,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool);

/**
 * Similar to svn_wc_get_diff_editor5(), but with an
 * #svn_wc_diff_callbacks2_t instead of #svn_wc_diff_callbacks3_t.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor4(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelist_filter,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool);

/**
 * Similar to svn_wc_get_diff_editor4(), but with @a changelist_filter
 * passed as @c NULL, and @a depth set to #svn_depth_infinity if @a
 * recurse is TRUE, or #svn_depth_files if @a recurse is FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.

 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor3(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool);


/**
 * Similar to svn_wc_get_diff_editor3(), but with an
 * #svn_wc_diff_callbacks_t instead of #svn_wc_diff_callbacks2_t.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor2(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool);


/**
 * Similar to svn_wc_get_diff_editor2(), but with @a ignore_ancestry
 * always set to @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_diff_editor(svn_wc_adm_access_t *anchor,
                       const char *target,
                       const svn_wc_diff_callbacks_t *callbacks,
                       void *callback_baton,
                       svn_boolean_t recurse,
                       svn_boolean_t use_text_base,
                       svn_boolean_t reverse_order,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       const svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_pool_t *pool);


/**
 * Compare working copy against the text-base.
 *
 * @a target_abspath represents the base of the hierarchy to be compared.
 *
 * @a callbacks/@a callback_baton is the callback table to use when two
 * files are to be compared.
 *
 * If @a depth is #svn_depth_empty, just diff exactly @a target_path.
 * If #svn_depth_files then do the same
 * and for top-level file entries as well (if any).  If
 * #svn_depth_immediates, do the same as #svn_depth_files but also diff
 * top-level subdirectories at #svn_depth_empty.  If #svn_depth_infinity,
 * then diff fully recursively.
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
 * @a changelist_filter is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose differences are
 * reported; that is, don't generate diffs about any item unless
 * it's a member of one of those changelists.  If @a changelist_filter is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * If @a cancel_func is non-NULL, invoke it with @a cancel_baton at various
 * points during the operation.  If it returns an error (typically
 * #SVN_ERR_CANCELLED), return that error immediately.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_diff6(svn_wc_context_t *wc_ctx,
             const char *target_abspath,
             const svn_wc_diff_callbacks4_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             svn_boolean_t show_copies_as_adds,
             svn_boolean_t use_git_diff_format,
             const apr_array_header_t *changelist_filter,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_diff6(), but with a #svn_wc_diff_callbacks3_t argument
 * instead of #svn_wc_diff_callbacks4_t, @a show_copies_as_adds,
 * and @a use_git_diff_format set to * @c FALSE.
 * It also doesn't allow specifying a cancel function.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_diff5(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks3_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelist_filter,
             apr_pool_t *pool);

/**
 * Similar to svn_wc_diff5(), but with a #svn_wc_diff_callbacks2_t argument
 * instead of #svn_wc_diff_callbacks3_t.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_diff4(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelist_filter,
             apr_pool_t *pool);

/**
 * Similar to svn_wc_diff4(), but with @a changelist_filter passed @c NULL,
 * and @a depth set to #svn_depth_infinity if @a recurse is TRUE, or
 * #svn_depth_files if @a recurse is FALSE.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_diff3(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool);

/**
 * Similar to svn_wc_diff3(), but with a #svn_wc_diff_callbacks_t argument
 * instead of #svn_wc_diff_callbacks2_t.
 *
 * @since New in 1.1.
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_diff2(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool);

/**
 * Similar to svn_wc_diff2(), but with @a ignore_ancestry always set
 * to @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_diff(svn_wc_adm_access_t *anchor,
            const char *target,
            const svn_wc_diff_callbacks_t *callbacks,
            void *callback_baton,
            svn_boolean_t recurse,
            apr_pool_t *pool);


/** Given a @a local_abspath to a file or directory under version control,
 * discover any local changes made to properties and/or the set of 'pristine'
 * properties.  @a wc_ctx will be used to access the working copy.
 *
 * If @a propchanges is non-@c NULL, return these changes as an array of
 * #svn_prop_t structures stored in @a *propchanges.  The structures and
 * array will be allocated in @a result_pool.  If there are no local property
 * modifications on @a local_abspath, then set @a *propchanges will be empty.
 *
 * If @a original_props is non-@c NULL, then set @a *original_props to
 * hashtable (<tt>const char *name</tt> -> <tt>const svn_string_t *value</tt>)
 * that represents the 'pristine' property list of @a path.  This hashtable is
 * allocated in @a result_pool.
 *
 * Use @a scratch_pool for temporary allocations.
 */
svn_error_t *
svn_wc_get_prop_diffs2(apr_array_header_t **propchanges,
                       apr_hash_t **original_props,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/** Similar to svn_wc_get_prop_diffs2(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_prop_diffs(apr_array_header_t **propchanges,
                      apr_hash_t **original_props,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_merging Merging
 * @{
 */

/** The outcome of a merge carried out (or tried as a dry-run) by
 * svn_wc_merge()
 */
typedef enum svn_wc_merge_outcome_t
{
   /** The working copy is (or would be) unchanged.  The changes to be
    * merged were already present in the working copy
    */
   svn_wc_merge_unchanged,

   /** The working copy has been (or would be) changed. */
   svn_wc_merge_merged,

   /** The working copy has been (or would be) changed, but there was (or
    * would be) a conflict
    */
   svn_wc_merge_conflict,

   /** No merge was performed, probably because the target file was
    * either absent or not under version control.
    */
   svn_wc_merge_no_merge

} svn_wc_merge_outcome_t;

/** Given absolute paths to three fulltexts, merge the differences between
 * @a left_abspath and @a right_abspath into @a target_abspath.
 * It may help to know that @a left_abspath, @a right_abspath and @a
 * target_abspath correspond to "OLDER", "YOURS", and "MINE",
 * respectively, in the diff3 documentation.
 *
 * @a wc_ctx should contain a write lock for the directory containing @a
 * target_abspath.
 *
 * This function assumes that @a left_abspath and @a right_abspath are
 * in repository-normal form (linefeeds, with keywords contracted); if
 * necessary, @a target_abspath is temporarily converted to this form to
 * receive the changes, then translated back again.
 *
 * If @a target_abspath is absent, or present but not under version
 * control, then set @a *merge_content_outcome to #svn_wc_merge_no_merge and
 * return success without merging anything.  (The reasoning is that if
 * the file is not versioned, then it is probably unrelated to the
 * changes being considered, so they should not be merged into it.
 * Furthermore, merging into an unversioned file is a lossy operation.)
 *
 * @a dry_run determines whether the working copy is modified.  When it
 * is @c FALSE the merge will cause @a target_abspath to be modified, when
 * it is @c TRUE the merge will be carried out to determine the result but
 * @a target_abspath will not be modified.
 *
 * If @a diff3_cmd is non-NULL, then use it as the diff3 command for
 * any merging; otherwise, use the built-in merge code.  If @a
 * merge_options is non-NULL, either pass its elements to @a diff3_cmd or
 * parse it and use as options to the internal merge code (see
 * svn_diff_file_options_parse()).  @a merge_options must contain
 * <tt>const char *</tt> elements.
 *
 * If @a merge_props_state is non-NULL, merge @a prop_diff into the
 * working properties before merging the text.  (If @a merge_props_state
 * is NULL, do not merge any property changes; in this case, @a prop_diff
 * is only used to help determine the text merge result.)  Handle any
 * conflicts as described for svn_wc_merge_props3(), with the parameters
 * @a dry_run, @a conflict_func and @a conflict_baton.  Return the
 * outcome of the property merge in @a *merge_props_state.
 *
 * The outcome of the text merge is returned in @a *merge_content_outcome. If
 * there is a conflict and @a dry_run is @c FALSE, then attempt to call @a
 * conflict_func with @a conflict_baton (if non-NULL).  If the
 * conflict callback cannot resolve the conflict, then:
 *
 *   * Put conflict markers around the conflicting regions in
 *     @a target_abspath, labeled with @a left_label, @a right_label, and
 *     @a target_label.  (If any of these labels are @c NULL, default
 *     values will be used.)
 *
 *   * Copy @a left_abspath, @a right_abspath, and the original @a
 *     target_abspath to unique names in the same directory as @a
 *     target_abspath, ending with the suffixes ".LEFT_LABEL", ".RIGHT_LABEL",
 *     and ".TARGET_LABEL" respectively.
 *
 *   * Mark @a target_abspath as "text-conflicted", and track the above
 *     mentioned backup files as well.
 *
 *   * If @a left_version and/or @a right_version are not NULL, provide
 *     these values to the conflict handler and track these while the conflict
 *     exists.
 *
 * Binary case:
 *
 *  If @a target_abspath is a binary file, then no merging is attempted,
 *  the merge is deemed to be a conflict.  If @a dry_run is @c FALSE the
 *  working @a target_abspath is untouched, and copies of @a left_abspath and
 *  @a right_abspath are created next to it using @a left_label and
 *  @a right_label. @a target_abspath is marked as "text-conflicted", and
 *  begins tracking the two backup files and the version information.
 *
 * If @a dry_run is @c TRUE no files are changed.  The outcome of the merge
 * is returned in @a *merge_content_outcome.
 * ### (and what about @a *merge_props_state?)
 *
 * ### BH: Two kinds of outcome is not how it should be.
 *
 * ### For text, we report the outcome as 'merged' if there was some
 *     incoming change that we dealt with (even if we decided to no-op?)
 *     but the callers then convert this outcome into a notification
 *     of 'merged' only if there was already a local modification;
 *     otherwise they notify it as simply 'updated'.  But for props
 *     we report a notify state of 'merged' here if there was an
 *     incoming change regardless of the local-mod state.  Inconsistent.
 *
 * Use @a scratch_pool for any temporary allocation.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc_merge5(enum svn_wc_merge_outcome_t *merge_content_outcome,
              enum svn_wc_notify_state_t *merge_props_state,
              svn_wc_context_t *wc_ctx,
              const char *left_abspath,
              const char *right_abspath,
              const char *target_abspath,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              const svn_wc_conflict_version_t *left_version,
              const svn_wc_conflict_version_t *right_version,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              apr_hash_t *original_props,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func2_t conflict_func,
              void *conflict_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool);

/** Similar to svn_wc_merge5() but with @a merge_props_state and @a
 * original_props always passed as NULL.
 *
 * Unlike svn_wc_merge5(), this function doesn't merge property
 * changes.  Callers of this function must first use
 * svn_wc_merge_props3() to get this functionality.
 *
 * @since New in 1.7.
 * @deprecated Provided for backwards compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge4(enum svn_wc_merge_outcome_t *merge_outcome,
              svn_wc_context_t *wc_ctx,
              const char *left_abspath,
              const char *right_abspath,
              const char *target_abspath,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              const svn_wc_conflict_version_t *left_version,
              const svn_wc_conflict_version_t *right_version,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func2_t conflict_func,
              void *conflict_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool);


/** Similar to svn_wc_merge4() but takes relative paths and an access
 * baton. It doesn't support a cancel function or tracking origin version
 * information.
 *
 * Uses a svn_wc_conflict_resolver_func_t conflict resolver instead of a
 * svn_wc_conflict_resolver_func2_t.
 *
 * This function assumes that @a diff3_cmd is path encoded. Later versions
 * assume utf-8.
 *
 * @since New in 1.5.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge3(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func_t conflict_func,
              void *conflict_baton,
              apr_pool_t *pool);


/** Similar to svn_wc_merge3(), but with @a prop_diff, @a
 * conflict_func, @a conflict_baton set to NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge2(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              apr_pool_t *pool);


/** Similar to svn_wc_merge2(), but with @a merge_options set to NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge(const char *left,
             const char *right,
             const char *merge_target,
             svn_wc_adm_access_t *adm_access,
             const char *left_label,
             const char *right_label,
             const char *target_label,
             svn_boolean_t dry_run,
             enum svn_wc_merge_outcome_t *merge_outcome,
             const char *diff3_cmd,
             apr_pool_t *pool);


/** Given a @a local_abspath under version control, merge an array of @a
 * propchanges into the path's existing properties.  @a propchanges is
 * an array of #svn_prop_t objects, and @a baseprops is a hash
 * representing the original set of properties that @a propchanges is
 * working against.  @a wc_ctx contains a lock for @a local_abspath.
 *
 * Only the working properties will be changed.
 *
 * If @a state is non-NULL, set @a *state to the state of the properties
 * after the merge.
 *
 * If a conflict is found when merging a property, and @a dry_run is
 * false and @a conflict_func is not null, then call @a conflict_func
 * with @a conflict_baton and a description of the conflict.  If any
 * conflicts are not resolved by such callbacks, describe the unresolved
 * conflicts in a temporary .prej file (or append to an already-existing
 * .prej file) and mark the path as conflicted in the WC DB.
 *
 * If @a cancel_func is non-NULL, invoke it with @a cancel_baton at various
 * points during the operation.  If it returns an error (typically
 * #SVN_ERR_CANCELLED), return that error immediately.
 *
 * If @a local_abspath is not under version control, return the error
 * #SVN_ERR_WC_PATH_NOT_FOUND and don't touch anyone's properties.
 *
 * If @a local_abspath has a status in which it doesn't have properties
 * (E.g. deleted) return the error SVN_ERR_WC_PATH_UNEXPECTED_STATUS.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_merge_props3(svn_wc_notify_state_t *state,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func2_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool);


/** Similar to svn_wc_merge_props3, but takes an access baton and relative
 * path, no cancel_function, and no left and right version.
 *
 * This function has the @a base_merge parameter which (when TRUE) will
 * apply @a propchanges to this node's pristine set of properties. This
 * functionality is not supported since API version 1.7 and will give an
 * error if requested (unless @a dry_run is TRUE). For details see
 * 'notes/api-errata/1.7/wc006.txt'.
 *
 * Uses a svn_wc_conflict_resolver_func_t conflict resolver instead of a
 * svn_wc_conflict_resolver_func2_t.
 *
 * For compatibility reasons this function returns
 * #SVN_ERR_UNVERSIONED_RESOURCE, when svn_wc_merge_props3 would return either
 * #SVN_ERR_WC_PATH_NOT_FOUND or #SVN_ERR_WC_PATH_UNEXPECTED_STATUS.
 *
 * @since New in 1.5. The base_merge option is not supported since 1.7.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge_props2(svn_wc_notify_state_t *state,
                    const char *path,
                    svn_wc_adm_access_t *adm_access,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    apr_pool_t *pool);


/**
 * Same as svn_wc_merge_props2(), but with a @a conflict_func (and
 * baton) of NULL.
 *
 * @since New in 1.3. The base_merge option is not supported since 1.7.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge_props(svn_wc_notify_state_t *state,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_hash_t *baseprops,
                   const apr_array_header_t *propchanges,
                   svn_boolean_t base_merge,
                   svn_boolean_t dry_run,
                   apr_pool_t *pool);


/**
 * Similar to svn_wc_merge_props(), but no baseprops are given.
 * Instead, it's assumed that the incoming propchanges are based
 * against the working copy's own baseprops.  While this assumption is
 * correct for 'svn update', it's incorrect for 'svn merge', and can
 * cause flawed behavior.  (See issue #2035.)
 *
 * @since The base_merge option is not supported since 1.7.
 * @deprecated Provided for backward compatibility with the 1.2 API.
 * Replaced by svn_wc_merge_props().
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_merge_prop_diffs(svn_wc_notify_state_t *state,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const apr_array_header_t *propchanges,
                        svn_boolean_t base_merge,
                        svn_boolean_t dry_run,
                        apr_pool_t *pool);

/** @} */


/** Given a @a path to a wc file, return in @a *contents a readonly stream to
 * the pristine contents of the file that would serve as base content for the
 * next commit. That means:
 *
 * When there is no change in node history scheduled, i.e. when there are only
 * local text-mods, prop-mods or a delete, return the last checked-out or
 * updated-/switched-to contents of the file.
 *
 * If the file is simply added or replaced (no copy-/move-here involved),
 * set @a *contents to @c NULL.
 *
 * When the file has been locally copied-/moved-here, return the contents of
 * the copy/move source (even if the copy-/move-here replaces a locally
 * deleted file).
 *
 * If @a local_abspath refers to an unversioned or non-existent path, return
 * @c SVN_ERR_WC_PATH_NOT_FOUND. Use @a wc_ctx to access the working copy.
 * @a contents may not be @c NULL (unlike @a *contents).
 *
 * @since New in 1.7. */
svn_error_t *
svn_wc_get_pristine_contents2(svn_stream_t **contents,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/** Similar to svn_wc_get_pristine_contents2, but takes no working copy
 * context and a path that can be relative
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_pristine_contents(svn_stream_t **contents,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


/** Set @a *pristine_path to the path of the "normal" pristine text file for
 * the versioned file @a path.
 *
 * If @a path does not have a pristine text, set @a *pristine_path to a path where
 * nothing exists on disk (in a directory that does exist).
 *
 * @note: Before version 1.7, the behaviour in that case was to provide the
 * path where the pristine text *would be* if it were present.  The new
 * behaviour is intended to provide backward compatibility for callers that
 * open or test the provided path immediately, and not for callers that
 * store the path for later use.
 *
 * @deprecated Provided for backwards compatibility with the 1.5 API.
 * Callers should use svn_wc_get_pristine_contents() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_pristine_copy_path(const char *path,
                              const char **pristine_path,
                              apr_pool_t *pool);


/**
 * Recurse from @a local_abspath, cleaning up unfinished tasks.  Perform
 * any temporary allocations in @a scratch_pool.  If @a break_locks is TRUE
 * Any working copy locks under @a local_abspath will be taken over and then
 * cleared by this function.
 * WARNING: If @a break_locks is TRUE there is no mechanism that will protect
 * locks that are still being used.
 *
 * If @a fix_recorded_timestamps is TRUE the recorded timestamps of unmodified
 * files will be updated, which will improve performance of future is-modified
 * checks.
 *
 * If @a clear_dav_cache is @c TRUE, the caching of DAV information for older
 * mod_dav served repositories is cleared. This clearing invalidates some
 * cached information used for pre-HTTPv2 repositories.
 *
 * If @a vacuum_pristines is TRUE, try to remove unreferenced pristines from
 * the working copy. (Will not remove anything unless the obtained lock applies
 * to the entire working copy)
 *
 * If @a cancel_func is non-NULL, invoke it with @a cancel_baton at various
 * points during the operation.  If it returns an error (typically
 * #SVN_ERR_CANCELLED), return that error immediately.
 *
 * If @a notify_func is non-NULL, invoke it with @a notify_baton to report
 * the progress of the operation.
 *
 * @note In 1.9, @a notify_func does not get called at all.  This may change
 * in later releases.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_wc_cleanup4(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_boolean_t break_locks,
                svn_boolean_t fix_recorded_timestamps,
                svn_boolean_t clear_dav_cache,
                svn_boolean_t vacuum_pristines,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                svn_wc_notify_func2_t notify_func,
                void *notify_baton,
                apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_cleanup4() but will always break locks, fix recorded
 * timestamps, clear the dav cache and vacuum pristines. This function also
 * doesn't support notifications.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_cleanup3(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_cleanup3() but uses relative paths and creates its own
 * #svn_wc_context_t.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_cleanup2(const char *path,
                const char *diff3_cmd,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool);

/**
 * Similar to svn_wc_cleanup2(). @a optional_adm_access is an historic
 * relic and not used, it may be NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_cleanup(const char *path,
               svn_wc_adm_access_t *optional_adm_access,
               const char *diff3_cmd,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool);

/** Callback for retrieving a repository root for a url from upgrade.
 *
 * Called by svn_wc_upgrade() when no repository root and/or repository
 * uuid are recorded in the working copy. For normal Subversion 1.5 and
 * later working copies, this callback will not be used.
 *
 * @since New in 1.7.
 */
typedef svn_error_t * (*svn_wc_upgrade_get_repos_info_t)(
                                    const char **repos_root,
                                    const char **repos_uuid,
                                    void *baton,
                                    const char *url,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);


/**
 * Upgrade the working copy at @a local_abspath to the latest metadata
 * storage format.  @a local_abspath should be an absolute path to the
 * root of the working copy.
 *
 * If @a cancel_func is non-NULL, invoke it with @a cancel_baton at
 * various points during the operation.  If it returns an error
 * (typically #SVN_ERR_CANCELLED), return that error immediately.
 *
 * For each directory converted, @a notify_func will be called with
 * in @a notify_baton action #svn_wc_notify_upgraded_path and as path
 * the path of the upgraded directory. @a notify_func may be @c NULL
 * if this notification is not needed.
 *
 * If the old working copy doesn't contain a repository root and/or
 * repository uuid, @a repos_info_func (if non-NULL) will be called
 * with @a repos_info_baton to provide the missing information.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_upgrade(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_wc_upgrade_get_repos_info_t repos_info_func,
               void *repos_info_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool);


/** Relocation validation callback typedef.
 *
 * Called for each relocated file/directory.  @a uuid, if non-NULL, contains
 * the expected repository UUID, @a url contains the tentative URL.
 *
 * @a baton is a closure object; it should be provided by the
 * implementation, and passed by the caller.
 *
 * If @a root_url is passed, then the implementation should make sure that
 * @a url is the repository root.
 * @a pool may be used for temporary allocations.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_wc_relocation_validator3_t)(void *baton,
                                                       const char *uuid,
                                                       const char *url,
                                                       const char *root_url,
                                                       apr_pool_t *pool);

/** Similar to #svn_wc_relocation_validator3_t, but with
 * the @a root argument.
 *
 * If @a root is TRUE, then the implementation should make sure that @a url
 * is the repository root.  Else, it can be a URL inside the repository.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 */
typedef svn_error_t *(*svn_wc_relocation_validator2_t)(void *baton,
                                                       const char *uuid,
                                                       const char *url,
                                                       svn_boolean_t root,
                                                       apr_pool_t *pool);

/** Similar to #svn_wc_relocation_validator2_t, but without
 * the @a root and @a pool arguments.  @a uuid will not be NULL in this version
 * of the function.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
typedef svn_error_t *(*svn_wc_relocation_validator_t)(void *baton,
                                                      const char *uuid,
                                                      const char *url);

/** Recursively change repository references at @a wcroot_abspath
 * (which is the root directory of a working copy).  The pre-change
 * URL should begin with @a from, and the post-change URL will begin
 * with @a to.  @a validator (and its baton, @a validator_baton), will
 * be called for the newly generated base URL and calculated repo
 * root.
 *
 * @a wc_ctx is an working copy context.
 *
 * @a scratch_pool will be used for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_relocate4(svn_wc_context_t *wc_ctx,
                 const char *wcroot_abspath,
                 const char *from,
                 const char *to,
                 svn_wc_relocation_validator3_t validator,
                 void *validator_baton,
                 apr_pool_t *scratch_pool);

/** Similar to svn_wc_relocate4(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * @note As of the 1.7 API, @a path is required to be a working copy
 * root directory, and @a recurse is required to be TRUE.
 *
 * @since New in 1.5.
 * @deprecated Provided for limited backwards compatibility with the
 * 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_relocate3(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const char *from,
                 const char *to,
                 svn_boolean_t recurse,
                 svn_wc_relocation_validator3_t validator,
                 void *validator_baton,
                 apr_pool_t *pool);

/** Similar to svn_wc_relocate3(), but uses #svn_wc_relocation_validator2_t.
 *
 * @since New in 1.4.
 * @deprecated Provided for backwards compatibility with the 1.4 API. */
SVN_DEPRECATED
svn_error_t *
svn_wc_relocate2(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const char *from,
                 const char *to,
                 svn_boolean_t recurse,
                 svn_wc_relocation_validator2_t validator,
                 void *validator_baton,
                 apr_pool_t *pool);

/** Similar to svn_wc_relocate2(), but uses #svn_wc_relocation_validator_t.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API. */
SVN_DEPRECATED
svn_error_t *
svn_wc_relocate(const char *path,
                svn_wc_adm_access_t *adm_access,
                const char *from,
                const char *to,
                svn_boolean_t recurse,
                svn_wc_relocation_validator_t validator,
                void *validator_baton,
                apr_pool_t *pool);


/**
 * Revert changes to @a local_abspath.  Perform necessary allocations in
 * @a scratch_pool.
 *
 * @a wc_ctx contains the necessary locks required for performing the
 * operation.
 *
 * If @a depth is #svn_depth_empty, revert just @a path (if a
 * directory, then revert just the properties on that directory).
 * Else if #svn_depth_files, revert @a path and any files
 * directly under @a path if it is directory.  Else if
 * #svn_depth_immediates, revert all of the preceding plus
 * properties on immediate subdirectories; else if #svn_depth_infinity,
 * revert path and everything under it fully recursively.
 *
 * @a changelist_filter is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items reverted; that is,
 * don't revert any item unless it's a member of one of those
 * changelists.  If @a changelist_filter is empty (or altogether @c NULL),
 * no changelist filtering occurs.
 *
 * If @a clear_changelists is TRUE, then changelist information for the
 * paths is cleared.
 *
 * If @a metadata_only is TRUE, the working copy files are untouched, but
 * if there are conflict marker files attached to these files these
 * markers are removed.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton at
 * various points during the reversion process.  If it returns an
 * error (typically #SVN_ERR_CANCELLED), return that error
 * immediately.
 *
 * If @a use_commit_times is TRUE, then all reverted working-files
 * will have their timestamp set to the last-committed-time.  If
 * FALSE, the reverted working-files will be touched with the 'now' time.
 *
 * For each item reverted, @a notify_func will be called with @a notify_baton
 * and the path of the reverted item. @a notify_func may be @c NULL if this
 * notification is not needed.
 *
 * If @a path is not under version control, return the error
 * #SVN_ERR_UNVERSIONED_RESOURCE.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_wc_revert5(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_boolean_t clear_changelists,
               svn_boolean_t metadata_only,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool);

/** Similar to svn_wc_revert5() but with @a clear_changelists always set to
 * FALSE and @a metadata_only set to FALSE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_revert4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool);

/** Similar to svn_wc_revert4() but takes a relative path and access baton.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_revert3(const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool);

/**
 * Similar to svn_wc_revert3(), but with @a changelist_filter passed as @c
 * NULL, and @a depth set according to @a recursive: if @a recursive
 * is TRUE, @a depth is #svn_depth_infinity; if FALSE, @a depth is
 * #svn_depth_empty.
 *
 * @note Most APIs map @a recurse==FALSE to @a depth==svn_depth_files;
 * revert is deliberately different.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_revert2(const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive,
               svn_boolean_t use_commit_times,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool);

/**
 * Similar to svn_wc_revert2(), but takes an #svn_wc_notify_func_t instead.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_revert(const char *path,
              svn_wc_adm_access_t *parent_access,
              svn_boolean_t recursive,
              svn_boolean_t use_commit_times,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool);

/**
 * Restores a missing node, @a local_abspath using the @a wc_ctx. Records
 * the new last modified time of the file for status processing.
 *
 * If @a use_commit_times is TRUE, then set restored files' timestamps
 * to their last-commit-times.
 *
 * Returns SVN_ERROR_WC_PATH_NOT_FOUND if LOCAL_ABSPATH is not versioned and
 * SVN_ERROR_WC_PATH_UNEXPECTED_STATUS if LOCAL_ABSPATH is in a status where
 * it can't be restored.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_restore(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t use_commit_times,
               apr_pool_t *scratch_pool);


/* Tmp files */

/** Create a unique temporary file in administrative tmp/ area of
 * directory @a path.  Return a handle in @a *fp and the path
 * in @a *new_name. Either @a fp or @a new_name can be NULL.
 *
 * The flags will be <tt>APR_WRITE | APR_CREATE | APR_EXCL</tt> and
 * optionally @c APR_DELONCLOSE (if the @a delete_when argument is
 * set to #svn_io_file_del_on_close).
 *
 * This means that as soon as @a fp is closed, the tmp file will vanish.
 *
 * @since New in 1.4
 * @deprecated For compatibility with 1.6 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_create_tmp_file2(apr_file_t **fp,
                        const char **new_name,
                        const char *path,
                        svn_io_file_del_t delete_when,
                        apr_pool_t *pool);


/** Same as svn_wc_create_tmp_file2(), but with @a new_name set to @c NULL,
 * and without the ability to delete the file on pool cleanup.
 *
 * @deprecated For compatibility with 1.3 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_create_tmp_file(apr_file_t **fp,
                       const char *path,
                       svn_boolean_t delete_on_close,
                       apr_pool_t *pool);


/**
 * @defgroup svn_wc_translate EOL conversion and keyword expansion
 * @{
 */


/** Set @a xlated_path to a translated copy of @a src
 * or to @a src itself if no translation is necessary.
 * That is, if @a versioned_file's properties indicate newline conversion or
 * keyword expansion, point @a *xlated_path to a copy of @a src
 * whose newlines and keywords are converted using the translation
 * as requested by @a flags.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * When translating to the normal form, inconsistent eol styles will be
 * repaired when appropriate for the given setting.  When translating
 * from normal form, no EOL repair is performed (consistency is assumed).
 * This behaviour can be overridden by specifying
 * #SVN_WC_TRANSLATE_FORCE_EOL_REPAIR.
 *
 * The caller can explicitly request a new file to be returned by setting the
 * #SVN_WC_TRANSLATE_FORCE_COPY flag in @a flags.
 *
 * This function is generally used to get a file that can be compared
 * meaningfully against @a versioned_file's text base, if
 * @c SVN_WC_TRANSLATE_TO_NF is specified, against @a versioned_file itself
 * if @c SVN_WC_TRANSLATE_FROM_NF is specified.
 *
 * If a new output file is created, it is created in the temp file area
 * belonging to @a versioned_file.  By default it will be deleted at pool
 * cleanup.  If @c SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP is specified, the
 * default pool cleanup handler to remove @a *xlated_path is not registered.
 * If the input file is returned as the output, its lifetime is not
 * specified.
 *
 * If an error is returned, the effect on @a *xlated_path is undefined.
 *
 * @since New in 1.4
 * @deprecated Provided for compatibility with the 1.6 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_translated_file2(const char **xlated_path,
                        const char *src,
                        const char *versioned_file,
                        svn_wc_adm_access_t *adm_access,
                        apr_uint32_t flags,
                        apr_pool_t *pool);


/** Same as svn_wc_translated_file2, but will never clean up
 * temporary files.
 *
 * @deprecated Provided for compatibility with the 1.3 API
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_translated_file(const char **xlated_p,
                       const char *vfile,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t force_repair,
                       apr_pool_t *pool);


/** Returns a @a stream allocated in @a pool with access to the given
 * @a path taking the file properties from @a versioned_file using
 * @a adm_access.
 *
 * If @a flags includes #SVN_WC_TRANSLATE_FROM_NF, the stream will
 * translate from Normal Form to working copy form while writing to
 * @a path; stream read operations are not supported.
 * Conversely, if @a flags includes #SVN_WC_TRANSLATE_TO_NF, the stream will
 * translate from working copy form to Normal Form while reading from
 * @a path; stream write operations are not supported.
 *
 * The @a flags are the same constants as those used for
 * svn_wc_translated_file2().
 *
 * @since New in 1.5.
 * @deprecated Provided for compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_translated_stream(svn_stream_t **stream,
                         const char *path,
                         const char *versioned_file,
                         svn_wc_adm_access_t *adm_access,
                         apr_uint32_t flags,
                         apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_deltas Text/Prop Deltas Using an Editor
 * @{
 */

/** Send the local modifications for versioned file @a local_abspath (with
 * matching @a file_baton) through @a editor, then close @a file_baton
 * afterwards.  Use @a scratch_pool for any temporary allocation.
 *
 * If @a new_text_base_md5_checksum is non-NULL, set
 * @a *new_text_base_md5_checksum to the MD5 checksum of (@a local_abspath
 * translated to repository-normal form), allocated in @a result_pool.
 *
 * If @a new_text_base_sha1_checksum in non-NULL, store a copy of (@a
 * local_abspath translated to repository-normal form) in the pristine text
 * store, and set @a *new_text_base_sha1_checksum to its SHA-1 checksum.
 *
 * If @a fulltext, send the untranslated copy of @a local_abspath through
 * @a editor as full-text; else send it as svndiff against the current text
 * base.
 *
 * If sending a diff, and the recorded checksum for @a local_abspath's
 * text-base does not match the current actual checksum, then remove the tmp
 * copy (and set @a *tempfile to NULL if appropriate), and return the
 * error #SVN_ERR_WC_CORRUPT_TEXT_BASE.
 *
 * @note This is intended for use with both infix and postfix
 * text-delta styled editor drivers.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_transmit_text_deltas3(const svn_checksum_t **new_text_base_md5_checksum,
                             const svn_checksum_t **new_text_base_sha1_checksum,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/** Similar to svn_wc_transmit_text_deltas3(), but with a relative path
 * and adm_access baton, and the checksum output is an MD5 digest instead of
 * two svn_checksum_t objects.
 *
 * If @a tempfile is non-NULL, make a copy of @a path with keywords
 * and eol translated to repository-normal form, and set @a *tempfile to the
 * absolute path to this copy, allocated in @a result_pool.  The copy will
 * be in the temporary-text-base directory.  Do not clean up the copy;
 * caller can do that.  (The purpose of handing back the tmp copy is that it
 * is usually about to become the new text base anyway, but the installation
 * of the new text base is outside the scope of this function.)
 *
 * @since New in 1.4.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_transmit_text_deltas2(const char **tempfile,
                             unsigned char digest[],
                             const char *path,
                             svn_wc_adm_access_t *adm_access,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *pool);

/** Similar to svn_wc_transmit_text_deltas2(), but with @a digest set to NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_transmit_text_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_boolean_t fulltext,
                            const svn_delta_editor_t *editor,
                            void *file_baton,
                            const char **tempfile,
                            apr_pool_t *pool);


/** Given a @a local_abspath, transmit all local property
 * modifications using the appropriate @a editor method (in conjunction
 * with @a baton). Use @a scratch_pool for any temporary allocation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_transmit_prop_deltas2(svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             const svn_delta_editor_t *editor,
                             void *baton,
                             apr_pool_t *scratch_pool);


/** Similar to svn_wc_transmit_prop_deltas2(), but with a relative path,
 * adm_access baton and tempfile.
 *
 * If a temporary file remains after this function is finished, the
 * path to that file is returned in @a *tempfile (so the caller can
 * clean this up if it wishes to do so).
 *
 * @note Starting version 1.5, no tempfile will ever be returned
 *       anymore.  If @a *tempfile is passed, its value is set to @c NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_transmit_prop_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_t *entry,
                            const svn_delta_editor_t *editor,
                            void *baton,
                            const char **tempfile,
                            apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_ignore Ignoring unversioned files and directories
 * @{
 */

/** Get the run-time configured list of ignore patterns from the
 * #svn_config_t's in the @a config hash, and store them in @a *patterns.
 * Allocate @a *patterns and its contents in @a pool.
 */
svn_error_t *
svn_wc_get_default_ignores(apr_array_header_t **patterns,
                           apr_hash_t *config,
                           apr_pool_t *pool);

/** Get the list of ignore patterns from the #svn_config_t's in the
 * @a config hash and the local ignore patterns from the directory
 * at @a local_abspath, using @a wc_ctx, and store them in @a *patterns.
 * Allocate @a *patterns and its contents in @a result_pool, use @a
 * scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_get_ignores2(apr_array_header_t **patterns,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    apr_hash_t *config,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/** Similar to svn_wc_get_ignores2(), but with a #svn_wc_adm_access_t
 * parameter in place of #svn_wc_context_t and @c local_abspath parameters.
 *
 * @since New in 1.3.
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_get_ignores(apr_array_header_t **patterns,
                   apr_hash_t *config,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool);

/** Return TRUE iff @a str matches any of the elements of @a list, a
 * list of zero or more ignore patterns.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_wc_match_ignore_list(const char *str,
                         const apr_array_header_t *list,
                         apr_pool_t *pool);

/** @} */


/**
 * @defgroup svn_wc_repos_locks Repository locks
 * @{
 */

/** Add @a lock to the working copy for @a local_abspath.  If @a
 * local_abspath is read-only, due to locking properties, make it writable.
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_add_lock2(svn_wc_context_t *wc_ctx,
                 const char *abspath,
                 const svn_lock_t *lock,
                 apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_add_lock2(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_add_lock(const char *path,
                const svn_lock_t *lock,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool);

/** Remove any lock from @a local_abspath.  If @a local_abspath has a
 * lock and the locking so specifies, make the file read-only.  Don't
 * return an error if @a local_abspath didn't have a lock.  Perform temporary
 * allocations in @a scratch_pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_remove_lock2(svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool);

/**
 * Similar to svn_wc_remove_lock2(), but with a #svn_wc_adm_access_t /
 * relative path parameter pair.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_remove_lock(const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool);

/** @} */


/** A structure to report a summary of a working copy, including the
 * mix of revisions found within it, whether any parts are switched or
 * locally modified, and whether it is a sparse checkout.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type.
 *
 * @since New in 1.4
 */
typedef struct svn_wc_revision_status_t
{
  svn_revnum_t min_rev;   /**< Lowest revision found */
  svn_revnum_t max_rev;   /**< Highest revision found */

  svn_boolean_t switched; /**< Is anything switched? */
  svn_boolean_t modified; /**< Is anything modified? */

  /** Whether any WC paths are at a depth other than #svn_depth_infinity or
   * are user excluded.
   * @since New in 1.5.
   */
  svn_boolean_t sparse_checkout;
} svn_wc_revision_status_t;

/** Set @a *result_p to point to a new #svn_wc_revision_status_t structure
 * containing a summary of the revision range and status of the working copy
 * at @a local_abspath (not including "externals").  @a local_abspath must
 * be absolute. Return SVN_ERR_WC_PATH_NOT_FOUND if @a local_abspath is not
 * a working copy path.
 *
 * Set @a (*result_p)->min_rev and @a (*result_p)->max_rev respectively to the
 * lowest and highest revision numbers in the working copy.  If @a committed
 * is TRUE, summarize the last-changed revisions, else the base revisions.
 *
 * Set @a (*result_p)->switched to indicate whether any item in the WC is
 * switched relative to its parent.  If @a trail_url is non-NULL, use it to
 * determine if @a local_abspath itself is switched.  It should be any trailing
 * portion of @a local_abspath's expected URL, long enough to include any parts
 * that the caller considers might be changed by a switch.  If it does not
 * match the end of @a local_abspath's actual URL, then report a "switched"
 * status.
 *
 * Set @a (*result_p)->modified to indicate whether any item is locally
 * modified.
 *
 * If @a cancel_func is non-NULL, call it with @a cancel_baton to determine
 * if the client has canceled the operation.
 *
 * Allocate *result_p in @a result_pool, use @a scratch_pool for temporary
 * allocations.
 *
 * @a wc_ctx should be a valid working copy context.
 *
 * @since New in 1.7
 */
svn_error_t *
svn_wc_revision_status2(svn_wc_revision_status_t **result_p,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const char *trail_url,
                        svn_boolean_t committed,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);


/** Similar to svn_wc_revision_status2(), but with a (possibly) local
 * path and no wc_ctx parameter.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_revision_status(svn_wc_revision_status_t **result_p,
                       const char *wc_path,
                       const char *trail_url,
                       svn_boolean_t committed,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool);


/**
 * Set @a local_abspath's 'changelist' attribute to @a changelist iff
 * @a changelist is not @c NULL; otherwise, remove any current
 * changelist assignment from @a local_abspath.  @a changelist may not
 * be the empty string.  Recurse to @a depth.
 *
 * @a changelist_filter is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose changelist
 * assignments are adjusted; that is, don't tweak the changeset of any
 * item unless it's currently a member of one of those changelists.
 * If @a changelist_filter is empty (or altogether @c NULL), no changelist
 * filtering occurs.
 *
 * If @a cancel_func is not @c NULL, call it with @a cancel_baton to
 * determine if the client has canceled the operation.
 *
 * If @a notify_func is not @c NULL, call it with @a notify_baton to
 * report the change (using notification types
 * #svn_wc_notify_changelist_set and #svn_wc_notify_changelist_clear).
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @note For now, directories are NOT allowed to be associated with
 * changelists; there is confusion about whether they should behave
 * as depth-0 or depth-infinity objects.  If @a local_abspath is a directory,
 * return an error.
 *
 * @note This metadata is purely a client-side "bookkeeping"
 * convenience, and is entirely managed by the working copy.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_set_changelist2(svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       const char *changelist,
                       svn_depth_t depth,
                       const apr_array_header_t *changelist_filter,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       svn_wc_notify_func2_t notify_func,
                       void *notify_baton,
                       apr_pool_t *scratch_pool);

/** Similar to svn_wc_set_changelist2(), but with an access baton and
 * relative path.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_set_changelist(const char *path,
                      const char *changelist,
                      svn_wc_adm_access_t *adm_access,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      apr_pool_t *pool);



/**
 * The callback type used by svn_wc_get_changelists() and
 * svn_client_get_changelists().
 *
 * On each invocation, @a path is a newly discovered member of the
 * changelist, and @a baton is a private function closure.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_changelist_receiver_t) (void *baton,
                                                   const char *path,
                                                   const char *changelist,
                                                   apr_pool_t *pool);


/**
 * Beginning at @a local_abspath, crawl to @a depth to discover every path in
 * or under @a local_abspath which belongs to one of the changelists in @a
 * changelist_filter (an array of <tt>const char *</tt> changelist names).
 * If @a changelist_filter is @c NULL, discover paths with any changelist.
 * Call @a callback_func (with @a callback_baton) each time a
 * changelist-having path is discovered.
 *
 * @a local_abspath is a local WC path.
 *
 * If @a cancel_func is not @c NULL, invoke it passing @a cancel_baton
 * during the recursive walk.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_wc_get_changelists(svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       svn_depth_t depth,
                       const apr_array_header_t *changelist_filter,
                       svn_changelist_receiver_t callback_func,
                       void *callback_baton,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool);


/** Crop @a local_abspath according to @a depth.
 *
 * Remove any item that exceeds the boundary of @a depth (relative to
 * @a local_abspath) from revision control.  Leave modified items behind
 * (unversioned), while removing unmodified ones completely.
 *
 * @a depth can be svn_depth_empty, svn_depth_files or svn_depth_immediates.
 * Excluding nodes is handled by svn_wc_exclude().
 *
 * If @a local_abspath starts out with a shallower depth than @a depth,
 * do not upgrade it to @a depth (that would not be cropping); however, do
 * check children and crop them appropriately according to @a depth.
 *
 * Returns immediately with an #SVN_ERR_UNSUPPORTED_FEATURE error if @a
 * local_abspath is not a directory, or if @a depth is not restrictive
 * (e.g., #svn_depth_infinity).
 *
 * @a wc_ctx contains a tree lock, for the local path to the working copy
 * which will be used as the root of this operation.
 *
 * If @a cancel_func is not @c NULL, call it with @a cancel_baton at
 * various points to determine if the client has canceled the operation.
 *
 * If @a notify_func is not @c NULL, call it with @a notify_baton to
 * report changes as they are made.
 *
 * @since New in 1.7
 */
svn_error_t *
svn_wc_crop_tree2(svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  svn_depth_t depth,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  apr_pool_t *scratch_pool);

/** Similar to svn_wc_crop_tree2(), but uses an access baton and target.
 *
 * svn_wc_crop_tree() also allows #svn_depth_exclude, which is now
 * handled via svn_wc_exclude()
 *
 * @a target is a basename in @a anchor or "" for @a anchor itself.
 *
 * @since New in 1.6
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_crop_tree(svn_wc_adm_access_t *anchor,
                 const char *target,
                 svn_depth_t depth,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool);

/** Remove the local node for @a local_abspath from the working copy and
 * add an excluded node placeholder in its place.
 *
 * This feature is only supported for unmodified nodes. An
 * #SVN_ERR_UNSUPPORTED_FEATURE error is returned if the node can't be
 * excluded in its current state.
 *
 * @a wc_ctx contains a tree lock, for the local path to the working copy
 * which will be used as the root of this operation
 *
 * If @a notify_func is not @c NULL, call it with @a notify_baton to
 * report changes as they are made.
 *
 * If @a cancel_func is not @c NULL, call it with @a cancel_baton at
 * various points to determine if the client has canceled the operation.
 *
 *
 * @since New in 1.7
 */
svn_error_t *
svn_wc_exclude(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool);


/** @} */

/**
 * Set @a kind to the #svn_node_kind_t of @a abspath.  Use @a wc_ctx to access
 * the working copy, and @a scratch_pool for all temporary allocations.
 *
 * If @a abspath is not under version control, set @a kind to #svn_node_none.
 *
 * If @a show_hidden and @a show_deleted are both @c FALSE, the kind of
 * scheduled for delete, administrative only 'not present' and excluded
 * nodes is reported as #svn_node_none. This is recommended as a check
 * for 'is there a versioned file or directory here?'
 *
 * If @a show_deleted is FALSE, but @a show_hidden is @c TRUE then only
 * scheduled for delete and administrative only 'not present' nodes are
 * reported as #svn_node_none. This is recommended as check for
 * 'Can I add a node here?'
 *
 * If @a show_deleted is TRUE, but @a show_hidden is FALSE, then only
 * administrative only 'not present' nodes and excluded nodes are reported as
 * #svn_node_none. This behavior is the behavior bescribed as 'hidden'
 * before Subversion 1.7.
 *
 * If @a show_hidden and @a show_deleted are both @c TRUE all nodes are
 * reported.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_wc_read_kind2(svn_node_kind_t *kind,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  svn_boolean_t show_deleted,
                  svn_boolean_t show_hidden,
                  apr_pool_t *scratch_pool);

/** Similar to svn_wc_read_kind2() but with @a show_deleted always
 * passed as TRUE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_wc_read_kind(svn_node_kind_t *kind,
                 svn_wc_context_t *wc_ctx,
                 const char *abspath,
                 svn_boolean_t show_hidden,
                 apr_pool_t *scratch_pool);


/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_WC_H */
