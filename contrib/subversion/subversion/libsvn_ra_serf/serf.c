/*
 * serf.c :  entry point for ra_serf
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



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_uri.h>
#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_version.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/* Implements svn_ra__vtable_t.get_version(). */
static const svn_version_t *
ra_serf_version(void)
{
  SVN_VERSION_BODY;
}

#define RA_SERF_DESCRIPTION \
    N_("Module for accessing a repository via WebDAV protocol using serf.")

#define RA_SERF_DESCRIPTION_VER \
    N_("Module for accessing a repository via WebDAV protocol using serf.\n" \
       "  - using serf %d.%d.%d (compiled with %d.%d.%d)")

/* Implements svn_ra__vtable_t.get_description(). */
static const char *
ra_serf_get_description(apr_pool_t *pool)
{
  int major, minor, patch;

  serf_lib_version(&major, &minor, &patch);
  return apr_psprintf(pool, _(RA_SERF_DESCRIPTION_VER),
                      major, minor, patch,
                      SERF_MAJOR_VERSION,
                      SERF_MINOR_VERSION,
                      SERF_PATCH_VERSION
                      );
}

/* Implements svn_ra__vtable_t.get_schemes(). */
static const char * const *
ra_serf_get_schemes(apr_pool_t *pool)
{
  static const char *serf_ssl[] = { "http", "https", NULL };
#if 0
  /* ### Temporary: to shut up a warning. */
  static const char *serf_no_ssl[] = { "http", NULL };
#endif

  /* TODO: Runtime detection. */
  return serf_ssl;
}

/* Load the setting http-auth-types from the global or server specific
   section, parse its value and set the types of authentication we should
   accept from the server. */
static svn_error_t *
load_http_auth_types(apr_pool_t *pool, svn_config_t *config,
                     const char *server_group,
                     int *authn_types)
{
  const char *http_auth_types = NULL;
  *authn_types = SERF_AUTHN_NONE;

  svn_config_get(config, &http_auth_types, SVN_CONFIG_SECTION_GLOBAL,
               SVN_CONFIG_OPTION_HTTP_AUTH_TYPES, NULL);

  if (server_group)
    {
      svn_config_get(config, &http_auth_types, server_group,
                     SVN_CONFIG_OPTION_HTTP_AUTH_TYPES, http_auth_types);
    }

  if (http_auth_types)
    {
      char *token;
      char *auth_types_list = apr_palloc(pool, strlen(http_auth_types) + 1);
      apr_collapse_spaces(auth_types_list, http_auth_types);
      while ((token = svn_cstring_tokenize(";", &auth_types_list)) != NULL)
        {
          if (svn_cstring_casecmp("basic", token) == 0)
            *authn_types |= SERF_AUTHN_BASIC;
          else if (svn_cstring_casecmp("digest", token) == 0)
            *authn_types |= SERF_AUTHN_DIGEST;
          else if (svn_cstring_casecmp("ntlm", token) == 0)
            *authn_types |= SERF_AUTHN_NTLM;
          else if (svn_cstring_casecmp("negotiate", token) == 0)
            *authn_types |= SERF_AUTHN_NEGOTIATE;
          else
            return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                                     _("Invalid config: unknown %s "
                                       "'%s'"),
                                     SVN_CONFIG_OPTION_HTTP_AUTH_TYPES, token);
      }
    }
  else
    {
      /* Nothing specified by the user, so accept all types. */
      *authn_types = SERF_AUTHN_ALL;
    }

  return SVN_NO_ERROR;
}

/* Default HTTP timeout (in seconds); overridden by the 'http-timeout'
   runtime configuration variable. */
#define DEFAULT_HTTP_TIMEOUT 600

