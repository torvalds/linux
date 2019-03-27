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
 * @file svn_auth.h
 * @brief Subversion's authentication system
 */

#ifndef SVN_AUTH_H
#define SVN_AUTH_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Overview of the svn authentication system.
 *
 * We define an authentication "provider" as a module that is able to
 * return a specific set of credentials. (e.g. username/password,
 * certificate, etc.)  Each provider implements a vtable that
 *
 * - can fetch initial credentials
 * - can retry the fetch (or try to fetch something different)
 * - can store the credentials for future use
 *
 * For any given type of credentials, there can exist any number of
 * separate providers -- each provider has a different method of
 * fetching. (i.e. from a disk store, by prompting the user, etc.)
 *
 * The application begins by creating an auth baton object, and
 * "registers" some number of providers with the auth baton, in a
 * specific order.  (For example, it may first register a
 * username/password provider that looks in disk store, then register
 * a username/password provider that prompts the user.)
 *
 * Later on, when any svn library is challenged, it asks the auth
 * baton for the specific credentials.  If the initial credentials
 * fail to authenticate, the caller keeps requesting new credentials.
 * Under the hood, libsvn_auth effectively "walks" over each provider
 * (in order of registry), one at a time, until all the providers have
 * exhausted all their retry options.
 *
 * This system allows an application to flexibly define authentication
 * behaviors (by changing registration order), and very easily write
 * new authentication providers.
 *
 * An auth_baton also contains an internal hashtable of run-time
 * parameters; any provider or library layer can set these run-time
 * parameters at any time, so that the provider has access to the
 * data.  (For example, certain run-time data may not be available
 * until an authentication challenge is made.)  Each credential type
 * must document the run-time parameters that are made available to
 * its providers.
 *
 * @defgroup auth_fns Authentication functions
 * @{
 */


/** The type of a Subversion authentication object */
typedef struct svn_auth_baton_t svn_auth_baton_t;

/** The type of a Subversion authentication-iteration object */
typedef struct svn_auth_iterstate_t svn_auth_iterstate_t;


/** The main authentication "provider" vtable. */
typedef struct svn_auth_provider_t
{
  /** The kind of credentials this provider knows how to retrieve. */
  const char *cred_kind;

  /** Get an initial set of credentials.
   *
   * Set @a *credentials to a set of valid credentials within @a
   * realmstring, or NULL if no credentials are available.  Set @a
   * *iter_baton to context that allows a subsequent call to @c
   * next_credentials, in case the first credentials fail to
   * authenticate.  @a provider_baton is general context for the
   * vtable, @a parameters contains any run-time data that the
   * provider may need, and @a realmstring comes from the
   * svn_auth_first_credentials() call.
   */
  svn_error_t * (*first_credentials)(void **credentials,
                                     void **iter_baton,
                                     void *provider_baton,
                                     apr_hash_t *parameters,
                                     const char *realmstring,
                                     apr_pool_t *pool);

  /** Get a different set of credentials.
   *
   * Set @a *credentials to another set of valid credentials (using @a
   * iter_baton as the context from previous call to first_credentials
   * or next_credentials).  If no more credentials are available, set
   * @a *credentials to NULL.  If the provider only has one set of
   * credentials, this function pointer should simply be NULL. @a
   * provider_baton is general context for the vtable, @a parameters
   * contains any run-time data that the provider may need, and @a
   * realmstring comes from the svn_auth_first_credentials() call.
   */
  svn_error_t * (*next_credentials)(void **credentials,
                                    void *iter_baton,
                                    void *provider_baton,
                                    apr_hash_t *parameters,
                                    const char *realmstring,
                                    apr_pool_t *pool);

  /** Save credentials.
   *
   * Store @a credentials for future use.  @a provider_baton is
   * general context for the vtable, and @a parameters contains any
   * run-time data the provider may need.  Set @a *saved to TRUE if
   * the save happened, or FALSE if not.  The provider is not required
   * to save; if it refuses or is unable to save for non-fatal
   * reasons, return FALSE.  If the provider never saves data, then
   * this function pointer should simply be NULL. @a realmstring comes
   * from the svn_auth_first_credentials() call.
   */
  svn_error_t * (*save_credentials)(svn_boolean_t *saved,
                                    void *credentials,
                                    void *provider_baton,
                                    apr_hash_t *parameters,
                                    const char *realmstring,
                                    apr_pool_t *pool);

} svn_auth_provider_t;


/** A provider object, ready to be put into an array and given to
    svn_auth_open(). */
typedef struct svn_auth_provider_object_t
{
  const svn_auth_provider_t *vtable;
  void *provider_baton;

} svn_auth_provider_object_t;

/** The type of function returning authentication provider. */
typedef void (*svn_auth_simple_provider_func_t)(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Specific types of credentials **/

/** Simple username/password pair credential kind.
 *
 * The following auth parameters are available to the providers:
 *
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG (@c svn_config_t*)
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS (@c svn_config_t*)
 *
 * The following auth parameters may be available to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 * - @c SVN_AUTH_PARAM_DEFAULT_USERNAME (@c char*)
 * - @c SVN_AUTH_PARAM_DEFAULT_PASSWORD (@c char*)
 */
#define SVN_AUTH_CRED_SIMPLE "svn.simple"

/** @c SVN_AUTH_CRED_SIMPLE credentials. */
typedef struct svn_auth_cred_simple_t
{
  /** Username */
  const char *username;
  /** Password */
  const char *password;
  /** Indicates if the credentials may be saved (to disk). For example, a
   * GUI prompt implementation with a remember password checkbox shall set
   * @a may_save to TRUE if the checkbox is checked.
   */
  svn_boolean_t may_save;
} svn_auth_cred_simple_t;


/** Username credential kind.
 *
 * The following optional auth parameters are relevant to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 * - @c SVN_AUTH_PARAM_DEFAULT_USERNAME (@c char*)
 */
#define SVN_AUTH_CRED_USERNAME "svn.username"

/** @c SVN_AUTH_CRED_USERNAME credentials. */
typedef struct svn_auth_cred_username_t
{
  /** Username */
  const char *username;
  /** Indicates if the credentials may be saved (to disk). For example, a
   * GUI prompt implementation with a remember username checkbox shall set
   * @a may_save to TRUE if the checkbox is checked.
   */
  svn_boolean_t may_save;
} svn_auth_cred_username_t;


/** SSL client certificate credential type.
 *
 * The following auth parameters are available to the providers:
 *
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS (@c svn_config_t*)
 * - @c SVN_AUTH_PARAM_SERVER_GROUP (@c char*)
 *
 * The following optional auth parameters are relevant to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 */
#define SVN_AUTH_CRED_SSL_CLIENT_CERT "svn.ssl.client-cert"

/** @c SVN_AUTH_CRED_SSL_CLIENT_CERT credentials. */
typedef struct svn_auth_cred_ssl_client_cert_t
{
  /** Absolute path to the certificate file */
  const char *cert_file;
  /** Indicates if the credentials may be saved (to disk). For example, a
   * GUI prompt implementation with a remember certificate checkbox shall
   * set @a may_save to TRUE if the checkbox is checked.
   */
  svn_boolean_t may_save;
} svn_auth_cred_ssl_client_cert_t;


/** A function returning an SSL client certificate passphrase provider. */
typedef void (*svn_auth_ssl_client_cert_pw_provider_func_t)(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);

/** SSL client certificate passphrase credential type.
 *
 * @note The realmstring used with this credential type must be a name that
 * makes it possible for the user to identify the certificate.
 *
 * The following auth parameters are available to the providers:
 *
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG (@c svn_config_t*)
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS (@c svn_config_t*)
 * - @c SVN_AUTH_PARAM_SERVER_GROUP (@c char*)
 *
 * The following optional auth parameters are relevant to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 */
#define SVN_AUTH_CRED_SSL_CLIENT_CERT_PW "svn.ssl.client-passphrase"

/** @c SVN_AUTH_CRED_SSL_CLIENT_CERT_PW credentials. */
typedef struct svn_auth_cred_ssl_client_cert_pw_t
{
  /** Certificate password */
  const char *password;
  /** Indicates if the credentials may be saved (to disk). For example, a
   * GUI prompt implementation with a remember password checkbox shall set
   * @a may_save to TRUE if the checkbox is checked.
   */
  svn_boolean_t may_save;
} svn_auth_cred_ssl_client_cert_pw_t;


/** SSL server verification credential type.
 *
 * The following auth parameters are available to the providers:
 *
 * - @c SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS (@c svn_config_t*)
 * - @c SVN_AUTH_PARAM_SERVER_GROUP (@c char*)
 * - @c SVN_AUTH_PARAM_SSL_SERVER_FAILURES (@c apr_uint32_t*)
 * - @c SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO
 *      (@c svn_auth_ssl_server_cert_info_t*)
 *
 * The following optional auth parameters are relevant to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 */
#define SVN_AUTH_CRED_SSL_SERVER_TRUST "svn.ssl.server"

/** SSL server certificate information used by @c
 * SVN_AUTH_CRED_SSL_SERVER_TRUST providers.
 */
typedef struct svn_auth_ssl_server_cert_info_t
{
  /** Primary CN */
  const char *hostname;
  /** ASCII fingerprint */
  const char *fingerprint;
  /** ASCII date from which the certificate is valid */
  const char *valid_from;
  /** ASCII date until which the certificate is valid */
  const char *valid_until;
  /** DN of the certificate issuer */
  const char *issuer_dname;
  /** Base-64 encoded DER certificate representation */
  const char *ascii_cert;
} svn_auth_ssl_server_cert_info_t;

/**
 * Return a deep copy of @a info, allocated in @a pool.
 *
 * @since New in 1.3.
 */
svn_auth_ssl_server_cert_info_t *
svn_auth_ssl_server_cert_info_dup(const svn_auth_ssl_server_cert_info_t *info,
                                  apr_pool_t *pool);

/** @c SVN_AUTH_CRED_SSL_SERVER_TRUST credentials. */
typedef struct svn_auth_cred_ssl_server_trust_t
{
  /** Indicates if the credentials may be saved (to disk). For example, a
   * GUI prompt implementation with a checkbox to accept the certificate
   * permanently shall set @a may_save to TRUE if the checkbox is checked.
   */
  svn_boolean_t may_save;
  /** Bit mask of the accepted failures */
  apr_uint32_t accepted_failures;
} svn_auth_cred_ssl_server_trust_t;



/** Credential-constructing prompt functions. **/

/** These exist so that different client applications can use
 * different prompt mechanisms to supply the same credentials.  For
 * example, if authentication requires a username and password, a
 * command-line client's prompting function might prompt first for the
 * username and then for the password, whereas a GUI client's would
 * present a single dialog box asking for both, and a telepathic
 * client's would read all the information directly from the user's
 * mind.  All these prompting functions return the same type of
 * credential, but the information used to construct the credential is
 * gathered in an interface-specific way in each case.
 */

/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * If @a realm is non-NULL, maybe use it in the prompt string.
 *
 * If @a username is non-NULL, then the user might be prompted only
 * for a password, but @a *cred would still be filled with both
 * username and password.  For example, a typical usage would be to
 * pass @a username on the first call, but then leave it NULL for
 * subsequent calls, on the theory that if credentials failed, it's
 * as likely to be due to incorrect username as incorrect password.
 *
 * If @a may_save is FALSE, the auth system does not allow the credentials
 * to be saved (to disk). A prompt function shall not ask the user if the
 * credentials shall be saved if @a may_save is FALSE. For example, a GUI
 * client with a remember password checkbox would grey out the checkbox if
 * @a may_save is FALSE.
 */
typedef svn_error_t *(*svn_auth_simple_prompt_func_t)(
  svn_auth_cred_simple_t **cred,
  void *baton,
  const char *realm,
  const char *username,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * If @a realm is non-NULL, maybe use it in the prompt string.
 *
 * If @a may_save is FALSE, the auth system does not allow the credentials
 * to be saved (to disk). A prompt function shall not ask the user if the
 * credentials shall be saved if @a may_save is FALSE. For example, a GUI
 * client with a remember username checkbox would grey out the checkbox if
 * @a may_save is FALSE.
 */
typedef svn_error_t *(*svn_auth_username_prompt_func_t)(
  svn_auth_cred_username_t **cred,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** @name SSL server certificate failure bits
 *
 * @note These values are stored in the on disk auth cache by the SSL
 * server certificate auth provider, so the meaning of these bits must
 * not be changed.
 * @{
 */
/** Certificate is not yet valid. */
#define SVN_AUTH_SSL_NOTYETVALID 0x00000001
/** Certificate has expired. */
#define SVN_AUTH_SSL_EXPIRED     0x00000002
/** Certificate's CN (hostname) does not match the remote hostname. */
#define SVN_AUTH_SSL_CNMISMATCH  0x00000004
/** @brief Certificate authority is unknown (i.e. not trusted) */
#define SVN_AUTH_SSL_UNKNOWNCA   0x00000008
/** @brief Other failure. This can happen if an unknown failure occurs
 * that we do not handle yet. */
#define SVN_AUTH_SSL_OTHER       0x40000000
/** @} */

/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.
 *
 * @a cert_info is a structure describing the server cert that was
 * presented to the client, and @a failures is a bitmask that
 * describes exactly why the cert could not be automatically validated,
 * composed from the constants SVN_AUTH_SSL_* (@c SVN_AUTH_SSL_NOTYETVALID
 * etc.).  @a realm is a string that can be used in the prompt string.
 *
 * If @a may_save is FALSE, the auth system does not allow the credentials
 * to be saved (to disk). A prompt function shall not ask the user if the
 * credentials shall be saved if @a may_save is FALSE. For example, a GUI
 * client with a trust permanently checkbox would grey out the checkbox if
 * @a may_save is FALSE.
 */
typedef svn_error_t *(*svn_auth_ssl_server_trust_prompt_func_t)(
  svn_auth_cred_ssl_server_trust_t **cred,
  void *baton,
  const char *realm,
  apr_uint32_t failures,
  const svn_auth_ssl_server_cert_info_t *cert_info,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.  @a realm is a string
 * that can be used in the prompt string.
 *
 * If @a may_save is FALSE, the auth system does not allow the credentials
 * to be saved (to disk). A prompt function shall not ask the user if the
 * credentials shall be saved if @a may_save is FALSE. For example, a GUI
 * client with a remember certificate checkbox would grey out the checkbox
 * if @a may_save is FALSE.
 */
typedef svn_error_t *(*svn_auth_ssl_client_cert_prompt_func_t)(
  svn_auth_cred_ssl_client_cert_t **cred,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool);


/** Set @a *cred by prompting the user, allocating @a *cred in @a pool.
 * @a baton is an implementation-specific closure.  @a realm is a string
 * identifying the certificate, and can be used in the prompt string.
 *
 * If @a may_save is FALSE, the auth system does not allow the credentials
 * to be saved (to disk). A prompt function shall not ask the user if the
 * credentials shall be saved if @a may_save is FALSE. For example, a GUI
 * client with a remember password checkbox would grey out the checkbox if
 * @a may_save is FALSE.
 */
typedef svn_error_t *(*svn_auth_ssl_client_cert_pw_prompt_func_t)(
  svn_auth_cred_ssl_client_cert_pw_t **cred,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool);

/** A type of callback function for asking whether storing a password to
 * disk in plaintext is allowed.
 *
 * In this callback, the client should ask the user whether storing
 * a password for the realm identified by @a realmstring to disk
 * in plaintext is allowed.
 *
 * The answer is returned in @a *may_save_plaintext.
 * @a baton is an implementation-specific closure.
 * All allocations should be done in @a pool.
 *
 * @since New in 1.6
 */
typedef svn_error_t *(*svn_auth_plaintext_prompt_func_t)(
  svn_boolean_t *may_save_plaintext,
  const char *realmstring,
  void *baton,
  apr_pool_t *pool);

/** A type of callback function for asking whether storing a passphrase to
 * disk in plaintext is allowed.
 *
 * In this callback, the client should ask the user whether storing
 * a passphrase for the realm identified by @a realmstring to disk
 * in plaintext is allowed.
 *
 * The answer is returned in @a *may_save_plaintext.
 * @a baton is an implementation-specific closure.
 * All allocations should be done in @a pool.
 *
 * @since New in 1.6
 */
typedef svn_error_t *(*svn_auth_plaintext_passphrase_prompt_func_t)(
  svn_boolean_t *may_save_plaintext,
  const char *realmstring,
  void *baton,
  apr_pool_t *pool);


/** Initialize an authentication system.
 *
 * Return an authentication object in @a *auth_baton (allocated in @a
 * pool) that represents a particular instance of the svn
 * authentication system.  @a providers is an array of @c
 * svn_auth_provider_object_t pointers, already allocated in @a pool
 * and intentionally ordered.  These pointers will be stored within @a
 * *auth_baton, grouped by credential type, and searched in this exact
 * order.
 */
void
svn_auth_open(svn_auth_baton_t **auth_baton,
              const apr_array_header_t *providers,
              apr_pool_t *pool);

/** Set an authentication run-time parameter.
 *
 * Store @a name / @a value pair as a run-time parameter in @a
 * auth_baton, making the data accessible to all providers.  @a name
 * and @a value will NOT be duplicated into the auth_baton's pool.
 * To delete a run-time parameter, pass NULL for @a value.
 */
void
svn_auth_set_parameter(svn_auth_baton_t *auth_baton,
                       const char *name,
                       const void *value);

/** Get an authentication run-time parameter.
 *
 * Return a value for run-time parameter @a name from @a auth_baton.
 * Return NULL if the parameter doesn't exist.
 */
const void *
svn_auth_get_parameter(svn_auth_baton_t *auth_baton,
                       const char *name);

/** Universal run-time parameters, made available to all providers.

    If you are writing a new provider, then to be a "good citizen",
    you should notice these global parameters!  Note that these
    run-time params should be treated as read-only by providers; the
    application is responsible for placing them into the auth_baton
    hash. */

/** The auth-hash prefix indicating that the parameter is global. */
#define SVN_AUTH_PARAM_PREFIX "svn:auth:"

/**
 * @name Default credentials defines
 * Property values are const char *.
 * @{ */
/** Default username provided by the application itself (e.g. --username) */
#define SVN_AUTH_PARAM_DEFAULT_USERNAME  SVN_AUTH_PARAM_PREFIX "username"
/** Default password provided by the application itself (e.g. --password) */
#define SVN_AUTH_PARAM_DEFAULT_PASSWORD  SVN_AUTH_PARAM_PREFIX "password"
/** @} */

/** @brief The application doesn't want any providers to prompt
 * users. Property value is irrelevant; only property's existence
 * matters. */
#define SVN_AUTH_PARAM_NON_INTERACTIVE  SVN_AUTH_PARAM_PREFIX "non-interactive"

/** @brief The application doesn't want any providers to save passwords
 * to disk. Property value is irrelevant; only property's existence
 * matters. */
#define SVN_AUTH_PARAM_DONT_STORE_PASSWORDS  SVN_AUTH_PARAM_PREFIX \
                                                 "dont-store-passwords"

/** @brief Indicates whether providers may save passwords to disk in
 * plaintext. Property value can be either SVN_CONFIG_TRUE,
 * SVN_CONFIG_FALSE, or SVN_CONFIG_ASK.
 * @since New in 1.6.
 */
#define SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS  SVN_AUTH_PARAM_PREFIX \
                                                  "store-plaintext-passwords"

/** @brief The application doesn't want any providers to save passphrase
 * to disk. Property value is irrelevant; only property's existence
 * matters.
 * @since New in 1.6.
 */
#define SVN_AUTH_PARAM_DONT_STORE_SSL_CLIENT_CERT_PP \
  SVN_AUTH_PARAM_PREFIX "dont-store-ssl-client-cert-pp"

/** @brief Indicates whether providers may save passphrase to disk in
 * plaintext. Property value can be either SVN_CONFIG_TRUE,
 * SVN_CONFIG_FALSE, or SVN_CONFIG_ASK.
 * @since New in 1.6.
 */
#define SVN_AUTH_PARAM_STORE_SSL_CLIENT_CERT_PP_PLAINTEXT \
  SVN_AUTH_PARAM_PREFIX "store-ssl-client-cert-pp-plaintext"

/** @brief The application doesn't want any providers to save credentials
 * to disk. Property value is irrelevant; only property's existence
 * matters. */
#define SVN_AUTH_PARAM_NO_AUTH_CACHE  SVN_AUTH_PARAM_PREFIX "no-auth-cache"

/** @brief The following property is for SSL server cert providers. This
 * provides a pointer to an @c apr_uint32_t containing the failures
 * detected by the certificate validator. */
#define SVN_AUTH_PARAM_SSL_SERVER_FAILURES SVN_AUTH_PARAM_PREFIX \
  "ssl:failures"

/** @brief The following property is for SSL server cert providers. This
 * provides the cert info (svn_auth_ssl_server_cert_info_t). */
#define SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO SVN_AUTH_PARAM_PREFIX \
  "ssl:cert-info"

/** This provides a pointer to a @c svn_config_t containting the config
 * category. */
#define SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG SVN_AUTH_PARAM_PREFIX \
  "config-category-config"

/** This provides a pointer to a @c svn_config_t containting the servers
 * category. */
#define SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS SVN_AUTH_PARAM_PREFIX \
  "config-category-servers"

/** @deprecated Provided for backward compatibility with the 1.5 API. */
#define SVN_AUTH_PARAM_CONFIG SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS

/** The current server group. */
#define SVN_AUTH_PARAM_SERVER_GROUP SVN_AUTH_PARAM_PREFIX "server-group"

/** @brief A configuration directory that overrides the default
 * ~/.subversion. */
#define SVN_AUTH_PARAM_CONFIG_DIR SVN_AUTH_PARAM_PREFIX "config-dir"

/** Get an initial set of credentials.
 *
 * Ask @a auth_baton to set @a *credentials to a set of credentials
 * defined by @a cred_kind and valid within @a realmstring, or NULL if
 * no credentials are available.  Otherwise, return an iteration state
 * in @a *state, so that the caller can call
 * svn_auth_next_credentials(), in case the first set of credentials
 * fails to authenticate.
 *
 * Use @a pool to allocate @a *state, and for temporary allocation.
 * Note that @a *credentials will be allocated in @a auth_baton's pool.
 */
svn_error_t *
svn_auth_first_credentials(void **credentials,
                           svn_auth_iterstate_t **state,
                           const char *cred_kind,
                           const char *realmstring,
                           svn_auth_baton_t *auth_baton,
                           apr_pool_t *pool);

/** Get another set of credentials, assuming previous ones failed to
 * authenticate.
 *
 * Use @a state to fetch a different set of @a *credentials, as a
 * follow-up to svn_auth_first_credentials() or
 * svn_auth_next_credentials().  If no more credentials are available,
 * set @a *credentials to NULL.
 *
 * Note that @a *credentials will be allocated in @c auth_baton's pool.
 */
svn_error_t *
svn_auth_next_credentials(void **credentials,
                          svn_auth_iterstate_t *state,
                          apr_pool_t *pool);

/** Save a set of credentials.
 *
 * Ask @a state to store the most recently returned credentials,
 * presumably because they successfully authenticated.
 * All allocations should be done in @a pool.
 *
 * If no credentials were ever returned, do nothing.
 */
svn_error_t *
svn_auth_save_credentials(svn_auth_iterstate_t *state,
                          apr_pool_t *pool);

/** Forget a set (or all) memory-cached credentials.
 *
 * Remove references (if any) in @a auth_baton to credentials cached
 * therein.  If @a cred_kind and @a realmstring are non-NULL, forget
 * only the credentials associated with those credential types and
 * realm.  Otherwise @a cred_kind and @a realmstring must both be
 * NULL, and this function will forget all credentials cached within
 * @a auth_baton.
 *
 * @note This function does not affect persisted authentication
 * credential storage at all.  It is merely a way to cause Subversion
 * to forget about credentials already fetched from a provider,
 * forcing them to be fetched again later should they be required.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_auth_forget_credentials(svn_auth_baton_t *auth_baton,
                            const char *cred_kind,
                            const char *realmstring,
                            apr_pool_t *pool);

/** @} */

/** Set @a *provider to an authentication provider of type
 * svn_auth_cred_simple_t that gets information by prompting the user
 * with @a prompt_func and @a prompt_baton.  Allocate @a *provider in
 * @a pool.
 *
 * If both @c SVN_AUTH_PARAM_DEFAULT_USERNAME and
 * @c SVN_AUTH_PARAM_DEFAULT_PASSWORD are defined as runtime
 * parameters in the @c auth_baton, then @a *provider will return the
 * default arguments when svn_auth_first_credentials() is called.  If
 * svn_auth_first_credentials() fails, then @a *provider will
 * re-prompt @a retry_limit times (via svn_auth_next_credentials()).
 * For infinite retries, set @a retry_limit to value less than 0.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_simple_prompt_provider(svn_auth_provider_object_t **provider,
                                    svn_auth_simple_prompt_func_t prompt_func,
                                    void *prompt_baton,
                                    int retry_limit,
                                    apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_username_t that gets information by prompting the
 * user with @a prompt_func and @a prompt_baton.  Allocate @a *provider
 * in @a pool.
 *
 * If @c SVN_AUTH_PARAM_DEFAULT_USERNAME is defined as a runtime
 * parameter in the @c auth_baton, then @a *provider will return the
 * default argument when svn_auth_first_credentials() is called.  If
 * svn_auth_first_credentials() fails, then @a *provider will
 * re-prompt @a retry_limit times (via svn_auth_next_credentials()).
 * For infinite retries, set @a retry_limit to value less than 0.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_username_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_username_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.
 *
 * If the provider is going to save the password unencrypted, it calls @a
 * plaintext_prompt_func, passing @a prompt_baton, before saving the
 * password.
 *
 * If @a plaintext_prompt_func is NULL it is not called and the answer is
 * assumed to be TRUE. This matches the deprecated behaviour of storing
 * unencrypted passwords by default, and is only done this way for backward
 * compatibility reasons.
 * Client developers are highly encouraged to provide this callback
 * to ensure their users are made aware of the fact that their password
 * is going to be stored unencrypted. In the future, providers may
 * default to not storing the password unencrypted if this callback is NULL.
 *
 * Clients can however set the callback to NULL and set
 * SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS to SVN_CONFIG_FALSE or
 * SVN_CONFIG_TRUE to enforce a certain behaviour.
 *
 * Allocate @a *provider in @a pool.
 *
 * If a default username or password is available, @a *provider will
 * honor them as well, and return them when
 * svn_auth_first_credentials() is called.  (see @c
 * SVN_AUTH_PARAM_DEFAULT_USERNAME and @c
 * SVN_AUTH_PARAM_DEFAULT_PASSWORD).
 *
 * @since New in 1.6.
 */
void
svn_auth_get_simple_provider2(
  svn_auth_provider_object_t **provider,
  svn_auth_plaintext_prompt_func_t plaintext_prompt_func,
  void *prompt_baton,
  apr_pool_t *pool);

/** Like svn_auth_get_simple_provider2, but without the ability to
 * call the svn_auth_plaintext_prompt_func_t callback, and the provider
 * always assumes that it is allowed to store the password in plaintext.
 *
 * @deprecated Provided for backwards compatibility with the 1.5 API.
 * @since New in 1.4.
 */
SVN_DEPRECATED
void
svn_auth_get_simple_provider(svn_auth_provider_object_t **provider,
                             apr_pool_t *pool);

/** Set @a *provider to an authentication provider of type @c
 * svn_auth_provider_object_t, or return @c NULL if the provider is not
 * available for the requested platform or the requested provider is unknown.
 *
 * Valid @a provider_name values are: "gnome_keyring", "keychain", "kwallet",
 * "gpg_agent", and "windows".
 *
 * Valid @a provider_type values are: "simple", "ssl_client_cert_pw" and
 * "ssl_server_trust".
 *
 * Allocate @a *provider in @a pool.
 *
 * What actually happens is we invoke the appropriate provider function to
 * supply the @a provider, like so:
 *
 *    svn_auth_get_<name>_<type>_provider(@a provider, @a pool);
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_auth_get_platform_specific_provider(
  svn_auth_provider_object_t **provider,
  const char *provider_name,
  const char *provider_type,
  apr_pool_t *pool);

/** Set @a *providers to an array of <tt>svn_auth_provider_object_t *</tt>
 * objects.
 * Only client authentication providers available for the current platform are
 * returned. Order of the platform-specific authentication providers is
 * determined by the 'password-stores' configuration option which is retrieved
 * from @a config. @a config can be NULL.
 *
 * Create and allocate @a *providers in @a pool.
 *
 * Default order of the platform-specific authentication providers:
 *   1. gnome-keyring
 *   2. kwallet
 *   3. keychain
 *   4. gpg-agent
 *   5. windows-cryptoapi
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_auth_get_platform_specific_client_providers(
  apr_array_header_t **providers,
  svn_config_t *config,
  apr_pool_t *pool);

#if (defined(WIN32) && !defined(__MINGW32__)) || defined(DOXYGEN)
/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_auth_get_simple_provider(), except that, when
 * running on Window 2000 or newer (or any other Windows version that
 * includes the CryptoAPI), the provider encrypts the password before
 * storing it to disk. On earlier versions of Windows, the provider
 * does nothing.
 *
 * @since New in 1.4.
 * @note This function is only available on Windows.
 *
 * @note An administrative password reset may invalidate the account's
 * secret key. This function will detect that situation and behave as
 * if the password were not cached at all.
 * @deprecated Provided for backwards compatibility with the 1.8 API.  Use
 * svn_auth_get_platform_specific_provider with provider_name of "windows"
 * and provider_type of "simple".
 */
SVN_DEPRECATED
void
svn_auth_get_windows_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool);

/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t that gets/sets information from the
 * user's ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_auth_get_ssl_client_cert_pw_file_provider(), except that
 * when running on Window 2000 or newer, the provider encrypts the password
 * before storing it to disk. On earlier versions of Windows, the provider
 * does nothing.
 *
 * @since New in 1.6
 * @note This function is only available on Windows.
 *
 * @note An administrative password reset may invalidate the account's
 * secret key. This function will detect that situation and behave as
 * if the password were not cached at all.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "windows" and provider_type of "ssl_client_cert_pw".
 */
SVN_DEPRECATED
void
svn_auth_get_windows_ssl_client_cert_pw_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);

/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_server_trust_t, allocated in @a pool.
 *
 * This provider automatically validates ssl server certificates with
 * the CryptoApi, like Internet Explorer and the Windows network API do.
 * This allows the rollout of root certificates via Windows Domain
 * policies, instead of Subversion specific configuration.
 *
 * @since New in 1.5.
 * @note This function is only available on Windows.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "windows" and provider_type of "ssl_server_trust".
 */
SVN_DEPRECATED
void
svn_auth_get_windows_ssl_server_trust_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);

#endif /* WIN32 && !__MINGW32__ || DOXYGEN */

#if defined(DARWIN) || defined(DOXYGEN)
/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_auth_get_simple_provider(), except that the
 * password is stored in the Mac OS KeyChain.
 *
 * @since New in 1.4
 * @note This function is only available on Mac OS 10.2 and higher.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "keychain" and provider_type of "simple".
 */
SVN_DEPRECATED
void
svn_auth_get_keychain_simple_provider(svn_auth_provider_object_t **provider,
                                      apr_pool_t *pool);

/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t that gets/sets information from the
 * user's ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_auth_get_ssl_client_cert_pw_file_provider(), except
 * that the password is stored in the Mac OS KeyChain.
 *
 * @since New in 1.6
 * @note This function is only available on Mac OS 10.2 and higher.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "keychain" and provider_type of "ssl_client_cert_pw".
 */
SVN_DEPRECATED
void
svn_auth_get_keychain_ssl_client_cert_pw_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);
#endif /* DARWIN || DOXYGEN */

/* Note that the gnome keyring unlock prompt related items below must be
 * declared for all platforms in order to allow SWIG interfaces to be
 * used regardless of the platform. */

/** A type of callback function for obtaining the GNOME Keyring password.
 *
 * In this callback, the client should ask the user for default keyring
 * @a keyring_name password.
 *
 * The answer is returned in @a *keyring_password.
 * @a baton is an implementation-specific closure.
 * All allocations should be done in @a pool.
 *
 * @since New in 1.6
 */
typedef svn_error_t *(*svn_auth_gnome_keyring_unlock_prompt_func_t)(
  char **keyring_password,
  const char *keyring_name,
  void *baton,
  apr_pool_t *pool);


/** libsvn_auth_gnome_keyring-specific run-time parameters. */

/** @brief The pointer to function which prompts user for GNOME Keyring
 * password.
 * The type of this pointer should be svn_auth_gnome_keyring_unlock_prompt_func_t.
 * @deprecated Only used by old libgnome-keyring implementation. */
#define SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC "gnome-keyring-unlock-prompt-func"

/** @brief The baton which is passed to
 * @c *SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC.
 * @deprecated Only used by old libgnome-keyring implementation. */
#define SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_BATON "gnome-keyring-unlock-prompt-baton"

#if (!defined(DARWIN) && !defined(WIN32)) || defined(DOXYGEN)
/**
 * Get libsvn_auth_gnome_keyring version information.
 *
 * @since New in 1.6
 */
const svn_version_t *
svn_auth_gnome_keyring_version(void);


/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.
 *
 * This is like svn_client_get_simple_provider(), except that the
 * password is stored in GNOME Keyring.
 *
 * If the GNOME Keyring is locked the old libgnome-keyring provider calls
 * @c *SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC in order to unlock
 * the keyring.
 *
 * @c SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_BATON is passed to
 * @c *SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC.
 *
 * Allocate @a *provider in @a pool.
 *
 * @since New in 1.6
 * @note This function actually works only on systems with
 * libsvn_auth_gnome_keyring and GNOME Keyring installed.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "gnome_keyring" and provider_type of "simple".
 */
SVN_DEPRECATED
void
svn_auth_get_gnome_keyring_simple_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t that gets/sets information from the
 * user's ~/.subversion configuration directory.
 *
 * This is like svn_client_get_ssl_client_cert_pw_file_provider(), except
 * that the password is stored in GNOME Keyring.
 *
 * If the GNOME Keyring is locked the provider calls
 * @c *SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC in order to unlock
 * the keyring.
 *
 * @c SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_BATON is passed to
 * @c *SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC.
 *
 * Allocate @a *provider in @a pool.
 *
 * @since New in 1.6
 * @note This function actually works only on systems with
 * libsvn_auth_gnome_keyring and GNOME Keyring installed.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "gnome_keyring" and provider_type of "ssl_client_cert_pw".
 */
SVN_DEPRECATED
void
svn_auth_get_gnome_keyring_ssl_client_cert_pw_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/**
 * Get libsvn_auth_kwallet version information.
 *
 * @since New in 1.6
 */
const svn_version_t *
svn_auth_kwallet_version(void);


/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_client_get_simple_provider(), except that the
 * password is stored in KWallet.
 *
 * @since New in 1.6
 * @note This function actually works only on systems with libsvn_auth_kwallet
 * and KWallet installed.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "kwallet" and provider_type of "simple".
 */
SVN_DEPRECATED
void
svn_auth_get_kwallet_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool);


/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t that gets/sets information from the
 * user's ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * This is like svn_client_get_ssl_client_cert_pw_file_provider(), except
 * that the password is stored in KWallet.
 *
 * @since New in 1.6
 * @note This function actually works only on systems with libsvn_auth_kwallet
 * and KWallet installed.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "kwallet" and provider_type of "ssl_client_cert_pw".
 */
SVN_DEPRECATED
void
svn_auth_get_kwallet_ssl_client_cert_pw_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);
#endif /* (!DARWIN && !WIN32) || DOXYGEN */

