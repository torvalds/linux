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

#ifdef SERF_HAVE_SPNEGO

/** These functions implement SPNEGO-based Kerberos and NTLM authentication,
 *  using either GSS-API (RFC 2743) or SSPI on Windows.
 *  The HTTP message exchange is documented in RFC 4559.
 **/

#include <serf.h>
#include <serf_private.h>
#include <auth/auth.h>

#include <apr.h>
#include <apr_base64.h>
#include <apr_strings.h>

/** TODO:
 ** - send session key directly on new connections where we already know
 **   the server requires Kerberos authn.
 ** - Add a way for serf to give detailed error information back to the
 **   application.
 **/

/* Authentication over HTTP using Kerberos
 *
 * Kerberos involves three servers:
 * - Authentication Server (AS): verifies users during login
 * - Ticket-Granting Server (TGS): issues proof of identity tickets
 * - HTTP server (S)
 *
 * Steps:
 * 0. User logs in to the AS and receives a TGS ticket. On workstations
 * where the login program doesn't support Kerberos, the user can use
 * 'kinit'.
 *
 * 1. C  --> S:    GET
 *
 *    C <--  S:    401 Authentication Required
 *                 WWW-Authenticate: Negotiate
 *
 * -> app contacts the TGS to request a session key for the HTTP service
 *    @ target host. The returned session key is encrypted with the HTTP
 *    service's secret key, so we can safely send it to the server.
 *
 * 2. C  --> S:    GET
 *                 Authorization: Negotiate <Base64 encoded session key>
 *                 gss_api_ctx->state = gss_api_auth_in_progress;
 *
 *    C <--  S:    200 OK
 *                 WWW-Authenticate: Negotiate <Base64 encoded server
 *                                              authentication data>
 *
 * -> The server returned an (optional) key to proof itself to us. We check this
 *    key with the TGS again. If it checks out, we can return the response
 *    body to the application.
 *
 * Note: It's possible that the server returns 401 again in step 2, if the
 *       Kerberos context isn't complete yet. This means there is 3rd step
 *       where we'll send a request with an Authorization header to the 
 *       server. Some (simple) tests with mod_auth_kerb and MIT Kerberos 5 show
 *       this never happens.
 *
 * Depending on the type of HTTP server, this handshake is required for either
 * every new connection, or for every new request! For more info see the next
 * comment on authn_persistence_state_t.
 *
 * Note: Step 1 of the handshake will only happen on the first connection, once
 * we know the server requires Kerberos authentication, the initial requests
 * on the other connections will include a session key, so we start at
 * step 2 in the handshake.
 * ### TODO: Not implemented yet!
 */

/* Current state of the authentication of the current request. */
typedef enum {
    gss_api_auth_not_started,
    gss_api_auth_in_progress,
    gss_api_auth_completed,
} gss_api_auth_state;

/**
   authn_persistence_state_t: state that indicates if we are talking with a
   server that requires authentication only of the first request (stateful),
   or of each request (stateless).
 
   INIT: Begin state. Authenticating the first request on this connection.
   UNDECIDED: we haven't identified the server yet, assume STATEFUL for now.
     Pipeline mode disabled, requests are sent only after the response off the
     previous request arrived.
   STATELESS: we know the server requires authentication for each request.
     On all new requests add the Authorization header with an initial SPNEGO
     token (created per request).
     To keep things simple, keep the connection in one by one mode.
     (otherwise we'd have to keep a queue of gssapi context objects to match
      the Negotiate header of the response with the session initiated by the
      mathing request).
     This state is an final state.
   STATEFUL: alright, we have authenticated the connection and for the server
     that is enough. Don't add an Authorization header to new requests.
     Serf will switch to pipelined mode.
     This state is not a final state, although in practical scenario's it will
     be. When we receive a 40x response from the server switch to STATELESS
     mode.

   We start in state init for the first request until it is authenticated.

   The rest of the state machine starts with the arrival of the response to the
   second request, and then goes on with each response:

      --------
      | INIT |     C --> S:    GET request in response to 40x of the server
      --------                 add [Proxy]-Authorization header
          |
          |
    ------------
    | UNDECIDED|   C --> S:    GET request, assume stateful,
    ------------               no [Proxy]-Authorization header
          |
          |
          |------------------------------------------------
          |                                               |
          | C <-- S: 40x Authentication                   | C <-- S: 200 OK
          |          Required                             |
          |                                               |
          v                                               v
      -------------                               ------------
    ->| STATELESS |<------------------------------| STATEFUL |<--
    | -------------       C <-- S: 40x            ------------  |
  * |    |                Authentication                  |     | 200 OK
    |    /                Required                        |     |
    -----                                                 -----/

 **/
