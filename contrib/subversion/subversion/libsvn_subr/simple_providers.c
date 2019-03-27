/*
 * simple_providers.c: providers for SVN_AUTH_CRED_SIMPLE
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
#include "svn_auth.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_user.h"

#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#include "auth.h"

/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* Baton type for the simple provider. */
typedef struct simple_provider_baton_t
{
  svn_auth_plaintext_prompt_func_t plaintext_prompt_func;
  void *prompt_baton;
  /* We cache the user's answer to the plaintext prompt, keyed
   * by realm, in case we'll be called multiple times for the
   * same realm. */
  apr_hash_t *plaintext_answers;
} simple_provider_baton_t;


/* Implementation of svn_auth__password_get_t that retrieves
   the plaintext password from CREDS. */
svn_error_t *
svn_auth__simple_password_get(svn_boolean_t *done,
                              const char **password,
                              apr_hash_t *creds,
                              const char *realmstring,
                              const char *username,
                              apr_hash_t *parameters,
                              svn_boolean_t non_interactive,
                              apr_pool_t *pool)
{
  svn_string_t *str;

  *done = FALSE;

  str = svn_hash_gets(creds, SVN_CONFIG_AUTHN_USERNAME_KEY);
  if (str && username && strcmp(str->data, username) == 0)
    {
      str = svn_hash_gets(creds, SVN_CONFIG_AUTHN_PASSWORD_KEY);
      if (str && str->data)
        {
          *password = str->data;
          *done = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

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
                              apr_pool_t *pool)
{
  svn_hash_sets(creds, SVN_CONFIG_AUTHN_PASSWORD_KEY,
                svn_string_create(password, pool));
  *done = TRUE;

  return SVN_NO_ERROR;
}

/* Set **USERNAME to the username retrieved from CREDS; ignore
   other parameters. *USERNAME will have the same lifetime as CREDS. */
static svn_boolean_t
simple_username_get(const char **username,
                    apr_hash_t *creds,
                    const char *realmstring,
                    svn_boolean_t non_interactive)
{
  svn_string_t *str;
  str = svn_hash_gets(creds, SVN_CONFIG_AUTHN_USERNAME_KEY);
  if (str && str->data)
    {
      *username = str->data;
      return TRUE;
    }
  return FALSE;
}


svn_error_t *
svn_auth__simple_creds_cache_get(void **credentials,
                                 void **iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_get_t password_get,
                                 const char *passtype,
                                 apr_pool_t *pool)
{
  const char *config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);
  svn_config_t *cfg = svn_hash_gets(parameters,
                                    SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS);
  const char *server_group = svn_hash_gets(parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP);
  const char *username = svn_hash_gets(parameters,
                                       SVN_AUTH_PARAM_DEFAULT_USERNAME);
  const char *password = svn_hash_gets(parameters,
                                       SVN_AUTH_PARAM_DEFAULT_PASSWORD);
  svn_boolean_t non_interactive = svn_hash_gets(parameters,
                                                SVN_AUTH_PARAM_NON_INTERACTIVE)
      != NULL;
  const char *default_username = NULL; /* Default username from cache. */
  const char *default_password = NULL; /* Default password from cache. */

  /* This checks if we should save the CREDS, iff saving the credentials is
     allowed by the run-time configuration. */
  svn_boolean_t need_to_save = FALSE;
  apr_hash_t *creds_hash = NULL;
  svn_error_t *err;
  svn_string_t *str;

  /* Try to load credentials from a file on disk, based on the
     realmstring.  Don't throw an error, though: if something went
     wrong reading the file, no big deal.  What really matters is that
     we failed to get the creds, so allow the auth system to try the
     next provider. */
  err = svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                  realmstring, config_dir, pool);
  if (err)
    {
      svn_error_clear(err);
      err = NULL;
    }
  else if (creds_hash)
    {
      /* We have something in the auth cache for this realm. */
      svn_boolean_t have_passtype = FALSE;

      /* The password type in the auth data must match the
         mangler's type, otherwise the password must be
         interpreted by another provider. */
      str = svn_hash_gets(creds_hash, SVN_CONFIG_AUTHN_PASSTYPE_KEY);
      if (str && str->data)
        if (passtype && (0 == strcmp(str->data, passtype)))
          have_passtype = TRUE;

      /* See if we need to save this username if it is not present in
         auth cache. */
      if (username)
        {
          if (!simple_username_get(&default_username, creds_hash, realmstring,
                                   non_interactive))
            {
              need_to_save = TRUE;
            }
          else
            {
              if (strcmp(default_username, username) != 0)
                need_to_save = TRUE;
            }
        }

      /* See if we need to save this password if it is not present in
         auth cache. */
      if (password)
        {
          if (have_passtype)
            {
              svn_boolean_t done;

              SVN_ERR(password_get(&done, &default_password, creds_hash,
                                   realmstring, username, parameters,
                                   non_interactive, pool));
              if (!done)
                {
                  need_to_save = TRUE;
                }
              else
                {
                  if (strcmp(default_password, password) != 0)
                    need_to_save = TRUE;
                }
            }
        }

      /* If we don't have a username and a password yet, we try the
         auth cache */
      if (! (username && password))
        {
          if (! username)
            if (!simple_username_get(&username, creds_hash, realmstring,
                                     non_interactive))
              username = NULL;

          if (username && ! password)
            {
              if (! have_passtype)
                password = NULL;
              else
                {
                  svn_boolean_t done;

                  SVN_ERR(password_get(&done, &password, creds_hash,
                                       realmstring, username, parameters,
                                       non_interactive, pool));
                  if (!done)
                    password = NULL;

                  /* If the auth data didn't contain a password type,
                     force a write to upgrade the format of the auth
                     data file. */
                  if (password && ! have_passtype)
                    need_to_save = TRUE;
                }
            }
        }
    }
  else
    {
      /* Nothing was present in the auth cache, so indicate that these
         credentials should be saved. */
      need_to_save = TRUE;
    }

  /* If we don't have a username yet, check the 'servers' file */
  if (! username)
    {
      username = svn_config_get_server_setting(cfg, server_group,
                                               SVN_CONFIG_OPTION_USERNAME,
                                               NULL);
    }

  /* Ask the OS for the username if we have a password but no
     username. */
  if (password && ! username)
    username = svn_user_get_name(pool);

  if (username && password)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc(pool, sizeof(*creds));
      creds->username = username;
      creds->password = password;
      creds->may_save = need_to_save;
      *credentials = creds;
    }
  else
    *credentials = NULL;

  *iter_baton = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_auth__simple_creds_cache_set(svn_boolean_t *saved,
                                 void *credentials,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 svn_auth__password_set_t password_set,
                                 const char *passtype,
                                 apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *err;
  svn_boolean_t dont_store_passwords =
    svn_hash_gets(parameters, SVN_AUTH_PARAM_DONT_STORE_PASSWORDS) != NULL;
  svn_boolean_t non_interactive = svn_hash_gets(parameters,
                                                SVN_AUTH_PARAM_NON_INTERACTIVE)
      != NULL;
  svn_boolean_t no_auth_cache =
    (! creds->may_save) || (svn_hash_gets(parameters,
                                          SVN_AUTH_PARAM_NO_AUTH_CACHE)
                            != NULL);

  /* Make sure we've been passed a passtype. */
  SVN_ERR_ASSERT(passtype != NULL);

  *saved = FALSE;

  if (no_auth_cache)
    return SVN_NO_ERROR;

  config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);

  /* Put the username into the credentials hash. */
  creds_hash = apr_hash_make(pool);
  svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_USERNAME_KEY,
                svn_string_create(creds->username, pool));

  /* Don't store passwords in any form if the user has told
   * us not to do so. */
  if (! dont_store_passwords)
    {
      svn_boolean_t may_save_password = FALSE;

      /* If the password is going to be stored encrypted, go right
       * ahead and store it to disk. Else determine whether saving
       * in plaintext is OK. */
      if (passtype &&
           (strcmp(passtype, SVN_AUTH__WINCRYPT_PASSWORD_TYPE) == 0
            || strcmp(passtype, SVN_AUTH__KEYCHAIN_PASSWORD_TYPE) == 0
            || strcmp(passtype, SVN_AUTH__KWALLET_PASSWORD_TYPE) == 0
            || strcmp(passtype, SVN_AUTH__GNOME_KEYRING_PASSWORD_TYPE) == 0
            || strcmp(passtype, SVN_AUTH__GPG_AGENT_PASSWORD_TYPE) == 0))
        {
          may_save_password = TRUE;
        }
      else
        {
#ifdef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
          may_save_password = FALSE;
#else
          const char *store_plaintext_passwords =
            svn_hash_gets(parameters, SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS);
          simple_provider_baton_t *b =
            (simple_provider_baton_t *)provider_baton;

          if (store_plaintext_passwords
              && svn_cstring_casecmp(store_plaintext_passwords,
                                     SVN_CONFIG_ASK) == 0)
            {
              if (non_interactive)
                /* In non-interactive mode, the default behaviour is
                 * to not store the password, because it is usually
                 * passed on the command line. */
                may_save_password = FALSE;
              else if (b->plaintext_prompt_func)
                {
                  /* We're interactive, and the client provided a
                   * prompt callback. So we can ask the user.
                   *
                   * Check for a cached answer before prompting. */
                  svn_boolean_t *cached_answer;
                  cached_answer = svn_hash_gets(b->plaintext_answers,
                                                realmstring);
                  if (cached_answer != NULL)
                    may_save_password = *cached_answer;
                  else
                    {
                      apr_pool_t *cached_answer_pool;

                      /* Nothing cached for this realm, prompt the user. */
                      SVN_ERR((*b->plaintext_prompt_func)(&may_save_password,
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
                                                 sizeof(svn_boolean_t));
                      *cached_answer = may_save_password;
                      svn_hash_sets(b->plaintext_answers, realmstring,
                                    cached_answer);
                    }
                }
              else
                {
                  /* TODO: We might want to default to not storing if the
                   * prompt callback is NULL, i.e. have may_save_password
                   * default to FALSE here, in order to force clients to
                   * implement the callback.
                   *
                   * This would change the semantics of old API though.
                   *
                   * So for now, clients that don't implement the callback
                   * and provide no explicit value for
                   * SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS
                   * cause unencrypted passwords to be stored by default.
                   * Needless to say, our own client is sane, but who knows
                   * what other clients are doing.
                   */
                  may_save_password = TRUE;
                }
            }
          else if (store_plaintext_passwords
                   && svn_cstring_casecmp(store_plaintext_passwords,
                                          SVN_CONFIG_FALSE) == 0)
            {
              may_save_password = FALSE;
            }
          else if (!store_plaintext_passwords
                   || svn_cstring_casecmp(store_plaintext_passwords,
                                          SVN_CONFIG_TRUE) == 0)
            {
              may_save_password = TRUE;
            }
          else
            {
              return svn_error_createf
                (SVN_ERR_BAD_CONFIG_VALUE, NULL,
                 _("Config error: invalid value '%s' for option '%s'"),
                store_plaintext_passwords,
                SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS);
            }
#endif
        }

      if (may_save_password)
        {
          SVN_ERR(password_set(saved, creds_hash, realmstring,
                               creds->username, creds->password,
                               parameters, non_interactive, pool));
          if (*saved && passtype)
            /* Store the password type with the auth data, so that we
               know which provider owns the password. */
            svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_PASSTYPE_KEY,
                          svn_string_create(passtype, pool));
        }
    }

  /* Save credentials to disk. */
  err = svn_config_write_auth_data(creds_hash, SVN_AUTH_CRED_SIMPLE,
                                   realmstring, config_dir, pool);
  if (err)
    *saved = FALSE;

  /* ### return error? */
  svn_error_clear(err);

  return SVN_NO_ERROR;
}

