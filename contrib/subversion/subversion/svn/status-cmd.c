/*
 * status-cmd.c -- Display status information in current directory
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_hash.h"
#include "svn_string.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_xml.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_cmdline.h"
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



/*** Code. ***/

struct status_baton
{
  /* These fields all correspond to the ones in the
     svn_cl__print_status() interface. */
  const char *target_abspath;
  const char *target_path;
  svn_boolean_t suppress_externals_placeholders;
  svn_boolean_t detailed;
  svn_boolean_t show_last_committed;
  svn_boolean_t skip_unrecognized;
  svn_boolean_t repos_locks;

  apr_hash_t *cached_changelists;
  apr_pool_t *cl_pool;          /* where cached changelists are allocated */

  svn_boolean_t had_print_error;  /* To avoid printing lots of errors if we get
                                     errors while printing to stdout */
  svn_boolean_t xml_mode;

  /* Conflict stats. */
  unsigned int text_conflicts;
  unsigned int prop_conflicts;
  unsigned int tree_conflicts;

  svn_client_ctx_t *ctx;
};


struct status_cache
{
  const char *path;
  const char *target_abspath;
  const char *target_path;
  svn_client_status_t *status;
};

/* Print conflict stats accumulated in status baton SB.
 * Do temporary allocations in POOL. */
static svn_error_t *
print_conflict_stats(struct status_baton *sb, apr_pool_t *pool)
{
  if (sb->text_conflicts > 0 || sb->prop_conflicts > 0 ||
      sb->tree_conflicts > 0)
      SVN_ERR(svn_cmdline_printf(pool, "%s", _("Summary of conflicts:\n")));

  if (sb->text_conflicts > 0)
    SVN_ERR(svn_cmdline_printf
      (pool, _("  Text conflicts: %u\n"), sb->text_conflicts));

  if (sb->prop_conflicts > 0)
    SVN_ERR(svn_cmdline_printf
      (pool, _("  Property conflicts: %u\n"), sb->prop_conflicts));

  if (sb->tree_conflicts > 0)
    SVN_ERR(svn_cmdline_printf
      (pool, _("  Tree conflicts: %u\n"), sb->tree_conflicts));

  return SVN_NO_ERROR;
}

/* Prints XML target element with path attribute TARGET, using POOL for
   temporary allocations. */
static svn_error_t *
print_start_target_xml(const char *target, apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);

  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "target",
                        "path", target, SVN_VA_NULL);

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* Finish a target element by optionally printing an against element if
 * REPOS_REV is a valid revision number, and then printing an target end tag.
 * Use POOL for temporary allocations. */
static svn_error_t *
print_finish_target_xml(svn_revnum_t repos_rev,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);

  if (SVN_IS_VALID_REVNUM(repos_rev))
    {
      const char *repos_rev_str;
      repos_rev_str = apr_psprintf(pool, "%ld", repos_rev);
      svn_xml_make_open_tag(&sb, pool, svn_xml_self_closing, "against",
                            "revision", repos_rev_str, SVN_VA_NULL);
    }

  svn_xml_make_close_tag(&sb, pool, "target");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* Function which *actually* causes a status structure to be output to
   the user.  Called by both print_status() and svn_cl__status(). */
static svn_error_t *
print_status_normal_or_xml(void *baton,
                           const char *path,
                           const svn_client_status_t *status,
                           apr_pool_t *pool)
{
  struct status_baton *sb = baton;

  if (sb->xml_mode)
    return svn_cl__print_status_xml(sb->target_abspath, sb->target_path,
                                    path, status, sb->ctx, pool);
  else
    return svn_cl__print_status(sb->target_abspath, sb->target_path,
                                path, status,
                                sb->suppress_externals_placeholders,
                                sb->detailed,
                                sb->show_last_committed,
                                sb->skip_unrecognized,
                                sb->repos_locks,
                                &sb->text_conflicts,
                                &sb->prop_conflicts,
                                &sb->tree_conflicts,
                                sb->ctx,
                                pool);
}


