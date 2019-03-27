/*
 * sasl_auth.c :  Functions for SASL-based authentication
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

#include "svn_private_config.h"
#ifdef SVN_HAVE_SASL

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_ra_svn.h"
#include "svn_base64.h"

#include "private/svn_atomic.h"
#include "private/ra_svn_sasl.h"
#include "private/svn_ra_svn_private.h"

#include "server.h"

/* SASL calls this function before doing anything with a username, which gives
   us an opportunity to do some sanity-checking.  If the username contains
   an '@', SASL interprets the part following the '@' as the name of the
   authentication realm, and worst of all, this realm overrides the one that
   we pass to sasl_server_new().  If we didn't check this, a user that could
   successfully authenticate in one realm would be able to authenticate
   in any other realm, simply by appending '@realm' to his username.

   Note that the value returned in *OUT does not need to be
   '\0'-terminated; we just need to set *OUT_LEN correctly.
*/
static int canonicalize_username(sasl_conn_t *conn,
                                 void *context, /* not used */
                                 const char *in, /* the username */
                                 unsigned inlen, /* its length */
                                 unsigned flags, /* not used */
                                 const char *user_realm,
                                 char *out, /* the output buffer */
                                 unsigned out_max, unsigned *out_len)
{
  size_t realm_len = strlen(user_realm);
  char *pos;

  *out_len = inlen;

  /* If the username contains an '@', the part after the '@' is the realm
     that the user wants to authenticate in. */
  pos = memchr(in, '@', inlen);
  if (pos)
    {
      /* The only valid realm is user_realm (i.e. the repository's realm).
         If the user gave us another realm, complain. */
      if (realm_len != inlen-(pos-in+1))
        return SASL_BADPROT;
      if (strncmp(pos+1, user_realm, inlen-(pos-in+1)) != 0)
        return SASL_BADPROT;
    }
  else
    *out_len += realm_len + 1;

  /* First, check that the output buffer is large enough. */
  if (*out_len > out_max)
    return SASL_BADPROT;

  /* Copy the username part. */
  strncpy(out, in, inlen);

  /* If necessary, copy the realm part. */
  if (!pos)
    {
      out[inlen] = '@';
      strncpy(&out[inlen+1], user_realm, realm_len);
    }

  return SASL_OK;
}

static sasl_callback_t callbacks[] =
{
  { SASL_CB_CANON_USER, (int (*)(void))canonicalize_username, NULL },
  { SASL_CB_LIST_END, NULL, NULL }
};

static svn_error_t *initialize(void *baton, apr_pool_t *pool)
{
  int result;
  SVN_ERR(svn_ra_svn__sasl_common_init(pool));

  /* The second parameter tells SASL to look for a configuration file
     named subversion.conf. */
  result = svn_sasl__server_init(callbacks, SVN_RA_SVN_SASL_NAME);
  if (result != SASL_OK)
    {
      svn_error_t *err = svn_error_create(
          SVN_ERR_RA_NOT_AUTHORIZED, NULL,
          svn_sasl__errstring(result, NULL, NULL));
      return svn_error_quick_wrap(err,
                                  _("Could not initialize the SASL library"));
    }
  return SVN_NO_ERROR;
}

svn_error_t *cyrus_init(apr_pool_t *pool)
{
  SVN_ERR(svn_atomic__init_once(&svn_ra_svn__sasl_status,
                                initialize, NULL, pool));
  return SVN_NO_ERROR;
}

/* Tell the client the authentication failed. This is only used during
   the authentication exchange (i.e. inside try_auth()). */
static svn_error_t *
fail_auth(svn_ra_svn_conn_t *conn, apr_pool_t *pool, sasl_conn_t *sasl_ctx)
{
  const char *msg = svn_sasl__errdetail(sasl_ctx);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c)", "failure", msg));
  return svn_ra_svn__flush(conn, pool);
}

/* Like svn_ra_svn_write_cmd_failure, but also clears the given error
   and sets it to SVN_NO_ERROR. */
