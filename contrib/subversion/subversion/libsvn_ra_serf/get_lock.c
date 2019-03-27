/*
 * get_lock.c :  obtain single lock information functions for ra_serf
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



#include <apr_uri.h>
#include <serf.h>

#include "svn_dav.h"
#include "svn_pools.h"
#include "svn_ra.h"

#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum {
  INITIAL = 0,
  MULTISTATUS,
  RESPONSE,
  PROPSTAT,
  PROP,
  LOCK_DISCOVERY,
  ACTIVE_LOCK,
  LOCK_TYPE,
  LOCK_SCOPE,
  DEPTH,
  TIMEOUT,
  LOCK_TOKEN,
  OWNER,
  HREF
};

typedef struct lock_info_t {
  apr_pool_t *pool;

  const char *path;

  svn_lock_t *lock;

  svn_boolean_t read_headers;

  svn_ra_serf__handler_t *handler;

  /* The expat handler. We wrap this to do a bit more work.  */
  svn_ra_serf__response_handler_t inner_handler;
  void *inner_baton;

} lock_info_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t locks_ttable[] = {
  /* The INITIAL state can transition into D:prop (LOCK) or
     to D:multistatus (PROPFIND)  */
  { INITIAL, D_, "multistatus", MULTISTATUS,
    FALSE, { NULL }, FALSE },

  { MULTISTATUS, D_, "response", RESPONSE,
    FALSE, { NULL }, FALSE },

  { RESPONSE, D_, "propstat", PROPSTAT,
    FALSE, { NULL }, FALSE },

  { PROPSTAT, D_, "prop", PROP,
    FALSE, { NULL }, FALSE },

  { PROP, D_, "lockdiscovery", LOCK_DISCOVERY,
    FALSE, { NULL }, FALSE },

  { LOCK_DISCOVERY, D_, "activelock", ACTIVE_LOCK,
    FALSE, { NULL }, FALSE },

#if 0
  /* ### we don't really need to parse locktype/lockscope. we know what
     ### the values are going to be. we *could* validate that the only
     ### possible children are D:write and D:exclusive. we'd need to
     ### modify the state transition to tell us about all children
     ### (ie. maybe support "*" for the name) and then validate. but it
     ### just isn't important to validate, so disable this for now... */

  { ACTIVE_LOCK, D_, "locktype", LOCK_TYPE,
    FALSE, { NULL }, FALSE },

  { LOCK_TYPE, D_, "write", WRITE,
    FALSE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "lockscope", LOCK_SCOPE,
    FALSE, { NULL }, FALSE },

  { LOCK_SCOPE, D_, "exclusive", EXCLUSIVE,
    FALSE, { NULL }, TRUE },
#endif /* 0  */

  { ACTIVE_LOCK, D_, "timeout", TIMEOUT,
    TRUE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "locktoken", LOCK_TOKEN,
    FALSE, { NULL }, FALSE },

  { LOCK_TOKEN, D_, "href", HREF,
    TRUE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "owner", OWNER,
    TRUE, { NULL }, TRUE },

  /* ACTIVE_LOCK has a D:depth child, but we can ignore that.  */

  { 0 }
};

