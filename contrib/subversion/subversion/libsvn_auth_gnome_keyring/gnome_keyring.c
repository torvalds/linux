/*
 * gnome_keyring.c: GNOME Keyring provider for SVN_AUTH_CRED_*
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
#include <apr_pools.h>
#include <apr_strings.h>
#include "svn_auth.h"
#include "svn_hash.h"
#include "svn_version.h"
#include "private/svn_auth_private.h"
#include "svn_private_config.h"

#ifdef SVN_HAVE_LIBSECRET

#include <libsecret/secret.h>

/* Return TRUE if the default collection is available and FALSE
   otherwise.  In interactive mode the collection only has to exist to
   be available, it can be locked or unlocked.  The default collection
   will be created if necessary.

   In non-interactive mode the collection is only available if it
   already exists and is unlocked.  Such an available collection can
   be used without prompting.  Strictly this is racy: nothing ensures
   the collection remains unlocked.  A similar issue affects the
   KWallet and original GNOME Keyring providers.

   As a non-racy alternative one could override prompt_async in the
   _SecretServiceClass vtable, the get/set would still fail but there
   would be no prompt and no race.  This "works" but it is not clear
   to me whether it is legitimate since the SecretService is a
   singleton and the effect would be application-wide.
 */
static svn_boolean_t
available_collection(svn_boolean_t non_interactive,
                     apr_pool_t *pool)
{
  GError *gerror = NULL;
  SecretService *service = NULL;
  SecretCollection *collection = NULL;

  service = secret_service_get_sync(SECRET_SERVICE_NONE, NULL, &gerror);
  if (gerror || !service)
    goto error_return;

  collection = secret_collection_for_alias_sync(service,
                                                SECRET_COLLECTION_DEFAULT,
                                                SECRET_COLLECTION_NONE,
                                                NULL, &gerror);
  if (gerror)
    goto error_return;

  if (!collection)
    {
      if (non_interactive)
        goto error_return;

      /* "Default" is the label used by the old libgnome-keyring. */
      collection = secret_collection_create_sync(service, "Default",
                                                 SECRET_COLLECTION_DEFAULT,
                                                 0, NULL, &gerror);
      if (gerror || !collection)
        goto error_return;
    }

  if (non_interactive && secret_collection_get_locked(collection))
    goto error_return;

  g_object_unref(collection);
  g_object_unref(service);

  return TRUE;

 error_return:
  if (gerror)
    g_error_free(gerror);
  if (collection)
    g_object_unref(collection);
  if (service)
    g_object_unref(service);
  return FALSE;
}

/* Implementation of svn_auth__password_get_t that retrieves the password
   using libsecret. */
