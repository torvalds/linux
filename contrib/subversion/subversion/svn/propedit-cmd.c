/*
 * propedit-cmd.c -- Edit properties of files/dirs using $EDITOR
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
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_props.h"
#include "cl.h"

#include "private/svn_cmdline_private.h"
#include "svn_private_config.h"


/*** Code. ***/
struct commit_info_baton
{
  const char *pname;
  const char *target_local;
};

static svn_error_t *
commit_info_handler(const svn_commit_info_t *commit_info,
                    void *baton,
                    apr_pool_t *pool)
{
  struct commit_info_baton *cib = baton;

  SVN_ERR(svn_cmdline_printf(pool,
                             _("Set new value for property '%s' on '%s'\n"),
                             cib->pname, cib->target_local));
  SVN_ERR(svn_cl__print_commit_info(commit_info, NULL, pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propedit(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname;
  apr_array_header_t *args, *targets;

  /* Validate the input and get the property's name (and a UTF-8
     version of that name). */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname, pname, pool));
  if (! svn_prop_name_is_valid(pname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             pname);
  if (!opt_state->force)
    SVN_ERR(svn_cl__check_svn_prop_name(pname, opt_state->revprop,
                                        svn_cl__prop_use_edit, pool));

  if (opt_state->encoding && !svn_prop_needs_translation(pname))
      return svn_error_create
          (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("--encoding option applies only to textual"
             " Subversion-controlled properties"));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* We do our own notifications */
  ctx->notify_func2 = NULL;

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;
      svn_string_t *propval;
      svn_string_t original_propval;
      const char *temp_dir;

      /* Implicit "." is okay for revision properties; it just helps
         us find the right repository. */
      svn_opt_push_implicit_dot_target(targets, pool);

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, ctx, pool));

      /* Fetch the current property. */
      SVN_ERR(svn_client_revprop_get(pname, &propval,
                                     URL, &(opt_state->start_revision),
                                     &rev, ctx, pool));

      if (! propval)
        {
          propval = svn_string_create_empty(pool);
          /* This is how we signify to svn_client_revprop_set2() that
             we want it to check that the original value hasn't
             changed, but that that original value was non-existent: */
          original_propval.data = NULL;  /* and .len is ignored */
        }
      else
        {
          original_propval = *propval;
        }

      /* Run the editor on a temporary file which contains the
         original property value... */
      SVN_ERR(svn_io_temp_dir(&temp_dir, pool));
      SVN_ERR(svn_cmdline__edit_string_externally(
               &propval, NULL,
               opt_state->editor_cmd, temp_dir,
               propval, "svn-prop",
               ctx->config,
               svn_prop_needs_translation(pname),
               opt_state->encoding, pool));

      /* ...and re-set the property's value accordingly. */
      if (propval)
        {
          SVN_ERR(svn_client_revprop_set2(pname,
                                          propval, &original_propval,
                                          URL, &(opt_state->start_revision),
                                          &rev, opt_state->force, ctx, pool));

          SVN_ERR
            (svn_cmdline_printf
             (pool,
              _("Set new value for property '%s' on revision %ld\n"),
              pname, rev));
        }
      else
        {
          SVN_ERR(svn_cmdline_printf
                  (pool, _("No changes to property '%s' on revision %ld\n"),
                   pname, rev));
        }
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot specify revision for editing versioned property '%s'"),
         pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      struct commit_info_baton cib;
      int i;

      /* The customary implicit dot rule has been prone to user error
       * here.  For example, Jon Trowbridge <trow@gnu.og> did
       *
       *    $ svn propedit HACKING
       *
       * and then when he closed his editor, he was surprised to see
       *
       *    Set new value for property 'HACKING' on ''
       *
       * ...meaning that the property named 'HACKING' had been set on
       * the current working directory, with the value taken from the
       * editor.  So we don't do the implicit dot thing anymore; an
       * explicit target is always required when editing a versioned
       * property.
       */
      if (targets->nelts == 0)
        {
          return svn_error_create
            (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
             _("Explicit target argument required"));
        }

      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

      cib.pname = pname;

      /* For each target, edit the property PNAME. */
      for (i = 0; i < targets->nelts; i++)
        {
          apr_hash_t *props;
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          svn_string_t *propval, *edited_propval;
          const char *base_dir = target;
          const char *target_local;
          const char *abspath_or_url;
          svn_node_kind_t kind;
          svn_opt_revision_t peg_revision;
          svn_revnum_t base_rev = SVN_INVALID_REVNUM;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          if (!svn_path_is_url(target))
            SVN_ERR(svn_dirent_get_absolute(&abspath_or_url, target, subpool));
          else
            abspath_or_url = target;

          /* Propedits can only happen on HEAD or the working copy, so
             the peg revision can be as unspecified. */
          peg_revision.kind = svn_opt_revision_unspecified;

          /* Fetch the current property. */
          SVN_ERR(svn_client_propget5(&props, NULL, pname, abspath_or_url,
                                      &peg_revision,
                                      &(opt_state->start_revision),
                                      &base_rev, svn_depth_empty,
                                      NULL, ctx, subpool, subpool));

          /* Get the property value. */
          propval = svn_hash_gets(props, abspath_or_url);
          if (! propval)
            propval = svn_string_create_empty(subpool);

          if (svn_path_is_url(target))
            {
              /* For URLs, put the temporary file in the current directory. */
              base_dir = ".";
            }
          else
            {
              if (opt_state->message || opt_state->filedata ||
                  opt_state->revprop_table)
                {
                  return svn_error_create
                    (SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE, NULL,
                     _("Local, non-commit operations do not take a log message "
                       "or revision properties"));
                }

              /* Split the path if it is a file path. */
              SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath_or_url,
                                        FALSE, FALSE, subpool));

              if (kind == svn_node_none)
                return svn_error_createf(
                   SVN_ERR_ENTRY_NOT_FOUND, NULL,
                   _("'%s' does not appear to be a working copy path"), target);
              if (kind == svn_node_file)
                base_dir = svn_dirent_dirname(target, subpool);
            }

          /* Run the editor on a temporary file which contains the
             original property value... */
          SVN_ERR(svn_cmdline__edit_string_externally(&edited_propval, NULL,
                                                      opt_state->editor_cmd,
                                                      base_dir,
                                                      propval,
                                                      "svn-prop",
                                                      ctx->config,
                                                      svn_prop_needs_translation
                                                      (pname),
                                                      opt_state->encoding,
                                                      subpool));

          target_local = svn_path_is_url(target) ? target
            : svn_dirent_local_style(target, subpool);
          cib.target_local = target_local;

          /* ...and re-set the property's value accordingly. */
          if (edited_propval && !svn_string_compare(propval, edited_propval))
            {
              svn_error_t *err = SVN_NO_ERROR;

              svn_cl__check_boolean_prop_val(pname, edited_propval->data,
                                             subpool);

              if (ctx->log_msg_func3)
                SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3),
                                                   opt_state, NULL, ctx->config,
                                                   subpool));
              if (svn_path_is_url(target))
                {
                  err = svn_client_propset_remote(pname, edited_propval,
                                                  target, opt_state->force,
                                                  base_rev,
                                                  opt_state->revprop_table,
                                                  commit_info_handler, &cib,
                                                  ctx, subpool);
                }
              else
                {
                  apr_array_header_t *targs = apr_array_make(subpool, 1,
                                                    sizeof(const char *));

                  APR_ARRAY_PUSH(targs, const char *) = target;

                  SVN_ERR(svn_cl__propset_print_binary_mime_type_warning(
                      targs, pname, propval, subpool));

                  err = svn_client_propset_local(pname, edited_propval,
                                                 targs, svn_depth_empty,
                                                 opt_state->force, NULL,
                                                 ctx, subpool);
                }

              if (ctx->log_msg_func3)
                SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                                err, pool));
              else if (err)
                return svn_error_trace(err);

              /* Print a message if we successfully committed or if it
                 was just a wc propset (but not if the user aborted a URL
                 propedit). */
              if (!svn_path_is_url(target))
                SVN_ERR(svn_cmdline_printf(
                        subpool, _("Set new value for property '%s' on '%s'\n"),
                        pname, target_local));
            }
          else
            {
              SVN_ERR
                (svn_cmdline_printf
                 (subpool, _("No changes to property '%s' on '%s'\n"),
                  pname, target_local));
            }
        }
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
