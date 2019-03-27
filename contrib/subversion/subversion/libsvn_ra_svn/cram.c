/*
 * cram.c :  Minimal standalone CRAM-MD5 implementation
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



#define APR_WANT_STRFUNC
#define APR_WANT_STDIO
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_time.h>
#include <apr_md5.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra_svn.h"
#include "svn_config.h"
#include "svn_private_config.h"

#include "ra_svn.h"

static int hex_to_int(char c)
{
  return (c >= '0' && c <= '9') ? c - '0'
    : (c >= 'a' && c <= 'f') ? c - 'a' + 10
    : -1;
}

static char int_to_hex(int v)
{
  return (char)((v < 10) ? '0' + v : 'a' + (v - 10));
}

static svn_boolean_t hex_decode(unsigned char *hashval, const char *hexval)
{
  int i, h1, h2;

  for (i = 0; i < APR_MD5_DIGESTSIZE; i++)
    {
      h1 = hex_to_int(hexval[2 * i]);
      h2 = hex_to_int(hexval[2 * i + 1]);
      if (h1 == -1 || h2 == -1)
        return FALSE;
      hashval[i] = (unsigned char)((h1 << 4) | h2);
    }
  return TRUE;
}

static void hex_encode(char *hexval, const unsigned char *hashval)
{
  int i;

  for (i = 0; i < APR_MD5_DIGESTSIZE; i++)
    {
      hexval[2 * i] = int_to_hex((hashval[i] >> 4) & 0xf);
      hexval[2 * i + 1] = int_to_hex(hashval[i] & 0xf);
    }
}

static void compute_digest(unsigned char *digest, const char *challenge,
                           const char *password)
{
  unsigned char secret[64];
  apr_size_t len = strlen(password), i;
  apr_md5_ctx_t ctx;

  /* Munge the password into a 64-byte secret. */
  memset(secret, 0, sizeof(secret));
  if (len <= sizeof(secret))
    memcpy(secret, password, len);
  else
    apr_md5(secret, password, len);

  /* Compute MD5(secret XOR opad, MD5(secret XOR ipad, challenge)),
   * where ipad is a string of 0x36 and opad is a string of 0x5c. */
  for (i = 0; i < sizeof(secret); i++)
    secret[i] ^= 0x36;
  apr_md5_init(&ctx);
  apr_md5_update(&ctx, secret, sizeof(secret));
  apr_md5_update(&ctx, challenge, strlen(challenge));
  apr_md5_final(digest, &ctx);
  for (i = 0; i < sizeof(secret); i++)
    secret[i] ^= (0x36 ^ 0x5c);
  apr_md5_init(&ctx);
  apr_md5_update(&ctx, secret, sizeof(secret));
  apr_md5_update(&ctx, digest, APR_MD5_DIGESTSIZE);
  apr_md5_final(digest, &ctx);
}

/* Fail the authentication, from the server's perspective. */
static svn_error_t *fail(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                         const char *msg)
{
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c)", "failure", msg));
  return svn_error_trace(svn_ra_svn__flush(conn, pool));
}

/* If we can, make the nonce with random bytes.  If we can't... well,
 * it just has to be different each time.  The current time isn't
 * absolutely guaranteed to be different for each connection, but it
 * should prevent replay attacks in practice. */
static apr_status_t make_nonce(apr_uint64_t *nonce)
{
#if APR_HAS_RANDOM
  return apr_generate_random_bytes((unsigned char *) nonce, sizeof(*nonce));
#else
  *nonce = apr_time_now();
  return APR_SUCCESS;
#endif
}

svn_error_t *svn_ra_svn_cram_server(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    svn_config_t *pwdb, const char **user,
                                    svn_boolean_t *success)
{
  apr_status_t status;
  apr_uint64_t nonce;
  char hostbuf[APRMAXHOSTLEN + 1];
  unsigned char cdigest[APR_MD5_DIGESTSIZE], sdigest[APR_MD5_DIGESTSIZE];
  const char *challenge, *sep, *password;
  svn_ra_svn__item_t *item;
  svn_string_t *resp;

  *success = FALSE;

  /* Send a challenge. */
  status = make_nonce(&nonce);
  if (!status)
    status = apr_gethostname(hostbuf, sizeof(hostbuf), pool);
  if (status)
    return fail(conn, pool, "Internal server error in authentication");
  challenge = apr_psprintf(pool,
                           "<%" APR_UINT64_T_FMT ".%" APR_TIME_T_FMT "@%s>",
                           nonce, apr_time_now(), hostbuf);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c)", "step", challenge));

  /* Read the client's response and decode it into *user and cdigest. */
  SVN_ERR(svn_ra_svn__read_item(conn, pool, &item));
  if (item->kind != SVN_RA_SVN_STRING)  /* Very wrong; don't report failure */
    return SVN_NO_ERROR;
  resp = &item->u.string;
  sep = strrchr(resp->data, ' ');
  if (!sep || resp->len - (sep + 1 - resp->data) != APR_MD5_DIGESTSIZE * 2
      || !hex_decode(cdigest, sep + 1))
    return fail(conn, pool, "Malformed client response in authentication");
  *user = apr_pstrmemdup(pool, resp->data, sep - resp->data);

  /* Verify the digest against the password in pwfile. */
  svn_config_get(pwdb, &password, SVN_CONFIG_SECTION_USERS, *user, NULL);
  if (!password)
    return fail(conn, pool, "Username not found");
  compute_digest(sdigest, challenge, password);
  if (memcmp(cdigest, sdigest, sizeof(sdigest)) != 0)
    return fail(conn, pool, "Password incorrect");

  *success = TRUE;
  return svn_ra_svn__write_tuple(conn, pool, "w()", "success");
}

svn_error_t *svn_ra_svn__cram_client(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const char *user, const char *password,
                                     const char **message)
{
  const char *status, *str, *reply;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  char hex[2 * APR_MD5_DIGESTSIZE + 1];

  /* Read the server challenge. */
  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "w(?c)", &status, &str));
  if (strcmp(status, "failure") == 0 && str)
    {
      *message = str;
      return SVN_NO_ERROR;
    }
  else if (strcmp(status, "step") != 0 || !str)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Unexpected server response to authentication"));

  /* Write our response. */
  compute_digest(digest, str, password);
  hex_encode(hex, digest);
  hex[sizeof(hex) - 1] = '\0';
  reply = apr_psprintf(pool, "%s %s", user, hex);
  SVN_ERR(svn_ra_svn__write_cstring(conn, pool, reply));

  /* Read the success or failure response from the server. */
  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "w(?c)", &status, &str));
  if (strcmp(status, "failure") == 0 && str)
    {
      *message = str;
      return SVN_NO_ERROR;
    }
  else if (strcmp(status, "success") != 0 || str)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Unexpected server response to authentication"));

  *message = NULL;
  return SVN_NO_ERROR;
}
