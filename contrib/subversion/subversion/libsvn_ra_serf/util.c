/*
 * util.c : serf utility routines for ra_serf
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



#include <assert.h>

#define APR_WANT_STRFUNC
#include <apr.h>
#include <apr_want.h>

#include <serf.h>
#include <serf_bucket_types.h>

#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "svn_string.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "../libsvn_ra/ra_loader.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"
#include "private/svn_auth_private.h"
#include "private/svn_cert.h"

#include "ra_serf.h"

static const apr_uint32_t serf_failure_map[][2] =
{
  { SERF_SSL_CERT_NOTYETVALID,   SVN_AUTH_SSL_NOTYETVALID },
  { SERF_SSL_CERT_EXPIRED,       SVN_AUTH_SSL_EXPIRED },
  { SERF_SSL_CERT_SELF_SIGNED,   SVN_AUTH_SSL_UNKNOWNCA },
  { SERF_SSL_CERT_UNKNOWNCA,     SVN_AUTH_SSL_UNKNOWNCA }
};

/* Return a Subversion failure mask based on FAILURES, a serf SSL
   failure mask.  If anything in FAILURES is not directly mappable to
   Subversion failures, set SVN_AUTH_SSL_OTHER in the returned mask. */
static apr_uint32_t
ssl_convert_serf_failures(int failures)
{
  apr_uint32_t svn_failures = 0;
  apr_size_t i;

  for (i = 0;
       i < sizeof(serf_failure_map) / (sizeof(serf_failure_map[0]));
       ++i)
    {
      if (failures & serf_failure_map[i][0])
        {
          svn_failures |= serf_failure_map[i][1];
          failures &= ~serf_failure_map[i][0];
        }
    }

  /* Map any remaining failure bits to our OTHER bit. */
  if (failures)
    {
      svn_failures |= SVN_AUTH_SSL_OTHER;
    }

  return svn_failures;
}


static apr_status_t
save_error(svn_ra_serf__session_t *session,
           svn_error_t *err)
{
  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(
                                  session->pending_error,
                                  err);
      return session->pending_error->apr_err;
    }

  return APR_SUCCESS;
}


/* Construct the realmstring, e.g. https://svn.collab.net:443. */
static const char *
construct_realm(svn_ra_serf__session_t *session,
                apr_pool_t *pool)
{
  const char *realm;
  apr_port_t port;

  if (session->session_url.port_str)
    {
      port = session->session_url.port;
    }
  else
    {
      port = apr_uri_port_of_scheme(session->session_url.scheme);
    }

  realm = apr_psprintf(pool, "%s://%s:%d",
                       session->session_url.scheme,
                       session->session_url.hostname,
                       port);

  return realm;
}

/* Convert a hash table containing the fields (as documented in X.509) of an
   organisation to a string ORG, allocated in POOL. ORG is as returned by
   serf_ssl_cert_issuer() and serf_ssl_cert_subject(). */