#if !defined(WIN32) || defined(DOXYGEN)
/**
 * Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_simple_t that gets/sets information from the user's
 * ~/.subversion configuration directory.
 *
 * This is like svn_client_get_simple_provider(), except that the
 * password is obtained from gpg_agent, which will keep it in
 * a memory cache.
 *
 * Allocate @a *provider in @a pool.
 *
 * @since New in 1.8
 * @note This function actually works only on systems with
 * GNU Privacy Guard installed.
 * @deprecated Provided for backwards compatibility with the 1.8 API.
 * Use svn_auth_get_platform_specific_provider with provider_name
 * of "gpg_agent" and provider_type of "simple".
 */
SVN_DEPRECATED
void
svn_auth_get_gpg_agent_simple_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool);
#endif /* !defined(WIN32) || defined(DOXYGEN) */


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_username_t that gets/sets information from a user's
 * ~/.subversion configuration directory.  Allocate @a *provider in
 * @a pool.
 *
 * If a default username is available, @a *provider will honor it,
 * and return it when svn_auth_first_credentials() is called.  (See
 * @c SVN_AUTH_PARAM_DEFAULT_USERNAME.)
 *
 * @since New in 1.4.
 */
void
svn_auth_get_username_provider(svn_auth_provider_object_t **provider,
                               apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_server_trust_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials from the configuration
 * mechanism.  The returned credential is used to override SSL
 * security on an error.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_ssl_server_trust_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);

