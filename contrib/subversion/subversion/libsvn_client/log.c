/*
 * log.c:  return log messages
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

#include <apr_strings.h>
#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_compat.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_props.h"

#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"

#include <assert.h>

/*** Getting misc. information ***/

/* The baton for use with copyfrom_info_receiver(). */
typedef struct copyfrom_info_t
{
  svn_boolean_t is_first;
  const char *path;
  svn_revnum_t rev;
  apr_pool_t *pool;
} copyfrom_info_t;

/* A location segment callback for obtaining the copy source of
   a node at a path and storing it in *BATON (a struct copyfrom_info_t *).
   Implements svn_location_segment_receiver_t. */
static svn_error_t *
copyfrom_info_receiver(svn_location_segment_t *segment,
                       void *baton,
                       apr_pool_t *pool)
{
  copyfrom_info_t *copyfrom_info = baton;

  /* If we've already identified the copy source, there's nothing more
     to do.
     ### FIXME:  We *should* be able to send */
  if (copyfrom_info->path)
    return SVN_NO_ERROR;

  /* If this is the first segment, it's not of interest to us. Otherwise
     (so long as this segment doesn't represent a history gap), it holds
     our path's previous location (from which it was last copied). */
  if (copyfrom_info->is_first)
    {
      copyfrom_info->is_first = FALSE;
    }
  else if (segment->path)
    {
      /* The end of the second non-gap segment is the location copied from.  */
      copyfrom_info->path = apr_pstrdup(copyfrom_info->pool, segment->path);
      copyfrom_info->rev = segment->range_end;

      /* ### FIXME: We *should* be able to return SVN_ERR_CEASE_INVOCATION
         ### here so we don't get called anymore. */
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_copy_source(const char **original_repos_relpath,
                            svn_revnum_t *original_revision,
                            const char *path_or_url,
                            const svn_opt_revision_t *revision,
                            svn_ra_session_t *ra_session,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  copyfrom_info_t copyfrom_info = { 0 };
  apr_pool_t *sesspool = svn_pool_create(scratch_pool);
  svn_client__pathrev_t *at_loc;
  const char *old_session_url = NULL;

  copyfrom_info.is_first = TRUE;
  copyfrom_info.path = NULL;
  copyfrom_info.rev = SVN_INVALID_REVNUM;
  copyfrom_info.pool = result_pool;

  if (!ra_session)
    {
      SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &at_loc,
                                                path_or_url, NULL,
                                                revision, revision,
                                                ctx, sesspool));
    }
  else
    {
      const char *url;
      if (svn_path_is_url(path_or_url))
        url = path_or_url;
      else
        {
          SVN_ERR(svn_client_url_from_path2(&url, path_or_url, ctx, sesspool,
                                            sesspool));

          if (! url)
            return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                     _("'%s' has no URL"), path_or_url);
        }

      SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                                url, sesspool));

      err = svn_client__resolve_rev_and_url(&at_loc, ra_session, path_or_url,
                                            revision, revision, ctx,
                                            sesspool);

      /* On error reparent back (and return), otherwise reparent to new
         location */
      SVN_ERR(svn_error_compose_create(
                err,
                svn_ra_reparent(ra_session, err ? old_session_url
                                                : at_loc->url, sesspool)));
    }

  /* Find the copy source.  Walk the location segments to find the revision
     at which this node was created (copied or added). */

  err = svn_ra_get_location_segments(ra_session, "", at_loc->rev, at_loc->rev,
                                     SVN_INVALID_REVNUM,
                                     copyfrom_info_receiver, &copyfrom_info,
                                     scratch_pool);

  if (old_session_url)
    err = svn_error_compose_create(
                    err,
                    svn_ra_reparent(ra_session, old_session_url, sesspool));

  svn_pool_destroy(sesspool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND ||
          err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
        {
          /* A locally-added but uncommitted versioned resource won't
             exist in the repository. */
            svn_error_clear(err);
            err = SVN_NO_ERROR;

            *original_repos_relpath = NULL;
            *original_revision = SVN_INVALID_REVNUM;
        }
      return svn_error_trace(err);
    }

  *original_repos_relpath = copyfrom_info.path;
  *original_revision = copyfrom_info.rev;
  return SVN_NO_ERROR;
}


