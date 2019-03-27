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
 * @file svn_client.h
 * @brief Subversion's client library
 *
 * Requires:  The working copy library and repository access library.
 * Provides:  Broad wrappers around working copy library functionality.
 * Used By:   Client programs.
 */

#ifndef SVN_CLIENT_H
#define SVN_CLIENT_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_getopt.h>
#include <apr_file_io.h>
#include <apr_time.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_wc.h"
#include "svn_opt.h"
#include "svn_ra.h"
#include "svn_diff.h"
#include "svn_auth.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * Get libsvn_client version information.
 *
 * @since New in 1.1.
 */
const svn_version_t *
svn_client_version(void);

/** Client supporting functions
 *
 * @defgroup clnt_support Client supporting subsystem
 *
 * @{
 */


/*** Authentication stuff ***/

/**  The new authentication system allows the RA layer to "pull"
 *   information as needed from libsvn_client.
 *
 *   @deprecated Replaced by the svn_auth_* functions.
 *   @see auth_fns
 *
 *   @defgroup auth_fns_depr (deprecated) AuthZ client subsystem
 *
 *   @{
 */

/** Create and return @a *provider, an authentication provider of type
 * svn_auth_cred_simple_t that gets information by prompting the user
 * with @a prompt_func and @a prompt_baton.  Allocate @a *provider in
 * @a pool.
 *
 * If both #SVN_AUTH_PARAM_DEFAULT_USERNAME and
 * #SVN_AUTH_PARAM_DEFAULT_PASSWORD are defined as runtime
 * parameters in the @c auth_baton, then @a *provider will return the
 * default arguments when svn_auth_first_credentials() is called.  If
 * svn_auth_first_credentials() fails, then @a *provider will
 * re-prompt @a retry_limit times (via svn_auth_next_credentials()).
 * For infinite retries, set @a retry_limit to value less than 0.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_simple_prompt_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_simple_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_simple_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_username_t that gets information by prompting the
 * user with @a prompt_func and @a prompt_baton.  Allocate @a *provider
 * in @a pool.
 *
 * If #SVN_AUTH_PARAM_DEFAULT_USERNAME is defined as a runtime
 * parameter in the @c auth_baton, then @a *provider will return the
 * default argument when svn_auth_first_credentials() is called.  If
 * svn_auth_first_credentials() fails, then @a *provider will
 * re-prompt @a retry_limit times (via svn_auth_next_credentials()).
 * For infinite retries, set @a retry_limit to value less than 0.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_username_prompt_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_username_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_username_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * If a default username or password is available, @a *provider will
 * honor them as well, and return them when
 * svn_auth_first_credentials() is called.  (see
 * #SVN_AUTH_PARAM_DEFAULT_USERNAME and #SVN_AUTH_PARAM_DEFAULT_PASSWORD).
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_simple_provider2() instead.
 */
SVN_DEPRECATED
void
svn_client_get_simple_provider(svn_auth_provider_object_t **provider,
                               apr_pool_t *pool);


#if (defined(WIN32) && !defined(__MINGW32__)) || defined(DOXYGEN) || defined(CTYPESGEN) || defined(SWIG)
/**
 * Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_client_get_simple_provider(), except that, when
 * running on Window 2000 or newer (or any other Windows version that
 * includes the CryptoAPI), the provider encrypts the password before
 * storing it to disk. On earlier versions of Windows, the provider
 * does nothing.
 *
 * @since New in 1.2.
 * @note This function is only available on Windows.
 *
 * @note An administrative password reset may invalidate the account's
 * secret key. This function will detect that situation and behave as
 * if the password were not cached at all.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_windows_simple_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_windows_simple_provider(svn_auth_provider_object_t **provider,
                                       apr_pool_t *pool);
#endif /* WIN32 && !__MINGW32__ || DOXYGEN || CTYPESGEN || SWIG */

/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_username_t that gets/sets information from a user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * If a default username is available, @a *provider will honor it,
 * and return it when svn_auth_first_credentials() is called.  (see
 * #SVN_AUTH_PARAM_DEFAULT_USERNAME).
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_username_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_username_provider(svn_auth_provider_object_t **provider,
                                 apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_server_trust_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials from the configuration
 * mechanism.  The returned credential is used to override SSL
 * security on an error.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_server_trust_file_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_server_trust_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_client_cert_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials from the configuration
 * mechanism.  The returned credential is used to load the appropriate
 * client certificate for authentication when requested by a server.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_client_cert_file_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_client_cert_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_client_cert_pw_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials from the configuration
 * mechanism.  The returned credential is used when a loaded client
 * certificate is protected by a passphrase.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_client_cert_pw_file_provider2() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_client_cert_pw_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_server_trust_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used to override
 * SSL security on an error.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_server_trust_prompt_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_server_trust_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_server_trust_prompt_func_t prompt_func,
  void *prompt_baton,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_client_cert_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used to load the
 * appropriate client certificate for authentication when requested by
 * a server.  The prompt will be retried @a retry_limit times.
 * For infinite retries, set @a retry_limit to value less than 0.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_client_cert_prompt_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_client_cert_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_client_cert_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


/** Create and return @a *provider, an authentication provider of type
 * #svn_auth_cred_ssl_client_cert_pw_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used when a loaded
 * client certificate is protected by a passphrase.  The prompt will
 * be retried @a retry_limit times. For infinite retries, set @a retry_limit
 * to value less than 0.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_auth_get_ssl_client_cert_pw_prompt_provider() instead.
 */
SVN_DEPRECATED
void
svn_client_get_ssl_client_cert_pw_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);

/** @} */

/**
 * Revisions and Peg Revisions
 *
 * @defgroup clnt_revisions Revisions and Peg Revisions
 *
 * A brief word on operative and peg revisions.
 *
 * If the kind of the peg revision is #svn_opt_revision_unspecified, then it
 * defaults to #svn_opt_revision_head for URLs and #svn_opt_revision_working
 * for local paths.
 *
 * For deeper insight, please see the
 * <a href="http://svnbook.red-bean.com/nightly/en/svn.advanced.pegrevs.html">
 * Peg and Operative Revisions</a> section of the Subversion Book.
 */

/**
 * Commit operations
 *
 * @defgroup clnt_commit Client commit subsystem
 *
 * @{
 */

/** This is a structure which stores a filename and a hash of property
 * names and values.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
typedef struct svn_client_proplist_item_t
{
  /** The name of the node on which these properties are set. */
  svn_stringbuf_t *node_name;

  /** A hash of (const char *) property names, and (svn_string_t *) property
   * values. */
  apr_hash_t *prop_hash;

} svn_client_proplist_item_t;

/**
 * The callback invoked by svn_client_proplist4().  Each invocation
 * provides the regular and/or inherited properties of @a path, which is
 * either a working copy path or a URL.  If @a prop_hash is not @c NULL, then
 * it maps explicit <tt>const char *</tt> property names to
 * <tt>svn_string_t *</tt> explicit property values.  If @a inherited_props
 * is not @c NULL, then it is a depth-first ordered array of
 * #svn_prop_inherited_item_t * structures representing the
 * properties inherited by @a path.  Use @a scratch_pool for all temporary
 * allocations.
 *
 * The #svn_prop_inherited_item_t->path_or_url members of the
 * #svn_prop_inherited_item_t * structures in @a inherited_props are
 * URLs if @a path is a URL or if @a path is a working copy path but the
 * property represented by the structure is above the working copy root (i.e.
 * the inherited property is from the cache).  In all other cases the
 * #svn_prop_inherited_item_t->path_or_url members are absolute working copy
 * paths.
 *
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_proplist_receiver2_t)(
  void *baton,
  const char *path,
  apr_hash_t *prop_hash,
  apr_array_header_t *inherited_props,
  apr_pool_t *scratch_pool);

/**
 * Similar to #svn_proplist_receiver2_t, but doesn't return inherited
 * properties.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_proplist_receiver_t)(
  void *baton,
  const char *path,
  apr_hash_t *prop_hash,
  apr_pool_t *pool);

/**
 * Return a duplicate of @a item, allocated in @a pool. No part of the new
 * structure will be shared with @a item.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_client_proplist_item_t *
svn_client_proplist_item_dup(const svn_client_proplist_item_t *item,
                             apr_pool_t *pool);

/** Information about commits passed back to client from this module.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef struct svn_client_commit_info_t
{
  /** just-committed revision. */
  svn_revnum_t revision;

  /** server-side date of the commit. */
  const char *date;

  /** author of the commit. */
  const char *author;

} svn_client_commit_info_t;


/**
 * @name Commit state flags
 * @brief State flags for use with the #svn_client_commit_item3_t structure
 * (see the note about the namespace for that structure, which also
 * applies to these flags).
 * @{
 */
#define SVN_CLIENT_COMMIT_ITEM_ADD         0x01
#define SVN_CLIENT_COMMIT_ITEM_DELETE      0x02
#define SVN_CLIENT_COMMIT_ITEM_TEXT_MODS   0x04
#define SVN_CLIENT_COMMIT_ITEM_PROP_MODS   0x08
#define SVN_CLIENT_COMMIT_ITEM_IS_COPY     0x10
/** One of the flags for a commit item.  The node has a lock token that
 * should be released after a successful commit and, if the node is also
 * modified, transferred to the server as part of the commit process.
 *
 * @since New in 1.2. */
#define SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN  0x20
/** One of the flags for a commit item.  The node is the 'moved here'
 * side of a local move.  This is used to check and enforce that the
 * other side of the move is also included in the commit.
 *
 * @since New in 1.8. */
#define SVN_CLIENT_COMMIT_ITEM_MOVED_HERE  0x40
/** @} */

/** The commit candidate structure.
 *
 * In order to avoid backwards compatibility problems clients should use
 * svn_client_commit_item3_create() to allocate and initialize this
 * structure instead of doing so themselves.
 *
 * @since New in 1.5.
 */
typedef struct svn_client_commit_item3_t
{
  /* IMPORTANT: If you extend this structure, add new fields to the end. */

  /** absolute working-copy path of item. Always set during normal commits
   * (and copies from a working copy) to the repository. Can only be NULL
   * when stub commit items are created for operations that only involve
   * direct repository operations. During WC->REPOS copy operations, this
   * path is the WC source path of the operation. */
  const char *path;

  /** node kind (dir, file) */
  svn_node_kind_t kind;

  /** commit URL for this item. Points to the repository location of PATH
   * during commits, or to the final URL of the item when copying from the
   * working copy to the repository. */
  const char *url;

  /** revision of textbase */
  svn_revnum_t revision;

  /** copyfrom-url or NULL if not a copied item */
  const char *copyfrom_url;

  /** copyfrom-rev, valid when copyfrom_url != NULL */
  svn_revnum_t copyfrom_rev;

  /** state flags */
  apr_byte_t state_flags;

  /** An array of #svn_prop_t *'s, which are incoming changes from
   * the repository to WC properties.  These changes are applied
   * post-commit.
   *
   * When adding to this array, allocate the #svn_prop_t and its
   * contents in @c incoming_prop_changes->pool, so that it has the
   * same lifetime as this data structure.
   *
   * See http://subversion.tigris.org/issues/show_bug.cgi?id=806 for a
   * description of what would happen if the post-commit process
   * didn't group these changes together with all other changes to the
   * item.
   */
  apr_array_header_t *incoming_prop_changes;

  /** An array of #svn_prop_t *'s, which are outgoing changes to
   * make to properties in the repository.  These extra property
   * changes are declared pre-commit, and applied to the repository as
   * part of a commit.
   *
   * When adding to this array, allocate the #svn_prop_t and its
   * contents in @c outgoing_prop_changes->pool, so that it has the
   * same lifetime as this data structure.
   */
  apr_array_header_t *outgoing_prop_changes;

  /**
   * When processing the commit this contains the relative path for
   * the commit session. #NULL until the commit item is preprocessed.
   * @since New in 1.7.
   */
  const char *session_relpath;

  /**
   * When committing a move, this contains the absolute path where
   * the node was directly moved from. (If an ancestor at the original
   * location was moved then it points to where the node itself was
   * moved from; not the original location.)
   * @since New in 1.8.
   */
  const char *moved_from_abspath;

} svn_client_commit_item3_t;

/** The commit candidate structure.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
typedef struct svn_client_commit_item2_t
{
  /** absolute working-copy path of item */
  const char *path;

  /** node kind (dir, file) */
  svn_node_kind_t kind;

  /** commit URL for this item */
  const char *url;

  /** revision of textbase */
  svn_revnum_t revision;

  /** copyfrom-url or NULL if not a copied item */
  const char *copyfrom_url;

  /** copyfrom-rev, valid when copyfrom_url != NULL */
  svn_revnum_t copyfrom_rev;

  /** state flags */
  apr_byte_t state_flags;

  /** Analogous to the #svn_client_commit_item3_t.incoming_prop_changes
   * field.
   */
  apr_array_header_t *wcprop_changes;
} svn_client_commit_item2_t;

/** The commit candidate structure.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef struct svn_client_commit_item_t
{
  /** absolute working-copy path of item */
  const char *path;

  /** node kind (dir, file) */
  svn_node_kind_t kind;

  /** commit URL for this item */
  const char *url;

  /** revision (copyfrom-rev if _IS_COPY) */
  svn_revnum_t revision;

  /** copyfrom-url */
  const char *copyfrom_url;

  /** state flags */
  apr_byte_t state_flags;

  /** Analogous to the #svn_client_commit_item3_t.incoming_prop_changes
   * field.
   */
  apr_array_header_t *wcprop_changes;

} svn_client_commit_item_t;

/** Return a new commit item object, allocated in @a pool.
 *
 * In order to avoid backwards compatibility problems, this function
 * is used to initialize and allocate the #svn_client_commit_item3_t
 * structure rather than doing so explicitly, as the size of this
 * structure may change in the future.
 *
 * @since New in 1.6.
 */
svn_client_commit_item3_t *
svn_client_commit_item3_create(apr_pool_t *pool);

/** Like svn_client_commit_item3_create() but with a stupid "const"
 * qualifier on the returned structure, and it returns an error that
 * will never happen.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit_item_create(const svn_client_commit_item3_t **item,
                              apr_pool_t *pool);

/**
 * Return a duplicate of @a item, allocated in @a pool. No part of the
 * new structure will be shared with @a item, except for the adm_access
 * member.
 *
 * @since New in 1.5.
 */
svn_client_commit_item3_t *
svn_client_commit_item3_dup(const svn_client_commit_item3_t *item,
                            apr_pool_t *pool);

/**
 * Return a duplicate of @a item, allocated in @a pool. No part of the new
 * structure will be shared with @a item.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_client_commit_item2_t *
svn_client_commit_item2_dup(const svn_client_commit_item2_t *item,
                            apr_pool_t *pool);

/** Callback type used by commit-y operations to get a commit log message
 * from the caller.
 *
 * Set @a *log_msg to the log message for the commit, allocated in @a
 * pool, or @c NULL if wish to abort the commit process.  Set @a *tmp_file
 * to the path of any temporary file which might be holding that log
 * message, or @c NULL if no such file exists (though, if @a *log_msg is
 * @c NULL, this value is undefined).  The log message MUST be a UTF8
 * string with LF line separators.
 *
 * @a commit_items is a read-only array of #svn_client_commit_item3_t
 * structures, which may be fully or only partially filled-in,
 * depending on the type of commit operation.
 *
 * @a baton is provided along with the callback for use by the handler.
 *
 * All allocations should be performed in @a pool.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_client_get_commit_log3_t)(
  const char **log_msg,
  const char **tmp_file,
  const apr_array_header_t *commit_items,
  void *baton,
  apr_pool_t *pool);

/** Callback type used by commit-y operations to get a commit log message
 * from the caller.
 *
 * Set @a *log_msg to the log message for the commit, allocated in @a
 * pool, or @c NULL if wish to abort the commit process.  Set @a *tmp_file
 * to the path of any temporary file which might be holding that log
 * message, or @c NULL if no such file exists (though, if @a *log_msg is
 * @c NULL, this value is undefined).  The log message MUST be a UTF8
 * string with LF line separators.
 *
 * @a commit_items is a read-only array of #svn_client_commit_item2_t
 * structures, which may be fully or only partially filled-in,
 * depending on the type of commit operation.
 *
 * @a baton is provided along with the callback for use by the handler.
 *
 * All allocations should be performed in @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
typedef svn_error_t *(*svn_client_get_commit_log2_t)(
  const char **log_msg,
  const char **tmp_file,
  const apr_array_header_t *commit_items,
  void *baton,
  apr_pool_t *pool);

/** Callback type used by commit-y operations to get a commit log message
 * from the caller.
 *
 * Set @a *log_msg to the log message for the commit, allocated in @a
 * pool, or @c NULL if wish to abort the commit process.  Set @a *tmp_file
 * to the path of any temporary file which might be holding that log
 * message, or @c NULL if no such file exists (though, if @a *log_msg is
 * @c NULL, this value is undefined).  The log message MUST be a UTF8
 * string with LF line separators.
 *
 * @a commit_items is a read-only array of #svn_client_commit_item_t
 * structures, which may be fully or only partially filled-in,
 * depending on the type of commit operation.
 *
 * @a baton is provided along with the callback for use by the handler.
 *
 * All allocations should be performed in @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef svn_error_t *(*svn_client_get_commit_log_t)(
  const char **log_msg,
  const char **tmp_file,
  apr_array_header_t *commit_items,
  void *baton,
  apr_pool_t *pool);

/** @} */

/**
 * Client blame
 *
 * @defgroup clnt_blame Client blame functionality
 *
 * @{
 */

/** Callback type used by svn_client_blame5() to notify the caller
 * that line @a line_no of the blamed file was last changed in @a revision
 * which has the revision properties @a rev_props, and that the contents were
 * @a line.
 *
 * @a start_revnum and @a end_revnum contain the start and end revision
 * number of the entire blame operation, as determined from the repository
 * inside svn_client_blame5(). This can be useful for the blame receiver
 * to format the blame output.
 *
 * If svn_client_blame5() was called with @a include_merged_revisions set to
 * TRUE, @a merged_revision, @a merged_rev_props and @a merged_path will be
 * set, otherwise they will be NULL. @a merged_path will be set to the
 * absolute repository path.
 *
 * All allocations should be performed in @a pool.
 *
 * @note If there is no blame information for this line, @a revision will be
 * invalid and @a rev_props will be NULL. In this case @a local_change
 * will be true if the reason there is no blame information is that the line
 * was modified locally. In all other cases @a local_change will be false.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_client_blame_receiver3_t)(
  void *baton,
  svn_revnum_t start_revnum,
  svn_revnum_t end_revnum,
  apr_int64_t line_no,
  svn_revnum_t revision,
  apr_hash_t *rev_props,
  svn_revnum_t merged_revision,
  apr_hash_t *merged_rev_props,
  const char *merged_path,
  const char *line,
  svn_boolean_t local_change,
  apr_pool_t *pool);

/**
 * Similar to #svn_client_blame_receiver3_t, but with separate author and
 * date revision properties instead of all revision properties, and without
 * information about local changes.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_client_blame_receiver2_t)(
  void *baton,
  apr_int64_t line_no,
  svn_revnum_t revision,
  const char *author,
  const char *date,
  svn_revnum_t merged_revision,
  const char *merged_author,
  const char *merged_date,
  const char *merged_path,
  const char *line,
  apr_pool_t *pool);

/**
 * Similar to #svn_client_blame_receiver2_t, but without @a merged_revision,
 * @a merged_author, @a merged_date, or @a merged_path members.
 *
 * @note New in 1.4 is that the line is defined to contain only the line
 * content (and no [partial] EOLs; which was undefined in older versions).
 * Using this callback with svn_client_blame() or svn_client_blame2()
 * will still give you the old behaviour.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
typedef svn_error_t *(*svn_client_blame_receiver_t)(
  void *baton,
  apr_int64_t line_no,
  svn_revnum_t revision,
  const char *author,
  const char *date,
  const char *line,
  apr_pool_t *pool);


/** @} */

/**
 * Client diff
 *
 * @defgroup clnt_diff Client diff functionality
 *
 * @{
 */
/** The difference type in an svn_diff_summarize_t structure.
 *
 * @since New in 1.4.
 */
typedef enum svn_client_diff_summarize_kind_t
{
  /** An item with no text modifications */
  svn_client_diff_summarize_kind_normal,

  /** An added item */
  svn_client_diff_summarize_kind_added,

  /** An item with text modifications */
  svn_client_diff_summarize_kind_modified,

  /** A deleted item */
  svn_client_diff_summarize_kind_deleted
} svn_client_diff_summarize_kind_t;


/** A struct that describes the diff of an item. Passed to
 * #svn_client_diff_summarize_func_t.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, users shouldn't allocate structures of this
 * type, to preserve binary compatibility.
 *
 * @since New in 1.4.
 */
typedef struct svn_client_diff_summarize_t
{
  /** Path relative to the target.  If the target is a file, path is
   * the empty string. */
  const char *path;

  /** Change kind */
  svn_client_diff_summarize_kind_t summarize_kind;

  /** Properties changed?  For consistency with 'svn status' output,
   * this should be false if summarize_kind is _added or _deleted. */
  svn_boolean_t prop_changed;

  /** File or dir */
  svn_node_kind_t node_kind;
} svn_client_diff_summarize_t;

/**
 * Return a duplicate of @a diff, allocated in @a pool. No part of the new
 * structure will be shared with @a diff.
 *
 * @since New in 1.4.
 */
svn_client_diff_summarize_t *
svn_client_diff_summarize_dup(const svn_client_diff_summarize_t *diff,
                              apr_pool_t *pool);


/** A callback used in svn_client_diff_summarize2() and
 * svn_client_diff_summarize_peg2() for reporting a @a diff summary.
 *
 * All allocations should be performed in @a pool.
 *
 * @a baton is a closure object; it should be provided by the implementation,
 * and passed by the caller.
 *
 * @since New in 1.4.
 */
typedef svn_error_t *(*svn_client_diff_summarize_func_t)(
  const svn_client_diff_summarize_t *diff,
  void *baton,
  apr_pool_t *pool);



/** @} */


/**
 * Client context
 *
 * @defgroup clnt_ctx Client context management
 *
 * @{
 */

/** A client context structure, which holds client specific callbacks,
 * batons, serves as a cache for configuration options, and other various
 * and sundry things.  In order to avoid backwards compatibility problems
 * clients should use svn_client_create_context() to allocate and
 * initialize this structure instead of doing so themselves.
 */
