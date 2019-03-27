/*
 * kwallet.cpp: KWallet provider for SVN_AUTH_CRED_*
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

/* ==================================================================== */



/*** Includes. ***/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include <dbus/dbus.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>

#include <kaboutdata.h>
#include <klocalizedstring.h>
#include <kwallet.h>

#include "svn_auth.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_version.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#ifndef SVN_HAVE_KF5
#include <kcmdlineargs.h>
#include <kcomponentdata.h>
#endif

/*-----------------------------------------------------------------------*/
/* KWallet simple provider, puts passwords in KWallet                    */
/*-----------------------------------------------------------------------*/

static int q_argc = 1;
static char q_argv0[] = "svn"; // Build non-const char * from string constant
static char *q_argv[] = { q_argv0 };

static const char *
get_application_name(apr_hash_t *parameters,
                     apr_pool_t *pool)
{
  svn_config_t *config =
    static_cast<svn_config_t *> (apr_hash_get(parameters,
                                              SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG,
                                              APR_HASH_KEY_STRING));
  svn_boolean_t svn_application_name_with_pid;
  svn_config_get_bool(config,
                      &svn_application_name_with_pid,
                      SVN_CONFIG_SECTION_AUTH,
                      SVN_CONFIG_OPTION_KWALLET_SVN_APPLICATION_NAME_WITH_PID,
                      FALSE);
  const char *svn_application_name;
  if (svn_application_name_with_pid)
    {
      svn_application_name = apr_psprintf(pool, "Subversion [%ld]", long(getpid()));
    }
  else
    {
      svn_application_name = "Subversion";
    }
  return svn_application_name;
}

static QString
get_wallet_name(apr_hash_t *parameters)
{
  svn_config_t *config =
    static_cast<svn_config_t *> (apr_hash_get(parameters,
                                              SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG,
                                              APR_HASH_KEY_STRING));
  const char *wallet_name;
  svn_config_get(config,
                 &wallet_name,
                 SVN_CONFIG_SECTION_AUTH,
                 SVN_CONFIG_OPTION_KWALLET_WALLET,
                 "");
  if (strcmp(wallet_name, "") == 0)
    {
      return KWallet::Wallet::NetworkWallet();
    }
  else
    {
      return QString::fromUtf8(wallet_name);
    }
}

static WId
get_wid(void)
{
  WId wid = 1;
  const char *wid_env_string = getenv("WINDOWID");

  if (wid_env_string)
    {
      apr_int64_t wid_env;
      svn_error_t *err;

      err = svn_cstring_atoi64(&wid_env, wid_env_string);
      if (err)
        svn_error_clear(err);
      else
        wid = (WId)wid_env;
    }

  return wid;
}

/* Forward definition */
static apr_status_t
kwallet_terminate(void *data);

static KWallet::Wallet *
get_wallet(QString wallet_name,
           apr_hash_t *parameters)
{
  KWallet::Wallet *wallet =
    static_cast<KWallet::Wallet *> (svn_hash_gets(parameters,
                                                  "kwallet-wallet"));
  if (! wallet && ! svn_hash_gets(parameters, "kwallet-opening-failed"))
    {
      wallet = KWallet::Wallet::openWallet(wallet_name, get_wid(),
                                           KWallet::Wallet::Synchronous);

      if (wallet)
        {
          svn_hash_sets(parameters, "kwallet-wallet", wallet);

          apr_pool_cleanup_register(apr_hash_pool_get(parameters),
                                    parameters, kwallet_terminate,
                                    apr_pool_cleanup_null);

          svn_hash_sets(parameters, "kwallet-initialized", "");
        }
      else
        {
          svn_hash_sets(parameters, "kwallet-opening-failed", "");
        }
    }
  return wallet;
}

static apr_status_t
kwallet_terminate(void *data)
{
  apr_hash_t *parameters = static_cast<apr_hash_t *> (data);
  if (svn_hash_gets(parameters, "kwallet-initialized"))
    {
      KWallet::Wallet *wallet = get_wallet(NULL, parameters);
      delete wallet;
      svn_hash_sets(parameters, "kwallet-wallet", NULL);
      svn_hash_sets(parameters, "kwallet-initialized", NULL);
    }
  return APR_SUCCESS;
}

/* Implementation of svn_auth__password_get_t that retrieves
   the password from KWallet. */