typedef enum {
    pstate_init,
    pstate_undecided,
    pstate_stateless,
    pstate_stateful,
} authn_persistence_state_t;


/* HTTP Service name, used to get the session key.  */
#define KRB_HTTP_SERVICE "HTTP"

/* Stores the context information related to Kerberos authentication. */
typedef struct
{
    apr_pool_t *pool;

    /* GSSAPI context */
    serf__spnego_context_t *gss_ctx;

    /* Current state of the authentication cycle. */
    gss_api_auth_state state;

    /* Current persistence state. */
    authn_persistence_state_t pstate;

    const char *header;
    const char *value;
} gss_authn_info_t;

/* On the initial 401 response of the server, request a session key from
   the Kerberos KDC to pass to the server, proving that we are who we
   claim to be. The session key can only be used with the HTTP service
   on the target host. */
static apr_status_t
gss_api_get_credentials(serf_connection_t *conn,
                        char *token, apr_size_t token_len,
                        const char *hostname,
                        const char **buf, apr_size_t *buf_len,
                        gss_authn_info_t *gss_info)
{
    serf__spnego_buffer_t input_buf;
    serf__spnego_buffer_t output_buf;
    apr_status_t status = APR_SUCCESS;

    /* If the server sent us a token, pass it to gss_init_sec_token for
       validation. */
    if (token) {
        input_buf.value = token;
        input_buf.length = token_len;
    } else {
        input_buf.value = 0;
        input_buf.length = 0;
    }

    /* Establish a security context to the server. */
    status = serf__spnego_init_sec_context(
         conn,
         gss_info->gss_ctx,
         KRB_HTTP_SERVICE, hostname,
         &input_buf,
         &output_buf,
         gss_info->pool,
         gss_info->pool
        );

    switch(status) {
    case APR_SUCCESS:
        if (output_buf.length == 0) {
            gss_info->state = gss_api_auth_completed;
        } else {
            gss_info->state = gss_api_auth_in_progress;
        }
        break;
    case APR_EAGAIN:
        gss_info->state = gss_api_auth_in_progress;
        status = APR_SUCCESS;
        break;
    default:
        return status;
    }

    /* Return the session key to our caller. */
    *buf = output_buf.value;
    *buf_len = output_buf.length;

    return status;
}

/* do_auth is invoked in two situations:
   - when a response from a server is received that contains an authn header
     (either from a 40x or 2xx response)
   - when a request is prepared on a connection with stateless authentication.

   Read the header sent by the server (if any), invoke the gssapi authn
   code and use the resulting Server Ticket on the next request to the
   server. */
