/*
 * info-cmd.c -- Display information about a resource
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

#include "svn_string.h"
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_xml.h"
#include "cl.h"

#include "svn_private_config.h"
#include "cl-conflicts.h"


/*** Code. ***/

static svn_error_t *
svn_cl__info_print_time(apr_time_t atime,
                        const char *desc,
                        apr_pool_t *pool)
{
  const char *time_utf8;

  time_utf8 = svn_time_to_human_cstring(atime, pool);
  return svn_cmdline_printf(pool, "%s: %s\n", desc, time_utf8);
}


/* Return string representation of SCHEDULE */
static const char *
schedule_str(svn_wc_schedule_t schedule)
{
  switch (schedule)
    {
    case svn_wc_schedule_normal:
      return "normal";
    case svn_wc_schedule_add:
      return "add";
    case svn_wc_schedule_delete:
      return "delete";
    case svn_wc_schedule_replace:
      return "replace";
    default:
      return "none";
    }
}

/* Return a relative URL from information in INFO using POOL for
   temporary allocation. */
static const char*
relative_url(const svn_client_info2_t *info, apr_pool_t *pool)
{
  return apr_pstrcat(pool, "^/",
                     svn_path_uri_encode(
                         svn_uri_skip_ancestor(info->repos_root_URL,
                                               info->URL, pool),
                         pool), SVN_VA_NULL);
}


/* The kinds of items for print_info_item(). */
typedef enum
{
  /* Entry kind */
  info_item_kind,

  /* Repository location. */
  info_item_url,
  info_item_relative_url,
  info_item_repos_root_url,
  info_item_repos_uuid,

  /* Working copy revision or repository HEAD revision */
  info_item_revision,

  /* Commit details. */
  info_item_last_changed_rev,
  info_item_last_changed_date,
  info_item_last_changed_author,

  /* Working copy information */
  info_item_wc_root
} info_item_t;

/* Mapping between option keywords and info_item_t. */
typedef struct info_item_map_t
{
  const svn_string_t keyword;
  const info_item_t print_what;
} info_item_map_t;

#define MAKE_STRING(x) { x, sizeof(x) - 1 }
static const info_item_map_t info_item_map[] =
  {
    { MAKE_STRING("kind"),                info_item_kind },
    { MAKE_STRING("url"),                 info_item_url },
    { MAKE_STRING("relative-url"),        info_item_relative_url },
    { MAKE_STRING("repos-root-url"),      info_item_repos_root_url },
    { MAKE_STRING("repos-uuid"),          info_item_repos_uuid },
    { MAKE_STRING("revision"),            info_item_revision },
    { MAKE_STRING("last-changed-revision"),
                                          info_item_last_changed_rev },
    { MAKE_STRING("last-changed-date"),   info_item_last_changed_date },
    { MAKE_STRING("last-changed-author"), info_item_last_changed_author },
    { MAKE_STRING("wc-root"),             info_item_wc_root }
  };
#undef MAKE_STRING

static const apr_size_t info_item_map_len =
  (sizeof(info_item_map) / sizeof(info_item_map[0]));


/* The baton type used by the info receiver functions. */
typedef struct print_info_baton_t
{
  /* The path prefix that output paths should be normalized to. */
  const char *path_prefix;

  /*
   * The following fields are used by print_info_item().
   */

  /* Which item to print. */
  info_item_t print_what;

  /* Do we expect to show info for multiple targets? */
  svn_boolean_t multiple_targets;

  /* TRUE iff the current is a local path. */
  svn_boolean_t target_is_path;

  /* Did we already print a line of output? */
  svn_boolean_t start_new_line;

  /* The client context. */
  svn_client_ctx_t *ctx;
} print_info_baton_t;


/* Find the appropriate info_item_t for KEYWORD and initialize
 * RECEIVER_BATON for print_info_item(). Use SCRATCH_POOL for
 * temporary allocation.
 */
