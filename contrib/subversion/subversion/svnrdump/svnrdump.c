/*
 *  svnrdump.c: Produce a dumpfile of a local or remote repository
 *              without touching the filesystem, but for temporary files.
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

#include <apr_uri.h>

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_private_config.h"
#include "svn_string.h"
#include "svn_props.h"

#include "svnrdump.h"

#include "private/svn_repos_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_ra_private.h"



/*** Cancellation ***/

/* Our cancellation callback. */
static svn_cancel_func_t check_cancel = NULL;



static svn_opt_subcommand_t dump_cmd, load_cmd;

enum svn_svnrdump__longopt_t
  {
    opt_config_dir = SVN_OPT_FIRST_LONGOPT_ID,
    opt_config_option,
    opt_auth_username,
    opt_auth_password,
    opt_auth_password_from_stdin,
    opt_auth_nocache,
    opt_non_interactive,
    opt_skip_revprop,
    opt_force_interactive,
    opt_incremental,
    opt_trust_server_cert,
    opt_trust_server_cert_failures,
    opt_version
  };

#define SVN_SVNRDUMP__BASE_OPTIONS opt_config_dir, \
                                   opt_config_option, \
                                   opt_auth_username, \
                                   opt_auth_password, \
                                   opt_auth_password_from_stdin, \
                                   opt_auth_nocache, \
                                   opt_trust_server_cert, \
                                   opt_trust_server_cert_failures, \
                                   opt_non_interactive, \
                                   opt_force_interactive

static const svn_opt_subcommand_desc2_t svnrdump__cmd_table[] =
{
  { "dump", dump_cmd, { 0 },
    N_("usage: svnrdump dump URL [-r LOWER[:UPPER]]\n\n"
       "Dump revisions LOWER to UPPER of repository at remote URL to stdout\n"
       "in a 'dumpfile' portable format.  If only LOWER is given, dump that\n"
       "one revision.\n"),
    { 'r', 'q', opt_incremental, SVN_SVNRDUMP__BASE_OPTIONS } },
  { "load", load_cmd, { 0 },
    N_("usage: svnrdump load URL\n\n"
       "Load a 'dumpfile' given on stdin to a repository at remote URL.\n"),
    { 'q', opt_skip_revprop, SVN_SVNRDUMP__BASE_OPTIONS } },
  { "help", 0, { "?", "h" },
    N_("usage: svnrdump help [SUBCOMMAND...]\n\n"
       "Describe the usage of this program or its subcommands.\n"),
    { 0 } },
  { NULL, NULL, { 0 }, NULL, { 0 } }
};

static const apr_getopt_option_t svnrdump__options[] =
  {
    {"revision",     'r', 1,
                      N_("specify revision number ARG (or X:Y range)")},
    {"quiet",         'q', 0,
                      N_("no progress (only errors) to stderr")},
    {"incremental",   opt_incremental, 0,
                      N_("dump incrementally")},
    {"skip-revprop",  opt_skip_revprop, 1,
                      N_("skip revision property ARG (e.g., \"svn:author\")")},
    {"config-dir",    opt_config_dir, 1,
                      N_("read user configuration files from directory ARG")},
    {"username",      opt_auth_username, 1,
                      N_("specify a username ARG")},
    {"password",      opt_auth_password, 1,
                      N_("specify a password ARG")},
    {"password-from-stdin",   opt_auth_password_from_stdin, 0,
                      N_("read password from stdin")},
    {"non-interactive", opt_non_interactive, 0,
                      N_("do no interactive prompting (default is to prompt\n"
                         "                             "
                         "only if standard input is a terminal device)")},
    {"force-interactive", opt_force_interactive, 0,
                      N_("do interactive prompting even if standard input\n"
                         "                             "
                         "is not a terminal device")},
    {"no-auth-cache", opt_auth_nocache, 0,
                      N_("do not cache authentication tokens")},
    {"help",          'h', 0,
                      N_("display this help")},
    {"version",       opt_version, 0,
                      N_("show program version information")},
    {"config-option", opt_config_option, 1,
                      N_("set user configuration option in the format:\n"
                         "                             "
                         "    FILE:SECTION:OPTION=[VALUE]\n"
                         "                             "
                         "For example:\n"
                         "                             "
                         "    servers:global:http-library=serf")},
  {"trust-server-cert", opt_trust_server_cert, 0,
                    N_("deprecated; same as\n"
                       "                             "
                       "--trust-server-cert-failures=unknown-ca")},
  {"trust-server-cert-failures", opt_trust_server_cert_failures, 1,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates with failures; ARG is comma-separated\n"
                       "                             "
                       "list of 'unknown-ca' (Unknown Authority),\n"
                       "                             "
                       "'cn-mismatch' (Hostname mismatch), 'expired'\n"
                       "                             "
                       "(Expired certificate), 'not-yet-valid' (Not yet\n"
                       "                             "
                       "valid certificate) and 'other' (all other not\n"
                       "                             "
                       "separately classified certificate errors).")},
    {"dumpfile", 'F', 1, N_("Read or write to a dumpfile instead of stdin/stdout")},
    {0, 0, 0, 0}
  };