static svn_error_t *
write_failure(svn_ra_svn_conn_t *conn, apr_pool_t *pool, svn_error_t **err_p)
{
  svn_error_t *write_err = svn_ra_svn__write_cmd_failure(conn, pool, *err_p);
  svn_error_clear(*err_p);
  *err_p = SVN_NO_ERROR;
  return write_err;
}

/* Used if we run into a SASL error outside try_auth(). */
static svn_error_t *
fail_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool, sasl_conn_t *sasl_ctx)
{
  svn_error_t *err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                      svn_sasl__errdetail(sasl_ctx));
  SVN_ERR(write_failure(conn, pool, &err));
  return svn_ra_svn__flush(conn, pool);
}

static svn_error_t *try_auth(svn_ra_svn_conn_t *conn,
                             sasl_conn_t *sasl_ctx,
                             apr_pool_t *pool,
                             server_baton_t *b,
                             svn_boolean_t *success)
{
  const char *out, *mech;
  const svn_string_t *arg = NULL, *in;
  unsigned int outlen;
  int result;
  svn_boolean_t use_base64;

  *success = FALSE;

  /* Read the client's chosen mech and the initial token. */
  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "w(?s)", &mech, &in));

  if (strcmp(mech, "EXTERNAL") == 0 && !in)
    in = svn_string_create(b->client_info->tunnel_user, pool);
  else if (in)
    in = svn_base64_decode_string(in, pool);

  /* For CRAM-MD5, we don't base64-encode stuff. */
  use_base64 = (strcmp(mech, "CRAM-MD5") != 0);

  /* sasl uses unsigned int for the length of strings, we use apr_size_t
   * which may not be the same size.  Deal with potential integer overflow */
  if (in && in->len > UINT_MAX)
    return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                             _("Initial token is too long"));

  result = svn_sasl__server_start(sasl_ctx, mech,
                                  in ? in->data : NULL,
                                  in ? (unsigned int) in->len : 0,
                                  &out, &outlen);

  if (result != SASL_OK && result != SASL_CONTINUE)
    return fail_auth(conn, pool, sasl_ctx);

  while (result == SASL_CONTINUE)
    {
      svn_ra_svn__item_t *item;

      arg = svn_string_ncreate(out, outlen, pool);
      /* Encode what we send to the client. */
      if (use_base64)
        arg = svn_base64_encode_string2(arg, TRUE, pool);

      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(s)", "step", arg));

      /* Read and decode the client response. */
      SVN_ERR(svn_ra_svn__read_item(conn, pool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return SVN_NO_ERROR;

      in = &item->u.string;
      if (use_base64)
        in = svn_base64_decode_string(in, pool);

      if (in->len > UINT_MAX)
        return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                 _("Step response is too long"));

      result = svn_sasl__server_step(sasl_ctx, in->data,
                                     (unsigned int) in->len,
                                     &out, &outlen);
    }

  if (result != SASL_OK)
    return fail_auth(conn, pool, sasl_ctx);

  /* Send our last response, if necessary. */
  if (outlen)
    arg = svn_base64_encode_string2(svn_string_ncreate(out, outlen, pool), TRUE,
                                    pool);
  else
    arg = NULL;

  *success = TRUE;
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(?s)", "success", arg));

  return SVN_NO_ERROR;
}

static apr_status_t sasl_dispose_cb(void *data)
{
  sasl_conn_t *sasl_ctx = (sasl_conn_t*) data;
  svn_sasl__dispose(&sasl_ctx);
  return APR_SUCCESS;
}