static svn_error_t *
kwallet_password_get(svn_boolean_t *done,
                     const char **password,
                     apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     apr_hash_t *parameters,
                     svn_boolean_t non_interactive,
                     apr_pool_t *pool)
{
  QString wallet_name = get_wallet_name(parameters);

  *done = FALSE;

  if (! dbus_bus_get(DBUS_BUS_SESSION, NULL))
    {
      return SVN_NO_ERROR;
    }

  if (non_interactive)
    {
      if (!KWallet::Wallet::isOpen(wallet_name))
        return SVN_NO_ERROR;

      /* There is a race here: the wallet was open just now, but will
         it still be open when we come to use it below? */
    }

  QCoreApplication *app;
  if (! qApp)
    {
      int argc = q_argc;
      app = new QCoreApplication(argc, q_argv);
    }

#if SVN_HAVE_KF5
  KLocalizedString::setApplicationDomain("subversion"); /* translation domain */

  /* componentName appears in KDE GUI prompts */
  KAboutData aboutData(QStringLiteral("subversion"),     /* componentName */
                       i18n(get_application_name(parameters,
                                                 pool)), /* displayName */
                       QStringLiteral(SVN_VER_NUMBER));
  KAboutData::setApplicationData(aboutData);
#else
  KCmdLineArgs::init(q_argc, q_argv,
                     get_application_name(parameters, pool),
                     "subversion",
                     ki18n(get_application_name(parameters, pool)),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KComponentData component_data(KCmdLineArgs::aboutData());
#endif

  QString folder = QString::fromUtf8("Subversion");
  QString key =
    QString::fromUtf8(username) + "@" + QString::fromUtf8(realmstring);
  if (! KWallet::Wallet::keyDoesNotExist(wallet_name, folder, key))
    {
      KWallet::Wallet *wallet = get_wallet(wallet_name, parameters);
      if (wallet)
        {
          if (wallet->setFolder(folder))
            {
              QString q_password;
              if (wallet->readPassword(key, q_password) == 0)
                {
                  *password = apr_pstrmemdup(pool,
                                             q_password.toUtf8().data(),
                                             q_password.size());
                  *done = TRUE;
                }
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_set_t that stores
   the password in KWallet. */
static svn_error_t *
kwallet_password_set(svn_boolean_t *done,
                     apr_hash_t *creds,
                     const char *realmstring,
                     const char *username,
                     const char *password,
                     apr_hash_t *parameters,
                     svn_boolean_t non_interactive,
                     apr_pool_t *pool)
{
  QString wallet_name = get_wallet_name(parameters);

  *done = FALSE;

  if (! dbus_bus_get(DBUS_BUS_SESSION, NULL))
    {
      return SVN_NO_ERROR;
    }

  if (non_interactive)
    {
      if (!KWallet::Wallet::isOpen(wallet_name))
        return SVN_NO_ERROR;

      /* There is a race here: the wallet was open just now, but will
         it still be open when we come to use it below? */
    }

  QCoreApplication *app;
  if (! qApp)
    {
      int argc = q_argc;
      app = new QCoreApplication(argc, q_argv);
    }

#if SVN_HAVE_KF5
  KLocalizedString::setApplicationDomain("subversion"); /* translation domain */

  /* componentName appears in KDE GUI prompts */
  KAboutData aboutData(QStringLiteral("subversion"),     /* componentName */
                       i18n(get_application_name(parameters,
                                                 pool)), /* displayName */
                       QStringLiteral(SVN_VER_NUMBER));
  KAboutData::setApplicationData(aboutData);
#else
  KCmdLineArgs::init(q_argc, q_argv,
                     get_application_name(parameters, pool),
                     "subversion",
                     ki18n(get_application_name(parameters, pool)),
                     SVN_VER_NUMBER,
                     ki18n("Version control system"),
                     KCmdLineArgs::CmdLineArgKDE);
  KComponentData component_data(KCmdLineArgs::aboutData());
#endif

  QString q_password = QString::fromUtf8(password);
  QString folder = QString::fromUtf8("Subversion");
  KWallet::Wallet *wallet = get_wallet(wallet_name, parameters);
  if (wallet)
    {
      if (! wallet->hasFolder(folder))
        {
          wallet->createFolder(folder);
        }
      if (wallet->setFolder(folder))
        {
          QString key = QString::fromUtf8(username) + "@"
            + QString::fromUtf8(realmstring);
          if (wallet->writePassword(key, q_password) == 0)
            {
              *done = TRUE;
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
kwallet_simple_first_creds(void **credentials,
                           void **iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_get(credentials,
                                          iter_baton,
                                          provider_baton,
                                          parameters,
                                          realmstring,
                                          kwallet_password_get,
                                          SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                          pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
kwallet_simple_save_creds(svn_boolean_t *saved,
                          void *credentials,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_set(saved, credentials,
                                          provider_baton,
                                          parameters,
                                          realmstring,
                                          kwallet_password_set,
                                          SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                          pool);
}

static const svn_auth_provider_t kwallet_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  kwallet_simple_first_creds,
  NULL,
  kwallet_simple_save_creds
};

/* Public API */
extern "C" {
void
svn_auth_get_kwallet_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po =
    static_cast<svn_auth_provider_object_t *> (apr_pcalloc(pool, sizeof(*po)));

  po->vtable = &kwallet_simple_provider;
  *provider = po;
}
}


/*-----------------------------------------------------------------------*/
/* KWallet SSL client certificate passphrase provider,                   */
/* puts passphrases in KWallet                                           */
/*-----------------------------------------------------------------------*/

/* Get cached encrypted credentials from the ssl client cert password
   provider's cache. */
static svn_error_t *
kwallet_ssl_client_cert_pw_first_creds(void **credentials,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_get(credentials,
                                                iter_baton, provider_baton,
                                                parameters, realmstring,
                                                kwallet_password_get,
                                                SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                                pool);
}

/* Save encrypted credentials to the ssl client cert password provider's
   cache. */
static svn_error_t *
kwallet_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
                                      void *credentials,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_set(saved, credentials,
                                                provider_baton, parameters,
                                                realmstring,
                                                kwallet_password_set,
                                                SVN_AUTH__KWALLET_PASSWORD_TYPE,
                                                pool);
}

static const svn_auth_provider_t kwallet_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  kwallet_ssl_client_cert_pw_first_creds,
  NULL,
  kwallet_ssl_client_cert_pw_save_creds
};

/* Public API */
extern "C" {
void
svn_auth_get_kwallet_ssl_client_cert_pw_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po =
    static_cast<svn_auth_provider_object_t *> (apr_pcalloc(pool, sizeof(*po)));

  po->vtable = &kwallet_ssl_client_cert_pw_provider;
  *provider = po;
}
}