static svn_error_t *
find_print_what(const char *keyword,
                print_info_baton_t *receiver_baton,
                apr_pool_t *scratch_pool)
{
  svn_cl__simcheck_t **keywords = apr_palloc(
      scratch_pool, info_item_map_len * sizeof(svn_cl__simcheck_t*));
  svn_cl__simcheck_t *kwbuf = apr_palloc(
      scratch_pool, info_item_map_len * sizeof(svn_cl__simcheck_t));
  apr_size_t i;

  for (i = 0; i < info_item_map_len; ++i)
    {
      keywords[i] = &kwbuf[i];
      kwbuf[i].token.data = info_item_map[i].keyword.data;
      kwbuf[i].token.len = info_item_map[i].keyword.len;
      kwbuf[i].data = &info_item_map[i];
    }

  switch (svn_cl__similarity_check(keyword, keywords,
                                   info_item_map_len, scratch_pool))
    {
      const info_item_map_t *kw0;
      const info_item_map_t *kw1;
      const info_item_map_t *kw2;

    case 0:                     /* Exact match. */
      kw0 = keywords[0]->data;
      receiver_baton->print_what = kw0->print_what;
      return SVN_NO_ERROR;

    case 1:
      /* The best alternative isn't good enough */
      return svn_error_createf(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("'%s' is not a valid value for --show-item"),
          keyword);

    case 2:
      /* There is only one good candidate */
      kw0 = keywords[0]->data;
      return svn_error_createf(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("'%s' is not a valid value for --show-item;"
            " did you mean '%s'?"),
          keyword, kw0->keyword.data);

    case 3:
      /* Suggest a list of the most likely candidates */
      kw0 = keywords[0]->data;
      kw1 = keywords[1]->data;
      return svn_error_createf(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("'%s' is not a valid value for --show-item;"
            " did you mean '%s' or '%s'?"),
          keyword, kw0->keyword.data, kw1->keyword.data);

    default:
      /* Never suggest more than three candidates */
      kw0 = keywords[0]->data;
      kw1 = keywords[1]->data;
      kw2 = keywords[2]->data;
      return svn_error_createf(
          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("'%s' is not a valid value for --show-item;"
            " did you mean '%s', '%s' or '%s'?"),
          keyword, kw0->keyword.data, kw1->keyword.data, kw2->keyword.data);
    }
}

/* A callback of type svn_client_info_receiver2_t.
   Prints svn info in xml mode to standard out */
