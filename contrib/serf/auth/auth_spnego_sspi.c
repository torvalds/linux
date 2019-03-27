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

#include "auth_spnego.h"
#include "serf.h"
#include "serf_private.h"

#ifdef SERF_USE_SSPI
#include <apr.h>
#include <apr_strings.h>

#define SECURITY_WIN32
#include <sspi.h>

/* SEC_E_MUTUAL_AUTH_FAILED is not defined in Windows Platform SDK 5.0. */
#ifndef SEC_E_MUTUAL_AUTH_FAILED
#define SEC_E_MUTUAL_AUTH_FAILED _HRESULT_TYPEDEF_(0x80090363L)
#endif

struct serf__spnego_context_t
{
    CredHandle sspi_credentials;
    CtxtHandle sspi_context;
    BOOL initalized;
    apr_pool_t *pool;

    /* Service Principal Name (SPN) used for authentication. */
    const char *target_name;

    /* One of SERF_AUTHN_* authentication types.*/
    int authn_type;
};

/* Map SECURITY_STATUS from SSPI to APR error code. Some error codes mapped
 * to our own codes and some to Win32 error codes:
 * http://support.microsoft.com/kb/113996
 */
static apr_status_t
map_sspi_status(SECURITY_STATUS sspi_status)
{
    switch(sspi_status)
    {
    case SEC_E_INSUFFICIENT_MEMORY:
        return APR_FROM_OS_ERROR(ERROR_NO_SYSTEM_RESOURCES);
    case SEC_E_INVALID_HANDLE:
        return APR_FROM_OS_ERROR(ERROR_INVALID_HANDLE);
    case SEC_E_UNSUPPORTED_FUNCTION:
        return APR_FROM_OS_ERROR(ERROR_INVALID_FUNCTION);
    case SEC_E_TARGET_UNKNOWN:
        return APR_FROM_OS_ERROR(ERROR_BAD_NETPATH);
    case SEC_E_INTERNAL_ERROR:
        return APR_FROM_OS_ERROR(ERROR_INTERNAL_ERROR);
    case SEC_E_SECPKG_NOT_FOUND:
    case SEC_E_BAD_PKGID:
        return APR_FROM_OS_ERROR(ERROR_NO_SUCH_PACKAGE);
    case SEC_E_NO_IMPERSONATION:
        return APR_FROM_OS_ERROR(ERROR_CANNOT_IMPERSONATE);
    case SEC_E_NO_AUTHENTICATING_AUTHORITY:
        return APR_FROM_OS_ERROR(ERROR_NO_LOGON_SERVERS);
    case SEC_E_UNTRUSTED_ROOT:
        return APR_FROM_OS_ERROR(ERROR_TRUST_FAILURE);
    case SEC_E_WRONG_PRINCIPAL:
        return APR_FROM_OS_ERROR(ERROR_WRONG_TARGET_NAME);
    case SEC_E_MUTUAL_AUTH_FAILED:
        return APR_FROM_OS_ERROR(ERROR_MUTUAL_AUTH_FAILED);
    case SEC_E_TIME_SKEW:
        return APR_FROM_OS_ERROR(ERROR_TIME_SKEW);
    default:
        return SERF_ERROR_AUTHN_FAILED;
    }
}

/* Cleans the SSPI context object, when the pool used to create it gets
   cleared or destroyed. */
static apr_status_t
cleanup_ctx(void *data)
{
    serf__spnego_context_t *ctx = data;

    if (SecIsValidHandle(&ctx->sspi_context)) {
        DeleteSecurityContext(&ctx->sspi_context);
        SecInvalidateHandle(&ctx->sspi_context);
    }

    if (SecIsValidHandle(&ctx->sspi_credentials)) {
        FreeCredentialsHandle(&ctx->sspi_credentials);
        SecInvalidateHandle(&ctx->sspi_credentials);
    }

    return APR_SUCCESS;
}

static apr_status_t
cleanup_sec_buffer(void *data)
{
    FreeContextBuffer(data);

    return APR_SUCCESS;
}

apr_status_t
serf__spnego_create_sec_context(serf__spnego_context_t **ctx_p,
                                const serf__authn_scheme_t *scheme,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
    SECURITY_STATUS sspi_status;
    serf__spnego_context_t *ctx;
    const char *sspi_package;

    ctx = apr_pcalloc(result_pool, sizeof(*ctx));

    SecInvalidateHandle(&ctx->sspi_context);
    SecInvalidateHandle(&ctx->sspi_credentials);
    ctx->initalized = FALSE;
    ctx->pool = result_pool;
    ctx->target_name = NULL;
    ctx->authn_type = scheme->type;

    apr_pool_cleanup_register(result_pool, ctx,
                              cleanup_ctx,
                              apr_pool_cleanup_null);

    if (ctx->authn_type == SERF_AUTHN_NEGOTIATE)
        sspi_package = "Negotiate";
    else
        sspi_package = "NTLM";

    sspi_status = AcquireCredentialsHandleA(
        NULL, sspi_package, SECPKG_CRED_OUTBOUND,
        NULL, NULL, NULL, NULL,
        &ctx->sspi_credentials, NULL);

    if (FAILED(sspi_status)) {
        return map_sspi_status(sspi_status);
    }

    *ctx_p = ctx;

    return APR_SUCCESS;
}