static apr_status_t
do_auth(peer_t peer,
        int code,
        gss_authn_info_t *gss_info,
        serf_connection_t *conn,
        serf_request_t *request,
        const char *auth_hdr,
        apr_pool_t *pool)
{
    serf_context_t *ctx = conn->ctx;
    serf__authn_info_t *authn_info;
    const char *tmp = NULL;
    char *token = NULL;
    apr_size_t tmp_len = 0, token_len = 0;
    apr_status_t status;

    if (peer == HOST) {
        authn_info = serf__get_authn_info_for_server(conn);
    } else {
        authn_info = &ctx->proxy_authn_info;
    }

    /* Is this a response from a host/proxy? auth_hdr should always be set. */
    if (code && auth_hdr) {
        const char *space = NULL;
        /* The server will return a token as attribute to the Negotiate key.
           Negotiate YGwGCSqGSIb3EgECAgIAb10wW6ADAgEFoQMCAQ+iTzBNoAMCARCiRgREa6
           mouMBAMFqKVdTGtfpZNXKzyw4Yo1paphJdIA3VOgncaoIlXxZLnkHiIHS2v65pVvrp
           bRIyjF8xve9HxpnNIucCY9c=

           Read this base64 value, decode it and validate it so we're sure the
           server is who we expect it to be. */
        space = strchr(auth_hdr, ' ');

        if (space) {
            token = apr_palloc(pool, apr_base64_decode_len(space + 1));
            token_len = apr_base64_decode(token, space + 1);
        }
    } else {
        /* This is a new request, not a retry in response to a 40x of the
           host/proxy. 
           Only add the Authorization header if we know the server requires
           per-request authentication (stateless). */
        if (gss_info->pstate != pstate_stateless)
            return APR_SUCCESS;
    }

    switch(gss_info->pstate) {
        case pstate_init:
            /* Nothing to do here */
            break;
        case pstate_undecided: /* Fall through */
        case pstate_stateful:
            {
                /* Switch to stateless mode, from now on handle authentication
                   of each request with a new gss context. This is easiest to
                   manage when sending requests one by one. */
                serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                              "Server requires per-request SPNEGO authn, "
                              "switching to stateless mode.\n");

                gss_info->pstate = pstate_stateless;
                serf_connection_set_max_outstanding_requests(conn, 1);
                break;
            }
        case pstate_stateless:
            /* Nothing to do here */
            break;
    }

    if (request->auth_baton && !token) {
        /* We provided token with this request, but server responded with empty
           authentication header. This means server rejected our credentials.
           XXX: Probably we need separate error code for this case like
           SERF_ERROR_AUTHN_CREDS_REJECTED? */
        return SERF_ERROR_AUTHN_FAILED;
    }

    /* If the server didn't provide us with a token, start with a new initial
       step in the SPNEGO authentication. */
    if (!token) {
        serf__spnego_reset_sec_context(gss_info->gss_ctx);
        gss_info->state = gss_api_auth_not_started;
    }

    if (peer == HOST) {
        status = gss_api_get_credentials(conn,
                                         token, token_len,
                                         conn->host_info.hostname,
                                         &tmp, &tmp_len,
                                         gss_info);
    } else {
        char *proxy_host = conn->ctx->proxy_address->hostname;
        status = gss_api_get_credentials(conn,
                                         token, token_len, proxy_host,
                                         &tmp, &tmp_len,
                                         gss_info);
    }
    if (status)
        return status;

    /* On the next request, add an Authorization header. */
    if (tmp_len) {
        serf__encode_auth_header(&gss_info->value, authn_info->scheme->name,
                                 tmp,
                                 tmp_len,
                                 pool);
        gss_info->header = (peer == HOST) ?
            "Authorization" : "Proxy-Authorization";
    }

    return APR_SUCCESS;
}

apr_status_t
serf__init_spnego(int code,
                  serf_context_t *ctx,
                  apr_pool_t *pool)
{
    return APR_SUCCESS;
}

/* A new connection is created to a server that's known to use
   Kerberos. */
