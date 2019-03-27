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
#include "auth.h"

#include <apr.h>
#include <apr_base64.h>
#include <apr_strings.h>
#include <apr_lib.h>

static apr_status_t
default_auth_response_handler(const serf__authn_scheme_t *scheme,
                              peer_t peer,
                              int code,
                              serf_connection_t *conn,
                              serf_request_t *request,
                              serf_bucket_t *response,
                              apr_pool_t *pool)
{
    return APR_SUCCESS;
}

/* These authentication schemes are in order of decreasing security, the topmost
   scheme will be used first when the server supports it.
 
   Each set of handlers should support both server (401) and proxy (407)
   authentication.
 
   Use lower case for the scheme names to enable case insensitive matching.
 */
static const serf__authn_scheme_t serf_authn_schemes[] = {
#ifdef SERF_HAVE_SPNEGO
    {
        "Negotiate",
        "negotiate",
        SERF_AUTHN_NEGOTIATE,
        serf__init_spnego,
        serf__init_spnego_connection,
        serf__handle_spnego_auth,
        serf__setup_request_spnego_auth,
        serf__validate_response_spnego_auth,
    },
#ifdef WIN32
    {
        "NTLM",
        "ntlm",
        SERF_AUTHN_NTLM,
        serf__init_spnego,
        serf__init_spnego_connection,
        serf__handle_spnego_auth,
        serf__setup_request_spnego_auth,
        serf__validate_response_spnego_auth,
    },
#endif /* #ifdef WIN32 */
#endif /* SERF_HAVE_SPNEGO */
    {
        "Digest",
        "digest",
        SERF_AUTHN_DIGEST,
        serf__init_digest,
        serf__init_digest_connection,
        serf__handle_digest_auth,
        serf__setup_request_digest_auth,
        serf__validate_response_digest_auth,
    },
    {
        "Basic",
        "basic",
        SERF_AUTHN_BASIC,
        serf__init_basic,
        serf__init_basic_connection,
        serf__handle_basic_auth,
        serf__setup_request_basic_auth,
        default_auth_response_handler,
    },
    /* ADD NEW AUTHENTICATION IMPLEMENTATIONS HERE (as they're written) */

    /* sentinel */
    { 0 }
};


/* Reads and discards all bytes in the response body. */
static apr_status_t discard_body(serf_bucket_t *response)
{
    apr_status_t status;
    const char *data;
    apr_size_t len;

    while (1) {
        status = serf_bucket_read(response, SERF_READ_ALL_AVAIL, &data, &len);

        if (status) {
            return status;
        }

        /* feed me */
    }
}

/**
 * handle_auth_header is called for each header in the response. It filters
 * out the Authenticate headers (WWW or Proxy depending on what's needed) and
 * tries to find a matching scheme handler.
 *
 * Returns a non-0 value of a matching handler was found.
 */
static int handle_auth_headers(int code,
                               void *baton,
                               apr_hash_t *hdrs,
                               serf_request_t *request,
                               serf_bucket_t *response,
                               apr_pool_t *pool)
{
    const serf__authn_scheme_t *scheme;
    serf_connection_t *conn = request->conn;
    serf_context_t *ctx = conn->ctx;
    apr_status_t status;

    status = SERF_ERROR_AUTHN_NOT_SUPPORTED;

    /* Find the matching authentication handler.
       Note that we don't reuse the auth scheme stored in the context,
       as that may have changed. (ex. fallback from ntlm to basic.) */
    for (scheme = serf_authn_schemes; scheme->name != 0; ++scheme) {
        const char *auth_hdr;
        serf__auth_handler_func_t handler;
        serf__authn_info_t *authn_info;

        if (! (ctx->authn_types & scheme->type))
            continue;

        serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                      "Client supports: %s\n", scheme->name);

        auth_hdr = apr_hash_get(hdrs, scheme->key, APR_HASH_KEY_STRING);

        if (!auth_hdr)
            continue;

        if (code == 401) {
            authn_info = serf__get_authn_info_for_server(conn);
        } else {
            authn_info = &ctx->proxy_authn_info;
        }

        if (authn_info->failed_authn_types & scheme->type) {
            /* Skip this authn type since we already tried it before. */
            continue;
        }

        /* Found a matching scheme */
        status = APR_SUCCESS;

        handler = scheme->handle_func;

        serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                      "... matched: %s\n", scheme->name);

        /* If this is the first time we use this scheme on this context and/or
           this connection, make sure to initialize the authentication handler 
           first. */
        if (authn_info->scheme != scheme) {
            status = scheme->init_ctx_func(code, ctx, ctx->pool);
            if (!status) {
                status = scheme->init_conn_func(scheme, code, conn,
                                                conn->pool);
                if (!status)
                    authn_info->scheme = scheme;
                else
                    authn_info->scheme = NULL;
            }
        }

        if (!status) {
            const char *auth_attr = strchr(auth_hdr, ' ');
            if (auth_attr) {
                auth_attr++;
            }

            status = handler(code, request, response,
                             auth_hdr, auth_attr, baton, ctx->pool);
        }

        if (status == APR_SUCCESS)
            break;

        /* No success authenticating with this scheme, try the next.
           If no more authn schemes are found the status of this scheme will be
           returned.
        */
        serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                      "%s authentication failed.\n", scheme->name);

        /* Clear per-request auth_baton when switching to next auth scheme. */
        request->auth_baton = NULL;

        /* Remember failed auth types to skip in future. */
        authn_info->failed_authn_types |= scheme->type;
    }

    return status;
}