/* compatibility with pre-1.5 servers, which send only author/date/log
 *revprops in log entries */
typedef struct pre_15_receiver_baton_t
{
  svn_client_ctx_t *ctx;
  /* ra session for retrieving revprops from old servers */
  svn_ra_session_t *ra_session;
  /* caller's list of requested revprops, receiver, and baton */
  const char *ra_session_url;
  apr_pool_t *ra_session_pool;
  const apr_array_header_t *revprops;
  svn_log_entry_receiver_t receiver;
  void *baton;
} pre_15_receiver_baton_t;

static svn_error_t *
pre_15_receiver(void *baton, svn_log_entry_t *log_entry, apr_pool_t *pool)
{
  pre_15_receiver_baton_t *rb = baton;

  if (log_entry->revision == SVN_INVALID_REVNUM)
    return rb->receiver(rb->baton, log_entry, pool);

  /* If only some revprops are requested, get them one at a time on the
     second ra connection.  If all are requested, get them all with
     svn_ra_rev_proplist.  This avoids getting unrequested revprops (which
     may be arbitrarily large), but means one round-trip per requested
     revprop.  epg isn't entirely sure which should be optimized for. */
  if (rb->revprops)
    {
      int i;
      svn_boolean_t want_author, want_date, want_log;
      want_author = want_date = want_log = FALSE;
      for (i = 0; i < rb->revprops->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(rb->revprops, i, const char *);
          svn_string_t *value;

          /* If a standard revprop is requested, we know it is already in
             log_entry->revprops if available. */
          if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0)
            {
              want_author = TRUE;
              continue;
            }
          if (strcmp(name, SVN_PROP_REVISION_DATE) == 0)
            {
              want_date = TRUE;
              continue;
            }
          if (strcmp(name, SVN_PROP_REVISION_LOG) == 0)
            {
              want_log = TRUE;
              continue;
            }

          if (rb->ra_session == NULL)
            SVN_ERR(svn_client_open_ra_session2(&rb->ra_session,
                                                rb->ra_session_url, NULL,
                                                rb->ctx, rb->ra_session_pool,
                                                pool));

          SVN_ERR(svn_ra_rev_prop(rb->ra_session, log_entry->revision,
                                  name, &value, pool));
          if (log_entry->revprops == NULL)
            log_entry->revprops = apr_hash_make(pool);
          svn_hash_sets(log_entry->revprops, name, value);
        }
      if (log_entry->revprops)
        {
          /* Pre-1.5 servers send the standard revprops unconditionally;
             clear those the caller doesn't want. */
          if (!want_author)
            svn_hash_sets(log_entry->revprops, SVN_PROP_REVISION_AUTHOR, NULL);
          if (!want_date)
            svn_hash_sets(log_entry->revprops, SVN_PROP_REVISION_DATE, NULL);
          if (!want_log)
            svn_hash_sets(log_entry->revprops, SVN_PROP_REVISION_LOG, NULL);
        }
    }
  else
    {
      if (rb->ra_session == NULL)
        SVN_ERR(svn_client_open_ra_session2(&rb->ra_session,
                                            rb->ra_session_url, NULL,
                                            rb->ctx, rb->ra_session_pool,
                                            pool));

      SVN_ERR(svn_ra_rev_proplist(rb->ra_session, log_entry->revision,
                                  &log_entry->revprops, pool));
    }

  return rb->receiver(rb->baton, log_entry, pool);
}

