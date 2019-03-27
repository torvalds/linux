/*
 * win32_crypto.c: win32 providers for SVN_AUTH_*
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

/* prevent "empty compilation unit" warning on e.g. UNIX */
typedef int win32_crypto__dummy;

/* ==================================================================== */

#if defined(WIN32) && !defined(__MINGW32__)

/*** Includes. ***/

#include <apr_pools.h>
#include <apr_base64.h>
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_user.h"
#include "svn_base64.h"

#include "auth.h"
#include "private/svn_auth_private.h"

#include "svn_private_config.h"

#include <wincrypt.h>


/* The description string that's combined with unencrypted data by the
   Windows CryptoAPI. Used during decryption to verify that the
   encrypted data were valid. */
static const WCHAR description[] = L"auth_svn.simple.wincrypt";


/* Return a copy of ORIG, encrypted using the Windows CryptoAPI and
   allocated from POOL. */
const svn_string_t *
encrypt_data(const svn_string_t *orig,
             apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  const svn_string_t *crypted = NULL;

  blobin.cbData = orig->len;
  blobin.pbData = (BYTE *)orig->data;
  if (CryptProtectData(&blobin, description, NULL, NULL, NULL,
                       CRYPTPROTECT_UI_FORBIDDEN, &blobout))
    {
      crypted = svn_string_ncreate((const char *)blobout.pbData,
                                   blobout.cbData, pool);
      LocalFree(blobout.pbData);
    }
  return crypted;
}

/* Return a copy of CRYPTED, decrypted using the Windows CryptoAPI and
   allocated from POOL. */
const svn_string_t *
decrypt_data(const svn_string_t *crypted,
             apr_pool_t *pool)
{
  DATA_BLOB blobin;
  DATA_BLOB blobout;
  LPWSTR descr;
  const svn_string_t *orig = NULL;

  blobin.cbData = crypted->len;
  blobin.pbData = (BYTE *)crypted->data;
  if (CryptUnprotectData(&blobin, &descr, NULL, NULL, NULL,
                         CRYPTPROTECT_UI_FORBIDDEN, &blobout))
    {
      if (0 == lstrcmpW(descr, description))
        orig = svn_string_ncreate((const char *)blobout.pbData,
                                  blobout.cbData, pool);
      LocalFree(blobout.pbData);
      LocalFree(descr);
    }
  return orig;
}


/*-----------------------------------------------------------------------*/
/* Windows simple provider, encrypts the password on Win2k and later.    */
/*-----------------------------------------------------------------------*/

/* Implementation of svn_auth__password_set_t that encrypts
   the incoming password using the Windows CryptoAPI. */