/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials from the configuration
 * mechanism.  The returned credential is used to load the appropriate
 * client certificate for authentication when requested by a server.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_ssl_client_cert_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t that gets/sets information from the user's
 * ~/.subversion configuration directory.
 *
 * If the provider is going to save the passphrase unencrypted,
 * it calls @a plaintext_passphrase_prompt_func, passing @a
 * prompt_baton, before saving the passphrase.
 *
 * If @a plaintext_passphrase_prompt_func is NULL it is not called
 * and the passphrase is not stored in plaintext.
 * Client developers are highly encouraged to provide this callback
 * to ensure their users are made aware of the fact that their passphrase
 * is going to be stored unencrypted.
 *
 * Clients can however set the callback to NULL and set
 * SVN_AUTH_PARAM_STORE_SSL_CLIENT_CERT_PP_PLAINTEXT to SVN_CONFIG_FALSE or
 * SVN_CONFIG_TRUE to enforce a certain behaviour.
 *
 * Allocate @a *provider in @a pool.
 *
 * @since New in 1.6.
 */
void
svn_auth_get_ssl_client_cert_pw_file_provider2(
  svn_auth_provider_object_t **provider,
  svn_auth_plaintext_passphrase_prompt_func_t plaintext_passphrase_prompt_func,
  void *prompt_baton,
  apr_pool_t *pool);

