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

#include "serf.h"
#include "serf_private.h"
#include "auth_spnego.h"

#ifdef SERF_USE_GSSAPI
#include <apr_strings.h>
#include <gssapi/gssapi.h>


/* This module can support all authentication mechanisms as provided by
   the GSS-API implementation, but for now it only supports SPNEGO for
   Negotiate.
   SPNEGO can delegate authentication to Kerberos if supported by the
   host. */

#ifndef GSS_SPNEGO_MECHANISM
static gss_OID_desc spnego_mech_oid = { 6, "\x2b\x06\x01\x05\x05\x02" };
#define GSS_SPNEGO_MECHANISM &spnego_mech_oid
#endif

struct serf__spnego_context_t
{
    /* GSSAPI context */
    gss_ctx_id_t gss_ctx;

    /* Mechanism used to authenticate. */
    gss_OID gss_mech;
};

static void
log_error(int verbose_flag, apr_socket_t *skt,
          serf__spnego_context_t *ctx,
          OM_uint32 err_maj_stat,
          OM_uint32 err_min_stat,
          const char *msg)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc stat_buff;
    OM_uint32 msg_ctx = 0;

    if (verbose_flag) {
        maj_stat = gss_display_status(&min_stat,
                                      err_maj_stat,
                                      GSS_C_GSS_CODE,
                                      ctx->gss_mech,
                                      &msg_ctx,
                                      &stat_buff);
        if (maj_stat == GSS_S_COMPLETE ||
            maj_stat == GSS_S_FAILURE) {
            maj_stat = gss_display_status(&min_stat,
                                          err_min_stat,
                                          GSS_C_MECH_CODE,
                                          ctx->gss_mech,
                                          &msg_ctx,
                                          &stat_buff);
        }

        serf__log_skt(verbose_flag, __FILE__, skt,
                  "%s (%x,%d): %s\n", msg,
                  err_maj_stat, err_min_stat, stat_buff.value);
    }
}

/* Cleans the GSS context object, when the pool used to create it gets
   cleared or destroyed. */
static apr_status_t
cleanup_ctx(void *data)
{
    serf__spnego_context_t *ctx = data;

    if (ctx->gss_ctx != GSS_C_NO_CONTEXT) {
        OM_uint32 gss_min_stat, gss_maj_stat;

        gss_maj_stat = gss_delete_sec_context(&gss_min_stat, &ctx->gss_ctx,
                                              GSS_C_NO_BUFFER);
        if(GSS_ERROR(gss_maj_stat)) {
            log_error(AUTH_VERBOSE, NULL, ctx,
                      gss_maj_stat, gss_min_stat,
                      "Error cleaning up GSS security context");
            return SERF_ERROR_AUTHN_FAILED;
        }
    }

    return APR_SUCCESS;
}

static apr_status_t
cleanup_sec_buffer(void *data)
{
    OM_uint32 min_stat;
    gss_buffer_desc *gss_buf = data;

    gss_release_buffer(&min_stat, gss_buf);

    return APR_SUCCESS;
}

apr_status_t
serf__spnego_create_sec_context(serf__spnego_context_t **ctx_p,
                                const serf__authn_scheme_t *scheme,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
    serf__spnego_context_t *ctx;

    ctx = apr_pcalloc(result_pool, sizeof(*ctx));

    ctx->gss_ctx = GSS_C_NO_CONTEXT;
    ctx->gss_mech = GSS_SPNEGO_MECHANISM;

    apr_pool_cleanup_register(result_pool, ctx,
                              cleanup_ctx,
                              apr_pool_cleanup_null);

    *ctx_p = ctx;

    return APR_SUCCESS;
}

apr_status_t
serf__spnego_reset_sec_context(serf__spnego_context_t *ctx)
{
    OM_uint32 dummy_stat;

    if (ctx->gss_ctx)
        (void)gss_delete_sec_context(&dummy_stat, &ctx->gss_ctx,
                                     GSS_C_NO_BUFFER);
    ctx->gss_ctx = GSS_C_NO_CONTEXT;

    return APR_SUCCESS;
}

apr_status_t
serf__spnego_init_sec_context(serf_connection_t *conn,
                              serf__spnego_context_t *ctx,
                              const char *service,
                              const char *hostname,
                              serf__spnego_buffer_t *input_buf,
                              serf__spnego_buffer_t *output_buf,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool
                              )
{
    gss_buffer_desc gss_input_buf = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc *gss_output_buf_p;
    OM_uint32 gss_min_stat, gss_maj_stat;
    gss_name_t host_gss_name;
    gss_buffer_desc bufdesc;
    gss_OID dummy; /* unused */

    /* Get the name for the HTTP service at the target host. */
    /* TODO: should be shared between multiple requests. */
    bufdesc.value = apr_pstrcat(scratch_pool, service, "@", hostname, NULL);
    bufdesc.length = strlen(bufdesc.value);
    serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                  "Get principal for %s\n", bufdesc.value);
    gss_maj_stat = gss_import_name (&gss_min_stat, &bufdesc,
                                    GSS_C_NT_HOSTBASED_SERVICE,
                                    &host_gss_name);
    if(GSS_ERROR(gss_maj_stat)) {
        log_error(AUTH_VERBOSE, conn->skt, ctx,
                  gss_maj_stat, gss_min_stat,
                  "Error converting principal name to GSS internal format ");
        return SERF_ERROR_AUTHN_FAILED;
    }

    /* If the server sent us a token, pass it to gss_init_sec_token for
       validation. */
    gss_input_buf.value = input_buf->value;
    gss_input_buf.length = input_buf->length;

    gss_output_buf_p = apr_pcalloc(result_pool, sizeof(*gss_output_buf_p));

    /* Establish a security context to the server. */
    gss_maj_stat = gss_init_sec_context
        (&gss_min_stat,             /* minor_status */
         GSS_C_NO_CREDENTIAL,       /* XXXXX claimant_cred_handle */
         &ctx->gss_ctx,              /* gssapi context handle */
         host_gss_name,             /* HTTP@server name */
         ctx->gss_mech,             /* mech_type (SPNEGO) */
         GSS_C_MUTUAL_FLAG,         /* ensure the peer authenticates itself */
         0,                         /* default validity period */
         GSS_C_NO_CHANNEL_BINDINGS, /* do not use channel bindings */
         &gss_input_buf,            /* server token, initially empty */
         &dummy,                    /* actual mech type */
         gss_output_buf_p,           /* output_token */
         NULL,                      /* ret_flags */
         NULL                       /* not interested in remaining validity */
         );

    apr_pool_cleanup_register(result_pool, gss_output_buf_p,
                              cleanup_sec_buffer,
                              apr_pool_cleanup_null);

    output_buf->value = gss_output_buf_p->value;
    output_buf->length = gss_output_buf_p->length;

    switch(gss_maj_stat) {
    case GSS_S_COMPLETE:
        return APR_SUCCESS;
    case GSS_S_CONTINUE_NEEDED:
        return APR_EAGAIN;
    default:
        log_error(AUTH_VERBOSE, conn->skt, ctx,
                  gss_maj_stat, gss_min_stat,
                  "Error during Kerberos handshake");
        return SERF_ERROR_AUTHN_FAILED;
    }
}

#endif /* SERF_USE_GSSAPI */