/* A status callback function for printing STATUS for PATH. */
static svn_error_t *
print_status(void *baton,
             const char *path,
             const svn_client_status_t *status,
             apr_pool_t *pool)
{
  struct status_baton *sb = baton;
  const char *local_abspath = status->local_abspath;

  /* ### The revision information with associates are based on what
   * ### _read_info() returns. The svn_wc_status_func4_t callback is
   * ### suppposed to handle the gathering of additional information from the
   * ### WORKING nodes on its own. Until we've agreed on how the CLI should
   * ### handle the revision information, we use this approach to stay compat
   * ### with our testsuite. */
  if (status->versioned
      && !SVN_IS_VALID_REVNUM(status->revision)
      && !status->copied
      && (status->node_status == svn_wc_status_deleted
          || status->node_status == svn_wc_status_replaced))
    {
      svn_client_status_t *twks = svn_client_status_dup(status, sb->cl_pool);

      /* Copied is FALSE, so either we have a local addition, or we have
         a delete that directly shadows a BASE node */

      switch(status->node_status)
        {
          case svn_wc_status_replaced:
            /* Just retrieve the revision below the replacement.
               The other fields are filled by a copy.
               (With ! copied, we know we have a BASE node)

               ### Is this really what we want to provide? */
            SVN_ERR(svn_wc__node_get_pre_ng_status_data(&twks->revision,
                                                        NULL, NULL, NULL,
                                                        sb->ctx->wc_ctx,
                                                        local_abspath,
                                                        sb->cl_pool, pool));
            break;
          case svn_wc_status_deleted:
            /* Retrieve some data from the original version below the delete */
            SVN_ERR(svn_wc__node_get_pre_ng_status_data(&twks->revision,
                                                        &twks->changed_rev,
                                                        &twks->changed_date,
                                                        &twks->changed_author,
                                                        sb->ctx->wc_ctx,
                                                        local_abspath,
                                                        sb->cl_pool, pool));
            break;

          default:
            /* This space intentionally left blank. */
            break;
        }

      status = twks;
    }

  /* If the path is part of a changelist, then we don't print
     the item, but instead dup & cache the status structure for later. */
  if (status->changelist)
    {
      /* The hash maps a changelist name to an array of status_cache
         structures. */
      apr_array_header_t *path_array;
      const char *cl_key = apr_pstrdup(sb->cl_pool, status->changelist);
      struct status_cache *scache = apr_pcalloc(sb->cl_pool, sizeof(*scache));
      scache->path = apr_pstrdup(sb->cl_pool, path);
      scache->target_abspath = apr_pstrdup(sb->cl_pool, sb->target_abspath);
      scache->target_path = apr_pstrdup(sb->cl_pool, sb->target_path);
      scache->status = svn_client_status_dup(status, sb->cl_pool);

      path_array =
        svn_hash_gets(sb->cached_changelists, cl_key);
      if (path_array == NULL)
        {
          path_array = apr_array_make(sb->cl_pool, 1,
                                      sizeof(struct status_cache *));
          svn_hash_sets(sb->cached_changelists, cl_key, path_array);
        }

      APR_ARRAY_PUSH(path_array, struct status_cache *) = scache;
      return SVN_NO_ERROR;
    }

  return print_status_normal_or_xml(baton, path, status, pool);
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__status(apr_getopt_t *os,
               void *baton,
               apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *iterpool;
  apr_hash_t *master_cl_hash = apr_hash_make(scratch_pool);
  int i;
  svn_opt_revision_t rev;
  struct status_baton sb;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, scratch_pool);

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  /* We want our -u statuses to be against HEAD by default. */
  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    rev.kind = svn_opt_revision_head;
  else if (! opt_state->update)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                        _("--revision (-r) option valid only with "
                          "--show-updates (-u) option"));
  else
    rev = opt_state->start_revision;

  sb.had_print_error = FALSE;

  if (opt_state->xml)
    {
      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("status", scratch_pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'incremental' option only valid in XML "
                                  "mode"));
    }

  sb.suppress_externals_placeholders = (opt_state->quiet
                                        && (! opt_state->verbose));
  sb.detailed = (opt_state->verbose || opt_state->update);
  sb.show_last_committed = opt_state->verbose;
  sb.skip_unrecognized = opt_state->quiet;
  sb.repos_locks = opt_state->update;
  sb.xml_mode = opt_state->xml;
  sb.cached_changelists = master_cl_hash;
  sb.cl_pool = scratch_pool;
  sb.text_conflicts = 0;
  sb.prop_conflicts = 0;
  sb.tree_conflicts = 0;
  sb.ctx = ctx;

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      svn_revnum_t repos_rev = SVN_INVALID_REVNUM;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_dirent_get_absolute(&(sb.target_abspath), target,
                                      scratch_pool));
      sb.target_path = target;

      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      if (opt_state->xml)
        SVN_ERR(print_start_target_xml(svn_dirent_local_style(target, iterpool),
                                       iterpool));

      /* Retrieve a hash of status structures with the information
         requested by the user. */
      SVN_ERR(svn_cl__try(svn_client_status6(&repos_rev, ctx, target, &rev,
                                             opt_state->depth,
                                             opt_state->verbose,
                                             opt_state->update,
                                             TRUE /* check_working_copy */,
                                             opt_state->no_ignore,
                                             opt_state->ignore_externals,
                                             FALSE /* depth_as_sticky */,
                                             opt_state->changelists,
                                             print_status, &sb,
                                             iterpool),
                          NULL, opt_state->quiet,
                          /* not versioned: */
                          SVN_ERR_WC_NOT_WORKING_COPY,
                          SVN_ERR_WC_PATH_NOT_FOUND,
                          0));

      if (opt_state->xml)
        SVN_ERR(print_finish_target_xml(repos_rev, iterpool));
    }

  /* If any paths were cached because they were associated with
     changelists, we can now display them as grouped changelists. */
  if (apr_hash_count(master_cl_hash) > 0)
    {
      apr_hash_index_t *hi;
      svn_stringbuf_t *buf;

      if (opt_state->xml)
        buf = svn_stringbuf_create_empty(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, master_cl_hash); hi;
           hi = apr_hash_next(hi))
        {
          const char *changelist_name = apr_hash_this_key(hi);
          apr_array_header_t *path_array = apr_hash_this_val(hi);
          int j;

          /* ### TODO: For non-XML output, we shouldn't print the
             ### leading \n on the first changelist if there were no
             ### non-changelist entries. */
          if (opt_state->xml)
            {
              svn_stringbuf_setempty(buf);
              svn_xml_make_open_tag(&buf, scratch_pool, svn_xml_normal,
                                    "changelist", "name", changelist_name,
                                    SVN_VA_NULL);
              SVN_ERR(svn_cl__error_checked_fputs(buf->data, stdout));
            }
          else
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                                       _("\n--- Changelist '%s':\n"),
                                       changelist_name));

          for (j = 0; j < path_array->nelts; j++)
            {
              struct status_cache *scache =
                APR_ARRAY_IDX(path_array, j, struct status_cache *);
              sb.target_abspath = scache->target_abspath;
              sb.target_path = scache->target_path;
              SVN_ERR(print_status_normal_or_xml(&sb, scache->path,
                                                 scache->status, scratch_pool));
            }

          if (opt_state->xml)
            {
              svn_stringbuf_setempty(buf);
              svn_xml_make_close_tag(&buf, scratch_pool, "changelist");
              SVN_ERR(svn_cl__error_checked_fputs(buf->data, stdout));
            }
        }
    }
  svn_pool_destroy(iterpool);

  if (opt_state->xml && (! opt_state->incremental))
    SVN_ERR(svn_cl__xml_print_footer("status", scratch_pool));

  if (! opt_state->quiet && ! opt_state->xml)
      SVN_ERR(print_conflict_stats(&sb, scratch_pool));

  return SVN_NO_ERROR;
}
