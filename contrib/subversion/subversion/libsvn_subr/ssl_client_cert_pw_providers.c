/*
 * ssl_client_cert_pw_providers.c: providers for
 * SVN_AUTH_CRED_SSL_CLIENT_CERT_PW
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


#include <apr_pools.h>

#include "svn_hash.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_config.h"
#include "svn_string.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* Baton type for the ssl client cert passphrase provider. */
typedef struct ssl_client_cert_pw_file_provider_baton_t
{
  svn_auth_plaintext_passphrase_prompt_func_t plaintext_passphrase_prompt_func;
  void *prompt_baton;
  /* We cache the user's answer to the plaintext prompt, keyed
     by realm, in case we'll be called multiple times for the
     same realm.  So: keys are 'const char *' realm strings, and
     values are 'svn_boolean_t *'. */
  apr_hash_t *plaintext_answers;
} ssl_client_cert_pw_file_provider_baton_t;

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
                                 apr_pool_t *pool)
{
  svn_string_t *str;
  str = svn_hash_gets(creds, SVN_CONFIG_AUTHN_PASSPHRASE_KEY);
  if (str && str->data)
    {
      *passphrase = str->data;
      *done = TRUE;
      return SVN_NO_ERROR;
    }
  *done = FALSE;
  return SVN_NO_ERROR;
}

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
                                 apr_pool_t *pool)
{
  svn_hash_sets(creds, SVN_CONFIG_AUTHN_PASSPHRASE_KEY,
                svn_string_create(passphrase, pool));
  *done = TRUE;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_auth__ssl_client_cert_pw_cache_get(void **credentials_p,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_get_t passphrase_get,
                                       const char *passtype,
                                       apr_pool_t *pool)
{
  svn_config_t *cfg = svn_hash_gets(parameters,
                                    SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS);
  const char *server_group = svn_hash_gets(parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP);
  svn_boolean_t non_interactive = svn_hash_gets(parameters,
                                                SVN_AUTH_PARAM_NON_INTERACTIVE)
      != NULL;
  const char *password =
    svn_config_get_server_setting(cfg, server_group,
                                  SVN_CONFIG_OPTION_SSL_CLIENT_CERT_PASSWORD,
                                  NULL);
  if (! password)
    {
      svn_error_t *err;
      apr_hash_t *creds_hash = NULL;
      const char *config_dir = svn_hash_gets(parameters,
                                             SVN_AUTH_PARAM_CONFIG_DIR);

      /* Try to load passphrase from the auth/ cache. */
      err = svn_config_read_auth_data(&creds_hash,
                                      SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                      realmstring, config_dir, pool);
      svn_error_clear(err);
      if (! err && creds_hash)
        {
          svn_boolean_t done;

          SVN_ERR(passphrase_get(&done, &password, creds_hash, realmstring,
                                 NULL, parameters, non_interactive, pool));
          if (!done)
            password = NULL;
        }
    }

  if (password)
    {
      svn_auth_cred_ssl_client_cert_pw_t *cred
        = apr_palloc(pool, sizeof(*cred));
      cred->password = password;
      cred->may_save = FALSE;
      *credentials_p = cred;
    }
  else *credentials_p = NULL;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__ssl_client_cert_pw_cache_set(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       svn_auth__password_set_t passphrase_set,
                                       const char *passtype,
                                       apr_pool_t *pool)
{
  svn_auth_cred_ssl_client_cert_pw_t *creds = credentials;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *err;
  svn_boolean_t dont_store_passphrase =
    svn_hash_gets(parameters, SVN_AUTH_PARAM_DONT_STORE_SSL_CLIENT_CERT_PP)
    != NULL;
  svn_boolean_t non_interactive =
      svn_hash_gets(parameters, SVN_AUTH_PARAM_NON_INTERACTIVE) != NULL;
  svn_boolean_t no_auth_cache =
    (! creds->may_save)
    || (svn_hash_gets(parameters, SVN_AUTH_PARAM_NO_AUTH_CACHE) != NULL);

  *saved = FALSE;

  if (no_auth_cache)
    return SVN_NO_ERROR;

  config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);
  creds_hash = apr_hash_make(pool);

  /* Don't store passphrase in any form if the user has told
     us not to do so. */
  if (! dont_store_passphrase)
    {
      svn_boolean_t may_save_passphrase = FALSE;

      /* If the passphrase is going to be stored encrypted, go right
         ahead and store it to disk. Else determine whether saving
         in plaintext is OK. */
      if (strcmp(passtype, SVN_AUTH__WINCRYPT_PASSWORD_TYPE) == 0
          || strcmp(passtype, SVN_AUTH__KWALLET_PASSWORD_TYPE) == 0
          || strcmp(passtype, SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE) == 0
          || strcmp(passtype, SVN_AUTH__KEYCHAIN_PASSWORD_TYPE) == 0)
        {
          may_save_passphrase = TRUE;
        }
      else
        {
#ifdef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
          may_save_passphrase = FALSE;
#else
          const char *store_ssl_client_cert_pp_plaintext =
            svn_hash_gets(parameters,
                          SVN_AUTH_PARAM_STORE_SSL_CLIENT_CERT_PP_PLAINTEXT);
          ssl_client_cert_pw_file_provider_baton_t *b =
            (ssl_client_cert_pw_file_provider_baton_t *)provider_baton;

          if (svn_cstring_casecmp(store_ssl_client_cert_pp_plaintext,
                                  SVN_CONFIG_ASK) == 0)
            {
              if (non_interactive)
                {
                  /* In non-interactive mode, the default behaviour is
                     to not store the passphrase */
                  may_save_passphrase = FALSE;
                }
              else if (b->plaintext_passphrase_prompt_func)
                {
                  /* We're interactive, and the client provided a
                     prompt callback.  So we can ask the user.
                     Check for a cached answer before prompting.

                     This is a pointer-to-boolean, rather than just a
                     boolean, because we must distinguish between
                     "cached answer is no" and "no answer has been
                     cached yet". */
                  svn_boolean_t *cached_answer =
                    svn_hash_gets(b->plaintext_answers, realmstring);

                  if (cached_answer != NULL)
                    {
                      may_save_passphrase = *cached_answer;
                    }
                  else
                    {
                      apr_pool_t *cached_answer_pool;

                      /* Nothing cached for this realm, prompt the user. */
                      SVN_ERR((*b->plaintext_passphrase_prompt_func)(
                                &may_save_passphrase,
                                realmstring,
                                b->prompt_baton,
                                pool));

                      /* Cache the user's answer in case we're called again
                       * for the same realm.
                       *
                       * We allocate the answer cache in the hash table's pool
                       * to make sure that is has the same life time as the
                       * hash table itself. This means that the answer will
                       * survive across RA sessions -- which is important,
                       * because otherwise we'd prompt users once per RA session.
                       */
                      cached_answer_pool = apr_hash_pool_get(b->plaintext_answers);
                      cached_answer = apr_palloc(cached_answer_pool,
                                                 sizeof(*cached_answer));
                      *cached_answer = may_save_passphrase;
                      svn_hash_sets(b->plaintext_answers, realmstring,
                                    cached_answer);
                    }
                }
              else
                {
                  may_save_passphrase = FALSE;
                }
            }
          else if (svn_cstring_casecmp(store_ssl_client_cert_pp_plaintext,
                                       SVN_CONFIG_FALSE) == 0)
            {
              may_save_passphrase = FALSE;
            }
          else if (svn_cstring_casecmp(store_ssl_client_cert_pp_plaintext,
                                       SVN_CONFIG_TRUE) == 0)
            {
              may_save_passphrase = TRUE;
            }
          else
            {
              return svn_error_createf
                (SVN_ERR_RA_DAV_INVALID_CONFIG_VALUE, NULL,
                 _("Config error: invalid value '%s' for option '%s'"),
                store_ssl_client_cert_pp_plaintext,
                SVN_AUTH_PARAM_STORE_SSL_CLIENT_CERT_PP_PLAINTEXT);
            }
#endif
        }

      if (may_save_passphrase)
        {
          SVN_ERR(passphrase_set(saved, creds_hash, realmstring,
                                 NULL, creds->password, parameters,
                                 non_interactive, pool));

          if (*saved && passtype)
            {
              svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_PASSTYPE_KEY,
                            svn_string_create(passtype, pool));
            }

          /* Save credentials to disk. */
          err = svn_config_write_auth_data(creds_hash,
                                           SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                           realmstring, config_dir, pool);
          svn_error_clear(err);
          *saved = ! err;
        }
    }

  return SVN_NO_ERROR;
}


