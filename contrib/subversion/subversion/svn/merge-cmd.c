/*
 * merge-cmd.c -- Merging changes into a working copy.
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

#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"
#include "private/svn_client_private.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Throw an error if PATH_OR_URL is a path and REVISION isn't a repository
 * revision. */
static svn_error_t *
ensure_wc_path_has_repo_revision(const char *path_or_url,
                                 const svn_opt_revision_t *revision,
                                 apr_pool_t *scratch_pool)
{
  if (revision->kind != svn_opt_revision_number
      && revision->kind != svn_opt_revision_date
      && revision->kind != svn_opt_revision_head
      && ! svn_path_is_url(path_or_url))
    return svn_error_createf(
      SVN_ERR_CLIENT_BAD_REVISION, NULL,
      _("Invalid merge source '%s'; a working copy path can only be "
        "used with a repository revision (a number, a date, or head)"),
      svn_dirent_local_style(path_or_url, scratch_pool));
  return SVN_NO_ERROR;
}

/* Run a merge.
 *
 * (No docs yet, as this code was just hoisted out of svn_cl__merge().)
 *
 * Having FIRST_RANGE_START/_END params is ugly -- we should be able to use
 * PEG_REVISION1/2 and/or RANGES_TO_MERGE instead, maybe adjusting the caller.
 */
static svn_error_t *
run_merge(svn_boolean_t two_sources_specified,
          const char *sourcepath1,
          svn_opt_revision_t peg_revision1,
          const char *sourcepath2,
          const char *targetpath,
          apr_array_header_t *ranges_to_merge,
          svn_opt_revision_t first_range_start,
          svn_opt_revision_t first_range_end,
          svn_cl__opt_state_t *opt_state,
          apr_array_header_t *options,
          svn_client_ctx_t *ctx,
          apr_pool_t *scratch_pool)
{
  svn_error_t *merge_err;

  if (opt_state->reintegrate)
    {
      merge_err = svn_cl__deprecated_merge_reintegrate(
                    sourcepath1, &peg_revision1, targetpath,
                    opt_state->dry_run, options, ctx, scratch_pool);
    }
  else if (! two_sources_specified)
    {
      /* If we don't have at least one valid revision range, pick a
         good one that spans the entire set of revisions on our
         source. */
      if ((first_range_start.kind == svn_opt_revision_unspecified)
          && (first_range_end.kind == svn_opt_revision_unspecified))
        {
          ranges_to_merge = NULL;
        }

      if (opt_state->verbose)
        SVN_ERR(svn_cmdline_printf(scratch_pool, _("--- Merging\n")));
      merge_err = svn_client_merge_peg5(sourcepath1,
                                        ranges_to_merge,
                                        &peg_revision1,
                                        targetpath,
                                        opt_state->depth,
                                        opt_state->ignore_ancestry,
                                        opt_state->ignore_ancestry,
                                        opt_state->force, /* force_delete */
                                        opt_state->record_only,
                                        opt_state->dry_run,
                                        opt_state->allow_mixed_rev,
                                        options,
                                        ctx,
                                        scratch_pool);
    }
  else
    {
      if (svn_path_is_url(sourcepath1) != svn_path_is_url(sourcepath2))
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Merge sources must both be "
                                  "either paths or URLs"));

      if (svn_path_is_url(targetpath))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Merge target '%s' must be a local path "
                                   "but looks like a URL"), targetpath);

      if (opt_state->verbose)
        SVN_ERR(svn_cmdline_printf(scratch_pool, _("--- Merging\n")));
      merge_err = svn_client_merge5(sourcepath1,
                                    &first_range_start,
                                    sourcepath2,
                                    &first_range_end,
                                    targetpath,
                                    opt_state->depth,
                                    opt_state->ignore_ancestry,
                                    opt_state->ignore_ancestry,
                                    opt_state->force, /* force_delete */
                                    opt_state->record_only,
                                    opt_state->dry_run,
                                    opt_state->allow_mixed_rev,
                                    options,
                                    ctx,
                                    scratch_pool);
    }

  return merge_err;
}

/* Baton type for conflict_func_merge_cmd(). */
struct conflict_func_merge_cmd_baton {
  svn_cl__accept_t accept_which;
  const char *path_prefix;
  svn_cl__conflict_stats_t *conflict_stats;
};