/* limit receiver */
typedef struct limit_receiver_baton_t
{
  int limit;
  svn_log_entry_receiver_t receiver;
  void *baton;
} limit_receiver_baton_t;

static svn_error_t *
limit_receiver(void *baton, svn_log_entry_t *log_entry, apr_pool_t *pool)
{
  limit_receiver_baton_t *rb = baton;

  rb->limit--;

  return rb->receiver(rb->baton, log_entry, pool);
}

/* Resolve the URLs or WC path in TARGETS as per the svn_client_log5 API.

   The limitations on TARGETS specified by svn_client_log5 are enforced here.
   So TARGETS can only contain a single WC path or a URL and zero or more
   relative paths -- anything else will raise an error.

   PEG_REVISION, TARGETS, and CTX are as per svn_client_log5.

   If TARGETS contains a single WC path then set *RA_TARGET to the absolute
   path of that single path if PEG_REVISION is dependent on the working copy
   (e.g. PREV).  Otherwise set *RA_TARGET to the corresponding URL for the
   single WC path.  Set *RELATIVE_TARGETS to an array with a single
   element "".

   If TARGETS contains only a single URL, then set *RA_TARGET to a copy of
   that URL and *RELATIVE_TARGETS to an array with a single element "".

   If TARGETS contains a single URL and one or more relative paths, then
   set *RA_TARGET to a copy of that URL and *RELATIVE_TARGETS to a copy of
   each relative path after the URL.

   If *PEG_REVISION is svn_opt_revision_unspecified, then *PEG_REVISION is
   set to svn_opt_revision_head for URLs or svn_opt_revision_working for a
   WC path.

   *RA_TARGET and *RELATIVE_TARGETS are allocated in RESULT_POOL. */
static svn_error_t *
resolve_log_targets(apr_array_header_t **relative_targets,
                    const char **ra_target,
                    svn_opt_revision_t *peg_revision,
                    const apr_array_header_t *targets,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  int i;
  svn_boolean_t url_targets;

  /* Per svn_client_log5, TARGETS contains either a URL followed by zero or
     more relative paths, or one working copy path. */
  const char *url_or_path = APR_ARRAY_IDX(targets, 0, const char *);

  /* svn_client_log5 requires at least one target. */
  if (targets->nelts == 0)
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("No valid target found"));

  /* Initialize the output array.  At a minimum, we need room for one
     (possibly empty) relpath.  Otherwise, we have to hold a relpath
     for every item in TARGETS except the first.  */
  *relative_targets = apr_array_make(result_pool,
                                     MAX(1, targets->nelts - 1),
                                     sizeof(const char *));

  if (svn_path_is_url(url_or_path))
    {
      /* An unspecified PEG_REVISION for a URL path defaults
         to svn_opt_revision_head. */
      if (peg_revision->kind == svn_opt_revision_unspecified)
        peg_revision->kind = svn_opt_revision_head;

      /* The logic here is this: If we get passed one argument, we assume
         it is the full URL to a file/dir we want log info for. If we get
         a URL plus some paths, then we assume that the URL is the base,
         and that the paths passed are relative to it.  */
      if (targets->nelts > 1)
        {
          /* We have some paths, let's use them. Start after the URL.  */
          for (i = 1; i < targets->nelts; i++)
            {
              const char *target;

              target = APR_ARRAY_IDX(targets, i, const char *);

              if (svn_path_is_url(target) || svn_dirent_is_absolute(target))
                return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                         _("'%s' is not a relative path"),
                                          target);

              APR_ARRAY_PUSH(*relative_targets, const char *) =
                apr_pstrdup(result_pool, target);
            }
        }
      else
        {
          /* If we have a single URL, then the session will be rooted at
             it, so just send an empty string for the paths we are
             interested in. */
          APR_ARRAY_PUSH(*relative_targets, const char *) = "";
        }

      /* Remember that our targets are URLs. */
      url_targets = TRUE;
    }
  else /* WC path target. */
    {
      const char *target;
      const char *target_abspath;

      url_targets = FALSE;
      if (targets->nelts > 1)
        return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                _("When specifying working copy paths, only "
                                  "one target may be given"));

      /* An unspecified PEG_REVISION for a working copy path defaults
         to svn_opt_revision_working. */
      if (peg_revision->kind == svn_opt_revision_unspecified)
        peg_revision->kind = svn_opt_revision_working;

      /* Get URLs for each target */
      target = APR_ARRAY_IDX(targets, 0, const char *);

      SVN_ERR(svn_dirent_get_absolute(&target_abspath, target, scratch_pool));
      SVN_ERR(svn_wc__node_get_url(&url_or_path, ctx->wc_ctx, target_abspath,
                                   scratch_pool, scratch_pool));
      APR_ARRAY_PUSH(*relative_targets, const char *) = "";
    }

  /* If this is a revision type that requires access to the working copy,
   * we use our initial target path to figure out where to root the RA
   * session, otherwise we use our URL. */
  if (SVN_CLIENT__REVKIND_NEEDS_WC(peg_revision->kind))
    {
      if (url_targets)
        return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                _("PREV, BASE, or COMMITTED revision "
                                  "keywords are invalid for URL"));

      else
        SVN_ERR(svn_dirent_get_absolute(
          ra_target, APR_ARRAY_IDX(targets, 0, const char *), result_pool));
    }
  else
    {
      *ra_target = apr_pstrdup(result_pool, url_or_path);
    }

  return SVN_NO_ERROR;
}