/**
 * Baton passed to the store_header_in_dict callback function
 */
typedef struct {
    const char *header;
    apr_pool_t *pool;
    apr_hash_t *hdrs;
} auth_baton_t;

static int store_header_in_dict(void *baton,
                                const char *key,
                                const char *header)
{
    auth_baton_t *ab = baton;
    const char *auth_attr;
    char *auth_name, *c;

    /* We're only interested in xxxx-Authenticate headers. */
    if (strcasecmp(key, ab->header) != 0)
        return 0;

    /* Extract the authentication scheme name.  */
    auth_attr = strchr(header, ' ');
    if (auth_attr) {
        auth_name = apr_pstrmemdup(ab->pool, header, auth_attr - header);
    }
    else
        auth_name = apr_pstrmemdup(ab->pool, header, strlen(header));

    /* Convert scheme name to lower case to enable case insensitive matching. */
    for (c = auth_name; *c != '\0'; c++)
        *c = (char)apr_tolower(*c);

    apr_hash_set(ab->hdrs, auth_name, APR_HASH_KEY_STRING,
                 apr_pstrdup(ab->pool, header));

    return 0;
}

/* Dispatch authentication handling. This function matches the possible
   authentication mechanisms with those available. Server and proxy
   authentication are evaluated separately. */
static apr_status_t dispatch_auth(int code,
                                  serf_request_t *request,
                                  serf_bucket_t *response,
                                  void *baton,
                                  apr_pool_t *pool)
{
    serf_bucket_t *hdrs;

    if (code == 401 || code == 407) {
        auth_baton_t ab = { 0 };
        const char *auth_hdr;

        ab.hdrs = apr_hash_make(pool);
        ab.pool = pool;

        /* Before iterating over all authn headers, check if there are any. */
        if (code == 401)
            ab.header = "WWW-Authenticate";
        else
            ab.header = "Proxy-Authenticate";

        hdrs = serf_bucket_response_get_headers(response);
        auth_hdr = serf_bucket_headers_get(hdrs, ab.header);

        if (!auth_hdr) {
            return SERF_ERROR_AUTHN_FAILED;
        }
        serf__log_skt(AUTH_VERBOSE, __FILE__, request->conn->skt,
                      "%s authz required. Response header(s): %s\n",
                      code == 401 ? "Server" : "Proxy", auth_hdr);


        /* Store all WWW- or Proxy-Authenticate headers in a dictionary.

           Note: it is possible to have multiple Authentication: headers. We do
           not want to combine them (per normal header combination rules) as that
           would make it hard to parse. Instead, we want to individually parse
           and handle each header in the response, looking for one that we can
           work with.
        */
        serf_bucket_headers_do(hdrs,
                               store_header_in_dict,
                               &ab);

        /* Iterate over all authentication schemes, in order of decreasing
           security. Try to find a authentication schema the server support. */
        return handle_auth_headers(code, baton, ab.hdrs,
                                   request, response, pool);
    }

    return APR_SUCCESS;
}

/* Read the headers of the response and try the available
   handlers if authentication or validation is needed. */