svn_error_t *cyrus_auth_request(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                server_baton_t *b,
                                enum access_type required,
                                svn_boolean_t needs_username)
{
  sasl_conn_t *sasl_ctx;
  apr_pool_t *subpool;
  apr_status_t apr_err;
  const char *localaddrport = NULL, *remoteaddrport = NULL;
  const char *mechlist;
  char hostname[APRMAXHOSTLEN + 1];
  sasl_security_properties_t secprops;
  svn_boolean_t success, no_anonymous;
  int mech_count, result = SASL_OK;

  SVN_ERR(svn_ra_svn__get_addresses(&localaddrport, &remoteaddrport,
                                        conn, pool));
  apr_err = apr_gethostname(hostname, sizeof(hostname), pool);
  if (apr_err)
    {
      svn_error_t *err = svn_error_wrap_apr(apr_err, _("Can't get hostname"));
      SVN_ERR(write_failure(conn, pool, &err));
      return svn_ra_svn__flush(conn, pool);
    }

  /* Create a SASL context. SASL_SUCCESS_DATA tells SASL that the protocol
     supports sending data along with the final "success" message. */
  result = svn_sasl__server_new(SVN_RA_SVN_SASL_NAME,
                                hostname, b->repository->realm,
                                localaddrport, remoteaddrport,
                                NULL, SASL_SUCCESS_DATA,
                                &sasl_ctx);
  if (result != SASL_OK)
    {
      svn_error_t *err = svn_error_create(
          SVN_ERR_RA_NOT_AUTHORIZED, NULL,
          svn_sasl__errstring(result, NULL, NULL));
      SVN_ERR(write_failure(conn, pool, &err));
      return svn_ra_svn__flush(conn, pool);
    }

  /* Make sure the context is always destroyed. */
  apr_pool_cleanup_register(b->pool, sasl_ctx, sasl_dispose_cb,
                            apr_pool_cleanup_null);

  /* Initialize security properties. */
  svn_ra_svn__default_secprops(&secprops);

  /* Don't allow ANONYMOUS if a username is required. */
  no_anonymous = needs_username || b->repository->anon_access < required;
  if (no_anonymous)
    secprops.security_flags |= SASL_SEC_NOANONYMOUS;

  secprops.min_ssf = b->repository->min_ssf;
  secprops.max_ssf = b->repository->max_ssf;

  /* Set security properties. */
  result = svn_sasl__setprop(sasl_ctx, SASL_SEC_PROPS, &secprops);
  if (result != SASL_OK)
    return fail_cmd(conn, pool, sasl_ctx);

  /* SASL needs to know if we are externally authenticated. */
  if (b->client_info->tunnel_user)
    result = svn_sasl__setprop(sasl_ctx, SASL_AUTH_EXTERNAL,
                               b->client_info->tunnel_user);
  if (result != SASL_OK)
    return fail_cmd(conn, pool, sasl_ctx);

  /* Get the list of mechanisms. */
  result = svn_sasl__listmech(sasl_ctx, NULL, NULL, " ", NULL,
                              &mechlist, NULL, &mech_count);

  if (result != SASL_OK)
    return fail_cmd(conn, pool, sasl_ctx);

  if (mech_count == 0)
    {
      svn_error_t *err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                          _("Could not obtain the list"
                                          " of SASL mechanisms"));
      SVN_ERR(write_failure(conn, pool, &err));
      return svn_ra_svn__flush(conn, pool);
    }

  /* Send the list of mechanisms and the realm to the client. */
  SVN_ERR(svn_ra_svn__write_cmd_response(conn, pool, "(w)c",
                                         mechlist, b->repository->realm));

  /* The main authentication loop. */
  subpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(subpool);
      SVN_ERR(try_auth(conn, sasl_ctx, subpool, b, &success));
    }
  while (!success);
  svn_pool_destroy(subpool);

  SVN_ERR(svn_ra_svn__enable_sasl_encryption(conn, sasl_ctx, pool));

  if (no_anonymous)
    {
      char *p;
      const void *user;

      /* Get the authenticated username. */
      result = svn_sasl__getprop(sasl_ctx, SASL_USERNAME, &user);

      if (result != SASL_OK)
        return fail_cmd(conn, pool, sasl_ctx);

      if ((p = strchr(user, '@')) != NULL)
        {
          /* Drop the realm part. */
          b->client_info->user = apr_pstrndup(b->pool, user,
                                              p - (const char *)user);
        }
      else
        {
          svn_error_t *err;
          err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                 _("Couldn't obtain the authenticated"
                                 " username"));
          SVN_ERR(write_failure(conn, pool, &err));
          return svn_ra_svn__flush(conn, pool);
        }
    }

  return SVN_NO_ERROR;
}

#endif /* SVN_HAVE_SASL */