static svn_error_t *
windows_password_encrypter(svn_boolean_t *done,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           const char *in,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  const svn_string_t *coded;

  coded = encrypt_data(svn_string_create(in, pool), pool);
  if (coded)
    {
      coded = svn_base64_encode_string2(coded, FALSE, pool);
      SVN_ERR(svn_auth__simple_password_set(done, creds, realmstring, username,
                                            coded->data, parameters,
                                            non_interactive, pool));
    }

  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_get_t that decrypts
   the incoming password using the Windows CryptoAPI and verifies its
   validity. */
static svn_error_t *
windows_password_decrypter(svn_boolean_t *done,
                           const char **out,
                           apr_hash_t *creds,
                           const char *realmstring,
                           const char *username,
                           apr_hash_t *parameters,
                           svn_boolean_t non_interactive,
                           apr_pool_t *pool)
{
  const svn_string_t *orig;
  const char *in;

  SVN_ERR(svn_auth__simple_password_get(done, &in, creds, realmstring, username,
                                        parameters, non_interactive, pool));
  if (!*done)
    return SVN_NO_ERROR;

  orig = svn_base64_decode_string(svn_string_create(in, pool), pool);
  orig = decrypt_data(orig, pool);
  if (orig)
    {
      *out = orig->data;
      *done = TRUE;
    }
  else
    {
      *done = FALSE;
    }
  return SVN_NO_ERROR;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
windows_simple_first_creds(void **credentials,
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
                                          windows_password_decrypter,
                                          SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
                                          pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
windows_simple_save_creds(svn_boolean_t *saved,
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
                                          windows_password_encrypter,
                                          SVN_AUTH__WINCRYPT_PASSWORD_TYPE,
                                          pool);
}

static const svn_auth_provider_t windows_simple_provider = {
  SVN_AUTH_CRED_SIMPLE,
  windows_simple_first_creds,
  NULL,
  windows_simple_save_creds
};


/* Public API */
void
svn_auth__get_windows_simple_provider(svn_auth_provider_object_t **provider,
                                     apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &windows_simple_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Windows SSL server trust provider, validates ssl certificate using    */
/* CryptoApi.                                                            */
/*-----------------------------------------------------------------------*/

/* Implementation of svn_auth__password_set_t that encrypts
   the incoming password using the Windows CryptoAPI. */
static svn_error_t *
windows_ssl_client_cert_pw_encrypter(svn_boolean_t *done,
                                     apr_hash_t *creds,
                                     const char *realmstring,
                                     const char *username,
                                     const char *in,
                                     apr_hash_t *parameters,
                                     svn_boolean_t non_interactive,
                                     apr_pool_t *pool)
{
  const svn_string_t *coded;

  coded = encrypt_data(svn_string_create(in, pool), pool);
  if (coded)
    {
      coded = svn_base64_encode_string2(coded, FALSE, pool);
      SVN_ERR(svn_auth__ssl_client_cert_pw_set(done, creds, realmstring,
                                               username, coded->data,
                                               parameters, non_interactive,
                                               pool));
    }

  return SVN_NO_ERROR;
}

/* Implementation of svn_auth__password_get_t that decrypts
   the incoming password using the Windows CryptoAPI and verifies its
   validity. */
static svn_error_t *
windows_ssl_client_cert_pw_decrypter(svn_boolean_t *done,
                                     const char **out,
                                     apr_hash_t *creds,
                                     const char *realmstring,
                                     const char *username,
                                     apr_hash_t *parameters,
                                     svn_boolean_t non_interactive,
                                     apr_pool_t *pool)
{
  const svn_string_t *orig;
  const char *in;

  SVN_ERR(svn_auth__ssl_client_cert_pw_get(done, &in, creds, realmstring,
                                           username, parameters,
                                           non_interactive, pool));
  if (!*done)
    return SVN_NO_ERROR;

  orig = svn_base64_decode_string(svn_string_create(in, pool), pool);
  orig = decrypt_data(orig, pool);
  if (orig)
    {
      *out = orig->data;
      *done = TRUE;
    }
  else
    {
      *done = FALSE;
    }
  return SVN_NO_ERROR;
}

/* Get cached encrypted credentials from the simple provider's cache. */
static svn_error_t *
windows_ssl_client_cert_pw_first_creds(void **credentials,
                                       void **iter_baton,
                                       void *provider_baton,
                                       apr_hash_t *parameters,
                                       const char *realmstring,
                                       apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_get(
             credentials, iter_baton, provider_baton, parameters, realmstring,
             windows_ssl_client_cert_pw_decrypter,
             SVN_AUTH__WINCRYPT_PASSWORD_TYPE, pool);
}

/* Save encrypted credentials to the simple provider's cache. */
static svn_error_t *
windows_ssl_client_cert_pw_save_creds(svn_boolean_t *saved,
                                      void *credentials,
                                      void *provider_baton,
                                      apr_hash_t *parameters,
                                      const char *realmstring,
                                      apr_pool_t *pool)
{
  return svn_auth__ssl_client_cert_pw_cache_set(
             saved, credentials, provider_baton, parameters, realmstring,
             windows_ssl_client_cert_pw_encrypter,
             SVN_AUTH__WINCRYPT_PASSWORD_TYPE, pool);
}

static const svn_auth_provider_t windows_ssl_client_cert_pw_provider = {
  SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
  windows_ssl_client_cert_pw_first_creds,
  NULL,
  windows_ssl_client_cert_pw_save_creds
};


/* Public API */
void
svn_auth__get_windows_ssl_client_cert_pw_provider
   (svn_auth_provider_object_t **provider,
    apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &windows_ssl_client_cert_pw_provider;
  *provider = po;
}


/*-----------------------------------------------------------------------*/
/* Windows SSL server trust provider, validates ssl certificate using    */
/* CryptoApi.                                                            */
/*-----------------------------------------------------------------------*/

/* Helper to create CryptoAPI CERT_CONTEXT from base64 encoded BASE64_CERT.
 * Returns NULL on error.
 */
static PCCERT_CONTEXT
certcontext_from_base64(const char *base64_cert, apr_pool_t *pool)
{
  PCCERT_CONTEXT cert_context = NULL;
  int cert_len;
  BYTE *binary_cert;

  /* Use apr-util as CryptStringToBinaryA is available only on XP+. */
  binary_cert = apr_palloc(pool,
                           apr_base64_decode_len(base64_cert));
  cert_len = apr_base64_decode((char*)binary_cert, base64_cert);

  /* Parse the certificate into a context. */
  cert_context = CertCreateCertificateContext
    (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, binary_cert, cert_len);

  return cert_context;
}

/* Helper for windows_ssl_server_trust_first_credentials for validating
 * certificate using CryptoApi. Sets *OK_P to TRUE if base64 encoded ASCII_CERT
 * certificate considered as valid.
 */
static svn_error_t *
windows_validate_certificate(svn_boolean_t *ok_p,
                             const char *ascii_cert,
                             apr_pool_t *pool)
{
  PCCERT_CONTEXT cert_context = NULL;
  CERT_CHAIN_PARA chain_para;
  PCCERT_CHAIN_CONTEXT chain_context = NULL;

  *ok_p = FALSE;

  /* Parse the certificate into a context. */
  cert_context = certcontext_from_base64(ascii_cert, pool);

  if (cert_context)
    {
      /* Retrieve the certificate chain of the certificate
         (a certificate without a valid root does not have a chain). */
      memset(&chain_para, 0, sizeof(chain_para));
      chain_para.cbSize = sizeof(chain_para);

      if (CertGetCertificateChain(NULL, cert_context, NULL, NULL, &chain_para,
                                  CERT_CHAIN_CACHE_END_CERT |
                                  CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT,
                                  NULL, &chain_context))
        {
          CERT_CHAIN_POLICY_PARA policy_para;
          CERT_CHAIN_POLICY_STATUS policy_status;

          policy_para.cbSize = sizeof(policy_para);
          policy_para.dwFlags = 0;
          policy_para.pvExtraPolicyPara = NULL;

          policy_status.cbSize = sizeof(policy_status);

          if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                               chain_context, &policy_para,
                                               &policy_status))
            {
              if (policy_status.dwError == S_OK)
                {
                  /* Windows thinks the certificate is valid. */
                  *ok_p = TRUE;
                }
            }

          CertFreeCertificateChain(chain_context);
        }
      CertFreeCertificateContext(cert_context);
    }

  return SVN_NO_ERROR;
}

/* Retrieve ssl server CA failure overrides (if any) from CryptoApi. */
static svn_error_t *
windows_ssl_server_trust_first_credentials(void **credentials,
                                           void **iter_baton,
                                           void *provider_baton,
                                           apr_hash_t *parameters,
                                           const char *realmstring,
                                           apr_pool_t *pool)
{
  apr_uint32_t *failure_ptr = svn_hash_gets(parameters,
                                            SVN_AUTH_PARAM_SSL_SERVER_FAILURES);
  apr_uint32_t failures = *failure_ptr;
  const svn_auth_ssl_server_cert_info_t *cert_info =
    svn_hash_gets(parameters, SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO);

  *credentials = NULL;
  *iter_baton = NULL;

  /* We can accept only unknown certificate authority. */
  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      svn_boolean_t ok;

      SVN_ERR(windows_validate_certificate(&ok, cert_info->ascii_cert, pool));

      /* Windows thinks that certificate is ok. */
      if (ok)
        {
          /* Clear failure flag. */
          failures &= ~SVN_AUTH_SSL_UNKNOWNCA;
        }
    }

  /* If all failures are cleared now, we return the creds */
  if (! failures)
    {
      svn_auth_cred_ssl_server_trust_t *creds =
        apr_pcalloc(pool, sizeof(*creds));
      creds->accepted_failures = *failure_ptr & ~failures;
      creds->may_save = FALSE; /* No need to save it. */
      *credentials = creds;
    }

  return SVN_NO_ERROR;
}

