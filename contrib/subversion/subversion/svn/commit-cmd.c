/*
 * commit-cmd.c -- Check changes into the repository.
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

#include <apr_general.h>

#include "svn_hash.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_config.h"
#include "cl.h"

#include "svn_private_config.h"



/* Wrapper notify_func2 function and baton for warning about
   reduced-depth commits of copied directories.  */
struct copy_warning_notify_baton
{
  svn_wc_notify_func2_t wrapped_func;
  void *wrapped_baton;
  svn_depth_t depth;
  svn_boolean_t warned;
};

static void
copy_warning_notify_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  struct copy_warning_notify_baton *b = baton;

  /* Call the wrapped notification system (if any). */
  if (b->wrapped_func)
    b->wrapped_func(b->wrapped_baton, notify, pool);

  /* If we're being notified about a copy of a directory when our
     commit depth is less-than-infinite, and we've not already warned
     about this situation, then warn about it (and remember that we
     now have.)  */
  if ((! b->warned)
      && (b->depth < svn_depth_infinity)
      && (notify->kind == svn_node_dir)
      && ((notify->action == svn_wc_notify_commit_copied) ||
          (notify->action == svn_wc_notify_commit_copied_replaced)))
    {
      svn_error_t *err;
      err = svn_cmdline_printf(pool,
                               _("svn: The depth of this commit is '%s', "
                                 "but copies are always performed "
                                 "recursively in the repository.\n"),
                               svn_depth_to_word(b->depth));
      /* ### FIXME: Try to return this error showhow? */
      svn_error_clear(err);

      /* We'll only warn once. */
      b->warned = TRUE;
    }
}




/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__commit(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  const char *base_dir;
  svn_config_t *cfg;
  svn_boolean_t no_unlock = FALSE;
  struct copy_warning_notify_baton cwnb;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  SVN_ERR_W(svn_cl__check_targets_are_local_paths(targets),
            _("Commit targets must be local paths"));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

  /* Condense the targets (like commit does)... */
  SVN_ERR(svn_dirent_condense_targets(&base_dir, &condensed_targets, targets,
                                      TRUE, pool, pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      const char *parent_dir, *base_name;

      SVN_ERR(svn_wc_get_actual_target2(&parent_dir, &base_name, ctx->wc_ctx,
                                        base_dir, pool, pool));
      if (*base_name)
        base_dir = apr_pstrdup(pool, parent_dir);
    }

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_infinity;

  cfg = ctx->config
           ? svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG)
           : NULL;
  if (cfg)
    SVN_ERR(svn_config_get_bool(cfg, &no_unlock,
                                SVN_CONFIG_SECTION_MISCELLANY,
                                SVN_CONFIG_OPTION_NO_UNLOCK, FALSE));

  /* We're creating a new log message baton because we can use our base_dir
     to store the temp file, instead of the current working directory.  The
     client might not have write access to their working directory, but they
     better have write access to the directory they're committing.  */
  SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3),
                                     opt_state, base_dir,
                                     ctx->config, pool));

  /* Copies are done server-side, and cheaply, which means they're
     effectively always done with infinite depth.  This is a potential
     cause of confusion for users trying to commit copied subtrees in
     part by restricting the commit's depth.  See issues #3699 and #3752. */
  if (opt_state->depth < svn_depth_infinity)
    {
      cwnb.wrapped_func = ctx->notify_func2;
      cwnb.wrapped_baton = ctx->notify_baton2;
      cwnb.depth = opt_state->depth;
      cwnb.warned = FALSE;
      ctx->notify_func2 = copy_warning_notify_func;
      ctx->notify_baton2 = &cwnb;
    }

  /* Commit. */
  err = svn_client_commit6(targets,
                           opt_state->depth,
                           no_unlock,
                           opt_state->keep_changelists,
                           TRUE /* commit_as_operations */,
                           opt_state->include_externals, /* file externals */
                           opt_state->include_externals, /* dir externals */
                           opt_state->changelists,
                           opt_state->revprop_table,
                           (opt_state->quiet
                            ? NULL : svn_cl__print_commit_info),
                           NULL,
                           ctx,
                           pool);
  SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3, err, pool));

  return SVN_NO_ERROR;
}
