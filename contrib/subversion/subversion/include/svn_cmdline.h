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
 * @file svn_cmdline.h
 * @brief Support functions for command line programs
 */




#ifndef SVN_CMDLINE_H
#define SVN_CMDLINE_H

#include <apr_pools.h>
#include <apr_getopt.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define APR_WANT_STDIO
#endif
#include <apr_want.h>

#include "svn_types.h"
#include "svn_auth.h"
#include "svn_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Set up the locale for character conversion, and initialize APR.
 * If @a error_stream is non-NULL, print error messages to the stream,
 * using @a progname as the program name.  Attempt to set @c stdout to
 * line-buffered mode, and @a error_stream to unbuffered mode.  Return
 * @c EXIT_SUCCESS if successful, otherwise @c EXIT_FAILURE.
 *
 * @note This function should be called exactly once at program startup,
 *       before calling any other APR or Subversion functions.
 */
int
svn_cmdline_init(const char *progname,
                 FILE *error_stream);


/** Set @a *dest to an output-encoded C string from UTF-8 C string @a
 * src; allocate @a *dest in @a pool.
 */
svn_error_t *
svn_cmdline_cstring_from_utf8(const char **dest,
                              const char *src,
                              apr_pool_t *pool);

/** Like svn_utf_cstring_from_utf8_fuzzy(), but converts to an
 * output-encoded C string. */
const char *
svn_cmdline_cstring_from_utf8_fuzzy(const char *src,
                                    apr_pool_t *pool);

/** Set @a *dest to a UTF-8-encoded C string from input-encoded C
 * string @a src; allocate @a *dest in @a pool.
 */
svn_error_t *
svn_cmdline_cstring_to_utf8(const char **dest,
                            const char *src,
                            apr_pool_t *pool);

/** Set @a *dest to an output-encoded natively-formatted path string
 * from canonical path @a src; allocate @a *dest in @a pool.
 */
svn_error_t *
svn_cmdline_path_local_style_from_utf8(const char **dest,
                                       const char *src,
                                       apr_pool_t *pool);

/** Write to stdout, using a printf-like format string @a fmt, passed
 * through apr_pvsprintf().  All string arguments are in UTF-8; the output
 * is converted to the output encoding.  Use @a pool for temporary
 * allocation.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_cmdline_printf(apr_pool_t *pool,
                   const char *fmt,
                   ...)
       __attribute__((format(printf, 2, 3)));

/** Write to the stdio @a stream, using a printf-like format string @a fmt,
 * passed through apr_pvsprintf().  All string arguments are in UTF-8;
 * the output is converted to the output encoding.  Use @a pool for
 * temporary allocation.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_cmdline_fprintf(FILE *stream,
                    apr_pool_t *pool,
                    const char *fmt,
                    ...)
       __attribute__((format(printf, 3, 4)));

/** Output the @a string to the stdio @a stream, converting from UTF-8
 * to the output encoding.  Use @a pool for temporary allocation.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_cmdline_fputs(const char *string,
                  FILE *stream,
                  apr_pool_t *pool);

/** Flush output buffers of the stdio @a stream, returning an error if that
 * fails.  This is just a wrapper for the standard fflush() function for
 * consistent error handling.
 *
 * @since New in 1.1.
 */
svn_error_t *
svn_cmdline_fflush(FILE *stream);

/** Return the name of the output encoding allocated in @a pool, or @c
 * APR_LOCALE_CHARSET if the output encoding is the same as the locale
 * encoding.
 *
 * @since New in 1.3.
 */
const char *
svn_cmdline_output_encoding(apr_pool_t *pool);

/** Handle @a error in preparation for immediate exit from a
 * command-line client.  Specifically:
 *
 * Call svn_handle_error2(@a error, stderr, FALSE, @a prefix), clear
 * @a error, destroy @a pool iff it is non-NULL, and return EXIT_FAILURE.
 *
 * @since New in 1.3.
 */
