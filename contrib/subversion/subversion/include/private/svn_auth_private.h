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
 * @file svn_auth_private.h
 * @brief Subversion's authentication system - Internal routines
 */

#ifndef SVN_AUTH_PRIVATE_H
#define SVN_AUTH_PRIVATE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** SSL server authority verification credential type.
 *
 * The followin auth parameters are available to the providers:
 *
 * - @c SVN_AUTH_PARAM_SSL_SERVER_FAILURES (@c apr_uint32_t*)
 * - @c SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO
 *      (@c svn_auth_ssl_server_cert_info_t*)
 *
 * The following optional auth parameters are relevant to the providers:
 *
 * - @c SVN_AUTH_PARAM_NO_AUTH_CACHE (@c void*)
 *
 * @since New in 1.9.
 */
#define SVN_AUTH_CRED_SSL_SERVER_AUTHORITY "svn.ssl.server.authority"



/* If you add a password type for a provider which stores
 * passwords on disk in encrypted form, remember to update
 * svn_auth__simple_save_creds_helper. Otherwise it will be
 * assumed that your provider stores passwords in plaintext. */
#define SVN_AUTH__SIMPLE_PASSWORD_TYPE             "simple"
#define SVN_AUTH__WINCRYPT_PASSWORD_TYPE           "wincrypt"
#define SVN_AUTH__KEYCHAIN_PASSWORD_TYPE           "keychain"
#define SVN_AUTH__KWALLET_PASSWORD_TYPE            "kwallet"
#define SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE      "gnome-keyring"
#define SVN_AUTH__GPG_AGENT_PASSWORD_TYPE          "gpg-agent"

/* A function that stores in *PASSWORD (potentially after decrypting it)
   the user's password.  It might be obtained directly from CREDS, or
   from an external store, using REALMSTRING and USERNAME as keys.
   (The behavior is undefined if REALMSTRING or USERNAME are NULL.)
   If NON_INTERACTIVE is set, the user must not be involved in the
   retrieval process.  Set *DONE to TRUE if a password was stored
   in *PASSWORD, to FALSE otherwise. POOL is used for any necessary
   allocation. */