typedef struct svn_client_ctx_t
{
  /** main authentication baton. */
  svn_auth_baton_t *auth_baton;

  /** notification callback function.
   * This will be called by notify_func2() by default.
   * @deprecated Provided for backward compatibility with the 1.1 API.
   * Use @c notify_func2 instead. */
  svn_wc_notify_func_t notify_func;

  /** notification callback baton for notify_func()
   * @deprecated Provided for backward compatibility with the 1.1 API.
   * Use @c notify_baton2 instead */
  void *notify_baton;

  /** Log message callback function.  NULL means that Subversion
    * should try not attempt to fetch a log message.
    * @deprecated Provided for backward compatibility with the 1.2 API.
    * Use @c log_msg_func2 instead. */
  svn_client_get_commit_log_t log_msg_func;

  /** log message callback baton
    * @deprecated Provided for backward compatibility with the 1.2 API.
    * Use @c log_msg_baton2 instead. */
  void *log_msg_baton;

  /** a hash mapping of <tt>const char *</tt> configuration file names to
   * #svn_config_t *'s. For example, the '~/.subversion/config' file's
   * contents should have the key "config".  May be left unset (or set to
   * NULL) to use the built-in default settings and not use any configuration.
   */
  apr_hash_t *config;

  /** a callback to be used to see if the client wishes to cancel the running
   * operation. */
  svn_cancel_func_t cancel_func;

  /** a baton to pass to the cancellation callback. */
  void *cancel_baton;

  /** notification function, defaulting to a function that forwards
   * to notify_func().  If @c NULL, it will not be invoked.
   * @since New in 1.2. */
  svn_wc_notify_func2_t notify_func2;

  /** notification baton for notify_func2().
   * @since New in 1.2. */
  void *notify_baton2;

  /** Log message callback function. NULL means that Subversion
   *   should try log_msg_func.
   * @since New in 1.3. */
  svn_client_get_commit_log2_t log_msg_func2;

  /** callback baton for log_msg_func2
   * @since New in 1.3. */
  void *log_msg_baton2;

  /** Notification callback for network progress information.
   * May be NULL if not used.
   * @since New in 1.3. */
  svn_ra_progress_notify_func_t progress_func;

  /** Callback baton for progress_func.
   * @since New in 1.3. */
  void *progress_baton;

  /** Log message callback function. NULL means that Subversion
   *   should try @c log_msg_func2, then @c log_msg_func.
   * @since New in 1.5. */
  svn_client_get_commit_log3_t log_msg_func3;

  /** The callback baton for @c log_msg_func3.
   * @since New in 1.5. */
  void *log_msg_baton3;

  /** MIME types map.
   * @since New in 1.5. */
  apr_hash_t *mimetypes_map;

  /** Conflict resolution callback and baton, if available.
   * @since New in 1.5. */
  svn_wc_conflict_resolver_func_t conflict_func;
  void *conflict_baton;

  /** Custom client name string, or @c NULL.
   * @since New in 1.5. */
  const char *client_name;

  /** Conflict resolution callback and baton, if available. NULL means that
   * subversion should try @c conflict_func.
   * @since New in 1.7. */
  svn_wc_conflict_resolver_func2_t conflict_func2;
  void *conflict_baton2;

  /** A working copy context for the client operation to use.
   * This is initialized by svn_client_create_context() and should never
   * be @c NULL.
   *
   * @since New in 1.7.  */
  svn_wc_context_t *wc_ctx;

  /** Check-tunnel callback
   *
   * If not @c NULL, and open_tunnel_func is also not @c NULL, this
   * callback will be invoked to check if open_tunnel_func should be
   * used to create a specific tunnel, or if the default tunnel
   * implementation (either built-in or configured in the client
   * configuration file) should be used instead.
   * @since New in 1.9.
   */
  svn_ra_check_tunnel_func_t check_tunnel_func;

  /** Open-tunnel callback
   *
   * If not @c NULL, this callback will be invoked to create a tunnel
   * for a ra_svn connection that needs one, overriding any tunnel
   * definitions in the client config file. This callback is used only
   * for ra_svn and ignored by the other RA modules.
   * @since New in 1.9.
   */
  svn_ra_open_tunnel_func_t open_tunnel_func;

  /** The baton used with check_tunnel_func and open_tunnel_func.
   * @since New in 1.9.
   */
  void *tunnel_baton;
} svn_client_ctx_t;

/** Initialize a client context.
 * Set @a *ctx to a client context object, allocated in @a pool, that
 * represents a particular instance of an svn client. @a cfg_hash is used
 * to initialise the config member of the returned context object and should
 * remain valid for the lifetime of the object. @a cfg_hash may be @c NULL,
 * in which case it is ignored.
 *
 * In order to avoid backwards compatibility problems, clients must
 * use this function to initialize and allocate the
 * #svn_client_ctx_t structure rather than doing so themselves, as
 * the size of this structure may change in the future.
 *
 * The current implementation never returns error, but callers should
 * still check for error, for compatibility with future versions.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_create_context2(svn_client_ctx_t **ctx,
                           apr_hash_t *cfg_hash,
                           apr_pool_t *pool);


/** Similar to svn_client_create_context2 but passes a NULL @a cfg_hash.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_create_context(svn_client_ctx_t **ctx,
                          apr_pool_t *pool);

/** @} end group: Client context management */

/**
 * @deprecated Provided for backward compatibility. This constant was never
 * used in released versions.
 */
#define SVN_CLIENT_AUTH_USERNAME            "username"
/**
 * @deprecated Provided for backward compatibility. This constant was never
 * used in released versions.
 */
#define SVN_CLIENT_AUTH_PASSWORD            "password"

/** Client argument processing
 *
 * @defgroup clnt_cmdline Client command-line processing
 *
 * @{
 */

/**
 * Pull remaining target arguments from @a os into @a *targets_p,
 * converting them to UTF-8, followed by targets from @a known_targets
 * (which might come from, for example, the "--targets" command line option).
 *
 * Process each target in one of the following ways.  For a repository-
 * relative URL: resolve to a full URL, contacting the repository if
 * necessary to do so, and then treat as a full URL.  For a URL: do some
 * IRI-to-URI encoding and some auto-escaping, and canonicalize.  For a
 * local path: canonicalize case and path separators.
 *
 * If @a keep_last_origpath_on_truepath_collision is TRUE, and there are
 * exactly two targets which both case-canonicalize to the same path, the last
 * target will be returned in the original non-case-canonicalized form.
 *
 * Allocate @a *targets_p and its elements in @a pool.
 *
 * @a ctx is required for possible repository authentication.
 *
 * If a path has the same name as a Subversion working copy
 * administrative directory, return #SVN_ERR_RESERVED_FILENAME_SPECIFIED;
 * if multiple reserved paths are encountered, return a chain of
 * errors, all of which are #SVN_ERR_RESERVED_FILENAME_SPECIFIED.  Do
 * not return this type of error in a chain with any other type of
 * error, and if this is the only type of error encountered, complete
 * the operation before returning the error(s).
 *
 * Return an error if a target is just a peg specifier with no path, such as
 * "@abc". Before v1.6.5 (r878062) this form was interpreted as a literal path;
 * it is now ambiguous. The form "@abc@" should now be used to refer to the
 * literal path "@abc" with no peg revision, or the form ".@abc" to refer to
 * the empty path with peg revision "abc".
 *
 * @since New in 1.7
 */
svn_error_t *
svn_client_args_to_target_array2(apr_array_header_t **targets_p,
                                 apr_getopt_t *os,
                                 const apr_array_header_t *known_targets,
                                 svn_client_ctx_t *ctx,
                                 svn_boolean_t keep_last_origpath_on_truepath_collision,
                                 apr_pool_t *pool);

/**
 * Similar to svn_client_args_to_target_array2() but with
 * @a keep_last_origpath_on_truepath_collision always set to FALSE.
 *
 * @since Since 1.6.5, this returns an error if a path contains a peg
 * specifier with no path before it, such as "@abc".
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_args_to_target_array(apr_array_header_t **targets_p,
                                apr_getopt_t *os,
                                const apr_array_header_t *known_targets,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool);

/** @} group end: Client command-line processing */

/** @} */

/**
 * Client working copy management functions
 *
 * @defgroup clnt_wc Client working copy management
 *
 * @{
 */

/**
 * @defgroup clnt_wc_checkout Checkout
 *
 * @{
 */


/**
 * Checkout a working copy from a repository.
 *
 * @param[out] result_rev   If non-NULL, the value of the revision checked
 *              out from the repository.
 * @param[in] URL       The repository URL of the checkout source.
 * @param[in] path      The root of the new working copy.
 * @param[in] peg_revision  The peg revision.
 * @param[in] revision  The operative revision.
 * @param[in] depth     The depth of the operation.  If #svn_depth_unknown,
 *              then behave as if for #svn_depth_infinity, except in the case
 *              of resuming a previous checkout of @a path (i.e., updating),
 *              in which case use the depth of the existing working copy.
 * @param[in] ignore_externals  If @c TRUE, don't process externals
 *              definitions as part of this operation.
 * @param[in] allow_unver_obstructions  If @c TRUE, then tolerate existing
 *              unversioned items that obstruct incoming paths.  Only
 *              obstructions of the same type (file or dir) as the added
 *              item are tolerated.  The text of obstructing files is left
 *              as-is, effectively treating it as a user modification after
 *              the checkout.  Working properties of obstructing items are
 *              set equal to the base properties. <br>
 *              If @c FALSE, then abort if there are any unversioned
 *              obstructing items.
 * @param[in] ctx   The standard client context, used for authentication and
 *              notification.
 * @param[in] pool  Used for any temporary allocation.
 *
 * @return A pointer to an #svn_error_t of the type (this list is not
 *         exhaustive): <br>
 *         #SVN_ERR_UNSUPPORTED_FEATURE if @a URL refers to a file rather
 *         than a directory; <br>
 *         #SVN_ERR_RA_ILLEGAL_URL if @a URL does not exist; <br>
 *         #SVN_ERR_CLIENT_BAD_REVISION if @a revision is not one of
 *         #svn_opt_revision_number, #svn_opt_revision_head, or
 *         #svn_opt_revision_date. <br>
 *         If no error occurred, return #SVN_NO_ERROR.
 *
 * @since New in 1.5.
 *
 * @see #svn_depth_t <br> #svn_client_ctx_t <br> @ref clnt_revisions for
 *      a discussion of operative and peg revisions.
 */
svn_error_t *
svn_client_checkout3(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     svn_boolean_t ignore_externals,
                     svn_boolean_t allow_unver_obstructions,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);


/**
 * Similar to svn_client_checkout3() but with @a allow_unver_obstructions
 * always set to FALSE, and @a depth set according to @a recurse: if
 * @a recurse is TRUE, @a depth is #svn_depth_infinity, if @a recurse
 * is FALSE, @a depth is #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_checkout2(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);


/**
 * Similar to svn_client_checkout2(), but with @a peg_revision
 * always set to #svn_opt_revision_unspecified and
 * @a ignore_externals always set to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_checkout(svn_revnum_t *result_rev,
                    const char *URL,
                    const char *path,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);
/** @} */

/**
 * @defgroup Update Bring a working copy up-to-date with a repository
 *
 * @{
 *
 */

/**
 * Update working trees @a paths to @a revision, authenticating with the
 * authentication baton cached in @a ctx.  @a paths is an array of const
 * char * paths to be updated.  Unversioned paths that are direct children
 * of a versioned path will cause an update that attempts to add that path;
 * other unversioned paths are skipped.  If @a result_revs is not NULL,
 * @a *result_revs will be set to an array of svn_revnum_t with each
 * element set to the revision to which @a revision was resolved for the
 * corresponding element of @a paths.
 *
 * @a revision must be of kind #svn_opt_revision_number,
 * #svn_opt_revision_head, or #svn_opt_revision_date.  If @a
 * revision does not meet these requirements, return the error
 * #SVN_ERR_CLIENT_BAD_REVISION.
 *
 * The paths in @a paths can be from multiple working copies from multiple
 * repositories, but even if they all come from the same repository there
 * is no guarantee that revision represented by #svn_opt_revision_head
 * will remain the same as each path is updated.
 *
 * If @a ignore_externals is set, don't process externals definitions
 * as part of this operation.
 *
 * If @a depth is #svn_depth_infinity, update fully recursively.
 * Else if it is #svn_depth_immediates or #svn_depth_files, update
 * each target and its file entries, but not its subdirectories.  Else
 * if #svn_depth_empty, update exactly each target, nonrecursively
 * (essentially, update the target's properties).
 *
 * If @a depth is #svn_depth_unknown, take the working depth from
 * @a paths and then behave as described above.
 *
 * If @a depth_is_sticky is set and @a depth is not
 * #svn_depth_unknown, then in addition to updating PATHS, also set
 * their sticky ambient depth value to @a depth.
 *
 * If @a allow_unver_obstructions is TRUE then the update tolerates
 * existing unversioned items that obstruct added paths.  Only
 * obstructions of the same type (file or dir) as the added item are
 * tolerated.  The text of obstructing files is left as-is, effectively
 * treating it as a user modification after the update.  Working
 * properties of obstructing items are set equal to the base properties.
 * If @a allow_unver_obstructions is FALSE then the update will abort
 * if there are any unversioned obstructing items.
 *
 * If @a adds_as_modification is TRUE, a local addition at the same path
 * as an incoming addition of the same node kind results in a normal node
 * with a possible local modification, instead of a tree conflict.
 *
 * If @a make_parents is TRUE, create any non-existent parent
 * directories also by checking them out at depth=empty.
 *
 * If @a ctx->notify_func2 is non-NULL, invoke @a ctx->notify_func2 with
 * @a ctx->notify_baton2 for each item handled by the update, and also for
 * files restored from text-base.  If @a ctx->cancel_func is non-NULL, invoke
 * it passing @a ctx->cancel_baton at various places during the update.
 *
 * Use @a pool for any temporary allocation.
 *
 *  @todo  Multiple Targets
 *  - Up for debate:  an update on multiple targets is *not* atomic.
 *  Right now, svn_client_update only takes one path.  What's
 *  debatable is whether this should ever change.  On the one hand,
 *  it's kind of losing to have the client application loop over
 *  targets and call svn_client_update() on each one;  each call to
 *  update initializes a whole new repository session (network
 *  overhead, etc.)  On the other hand, it's a very simple
 *  implementation, and allows for the possibility that different
 *  targets may come from different repositories.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_update4(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_boolean_t adds_as_modification,
                   svn_boolean_t make_parents,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_update4() but with @a make_parents always set
 * to FALSE and @a adds_as_modification set to TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_update3(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_update3() but with @a allow_unver_obstructions
 * always set to FALSE, @a depth_is_sticky to FALSE, and @a depth set
 * according to @a recurse: if @a recurse is TRUE, set @a depth to
 * #svn_depth_infinity, if @a recurse is FALSE, set @a depth to
 * #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_update2(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_update2() except that it accepts only a single
 * target in @a path, returns a single revision if @a result_rev is
 * not NULL, and @a ignore_externals is always set to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_update(svn_revnum_t *result_rev,
                  const char *path,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);
/** @} */

/**
 * @defgroup Switch Switch a working copy to another location.
 *
 * @{
 */

/**
 * Switch an existing working copy directory to a different repository
 * location.
 *
 * This is normally used to switch a working copy directory over to another
 * line of development, such as a branch or a tag.  Switching an existing
 * working copy directory is more efficient than checking out @a url from
 * scratch.
 *
 * @param[out] result_rev   If non-NULL, the value of the revision to which
 *                          the working copy was actually switched.
 * @param[in] path      The directory to be switched.  This need not be the
 *              root of a working copy.
 * @param[in] url       The repository URL to switch to.
 * @param[in] peg_revision  The peg revision.
 * @param[in] revision  The operative revision.
 * @param[in] depth     The depth of the operation.  If #svn_depth_infinity,
 *                      switch fully recursively.  Else if #svn_depth_immediates,
 *                      switch @a path and its file children (if any), and
 *                      switch subdirectories but do not update them.  Else if
 *                      #svn_depth_files, switch just file children, ignoring
 *                      subdirectories completely.  Else if #svn_depth_empty,
 *                      switch just @a path and touch nothing underneath it.
 * @param[in] depth_is_sticky   If @c TRUE, and @a depth is not
 *              #svn_depth_unknown, then in addition to switching @a path, also
 *              set its sticky ambient depth value to @a depth.
 * @param[in] ignore_externals  If @c TRUE, don't process externals
 *              definitions as part of this operation.
 * @param[in] allow_unver_obstructions  If @c TRUE, then tolerate existing
 *              unversioned items that obstruct incoming paths.  Only
 *              obstructions of the same type (file or dir) as the added
 *              item are tolerated.  The text of obstructing files is left
 *              as-is, effectively treating it as a user modification after
 *              the checkout.  Working properties of obstructing items are
 *              set equal to the base properties. <br>
 *              If @c FALSE, then abort if there are any unversioned
 *              obstructing items.
 * @param[in] ignore_ancestry  If @c FALSE, then verify that the file
 *              or directory at @a path shares some common version control
 *              ancestry with the switch URL location (represented by the
 *              combination of @a url, @a peg_revision, and @a revision),
 *              and returning #SVN_ERR_CLIENT_UNRELATED_RESOURCES if they
 *              do not. If @c TRUE, no such sanity checks are performed.
 *
 * @param[in] ctx   The standard client context, used for authentication and
 *              notification.  The notifier is invoked for paths affected by
 *              the switch, and also for files which may be restored from the
 *              pristine store after being previously removed from the working
 *              copy.
 * @param[in] pool  Used for any temporary allocation.
 *
 * @return A pointer to an #svn_error_t of the type (this list is not
 *         exhaustive): <br>
 *         #SVN_ERR_CLIENT_BAD_REVISION if @a revision is not one of
 *         #svn_opt_revision_number, #svn_opt_revision_head, or
 *         #svn_opt_revision_date. <br>
 *         If no error occurred, return #SVN_NO_ERROR.
 *
 * @since New in 1.7.
 *
 * @see #svn_depth_t <br> #svn_client_ctx_t <br> @ref clnt_revisions for
 *      a discussion of operative and peg revisions.
 */