static svn_error_t *
load_config(svn_ra_serf__session_t *session,
            apr_hash_t *config_hash,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_config_t *config, *config_client;
  const char *server_group;
  const char *proxy_host = NULL;
  const char *port_str = NULL;
  const char *timeout_str = NULL;
  const char *exceptions;
  apr_port_t proxy_port;
  svn_tristate_t chunked_requests;
#if SERF_VERSION_AT_LEAST(1, 4, 0) && !defined(SVN_SERF_NO_LOGGING)
  apr_int64_t log_components;
  apr_int64_t log_level;
#endif

  if (config_hash)
    {
      config = svn_hash_gets(config_hash, SVN_CONFIG_CATEGORY_SERVERS);
      config_client = svn_hash_gets(config_hash, SVN_CONFIG_CATEGORY_CONFIG);
    }
  else
    {
      config = NULL;
      config_client = NULL;
    }

  SVN_ERR(svn_config_get_tristate(config, &session->using_compression,
                                  SVN_CONFIG_SECTION_GLOBAL,
                                  SVN_CONFIG_OPTION_HTTP_COMPRESSION,
                                  "auto", svn_tristate_unknown));
  svn_config_get(config, &timeout_str, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_TIMEOUT, NULL);

  if (session->auth_baton)
    {
      if (config_client)
        {
          svn_auth_set_parameter(session->auth_baton,
                                 SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG,
                                 config_client);
        }
      if (config)
        {
          svn_auth_set_parameter(session->auth_baton,
                                 SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS,
                                 config);
        }
    }

  /* Use the default proxy-specific settings if and only if
     "http-proxy-exceptions" is not set to exclude this host. */
  svn_config_get(config, &exceptions, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_HTTP_PROXY_EXCEPTIONS, "");
  if (! svn_cstring_match_glob_list(session->session_url.hostname,
                                    svn_cstring_split(exceptions, ",",
                                                      TRUE, scratch_pool)))
    {
      svn_config_get(config, &proxy_host, SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_PROXY_HOST, NULL);
      svn_config_get(config, &port_str, SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PORT, NULL);
      svn_config_get(config, &session->proxy_username,
                     SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME, NULL);
      svn_config_get(config, &session->proxy_password,
                     SVN_CONFIG_SECTION_GLOBAL,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD, NULL);
    }

  /* Load the global ssl settings, if set. */
  SVN_ERR(svn_config_get_bool(config, &session->trust_default_ca,
                              SVN_CONFIG_SECTION_GLOBAL,
                              SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA,
                              TRUE));
  svn_config_get(config, &session->ssl_authorities, SVN_CONFIG_SECTION_GLOBAL,
                 SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES, NULL);

  /* If set, read the flag that tells us to do bulk updates or not. Defaults
     to skelta updates. */
  SVN_ERR(svn_config_get_tristate(config, &session->bulk_updates,
                                  SVN_CONFIG_SECTION_GLOBAL,
                                  SVN_CONFIG_OPTION_HTTP_BULK_UPDATES,
                                  "auto",
                                  svn_tristate_unknown));

  /* Load the maximum number of parallel session connections. */
  SVN_ERR(svn_config_get_int64(config, &session->max_connections,
                               SVN_CONFIG_SECTION_GLOBAL,
                               SVN_CONFIG_OPTION_HTTP_MAX_CONNECTIONS,
                               SVN_CONFIG_DEFAULT_OPTION_HTTP_MAX_CONNECTIONS));

  /* Should we use chunked transfer encoding. */
  SVN_ERR(svn_config_get_tristate(config, &chunked_requests,
                                  SVN_CONFIG_SECTION_GLOBAL,
                                  SVN_CONFIG_OPTION_HTTP_CHUNKED_REQUESTS,
                                  "auto", svn_tristate_unknown));

#if SERF_VERSION_AT_LEAST(1, 4, 0) && !defined(SVN_SERF_NO_LOGGING)
  SVN_ERR(svn_config_get_int64(config, &log_components,
                               SVN_CONFIG_SECTION_GLOBAL,
                               SVN_CONFIG_OPTION_SERF_LOG_COMPONENTS,
                               SERF_LOGCOMP_NONE));
  SVN_ERR(svn_config_get_int64(config, &log_level,
                               SVN_CONFIG_SECTION_GLOBAL,
                               SVN_CONFIG_OPTION_SERF_LOG_LEVEL,
                               SERF_LOG_INFO));
#endif

  server_group = svn_auth_get_parameter(session->auth_baton,
                                        SVN_AUTH_PARAM_SERVER_GROUP);

  if (server_group)
    {
      SVN_ERR(svn_config_get_tristate(config, &session->using_compression,
                                      server_group,
                                      SVN_CONFIG_OPTION_HTTP_COMPRESSION,
                                      "auto", session->using_compression));
      svn_config_get(config, &timeout_str, server_group,
                     SVN_CONFIG_OPTION_HTTP_TIMEOUT, timeout_str);

      /* Load the group proxy server settings, overriding global
         settings.  We intentionally ignore 'http-proxy-exceptions'
         here because, well, if this site was an exception, why is
         there a per-server proxy configuration for it?  */
      svn_config_get(config, &proxy_host, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_HOST, proxy_host);
      svn_config_get(config, &port_str, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PORT, port_str);
      svn_config_get(config, &session->proxy_username, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME,
                     session->proxy_username);
      svn_config_get(config, &session->proxy_password, server_group,
                     SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD,
                     session->proxy_password);

      /* Load the group ssl settings. */
      SVN_ERR(svn_config_get_bool(config, &session->trust_default_ca,
                                  server_group,
                                  SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA,
                                  session->trust_default_ca));
      svn_config_get(config, &session->ssl_authorities, server_group,
                     SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES,
                     session->ssl_authorities);

      /* Load the group bulk updates flag. */
      SVN_ERR(svn_config_get_tristate(config, &session->bulk_updates,
                                      server_group,
                                      SVN_CONFIG_OPTION_HTTP_BULK_UPDATES,
                                      "auto",
                                      session->bulk_updates));

      /* Load the maximum number of parallel session connections,
         overriding global values. */
      SVN_ERR(svn_config_get_int64(config, &session->max_connections,
                                   server_group,
                                   SVN_CONFIG_OPTION_HTTP_MAX_CONNECTIONS,
                                   session->max_connections));

      /* Should we use chunked transfer encoding. */
      SVN_ERR(svn_config_get_tristate(config, &chunked_requests,
                                      server_group,
                                      SVN_CONFIG_OPTION_HTTP_CHUNKED_REQUESTS,
                                      "auto", chunked_requests));

#if SERF_VERSION_AT_LEAST(1, 4, 0) && !defined(SVN_SERF_NO_LOGGING)
      SVN_ERR(svn_config_get_int64(config, &log_components,
                                   server_group,
                                   SVN_CONFIG_OPTION_SERF_LOG_COMPONENTS,
                                   log_components));
       SVN_ERR(svn_config_get_int64(config, &log_level,
                                    server_group,
                                    SVN_CONFIG_OPTION_SERF_LOG_LEVEL,
                                    log_level));
#endif
    }

#if SERF_VERSION_AT_LEAST(1, 4, 0) && !defined(SVN_SERF_NO_LOGGING)
  if (log_components != SERF_LOGCOMP_NONE)
    {
      serf_log_output_t *output;
      apr_status_t status;

      status = serf_logging_create_stream_output(&output,
                                                 session->context,
                                                 (apr_uint32_t)log_level,
                                                 (apr_uint32_t)log_components,
                                                 SERF_LOG_DEFAULT_LAYOUT,
                                                 stderr,
                                                 result_pool);

      if (!status)
          serf_logging_add_output(session->context, output);
    }
#endif

  /* Don't allow the http-max-connections value to be larger than our
     compiled-in limit, or to be too small to operate.  Broken
     functionality and angry administrators are equally undesirable. */
  if (session->max_connections > SVN_RA_SERF__MAX_CONNECTIONS_LIMIT)
    session->max_connections = SVN_RA_SERF__MAX_CONNECTIONS_LIMIT;
  if (session->max_connections < 2)
    session->max_connections = 2;

  /* Parse the connection timeout value, if any. */
  session->timeout = apr_time_from_sec(DEFAULT_HTTP_TIMEOUT);
  if (timeout_str)
    {
      apr_int64_t timeout;
      svn_error_t *err;
      
      err = svn_cstring_strtoi64(&timeout, timeout_str, 0, APR_INT64_MAX, 10);
      if (err)
        return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, err,
                                _("invalid config: bad value for '%s' option"),
                                SVN_CONFIG_OPTION_HTTP_TIMEOUT);
      session->timeout = apr_time_from_sec(timeout);
    }

  /* Convert the proxy port value, if any. */
  if (port_str)
    {
      char *endstr;
      const long int port = strtol(port_str, &endstr, 10);

      if (*endstr)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: illegal character in proxy "
                                  "port number"));
      if (port < 0)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: negative proxy port number"));
      if (port > 65535)
        return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                _("Invalid URL: proxy port number greater "
                                  "than maximum TCP port number 65535"));
      proxy_port = (apr_port_t) port;
    }
  else
    {
      proxy_port = 80;
    }

  if (proxy_host)
    {
      apr_sockaddr_t *proxy_addr;
      apr_status_t status;

      status = apr_sockaddr_info_get(&proxy_addr, proxy_host,
                                     APR_UNSPEC, proxy_port, 0,
                                     session->pool);
      if (status)
        {
          return svn_ra_serf__wrap_err(
                   status, _("Could not resolve proxy server '%s'"),
                   proxy_host);
        }
      session->using_proxy = TRUE;
      serf_config_proxy(session->context, proxy_addr);
    }
  else
    {
      session->using_proxy = FALSE;
    }

  /* Setup detect_chunking and using_chunked_requests based on
   * the chunked_requests tristate */
  if (chunked_requests == svn_tristate_unknown)
    {
      session->detect_chunking = TRUE;
      session->using_chunked_requests = TRUE;
    }
  else if (chunked_requests == svn_tristate_true)
    {
      session->detect_chunking = FALSE;
      session->using_chunked_requests = TRUE;
    }
  else /* chunked_requests == svn_tristate_false */
    {
      session->detect_chunking = FALSE;
      session->using_chunked_requests = FALSE;
    }

  /* Setup authentication. */
  SVN_ERR(load_http_auth_types(result_pool, config, server_group,
                               &session->authn_types));
  serf_config_authn_types(session->context, session->authn_types);
  serf_config_credentials_callback(session->context,
                                   svn_ra_serf__credentials_callback);

  return SVN_NO_ERROR;
}
#undef DEFAULT_HTTP_TIMEOUT