apr_status_t serf__handle_auth_response(int *consumed_response,
                                        serf_request_t *request,
                                        serf_bucket_t *response,
                                        void *baton,
                                        apr_pool_t *pool)
{
    apr_status_t status;
    serf_status_line sl;

    *consumed_response = 0;

    /* TODO: the response bucket was created by the application, not at all
       guaranteed that this is of type response_bucket!! */
    status = serf_bucket_response_status(response, &sl);
    if (SERF_BUCKET_READ_ERROR(status)) {
        return status;
    }
    if (!sl.version && (APR_STATUS_IS_EOF(status) ||
                        APR_STATUS_IS_EAGAIN(status))) {
        return status;
    }

    status = serf_bucket_response_wait_for_headers(response);
    if (status) {
        if (!APR_STATUS_IS_EOF(status)) {
            return status;
        }

        /* If status is APR_EOF, there were no headers to read.
           This can be ok in some situations, and it definitely
           means there's no authentication requested now. */
        return APR_SUCCESS;
    }

    if (sl.code == 401 || sl.code == 407) {
        /* Authentication requested. */

        /* Don't bother handling the authentication request if the response
           wasn't received completely yet. Serf will call serf__handle_auth_response
           again when more data is received. */
        status = discard_body(response);
        *consumed_response = 1;
        
        /* Discard all response body before processing authentication. */
        if (!APR_STATUS_IS_EOF(status)) {
            return status;
        }

        status = dispatch_auth(sl.code, request, response, baton, pool);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* Requeue the request with the necessary auth headers. */
        /* ### Application doesn't know about this request! */
        if (request->ssltunnel) {
            serf__ssltunnel_request_create(request->conn,
                                           request->setup,
                                           request->setup_baton);
        } else {
            serf_connection_priority_request_create(request->conn,
                                                    request->setup,
                                                    request->setup_baton);
        }

        return APR_EOF;
    } else {
        serf__validate_response_func_t validate_resp;
        serf_connection_t *conn = request->conn;
        serf_context_t *ctx = conn->ctx;
        serf__authn_info_t *authn_info;
        apr_status_t resp_status = APR_SUCCESS;


        /* Validate the response server authn headers. */
        authn_info = serf__get_authn_info_for_server(conn);
        if (authn_info->scheme) {
            validate_resp = authn_info->scheme->validate_response_func;
            resp_status = validate_resp(authn_info->scheme, HOST, sl.code,
                                        conn, request, response, pool);
        }

        /* Validate the response proxy authn headers. */
        authn_info = &ctx->proxy_authn_info;
        if (!resp_status && authn_info->scheme) {
            validate_resp = authn_info->scheme->validate_response_func;
            resp_status = validate_resp(authn_info->scheme, PROXY, sl.code,
                                        conn, request, response, pool);
        }

        if (resp_status) {
            /* If there was an error in the final step of the authentication,
               consider the reponse body as invalid and discard it. */
            status = discard_body(response);
            *consumed_response = 1;
            if (!APR_STATUS_IS_EOF(status)) {
                return status;
            }
            /* The whole body was discarded, now return our error. */
            return resp_status;
        }
    }

    return APR_SUCCESS;
}

/**
 * base64 encode the authentication data and build an authentication
 * header in this format:
 * [SCHEME] [BASE64 of auth DATA]
 */
void serf__encode_auth_header(const char **header,
                              const char *scheme,
                              const char *data, apr_size_t data_len,
                              apr_pool_t *pool)
{
    apr_size_t encoded_len, scheme_len;
    char *ptr;

    encoded_len = apr_base64_encode_len(data_len);
    scheme_len = strlen(scheme);

    ptr = apr_palloc(pool, encoded_len + scheme_len + 1);
    *header = ptr;

    apr_cpystrn(ptr, scheme, scheme_len + 1);
    ptr += scheme_len;
    *ptr++ = ' ';

    apr_base64_encode(ptr, data, data_len);
}

const char *serf__construct_realm(peer_t peer,
                                  serf_connection_t *conn,
                                  const char *realm_name,
                                  apr_pool_t *pool)
{
    if (peer == HOST) {
        return apr_psprintf(pool, "<%s://%s:%d> %s",
                            conn->host_info.scheme,
                            conn->host_info.hostname,
                            conn->host_info.port,
                            realm_name);
    } else {
        serf_context_t *ctx = conn->ctx;

        return apr_psprintf(pool, "<http://%s:%d> %s",
                            ctx->proxy_address->hostname,
                            ctx->proxy_address->port,
                            realm_name);
    }
}

serf__authn_info_t *serf__get_authn_info_for_server(serf_connection_t *conn)
{
    serf_context_t *ctx = conn->ctx;
    serf__authn_info_t *authn_info;

    authn_info = apr_hash_get(ctx->server_authn_info, conn->host_url,
                              APR_HASH_KEY_STRING);

    if (!authn_info) {
        authn_info = apr_pcalloc(ctx->pool, sizeof(serf__authn_info_t));
        apr_hash_set(ctx->server_authn_info,
                     apr_pstrdup(ctx->pool, conn->host_url),
                     APR_HASH_KEY_STRING, authn_info);
    }

    return authn_info;
}