static const int locks_expected_status[] = {
  207,
  0
};

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
locks_closed(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int leaving_state,
             const svn_string_t *cdata,
             apr_hash_t *attrs,
             apr_pool_t *scratch_pool)
{
  lock_info_t *lock_ctx = baton;

  if (leaving_state == TIMEOUT)
    {
      if (strcasecmp(cdata->data, "Infinite") == 0)
        lock_ctx->lock->expiration_date = 0;
      else if (strncasecmp(cdata->data, "Second-", 7) == 0)
        {
          unsigned n;
          SVN_ERR(svn_cstring_atoui(&n, cdata->data+7));

          lock_ctx->lock->expiration_date = apr_time_now() +
                                            apr_time_from_sec(n);
        }
      else
         return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                  _("Invalid LOCK timeout value '%s'"),
                                  cdata->data);
    }
  else if (leaving_state == HREF)
    {
      if (cdata->len)
        {
          char *buf = apr_pstrmemdup(lock_ctx->pool, cdata->data, cdata->len);

          apr_collapse_spaces(buf, buf);
          lock_ctx->lock->token = buf;
        }
    }
  else if (leaving_state == OWNER)
    {
      if (cdata->len)
        {
          lock_ctx->lock->comment = apr_pstrmemdup(lock_ctx->pool,
                                                   cdata->data, cdata->len);
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
handle_lock(serf_request_t *request,
            serf_bucket_t *response,
            void *handler_baton,
            apr_pool_t *pool)
{
  lock_info_t *ctx = handler_baton;

  if (!ctx->read_headers)
    {
      serf_bucket_t *headers;
      const char *val;

      headers = serf_bucket_response_get_headers(response);

      val = serf_bucket_headers_get(headers, SVN_DAV_LOCK_OWNER_HEADER);
      if (val)
        {
          ctx->lock->owner = apr_pstrdup(ctx->pool, val);
        }

      val = serf_bucket_headers_get(headers, SVN_DAV_CREATIONDATE_HEADER);
      if (val)
        {
          SVN_ERR(svn_time_from_cstring(&ctx->lock->creation_date, val,
                                        ctx->pool));
        }

      ctx->read_headers = TRUE;
    }

  return ctx->inner_handler(request, response, ctx->inner_baton, pool);
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_getlock_body(serf_bucket_t **body_bkt,
                    void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool /* request pool */,
                    apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(buckets, alloc);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "propfind",
                                    "xmlns", "DAV:",
                                    SVN_VA_NULL);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "prop", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                     "lockdiscovery", SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "prop");
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "propfind");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

static svn_error_t*
setup_getlock_headers(serf_bucket_t *headers,
                      void *baton,
                      apr_pool_t *pool /* request pool */,
                      apr_pool_t *scratch_pool)
{
  serf_bucket_headers_setn(headers, "Depth", "0");

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_lock(svn_ra_session_t *ra_session,
                      svn_lock_t **lock,
                      const char *path,
                      apr_pool_t *result_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  lock_info_t *lock_ctx;
  const char *req_url;
  svn_error_t *err;

  req_url = svn_path_url_add_component2(session->session_url.path, path,
                                        scratch_pool);

  lock_ctx = apr_pcalloc(scratch_pool, sizeof(*lock_ctx));
  lock_ctx->pool = result_pool;
  lock_ctx->path = req_url;
  lock_ctx->lock = svn_lock_create(result_pool);
  lock_ctx->lock->path = apr_pstrdup(result_pool, path);

  xmlctx = svn_ra_serf__xml_context_create(locks_ttable,
                                           NULL, locks_closed, NULL,
                                           lock_ctx,
                                           scratch_pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx,
                                              locks_expected_status,
                                              scratch_pool);

  handler->method = "PROPFIND";
  handler->path = req_url;
  handler->body_type = "text/xml";

  handler->body_delegate = create_getlock_body;
  handler->body_delegate_baton = lock_ctx;

  handler->header_delegate = setup_getlock_headers;
  handler->header_delegate_baton = lock_ctx;

  handler->no_dav_headers = TRUE;

  lock_ctx->inner_handler = handler->response_handler;
  lock_ctx->inner_baton = handler->response_baton;
  handler->response_handler = handle_lock;
  handler->response_baton = lock_ctx;

  lock_ctx->handler = handler;

  err = svn_ra_serf__context_run_one(handler, scratch_pool);

  if ((err && (handler->sline.code == 500 || handler->sline.code == 501))
      || svn_error_find_cause(err, SVN_ERR_UNSUPPORTED_FEATURE))
    return svn_error_trace(
             svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                              _("Server does not support locking features")));
  else if (svn_error_find_cause(err, SVN_ERR_FS_NOT_FOUND))
    svn_error_clear(err); /* Behave like the other RA layers */
  else if (handler->sline.code != 207)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  if (lock_ctx->lock && lock_ctx->lock->token)
    *lock = lock_ctx->lock;
  else
    *lock = NULL;

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}