static void
svn_ra_serf__progress(void *progress_baton, apr_off_t bytes_read,
                      apr_off_t bytes_written)
{
  const svn_ra_serf__session_t *serf_sess = progress_baton;
  if (serf_sess->progress_func)
    {
      serf_sess->progress_func(bytes_read + bytes_written, -1,
                               serf_sess->progress_baton,
                               serf_sess->pool);
    }
}

/** Our User-Agent string. */
static const char *
get_user_agent_string(apr_pool_t *pool)
{
  int major, minor, patch;
  serf_lib_version(&major, &minor, &patch);

  return apr_psprintf(pool, "SVN/%s (%s) serf/%d.%d.%d",
                      SVN_VER_NUMBER, SVN_BUILD_TARGET,
                      major, minor, patch);
}

/* Implements svn_ra__vtable_t.open_session(). */
static svn_error_t *
svn_ra_serf__open(svn_ra_session_t *session,
                  const char **corrected_url,
                  const char *session_URL,
                  const svn_ra_callbacks2_t *callbacks,
                  void *callback_baton,
                  svn_auth_baton_t *auth_baton,
                  apr_hash_t *config,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_status_t status;
  svn_ra_serf__session_t *serf_sess;
  apr_uri_t url;
  const char *client_string = NULL;
  svn_error_t *err;

  if (corrected_url)
    *corrected_url = NULL;

  serf_sess = apr_pcalloc(result_pool, sizeof(*serf_sess));
  serf_sess->pool = result_pool;
  if (config)
    SVN_ERR(svn_config_copy_config(&serf_sess->config, config, result_pool));
  else
    serf_sess->config = NULL;
  serf_sess->wc_callbacks = callbacks;
  serf_sess->wc_callback_baton = callback_baton;
  serf_sess->auth_baton = auth_baton;
  serf_sess->progress_func = callbacks->progress_func;
  serf_sess->progress_baton = callbacks->progress_baton;
  serf_sess->cancel_func = callbacks->cancel_func;
  serf_sess->cancel_baton = callback_baton;

  /* todo: reuse serf context across sessions */
  serf_sess->context = serf_context_create(serf_sess->pool);

  SVN_ERR(svn_ra_serf__blncache_create(&serf_sess->blncache,
                                       serf_sess->pool));


  SVN_ERR(svn_ra_serf__uri_parse(&url, session_URL, serf_sess->pool));

  if (!url.port)
    {
      url.port = apr_uri_port_of_scheme(url.scheme);
    }
  serf_sess->session_url = url;
  serf_sess->session_url_str = apr_pstrdup(serf_sess->pool, session_URL);
  serf_sess->using_ssl = (svn_cstring_casecmp(url.scheme, "https") == 0);

  serf_sess->supports_deadprop_count = svn_tristate_unknown;

  serf_sess->capabilities = apr_hash_make(serf_sess->pool);

  /* We have to assume that the server only supports HTTP/1.0. Once it's clear
     HTTP/1.1 is supported, we can upgrade. */
  serf_sess->http10 = TRUE;
  serf_sess->http20 = FALSE;

  /* If we switch to HTTP/1.1, then we will use chunked requests. We may disable
     this, if we find an intervening proxy does not support chunked requests.  */
  serf_sess->using_chunked_requests = TRUE;

  SVN_ERR(load_config(serf_sess, config, serf_sess->pool, scratch_pool));

  serf_sess->conns[0] = apr_pcalloc(serf_sess->pool,
                                    sizeof(*serf_sess->conns[0]));
  serf_sess->conns[0]->bkt_alloc =
          serf_bucket_allocator_create(serf_sess->pool, NULL, NULL);
  serf_sess->conns[0]->session = serf_sess;
  serf_sess->conns[0]->last_status_code = -1;

  /* create the user agent string */
  if (callbacks->get_client_string)
    SVN_ERR(callbacks->get_client_string(callback_baton, &client_string,
                                         scratch_pool));

  if (client_string)
    serf_sess->useragent = apr_pstrcat(result_pool,
                                       get_user_agent_string(scratch_pool),
                                       " ",
                                       client_string, SVN_VA_NULL);
  else
    serf_sess->useragent = get_user_agent_string(result_pool);

  /* go ahead and tell serf about the connection. */
  status =
    serf_connection_create2(&serf_sess->conns[0]->conn,
                            serf_sess->context,
                            url,
                            svn_ra_serf__conn_setup, serf_sess->conns[0],
                            svn_ra_serf__conn_closed, serf_sess->conns[0],
                            serf_sess->pool);
  if (status)
    return svn_ra_serf__wrap_err(status, NULL);

  /* Set the progress callback. */
  serf_context_set_progress_cb(serf_sess->context, svn_ra_serf__progress,
                               serf_sess);

  serf_sess->num_conns = 1;

  session->priv = serf_sess;

  /* The following code explicitly works around a bug in serf <= r2319 / 1.3.8
     where serf doesn't report the request as failed/cancelled when the
     authorization request handler fails to handle the request.

     As long as we allocate the request in a subpool of the serf connection
     pool, we know that the handler is always cleaned before the connection.

     Luckily our caller now passes us two pools which handle this case.
   */
#if defined(SVN_DEBUG) && !SERF_VERSION_AT_LEAST(1,4,0)
  /* Currently ensured by svn_ra_open4().
     If failing causes segfault in basic_tests.py 48, "basic auth test" */
  SVN_ERR_ASSERT((serf_sess->pool != scratch_pool)
                 && apr_pool_is_ancestor(serf_sess->pool, scratch_pool));
#endif

  /* The actual latency will be determined as a part of the initial
     OPTIONS request. */
  serf_sess->conn_latency = -1;

  err = svn_ra_serf__exchange_capabilities(serf_sess, corrected_url,
                                           result_pool, scratch_pool);

  /* serf should produce a usable error code instead of APR_EGENERAL */
  if (err && err->apr_err == APR_EGENERAL)
    err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, err,
                            _("Connection to '%s' failed"), session_URL);
  SVN_ERR(err);

  /* We have set up a useful connection (that doesn't indication a redirect).
     If we've been told there is possibly a worrisome proxy in our path to the
     server AND we switched to HTTP/1.1 (chunked requests), then probe for
     problems in any proxy.  */
  if ((corrected_url == NULL || *corrected_url == NULL)
      && serf_sess->detect_chunking && !serf_sess->http10)
    SVN_ERR(svn_ra_serf__probe_proxy(serf_sess, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.dup_session */
static svn_error_t *
ra_serf_dup_session(svn_ra_session_t *new_session,
                    svn_ra_session_t *old_session,
                    const char *new_session_url,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *old_sess = old_session->priv;
  svn_ra_serf__session_t *new_sess;
  apr_status_t status;

  new_sess = apr_pmemdup(result_pool, old_sess, sizeof(*new_sess));

  new_sess->pool = result_pool;

  if (new_sess->config)
    SVN_ERR(svn_config_copy_config(&new_sess->config, new_sess->config,
                                   result_pool));

  /* max_connections */
  /* using_ssl */
  /* using_compression */
  /* http10 */
  /* http20 */
  /* using_chunked_requests */
  /* detect_chunking */

  if (new_sess->useragent)
    new_sess->useragent = apr_pstrdup(result_pool, new_sess->useragent);

  if (new_sess->vcc_url)
    new_sess->vcc_url = apr_pstrdup(result_pool, new_sess->vcc_url);

  new_sess->auth_state = NULL;
  new_sess->auth_attempts = 0;

  /* Callback functions to get info from WC */
  /* wc_callbacks */
  /* wc_callback_baton */

  /* progress_func */
  /* progress_baton */

  /* cancel_func */
  /* cancel_baton */

  /* shim_callbacks */

  new_sess->pending_error = NULL;

  /* authn_types */

  /* Keys and values are static */
  if (new_sess->capabilities)
    new_sess->capabilities = apr_hash_copy(result_pool, new_sess->capabilities);

  if (new_sess->activity_collection_url)
    {
      new_sess->activity_collection_url
                = apr_pstrdup(result_pool, new_sess->activity_collection_url);
    }

   /* using_proxy */

  if (new_sess->proxy_username)
    {
      new_sess->proxy_username
                = apr_pstrdup(result_pool, new_sess->proxy_username);
    }

  if (new_sess->proxy_password)
    {
      new_sess->proxy_password
                = apr_pstrdup(result_pool, new_sess->proxy_password);
    }

  new_sess->proxy_auth_attempts = 0;

  /* trust_default_ca */

  if (new_sess->ssl_authorities)
    {
      new_sess->ssl_authorities = apr_pstrdup(result_pool,
                                              new_sess->ssl_authorities);
    }

  if (new_sess->uuid)
    new_sess->uuid = apr_pstrdup(result_pool, new_sess->uuid);

  /* timeout */
  /* supports_deadprop_count */

  if (new_sess->me_resource)
    new_sess->me_resource = apr_pstrdup(result_pool, new_sess->me_resource);
  if (new_sess->rev_stub)
    new_sess->rev_stub = apr_pstrdup(result_pool, new_sess->rev_stub);
  if (new_sess->txn_stub)
    new_sess->txn_stub = apr_pstrdup(result_pool, new_sess->txn_stub);
  if (new_sess->txn_root_stub)
    new_sess->txn_root_stub = apr_pstrdup(result_pool,
                                          new_sess->txn_root_stub);
  if (new_sess->vtxn_stub)
    new_sess->vtxn_stub = apr_pstrdup(result_pool, new_sess->vtxn_stub);
  if (new_sess->vtxn_root_stub)
    new_sess->vtxn_root_stub = apr_pstrdup(result_pool,
                                           new_sess->vtxn_root_stub);

  /* Keys and values are static */
  if (new_sess->supported_posts)
    new_sess->supported_posts = apr_hash_copy(result_pool,
                                              new_sess->supported_posts);

  /* ### Can we copy this? */
  SVN_ERR(svn_ra_serf__blncache_create(&new_sess->blncache,
                                       new_sess->pool));

  if (new_sess->server_allows_bulk)
    new_sess->server_allows_bulk = apr_pstrdup(result_pool,
                                               new_sess->server_allows_bulk);

  if (new_sess->repos_root_str)
    {
      new_sess->repos_root_str = apr_pstrdup(result_pool,
                                             new_sess->repos_root_str);
      SVN_ERR(svn_ra_serf__uri_parse(&new_sess->repos_root,
                                     new_sess->repos_root_str,
                                     result_pool));
    }

  new_sess->session_url_str = apr_pstrdup(result_pool, new_session_url);

  SVN_ERR(svn_ra_serf__uri_parse(&new_sess->session_url,
                                 new_sess->session_url_str,
                                 result_pool));

  /* svn_boolean_t supports_inline_props */
  /* supports_rev_rsrc_replay */
  /* supports_svndiff1 */
  /* supports_svndiff2 */
  /* supports_put_result_checksum */
  /* conn_latency */

  new_sess->context = serf_context_create(result_pool);

  SVN_ERR(load_config(new_sess, old_sess->config,
                      result_pool, scratch_pool));

  new_sess->conns[0] = apr_pcalloc(result_pool,
                                   sizeof(*new_sess->conns[0]));
  new_sess->conns[0]->bkt_alloc =
          serf_bucket_allocator_create(result_pool, NULL, NULL);
  new_sess->conns[0]->session = new_sess;
  new_sess->conns[0]->last_status_code = -1;

  /* go ahead and tell serf about the connection. */
  status =
    serf_connection_create2(&new_sess->conns[0]->conn,
                            new_sess->context,
                            new_sess->session_url,
                            svn_ra_serf__conn_setup, new_sess->conns[0],
                            svn_ra_serf__conn_closed, new_sess->conns[0],
                            result_pool);
  if (status)
    return svn_ra_serf__wrap_err(status, NULL);

  /* Set the progress callback. */
  serf_context_set_progress_cb(new_sess->context, svn_ra_serf__progress,
                               new_sess);

  new_sess->num_conns = 1;
  new_sess->cur_conn = 0;

  new_session->priv = new_sess;

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.reparent(). */
svn_error_t *
svn_ra_serf__reparent(svn_ra_session_t *ra_session,
                      const char *url,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_uri_t new_url;

  /* If it's the URL we already have, wave our hands and do nothing. */
  if (strcmp(session->session_url_str, url) == 0)
    {
      return SVN_NO_ERROR;
    }

  if (!session->repos_root_str)
    {
      const char *vcc_url;
      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, pool));
    }

  if (!svn_uri__is_ancestor(session->repos_root_str, url))
    {
      return svn_error_createf(
          SVN_ERR_RA_ILLEGAL_URL, NULL,
          _("URL '%s' is not a child of the session's repository root "
            "URL '%s'"), url, session->repos_root_str);
    }

  SVN_ERR(svn_ra_serf__uri_parse(&new_url, url, pool));

  /* ### Maybe we should use a string buffer for these strings so we
     ### don't allocate memory in the session on every reparent? */
  session->session_url.path = apr_pstrdup(session->pool, new_url.path);
  session->session_url_str = apr_pstrdup(session->pool, url);

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.get_session_url(). */
static svn_error_t *
svn_ra_serf__get_session_url(svn_ra_session_t *ra_session,
                             const char **url,
                             apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  *url = apr_pstrdup(pool, session->session_url_str);
  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.get_latest_revnum(). */
static svn_error_t *
svn_ra_serf__get_latest_revnum(svn_ra_session_t *ra_session,
                               svn_revnum_t *latest_revnum,
                               apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return svn_error_trace(svn_ra_serf__get_youngest_revnum(
                           latest_revnum, session, pool));
}

/* Implementation of svn_ra_serf__rev_proplist(). */
static svn_error_t *
serf__rev_proplist(svn_ra_session_t *ra_session,
                   svn_revnum_t rev,
                   const svn_ra_serf__dav_props_t *fetch_props,
                   apr_hash_t **ret_props,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *propfind_path;
  svn_ra_serf__handler_t *handler;

  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    {
      propfind_path = apr_psprintf(scratch_pool, "%s/%ld", session->rev_stub,
                                   rev);

      /* svn_ra_serf__retrieve_props() wants to added the revision as
         a Label to the PROPFIND, which isn't really necessary when
         querying a rev-stub URI.  *Shrug*  Probably okay to leave the
         Label, but whatever. */
      rev = SVN_INVALID_REVNUM;
    }
  else
    {
      /* Use the VCC as the propfind target path. */
      SVN_ERR(svn_ra_serf__discover_vcc(&propfind_path, session,
                                        scratch_pool));
    }

  props = apr_hash_make(result_pool);
  SVN_ERR(svn_ra_serf__create_propfind_handler(&handler, session,
                                               propfind_path, rev, "0",
                                               fetch_props,
                                               svn_ra_serf__deliver_svn_props,
                                               props,
                                               scratch_pool));

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  svn_ra_serf__keep_only_regular_props(props, scratch_pool);

  *ret_props = props;

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.rev_proplist(). */
static svn_error_t *
svn_ra_serf__rev_proplist(svn_ra_session_t *ra_session,
                          svn_revnum_t rev,
                          apr_hash_t **ret_props,
                          apr_pool_t *result_pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  svn_error_t *err;

  err = serf__rev_proplist(ra_session, rev, all_props, ret_props,
                           result_pool, scratch_pool);

  svn_pool_destroy(scratch_pool);
  return svn_error_trace(err);
}


/* Implements svn_ra__vtable_t.rev_prop(). */
svn_error_t *
svn_ra_serf__rev_prop(svn_ra_session_t *session,
                      svn_revnum_t rev,
                      const char *name,
                      svn_string_t **value,
                      apr_pool_t *result_pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  apr_hash_t *props;
  svn_ra_serf__dav_props_t specific_props[2];
  const svn_ra_serf__dav_props_t *fetch_props = all_props;

  /* The DAV propfind doesn't allow property fetches for any property name
     as there is no defined way to quote values. If we are just fetching a
     "svn:property" we can safely do this. In other cases we just fetch all
     revision properties and filter the right one out */
  if (strncmp(name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX)-1) == 0
      && !strchr(name + sizeof(SVN_PROP_PREFIX)-1, ':'))
    {
      specific_props[0].xmlns = SVN_DAV_PROP_NS_SVN;
      specific_props[0].name = name + sizeof(SVN_PROP_PREFIX)-1;
      specific_props[1].xmlns = NULL;
      specific_props[1].name = NULL;

      fetch_props = specific_props;
    }

  SVN_ERR(serf__rev_proplist(session, rev, fetch_props, &props,
                             result_pool, scratch_pool));

  *value = svn_hash_gets(props, name);

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_repos_root(svn_ra_session_t *ra_session,
                            const char **url,
                            apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  if (!session->repos_root_str)
    {
      const char *vcc_url;
      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, pool));
    }

  *url = session->repos_root_str;
  return SVN_NO_ERROR;
}

/* TODO: to fetch the uuid from the repository, we need:
   1. a path that exists in HEAD
   2. a path that's readable

   get_uuid handles the case where a path doesn't exist in HEAD and also the
   case where the root of the repository is not readable.
   However, it does not handle the case where we're fetching path not existing
   in HEAD of a repository with unreadable root directory.

   Implements svn_ra__vtable_t.get_uuid().
 */
static svn_error_t *
svn_ra_serf__get_uuid(svn_ra_session_t *ra_session,
                      const char **uuid,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  if (!session->uuid)
    {
      const char *vcc_url;

      /* We should never get here if we have HTTP v2 support, because
         any server with that support should be transmitting the
         UUID in the initial OPTIONS response.  */
      SVN_ERR_ASSERT(! SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session));

      /* We're not interested in vcc_url and relative_url, but this call also
         stores the repository's uuid in the session. */
      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, pool));
      if (!session->uuid)
        {
          return svn_error_create(SVN_ERR_RA_DAV_RESPONSE_HEADER_BADNESS, NULL,
                                  _("The UUID property was not found on the "
                                    "resource or any of its parents"));
        }
    }

  *uuid = session->uuid;

  return SVN_NO_ERROR;
}


