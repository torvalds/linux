/*
 * log.c :  entry point for log RA functions for ra_serf
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
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_config.h"
#include "svn_path.h"
#include "svn_props.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"



/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum log_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  ITEM,
  VERSION,
  CREATOR,
  DATE,
  COMMENT,
  REVPROP,
  HAS_CHILDREN,
  ADDED_PATH,
  REPLACED_PATH,
  DELETED_PATH,
  MODIFIED_PATH,
  SUBTRACTIVE_MERGE
};

typedef struct log_context_t {
  apr_pool_t *pool;

  /* parameters set by our caller */
  const apr_array_header_t *paths;
  svn_revnum_t start;
  svn_revnum_t end;
  int limit;
  svn_boolean_t changed_paths;
  svn_boolean_t strict_node_history;
  svn_boolean_t include_merged_revisions;
  const apr_array_header_t *revprops;
  int nest_level; /* used to track mergeinfo nesting levels */
  int count; /* only incremented when nest_level == 0 */

  /* Collect information for storage into a log entry. Most of the entry
     members are collected by individual states. revprops and paths are
     N datapoints per entry.  */
  apr_hash_t *collect_revprops;
  apr_hash_t *collect_paths;

  /* log receiver function and baton */
  svn_log_entry_receiver_t receiver;
  void *receiver_baton;

  /* pre-1.5 compatibility */
  svn_boolean_t want_author;
  svn_boolean_t want_date;
  svn_boolean_t want_message;
} log_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t log_ttable[] = {
  { INITIAL, S_, "log-report", REPORT,
    FALSE, { NULL }, FALSE },

  /* Note that we have an opener here. We need to construct a new LOG_ENTRY
     to record multiple paths.  */
  { REPORT, S_, "log-item", ITEM,
    FALSE, { NULL }, TRUE },

  { ITEM, D_, SVN_DAV__VERSION_NAME, VERSION,
    TRUE, { NULL }, TRUE },

  { ITEM, D_, "creator-displayname", CREATOR,
    TRUE, { "?encoding", NULL }, TRUE },

  { ITEM, S_, "date", DATE,
    TRUE, { "?encoding", NULL }, TRUE },

  { ITEM, D_, "comment", COMMENT,
    TRUE, { "?encoding", NULL }, TRUE },

  { ITEM, S_, "revprop", REVPROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { ITEM, S_, "has-children", HAS_CHILDREN,
    FALSE, { NULL }, TRUE },

  { ITEM, S_, "subtractive-merge", SUBTRACTIVE_MERGE,
    FALSE, { NULL }, TRUE },

  { ITEM, S_, "added-path", ADDED_PATH,
    TRUE, { "?node-kind", "?text-mods", "?prop-mods",
            "?copyfrom-path", "?copyfrom-rev", NULL }, TRUE },

  { ITEM, S_, "replaced-path", REPLACED_PATH,
    TRUE, { "?node-kind", "?text-mods", "?prop-mods",
            "?copyfrom-path", "?copyfrom-rev", NULL }, TRUE },

  { ITEM, S_, "deleted-path", DELETED_PATH,
    TRUE, { "?node-kind", "?text-mods", "?prop-mods", NULL }, TRUE },

  { ITEM, S_, "modified-path", MODIFIED_PATH,
    TRUE, { "?node-kind", "?text-mods", "?prop-mods", NULL }, TRUE },

  { 0 }
};



/* Store CDATA into REVPROPS, associated with PROPNAME. If ENCODING is not
   NULL, then it must base "base64" and CDATA will be decoded first.

   NOTE: PROPNAME must live longer than REVPROPS.  */
static svn_error_t *
collect_revprop(apr_hash_t *revprops,
                const char *propname,
                const svn_string_t *cdata,
                const char *encoding)
{
  apr_pool_t *result_pool = apr_hash_pool_get(revprops);
  const svn_string_t *decoded;

  if (encoding)
    {
      /* Check for a known encoding type.  This is easy -- there's
         only one.  */
      if (strcmp(encoding, "base64") != 0)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                   _("Unsupported encoding '%s'"),
                                   encoding);
        }

      decoded = svn_base64_decode_string(cdata, result_pool);
    }
  else
    {
      decoded = svn_string_dup(cdata, result_pool);
    }

  /* Caller has ensured PROPNAME has sufficient lifetime.  */
  svn_hash_sets(revprops, propname, decoded);

  return SVN_NO_ERROR;
}