static char *
convert_organisation_to_str(apr_hash_t *org, apr_pool_t *pool)
{
  const char *cn = svn_hash_gets(org, "CN");
  const char *org_unit = svn_hash_gets(org, "OU");
  const char *org_name = svn_hash_gets(org, "O");
  const char *locality = svn_hash_gets(org, "L");
  const char *state = svn_hash_gets(org, "ST");
  const char *country = svn_hash_gets(org, "C");
  const char *email = svn_hash_gets(org, "E");
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);

  if (cn)
    {
      svn_stringbuf_appendcstr(buf, cn);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  if (org_unit)
    {
      svn_stringbuf_appendcstr(buf, org_unit);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  if (org_name)
    {
      svn_stringbuf_appendcstr(buf, org_name);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  if (locality)
    {
      svn_stringbuf_appendcstr(buf, locality);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  if (state)
    {
      svn_stringbuf_appendcstr(buf, state);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  if (country)
    {
      svn_stringbuf_appendcstr(buf, country);
      svn_stringbuf_appendcstr(buf, ", ");
    }

  /* Chop ', ' if any. */
  svn_stringbuf_chop(buf, 2);

  if (email)
    {
      svn_stringbuf_appendcstr(buf, "(");
      svn_stringbuf_appendcstr(buf, email);
      svn_stringbuf_appendcstr(buf, ")");
    }

  return buf->data;
}

static void append_reason(svn_stringbuf_t *errmsg, const char *reason, int *reasons)
{
  if (*reasons < 1)
    svn_stringbuf_appendcstr(errmsg, _(": "));
  else
    svn_stringbuf_appendcstr(errmsg, _(", "));
  svn_stringbuf_appendcstr(errmsg, reason);
  (*reasons)++;
}

/* This function is called on receiving a ssl certificate of a server when
   opening a https connection. It allows Subversion to override the initial
   validation done by serf.
   Serf provides us the @a baton as provided in the call to
   serf_ssl_server_cert_callback_set. The result of serf's initial validation
   of the certificate @a CERT is returned as a bitmask in FAILURES. */
static svn_error_t *
ssl_server_cert(void *baton, int failures,
                const serf_ssl_certificate_t *cert,
                apr_pool_t *scratch_pool)
{
  svn_ra_serf__connection_t *conn = baton;
  svn_auth_ssl_server_cert_info_t cert_info;
  svn_auth_cred_ssl_server_trust_t *server_creds = NULL;
  svn_auth_iterstate_t *state;
  const char *realmstring;
  apr_uint32_t svn_failures;
  apr_hash_t *issuer;
  apr_hash_t *subject = NULL;
  apr_hash_t *serf_cert = NULL;
  void *creds;

  svn_failures = (ssl_convert_serf_failures(failures)
      | conn->server_cert_failures);

  if (serf_ssl_cert_depth(cert) == 0)
    {
      /* If the depth is 0, the hostname must match the certificate.

      ### This should really be handled by serf, which should pass an error
          for this case, but that has backwards compatibility issues. */
      apr_array_header_t *san;
      svn_boolean_t found_matching_hostname = FALSE;
      svn_string_t *actual_hostname =
          svn_string_create(conn->session->session_url.hostname, scratch_pool);

      serf_cert = serf_ssl_cert_certificate(cert, scratch_pool);

      san = svn_hash_gets(serf_cert, "subjectAltName");
      /* Match server certificate CN with the hostname of the server iff
       * we didn't find any subjectAltName fields and try to match them.
       * Per RFC 2818 they are authoritative if present and CommonName
       * should be ignored.  NOTE: This isn't 100% correct since serf
       * only loads the subjectAltName hash with dNSNames, technically
       * we should ignore the CommonName if any subjectAltName entry
       * exists even if it is one we don't support. */
      if (san && san->nelts > 0)
        {
          int i;
          for (i = 0; i < san->nelts; i++)
            {
              const char *s = APR_ARRAY_IDX(san, i, const char*);
              svn_string_t *cert_hostname = svn_string_create(s, scratch_pool);

              if (svn_cert__match_dns_identity(cert_hostname, actual_hostname))
                {
                  found_matching_hostname = TRUE;
                  break;
                }
            }
        }
      else
        {
          const char *hostname = NULL;

          subject = serf_ssl_cert_subject(cert, scratch_pool);

          if (subject)
            hostname = svn_hash_gets(subject, "CN");

          if (hostname)
            {
              svn_string_t *cert_hostname = svn_string_create(hostname,
                                                              scratch_pool);

              if (svn_cert__match_dns_identity(cert_hostname, actual_hostname))
                {
                  found_matching_hostname = TRUE;
                }
            }
        }

      if (!found_matching_hostname)
        svn_failures |= SVN_AUTH_SSL_CNMISMATCH;
    }

  if (!svn_failures)
    return SVN_NO_ERROR;

  /* Extract the info from the certificate */
  if (! subject)
    subject = serf_ssl_cert_subject(cert, scratch_pool);
  issuer = serf_ssl_cert_issuer(cert, scratch_pool);
  if (! serf_cert)
    serf_cert = serf_ssl_cert_certificate(cert, scratch_pool);

  cert_info.hostname = svn_hash_gets(subject, "CN");
  cert_info.fingerprint = svn_hash_gets(serf_cert, "sha1");
  if (! cert_info.fingerprint)
    cert_info.fingerprint = apr_pstrdup(scratch_pool, "<unknown>");
  cert_info.valid_from = svn_hash_gets(serf_cert, "notBefore");
  if (! cert_info.valid_from)
    cert_info.valid_from = apr_pstrdup(scratch_pool, "[invalid date]");
  cert_info.valid_until = svn_hash_gets(serf_cert, "notAfter");
  if (! cert_info.valid_until)
    cert_info.valid_until = apr_pstrdup(scratch_pool, "[invalid date]");
  cert_info.issuer_dname = convert_organisation_to_str(issuer, scratch_pool);
  cert_info.ascii_cert = serf_ssl_cert_export(cert, scratch_pool);

  /* Handle any non-server certs. */
  if (serf_ssl_cert_depth(cert) > 0)
    {
      svn_error_t *err;

      svn_auth_set_parameter(conn->session->auth_baton,
                             SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                             &cert_info);

      svn_auth_set_parameter(conn->session->auth_baton,
                             SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                             &svn_failures);

      realmstring = apr_psprintf(scratch_pool, "AUTHORITY:%s",
                                 cert_info.fingerprint);

      err = svn_auth_first_credentials(&creds, &state,
                                       SVN_AUTH_CRED_SSL_SERVER_AUTHORITY,
                                       realmstring,
                                       conn->session->auth_baton,
                                       scratch_pool);

      svn_auth_set_parameter(conn->session->auth_baton,
                             SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO, NULL);

      svn_auth_set_parameter(conn->session->auth_baton,
                             SVN_AUTH_PARAM_SSL_SERVER_FAILURES, NULL);

      if (err)
        {
          if (err->apr_err != SVN_ERR_AUTHN_NO_PROVIDER)
            return svn_error_trace(err);

          /* No provider registered that handles server authorities */
          svn_error_clear(err);
          creds = NULL;
        }

      if (creds)
        {
          server_creds = creds;
          SVN_ERR(svn_auth_save_credentials(state, scratch_pool));

          svn_failures &= ~server_creds->accepted_failures;
        }

      if (svn_failures)
        conn->server_cert_failures |= svn_failures;

      return APR_SUCCESS;
    }

  svn_auth_set_parameter(conn->session->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                         &svn_failures);

  svn_auth_set_parameter(conn->session->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                         &cert_info);

  realmstring = construct_realm(conn->session, conn->session->pool);

  SVN_ERR(svn_auth_first_credentials(&creds, &state,
                                     SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                     realmstring,
                                     conn->session->auth_baton,
                                     scratch_pool));
  if (creds)
    {
      server_creds = creds;
      svn_failures &= ~server_creds->accepted_failures;
      SVN_ERR(svn_auth_save_credentials(state, scratch_pool));
    }

  while (svn_failures && creds)
    {
      SVN_ERR(svn_auth_next_credentials(&creds, state, scratch_pool));

      if (creds)
        {
          server_creds = creds;
          svn_failures &= ~server_creds->accepted_failures;
          SVN_ERR(svn_auth_save_credentials(state, scratch_pool));
        }
    }

  svn_auth_set_parameter(conn->session->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO, NULL);

  /* Are there non accepted failures left? */
  if (svn_failures)
    {
      svn_stringbuf_t *errmsg;
      int reasons = 0;

      errmsg = svn_stringbuf_create(
                 _("Server SSL certificate verification failed"),
                 scratch_pool);


      if (svn_failures & SVN_AUTH_SSL_NOTYETVALID)
        append_reason(errmsg, _("certificate is not yet valid"), &reasons);

      if (svn_failures & SVN_AUTH_SSL_EXPIRED)
        append_reason(errmsg, _("certificate has expired"), &reasons);

      if (svn_failures & SVN_AUTH_SSL_CNMISMATCH)
        append_reason(errmsg,
                      _("certificate issued for a different hostname"),
                      &reasons);

      if (svn_failures & SVN_AUTH_SSL_UNKNOWNCA)
        append_reason(errmsg, _("issuer is not trusted"), &reasons);

      if (svn_failures & SVN_AUTH_SSL_OTHER)
        append_reason(errmsg, _("and other reason(s)"), &reasons);

      return svn_error_create(SVN_ERR_RA_SERF_SSL_CERT_UNTRUSTED, NULL,
                              errmsg->data);
    }

  return SVN_NO_ERROR;
}

/* Implements serf_ssl_need_server_cert_t for ssl_server_cert */
static apr_status_t
ssl_server_cert_cb(void *baton, int failures,
                const serf_ssl_certificate_t *cert)
{
  svn_ra_serf__connection_t *conn = baton;
  svn_ra_serf__session_t *session = conn->session;
  apr_pool_t *subpool;
  svn_error_t *err;

  subpool = svn_pool_create(session->pool);
  err = svn_error_trace(ssl_server_cert(baton, failures, cert, subpool));
  svn_pool_destroy(subpool);

  return save_error(session, err);
}

static svn_error_t *
load_authorities(svn_ra_serf__connection_t *conn, const char *authorities,
                 apr_pool_t *pool)
{
  apr_array_header_t *files = svn_cstring_split(authorities, ";",
                                                TRUE /* chop_whitespace */,
                                                pool);
  int i;

  for (i = 0; i < files->nelts; ++i)
    {
      const char *file = APR_ARRAY_IDX(files, i, const char *);
      serf_ssl_certificate_t *ca_cert;
      apr_status_t status = serf_ssl_load_cert_file(&ca_cert, file, pool);

      if (status == APR_SUCCESS)
        status = serf_ssl_trust_cert(conn->ssl_context, ca_cert);

      if (status != APR_SUCCESS)
        {
          return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
             _("Invalid config: unable to load certificate file '%s'"),
             svn_dirent_local_style(file, pool));
        }
    }

  return SVN_NO_ERROR;
}

#if SERF_VERSION_AT_LEAST(1, 4, 0) && defined(SVN__SERF_TEST_HTTP2)
/* Implements serf_ssl_protocol_result_cb_t */
static apr_status_t
conn_negotiate_protocol(void *data,
                        const char *protocol)
{
  svn_ra_serf__connection_t *conn = data;

  if (!strcmp(protocol, "h2"))
    {
      serf_connection_set_framing_type(
            conn->conn,
            SERF_CONNECTION_FRAMING_TYPE_HTTP2);

      /* Disable generating content-length headers. */
      conn->session->http10 = FALSE;
      conn->session->http20 = TRUE;
      conn->session->using_chunked_requests = TRUE;
      conn->session->detect_chunking = FALSE;
    }
  else
    {
      /* protocol should be "" or "http/1.1" */
      serf_connection_set_framing_type(
            conn->conn,
            SERF_CONNECTION_FRAMING_TYPE_HTTP1);
    }

  return APR_SUCCESS;
}
#endif

static svn_error_t *
conn_setup(apr_socket_t *sock,
           serf_bucket_t **read_bkt,
           serf_bucket_t **write_bkt,
           void *baton,
           apr_pool_t *pool)
{
  svn_ra_serf__connection_t *conn = baton;

  *read_bkt = serf_context_bucket_socket_create(conn->session->context,
                                               sock, conn->bkt_alloc);

  if (conn->session->using_ssl)
    {
      /* input stream */
      *read_bkt = serf_bucket_ssl_decrypt_create(*read_bkt, conn->ssl_context,
                                                 conn->bkt_alloc);
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_encrypt_context_get(*read_bkt);

          serf_ssl_set_hostname(conn->ssl_context,
                                conn->session->session_url.hostname);

          serf_ssl_client_cert_provider_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert,
                                            conn, conn->session->pool);
          serf_ssl_client_cert_password_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert_pw,
                                            conn, conn->session->pool);
          serf_ssl_server_cert_callback_set(conn->ssl_context,
                                            ssl_server_cert_cb,
                                            conn);

          /* See if the user wants us to trust "default" openssl CAs. */
          if (conn->session->trust_default_ca)
            {
              serf_ssl_use_default_certificates(conn->ssl_context);
            }
          /* Are there custom CAs to load? */
          if (conn->session->ssl_authorities)
            {
              SVN_ERR(load_authorities(conn, conn->session->ssl_authorities,
                                       conn->session->pool));
            }
#if SERF_VERSION_AT_LEAST(1, 4, 0) && defined(SVN__SERF_TEST_HTTP2)
          if (APR_SUCCESS ==
                serf_ssl_negotiate_protocol(conn->ssl_context, "h2,http/1.1",
                                            conn_negotiate_protocol, conn))
            {
                serf_connection_set_framing_type(
                            conn->conn,
                            SERF_CONNECTION_FRAMING_TYPE_NONE);
            }
#endif
        }

      if (write_bkt)
        {
          /* output stream */
          *write_bkt = serf_bucket_ssl_encrypt_create(*write_bkt,
                                                      conn->ssl_context,
                                                      conn->bkt_alloc);
        }
    }

  return SVN_NO_ERROR;
}

