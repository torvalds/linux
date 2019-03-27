/*
 * diff-cmd.c -- Display context diff of a file
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_cmdline.h"
#include "svn_xml.h"
#include "svn_hash.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Convert KIND into a single character for display to the user. */
static char
kind_to_char(svn_client_diff_summarize_kind_t kind)
{
  switch (kind)
    {
      case svn_client_diff_summarize_kind_modified:
        return 'M';

      case svn_client_diff_summarize_kind_added:
        return 'A';

      case svn_client_diff_summarize_kind_deleted:
        return 'D';

      default:
        return ' ';
    }
}

/* Convert KIND into a word describing the kind to the user. */
static const char *
kind_to_word(svn_client_diff_summarize_kind_t kind)
{
  switch (kind)
    {
      case svn_client_diff_summarize_kind_modified: return "modified";
      case svn_client_diff_summarize_kind_added:    return "added";
      case svn_client_diff_summarize_kind_deleted:  return "deleted";
      default:                                      return "none";
    }
}

/* Baton for summarize_xml and summarize_regular */
struct summarize_baton_t
{
  const char *anchor;
  svn_boolean_t ignore_properties;
};

/* Print summary information about a given change as XML, implements the
 * svn_client_diff_summarize_func_t interface. The @a baton is a 'char *'
 * representing the either the path to the working copy root or the url
 * the path the working copy root corresponds to. */
static svn_error_t *
summarize_xml(const svn_client_diff_summarize_t *summary,
              void *baton,
              apr_pool_t *pool)
{
  struct summarize_baton_t *b = baton;
  /* Full path to the object being diffed.  This is created by taking the
   * baton, and appending the target's relative path. */
  const char *path = b->anchor;
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
  const char *prop_change;

  if (b->ignore_properties &&
      summary->summarize_kind == svn_client_diff_summarize_kind_normal)
    return SVN_NO_ERROR;

  /* Tack on the target path, so we can differentiate between different parts
   * of the output when we're given multiple targets. */
  if (svn_path_is_url(path))
    {
      path = svn_path_url_add_component2(path, summary->path, pool);
    }
  else
    {
      path = svn_dirent_join(path, summary->path, pool);

      /* Convert non-urls to local style, so that things like ""
         show up as "." */
      path = svn_dirent_local_style(path, pool);
    }

  prop_change = summary->prop_changed ? "modified" : "none";
  if (b->ignore_properties)
    prop_change = "none";

  svn_xml_make_open_tag(&sb, pool, svn_xml_protect_pcdata, "path",
                        "kind", svn_cl__node_kind_str_xml(summary->node_kind),
                        "item", kind_to_word(summary->summarize_kind),
                        "props",  prop_change,
                        SVN_VA_NULL);

  svn_xml_escape_cdata_cstring(&sb, path, pool);
  svn_xml_make_close_tag(&sb, pool, "path");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}

/* Print summary information about a given change, implements the
 * svn_client_diff_summarize_func_t interface. */
static svn_error_t *
summarize_regular(const svn_client_diff_summarize_t *summary,
                  void *baton,
                  apr_pool_t *pool)
{
  struct summarize_baton_t *b = baton;
  const char *path = b->anchor;
  char prop_change;

  if (b->ignore_properties &&
      summary->summarize_kind == svn_client_diff_summarize_kind_normal)
    return SVN_NO_ERROR;

  /* Tack on the target path, so we can differentiate between different parts
   * of the output when we're given multiple targets. */
  if (svn_path_is_url(path))
    {
      path = svn_path_url_add_component2(path, summary->path, pool);
    }
  else
    {
      path = svn_dirent_join(path, summary->path, pool);

      /* Convert non-urls to local style, so that things like ""
         show up as "." */
      path = svn_dirent_local_style(path, pool);
    }

  /* Note: This output format tries to look like the output of 'svn status',
   *       thus the blank spaces where information that is not relevant to
   *       a diff summary would go. */

  prop_change = summary->prop_changed ? 'M' : ' ';
  if (b->ignore_properties)
    prop_change = ' ';

  SVN_ERR(svn_cmdline_printf(pool, "%c%c      %s\n",
                             kind_to_char(summary->summarize_kind),
                             prop_change, path));

  return svn_cmdline_fflush(stdout);
}