static apr_status_t
get_canonical_hostname(const char **canonname,
                       const char *hostname,
                       apr_pool_t *pool)
{
    struct addrinfo hints;
    struct addrinfo *addrinfo;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(hostname, NULL, &hints, &addrinfo)) {
        return apr_get_netos_error();
    }

    if (addrinfo) {
        *canonname = apr_pstrdup(pool, addrinfo->ai_canonname);
    }
    else {
        *canonname = apr_pstrdup(pool, hostname);
    }

    freeaddrinfo(addrinfo);
    return APR_SUCCESS;
}

apr_status_t
serf__spnego_reset_sec_context(serf__spnego_context_t *ctx)
{
    if (SecIsValidHandle(&ctx->sspi_context)) {
        DeleteSecurityContext(&ctx->sspi_context);
        SecInvalidateHandle(&ctx->sspi_context);
    }

    ctx->initalized = FALSE;

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
    SECURITY_STATUS status;
    ULONG actual_attr;
    SecBuffer sspi_in_buffer;
    SecBufferDesc sspi_in_buffer_desc;
    SecBuffer sspi_out_buffer;
    SecBufferDesc sspi_out_buffer_desc;
    apr_status_t apr_status;
    const char *canonname;

    if (!ctx->initalized && ctx->authn_type == SERF_AUTHN_NEGOTIATE) {
        apr_status = get_canonical_hostname(&canonname, hostname, scratch_pool);
        if (apr_status) {
            return apr_status;
        }

        ctx->target_name = apr_pstrcat(scratch_pool, service, "/", canonname,
                                       NULL);

        serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                      "Using SPN '%s' for '%s'\n", ctx->target_name, hostname);
    }
    else if (ctx->authn_type == SERF_AUTHN_NTLM)
    {
        /* Target name is not used for NTLM authentication. */
        ctx->target_name = NULL;
    }

    /* Prepare input buffer description. */
    sspi_in_buffer.BufferType = SECBUFFER_TOKEN;
    sspi_in_buffer.pvBuffer = input_buf->value;
    sspi_in_buffer.cbBuffer = input_buf->length; 

    sspi_in_buffer_desc.cBuffers = 1;
    sspi_in_buffer_desc.pBuffers = &sspi_in_buffer;
    sspi_in_buffer_desc.ulVersion = SECBUFFER_VERSION;

    /* Output buffers. Output buffer will be allocated by system. */
    sspi_out_buffer.BufferType = SECBUFFER_TOKEN;
    sspi_out_buffer.pvBuffer = NULL; 
    sspi_out_buffer.cbBuffer = 0;

    sspi_out_buffer_desc.cBuffers = 1;
    sspi_out_buffer_desc.pBuffers = &sspi_out_buffer;
    sspi_out_buffer_desc.ulVersion = SECBUFFER_VERSION;

    status = InitializeSecurityContextA(
        &ctx->sspi_credentials,
        ctx->initalized ? &ctx->sspi_context : NULL,
        ctx->target_name,
        ISC_REQ_ALLOCATE_MEMORY
        | ISC_REQ_MUTUAL_AUTH
        | ISC_REQ_CONFIDENTIALITY,
        0,                          /* Reserved1 */
        SECURITY_NETWORK_DREP,
        &sspi_in_buffer_desc,
        0,                          /* Reserved2 */
        &ctx->sspi_context,
        &sspi_out_buffer_desc,
        &actual_attr,
        NULL);

    if (sspi_out_buffer.cbBuffer > 0) {
        apr_pool_cleanup_register(result_pool, sspi_out_buffer.pvBuffer,
                                  cleanup_sec_buffer,
                                  apr_pool_cleanup_null);
    }

    ctx->initalized = TRUE;

    /* Finish authentication if SSPI requires so. */
    if (status == SEC_I_COMPLETE_NEEDED
        || status == SEC_I_COMPLETE_AND_CONTINUE)
    {
        CompleteAuthToken(&ctx->sspi_context, &sspi_out_buffer_desc);
    }

    output_buf->value = sspi_out_buffer.pvBuffer;
    output_buf->length = sspi_out_buffer.cbBuffer;

    switch(status) {
    case SEC_I_COMPLETE_AND_CONTINUE:
    case SEC_I_CONTINUE_NEEDED:
        return APR_EAGAIN;

    case SEC_I_COMPLETE_NEEDED:
    case SEC_E_OK:
        return APR_SUCCESS;

    default:
        return map_sspi_status(status);
    }
}

#endif /* SERF_USE_SSPI */
