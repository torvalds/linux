/*
 * get_file.c :  entry point for update RA functions for ra_serf
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
#include <apr_version.h>
#include <apr_want.h>

#include <apr_uri.h>

#include <serf.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_props.h"

#include "private/svn_dep_compat.h"
#include "private/svn_string_private.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"




/*
 * This structure represents a single request to GET (fetch) a file with
 * its associated Serf session/connection.
 */
typedef struct stream_ctx_t {

  /* The handler representing this particular fetch.  */
  svn_ra_serf__handler_t *handler;

  /* Have we read our response headers yet? */
  svn_boolean_t read_headers;

  svn_ra_serf__session_t *session;

  /* This flag is set when our response is aborted before we reach the
   * end and we decide to requeue this request.
   */
  svn_boolean_t aborted_read;
  apr_off_t aborted_read_size;

  /* This is the amount of data that we have read so far. */
  apr_off_t read_size;

  /* If we're writing this file to a stream, this will be non-NULL. */
  svn_stream_t *result_stream;

} stream_ctx_t;



/** Routines called when we are fetching a file */

static svn_error_t *
headers_fetch(serf_bucket_t *headers,
              void *baton,
              apr_pool_t *pool /* request pool */,
              apr_pool_t *scratch_pool)
{
  stream_ctx_t *fetch_ctx = baton;

  if (fetch_ctx->session->using_compression != svn_tristate_false)
    {
      serf_bucket_headers_setn(headers, "Accept-Encoding", "gzip");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cancel_fetch(serf_request_t *request,
             serf_bucket_t *response,
             int status_code,
             void *baton)
{
  stream_ctx_t *fetch_ctx = baton;

  /* Uh-oh.  Our connection died on us.
   *
   * The core ra_serf layer will requeue our request - we just need to note
   * that we got cut off in the middle of our song.
   */
  if (!response)
    {
      /* If we already started the fetch and opened the file handle, we need
       * to hold subsequent read() ops until we get back to where we were
       * before the close and we can then resume the textdelta() calls.
       */
      if (fetch_ctx->read_headers)
        {
          if (!fetch_ctx->aborted_read && fetch_ctx->read_size)
            {
              fetch_ctx->aborted_read = TRUE;
              fetch_ctx->aborted_read_size = fetch_ctx->read_size;
            }
          fetch_ctx->read_size = 0;
        }

      return SVN_NO_ERROR;
    }

  /* We have no idea what went wrong. */
  SVN_ERR_MALFUNCTION();
}


/* Helper svn_ra_serf__get_file(). Attempts to fetch file contents
 * using SESSION->wc_callbacks->get_wc_contents() if sha1 property is
 * present in PROPS.
 *
 * Sets *FOUND_P to TRUE if file contents was successfuly fetched.
 *
 * Performs all temporary allocations in POOL.
 */
static svn_error_t *
try_get_wc_contents(svn_boolean_t *found_p,
                    svn_ra_serf__session_t *session,
                    const char *sha1_checksum_prop,
                    svn_stream_t *dst_stream,
                    apr_pool_t *pool)
{
  svn_checksum_t *checksum;
  svn_stream_t *wc_stream;
  svn_error_t *err;

  /* No contents found by default. */
  *found_p = FALSE;

  if (!session->wc_callbacks->get_wc_contents
      || sha1_checksum_prop == NULL)
    {
      /* Nothing to do. */
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1,
                                 sha1_checksum_prop, pool));

  err = session->wc_callbacks->get_wc_contents(
          session->wc_callback_baton, &wc_stream, checksum, pool);

  if (err)
    {
      svn_error_clear(err);

      /* Ignore errors for now. */
      return SVN_NO_ERROR;
    }

  if (wc_stream)
    {
        SVN_ERR(svn_stream_copy3(wc_stream,
                                 svn_stream_disown(dst_stream, pool),
                                 NULL, NULL, pool));
      *found_p = TRUE;
    }

  return SVN_NO_ERROR;
}

/* -----------------------------------------------------------------------
   svn_ra_get_file() specific */

/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
handle_stream(serf_request_t *request,
              serf_bucket_t *response,
              void *handler_baton,
              apr_pool_t *pool)
{
  stream_ctx_t *fetch_ctx = handler_baton;
  apr_status_t status;

  if (fetch_ctx->handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(fetch_ctx->handler));

  while (1)
    {
      const char *data;
      apr_size_t len;

      status = serf_bucket_read(response, 8000, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return svn_ra_serf__wrap_err(status, NULL);
        }

      fetch_ctx->read_size += len;

      if (fetch_ctx->aborted_read)
        {
          apr_off_t skip;

          /* We haven't caught up to where we were before. */
          if (fetch_ctx->read_size < fetch_ctx->aborted_read_size)
            {
              /* Eek.  What did the file shrink or something? */
              if (APR_STATUS_IS_EOF(status))
                {
                  SVN_ERR_MALFUNCTION();
                }

              /* Skip on to the next iteration of this loop. */
              if (APR_STATUS_IS_EAGAIN(status))
                {
                  return svn_ra_serf__wrap_err(status, NULL);
                }
              continue;
            }

          /* Woo-hoo.  We're back. */
          fetch_ctx->aborted_read = FALSE;

          /* Increment data and len by the difference. */
          skip = len - (fetch_ctx->read_size - fetch_ctx->aborted_read_size);
          data += skip;
          len -= (apr_size_t)skip;
        }

      if (len)
        {
          apr_size_t written_len;

          written_len = len;

          SVN_ERR(svn_stream_write(fetch_ctx->result_stream, data,
                                   &written_len));
        }

      if (status)
        {
          return svn_ra_serf__wrap_err(status, NULL);
        }
    }
  /* not reached */
}

/* Baton for get_file_prop_cb */
struct file_prop_baton_t
{
  apr_pool_t *result_pool;
  svn_node_kind_t kind;
  apr_hash_t *props;
  const char *sha1_checksum;
};

/* Implements svn_ra_serf__prop_func_t for svn_ra_serf__get_file */
static svn_error_t *
get_file_prop_cb(void *baton,
                 const char *path,
                 const char *ns,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *scratch_pool)
{
  struct file_prop_baton_t *fb = baton;
  const char *svn_name;

  if (strcmp(ns, "DAV:") == 0 && strcmp(name, "resourcetype") == 0)
    {
      const char *val = value->data;

      if (strcmp(val, "collection") == 0)
        fb->kind = svn_node_dir;
      else
        fb->kind = svn_node_file;

      return SVN_NO_ERROR;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_DAV) == 0
           && strcmp(name, "sha1-checksum") == 0)
    {
      fb->sha1_checksum = apr_pstrdup(fb->result_pool, value->data);
    }

  if (!fb->props)
    return SVN_NO_ERROR;

  svn_name = svn_ra_serf__svnname_from_wirename(ns, name, fb->result_pool);
  if (svn_name)
    {
      svn_hash_sets(fb->props, svn_name,
                    svn_string_dup(value, fb->result_pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_file(svn_ra_session_t *ra_session,
                      const char *path,
                      svn_revnum_t revision,
                      svn_stream_t *stream,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      apr_pool_t *result_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  const char *fetch_url;
  const svn_ra_serf__dav_props_t *which_props;
  svn_ra_serf__handler_t *propfind_handler;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  struct file_prop_baton_t fb;

  /* Fetch properties. */

  fetch_url = svn_path_url_add_component2(session->session_url.path, path,
                                          scratch_pool);

  /* The simple case is if we want HEAD - then a GET on the fetch_url is fine.
   *
   * Otherwise, we need to get the baseline version for this particular
   * revision and then fetch that file.
   */
  if (SVN_IS_VALID_REVNUM(revision) || fetched_rev)
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&fetch_url, fetched_rev,
                                          session,
                                          fetch_url, revision,
                                          scratch_pool, scratch_pool));
      revision = SVN_INVALID_REVNUM;
    }
  /* REVISION is always SVN_INVALID_REVNUM  */
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(revision));

  if (props)
      which_props = all_props;
  else if (stream && session->wc_callbacks->get_wc_contents)
      which_props = type_and_checksum_props;
  else
      which_props = check_path_props;

  fb.result_pool = result_pool;
  fb.props = props ? apr_hash_make(result_pool) : NULL;
  fb.kind = svn_node_unknown;
  fb.sha1_checksum = NULL;

  SVN_ERR(svn_ra_serf__create_propfind_handler(&propfind_handler, session,
                                               fetch_url, SVN_INVALID_REVNUM,
                                               "0", which_props,
                                               get_file_prop_cb, &fb,
                                               scratch_pool));

  SVN_ERR(svn_ra_serf__context_run_one(propfind_handler, scratch_pool));

  /* Verify that resource type is not collection. */
  if (fb.kind != svn_node_file)
    {
      return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL,
                              _("Can't get text contents of a directory"));
    }

  if (props)
    *props = fb.props;

  if (stream)
    {
      svn_boolean_t found;
      SVN_ERR(try_get_wc_contents(&found, session, fb.sha1_checksum, stream,
                                  scratch_pool));

      /* No contents found in the WC, let's fetch from server. */
      if (!found)
        {
          stream_ctx_t *stream_ctx;
          svn_ra_serf__handler_t *handler;

          /* Create the fetch context. */
          stream_ctx = apr_pcalloc(scratch_pool, sizeof(*stream_ctx));
          stream_ctx->result_stream = stream;
          stream_ctx->session = session;

          handler = svn_ra_serf__create_handler(session, scratch_pool);

          handler->method = "GET";
          handler->path = fetch_url;

          handler->custom_accept_encoding = TRUE;
          handler->no_dav_headers = TRUE;

          handler->header_delegate = headers_fetch;
          handler->header_delegate_baton = stream_ctx;

          handler->response_handler = handle_stream;
          handler->response_baton = stream_ctx;

          handler->response_error = cancel_fetch;
          handler->response_error_baton = stream_ctx;

          stream_ctx->handler = handler;

          SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

          if (handler->sline.code != 200)
            return svn_error_trace(svn_ra_serf__unexpected_status(handler));
        }
    }

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}