/* Keep track of oldest and youngest opt revs found.

   If REV is younger than *YOUNGEST_REV, or *YOUNGEST_REV is
   svn_opt_revision_unspecified, then set *YOUNGEST_REV equal to REV.

   If REV is older than *OLDEST_REV, or *OLDEST_REV is
   svn_opt_revision_unspecified, then set *OLDEST_REV equal to REV. */
static void
find_youngest_and_oldest_revs(svn_revnum_t *youngest_rev,
                              svn_revnum_t *oldest_rev,
                              svn_revnum_t rev)
{
  /* Is REV younger than YOUNGEST_REV? */
  if (! SVN_IS_VALID_REVNUM(*youngest_rev)
      || rev > *youngest_rev)
    *youngest_rev = rev;

  if (! SVN_IS_VALID_REVNUM(*oldest_rev)
      || rev < *oldest_rev)
    *oldest_rev = rev;
}

typedef struct rev_range_t
{
  svn_revnum_t range_start;
  svn_revnum_t range_end;
} rev_range_t;

/* Convert array of svn_opt_revision_t ranges to an array of svn_revnum_t
   ranges.

   Given a log target URL_OR_ABSPATH@PEG_REV and an array of
   svn_opt_revision_range_t's OPT_REV_RANGES, resolve the opt revs in
   OPT_REV_RANGES to svn_revnum_t's and return these in *REVISION_RANGES, an
   array of rev_range_t *.

   Set *YOUNGEST_REV and *OLDEST_REV to the youngest and oldest revisions
   found in *REVISION_RANGES.

   If the repository needs to be contacted to resolve svn_opt_revision_date or
   svn_opt_revision_head revisions, then the session used to do this is
   RA_SESSION; it must be an open session to any URL in the right repository.
*/
static svn_error_t*
convert_opt_rev_array_to_rev_range_array(
  apr_array_header_t **revision_ranges,
  svn_revnum_t *youngest_rev,
  svn_revnum_t *oldest_rev,
  svn_ra_session_t *ra_session,
  const char *url_or_abspath,
  const apr_array_header_t *opt_rev_ranges,
  const svn_opt_revision_t *peg_rev,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  int i;
  svn_revnum_t head_rev = SVN_INVALID_REVNUM;

  /* Initialize the input/output parameters. */
  *youngest_rev = *oldest_rev = SVN_INVALID_REVNUM;

  /* Convert OPT_REV_RANGES to an array of rev_range_t and find the youngest
     and oldest revision range that spans all of OPT_REV_RANGES. */
  *revision_ranges = apr_array_make(result_pool, opt_rev_ranges->nelts,
                                    sizeof(rev_range_t *));

  for (i = 0; i < opt_rev_ranges->nelts; i++)
    {
      svn_opt_revision_range_t *range;
      rev_range_t *rev_range;
      svn_boolean_t start_same_as_end = FALSE;

      range = APR_ARRAY_IDX(opt_rev_ranges, i, svn_opt_revision_range_t *);

      /* Right now RANGE can be any valid pair of svn_opt_revision_t's.  We
         will now convert all RANGEs in place to the corresponding
         svn_opt_revision_number kind. */
      if ((range->start.kind != svn_opt_revision_unspecified)
          && (range->end.kind == svn_opt_revision_unspecified))
        {
          /* If the user specified exactly one revision, then start rev is
           * set but end is not.  We show the log message for just that
           * revision by making end equal to start.
           *
           * Note that if the user requested a single dated revision, then
           * this will cause the same date to be resolved twice.  The
           * extra code complexity to get around this slight inefficiency
           * doesn't seem worth it, however. */
          range->end = range->start;
        }
      else if (range->start.kind == svn_opt_revision_unspecified)
        {
          /* Default to any specified peg revision.  Otherwise, if the
           * first target is a URL, then we default to HEAD:0.  Lastly,
           * the default is BASE:0 since WC@HEAD may not exist. */
          if (peg_rev->kind == svn_opt_revision_unspecified)
            {
              if (svn_path_is_url(url_or_abspath))
                range->start.kind = svn_opt_revision_head;
              else
                range->start.kind = svn_opt_revision_base;
            }
          else
            range->start = *peg_rev;

          if (range->end.kind == svn_opt_revision_unspecified)
            {
              range->end.kind = svn_opt_revision_number;
              range->end.value.number = 0;
            }
        }

      if ((range->start.kind == svn_opt_revision_unspecified)
          || (range->end.kind == svn_opt_revision_unspecified))
        {
          return svn_error_create
            (SVN_ERR_CLIENT_BAD_REVISION, NULL,
             _("Missing required revision specification"));
        }

      /* Does RANGE describe a single svn_opt_revision_t? */
      if (range->start.kind == range->end.kind)
        {
          if (range->start.kind == svn_opt_revision_number)
            {
              if (range->start.value.number == range->end.value.number)
                start_same_as_end = TRUE;
            }
          else if (range->start.kind == svn_opt_revision_date)
            {
              if (range->start.value.date == range->end.value.date)
                start_same_as_end = TRUE;
            }
          else
            {
              start_same_as_end = TRUE;
            }
        }

      rev_range = apr_palloc(result_pool, sizeof(*rev_range));
      SVN_ERR(svn_client__get_revision_number(
                &rev_range->range_start, &head_rev,
                ctx->wc_ctx, url_or_abspath, ra_session,
                &range->start, scratch_pool));
      if (start_same_as_end)
        rev_range->range_end = rev_range->range_start;
      else
        SVN_ERR(svn_client__get_revision_number(
                  &rev_range->range_end, &head_rev,
                  ctx->wc_ctx, url_or_abspath, ra_session,
                  &range->end, scratch_pool));

      /* Possibly update the oldest and youngest revisions requested. */
      find_youngest_and_oldest_revs(youngest_rev,
                                    oldest_rev,
                                    rev_range->range_start);
      find_youngest_and_oldest_revs(youngest_rev,
                                    oldest_rev,
                                    rev_range->range_end);
      APR_ARRAY_PUSH(*revision_ranges, rev_range_t *) = rev_range;
    }

  return SVN_NO_ERROR;
}

