/*
 * auth.h :  shared stuff internal to the subr library.
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

#ifndef SVN_LIBSVN_SUBR_AUTH_H
#define SVN_LIBSVN_SUBR_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "svn_auth.h"

/* Helper for svn_config_{read|write}_auth_data.  Return a path to a
   file within ~/.subversion/auth/ that holds CRED_KIND credentials
   within REALMSTRING.  If no path is available *PATH will be set to
   NULL. */
svn_error_t *
svn_auth__file_path(const char **path,
                    const char *cred_kind,
                    const char *realmstring,
                    const char *config_dir,
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
 * @note This function is only available on Windows.
 *
 * @note An administrative password reset may invalidate the account's
 * secret key. This function will detect that situation and behave as
 * if the password were not cached at all.
 */
void
svn_auth__get_windows_simple_provider(svn_auth_provider_object_t **provider,
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
 * @note This function is only available on Windows.
 *
 * @note An administrative password reset may invalidate the account's
 * secret key. This function will detect that situation and behave as
 * if the password were not cached at all.
 */
void
svn_auth__get_windows_ssl_client_cert_pw_provider(
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
 * @note This function is only available on Windows.
 */
void
svn_auth__get_windows_ssl_server_trust_provider(
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
 * @note This function is only available on Mac OS 10.2 and higher.
 */
void
svn_auth__get_keychain_simple_provider(svn_auth_provider_object_t **provider,
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
 * @note This function is only available on Mac OS 10.2 and higher.
 */
void
svn_auth__get_keychain_ssl_client_cert_pw_provider(
  svn_auth_provider_object_t **provider,
  apr_pool_t *pool);
#endif /* DARWIN || DOXYGEN */

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
 * @note This function actually works only on systems with
 * GNU Privacy Guard installed.
 */
void
svn_auth__get_gpg_agent_simple_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool);
#endif /* !defined(WIN32) || defined(DOXYGEN) */

/**
 * Set @a *provider to a dummy provider of type @c
 * svn_auth_cred_simple_t that never returns or stores any
 * credentials.
 */
void
svn_auth__get_dummmy_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_AUTH_H */