/* Baton for the RA replay session. */
struct replay_baton {
  /* A backdoor ra session for fetching information. */
  svn_ra_session_t *extra_ra_session;

  /* The output stream */
  svn_stream_t *stdout_stream;

  /* Whether to be quiet. */
  svn_boolean_t quiet;
};

/* Option set */
typedef struct opt_baton_t {
  svn_client_ctx_t *ctx;
  svn_ra_session_t *session;
  const char *url;
  const char *dumpfile;
  svn_boolean_t help;
  svn_boolean_t version;
  svn_opt_revision_t start_revision;
  svn_opt_revision_t end_revision;
  svn_boolean_t quiet;
  svn_boolean_t incremental;
  apr_hash_t *skip_revprops;
} opt_baton_t;

/* Print dumpstream-formatted information about REVISION.
 * Implements the `svn_ra_replay_revstart_callback_t' interface.
 */
static svn_error_t *
replay_revstart(svn_revnum_t revision,
                void *replay_baton,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_hash_t *rev_props,
                apr_pool_t *pool)
{
  struct replay_baton *rb = replay_baton;
  apr_hash_t *normal_props;

  /* Normalize and dump the revprops */
  SVN_ERR(svn_rdump__normalize_props(&normal_props, rev_props, pool));
  SVN_ERR(svn_repos__dump_revision_record(rb->stdout_stream, revision, NULL,
                                          normal_props,
                                          TRUE /*props_section_always*/,
                                          pool));

  SVN_ERR(svn_rdump__get_dump_editor(editor, edit_baton, revision,
                                     rb->stdout_stream, rb->extra_ra_session,
                                     NULL, check_cancel, NULL, pool));

  return SVN_NO_ERROR;
}

/* Print progress information about the dump of REVISION.
   Implements the `svn_ra_replay_revfinish_callback_t' interface. */
static svn_error_t *
replay_revend(svn_revnum_t revision,
              void *replay_baton,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_hash_t *rev_props,
              apr_pool_t *pool)
{
  /* No resources left to free. */
  struct replay_baton *rb = replay_baton;

  SVN_ERR(editor->close_edit(edit_baton, pool));

  if (! rb->quiet)
    SVN_ERR(svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n",
                                revision));
  return SVN_NO_ERROR;
}

#ifdef USE_EV2_IMPL
/* Print dumpstream-formatted information about REVISION.
 * Implements the `svn_ra_replay_revstart_callback_t' interface.
 */
static svn_error_t *
replay_revstart_v2(svn_revnum_t revision,
                   void *replay_baton,
                   svn_editor_t **editor,
                   apr_hash_t *rev_props,
                   apr_pool_t *pool)
{
  struct replay_baton *rb = replay_baton;
  apr_hash_t *normal_props;

  /* Normalize and dump the revprops */
  SVN_ERR(svn_rdump__normalize_props(&normal_props, rev_props, pool));
  SVN_ERR(svn_repos__dump_revision_record(rb->stdout_stream, revision,
                                          normal_props,
                                          TRUE /*props_section_always*/,
                                          pool));

  SVN_ERR(svn_rdump__get_dump_editor_v2(editor, revision,
                                        rb->stdout_stream,
                                        rb->extra_ra_session,
                                        NULL, check_cancel, NULL, pool, pool));

  return SVN_NO_ERROR;
}

/* Print progress information about the dump of REVISION.
   Implements the `svn_ra_replay_revfinish_callback_t' interface. */