/* Get cached (unencrypted) credentials from the simple provider's cache. */
static svn_error_t *
simple_first_creds(void **credentials,
                   void **iter_baton,
                   void *provider_baton,
                   apr_hash_t *parameters,
                   const char *realmstring,
                   apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_get(credentials, iter_baton,
                                          provider_baton, parameters,
                                          realmstring,
                                          svn_auth__simple_password_get,
                                          SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                          pool);
}

/* Save (unencrypted) credentials to the simple provider's cache. */
static svn_error_t *
simple_save_creds(svn_boolean_t *saved,
                  void *credentials,
                  void *provider_baton,
                  apr_hash_t *parameters,
                  const char *realmstring,
                  apr_pool_t *pool)
{
  return svn_auth__simple_creds_cache_set(saved, credentials, provider_baton,
                                          parameters, realmstring,
                                          svn_auth__simple_password_set,
                                          SVN_AUTH__SIMPLE_PASSWORD_TYPE,
                                          pool);
}

static const svn_auth_provider_t simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_first_creds,
  NULL,
  simple_save_creds
};


/* Public API */
void
svn_auth_get_simple_provider2
  (svn_auth_provider_object_t **provider,
   svn_auth_plaintext_prompt_func_t plaintext_prompt_func,
   void* prompt_baton,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  simple_provider_baton_t *pb = apr_pcalloc(pool, sizeof(*pb));

  pb->plaintext_prompt_func = plaintext_prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->plaintext_answers = apr_hash_make(pool);

  po->vtable = &simple_provider;
  po->provider_baton = pb;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for username/password prompting. */
typedef struct simple_prompt_provider_baton_t
{
  svn_auth_simple_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} simple_prompt_provider_baton_t;


/* Iteration baton type for username/password prompting. */
typedef struct simple_prompt_iter_baton_t
{
  /* how many times we've reprompted */
  int retries;
} simple_prompt_iter_baton_t;



/*** Helper Functions ***/
static svn_error_t *
prompt_for_simple_creds(svn_auth_cred_simple_t **cred_p,
                        simple_prompt_provider_baton_t *pb,
                        apr_hash_t *parameters,
                        const char *realmstring,
                        svn_boolean_t first_time,
                        svn_boolean_t may_save,
                        apr_pool_t *pool)
{
  const char *default_username = NULL;
  const char *default_password = NULL;

  *cred_p = NULL;

  /* If we're allowed to check for default usernames and passwords, do
     so. */
  if (first_time)
    {
      default_username = svn_hash_gets(parameters,
                                       SVN_AUTH_PARAM_DEFAULT_USERNAME);

      /* No default username?  Try the auth cache. */
      if (! default_username)
        {
          const char *config_dir = svn_hash_gets(parameters,
                                                 SVN_AUTH_PARAM_CONFIG_DIR);
          apr_hash_t *creds_hash = NULL;
          svn_string_t *str;
          svn_error_t *err;

          err = svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SIMPLE,
                                          realmstring, config_dir, pool);
          svn_error_clear(err);
          if (! err && creds_hash)
            {
              str = svn_hash_gets(creds_hash, SVN_CONFIG_AUTHN_USERNAME_KEY);
              if (str && str->data)
                default_username = str->data;
            }
        }

      /* Still no default username?  Try the 'servers' file. */
      if (! default_username)
        {
          svn_config_t *cfg = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS);
          const char *server_group = svn_hash_gets(parameters,
                                                   SVN_AUTH_PARAM_SERVER_GROUP);
          default_username =
            svn_config_get_server_setting(cfg, server_group,
                                          SVN_CONFIG_OPTION_USERNAME,
                                          NULL);
        }

      /* Still no default username?  Try the UID. */
      if (! default_username)
        default_username = svn_user_get_name(pool);

      default_password = svn_hash_gets(parameters,
                                       SVN_AUTH_PARAM_DEFAULT_PASSWORD);
    }

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (default_username && default_password)
    {
      *cred_p = apr_palloc(pool, sizeof(**cred_p));
      (*cred_p)->username = apr_pstrdup(pool, default_username);
      (*cred_p)->password = apr_pstrdup(pool, default_password);
      (*cred_p)->may_save = TRUE;
    }
  else
    {
      SVN_ERR(pb->prompt_func(cred_p, pb->prompt_baton, realmstring,
                              default_username, may_save, pool));
    }

  return SVN_NO_ERROR;
}