static svn_error_t *
print_info_xml(void *baton,
               const char *target,
               const svn_client_info2_t *info,
               apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
  const char *rev_str;
  print_info_baton_t *const receiver_baton = baton;

  if (SVN_IS_VALID_REVNUM(info->rev))
    rev_str = apr_psprintf(pool, "%ld", info->rev);
  else
    rev_str = apr_pstrdup(pool, _("Resource is not under version control."));

  /* "<entry ...>" */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "entry",
                        "path", svn_cl__local_style_skip_ancestor(
                                  receiver_baton->path_prefix, target, pool),
                        "kind", svn_cl__node_kind_str_xml(info->kind),
                        "revision", rev_str,
                        SVN_VA_NULL);

  /* "<url> xx </url>" */
  svn_cl__xml_tagged_cdata(&sb, pool, "url", info->URL);

  if (info->repos_root_URL && info->URL)
    {
      /* "<relative-url> xx </relative-url>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "relative-url",
                               relative_url(info, pool));
    }

  if (info->repos_root_URL || info->repos_UUID)
    {
      /* "<repository>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "repository",
                            SVN_VA_NULL);

      /* "<root> xx </root>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "root", info->repos_root_URL);

      /* "<uuid> xx </uuid>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "uuid", info->repos_UUID);

      /* "</repository>" */
      svn_xml_make_close_tag(&sb, pool, "repository");
    }

  if (info->wc_info)
    {
      /* "<wc-info>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "wc-info",
                            SVN_VA_NULL);

      /* "<wcroot-abspath> xx </wcroot-abspath>" */
      if (info->wc_info->wcroot_abspath)
        svn_cl__xml_tagged_cdata(&sb, pool, "wcroot-abspath",
                                 info->wc_info->wcroot_abspath);

      /* "<schedule> xx </schedule>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "schedule",
                               schedule_str(info->wc_info->schedule));

      /* "<depth> xx </depth>" */
      {
        svn_depth_t depth = info->wc_info->depth;

        /* In the entries world info just passed depth infinity for files */
        if (depth == svn_depth_unknown && info->kind == svn_node_file)
          depth = svn_depth_infinity;

        svn_cl__xml_tagged_cdata(&sb, pool, "depth", svn_depth_to_word(depth));
      }

      /* "<copy-from-url> xx </copy-from-url>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "copy-from-url",
                               info->wc_info->copyfrom_url);

      /* "<copy-from-rev> xx </copy-from-rev>" */
      if (SVN_IS_VALID_REVNUM(info->wc_info->copyfrom_rev))
        svn_cl__xml_tagged_cdata(&sb, pool, "copy-from-rev",
                                 apr_psprintf(pool, "%ld",
                                              info->wc_info->copyfrom_rev));

      /* "<text-updated> xx </text-updated>" */
      if (info->wc_info->recorded_time)
        svn_cl__xml_tagged_cdata(&sb, pool, "text-updated",
                                 svn_time_to_cstring(
                                          info->wc_info->recorded_time,
                                          pool));

      /* "<checksum> xx </checksum>" */
      /* ### Print the checksum kind. */
      svn_cl__xml_tagged_cdata(&sb, pool, "checksum",
                               svn_checksum_to_cstring(info->wc_info->checksum,
                                                       pool));

      if (info->wc_info->changelist)
        /* "<changelist> xx </changelist>" */
        svn_cl__xml_tagged_cdata(&sb, pool, "changelist",
                                 info->wc_info->changelist);

      if (info->wc_info->moved_from_abspath)
        {
          const char *relpath;

          relpath = svn_dirent_skip_ancestor(info->wc_info->wcroot_abspath,
                                             info->wc_info->moved_from_abspath);

          /* <moved-from> xx </moved-from> */
          if (relpath && relpath[0] != '\0')
            svn_cl__xml_tagged_cdata(&sb, pool, "moved-from", relpath);
          else
            svn_cl__xml_tagged_cdata(&sb, pool, "moved-from",
                                     info->wc_info->moved_from_abspath);
        }

      if (info->wc_info->moved_to_abspath)
        {
          const char *relpath;

          relpath = svn_dirent_skip_ancestor(info->wc_info->wcroot_abspath,
                                             info->wc_info->moved_to_abspath);
          /* <moved-to> xx </moved-to> */
          if (relpath && relpath[0] != '\0')
            svn_cl__xml_tagged_cdata(&sb, pool, "moved-to", relpath);
          else
            svn_cl__xml_tagged_cdata(&sb, pool, "moved-to",
                                     info->wc_info->moved_to_abspath);
        }

      /* "</wc-info>" */
      svn_xml_make_close_tag(&sb, pool, "wc-info");
    }

  if (info->last_changed_author
      || SVN_IS_VALID_REVNUM(info->last_changed_rev)
      || info->last_changed_date)
    {
      svn_cl__print_xml_commit(&sb, info->last_changed_rev,
                               info->last_changed_author,
                               svn_time_to_cstring(info->last_changed_date,
                                                   pool),
                               pool);
    }

  if (info->wc_info && info->wc_info->conflicts)
    {
      int i;
      apr_pool_t *iterpool;

      iterpool = svn_pool_create(pool);
      for (i = 0; i < info->wc_info->conflicts->nelts; i++)
        {
          const svn_wc_conflict_description2_t *desc =
                      APR_ARRAY_IDX(info->wc_info->conflicts, i,
                                    const svn_wc_conflict_description2_t *);
          svn_client_conflict_t *conflict;

          svn_pool_clear(iterpool);

          SVN_ERR(svn_client_conflict_get(&conflict, desc->local_abspath,
                                          receiver_baton->ctx,
                                          iterpool, iterpool));
          SVN_ERR(svn_cl__append_conflict_info_xml(sb, conflict, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  if (info->lock)
    svn_cl__print_xml_lock(&sb, info->lock, pool);

  /* "</entry>" */
  svn_xml_make_close_tag(&sb, pool, "entry");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* A callback of type svn_client_info_receiver2_t. */
static svn_error_t *
print_info(void *baton,
           const char *target,
           const svn_client_info2_t *info,
           apr_pool_t *pool)
{
  print_info_baton_t *const receiver_baton = baton;

  SVN_ERR(svn_cmdline_printf(pool, _("Path: %s\n"),
                             svn_cl__local_style_skip_ancestor(
                               receiver_baton->path_prefix, target, pool)));

  /* ### remove this someday:  it's only here for cmdline output
     compatibility with svn 1.1 and older.  */
  if (info->kind != svn_node_dir)
    SVN_ERR(svn_cmdline_printf(pool, _("Name: %s\n"),
                               svn_dirent_basename(target, pool)));

  if (info->wc_info && info->wc_info->wcroot_abspath)
    SVN_ERR(svn_cmdline_printf(pool, _("Working Copy Root Path: %s\n"),
                               svn_dirent_local_style(
                                            info->wc_info->wcroot_abspath,
                                            pool)));

  if (info->URL)
    SVN_ERR(svn_cmdline_printf(pool, _("URL: %s\n"), info->URL));

  if (info->URL && info->repos_root_URL)
    SVN_ERR(svn_cmdline_printf(pool, _("Relative URL: %s\n"),
                               relative_url(info, pool)));

  if (info->repos_root_URL)
    SVN_ERR(svn_cmdline_printf(pool, _("Repository Root: %s\n"),
                               info->repos_root_URL));

  if (info->repos_UUID)
    SVN_ERR(svn_cmdline_printf(pool, _("Repository UUID: %s\n"),
                               info->repos_UUID));

  if (SVN_IS_VALID_REVNUM(info->rev))
    SVN_ERR(svn_cmdline_printf(pool, _("Revision: %ld\n"), info->rev));

  switch (info->kind)
    {
    case svn_node_file:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: file\n")));
      break;

    case svn_node_dir:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: directory\n")));
      break;

    case svn_node_none:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: none\n")));
      break;

    case svn_node_unknown:
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: unknown\n")));
      break;
    }

  if (info->wc_info)
    {
      switch (info->wc_info->schedule)
        {
        case svn_wc_schedule_normal:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: normal\n")));
          break;

        case svn_wc_schedule_add:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: add\n")));
          break;

        case svn_wc_schedule_delete:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: delete\n")));
          break;

        case svn_wc_schedule_replace:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: replace\n")));
          break;

        default:
          break;
        }

      switch (info->wc_info->depth)
        {
        case svn_depth_unknown:
          /* Unknown depth is the norm for remote directories anyway
             (although infinity would be equally appropriate).  Let's
             not bother to print it. */
          break;

        case svn_depth_empty:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: empty\n")));
          break;

        case svn_depth_files:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: files\n")));
          break;

        case svn_depth_immediates:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: immediates\n")));
          break;

        case svn_depth_exclude:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: exclude\n")));
          break;

        case svn_depth_infinity:
          /* Infinity is the default depth for working copy
             directories.  Let's not print it, it's not special enough
             to be worth mentioning.  */
          break;

        default:
          /* Other depths should never happen here. */
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: INVALID\n")));
        }

      if (info->wc_info->copyfrom_url)
        SVN_ERR(svn_cmdline_printf(pool, _("Copied From URL: %s\n"),
                                   info->wc_info->copyfrom_url));

      if (SVN_IS_VALID_REVNUM(info->wc_info->copyfrom_rev))
        SVN_ERR(svn_cmdline_printf(pool, _("Copied From Rev: %ld\n"),
                                   info->wc_info->copyfrom_rev));
      if (info->wc_info->moved_from_abspath)
        SVN_ERR(svn_cmdline_printf(pool, _("Moved From: %s\n"),
                                   svn_cl__local_style_skip_ancestor(
                                      receiver_baton->path_prefix,
                                      info->wc_info->moved_from_abspath,
                                      pool)));

      if (info->wc_info->moved_to_abspath)
        SVN_ERR(svn_cmdline_printf(pool, _("Moved To: %s\n"),
                                   svn_cl__local_style_skip_ancestor(
                                      receiver_baton->path_prefix,
                                      info->wc_info->moved_to_abspath,
                                      pool)));
    }

  if (info->last_changed_author)
    SVN_ERR(svn_cmdline_printf(pool, _("Last Changed Author: %s\n"),
                               info->last_changed_author));

  if (SVN_IS_VALID_REVNUM(info->last_changed_rev))
    SVN_ERR(svn_cmdline_printf(pool, _("Last Changed Rev: %ld\n"),
                               info->last_changed_rev));

  if (info->last_changed_date)
    SVN_ERR(svn_cl__info_print_time(info->last_changed_date,
                                    _("Last Changed Date"), pool));

  if (info->wc_info)
    {
      if (info->wc_info->recorded_time)
        SVN_ERR(svn_cl__info_print_time(info->wc_info->recorded_time,
                                        _("Text Last Updated"), pool));

      if (info->wc_info->checksum)
        SVN_ERR(svn_cmdline_printf(pool, _("Checksum: %s\n"),
                                   svn_checksum_to_cstring(
                                              info->wc_info->checksum, pool)));

      if (info->wc_info->conflicts)
        {
          svn_boolean_t printed_tc = FALSE;
          svn_stringbuf_t *conflicted_props = NULL;
          svn_client_conflict_t *conflict;
          svn_boolean_t text_conflicted;
          apr_array_header_t *props_conflicted;
          svn_boolean_t tree_conflicted;
          const svn_wc_conflict_description2_t *desc2 =
                APR_ARRAY_IDX(info->wc_info->conflicts, 0,
                              const svn_wc_conflict_description2_t *);

          SVN_ERR(svn_client_conflict_get(&conflict, desc2->local_abspath,
                                          receiver_baton->ctx, pool, pool));
          SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                                     &props_conflicted,
                                                     &tree_conflicted,
                                                     conflict, pool, pool));
          if (text_conflicted)
            {
              const char *base_abspath = NULL;
              const char *my_abspath = NULL;
              const char *their_abspath = NULL;

              SVN_ERR(svn_client_conflict_text_get_contents(
                        NULL, &my_abspath, &base_abspath, &their_abspath,
                        conflict, pool, pool));

              if (base_abspath)
                SVN_ERR(svn_cmdline_printf(pool,
                          _("Conflict Previous Base File: %s\n"),
                          svn_cl__local_style_skip_ancestor(
                                  receiver_baton->path_prefix,
                                  base_abspath,
                                  pool)));

              if (my_abspath)
                SVN_ERR(svn_cmdline_printf(pool,
                          _("Conflict Previous Working File: %s\n"),
                          svn_cl__local_style_skip_ancestor(
                                  receiver_baton->path_prefix,
                                  my_abspath,
                                  pool)));

              if (their_abspath)
                SVN_ERR(svn_cmdline_printf(pool,
                          _("Conflict Current Base File: %s\n"),
                          svn_cl__local_style_skip_ancestor(
                                  receiver_baton->path_prefix,
                                  their_abspath,
                                  pool)));
            }

          if (props_conflicted)
            {
              int i;

              for (i = 0; i < props_conflicted->nelts; i++)
                {
                  const char *name;

                  name = APR_ARRAY_IDX(props_conflicted, i, const char *);
                  if (conflicted_props == NULL)
                    conflicted_props = svn_stringbuf_create(name, pool);
                  else
                    {
                      svn_stringbuf_appendbyte(conflicted_props, ' ');
                      svn_stringbuf_appendcstr(conflicted_props, name);
                    }
                }
            }

          if (tree_conflicted)
            {
              const char *desc;

              printed_tc = TRUE;
              SVN_ERR(
                  svn_cl__get_human_readable_tree_conflict_description(
                                              &desc, conflict, pool));

              SVN_ERR(svn_cmdline_printf(pool, "%s: %s\n",
                                         _("Tree conflict"), desc));
            }

          if (conflicted_props)
            SVN_ERR(svn_cmdline_printf(pool, _("Conflicted Properties: %s\n"),
                                       conflicted_props->data));

          /* We only store one left and right version for all conflicts, which is
             referenced from all conflicts.
             Print it after the conflicts to match the 1.6/1.7 output where it is
             only available for tree conflicts */
          {
            const char *src_left_version;
            const char *src_right_version;
            const char *repos_root_url;
            const char *repos_relpath;
            svn_revnum_t peg_rev;
            svn_node_kind_t node_kind;

            if (!printed_tc)
              {
                const char *desc;

                SVN_ERR(svn_cl__get_human_readable_action_description(&desc,
                          svn_wc_conflict_action_edit,
                          svn_client_conflict_get_operation(conflict),
                          info->kind,
                          pool));

                SVN_ERR(svn_cmdline_printf(pool, "%s: %s\n",
                                               _("Conflict Details"), desc));
              }

            SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL,
                                                       conflict, pool, pool));
            SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(
                      &repos_relpath, &peg_rev, &node_kind, conflict,
                      pool, pool));
            src_left_version =
                        svn_cl__node_description(repos_root_url, repos_relpath,
                          peg_rev, node_kind, info->repos_root_URL, pool);

            SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(
                      &repos_relpath, &peg_rev, &node_kind, conflict,
                      pool, pool));
            src_right_version =
                        svn_cl__node_description(repos_root_url, repos_relpath,
                          peg_rev, node_kind, info->repos_root_URL, pool);

            if (src_left_version)
              SVN_ERR(svn_cmdline_printf(pool, "  %s: %s\n",
                                         _("Source  left"), /* (1) */
                                         src_left_version));
            /* (1): Sneaking in a space in "Source  left" so that
             * it is the same length as "Source right" while it still
             * starts in the same column. That's just a tiny tweak in
             * the English `svn'. */

            if (src_right_version)
              SVN_ERR(svn_cmdline_printf(pool, "  %s: %s\n",
                                         _("Source right"),
                                         src_right_version));
          }
        }
    }

  if (info->lock)
    {
      if (info->lock->token)
        SVN_ERR(svn_cmdline_printf(pool, _("Lock Token: %s\n"),
                                   info->lock->token));

      if (info->lock->owner)
        SVN_ERR(svn_cmdline_printf(pool, _("Lock Owner: %s\n"),
                                   info->lock->owner));

      if (info->lock->creation_date)
        SVN_ERR(svn_cl__info_print_time(info->lock->creation_date,
                                        _("Lock Created"), pool));

      if (info->lock->expiration_date)
        SVN_ERR(svn_cl__info_print_time(info->lock->expiration_date,
                                        _("Lock Expires"), pool));

      if (info->lock->comment)
        {
          int comment_lines;
          /* NOTE: The stdio will handle newline translation. */
          comment_lines = svn_cstring_count_newlines(info->lock->comment) + 1;
          SVN_ERR(svn_cmdline_printf(pool,
                                     Q_("Lock Comment (%i line):\n%s\n",
                                        "Lock Comment (%i lines):\n%s\n",
                                        comment_lines),
                                     comment_lines,
                                     info->lock->comment));
        }
    }

  if (info->wc_info && info->wc_info->changelist)
    SVN_ERR(svn_cmdline_printf(pool, _("Changelist: %s\n"),
                               info->wc_info->changelist));

  /* Print extra newline separator. */
  return svn_cmdline_printf(pool, "\n");
}