svn_error_t *
svn_client_switch3(svn_revnum_t *result_rev,
                   const char *path,
                   const char *url,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_boolean_t ignore_ancestry,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_switch3() but with @a ignore_ancestry always
 * set to TRUE.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_switch2(svn_revnum_t *result_rev,
                   const char *path,
                   const char *url,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_switch2() but with @a allow_unver_obstructions,
 * @a ignore_externals, and @a depth_is_sticky always set to FALSE,
 * and @a depth set according to @a recurse: if @a recurse is TRUE,
 * set @a depth to #svn_depth_infinity, if @a recurse is FALSE, set
 * @a depth to #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_switch(svn_revnum_t *result_rev,
                  const char *path,
                  const char *url,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Add Begin versioning files/directories in a working copy.
 *
 * @{
 */

/**
 * Schedule a working copy @a path for addition to the repository.
 *
 * If @a depth is #svn_depth_empty, add just @a path and nothing
 * below it.  If #svn_depth_files, add @a path and any file
 * children of @a path.  If #svn_depth_immediates, add @a path, any
 * file children, and any immediate subdirectories (but nothing
 * underneath those subdirectories).  If #svn_depth_infinity, add
 * @a path and everything under it fully recursively.
 *
 * @a path's parent must be under revision control already (unless
 * @a add_parents is TRUE), but @a path is not.
 *
 * If @a force is not set and @a path is already under version
 * control, return the error #SVN_ERR_ENTRY_EXISTS.  If @a force is
 * set, do not error on already-versioned items.  When used on a
 * directory in conjunction with a @a depth value greater than
 * #svn_depth_empty, this has the effect of scheduling for addition
 * any unversioned files and directories scattered within even a
 * versioned tree (up to @a depth).
 *
 * If @a ctx->notify_func2 is non-NULL, then for each added item, call
 * @a ctx->notify_func2 with @a ctx->notify_baton2 and the path of the
 * added item.
 *
 * If @a no_ignore is FALSE, don't add any file or directory (or recurse
 * into any directory) that is unversioned and found by recursion (as
 * opposed to being the explicit target @a path) and whose name matches the
 * svn:ignore property on its parent directory or the global-ignores list in
 * @a ctx->config. If @a no_ignore is TRUE, do include such files and
 * directories. (Note that an svn:ignore property can influence this
 * behaviour only when recursing into an already versioned directory with @a
 * force.)
 *
 * If @a no_autoprops is TRUE, don't set any autoprops on added files. If
 * @a no_autoprops is FALSE then all added files have autprops set as per
 * the auto-props list in @a ctx->config and the value of any
 * @c SVN_PROP_INHERITABLE_AUTO_PROPS properties inherited by the nearest
 * parents of @a path which are already under version control.
 *
 * If @a add_parents is TRUE, recurse up @a path's directory and look for
 * a versioned directory.  If found, add all intermediate paths between it
 * and @a path.  If not found, return #SVN_ERR_CLIENT_NO_VERSIONED_PARENT.
 *
 * @a scratch_pool is used for temporary allocations only.
 *
 * @par Important:
 * This is a *scheduling* operation.  No changes will
 * happen to the repository until a commit occurs.  This scheduling
 * can be removed with svn_client_revert2().
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_add5(const char *path,
                svn_depth_t depth,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_boolean_t no_autoprops,
                svn_boolean_t add_parents,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_add5(), but with @a no_autoprops always set to
 * FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_add4(const char *path,
                svn_depth_t depth,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_boolean_t add_parents,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Similar to svn_client_add4(), but with @a add_parents always set to
 * FALSE and @a depth set according to @a recursive: if TRUE, then
 * @a depth is #svn_depth_infinity, if FALSE, then #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_add3(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Similar to svn_client_add3(), but with @a no_ignore always set to
 * FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_add2(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Similar to svn_client_add2(), but with @a force always set to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_add(const char *path,
               svn_boolean_t recursive,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool);

/** @} */

/**
 * @defgroup Mkdir Create directories in a working copy or repository.
 *
 * @{
 */

/** Create a directory, either in a repository or a working copy.
 *
 * @a paths is an array of (const char *) paths, either all local WC paths
 * or all URLs.
 *
 * If @a paths contains URLs, use the authentication baton in @a ctx
 * and @a message to immediately attempt to commit the creation of the
 * directories in @a paths in the repository.
 *
 * Else, create the directories on disk, and attempt to schedule them
 * for addition (using svn_client_add(), whose docstring you should
 * read).
 *
 * If @a make_parents is TRUE, create any non-existent parent directories
 * also.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision in
 * the event that this is a committing operation.  This table cannot
 * contain any standard Subversion properties.
 *
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 are a callback/baton
 * combo that this function can use to query for a commit log message
 * when one is needed.
 *
 * If @a ctx->notify_func2 is non-NULL, when the directory has been created
 * (successfully) in the working copy, call @a ctx->notify_func2 with
 * @a ctx->notify_baton2 and the path of the new directory.  Note that this is
 * only called for items added to the working copy.
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_mkdir4(const apr_array_header_t *paths,
                  svn_boolean_t make_parents,
                  const apr_hash_t *revprop_table,
                  svn_commit_callback2_t commit_callback,
                  void *commit_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_mkdir4(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mkdir3(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t make_parents,
                  const apr_hash_t *revprop_table,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);


/**
 * Same as svn_client_mkdir3(), but with @a make_parents always FALSE,
 * and @a revprop_table always NULL.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mkdir2(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Same as svn_client_mkdir2(), but takes the #svn_client_commit_info_t
 * type for @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mkdir(svn_client_commit_info_t **commit_info_p,
                 const apr_array_header_t *paths,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/** @} */

/**
 * @defgroup Delete Remove files/directories from a working copy or repository.
 *
 * @{
 */

/** Delete items from a repository or working copy.
 *
 * @a paths is an array of (const char *) paths, either all local WC paths
 * or all URLs.
 *
 * If the paths in @a paths are URLs, use the authentication baton in
 * @a ctx and @a ctx->log_msg_func3/@a ctx->log_msg_baton3 to
 * immediately attempt to commit a deletion of the URLs from the
 * repository.  Every path must belong to the same repository.
 *
 * Else, schedule the working copy paths in @a paths for removal from
 * the repository.  Each path's parent must be under revision control.
 * This is just a *scheduling* operation.  No changes will happen to
 * the repository until a commit occurs.  This scheduling can be
 * removed with svn_client_revert2(). If a path is a file it is
 * immediately removed from the working copy. If the path is a
 * directory it will remain in the working copy but all the files, and
 * all unversioned items, it contains will be removed. If @a force is
 * not set then this operation will fail if any path contains locally
 * modified and/or unversioned items. If @a force is set such items
 * will be deleted.
 *
 * If the paths are working copy paths and @a keep_local is TRUE then
 * the paths will not be removed from the working copy, only scheduled
 * for removal from the repository.  Once the scheduled deletion is
 * committed, they will appear as unversioned paths in the working copy.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision in
 * the event that this is a committing operation.  This table cannot
 * contain any standard Subversion properties.
 *
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 are a callback/baton
 * combo that this function can use to query for a commit log message
 * when one is needed.
 *
 * If @a ctx->notify_func2 is non-NULL, then for each item deleted, call
 * @a ctx->notify_func2 with @a ctx->notify_baton2 and the path of the deleted
 * item.
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_delete4(const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_boolean_t keep_local,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_delete4(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_delete3(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_boolean_t keep_local,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_delete3(), but with @a keep_local always set
 * to FALSE, and @a revprop_table passed as NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_delete2(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_delete2(), but takes the #svn_client_commit_info_t
 * type for @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_delete(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);


/** @} */

/**
 * @defgroup Import Import files into the repository.
 *
 * @{
 */

/**
 * The callback invoked by svn_client_import5() before adding a node to the
 * list of nodes to be imported.
 *
 * @a baton is the value passed to @a svn_client_import5 as filter_baton.
 *
 * The callback receives the @a local_abspath for each node and the @a dirent
 * for it when walking the directory tree. Only the kind of node, including
 * special status is available in @a dirent.
 *
 * Implementations can set @a *filtered to TRUE, to make the import
 * process omit the node and (if the node is a directory) all its
 * descendants.
 *
 * @a scratch_pool can be used for temporary allocations.
 *
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_client_import_filter_func_t)(
  void *baton,
  svn_boolean_t *filtered,
  const char *local_abspath,
  const svn_io_dirent2_t *dirent,
  apr_pool_t *scratch_pool);

/** Import file or directory @a path into repository directory @a url at
 * head, authenticating with the authentication baton cached in @a ctx,
 * and using @a ctx->log_msg_func3/@a ctx->log_msg_baton3 to get a log message
 * for the (implied) commit.  If some components of @a url do not exist
 * then create parent directories as necessary.
 *
 * This function reads an unversioned tree from disk and skips any ".svn"
 * directories. Even if a file or directory being imported is part of an
 * existing WC, this function sees it as unversioned and does not notice any
 * existing Subversion properties in it.
 *
 * If @a path is a directory, the contents of that directory are
 * imported directly into the directory identified by @a url.  Note that the
 * directory @a path itself is not imported -- that is, the basename of
 * @a path is not part of the import.
 *
 * If @a path is a file, then the dirname of @a url is the directory
 * receiving the import.  The basename of @a url is the filename in the
 * repository.  In this case if @a url already exists, return error.
 *
 * If @a ctx->notify_func2 is non-NULL, then call @a ctx->notify_func2 with
 * @a ctx->notify_baton2 as the import progresses, with any of the following
 * actions: #svn_wc_notify_commit_added,
 * #svn_wc_notify_commit_postfix_txdelta.
 *
 * Use @a scratch_pool for any temporary allocation.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision.
 * This table cannot contain any standard Subversion properties.
 *
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 are a callback/baton
 * combo that this function can use to query for a commit log message
 * when one is needed.
 *
 * If @a depth is #svn_depth_empty, import just @a path and nothing
 * below it.  If #svn_depth_files, import @a path and any file
 * children of @a path.  If #svn_depth_immediates, import @a path, any
 * file children, and any immediate subdirectories (but nothing
 * underneath those subdirectories).  If #svn_depth_infinity, import
 * @a path and everything under it fully recursively.
 *
 * If @a no_ignore is @c FALSE, don't import any file or directory (or
 * recurse into any directory) that is found by recursion (as opposed to
 * being the explicit target @a path) and whose name matches the
 * global-ignores list in @a ctx->config. If @a no_ignore is @c TRUE, do
 * include such files and directories. (Note that svn:ignore properties are
 * not involved, as auto-props cannot set properties on directories and even
 * if the target is part of a WC the import ignores any existing
 * properties.)
 *
 * If @a no_autoprops is TRUE, don't set any autoprops on imported files. If
 * @a no_autoprops is FALSE then all imported files have autprops set as per
 * the auto-props list in @a ctx->config and the value of any
 * @c SVN_PROP_INHERITABLE_AUTO_PROPS properties inherited by and explicitly set
 * on @a url if @a url is already under versioned control, or the nearest parents
 * of @a path which are already under version control if not.
 *
 * If @a ignore_unknown_node_types is @c TRUE, ignore files of which the
 * node type is unknown, such as device files and pipes.
 *
 * If @a filter_callback is non-NULL, call it for each node that isn't ignored
 * for other reasons with @a filter_baton, to allow third party to ignore
 * specific nodes during importing.
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_import5(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t no_autoprops,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_import_filter_func_t filter_callback,
                   void *filter_baton,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_import5(), but without support for an optional
 * @a filter_callback and @a no_autoprops always set to FALSE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_import4(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_import4(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_import3(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_import3(), but with @a ignore_unknown_node_types
 * always set to @c FALSE, @a revprop_table passed as NULL, and @a
 * depth set according to @a nonrecursive: if TRUE, then @a depth is
 * #svn_depth_files, else #svn_depth_infinity.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API
 */
SVN_DEPRECATED
svn_error_t *
svn_client_import2(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_boolean_t nonrecursive,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_import2(), but with @a no_ignore always set
 * to FALSE and using the #svn_client_commit_info_t type for
 * @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_import(svn_client_commit_info_t **commit_info_p,
                  const char *path,
                  const char *url,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Commit Commit local modifications to the repository.
 *
 * @{
 */

/**
 * Commit files or directories into repository, authenticating with
 * the authentication baton cached in @a ctx, and using
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 to obtain the log message.
 * Set @a *commit_info_p to the results of the commit, allocated in @a pool.
 *
 * @a targets is an array of <tt>const char *</tt> paths to commit.  They
 * need not be canonicalized nor condensed; this function will take care of
 * that.  If @a targets has zero elements, then do nothing and return
 * immediately without error.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision.
 * This table cannot contain any standard Subversion properties.
 *
 * If @a ctx->notify_func2 is non-NULL, then call @a ctx->notify_func2 with
 * @a ctx->notify_baton2 as the commit progresses, with any of the following
 * actions: #svn_wc_notify_commit_modified, #svn_wc_notify_commit_added,
 * #svn_wc_notify_commit_deleted, #svn_wc_notify_commit_replaced,
 * #svn_wc_notify_commit_copied, #svn_wc_notify_commit_copied_replaced,
 * #svn_wc_notify_commit_postfix_txdelta.
 *
 * If @a depth is #svn_depth_infinity, commit all changes to and
 * below named targets.  If @a depth is #svn_depth_empty, commit
 * only named targets (that is, only property changes on named
 * directory targets, and property and content changes for named file
 * targets).  If @a depth is #svn_depth_files, behave as above for
 * named file targets, and for named directory targets, commit
 * property changes on a named directory and all changes to files
 * directly inside that directory.  If #svn_depth_immediates, behave
 * as for #svn_depth_files, and for subdirectories of any named
 * directory target commit as though for #svn_depth_empty.
 *
 * Unlock paths in the repository, unless @a keep_locks is TRUE.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items that are committed;
 * that is, don't commit anything unless it's a member of one of those
 * changelists.  After the commit completes successfully, remove
 * changelist associations from the targets, unless @a
 * keep_changelists is set.  If @a changelists is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * If @a commit_as_operations is set to FALSE, when a copy is committed
 * all changes below the copy are always committed at the same time
 * (independent of the value of @a depth). If @a commit_as_operations is
 * #TRUE, changes to descendants are only committed if they are itself
 * included via @a depth and targets.
 *
 * If @a include_file_externals and/or @a include_dir_externals are #TRUE,
 * also commit all file and/or dir externals (respectively) that are reached
 * by recursion, except for those externals which:
 *     - have a fixed revision, or
 *     - come from a different repository root URL (dir externals).
 * These flags affect only recursion; externals that directly appear in @a
 * targets are always included in the commit.
 *
 * ### TODO: currently, file externals hidden inside an unversioned dir are
 *     skipped deliberately, because we can't commit those yet.
 *     See STMT_SELECT_COMMITTABLE_EXTERNALS_BELOW.
 *
 * ### TODO: With @c depth_immediates, this function acts as if
 *     @a include_dir_externals was passed #FALSE, but caller expects
 *     immediate child dir externals to be included @c depth_empty.
 *
 * When @a commit_as_operations is #TRUE it is possible to delete a node and
 * all its descendants by selecting just the root of the deletion. If it is
 * set to #FALSE this will raise an error.
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @note #svn_depth_unknown and #svn_depth_exclude must not be passed
 * for @a depth.
 *
 * Use @a pool for any temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_commit6(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   svn_boolean_t include_file_externals,
                   svn_boolean_t include_dir_externals,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_commit6(), but passes @a include_file_externals as
 * FALSE and @a include_dir_externals as FALSE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit5(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_commit5(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.  Does not use
 * #svn_wc_notify_commit_copied or #svn_wc_notify_commit_copied_replaced
 * (preferring #svn_wc_notify_commit_added and
 * #svn_wc_notify_commit_replaced, respectively, instead).
 *
 * Also, if no error is returned and @a (*commit_info_p)->revision is set to
 * #SVN_INVALID_REVNUM, then the commit was a no-op; nothing needed to
 * be committed.
 *
 * Sets @a commit_as_operations to FALSE to match Subversion 1.6's behavior.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit4(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_commit4(), but always with NULL for
 * @a changelist_name, FALSE for @a keep_changelist, NULL for @a
 * revprop_table, and @a depth set according to @a recurse: if @a
 * recurse is TRUE, use #svn_depth_infinity, else #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 *
 * @since New in 1.3.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit3(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_commit3(), but uses #svn_client_commit_info_t
 * for @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 *
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit2(svn_client_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_commit2(), but with @a keep_locks set to
 * TRUE and @a nonrecursive instead of @a recurse.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_commit(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *targets,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Status Report interesting information about paths in the \
 *                  working copy.
 *
 * @{
 */

/**
 * Structure for holding the "status" of a working copy item.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, to preserve binary compatibility, users
 * should not directly allocate structures of this type.
 *
 * @since New in 1.7.
 */
typedef struct svn_client_status_t
{
  /** The kind of node as recorded in the working copy */
  svn_node_kind_t kind;

  /** The absolute path to the node */
  const char *local_abspath;

  /** The actual size of the working file on disk, or SVN_INVALID_FILESIZE
   * if unknown (or if the item isn't a file at all). */
  svn_filesize_t filesize;

  /** If the path is under version control, versioned is TRUE, otherwise
   * FALSE. */
  svn_boolean_t versioned;

  /** Set to TRUE if the node is the victim of some kind of conflict. */
  svn_boolean_t conflicted;

  /** The status of the node, based on the restructuring changes and if the
   * node has no restructuring changes the text and prop status. */
  enum svn_wc_status_kind node_status;

  /** The status of the text of the node, not including restructuring changes.
   * Valid values are: svn_wc_status_none, svn_wc_status_normal,
   * svn_wc_status_modified and svn_wc_status_conflicted. */
  enum svn_wc_status_kind text_status;

  /** The status of the node's properties.
   * Valid values are: svn_wc_status_none, svn_wc_status_normal,
   * svn_wc_status_modified and svn_wc_status_conflicted. */
  enum svn_wc_status_kind prop_status;

  /** A node can be 'locked' if a working copy update is in progress or
   * was interrupted. */
  svn_boolean_t wc_is_locked;

  /** A file or directory can be 'copied' if it's scheduled for
   * addition-with-history (or part of a subtree that is scheduled as such.).
   */
  svn_boolean_t copied;

  /** The URL of the repository root. */
  const char *repos_root_url;

  /** The UUID of the repository */
  const char *repos_uuid;

  /** The in-repository path relative to the repository root. */
  const char *repos_relpath;

  /** Base revision. */
  svn_revnum_t revision;

  /** Last revision this was changed */
  svn_revnum_t changed_rev;

  /** Date of last commit. */
  apr_time_t changed_date;

  /** Last commit author of this item */
  const char *changed_author;

  /** A file or directory can be 'switched' if the switch command has been
   * used.  If this is TRUE, then file_external will be FALSE.
   */
  svn_boolean_t switched;

  /** If the item is a file that was added to the working copy with an
   * svn:externals; if file_external is TRUE, then switched is always
   * FALSE.
   */
  svn_boolean_t file_external;

  /** The locally present lock. (Values of path, token, owner, comment and
   * are available if a lock is present) */
  const svn_lock_t *lock;

  /** Which changelist this item is part of, or NULL if not part of any. */
  const char *changelist;

  /** The depth of the node as recorded in the working copy
   * (#svn_depth_unknown for files or when no depth is recorded) */
  svn_depth_t depth;

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

  /** The node's text status in the repository. */
  enum svn_wc_status_kind repos_text_status;

  /** The node's property status in the repository. */
  enum svn_wc_status_kind repos_prop_status;

  /** The node's lock in the repository, if any. */
  const svn_lock_t *repos_lock;

  /** Set to the youngest committed revision, or #SVN_INVALID_REVNUM
   * if not out of date. */
  svn_revnum_t ood_changed_rev;

  /** Set to the most recent commit date, or @c 0 if not out of date. */
  apr_time_t ood_changed_date;

  /** Set to the user name of the youngest commit, or @c NULL if not
   * out of date or non-existent.  Because a non-existent @c
   * svn:author property has the same behavior as an out-of-date
   * working copy, examine @c ood_changed_rev to determine whether
   * the working copy is out of date. */
  const char *ood_changed_author;

  /** @} */

  /** Reserved for libsvn_client's internal use; this value is only to be used
   * for libsvn_client backwards compatibility wrappers. This value may be NULL
   * or to other data in future versions. */
  const void *backwards_compatibility_baton;

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

  /* NOTE! Please update svn_client_status_dup() when adding new fields here. */
} svn_client_status_t;

/**
 * Return a duplicate of @a status, allocated in @a result_pool. No part of the new
 * structure will be shared with @a status.
 *
 * @since New in 1.7.
 */
svn_client_status_t *
svn_client_status_dup(const svn_client_status_t *status,
                      apr_pool_t *result_pool);

/**
 * A callback for reporting a @a status about @a path (which may be an
 * absolute or relative path).
 *
 * @a baton is a closure object; it should be provided by the
 * implementation, and passed by the caller.
 *
 * @a scratch_pool will be cleared between invocations to the callback.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_client_status_func_t)(
                                            void *baton,
                                            const char *path,
                                            const svn_client_status_t *status,
                                            apr_pool_t *scratch_pool);

/**
 * Given @a path to a working copy directory (or single file), call
 * @a status_func/status_baton with a set of #svn_wc_status_t *
 * structures which describe the status of @a path, and its children
 * (recursing according to @a depth).
 *
 *    - If @a get_all is set, retrieve all entries; otherwise,
 *      retrieve only "interesting" entries (local mods and/or
 *      out of date).
 *
 *    - If @a check_out_of_date is set, contact the repository and
 *      augment the status structures with information about
 *      out-of-dateness (with respect to @a revision).  Also, if @a
 *      result_rev is not @c NULL, set @a *result_rev to the actual
 *      revision against which the working copy was compared (@a
 *      *result_rev is not meaningful unless @a check_out_of_date is
 *      set).
 *
 *    - If @a check_working_copy is not set, do not scan the working
 *      copy for local modifications. This parameter will be ignored
 *      unless @a check_out_of_date is set.  When set, the status
 *      report will not contain any information about local changes in
 *      the working copy; this includes local deletions and
 *      replacements.
 *
 * If @a no_ignore is @c FALSE, don't report any file or directory (or
 * recurse into any directory) that is found by recursion (as opposed to
 * being the explicit target @a path) and whose name matches the
 * svn:ignore property on its parent directory or the global-ignores
 * list in @a ctx->config. If @a no_ignore is @c TRUE, report each such
 * file or directory with the status code #svn_wc_status_ignored.
 *
 * If @a ignore_externals is not set, then recurse into externals
 * definitions (if any exist) after handling the main target.  This
 * calls the client notification function (in @a ctx) with the
 * #svn_wc_notify_status_external action before handling each externals
 * definition, and with #svn_wc_notify_status_completed
 * after each.
 *
 * If @a depth_as_sticky is set and @a depth is not
 * #svn_depth_unknown, then the status is calculated as if depth_is_sticky
 * was passed to an equivalent update command.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose statuses are
 * reported; that is, don't report status about any item unless
 * it's a member of one of those changelists.  If @a changelists is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * If @a path is an absolute path then the @c path parameter passed in each
 * call to @a status_func will be an absolute path.
 *
 * All temporary allocations are performed in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_status6(svn_revnum_t *result_rev,
                   svn_client_ctx_t *ctx,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t check_out_of_date,
                   svn_boolean_t check_working_copy,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t depth_as_sticky,
                   const apr_array_header_t *changelists,
                   svn_client_status_func_t status_func,
                   void *status_baton,
                   apr_pool_t *scratch_pool);


/**
 * Same as svn_client_status6(), but with @a check_out_of_date set to
 * @a update and @a check_working_copy set to @c TRUE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_status5(svn_revnum_t *result_rev,
                   svn_client_ctx_t *ctx,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t depth_as_sticky,
                   const apr_array_header_t *changelists,
                   svn_client_status_func_t status_func,
                   void *status_baton,
                   apr_pool_t *scratch_pool);

/**
 * Same as svn_client_status5(), but using #svn_wc_status_func3_t
 * instead of #svn_client_status_func_t and depth_as_sticky set to TRUE.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_status4(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func3_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Same as svn_client_status4(), but using an #svn_wc_status_func2_t
 * instead of an #svn_wc_status_func3_t.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_status3(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Like svn_client_status3(), except with @a changelists passed as @c
 * NULL, and with @a recurse instead of @a depth.  If @a recurse is
 * TRUE, behave as if for #svn_depth_infinity; else if @a recurse is
 * FALSE, behave as if for #svn_depth_immediates.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_status2(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_boolean_t recurse,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_status2(), but with @a ignore_externals
 * always set to FALSE, taking the #svn_wc_status_func_t type
 * instead of the #svn_wc_status_func2_t type for @a status_func,
 * and requiring @a *revision to be non-const even though it is
 * treated as constant.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_status(svn_revnum_t *result_rev,
                  const char *path,
                  svn_opt_revision_t *revision,
                  svn_wc_status_func_t status_func,
                  void *status_baton,
                  svn_boolean_t recurse,
                  svn_boolean_t get_all,
                  svn_boolean_t update,
                  svn_boolean_t no_ignore,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Log View information about previous revisions of an object.
 *
 * @{
 */

/**
 * Invoke @a receiver with @a receiver_baton on each log message from
 * each (#svn_opt_revision_range_t *) range in @a revision_ranges in turn,
 * inclusive (but never invoke @a receiver on a given log message more
 * than once).
 *
 * @a targets contains either a URL followed by zero or more relative
 * paths, or 1 working copy path, as <tt>const char *</tt>, for which log
 * messages are desired.  @a receiver is invoked only on messages whose
 * revisions involved a change to some path in @a targets.  @a peg_revision
 * indicates in which revision @a targets are valid.  If @a peg_revision is
 * #svn_opt_revision_unspecified, it defaults to #svn_opt_revision_head
 * for URLs or #svn_opt_revision_working for WC paths.
 *
 * If @a limit is greater than zero only invoke @a receiver on the first
 * @a limit logs.
 *
 * If @a discover_changed_paths is set, then the @c changed_paths and @c
 * changed_paths2 fields in the @c log_entry argument to @a receiver will be
 * populated on each invocation.  @note The @c text_modified and @c
 * props_modified fields of the changed paths structure may have the value
 * #svn_tristate_unknown if the repository does not report that information.
 *
 * If @a strict_node_history is set, copy history (if any exists) will
 * not be traversed while harvesting revision logs for each target.
 *
 * If @a include_merged_revisions is set, log information for revisions
 * which have been merged to @a targets will also be returned.
 *
 * If @a revprops is NULL, retrieve all revision properties; else, retrieve
 * only the revision properties named by the (const char *) array elements
 * (i.e. retrieve none if the array is empty).
 *
 * Use @a pool for any temporary allocation.
 *
 * If @a ctx->notify_func2 is non-NULL, then call @a ctx->notify_func2/baton2
 * with a 'skip' signal on any unversioned targets.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_client_log5(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const apr_array_header_t *revision_ranges,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Similar to svn_client_log5(), but takes explicit start and end parameters
 * instead of an array of revision ranges.
 *
 * @deprecated Provided for compatibility with the 1.5 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_log4(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Similar to svn_client_log4(), but using #svn_log_message_receiver_t
 * instead of #svn_log_entry_receiver_t.  Also, @a
 * include_merged_revisions is set to @c FALSE and @a revprops is
 * svn:author, svn:date, and svn:log.
 *
 * @deprecated Provided for compatibility with the 1.4 API.
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_log3(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);


/**
 * Similar to svn_client_log3(), but with the @c kind field of
 * @a peg_revision set to #svn_opt_revision_unspecified.
 *
 * @par Important:
 * A special case for the revision range HEAD:1, which was present
 * in svn_client_log(), has been removed from svn_client_log2().  Instead, it
 * is expected that callers will specify the range HEAD:0, to avoid a
 * #SVN_ERR_FS_NO_SUCH_REVISION error when invoked against an empty repository
 * (i.e. one not containing a revision 1).
 *
 * @deprecated Provided for compatibility with the 1.3 API.
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_log2(const apr_array_header_t *targets,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);


/**
 * Similar to svn_client_log2(), but with @a limit set to 0, and the
 * following special case:
 *
 * Special case for repositories at revision 0:
 *
 * If @a start->kind is #svn_opt_revision_head, and @a end->kind is
 * #svn_opt_revision_number && @a end->number is @c 1, then handle an
 * empty (no revisions) repository specially: instead of erroring
 * because requested revision 1 when the highest revision is 0, just
 * invoke @a receiver on revision 0, passing @c NULL for changed paths and
 * empty strings for the author and date.  This is because that
 * particular combination of @a start and @a end usually indicates the
 * common case of log invocation -- the user wants to see all log
 * messages from youngest to oldest, where the oldest commit is
 * revision 1.  That works fine, except when there are no commits in
 * the repository, hence this special case.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_log(const apr_array_header_t *targets,
               const svn_opt_revision_t *start,
               const svn_opt_revision_t *end,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_log_message_receiver_t receiver,
               void *receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool);

/** @} */

/**
 * @defgroup Blame Show modification information about lines in a file.
 *
 * @{
 */

/**
 * Invoke @a receiver with @a receiver_baton on each line-blame item
 * associated with revision @a end of @a path_or_url, using @a start
 * as the default source of all blame.  @a peg_revision indicates in
 * which revision @a path_or_url is valid.  If @a peg_revision->kind
 * is #svn_opt_revision_unspecified, then it defaults to
 * #svn_opt_revision_head for URLs or #svn_opt_revision_working for
 * WC targets.
 *
 * If @a start->kind or @a end->kind is #svn_opt_revision_unspecified,
 * return the error #SVN_ERR_CLIENT_BAD_REVISION.  If either are
 * #svn_opt_revision_working, return the error
 * #SVN_ERR_UNSUPPORTED_FEATURE.  If any of the revisions of @a
 * path_or_url have a binary mime-type, return the error
 * #SVN_ERR_CLIENT_IS_BINARY_FILE, unless @a ignore_mime_type is TRUE,
 * in which case blame information will be generated regardless of the
 * MIME types of the revisions.
 *
 * @a start may resolve to a revision number greater (younger) than @a end
 * only if the server is 1.8.0 or greater (supports
 * #SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE) and the client is 1.9.0 or
 * newer.
 *
 * Use @a diff_options to determine how to compare different revisions of the
 * target.
 *
 * If @a include_merged_revisions is TRUE, also return data based upon
 * revisions which have been merged to @a path_or_url.
 *
 * Use @a pool for any temporary allocation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_blame5(const char *path_or_url,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_boolean_t include_merged_revisions,
                  svn_client_blame_receiver3_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);


/**
 * Similar to svn_client_blame5(), but with #svn_client_blame_receiver3_t
 * as the receiver.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API.
 *
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_blame4(const char *path_or_url,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_boolean_t include_merged_revisions,
                  svn_client_blame_receiver2_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_blame4(), but with @a include_merged_revisions set
 * to FALSE, and using a #svn_client_blame_receiver2_t as the receiver.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 *
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_blame3(const char *path_or_url,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_blame3(), but with @a diff_options set to
 * default options as returned by svn_diff_file_options_parse() and
 * @a ignore_mime_type set to FALSE.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 *
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_blame2(const char *path_or_url,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_blame2() except that @a peg_revision is always
 * the same as @a end.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_blame(const char *path_or_url,
                 const svn_opt_revision_t *start,
                 const svn_opt_revision_t *end,
                 svn_client_blame_receiver_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/** @} */

/**
 * @defgroup Diff Generate differences between paths.
 *
 * @{
 */

/**
 * Produce diff output which describes the delta between
 * @a path_or_url1/@a revision1 and @a path_or_url2/@a revision2.  Print
 * the output of the diff to @a outstream, and any errors to @a
 * errstream.  @a path_or_url1 and @a path_or_url2 can be either
 * working-copy paths or URLs.
 *
 * If @a relative_to_dir is not @c NULL, the original path and
 * modified path will have the @a relative_to_dir stripped from the
 * front of the respective paths.  If @a relative_to_dir is @c NULL,
 * paths will not be modified.  If @a relative_to_dir is not
 * @c NULL but @a relative_to_dir is not a parent path of the target,
 * an error is returned. Finally, if @a relative_to_dir is a URL, an
 * error will be returned.
 *
 * If either @a revision1 or @a revision2 has an `unspecified' or
 * unrecognized `kind', return #SVN_ERR_CLIENT_BAD_REVISION.
 *
 * @a path_or_url1 and @a path_or_url2 must both represent the same node
 * kind -- that is, if @a path_or_url1 is a directory, @a path_or_url2
 * must also be, and if @a path_or_url1 is a file, @a path_or_url2 must
 * also be.
 *
 * If @a depth is #svn_depth_infinity, diff fully recursively.
 * Else if it is #svn_depth_immediates, diff the named paths and
 * their file children (if any), and diff properties of
 * subdirectories, but do not descend further into the subdirectories.
 * Else if #svn_depth_files, behave as if for #svn_depth_immediates
 * except don't diff properties of subdirectories.  If
 * #svn_depth_empty, diff exactly the named paths but nothing
 * underneath them.
 *
 * Use @a ignore_ancestry to control whether or not items being
 * diffed will be checked for relatedness first.  Unrelated items
 * are typically transmitted to the editor as a deletion of one thing
 * and the addition of another, but if this flag is TRUE, unrelated
 * items will be diffed as if they were related.
 *
 * If @a no_diff_added is TRUE, then no diff output will be generated
 * on added files.
 *
 * If @a no_diff_deleted is TRUE, then no diff output will be
 * generated on deleted files.
 *
 * If @a show_copies_as_adds is TRUE, then copied files will not be diffed
 * against their copyfrom source, and will appear in the diff output
 * in their entirety, as if they were newly added.
 * ### BUGS: For a repos-repos diff, this is ignored. Instead, a file is
 *     diffed against its copyfrom source iff the file is the diff target
 *     and not if some parent directory is the diff target. For a repos-WC
 *     diff, this is ignored if the file is the diff target.
 *
 * If @a use_git_diff_format is TRUE, then the git's extended diff format
 * will be used.
 * ### Do we need to say more about the format? A reference perhaps?
 *
 * If @a ignore_properties is TRUE, do not show property differences.
 * If @a properties_only is TRUE, show only property changes.
 * The above two options are mutually exclusive. It is an error to set
 * both to TRUE.
 *
 * Generated headers are encoded using @a header_encoding.
 *
 * Diff output will not be generated for binary files, unless @a
 * ignore_content_type is TRUE, in which case diffs will be shown
 * regardless of the content types.
 *
 * @a diff_options (an array of <tt>const char *</tt>) is used to pass
 * additional command line options to the diff processes invoked to compare
 * files. @a diff_options is allowed to be @c NULL, in which case a value
 * for this option might still be obtained from the Subversion configuration
 * file via client context @a ctx.
 *
 * The authentication baton cached in @a ctx is used to communicate with
 * the repository.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose differences are
 * reported; that is, don't generate diffs about any item unless
 * it's a member of one of those changelists.  If @a changelists is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * @note Changelist filtering only applies to diffs in which at least
 * one side of the diff represents working copy data.
 *
 * @note @a header_encoding doesn't affect headers generated by external
 * diff programs.
 *
 * @note @a relative_to_dir doesn't affect the path index generated by
 * external diff programs.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_diff6(const apr_array_header_t *diff_options,
                 const char *path_or_url1,
                 const svn_opt_revision_t *revision1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_added,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t ignore_properties,
                 svn_boolean_t properties_only,
                 svn_boolean_t use_git_diff_format,
                 const char *header_encoding,
                 svn_stream_t *outstream,
                 svn_stream_t *errstream,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/** Similar to svn_client_diff6(), but with @a outfile and @a errfile,
 * instead of @a outstream and @a errstream, and with @a
 * no_diff_added, @a ignore_properties, and @a properties_only always
 * passed as @c FALSE (which means that additions and property changes
 * are always transmitted).
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff5(const apr_array_header_t *diff_options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t use_git_diff_format,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_diff5(), but with @a show_copies_as_adds set to
 * @c FALSE and @a use_git_diff_format set to @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff4(const apr_array_header_t *diff_options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_diff4(), but with @a changelists passed as @c
 * NULL, and @a depth set according to @a recurse: if @a recurse is
 * TRUE, set @a depth to #svn_depth_infinity, if @a recurse is
 * FALSE, set @a depth to #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * @since New in 1.3.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff3(const apr_array_header_t *diff_options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);


/**
 * Similar to svn_client_diff3(), but with @a header_encoding set to
 * @c APR_LOCALE_CHARSET.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff2(const apr_array_header_t *diff_options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_diff2(), but with @a ignore_content_type
 * always set to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff(const apr_array_header_t *diff_options,
                const char *path1,
                const svn_opt_revision_t *revision1,
                const char *path2,
                const svn_opt_revision_t *revision2,
                svn_boolean_t recurse,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t no_diff_deleted,
                apr_file_t *outfile,
                apr_file_t *errfile,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Produce diff output which describes the delta between the filesystem
 * object @a path_or_url in peg revision @a peg_revision, as it changed
 * between @a start_revision and @a end_revision.  @a path_or_url can
 * be either a working-copy path or URL.
 *
 * If @a peg_revision is #svn_opt_revision_unspecified, behave
 * identically to svn_client_diff6(), using @a path_or_url for both of that
 * function's @a path_or_url1 and @a path_or_url2 arguments.
 *
 * All other options are handled identically to svn_client_diff6().
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_diff_peg6(const apr_array_header_t *diff_options,
                     const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_added,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t ignore_properties,
                     svn_boolean_t properties_only,
                     svn_boolean_t use_git_diff_format,
                     const char *header_encoding,
                     svn_stream_t *outstream,
                     svn_stream_t *errstream,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/** Similar to svn_client_diff6_peg6(), but with @a outfile and @a errfile,
 * instead of @a outstream and @a errstream, and with @a
 * no_diff_added, @a ignore_properties, and @a properties_only always
 * passed as @c FALSE (which means that additions and property changes
 * are always transmitted).
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_peg5(const apr_array_header_t *diff_options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t use_git_diff_format,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_diff_peg5(), but with @a show_copies_as_adds set to
 * @c FALSE and @a use_git_diff_format set to @c FALSE.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_peg4(const apr_array_header_t *diff_options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_diff_peg4(), but with @a changelists passed
 * as @c NULL, and @a depth set according to @a recurse: if @a recurse
 * is TRUE, set @a depth to #svn_depth_infinity, if @a recurse is
 * FALSE, set @a depth to #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * @since New in 1.3.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_peg3(const apr_array_header_t *diff_options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_diff_peg3(), but with @a header_encoding set to
 * @c APR_LOCALE_CHARSET.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_peg2(const apr_array_header_t *diff_options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_diff_peg2(), but with @a ignore_content_type
 * always set to FALSE.
 *
 * @since New in 1.1.
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_peg(const apr_array_header_t *diff_options,
                    const char *path,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *start_revision,
                    const svn_opt_revision_t *end_revision,
                    svn_boolean_t recurse,
                    svn_boolean_t ignore_ancestry,
                    svn_boolean_t no_diff_deleted,
                    apr_file_t *outfile,
                    apr_file_t *errfile,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/**
 * Produce a diff summary which lists the changed items between
 * @a path_or_url1/@a revision1 and @a path_or_url2/@a revision2 without
 * creating text deltas. @a path_or_url1 and @a path_or_url2 can be
 * either working-copy paths or URLs.
 *
 * The function may report false positives if @a ignore_ancestry is false,
 * since a file might have been modified between two revisions, but still
 * have the same contents.
 *
 * Calls @a summarize_func with @a summarize_baton for each difference
 * with a #svn_client_diff_summarize_t structure describing the difference.
 *
 * See svn_client_diff6() for a description of the other parameters.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_diff_summarize2(const char *path_or_url1,
                           const svn_opt_revision_t *revision1,
                           const char *path_or_url2,
                           const svn_opt_revision_t *revision2,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           const apr_array_header_t *changelists,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool);

/**
 * Similar to svn_client_diff_summarize2(), but with @a changelists
 * passed as @c NULL, and @a depth set according to @a recurse: if @a
 * recurse is TRUE, set @a depth to #svn_depth_infinity, if @a
 * recurse is FALSE, set @a depth to #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 *
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_summarize(const char *path1,
                          const svn_opt_revision_t *revision1,
                          const char *path2,
                          const svn_opt_revision_t *revision2,
                          svn_boolean_t recurse,
                          svn_boolean_t ignore_ancestry,
                          svn_client_diff_summarize_func_t summarize_func,
                          void *summarize_baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool);

/**
 * Produce a diff summary which lists the changed items between the
 * filesystem object @a path_or_url in peg revision @a peg_revision, as it
 * changed between @a start_revision and @a end_revision. @a path_or_url can
 * be either a working-copy path or URL.
 *
 * If @a peg_revision is #svn_opt_revision_unspecified, behave
 * identically to svn_client_diff_summarize2(), using @a path_or_url for
 * both of that function's @a path_or_url1 and @a path_or_url2 arguments.
 *
 * The function may report false positives if @a ignore_ancestry is false,
 * as described in the documentation for svn_client_diff_summarize2().
 *
 * Call @a summarize_func with @a summarize_baton for each difference
 * with a #svn_client_diff_summarize_t structure describing the difference.
 *
 * See svn_client_diff_peg5() for a description of the other parameters.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_diff_summarize_peg2(const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               svn_depth_t depth,
                               svn_boolean_t ignore_ancestry,
                               const apr_array_header_t *changelists,
                               svn_client_diff_summarize_func_t summarize_func,
                               void *summarize_baton,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool);

/**
 * Similar to svn_client_diff_summarize_peg2(), but with @a
 * changelists passed as @c NULL, and @a depth set according to @a
 * recurse: if @a recurse is TRUE, set @a depth to
 * #svn_depth_infinity, if @a recurse is FALSE, set @a depth to
 * #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 *
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_diff_summarize_peg(const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *start_revision,
                              const svn_opt_revision_t *end_revision,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_ancestry,
                              svn_client_diff_summarize_func_t summarize_func,
                              void *summarize_baton,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool);

/** @} */

/**
 * @defgroup Merge Merge changes between branches.
 *
 * @{
 */

/** Get information about the state of merging between two branches.
 *
 * The source is specified by @a source_path_or_url at @a source_revision.
 * The target is specified by @a target_path_or_url at @a target_revision,
 * which refers to either a WC or a repository location.
 *
 * Set @a *needs_reintegration to true if an automatic merge from source
 * to target would be a reintegration merge: that is, if the last automatic
 * merge was in the opposite direction; or to false otherwise.
 *
 * Set @a *yca_url, @a *yca_rev, @a *base_url, @a *base_rev, @a *right_url,
 * @a *right_rev, @a *target_url, @a *target_rev to the repository locations
 * of, respectively: the youngest common ancestor of the branches, the base
 * chosen for 3-way merge, the right-hand side of the source diff, and the
 * target.
 *
 * Set @a repos_root_url to the URL of the repository root.  This is a
 * common prefix of all four URL outputs.
 *
 * Allocate the results in @a result_pool.  Any of the output pointers may
 * be NULL if not wanted.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_get_merging_summary(svn_boolean_t *needs_reintegration,
                               const char **yca_url, svn_revnum_t *yca_rev,
                               const char **base_url, svn_revnum_t *base_rev,
                               const char **right_url, svn_revnum_t *right_rev,
                               const char **target_url, svn_revnum_t *target_rev,
                               const char **repos_root_url,
                               const char *source_path_or_url,
                               const svn_opt_revision_t *source_revision,
                               const char *target_path_or_url,
                               const svn_opt_revision_t *target_revision,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);


/** Merge changes from @a source1/@a revision1 to @a source2/@a revision2 into
 * the working-copy path @a target_wcpath.
 *
 * @a source1 and @a source2 are either URLs that refer to entries in the
 * repository, or paths to entries in the working copy.
 *
 * By "merging", we mean:  apply file differences using
 * svn_wc_merge(), and schedule additions & deletions when appropriate.
 *
 * @a source1 and @a source2 must both represent the same node kind -- that
 * is, if @a source1 is a directory, @a source2 must also be, and if @a source1
 * is a file, @a source2 must also be.
 *
 * If either @a revision1 or @a revision2 has an `unspecified' or
 * unrecognized `kind', return #SVN_ERR_CLIENT_BAD_REVISION.
 *
 * If @a depth is #svn_depth_infinity, merge fully recursively.
 * Else if #svn_depth_immediates, merge changes at most to files
 * that are immediate children of @a target_wcpath and to directory
 * properties of @a target_wcpath and its immediate subdirectory children.
 * Else if #svn_depth_files, merge at most to immediate file
 * children of @a target_wcpath and to @a target_wcpath itself.
 * Else if #svn_depth_empty, apply changes only to @a target_wcpath
 * (i.e., directory property changes only)
 *
 * If @a depth is #svn_depth_unknown, use the depth of @a target_wcpath.
 *
 * If @a ignore_mergeinfo is true, disable merge tracking, by treating the
 * two sources as unrelated even if they actually have a common ancestor.
 *
 * If @a diff_ignore_ancestry is true, diff unrelated nodes as if related:
 * that is, diff the 'left' and 'right' versions of a node as if they were
 * related (if they are the same kind) even if they are not related.
 * Otherwise, diff unrelated items as a deletion of one thing and the
 * addition of another.
 *
 * If @a force_delete is false and the merge involves deleting a file whose
 * content differs from the source-left version, or a locally modified
 * directory, or an unversioned item, then the operation will fail.  If
 * @a force_delete is true then all such items will be deleted.
 *
 * @a merge_options (an array of <tt>const char *</tt>), if non-NULL,
 * is used to pass additional command line arguments to the merge
 * processes (internal or external).  @see
 * svn_diff_file_options_parse().
 *
 * If @a ctx->notify_func2 is non-NULL, then call @a ctx->notify_func2 with @a
 * ctx->notify_baton2 once for each merged target, passing the target's local
 * path.
 *
 * If @a record_only is TRUE, the merge is performed, but is limited only to
 * mergeinfo property changes on existing paths in @a target_wcpath.
 *
 * If @a dry_run is TRUE, the merge is carried out, and full notification
 * feedback is provided, but the working copy is not modified.
 *
 * If allow_mixed_rev is @c FALSE, and @a merge_target is a mixed-revision
 * working copy, raise @c SVN_ERR_CLIENT_MERGE_UPDATE_REQUIRED.
 * Because users rarely intend to merge into mixed-revision working copies,
 * it is recommended to set this parameter to FALSE by default unless the
 * user has explicitly requested a merge into a mixed-revision working copy.
 *
 * The authentication baton cached in @a ctx is used to communicate with the
 * repository.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_merge5(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_mergeinfo,
                  svn_boolean_t diff_ignore_ancestry,
                  svn_boolean_t force_delete,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  svn_boolean_t allow_mixed_rev,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_merge5(), but the single @a ignore_ancestry
 * parameter maps to both @c ignore_mergeinfo and @c diff_ignore_ancestry.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge4(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force_delete,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  svn_boolean_t allow_mixed_rev,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_merge4(), but with @a allow_mixed_rev set to
 * @c TRUE.  The @a force parameter maps to the @c force_delete parameter
 * of svn_client_merge4().
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge3(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/**
 * Similar to svn_client_merge3(), but with @a record_only set to @c
 * FALSE, and @a depth set according to @a recurse: if @a recurse is
 * TRUE, set @a depth to #svn_depth_infinity, if @a recurse is
 * FALSE, set @a depth to #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 *
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge2(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);


/**
 * Similar to svn_client_merge2(), but with @a merge_options set to NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge(const char *source1,
                 const svn_opt_revision_t *revision1,
                 const char *source2,
                 const svn_opt_revision_t *revision2,
                 const char *target_wcpath,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t force,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);


/**
 * Perform a reintegration merge of @a source_path_or_url at @a source_peg_revision
 * into @a target_wcpath.
 * @a target_wcpath must be a single-revision, #svn_depth_infinity,
 * pristine, unswitched working copy -- in other words, it must
 * reflect a single revision tree, the "target".  The mergeinfo on @a
 * source_path_or_url must reflect that all of the target has been merged into it.
 * Then this behaves like a merge with svn_client_merge5() from the
 * target's URL to the source.
 *
 * All other options are handled identically to svn_client_merge5().
 * The depth of the merge is always #svn_depth_infinity.
 *
 * @since New in 1.5.
 * @deprecated Provided for backwards compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge_reintegrate(const char *source_path_or_url,
                             const svn_opt_revision_t *source_peg_revision,
                             const char *target_wcpath,
                             svn_boolean_t dry_run,
                             const apr_array_header_t *merge_options,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/**
 * Merge changes from the source branch identified by
 * @a source_path_or_url in peg revision @a source_peg_revision,
 * into the target branch working copy at @a target_wcpath.
 *
 * If @a ranges_to_merge is NULL then perform an automatic merge of
 * all the eligible changes up to @a source_peg_revision.  If the merge
 * required is a reintegrate merge, then return an error if the WC has
 * mixed revisions, local modifications and/or switched subtrees; if
 * the merge is determined to be of the non-reintegrate kind, then
 * return an error if @a allow_mixed_rev is false and the WC contains
 * mixed revisions.
 *
 * If @a ranges_to_merge is not NULL then merge the changes specified
 * by the revision ranges in @a ranges_to_merge, or, when honouring
 * mergeinfo, only the eligible parts of those revision ranges.
 * @a ranges_to_merge is an array of <tt>svn_opt_revision_range_t
 * *</tt> ranges.  These ranges may describe additive and/or
 * subtractive merge ranges, they may overlap fully or partially,
 * and/or they may partially or fully negate each other.  This
 * rangelist is not required to be sorted.  If any revision in the
 * list of provided ranges has an `unspecified' or unrecognized
 * `kind', return #SVN_ERR_CLIENT_BAD_REVISION.
 *
 * If @a ranges_to_merge is an empty array, then do nothing.
 *
 * All other options are handled identically to svn_client_merge5().
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_merge_peg5(const char *source_path_or_url,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *source_peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_mergeinfo,
                      svn_boolean_t diff_ignore_ancestry,
                      svn_boolean_t force_delete,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      svn_boolean_t allow_mixed_rev,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);

/**
 * Similar to svn_client_merge_peg5(), but automatic merge is not available
 * (@a ranges_to_merge must not be NULL), and the single @a ignore_ancestry
 * parameter maps to both @c ignore_mergeinfo and @c diff_ignore_ancestry.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge_peg4(const char *source_path_or_url,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *source_peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force_delete,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      svn_boolean_t allow_mixed_rev,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);

/**
 * Similar to svn_client_merge_peg4(), but with @a allow_mixed_rev set to
 * @c TRUE.  The @a force parameter maps to the @c force_delete parameter
 * of svn_client_merge_peg4().
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 *
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge_peg3(const char *source,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);

/**
 * Similar to svn_client_merge_peg3(), but with @a record_only set to
 * @c FALSE, and @a depth set according to @a recurse: if @a recurse
 * is TRUE, set @a depth to #svn_depth_infinity, if @a recurse is
 * FALSE, set @a depth to #svn_depth_files.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 *
 * @since New in 1.4.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge_peg2(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);

/**
 * Similar to svn_client_merge_peg2(), but with @a merge_options set to
 * NULL.
 *
 * @deprecated Provided for backwards compatibility with the 1.3 API.
 *
 * @since New in 1.1.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_merge_peg(const char *source,
                     const svn_opt_revision_t *revision1,
                     const svn_opt_revision_t *revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t force,
                     svn_boolean_t dry_run,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);


/** Set @a suggestions to an ordered array of @c const char *
 * potential merge sources (expressed as full repository URLs) for @a
 * path_or_url at @a peg_revision.  @a path_or_url is a working copy
 * path or repository URL.  @a ctx is a context used for
 * authentication in the repository case.  Use @a pool for all
 * allocations.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_suggest_merge_sources(apr_array_header_t **suggestions,
                                 const char *path_or_url,
                                 const svn_opt_revision_t *peg_revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);


/**
 * Get the mergeinfo for a single target node (ignoring any subtrees).
 *
 * Set @a *mergeinfo to a hash mapping <tt>const char *</tt> merge source
 * URLs to <tt>svn_rangelist_t *</tt> rangelists describing the ranges which
 * have been merged into @a path_or_url as of @a peg_revision, per
 * @a path_or_url's explicit mergeinfo or inherited mergeinfo if no
 * explicit mergeinfo if found.  If no explicit or inherited mergeinfo
 * is found, then set @a *mergeinfo to NULL.
 *
 * Use @a pool for all necessary allocations.
 *
 * If the server doesn't support retrieval of mergeinfo (which will
 * never happen for file:// URLs), return an
 * #SVN_ERR_UNSUPPORTED_FEATURE error.
 *
 * @note Unlike most APIs which deal with mergeinfo, this one returns
 * data where the keys of the hash are absolute repository URLs rather
 * than repository filesystem paths.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_mergeinfo_get_merged(apr_hash_t **mergeinfo,
                                const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool);


/**
 * Describe the revisions that either have or have not been merged from
 * one source branch (or subtree) into another.
 *
 * If @a finding_merged is TRUE, then drive log entry callbacks
 * @a receiver / @a receiver_baton with the revisions merged from
 * @a source_path_or_url (as of @a source_peg_revision) into
 * @a target_path_or_url (as of @a target_peg_revision).  If @a
 * finding_merged is FALSE then find the revisions eligible for merging.
 *
 * If both @a source_start_revision and @a source_end_revision are
 * unspecified (that is, of kind @c svn_opt_revision_unspecified),
 * @a receiver will be called the requested revisions from 0 to
 * @a source_peg_revision and in that order (that is, oldest to
 * youngest).  Otherwise, both @a source_start_revision and
 * @a source_end_revision must be specified, which has two effects:
 *
 *   - @a receiver will be called only with revisions which fall
 *     within range of @a source_start_revision to
 *     @a source_end_revision, inclusive, and
 *
 *   - those revisions will be ordered in the same "direction" as the
 *     walk from @a source_start_revision to @a source_end_revision.
 *     (If @a source_start_revision is the younger of the two, @a
 *     receiver will be called with revisions in youngest-to-oldest
 *     order; otherwise, the reverse occurs.)
 *
 * If @a depth is #svn_depth_empty consider only the explicit or
 * inherited mergeinfo on @a target_path_or_url when calculating merged
 * revisions from @a source_path_or_url.  If @a depth is #svn_depth_infinity
 * then also consider the explicit subtree mergeinfo under @a
 * target_path_or_url.
 * If a depth other than #svn_depth_empty or #svn_depth_infinity is
 * requested then return a #SVN_ERR_UNSUPPORTED_FEATURE error.
 *
 * In addition to the behavior of @a discover_changed_paths described in
 * svn_client_log5(), if set to TRUE it enables detection of sub-tree
 * merges that are complete but can't be detected as complete without
 * access to the changed paths.  Sub-tree merges detected as complete will
 * be included if @a finding_merged is TRUE or filtered if @a finding_merged
 * is FALSE.
 *
 * @a revprops is the same as for svn_client_log5().  Use @a scratch_pool for
 * all temporary allocations.
 *
 * @a ctx is a context used for authentication.
 *
 * If the server doesn't support retrieval of mergeinfo, return an
 * #SVN_ERR_UNSUPPORTED_FEATURE error.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_mergeinfo_log2(svn_boolean_t finding_merged,
                          const char *target_path_or_url,
                          const svn_opt_revision_t *target_peg_revision,
                          const char *source_path_or_url,
                          const svn_opt_revision_t *source_peg_revision,
                          const svn_opt_revision_t *source_start_revision,
                          const svn_opt_revision_t *source_end_revision,
                          svn_log_entry_receiver_t receiver,
                          void *receiver_baton,
                          svn_boolean_t discover_changed_paths,
                          svn_depth_t depth,
                          const apr_array_header_t *revprops,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_mergeinfo_log2(), but with @a source_start_revision
 * and @a source_end_revision always of kind @c svn_opt_revision_unspecified;
 *
 * @deprecated Provided for backwards compatibility with the 1.7 API.
 * @since New in 1.7.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mergeinfo_log(svn_boolean_t finding_merged,
                         const char *target_path_or_url,
                         const svn_opt_revision_t *target_peg_revision,
                         const char *source_path_or_url,
                         const svn_opt_revision_t *source_peg_revision,
                         svn_log_entry_receiver_t receiver,
                         void *receiver_baton,
                         svn_boolean_t discover_changed_paths,
                         svn_depth_t depth,
                         const apr_array_header_t *revprops,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_mergeinfo_log(), but finds only merged revisions
 * and always operates at @a depth #svn_depth_empty.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API. Use
 * svn_client_mergeinfo_log() instead.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mergeinfo_log_merged(const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                const char *merge_source_path_or_url,
                                const svn_opt_revision_t *src_peg_revision,
                                svn_log_entry_receiver_t receiver,
                                void *receiver_baton,
                                svn_boolean_t discover_changed_paths,
                                const apr_array_header_t *revprops,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool);

/**
 * Similar to svn_client_mergeinfo_log(), but finds only eligible revisions
 * and always operates at @a depth #svn_depth_empty.
 *
 * @deprecated Provided for backwards compatibility with the 1.6 API. Use
 * svn_client_mergeinfo_log() instead.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_mergeinfo_log_eligible(const char *path_or_url,
                                  const svn_opt_revision_t *peg_revision,
                                  const char *merge_source_path_or_url,
                                  const svn_opt_revision_t *src_peg_revision,
                                  svn_log_entry_receiver_t receiver,
                                  void *receiver_baton,
                                  svn_boolean_t discover_changed_paths,
                                  const apr_array_header_t *revprops,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Cleanup Cleanup an abnormally terminated working copy.
 *
 * @{
 */

/** Recursively vacuum a working copy directory @a dir_abspath,
 * removing unnecessary data.
 *
 * If @a include_externals is @c TRUE, recurse into externals and vacuum them
 * as well.
 *
 * If @a remove_unversioned_items is @c TRUE, remove unversioned items
 * in @a dir_abspath after successful working copy cleanup.
 * If @a remove_ignored_items is @c TRUE, remove ignored unversioned items
 * in @a dir_abspath after successful working copy cleanup.
 *
 * If @a fix_recorded_timestamps is @c TRUE, this function fixes recorded
 * timestamps for unmodified files in the working copy, reducing comparision
 * time on future checks.
 *
 * If @a vacuum_pristines is @c TRUE, and @a dir_abspath points to the working
 * copy root unreferenced files in the pristine store are removed.
 *
 * When asked to remove unversioned or ignored items, and the working copy
 * is already locked, return #SVN_ERR_WC_LOCKED. This prevents accidental
 * working copy corruption in case users run the cleanup operation to
 * remove unversioned items while another client is performing some other
 * operation on the working copy.
 *
 * If @a ctx->cancel_func is non-NULL, invoke it with @a
 * ctx->cancel_baton at various points during the operation.  If it
 * returns an error (typically #SVN_ERR_CANCELLED), return that error
 * immediately.
 *
 * Use @a scratch_pool for any temporary allocations.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_vacuum(const char *dir_abspath,
                  svn_boolean_t remove_unversioned_items,
                  svn_boolean_t remove_ignored_items,
                  svn_boolean_t fix_recorded_timestamps,
                  svn_boolean_t vacuum_pristines,
                  svn_boolean_t include_externals,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool);


/** Recursively cleanup a working copy directory @a dir_abspath, finishing any
 * incomplete operations, removing lockfiles, etc.
 *
 * If @a break_locks is @c TRUE, existing working copy locks at or below @a
 * dir_abspath are broken, otherwise a normal write lock is obtained.
 *
 * If @a fix_recorded_timestamps is @c TRUE, this function fixes recorded
 * timestamps for unmodified files in the working copy, reducing comparision
 * time on future checks.
 *
 * If @a clear_dav_cache is @c TRUE, the caching of DAV information for older
 * mod_dav served repositories is cleared. This clearing invalidates some
 * cached information used for pre-HTTPv2 repositories.
 *
 * If @a vacuum_pristines is @c TRUE, and @a dir_abspath points to the working
 * copy root unreferenced files in the pristine store are removed.
 *
 * If @a include_externals is @c TRUE, recurse into externals and clean
 * them up as well.
 *
 * If @a ctx->cancel_func is non-NULL, invoke it with @a
 * ctx->cancel_baton at various points during the operation.  If it
 * returns an error (typically #SVN_ERR_CANCELLED), return that error
 * immediately.
 *
 * Use @a scratch_pool for any temporary allocations.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_cleanup2(const char *dir_abspath,
                    svn_boolean_t break_locks,
                    svn_boolean_t fix_recorded_timestamps,
                    svn_boolean_t clear_dav_cache,
                    svn_boolean_t vacuum_pristines,
                    svn_boolean_t include_externals,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool);

/** Like svn_client_cleanup2(), but no support for not breaking locks and
 * cleaning up externals and using a potentially non absolute path.
 *
 * @deprecated Provided for limited backwards compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_cleanup(const char *dir,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool);


/** @} */

/**
 * @defgroup Upgrade Upgrade a working copy.
 *
 * @{
 */

/** Recursively upgrade a working copy from any older format to the current
 * WC metadata storage format.  @a wcroot_dir is the path to the WC root.
 *
 * Use @a scratch_pool for any temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_upgrade(const char *wcroot_dir,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool);


/** @} */

/**
 * @defgroup Relocate Switch a working copy to a different repository.
 *
 * @{
 */

/**
 * Recursively modify a working copy rooted at @a wcroot_dir, changing
 * any repository URLs that begin with @a from_prefix to begin with @a
 * to_prefix instead.
 *
 * @param wcroot_dir Working copy root directory
 * @param from_prefix Original URL
 * @param to_prefix New URL
 * @param ignore_externals If not set, recurse into external working
 *        copies after relocating the primary working copy
 * @param ctx svn_client_ctx_t
 * @param pool The pool from which to perform memory allocations
 *
 * @since New in 1.7
 */
svn_error_t *
svn_client_relocate2(const char *wcroot_dir,
                     const char *from_prefix,
                     const char *to_prefix,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_relocate2(), but with @a ignore_externals
 * always TRUE.
 *
 * @note As of the 1.7 API, @a dir is required to be a working copy
 * root directory, and @a recurse is required to be TRUE.
 *
 * @deprecated Provided for limited backwards compatibility with the
 * 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_relocate(const char *dir,
                    const char *from_prefix,
                    const char *to_prefix,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/** @} */

/**
 * @defgroup Revert Remove local changes in a repository.
 *
 * @{
 */

/**
 * Restore the pristine version of working copy @a paths,
 * effectively undoing any local mods.  For each path in @a paths,
 * revert it if it is a file.  Else if it is a directory, revert
 * according to @a depth:
 *
 * @a paths is an array of (const char *) local WC paths.
 *
 * If @a depth is #svn_depth_empty, revert just the properties on
 * the directory; else if #svn_depth_files, revert the properties
 * and any files immediately under the directory; else if
 * #svn_depth_immediates, revert all of the preceding plus
 * properties on immediate subdirectories; else if #svn_depth_infinity,
 * revert path and everything under it fully recursively.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items reverted; that is,
 * don't revert any item unless it's a member of one of those
 * changelists.  If @a changelists is empty (or altogether @c NULL),
 * no changelist filtering occurs.
 *
 * If @a clear_changelists is TRUE, then changelist information for the
 * paths is cleared while reverting.
 *
 * If @a metadata_only is TRUE, the files and directories aren't changed
 * by the operation. If there are conflict marker files attached to the
 * targets these are removed.
 *
 * If @a ctx->notify_func2 is non-NULL, then for each item reverted,
 * call @a ctx->notify_func2 with @a ctx->notify_baton2 and the path of
 * the reverted item.
 *
 * If an item specified for reversion is not under version control,
 * then do not error, just invoke @a ctx->notify_func2 with @a
 * ctx->notify_baton2, using notification code #svn_wc_notify_skip.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_revert3(const apr_array_header_t *paths,
                   svn_depth_t depth,
                   const apr_array_header_t *changelists,
                   svn_boolean_t clear_changelists,
                   svn_boolean_t metadata_only,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/** Similar to svn_client_revert2, but with @a clear_changelists set to
 * FALSE and @a metadata_only set to FALSE.
 *
 * @since New in 1.5.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_revert2(const apr_array_header_t *paths,
                   svn_depth_t depth,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_revert2(), but with @a changelists passed as
 * @c NULL, and @a depth set according to @a recurse: if @a recurse is
 * TRUE, @a depth is #svn_depth_infinity, else if @a recurse is
 * FALSE, @a depth is #svn_depth_empty.
 *
 * @note Most APIs map @a recurse==FALSE to @a depth==svn_depth_files;
 * revert is deliberately different.
 *
 * @deprecated Provided for backwards compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_revert(const apr_array_header_t *paths,
                  svn_boolean_t recursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);


/** @} */

/**
 * @defgroup Conflicts Dealing with conflicted paths.
 *
 * @{
 */

/**
 * An opaque type which represents a conflicted node in the working copy.
 *
 * @since New in 1.10.
 */
typedef struct svn_client_conflict_t svn_client_conflict_t;

/**
 * An opaque type which represents a resolution option for a conflict.
 *
 * @since New in 1.10.
 */
typedef struct svn_client_conflict_option_t svn_client_conflict_option_t;

/**
 * A public enumeration of conflict option IDs.
 *
 * @since New in 1.10, unless noted otherwise.
 */
typedef enum svn_client_conflict_option_id_t {

  /* Options for text and property conflicts.
   * These values intentionally mirror svn_wc_conflict_choice_t. */
  svn_client_conflict_option_undefined = -1, /* for private use only */
  svn_client_conflict_option_postpone = 0,
  svn_client_conflict_option_base_text,
  svn_client_conflict_option_incoming_text, /* "theirs-full" */
  svn_client_conflict_option_working_text,  /* "mine-full" */
  svn_client_conflict_option_incoming_text_where_conflicted,
  svn_client_conflict_option_working_text_where_conflicted,
  svn_client_conflict_option_merged_text,
  svn_client_conflict_option_unspecified,
  /* Values derived from svn_wc_conflict_choice_t end here. */

  /* Tree conflict resolution options start here. */

  /* Accept current working copy state. */
  svn_client_conflict_option_accept_current_wc_state,

  /* Options for local move vs incoming edit on update. */
  svn_client_conflict_option_update_move_destination,

  /* Options for local delete/replace vs incoming edit on update. */
  svn_client_conflict_option_update_any_moved_away_children,

  /* Options for incoming add vs local add or obstruction. */
  svn_client_conflict_option_incoming_add_ignore,

  /* Options for incoming file add vs local file add or obstruction. */
  svn_client_conflict_option_incoming_added_file_text_merge,
  svn_client_conflict_option_incoming_added_file_replace_and_merge,

  /* Options for incoming dir add vs local dir add or obstruction. */
  svn_client_conflict_option_incoming_added_dir_merge,
  svn_client_conflict_option_incoming_added_dir_replace,
  svn_client_conflict_option_incoming_added_dir_replace_and_merge,

  /* Options for incoming delete vs any */
  svn_client_conflict_option_incoming_delete_ignore,
  svn_client_conflict_option_incoming_delete_accept,

  /* Options for incoming move vs local edit */
  svn_client_conflict_option_incoming_move_file_text_merge,
  svn_client_conflict_option_incoming_move_dir_merge,

  /* Options for local move vs incoming edit on merge. */
  svn_client_conflict_option_local_move_file_text_merge
} svn_client_conflict_option_id_t;

/**
 * Set a merged property value on @a option to @a merged_propval.
 * 
 * Setting the merged value is required before resolving the property
 * conflict using an option with ID svn_client_conflict_option_merged_text.
 *
 * The contents of @a merged_propval are not copied, so the storage it
 * points to needs to remain valid until svn_client_conflict_prop_resolve()
 * has been called with @a option.
 *
 * @since New in 1.10.
 */
void
svn_client_conflict_option_set_merged_propval(
  svn_client_conflict_option_t *option,
  const svn_string_t *merged_propval);

/**
 * Get a list of possible repository paths which can be applied to the
 * svn_client_conflict_option_incoming_move_file_text_merge or
 * svn_client_conflict_option_incoming_move_dir_merge resolution
 * @a option. (If a different option is passed in, this function will
 * raise an assertion failure.)
 *
 * In some situations, there can be multiple possible destinations for an
 * incoming move. One such situation is where a file was copied and moved
 * in the same revision: svn cp a b; svn mv a c; svn commit
 * When this move is merged elsewhere, both b and c will appear as valid move
 * destinations to the conflict resolver. To resolve such ambiguity, the client
 * may call this function to obtain a list of possible destinations the user
 * may choose from.
 *
 * The array is allocated in @a result_pool and will have "const char *"
 * elements pointing to strings also allocated in @a result_pool.
 * All paths are relpaths, and relative to the repository root.
 *
 * @see svn_client_conflict_option_set_moved_to_repos_relpath()
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_option_get_moved_to_repos_relpath_candidates(
  apr_array_header_t **possible_moved_to_repos_relpaths,
  svn_client_conflict_option_t *option,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Set the preferred moved target repository path for the
 * svn_client_conflict_option_incoming_move_file_text_merge or
 * svn_client_conflict_option_incoming_move_dir_merge resolution option.
 * 
 * @a preferred_move_target_idx must be a valid index into the list returned
 * by svn_client_conflict_option_get_moved_to_repos_relpath_candidates().
 * 
 * This function can be called multiple times.
 * It affects the output of svn_client_conflict_tree_get_description() and
 * svn_client_conflict_option_get_description(). Call these functions again
 * to get updated descriptions containing the newly selected move target.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_option_set_moved_to_repos_relpath(
  svn_client_conflict_option_t *option,
  int preferred_move_target_idx,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

/**
 * Get a list of possible moved-to abspaths in the working copy which can be
 * applied to the svn_client_conflict_option_incoming_move_file_text_merge
 * or svn_client_conflict_option_incoming_move_dir_merge resolution @a option.
 * (If a different option is passed in, this function will raise an assertion
 * failure.)
 *
 * All paths in the returned list correspond to the repository path which
 * is assumed to be the destination of the incoming move operation.
 * To support cases where this destination path is ambiguous, the client may
 * call svn_client_conflict_option_get_moved_to_repos_relpath_candidates()
 * before calling this function to let the user select a repository path first.
 * 
 * If no possible moved-to paths can be found, return an empty array.
 * This doesn't mean that no move happened in the repository. It is possible
 * that the move destination is outside the scope of the current working copy,
 * for example, in which case the conflict must be resolved in some other way.
 *
 * @see svn_client_conflict_option_set_moved_to_abspath()
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_option_get_moved_to_abspath_candidates(
  apr_array_header_t **possible_moved_to_abspaths,
  svn_client_conflict_option_t *option,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Set the preferred moved target abspath for the
 * svn_client_conflict_option_incoming_move_file_text_merge or
 * svn_client_conflict_option_incoming_move_dir_merge resolution option.
 * 
 * @a preferred_move_target_idx must be a valid index into the list
 * returned by svn_client_conflict_option_get_moved_to_abspath_candidates().
 * 
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_option_set_moved_to_abspath(
  svn_client_conflict_option_t *option,
  int preferred_move_target_idx,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

/**
 * Given an @a option_id, try to find the corresponding option in @a options,
 * which is an array of svn_client_conflict_option_t * elements.
 *
 * Return NULL if no corresponding option can be be found.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_t *
svn_client_conflict_option_find_by_id(
  apr_array_header_t *options,
  svn_client_conflict_option_id_t option_id);

/**
 * Return a conflict for the conflicted path @a local_abspath.
 * 
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_get(svn_client_conflict_t **conflict,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/**
 * Callback for svn_client_conflict_conflict_walk();
 *
 * The lifetime of @a conflict is limited. Its allocation in
 * memory will not persist beyond this callback's execution.
 * 
 * @since New in 1.10.
 */
typedef svn_error_t *(*svn_client_conflict_walk_func_t)(
  void *baton,
  svn_client_conflict_t *conflict,
  apr_pool_t *scratch_pool);

/**
 * Walk all conflicts within the specified @a depth of @a local_abspath.
 * Pass each conflict found during the walk to the @conflict_walk_func
 * callback, along with @a conflict_walk_func_baton.
 * Use cancellation and notification support provided by client context @a ctx.
 * 
 * This callback may choose to resolve the conflict. If the act of resolving
 * a conflict creates new conflicts within the walked working copy (as might
 * be the case for some tree conflicts), the callback will be invoked for each
 * such new conflict as well.
 * 
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_walk(const char *local_abspath,
                         svn_depth_t depth,
                         svn_client_conflict_walk_func_t conflict_walk_func,
                         void *conflict_walk_func_baton,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool);

/**
* Indicate the types of conflicts present on the working copy node
* described by @a conflict. Any output argument may be @c NULL if
* the caller is not interested in the status of a particular type.
*
* The returned @a *props_conflicted array is allocated in @a result_pool.
* It contains the names of conflicted properties. If no property conflict
* exists, the array will contain no elements.
*
* @since New in 1.10. 
*/
svn_error_t *
svn_client_conflict_get_conflicted(svn_boolean_t *text_conflicted,
                                   apr_array_header_t **props_conflicted,
                                   svn_boolean_t *tree_conflicted,
                                   svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);

/**
 * Return a textual human-readable description of the property conflict
 * described by @a conflict, allocated in @a result_pool. The description
 * is encoded in UTF-8 and may contain multiple lines separated by
 * @c APR_EOL_STR. The last line is not terminated by a newline.
 *
 * Additionally, the description may be localized to the language used
 * by the current locale.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_prop_get_description(const char **description,
                                         svn_client_conflict_t *conflict,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool);

/**
 * Return a textual human-readable description of the tree conflict
 * described by @a conflict, allocated in @a result_pool. The description
 * is encoded in UTF-8 and may contain multiple lines separated by
 * @c APR_EOL_STR. The last line is not terminated by a newline.
 *
 * Additionally, the description may be localized to the language used
 * by the current locale.
 *
 * While client implementors are free to enhance descriptions by adding
 * additional information to the text, or break up very long lines for
 * formatting purposes, there is no syntax defined for descriptions, and
 * implementors should not rely on any particular parts of descriptions
 * to remain stable over time, apart from what is stated below.
 * Descriptions may or may not form complete sentences. They may contain
 * paths relative to the repository root; such paths always start with "^/",
 * and end with either a peg revision (e.g. "@100") or a colon followed by
 * a range of revisions (as in svn:mergeinfo, e.g. ":100-200").
 * Paths may appear on a line of their own to avoid overlong lines.
 * Any revision numbers mentioned elsewhere in the description are
 * prefixed with the letter 'r' (e.g. "r99").
 *
 * The description has two parts: One part describes the "incoming change"
 * applied by an update, merge, or switch operation. The other part
 * describes the "local change" which occurred in the working copy or
 * perhaps in the history of a merge target branch.
 * Both parts are provided independently to allow for some flexibility
 * when displaying the description. As a convention, displaying the
 * "incoming change" first and the "local change" second is recommended.
 *
 * By default, the description is based only on information available in
 * the working copy. If svn_client_conflict_tree_get_details() was already
 * called for @a conflict, the description might also contain useful
 * information obtained from the repository and provide for a much better
 * user experience.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_tree_get_description(
  const char **incoming_change_description,
  const char **local_change_description,
  svn_client_conflict_t *conflict,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Set @a *options to an array of pointers to svn_client_conflict_option_t
 * objects applicable to text conflicts described by @a conflict.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_text_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool);

/**
 * Set @a *options to an array of pointers to svn_client_conflict_option_t
 * objects applicable to property conflicts described by @a conflict.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_prop_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool);

/**
 * Set @a *options to an array of pointers to svn_client_conflict_option_t
 * objects applicable to the tree conflict described by @a conflict.
 *
 * By default, the list of options is based only on information available in
 * the working copy. If svn_client_conflict_tree_get_details() was already
 * called for @a conflict, a more useful list of options might be returned.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_tree_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                svn_client_ctx_t *ctx,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool);

/**
 * Find more information about the tree conflict represented by @a conflict.
 *
 * A call to svn_client_conflict_tree_get_description() may yield much better
 * results after this function has been called.
 *
 * A call to svn_client_conflict_tree_get_resolution_options() may provide
 * more useful resolution options if this function has been called.
 *
 * This function may contact the repository. Use the authentication baton
 * cached in @a ctx for authentication if contacting the repository.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_tree_get_details(svn_client_conflict_t *conflict,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *scratch_pool);

/**
 * Return an ID for @a option. This ID can be used by callers to associate
 * arbitrary data with a particular conflict resolution option.
 *
 * The ID of a particular resolution option will never change in future
 * revisions of this API.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_id_t
svn_client_conflict_option_get_id(svn_client_conflict_option_t *option);

/**
 * Return a textual human-readable label of @a option, allocated in
 * @a result_pool. The label is encoded in UTF-8 and usually
 * contains up to three words.
 *
 * Additionally, the label may be localized to the language used
 * by the current locale.
 *
 * @since New in 1.10.
 */
const char *
svn_client_conflict_option_get_label(svn_client_conflict_option_t *option,
                                     apr_pool_t *result_pool);

/**
 * Return a textual human-readable description of @a option, allocated in
 * @a result_pool. The description is encoded in UTF-8 and may contain
 * multiple lines separated by @c APR_EOL_STR.
 *
 * Additionally, the description may be localized to the language used
 * by the current locale.
 *
 * @since New in 1.10.
 */
const char *
svn_client_conflict_option_get_description(svn_client_conflict_option_t *option,
                                           apr_pool_t *result_pool);

/**
 * Return the ID of the recommended resolution option. If no specific option
 * is recommended, return @c svn_client_conflict_option_unspecified;
 *
 * Client implementations which aim to avoid excessive interactive prompting
 * may wish to try a recommended resolution option before falling back to
 * asking the user which option to use.
 * 
 * Conflict resolution with a recommended option is not guaranteed to succeed.
 * Clients should check for errors when trying to resolve a conflict and fall
 * back to other options and/or interactive prompting when the recommended
 * option fails to resolve a conflict.
 *
 * If @a conflict is a tree conflict, svn_client_conflict_tree_get_details()
 * should be called before this function to allow for useful recommendations.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_id_t
svn_client_conflict_get_recommended_option_id(svn_client_conflict_t *conflict);

/**
 * Return the absolute path to the conflicted working copy node described
 * by @a conflict.
 *
 * @since New in 1.10. 
 */
const char *
svn_client_conflict_get_local_abspath(svn_client_conflict_t *conflict);

/**
 * Return the operation during which the conflict described by @a
 * conflict was recorded.
 *
 * @since New in 1.10. 
 */
svn_wc_operation_t
svn_client_conflict_get_operation(svn_client_conflict_t *conflict);

/**
 * Return the action an update, switch, or merge operation attempted to
 * perform on the working copy node described by @a conflict.
 * 
 * @since New in 1.10. 
 */
svn_wc_conflict_action_t
svn_client_conflict_get_incoming_change(svn_client_conflict_t *conflict);

/**
 * Return the reason why the attempted action performed by an update, switch,
 * or merge operation conflicted with the state of the node in the working copy.
 *
 * During update and switch operations this local change is part of uncommitted
 * modifications in the working copy. During merge operations it may
 * additionally be part of the history of the merge target branch, anywhere
 * between the common ancestor revision and the working copy revision.
 * 
 * @since New in 1.10. 
 */
svn_wc_conflict_reason_t
svn_client_conflict_get_local_change(svn_client_conflict_t *conflict);

/**
 * Return information about the repository associated with @a conflict. 
 * In case of a foreign-repository merge this will differ from the
 * repository information associated with the merge target working copy.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_get_repos_info(const char **repos_root_url,
                                   const char **repos_uuid,
                                   svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);

/**
 * Return the repository-relative location and the node kind of the incoming
 * old version of the conflicted node described by @a conflict.
 *
 * If the repository-relative path is not available, the @a
 * *incoming_old_repos_relpath will be set to @c NULL, 
 *
 * If the peg revision is not available, @a *incoming_old_regrev will be
 * set to SVN_INVALID_REVNUM.
 * 
 * If the node kind is not available or if the node does not exist at the
 * specified path and revision, @a *incoming_old_node_kind will be set to
 * svn_node_none.
 * ### Should return svn_node_unkown if not available?
 *
 * Any output parameter may be set to @c NULL by the caller to indicate that
 * a particular piece of information should not be returned.
 *
 * In case of tree conflicts, this path@revision does not necessarily exist
 * in the repository, and it does not necessarily represent the incoming
 * change which is responsible for the occurance of the tree conflict.
 * The responsible incoming change is generally located somewhere between
 * the old and new incoming versions.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_get_incoming_old_repos_location(
  const char **incoming_old_repos_relpath,
  svn_revnum_t *incoming_old_regrev,
  svn_node_kind_t *incoming_old_node_kind,
  svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Like svn_client_conflict_get_incoming_old_repos_location(), expect this
 * function returns the same data for the incoming new version.
 *
 * The same note about tree conflicts applies.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_get_incoming_new_repos_location(
  const char **incoming_new_repos_relpath,
  svn_revnum_t *incoming_new_regrev,
  svn_node_kind_t *incoming_new_node_kind,
  svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/**
 * Return the node kind of the tree conflict victim described by @a conflict.
 * The victim is the local node in the working copy which was affected by the
 * tree conflict at the time the conflict was raised.
 *
 * @since New in 1.10.
 */
svn_node_kind_t
svn_client_conflict_tree_get_victim_node_kind(svn_client_conflict_t *conflict);

/**
 * Resolve a tree @a conflict using resolution option @a option.
 *
 * May raise an error in case the tree conflict cannot be resolved yet, for
 * instance @c SVN_ERR_WC_OBSTRUCTED_UPDATE, @c SVN_ERR_WC_FOUND_CONFLICT,
 * or @c SVN_ERR_WC_CONFLICT_RESOLVER_FAILUE.
 * This may happen when other tree conflicts, or unversioned obstructions,
 * block the resolution of this tree conflict. In such a case the other
 * conflicts should be resolved first and resolution of this conflict should
 * be attempted again later.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_tree_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool);

/**
 * Like svn_client_conflict_tree_resolve(), except that it identifies
 * the desired resolution option by ID @a option_id.
 *
 * If the provided @a option_id is the ID of an option which resolves
 * @a conflict, try to resolve the tree conflict using that option.
 * Else, return @c SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_tree_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

/**
 * Return the ID of the option this tree @a conflict has been resolved to.
 * If the conflict has not been resolved yet, then return
 * @c svn_client_conflict_option_unspecified.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_id_t
svn_client_conflict_tree_get_resolution(svn_client_conflict_t *conflict);

/**
 * Return the path to the legacy property conflicts reject file
 * for the property conflicts represented by @a conflict.
 *
 * This function exists for backwards compatibility only and should not be
 * used in new code.
 *
 * @since New in 1.10.
 */
const char *
svn_client_conflict_prop_get_reject_abspath(svn_client_conflict_t *conflict);

/**
 * Return the set of property values involved in the conflict of property
 * PROPNAME described by @a conflict. If a property value is unavailable the
 * corresponding output argument is set to @c NULL.
 *  
 * A 3-way diff of these property values can be generated with
 * svn_diff_mem_string_diff3(). A merged version with conflict
 * markers can be generated with svn_diff_mem_string_output_merge3().
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_prop_get_propvals(const svn_string_t **base_propval,
                                      const svn_string_t **working_propval,
                                      const svn_string_t **incoming_old_propval,
                                      const svn_string_t **incoming_new_propval,
                                      svn_client_conflict_t *conflict,
                                      const char *propname,
                                      apr_pool_t *result_pool);

/**
 * Resolve a property @a conflict in property @a propname using resolution
 * option @a option. To resolve all properties to the same option at once,
 * set @a propname to the empty string "".
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_prop_resolve(svn_client_conflict_t *conflict,
                                 const char *propname,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool);
/**
 * If the provided @a option_id is the ID of an option which resolves
 * @a conflict, resolve the property conflict in property @a propname
 * using that option.
 * Else, return @c SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_prop_resolve_by_id(
  svn_client_conflict_t *conflict,
  const char *propname,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

/**
 * Return the ID of the option this property @a conflict in property
 * @a propname has been resolved to.
 * If the conflict has not been resolved yet, then return
 * @c svn_client_conflict_option_unspecified.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_id_t
svn_client_conflict_prop_get_resolution(svn_client_conflict_t *conflict,
                                        const char *propname);

/**
 * Return the MIME-type of the working version of the text-conflicted file
 * described by @a conflict.
 *
 * ### Really needed? What about base/incoming_old/incoming_new values?
 * @since: New in 1.10.
 */
const char *
svn_client_conflict_text_get_mime_type(svn_client_conflict_t *conflict);

/**
 * Return absolute paths to the versions of the text-conflicted file 
 * described by @a conflict.
 *
 * If a particular content is not available, it is set to @c NULL.
 * 
 * ### Should this be returning svn_stream_t instead of paths?
 * @since: New in 1.10.
 */
svn_error_t *
svn_client_conflict_text_get_contents(const char **base_abspath,
                                      const char **working_abspath,
                                      const char **incoming_old_abspath,
                                      const char **incoming_new_abspath,
                                      svn_client_conflict_t *conflict,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/**
 * Resolve a text @a conflict using resolution option @a option.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_text_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *scratch_pool);

/**
 * If the provided @a option_id is the ID of an option which resolves
 * @a conflict, resolve the text conflict using that option.
 * Else, return @c SVN_ERR_CLIENT_CONFLICT_OPTION_NOT_APPLICABLE.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_conflict_text_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  svn_client_ctx_t *ctx,
  apr_pool_t *scratch_pool);

/**
 * Return the ID of the option this text @a conflict has been resolved to.
 * If the conflict has not been resolved yet, then return
 * @c svn_client_conflict_option_unspecified.
 *
 * @since New in 1.10.
 */
svn_client_conflict_option_id_t
svn_client_conflict_text_get_resolution(svn_client_conflict_t *conflict);

/** @} */

/**
 * @defgroup Resolved Mark conflicted paths as resolved.
 *
 * @{
 */

/**
 * Similar to svn_client_resolve(), but without automatic conflict
 * resolution support.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 * Use svn_client_resolve() with @a conflict_choice == @c
 * svn_wc_conflict_choose_merged instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_resolved(const char *path,
                    svn_boolean_t recursive,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/** Perform automatic conflict resolution on a working copy @a path.
 *
 * If @a conflict_choice is
 *
 *   - #svn_wc_conflict_choose_base:
 *     resolve the conflict with the old file contents
 *
 *   - #svn_wc_conflict_choose_mine_full:
 *     use the original working contents
 *
 *   - #svn_wc_conflict_choose_theirs_full:
 *     use the new contents
 *
 *   - #svn_wc_conflict_choose_merged:
 *     don't change the contents at all, just remove the conflict
 *     status, which is the pre-1.5 behavior.
 *
 *   - #svn_wc_conflict_choose_theirs_conflict
 *     ###...
 *
 *   - #svn_wc_conflict_choose_mine_conflict
 *     ###...
 *
 *   - svn_wc_conflict_choose_unspecified
 *     invoke @a ctx->conflict_func2 with @a ctx->conflict_baton2 to obtain
 *     a resolution decision for each conflict.  This can be used to
 *     implement interactive conflict resolution but is NOT RECOMMENDED for
 *     new code. To perform conflict resolution based on interactive user
 *     input on a per-conflict basis, use svn_client_conflict_text_resolve(),
 *     svn_client_conflict_prop_resolve(), and
 *     svn_client_conflict_tree_resolve() instead of svn_client_resolve().
 *
 * #svn_wc_conflict_choose_theirs_conflict and
 * #svn_wc_conflict_choose_mine_conflict are not legal for binary
 * files or properties.
 *
 * If @a path is not in a state of conflict to begin with, do nothing.
 * If @a path's conflict state is removed and @a ctx->notify_func2 is non-NULL,
 * call @a ctx->notify_func2 with @a ctx->notify_baton2 and @a path.
 * ### with what notification parameters?
 *
 * If @a depth is #svn_depth_empty, act only on @a path; if
 * #svn_depth_files, resolve @a path and its conflicted file
 * children (if any); if #svn_depth_immediates, resolve @a path and
 * all its immediate conflicted children (both files and directories,
 * if any); if #svn_depth_infinity, resolve @a path and every
 * conflicted file or directory anywhere beneath it.
 *
 * Note that this operation will try to lock the parent directory of
 * @a path in order to be able to resolve tree-conflicts on @a path.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_resolve(const char *path,
                   svn_depth_t depth,
                   svn_wc_conflict_choice_t conflict_choice,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/** @} */

/**
 * @defgroup Copy Copy paths in the working copy and repository.
 *
 * @{
 */

/**
 * A structure which describes the source of a copy operation--its path,
 * revision, and peg revision.
 *
 * @since New in 1.5.
 */
typedef struct svn_client_copy_source_t
{
    /** The source path or URL. */
    const char *path;

    /** The source operational revision. */
    const svn_opt_revision_t *revision;

    /** The source peg revision. */
    const svn_opt_revision_t *peg_revision;
} svn_client_copy_source_t;

/** Copy each source in @a sources to @a dst_path.
 *
 * If multiple @a sources are given, @a dst_path must be a directory,
 * and @a sources will be copied as children of @a dst_path.
 *
 * @a sources is an array of <tt>svn_client_copy_source_t *</tt> elements,
 * either all referring to local WC items or all referring to versioned
 * items in the repository.
 *
 * If @a sources has only one item, attempt to copy it to @a dst_path.  If
 * @a copy_as_child is TRUE and @a dst_path already exists, attempt to copy the
 * item as a child of @a dst_path.  If @a copy_as_child is FALSE and
 * @a dst_path already exists, fail with #SVN_ERR_ENTRY_EXISTS if @a dst_path
 * is a working copy path and #SVN_ERR_FS_ALREADY_EXISTS if @a dst_path is a
 * URL.
 *
 * If @a sources has multiple items, and @a copy_as_child is TRUE, all
 * @a sources are copied as children of @a dst_path.  If any child of
 * @a dst_path already exists with the same name any item in @a sources,
 * fail with #SVN_ERR_ENTRY_EXISTS if @a dst_path is a working copy path and
 * #SVN_ERR_FS_ALREADY_EXISTS if @a dst_path is a URL.
 *
 * If @a sources has multiple items, and @a copy_as_child is FALSE, fail
 * with #SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED.
 *
 * If @a dst_path is a URL, use the authentication baton
 * in @a ctx and @a ctx->log_msg_func3/@a ctx->log_msg_baton3 to immediately
 * attempt to commit the copy action in the repository.
 *
 * If @a dst_path is not a URL, then this is just a variant of
 * svn_client_add(), where the @a sources are scheduled for addition
 * as copies.  No changes will happen to the repository until a commit occurs.
 * This scheduling can be removed with svn_client_revert2().
 *
 * If @a make_parents is TRUE, create any non-existent parent directories
 * also.  Otherwise the parent of @a dst_path must already exist.
 *
 * If @a ignore_externals is set, don't process externals definitions
 * as part of this operation.
 *
 * If @a metadata_only is @c TRUE and copying a file in a working copy,
 * everything in the metadata is updated as if the node is moved, but the
 * actual disk copy operation is not performed. This feature is useful for
 * clients that want to keep the working copy in sync while the actual working
 * copy is updated by some other task.
 *
 * If @a pin_externals is set, pin URLs in copied externals definitions
 * to their current revision unless they were already pinned to a
 * particular revision. A pinned external uses a URL which points at a
 * fixed revision, rather than the HEAD revision. Externals in the copy
 * destination are pinned to either a working copy base revision or the
 * HEAD revision of a repository (as of the time the copy operation is
 * performed), depending on the type of the copy source:
 <pre>
    copy source: working copy (WC)       REPOS
   ------------+------------------------+---------------------------+
    copy    WC | external's WC BASE rev | external's repos HEAD rev |
    dest:      |------------------------+---------------------------+
         REPOS | external's WC BASE rev | external's repos HEAD rev |
   ------------+------------------------+---------------------------+
 </pre>
 * If the copy source is a working copy, then all externals must be checked
 * out, be at a single-revision, contain no local modifications, and contain
 * no switched subtrees. Else, #SVN_ERR_WC_PATH_UNEXPECTED_STATUS is returned.
 *
 * If non-NULL, @a externals_to_pin restricts pinning to a subset of externals.
 * It is a hash table keyed by either a local absolute path or a URL at which
 * an svn:externals property is set. The hash table contains apr_array_header_t*
 * elements as returned by svn_wc_parse_externals_description3(). These arrays
 * contain elements of type svn_wc_external_item2_t*, each of which corresponds
 * to a single line of an svn:externals definition. Externals corresponding to
 * these items will be pinned, other externals will not be pinned.
 * If @a externals_to_pin is @c NULL then all externals are pinned.
 * If @a pin_externals is @c FALSE then @a externals_to_pin is ignored.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision in
 * the event that this is a committing operation.  This table cannot
 * contain any standard Subversion properties.
 *
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 are a callback/baton combo
 * that this function can use to query for a commit log message when one is
 * needed.
 *
 * If @a ctx->notify_func2 is non-NULL, invoke it with @a ctx->notify_baton2
 * for each item added at the new location, passing the new, relative path of
 * the added item.
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_copy7(const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 svn_boolean_t metadata_only,
                 svn_boolean_t pin_externals,
                 const apr_hash_t *externals_to_pin,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_copy7(), but doesn't support meta_data_only
 * and cannot pin externals.
 * 
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy6(const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_copy6(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.
 *
 * @since New in 1.6.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy5(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_copy5(), with @a ignore_externals set to @c FALSE.
 *
 * @since New in 1.5.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy4(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_copy4(), with only one @a src_path, @a
 * copy_as_child set to @c FALSE, @a revprop_table passed as NULL, and
 * @a make_parents set to @c FALSE.  Also, use @a src_revision as both
 * the operational and peg revision.
 *
 * @since New in 1.4.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);


/**
 * Similar to svn_client_copy3(), with the difference that if @a dst_path
 * already exists and is a directory, copy the item into that directory,
 * keeping its name (the last component of @a src_path).
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy2(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);


/**
 * Similar to svn_client_copy2(), but uses #svn_client_commit_info_t
 * for @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_copy(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);


/** @} */

/**
 * @defgroup Move Move paths in the working copy or repository.
 *
 * @{
 */

/**
 * Move @a src_paths to @a dst_path.
 *
 * @a src_paths is an array of (const char *) paths -- either all WC paths
 * or all URLs -- of versioned items.  If multiple @a src_paths are given,
 * @a dst_path must be a directory and @a src_paths will be moved as
 * children of @a dst_path.
 *
 * If @a src_paths are repository URLs:
 *
 *   - @a dst_path must also be a repository URL.
 *
 *   - The authentication baton in @a ctx and @a ctx->log_msg_func/@a
 *     ctx->log_msg_baton are used to commit the move.
 *
 *   - The move operation will be immediately committed.
 *
 * If @a src_paths are working copy paths:
 *
 *   - @a dst_path must also be a working copy path.
 *
 *   - @a ctx->log_msg_func3 and @a ctx->log_msg_baton3 are ignored.
 *
 *   - This is a scheduling operation.  No changes will happen to the
 *     repository until a commit occurs.  This scheduling can be removed
 *     with svn_client_revert2().  If one of @a src_paths is a file it is
 *     removed from the working copy immediately.  If one of @a src_path
 *     is a directory it will remain in the working copy but all the files,
 *     and unversioned items, it contains will be removed.
 *
 * If @a src_paths has only one item, attempt to move it to @a dst_path.  If
 * @a move_as_child is TRUE and @a dst_path already exists, attempt to move the
 * item as a child of @a dst_path.  If @a move_as_child is FALSE and
 * @a dst_path already exists, fail with #SVN_ERR_ENTRY_EXISTS if @a dst_path
 * is a working copy path and #SVN_ERR_FS_ALREADY_EXISTS if @a dst_path is a
 * URL.
 *
 * If @a src_paths has multiple items, and @a move_as_child is TRUE, all
 * @a src_paths are moved as children of @a dst_path.  If any child of
 * @a dst_path already exists with the same name any item in @a src_paths,
 * fail with #SVN_ERR_ENTRY_EXISTS if @a dst_path is a working copy path and
 * #SVN_ERR_FS_ALREADY_EXISTS if @a dst_path is a URL.
 *
 * If @a src_paths has multiple items, and @a move_as_child is FALSE, fail
 * with #SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED.
 *
 * If @a make_parents is TRUE, create any non-existent parent directories
 * also.  Otherwise, the parent of @a dst_path must already exist.
 *
 * If @a allow_mixed_revisions is @c FALSE, #SVN_ERR_WC_MIXED_REVISIONS
 * will be raised if the move source is a mixed-revision subtree.
 * If @a allow_mixed_revisions is TRUE, a mixed-revision move source is
 * allowed but the move will degrade to a copy and a delete without local
 * move tracking. This parameter should be set to FALSE except where backwards
 * compatibility to svn_client_move6() is required.
 *
 * If @a metadata_only is @c TRUE and moving a file in a working copy,
 * everything in the metadata is updated as if the node is moved, but the
 * actual disk move operation is not performed. This feature is useful for
 * clients that want to keep the working copy in sync while the actual working
 * copy is updated by some other task.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision in
 * the event that this is a committing operation.  This table cannot
 * contain any standard Subversion properties.
 *
 * @a ctx->log_msg_func3/@a ctx->log_msg_baton3 are a callback/baton combo that
 * this function can use to query for a commit log message when one is needed.
 *
 * If @a ctx->notify_func2 is non-NULL, then for each item moved, call
 * @a ctx->notify_func2 with the @a ctx->notify_baton2 twice, once to indicate
 * the deletion of the moved thing, and once to indicate the addition of
 * the new location of the thing.
 *
 * ### Is this really true?  What about svn_wc_notify_commit_replaced()? ###
 *
 * If @a commit_callback is non-NULL, then for each successful commit, call
 * @a commit_callback with @a commit_baton and a #svn_commit_info_t for
 * the commit.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_move7(const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t allow_mixed_revisions,
                 svn_boolean_t metadata_only,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move7(), but with @a allow_mixed_revisions always
 * set to @c TRUE and @a metadata_only always to @c FALSE.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move6(const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move6(), but returns the commit info in
 * @a *commit_info_p rather than through a callback function.
 *
 * A WC-to-WC move will include any modified and/or unversioned children.
 * @a force is ignored.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move5(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move5(), with only one @a src_path, @a
 * move_as_child set to @c FALSE, @a revprop_table passed as NULL, and
 * @a make_parents set to @c FALSE.
 *
 * Note: The behaviour of @a force changed in 1.5 (r860885 and r861421), when
 * the 'move' semantics were improved to just move the source including any
 * modified and/or unversioned items in it.  Before that, @a force
 * controlled what happened to such items, but now @a force is ignored.
 *
 * @since New in 1.4.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move4(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move4(), with the difference that if @a dst_path
 * already exists and is a directory, move the item into that directory,
 * keeping its name (the last component of @a src_path).
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move3(), but uses #svn_client_commit_info_t
 * for @a commit_info_p.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 *
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move2(svn_client_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_move2(), but an extra argument @a src_revision
 * must be passed.  This has no effect, but must be of kind
 * #svn_opt_revision_unspecified or #svn_opt_revision_head,
 * otherwise error #SVN_ERR_UNSUPPORTED_FEATURE is returned.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_move(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/** @} */


/** Properties
 *
 * Note that certain svn-controlled properties must always have their
 * values set and stored in UTF8 with LF line endings.  When
 * retrieving these properties, callers must convert the values back
 * to native locale and native line-endings before displaying them to
 * the user.  For help with this task, see
 * svn_prop_needs_translation(), svn_subst_translate_string(),  and
 * svn_subst_detranslate_string().
 *
 * @defgroup svn_client_prop_funcs Property functions
 * @{
 */


/**
 * Set @a propname to @a propval on @a url.  A @a propval of @c NULL will
 * delete the property.
 *
 * Immediately attempt to commit the property change in the repository,
 * using the authentication baton in @a ctx and @a
 * ctx->log_msg_func3/@a ctx->log_msg_baton3.
 *
 * If the property has changed on @a url since revision
 * @a base_revision_for_url (which must not be #SVN_INVALID_REVNUM), no
 * change will be made and an error will be returned.
 *
 * If non-NULL, @a revprop_table is a hash table holding additional,
 * custom revision properties (<tt>const char *</tt> names mapped to
 * <tt>svn_string_t *</tt> values) to be set on the new revision.  This
 * table cannot contain any standard Subversion properties.
 *
 * If @a commit_callback is non-NULL, then call @a commit_callback with
 * @a commit_baton and a #svn_commit_info_t for the commit.
 *
 * If @a propname is an svn-controlled property (i.e. prefixed with
 * #SVN_PROP_PREFIX), then the caller is responsible for ensuring that
 * the value is UTF8-encoded and uses LF line-endings.
 *
 * If @a skip_checks is TRUE, do no validity checking.  But if @a
 * skip_checks is FALSE, and @a propname is not a valid property for @a
 * url, return an error, either #SVN_ERR_ILLEGAL_TARGET (if the property is
 * not appropriate for @a url), or * #SVN_ERR_BAD_MIME_TYPE (if @a propname
 * is "svn:mime-type", but @a propval is not a valid mime-type).
 *
 * Use @a scratch_pool for all memory allocation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_propset_remote(const char *propname,
                          const svn_string_t *propval,
                          const char *url,
                          svn_boolean_t skip_checks,
                          svn_revnum_t base_revision_for_url,
                          const apr_hash_t *revprop_table,
                          svn_commit_callback2_t commit_callback,
                          void *commit_baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool);

/**
 * Set @a propname to @a propval on each (const char *) target in @a
 * targets.  The targets must be all working copy paths.  A @a propval
 * of @c NULL will delete the property.
 *
 * If @a depth is #svn_depth_empty, set the property on each member of
 * @a targets only; if #svn_depth_files, set it on @a targets and their
 * file children (if any); if #svn_depth_immediates, on @a targets and all
 * of their immediate children (both files and directories); if
 * #svn_depth_infinity, on @a targets and everything beneath them.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose properties are
 * set; that is, don't set properties on any item unless it's a member
 * of one of those changelists.  If @a changelists is empty (or
 * altogether @c NULL), no changelist filtering occurs.
 *
 * If @a propname is an svn-controlled property (i.e. prefixed with
 * #SVN_PROP_PREFIX), then the caller is responsible for ensuring that
 * the value is UTF8-encoded and uses LF line-endings.
 *
 * If @a skip_checks is TRUE, do no validity checking.  But if @a
 * skip_checks is FALSE, and @a propname is not a valid property for @a
 * targets, return an error, either #SVN_ERR_ILLEGAL_TARGET (if the
 * property is not appropriate for @a targets), or
 * #SVN_ERR_BAD_MIME_TYPE (if @a propname is "svn:mime-type", but @a
 * propval is not a valid mime-type).
 *
 * If @a ctx->cancel_func is non-NULL, invoke it passing @a
 * ctx->cancel_baton at various places during the operation.
 *
 * Use @a scratch_pool for all memory allocation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_propset_local(const char *propname,
                         const svn_string_t *propval,
                         const apr_array_header_t *targets,
                         svn_depth_t depth,
                         svn_boolean_t skip_checks,
                         const apr_array_header_t *changelists,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool);

/**
 * An amalgamation of svn_client_propset_local() and
 * svn_client_propset_remote() that takes only a single target, and
 * returns the commit info in @a *commit_info_p rather than through a
 * callback function.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propset3(svn_commit_info_t **commit_info_p,
                    const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_depth_t depth,
                    svn_boolean_t skip_checks,
                    svn_revnum_t base_revision_for_url,
                    const apr_array_header_t *changelists,
                    const apr_hash_t *revprop_table,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/**
 * Like svn_client_propset3(), but with @a base_revision_for_url
 * always #SVN_INVALID_REVNUM; @a commit_info_p always @c NULL; @a
 * changelists always @c NULL; @a revprop_table always @c NULL; and @a
 * depth set according to @a recurse: if @a recurse is TRUE, @a depth
 * is #svn_depth_infinity, else #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propset2(const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    svn_boolean_t skip_checks,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/**
 * Like svn_client_propset2(), but with @a skip_checks always FALSE and a
 * newly created @a ctx.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propset(const char *propname,
                   const svn_string_t *propval,
                   const char *target,
                   svn_boolean_t recurse,
                   apr_pool_t *pool);

/** Set @a propname to @a propval on revision @a revision in the repository
 * represented by @a URL.  Use the authentication baton in @a ctx for
 * authentication, and @a pool for all memory allocation.  Return the actual
 * rev affected in @a *set_rev.  A @a propval of @c NULL will delete the
 * property.
 *
 * If @a original_propval is non-NULL, then just before setting the
 * new value, check that the old value matches @a original_propval;
 * if they do not match, return the error #SVN_ERR_RA_OUT_OF_DATE.
 * This is to help clients support interactive editing of revprops:
 * without this check, the window during which the property may change
 * underneath the user is as wide as the amount of time the user
 * spends editing the property.  With this check, the window is
 * reduced to a small, constant amount of time right before we set the
 * new value.  (To check that an old value is still non-existent, set
 * @a original_propval->data to NULL, and @a original_propval->len is
 * ignored.)
 * If the server advertises #SVN_RA_CAPABILITY_ATOMIC_REVPROPS, the
 * check of @a original_propval is done atomically.
 *
 * Note: the representation of "property is not set" in @a
 * original_propval differs from the representation in other APIs
 * (such as svn_fs_change_rev_prop2() and svn_ra_change_rev_prop2()).
 *
 * If @a force is TRUE, allow newlines in the author property.
 *
 * If @a propname is an svn-controlled property (i.e. prefixed with
 * #SVN_PROP_PREFIX), then the caller is responsible for ensuring that
 * the value UTF8-encoded and uses LF line-endings.
 *
 * Note that unlike its cousin svn_client_propset3(), this routine
 * doesn't affect the working copy at all;  it's a pure network
 * operation that changes an *unversioned* property attached to a
 * revision.  This can be used to tweak log messages, dates, authors,
 * and the like.  Be careful:  it's a lossy operation.

 * @a ctx->notify_func2 and @a ctx->notify_baton2 are the notification
 * functions and baton which are called upon successful setting of the
 * property.
 *
 * Also note that unless the administrator creates a
 * pre-revprop-change hook in the repository, this feature will fail.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_client_revprop_set2(const char *propname,
                        const svn_string_t *propval,
                        const svn_string_t *original_propval,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_boolean_t force,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool);

/**
 * Similar to svn_client_revprop_set2(), but with @a original_propval
 * always @c NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_revprop_set(const char *propname,
                       const svn_string_t *propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_boolean_t force,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool);

/**
 * Set @a *props to a hash table whose keys are absolute paths or URLs
 * of items on which property @a propname is explicitly set, and whose
 * values are <tt>svn_string_t *</tt> representing the property value for
 * @a propname at that path.
 *
 * If @a inherited_props is not @c NULL, then set @a *inherited_props to a
 * depth-first ordered array of #svn_prop_inherited_item_t * structures
 * representing the properties inherited by @a target.  If @a target is a
 * working copy path, then properties inherited by @a target as far as the
 * root of the working copy are obtained from the working copy's actual
 * property values.  Properties inherited from above the working copy root
 * come from the inherited properties cache.  If @a target is a URL, then
 * the inherited properties come from the repository.  If @a inherited_props
 * is not @c NULL and no inheritable properties are found, then set
 * @a *inherited_props to an empty array.
 *
 * The #svn_prop_inherited_item_t->path_or_url members of the
 * #svn_prop_inherited_item_t * structures in @a *inherited_props are
 * URLs if @a target is a URL or if @a target is a working copy path but the
 * property represented by the structure is above the working copy root (i.e.
 * the inherited property is from the cache).  In all other cases the
 * #svn_prop_inherited_item_t->path_or_url members are absolute working copy
 * paths.
 *
 * Allocate @a *props (including keys and values) and @a *inherited_props
 * (including its elements) in @a result_pool, use @a scratch_pool for
 * temporary allocations.
 *
 * @a target is a WC absolute path or a URL.
 *
 * Don't store any path, not even @a target, if it does not have a
 * property named @a propname.
 *
 * If @a revision->kind is #svn_opt_revision_unspecified, then: get
 * properties from the working copy if @a target is a working copy
 * path, or from the repository head if @a target is a URL.  Else get
 * the properties as of @a revision.  The actual node revision
 * selected is determined by the path as it exists in @a peg_revision.
 * If @a peg_revision->kind is #svn_opt_revision_unspecified, then
 * it defaults to #svn_opt_revision_head for URLs or
 * #svn_opt_revision_working for WC targets.  Use the authentication
 * baton in @a ctx for authentication if contacting the repository.
 * If @a actual_revnum is not @c NULL, the actual revision number used
 * for the fetch is stored in @a *actual_revnum.
 *
 * If @a depth is #svn_depth_empty, fetch the property from
 * @a target only; if #svn_depth_files, fetch from @a target and its
 * file children (if any); if #svn_depth_immediates, from @a target
 * and all of its immediate children (both files and directories); if
 * #svn_depth_infinity, from @a target and everything beneath it.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose properties are
 * gotten; that is, don't get @a propname on any item unless it's a member
 * of one of those changelists.  If @a changelists is empty (or
 * altogether @c NULL), no changelist filtering occurs.
 *
 * If error, don't touch @a *props, otherwise @a *props is a hash table
 * even if empty.
 *
 * This function returns SVN_ERR_UNVERSIONED_RESOURCE when it is called on
 * unversioned nodes.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_propget5(apr_hash_t **props,
                    apr_array_header_t **inherited_props,
                    const char *propname,
                    const char *target,  /* abspath or URL */
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_propget5 but with @a inherited_props always
 * passed as NULL.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propget4(apr_hash_t **props,
                    const char *propname,
                    const char *target,  /* abspath or URL */
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_propget4(), but with the following change to the
 * output hash keys:  keys are `<tt>char *</tt>' paths, prefixed by
 * @a target, which is a working copy path or a URL.
 *
 * This function returns SVN_ERR_ENTRY_NOT_FOUND where svn_client_propget4
 * would return SVN_ERR_UNVERSIONED_RESOURCE.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propget3(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/**
 * Similar to svn_client_propget3(), except that @a actual_revnum and
 * @a changelists are always @c NULL, and @a depth is set according to
 * @a recurse: if @a recurse is TRUE, then @a depth is
 * #svn_depth_infinity, else #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propget2(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/**
 * Similar to svn_client_propget2(), except that @a peg_revision is
 * always the same as @a revision.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_propget(apr_hash_t **props,
                   const char *propname,
                   const char *target,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/** Set @a *propval to the value of @a propname on revision @a revision
 * in the repository represented by @a URL.  Use the authentication baton
 * in @a ctx for authentication, and @a pool for all memory allocation.
 * Return the actual rev queried in @a *set_rev.
 *
 * If @a propname does not exist on @a revision, set @a *propval to @c NULL.
 *
 * Note that unlike its cousin svn_client_propget(), this routine
 * doesn't affect the working copy at all; it's a pure network
 * operation that queries an *unversioned* property attached to a
 * revision.  This can query log messages, dates, authors, and the
 * like.
 */
svn_error_t *
svn_client_revprop_get(const char *propname,
                       svn_string_t **propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool);

/**
 * Invoke @a receiver with @a receiver_baton to return the regular explicit, and
 * possibly the inherited, properties of @a target, a URL or working copy path.
 * @a receiver will be called for each path encountered.
 *
 * @a target is a WC path or a URL.
 *
 * If @a revision->kind is #svn_opt_revision_unspecified, then get the
 * explicit (and possibly the inherited) properties from the working copy,
 * if @a target is a working copy path, or from the repository head if
 * @a target is a URL.  Else get the properties as of @a revision.
 * The actual node revision selected is determined by the path as it exists
 * in @a peg_revision.  If @a peg_revision->kind is
 * #svn_opt_revision_unspecified, then it defaults to #svn_opt_revision_head
 * for URLs or #svn_opt_revision_working for WC targets.  Use the
 * authentication baton cached in @a ctx for authentication if contacting
 * the repository.
 *
 * If @a depth is #svn_depth_empty, list only the properties of
 * @a target itself.  If @a depth is #svn_depth_files, and
 * @a target is a directory, list the properties of @a target
 * and its file entries.  If #svn_depth_immediates, list the properties
 * of its immediate file and directory entries.  If #svn_depth_infinity,
 * list the properties of its file entries and recurse (with
 * #svn_depth_infinity) on directory entries.  #svn_depth_unknown is
 * equivalent to #svn_depth_empty.  All other values produce undefined
 * results.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose properties are
 * listed; that is, don't list properties on any item unless it's a member
 * of one of those changelists.  If @a changelists is empty (or
 * altogether @c NULL), no changelist filtering occurs.
 *
 * If @a get_target_inherited_props is true, then also return any inherited
 * properties when @a receiver is called for @a target.  If @a target is a
 * working copy path, then properties inherited by @a target as far as the
 * root of the working copy are obtained from the working copy's actual
 * property values.  Properties inherited from above the working copy
 * root come from the inherited properties cache.  If @a target is a URL,
 * then the inherited properties come from the repository.
 * If @a get_target_inherited_props is false, then no inherited properties
 * are returned to @a receiver.
 *
 * If @a target is not found, return the error #SVN_ERR_ENTRY_NOT_FOUND.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_proplist4(const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_boolean_t get_target_inherited_props,
                     svn_proplist_receiver2_t receiver,
                     void *receiver_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_proplist4(), except that the @a receiver type
 * is a #svn_proplist_receiver_t, @a get_target_inherited_props is
 * always passed NULL, and there is no separate scratch pool.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_proplist3(const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_proplist_receiver_t receiver,
                     void *receiver_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_proplist3(), except the properties are
 * returned as an array of #svn_client_proplist_item_t * structures
 * instead of by invoking the receiver function, there's no support
 * for @a changelists filtering, and @a recurse is used instead of a
 * #svn_depth_t parameter (FALSE corresponds to #svn_depth_empty,
 * and TRUE to #svn_depth_infinity).
 *
 * @since New in 1.2.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_proplist2(apr_array_header_t **props,
                     const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);

/**
 * Similar to svn_client_proplist2(), except that @a peg_revision is
 * always the same as @a revision.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_proplist(apr_array_header_t **props,
                    const char *target,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/** Set @a *props to a hash of the revision props attached to @a revision in
 * the repository represented by @a URL.  Use the authentication baton cached
 * in @a ctx for authentication, and @a pool for all memory allocation.
 * Return the actual rev queried in @a *set_rev.
 *
 * The allocated hash maps (<tt>const char *</tt>) property names to
 * (#svn_string_t *) property values.
 *
 * Note that unlike its cousin svn_client_proplist(), this routine
 * doesn't read a working copy at all; it's a pure network operation
 * that reads *unversioned* properties attached to a revision.
 */
svn_error_t *
svn_client_revprop_list(apr_hash_t **props,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool);
/** @} */


/**
 * @defgroup Export Export a tree from version control.
 *
 * @{
 */

/**
 * Export the contents of either a subversion repository or a
 * subversion working copy into a 'clean' directory (meaning a
 * directory with no administrative directories).  If @a result_rev
 * is not @c NULL and the path being exported is a repository URL, set
 * @a *result_rev to the value of the revision actually exported (set
 * it to #SVN_INVALID_REVNUM for local exports).
 *
 * @a from_path_or_url is either the path the working copy on disk, or
 * a URL to the repository you wish to export.
 *
 * When exporting a directory, @a to_path is the path to the directory
 * where you wish to create the exported tree; when exporting a file, it
 * is the path of the file that will be created.  If @a to_path is the
 * empty path, then the basename of the export file/directory in the repository
 * will be used.  If @a to_path represents an existing directory, and a
 * file is being exported, then a file with the that basename will be
 * created under that directory (as with 'copy' operations).
 *
 * @a peg_revision is the revision where the path is first looked up
 * when exporting from a repository.  If @a peg_revision->kind is
 * #svn_opt_revision_unspecified, then it defaults to #svn_opt_revision_head
 * for URLs or #svn_opt_revision_working for WC targets.
 *
 * @a revision is the revision that should be exported.
 *
 * @a peg_revision and @a revision must not be @c NULL.
 *
 * @a ctx->notify_func2 and @a ctx->notify_baton2 are the notification
 * functions and baton which are passed to svn_client_checkout() when
 * exporting from a repository.
 *
 * @a ctx is a context used for authentication in the repository case.
 *
 * @a overwrite if TRUE will cause the export to overwrite files or
 * directories.
 *
 * If @a ignore_externals is set, don't process externals definitions
 * as part of this operation.
 *
 * If @a ignore_keywords is set, don't expand keywords as part of this
 * operation.
 *
 * @a native_eol allows you to override the standard eol marker on the
 * platform you are running on.  Can be either "LF", "CR" or "CRLF" or
 * NULL.  If NULL will use the standard eol marker.  Any other value
 * will cause the #SVN_ERR_IO_UNKNOWN_EOL error to be returned.
 *
 * If @a depth is #svn_depth_infinity, export fully recursively.  Else
 * if it is #svn_depth_immediates, export @a from_path_or_url and its
 * immediate children (if any), but with subdirectories empty and at
 * #svn_depth_empty.  Else if #svn_depth_files, export @a
 * from_path_or_url and its immediate file children (if any) only.  If
 * @a depth is #svn_depth_empty, then export exactly @a
 * from_path_or_url and none of its children.
 *
 * All allocations are done in @a pool.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_export5(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t ignore_keywords,
                   svn_depth_t depth,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);

/**
 * Similar to svn_client_export5(), but with @a ignore_keywords set
 * to FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 * @since New in 1.5.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_export4(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_depth_t depth,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_export4(), but with @a depth set according to
 * @a recurse: if @a recurse is TRUE, set @a depth to
 * #svn_depth_infinity, if @a recurse is FALSE, set @a depth to
 * #svn_depth_files.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 *
 * @since New in 1.2.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_export3(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t recurse,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_export3(), but with @a peg_revision
 * always set to #svn_opt_revision_unspecified, @a overwrite set to
 * the value of @a force, @a ignore_externals always FALSE, and
 * @a recurse always TRUE.
 *
 * @since New in 1.1.
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_export2(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   svn_opt_revision_t *revision,
                   svn_boolean_t force,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool);


/**
 * Similar to svn_client_export2(), but with @a native_eol always set
 * to NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_export(svn_revnum_t *result_rev,
                  const char *from_path_or_url,
                  const char *to_path,
                  svn_opt_revision_t *revision,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup List List / ls
 *
 * @{
 */

/** The type of function invoked by svn_client_list3() to report the details
 * of each directory entry being listed.
 *
 * @a baton is the baton that was passed to the caller.  @a path is the
 * entry's path relative to @a abs_path; it is the empty path when reporting
 * the top node of the list operation.  @a dirent contains some or all of
 * the directory entry's details, as determined by the caller.  @a lock is
 * the entry's lock, if it is locked and if lock information is being
 * reported by the caller; otherwise @a lock is NULL.  @a abs_path is the
 * repository path of the top node of the list operation; it is relative to
 * the repository root and begins with "/".
 *
 * If svn_client_list3() was called with @a include_externals set to TRUE,
 * @a external_parent_url and @a external_target will be set.
 * @a external_parent_url is url of the directory which has the
 * externals definitions. @a external_target is the target subdirectory of
 * externals definitions which is relative to the parent directory that holds
 * the external item.
 *
 * If external_parent_url and external_target are defined, the item being
 * listed is part of the external described by external_parent_url and
 * external_target. Else, the item is not part of any external.
 * Moreover, we will never mix items which are part of separate
 * externals, and will always finish listing an external before listing
 * the next one.
 *
 * @a scratch_pool may be used for temporary allocations.
 *
 * @since New in 1.8.
 */
typedef svn_error_t *(*svn_client_list_func2_t)(
  void *baton,
  const char *path,
  const svn_dirent_t *dirent,
  const svn_lock_t *lock,
  const char *abs_path,
  const char *external_parent_url,
  const char *external_target,
  apr_pool_t *scratch_pool);

/**
 * Similar to #svn_client_list_func2_t, but without any information about
 * externals definitions.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API.
 *
 * @since New in 1.4
 *
 * */
typedef svn_error_t *(*svn_client_list_func_t)(void *baton,
                                               const char *path,
                                               const svn_dirent_t *dirent,
                                               const svn_lock_t *lock,
                                               const char *abs_path,
                                               apr_pool_t *pool);

/**
 * Report the directory entry, and possibly children, for @a
 * path_or_url at @a revision.  The actual node revision selected is
 * determined by the path as it exists in @a peg_revision.  If @a
 * peg_revision->kind is #svn_opt_revision_unspecified, then it defaults
 * to #svn_opt_revision_head for URLs or #svn_opt_revision_working
 * for WC targets.
 *
 * Report directory entries by invoking @a list_func/@a baton with @a path
 * relative to @a path_or_url.  The dirent for @a path_or_url is reported
 * using an empty @a path.  If @a path_or_url is a directory, also report
 * its children.  If @a path_or_url is non-existent, return
 * #SVN_ERR_FS_NOT_FOUND.
 *
 * If the @a patterns array of <tt>const char *</tt> is not @c NULL, only
 * report paths whose last segment matches one of the specified glob
 * patterns.  This does not affect the size of the tree nor the number of
 * externals being covered.
 *
 * If @a fetch_locks is TRUE, include locks when reporting directory entries.
 *
 * If @a include_externals is TRUE, also list all external items
 * reached by recursion. @a depth value passed to the original list target
 * applies for the externals also.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * Use authentication baton cached in @a ctx to authenticate against the
 * repository.
 *
 * If @a depth is #svn_depth_empty, list just @a path_or_url itself.
 * If @a depth is #svn_depth_files, list @a path_or_url and its file
 * entries.  If #svn_depth_immediates, list its immediate file and
 * directory entries.  If #svn_depth_infinity, list file entries and
 * recurse (with #svn_depth_infinity) on directory entries.
 *
 * @a dirent_fields controls which fields in the #svn_dirent_t's are
 * filled in.  To have them totally filled in use #SVN_DIRENT_ALL,
 * otherwise simply bitwise OR together the combination of @c SVN_DIRENT_
 * fields you care about.
 *
 * @since New in 1.10.
 */
svn_error_t *
svn_client_list4(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 const apr_array_header_t *patterns,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_boolean_t include_externals,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool);

/** Similar to svn_client_list4(), but with @a patterns set to @c NULL.
 *
 * @since New in 1.8.
 *
 * @deprecated Provided for backwards compatibility with the 1.9 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_list3(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_boolean_t include_externals,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);


/** Similar to svn_client_list3(), but with @a include_externals set
 * to FALSE, and using a #svn_client_list_func_t as callback.
 *
 * @since New in 1.5.
 *
 * @deprecated Provided for backwards compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_list2(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_client_list_func_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_list2(), but with @a recurse instead of @a depth.
 * If @a recurse is TRUE, pass #svn_depth_files for @a depth; else
 * pass #svn_depth_infinity.
 *
 * @since New in 1.4.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_list(const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,
                apr_uint32_t dirent_fields,
                svn_boolean_t fetch_locks,
                svn_client_list_func_t list_func,
                void *baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Same as svn_client_list(), but always passes #SVN_DIRENT_ALL for
 * the @a dirent_fields argument and returns all information in two
 * hash tables instead of invoking a callback.
 *
 * Set @a *dirents to a newly allocated hash of directory entries.
 * The @a dirents hash maps entry names (<tt>const char *</tt>) to
 * #svn_dirent_t *'s.
 *
 * If @a locks is not @c NULL, set @a *locks to a hash table mapping
 * entry names (<tt>const char *</tt>) to #svn_lock_t *'s.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 * Use svn_client_list2() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_ls3(apr_hash_t **dirents,
               apr_hash_t **locks,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool);

/**
 * Same as svn_client_ls3(), but without the ability to get locks.
 *
 * @since New in 1.2.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 * Use svn_client_list2() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_ls2(apr_hash_t **dirents,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool);

/**
 * Similar to svn_client_ls2() except that @a peg_revision is always
 * the same as @a revision.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 * Use svn_client_list2() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_ls(apr_hash_t **dirents,
              const char *path_or_url,
              svn_opt_revision_t *revision,
              svn_boolean_t recurse,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool);


/** @} */

/**
 * @defgroup Cat View the contents of a file in the repository.
 *
 * @{
 */

/**
 * Output the content of a file.
 *
 * @param[out] props           Optional output argument to obtain properties.
 * @param[in] out              The stream to which the content will be written.
 * @param[in] path_or_url      The path or URL of the file.
 * @param[in] peg_revision     The peg revision.
 * @param[in] revision         The operative revision.
 * @param[in] expand_keywords  When true, keywords (when set) are expanded.
 * @param[in] ctx   The standard client context, used for possible
 *                  authentication.
 *
 * @return A pointer to an #svn_error_t of the type (this list is not
 *         exhaustive): <br>
 *         An unspecified error if @a revision is of kind
 *         #svn_opt_revision_previous (or some other kind that requires
 *         a local path), because the desired revision cannot be
 *         determined. <br>
 *         If no error occurred, return #SVN_NO_ERROR.
 *
 * If @a *props is not NULL it is set to a hash of all the file's
 * non-inherited properties. If it is NULL, the properties are only
 * used for determining how and if the file should be translated.
 *
 * @see #svn_client_ctx_t <br> @ref clnt_revisions for
 *      a discussion of operative and peg revisions.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_cat3(apr_hash_t **props,
                svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t expand_keywords,
                svn_client_ctx_t *ctx,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);

/**
 * Similar to svn_client_cat3() except without the option of directly
 * reading the properties, and with @a expand_keywords always TRUE.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_cat2(svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);


/**
 * Similar to svn_client_cat2() except that the peg revision is always
 * the same as @a revision.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_cat(svn_stream_t *out,
               const char *path_or_url,
               const svn_opt_revision_t *revision,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool);

/** @} end group: cat */



/** Shelving commands
 *
 * @defgroup svn_client_shelve_funcs Client Shelving Functions
 * @{
 */

/** Shelve a change.
 *
 * Shelve as @a name the local modifications found by @a paths, @a depth,
 * @a changelists. Revert the shelved change from the WC unless @a keep_local
 * is true.
 *
 * If @a dry_run is true, don't actually do it.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelve(const char *name,
                  const apr_array_header_t *paths,
                  svn_depth_t depth,
                  const apr_array_header_t *changelists,
                  svn_boolean_t keep_local,
                  svn_boolean_t dry_run,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** Unshelve the shelved change @a name.
 *
 * @a local_abspath is any path in the WC and is used to find the WC root.
 * Rename the shelved patch to add a '.bak' extension unless @a keep is true.
 *
 * If @a dry_run is true, don't actually do it.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_unshelve(const char *name,
                    const char *local_abspath,
                    svn_boolean_t keep,
                    svn_boolean_t dry_run,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool);

/** Delete the shelved patch @a name.
 *
 * @a local_abspath is any path in the WC and is used to find the WC root.
 *
 * If @a dry_run is true, don't actually do it.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelves_delete(const char *name,
                          const char *local_abspath,
                          svn_boolean_t dry_run,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool);

/** Information about a shelved patch.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
typedef struct svn_client_shelved_patch_info_t
{
  const char *message;  /* first line of log message */
  const char *patch_path;  /* abspath of the patch file */
  svn_io_dirent2_t *dirent;  /* info about the patch file */
  apr_time_t mtime;  /* a copy of dirent->mtime */
} svn_client_shelved_patch_info_t;

/** Set @a *shelved_patch_infos to a hash, keyed by patch name, of pointers to
 * @c svn_client_shelved_patch_info_t structures.
 *
 * @a local_abspath is any path in the WC and is used to find the WC root.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelves_list(apr_hash_t **shelved_patch_infos,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/** Set @a *any_shelved to indicate if there are any shelved changes in this WC.
 *
 * This shall provide the answer fast, regardless of how many changes
 * are stored, unlike svn_client_shelves_list().
 *
 * ### Initial implementation isn't O(1) fast -- it just calls
 *     svn_client_shelves_list().
 *
 * @a local_abspath is any path in the WC and is used to find the WC root.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelves_any(svn_boolean_t *any_shelved,
                       const char *local_abspath,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool);

/** Set @a *affected_paths to a hash with one entry for each path affected
 * by the shelf @a name. The hash key is the old path and value is
 * the new path, both relative to the WC root. The key and value are the
 * same except when a path is moved or copied.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelf_get_paths(apr_hash_t **affected_paths,
                           const char *name,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** Set @a *has_changes to indicate whether the shelf @a name
 * contains any modifications, in other words if svn_client_shelf_get_paths()
 * would return a non-empty set of paths.
 *
 * @since New in 1.10.
 * @warning EXPERIMENTAL.
 */
SVN_EXPERIMENTAL
svn_error_t *
svn_client_shelf_has_changes(svn_boolean_t *has_changes,
                             const char *name,
                             const char *local_abspath,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool);

/** @} */

/** Changelist commands
 *
 * @defgroup svn_client_changelist_funcs Client Changelist Functions
 * @{
 */

/** Implementation note:
 *
 *  For now, changelists are implemented by scattering the
 *  associations across multiple .svn/entries files in a working copy.
 *  However, this client API was written so that we have the option of
 *  changing the underlying implementation -- we may someday want to
 *  store changelist definitions in a centralized database.
 */

/**
 * Add each path in @a paths (recursing to @a depth as necessary) to
 * @a changelist.  If a path is already a member of another
 * changelist, then remove it from the other changelist and add it to
 * @a changelist.  (For now, a path cannot belong to two changelists
 * at once.)
 *
 * @a paths is an array of (const char *) local WC paths.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose changelist
 * assignments are adjusted; that is, don't tweak the changeset of any
 * item unless it's currently a member of one of those changelists.
 * If @a changelists is empty (or altogether @c NULL), no changelist
 * filtering occurs.
 *
 * @note This metadata is purely a client-side "bookkeeping"
 * convenience, and is entirely managed by the working copy.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_add_to_changelist(const apr_array_header_t *paths,
                             const char *changelist,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/**
 * Remove each path in @a paths (recursing to @a depth as necessary)
 * from changelists to which they are currently assigned.
 *
 * @a paths is an array of (const char *) local WC paths.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose changelist
 * assignments are removed; that is, don't remove from a changeset any
 * item unless it's currently a member of one of those changelists.
 * If @a changelists is empty (or altogether @c NULL), all changelist
 * assignments in and under each path in @a paths (to @a depth) will
 * be removed.
 *
 * @note This metadata is purely a client-side "bookkeeping"
 * convenience, and is entirely managed by the working copy.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_remove_from_changelists(const apr_array_header_t *paths,
                                   svn_depth_t depth,
                                   const apr_array_header_t *changelists,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool);


/**
 * Beginning at @a path, crawl to @a depth to discover every path in
 * or under @a path which belongs to one of the changelists in @a
 * changelists (an array of <tt>const char *</tt> changelist names).
 * If @a changelists is @c NULL, discover paths with any changelist.
 * Call @a callback_func (with @a callback_baton) each time a
 * changelist-having path is discovered.
 *
 * @a path is a local WC path.
 *
 * If @a ctx->cancel_func is not @c NULL, invoke it passing @a
 * ctx->cancel_baton during the recursive walk.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_client_get_changelists(const char *path,
                           const apr_array_header_t *changelists,
                           svn_depth_t depth,
                           svn_changelist_receiver_t callback_func,
                           void *callback_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool);

/** @} */



/** Locking commands
 *
 * @defgroup svn_client_locking_funcs Client Locking Functions
 * @{
 */

/**
 * Lock @a targets in the repository.  @a targets is an array of
 * <tt>const char *</tt> paths - either all working copy paths or all URLs.
 * All targets must be in the same repository.
 *
 * If a target is already locked in the repository, no lock will be
 * acquired unless @a steal_lock is TRUE, in which case the locks are
 * stolen.  @a comment, if non-NULL, is an xml-escapable description
 * stored with each lock in the repository.  Each acquired lock will
 * be stored in the working copy if the targets are WC paths.
 *
 * For each target @a ctx->notify_func2/notify_baton2 will be used to indicate
 * whether it was locked.  An action of #svn_wc_notify_locked
 * means that the path was locked.  If the path was not locked because
 * it was out of date or there was already a lock in the repository,
 * the notification function will be called with
 * #svn_wc_notify_failed_lock, and the error passed in the notification
 * structure.
 *
 * Use @a pool for temporary allocations.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_client_lock(const apr_array_header_t *targets,
                const char *comment,
                svn_boolean_t steal_lock,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Unlock @a targets in the repository.  @a targets is an array of
 * <tt>const char *</tt> paths - either all working copy paths or all URLs.
 * All targets must be in the same repository.
 *
 * If the targets are WC paths, and @a break_lock is FALSE, the working
 * copy must contain a lock for each target.
 * If this is not the case, or the working copy lock doesn't match the
 * lock token in the repository, an error will be signaled.
 *
 * If the targets are URLs, the locks may be broken even if @a break_lock
 * is FALSE, but only if the lock owner is the same as the
 * authenticated user.
 *
 * If @a break_lock is TRUE, the locks will be broken in the
 * repository.  In both cases, the locks, if any, will be removed from
 * the working copy if the targets are WC paths.
 *
 * The notification functions in @a ctx will be called for each
 * target.  If the target was successfully unlocked,
 * #svn_wc_notify_unlocked will be used.  Else, if the error is
 * directly related to unlocking the path (see
 * #SVN_ERR_IS_UNLOCK_ERROR), #svn_wc_notify_failed_unlock will be
 * used and the error will be passed in the notification structure.

 * Use @a pool for temporary allocations.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_client_unlock(const apr_array_header_t *targets,
                  svn_boolean_t break_lock,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool);

/** @} */

/**
 * @defgroup Info Show repository information about a working copy.
 *
 * @{
 */

/** The size of the file is unknown.
 * Used as value in fields of type @c apr_size_t in #svn_info_t.
 *
 * @since New in 1.5
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
#define SVN_INFO_SIZE_UNKNOWN ((apr_size_t) -1)

/**
 * A structure which describes various system-generated metadata about
 * a working-copy path or URL.
 *
 * @note Fields may be added to the end of this structure in future
 * versions.  Therefore, users shouldn't allocate structures of this
 * type, to preserve binary compatibility.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.  The new
 * API is #svn_client_info2_t.
 */
typedef struct svn_info_t
{
  /** Where the item lives in the repository. */
  const char *URL;

  /** The revision of the object.  If path_or_url is a working-copy
   * path, then this is its current working revnum.  If path_or_url
   * is a URL, then this is the repos revision that path_or_url lives in. */
  svn_revnum_t rev;

  /** The node's kind. */
  svn_node_kind_t kind;

  /** The root URL of the repository. */
  const char *repos_root_URL;

  /** The repository's UUID. */
  const char *repos_UUID;

  /** The last revision in which this object changed. */
  svn_revnum_t last_changed_rev;

  /** The date of the last_changed_rev. */
  apr_time_t last_changed_date;

  /** The author of the last_changed_rev. */
  const char *last_changed_author;

  /** An exclusive lock, if present.  Could be either local or remote. */
  svn_lock_t *lock;

  /** Whether or not to ignore the next 10 wc-specific fields. */
  svn_boolean_t has_wc_info;

  /**
   * @name Working-copy path fields
   * These things only apply to a working-copy path.
   * See svn_wc_entry_t for explanations.
   * @{
   */
  svn_wc_schedule_t schedule;
  const char *copyfrom_url;
  svn_revnum_t copyfrom_rev;
  apr_time_t text_time;
  apr_time_t prop_time;  /* will always be 0 for svn 1.4 and later */
  const char *checksum;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_wrk;
  const char *prejfile;
  /** @since New in 1.5. */
  const char *changelist;
  /** @since New in 1.5. */
  svn_depth_t depth;

  /**
   * Similar to working_size64, but will be #SVN_INFO_SIZE_UNKNOWN when
   * its value would overflow apr_size_t (so when size >= 4 GB - 1 byte).
   *
   * @deprecated Provided for backward compatibility with the 1.5 API.
   */
  apr_size_t working_size;

  /** @} */

  /**
   * Similar to size64, but size will be #SVN_INFO_SIZE_UNKNOWN when
   * its value would overflow apr_size_t (so when size >= 4 GB - 1 byte).
   *
   * @deprecated Provided for backward compatibility with the 1.5 API.
   */
  apr_size_t size;

  /**
   * The size of the file in the repository (untranslated,
   * e.g. without adjustment of line endings and keyword
   * expansion). Only applicable for file -- not directory -- URLs.
   * For working copy paths, size64 will be #SVN_INVALID_FILESIZE.
   * @since New in 1.6.
   */
  svn_filesize_t size64;

  /**
   * The size of the file after being translated into its local
   * representation, or #SVN_INVALID_FILESIZE if unknown.
   * Not applicable for directories.
   * @since New in 1.6.
   * @name Working-copy path fields
   * @{
   */
  svn_filesize_t working_size64;

  /**
   * Info on any tree conflict of which this node is a victim. Otherwise NULL.
   * @since New in 1.6.
   */
  svn_wc_conflict_description_t *tree_conflict;

  /** @} */

} svn_info_t;


/**
 * The callback invoked by svn_client_info2().  Each invocation
 * describes @a path with the information present in @a info.  Note
 * that any fields within @a info may be NULL if information is
 * unavailable.  Use @a pool for all temporary allocation.
 *
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.6 API.  The new
 * API is #svn_client_info_receiver2_t.
 */
typedef svn_error_t *(*svn_info_receiver_t)(
  void *baton,
  const char *path,
  const svn_info_t *info,
  apr_pool_t *pool);

/**
 * Return a duplicate of @a info, allocated in @a pool. No part of the new
 * structure will be shared with @a info.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.6 API.  The new
 * API is #svn_client_info2_dup().
 */
SVN_DEPRECATED
svn_info_t *
svn_info_dup(const svn_info_t *info,
             apr_pool_t *pool);

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
typedef struct svn_client_info2_t
{
  /** Where the item lives in the repository. */
  const char *URL;

  /** The revision of the object.  If the target is a working-copy
   * path, then this is its current working revnum.  If the target
   * is a URL, then this is the repos revision that it lives in. */
  svn_revnum_t rev;

  /** The root URL of the repository. */
  const char *repos_root_URL;

  /** The repository's UUID. */
  const char *repos_UUID;

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
  const svn_lock_t *lock;

  /** Possible information about the working copy, NULL if not valid. */
  const svn_wc_info_t *wc_info;

} svn_client_info2_t;

/**
 * Return a duplicate of @a info, allocated in @a pool. No part of the new
 * structure will be shared with @a info.
 *
 * @since New in 1.7.
 */
svn_client_info2_t *
svn_client_info2_dup(const svn_client_info2_t *info,
                     apr_pool_t *pool);

/**
 * The callback invoked by info retrievers.  Each invocation
 * describes @a abspath_or_url with the information present in @a info.
 * Use @a scratch_pool for all temporary allocation.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_client_info_receiver2_t)(
                         void *baton,
                         const char *abspath_or_url,
                         const svn_client_info2_t *info,
                         apr_pool_t *scratch_pool);

/**
 * Invoke @a receiver with @a receiver_baton to return information
 * about @a abspath_or_url in @a revision.  The information returned is
 * system-generated metadata, not the sort of "property" metadata
 * created by users.  See #svn_client_info2_t.
 *
 * If both revision arguments are either #svn_opt_revision_unspecified
 * or @c NULL, then information will be pulled solely from the working copy;
 * no network connections will be made.
 *
 * Otherwise, information will be pulled from a repository.  The
 * actual node revision selected is determined by the @a abspath_or_url
 * as it exists in @a peg_revision.  If @a peg_revision->kind is
 * #svn_opt_revision_unspecified, then it defaults to
 * #svn_opt_revision_head for URLs or #svn_opt_revision_working for
 * WC targets.
 *
 * If @a abspath_or_url is not a local path, then if @a revision is of
 * kind #svn_opt_revision_previous (or some other kind that requires
 * a local path), an error will be returned, because the desired
 * revision cannot be determined.
 *
 * Use the authentication baton cached in @a ctx to authenticate
 * against the repository.
 *
 * If @a abspath_or_url is a file, just invoke @a receiver on it.  If it
 * is a directory, then descend according to @a depth.  If @a depth is
 * #svn_depth_empty, invoke @a receiver on @a abspath_or_url and
 * nothing else; if #svn_depth_files, on @a abspath_or_url and its
 * immediate file children; if #svn_depth_immediates, the preceding
 * plus on each immediate subdirectory; if #svn_depth_infinity, then
 * recurse fully, invoking @a receiver on @a abspath_or_url and
 * everything beneath it.
 *
 * If @a fetch_excluded is TRUE, also also send excluded nodes in the working
 * copy to @a receiver, otherwise these are skipped. If @a fetch_actual_only
 * is TRUE also send nodes that don't exist as versioned but are still
 * tree conflicted.
 *
 * If @a include_externals is @c TRUE, recurse into externals and report about
 * them as well.
 *
 * @a changelists is an array of <tt>const char *</tt> changelist
 * names, used as a restrictive filter on items whose info is
 * reported; that is, don't report info about any item unless
 * it's a member of one of those changelists.  If @a changelists is
 * empty (or altogether @c NULL), no changelist filtering occurs.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client_info4(const char *abspath_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 svn_boolean_t fetch_excluded,
                 svn_boolean_t fetch_actual_only,
                 svn_boolean_t include_externals,
                 const apr_array_header_t *changelists,
                 svn_client_info_receiver2_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool);


/** Similar to svn_client_info4, but doesn't support walking externals.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.8 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_info3(const char *abspath_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 svn_boolean_t fetch_excluded,
                 svn_boolean_t fetch_actual_only,
                 const apr_array_header_t *changelists,
                 svn_client_info_receiver2_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool);

/** Similar to svn_client_info3, but uses an svn_info_receiver_t instead of
 * a #svn_client_info_receiver2_t, and @a path_or_url may be a relative path.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_info2(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_info_receiver_t receiver,
                 void *receiver_baton,
                 svn_depth_t depth,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool);

/**
 * Similar to svn_client_info2() but with @a changelists passed as @c
 * NULL, and @a depth set according to @a recurse: if @a recurse is
 * TRUE, @a depth is #svn_depth_infinity, else #svn_depth_empty.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_info(const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_info_receiver_t receiver,
                void *receiver_baton,
                svn_boolean_t recurse,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool);

/**
 * Set @a *wcroot_abspath to the local abspath of the root of the
 * working copy in which @a local_abspath resides.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_get_wc_root(const char **wcroot_abspath,
                       const char *local_abspath,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/**
 * Set @a *min_revision and @a *max_revision to the lowest and highest
 * revision numbers found within @a local_abspath.  If @a committed is
 * TRUE, set @a *min_revision and @a *max_revision to the lowest and
 * highest comitted (i.e. "last changed") revision numbers,
 * respectively.  NULL may be passed for either of @a min_revision and
 * @a max_revision to indicate the caller's lack of interest in the
 * value.  Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_min_max_revisions(svn_revnum_t *min_revision,
                             svn_revnum_t *max_revision,
                             const char *local_abspath,
                             svn_boolean_t committed,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *scratch_pool);

/** @} */


/**
 * @defgroup Patch Apply a patch to the working copy
 *
 * @{
 */

/**
 * The callback invoked by svn_client_patch() before attempting to patch
 * the target file at @a canon_path_from_patchfile (the path as parsed from
 * the patch file, but in canonicalized form). The callback can set
 * @a *filtered to @c TRUE to prevent the file from being patched, or else
 * must set it to @c FALSE.
 *
 * The callback is also provided with @a patch_abspath, the path of a
 * temporary file containing the patched result, and with @a reject_abspath,
 * the path to a temporary file containing the diff text of any hunks
 * which were rejected during patching.
 *
 * Because the callback is invoked before the patching attempt is made,
 * there is no guarantee that the target file will actually be patched
 * successfully. Client implementations must pay attention to notification
 * feedback provided by svn_client_patch() to find out which paths were
 * patched successfully.
 *
 * Note also that the files at @a patch_abspath and @a reject_abspath are
 * guaranteed to remain on disk after patching only if the
 * @a remove_tempfiles parameter for svn_client_patch() is @c FALSE.
 *
 * The const char * parameters may be allocated in @a scratch_pool which
 * will be cleared after each invocation.
 *
 * @since New in 1.7.
 */
typedef svn_error_t *(*svn_client_patch_func_t)(
  void *baton,
  svn_boolean_t *filtered,
  const char *canon_path_from_patchfile,
  const char *patch_abspath,
  const char *reject_abspath,
  apr_pool_t *scratch_pool);

/**
 * Apply a unidiff patch that's located at absolute path
 * @a patch_abspath to the working copy directory at @a wc_dir_abspath.
 *
 * This function makes a best-effort attempt at applying the patch.
 * It might skip patch targets which cannot be patched (e.g. targets
 * that are outside of the working copy). It will also reject hunks
 * which cannot be applied to a target in case the hunk's context
 * does not match anywhere in the patch target.
 *
 * If @a dry_run is TRUE, the patching process is carried out, and full
 * notification feedback is provided, but the working copy is not modified.
 *
 * @a strip_count specifies how many leading path components should be
 * stripped from paths obtained from the patch. It is an error if a
 * negative strip count is passed.
 *
 * If @a reverse is @c TRUE, apply patches in reverse, deleting lines
 * the patch would add and adding lines the patch would delete.
 *
 * If @a ignore_whitespace is TRUE, allow patches to be applied if they
 * only differ from the target by whitespace.
 *
 * If @a remove_tempfiles is TRUE, lifetimes of temporary files created
 * during patching will be managed internally. Otherwise, the caller should
 * take ownership of these files, the names of which can be obtained by
 * passing a @a patch_func callback.
 *
 * If @a patch_func is non-NULL, invoke @a patch_func with @a patch_baton
 * for each patch target processed.
 *
 * If @a ctx->notify_func2 is non-NULL, invoke @a ctx->notify_func2 with
 * @a ctx->notify_baton2 as patching progresses.
 *
 * If @a ctx->cancel_func is non-NULL, invoke it passing @a
 * ctx->cancel_baton at various places during the operation.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_patch(const char *patch_abspath,
                 const char *wc_dir_abspath,
                 svn_boolean_t dry_run,
                 int strip_count,
                 svn_boolean_t reverse,
                 svn_boolean_t ignore_whitespace,
                 svn_boolean_t remove_tempfiles,
                 svn_client_patch_func_t patch_func,
                 void *patch_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool);

/** @} */

/** @} end group: Client working copy management */

/**
 *
 * @defgroup clnt_sessions Client session related functions
 *
 * @{
 *
 */


/* Converting paths to URLs. */

/** Set @a *url to the URL for @a path_or_url allocated in result_pool.
 *
 * If @a path_or_url is already a URL, set @a *url to @a path_or_url.
 *
 * If @a path_or_url is a versioned item, set @a *url to @a
 * path_or_url's entry URL.  If @a path_or_url is unversioned (has
 * no entry), set @a *url to NULL.
 *
 * Use @a ctx->wc_ctx to retrieve the information. Use
 ** @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client_url_from_path2(const char **url,
                          const char *path_or_url,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Similar to svn_client_url_from_path2(), but without a context argument.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_url_from_path(const char **url,
                         const char *path_or_url,
                         apr_pool_t *pool);



/* Fetching a repository's root URL and UUID. */

/** Set @a *repos_root_url and @a *repos_uuid, to the root URL and UUID of
 * the repository in which @a abspath_or_url is versioned. Use the
 * authentication baton and working copy context cached in @a ctx as
 * necessary. @a repos_root_url and/or @a repos_uuid may be NULL if not
 * wanted.
 *
 * This function will open a temporary RA session to the repository if
 * necessary to get the information.
 *
 * Allocate @a *repos_root_url and @a *repos_uuid in @a result_pool.
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client_get_repos_root(const char **repos_root_url,
                          const char **repos_uuid,
                          const char *abspath_or_url,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/** Set @a *url to the repository root URL of the repository in which
 * @a path_or_url is versioned (or scheduled to be versioned),
 * allocated in @a pool.  @a ctx is required for possible repository
 * authentication.
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.7 API. Use
 * svn_client_get_repos_root() instead, with an absolute path.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool);

/** Get repository @a uuid for @a url.
 *
 * Use a @a pool to open a temporary RA session to @a url, discover the
 * repository uuid, and free the session.  Return the uuid in @a uuid,
 * allocated in @a pool.  @a ctx is required for possible repository
 * authentication.
 *
 * @deprecated Provided for backward compatibility with the 1.7 API. Use
 * svn_client_get_repos_root() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_uuid_from_url(const char **uuid,
                         const char *url,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool);


/** Return the repository @a uuid for working-copy @a local_abspath,
 * allocated in @a result_pool.  Use @a ctx->wc_ctx to retrieve the
 * information.
 *
 * Use @a scratch_pool for temporary allocations.
 *
 * @since New in 1.7.
 * @deprecated Provided for backward compatibility with the 1.7 API. Use
 * svn_client_get_repos_root() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_uuid_from_path2(const char **uuid,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** Similar to svn_client_uuid_from_path2(), but with a relative path and
 * an access baton.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_uuid_from_path(const char **uuid,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool);


/* Opening RA sessions. */

/** Open an RA session rooted at @a url, and return it in @a *session.
 *
 * Use the authentication baton stored in @a ctx for authentication.
 * @a *session is allocated in @a result_pool.
 *
 * If @a wri_abspath is not NULL, use the working copy identified by @a
 * wri_abspath to potentially avoid transferring unneeded data.
 *
 * @note This function is similar to svn_ra_open4(), but the caller avoids
 * having to providing its own callback functions.
 * @since New in 1.8.
 */
svn_error_t *
svn_client_open_ra_session2(svn_ra_session_t **session,
                           const char *url,
                           const char *wri_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/** Similar to svn_client_open_ra_session2(), but with @ wri_abspath
 * always passed as NULL, and with the same pool used as both @a
 * result_pool and @a scratch_pool.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_client_open_ra_session(svn_ra_session_t **session,
                           const char *url,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool);


/** @} end group: Client session related functions */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_CLIENT_H */
