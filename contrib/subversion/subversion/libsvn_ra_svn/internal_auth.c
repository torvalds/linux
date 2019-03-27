/*
 * simple_auth.c :  Simple SASL-based authentication, used in case
 * Cyrus SASL isn't available.
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

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"

#include "ra_svn.h"

svn_boolean_t svn_ra_svn__find_mech(const svn_ra_svn__list_t *mechlist,
                                    const char *mech)
{
  int i;
  svn_ra_svn__item_t *elt;

  for (i = 0; i < mechlist->nelts; i++)
    {
      elt = &SVN_RA_SVN__LIST_ITEM(mechlist, i);
      if (elt->kind == SVN_RA_SVN_WORD && strcmp(elt->u.word.data, mech) == 0)
        return TRUE;
    }
  return FALSE;
}

/* Read the "success" response to ANONYMOUS or EXTERNAL authentication. */
static svn_error_t *read_success(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  const char *status, *arg;

  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "w(?c)", &status, &arg));
  if (strcmp(status, "failure") == 0 && arg)
    return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                             _("Authentication error from server: %s"), arg);
  else if (strcmp(status, "success") != 0 || arg)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Unexpected server response to authentication"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__do_internal_auth(svn_ra_svn__session_baton_t *sess,
                             const svn_ra_svn__list_t *mechlist,
                             const char *realm, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess->conn;
  const char *realmstring, *user, *password, *msg;
  svn_auth_iterstate_t *iterstate;
  void *creds;

  realmstring = apr_psprintf(pool, "%s %s", sess->realm_prefix, realm);

  if (sess->is_tunneled && svn_ra_svn__find_mech(mechlist, "EXTERNAL"))
    {
        /* Ask the server to use the tunnel connection environment (on
        * Unix, that means uid) to determine the authentication name. */
      SVN_ERR(svn_ra_svn__auth_response(conn, pool, "EXTERNAL", ""));
      return read_success(conn, pool);
    }
  else if (svn_ra_svn__find_mech(mechlist, "ANONYMOUS"))
    {
      SVN_ERR(svn_ra_svn__auth_response(conn, pool, "ANONYMOUS", ""));
      return read_success(conn, pool);
    }
  else if (svn_ra_svn__find_mech(mechlist, "CRAM-MD5"))
    {
      SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                         SVN_AUTH_CRED_SIMPLE, realmstring,
                                         sess->auth_baton, pool));
      if (!creds)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Can't get password"));
      while (creds)
        {
          user = ((svn_auth_cred_simple_t *) creds)->username;
          password = ((svn_auth_cred_simple_t *) creds)->password;
          SVN_ERR(svn_ra_svn__auth_response(conn, pool, "CRAM-MD5", NULL));
          SVN_ERR(svn_ra_svn__cram_client(conn, pool, user, password, &msg));
          if (!msg)
            break;
          SVN_ERR(svn_auth_next_credentials(&creds, iterstate, pool));
        }
      if (!creds)
        return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Authentication error from server: %s"),
                                msg);
      SVN_ERR(svn_auth_save_credentials(iterstate, pool));
      return SVN_NO_ERROR;
    }
  else
    return svn_error_create(SVN_ERR_RA_SVN_NO_MECHANISMS, NULL, NULL);
}
