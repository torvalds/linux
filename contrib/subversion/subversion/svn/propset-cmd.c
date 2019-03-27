/*
 * propset-cmd.c -- Set property values on files/dirs
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_props.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propset(apr_getopt_t *os,
                void *baton,
                apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname;
  svn_string_t *propval = NULL;
  svn_boolean_t propval_came_from_cmdline;
  apr_array_header_t *args, *targets;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR(svn_opt_parse_num_args(&args, os,
                                 opt_state->filedata ? 1 : 2, scratch_pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname, pname, scratch_pool));
  if (! svn_prop_name_is_valid(pname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             pname);
  if (!opt_state->force)
    SVN_ERR(svn_cl__check_svn_prop_name(pname, opt_state->revprop,
                                        svn_cl__prop_use_set, scratch_pool));

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata)
    {
      propval = svn_string_create_from_buf(opt_state->filedata, scratch_pool);
      propval_came_from_cmdline = FALSE;
    }
  else
    {
      propval = svn_string_create(APR_ARRAY_IDX(args, 1, const char *),
                                  scratch_pool);
      propval_came_from_cmdline = TRUE;
    }

  /* We only want special Subversion property values to be in UTF-8
     and LF line endings.  All other propvals are taken literally. */
  if (svn_prop_needs_translation(pname))
    SVN_ERR(svn_subst_translate_string2(&propval, NULL, NULL, propval,
                                        opt_state->encoding, FALSE,
                                        scratch_pool, scratch_pool));
  else if (opt_state->encoding)
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("--encoding option applies only to textual"
         " Subversion-controlled properties"));

  /* Suck up all the remaining arguments into a targets array */

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Implicit "." is okay for revision properties; it just helps
     us find the right repository. */
  if (opt_state->revprop)
    svn_opt_push_implicit_dot_target(targets, scratch_pool);

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, ctx, scratch_pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_set2(pname, propval, NULL,
                                      URL, &(opt_state->start_revision),
                                      &rev, opt_state->force, ctx,
                                      scratch_pool));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot specify revision for setting versioned property '%s'"),
         pname);
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* The customary implicit dot rule has been prone to user error
       * here.  People would do intuitive things like
       *
       *    $ svn propset svn:executable script
       *
       * and then be surprised to get an error like:
       *
       *    svn: Illegal target for the requested operation
       *    svn: Cannot set svn:executable on a directory ()
       *
       * So we don't do the implicit dot thing anymore.  A * target
       * must always be explicitly provided when setting a versioned
       * property.  See
       *
       *    http://subversion.tigris.org/issues/show_bug.cgi?id=924
       *
       * for more details.
       */

      if (targets->nelts == 0)
        {
          if (propval_came_from_cmdline)
            {
              return svn_error_createf
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target required ('%s' interpreted as prop value)"),
                 propval->data);
            }
          else
            {
              return svn_error_create
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target argument required"));
            }
        }

      SVN_ERR(svn_cl__propset_print_binary_mime_type_warning(targets,
                                                             pname,
                                                             propval,
                                                             scratch_pool));

      SVN_ERR(svn_client_propset_local(pname, propval, targets,
                                       opt_state->depth, opt_state->force,
                                       opt_state->changelists, ctx,
                                       scratch_pool));

      if (! opt_state->quiet)
        svn_cl__check_boolean_prop_val(pname, propval->data, scratch_pool);
    }

  return SVN_NO_ERROR;
}