static const svn_ra__vtable_t serf_vtable = {
  ra_serf_version,
  ra_serf_get_description,
  ra_serf_get_schemes,
  svn_ra_serf__open,
  ra_serf_dup_session,
  svn_ra_serf__reparent,
  svn_ra_serf__get_session_url,
  svn_ra_serf__get_latest_revnum,
  svn_ra_serf__get_dated_revision,
  svn_ra_serf__change_rev_prop,
  svn_ra_serf__rev_proplist,
  svn_ra_serf__rev_prop,
  svn_ra_serf__get_commit_editor,
  svn_ra_serf__get_file,
  svn_ra_serf__get_dir,
  svn_ra_serf__get_mergeinfo,
  svn_ra_serf__do_update,
  svn_ra_serf__do_switch,
  svn_ra_serf__do_status,
  svn_ra_serf__do_diff,
  svn_ra_serf__get_log,
  svn_ra_serf__check_path,
  svn_ra_serf__stat,
  svn_ra_serf__get_uuid,
  svn_ra_serf__get_repos_root,
  svn_ra_serf__get_locations,
  svn_ra_serf__get_location_segments,
  svn_ra_serf__get_file_revs,
  svn_ra_serf__lock,
  svn_ra_serf__unlock,
  svn_ra_serf__get_lock,
  svn_ra_serf__get_locks,
  svn_ra_serf__replay,
  svn_ra_serf__has_capability,
  svn_ra_serf__replay_range,
  svn_ra_serf__get_deleted_rev,
  svn_ra_serf__get_inherited_props,
  NULL /* set_svn_ra_open */,
  svn_ra_serf__list,
  svn_ra_serf__register_editor_shim_callbacks,
  NULL /* commit_ev2 */,
  NULL /* replay_range_ev2 */
};