/* An svn_opt_subcommand_t to handle the 'diff' command.
   This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__diff(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *options;
  apr_array_header_t *targets;
  svn_stream_t *outstream;
  svn_stream_t *errstream;
  const char *old_target, *new_target;
  apr_pool_t *iterpool;
  svn_boolean_t pegged_diff = FALSE;
  svn_boolean_t ignore_content_type;
  svn_boolean_t show_copies_as_adds =
    opt_state->diff.patch_compatible || opt_state->diff.show_copies_as_adds;
  svn_boolean_t ignore_properties =
    opt_state->diff.patch_compatible || opt_state->diff.ignore_properties;
  int i;
  struct summarize_baton_t summarize_baton;
  const svn_client_diff_summarize_func_t summarize_func =
    (opt_state->xml ? summarize_xml : summarize_regular);

  if (opt_state->extensions)
    options = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
  else
    options = NULL;

  /* Get streams representing stdout and stderr, which is where
     we'll have the external 'diff' program print to. */
  SVN_ERR(svn_stream_for_stdout(&outstream, pool));
  SVN_ERR(svn_stream_for_stderr(&errstream, pool));

  if (opt_state->xml)
    {
      svn_stringbuf_t *sb;

      /* Check that the --summarize is passed as well. */
      if (!opt_state->diff.summarize)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'--xml' option only valid with "
                                  "'--summarize' option"));

      SVN_ERR(svn_cl__xml_print_header("diff", pool));

      sb = svn_stringbuf_create_empty(pool);
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "paths", SVN_VA_NULL);
      SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
    }
  if (opt_state->diff.summarize)
    {
      if (opt_state->diff.use_git_diff_format)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--git");
      if (opt_state->diff.patch_compatible)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--patch-compatible");
      if (opt_state->diff.show_copies_as_adds)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--show-copies-as-adds");
      if (opt_state->diff.internal_diff)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--internal-diff");
      if (opt_state->diff.diff_cmd)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--diff-cmd");
      if (opt_state->diff.no_diff_added)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--no-diff-added");
      if (opt_state->diff.no_diff_deleted)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--no-diff-deleted");
      if (opt_state->force)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'%s' not valid with '--summarize' option"),
                                 "--force");
      /* Not handling ignore-properties, and properties-only as there should
         be a patch adding support for these being applied soon */
    }

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  if (! opt_state->old_target && ! opt_state->new_target
      && (targets->nelts == 2)
      && (svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *))
          || svn_path_is_url(APR_ARRAY_IDX(targets, 1, const char *)))
      && opt_state->start_revision.kind == svn_opt_revision_unspecified
      && opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      /* A 2-target diff where one or both targets are URLs. These are
       * shorthands for some 'svn diff --old X --new Y' invocations. */

      SVN_ERR(svn_opt_parse_path(&opt_state->start_revision, &old_target,
                                 APR_ARRAY_IDX(targets, 0, const char *),
                                 pool));
      SVN_ERR(svn_opt_parse_path(&opt_state->end_revision, &new_target,
                                 APR_ARRAY_IDX(targets, 1, const char *),
                                 pool));
      targets->nelts = 0;

      /* Set default start/end revisions based on target types, in the same
       * manner as done for the corresponding '--old X --new Y' cases,
       * (note that we have an explicit --new target) */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_path_is_url(old_target)
            ? svn_opt_revision_head : svn_opt_revision_working;

      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_path_is_url(new_target)
            ? svn_opt_revision_head : svn_opt_revision_working;
    }
  else if (opt_state->old_target)
    {
      apr_array_header_t *tmp, *tmp2;
      svn_opt_revision_t old_rev, new_rev;

      /* The 'svn diff --old=OLD[@OLDREV] [--new=NEW[@NEWREV]]
         [PATH...]' case matches. */

      tmp = apr_array_make(pool, 2, sizeof(const char *));
      APR_ARRAY_PUSH(tmp, const char *) = (opt_state->old_target);
      APR_ARRAY_PUSH(tmp, const char *) = (opt_state->new_target
                                           ? opt_state->new_target
                                           : opt_state->old_target);

      SVN_ERR(svn_cl__args_to_target_array_print_reserved(&tmp2, os, tmp,
                                                          ctx, FALSE, pool));

      /* Check if either or both targets were skipped (e.g. because they
       * were .svn directories). */
      if (tmp2->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);

      SVN_ERR(svn_opt_parse_path(&old_rev, &old_target,
                                 APR_ARRAY_IDX(tmp2, 0, const char *),
                                 pool));
      if (old_rev.kind != svn_opt_revision_unspecified)
        opt_state->start_revision = old_rev;
      SVN_ERR(svn_opt_parse_path(&new_rev, &new_target,
                                 APR_ARRAY_IDX(tmp2, 1, const char *),
                                 pool));
      if (new_rev.kind != svn_opt_revision_unspecified)
        opt_state->end_revision = new_rev;

      /* For URLs, default to HEAD. For WC paths, default to WORKING if
       * new target is explicit; if new target is implicitly the same as
       * old target, then default the old to BASE and new to WORKING. */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_path_is_url(old_target)
          ? svn_opt_revision_head
          : (opt_state->new_target
             ? svn_opt_revision_working : svn_opt_revision_base);
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_path_is_url(new_target)
          ? svn_opt_revision_head : svn_opt_revision_working;
    }
  else if (opt_state->new_target)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("'--new' option only valid with "
                                "'--old' option"));
    }
  else
    {
      svn_boolean_t working_copy_present;

      /* The 'svn diff [-r N[:M]] [TARGET[@REV]...]' case matches. */

      /* Here each target is a pegged object. Find out the starting
         and ending paths for each target. */

      svn_opt_push_implicit_dot_target(targets, pool);

      old_target = "";
      new_target = "";

      SVN_ERR_W(svn_cl__assert_homogeneous_target_type(targets),
        _("'svn diff [-r N[:M]] [TARGET[@REV]...]' does not support mixed "
          "target types. Try using the --old and --new options or one of "
          "the shorthand invocations listed in 'svn help diff'."));

      working_copy_present = ! svn_path_is_url(APR_ARRAY_IDX(targets, 0,
                                                             const char *));

      if (opt_state->start_revision.kind == svn_opt_revision_unspecified
          && working_copy_present)
        opt_state->start_revision.kind = svn_opt_revision_base;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = working_copy_present
          ? svn_opt_revision_working : svn_opt_revision_head;

      /* Determine if we need to do pegged diffs. */
      if ((opt_state->start_revision.kind != svn_opt_revision_base
           && opt_state->start_revision.kind != svn_opt_revision_working)
          || (opt_state->end_revision.kind != svn_opt_revision_base
              && opt_state->end_revision.kind != svn_opt_revision_working))
        pegged_diff = TRUE;

    }

  /* Should we ignore the content-type when deciding what to diff? */
  if (opt_state->force)
    {
      ignore_content_type = TRUE;
    }
  else if (ctx->config)
    {
      SVN_ERR(svn_config_get_bool(svn_hash_gets(ctx->config,
                                                SVN_CONFIG_CATEGORY_CONFIG),
                                  &ignore_content_type,
                                  SVN_CONFIG_SECTION_MISCELLANY,
                                  SVN_CONFIG_OPTION_DIFF_IGNORE_CONTENT_TYPE,
                                  FALSE));
    }
  else
    {
      ignore_content_type = FALSE;
    }

  svn_opt_push_implicit_dot_target(targets, pool);

  iterpool = svn_pool_create(pool);

  for (i = 0; i < targets->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      const char *target1, *target2;

      svn_pool_clear(iterpool);
      if (! pegged_diff)
        {
          /* We can't be tacking URLs onto base paths! */
          if (svn_path_is_url(path))
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Path '%s' not relative to base URLs"),
                                     path);

          if (svn_path_is_url(old_target))
            target1 = svn_path_url_add_component2(
                          old_target,
                          svn_relpath_canonicalize(path, iterpool),
                          iterpool);
          else
            target1 = svn_dirent_join(old_target, path, iterpool);

          if (svn_path_is_url(new_target))
            target2 = svn_path_url_add_component2(
                          new_target,
                          svn_relpath_canonicalize(path, iterpool),
                          iterpool);
          else
            target2 = svn_dirent_join(new_target, path, iterpool);

          if (opt_state->diff.summarize)
            {
              summarize_baton.anchor = target1;
              summarize_baton.ignore_properties = ignore_properties;

              SVN_ERR(svn_client_diff_summarize2(
                                target1,
                                &opt_state->start_revision,
                                target2,
                                &opt_state->end_revision,
                                opt_state->depth,
                                ! opt_state->diff.notice_ancestry,
                                opt_state->changelists,
                                summarize_func, &summarize_baton,
                                ctx, iterpool));
            }
          else
            SVN_ERR(svn_client_diff6(
                     options,
                     target1,
                     &(opt_state->start_revision),
                     target2,
                     &(opt_state->end_revision),
                     NULL,
                     opt_state->depth,
                     ! opt_state->diff.notice_ancestry,
                     opt_state->diff.no_diff_added,
                     opt_state->diff.no_diff_deleted,
                     show_copies_as_adds,
                     ignore_content_type,
                     ignore_properties,
                     opt_state->diff.properties_only,
                     opt_state->diff.use_git_diff_format,
                     svn_cmdline_output_encoding(pool),
                     outstream,
                     errstream,
                     opt_state->changelists,
                     ctx, iterpool));
        }
      else
        {
          const char *truepath;
          svn_opt_revision_t peg_revision;

          /* First check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, path,
                                     iterpool));

          /* Set the default peg revision if one was not specified. */
          if (peg_revision.kind == svn_opt_revision_unspecified)
            peg_revision.kind = svn_path_is_url(path)
              ? svn_opt_revision_head : svn_opt_revision_working;

          if (opt_state->diff.summarize)
            {
              summarize_baton.anchor = truepath;
              summarize_baton.ignore_properties = ignore_properties;
              SVN_ERR(svn_client_diff_summarize_peg2(
                                truepath,
                                &peg_revision,
                                &opt_state->start_revision,
                                &opt_state->end_revision,
                                opt_state->depth,
                                ! opt_state->diff.notice_ancestry,
                                opt_state->changelists,
                                summarize_func, &summarize_baton,
                                ctx, iterpool));
            }
          else
            SVN_ERR(svn_client_diff_peg6(
                     options,
                     truepath,
                     &peg_revision,
                     &opt_state->start_revision,
                     &opt_state->end_revision,
                     NULL,
                     opt_state->depth,
                     ! opt_state->diff.notice_ancestry,
                     opt_state->diff.no_diff_added,
                     opt_state->diff.no_diff_deleted,
                     show_copies_as_adds,
                     ignore_content_type,
                     ignore_properties,
                     opt_state->diff.properties_only,
                     opt_state->diff.use_git_diff_format,
                     svn_cmdline_output_encoding(pool),
                     outstream,
                     errstream,
                     opt_state->changelists,
                     ctx, iterpool));
        }
    }

  if (opt_state->xml)
    {
      svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
      svn_xml_make_close_tag(&sb, pool, "paths");
      SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
      SVN_ERR(svn_cl__xml_print_footer("diff", pool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