static const svn_auth_provider_t windows_server_trust_provider = {
  SVN_AUTH_CRED_SSL_SERVER_TRUST,
  windows_ssl_server_trust_first_credentials,
  NULL,
  NULL,
};

/* Public API */
void
svn_auth__get_windows_ssl_server_trust_provider
  (svn_auth_provider_object_t **provider, apr_pool_t *pool)
{
  svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

  po->vtable = &windows_server_trust_provider;
  *provider = po;
}

static const svn_auth_provider_t windows_server_authority_provider = {
    SVN_AUTH_CRED_SSL_SERVER_AUTHORITY,
    windows_ssl_server_trust_first_credentials,
    NULL,
    NULL,
};

/* Public API */
void
svn_auth__get_windows_ssl_server_authority_provider(
                            svn_auth_provider_object_t **provider,
                            apr_pool_t *pool)
{
    svn_auth_provider_object_t *po = apr_pcalloc(pool, sizeof(*po));

    po->vtable = &windows_server_authority_provider;
    *provider = po;
}


#else  /* !WIN32 */

/* Silence OSX ranlib warnings about object files with no symbols. */
#include <apr.h>
extern const apr_uint32_t svn__fake__win32_crypto;
const apr_uint32_t svn__fake__win32_crypto = 0xdeadbeef;

#endif /* WIN32 */