static svn_error_t *
replay_revend_v2(svn_revnum_t revision,
                 void *replay_baton,
                 svn_editor_t *editor,
                 apr_hash_t *rev_props,
                 apr_pool_t *pool)
{
  /* No resources left to free. */
  struct replay_baton *rb = replay_baton;

  SVN_ERR(svn_editor_complete(editor));

  if (! rb->quiet)
    SVN_ERR(svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n",
                                revision));
  return SVN_NO_ERROR;
}
#endif

/* Initialize the RA layer, and set *CTX to a new client context baton
 * allocated from POOL.  Use CONFIG_DIR and pass USERNAME, PASSWORD,
 * CONFIG_DIR and NO_AUTH_CACHE to initialize the authorization baton.
 * CONFIG_OPTIONS (if not NULL) is a list of configuration overrides.
 * REPOS_URL is used to fiddle with server-specific configuration
 * options.
 */
static svn_error_t *
init_client_context(svn_client_ctx_t **ctx_p,
                    svn_boolean_t non_interactive,
                    const char *username,
                    const char *password,
                    const char *config_dir,
                    const char *repos_url,
                    svn_boolean_t no_auth_cache,
                    svn_boolean_t trust_unknown_ca,
                    svn_boolean_t trust_cn_mismatch,
                    svn_boolean_t trust_expired,
                    svn_boolean_t trust_not_yet_valid,
                    svn_boolean_t trust_other_failure,
                    apr_array_header_t *config_options,
                    apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = NULL;
  svn_config_t *cfg_config, *cfg_servers;

  SVN_ERR(svn_ra_initialize(pool));

  SVN_ERR(svn_config_ensure(config_dir, pool));
  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));

  SVN_ERR(svn_config_get_config(&(ctx->config), config_dir, pool));

  if (config_options)
    SVN_ERR(svn_cmdline__apply_config_options(ctx->config, config_options,
                                              "svnrdump: ", "--config-option"));

  cfg_config = svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG);

  /* ### FIXME: This is a hack to work around the fact that our dump
     ### editor simply can't handle the way ra_serf violates the
     ### editor v1 drive ordering requirements.
     ###
     ### We'll override both the global value and server-specific one
     ### for the 'http-bulk-updates' and 'http-max-connections'
     ### options in order to get ra_serf to try a bulk-update if the
     ### server will allow it, or at least try to limit all its
     ### auxiliary GETs/PROPFINDs to happening (well-ordered) on a
     ### single server connection.
     ###
     ### See http://subversion.tigris.org/issues/show_bug.cgi?id=4116.
  */
  cfg_servers = svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_SERVERS);
  svn_config_set_bool(cfg_servers, SVN_CONFIG_SECTION_GLOBAL,
                      SVN_CONFIG_OPTION_HTTP_BULK_UPDATES, TRUE);
  svn_config_set_int64(cfg_servers, SVN_CONFIG_SECTION_GLOBAL,
                       SVN_CONFIG_OPTION_HTTP_MAX_CONNECTIONS, 2);
  if (cfg_servers)
    {
      apr_status_t status;
      apr_uri_t parsed_url;

      status = apr_uri_parse(pool, repos_url, &parsed_url);
      if (! status)
        {
          const char *server_group;

          server_group = svn_config_find_group(cfg_servers, parsed_url.hostname,
                                               SVN_CONFIG_SECTION_GROUPS, pool);
          if (server_group)
            {
              svn_config_set_bool(cfg_servers, server_group,
                                  SVN_CONFIG_OPTION_HTTP_BULK_UPDATES, TRUE);
              svn_config_set_int64(cfg_servers, server_group,
                                   SVN_CONFIG_OPTION_HTTP_MAX_CONNECTIONS, 2);
            }
        }
    }

  /* Set up our cancellation support. */
  ctx->cancel_func = check_cancel;

  /* Default authentication providers for non-interactive use */
  SVN_ERR(svn_cmdline_create_auth_baton2(&(ctx->auth_baton), non_interactive,
                                         username, password, config_dir,
                                         no_auth_cache, trust_unknown_ca,
                                         trust_cn_mismatch, trust_expired,
                                         trust_not_yet_valid,
                                         trust_other_failure,
                                         cfg_config, ctx->cancel_func,
                                         ctx->cancel_baton, pool));
  *ctx_p = ctx;
  return SVN_NO_ERROR;
}

/* Print a revision record header for REVISION to STDOUT_STREAM.  Use
 * SESSION to contact the repository for revision properties and
 * such.
 */