/* Our first attempt will use any default username/password passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
simple_prompt_first_creds(void **credentials_p,
                          void **iter_baton,
                          void *provider_baton,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          apr_pool_t *pool)
{
  simple_prompt_provider_baton_t *pb = provider_baton;
  simple_prompt_iter_baton_t *ibaton = apr_pcalloc(pool, sizeof(*ibaton));
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  SVN_ERR(prompt_for_simple_creds((svn_auth_cred_simple_t **) credentials_p,
                                  pb, parameters, realmstring, TRUE,
                                  ! no_auth_cache, pool));

  ibaton->retries = 0;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default values, and
   simply re-prompt for both, up to a maximum of ib->pb->retry_limit. */
static svn_error_t *
simple_prompt_next_creds(void **credentials_p,
                         void *iter_baton,
                         void *provider_baton,
                         apr_hash_t *parameters,
                         const char *realmstring,
                         apr_pool_t *pool)
{
  simple_prompt_iter_baton_t *ib = iter_baton;
  simple_prompt_provider_baton_t *pb = provider_baton;
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  if ((pb->retry_limit >= 0) && (ib->retries >= pb->retry_limit))
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  return prompt_for_simple_creds((svn_auth_cred_simple_t **) credentials_p,
                                 pb, parameters, realmstring, FALSE,
                                 ! no_auth_cache, pool);
}

static const svn_auth_provider_t simple_prompt_provider = {
  SVN_AUTH_CRED_SIMPLE,
  simple_prompt_first_creds,
  simple_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_auth_get_simple_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_simple_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  simple_prompt_provider_baton_t *pb = apr_pcalloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &simple_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