/* Helper for print_info_item(): Print the value TEXT for TARGET_PATH,
   either of which may be NULL. Use POOL for temporary allocation. */
static svn_error_t *
print_info_item_string(const char *text, const char *target_path,
                       apr_pool_t *pool)
{
  if (text)
    {
      if (target_path)
        SVN_ERR(svn_cmdline_printf(pool, "%-10s %s", text, target_path));
      else
        SVN_ERR(svn_cmdline_fputs(text, stdout, pool));
    }
  else if (target_path)
    SVN_ERR(svn_cmdline_printf(pool, "%-10s %s", "", target_path));

  return SVN_NO_ERROR;
}

/* Helper for print_info_item(): Print the revision number REV, which
   may be SVN_INVALID_REVNUM, for TARGET_PATH, which may be NULL. Use
   POOL for temporary allocation. */
static svn_error_t *
print_info_item_revision(svn_revnum_t rev, const char *target_path,
                         apr_pool_t *pool)
{
  if (SVN_IS_VALID_REVNUM(rev))
    {
      if (target_path)
        SVN_ERR(svn_cmdline_printf(pool, "%-10ld %s", rev, target_path));
      else
        SVN_ERR(svn_cmdline_printf(pool, "%ld", rev));
    }
  else if (target_path)
    SVN_ERR(svn_cmdline_printf(pool, "%-10s %s", "", target_path));

  return SVN_NO_ERROR;
}