/* Record ACTION on the path in CDATA into PATHS. Other properties about
   the action are pulled from ATTRS.  */
static svn_error_t *
collect_path(apr_hash_t *paths,
             char action,
             const svn_string_t *cdata,
             apr_hash_t *attrs)
{
  apr_pool_t *result_pool = apr_hash_pool_get(paths);
  svn_log_changed_path2_t *lcp;
  const char *copyfrom_path;
  const char *copyfrom_rev;
  const char *path;

  lcp = svn_log_changed_path2_create(result_pool);
  lcp->action = action;
  lcp->copyfrom_rev = SVN_INVALID_REVNUM;

  /* COPYFROM_* are only recorded for ADDED_PATH and REPLACED_PATH.  */
  copyfrom_path = svn_hash_gets(attrs, "copyfrom-path");
  copyfrom_rev = svn_hash_gets(attrs, "copyfrom-rev");
  if (copyfrom_path && copyfrom_rev)
    {
      apr_int64_t rev;

      SVN_ERR(svn_cstring_atoi64(&rev, copyfrom_rev));

      if (SVN_IS_VALID_REVNUM((svn_revnum_t)rev))
        {
          lcp->copyfrom_path = apr_pstrdup(result_pool, copyfrom_path);
          lcp->copyfrom_rev = (svn_revnum_t)rev;
        }
    }

  lcp->node_kind = svn_node_kind_from_word(svn_hash_gets(attrs, "node-kind"));
  lcp->text_modified = svn_tristate__from_word(svn_hash_gets(attrs,
                                                             "text-mods"));
  lcp->props_modified = svn_tristate__from_word(svn_hash_gets(attrs,
                                                              "prop-mods"));

  path = apr_pstrmemdup(result_pool, cdata->data, cdata->len);
  svn_hash_sets(paths, path, lcp);

  return SVN_NO_ERROR;
}