/* This implements the `svn_wc_conflict_resolver_func2_t ' interface.
 *
 * The merge subcommand needs to install this legacy conflict callback
 * in case the user passed an --accept option to 'svn merge'.
 * Otherwise, merges involving multiple editor drives might encounter a
 * conflict during one of the editor drives and abort with an error,
 * rather than resolving conflicts as per the --accept option and
 * continuing with the next editor drive.
 * ### TODO add an svn_client_merge API that makes this callback unnecessary
 */
static svn_error_t *
conflict_func_merge_cmd(svn_wc_conflict_result_t **result,
                        const svn_wc_conflict_description2_t *desc,
                        void *baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  struct conflict_func_merge_cmd_baton *b = baton;
  svn_wc_conflict_choice_t choice;

  switch (b->accept_which)
    {
    case svn_cl__accept_postpone:
    case svn_cl__accept_invalid:
    case svn_cl__accept_unspecified:
    case svn_cl__accept_recommended:
      /* Postpone or no valid --accept option, postpone the conflict. */
      choice = svn_wc_conflict_choose_postpone;
      break;
    case svn_cl__accept_base:
      choice = svn_wc_conflict_choose_base;
      break;
    case svn_cl__accept_working:
      choice = svn_wc_conflict_choose_merged;
      break;
    case svn_cl__accept_mine_conflict:
      choice = svn_wc_conflict_choose_mine_conflict;
      break;
    case svn_cl__accept_theirs_conflict:
      choice = svn_wc_conflict_choose_theirs_conflict;
      break;
    case svn_cl__accept_mine_full:
      choice = svn_wc_conflict_choose_mine_full;
      break;
    case svn_cl__accept_theirs_full:
      choice = svn_wc_conflict_choose_theirs_full;
      break;
    case svn_cl__accept_edit:
    case svn_cl__accept_launch:
      /* The 'edit' and 'launch' options used to be valid in Subversion 1.9 but
       * we can't support these options for the purposes of this callback. */
      choice = svn_wc_conflict_choose_postpone;
      break;
    }

  *result = svn_wc_create_conflict_result(choice, NULL, result_pool);

  /* If we are resolving a conflict, adjust the summary of conflicts. */
  if (choice != svn_wc_conflict_choose_postpone)
    {
      const char *local_path;

      local_path = svn_cl__local_style_skip_ancestor(b->path_prefix,
                                                     desc->local_abspath,
                                                     scratch_pool);
      svn_cl__conflict_stats_resolved(b->conflict_stats, local_path,
                                      desc->kind);
    }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__merge(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_cl__conflict_stats_t *conflict_stats =
    ((svn_cl__cmd_baton_t *) baton)->conflict_stats;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *sourcepath1 = NULL, *sourcepath2 = NULL, *targetpath = "";
  svn_boolean_t two_sources_specified = TRUE;
  svn_error_t *merge_err;
  svn_opt_revision_t first_range_start, first_range_end, peg_revision1,
    peg_revision2;
  apr_array_header_t *options, *ranges_to_merge = opt_state->revision_ranges;
  apr_array_header_t *conflicted_paths;
  svn_boolean_t has_explicit_target = FALSE;

  /* Merge doesn't support specifying a revision or revision range
     when using --reintegrate. */
  if (opt_state->reintegrate
      && opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("-r and -c can't be used with --reintegrate"));
    }

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* For now, we require at least one source.  That may change in
     future versions of Subversion, for example if we have support for
     negated mergeinfo.  See this IRC conversation:

       <bhuvan>   kfogel: yeah, i think you are correct; we should
                  specify the source url

       <kfogel>   bhuvan: I'll change the help output and propose for
                  backport.  Thanks.

       <bhuvan>   kfogel: np; while we are at it, 'svn merge' simply
                  returns nothing; i think we should say: """svn: Not
                  enough arguments provided; try 'svn help' for more
                  info"""

       <kfogel>   good idea

       <kfogel>   (in the future, 'svn merge' might actually do
                  something, but that's all the more reason to make
                  sure it errors now)

       <cmpilato> actually, i'm pretty sure 'svn merge' does something

       <cmpilato> it says "please merge any unmerged changes from
                  myself to myself."

       <cmpilato> :-)

       <kfogel>   har har

       <cmpilato> kfogel: i was serious.

       <kfogel>   cmpilato: urrr, uh.  Is that meaningful?  Is there
                  ever a reason for a user to run it?

       <cmpilato> kfogel: not while we don't have support for negated
                  mergeinfo.

       <kfogel>   cmpilato: do you concur that until it does something
                  useful it should error?

       <cmpilato> kfogel: yup.

       <kfogel>   cool
  */
  if (targets->nelts < 1)
    {
      return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                              _("Merge source required"));
    }
  else  /* Parse at least one, and possible two, sources. */
    {
      SVN_ERR(svn_opt_parse_path(&peg_revision1, &sourcepath1,
                                 APR_ARRAY_IDX(targets, 0, const char *),
                                 pool));
      if (targets->nelts >= 2)
        SVN_ERR(svn_opt_parse_path(&peg_revision2, &sourcepath2,
                                   APR_ARRAY_IDX(targets, 1, const char *),
                                   pool));
    }

  /* We could have one or two sources.  Deliberately written to stay
     correct even if we someday permit implied merge source. */
  if (targets->nelts <= 1)
    {
      two_sources_specified = FALSE;
    }
  else if (targets->nelts == 2)
    {
      if (svn_path_is_url(sourcepath1) && !svn_path_is_url(sourcepath2))
        two_sources_specified = FALSE;
    }

  if (opt_state->revision_ranges->nelts > 0)
    {
      first_range_start = APR_ARRAY_IDX(opt_state->revision_ranges, 0,
                                        svn_opt_revision_range_t *)->start;
      first_range_end = APR_ARRAY_IDX(opt_state->revision_ranges, 0,
                                      svn_opt_revision_range_t *)->end;
    }
  else
    {
      first_range_start.kind = first_range_end.kind =
        svn_opt_revision_unspecified;
    }

  /* If revision_ranges has at least one real range at this point, then
     we know the user must have used the '-r' and/or '-c' switch(es).
     This means we're *not* doing two distinct sources. */
  if (first_range_start.kind != svn_opt_revision_unspecified)
    {
      /* A revision *range* is required. */
      if (first_range_end.kind == svn_opt_revision_unspecified)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                _("Second revision required"));

      two_sources_specified = FALSE;
    }

  if (! two_sources_specified) /* TODO: Switch order of if */
    {
      if (targets->nelts > 2)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      /* Set the default value for unspecified paths and peg revision. */
      /* targets->nelts is 1 ("svn merge SOURCE") or 2 ("svn merge
         SOURCE WCPATH") here. */
      sourcepath2 = sourcepath1;

      if (peg_revision1.kind == svn_opt_revision_unspecified)
        peg_revision1.kind = svn_path_is_url(sourcepath1)
          ? svn_opt_revision_head : svn_opt_revision_working;

      if (targets->nelts == 2)
        {
          targetpath = APR_ARRAY_IDX(targets, 1, const char *);
          has_explicit_target = TRUE;
          if (svn_path_is_url(targetpath))
            return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                    _("Cannot specify a revision range "
                                      "with two URLs"));
        }
    }
  else /* using @rev syntax */
    {
      if (targets->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 3)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      first_range_start = peg_revision1;
      first_range_end = peg_revision2;

      /* Catch 'svn merge wc_path1 wc_path2 [target]' without explicit
         revisions--since it ignores local modifications it may not do what
         the user expects.  That is, it doesn't read from the WC itself, it
         reads from the WC's URL.  Forcing the user to specify a repository
         revision should avoid any confusion. */
      SVN_ERR(ensure_wc_path_has_repo_revision(sourcepath1, &first_range_start,
                                               pool));
      SVN_ERR(ensure_wc_path_has_repo_revision(sourcepath2, &first_range_end,
                                               pool));

      /* Default peg revisions to each URL's youngest revision. */
      if (first_range_start.kind == svn_opt_revision_unspecified)
        first_range_start.kind = svn_opt_revision_head;
      if (first_range_end.kind == svn_opt_revision_unspecified)
        first_range_end.kind = svn_opt_revision_head;

      /* Decide where to apply the delta (defaulting to "."). */
      if (targets->nelts == 3)
        {
          targetpath = APR_ARRAY_IDX(targets, 2, const char *);
          has_explicit_target = TRUE;
        }
    }

  /* If no targetpath was specified, see if we can infer it from the
     sourcepaths. */
  if (! has_explicit_target
      && sourcepath1 && sourcepath2
      && strcmp(targetpath, "") == 0)
    {
      /* If the sourcepath is a URL, it can only refer to a target in
         the current working directory or which is the current working
         directory.  However, if the sourcepath is a local path, it can
         refer to a target somewhere deeper in the directory structure. */
      if (svn_path_is_url(sourcepath1))
        {
          const char *sp1_basename = svn_uri_basename(sourcepath1, pool);
          const char *sp2_basename = svn_uri_basename(sourcepath2, pool);

          if (strcmp(sp1_basename, sp2_basename) == 0)
            {
              const char *target_url;
              const char *target_base;

              SVN_ERR(svn_client_url_from_path2(&target_url, targetpath, ctx,
                                                pool, pool));
              target_base = svn_uri_basename(target_url, pool);

              /* If the basename of the source is the same as the basename of
                 the cwd assume the cwd is the target. */
              if (strcmp(sp1_basename, target_base) != 0)
                {
                  svn_node_kind_t kind;

                  /* If the basename of the source differs from the basename
                     of the target.  We still might assume the cwd is the
                     target, but first check if there is a file in the cwd
                     with the same name as the source basename.  If there is,
                     then assume that file is the target. */
                  SVN_ERR(svn_io_check_path(sp1_basename, &kind, pool));
                  if (kind == svn_node_file)
                    {
                      targetpath = sp1_basename;
                    }
                }
            }
        }
      else if (strcmp(sourcepath1, sourcepath2) == 0)
        {
          svn_node_kind_t kind;

          SVN_ERR(svn_io_check_path(sourcepath1, &kind, pool));
          if (kind == svn_node_file)
            {
              targetpath = sourcepath1;
            }
        }
    }

  if (opt_state->extensions)
    options = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
  else
    options = NULL;

  /* More input validation. */
  if (opt_state->reintegrate)
    {
      if (opt_state->ignore_ancestry)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--reintegrate cannot be used with "
                                  "--ignore-ancestry"));

      if (opt_state->record_only)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--reintegrate cannot be used with "
                                  "--record-only"));

      if (opt_state->depth != svn_depth_unknown)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--depth cannot be used with "
                                  "--reintegrate"));

      if (opt_state->force)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--force cannot be used with "
                                  "--reintegrate"));

      if (two_sources_specified)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--reintegrate can only be used with "
                                  "a single merge source"));
      if (opt_state->allow_mixed_rev)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--allow-mixed-revisions cannot be used "
                                  "with --reintegrate"));
    }

  /* Install a legacy conflict handler if the --accept option was given.
   * Else, svn_client_merge5() may abort the merge in an undesirable way.
   * See the docstring at conflict_func_merge_cmd() for details */
  if (opt_state->accept_which != svn_cl__accept_unspecified)
    {
      struct conflict_func_merge_cmd_baton *b = apr_pcalloc(pool, sizeof(*b));

      b->accept_which = opt_state->accept_which;
      SVN_ERR(svn_dirent_get_absolute(&b->path_prefix, "", pool));
      b->conflict_stats = conflict_stats;

      ctx->conflict_func2 = conflict_func_merge_cmd;
      ctx->conflict_baton2 = b;
    }