/** Like svn_auth_get_ssl_client_cert_pw_file_provider2, but without
 * the ability to call the svn_auth_plaintext_passphrase_prompt_func_t
 * callback, and the provider always assumes that it is not allowed
 * to store the passphrase in plaintext.
 *
 * @deprecated Provided for backwards compatibility with the 1.5 API.
 * @since New in 1.4.
 */
SVN_DEPRECATED
void
svn_auth_get_ssl_client_cert_pw_file_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_server_trust_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used to override
 * SSL security on an error.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_ssl_server_trust_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_server_trust_prompt_func_t prompt_func,
  void *prompt_baton,
  apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used to load the
 * appropriate client certificate for authentication when requested by
 * a server.  The prompt will be retried @a retry_limit times. For
 * infinite retries, set @a retry_limit to value less than 0.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_ssl_client_cert_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_client_cert_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


/** Set @a *provider to an authentication provider of type @c
 * svn_auth_cred_ssl_client_cert_pw_t, allocated in @a pool.
 *
 * @a *provider retrieves its credentials by using the @a prompt_func
 * and @a prompt_baton.  The returned credential is used when a loaded
 * client certificate is protected by a passphrase.  The prompt will
 * be retried @a retry_limit times. For infinite retries, set
 * @a retry_limit to value less than 0.
 *
 * @since New in 1.4.
 */
void
svn_auth_get_ssl_client_cert_pw_prompt_provider(
  svn_auth_provider_object_t **provider,
  svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
  void *prompt_baton,
  int retry_limit,
  apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_H */
