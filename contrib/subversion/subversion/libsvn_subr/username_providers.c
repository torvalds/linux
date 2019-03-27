/*
 * username_providers.c: providers for SVN_AUTH_CRED_USERNAME
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
#include "svn_hash.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_user.h"


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/*** Username-only Provider ***/
static svn_error_t *
username_first_creds(void **credentials,
                     void **iter_baton,
                     void *provider_baton,
                     apr_hash_t *parameters,
                     const char *realmstring,
                     apr_pool_t *pool)
{
  const char *config_dir = svn_hash_gets(parameters,
                                         SVN_AUTH_PARAM_CONFIG_DIR);
  const char *username = svn_hash_gets(parameters,
                                       SVN_AUTH_PARAM_DEFAULT_USERNAME);
  svn_boolean_t may_save = !! username;
  svn_error_t *err;

  /* If we don't have a usename yet, try the auth cache */
  if (! username)
    {
      apr_hash_t *creds_hash = NULL;

      /* Try to load credentials from a file on disk, based on the
         realmstring.  Don't throw an error, though: if something went
         wrong reading the file, no big deal.  What really matters is that
         we failed to get the creds, so allow the auth system to try the
         next provider. */
      err = svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_USERNAME,
                                      realmstring, config_dir, pool);
      svn_error_clear(err);
      if (! err && creds_hash)
        {
          svn_string_t *str = svn_hash_gets(creds_hash,
                                            SVN_CONFIG_AUTHN_USERNAME_KEY);
          if (str && str->data)
            username = str->data;
        }
    }

  /* If that failed, ask the OS for the username */
  if (! username)
    username = svn_user_get_name(pool);

  if (username)
    {
      svn_auth_cred_simple_t *creds = apr_pcalloc(pool, sizeof(*creds));
      creds->username = username;
      creds->may_save = may_save;
      *credentials = creds;
    }
  else
    *credentials = NULL;

  *iter_baton = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
username_save_creds(svn_boolean_t *saved,
                    void *credentials,
                    void *provider_baton,
                    apr_hash_t *parameters,
                    const char *realmstring,
                    apr_pool_t *pool)
{
  svn_auth_cred_simple_t *creds = credentials;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *err;

  *saved = FALSE;

  if (! creds->may_save)
    return SVN_NO_ERROR;

  config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);

  /* Put the credentials in a hash and save it to disk */
  creds_hash = apr_hash_make(pool);
  svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_USERNAME_KEY,
                svn_string_create(creds->username, pool));
  err = svn_config_write_auth_data(creds_hash, SVN_AUTH_CRED_USERNAME,
                                   realmstring, config_dir, pool);
  svn_error_clear(err);
  *saved = ! err;

  return SVN_NO_ERROR;
}


static const svn_auth_provider_t username_provider = {
  SVN_AUTH_CRED_USERNAME,
  username_first_creds,
  NULL,
  username_save_creds
};


/* Public API */
void
svn_auth_get_username_provider(svn_auth_provider_object_t **provider,
                               apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &username_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for username-only prompting. */
typedef struct username_prompt_provider_baton_t
{
  svn_auth_username_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} username_prompt_provider_baton_t;


/* Iteration baton type for username-only prompting. */
typedef struct username_prompt_iter_baton_t
{
  /* how many times we've reprompted */
  int retries;

} username_prompt_iter_baton_t;


/*** Helper Functions ***/
static svn_error_t *
prompt_for_username_creds(svn_auth_cred_username_t **cred_p,
                          username_prompt_provider_baton_t *pb,
                          apr_hash_t *parameters,
                          const char *realmstring,
                          svn_boolean_t first_time,
                          svn_boolean_t may_save,
                          apr_pool_t *pool)
{
  const char *def_username = NULL;

  *cred_p = NULL;

  /* If we're allowed to check for default usernames, do so. */
  if (first_time)
    def_username = svn_hash_gets(parameters, SVN_AUTH_PARAM_DEFAULT_USERNAME);

  /* If we have defaults, just build the cred here and return it.
   *
   * ### I do wonder why this is here instead of in a separate
   * ### 'defaults' provider that would run before the prompt
   * ### provider... Hmmm.
   */
  if (def_username)
    {
      *cred_p = apr_palloc(pool, sizeof(**cred_p));
      (*cred_p)->username = apr_pstrdup(pool, def_username);
      (*cred_p)->may_save = TRUE;
    }
  else
    {
      SVN_ERR(pb->prompt_func(cred_p, pb->prompt_baton, realmstring,
                              may_save, pool));
    }

  return SVN_NO_ERROR;
}


/* Our first attempt will use any default username passed
   in, and prompt for the remaining stuff. */
static svn_error_t *
username_prompt_first_creds(void **credentials_p,
                            void **iter_baton,
                            void *provider_baton,
                            apr_hash_t *parameters,
                            const char *realmstring,
                            apr_pool_t *pool)
{
  username_prompt_provider_baton_t *pb = provider_baton;
  username_prompt_iter_baton_t *ibaton = apr_pcalloc(pool, sizeof(*ibaton));
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  SVN_ERR(prompt_for_username_creds
          ((svn_auth_cred_username_t **) credentials_p, pb,
           parameters, realmstring, TRUE, ! no_auth_cache, pool));

  ibaton->retries = 0;
  *iter_baton = ibaton;

  return SVN_NO_ERROR;
}


/* Subsequent attempts to fetch will ignore the default username
   value, and simply re-prompt for the username, up to a maximum of
   ib->pb->retry_limit. */
static svn_error_t *
username_prompt_next_creds(void **credentials_p,
                           void *iter_baton,
                           void *provider_baton,
                           apr_hash_t *parameters,
                           const char *realmstring,
                           apr_pool_t *pool)
{
  username_prompt_iter_baton_t *ib = iter_baton;
  username_prompt_provider_baton_t *pb = provider_baton;
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  if ((pb->retry_limit >= 0) && (ib->retries >= pb->retry_limit))
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  return prompt_for_username_creds
         ((svn_auth_cred_username_t **) credentials_p, pb,
          parameters, realmstring, FALSE, ! no_auth_cache, pool);
}


static const svn_auth_provider_t username_prompt_provider = {
  SVN_AUTH_CRED_USERNAME,
  username_prompt_first_creds,
  username_prompt_next_creds,
  NULL,
};


/* Public API */
void
svn_auth_get_username_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_username_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  username_prompt_provider_baton_t *pb = apr_pcalloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &username_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