retry:
  merge_err = run_merge(two_sources_specified,
                        sourcepath1, peg_revision1,
                        sourcepath2,
                        targetpath,
                        ranges_to_merge, first_range_start, first_range_end,
                        opt_state, options, ctx, pool);
  if (merge_err && merge_err->apr_err
                   == SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING)
    {
      return svn_error_quick_wrap(
               merge_err,
               _("Merge tracking not possible, use --ignore-ancestry or\n"
                 "fix invalid mergeinfo in target with 'svn propset'"));
    }

  /* Run the interactive resolver if conflicts were raised. */
  SVN_ERR(svn_cl__conflict_stats_get_paths(&conflicted_paths, conflict_stats,
                                           pool, pool));
  if (conflicted_paths)
    {
      SVN_ERR(svn_cl__walk_conflicts(conflicted_paths, conflict_stats,
                                     opt_state, ctx, pool));
      if (merge_err &&
          svn_error_root_cause(merge_err)->apr_err == SVN_ERR_WC_FOUND_CONFLICT)
        {
          svn_error_t *err;

          /* Check if all conflicts were resolved just now. */
          err = svn_cl__conflict_stats_get_paths(&conflicted_paths,
                                                 conflict_stats, pool, pool);
          if (err)
            merge_err = svn_error_compose_create(merge_err, err);
          else if (conflicted_paths == NULL)
            {
              svn_error_clear(merge_err);
              goto retry; /* ### conflicts resolved; continue merging */
            }
        }
    }

  if (!opt_state->quiet)
    {
      svn_error_t *err = svn_cl__notifier_print_conflict_stats(
                           ctx->notify_baton2, pool);

      merge_err = svn_error_compose_create(merge_err, err);
    }

  return svn_cl__may_need_force(merge_err);
}
