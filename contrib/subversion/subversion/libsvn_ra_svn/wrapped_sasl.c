/*
 * wrapped_sasl.c :  wrapped SASL API
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

#include "private/ra_svn_wrapped_sasl.h"

/* See the comment at the top of svn_wrapped_sasl.h */
#ifdef __APPLE__
#  if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2)
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  endif
#endif /* __APPLE__ */

void
svn_sasl__set_mutex(sasl_mutex_alloc_t *alloc,
                    sasl_mutex_lock_t *lock,
                    sasl_mutex_unlock_t *unlock,
                    sasl_mutex_free_t *free)
{
  sasl_set_mutex(alloc, lock, unlock, free);
}

void
svn_sasl__done(void)
{
  sasl_done();
}

void
svn_sasl__dispose(sasl_conn_t **pconn)
{
  sasl_dispose(pconn);
}

const char *
svn_sasl__errstring(int saslerr, const char *langlist, const char **outlang)
{
  return sasl_errstring(saslerr, langlist, outlang);
}

const char *
svn_sasl__errdetail(sasl_conn_t *conn)
{
  return sasl_errdetail(conn);
}

int
svn_sasl__getprop(sasl_conn_t *conn, int propnum, const void **pvalue)
{
  return sasl_getprop(conn, propnum, pvalue);
}

int
svn_sasl__setprop(sasl_conn_t *conn, int propnum, const void *value)
{
  return sasl_setprop(conn, propnum, value);
}

int
svn_sasl__client_init(const sasl_callback_t *callbacks)
{
  return sasl_client_init(callbacks);
}

int
svn_sasl__client_new(const char *service,
                     const char *serverFQDN,
                     const char *iplocalport,
                     const char *ipremoteport,
                     const sasl_callback_t *prompt_supp,
                     unsigned flags,
                     sasl_conn_t **pconn)
{
  return sasl_client_new(service, serverFQDN, iplocalport, ipremoteport,
                         prompt_supp, flags, pconn);
}

int
svn_sasl__client_start(sasl_conn_t *conn,
                       const char *mechlist,
                       sasl_interact_t **prompt_need,
                       const char **clientout,
                       unsigned *clientoutlen,
                       const char **mech)
{
  return sasl_client_start(conn, mechlist, prompt_need,
                           clientout, clientoutlen, mech);
}

int
svn_sasl__client_step(sasl_conn_t *conn,
                      const char *serverin,
                      unsigned serverinlen,
                      sasl_interact_t **prompt_need,
                      const char **clientout,
                      unsigned *clientoutlen)
{
  return sasl_client_step(conn, serverin, serverinlen, prompt_need,
                          clientout, clientoutlen);
}

int
svn_sasl__server_init(const sasl_callback_t *callbacks,
                      const char *appname)
{
  return sasl_server_init(callbacks, appname);
}

int
svn_sasl__server_new(const char *service,
                     const char *serverFQDN,
                     const char *user_realm,
                     const char *iplocalport,
                     const char *ipremoteport,
                     const sasl_callback_t *callbacks,
                     unsigned flags,
                     sasl_conn_t **pconn)
{
  return sasl_server_new(service, serverFQDN, user_realm,
                         iplocalport, ipremoteport, callbacks, flags, pconn);
}

int
svn_sasl__listmech(sasl_conn_t *conn,
                   const char *user,
                   const char *prefix,
                   const char *sep,
                   const char *suffix,
                   const char **result,
                   unsigned *plen,
                   int *pcount)
{
  return sasl_listmech(conn, user, prefix, sep, suffix, result, plen, pcount);
}

int
svn_sasl__server_start(sasl_conn_t *conn,
                       const char *mech,
                       const char *clientin,
                       unsigned clientinlen,
                       const char **serverout,
                       unsigned *serveroutlen)
{
  return sasl_server_start(conn, mech, clientin, clientinlen,
                           serverout, serveroutlen);
}

int
svn_sasl__server_step(sasl_conn_t *conn,
                      const char *clientin,
                      unsigned clientinlen,
                      const char **serverout,
                      unsigned *serveroutlen)
{
  return sasl_server_step(conn, clientin, clientinlen,
                          serverout, serveroutlen);
}

int
svn_sasl__encode(sasl_conn_t *conn,
                 const char *input, unsigned inputlen,
                 const char **output, unsigned *outputlen)
{
  return sasl_encode(conn, input, inputlen, output, outputlen);
}

int
svn_sasl__decode(sasl_conn_t *conn,
                 const char *input, unsigned inputlen,
                 const char **output, unsigned *outputlen)
{
  return sasl_decode(conn, input, inputlen, output, outputlen);
}

#endif /* SVN_HAVE_SASL */
