/*
 * ssl_server_trust_providers.c: providers for
 * SVN_AUTH_CRED_SSL_SERVER_TRUST
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


/*-----------------------------------------------------------------------*/
/* File provider                                                         */
/*-----------------------------------------------------------------------*/

/* retrieve ssl server CA failure overrides (if any) from servers
   config */
static svn_error_t *
ssl_server_trust_file_first_credentials(void **credentials,
                                        void **iter_baton,
                                        void *provider_baton,
                                        apr_hash_t *parameters,
                                        const char *realmstring,
                                        apr_pool_t *pool)
{
  apr_uint32_t *failures = svn_hash_gets(parameters,
                                         SVN_AUTH_PARAM_SSL_SERVER_FAILURES);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    svn_hash_gets(parameters, SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO);
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;
  svn_error_t *error = SVN_NO_ERROR;

  *credentials = NULL;
  *iter_baton = NULL;

  /* Check if this is a permanently accepted certificate */
  config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);
  error =
    svn_config_read_auth_data(&creds_hash, SVN_AUTH_CRED_SSL_SERVER_TRUST,
                              realmstring, config_dir, pool);
  svn_error_clear(error);
  if (! error && creds_hash)
    {
      svn_string_t *trusted_cert, *this_cert, *failstr;
      apr_uint32_t last_failures = 0;

      trusted_cert = svn_hash_gets(creds_hash, SVN_CONFIG_AUTHN_ASCII_CERT_KEY);
      this_cert = svn_string_create(cert_info->ascii_cert, pool);
      failstr = svn_hash_gets(creds_hash, SVN_CONFIG_AUTHN_FAILURES_KEY);

      if (failstr)
        SVN_ERR(svn_cstring_atoui(&last_failures, failstr->data));

      /* If the cert is trusted and there are no new failures, we
       * accept it by clearing all failures. */
      if (trusted_cert &&
          svn_string_compare(this_cert, trusted_cert) &&
          (*failures & ~last_failures) == 0)
        {
          *failures = 0;
        }
    }

  /* If all failures are cleared now, we return the creds */
  if (! *failures)
    {
      svn_auth_cred_ssl_server_trust_t *creds =
        apr_pcalloc(pool, sizeof(*creds));
      creds->may_save = FALSE; /* No need to save it again... */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
ssl_server_trust_file_save_credentials(svn_boolean_t *saved,
                                       void *credentials,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  svn_auth_cred_ssl_server_trust_t *creds = credentials;
  const svn_auth_ssl_server_cert_info_t *cert_info;
  apr_hash_t *creds_hash = NULL;
  const char *config_dir;

  if (! creds->may_save)
    return SVN_NO_ERROR;

  config_dir = svn_hash_gets(parameters, SVN_AUTH_PARAM_CONFIG_DIR);

  cert_info = svn_hash_gets(parameters, SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO);

  creds_hash = apr_hash_make(pool);
  svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_ASCII_CERT_KEY,
                svn_string_create(cert_info->ascii_cert, pool));
  svn_hash_sets(creds_hash, SVN_CONFIG_AUTHN_FAILURES_KEY,
                svn_string_createf(pool, "%lu",
                                   (unsigned long)creds->accepted_failures));

  SVN_ERR(svn_config_write_auth_data(creds_hash,
                                     SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                     realmstring,
                                     config_dir,
                                     pool));
  *saved = TRUE;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_server_trust_file_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  ssl_server_trust_file_first_credentials,
  NULL,
  ssl_server_trust_file_save_credentials,
};


/*** Public API to SSL file providers. ***/
void
svn_auth_get_ssl_server_trust_file_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &ssl_server_trust_file_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Prompt provider                                                       */
/*-----------------------------------------------------------------------*/

/* Baton type for prompting to verify server ssl creds.
   There is no iteration baton type. */
typedef struct ssl_server_trust_prompt_provider_baton_t
{
  svn_auth_ssl_server_trust_prompt_func_t prompt_func;
  void *prompt_baton;
} ssl_server_trust_prompt_provider_baton_t;


static svn_error_t *
ssl_server_trust_prompt_first_cred(void **credentials_p,
                                   void **iter_baton,
                                   void *provider_baton,
                                   apr_hash_t *parameters,
                                   const char *realmstring,
                                   apr_pool_t *pool)
{
  ssl_server_trust_prompt_provider_baton_t *pb = provider_baton;
  apr_uint32_t *failures = svn_hash_gets(parameters,
                                         SVN_AUTH_PARAM_SSL_SERVER_FAILURES);
  const char *no_auth_cache = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_NO_AUTH_CACHE);
  const svn_auth_ssl_server_cert_info_t *cert_info =
    svn_hash_gets(parameters, SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO);
  svn_boolean_t may_save = (!no_auth_cache
                            && !(*failures & SVN_AUTH_SSL_OTHER));

  SVN_ERR(pb->prompt_func((svn_auth_cred_ssl_server_trust_t **)credentials_p,
                          pb->prompt_baton, realmstring, *failures, cert_info,
                          may_save, pool));

  *iter_baton = NULL;
  return SVN_NO_ERROR;
}


static const svn_auth_provider_t ssl_server_trust_prompt_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  ssl_server_trust_prompt_first_cred,
  NULL,
  NULL
};


/*** Public API to SSL prompting providers. ***/
void
svn_auth_get_ssl_server_trust_prompt_provider
  (svn_auth_provider_object_t **provider,
   svn_auth_ssl_server_trust_prompt_func_t prompt_func,
   void *prompt_baton,
   apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));
  ssl_server_trust_prompt_provider_baton_t *pb =
    apr_palloc(pool, sizeof(*pb));
  pb->prompt_func = prompt_func;
  pb->prompt_baton = prompt_baton;
  po->vtable = &ssl_server_trust_prompt_provider;
  po->provider_baton = pb;
  *provider = po;
}