/* svn_ra_serf__conn_setup is a callback for serf. This function
   creates a read bucket and will wrap the write bucket if SSL
   is needed. */
apr_status_t
svn_ra_serf__conn_setup(apr_socket_t *sock,
                        serf_bucket_t **read_bkt,
                        serf_bucket_t **write_bkt,
                        void *baton,
                        apr_pool_t *pool)
{
  svn_ra_serf__connection_t *conn = baton;
  svn_ra_serf__session_t *session = conn->session;
  svn_error_t *err;

  err = svn_error_trace(conn_setup(sock,
                                   read_bkt,
                                   write_bkt,
                                   baton,
                                   pool));
  return save_error(session, err);
}


/* Our default serf response acceptor.  */
static serf_bucket_t *
accept_response(serf_request_t *request,
                serf_bucket_t *stream,
                void *acceptor_baton,
                apr_pool_t *pool)
{
  /* svn_ra_serf__handler_t *handler = acceptor_baton; */
  serf_bucket_t *c;
  serf_bucket_alloc_t *bkt_alloc;

  bkt_alloc = serf_request_get_alloc(request);
  c = serf_bucket_barrier_create(stream, bkt_alloc);

  return serf_bucket_response_create(c, bkt_alloc);
}


/* Custom response acceptor for HEAD requests.  */
static serf_bucket_t *
accept_head(serf_request_t *request,
            serf_bucket_t *stream,
            void *acceptor_baton,
            apr_pool_t *pool)
{
  /* svn_ra_serf__handler_t *handler = acceptor_baton; */
  serf_bucket_t *response;

  response = accept_response(request, stream, acceptor_baton, pool);

  /* We know we shouldn't get a response body. */
  serf_bucket_response_set_head(response);

  return response;
}

static svn_error_t *
connection_closed(svn_ra_serf__connection_t *conn,
                  apr_status_t why,
                  apr_pool_t *pool)
{
  if (why)
    {
      return svn_ra_serf__wrap_err(why, NULL);
    }

  if (conn->session->using_ssl)
    conn->ssl_context = NULL;

  return SVN_NO_ERROR;
}

void
svn_ra_serf__conn_closed(serf_connection_t *conn,
                         void *closed_baton,
                         apr_status_t why,
                         apr_pool_t *pool)
{
  svn_ra_serf__connection_t *ra_conn = closed_baton;
  svn_error_t *err;

  err = svn_error_trace(connection_closed(ra_conn, why, pool));

  (void) save_error(ra_conn->session, err);
}


/* Implementation of svn_ra_serf__handle_client_cert */
static svn_error_t *
handle_client_cert(void *data,
                   const char **cert_path,
                   apr_pool_t *pool)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    const char *realm;
    void *creds;

    *cert_path = NULL;

    realm = construct_realm(session, session->pool);

    if (!conn->ssl_client_auth_state)
      {
        SVN_ERR(svn_auth_first_credentials(&creds,
                                           &conn->ssl_client_auth_state,
                                           SVN_AUTH_CRED_SSL_CLIENT_CERT,
                                           realm,
                                           session->auth_baton,
                                           pool));
      }
    else
      {
        SVN_ERR(svn_auth_next_credentials(&creds,
                                          conn->ssl_client_auth_state,
                                          session->pool));
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_t *client_creds;
        client_creds = creds;
        *cert_path = client_creds->cert_file;
      }

    return SVN_NO_ERROR;
}

/* Implements serf_ssl_need_client_cert_t for handle_client_cert */
apr_status_t svn_ra_serf__handle_client_cert(void *data,
                                             const char **cert_path)
{
  svn_ra_serf__connection_t *conn = data;
  svn_ra_serf__session_t *session = conn->session;
  svn_error_t *err;

  err = svn_error_trace(handle_client_cert(data, cert_path, session->pool));

  return save_error(session, err);
}

/* Implementation for svn_ra_serf__handle_client_cert_pw */
static svn_error_t *
handle_client_cert_pw(void *data,
                      const char *cert_path,
                      const char **password,
                      apr_pool_t *pool)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    void *creds;

    *password = NULL;

    if (!conn->ssl_client_pw_auth_state)
      {
        SVN_ERR(svn_auth_first_credentials(&creds,
                                           &conn->ssl_client_pw_auth_state,
                                           SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                           cert_path,
                                           session->auth_baton,
                                           pool));
      }
    else
      {
        SVN_ERR(svn_auth_next_credentials(&creds,
                                          conn->ssl_client_pw_auth_state,
                                          pool));
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_pw_t *pw_creds;
        pw_creds = creds;
        *password = pw_creds->password;
      }

    return APR_SUCCESS;
}

/* Implements serf_ssl_need_client_cert_pw_t for handle_client_cert_pw */
apr_status_t svn_ra_serf__handle_client_cert_pw(void *data,
                                                const char *cert_path,
                                                const char **password)
{
  svn_ra_serf__connection_t *conn = data;
  svn_ra_serf__session_t *session = conn->session;
  svn_error_t *err;

  err = svn_error_trace(handle_client_cert_pw(data,
                                              cert_path,
                                              password,
                                              session->pool));

  return save_error(session, err);
}


/*
 * Given a REQUEST on connection CONN, construct a request bucket for it,
 * returning the bucket in *REQ_BKT.
 *
 * If HDRS_BKT is not-NULL, it will be set to a headers_bucket that
 * corresponds to the new request.
 *
 * The request will be METHOD at URL.
 *
 * If BODY_BKT is not-NULL, it will be sent as the request body.
 *
 * If CONTENT_TYPE is not-NULL, it will be sent as the Content-Type header.
 *
 * If DAV_HEADERS is non-zero, it will add standard DAV capabilites headers
 * to request.
 *
 * REQUEST_POOL should live for the duration of the request. Serf will
 * construct this and provide it to the request_setup callback, so we
 * should just use that one.
 */
