/*
 * log-cmd.c -- Display log messages
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
#define APR_WANT_STDIO
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_compat.h"
#include "svn_path.h"
#include "svn_props.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_string_private.h"


/*** Code. ***/

/* Baton for log_entry_receiver() and log_entry_receiver_xml(). */
struct log_receiver_baton
{
  /* Client context. */
  svn_client_ctx_t *ctx;

  /* Level of merge revision nesting */
  apr_size_t merge_depth;

  /* collect counters? */
  svn_boolean_t quiet;

  /* total revision counters */
  apr_int64_t revisions;
  apr_int64_t changes;
  apr_int64_t message_lines;

  /* part that came from merges */
  apr_int64_t merges;
  apr_int64_t merged_revs;
  apr_int64_t merged_changes;
  apr_int64_t merged_message_lines;
};


/* Implement `svn_log_entry_receiver_t', printing the logs in
 * a human-readable and machine-parseable format.
 *
 * BATON is of type `struct log_receiver_baton'.
 */
static svn_error_t *
log_entry_receiver(void *baton,
                   svn_log_entry_t *log_entry,
                   apr_pool_t *pool)
{
  struct log_receiver_baton *lb = baton;
  const char *author;
  const char *date;
  const char *message;

  if (lb->ctx->cancel_func)
    SVN_ERR(lb->ctx->cancel_func(lb->ctx->cancel_baton));

  if (! SVN_IS_VALID_REVNUM(log_entry->revision))
    {
      lb->merge_depth--;
      return SVN_NO_ERROR;
    }

  /* if we don't want counters, we are done */
  if (lb->quiet)
    return SVN_NO_ERROR;

  /* extract the message and do all the other counting */
  svn_compat_log_revprops_out(&author, &date, &message, log_entry->revprops);
  if (log_entry->revision == 0 && message == NULL)
    return SVN_NO_ERROR;

  lb->revisions++;
  if (lb->merge_depth)
    lb->merged_revs++;

  if (message != NULL)
    {
      int count = svn_cstring_count_newlines(message) + 1;
      lb->message_lines += count;
      if (lb->merge_depth)
        lb->merged_message_lines += count;
    }

  if (log_entry->changed_paths2)
    {
      unsigned count = apr_hash_count(log_entry->changed_paths2);
      lb->changes += count;
      if (lb->merge_depth)
        lb->merged_changes += count;
    }

  if (log_entry->has_children)
    {
      lb->merge_depth++;
      lb->merges++;
    }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__null_log(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  struct log_receiver_baton lb = { 0 };
  const char *target;
  int i;
  apr_array_header_t *revprops;
  svn_opt_revision_t target_peg_revision;
  const char *target_path_or_url;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  /* Determine if they really want a two-revision range. */
  if (opt_state->used_change_arg)
    {
      if (opt_state->used_revision_arg && opt_state->revision_ranges->nelts > 1)
        {
          return svn_error_create
            (SVN_ERR_CLIENT_BAD_REVISION, NULL,
             _("-c and -r are mutually exclusive"));
        }
      for (i = 0; i < opt_state->revision_ranges->nelts; i++)
        {
          svn_opt_revision_range_t *range;
          range = APR_ARRAY_IDX(opt_state->revision_ranges, i,
                                svn_opt_revision_range_t *);
          if (range->start.value.number < range->end.value.number)
            range->start.value.number++;
          else
            range->end.value.number++;
        }
    }

  /* Parse the first target into path-or-url and peg revision. */
  target = APR_ARRAY_IDX(targets, 0, const char *);
  SVN_ERR(svn_opt_parse_path(&target_peg_revision, &target_path_or_url,
                             target, pool));
  if (target_peg_revision.kind == svn_opt_revision_unspecified)
    target_peg_revision.kind = (svn_path_is_url(target)
                                     ? svn_opt_revision_head
                                     : svn_opt_revision_working);
  APR_ARRAY_IDX(targets, 0, const char *) = target_path_or_url;

  if (svn_path_is_url(target))
    {
      for (i = 1; i < targets->nelts; i++)
        {
          target = APR_ARRAY_IDX(targets, i, const char *);

          if (svn_path_is_url(target) || target[0] == '/')
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Only relative paths can be specified"
                                       " after a URL for 'svnbench log', "
                                       "but '%s' is not a relative path"),
                                     target);
        }
    }

  lb.ctx = ctx;
  lb.quiet = opt_state->quiet;

  revprops = apr_array_make(pool, 3, sizeof(char *));
  if (!opt_state->no_revprops)
    {
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_AUTHOR;
      APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_DATE;
      if (!opt_state->quiet)
        APR_ARRAY_PUSH(revprops, const char *) = SVN_PROP_REVISION_LOG;
    }

  SVN_ERR(svn_client_log5(targets,
                          &target_peg_revision,
                          opt_state->revision_ranges,
                          opt_state->limit,
                          opt_state->verbose,
                          opt_state->stop_on_copy,
                          opt_state->use_merge_history,
                          revprops,
                          log_entry_receiver,
                          &lb,
                          ctx,
                          pool));

  if (!opt_state->quiet)
    {
      if (opt_state->use_merge_history)
        SVN_ERR(svn_cmdline_printf(pool,
                      _("%15s revisions, %15s merged in %s merges\n"
                        "%15s msg lines, %15s in merged revisions\n"
                        "%15s changes,   %15s in merged revisions\n"),
                      svn__ui64toa_sep(lb.revisions, ',', pool),
                      svn__ui64toa_sep(lb.merged_revs, ',', pool),
                      svn__ui64toa_sep(lb.merges, ',', pool),
                      svn__ui64toa_sep(lb.message_lines, ',', pool),
                      svn__ui64toa_sep(lb.merged_message_lines, ',', pool),
                      svn__ui64toa_sep(lb.changes, ',', pool),
                      svn__ui64toa_sep(lb.merged_changes, ',', pool)));
      else
        SVN_ERR(svn_cmdline_printf(pool,
                      _("%15s revisions\n"
                        "%15s msg lines\n"
                        "%15s changes\n"),
                      svn__ui64toa_sep(lb.revisions, ',', pool),
                      svn__ui64toa_sep(lb.message_lines, ',', pool),
                      svn__ui64toa_sep(lb.changes, ',', pool)));
    }

  return SVN_NO_ERROR;
}
