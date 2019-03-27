/*
 * lock.c :  entry point for locking RA functions for ra_serf
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
#include <assert.h>

#include "svn_dav.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_ra.h"

#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_private_config.h"
#include "private/svn_sorts_private.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum {
  INITIAL = 0,
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


typedef struct lock_ctx_t {
  apr_pool_t *pool;

  const char *path;

  const char *token; /* For unlock */
  svn_lock_t *lock; /* For lock */

  svn_boolean_t force;
  svn_revnum_t revision;

  svn_boolean_t read_headers;

  svn_ra_serf__handler_t *handler;

  /* The expat handler. We wrap this to do a bit more work.  */
  svn_ra_serf__response_handler_t inner_handler;
  void *inner_baton;

} lock_ctx_t;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t locks_ttable[] = {
  /* The INITIAL state can transition into D:prop (LOCK) or
     to D:multistatus (PROPFIND)  */
  { INITIAL, D_, "prop", PROP,
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

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
locks_closed(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int leaving_state,
             const svn_string_t *cdata,
             apr_hash_t *attrs,
             apr_pool_t *scratch_pool)
{
  lock_ctx_t *lock_ctx = baton;

  if (leaving_state == TIMEOUT)
    {
      /* This function just parses the result of our own lock request,
         so on a normal server we will only encounter 'Infinite' here. */
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


static svn_error_t *
set_lock_headers(serf_bucket_t *headers,
                 void *baton,
                 apr_pool_t *pool /* request pool */,
                 apr_pool_t *scratch_pool)
{
  lock_ctx_t *lock_ctx = baton;

  if (lock_ctx->force)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_STEAL);
    }

  if (SVN_IS_VALID_REVNUM(lock_ctx->revision))
    {
      serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                              apr_ltoa(pool, lock_ctx->revision));
    }

  return APR_SUCCESS;
}

/* Helper function for svn_ra_serf__lock and svn_ra_serf__unlock */
static svn_error_t *
run_locks(svn_ra_serf__session_t *sess,
          apr_array_header_t *lock_ctxs,
          svn_boolean_t locking,
          svn_ra_lock_callback_t lock_func,
          void *lock_baton,
          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_interval_time_t waittime_left = sess->timeout;

  assert(sess->pending_error == SVN_NO_ERROR);

  iterpool = svn_pool_create(scratch_pool);
  while (lock_ctxs->nelts)
    {
      int i;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_serf__context_run(sess, &waittime_left, iterpool));

      for (i = 0; i < lock_ctxs->nelts; i++)
        {
          lock_ctx_t *ctx = APR_ARRAY_IDX(lock_ctxs, i, lock_ctx_t *);

          if (ctx->handler->done)
            {
              svn_error_t *server_err = NULL;
              svn_error_t *cb_err = NULL;
              svn_error_t *err;

              if (ctx->handler->server_error)
                server_err = svn_ra_serf__server_error_create(ctx->handler, iterpool);

              /* Api users expect specific error code to detect failures,
                 pass the rest to svn_ra_serf__error_on_status */
              switch (ctx->handler->sline.code)
                {
                  case 200:
                  case 204:
                    err = NULL; /* (un)lock succeeded */
                    break;

                  case 400:
                    err = svn_error_createf(SVN_ERR_FS_NO_SUCH_LOCK, NULL,
                                            _("No lock on path '%s' (%d %s)"),
                                            ctx->path,
                                            ctx->handler->sline.code,
                                            ctx->handler->sline.reason);
                    break;
                  case 403:
                    /* ### Authz can also lead to 403. */
                    err = svn_error_createf(SVN_ERR_FS_LOCK_OWNER_MISMATCH,
                                            NULL,
                                            _("Not authorized to perform lock "
                                              "operation on '%s'"),
                                            ctx->path);
                    break;
                  case 405:
                    err = svn_error_createf(SVN_ERR_FS_OUT_OF_DATE,
                                            NULL,
                                            _("Path '%s' doesn't exist in "
                                              "HEAD revision (%d %s)"),
                                            ctx->path,
                                            ctx->handler->sline.code,
                                            ctx->handler->sline.reason);
                    break;
                  case 423:
                    if (server_err
                        && SVN_ERROR_IN_CATEGORY(server_err->apr_err,
                                                 SVN_ERR_FS_CATEGORY_START))
                      {
                        err = NULL;
                      }
                    else
                      err = svn_error_createf(SVN_ERR_FS_PATH_ALREADY_LOCKED,
                                              NULL,
                                              _("Path '%s' already locked "
                                                "(%d %s)"),
                                              ctx->path,
                                              ctx->handler->sline.code,
                                              ctx->handler->sline.reason);
                    break;

                  case 404:
                  case 409:
                  case 500:
                    if (server_err)
                      {
                        /* Handle out of date, etc by just passing the server
                           error */
                        err = NULL;
                        break;
                      }

                    /* Fall through */
                  default:
                    err = svn_ra_serf__unexpected_status(ctx->handler);
                    break;
                }

              if (server_err && err && server_err->apr_err == err->apr_err)
                err = svn_error_compose_create(server_err, err);
              else
                err = svn_error_compose_create(err, server_err);

              if (err
                  && !SVN_ERR_IS_UNLOCK_ERROR(err)
                  && !SVN_ERR_IS_LOCK_ERROR(err))
                {
                  /* If the error that we are going to report is just about the
                     POST unlock hook, we should first report that the operation
                     succeeded, or the repository and working copy will be
                     out of sync... */

                  if (lock_func &&
                      err->apr_err == SVN_ERR_REPOS_POST_UNLOCK_HOOK_FAILED)
                    {
                      err = svn_error_compose_create(
                                  err, lock_func(lock_baton, ctx->path, locking,
                                                 NULL, NULL, ctx->pool));
                    }

                  return svn_error_trace(err); /* Don't go through callbacks */
                }

              if (lock_func)
                {
                  svn_lock_t *report_lock = NULL;

                  if (locking && ctx->lock->token)
                    report_lock = ctx->lock;

                  cb_err = lock_func(lock_baton, ctx->path, locking,
                                     report_lock, err, ctx->pool);
                }
              svn_error_clear(err);

              SVN_ERR(cb_err);

              waittime_left = sess->timeout;
              svn_sort__array_delete(lock_ctxs, i, 1);
              i--;

              svn_pool_destroy(ctx->pool);
              continue;
            }
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
handle_lock(serf_request_t *request,
            serf_bucket_t *response,
            void *handler_baton,
            apr_pool_t *pool)
{
  lock_ctx_t *ctx = handler_baton;

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
create_lock_body(serf_bucket_t **body_bkt,
                 void *baton,
                 serf_bucket_alloc_t *alloc,
                 apr_pool_t *pool /* request pool */,
                 apr_pool_t *scratch_pool)
{
  lock_ctx_t *ctx = baton;
  serf_bucket_t *buckets;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(buckets, alloc);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "lockinfo",
                                    "xmlns", "DAV:",
                                    SVN_VA_NULL);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "lockscope", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(buckets, alloc, "exclusive", SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "lockscope");

  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "locktype", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(buckets, alloc, "write", SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "locktype");

  if (ctx->lock->comment)
    {
      svn_ra_serf__add_tag_buckets(buckets, "owner", ctx->lock->comment,
                                   alloc);
    }

  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "lockinfo");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__lock(svn_ra_session_t *ra_session,
                  apr_hash_t *path_revs,
                  const char *comment,
                  svn_boolean_t force,
                  svn_ra_lock_callback_t lock_func,
                  void *lock_baton,
                  apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  apr_array_header_t *lock_requests;

  lock_requests = apr_array_make(scratch_pool, apr_hash_count(path_revs),
                                 sizeof(lock_ctx_t*));

  /* ### Perhaps we should open more connections than just one? See update.c */

  iterpool = svn_pool_create(scratch_pool);

  for (hi = apr_hash_first(scratch_pool, path_revs);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__xml_context_t *xmlctx;
      const char *req_url;
      lock_ctx_t *lock_ctx;
      apr_pool_t *lock_pool;

      svn_pool_clear(iterpool);

      lock_pool = svn_pool_create(scratch_pool);
      lock_ctx = apr_pcalloc(scratch_pool, sizeof(*lock_ctx));

      lock_ctx->pool = lock_pool;
      lock_ctx->path = apr_hash_this_key(hi);
      lock_ctx->revision = *((svn_revnum_t*)apr_hash_this_val(hi));
      lock_ctx->lock = svn_lock_create(lock_pool);
      lock_ctx->lock->path = lock_ctx->path;
      lock_ctx->lock->comment = comment;

      lock_ctx->force = force;
      req_url = svn_path_url_add_component2(session->session_url.path,
                                            lock_ctx->path, lock_pool);

      xmlctx = svn_ra_serf__xml_context_create(locks_ttable,
                                               NULL, locks_closed, NULL,
                                               lock_ctx,
                                               lock_pool);
      handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                                  lock_pool);

      handler->method = "LOCK";
      handler->path = req_url;
      handler->body_type = "text/xml";

      /* Same stupid algorithm from get_best_connection() in update.c */
      handler->conn = session->conns[session->cur_conn];
      session->cur_conn++;

      if (session->cur_conn >= session->num_conns)
        session->cur_conn = 0;

      handler->header_delegate = set_lock_headers;
      handler->header_delegate_baton = lock_ctx;

      handler->body_delegate = create_lock_body;
      handler->body_delegate_baton = lock_ctx;

      lock_ctx->inner_handler = handler->response_handler;
      lock_ctx->inner_baton = handler->response_baton;
      handler->response_handler = handle_lock;
      handler->response_baton = lock_ctx;

      handler->no_fail_on_http_failure_status = TRUE;

      lock_ctx->handler = handler;

      APR_ARRAY_PUSH(lock_requests, lock_ctx_t *) = lock_ctx;

      svn_ra_serf__request_create(handler);
    }

  SVN_ERR(run_locks(session, lock_requests, TRUE, lock_func, lock_baton,
                    iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
set_unlock_headers(serf_bucket_t *headers,
                   void *baton,
                   apr_pool_t *pool /* request pool */,
                   apr_pool_t *scratch_pool)
{
  lock_ctx_t *ctx = baton;

  serf_bucket_headers_set(headers, "Lock-Token", ctx->token);
  if (ctx->force)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_BREAK);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__unlock(svn_ra_session_t *ra_session,
                    apr_hash_t *path_tokens,
                    svn_boolean_t force,
                    svn_ra_lock_callback_t lock_func,
                    void *lock_baton,
                    apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  apr_array_header_t *lock_requests;

  iterpool = svn_pool_create(scratch_pool);

  /* If we are stealing locks we need the lock tokens */
  if (force)
    {
      /* Theoretically this part can be improved (for performance) by using
         svn_ra_get_locks() to obtain all the locks in a single request, but
         do we really want to improve the performance of
            $ svn unlock --force *
       */

      for (hi = apr_hash_first(scratch_pool, path_tokens);
       hi;
       hi = apr_hash_next(hi))
        {
          const char *path;
          const char *token;
          svn_lock_t *existing_lock;
          svn_error_t *err;

          svn_pool_clear(iterpool);

          path = apr_hash_this_key(hi);
          token = apr_hash_this_val(hi);

          if (token && token[0])
            continue;

          if (session->cancel_func)
            SVN_ERR(session->cancel_func(session->cancel_baton));

          err = svn_ra_serf__get_lock(ra_session, &existing_lock, path,
                                      iterpool);

          if (!err && existing_lock)
            {
              svn_hash_sets(path_tokens, path,
                            apr_pstrdup(scratch_pool, existing_lock->token));
              continue;
            }

          err = svn_error_createf(SVN_ERR_RA_NOT_LOCKED, err,
                                  _("'%s' is not locked in the repository"),
                                  path);

          if (lock_func)
            {
              svn_error_t *err2;
              err2 = lock_func(lock_baton, path, FALSE, NULL, err, iterpool);
              svn_error_clear(err);

              SVN_ERR(err2);
            }
          else
            {
              svn_error_clear(err);
            }

          svn_hash_sets(path_tokens, path, NULL);
        }
    }

  /* ### Perhaps we should open more connections than just one? See update.c */

  lock_requests = apr_array_make(scratch_pool, apr_hash_count(path_tokens),
                                 sizeof(lock_ctx_t*));

  for (hi = apr_hash_first(scratch_pool, path_tokens);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      const char *req_url, *token;
      lock_ctx_t *lock_ctx;
      apr_pool_t *lock_pool;

      svn_pool_clear(iterpool);

      lock_pool = svn_pool_create(scratch_pool);
      lock_ctx = apr_pcalloc(lock_pool, sizeof(*lock_ctx));

      lock_ctx->pool = lock_pool;

      lock_ctx->path = apr_hash_this_key(hi);
      token = apr_hash_this_val(hi);

      lock_ctx->force = force;
      lock_ctx->token = apr_pstrcat(lock_pool, "<", token, ">", SVN_VA_NULL);

      req_url = svn_path_url_add_component2(session->session_url.path, lock_ctx->path,
                                            lock_pool);

      handler = svn_ra_serf__create_handler(session, lock_pool);

      handler->method = "UNLOCK";
      handler->path = req_url;

      handler->header_delegate = set_unlock_headers;
      handler->header_delegate_baton = lock_ctx;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      handler->no_fail_on_http_failure_status = TRUE;

      lock_ctx->handler = handler;

      APR_ARRAY_PUSH(lock_requests, lock_ctx_t *) = lock_ctx;

      svn_ra_serf__request_create(handler);
    }

  SVN_ERR(run_locks(session, lock_requests, FALSE, lock_func, lock_baton,
                    iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