int
svn_cmdline_handle_exit_error(svn_error_t *error,
                              apr_pool_t *pool,
                              const char *prefix);

/** A prompt function/baton pair, and the path to the configuration
 * directory. To be passed as the baton argument to the
 * @c svn_cmdline_*_prompt functions.
 *
 * @since New in 1.6.
 */
typedef struct svn_cmdline_prompt_baton2_t {
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  const char *config_dir;
} svn_cmdline_prompt_baton2_t;

/** Like svn_cmdline_prompt_baton2_t, but without the path to the
 * configuration directory.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
typedef struct svn_cmdline_prompt_baton_t {
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} svn_cmdline_prompt_baton_t;

/** Prompt the user for input, using @a prompt_str for the prompt and
 * @a baton (which may be @c NULL) for cancellation, and returning the
 * user's response in @a result, allocated in @a pool.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_cmdline_prompt_user2(const char **result,
                         const char *prompt_str,
                         svn_cmdline_prompt_baton_t *baton,
                         apr_pool_t *pool);

/** Similar to svn_cmdline_prompt_user2, but without cancellation
 * support.
 *
 * @deprecated Provided for backward compatibility with the 1.4 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_cmdline_prompt_user(const char **result,
                        const char *prompt_str,
                        apr_pool_t *pool);

/** An implementation of @c svn_auth_simple_prompt_func_t that prompts
 * the user for keyboard input on the command line.
 *
 * @since New in 1.4.
 *
 * Expects a @c svn_cmdline_prompt_baton_t to be passed as @a baton.
 */
svn_error_t *
svn_cmdline_auth_simple_prompt(svn_auth_cred_simple_t **cred_p,
                               void *baton,
                               const char *realm,
                               const char *username,
                               svn_boolean_t may_save,
                               apr_pool_t *pool);


/** An implementation of @c svn_auth_username_prompt_func_t that prompts
 * the user for their username via the command line.
 *
 * @since New in 1.4.
 *
 * Expects a @c svn_cmdline_prompt_baton_t to be passed as @a baton.
 */
svn_error_t *
svn_cmdline_auth_username_prompt(svn_auth_cred_username_t **cred_p,
                                 void *baton,
                                 const char *realm,
                                 svn_boolean_t may_save,
                                 apr_pool_t *pool);


/** An implementation of @c svn_auth_ssl_server_trust_prompt_func_t that
 * asks the user if they trust a specific ssl server via the command line.
 *
 * @since New in 1.4.
 *
 * Expects a @c svn_cmdline_prompt_baton_t to be passed as @a baton.
 */
