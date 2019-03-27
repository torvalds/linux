/*
 * getlocks.c :  entry point for get_locks RA functions for ra_serf
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

#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_xml.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_fspath.h"
#include "svn_private_config.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum getlocks_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  LOCK,
  PATH,
  TOKEN,
  OWNER,
  COMMENT,
  CREATION_DATE,
  EXPIRATION_DATE
};

typedef struct lock_context_t {
  apr_pool_t *pool;

  /* target and requested depth of the operation. */
  const char *path;
  svn_depth_t requested_depth;

  /* return hash */
  apr_hash_t *hash;

} lock_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t getlocks_ttable[] = {
  { INITIAL, S_, "get-locks-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, S_, "lock", LOCK,
    FALSE, { NULL }, TRUE },

  { LOCK, S_, "path", PATH,
    TRUE, { NULL }, TRUE },

  { LOCK, S_, "token", TOKEN,
    TRUE, { NULL }, TRUE },

  { LOCK, S_, "owner", OWNER,
    TRUE, { NULL }, TRUE },

  { LOCK, S_, "comment", COMMENT,
    TRUE, { NULL }, TRUE },

  { LOCK, S_, SVN_DAV__CREATIONDATE, CREATION_DATE,
    TRUE, { NULL }, TRUE },

  { LOCK, S_, "expirationdate", EXPIRATION_DATE,
    TRUE, { NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
getlocks_closed(svn_ra_serf__xml_estate_t *xes,
                void *baton,
                int leaving_state,
                const svn_string_t *cdata,
                apr_hash_t *attrs,
                apr_pool_t *scratch_pool)
{
  lock_context_t *lock_ctx = baton;

  if (leaving_state == LOCK)
    {
      const char *path = svn_hash_gets(attrs, "path");
      const char *token = svn_hash_gets(attrs, "token");
      svn_boolean_t save_lock = FALSE;

      /* Filter out unwanted paths.  Since Subversion only allows
         locks on files, we can treat depth=immediates the same as
         depth=files for filtering purposes.  Meaning, we'll keep
         this lock if:

         a) its path is the very path we queried, or
         b) we've asked for a fully recursive answer, or
         c) we've asked for depth=files or depth=immediates, and this
            lock is on an immediate child of our query path.
      */
      if (! token)
        {
          /* A lock without a token is not a lock; just an answer that there
             is no lock on the node. */
          save_lock = FALSE;
        }
      if (strcmp(lock_ctx->path, path) == 0
          || lock_ctx->requested_depth == svn_depth_infinity)
        {
          save_lock = TRUE;
        }
      else if (lock_ctx->requested_depth == svn_depth_files
               || lock_ctx->requested_depth == svn_depth_immediates)
        {
          const char *relpath = svn_fspath__skip_ancestor(lock_ctx->path,
                                                          path);
          if (relpath && (svn_path_component_count(relpath) == 1))
            save_lock = TRUE;
        }

      if (save_lock)
        {
          /* We get to put the structure on the stack rather than using
             svn_lock_create(). Bwahahaha....   */
          svn_lock_t lock = { 0 };
          const char *date;
          svn_lock_t *result_lock;

          /* Note: these "attributes" came from child elements. Some of
             them may have not been sent, so the value will be NULL.  */

          lock.path = path;
          lock.token = token;
          lock.owner = svn_hash_gets(attrs, "owner");
          lock.comment = svn_hash_gets(attrs, "comment");

          date = svn_hash_gets(attrs, SVN_DAV__CREATIONDATE);
          if (date)
            SVN_ERR(svn_time_from_cstring(&lock.creation_date, date,
                                          scratch_pool));

          date = svn_hash_gets(attrs, "expirationdate");
          if (date)
            SVN_ERR(svn_time_from_cstring(&lock.expiration_date, date,
                                          scratch_pool));

          result_lock = svn_lock_dup(&lock, lock_ctx->pool);
          svn_hash_sets(lock_ctx->hash, result_lock->path, result_lock);
        }
    }
  else
    {
      const char *name;

      SVN_ERR_ASSERT(cdata != NULL);

      if (leaving_state == PATH)
        name = "path";
      else if (leaving_state == TOKEN)
        name = "token";
      else if (leaving_state == OWNER)
        name = "owner";
      else if (leaving_state == COMMENT)
        name = "comment";
      else if (leaving_state == CREATION_DATE)
        name = SVN_DAV__CREATIONDATE;
      else if (leaving_state == EXPIRATION_DATE)
        name = "expirationdate";
      else
        SVN_ERR_MALFUNCTION();

      /* Store the lock information onto the LOCK elemstate.  */
      svn_ra_serf__xml_note(xes, LOCK, name, cdata->data);
    }

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_getlocks_body(serf_bucket_t **body_bkt,
                     void *baton,
                     serf_bucket_alloc_t *alloc,
                     apr_pool_t *pool /* request pool */,
                     apr_pool_t *scratch_pool)
{
  lock_context_t *lock_ctx = baton;
  serf_bucket_t *buckets;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(
    buckets, alloc, "S:get-locks-report", "xmlns:S", SVN_XML_NAMESPACE,
    "depth", svn_depth_to_word(lock_ctx->requested_depth), SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "S:get-locks-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_locks(svn_ra_session_t *ra_session,
                       apr_hash_t **locks,
                       const char *path,
                       svn_depth_t depth,
                       apr_pool_t *pool)
{
  lock_context_t *lock_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url, *rel_path;
  svn_error_t *err;

  req_url = svn_path_url_add_component2(session->session_url.path, path, pool);
  SVN_ERR(svn_ra_serf__get_relative_path(&rel_path, req_url, session, pool));

  lock_ctx = apr_pcalloc(pool, sizeof(*lock_ctx));
  lock_ctx->pool = pool;
  lock_ctx->path = apr_pstrcat(pool, "/", rel_path, SVN_VA_NULL);
  lock_ctx->requested_depth = depth;
  lock_ctx->hash = apr_hash_make(pool);

  xmlctx = svn_ra_serf__xml_context_create(getlocks_ttable,
                                           NULL, getlocks_closed, NULL,
                                           lock_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_type = "text/xml";

  handler->body_delegate = create_getlocks_body;
  handler->body_delegate_baton = lock_ctx;

  err = svn_ra_serf__context_run_one(handler, pool);

  if (err)
    {
      if (svn_error_find_cause(err, SVN_ERR_UNSUPPORTED_FEATURE))
        {
          /* The server told us that it doesn't support this report type.
             We return the documented error for svn_ra_get_locks(), but
             with the original error report */
          return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err, NULL);
        }
      else if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          /* File doesn't exist in HEAD: Not an error */
          svn_error_clear(err);
        }
      else
        return svn_error_trace(err);
    }

  /* We get a 404 when a path doesn't exist in HEAD, but it might
     have existed earlier (E.g. 'svn ls http://s/svn/trunk/file@1' */
  if (handler->sline.code != 200
      && handler->sline.code != 404)
    {
      return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  *locks = lock_ctx->hash;

  return SVN_NO_ERROR;
}