static svn_error_t *
password_get_gnome_keyring(svn_boolean_t *done,
                           const char **password,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  GError *gerror = NULL;
  gchar *gpassword;
  
  if (!available_collection(non_interactive, pool))
    return SVN_NO_ERROR;
  
  gpassword = secret_password_lookup_sync(SECRET_SCHEMA_COMPAT_NETWORK, NULL,
                                          &gerror,
                                          "domain", realmstring,
                                          "user", username,
                                          NULL);
  if (gerror)
    {
      g_error_free(gerror);
    }
  else if (gpassword)
    {
      *password = apr_pstrdup(pool, gpassword);
      g_free(gpassword);
      *done = TRUE;
    }
  
  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_set_t that stores the password
   using libsecret. */
static svn_error_t *
password_set_gnome_keyring(svn_boolean_t *done,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *password,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  GError *gerror = NULL;
  gboolean gstatus;
  
  if (!available_collection(non_interactive, pool))
    return SVN_NO_ERROR;

  /* "network password" is the label used by the old libgnome-keyring. */
  gstatus = secret_password_store_sync(SECRET_SCHEMA_COMPAT_NETWORK,
                                       SECRET_COLLECTION_DEFAULT,
                                       "network password",
                                       password,
                                       NULL, &gerror,
                                       "domain", realmstring,
                                       "user", username,
                                       NULL);
  if (gerror)
    {
      g_error_free(gerror);
    }
  else if (gstatus)
    {
      *done = TRUE;
    }
  
  return SVN_NO_ERROR;
}

#endif /* SVN_HAVE_LIBSECRET */

#ifdef SVN_HAVE_GNOME_KEYRING

#include <glib.h>
#include <gnome-keyring.h>

/* Returns the default keyring name, allocated in RESULT_POOL. */
static char*
get_default_keyring_name(apr_pool_t *result_pool)
{
  char *name, *def;
  GnomeKeyringResult gkr;

  gkr = gnome_keyring_get_default_keyring_sync(&name);
  if (gkr != GNOME_KEYRING_RESULT_OK)
    return NULL;

  def = apr_pstrdup(result_pool, name);
  g_free(name);

  return def;
}

/* Returns TRUE if the KEYRING_NAME is locked. */
static svn_boolean_t
check_keyring_is_locked(const char *keyring_name)
{
  GnomeKeyringInfo *info;
  svn_boolean_t locked;
  GnomeKeyringResult gkr;

  gkr = gnome_keyring_get_info_sync(keyring_name, &info);
  if (gkr != GNOME_KEYRING_RESULT_OK)
    return FALSE;

  if (gnome_keyring_info_get_is_locked(info))
    locked = TRUE;
  else
    locked = FALSE;

  gnome_keyring_info_free(info);

  return locked;
}

/* Unlock the KEYRING_NAME with the KEYRING_PASSWORD. If KEYRING was
   successfully unlocked return TRUE. */
static svn_boolean_t
unlock_gnome_keyring(const char *keyring_name,
                     const char *keyring_password,
                     apr_pool_t *pool)
{
  GnomeKeyringInfo *info;
  GnomeKeyringResult gkr;

  gkr = gnome_keyring_get_info_sync(keyring_name, &info);
  if (gkr != GNOME_KEYRING_RESULT_OK)
    return FALSE;

  gkr = gnome_keyring_unlock_sync(keyring_name, keyring_password);
  gnome_keyring_info_free(info);
  if (gkr != GNOME_KEYRING_RESULT_OK)
    return FALSE;

  return check_keyring_is_locked(keyring_name);
}


/* There is a race here: this ensures keyring is unlocked just now,
   but will it still be unlocked when we use it? */
static svn_error_t *
ensure_gnome_keyring_is_unlocked(svn_boolean_t non_interactive,
                                 apr_hash_t *parameters,
                                 apr_pool_t *scratch_pool)
{
  const char *default_keyring = get_default_keyring_name(scratch_pool);

  if (! non_interactive)
    {
      svn_auth_gnome_keyring_unlock_prompt_func_t unlock_prompt_func =
        svn_hash_gets(parameters,
                      SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_FUNC);
      void *unlock_prompt_baton =
        svn_hash_gets(parameters,
                      SVN_AUTH_PARAM_GNOME_KEYRING_UNLOCK_PROMPT_BATON);

      char *keyring_password;

      if (unlock_prompt_func && check_keyring_is_locked(default_keyring))
        {
          SVN_ERR((*unlock_prompt_func)(&keyring_password,
                                        default_keyring,
                                        unlock_prompt_baton,
                                        scratch_pool));

          /* If keyring is locked give up and try the next provider. */
          if (! unlock_gnome_keyring(default_keyring, keyring_password,
                                     scratch_pool))
            return SVN_NO_ERROR;
        }
    }
  else
    {
      if (check_keyring_is_locked(default_keyring))
        {
          return svn_error_create(SVN_ERR_AUTHN_CREDS_UNAVAILABLE, NULL,
                                  _("GNOME Keyring is locked and "
                                    "we are non-interactive"));
        }
    }

  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_get_t that retrieves the password
   from GNOME Keyring. */
static svn_error_t *
password_get_gnome_keyring(svn_boolean_t *done,
                           const char **password,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  GnomeKeyringResult result;
  GList *items;

  *done = FALSE;

  SVN_ERR(ensure_gnome_keyring_is_unlocked(non_interactive, parameters, pool));

  if (! svn_hash_gets(parameters, "gnome-keyring-opening-failed"))
    {
      result = gnome_keyring_find_network_password_sync(username, realmstring,
                                                        NULL, NULL, NULL, NULL,
                                                        0, &items);
    }
  else
    {
      result = GNOME_KEYRING_RESULT_DENIED;
    }

  if (result == GNOME_KEYRING_RESULT_OK)
    {
      if (items && items->data)
        {
          GnomeKeyringNetworkPasswordData *item = items->data;
          if (item->password)
            {
              size_t len = strlen(item->password);
              if (len > 0)
                {
                  *password = apr_pstrmemdup(pool, item->password, len);
                  *done = TRUE;
                }
            }
          gnome_keyring_network_password_list_free(items);
        }
    }
  else
    {
      svn_hash_sets(parameters, "gnome-keyring-opening-failed", "");
    }

  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_set_t that stores the password in
   GNOME Keyring. */
static svn_error_t *
password_set_gnome_keyring(svn_boolean_t *done,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *password,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  GnomeKeyringResult result;
  guint32 item_id;

  *done = FALSE;

  SVN_ERR(ensure_gnome_keyring_is_unlocked(non_interactive, parameters, pool));

  if (! svn_hash_gets(parameters, "gnome-keyring-opening-failed"))
    {
      result = gnome_keyring_set_network_password_sync(NULL, /* default keyring */
                                                       username, realmstring,
                                                       NULL, NULL, NULL, NULL,
                                                       0, password,
                                                       &item_id);
    }
  else
    {
      result = GNOME_KEYRING_RESULT_DENIED;
    }
  if (result != GNOME_KEYRING_RESULT_OK)
    {
      svn_hash_sets(parameters, "gnome-keyring-opening-failed", "");
    }

  *done = (result == GNOME_KEYRING_RESULT_OK);
  return SVN_NO_ERROR;
}

#if GLIB_CHECK_VERSION(2,6,0)
static void
log_noop(const gchar *log_domain, GLogLevelFlags log_level,
         const gchar *message, gpointer user_data)
{
  /* do nothing */
}
#endif

static void
init_gnome_keyring(void)
{
  const char *application_name = NULL;
  application_name = g_get_application_name();
  if (!application_name)
    g_set_application_name("Subversion");

  /* Ideally we call g_log_set_handler() with a log_domain specific to
     libgnome-keyring.  Unfortunately, at least as of gnome-keyring
     2.22.3, it doesn't have its own log_domain.  As a result, we
     suppress stderr spam for not only libgnome-keyring, but for
     anything else the app is linked to that uses glib logging and
     doesn't specify a log_domain. */
#if GLIB_CHECK_VERSION(2,6,0)
  g_log_set_default_handler(log_noop, NULL);
#endif
}

#endif /* SVN_HAVE_GNOME_KEYRING */


/*-----------------------------------------------------------------------*/
/* GNOME Keyring simple provider, puts passwords in GNOME Keyring        */
/*-----------------------------------------------------------------------*/

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
simple_gnome_keyring_first_creds(void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_get(credentials,
                                          iter_baton, provider_baton,
                                          parameters, realmstring,
                                          password_get_gnome_keyring,
                                          SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
                                          pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
simple_gnome_keyring_save_creds(svn_boolean_t *saved,
                                void *credentials,
                                void *provider_baton,
                                apr_hash_t *parameters,
                                const char *realmstring,
                                apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_set(saved, credentials,
                                          provider_baton, parameters,
                                          realmstring,
                                          password_set_gnome_keyring,
                                          SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
                                          pool);
}

static const svn_auth_provider_t gnome_keyring_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_gnome_keyring_first_creds,
  NULL,
  simple_gnome_keyring_save_creds
};

/* Public API */
void
svn_auth_get_gnome_keyring_simple_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gnome_keyring_simple_provider;
  *provider = po;

#ifdef SVN_HAVE_GNOME_KEYRING
  init_gnome_keyring();
#endif
}



/*-----------------------------------------------------------------------*/
/* GNOME Keyring SSL client certificate passphrase provider,             */
/* puts passphrases in GNOME Keyring                                     */
/*-----------------------------------------------------------------------*/

/* Get cached encrypted credentials from the ssl client cert password
   provider's cache. */
static svn_error_t *
ssl_client_cert_pw_gnome_keyring_first_creds(void **credentials,
                                             void **iter_baton,
                                             void *provider_baton,
                                             apr_hash_t *parameters,
                                             const char *realmstring,
                                             apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_get(
             credentials, iter_baton, provider_baton, parameters, realmstring,
             password_get_gnome_keyring, SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
             pool);
}

/* Save encrypted credentials to the ssl client cert password provider's
   cache. */
static svn_error_t *
ssl_client_cert_pw_gnome_keyring_save_creds(svn_boolean_t *saved,
                                            void *credentials,
                                            void *provider_baton,
                                            apr_hash_t *parameters,
                                            const char *realmstring,
                                            apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_set(
             saved, credentials, provider_baton, parameters, realmstring,
             password_set_gnome_keyring, SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE,
             pool);
}

static const svn_auth_provider_t gnome_keyring_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  ssl_client_cert_pw_gnome_keyring_first_creds,
  NULL,
  ssl_client_cert_pw_gnome_keyring_save_creds
};

/* Public API */
void
svn_auth_get_gnome_keyring_ssl_client_cert_pw_provider
    (svn_auth_provider_object_t **provider,
     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &gnome_keyring_ssl_client_cert_pw_provider;
  *provider = po;

#ifdef SVN_HAVE_GNOME_KEYRING
  init_gnome_keyring();
#endif
}