/* Conforms to svn_ra_serf__xml_opened_t  */
static svn_error_t *
log_opened(svn_ra_serf__xml_estate_t *xes,
           void *baton,
           int entered_state,
           const svn_ra_serf__dav_props_t *tag,
           apr_pool_t *scratch_pool)
{
  log_context_t *log_ctx = baton;

  if (entered_state == ITEM)
    {
      apr_pool_t *state_pool = svn_ra_serf__xml_state_pool(xes);

      log_ctx->collect_revprops = apr_hash_make(state_pool);
      log_ctx->collect_paths = apr_hash_make(state_pool);
    }

  return SVN_NO_ERROR;
}


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
log_closed(svn_ra_serf__xml_estate_t *xes,
           void *baton,
           int leaving_state,
           const svn_string_t *cdata,
           apr_hash_t *attrs,
           apr_pool_t *scratch_pool)
{
  log_context_t *log_ctx = baton;

  if (leaving_state == ITEM)
    {
      svn_log_entry_t *log_entry;
      const char *rev_str;

      if ((log_ctx->limit > 0) && (log_ctx->nest_level == 0)
          && (++log_ctx->count > log_ctx->limit))
        {
          return SVN_NO_ERROR;
        }

      log_entry = svn_log_entry_create(scratch_pool);

      /* Pick up the paths from the context. These have the same lifetime
         as this state. That is long enough for us to pass the paths to
         the receiver callback.  */
      if (apr_hash_count(log_ctx->collect_paths) > 0)
        {
          log_entry->changed_paths = log_ctx->collect_paths;
          log_entry->changed_paths2 = log_ctx->collect_paths;
        }

      /* ... and same story for the collected revprops.  */
      log_entry->revprops = log_ctx->collect_revprops;

      log_entry->has_children = svn_hash__get_bool(attrs,
                                                   "has-children",
                                                   FALSE);
      log_entry->subtractive_merge = svn_hash__get_bool(attrs,
                                                        "subtractive-merge",
                                                        FALSE);

      rev_str = svn_hash_gets(attrs, "revision");
      if (rev_str)
        {
          apr_int64_t rev;

          SVN_ERR(svn_cstring_atoi64(&rev, rev_str));
          log_entry->revision = (svn_revnum_t)rev;
        }
      else
        log_entry->revision = SVN_INVALID_REVNUM;

      /* Give the info to the reporter */
      SVN_ERR(log_ctx->receiver(log_ctx->receiver_baton,
                                log_entry,
                                scratch_pool));

      if (log_entry->has_children)
        {
          log_ctx->nest_level++;
        }
      if (! SVN_IS_VALID_REVNUM(log_entry->revision))
        {
          SVN_ERR_ASSERT(log_ctx->nest_level);
          log_ctx->nest_level--;
        }

      /* These hash tables are going to be unusable once this state's
         pool is destroyed. But let's not leave stale pointers in
         structures that have a longer life.  */
      log_ctx->collect_revprops = NULL;
      log_ctx->collect_paths = NULL;
    }
  else if (leaving_state == VERSION)
    {
      svn_ra_serf__xml_note(xes, ITEM, "revision", cdata->data);
    }
  else if (leaving_state == CREATOR)
    {
      if (log_ctx->want_author)
        {
          SVN_ERR(collect_revprop(log_ctx->collect_revprops,
                                  SVN_PROP_REVISION_AUTHOR,
                                  cdata,
                                  svn_hash_gets(attrs, "encoding")));
        }
    }
  else if (leaving_state == DATE)
    {
      if (log_ctx->want_date)
        {
          SVN_ERR(collect_revprop(log_ctx->collect_revprops,
                                  SVN_PROP_REVISION_DATE,
                                  cdata,
                                  svn_hash_gets(attrs, "encoding")));
        }
    }
  else if (leaving_state == COMMENT)
    {
      if (log_ctx->want_message)
        {
          SVN_ERR(collect_revprop(log_ctx->collect_revprops,
                                  SVN_PROP_REVISION_LOG,
                                  cdata,
                                  svn_hash_gets(attrs, "encoding")));
        }
    }
  else if (leaving_state == REVPROP)
    {
      apr_pool_t *result_pool = apr_hash_pool_get(log_ctx->collect_revprops);

      SVN_ERR(collect_revprop(
                log_ctx->collect_revprops,
                apr_pstrdup(result_pool,
                            svn_hash_gets(attrs, "name")),
                cdata,
                svn_hash_gets(attrs, "encoding")
                ));
    }
  else if (leaving_state == HAS_CHILDREN)
    {
      svn_ra_serf__xml_note(xes, ITEM, "has-children", "yes");
    }
  else if (leaving_state == SUBTRACTIVE_MERGE)
    {
      svn_ra_serf__xml_note(xes, ITEM, "subtractive-merge", "yes");
    }
  else
    {
      char action;

      if (leaving_state == ADDED_PATH)
        action = 'A';
      else if (leaving_state == REPLACED_PATH)
        action = 'R';
      else if (leaving_state == DELETED_PATH)
        action = 'D';
      else
        {
          SVN_ERR_ASSERT(leaving_state == MODIFIED_PATH);
          action = 'M';
        }

      SVN_ERR(collect_path(log_ctx->collect_paths, action, cdata, attrs));
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_log_body(serf_bucket_t **body_bkt,
                void *baton,
                serf_bucket_alloc_t *alloc,
                apr_pool_t *pool /* request pool */,
                apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  log_context_t *log_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:log-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision",
                               apr_ltoa(pool, log_ctx->start),
                               alloc);
  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision",
                               apr_ltoa(pool, log_ctx->end),
                               alloc);

  if (log_ctx->limit)
    {
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:limit", apr_ltoa(pool, log_ctx->limit),
                                   alloc);
    }

  if (log_ctx->changed_paths)
    {
      svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                         "S:discover-changed-paths",
                                         SVN_VA_NULL);
    }

  if (log_ctx->strict_node_history)
    {
      svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                         "S:strict-node-history", SVN_VA_NULL);
    }

  if (log_ctx->include_merged_revisions)
    {
      svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                         "S:include-merged-revisions",
                                         SVN_VA_NULL);
    }

  if (log_ctx->revprops)
    {
      int i;
      for (i = 0; i < log_ctx->revprops->nelts; i++)
        {
          char *name = APR_ARRAY_IDX(log_ctx->revprops, i, char *);
          svn_ra_serf__add_tag_buckets(buckets,
                                       "S:revprop", name,
                                       alloc);
        }
      if (log_ctx->revprops->nelts == 0)
        {
          svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                             "S:no-revprops", SVN_VA_NULL);
        }
    }
  else
    {
      svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                         "S:all-revprops", SVN_VA_NULL);
    }

  if (log_ctx->paths)
    {
      int i;
      for (i = 0; i < log_ctx->paths->nelts; i++)
        {
          svn_ra_serf__add_tag_buckets(buckets,
                                       "S:path", APR_ARRAY_IDX(log_ctx->paths, i,
                                                               const char*),
                                       alloc);
        }
    }

  svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                     "S:encode-binary-props", SVN_VA_NULL);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:log-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_log(svn_ra_session_t *ra_session,
                     const apr_array_header_t *paths,
                     svn_revnum_t start,
                     svn_revnum_t end,
                     int limit,
                     svn_boolean_t discover_changed_paths,
                     svn_boolean_t strict_node_history,
                     svn_boolean_t include_merged_revisions,
                     const apr_array_header_t *revprops,
                     svn_log_entry_receiver_t receiver,
                     void *receiver_baton,
                     apr_pool_t *pool)
{
  log_context_t *log_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  svn_boolean_t want_custom_revprops;
  svn_revnum_t peg_rev;
  const char *req_url;

  log_ctx = apr_pcalloc(pool, sizeof(*log_ctx));
  log_ctx->pool = pool;
  log_ctx->receiver = receiver;
  log_ctx->receiver_baton = receiver_baton;
  log_ctx->paths = paths;
  log_ctx->start = start;
  log_ctx->end = end;
  log_ctx->limit = limit;
  log_ctx->changed_paths = discover_changed_paths;
  log_ctx->strict_node_history = strict_node_history;
  log_ctx->include_merged_revisions = include_merged_revisions;
  log_ctx->revprops = revprops;
  log_ctx->nest_level = 0;

  want_custom_revprops = FALSE;
  if (revprops)
    {
      int i;
      for (i = 0; i < revprops->nelts; i++)
        {
          char *name = APR_ARRAY_IDX(revprops, i, char *);
          if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0)
            log_ctx->want_author = TRUE;
          else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0)
            log_ctx->want_date = TRUE;
          else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0)
            log_ctx->want_message = TRUE;
          else
            want_custom_revprops = TRUE;
        }
    }
  else
    {
      log_ctx->want_author = log_ctx->want_date = log_ctx->want_message = TRUE;
      want_custom_revprops = TRUE;
    }

  if (want_custom_revprops)
    {
      svn_boolean_t has_log_revprops;
      SVN_ERR(svn_ra_serf__has_capability(ra_session, &has_log_revprops,
                                          SVN_RA_CAPABILITY_LOG_REVPROPS, pool));
      if (!has_log_revprops)
        return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                _("Server does not support custom revprops"
                                  " via log"));
    }
  /* At this point, we may have a deleted file.  So, we'll match ra_neon's
   * behavior and use the larger of start or end as our 'peg' rev.
   */
  peg_rev = (start == SVN_INVALID_REVNUM || start > end) ? start : end;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session,
                                      NULL /* url */, peg_rev,
                                      pool, pool));

  xmlctx = svn_ra_serf__xml_context_create(log_ttable,
                                           log_opened, log_closed, NULL,
                                           log_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_delegate = create_log_body;
  handler->body_delegate_baton = log_ctx;
  handler->body_type = "text/xml";

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code != 200)
    SVN_ERR(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}
