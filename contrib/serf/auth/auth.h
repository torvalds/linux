/* ====================================================================
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

#ifndef AUTH_H
#define AUTH_H

#include "auth_spnego.h"

#ifdef __cplusplus
extern "C" {
#endif

void serf__encode_auth_header(const char **header, const char *protocol,
                              const char *data, apr_size_t data_len,
                              apr_pool_t *pool);

/* Prefixes the realm_name with a string containing scheme, hostname and port
   of the connection, for providing it to the application. */
const char *serf__construct_realm(peer_t peer,
                                  serf_connection_t *conn,
                                  const char *realm_name,
                                  apr_pool_t *pool);

/** Basic authentication **/
apr_status_t serf__init_basic(int code,
                              serf_context_t *ctx,
                              apr_pool_t *pool);
apr_status_t serf__init_basic_connection(const serf__authn_scheme_t *scheme,
                                         int code,
                                         serf_connection_t *conn,
                                         apr_pool_t *pool);
apr_status_t serf__handle_basic_auth(int code,
                                     serf_request_t *request,
                                     serf_bucket_t *response,
                                     const char *auth_hdr,
                                     const char *auth_attr,
                                     void *baton,
                                     apr_pool_t *pool);
apr_status_t serf__setup_request_basic_auth(peer_t peer,
                                            int code,
                                            serf_connection_t *conn,
                                            serf_request_t *request,
                                            const char *method,
                                            const char *uri,
                                            serf_bucket_t *hdrs_bkt);

/** Digest authentication **/
apr_status_t serf__init_digest(int code,
                               serf_context_t *ctx,
                               apr_pool_t *pool);
apr_status_t serf__init_digest_connection(const serf__authn_scheme_t *scheme,
                                          int code,
                                          serf_connection_t *conn,
                                          apr_pool_t *pool);
apr_status_t serf__handle_digest_auth(int code,
                                      serf_request_t *request,
                                      serf_bucket_t *response,
                                      const char *auth_hdr,
                                      const char *auth_attr,
                                      void *baton,
                                      apr_pool_t *pool);
apr_status_t serf__setup_request_digest_auth(peer_t peer,
                                             int code,
                                             serf_connection_t *conn,
                                             serf_request_t *request,
                                             const char *method,
                                             const char *uri,
                                             serf_bucket_t *hdrs_bkt);
apr_status_t serf__validate_response_digest_auth(const serf__authn_scheme_t *scheme,
                                                 peer_t peer,
                                                 int code,
                                                 serf_connection_t *conn,
                                                 serf_request_t *request,
                                                 serf_bucket_t *response,
                                                 apr_pool_t *pool);

#ifdef SERF_HAVE_SPNEGO
/** Kerberos authentication **/
apr_status_t serf__init_spnego(int code,
                               serf_context_t *ctx,
                               apr_pool_t *pool);
apr_status_t serf__init_spnego_connection(const serf__authn_scheme_t *scheme,
                                          int code,
                                          serf_connection_t *conn,
                                          apr_pool_t *pool);
apr_status_t serf__handle_spnego_auth(int code,
                                     serf_request_t *request,
                                     serf_bucket_t *response,
                                     const char *auth_hdr,
                                     const char *auth_attr,
                                     void *baton,
                                     apr_pool_t *pool);
apr_status_t serf__setup_request_spnego_auth(peer_t peer,
                                             int code,
                                             serf_connection_t *conn,
                                             serf_request_t *request,
                                             const char *method,
                                             const char *uri,
                                             serf_bucket_t *hdrs_bkt);
apr_status_t serf__validate_response_spnego_auth(const serf__authn_scheme_t *scheme,
                                                 peer_t peer,
                                                 int code,
                                                 serf_connection_t *conn,
                                                 serf_request_t *request,
                                                 serf_bucket_t *response,
                                                 apr_pool_t *pool);
#endif /* SERF_HAVE_SPNEGO */

#ifdef __cplusplus
}
#endif

#endif    /* !AUTH_H */