/* A callback of type svn_client_info_receiver2_t. */
static svn_error_t *
print_info_item(void *baton,
                  const char *target,
                  const svn_client_info2_t *info,
                  apr_pool_t *pool)
{
  print_info_baton_t *const receiver_baton = baton;
  const char *const target_path =
    (!receiver_baton->multiple_targets ? NULL
     : (!receiver_baton->target_is_path ? info->URL
        : svn_cl__local_style_skip_ancestor(
            receiver_baton->path_prefix, target, pool)));

  if (receiver_baton->start_new_line)
    SVN_ERR(svn_cmdline_fputs("\n", stdout, pool));

  switch (receiver_baton->print_what)
    {
    case info_item_kind:
      SVN_ERR(print_info_item_string(svn_node_kind_to_word(info->kind),
                                     target_path, pool));
      break;

    case info_item_url:
      SVN_ERR(print_info_item_string(info->URL, target_path, pool));
      break;

    case info_item_relative_url:
      SVN_ERR(print_info_item_string(relative_url(info, pool),
                                     target_path, pool));
      break;

    case info_item_repos_root_url:
      SVN_ERR(print_info_item_string(info->repos_root_URL, target_path, pool));
      break;

    case info_item_repos_uuid:
      SVN_ERR(print_info_item_string(info->repos_UUID, target_path, pool));
      break;

    case info_item_revision:
      SVN_ERR(print_info_item_revision(info->rev, target_path, pool));
      break;

    case info_item_last_changed_rev:
      SVN_ERR(print_info_item_revision(info->last_changed_rev,
                                       target_path, pool));
      break;

    case info_item_last_changed_date:
      SVN_ERR(print_info_item_string(
                  (!info->last_changed_date ? NULL
                   : svn_time_to_cstring(info->last_changed_date, pool)),
                  target_path, pool));
      break;

    case info_item_last_changed_author:
      SVN_ERR(print_info_item_string(info->last_changed_author,
                                     target_path, pool));
      break;

    case info_item_wc_root:
      SVN_ERR(print_info_item_string(
                  (info->wc_info && info->wc_info->wcroot_abspath
                   ? info->wc_info->wcroot_abspath : NULL),
                  target_path, pool));
      break;

    default:
      SVN_ERR_MALFUNCTION();
    }

  receiver_baton->start_new_line = TRUE;
  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__info(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets = NULL;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;
  svn_error_t *err;
  svn_boolean_t seen_nonexistent_target = FALSE;
  svn_opt_revision_t peg_revision;
  svn_client_info_receiver2_t receiver;
  print_info_baton_t receiver_baton = { 0 };

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  receiver_baton.ctx = ctx;

  if (opt_state->xml)
    {
      receiver = print_info_xml;

      if (opt_state->show_item)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--show-item is not valid in --xml mode"));
      if (opt_state->no_newline)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--no-newline is not valid in --xml mode"));

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("info", pool));
    }
  else if (opt_state->show_item)
    {
      receiver = print_info_item;

      if (opt_state->incremental)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--incremental is only valid in --xml mode"));

      receiver_baton.multiple_targets = (opt_state->depth > svn_depth_empty
                                         || targets->nelts > 1);
      if (receiver_baton.multiple_targets && opt_state->no_newline)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--no-newline is only available for single-target,"
              " non-recursive info operations"));

      SVN_ERR(find_print_what(opt_state->show_item, &receiver_baton, pool));
      receiver_baton.start_new_line = FALSE;
    }
  else
    {
      receiver = print_info;

      if (opt_state->incremental)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--incremental is only valid in --xml mode"));
      if (opt_state->no_newline)
        return svn_error_create(
            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
            _("--no-newline' is only valid with --show-item"));
    }

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_empty;

  SVN_ERR(svn_dirent_get_absolute(&receiver_baton.path_prefix, "", pool));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *truepath;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target, subpool));

      /* If no peg-rev was attached to a URL target, then assume HEAD. */
      if (svn_path_is_url(truepath))
        {
          if (peg_revision.kind == svn_opt_revision_unspecified)
            peg_revision.kind = svn_opt_revision_head;
          receiver_baton.target_is_path = FALSE;
        }
      else
        {
          SVN_ERR(svn_dirent_get_absolute(&truepath, truepath, subpool));
          receiver_baton.target_is_path = TRUE;
        }

      err = svn_client_info4(truepath,
                             &peg_revision, &(opt_state->start_revision),
                             opt_state->depth,
                             TRUE /* fetch_excluded */,
                             TRUE /* fetch_actual_only */,
                             opt_state->include_externals,
                             opt_state->changelists,
                             receiver, &receiver_baton,
                             ctx, subpool);

      if (err)
        {
          /* If one of the targets is a non-existent URL or wc-entry,
             don't bail out.  Just warn and move on to the next target. */
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
              err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
            {
              svn_handle_warning2(stderr, err, "svn: ");
              svn_error_clear(svn_cmdline_fprintf(stderr, subpool, "\n"));
            }
          else
            {
              return svn_error_trace(err);
            }

          svn_error_clear(err);
          err = NULL;
          seen_nonexistent_target = TRUE;
        }
    }
  svn_pool_destroy(subpool);

  if (opt_state->xml && (! opt_state->incremental))
    SVN_ERR(svn_cl__xml_print_footer("info", pool));
  else if (opt_state->show_item && !opt_state->no_newline
           && receiver_baton.start_new_line)
    SVN_ERR(svn_cmdline_fputs("\n", stdout, pool));

  if (seen_nonexistent_target)
    return svn_error_create(
      SVN_ERR_ILLEGAL_TARGET, NULL,
      _("Could not display info for all targets because some "
        "targets don't exist"));
  else
    return SVN_NO_ERROR;
}