/* This implements the svn_auth_provider_t.first_credentials API.
   It gets cached (unencrypted) credentials from the ssl client cert
   password provider's cache. */
static svn_error_t *
ssl_client_cert_pw_file_first_credentials(void **credentials_p,
                                          void **iter_baton,
                                          void *provider_baton,
                                          apr_hash_t *parameters,
                                          const char *realmstring,
                                          apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_get(credentials_p, iter_baton,
                                                provider_baton, parameters,
                                                realmstring,
                                                svn_auth__ssl_client_cert_pw_get,
                                                SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                                pool);
}


/* This implements the svn_auth_provider_t.save_credentials API.
   It saves the credentials unencrypted. */
static svn_error_t *
ssl_client_cert_pw_file_save_credentials(svn_boolean_t *saved,
                                         void *credentials,
                                         void *provider_baton,
                                         apr_hash_t *parameters,
                                         const char *realmstring,
                                         apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_set(saved, credentials,
                                                provider_baton,
                                                parameters,
                                                realmstring,
                                                svn_auth__ssl_client_cert_pw_set,
                                                SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                                pool);
}


static const svn_auth_provider_t ssl_client_cert_pw_file_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  ssl_client_cert_pw_file_first_credentials,
  NULL,
  ssl_client_cert_pw_file_save_credentials
};