typedef svn_error_t * (*svn_auth__password_get_t)
  (svn_boolean_t *done,
   const char **password,
   apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   apr_hash_t *parameters,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* A function that stores PASSWORD (or some encrypted version thereof)
   either directly in CREDS, or externally using REALMSTRING and USERNAME
   as keys into the external store.  If NON_INTERACTIVE is set, the user
   must not be involved in the storage process. Set *DONE to TRUE if the
   password was store, to FALSE otherwise. POOL is used for any necessary
   allocation. */
typedef svn_error_t * (*svn_auth__password_set_t)
  (svn_boolean_t *done,
   apr_hash_t *creds,
   const char *realmstring,
   const char *username,
   const char *password,
   apr_hash_t *parameters,
   svn_boolean_t non_interactive,
   apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to set *CREDENTIALS to a set of
   pre-cached authentication credentials pulled from the simple
   credential cache store identified by PASSTYPE.  PASSWORD_GET is
   used to obtain the password value.  Allocate *CREDENTIALS from
   POOL.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.), typically in
   their "first_creds" implementation.  */
svn_error_t *
svn_auth__simple_creds_cache_get(void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_get_t password_get,
                                 const char *passtype,
                                 apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to save CREDENTIALS in the simple
   credential cache store identified by PASSTYPE.  PASSWORD_SET is
   used to do the actual storage.  Use POOL for necessary allocations.
   Set *SAVED according to whether or not the credentials were
   successfully stored.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.) typically in
   their "save_creds" implementation.  */
svn_error_t *
svn_auth__simple_creds_cache_set(svn_boolean_t *saved,
                                 void *credentials,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_set_t password_set,
                                 const char *passtype,
                                 apr_pool_t *pool);

/* Implementation of svn_auth__password_get_t that retrieves
   the plaintext password from CREDS when USERNAME matches the stored
   credentials. */
svn_error_t *
svn_auth__simple_password_get(svn_boolean_t *done,
                              const char **password,
                              apr_hash_t *creds,
                              const char *realmstring,
                              const char *username,
                              apr_hash_t *parameters,
                              svn_boolean_t non_interactive,
                              apr_pool_t *pool);

/* Implementation of svn_auth__password_set_t that stores
   the plaintext password in CREDS. */
svn_error_t *
svn_auth__simple_password_set(svn_boolean_t *done,
                              apr_hash_t *creds,
                              const char *realmstring,
                              const char *username,
                              const char *password,
                              apr_hash_t *parameters,
                              svn_boolean_t non_interactive,
                              apr_pool_t *pool);


/* Use PARAMETERS and REALMSTRING to set *CREDENTIALS to a set of
   pre-cached authentication credentials pulled from the SSL client
   certificate passphrase credential cache store identified by
   PASSTYPE.  PASSPHRASE_GET is used to obtain the passphrase value.
   Allocate *CREDENTIALS from POOL.

   NOTE:  This function is a common implementation of code used by
   several of the ssl client passphrase credential providers (the
   default disk cache mechanism, Windows CryptoAPI, GNOME Keyring,
   etc.), typically in their "first_creds" implementation.  */
svn_error_t *
svn_auth__ssl_client_cert_pw_cache_get(void **credentials,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_get_t passphrase_get,
                                       const char *passtype,
                                       apr_pool_t *pool);

/* Use PARAMETERS and REALMSTRING to save CREDENTIALS in the SSL
   client certificate passphrase credential cache store identified by
   PASSTYPE.  PASSPHRASE_SET is used to do the actual storage.  Use
   POOL for necessary allocations.  Set *SAVED according to whether or
   not the credentials were successfully stored.

   NOTE:  This function is a common implementation of code used by
   several of the simple credential providers (the default disk cache
   mechanism, Windows CryptoAPI, GNOME Keyring, etc.) typically in
   their "save_creds" implementation.  */
svn_error_t *
svn_auth__ssl_client_cert_pw_cache_set(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_set_t passphrase_set,
                                       const char *passtype,
                                       apr_pool_t *pool);

/* This implements the svn_auth__password_get_t interface.
   Set **PASSPHRASE to the plaintext passphrase retrieved from CREDS;
   ignore other parameters. */
svn_error_t *
svn_auth__ssl_client_cert_pw_get(svn_boolean_t *done,
                                 const char **passphrase,
                                 apr_hash_t *creds,
                                 const char *realmstring,
                                 const char *username,
                                 apr_hash_t *parameters,
                                 svn_boolean_t non_interactive,
                                 apr_pool_t *pool);

/* This implements the svn_auth__password_set_t interface.
   Store PASSPHRASE in CREDS; ignore other parameters. */
svn_error_t *
svn_auth__ssl_client_cert_pw_set(svn_boolean_t *done,
                                 apr_hash_t *creds,
                                 const char *realmstring,
                                 const char *username,
                                 const char *passphrase,
                                 apr_hash_t *parameters,
                                 svn_boolean_t non_interactive,
                                 apr_pool_t *pool);

/* Apply the specified configuration for connecting with SERVER_NAME
   to the auth baton */
svn_error_t *
svn_auth__make_session_auth(svn_auth_baton_t **session_auth_baton,
                            const svn_auth_baton_t *auth_baton,
                            apr_hash_t *config,
                            const char *server_name,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

#if (defined(WIN32) && !defined(__MINGW32__)) || defined(DOXYGEN)
/**
 * Set @a *provider to an authentication provider that implements
 * ssl authority verification via the Windows CryptoApi.
 *
 * This provider automatically validates authority certificates with
 * the CryptoApi, like Internet Explorer and the Windows network API do.
 * This allows the rollout of root certificates via Windows Domain
 * policies, instead of Subversion specific configuration.
 *
 * @note This function is only available on Windows.
 */
void
svn_auth__get_windows_ssl_server_authority_provider(
                            svn_auth_provider_object_t **provider,
                            apr_pool_t *pool);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_AUTH_PRIVATE_H */
