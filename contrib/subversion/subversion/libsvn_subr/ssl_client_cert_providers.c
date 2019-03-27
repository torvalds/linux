/*
 * ssl_client_cert_providers.c: providers for
 * SVN_AUTH_CRED_SSL_CLIENT_CERT
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
#include "svn_config.h"


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* retrieve and load the ssl client certificate file from servers
   config */
static svn_error_t *
ssl_client_cert_file_first_credentials(void **credentials_p,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  svn_config_t *cfg = svn_hash_gets(parameters,
                                    SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS);
  const char *server_group = svn_hash_gets(parameters,
                                           SVN_AUTH_PARAM_SERVER_GROUP);
  const char *cert_file;

  cert_file =
    svn_config_get_server_setting(cfg, server_group,
                                  SVN_CONFIG_OPTION_SSL_CLIENT_CERT_FILE,
                                  NULL);

  if (cert_file != NULL)
    {
      svn_auth_cred_ssl_client_cert_t *cred =
        apr_palloc(pool, sizeof(*cred));

      cred->cert_file = cert_file;
      cred->may_save = FALSE;
      *credentials_p = cred;
    }
  else
    {
      *credentials_p = NULL;
    }

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_client_cert_file_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT,
  ssl_client_cert_file_first_credentials,
  NULL,
  NULL
};


/*** Public API to SSL file providers. ***/
void svn_auth_get_ssl_client_cert_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  po->vtable = &ssl_client_cert_file_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for prompting to send client ssl creds.
   There is no iteration baton type. */
typedef struct ssl_client_cert_prompt_provider_baton_t
{
  svn_auth_ssl_client_cert_prompt_func_t prompt_func;
  void *prompt_baton;

  /* how many times to re-prompt after the first one fails */
  int retry_limit;
} ssl_client_cert_prompt_provider_baton_t;

/* Iteration baton. */
typedef struct ssl_client_cert_prompt_iter_baton_t
{
  /* The original provider baton */
  ssl_client_cert_prompt_provider_baton_t *pb;

  /* The original realmstring */
  const char *realmstring;

  /* how many times we've reprompted */
  int retries;
} ssl_client_cert_prompt_iter_baton_t;


static svn_error_t *
ssl_client_cert_prompt_first_cred(void **credentials_p,
                                  void **iter_baton,
                                  void *provider_baton,
                                  apr_hash_t *parameters,
                                  const char *realmstring,
                                  apr_pool_t *pool)
{
  ssl_client_cert_prompt_provider_baton_t *pb = provider_baton;
  ssl_client_cert_prompt_iter_baton_t *ib =
    apr_pcalloc(pool, sizeof(*ib));
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  SVN_ERR(pb->prompt_func((svn_auth_cred_ssl_client_cert_t **) credentials_p,
                          pb->prompt_baton, realmstring, ! no_auth_cache,
                          pool));

  ib->pb = pb;
  ib->realmstring = apr_pstrdup(pool, realmstring);
  ib->retries = 0;
  *iter_baton = ib;

  return SVN_NO_ERROR;
}


static svn_error_t *
ssl_client_cert_prompt_next_cred(void **credentials_p,
                                 void *iter_baton,
                                 void *provider_baton,
                                 apr_hash_t *parameters,
                                 const char *realmstring,
                                 apr_pool_t *pool)
{
  ssl_client_cert_prompt_iter_baton_t *ib = iter_baton;
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);

  if ((ib->pb->retry_limit >= 0) && (ib->retries >= ib->pb->retry_limit))
    {
      /* give up, go on to next provider. */
      *credentials_p = NULL;
      return SVN_NO_ERROR;
    }
  ib->retries++;

  return ib->pb->prompt_func((svn_auth_cred_ssl_client_cert_t **)
                             credentials_p, ib->pb->prompt_baton,
                             ib->realmstring, ! no_auth_cache, pool);
}


static const svn_auth_provider_t ssl_client_cert_prompt_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT,
  ssl_client_cert_prompt_first_cred,
  ssl_client_cert_prompt_next_cred,
  NULL
};


/*** Public API to SSL prompting providers. ***/
void svn_auth_get_ssl_client_cert_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_client_cert_prompt_func_t prompt_func,
   void *prompt_baton,
   int retry_limit,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  ssl_client_cert_prompt_provider_baton_t *pb = apr_palloc(pool, sizeof(*pb));

  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  pb->retry_limit = retry_limit;

  po->vtable = &ssl_client_cert_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