apr_status_t
serf__init_spnego_connection(const serf__authn_scheme_t *scheme,
                             int code,
                             serf_connection_t *conn,
                             apr_pool_t *pool)
{
    serf_context_t *ctx = conn->ctx;
    serf__authn_info_t *authn_info;
    gss_authn_info_t *gss_info = NULL;

    /* For proxy authentication, reuse the gss context for all connections. 
       For server authentication, create a new gss context per connection. */
    if (code == 401) {
        authn_info = &conn->authn_info;
    } else {
        authn_info = &ctx->proxy_authn_info;
    }
    gss_info = authn_info->baton;

    if (!gss_info) {
        apr_status_t status;

        gss_info = apr_pcalloc(conn->pool, sizeof(*gss_info));
        gss_info->pool = conn->pool;
        gss_info->state = gss_api_auth_not_started;
        gss_info->pstate = pstate_init;
        status = serf__spnego_create_sec_context(&gss_info->gss_ctx, scheme,
                                                 gss_info->pool, pool);
        if (status) {
            return status;
        }
        authn_info->baton = gss_info;
    }

    /* Make serf send the initial requests one by one */
    serf_connection_set_max_outstanding_requests(conn, 1);

    serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                  "Initialized Kerberos context for this connection.\n");

    return APR_SUCCESS;
}

/* A 40x response was received, handle the authentication. */
apr_status_t
serf__handle_spnego_auth(int code,
                         serf_request_t *request,
                         serf_bucket_t *response,
                         const char *auth_hdr,
                         const char *auth_attr,
                         void *baton,
                         apr_pool_t *pool)
{
    serf_connection_t *conn = request->conn;
    serf_context_t *ctx = conn->ctx;
    gss_authn_info_t *gss_info = (code == 401) ? conn->authn_info.baton :
                                                 ctx->proxy_authn_info.baton;

    return do_auth(code == 401 ? HOST : PROXY,
                   code,
                   gss_info,
                   request->conn,
                   request,
                   auth_hdr,
                   pool);
}

/* Setup the authn headers on this request message. */
apr_status_t
serf__setup_request_spnego_auth(peer_t peer,
                                int code,
                                serf_connection_t *conn,
                                serf_request_t *request,
                                const char *method,
                                const char *uri,
                                serf_bucket_t *hdrs_bkt)
{
    serf_context_t *ctx = conn->ctx;
    gss_authn_info_t *gss_info = (peer == HOST) ? conn->authn_info.baton :
                                                  ctx->proxy_authn_info.baton;

    /* If we have an ongoing authentication handshake, the handler of the
       previous response will have created the authn headers for this request
       already. */
    if (gss_info && gss_info->header && gss_info->value) {
        serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                      "Set Negotiate authn header on retried request.\n");

        serf_bucket_headers_setn(hdrs_bkt, gss_info->header,
                                 gss_info->value);

        /* Remember that we're using this request for authentication
           handshake. */
        request->auth_baton = (void*) TRUE;

        /* We should send each token only once. */
        gss_info->header = NULL;
        gss_info->value = NULL;

        return APR_SUCCESS;
    }

    switch (gss_info->pstate) {
        case pstate_init:
            /* We shouldn't normally arrive here, do nothing. */
            break;
        case pstate_undecided: /* fall through */
            serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                          "Assume for now that the server supports persistent "
                          "SPNEGO authentication.\n");
            /* Nothing to do here. */
            break;
        case pstate_stateful:
            serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                          "SPNEGO on this connection is persistent, "
                          "don't set authn header on next request.\n");
            /* Nothing to do here. */
            break;
        case pstate_stateless:
            {
                apr_status_t status;

                /* Authentication on this connection is known to be stateless.
                   Add an initial Negotiate token for the server, to bypass the
                   40x response we know we'll otherwise receive.
                  (RFC 4559 section 4.2) */
                serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                              "Add initial Negotiate header to request.\n");

                status = do_auth(peer,
                                 code,
                                 gss_info,
                                 conn,
                                 request,
                                 0l,    /* no response authn header */
                                 conn->pool);
                if (status)
                    return status;

                serf_bucket_headers_setn(hdrs_bkt, gss_info->header,
                                         gss_info->value);

                /* Remember that we're using this request for authentication
                   handshake. */
                request->auth_baton = (void*) TRUE;

                /* We should send each token only once. */
                gss_info->header = NULL;
                gss_info->value = NULL;
                break;
            }
    }

    return APR_SUCCESS;
}

