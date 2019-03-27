/*
 * export-cmd.c -- Subversion export command
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
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_cmdline.h"
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_string_private.h"
#include "private/svn_client_private.h"

/*** The export editor code. ***/

/* ---------------------------------------------------------------------- */

/*** A dedicated 'export' editor, which does no .svn/ accounting.  ***/

typedef struct edit_baton_t
{
  apr_int64_t file_count;
  apr_int64_t dir_count;
  apr_int64_t byte_count;
  apr_int64_t prop_count;
  apr_int64_t prop_byte_count;
} edit_baton_t;

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


/* Just ensure that the main export directory exists. */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}


/* Ensure the directory exists, and send feedback. */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **baton)
{
  edit_baton_t *eb = parent_baton;
  eb->dir_count++;

  *baton = parent_baton;
  return SVN_NO_ERROR;
}


/* Build a file baton. */
static svn_error_t *
add_file(const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **baton)
{
  edit_baton_t *eb = parent_baton;
  eb->file_count++;

  *baton = parent_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  edit_baton_t *eb = baton;
  if (window != NULL)
    eb->byte_count += window->tview_len;

  return SVN_NO_ERROR;
}

/* Write incoming data into the tmpfile stream */

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  *handler_baton = file_baton;
  *handler = window_handler;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  edit_baton_t *eb = file_baton;
  eb->prop_count++;
  eb->prop_byte_count += value->len;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  edit_baton_t *eb = dir_baton;
  eb->prop_count++;

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* Implement svn_write_fn_t, simply counting the incoming data. */
static svn_error_t *
file_write_handler(void *baton, const char *data, apr_size_t *len)
{
  edit_baton_t *eb = baton;
  eb->byte_count += *len;

  return SVN_NO_ERROR;
}

/*** Public Interfaces ***/

static svn_error_t *
bench_null_export(svn_revnum_t *result_rev,
                  const char *from_path_or_url,
                  svn_opt_revision_t *peg_revision,
                  svn_opt_revision_t *revision,
                  svn_depth_t depth,
                  void *baton,
                  svn_client_ctx_t *ctx,
                  svn_boolean_t quiet,
                  apr_pool_t *pool)
{
  svn_revnum_t edit_revision = SVN_INVALID_REVNUM;
  svn_boolean_t from_is_url = svn_path_is_url(from_path_or_url);

  SVN_ERR_ASSERT(peg_revision != NULL);
  SVN_ERR_ASSERT(revision != NULL);

  if (peg_revision->kind == svn_opt_revision_unspecified)
    peg_revision->kind = svn_path_is_url(from_path_or_url)
                       ? svn_opt_revision_head
                       : svn_opt_revision_working;

  if (revision->kind == svn_opt_revision_unspecified)
    revision = peg_revision;

  if (from_is_url || ! SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      svn_client__pathrev_t *loc;
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;
      edit_baton_t *eb = baton;

      /* Get the RA connection. */
      SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                                from_path_or_url, NULL,
                                                peg_revision,
                                                revision, ctx, pool));

      SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind, pool));

      if (kind == svn_node_file)
        {
          apr_hash_t *props;

          /* Since we don't use the editor, we must count "manually". */
          svn_stream_t *stream = svn_stream_create(eb, pool);
          svn_stream_set_write(stream, file_write_handler);
          eb->file_count++;

          /* Since you cannot actually root an editor at a file, we
           * manually drive a few functions of our editor. */

          /* Step outside the editor-likeness for a moment, to actually talk
           * to the repository. */
          /* ### note: the stream will not be closed */
          SVN_ERR(svn_ra_get_file(ra_session, "", loc->rev,
                                  stream, NULL, &props, pool));
        }
      else if (kind == svn_node_dir)
        {
          void *edit_baton = NULL;
          const svn_delta_editor_t *export_editor = NULL;
          const svn_ra_reporter3_t *reporter;
          void *report_baton;

          svn_delta_editor_t *editor = svn_delta_default_editor(pool);

          editor->set_target_revision = set_target_revision;
          editor->open_root = open_root;
          editor->add_directory = add_directory;
          editor->add_file = add_file;
          editor->apply_textdelta = apply_textdelta;
          editor->close_file = close_file;
          editor->change_file_prop = change_file_prop;
          editor->change_dir_prop = change_dir_prop;

          /* for ra_svn, we don't need an editior in quiet mode */
          if (!quiet || strncmp(loc->repos_root_url, "svn:", 4))
            SVN_ERR(svn_delta_get_cancellation_editor(ctx->cancel_func,
                                                      ctx->cancel_baton,
                                                      editor,
                                                      baton,
                                                      &export_editor,
                                                      &edit_baton,
                                                      pool));

          /* Manufacture a basic 'report' to the update reporter. */
          SVN_ERR(svn_ra_do_update3(ra_session,
                                    &reporter, &report_baton,
                                    loc->rev,
                                    "", /* no sub-target */
                                    depth,
                                    FALSE, /* don't want copyfrom-args */
                                    FALSE, /* don't want ignore_ancestry */
                                    export_editor, edit_baton,
                                    pool, pool));

          SVN_ERR(reporter->set_path(report_baton, "", loc->rev,
                                     /* Depth is irrelevant, as we're
                                        passing start_empty=TRUE anyway. */
                                     svn_depth_infinity,
                                     TRUE, /* "help, my dir is empty!" */
                                     NULL, pool));

          SVN_ERR(reporter->finish_report(report_baton, pool));

          /* We don't receive the "add directory" callback for the starting
           * node. */
          eb->dir_count++;
        }
      else if (kind == svn_node_none)
        {
          return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                   _("URL '%s' doesn't exist"),
                                   from_path_or_url);
        }
      /* kind == svn_node_unknown not handled */
    }


  if (result_rev)
    *result_rev = edit_revision;

  return SVN_NO_ERROR;
}


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__null_export(apr_getopt_t *os,
                    void *baton,
                    apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *from;
  apr_array_header_t *targets;
  svn_error_t *err;
  svn_opt_revision_t peg_revision;
  const char *truefrom;
  edit_baton_t eb = { 0 };

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* We want exactly 1 or 2 targets for this subcommand. */
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* The first target is the `from' path. */
  from = APR_ARRAY_IDX(targets, 0, const char *);

  /* Get the peg revision if present. */
  SVN_ERR(svn_opt_parse_path(&peg_revision, &truefrom, from, pool));

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_infinity;

  /* Do the export. */
  err = bench_null_export(NULL, truefrom, &peg_revision,
                          &(opt_state->start_revision),
                          opt_state->depth,
                          &eb,
                          ctx, opt_state->quiet, pool);

  if (!opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("%15s directories\n"
                                 "%15s files\n"
                                 "%15s bytes in files\n"
                                 "%15s properties\n"
                                 "%15s bytes in properties\n"),
                               svn__ui64toa_sep(eb.dir_count, ',', pool),
                               svn__ui64toa_sep(eb.file_count, ',', pool),
                               svn__ui64toa_sep(eb.byte_count, ',', pool),
                               svn__ui64toa_sep(eb.prop_count, ',', pool),
                               svn__ui64toa_sep(eb.prop_byte_count, ',', pool)));

  return svn_error_trace(err);
}
