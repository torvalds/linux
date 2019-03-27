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
 * @file svn_repos_private.h
 * @brief Subversion-internal repos APIs.
 */

#ifndef SVN_REPOS_PRIVATE_H
#define SVN_REPOS_PRIVATE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_repos.h"
#include "svn_editor.h"
#include "svn_config.h"

#include "private/svn_object_pool.h"
#include "private/svn_string_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Validate that property @a name with @a value is valid (as an addition
 * or edit or deletion) in a Subversion repository.  Return an error if not.
 *
 * If @a value is NULL, return #SVN_NO_ERROR to indicate that any property
 * may be deleted, even an invalid one.  Otherwise, if the @a name is not
 * of kind #svn_prop_regular_kind (see #svn_prop_kind_t), return
 * #SVN_ERR_REPOS_BAD_ARGS.  Otherwise, for some "svn:" properties, also
 * perform some validations on the @a value (e.g., for such properties,
 * typically the @a value must be in UTF-8 with LF linefeeds), and return
 * #SVN_ERR_BAD_PROPERTY_VALUE if it is not valid.
 *
 * Validations may be added in future releases, for example, for
 * newly-added #SVN_PROP_PREFIX properties.  However, user-defined
 * (non-#SVN_PROP_PREFIX) properties will never have their @a value
 * validated in any way.
 *
 * Use @a pool for temporary allocations.
 *
 * @note This function is used to implement server-side validation.
 * Consequently, if you make this function stricter in what it accepts, you
 * (a) break svnsync'ing of existing repositories that contain now-invalid
 * properties, (b) do not preclude such invalid values from entering the
 * repository via tools that use the svn_fs_* API directly (possibly
 * including svnadmin and svnlook).  This has happened before and there
 * are known (documented, but unsupported) upgrade paths in some cases.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_repos__validate_prop(const char *name,
                         const svn_string_t *value,
                         apr_pool_t *pool);

/* Attempt to normalize a Subversion property if it "needs translation"
 * (according to svn_prop_needs_translation(), currently all svn:* props).
 *
 * At this time, the only performed normalization is translation of
 * the line endings of the property value so that it would only contain
 * LF (\n) characters. "\r" characters found mid-line are replaced with "\n".
 * "\r\n" sequences are replaced with "\n".
 *
 * NAME is used to check that VALUE should be normalized, and if this
 * is the case, VALUE is then normalized, allocated from RESULT_POOL.
 * If no normalization is required, VALUE will be copied to RESULT_POOL
 * unchanged.  If NORMALIZED_P is not NULL, and the normalization
 * happened, set *NORMALIZED_P to non-zero.  If the property is returned
 * unchanged and NORMALIZED_P is not NULL, then *NORMALIZED_P will be
 * set to zero.  SCRATCH_POOL will be used for temporary allocations.
 */
svn_error_t *
svn_repos__normalize_prop(const svn_string_t **result_p,
                          svn_boolean_t *normalized_p,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/**
 * Given the error @a err from svn_repos_fs_commit_txn(), return an
 * string containing either or both of the svn_fs_commit_txn() error
 * and the SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED wrapped error from
 * the post-commit hook.  Any error tracing placeholders in the error
 * chain are skipped over.
 *
 * This function does not modify @a err.
 *
 * ### This method should not be necessary, but there are a few
 * ### places, e.g. mod_dav_svn, where only a single error message
 * ### string is returned to the caller and it is useful to have both
 * ### error messages included in the message.
 *
 * Use @a pool to do any allocations in.
 *
 * @since New in 1.7.
 */
const char *
svn_repos__post_commit_error_str(svn_error_t *err,
                                 apr_pool_t *pool);

/* A repos version of svn_fs_type */
svn_error_t *
svn_repos__fs_type(const char **fs_type,
                   const char *repos_path,
                   apr_pool_t *pool);


/* Create a commit editor for REPOS, based on REVISION.  */
svn_error_t *
svn_repos__get_commit_ev2(svn_editor_t **editor,
                          svn_repos_t *repos,
                          svn_authz_t *authz,
                          const char *authz_repos_name,
                          const char *authz_user,
                          apr_hash_t *revprops,
                          svn_commit_callback2_t commit_cb,
                          void *commit_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_repos__replay_ev2(svn_fs_root_t *root,
                      const char *base_dir,
                      svn_revnum_t low_water_mark,
                      svn_editor_t *editor,
                      svn_repos_authz_func_t authz_read_func,
                      void *authz_read_baton,
                      apr_pool_t *scratch_pool);

/**
 * Non-deprecated alias for svn_repos_get_logs4.
 *
 * Since the mapping of log5 to ra_get_log is would basically duplicate the
 * log5->log4 adapter, we provide this log4 wrapper that does not create a
 * deprecation warning.
 */
svn_error_t *
svn_repos__get_logs_compat(svn_repos_t *repos,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           int limit,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_boolean_t include_merged_revisions,
                           const apr_array_header_t *revprops,
                           svn_repos_authz_func_t authz_read_func,
                           void *authz_read_baton,
                           svn_log_entry_receiver_t receiver,
                           void *receiver_baton,
                           apr_pool_t *pool);

/**
 * @defgroup svn_config_pool Configuration object pool API
 * @{
 */

/* Opaque thread-safe factory and container for configuration objects.
 *
 * Instances handed out are read-only and may be given to multiple callers
 * from multiple threads.  Configuration objects no longer referenced by
 * any user may linger for a while before being cleaned up.
 */
typedef svn_object_pool__t svn_repos__config_pool_t;

/* Create a new configuration pool object with a lifetime determined by
 * POOL and return it in *CONFIG_POOL.
 *
 * The THREAD_SAFE flag indicates whether the pool actually needs to be
 * thread-safe and POOL must be also be thread-safe if this flag is set.
 */
svn_error_t *
svn_repos__config_pool_create(svn_repos__config_pool_t **config_pool,
                              svn_boolean_t thread_safe,
                              apr_pool_t *pool);

/* Set *CFG to a read-only reference to the current contents of the
 * configuration specified by PATH.  If the latter is a URL, we read the
 * data from a local repository.  CONFIG_POOL will store the configuration
 * and make further callers use the same instance if the content matches.
 * Section and option names will be case-insensitive.
 *
 * If MUST_EXIST is TRUE, a missing config file is also an error, *CFG
 * is otherwise simply NULL.
 *
 * PREFERRED_REPOS is only used if it is not NULL and PATH is a URL.
 * If it matches the URL, access the repository through this object
 * instead of creating a new repo instance.  Note that this might not
 * return the latest content.
 *
 * POOL determines the minimum lifetime of *CFG (may remain cached after
 * release) but must not exceed the lifetime of the pool provided to
 * #svn_repos__config_pool_create.
 */
svn_error_t *
svn_repos__config_pool_get(svn_config_t **cfg,
                           svn_repos__config_pool_t *config_pool,
                           const char *path,
                           svn_boolean_t must_exist,
                           svn_repos_t *preferred_repos,
                           apr_pool_t *pool);

/** @} */

/* Adjust mergeinfo paths and revisions in ways that are useful when loading
 * a dump stream.
 *
 * Set *NEW_VALUE_P to an adjusted version of the mergeinfo property value
 * supplied in OLD_VALUE, with the following adjustments.
 *
 *   - Normalize line endings: if all CRLF, change to LF; but error if
 *     mixed. If this normalization is performed, send a notification type
 *     svn_repos_notify_load_normalized_mergeinfo to NOTIFY_FUNC/NOTIFY_BATON.
 *
 *   - Prefix all the merge source paths with PARENT_DIR, if not null.
 *
 *   - Adjust any mergeinfo revisions not older than OLDEST_DUMPSTREAM_REV
 *     by using REV_MAP which maps (svn_revnum_t) old rev to (svn_revnum_t)
 *     new rev.
 *
 *   - Adjust any mergeinfo revisions older than OLDEST_DUMPSTREAM_REV by
 *     (-OLDER_REVS_OFFSET), dropping any revisions that become <= 0.
 *
 * Allocate *NEW_VALUE_P in RESULT_POOL.
 */
svn_error_t *
svn_repos__adjust_mergeinfo_property(svn_string_t **new_value_p,
                                     const svn_string_t *old_value,
                                     const char *parent_dir,
                                     apr_hash_t *rev_map,
                                     svn_revnum_t oldest_dumpstream_rev,
                                     apr_int32_t older_revs_offset,
                                     svn_repos_notify_func_t notify_func,
                                     void *notify_baton,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* A (nearly) opaque representation of an ordered list of header lines.
 */
typedef struct apr_array_header_t svn_repos__dumpfile_headers_t;

/* Create an empty set of headers.
 */
svn_repos__dumpfile_headers_t *
svn_repos__dumpfile_headers_create(apr_pool_t *pool);

/* Push the header (KEY, VAL) onto HEADERS.
 *
 * Duplicate the key and value into HEADERS's pool.
 */
void
svn_repos__dumpfile_header_push(svn_repos__dumpfile_headers_t *headers,
                                const char *key,
                                const char *val);

/* Push the header (KEY, val = VAL_FMT ...) onto HEADERS.
 *
 * Duplicate the key and value into HEADERS's pool.
 */
void
svn_repos__dumpfile_header_pushf(svn_repos__dumpfile_headers_t *headers,
                                 const char *key,
                                 const char *val_fmt,
                                 ...)
        __attribute__((format(printf, 3, 4)));

/* Write to STREAM the headers in HEADERS followed by a blank line.
 */
svn_error_t *
svn_repos__dump_headers(svn_stream_t *stream,
                        svn_repos__dumpfile_headers_t *headers,
                        apr_pool_t *scratch_pool);

/* Write a revision record to DUMP_STREAM for revision REVISION with revision
 * properies REVPROPS, creating appropriate headers.
 *
 * Include all of the headers in EXTRA_HEADERS (if non-null), ignoring
 * the revision number header and the three content length headers (which
 * will be recreated as needed). EXTRA_HEADERS maps (char *) key to
 * (char *) value.
 *
 * REVPROPS maps (char *) key to (svn_string_t *) value.
 *
 * Iff PROPS_SECTION_ALWAYS is true, include a prop content section (and
 * corresponding header) even when REVPROPS is empty. This option exists
 * to support a historical difference between svndumpfilter and svnadmin
 * dump.
 *
 * Finally write another blank line.
 */
svn_error_t *
svn_repos__dump_revision_record(svn_stream_t *dump_stream,
                                svn_revnum_t revision,
                                apr_hash_t *extra_headers,
                                apr_hash_t *revprops,
                                svn_boolean_t props_section_always,
                                apr_pool_t *scratch_pool);

/* Output node headers and props.
 *
 * Output HEADERS, content length headers, blank line, and
 * then PROPS_STR (if non-null) to DUMP_STREAM.
 *
 * HEADERS is an array of headers as struct {const char *key, *val;}.
 * Write them all in the given order.
 *
 * PROPS_STR is the property content block, including a terminating
 * 'PROPS_END\n' line. Iff PROPS_STR is non-null, write a
 * Prop-content-length header and the prop content block.
 *
 * Iff HAS_TEXT is true, write a Text-content length, using the value
 * TEXT_CONTENT_LENGTH.
 *
 * Write a Content-length header, its value being the sum of the
 * Prop- and Text- content length headers, if props and/or text are present
 * or if CONTENT_LENGTH_ALWAYS is true.
 */
svn_error_t *
svn_repos__dump_node_record(svn_stream_t *dump_stream,
                            svn_repos__dumpfile_headers_t *headers,
                            svn_stringbuf_t *props_str,
                            svn_boolean_t has_text,
                            svn_filesize_t text_content_length,
                            svn_boolean_t content_length_always,
                            apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_PRIVATE_H */