static int
compare_rev_to_segment(const void *key_p,
                       const void *element_p)
{
  svn_revnum_t rev =
    * (svn_revnum_t *)key_p;
  const svn_location_segment_t *segment =
    *((const svn_location_segment_t * const *) element_p);

  if (rev < segment->range_start)
    return -1;
  else if (rev > segment->range_end)
    return 1;
  else
    return 0;
}

/* Run svn_ra_get_log2 for PATHS, one or more paths relative to RA_SESSION's
   common parent, for each revision in REVISION_RANGES, an array of
   rev_range_t.

   RA_SESSION is an open session pointing to ACTUAL_LOC.

   LOG_SEGMENTS is an array of svn_location_segment_t * items representing the
   history of PATHS from the oldest to youngest revisions found in
   REVISION_RANGES.

   The TARGETS, LIMIT, DISCOVER_CHANGED_PATHS, STRICT_NODE_HISTORY,
   INCLUDE_MERGED_REVISIONS, REVPROPS, REAL_RECEIVER, and REAL_RECEIVER_BATON
   parameters are all as per the svn_client_log5 API. */
static svn_error_t *
run_ra_get_log(apr_array_header_t *revision_ranges,
               apr_array_header_t *paths,
               apr_array_header_t *log_segments,
               svn_client__pathrev_t *actual_loc,
               svn_ra_session_t *ra_session,
               /* The following are as per svn_client_log5. */
               const apr_array_header_t *targets,
               int limit,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_boolean_t include_merged_revisions,
               const apr_array_header_t *revprops,
               svn_log_entry_receiver_t real_receiver,
               void *real_receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  int i;
  pre_15_receiver_baton_t rb = {0};
  apr_pool_t *iterpool;
  svn_boolean_t has_log_revprops;

  SVN_ERR(svn_ra_has_capability(ra_session, &has_log_revprops,
                                SVN_RA_CAPABILITY_LOG_REVPROPS,
                                scratch_pool));

  if (!has_log_revprops)
    {
      /* See above pre-1.5 notes. */
      rb.ctx = ctx;

      /* Create ra session on first use */
      rb.ra_session_pool = scratch_pool;
      rb.ra_session_url = actual_loc->url;
    }

  /* It's a bit complex to correctly handle the special revision words
   * such as "BASE", "COMMITTED", and "PREV".  For example, if the
   * user runs
   *
   *   $ svn log -rCOMMITTED foo.txt bar.c
   *
   * which committed rev should be used?  The younger of the two?  The
   * first one?  Should we just error?
   *
   * None of the above, I think.  Rather, the committed rev of each
   * target in turn should be used.  This is what most users would
   * expect, and is the most useful interpretation.  Of course, this
   * goes for the other dynamic (i.e., local) revision words too.
   *
   * Note that the code to do this is a bit more complex than a simple
   * loop, because the user might run
   *
   *    $ svn log -rCOMMITTED:42 foo.txt bar.c
   *
   * in which case we want to avoid recomputing the static revision on
   * every iteration.
   *
   * ### FIXME: However, we can't yet handle multiple wc targets anyway.
   *
   * We used to iterate over each target in turn, getting the logs for
   * the named range.  This led to revisions being printed in strange
   * order or being printed more than once.  This is issue 1550.
   *
   * In r851673, jpieper blocked multiple wc targets in svn/log-cmd.c,
   * meaning this block not only doesn't work right in that case, but isn't
   * even testable that way (svn has no unit test suite; we can only test
   * via the svn command).  So, that check is now moved into this function
   * (see above).
   *
   * kfogel ponders future enhancements in r844260:
   * I think that's okay behavior, since the sense of the command is
   * that one wants a particular range of logs for *this* file, then
   * another range for *that* file, and so on.  But we should
   * probably put some sort of separator header between the log
   * groups.  Of course, libsvn_client can't just print stuff out --
   * it has to take a callback from the client to do that.  So we
   * need to define that callback interface, then have the command
   * line client pass one down here.
   *
   * epg wonders if the repository could send a unified stream of log
   * entries if the paths and revisions were passed down.
   */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < revision_ranges->nelts; i++)
    {
      const char *old_session_url;
      const char *path = APR_ARRAY_IDX(targets, 0, const char *);
      const char *local_abspath_or_url;
      rev_range_t *range;
      limit_receiver_baton_t lb;
      svn_log_entry_receiver_t passed_receiver;
      void *passed_receiver_baton;
      const apr_array_header_t *passed_receiver_revprops;
      svn_location_segment_t **matching_segment;
      svn_revnum_t younger_rev;

      svn_pool_clear(iterpool);

      if (!svn_path_is_url(path))
        SVN_ERR(svn_dirent_get_absolute(&local_abspath_or_url, path,
                                        iterpool));
      else
        local_abspath_or_url = path;

      range = APR_ARRAY_IDX(revision_ranges, i, rev_range_t *);

      /* Issue #4355: Account for renames spanning requested
         revision ranges. */
      younger_rev = MAX(range->range_start, range->range_end);
      matching_segment = bsearch(&younger_rev, log_segments->elts,
                                 log_segments->nelts, log_segments->elt_size,
                                 compare_rev_to_segment);
      /* LOG_SEGMENTS is supposed to represent the history of PATHS from
         the oldest to youngest revs in REVISION_RANGES.  This function's
         current sole caller svn_client_log5 *should* be providing
         LOG_SEGMENTS that span the oldest to youngest revs in
         REVISION_RANGES, even if one or more of the svn_location_segment_t's
         returned have NULL path members indicating a gap in the history. So
         MATCHING_SEGMENT should never be NULL, but clearly sometimes it is,
         see http://svn.haxx.se/dev/archive-2013-06/0522.shtml
         So to be safe we handle that case. */
      if (matching_segment == NULL)
        continue;

      /* A segment with a NULL path means there is gap in the history.
         We'll just proceed and let svn_ra_get_log2 fail with a useful
         error...*/
      if ((*matching_segment)->path != NULL)
        {
          /* ...but if there is history, then we must account for issue
             #4355 and make sure our RA session is pointing at the correct
             location. */
          const char *segment_url = svn_path_url_add_component2(
            actual_loc->repos_root_url, (*matching_segment)->path,
            scratch_pool);
          SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url,
                                                    ra_session,
                                                    segment_url,
                                                    scratch_pool));
        }

      if (has_log_revprops)
        {
          passed_receiver = real_receiver;
          passed_receiver_baton = real_receiver_baton;
          passed_receiver_revprops = revprops;
        }
      else
        {
          rb.revprops = revprops;
          rb.receiver = real_receiver;
          rb.baton = real_receiver_baton;

          passed_receiver = pre_15_receiver;
          passed_receiver_baton = &rb;
          passed_receiver_revprops = svn_compat_log_revprops_in(iterpool);
        }

      if (limit && revision_ranges->nelts > 1)
        {
          lb.limit = limit;
          lb.receiver = passed_receiver;
          lb.baton = passed_receiver_baton;

          passed_receiver = limit_receiver;
          passed_receiver_baton = &lb;
        }

      SVN_ERR(svn_ra_get_log2(ra_session,
                              paths,
                              range->range_start,
                              range->range_end,
                              limit,
                              discover_changed_paths,
                              strict_node_history,
                              include_merged_revisions,
                              passed_receiver_revprops,
                              passed_receiver,
                              passed_receiver_baton,
                              iterpool));

      if (limit && revision_ranges->nelts > 1)
        {
          limit = lb.limit;
          if (limit == 0)
            {
              return SVN_NO_ERROR;
            }
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/*** Public Interface. ***/

svn_error_t *
svn_client_log5(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const apr_array_header_t *opt_rev_ranges,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t real_receiver,
                void *real_receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *old_session_url;
  const char *ra_target;
  const char *path_or_url;
  svn_opt_revision_t youngest_opt_rev;
  svn_revnum_t youngest_rev;
  svn_revnum_t oldest_rev;
  svn_opt_revision_t peg_rev;
  svn_client__pathrev_t *ra_session_loc;
  svn_client__pathrev_t *actual_loc;
  apr_array_header_t *log_segments;
  apr_array_header_t *revision_ranges;
  apr_array_header_t *relative_targets;

  if (opt_rev_ranges->nelts == 0)
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, NULL,
         _("Missing required revision specification"));
    }

  /* Make a copy of PEG_REVISION, we may need to change it to a
     default value. */
  peg_rev = *peg_revision;

  SVN_ERR(resolve_log_targets(&relative_targets, &ra_target, &peg_rev,
                              targets, ctx, pool, pool));

  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &ra_session_loc,
                                            ra_target, NULL, &peg_rev, &peg_rev,
                                            ctx, pool));

  /* Convert OPT_REV_RANGES to an array of rev_range_t and find the youngest
     and oldest revision range that spans all of OPT_REV_RANGES. */
  SVN_ERR(convert_opt_rev_array_to_rev_range_array(&revision_ranges,
                                                   &youngest_rev,
                                                   &oldest_rev,
                                                   ra_session,
                                                   ra_target,
                                                   opt_rev_ranges, &peg_rev,
                                                   ctx, pool,  pool));

  /* For some peg revisions we must resolve revision and url via a local path
     so use the original RA_TARGET. For others, use the potentially corrected
     (redirected) ra session URL. */
  if (peg_rev.kind == svn_opt_revision_previous ||
      peg_rev.kind == svn_opt_revision_base ||
      peg_rev.kind == svn_opt_revision_committed ||
      peg_rev.kind == svn_opt_revision_working)
    path_or_url = ra_target;
  else
    path_or_url = ra_session_loc->url;

  /* Make ACTUAL_LOC and RA_SESSION point to the youngest operative rev. */
  youngest_opt_rev.kind = svn_opt_revision_number;
  youngest_opt_rev.value.number = youngest_rev;
  SVN_ERR(svn_client__resolve_rev_and_url(&actual_loc, ra_session,
                                          path_or_url, &peg_rev,
                                          &youngest_opt_rev, ctx, pool));
  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                            actual_loc->url, pool));

  /* Save us an RA layer round trip if we are on the repository root and
     know the result in advance, or if we don't need multiple ranges.
     All the revision data has already been validated.
   */
  if (strcmp(actual_loc->url, actual_loc->repos_root_url) == 0
      || opt_rev_ranges->nelts <= 1)
    {
      svn_location_segment_t *segment = apr_pcalloc(pool, sizeof(*segment));
      log_segments = apr_array_make(pool, 1, sizeof(segment));

      segment->range_start = oldest_rev;
      segment->range_end = actual_loc->rev;
      segment->path = svn_uri_skip_ancestor(actual_loc->repos_root_url,
                                            actual_loc->url, pool);
      APR_ARRAY_PUSH(log_segments, svn_location_segment_t *) = segment;
    }
  else
    {
      /* Get the svn_location_segment_t's representing the requested log
       * ranges. */
      SVN_ERR(svn_client__repos_location_segments(&log_segments, ra_session,
                                                  actual_loc->url,
                                                  actual_loc->rev, /* peg */
                                                  actual_loc->rev, /* start */
                                                  oldest_rev,      /* end */
                                                  ctx, pool));
    }


  SVN_ERR(run_ra_get_log(revision_ranges, relative_targets, log_segments,
                         actual_loc, ra_session, targets, limit,
                         discover_changed_paths, strict_node_history,
                         include_merged_revisions, revprops,
                         real_receiver, real_receiver_baton, ctx, pool));

  return SVN_NO_ERROR;
}