svn_error_t *
svn_cmdline_auth_ssl_server_trust_prompt(
  svn_auth_cred_ssl_server_trust_t **cred_p,
  void *baton,
  const char *realm,
  apr_uint32_t failures,
  const svn_auth_ssl_server_cert_info_t *cert_info,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** An implementation of @c svn_auth_ssl_client_cert_prompt_func_t that
 * prompts the user for the filename of their SSL client certificate via
 * the command line.
 *
 * Records absolute path of the SSL client certificate file.
 *
 * @since New in 1.4.
 *
 * Expects a @c svn_cmdline_prompt_baton_t to be passed as @a baton.
 */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_prompt(
  svn_auth_cred_ssl_client_cert_t **cred_p,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** An implementation of @c svn_auth_ssl_client_cert_pw_prompt_func_t that
 * prompts the user for their SSL certificate password via the command line.
 *
 * @since New in 1.4.
 *
 * Expects a @c svn_cmdline_prompt_baton_t to be passed as @a baton.
 */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_pw_prompt(
  svn_auth_cred_ssl_client_cert_pw_t **cred_p,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool);

/** An implementation of @c svn_auth_plaintext_prompt_func_t that
 * prompts the user whether storing unencrypted passwords to disk is OK.
 *
 * Expects a @c svn_cmdline_prompt_baton2_t to be passed as @a baton.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_cmdline_auth_plaintext_prompt(svn_boolean_t *may_save_plaintext,
                                  const char *realmstring,
                                  void *baton,
                                  apr_pool_t *pool);

/** An implementation of @c svn_auth_plaintext_passphrase_prompt_func_t that
 * prompts the user whether storing unencrypted passphrase to disk is OK.
 *
 * Expects a @c svn_cmdline_prompt_baton2_t to be passed as @a baton.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_cmdline_auth_plaintext_passphrase_prompt(svn_boolean_t *may_save_plaintext,
                                             const char *realmstring,
                                             void *baton,
                                             apr_pool_t *pool);


/** Set @a *ab to an authentication baton allocated from @a pool and
 * initialized with the standard set of authentication providers used
 * by the command line client.
 *
 * @a non_interactive, @a username, @a password, @a config_dir,
 * and @a no_auth_cache are the values of the command line options
 * of the corresponding names.
 *
 * If @a non_interactive is @c TRUE, then the following parameters
 * control whether an invalid SSL certificate will be accepted
 * regardless of a specific verification failure:
 *
 * @a trust_server_cert_unknown_ca: If @c TRUE, accept certificates
 * from unknown certificate authorities.
 *
 * @a trust_server_cert_cn_mismatch: If @c TRUE, accept certificates
 * even if the Common Name attribute of the certificate differs from
 * the hostname of the server.
 *
 * @a trust_server_cert_expired: If @c TRUE, accept certificates even
 * if they are expired.
 *
 * @a trust_server_cert_not_yet_valid: If @c TRUE, accept certificates
 * from the future.
 *
 * @a trust_server_cert_other_failure: If @c TRUE, accept certificates
 * even if any other verification failure than the above occured.
 *
 * @a cfg is the @c SVN_CONFIG_CATEGORY_CONFIG configuration, and
 * @a cancel_func and @a cancel_baton control the cancellation of the
 * prompting providers that are initialized.
 *
 * Use @a pool for all allocations.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_cmdline_create_auth_baton2(svn_auth_baton_t **ab,
                               svn_boolean_t non_interactive,
                               const char *username,
                               const char *password,
                               const char *config_dir,
                               svn_boolean_t no_auth_cache,
                               svn_boolean_t trust_server_cert_unknown_ca,
                               svn_boolean_t trust_server_cert_cn_mismatch,
                               svn_boolean_t trust_server_cert_expired,
                               svn_boolean_t trust_server_cert_not_yet_valid,
                               svn_boolean_t trust_server_cert_other_failure,
                               svn_config_t *cfg,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *pool);

/* Like svn_cmdline_create_auth_baton2, but with only one trust_server_cert
 * option which corresponds to trust_server_cert_unknown_ca.
 *
 * @deprecated Provided for backward compatibility with the 1.8 API.
 * @since New in 1.6.
 */
SVN_DEPRECATED
svn_error_t *
svn_cmdline_create_auth_baton(svn_auth_baton_t **ab,
                              svn_boolean_t non_interactive,
                              const char *username,
                              const char *password,
                              const char *config_dir,
                              svn_boolean_t no_auth_cache,
                              svn_boolean_t trust_server_cert,
                              svn_config_t *cfg,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *pool);

/** Similar to svn_cmdline_create_auth_baton(), but with
 * @a trust_server_cert always set to false.
 *
 * @since New in 1.4.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 * Use svn_cmdline_create_auth_baton() instead.
 *
 * @note This deprecation does not follow the usual pattern of putting
 * a new number on end of the function's name.  Instead, the new
 * function name is distinguished from the old by a grammatical
 * improvement: the verb "create" instead of the noun "setup".
 */
SVN_DEPRECATED
svn_error_t *
svn_cmdline_setup_auth_baton(svn_auth_baton_t **ab,
                             svn_boolean_t non_interactive,
                             const char *username,
                             const char *password,
                             const char *config_dir,
                             svn_boolean_t no_auth_cache,
                             svn_config_t *cfg,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CMDLINE_H */