static svn_error_t *
dump_revision_header(svn_ra_session_t *session,
                     svn_stream_t *stdout_stream,
                     svn_revnum_t revision,
                     apr_pool_t *pool)
{
  apr_hash_t *prophash;

  SVN_ERR(svn_ra_rev_proplist(session, revision, &prophash, pool));
  SVN_ERR(svn_repos__dump_revision_record(stdout_stream, revision, NULL,
                                          prophash,
                                          TRUE /*props_section_always*/,
                                          pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
dump_initial_full_revision(svn_ra_session_t *session,
                           svn_ra_session_t *extra_ra_session,
                           svn_stream_t *stdout_stream,
                           svn_revnum_t revision,
                           svn_boolean_t quiet,
                           apr_pool_t *pool)
{
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const svn_delta_editor_t *dump_editor;
  void *dump_baton;
  const char *session_url, *source_relpath;

  /* Determine whether we're dumping the repository root URL or some
     child thereof.  If we're dumping a subtree of the repository
     rather than the root, we have to jump through some hoops to make
     our update-driven dump generation work the way a replay-driven
     one would.

     See http://subversion.tigris.org/issues/show_bug.cgi?id=4101
  */
  SVN_ERR(svn_ra_get_session_url(session, &session_url, pool));
  SVN_ERR(svn_ra_get_path_relative_to_root(session, &source_relpath,
                                           session_url, pool));

  /* Start with a revision record header. */
  SVN_ERR(dump_revision_header(session, stdout_stream, revision, pool));

  /* Then, we'll drive the dump editor with what would look like a
     full checkout of the repository as it looked in START_REVISION.
     We do this by manufacturing a basic 'report' to the update
     reporter, telling it that we have nothing to start with.  The
     delta between nothing and everything-at-REV is, effectively, a
     full dump of REV. */
  SVN_ERR(svn_rdump__get_dump_editor(&dump_editor, &dump_baton, revision,
                                     stdout_stream, extra_ra_session,
                                     source_relpath, check_cancel, NULL, pool));
  SVN_ERR(svn_ra_do_update3(session, &reporter, &report_baton, revision,
                            "", svn_depth_infinity, FALSE, FALSE,
                            dump_editor, dump_baton, pool, pool));
  SVN_ERR(reporter->set_path(report_baton, "", revision,
                             svn_depth_infinity, TRUE, NULL, pool));
  SVN_ERR(reporter->finish_report(report_baton, pool));

  /* All finished with START_REVISION! */
  if (! quiet)
    SVN_ERR(svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n",
                                revision));

  return SVN_NO_ERROR;
}

/* Replay revisions START_REVISION thru END_REVISION (inclusive) of
 * the repository URL at which SESSION is rooted, using callbacks
 * which generate Subversion repository dumpstreams describing the
 * changes made in those revisions.  If QUIET is set, don't generate
 * progress messages.
 */
static svn_error_t *
replay_revisions(svn_ra_session_t *session,
                 svn_ra_session_t *extra_ra_session,
                 svn_revnum_t start_revision,
                 svn_revnum_t end_revision,
                 svn_boolean_t quiet,
                 svn_boolean_t incremental,
                 const char *dumpfile,
                 apr_pool_t *pool)
{
  struct replay_baton *replay_baton;
  const char *uuid;
  svn_stream_t *output_stream;

  if (dumpfile)
    {
      SVN_ERR(svn_stream_open_writable(&output_stream, dumpfile, pool, pool));
    }
  else
    {
      SVN_ERR(svn_stream_for_stdout(&output_stream, pool));
    }

  replay_baton = apr_pcalloc(pool, sizeof(*replay_baton));
  replay_baton->stdout_stream = output_stream;
  replay_baton->extra_ra_session = extra_ra_session;
  replay_baton->quiet = quiet;

  /* Write the magic header and UUID */
  SVN_ERR(svn_stream_printf(output_stream, pool,
                            SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n\n",
                            SVN_REPOS_DUMPFILE_FORMAT_VERSION));
  SVN_ERR(svn_ra_get_uuid2(session, &uuid, pool));
  SVN_ERR(svn_stream_printf(output_stream, pool,
                            SVN_REPOS_DUMPFILE_UUID ": %s\n\n", uuid));

  /* Fake revision 0 if necessary */
  if (start_revision == 0)
    {
      SVN_ERR(dump_revision_header(session, output_stream,
                                   start_revision, pool));

      /* Revision 0 has no tree changes, so we're done. */
      if (! quiet)
        SVN_ERR(svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n",
                                    start_revision));
      start_revision++;

      /* If our first revision is 0, we can treat this as an
         incremental dump. */
      incremental = TRUE;
    }

  /* If what remains to be dumped is not going to be dumped
     incrementally, then dump the first revision in full. */
  if (!incremental)
    {
      SVN_ERR(dump_initial_full_revision(session, extra_ra_session,
                                         output_stream, start_revision,
                                         quiet, pool));
      start_revision++;
    }

  /* If there are still revisions left to be dumped, do so. */
  if (start_revision <= end_revision)
    {
#ifndef USE_EV2_IMPL
      SVN_ERR(svn_ra_replay_range(session, start_revision, end_revision,
                                  0, TRUE, replay_revstart, replay_revend,
                                  replay_baton, pool));
#else
      SVN_ERR(svn_ra__replay_range_ev2(session, start_revision, end_revision,
                                       0, TRUE, replay_revstart_v2,
                                       replay_revend_v2, replay_baton,
                                       NULL, NULL, NULL, NULL, pool));
#endif
    }

  return SVN_NO_ERROR;
}

/* Read a dumpstream from stdin, and use it to feed a loader capable
 * of transmitting that information to the repository located at URL
 * (to which SESSION has been opened).  AUX_SESSION is a second RA
 * session opened to the same URL for performing auxiliary out-of-band
 * operations.
 */
static svn_error_t *
load_revisions(svn_ra_session_t *session,
               svn_ra_session_t *aux_session,
               const char *dumpfile,
               svn_boolean_t quiet,
               apr_hash_t *skip_revprops,
               apr_pool_t *pool)
{
  svn_stream_t *output_stream;

  if (dumpfile)
    {
      SVN_ERR(svn_stream_open_readonly(&output_stream, dumpfile, pool, pool));
    }
  else
    {
      SVN_ERR(svn_stream_for_stdin2(&output_stream, TRUE, pool));
    }

  SVN_ERR(svn_rdump__load_dumpstream(output_stream, session, aux_session,
                                     quiet, skip_revprops,
                                     check_cancel, NULL, pool));

  return SVN_NO_ERROR;
}

/* Return a program name for this program, the basename of the path
 * represented by PROGNAME if not NULL; use "svnrdump" otherwise.
 */
static const char *
ensure_appname(const char *progname,
               apr_pool_t *pool)
{
  if (!progname)
    return "svnrdump";

  return svn_dirent_basename(svn_dirent_internal_style(progname, pool), NULL);
}

/* Print a simple usage string. */
static svn_error_t *
usage(const char *progname,
      apr_pool_t *pool)
{
  return svn_cmdline_fprintf(stderr, pool,
                             _("Type '%s help' for usage.\n"),
                             ensure_appname(progname, pool));
}

/* Print information about the version of this program and dependent
 * modules.
 */
static svn_error_t *
version(const char *progname,
        svn_boolean_t quiet,
        apr_pool_t *pool)
{
  svn_stringbuf_t *version_footer =
    svn_stringbuf_create(_("The following repository access (RA) modules "
                           "are available:\n\n"),
                         pool);

  SVN_ERR(svn_ra_print_modules(version_footer, pool));
  return svn_opt_print_help4(NULL, ensure_appname(progname, pool),
                             TRUE, quiet, FALSE, version_footer->data,
                             NULL, NULL, NULL, NULL, NULL, pool);
}


/* Handle the "dump" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
dump_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;
  svn_ra_session_t *extra_ra_session;
  const char *repos_root;

  SVN_ERR(svn_client_open_ra_session2(&extra_ra_session,
                                      opt_baton->url, NULL,
                                      opt_baton->ctx, pool, pool));
  SVN_ERR(svn_ra_get_repos_root2(extra_ra_session, &repos_root, pool));
  SVN_ERR(svn_ra_reparent(extra_ra_session, repos_root, pool));

  return replay_revisions(opt_baton->session, extra_ra_session,
                          opt_baton->start_revision.value.number,
                          opt_baton->end_revision.value.number,
                          opt_baton->quiet, opt_baton->incremental,
                          opt_baton->dumpfile, pool);
}

/* Handle the "load" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
load_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;
  svn_ra_session_t *aux_session;

  SVN_ERR(svn_client_open_ra_session2(&aux_session, opt_baton->url, NULL,
                                      opt_baton->ctx, pool, pool));
  return load_revisions(opt_baton->session, aux_session,
                        opt_baton->dumpfile, opt_baton->quiet,
                        opt_baton->skip_revprops, pool);
}

/* Handle the "help" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
help_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  const char *header =
    _("general usage: svnrdump SUBCOMMAND URL [-r LOWER[:UPPER]]\n"
      "Subversion remote repository dump and load tool.\n"
      "Type 'svnrdump help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnrdump --version' to see the program version and RA modules.\n"
      "\n"
      "Available subcommands:\n");

  return svn_opt_print_help4(os, "svnrdump", FALSE, FALSE, FALSE, NULL,
                             header, svnrdump__cmd_table, svnrdump__options,
                             NULL, NULL, pool);
}

/* Examine the OPT_BATON's 'start_revision' and 'end_revision'
 * members, making sure that they make sense (in general, and as
 * applied to a repository whose current youngest revision is
 * LATEST_REVISION).
 */
static svn_error_t *
validate_and_resolve_revisions(opt_baton_t *opt_baton,
                               svn_revnum_t latest_revision,
                               apr_pool_t *pool)
{
  svn_revnum_t provided_start_rev = SVN_INVALID_REVNUM;

  /* Ensure that the start revision is something we can handle.  We
     want a number >= 0.  If unspecified, make it a number (r0) --
     anything else is bogus.  */
  if (opt_baton->start_revision.kind == svn_opt_revision_number)
    {
      provided_start_rev = opt_baton->start_revision.value.number;
    }
  else if (opt_baton->start_revision.kind == svn_opt_revision_head)
    {
      opt_baton->start_revision.kind = svn_opt_revision_number;
      opt_baton->start_revision.value.number = latest_revision;
    }
  else if (opt_baton->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_baton->start_revision.kind = svn_opt_revision_number;
      opt_baton->start_revision.value.number = 0;
    }

  if (opt_baton->start_revision.kind != svn_opt_revision_number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Unsupported revision specifier used; use "
                                "only integer values or 'HEAD'"));
    }

  if ((opt_baton->start_revision.value.number < 0) ||
      (opt_baton->start_revision.value.number > latest_revision))
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Revision '%ld' does not exist"),
                               opt_baton->start_revision.value.number);
    }

  /* Ensure that the end revision is something we can handle.  We want
     a number <= the youngest, and > the start revision.  If
     unspecified, make it a number (start_revision + 1 if that was
     specified, the youngest revision in the repository otherwise) --
     anything else is bogus.  */
  if (opt_baton->end_revision.kind == svn_opt_revision_unspecified)
    {
      opt_baton->end_revision.kind = svn_opt_revision_number;
      if (SVN_IS_VALID_REVNUM(provided_start_rev))
        opt_baton->end_revision.value.number = provided_start_rev;
      else
        opt_baton->end_revision.value.number = latest_revision;
    }
  else if (opt_baton->end_revision.kind == svn_opt_revision_head)
    {
      opt_baton->end_revision.kind = svn_opt_revision_number;
      opt_baton->end_revision.value.number = latest_revision;
    }

  if (opt_baton->end_revision.kind != svn_opt_revision_number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Unsupported revision specifier used; use "
                                "only integer values or 'HEAD'"));
    }

  if ((opt_baton->end_revision.value.number < 0) ||
      (opt_baton->end_revision.value.number > latest_revision))
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Revision '%ld' does not exist"),
                               opt_baton->end_revision.value.number);
    }

  /* Finally, make sure that the end revision is younger than the
     start revision.  We don't do "backwards" 'round here.  */
  if (opt_baton->end_revision.value.number <
      opt_baton->start_revision.value.number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("LOWER revision cannot be greater than "
                                "UPPER revision; consider reversing your "
                                "revision range"));
    }
  return SVN_NO_ERROR;
}

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  opt_baton_t *opt_baton;
  svn_revnum_t latest_revision = SVN_INVALID_REVNUM;
  const char *config_dir = NULL;
  const char *username = NULL;
  const char *password = NULL;
  svn_boolean_t no_auth_cache = FALSE;
  svn_boolean_t trust_unknown_ca = FALSE;
  svn_boolean_t trust_cn_mismatch = FALSE;
  svn_boolean_t trust_expired = FALSE;
  svn_boolean_t trust_not_yet_valid = FALSE;
  svn_boolean_t trust_other_failure = FALSE;
  svn_boolean_t non_interactive = FALSE;
  svn_boolean_t force_interactive = FALSE;
  apr_array_header_t *config_options = NULL;
  apr_getopt_t *os;
  apr_array_header_t *received_opts;
  int i;
  svn_boolean_t read_pass_from_stdin = FALSE;

  opt_baton = apr_pcalloc(pool, sizeof(*opt_baton));
  opt_baton->start_revision.kind = svn_opt_revision_unspecified;
  opt_baton->end_revision.kind = svn_opt_revision_unspecified;
  opt_baton->url = NULL;
  opt_baton->skip_revprops = apr_hash_make(pool);
  opt_baton->dumpfile = NULL;

  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = TRUE; /* Options and arguments can be interleaved */

  /* Set up our cancellation support. */
  check_cancel = svn_cmdline__setup_cancellation_handler();

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  while (1)
    {
      int opt;
      const char *opt_arg;
      apr_status_t status = apr_getopt_long(os, svnrdump__options, &opt,
                                            &opt_arg);

      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          SVN_ERR(usage(argv[0], pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt;

      switch(opt)
        {
        case 'r':
          {
            /* Make sure we've not seen -r already. */
            if (opt_baton->start_revision.kind != svn_opt_revision_unspecified)
              {
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                        _("Multiple revision arguments "
                                          "encountered; try '-r N:M' instead "
                                          "of '-r N -r M'"));
              }
            /* Parse the -r argument. */
            if (svn_opt_parse_revision(&(opt_baton->start_revision),
                                       &(opt_baton->end_revision),
                                       opt_arg, pool) != 0)
              {
                const char *utf8_opt_arg;
                SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
                return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                         _("Syntax error in revision "
                                           "argument '%s'"), utf8_opt_arg);
              }
          }
          break;
        case 'q':
          opt_baton->quiet = TRUE;
          break;
        case opt_config_dir:
          config_dir = opt_arg;
          break;
        case opt_version:
          opt_baton->version = TRUE;
          break;
        case 'h':
          opt_baton->help = TRUE;
          break;
        case opt_auth_username:
          SVN_ERR(svn_utf_cstring_to_utf8(&username, opt_arg, pool));
          break;
        case opt_auth_password:
          SVN_ERR(svn_utf_cstring_to_utf8(&password, opt_arg, pool));
          break;
        case opt_auth_password_from_stdin:
          read_pass_from_stdin = TRUE;
          break;
        case opt_auth_nocache:
          no_auth_cache = TRUE;
          break;
        case opt_non_interactive:
          non_interactive = TRUE;
          break;
        case opt_force_interactive:
          force_interactive = TRUE;
          break;
        case opt_incremental:
          opt_baton->incremental = TRUE;
          break;
        case opt_skip_revprop:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
          svn_hash_sets(opt_baton->skip_revprops, opt_arg, opt_arg);
          break;
        case opt_trust_server_cert: /* backward compat */
          trust_unknown_ca = TRUE;
          break;
        case opt_trust_server_cert_failures:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
          SVN_ERR(svn_cmdline__parse_trust_options(
                      &trust_unknown_ca,
                      &trust_cn_mismatch,
                      &trust_expired,
                      &trust_not_yet_valid,
                      &trust_other_failure,
                      opt_arg, pool));
          break;
        case opt_config_option:
          if (!config_options)
              config_options =
                    apr_array_make(pool, 1,
                                   sizeof(svn_cmdline__config_argument_t*));

            SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVN_ERR(svn_cmdline__parse_config_option(config_options,
                                                     opt_arg, 
                                                     "svnrdump: ",
                                                     pool));
          break;
        case 'F':
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
          opt_baton->dumpfile = opt_arg;
          break;
        }
    }

  /* The --non-interactive and --force-interactive options are mutually
   * exclusive. */
  if (non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }

  if (opt_baton->help)
    {
      subcommand = svn_opt_get_canonical_subcommand2(svnrdump__cmd_table,
                                                     "help");
    }
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_baton->version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", help_cmd, {0}, "",
                  {opt_version,  /* must accept its own option */
                   'q',  /* --quiet */
                  } };
              subcommand = &pseudo_cmd;
            }

          else
            {
              SVN_ERR(help_cmd(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(svnrdump__cmd_table,
                                                         first_arg);

          if (subcommand == NULL)
            {
              svn_error_clear(
                svn_cmdline_fprintf(stderr, pool,
                                    _("Unknown subcommand: '%s'\n"),
                                    first_arg));
              SVN_ERR(help_cmd(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      int opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svnrdump__options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            SVN_ERR(help_cmd(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf(
                                stderr, pool,
                                _("Subcommand '%s' doesn't accept option '%s'\n"
                                  "Type 'svnrdump help %s' for usage.\n"),
                                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  if (strcmp(subcommand->name, "--version") == 0)
    {
      SVN_ERR(version(argv[0], opt_baton->quiet, pool));
      return SVN_NO_ERROR;
    }

  if (strcmp(subcommand->name, "help") == 0)
    {
      SVN_ERR(help_cmd(os, opt_baton, pool));
      return SVN_NO_ERROR;
    }

  /* --trust-* can only be used with --non-interactive */
  if (!non_interactive)
    {
      if (trust_unknown_ca || trust_cn_mismatch || trust_expired
          || trust_not_yet_valid || trust_other_failure)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-server-cert-failures requires "
                                  "--non-interactive"));
    }

  if (read_pass_from_stdin && !non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--password-from-stdin requires "
                                "--non-interactive"));
    }

  if (strcmp(subcommand->name, "load") == 0)
    {
      if (read_pass_from_stdin && opt_baton->dumpfile == NULL)
        {
          /* error here, since load cannot process a password over stdin */
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("load subcommand with "
                                    "--password-from-stdin requires -F"));
        }
    }

  /* Expect one more non-option argument:  the repository URL. */
  if (os->ind != os->argc - 1)
    {
      SVN_ERR(usage(argv[0], pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }
  else
    {
      const char *repos_url;

      SVN_ERR(svn_utf_cstring_to_utf8(&repos_url, os->argv[os->ind], pool));
      if (! svn_path_is_url(repos_url))
        {
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                                   "Target '%s' is not a URL",
                                   repos_url);
        }
      opt_baton->url = svn_uri_canonicalize(repos_url, pool);
    }

  if (strcmp(subcommand->name, "load") == 0)
    {
      /*
       * By default (no --*-interactive options given), the 'load' subcommand
       * is interactive unless username and password were provided on the
       * command line. This allows prompting for auth creds to work without
       * requiring users to remember to use --force-interactive.
       * See issue #3913, "svnrdump load is not working in interactive mode".
       */
      if (!non_interactive && !force_interactive)
        force_interactive = (username == NULL || password == NULL);
    }

  /* Get password from stdin if necessary */
  if (read_pass_from_stdin)
    {
      SVN_ERR(svn_cmdline__stdin_readline(&password, pool, pool));
    }

  non_interactive = !svn_cmdline__be_interactive(non_interactive,
                                                 force_interactive);

  SVN_ERR(init_client_context(&(opt_baton->ctx),
                              non_interactive,
                              username,
                              password,
                              config_dir,
                              opt_baton->url,
                              no_auth_cache,
                              trust_unknown_ca,
                              trust_cn_mismatch,
                              trust_expired,
                              trust_not_yet_valid,
                              trust_other_failure,
                              config_options,
                              pool));

  err = svn_client_open_ra_session2(&(opt_baton->session),
                                    opt_baton->url, NULL,
                                    opt_baton->ctx, pool, pool);

  /* Have sane opt_baton->start_revision and end_revision defaults if
     unspecified.  */
  if (!err)
    err = svn_ra_get_latest_revnum(opt_baton->session, &latest_revision, pool);

  /* Make sure any provided revisions make sense. */
  if (!err)
    err = validate_and_resolve_revisions(opt_baton, latest_revision, pool);

  /* Dispatch the subcommand */
  if (!err)
    err = (*subcommand->cmd_func)(os, opt_baton, pool);

  if (err && err->apr_err == SVN_ERR_AUTHN_FAILED && non_interactive)
    {
      return svn_error_quick_wrap(err,
                                  _("Authentication failed and interactive"
                                    " prompting is disabled; see the"
                                    " --force-interactive option"));
    }
  else if (err)
    return err;
  else
    return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("svnrdump", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svnrdump: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
