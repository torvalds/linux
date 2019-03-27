/*
 * replay.c :  entry point for replay RA functions for ra_serf
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

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_hash.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_base64.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "private/svn_string_private.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing.
 */
typedef enum replay_state_e {
  INITIAL = XML_STATE_INITIAL,

  REPLAY_REPORT,
  REPLAY_TARGET_REVISION,
  REPLAY_OPEN_ROOT,
  REPLAY_OPEN_DIRECTORY,
  REPLAY_OPEN_FILE,
  REPLAY_ADD_DIRECTORY,
  REPLAY_ADD_FILE,
  REPLAY_DELETE_ENTRY,
  REPLAY_CLOSE_FILE,
  REPLAY_CLOSE_DIRECTORY,
  REPLAY_CHANGE_DIRECTORY_PROP,
  REPLAY_CHANGE_FILE_PROP,
  REPLAY_APPLY_TEXTDELTA
} replay_state_e;

#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t replay_ttable[] = {
  { INITIAL, S_, "editor-report", REPLAY_REPORT,
    FALSE, { NULL }, TRUE },

  /* Replay just throws every operation as xml element directly
     in the replay report, so we can't really use the nice exit
     handling of the transition parser to handle clean callbacks */

  { REPLAY_REPORT, S_, "target-revision", REPLAY_TARGET_REVISION,
    FALSE, { "rev", NULL }, TRUE },

  { REPLAY_REPORT, S_, "open-root", REPLAY_OPEN_ROOT,
    FALSE, { "rev", NULL }, TRUE },

  { REPLAY_REPORT, S_, "open-directory", REPLAY_OPEN_DIRECTORY,
    FALSE, { "name", "rev", NULL }, TRUE },

  { REPLAY_REPORT, S_, "open-file", REPLAY_OPEN_FILE,
    FALSE, { "name", "rev", NULL }, TRUE },

  { REPLAY_REPORT, S_, "add-directory", REPLAY_ADD_DIRECTORY,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev", NULL}, TRUE },

  { REPLAY_REPORT, S_, "add-file", REPLAY_ADD_FILE,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev", NULL}, TRUE },

  { REPLAY_REPORT, S_, "delete-entry", REPLAY_DELETE_ENTRY,
    FALSE, { "name", "rev", NULL }, TRUE },

  { REPLAY_REPORT, S_, "close-file", REPLAY_CLOSE_FILE,
    FALSE, { "?checksum", NULL }, TRUE },

  { REPLAY_REPORT, S_, "close-directory", REPLAY_CLOSE_DIRECTORY,
    FALSE, { NULL }, TRUE },

  { REPLAY_REPORT, S_, "change-dir-prop", REPLAY_CHANGE_DIRECTORY_PROP,
    TRUE, { "name", "?del", NULL }, TRUE },

  { REPLAY_REPORT, S_, "change-file-prop", REPLAY_CHANGE_FILE_PROP,
    TRUE, { "name", "?del", NULL }, TRUE },

  { REPLAY_REPORT, S_, "apply-textdelta", REPLAY_APPLY_TEXTDELTA,
    FALSE, { "?checksum", NULL }, TRUE },

  { 0 }
};

/* Per directory/file state */
typedef struct replay_node_t {
  apr_pool_t *pool; /* pool allocating this node's data */
  svn_boolean_t file; /* file or dir */

  void *baton; /* node baton */
  svn_stream_t *stream; /* stream while handling txdata */

  struct replay_node_t *parent; /* parent node or NULL */
} replay_node_t;

/* Per revision replay report state */
typedef struct revision_report_t {
  apr_pool_t *pool; /* per revision pool */

  struct replay_node_t *current_node;
  struct replay_node_t *root_node;

  /* Are we done fetching this file?
     Handles book-keeping in multi-report case */
  svn_boolean_t *done;
  int *replay_reports; /* NULL or number of outstanding reports */

  /* callback to get an editor */
  svn_ra_replay_revstart_callback_t revstart_func;
  svn_ra_replay_revfinish_callback_t revfinish_func;
  void *replay_baton;

  /* replay receiver function and baton */
  const svn_delta_editor_t *editor;
  void *editor_baton;

  /* Path and revision used to filter replayed changes.  If
     INCLUDE_PATH is non-NULL, REVISION is unnecessary and will not be
     included in the replay REPORT.  (Because the REPORT is being
     aimed an HTTP v2 revision resource.)  */
  const char *include_path;
  svn_revnum_t revision;

  /* Information needed to create the replay report body */
  svn_revnum_t low_water_mark;
  svn_boolean_t send_deltas;

  /* Target and revision to fetch revision properties on */
  const char *revprop_target;
  svn_revnum_t revprop_rev;

  /* Revision properties for this revision. */
  apr_hash_t *rev_props;

  /* Handlers for the PROPFIND and REPORT for the current revision. */
  svn_ra_serf__handler_t *propfind_handler;
  svn_ra_serf__handler_t *report_handler; /* For done handler */

  svn_ra_serf__session_t *session;

} revision_report_t;

/* Conforms to svn_ra_serf__xml_opened_t */
static svn_error_t *
replay_opened(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int entered_state,
              const svn_ra_serf__dav_props_t *tag,
              apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;

  if (entered_state == REPLAY_REPORT)
    {
      /* Before we can continue, we need the revision properties. */
      SVN_ERR_ASSERT(!ctx->propfind_handler || ctx->propfind_handler->done);

      svn_ra_serf__keep_only_regular_props(ctx->rev_props, scratch_pool);

      if (ctx->revstart_func)
        {
          SVN_ERR(ctx->revstart_func(ctx->revision, ctx->replay_baton,
                                     &ctx->editor, &ctx->editor_baton,
                                     ctx->rev_props,
                                     ctx->pool));
        }
    }
  else if (entered_state == REPLAY_APPLY_TEXTDELTA)
    {
       struct replay_node_t *node = ctx->current_node;
       apr_hash_t *attrs;
       const char *checksum;
       svn_txdelta_window_handler_t handler;
       void *handler_baton;

       if (! node || ! node->file || node->stream)
         return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

       /* ### Is there a better way to access a specific attr here? */
       attrs = svn_ra_serf__xml_gather_since(xes, REPLAY_APPLY_TEXTDELTA);
       checksum = svn_hash_gets(attrs, "checksum");

       SVN_ERR(ctx->editor->apply_textdelta(node->baton, checksum, node->pool,
                                            &handler, &handler_baton));

       if (handler != svn_delta_noop_window_handler)
         {
            node->stream = svn_base64_decode(
                                    svn_txdelta_parse_svndiff(handler,
                                                              handler_baton,
                                                              TRUE,
                                                              node->pool),
                                    node->pool);
         }
    }

  return SVN_NO_ERROR;
}

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
replay_closed(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int leaving_state,
              const svn_string_t *cdata,
              apr_hash_t *attrs,
              apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;

  if (leaving_state == REPLAY_REPORT)
    {
      if (ctx->current_node)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      if (ctx->revfinish_func)
        {
          SVN_ERR(ctx->revfinish_func(ctx->revision, ctx->replay_baton,
                                      ctx->editor, ctx->editor_baton,
                                      ctx->rev_props, scratch_pool));
        }
    }
  else if (leaving_state == REPLAY_TARGET_REVISION)
    {
      const char *revstr = svn_hash_gets(attrs, "rev");
      apr_int64_t rev;

      SVN_ERR(svn_cstring_atoi64(&rev, revstr));
      SVN_ERR(ctx->editor->set_target_revision(ctx->editor_baton,
                                               (svn_revnum_t)rev,
                                               scratch_pool));
    }
  else if (leaving_state == REPLAY_OPEN_ROOT)
    {
      const char *revstr = svn_hash_gets(attrs, "rev");
      apr_int64_t rev;
      apr_pool_t *root_pool = svn_pool_create(ctx->pool);

      if (ctx->current_node || ctx->root_node)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      ctx->root_node = apr_pcalloc(root_pool, sizeof(*ctx->root_node));
      ctx->root_node->pool = root_pool;

      ctx->current_node = ctx->root_node;

      SVN_ERR(svn_cstring_atoi64(&rev, revstr));
      SVN_ERR(ctx->editor->open_root(ctx->editor_baton, (svn_revnum_t)rev,
                                     root_pool,
                                     &ctx->current_node->baton));
    }
  else if (leaving_state == REPLAY_OPEN_DIRECTORY
           || leaving_state == REPLAY_OPEN_FILE
           || leaving_state == REPLAY_ADD_DIRECTORY
           || leaving_state == REPLAY_ADD_FILE)
    {
      struct replay_node_t *node;
      apr_pool_t *node_pool;
      const char *name = svn_hash_gets(attrs, "name");
      const char *rev_str;
      apr_int64_t rev;

      if (!ctx->current_node || ctx->current_node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      node_pool = svn_pool_create(ctx->current_node->pool);
      node = apr_pcalloc(node_pool, sizeof(*node));
      node->pool = node_pool;
      node->parent = ctx->current_node;

      if (leaving_state == REPLAY_OPEN_DIRECTORY
          || leaving_state == REPLAY_OPEN_FILE)
        {
          rev_str = svn_hash_gets(attrs, "rev");
        }
      else
        rev_str = svn_hash_gets(attrs, "copyfrom-rev");

      if (rev_str)
        SVN_ERR(svn_cstring_atoi64(&rev, rev_str));
      else
        rev = SVN_INVALID_REVNUM;

      switch (leaving_state)
        {
          case REPLAY_OPEN_DIRECTORY:
            node->file = FALSE;
            SVN_ERR(ctx->editor->open_directory(name,
                                    ctx->current_node->baton,
                                    (svn_revnum_t)rev,
                                    node->pool,
                                    &node->baton));
            break;
          case REPLAY_OPEN_FILE:
            node->file = TRUE;
            SVN_ERR(ctx->editor->open_file(name,
                                    ctx->current_node->baton,
                                    (svn_revnum_t)rev,
                                    node->pool,
                                    &node->baton));
            break;
          case REPLAY_ADD_DIRECTORY:
            node->file = FALSE;
            SVN_ERR(ctx->editor->add_directory(
                                    name,
                                    ctx->current_node->baton,
                                    SVN_IS_VALID_REVNUM(rev)
                                        ? svn_hash_gets(attrs, "copyfrom-path")
                                        : NULL,
                                    (svn_revnum_t)rev,
                                    node->pool,
                                    &node->baton));
            break;
          case REPLAY_ADD_FILE:
            node->file = TRUE;
            SVN_ERR(ctx->editor->add_file(
                                    name,
                                    ctx->current_node->baton,
                                    SVN_IS_VALID_REVNUM(rev)
                                        ? svn_hash_gets(attrs, "copyfrom-path")
                                        : NULL,
                                    (svn_revnum_t)rev,
                                    node->pool,
                                    &node->baton));
            break;
          /* default: unreachable */
        }
      ctx->current_node = node;
    }
  else if (leaving_state == REPLAY_CLOSE_FILE)
    {
      struct replay_node_t *node = ctx->current_node;

      if (! node || ! node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      SVN_ERR(ctx->editor->close_file(node->baton,
                                      svn_hash_gets(attrs, "checksum"),
                                      node->pool));
      ctx->current_node = node->parent;
      svn_pool_destroy(node->pool);
    }
  else if (leaving_state == REPLAY_CLOSE_DIRECTORY)
    {
      struct replay_node_t *node = ctx->current_node;

      if (! node || node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      SVN_ERR(ctx->editor->close_directory(node->baton, node->pool));
      ctx->current_node = node->parent;
      svn_pool_destroy(node->pool);
    }
  else if (leaving_state == REPLAY_DELETE_ENTRY)
    {
      struct replay_node_t *parent_node = ctx->current_node;
      const char *name = svn_hash_gets(attrs, "name");
      const char *revstr = svn_hash_gets(attrs, "rev");
      apr_int64_t rev;

      if (! parent_node || parent_node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      SVN_ERR(svn_cstring_atoi64(&rev, revstr));
      SVN_ERR(ctx->editor->delete_entry(name,
                                        (svn_revnum_t)rev,
                                        parent_node->baton,
                                        scratch_pool));
    }
  else if (leaving_state == REPLAY_CHANGE_FILE_PROP
           || leaving_state == REPLAY_CHANGE_DIRECTORY_PROP)
    {
      struct replay_node_t *node = ctx->current_node;
      const char *name;
      const svn_string_t *value;

      if (! node || node->file != (leaving_state == REPLAY_CHANGE_FILE_PROP))
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      name = svn_hash_gets(attrs, "name");

      if (svn_hash_gets(attrs, "del"))
        value = NULL;
      else
        value = svn_base64_decode_string(cdata, scratch_pool);

      if (node->file)
        {
          SVN_ERR(ctx->editor->change_file_prop(node->baton, name, value,
                                                scratch_pool));
        }
      else
        {
          SVN_ERR(ctx->editor->change_dir_prop(node->baton, name, value,
                                               scratch_pool));
        }
    }
  else if (leaving_state == REPLAY_APPLY_TEXTDELTA)
    {
      struct replay_node_t *node = ctx->current_node;

      if (! node || ! node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      if (node->stream)
        SVN_ERR(svn_stream_close(node->stream));

      node->stream = NULL;
    }
  return SVN_NO_ERROR;
}

/* Conforms to svn_ra_serf__xml_cdata_t  */
static svn_error_t *
replay_cdata(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int current_state,
             const char *data,
             apr_size_t len,
             apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;

  if (current_state == REPLAY_APPLY_TEXTDELTA)
    {
      struct replay_node_t *node = ctx->current_node;

      if (! node || ! node->file)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      if (node->stream)
        {
          apr_size_t written = len;

          SVN_ERR(svn_stream_write(node->stream, data, &written));
          if (written != len)
            return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                                    _("Error writing stream: unexpected EOF"));
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_replay_body(serf_bucket_t **bkt,
                   void *baton,
                   serf_bucket_alloc_t *alloc,
                   apr_pool_t *pool /* request pool */,
                   apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc,
                                    "S:replay-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  /* If we have a non-NULL include path, we add it to the body and
     omit the revision; otherwise, the reverse. */
  if (ctx->include_path)
    {
      svn_ra_serf__add_tag_buckets(body_bkt,
                                   "S:include-path",
                                   ctx->include_path,
                                   alloc);
    }
  else
    {
      svn_ra_serf__add_tag_buckets(body_bkt,
                                   "S:revision",
                                   apr_ltoa(pool, ctx->revision),
                                   alloc);
    }
  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:low-water-mark",
                               apr_ltoa(pool, ctx->low_water_mark),
                               alloc);

  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:send-deltas",
                               apr_ltoa(pool, ctx->send_deltas),
                               alloc);

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "S:replay-report");

  *bkt = body_bkt;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__replay(svn_ra_session_t *ra_session,
                    svn_revnum_t revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t send_deltas,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    apr_pool_t *scratch_pool)
{
  struct revision_report_t ctx = { NULL };
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *report_target;

  SVN_ERR(svn_ra_serf__report_resource(&report_target, session,
                                       scratch_pool));

  ctx.pool = svn_pool_create(scratch_pool);
  ctx.editor = editor;
  ctx.editor_baton = edit_baton;
  ctx.done = FALSE;
  ctx.revision = revision;
  ctx.low_water_mark = low_water_mark;
  ctx.send_deltas = send_deltas;
  ctx.rev_props = apr_hash_make(scratch_pool);

  xmlctx = svn_ra_serf__xml_context_create(replay_ttable,
                                           replay_opened, replay_closed,
                                           replay_cdata,
                                           &ctx,
                                           scratch_pool);

  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                              scratch_pool);

  handler->method = "REPORT";
  handler->path = session->session_url.path;
  handler->body_delegate = create_replay_body;
  handler->body_delegate_baton = &ctx;
  handler->body_type = "text/xml";

  /* Not setting up done handler as we don't use a global context */

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  if (handler->sline.code != 200)
    SVN_ERR(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}

/* The maximum number of outstanding requests at any time. When this
 * number is reached, ra_serf will stop sending requests until
 * responses on the previous requests are received and handled.
 *
 * Some observations about serf which lead us to the current value.
 * ----------------------------------------------------------------
 *
 * We aim to keep serf's outgoing queue filled with enough requests so
 * the network bandwidth and server capacity is used
 * optimally. Originally we used 5 as the max. number of outstanding
 * requests, but this turned out to be too low.
 *
 * Serf doesn't exit out of the svn_ra_serf__context_run_wait loop as long as
 * it has data to send or receive. With small responses (revs of a few
 * kB), serf doesn't come out of this loop at all. So with
 * MAX_OUTSTANDING_REQUESTS set to a low number, there's a big chance
 * that serf handles those requests completely in its internal loop,
 * and only then gives us a chance to create new requests. This
 * results in hiccups, slowing down the whole process.
 *
 * With a larger MAX_OUTSTANDING_REQUESTS, like 100 or more, there's
 * more chance that serf can come out of its internal loop so we can
 * replenish the outgoing request queue.  There's no real disadvantage
 * of using a large number here, besides the memory used to store the
 * message, parser and handler objects (approx. 250 bytes).
 *
 * In my test setup peak performance was reached at max. 30-35
 * requests. So I added a small margin and chose 50.
 */
#define MAX_OUTSTANDING_REQUESTS 50

/* Implements svn_ra_serf__response_done_delegate_t for svn_ra_serf__replay_range */
static svn_error_t *
replay_done(serf_request_t *request,
            void *baton,
            apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;
  svn_ra_serf__handler_t *handler = ctx->report_handler;

  if (handler->server_error)
    return svn_ra_serf__server_error_create(handler, scratch_pool);
  else if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  *ctx->done = TRUE; /* Breaks out svn_ra_serf__context_run_wait */

  /* Are re replaying multiple revisions? */
  if (ctx->replay_reports)
    {
      (*ctx->replay_reports)--;
    }

  svn_pool_destroy(ctx->pool); /* Destroys handler and request! */

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_header_delegate_t */
static svn_error_t *
setup_headers(serf_bucket_t *headers,
              void *baton,
              apr_pool_t *request_pool,
              apr_pool_t *scratch_pool)
{
  struct revision_report_t *ctx = baton;

  svn_ra_serf__setup_svndiff_accept_encoding(headers, ctx->session);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__replay_range(svn_ra_session_t *ra_session,
                          svn_revnum_t start_revision,
                          svn_revnum_t end_revision,
                          svn_revnum_t low_water_mark,
                          svn_boolean_t send_deltas,
                          svn_ra_replay_revstart_callback_t revstart_func,
                          svn_ra_replay_revfinish_callback_t revfinish_func,
                          void *replay_baton,
                          apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_revnum_t rev = start_revision;
  const char *report_target;
  int active_reports = 0;
  const char *include_path;
  svn_boolean_t done;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  if (session->http20) {
      /* ### Auch... this doesn't work yet... 

         This code relies on responses coming in in an exact order, while
         http2 does everything to deliver responses as fast as possible.

         With http/1.1 we were quite lucky that this worked, as serf doesn't
         promise in order delivery.... (Please do not use authz with keys
         that expire)

         For now fall back to the legacy callback in libsvn_ra that is
         used by all the other ra layers as workaround.

         ### TODO: Optimize
         */
      return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
  }

  SVN_ERR(svn_ra_serf__report_resource(&report_target, session,
                                       subpool));

  /* Prior to 1.8, mod_dav_svn expect to get replay REPORT requests
     aimed at the session URL.  But that's incorrect -- these reports
     aren't about specific resources -- they are above revisions.  The
     path-based filtering offered by this API is just that: a filter
     applied to the full set of changes made in the revision.  As
     such, the correct target for these REPORT requests is the "me
     resource" (or, pre-http-v2, the default VCC).

     Our server should have told us if it supported this protocol
     correction.  If so, we aimed our report at the correct resource
     and include the filtering path as metadata within the report
     body.  Otherwise, we fall back to the pre-1.8 behavior and just
     wish for the best.

     See issue #4287:
     http://subversion.tigris.org/issues/show_bug.cgi?id=4287
  */
  if (session->supports_rev_rsrc_replay)
    {
      SVN_ERR(svn_ra_serf__get_relative_path(&include_path,
                                             session->session_url.path,
                                             session, subpool));
    }
  else
    {
      include_path = NULL;
    }

  while (active_reports || rev <= end_revision)
    {
      if (session->cancel_func)
        SVN_ERR(session->cancel_func(session->cancel_baton));

      /* Send pending requests, if any. Limit the number of outstanding
         requests to MAX_OUTSTANDING_REQUESTS. */
      if (rev <= end_revision  && active_reports < MAX_OUTSTANDING_REQUESTS)
        {
          struct revision_report_t *rev_ctx;
          svn_ra_serf__handler_t *handler;
          apr_pool_t *rev_pool = svn_pool_create(subpool);
          svn_ra_serf__xml_context_t *xmlctx;
          const char *replay_target;

          rev_ctx = apr_pcalloc(rev_pool, sizeof(*rev_ctx));
          rev_ctx->pool = rev_pool;
          rev_ctx->revstart_func = revstart_func;
          rev_ctx->revfinish_func = revfinish_func;
          rev_ctx->replay_baton = replay_baton;
          rev_ctx->done = &done;
          rev_ctx->replay_reports = &active_reports;
          rev_ctx->include_path = include_path;
          rev_ctx->revision = rev;
          rev_ctx->low_water_mark = low_water_mark;
          rev_ctx->send_deltas = send_deltas;
          rev_ctx->session = session;

          /* Request all properties of a certain revision. */
          rev_ctx->rev_props = apr_hash_make(rev_ctx->pool);

          if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
            {
              rev_ctx->revprop_target = apr_psprintf(rev_pool, "%s/%ld",
                                                     session->rev_stub, rev);
              rev_ctx->revprop_rev = SVN_INVALID_REVNUM;
            }
          else
            {
              rev_ctx->revprop_target = report_target;
              rev_ctx->revprop_rev = rev;
            }

          SVN_ERR(svn_ra_serf__create_propfind_handler(
                                              &rev_ctx->propfind_handler,
                                              session,
                                              rev_ctx->revprop_target,
                                              rev_ctx->revprop_rev,
                                              "0", all_props,
                                              svn_ra_serf__deliver_svn_props,
                                              rev_ctx->rev_props,
                                              rev_pool));

          /* Spin up the serf request for the PROPFIND.  */
          svn_ra_serf__request_create(rev_ctx->propfind_handler);

          /* Send the replay REPORT request. */
          if (session->supports_rev_rsrc_replay)
            {
              replay_target = apr_psprintf(rev_pool, "%s/%ld",
                                           session->rev_stub, rev);
            }
          else
            {
              replay_target = session->session_url.path;
            }

          xmlctx = svn_ra_serf__xml_context_create(replay_ttable,
                                           replay_opened, replay_closed,
                                           replay_cdata, rev_ctx,
                                           rev_pool);

          handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                                      rev_pool);

          handler->method = "REPORT";
          handler->path = replay_target;
          handler->body_delegate = create_replay_body;
          handler->body_delegate_baton = rev_ctx;
          handler->body_type = "text/xml";

          handler->done_delegate = replay_done;
          handler->done_delegate_baton = rev_ctx;

          handler->custom_accept_encoding = TRUE;
          handler->header_delegate = setup_headers;
          handler->header_delegate_baton = rev_ctx;

          rev_ctx->report_handler = handler;
          svn_ra_serf__request_create(handler);

          rev++;
          active_reports++;
        }

      /* Run the serf loop. */
      done = FALSE;
      {
        svn_error_t *err = svn_ra_serf__context_run_wait(&done, session,
                                                         subpool);

        if (err)
          {
            svn_pool_destroy(subpool); /* Unregister all requests! */
            return svn_error_trace(err);
          }
      }

      /* The done handler of reports decrements active_reports when a report
         is done. This same handler reports (fatal) report errors, so we can
         just loop here. */
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
#undef MAX_OUTSTANDING_REQUESTS