svn_error_t *
svn_ra_serf__init(const svn_version_t *loader_version,
                  const svn_ra__vtable_t **vtable,
                  apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };
  int serf_major;
  int serf_minor;
  int serf_patch;

  SVN_ERR(svn_ver_check_list2(ra_serf_version(), checklist, svn_ver_equal));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf(
         SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_serf"),
         loader_version->major);
    }

  /* Make sure that we have loaded a compatible library: the MAJOR must
     match, and the minor must be at *least* what we compiled against.
     The patch level is simply ignored.  */
  serf_lib_version(&serf_major, &serf_minor, &serf_patch);
  if (serf_major != SERF_MAJOR_VERSION
      || serf_minor < SERF_MINOR_VERSION)
    {
      return svn_error_createf(
         /* ### should return a unique error  */
         SVN_ERR_VERSION_MISMATCH, NULL,
         _("ra_serf was compiled for serf %d.%d.%d but loaded "
           "an incompatible %d.%d.%d library"),
         SERF_MAJOR_VERSION, SERF_MINOR_VERSION, SERF_PATCH_VERSION,
         serf_major, serf_minor, serf_patch);
    }

  *vtable = &serf_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for pre-1.2 subversions.  Needed? */
#define NAME "ra_serf"
#define DESCRIPTION RA_SERF_DESCRIPTION
#define VTBL serf_vtable
#define INITFUNC svn_ra_serf__init
#define COMPAT_INITFUNC svn_ra_serf_init
#include "../libsvn_ra/wrapper_template.h"