/**
 * Baton passed to the get_auth_header callback function.
 */
typedef struct {
    const char *hdr_name;
    const char *auth_name;
    const char *hdr_value;
    apr_pool_t *pool;
} get_auth_header_baton_t;

static int
get_auth_header_cb(void *baton,
                   const char *key,
                   const char *header)
{
    get_auth_header_baton_t *b = baton;

    /* We're only interested in xxxx-Authenticate headers. */
    if (strcasecmp(key, b->hdr_name) != 0)
        return 0;

    /* Check if header value starts with interesting auth name. */
    if (strncmp(header, b->auth_name, strlen(b->auth_name)) == 0) {
        /* Save interesting header value and stop iteration. */
        b->hdr_value = apr_pstrdup(b->pool,  header);
        return 1;
    }

    return 0;
}

static const char *
get_auth_header(serf_bucket_t *hdrs,
                const char *hdr_name,
                const char *auth_name,
                apr_pool_t *pool)
{
    get_auth_header_baton_t b;

    b.auth_name = hdr_name;
    b.hdr_name = auth_name;
    b.hdr_value = NULL;
    b.pool = pool;

    serf_bucket_headers_do(hdrs, get_auth_header_cb, &b);

    return b.hdr_value;
}

/* Function is called when 2xx responses are received. Normally we don't
 * have to do anything, except for the first response after the
 * authentication handshake. This specific response includes authentication
 * data which should be validated by the client (mutual authentication).
 */
apr_status_t
serf__validate_response_spnego_auth(const serf__authn_scheme_t *scheme,
                                    peer_t peer,
                                    int code,
                                    serf_connection_t *conn,
                                    serf_request_t *request,
                                    serf_bucket_t *response,
                                    apr_pool_t *pool)
{
    serf_context_t *ctx = conn->ctx;
    gss_authn_info_t *gss_info;
    const char *auth_hdr_name;

    /* TODO: currently this function is only called when a response includes
       an Authenticate header. This header is optional. If the server does
       not provide this header on the first 2xx response, we will not promote
       the connection from undecided to stateful. This won't break anything,
       but means we stay in non-pipelining mode. */
    serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                  "Validate Negotiate response header.\n");

    if (peer == HOST) {
        gss_info = conn->authn_info.baton;
        auth_hdr_name = "WWW-Authenticate";
    } else {
        gss_info = ctx->proxy_authn_info.baton;
        auth_hdr_name = "Proxy-Authenticate";
    }

    if (gss_info->state != gss_api_auth_completed) {
        serf_bucket_t *hdrs;
        const char *auth_hdr_val;
        apr_status_t status;

        hdrs = serf_bucket_response_get_headers(response);
        auth_hdr_val = get_auth_header(hdrs, auth_hdr_name, scheme->name,
                                       pool);

        if (auth_hdr_val) {
            status = do_auth(peer, code, gss_info, conn, request, auth_hdr_val,
                             pool);
            if (status) {
                return status;
            }
        } else {
            /* No Authenticate headers, nothing to validate: authentication
               completed.*/
            gss_info->state = gss_api_auth_completed;

            serf__log_skt(AUTH_VERBOSE, __FILE__, conn->skt,
                          "SPNEGO handshake completed.\n");
        }
    }

    if (gss_info->state == gss_api_auth_completed) {
        switch(gss_info->pstate) {
            case pstate_init:
                /* Authentication of the first request is done. */
                gss_info->pstate = pstate_undecided;
                break;
            case pstate_undecided:
                /* The server didn't request for authentication even though
                   we didn't add an Authorization header to previous
                   request. That means it supports persistent authentication. */
                gss_info->pstate = pstate_stateful;
                serf_connection_set_max_outstanding_requests(conn, 0);
                break;
            default:
                /* Nothing to do here. */
                break;
        }
    }

    return APR_SUCCESS;
}

#endif /* SERF_HAVE_SPNEGO */
