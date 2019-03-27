/*
 * shelve-cmd.c -- Shelve commands.
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

/* We define this here to remove any further warnings about the usage of
   experimental functions in this file. */
#define SVN_EXPERIMENTAL

#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_utf.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_sorts_private.h"


/* First argument should be the name of a shelved change. */
static svn_error_t *
get_name(const char **name,
         apr_getopt_t *os,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  apr_array_header_t *args;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, scratch_pool));
  SVN_ERR(svn_utf_cstring_to_utf8(name,
                                  APR_ARRAY_IDX(args, 0, const char *),
                                  result_pool));
  return SVN_NO_ERROR;
}

/* A comparison function for svn_sort__hash(), comparing the mtime of two
   svn_client_shelved_patch_info_t's. */
static int
compare_shelved_patch_infos_by_mtime(const svn_sort__item_t *a,
                                     const svn_sort__item_t *b)
{
  svn_client_shelved_patch_info_t *a_val = a->value;
  svn_client_shelved_patch_info_t *b_val = b->value;

  return (a_val->dirent->mtime < b_val->dirent->mtime)
           ? -1 : (a_val->dirent->mtime > b_val->dirent->mtime) ? 1 : 0;
}

/* Return a list of shelved changes sorted by patch file mtime, oldest first.
 */
static svn_error_t *
list_sorted_by_date(apr_array_header_t **list,
                    const char *local_abspath,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  apr_hash_t *shelved_patch_infos;

  SVN_ERR(svn_client_shelves_list(&shelved_patch_infos, local_abspath,
                                  ctx, scratch_pool, scratch_pool));
  *list = svn_sort__hash(shelved_patch_infos,
                         compare_shelved_patch_infos_by_mtime,
                         scratch_pool);
  return SVN_NO_ERROR;
}

#ifndef WIN32
/* Run CMD with ARGS.
 * Send its stdout to the parent's stdout. Disconnect its stdin and stderr.
 */
static svn_error_t *
run_cmd(const char *cmd,
        const char *const *args,
        apr_pool_t *scratch_pool)
{
  apr_status_t apr_err;
  apr_file_t *outfile;
  svn_error_t *err;
  int exitcode;

  apr_err = apr_file_open_stdout(&outfile, scratch_pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Can't open stdout");

  err = svn_io_run_cmd(NULL /*path*/, cmd, args,
                       &exitcode, NULL /*exitwhy*/,
                       TRUE /*inherit*/,
                       NULL /*infile*/, outfile, NULL /*errfile*/,
                       scratch_pool);
  if (err || exitcode)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, err,
                             _("Could not run external command '%s'"), cmd);
  return SVN_NO_ERROR;
}
#endif

/* Display a list of shelved changes */
static svn_error_t *
shelves_list(const char *local_abspath,
             svn_boolean_t diffstat,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  apr_array_header_t *list;
  int i;

  SVN_ERR(list_sorted_by_date(&list,
                              local_abspath, ctx, scratch_pool));

  for (i = 0; i < list->nelts; i++)
    {
      const svn_sort__item_t *item = &APR_ARRAY_IDX(list, i, svn_sort__item_t);
      const char *name = item->key;
      svn_client_shelved_patch_info_t *info = item->value;
      int age = (int)((apr_time_now() - info->mtime) / 1000000 / 60);
      apr_hash_t *paths;

      SVN_ERR(svn_client_shelf_get_paths(&paths,
                                         name, local_abspath, ctx,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("%-30s %6d mins old %10ld bytes %4d paths changed\n"),
                                 name, age, (long)info->dirent->filesize,
                                 apr_hash_count(paths)));
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _(" %.50s\n"),
                                 info->message));

      if (diffstat)
        {
#ifndef WIN32
          const char *args[4];
          svn_error_t *err;

          args[0] = "diffstat";
          args[1] = "-p0";
          args[2] = info->patch_path;
          args[3] = NULL;
          err = run_cmd("diffstat", args, scratch_pool);
          if (err)
            svn_error_clear(err);
          else
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                                       "\n"));
#endif
        }
    }

  return SVN_NO_ERROR;
}

/* Find the name of the youngest shelved change.
 */
static svn_error_t *
name_of_youngest(const char **name_p,
                 const char *local_abspath,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *list;
  const svn_sort__item_t *youngest_item;

  SVN_ERR(list_sorted_by_date(&list,
                              local_abspath, ctx, scratch_pool));
  if (list->nelts == 0)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                            _("No shelved changes found"));

  youngest_item = &APR_ARRAY_IDX(list, list->nelts - 1, svn_sort__item_t);
  *name_p = youngest_item->key;
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelve(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  apr_array_header_t *targets;
  svn_boolean_t has_changes;

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->list)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(shelves_list(local_abspath,
                           ! opt_state->quiet /*diffstat*/,
                           ctx, pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_name(&name, os, pool, pool));

  if (opt_state->remove)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(svn_client_shelves_delete(name, local_abspath,
                                        opt_state->dry_run,
                                        ctx, pool));
      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool, "deleted '%s'\n", name));
      return SVN_NO_ERROR;
    }

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  svn_opt_push_implicit_dot_target(targets, pool);

  {
      svn_depth_t depth = opt_state->depth;
      svn_error_t *err;

      /* shelve has no implicit dot-target `.', so don't you put that
         code here! */
      if (!targets->nelts)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

      SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__make_log_msg_baton(&ctx->log_msg_baton3,
                                           opt_state, NULL, ctx->config,
                                           pool));
      err = svn_client_shelve(name,
                              targets, depth, opt_state->changelists,
                              opt_state->keep_local, opt_state->dry_run,
                              ctx, pool);
      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                        err, pool));
      else
        SVN_ERR(err);
  }

  /* If no modifications were shelved, throw an error. */
  SVN_ERR(svn_client_shelf_has_changes(&has_changes,
                                       name, local_abspath, ctx, pool));
  if (! has_changes)
    {
      SVN_ERR(svn_client_shelves_delete(name, local_abspath,
                                        opt_state->dry_run, ctx, pool));
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("No changes were shelved"));
    }

  if (! opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, "shelved '%s'\n", name));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unshelve(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  apr_array_header_t *targets;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->list)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(shelves_list(local_abspath,
                           ! opt_state->quiet /*diffstat*/,
                           ctx, pool));
      return SVN_NO_ERROR;
    }

  if (os->ind < os->argc)
    {
      SVN_ERR(get_name(&name, os, pool, pool));
    }
  else
    {
      SVN_ERR(name_of_youngest(&name, local_abspath, ctx, pool, pool));
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("unshelving the youngest change, '%s'\n"),
                                 name));
    }

  /* There should be no remaining arguments. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  if (targets->nelts)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(svn_client_unshelve(name, local_abspath,
                              opt_state->keep_local, opt_state->dry_run,
                              ctx, pool));
  if (! opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, "unshelved '%s'\n", name));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelves(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;

  /* There should be no remaining arguments. */
  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));
  SVN_ERR(shelves_list(local_abspath, ! opt_state->quiet /*diffstat*/,
                       ctx, pool));

  return SVN_NO_ERROR;
}
