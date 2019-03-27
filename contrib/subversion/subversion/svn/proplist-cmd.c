/*
 * proplist-cmd.c -- List properties of files/dirs
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_props.h"
#include "cl.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"

typedef struct proplist_baton_t
{
  svn_cl__opt_state_t *opt_state;
  svn_boolean_t is_url;
} proplist_baton_t;


/*** Code. ***/

/* This implements the svn_proplist_receiver2_t interface, printing XML to
   stdout. */
static svn_error_t *
proplist_receiver_xml(void *baton,
                      const char *path,
                      apr_hash_t *prop_hash,
                      apr_array_header_t *inherited_props,
                      apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((proplist_baton_t *)baton)->opt_state;
  svn_boolean_t is_url = ((proplist_baton_t *)baton)->is_url;
  svn_stringbuf_t *sb;
  const char *name_local;

  if (inherited_props && inherited_props->nelts)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
            APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);

          sb = NULL;

          if (svn_path_is_url(iprop->path_or_url))
            name_local = iprop->path_or_url;
          else
            name_local = svn_dirent_local_style(iprop->path_or_url, iterpool);

          svn_xml_make_open_tag(&sb, iterpool, svn_xml_normal, "target",
                            "path", name_local, SVN_VA_NULL);
          SVN_ERR(svn_cmdline__print_xml_prop_hash(&sb, iprop->prop_hash,
                                                   (! opt_state->verbose),
                                                   TRUE, iterpool));
          svn_xml_make_close_tag(&sb, iterpool, "target");
          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
        }
      svn_pool_destroy(iterpool);
    }

  if (! is_url)
    name_local = svn_dirent_local_style(path, pool);
  else
    name_local = path;

  sb = NULL;


  if (prop_hash)
    {
      /* "<target ...>" */
        svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "target",
                              "path", name_local, SVN_VA_NULL);

        SVN_ERR(svn_cmdline__print_xml_prop_hash(&sb, prop_hash,
                                                 (! opt_state->verbose),
                                                 FALSE, pool));

        /* "</target>" */
        svn_xml_make_close_tag(&sb, pool, "target");
        SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
    }

  return SVN_NO_ERROR;
}


/* This implements the svn_proplist_receiver2_t interface. */
static svn_error_t *
proplist_receiver(void *baton,
                  const char *path,
                  apr_hash_t *prop_hash,
                  apr_array_header_t *inherited_props,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((proplist_baton_t *)baton)->opt_state;
  svn_boolean_t is_url = ((proplist_baton_t *)baton)->is_url;
  const char *name_local;

  if (! is_url)
    name_local = svn_dirent_local_style(path, pool);
  else
    name_local = path;

  if (inherited_props)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
            APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);

          svn_pool_clear(iterpool);

          if (!opt_state->quiet)
            {
              if (svn_path_is_url(iprop->path_or_url))
                SVN_ERR(svn_cmdline_printf(
                  iterpool, _("Inherited properties on '%s',\nfrom '%s':\n"),
                  name_local, iprop->path_or_url));
              else
                SVN_ERR(svn_cmdline_printf(
                  iterpool, _("Inherited properties on '%s',\nfrom '%s':\n"),
                  name_local, svn_dirent_local_style(iprop->path_or_url,
                                                     iterpool)));
            }

          SVN_ERR(svn_cmdline__print_prop_hash(NULL, iprop->prop_hash,
                                               (! opt_state->verbose),
                                               iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  if (prop_hash && apr_hash_count(prop_hash))
    {
      if (!opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool, _("Properties on '%s':\n"),
                                   name_local));
      SVN_ERR(svn_cmdline__print_prop_hash(NULL, prop_hash,
                                           (! opt_state->verbose), pool));
    }

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__proplist(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *errors = apr_array_make(scratch_pool, 0,
                                              sizeof(apr_status_t));

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, scratch_pool);

  if (opt_state->revprop)  /* operate on revprops */
    {
      svn_revnum_t rev;
      const char *URL;
      apr_hash_t *proplist;

      if (opt_state->show_inherited_props)
        return svn_error_create(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("--show-inherited-props can't be used with --revprop"));

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, ctx, scratch_pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_list(&proplist,
                                      URL, &(opt_state->start_revision),
                                      &rev, ctx, scratch_pool));

      if (opt_state->xml)
        {
          svn_stringbuf_t *sb = NULL;
          char *revstr = apr_psprintf(scratch_pool, "%ld", rev);

          SVN_ERR(svn_cl__xml_print_header("properties", scratch_pool));

          svn_xml_make_open_tag(&sb, scratch_pool, svn_xml_normal,
                                "revprops",
                                "rev", revstr, SVN_VA_NULL);
          SVN_ERR(svn_cmdline__print_xml_prop_hash(&sb, proplist,
                                                   (! opt_state->verbose),
                                                   FALSE, scratch_pool));
          svn_xml_make_close_tag(&sb, scratch_pool, "revprops");

          SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
          SVN_ERR(svn_cl__xml_print_footer("properties", scratch_pool));
        }
      else
        {
          SVN_ERR
            (svn_cmdline_printf(scratch_pool,
                                _("Unversioned properties on revision %ld:\n"),
                                rev));

          SVN_ERR(svn_cmdline__print_prop_hash(NULL, proplist,
                                               (! opt_state->verbose),
                                               scratch_pool));
        }
    }
  else  /* operate on normal, versioned properties (not revprops) */
    {
      int i;
      apr_pool_t *iterpool;
      svn_proplist_receiver2_t pl_receiver;

      if (opt_state->xml)
        {
          SVN_ERR(svn_cl__xml_print_header("properties", scratch_pool));
          pl_receiver = proplist_receiver_xml;
        }
      else
        {
          pl_receiver = proplist_receiver;
        }

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          proplist_baton_t pl_baton;
          const char *truepath;
          svn_opt_revision_t peg_revision;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          pl_baton.is_url = svn_path_is_url(target);
          pl_baton.opt_state = opt_state;

          /* Check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                     iterpool));

          SVN_ERR(svn_cl__try(
                   svn_client_proplist4(truepath, &peg_revision,
                                        &(opt_state->start_revision),
                                        opt_state->depth,
                                        opt_state->changelists,
                                        opt_state->show_inherited_props,
                                        pl_receiver, &pl_baton,
                                        ctx, iterpool),
                   errors, opt_state->quiet,
                   SVN_ERR_UNVERSIONED_RESOURCE,
                   SVN_ERR_ENTRY_NOT_FOUND,
                   0));
        }
      svn_pool_destroy(iterpool);

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_footer("properties", scratch_pool));

      /* Error out *after* we closed the XML element */
      if (errors->nelts > 0)
        {
          svn_error_t *err;

          err = svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL, NULL);
          for (i = 0; i < errors->nelts; i++)
            {
              apr_status_t status = APR_ARRAY_IDX(errors, i, apr_status_t);

              if (status == SVN_ERR_ENTRY_NOT_FOUND)
                err = svn_error_quick_wrap(err,
                                           _("Could not display properties "
                                             "of all targets because some "
                                             "targets don't exist"));
              else if (status == SVN_ERR_UNVERSIONED_RESOURCE)
                err = svn_error_quick_wrap(err,
                                           _("Could not display properties "
                                             "of all targets because some "
                                             "targets are not versioned"));
            }

          return svn_error_trace(err);
        }
    }

  return SVN_NO_ERROR;
}