static svn_error_t *
setup_serf_req(serf_request_t *request,
               serf_bucket_t **req_bkt,
               serf_bucket_t **hdrs_bkt,
               svn_ra_serf__session_t *session,
               const char *method, const char *url,
               serf_bucket_t *body_bkt, const char *content_type,
               const char *accept_encoding,
               svn_boolean_t dav_headers,
               apr_pool_t *request_pool,
               apr_pool_t *scratch_pool)
{
  serf_bucket_alloc_t *allocator = serf_request_get_alloc(request);

  svn_spillbuf_t *buf;
  svn_boolean_t set_CL = session->http10 || !session->using_chunked_requests;

  if (set_CL && body_bkt != NULL)
    {
      /* Ugh. Use HTTP/1.0 to talk to the server because we don't know if
         it speaks HTTP/1.1 (and thus, chunked requests), or because the
         server actually responded as only supporting HTTP/1.0.

         We'll take the existing body_bkt, spool it into a spillbuf, and
         then wrap a bucket around that spillbuf. The spillbuf will give
         us the Content-Length value.  */
      SVN_ERR(svn_ra_serf__copy_into_spillbuf(&buf, body_bkt,
                                              request_pool,
                                              scratch_pool));
      /* Destroy original bucket since it content is already copied
         to spillbuf. */
      serf_bucket_destroy(body_bkt);

      body_bkt = svn_ra_serf__create_sb_bucket(buf, allocator,
                                               request_pool,
                                               scratch_pool);
    }

  /* Create a request bucket.  Note that this sucker is kind enough to
     add a "Host" header for us.  */
  *req_bkt = serf_request_bucket_request_create(request, method, url,
                                                body_bkt, allocator);

  /* Set the Content-Length value. This will also trigger an HTTP/1.0
     request (rather than the default chunked request).  */
  if (set_CL)
    {
      if (body_bkt == NULL)
        serf_bucket_request_set_CL(*req_bkt, 0);
      else
        serf_bucket_request_set_CL(*req_bkt, svn_spillbuf__get_size(buf));
    }

  *hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);

  /* We use serf_bucket_headers_setn() because the USERAGENT has a
     lifetime longer than this bucket. Thus, there is no need to copy
     the header values.  */
  serf_bucket_headers_setn(*hdrs_bkt, "User-Agent", session->useragent);

  if (content_type)
    {
      serf_bucket_headers_setn(*hdrs_bkt, "Content-Type", content_type);
    }

  if (session->http10)
    {
      serf_bucket_headers_setn(*hdrs_bkt, "Connection", "keep-alive");
    }

  if (accept_encoding)
    {
      serf_bucket_headers_setn(*hdrs_bkt, "Accept-Encoding", accept_encoding);
    }

  /* These headers need to be sent with every request that might need
     capability processing (e.g. during commit, reports, etc.), see
     issue #3255 ("mod_dav_svn does not pass client capabilities to
     start-commit hooks") for why.

     Some request types like GET/HEAD/PROPFIND are unaware of capability
     handling; and in some cases the responses can even be cached by
     proxies, so we don't have to send these hearders there. */
  if (dav_headers)
    {
      serf_bucket_headers_setn(*hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_DEPTH);
      serf_bucket_headers_setn(*hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_MERGEINFO);
      serf_bucket_headers_setn(*hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_LOG_REVPROPS);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__context_run(svn_ra_serf__session_t *sess,
                         apr_interval_time_t *waittime_left,
                         apr_pool_t *scratch_pool)
{
  apr_status_t status;
  svn_error_t *err;
  assert(sess->pending_error == SVN_NO_ERROR);

  if (sess->cancel_func)
    SVN_ERR(sess->cancel_func(sess->cancel_baton));

  status = serf_context_run(sess->context,
                            SVN_RA_SERF__CONTEXT_RUN_DURATION,
                            scratch_pool);

  err = sess->pending_error;
  sess->pending_error = SVN_NO_ERROR;

   /* If the context duration timeout is up, we'll subtract that
      duration from the total time alloted for such things.  If
      there's no time left, we fail with a message indicating that
      the connection timed out.  */
  if (APR_STATUS_IS_TIMEUP(status))
    {
      status = 0;

      if (sess->timeout)
        {
          if (*waittime_left > SVN_RA_SERF__CONTEXT_RUN_DURATION)
            {
              *waittime_left -= SVN_RA_SERF__CONTEXT_RUN_DURATION;
            }
          else
            {
              return
                  svn_error_compose_create(
                        err,
                        svn_error_create(SVN_ERR_RA_DAV_CONN_TIMEOUT, NULL,
                                         _("Connection timed out")));
            }
        }
    }
  else
    {
      *waittime_left = sess->timeout;
    }

  SVN_ERR(err);
  if (status)
    {
      /* ### This omits SVN_WARNING, and possibly relies on the fact that
         ### MAX(SERF_ERROR_*) < SVN_ERR_BAD_CATEGORY_START? */
      if (status >= SVN_ERR_BAD_CATEGORY_START && status < SVN_ERR_LAST)
        {
          /* apr can't translate subversion errors to text */
          SVN_ERR_W(svn_error_create(status, NULL, NULL),
                    _("Error running context"));
        }

      return svn_ra_serf__wrap_err(status, _("Error running context"));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__context_run_wait(svn_boolean_t *done,
                              svn_ra_serf__session_t *sess,
                              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_interval_time_t waittime_left = sess->timeout;

  assert(sess->pending_error == SVN_NO_ERROR);

  iterpool = svn_pool_create(scratch_pool);
  while (!*done)
    {
      int i;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_serf__context_run(sess, &waittime_left, iterpool));

      /* Debugging purposes only! */
      for (i = 0; i < sess->num_conns; i++)
        {
          serf_debug__closed_conn(sess->conns[i]->bkt_alloc);
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ensure that a handler is no longer scheduled on the connection.

   Eventually serf will have a reliable way to cancel existing requests,
   but currently it doesn't even have a way to relyable identify a request
   after rescheduling, for auth reasons.

   So the only thing we can do today is reset the connection, which
   will cancel all outstanding requests and prepare the connection
   for re-use.
*/
void
svn_ra_serf__unschedule_handler(svn_ra_serf__handler_t *handler)
{
  serf_connection_reset(handler->conn->conn);
  handler->scheduled = FALSE;
}

svn_error_t *
svn_ra_serf__context_run_one(svn_ra_serf__handler_t *handler,
                             apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* Create a serf request based on HANDLER.  */
  svn_ra_serf__request_create(handler);

  /* Wait until the response logic marks its DONE status.  */
  err = svn_ra_serf__context_run_wait(&handler->done, handler->session,
                                      scratch_pool);

  if (handler->scheduled)
    {
      /* We reset the connection (breaking  pipelining, etc.), as
         if we didn't the next data would still be handled by this handler,
         which is done as far as our caller is concerned. */
      svn_ra_serf__unschedule_handler(handler);
    }

  return svn_error_trace(err);
}




static apr_status_t
drain_bucket(serf_bucket_t *bucket)
{
  /* Read whatever is in the bucket, and just drop it.  */
  while (1)
    {
      apr_status_t status;
      const char *data;
      apr_size_t len;

      status = serf_bucket_read(bucket, SERF_READ_ALL_AVAIL, &data, &len);
      if (status)
        return status;
    }
}




/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_discard_body(serf_request_t *request,
                                 serf_bucket_t *response,
                                 void *baton,
                                 apr_pool_t *pool)
{
  apr_status_t status;

  status = drain_bucket(response);
  if (status)
    return svn_ra_serf__wrap_err(status, NULL);

  return SVN_NO_ERROR;
}

apr_status_t
svn_ra_serf__response_discard_handler(serf_request_t *request,
                                      serf_bucket_t *response,
                                      void *baton,
                                      apr_pool_t *pool)
{
  return drain_bucket(response);
}


/* Return the value of the RESPONSE's Location header if any, or NULL
   otherwise.  */
static const char *
response_get_location(serf_bucket_t *response,
                      const char *base_url,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  serf_bucket_t *headers;
  const char *location;

  headers = serf_bucket_response_get_headers(response);
  location = serf_bucket_headers_get(headers, "Location");
  if (location == NULL)
    return NULL;

  /* The RFCs say we should have received a full url in LOCATION, but
     older apache versions and many custom web handlers just return a
     relative path here...

     And we can't trust anything because it is network data.
   */
  if (*location == '/')
    {
      apr_uri_t uri;
      apr_status_t status;

      status = apr_uri_parse(scratch_pool, base_url, &uri);

      if (status != APR_SUCCESS)
        return NULL;

      /* Replace the path path with what we got */
      uri.path = (char*)svn_urlpath__canonicalize(location, scratch_pool);

      /* And make APR produce a proper full url for us */
      location = apr_uri_unparse(scratch_pool, &uri, 0);

      /* Fall through to ensure our canonicalization rules */
    }
  else if (!svn_path_is_url(location))
    {
      return NULL; /* Any other formats we should support? */
    }

  return svn_uri_canonicalize(location, result_pool);
}


/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__expect_empty_body(serf_request_t *request,
                               serf_bucket_t *response,
                               void *baton,
                               apr_pool_t *scratch_pool)
{
  svn_ra_serf__handler_t *handler = baton;
  serf_bucket_t *hdrs;
  const char *val;

  /* This function is just like handle_multistatus_only() except for the
     XML parsing callbacks. We want to look for the -readable element.  */

  /* We should see this just once, in order to initialize SERVER_ERROR.
     At that point, the core error processing will take over. If we choose
     not to parse an error, then we'll never return here (because we
     change the response handler).  */
  SVN_ERR_ASSERT(handler->server_error == NULL);

  hdrs = serf_bucket_response_get_headers(response);
  val = serf_bucket_headers_get(hdrs, "Content-Type");
  if (val
      && (handler->sline.code < 200 || handler->sline.code >= 300)
      && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
    {
      svn_ra_serf__server_error_t *server_err;

      SVN_ERR(svn_ra_serf__setup_error_parsing(&server_err, handler,
                                               FALSE,
                                               handler->handler_pool,
                                               handler->handler_pool));

      handler->server_error = server_err;
    }
  else
    {
      /* The body was not text/xml, or we got a success code.
         Toss anything that arrives.  */
      handler->discard_body = TRUE;
    }

  /* Returning SVN_NO_ERROR will return APR_SUCCESS to serf, which tells it
     to call the response handler again. That will start up the XML parsing,
     or it will be dropped on the floor (per the decision above).  */
  return SVN_NO_ERROR;
}


apr_status_t
svn_ra_serf__credentials_callback(char **username, char **password,
                                  serf_request_t *request, void *baton,
                                  int code, const char *authn_type,
                                  const char *realm,
                                  apr_pool_t *pool)
{
  svn_ra_serf__handler_t *handler = baton;
  svn_ra_serf__session_t *session = handler->session;
  void *creds;
  svn_auth_cred_simple_t *simple_creds;
  svn_error_t *err;

  if (code == 401)
    {
      /* Use svn_auth_first_credentials if this is the first time we ask for
         credentials during this session OR if the last time we asked
         session->auth_state wasn't set (eg. if the credentials provider was
         cancelled by the user). */
      if (!session->auth_state)
        {
          err = svn_auth_first_credentials(&creds,
                                           &session->auth_state,
                                           SVN_AUTH_CRED_SIMPLE,
                                           realm,
                                           session->auth_baton,
                                           session->pool);
        }
      else
        {
          err = svn_auth_next_credentials(&creds,
                                          session->auth_state,
                                          session->pool);
        }

      if (err)
        {
          (void) save_error(session, err);
          return err->apr_err;
        }

      session->auth_attempts++;

      if (!creds || session->auth_attempts > 4)
        {
          /* No more credentials. */
          (void) save_error(session,
                            svn_error_create(
                              SVN_ERR_AUTHN_FAILED, NULL,
                              _("No more credentials or we tried too many "
                                "times.\nAuthentication failed")));
          return SVN_ERR_AUTHN_FAILED;
        }

      simple_creds = creds;
      *username = apr_pstrdup(pool, simple_creds->username);
      *password = apr_pstrdup(pool, simple_creds->password);
    }
  else
    {
      *username = apr_pstrdup(pool, session->proxy_username);
      *password = apr_pstrdup(pool, session->proxy_password);

      session->proxy_auth_attempts++;

      if (!session->proxy_username || session->proxy_auth_attempts > 4)
        {
          /* No more credentials. */
          (void) save_error(session,
                            svn_error_create(
                              SVN_ERR_AUTHN_FAILED, NULL,
                              _("Proxy authentication failed")));
          return SVN_ERR_AUTHN_FAILED;
        }
    }

  handler->conn->last_status_code = code;

  return APR_SUCCESS;
}

/* Wait for HTTP response status and headers, and invoke HANDLER->
   response_handler() to carry out operation-specific processing.
   Afterwards, check for connection close.

   SERF_STATUS allows returning errors to serf without creating a
   subversion error object.
   */
static svn_error_t *
handle_response(serf_request_t *request,
                serf_bucket_t *response,
                svn_ra_serf__handler_t *handler,
                apr_status_t *serf_status,
                apr_pool_t *scratch_pool)
{
  apr_status_t status;
  svn_error_t *err;

  /* ### need to verify whether this already gets init'd on every
     ### successful exit. for an error-exit, it will (properly) be
     ### ignored by the caller.  */
  *serf_status = APR_SUCCESS;

  if (!response)
    {
      /* Uh-oh. Our connection died.  */
      handler->scheduled = FALSE;

      if (handler->response_error)
        {
          /* Give a handler chance to prevent request requeue. */
          SVN_ERR(handler->response_error(request, response, 0,
                                          handler->response_error_baton));

          svn_ra_serf__request_create(handler);
        }
      /* Response error callback is not configured. Requeue another request
         for this handler only if we didn't started to process body.
         Return error otherwise. */
      else if (!handler->reading_body)
        {
          svn_ra_serf__request_create(handler);
        }
      else
        {
          return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                    _("%s request on '%s' failed"),
                                   handler->method, handler->path);
        }

      return SVN_NO_ERROR;
    }

  /* If we're reading the body, then skip all this preparation.  */
  if (handler->reading_body)
    goto process_body;

  /* Copy the Status-Line info into HANDLER, if we don't yet have it.  */
  if (handler->sline.version == 0)
    {
      serf_status_line sl;

      status = serf_bucket_response_status(response, &sl);
      if (status != APR_SUCCESS)
        {
          /* The response line is not (yet) ready, or some other error.  */
          *serf_status = status;
          return SVN_NO_ERROR; /* Handled by serf */
        }

      /* If we got APR_SUCCESS, then we should have Status-Line info.  */
      SVN_ERR_ASSERT(sl.version != 0);

      handler->sline = sl;
      handler->sline.reason = apr_pstrdup(handler->handler_pool, sl.reason);

      /* HTTP/1.1? (or later)  */
      if (sl.version != SERF_HTTP_10)
        handler->session->http10 = FALSE;

      if (sl.version >= SERF_HTTP_VERSION(2, 0)) {
        handler->session->http20 = TRUE;
      }
    }

  /* Keep reading from the network until we've read all the headers.  */
  status = serf_bucket_response_wait_for_headers(response);
  if (status)
    {
      /* The typical "error" will be APR_EAGAIN, meaning that more input
         from the network is required to complete the reading of the
         headers.  */
      if (!APR_STATUS_IS_EOF(status))
        {
          /* Either the headers are not (yet) complete, or there really
             was an error.  */
          *serf_status = status;
          return SVN_NO_ERROR;
        }

      /* wait_for_headers() will return EOF if there is no body in this
         response, or if we completely read the body. The latter is not
         true since we would have set READING_BODY to get the body read,
         and we would not be back to this code block.

         It can also return EOF if we truly hit EOF while (say) processing
         the headers. aka Badness.  */

      /* Cases where a lack of a response body (via EOF) is okay:
       *  - A HEAD request
       *  - 204/304 response
       *
       * Otherwise, if we get an EOF here, something went really wrong: either
       * the server closed on us early or we're reading too much.  Either way,
       * scream loudly.
       */
      if (strcmp(handler->method, "HEAD") != 0
          && handler->sline.code != 204
          && handler->sline.code != 304)
        {
          err = svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA,
                                  svn_ra_serf__wrap_err(status, NULL),
                                  _("Premature EOF seen from server"
                                    " (http status=%d)"),
                                  handler->sline.code);

          /* In case anything else arrives... discard it.  */
          handler->discard_body = TRUE;

          return err;
        }
    }

  /* ... and set up the header fields in HANDLER.  */
  handler->location = response_get_location(response,
                                            handler->session->session_url_str,
                                            handler->handler_pool,
                                            scratch_pool);

  /* On the last request, we failed authentication. We succeeded this time,
     so let's save away these credentials.  */
  if (handler->conn->last_status_code == 401 && handler->sline.code < 400)
    {
      SVN_ERR(svn_auth_save_credentials(handler->session->auth_state,
                                        handler->session->pool));
      handler->session->auth_attempts = 0;
      handler->session->auth_state = NULL;
    }
  handler->conn->last_status_code = handler->sline.code;

  if (handler->sline.code >= 400)
    {
      /* 405 Method Not allowed.
         408 Request Timeout
         409 Conflict: can indicate a hook error.
         5xx (Internal) Server error. */
      serf_bucket_t *hdrs;
      const char *val;

      hdrs = serf_bucket_response_get_headers(response);
      val = serf_bucket_headers_get(hdrs, "Content-Type");
      if (val && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
        {
          svn_ra_serf__server_error_t *server_err;

          SVN_ERR(svn_ra_serf__setup_error_parsing(&server_err, handler,
                                                   FALSE,
                                                   handler->handler_pool,
                                                   handler->handler_pool));

          handler->server_error = server_err;
        }
      else
        {
          handler->discard_body = TRUE;
        }
    }
  else if (handler->sline.code <= 199)
    {
      handler->discard_body = TRUE;
    }

  /* Stop processing the above, on every packet arrival.  */
  handler->reading_body = TRUE;

 process_body:

  /* We've been instructed to ignore the body. Drain whatever is present.  */
  if (handler->discard_body)
    {
      *serf_status = drain_bucket(response);

      return SVN_NO_ERROR;
    }

  /* If we are supposed to parse the body as a server_error, then do
     that now.  */
  if (handler->server_error != NULL)
    {
      return svn_error_trace(
                svn_ra_serf__handle_server_error(handler->server_error,
                                                 handler,
                                                 request, response,
                                                 serf_status,
                                                 scratch_pool));
    }

  /* Pass the body along to the registered response handler.  */
  err = handler->response_handler(request, response,
                                  handler->response_baton,
                                  scratch_pool);

  if (err
      && (!SERF_BUCKET_READ_ERROR(err->apr_err)
          || APR_STATUS_IS_ECONNRESET(err->apr_err)
          || APR_STATUS_IS_ECONNABORTED(err->apr_err)))
    {
      /* These errors are special cased in serf
         ### We hope no handler returns these by accident. */
      *serf_status = err->apr_err;
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return svn_error_trace(err);
}


/* Implements serf_response_handler_t for handle_response. Storing
   errors in handler->session->pending_error if appropriate. */
static apr_status_t
handle_response_cb(serf_request_t *request,
                   serf_bucket_t *response,
                   void *baton,
                   apr_pool_t *response_pool)
{
  svn_ra_serf__handler_t *handler = baton;
  svn_error_t *err;
  apr_status_t inner_status;
  apr_status_t outer_status;
  apr_pool_t *scratch_pool = response_pool; /* Scratch pool needed? */

  err = svn_error_trace(handle_response(request, response,
                                        handler, &inner_status,
                                        scratch_pool));

  /* Select the right status value to return.  */
  outer_status = save_error(handler->session, err);
  if (!outer_status)
    outer_status = inner_status;

  /* Make sure the DONE flag is set properly and requests are cleaned up. */
  if (APR_STATUS_IS_EOF(outer_status) || APR_STATUS_IS_EOF(inner_status))
    {
      svn_ra_serf__session_t *sess = handler->session;
      handler->done = TRUE;
      handler->scheduled = FALSE;
      outer_status = APR_EOF;

      /* We use a cached handler->session here to allow handler to free the
         memory containing the handler */
      save_error(sess,
                 handler->done_delegate(request, handler->done_delegate_baton,
                                        scratch_pool));
    }
  else if (SERF_BUCKET_READ_ERROR(outer_status)
           && handler->session->pending_error)
    {
      handler->discard_body = TRUE; /* Discard further data */
      handler->done = TRUE; /* Mark as done */
      /* handler->scheduled is still TRUE, as we still expect data.
         If we would return an error outer-status the connection
         would have to be restarted. With scheduled still TRUE
         destroying the handler's pool will still reset the
         connection, avoiding the posibility of returning
         an error for this handler when a new request is
         scheduled. */
      outer_status = APR_EAGAIN; /* Exit context loop */
    }

  return outer_status;
}

/* Perform basic request setup, with special handling for HEAD requests,
   and finer-grained callbacks invoked (if non-NULL) to produce the request
   headers and body. */
static svn_error_t *
setup_request(serf_request_t *request,
              svn_ra_serf__handler_t *handler,
              serf_bucket_t **req_bkt,
              apr_pool_t *request_pool,
              apr_pool_t *scratch_pool)
{
  serf_bucket_t *body_bkt;
  serf_bucket_t *headers_bkt;
  const char *accept_encoding;

  if (handler->body_delegate)
    {
      serf_bucket_alloc_t *bkt_alloc = serf_request_get_alloc(request);

      SVN_ERR(handler->body_delegate(&body_bkt, handler->body_delegate_baton,
                                     bkt_alloc, request_pool, scratch_pool));
    }
  else
    {
      body_bkt = NULL;
    }

  if (handler->custom_accept_encoding)
    {
      accept_encoding = NULL;
    }
  else if (handler->session->using_compression != svn_tristate_false)
    {
      /* Accept gzip compression if enabled. */
      accept_encoding = "gzip";
    }
  else
    {
      accept_encoding = NULL;
    }

  SVN_ERR(setup_serf_req(request, req_bkt, &headers_bkt,
                         handler->session, handler->method, handler->path,
                         body_bkt, handler->body_type, accept_encoding,
                         !handler->no_dav_headers, request_pool,
                         scratch_pool));

  if (handler->header_delegate)
    {
      SVN_ERR(handler->header_delegate(headers_bkt,
                                       handler->header_delegate_baton,
                                       request_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Implements the serf_request_setup_t interface (which sets up both a
   request and its response handler callback). Handles errors for
   setup_request_cb */
static apr_status_t
setup_request_cb(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *s_handler,
              void **s_handler_baton,
              apr_pool_t *request_pool)
{
  svn_ra_serf__handler_t *handler = setup_baton;
  apr_pool_t *scratch_pool;
  svn_error_t *err;

  /* Construct a scratch_pool? serf gives us a pool that will live for
     the duration of the request. But requests are retried in some cases */
  scratch_pool = svn_pool_create(request_pool);

  if (strcmp(handler->method, "HEAD") == 0)
    *acceptor = accept_head;
  else
    *acceptor = accept_response;
  *acceptor_baton = handler;

  *s_handler = handle_response_cb;
  *s_handler_baton = handler;

  err = svn_error_trace(setup_request(request, handler, req_bkt,
                                      request_pool, scratch_pool));

  svn_pool_destroy(scratch_pool);
  return save_error(handler->session, err);
}

void
svn_ra_serf__request_create(svn_ra_serf__handler_t *handler)
{
  SVN_ERR_ASSERT_NO_RETURN(handler->handler_pool != NULL
                           && !handler->scheduled);

  /* In case HANDLER is re-queued, reset the various transient fields. */
  handler->done = FALSE;
  handler->server_error = NULL;
  handler->sline.version = 0;
  handler->location = NULL;
  handler->reading_body = FALSE;
  handler->discard_body = FALSE;
  handler->scheduled = TRUE;

  /* Keeping track of the returned request object would be nice, but doesn't
     work the way we would expect in ra_serf..

     Serf sometimes creates a new request for us (and destroys the old one)
     without telling, like when authentication failed (401/407 response.

     We 'just' trust serf to do the right thing and expect it to tell us
     when the state of the request changes.

     ### I fixed a request leak in serf in r2258 on auth failures.
   */
  (void) serf_connection_request_create(handler->conn->conn,
                                        setup_request_cb, handler);
}


svn_error_t *
svn_ra_serf__discover_vcc(const char **vcc_url,
                          svn_ra_serf__session_t *session,
                          apr_pool_t *scratch_pool)
{
  const char *path;
  const char *relative_path;
  const char *uuid;

  /* If we've already got the information our caller seeks, just return it.  */
  if (session->vcc_url && session->repos_root_str)
    {
      *vcc_url = session->vcc_url;
      return SVN_NO_ERROR;
    }

  path = session->session_url.path;
  *vcc_url = NULL;
  uuid = NULL;

  do
    {
      apr_hash_t *props;
      svn_error_t *err;

      err = svn_ra_serf__fetch_node_props(&props, session,
                                          path, SVN_INVALID_REVNUM,
                                          base_props,
                                          scratch_pool, scratch_pool);
      if (! err)
        {
          apr_hash_t *ns_props;

          ns_props = apr_hash_get(props, "DAV:", 4);
          *vcc_url = svn_prop_get_value(ns_props,
                                        "version-controlled-configuration");

          ns_props = svn_hash_gets(props, SVN_DAV_PROP_NS_DAV);
          relative_path = svn_prop_get_value(ns_props,
                                             "baseline-relative-path");
          uuid = svn_prop_get_value(ns_props, "repository-uuid");
          break;
        }
      else
        {
          if ((err->apr_err != SVN_ERR_FS_NOT_FOUND) &&
              (err->apr_err != SVN_ERR_RA_DAV_FORBIDDEN))
            {
              return svn_error_trace(err);  /* found a _real_ error */
            }
          else
            {
              /* This happens when the file is missing in HEAD. */
              svn_error_clear(err);

              /* Okay, strip off a component from PATH. */
              path = svn_urlpath__dirname(path, scratch_pool);
            }
        }
    }
  while ((path[0] != '\0')
         && (! (path[0] == '/' && path[1] == '\0')));

  if (!*vcc_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The PROPFIND response did not include the "
                                "requested version-controlled-configuration "
                                "value"));
    }

  /* Store our VCC in our cache. */
  if (!session->vcc_url)
    {
      session->vcc_url = apr_pstrdup(session->pool, *vcc_url);
    }

  /* Update our cached repository root URL. */
  if (!session->repos_root_str)
    {
      svn_stringbuf_t *url_buf;

      url_buf = svn_stringbuf_create(path, scratch_pool);

      svn_path_remove_components(url_buf,
                                 svn_path_component_count(relative_path));

      /* Now recreate the root_url. */
      session->repos_root = session->session_url;
      session->repos_root.path =
        (char *)svn_fspath__canonicalize(url_buf->data, session->pool);
      session->repos_root_str =
        svn_urlpath__canonicalize(apr_uri_unparse(session->pool,
                                                  &session->repos_root, 0),
                                  session->pool);
    }

  /* Store the repository UUID in the cache. */
  if (!session->uuid)
    {
      session->uuid = apr_pstrdup(session->pool, uuid);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_relative_path(const char **rel_path,
                               const char *orig_path,
                               svn_ra_serf__session_t *session,
                               apr_pool_t *pool)
{
  const char *decoded_root, *decoded_orig;

  if (! session->repos_root.path)
    {
      const char *vcc_url;

      /* This should only happen if we haven't detected HTTP v2
         support from the server.  */
      assert(! SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session));

      /* We don't actually care about the VCC_URL, but this API
         promises to populate the session's root-url cache, and that's
         what we really want. */
      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session,
                                        pool));
    }

  decoded_root = svn_path_uri_decode(session->repos_root.path, pool);
  decoded_orig = svn_path_uri_decode(orig_path, pool);
  *rel_path = svn_urlpath__skip_ancestor(decoded_root, decoded_orig);
  SVN_ERR_ASSERT(*rel_path != NULL);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__report_resource(const char **report_target,
                             svn_ra_serf__session_t *session,
                             apr_pool_t *pool)
{
  /* If we have HTTP v2 support, we want to report against the 'me'
     resource. */
  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    *report_target = apr_pstrdup(pool, session->me_resource);

  /* Otherwise, we'll use the default VCC. */
  else
    SVN_ERR(svn_ra_serf__discover_vcc(report_target, session, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__error_on_status(serf_status_line sline,
                             const char *path,
                             const char *location)
{
  switch(sline.code)
    {
      case 301:
      case 302:
      case 303:
      case 307:
      case 308:
        return svn_error_createf(SVN_ERR_RA_DAV_RELOCATED, NULL,
                                 (sline.code == 301)
                                  ? _("Repository moved permanently to '%s'")
                                  : _("Repository moved temporarily to '%s'"),
                                 location);
      case 403:
        return svn_error_createf(SVN_ERR_RA_DAV_FORBIDDEN, NULL,
                                 _("Access to '%s' forbidden"), path);

      case 404:
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("'%s' path not found"), path);
      case 405:
        return svn_error_createf(SVN_ERR_RA_DAV_METHOD_NOT_ALLOWED, NULL,
                                 _("HTTP method is not allowed on '%s'"),
                                 path);
      case 409:
        return svn_error_createf(SVN_ERR_FS_CONFLICT, NULL,
                                 _("'%s' conflicts"), path);
      case 412:
        return svn_error_createf(SVN_ERR_RA_DAV_PRECONDITION_FAILED, NULL,
                                 _("Precondition on '%s' failed"), path);
      case 423:
        return svn_error_createf(SVN_ERR_FS_NO_LOCK_TOKEN, NULL,
                                 _("'%s': no lock token available"), path);

      case 411:
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                    _("DAV request failed: 411 Content length required. The "
                      "server or an intermediate proxy does not accept "
                      "chunked encoding. Try setting 'http-chunked-requests' "
                      "to 'auto' or 'no' in your client configuration."));
      case 500:
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                 _("Unexpected server error %d '%s' on '%s'"),
                                 sline.code, sline.reason, path);
      case 501:
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("The requested feature is not supported by "
                                   "'%s'"), path);
    }

  if (sline.code >= 300 || sline.code <= 199)
    return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                             _("Unexpected HTTP status %d '%s' on '%s'"),
                             sline.code, sline.reason, path);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__unexpected_status(svn_ra_serf__handler_t *handler)
{
  /* Is it a standard error status? */
  if (handler->sline.code != 405)
    SVN_ERR(svn_ra_serf__error_on_status(handler->sline,
                                         handler->path,
                                         handler->location));

  switch (handler->sline.code)
    {
      case 201:
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                 _("Path '%s' unexpectedly created"),
                                 handler->path);
      case 204:
        return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                 _("Path '%s' already exists"),
                                 handler->path);

      case 405:
        return svn_error_createf(SVN_ERR_RA_DAV_METHOD_NOT_ALLOWED, NULL,
                                 _("The HTTP method '%s' is not allowed"
                                   " on '%s'"),
                                 handler->method, handler->path);
      default:
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                 _("Unexpected HTTP status %d '%s' on '%s' "
                                   "request to '%s'"),
                                 handler->sline.code, handler->sline.reason,
                                 handler->method, handler->path);
    }
}

svn_error_t *
svn_ra_serf__register_editor_shim_callbacks(svn_ra_session_t *ra_session,
                                    svn_delta_shim_callbacks_t *callbacks)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  session->shim_callbacks = callbacks;
  return SVN_NO_ERROR;
}

/* Shared/standard done_delegate handler */
static svn_error_t *
response_done(serf_request_t *request,
              void *handler_baton,
              apr_pool_t *scratch_pool)
{
  svn_ra_serf__handler_t *handler = handler_baton;

  assert(handler->done);

  if (handler->no_fail_on_http_failure_status)
    return SVN_NO_ERROR;

  if (handler->server_error)
    return svn_ra_serf__server_error_create(handler, scratch_pool);

  if (handler->sline.code >= 400 || handler->sline.code <= 199)
    {
      return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  if ((handler->sline.code >= 300 && handler->sline.code < 399)
      && !handler->no_fail_on_http_redirect_status)
    {
      return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  return SVN_NO_ERROR;
}

/* Pool cleanup handler for request handlers.

   If a serf context run stops for some outside error, like when the user
   cancels a request via ^C in the context loop, the handler is still
   registered in the serf context. With the pool cleanup there would be
   handlers registered in no freed memory.

   This fallback kills the connection for this case, which will make serf
   unregister any outstanding requests on it. */
static apr_status_t
handler_cleanup(void *baton)
{
  svn_ra_serf__handler_t *handler = baton;
  if (handler->scheduled)
    {
      svn_ra_serf__unschedule_handler(handler);
    }

  return APR_SUCCESS;
}

svn_ra_serf__handler_t *
svn_ra_serf__create_handler(svn_ra_serf__session_t *session,
                            apr_pool_t *result_pool)
{
  svn_ra_serf__handler_t *handler;

  handler = apr_pcalloc(result_pool, sizeof(*handler));
  handler->handler_pool = result_pool;

  apr_pool_cleanup_register(result_pool, handler, handler_cleanup,
                            apr_pool_cleanup_null);

  handler->session = session;
  handler->conn = session->conns[0];

  /* Setup the default done handler, to handle server errors */
  handler->done_delegate_baton = handler;
  handler->done_delegate = response_done;

  return handler;
}

svn_error_t *
svn_ra_serf__uri_parse(apr_uri_t *uri,
                       const char *url_str,
                       apr_pool_t *result_pool)
{
  apr_status_t status;

  status = apr_uri_parse(result_pool, url_str, uri);
  if (status)
    {
      /* Do not use returned error status in error message because currently
         apr_uri_parse() returns APR_EGENERAL for all parsing errors. */
      return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                               _("Illegal URL '%s'"),
                               url_str);
    }

  /* Depending the version of apr-util in use, for root paths uri.path
     will be NULL or "", where serf requires "/". */
  if (uri->path == NULL || uri->path[0] == '\0')
    {
      uri->path = apr_pstrdup(result_pool, "/");
    }

  return SVN_NO_ERROR;
}

void
svn_ra_serf__setup_svndiff_accept_encoding(serf_bucket_t *headers,
                                           svn_ra_serf__session_t *session)
{
  if (session->using_compression == svn_tristate_false)
    {
      /* Don't advertise support for compressed svndiff formats if
         compression is disabled. */
      serf_bucket_headers_setn(
        headers, "Accept-Encoding", "svndiff");
    }
  else if (session->using_compression == svn_tristate_unknown &&
           svn_ra_serf__is_low_latency_connection(session))
    {
      /* With http-compression=auto, advertise that we prefer svndiff2
         to svndiff1 with a low latency connection (assuming the underlying
         network has high bandwidth), as it is faster and in this case, we
         don't care about worse compression ratio. */
      serf_bucket_headers_setn(
        headers, "Accept-Encoding",
        "gzip,svndiff2;q=0.9,svndiff1;q=0.8,svndiff;q=0.7");
    }
  else
    {
      /* Otherwise, advertise that we prefer svndiff1 over svndiff2.
         svndiff2 is not a reasonable substitute for svndiff1 with default
         compression level, because, while it is faster, it also gives worse
         compression ratio.  While we can use svndiff2 in some cases (see
         above), we can't do this generally. */
      serf_bucket_headers_setn(
        headers, "Accept-Encoding",
        "gzip,svndiff1;q=0.9,svndiff2;q=0.8,svndiff;q=0.7");
    }
}

svn_boolean_t
svn_ra_serf__is_low_latency_connection(svn_ra_serf__session_t *session)
{
  return session->conn_latency >= 0 &&
         session->conn_latency < apr_time_from_msec(5);
}

apr_array_header_t *
svn_ra_serf__get_dirent_props(apr_uint32_t dirent_fields,
                              svn_ra_serf__session_t *session,
                              apr_pool_t *result_pool)
{
  svn_ra_serf__dav_props_t *prop;
  apr_array_header_t *props = apr_array_make
    (result_pool, 7, sizeof(svn_ra_serf__dav_props_t));

  if (session->supports_deadprop_count != svn_tristate_false
      || ! (dirent_fields & SVN_DIRENT_HAS_PROPS))
    {
      if (dirent_fields & SVN_DIRENT_KIND)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "resourcetype";
        }

      if (dirent_fields & SVN_DIRENT_SIZE)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "getcontentlength";
        }

      if (dirent_fields & SVN_DIRENT_HAS_PROPS)
        {
          prop = apr_array_push(props);
          prop->xmlns = SVN_DAV_PROP_NS_DAV;
          prop->name = "deadprop-count";
        }

      if (dirent_fields & SVN_DIRENT_CREATED_REV)
        {
          svn_ra_serf__dav_props_t *p = apr_array_push(props);
          p->xmlns = "DAV:";
          p->name = SVN_DAV__VERSION_NAME;
        }

      if (dirent_fields & SVN_DIRENT_TIME)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = SVN_DAV__CREATIONDATE;
        }

      if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "creator-displayname";
        }
    }
  else
    {
      /* We found an old subversion server that can't handle
         the deadprop-count property in the way we expect.

         The neon behavior is to retrieve all properties in this case */
      prop = apr_array_push(props);
      prop->xmlns = "DAV:";
      prop->name = "allprop";
    }

  return props;
}

