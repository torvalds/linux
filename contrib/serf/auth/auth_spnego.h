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

#ifndef AUTH_SPNEGO_H
#define AUTH_SPNEGO_H

#include <apr.h>
#include <apr_pools.h>
#include "serf.h"
#include "serf_private.h"

#if defined(SERF_HAVE_SSPI)
#define SERF_HAVE_SPNEGO
#define SERF_USE_SSPI
#elif defined(SERF_HAVE_GSSAPI)
#define SERF_HAVE_SPNEGO
#define SERF_USE_GSSAPI
#endif

#ifdef SERF_HAVE_SPNEGO

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serf__spnego_context_t serf__spnego_context_t;

typedef struct serf__spnego_buffer_t {
    apr_size_t length;
    void *value;
} serf__spnego_buffer_t;

/* Create outbound security context.
 *
 * All temporary allocations will be performed in SCRATCH_POOL, while security
 * context will be allocated in result_pool and will be destroyed automatically
 * on RESULT_POOL cleanup.
 *
 */
apr_status_t
serf__spnego_create_sec_context(serf__spnego_context_t **ctx_p,
                                const serf__authn_scheme_t *scheme,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Initialize outbound security context.
 *
 * The function is used to build a security context between the client
 * application and a remote peer.
 *
 * CTX is pointer to existing context created using
 * serf__spnego_create_sec_context() function.
 *
 * SERVICE is name of Kerberos service name. Usually 'HTTP'. HOSTNAME is
 * canonical name of destination server. Caller should resolve server's alias
 * to canonical name.
 *
 * INPUT_BUF is pointer structure describing input token if any. Should be
 * zero length on first call.
 *
 * OUTPUT_BUF will be populated with pointer to output data that should send
 * to destination server. This buffer will be automatically freed on
 * RESULT_POOL cleanup.
 *
 * All temporary allocations will be performed in SCRATCH_POOL.
 *
 * Return value:
 * - APR_EAGAIN The client must send the output token to the server and wait
 *   for a return token.
 *
 * - APR_SUCCESS The security context was successfully initialized. There is no
 *   need for another serf__spnego_init_sec_context call. If the function returns
 *   an output token, that is, if the OUTPUT_BUF is of nonzero length, that
 *   token must be sent to the server.
 *
 * Other returns values indicates error.
 */
apr_status_t
serf__spnego_init_sec_context(serf_connection_t *conn,
                              serf__spnego_context_t *ctx,
                              const char *service,
                              const char *hostname,
                              serf__spnego_buffer_t *input_buf,
                              serf__spnego_buffer_t *output_buf,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool
                              );

/*
 * Reset a previously created security context so we can start with a new one.
 *
 * This is triggered when the server requires per-request authentication,
 * where each request requires a new security context.
 */
apr_status_t
serf__spnego_reset_sec_context(serf__spnego_context_t *ctx);
    
#ifdef __cplusplus
}
#endif

#endif    /* SERF_HAVE_SPNEGO */

#endif    /* !AUTH_SPNEGO_H */