/*** Public API to SSL file providers. ***/
void
svn_auth_get_ssl_client_cert_pw_file_provider2
  (svn_auth_provider_object_t **provider,
   svn_auth_plaintext_passphrase_prompt_func_t plaintext_passphrase_prompt_func,
   void *prompt_baton,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  ssl_client_cert_pw_file_provider_baton_t *pb = apr_pcalloc(pool,
                                                             sizeof(*pb));

  pb->plaintext_passphrase_prompt_func = plaintext_passphrase_prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->plaintext_answers = apr_hash_make(pool);

  po->vtable = &ssl_client_cert_pw_file_provider;
  po->provider_baton = pb;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for client passphrase prompting.
   There is no iteration baton type. */
typedef struct ssl_client_cert_pw_prompt_provider_baton_t
{
  svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} ssl_client_cert_pw_prompt_provider_baton_t;

/* Iteration baton. */
typedef struct ssl_client_cert_pw_prompt_iter_baton_t
{
  /* The original provider baton */
  ssl_client_cert_pw_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;
} ssl_client_cert_pw_prompt_iter_baton_t;


static svn_error_t *
ssl_client_cert_pw_prompt_first_cred(void **credentials_p,
                                     void **iter_baton,
                                     void *provider_baton,
                                     apr_hash_t *parameters,
                                     const char *realmstring,
                                     apr_pool_t *pool)
{
  ssl_client_cert_pw_prompt_provider_baton_t *pb = provider_baton;
  ssl_client_cert_pw_prompt_iter_baton_t *ib =
    apr_pcalloc(pool, sizeof(*ib));
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  SVN_ERR(pb->prompt_func((svn_auth_cred_ssl_client_cert_pw_t **)
                          credentials_p, pb->prompt_baton, realmstring,
                          ! no_auth_cache, pool));

  ib->pb = pb;
  ib->realmstring = apr_pstrdup(pool, realmstring);
  ib->retries = 0;
  *iter_baton = ib;

  return SVN_NO_ERROR;
}


static svn_error_t *
ssl_client_cert_pw_prompt_next_cred(void **credentials_p,
                                    void *iter_baton,
                                    void *provider_baton,
                                    apr_hash_t *parameters,
                                    const char *realmstring,
                                    apr_pool_t *pool)
{
  ssl_client_cert_pw_prompt_iter_baton_t *ib = iter_baton;
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  if ((ib->pb->retry_limit >= 0) && (ib->retries >= ib->pb->retry_limit))
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  return ib->pb->prompt_func((svn_auth_cred_ssl_client_cert_pw_t **)
                             credentials_p, ib->pb->prompt_baton,
                             ib->realmstring, ! no_auth_cache, pool);
}


static const svn_auth_provider_t client_cert_pw_prompt_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  ssl_client_cert_pw_prompt_first_cred,
  ssl_client_cert_pw_prompt_next_cred,
  NULL
};


void svn_auth_get_ssl_client_cert_pw_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_client_cert_pw_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  ssl_client_cert_pw_prompt_provider_baton_t *pb =
    apr_palloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &client_cert_pw_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
