/*
 * svnbench.c:  Subversion benchmark client.
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

#include <string.h>
#include <assert.h>

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "cl.h"

#include "private/svn_opt_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_string_private.h"
#include "private/svn_utf_private.h"

#include "svn_private_config.h"


/*** Option Processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum svn_cl__longopt_t {
  opt_auth_password = SVN_OPT_FIRST_LONGOPT_ID,
  opt_auth_password_from_stdin,
  opt_auth_username,
  opt_config_dir,
  opt_config_options,
  opt_depth,
  opt_no_auth_cache,
  opt_non_interactive,
  opt_stop_on_copy,
  opt_strict,
  opt_targets,
  opt_version,
  opt_with_revprop,
  opt_with_all_revprops,
  opt_with_no_revprops,
  opt_trust_server_cert,
  opt_trust_server_cert_failures,
  opt_changelist,
  opt_search
} svn_cl__longopt_t;


/* Option codes and descriptions for the command line client.
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_cl__options[] =
{
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"quiet",         'q', 0, N_("print nothing, or only summary information")},
  {"recursive",     'R', 0, N_("descend recursively, same as --depth=infinity")},
  {"non-recursive", 'N', 0, N_("obsolete; try --depth=files or --depth=immediates")},
  {"change",        'c', 1,
                    N_("the change made by revision ARG (like -r ARG-1:ARG)\n"
                       "                             "
                       "If ARG is negative this is like -r ARG:ARG-1\n"
                       "                             "
                       "If ARG is of the form ARG1-ARG2 then this is like\n"
                       "                             "
                       "ARG1:ARG2, where ARG1 is inclusive")},
  {"revision",      'r', 1,
                    N_("ARG (some commands also take ARG1:ARG2 range)\n"
                       "                             "
                       "A revision argument can be one of:\n"
                       "                             "
                       "   NUMBER       revision number\n"
                       "                             "
                       "   '{' DATE '}' revision at start of the date\n"
                       "                             "
                       "   'HEAD'       latest in repository\n"
                       "                             "
                       "   'BASE'       base rev of item's working copy\n"
                       "                             "
                       "   'COMMITTED'  last commit at or before BASE\n"
                       "                             "
                       "   'PREV'       revision just before COMMITTED")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1, N_("specify a password ARG")},
  {"password-from-stdin",
                    opt_auth_password_from_stdin, 0, N_("read password from stdin")},
  {"targets",       opt_targets, 1,
                    N_("pass contents of file ARG as additional args")},
  {"depth",         opt_depth, 1,
                    N_("limit operation by depth ARG ('empty', 'files',\n"
                       "                             "
                       "'immediates', or 'infinity')")},
  {"strict",        opt_strict, 0, N_("use strict semantics")},
  {"stop-on-copy",  opt_stop_on_copy, 0,
                    N_("do not cross copies while traversing history")},
  {"no-auth-cache", opt_no_auth_cache, 0,
                    N_("do not cache authentication tokens")},
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
  {"non-interactive", opt_non_interactive, 0,
                    N_("do no interactive prompting")},
  {"config-dir",    opt_config_dir, 1,
                    N_("read user configuration files from directory ARG")},
  {"config-option", opt_config_options, 1,
                    N_("set user configuration option in the format:\n"
                       "                             "
                       "    FILE:SECTION:OPTION=[VALUE]\n"
                       "                             "
                       "For example:\n"
                       "                             "
                       "    servers:global:http-library=serf")},
  {"limit",         'l', 1, N_("maximum number of log entries")},
  {"with-all-revprops",  opt_with_all_revprops, 0,
                    N_("retrieve all revision properties")},
  {"with-no-revprops",  opt_with_no_revprops, 0,
                    N_("retrieve no revision properties")},
  {"with-revprop",  opt_with_revprop, 1,
                    N_("set revision property ARG in new revision\n"
                       "                             "
                       "using the name[=value] format")},
  {"use-merge-history", 'g', 0,
                    N_("use/display additional information from merge\n"
                       "                             "
                       "history")},
  {"search", opt_search, 1,
                       N_("use ARG as search pattern (glob syntax)")},

  /* Long-opt Aliases
   *
   * These have NULL desriptions, but an option code that matches some
   * other option (whose description should probably mention its aliases).
  */

  {0,               0, 0, 0},
};



/*** Command dispatch. ***/

/* Our array of available subcommands.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * In most of the help text "PATH" is used where a working copy path is
 * required, "URL" where a repository URL is required and "TARGET" when
 * either a path or a url can be used.  Hmm, should this be part of the
 * help text?
 */

/* Options that apply to all commands.  (While not every command may
   currently require authentication or be interactive, allowing every
   command to take these arguments allows scripts to just pass them
   willy-nilly to every invocation of 'svn') . */
const int svn_cl__global_options[] =
{ opt_auth_username, opt_auth_password, opt_auth_password_from_stdin,
  opt_no_auth_cache, opt_non_interactive,
  opt_trust_server_cert, opt_trust_server_cert_failures,
  opt_config_dir, opt_config_options, 0
};

const svn_opt_subcommand_desc2_t svn_cl__cmd_table[] =
{
  { "help", svn_cl__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },
  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "null-blame", svn_cl__null_blame, {0}, N_
    ("Fetch all versions of a file in a batch.\n"
     "usage: null-blame [-rM:N] TARGET[@REV]...\n"
     "\n"
     "  With no revision range (same as -r0:REV), or with '-r M:N' where M < N,\n"
     "  annotate each line that is present in revision N of the file, with\n"
     "  the last revision at or before rN that changed or added the line,\n"
     "  looking back no further than rM.\n"
     "\n"
     "  With a reverse revision range '-r M:N' where M > N,\n"
     "  annotate each line that is present in revision N of the file, with\n"
     "  the next revision after rN that changed or deleted the line,\n"
     "  looking forward no further than rM.\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"
     "\n"
     "  Write the annotated result to standard output.\n"),
    {'r', 'g'} },

  { "null-export", svn_cl__null_export, {0}, N_
    ("Create an unversioned copy of a tree.\n"
     "usage: null-export [-r REV] URL[@PEGREV]\n"
     "\n"
     "  Exports a clean directory tree from the repository specified by\n"
     "  URL, at revision REV if it is given, otherwise at HEAD.\n"
     "\n"
     "  If specified, PEGREV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', 'q', 'N', opt_depth} },

  { "null-list", svn_cl__null_list, {"ls"}, N_
    ("List directory entries in the repository.\n"
     "usage: null-list [TARGET[@REV]...]\n"
     "\n"
     "  List each TARGET file and the contents of each TARGET directory as\n"
     "  they exist in the repository.  If TARGET is a working copy path, the\n"
     "  corresponding repository URL will be used. If specified, REV determines\n"
     "  in which revision the target is first looked up.\n"
     "\n"
     "  The default TARGET is '.', meaning the repository URL of the current\n"
     "  working directory.\n"
     "\n"
     "  With --verbose, the following fields will be fetched for each item:\n"
     "\n"
     "    Revision number of the last commit\n"
     "    Author of the last commit\n"
     "    If locked, the letter 'O'.  (Use 'svn info URL' to see details)\n"
     "    Size (in bytes)\n"
     "    Date and time of the last commit\n"),
    {'r', 'v', 'q', 'R', opt_depth, opt_search} },

  { "null-log", svn_cl__null_log, {0}, N_
    ("Fetch the log messages for a set of revision(s) and/or path(s).\n"
     "usage: 1. null-log [PATH][@REV]\n"
     "       2. null-log URL[@REV] [PATH...]\n"
     "\n"
     "  1. Fetch the log messages for the URL corresponding to PATH\n"
     "     (default: '.'). If specified, REV is the revision in which the\n"
     "     URL is first looked up, and the default revision range is REV:1.\n"
     "     If REV is not specified, the default revision range is BASE:1,\n"
     "     since the URL might not exist in the HEAD revision.\n"
     "\n"
     "  2. Fetch the log messages for the PATHs (default: '.') under URL.\n"
     "     If specified, REV is the revision in which the URL is first\n"
     "     looked up, and the default revision range is REV:1; otherwise,\n"
     "     the URL is looked up in HEAD, and the default revision range is\n"
     "     HEAD:1.\n"
     "\n"
     "  Multiple '-c' or '-r' options may be specified (but not a\n"
     "  combination of '-c' and '-r' options), and mixing of forward and\n"
     "  reverse ranges is allowed.\n"
     "\n"
     "  With -v, also print all affected paths with each log message.\n"
     "  With -q, don't print the log message body itself (note that this is\n"
     "  compatible with -v).\n"
     "\n"
     "  Each log message is printed just once, even if more than one of the\n"
     "  affected paths for that revision were explicitly requested.  Logs\n"
     "  follow copy history by default.  Use --stop-on-copy to disable this\n"
     "  behavior, which can be useful for determining branchpoints.\n"),
    {'r', 'q', 'v', 'g', 'c', opt_targets, opt_stop_on_copy,
     'l', opt_with_all_revprops, opt_with_no_revprops, opt_with_revprop,},
    {{opt_with_revprop, N_("retrieve revision property ARG")},
     {'c', N_("the change made in revision ARG")}} },

  { "null-info", svn_cl__null_info, {0}, N_
    ("Display information about a local or remote item.\n"
     "usage: null-info [TARGET[@REV]...]\n"
     "\n"
     "  Print information about each TARGET (default: '.').\n"
     "  TARGET may be either a working-copy path or URL.  If specified, REV\n"
     "  determines in which revision the target is first looked up.\n"),
    {'r', 'R', opt_depth, opt_targets, opt_changelist}
  },

  { NULL, NULL, {0}, NULL, {0} }
};


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_client", svn_client_version },
      { "svn_wc",     svn_wc_version },
      { "svn_ra",     svn_ra_version },
      { "svn_delta",  svn_delta_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}


/* Baton for ra_progress_func() callback. */
typedef struct ra_progress_baton_t
{
  apr_off_t bytes_transferred;
} ra_progress_baton_t;

/* Implements svn_ra_progress_notify_func_t. */
static void
ra_progress_func(apr_off_t progress,
                 apr_off_t total,
                 void *baton,
                 apr_pool_t *pool)
{
  ra_progress_baton_t *b = baton;
  b->bytes_transferred = progress;
}

/* Our cancellation callback. */
svn_cancel_func_t svn_cl__check_cancel = NULL;

/* Add a --search argument to OPT_STATE.
 * These options start a new search pattern group. */
static void
add_search_pattern_group(svn_cl__opt_state_t *opt_state,
                         const char *pattern,
                         apr_pool_t *result_pool)
{
  apr_array_header_t *group = NULL;

  if (opt_state->search_patterns == NULL)
    opt_state->search_patterns = apr_array_make(result_pool, 1,
                                                sizeof(apr_array_header_t *));

  group = apr_array_make(result_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(group, const char *) = pattern;
  APR_ARRAY_PUSH(opt_state->search_patterns, apr_array_header_t *) = group;
}


/*** Main. ***/

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  int opt_id;
  apr_getopt_t *os;
  svn_cl__opt_state_t opt_state = { 0, { 0 } };
  svn_client_ctx_t *ctx;
  apr_array_header_t *received_opts;
  int i;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  svn_cl__cmd_baton_t command_baton;
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config;
  svn_boolean_t descend = TRUE;
  svn_boolean_t use_notifier = TRUE;
  apr_time_t start_time, time_taken;
  ra_progress_baton_t ra_progress_baton = {0};
  svn_membuf_t buf;
  svn_boolean_t read_pass_from_stdin = FALSE;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Init the temporary buffer. */
  svn_membuf__create(&buf, 0, pool);

  /* Check library versions */
  SVN_ERR(check_lib_versions());

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      SVN_ERR(svn_wc_set_adm_dir("_svn", pool));
    }
#endif

  /* Initialize the RA library. */
  SVN_ERR(svn_ra_initialize(pool));

  /* Begin processing arguments. */
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
  opt_state.revision_ranges =
    apr_array_make(pool, 0, sizeof(svn_opt_revision_range_t *));
  opt_state.depth = svn_depth_unknown;

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      SVN_ERR(svn_cl__help(NULL, NULL, pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  /* Else, parse options. */
  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;
      const char *utf8_opt_arg;

      /* Parse the next option. */
      apr_status_t apr_err = apr_getopt_long(os, svn_cl__options, &opt_id,
                                             &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(svn_cl__help(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id) {
      case 'l':
        {
          err = svn_cstring_atoi(&opt_state.limit, opt_arg);
          if (err)
            {
              return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                      _("Non-numeric limit argument given"));
            }
          if (opt_state.limit <= 0)
            {
              return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                      _("Argument to --limit must be positive"));
            }
        }
        break;
      case 'c':
        {
          apr_array_header_t *change_revs =
            svn_cstring_split(opt_arg, ", \n\r\t\v", TRUE, pool);

          for (i = 0; i < change_revs->nelts; i++)
            {
              char *end;
              svn_revnum_t changeno, changeno_end;
              const char *change_str =
                APR_ARRAY_IDX(change_revs, i, const char *);
              const char *s = change_str;
              svn_boolean_t is_negative;

              /* Check for a leading minus to allow "-c -r42".
               * The is_negative flag is used to handle "-c -42" and "-c -r42".
               * The "-c r-42" case is handled by strtol() returning a
               * negative number. */
              is_negative = (*s == '-');
              if (is_negative)
                s++;

              /* Allow any number of 'r's to prefix a revision number. */
              while (*s == 'r')
                s++;
              changeno = changeno_end = strtol(s, &end, 10);
              if (end != s && *end == '-')
                {
                  if (changeno < 0 || is_negative)
                    {
                      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR,
                                               NULL,
                                               _("Negative number in range (%s)"
                                                 " not supported with -c"),
                                               change_str);
                    }
                  s = end + 1;
                  while (*s == 'r')
                    s++;
                  changeno_end = strtol(s, &end, 10);
                }
              if (end == change_str || *end != '\0')
                {
                  return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                           _("Non-numeric change argument (%s) "
                                             "given to -c"), change_str);
                }

              if (changeno == 0)
                {
                  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                          _("There is no change 0"));
                }

              if (is_negative)
                changeno = -changeno;

              /* Figure out the range:
                    -c N  -> -r N-1:N
                    -c -N -> -r N:N-1
                    -c M-N -> -r M-1:N for M < N
                    -c M-N -> -r M:N-1 for M > N
                    -c -M-N -> error (too confusing/no valid use case)
              */
              if (changeno > 0)
                {
                  if (changeno <= changeno_end)
                    changeno--;
                  else
                    changeno_end--;
                }
              else
                {
                  changeno = -changeno;
                  changeno_end = changeno - 1;
                }

              opt_state.used_change_arg = TRUE;
              APR_ARRAY_PUSH(opt_state.revision_ranges,
                             svn_opt_revision_range_t *)
                = svn_opt__revision_range_from_revnums(changeno, changeno_end,
                                                       pool);
            }
        }
        break;
      case 'r':
        opt_state.used_revision_arg = TRUE;
        if (svn_opt_parse_revision_to_range(opt_state.revision_ranges,
                                            opt_arg, pool) != 0)
          {
            SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
            return svn_error_createf(
                     SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                     _("Syntax error in revision argument '%s'"),
                     utf8_opt_arg);
          }
        break;
      case 'v':
        opt_state.verbose = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case opt_targets:
        {
          svn_stringbuf_t *buffer, *buffer_utf8;

          /* We need to convert to UTF-8 now, even before we divide
             the targets into an array, because otherwise we wouldn't
             know what delimiter to use for svn_cstring_split().  */

          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          SVN_ERR(svn_stringbuf_from_file2(&buffer, utf8_opt_arg, pool));
          SVN_ERR(svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool));
          opt_state.targets = svn_cstring_split(buffer_utf8->data, "\n\r",
                                                TRUE, pool);
        }
        break;
      case 'R':
        opt_state.depth = svn_depth_infinity;
        break;
      case 'N':
        descend = FALSE;
        break;
      case opt_depth:
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (err)
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                   _("Error converting depth "
                                     "from locale to UTF-8"));
        opt_state.depth = svn_depth_from_word(utf8_opt_arg);
        if (opt_state.depth == svn_depth_unknown
            || opt_state.depth == svn_depth_exclude)
          {
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("'%s' is not a valid depth; try "
                                       "'empty', 'files', 'immediates', "
                                       "or 'infinity'"),
                                     utf8_opt_arg);
          }
        break;
      case opt_version:
        opt_state.version = TRUE;
        break;
      case opt_auth_username:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.auth_username,
                                            opt_arg, pool));
        break;
      case opt_auth_password:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.auth_password,
                                            opt_arg, pool));
        break;
      case opt_auth_password_from_stdin:
        read_pass_from_stdin = TRUE;
        break;
      case opt_stop_on_copy:
        opt_state.stop_on_copy = TRUE;
        break;
      case opt_strict:
        opt_state.strict = TRUE;
        break;
      case opt_no_auth_cache:
        opt_state.no_auth_cache = TRUE;
        break;
      case opt_non_interactive:
        opt_state.non_interactive = TRUE;
        break;
      case opt_trust_server_cert: /* backwards compat to 1.8 */
        opt_state.trust_server_cert_unknown_ca = TRUE;
        break;
      case opt_trust_server_cert_failures:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_cmdline__parse_trust_options(
                      &opt_state.trust_server_cert_unknown_ca,
                      &opt_state.trust_server_cert_cn_mismatch,
                      &opt_state.trust_server_cert_expired,
                      &opt_state.trust_server_cert_not_yet_valid,
                      &opt_state.trust_server_cert_other_failure,
                      utf8_opt_arg, pool));
        break;
      case opt_config_dir:
        {
          const char *path_utf8;
          SVN_ERR(svn_utf_cstring_to_utf8(&path_utf8, opt_arg, pool));
          opt_state.config_dir = svn_dirent_internal_style(path_utf8, pool);
        }
        break;
      case opt_config_options:
        if (!opt_state.config_options)
          opt_state.config_options =
                   apr_array_make(pool, 1,
                                  sizeof(svn_cmdline__config_argument_t*));

        SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
        SVN_ERR(svn_cmdline__parse_config_option(opt_state.config_options,
                                                 opt_arg, "svnbench: ", pool));
        break;
      case opt_with_all_revprops:
        /* If --with-all-revprops is specified along with one or more
         * --with-revprops options, --with-all-revprops takes precedence. */
        opt_state.all_revprops = TRUE;
        break;
      case opt_with_no_revprops:
        opt_state.no_revprops = TRUE;
        break;
      case opt_with_revprop:
        SVN_ERR(svn_opt_parse_revprop(&opt_state.revprop_table,
                                          opt_arg, pool));
        break;
      case 'g':
        opt_state.use_merge_history = TRUE;
        break;
      case opt_search:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_utf__xfrm(&utf8_opt_arg, utf8_opt_arg,
                              strlen(utf8_opt_arg), TRUE, TRUE, &buf));
        add_search_pattern_group(&opt_state,
                                 apr_pstrdup(pool, utf8_opt_arg),
                                 pool);
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;
      }
    }

  /* ### This really belongs in libsvn_client.  The trouble is,
     there's no one place there to run it from, no
     svn_client_init().  We'd have to add it to all the public
     functions that a client might call.  It's unmaintainable to do
     initialization from within libsvn_client itself, but it seems
     burdensome to demand that all clients call svn_client_init()
     before calling any other libsvn_client function... On the other
     hand, the alternative is effectively to demand that they call
     svn_config_ensure() instead, so maybe we should have a generic
     init function anyway.  Thoughts?  */
  SVN_ERR(svn_config_ensure(opt_state.config_dir, pool));

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(svn_cl__cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", svn_cl__help, {0}, "",
                  {opt_version,    /* must accept its own option */
                   'q',            /* brief output */
                   'v',            /* verbose output */
                   opt_config_dir  /* all commands accept this */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Subcommand argument required\n")));
              SVN_ERR(svn_cl__help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(svn_cl__cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Unknown subcommand: '%s'\n"),
                                     first_arg));
              SVN_ERR(svn_cl__help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id,
                                             svn_cl__global_options))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svn_cl__options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            SVN_ERR(svn_cl__help(NULL, NULL, pool));
          else
            svn_error_clear(
              svn_cmdline_fprintf(
                stderr, pool, _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnbench help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* Only merge and log support multiple revisions/revision ranges. */
  if (subcommand->cmd_func != svn_cl__null_log)
    {
      if (opt_state.revision_ranges->nelts > 1)
        {
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("Multiple revision arguments "
                                    "encountered; can't specify -c twice, "
                                    "or both -c and -r"));
        }
    }

  /* Disallow simultaneous use of both --with-all-revprops and
     --with-no-revprops.  */
  if (opt_state.all_revprops && opt_state.no_revprops)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--with-all-revprops and --with-no-revprops "
                                "are mutually exclusive"));
    }

  /* Disallow simultaneous use of both --with-revprop and
     --with-no-revprops.  */
  if (opt_state.revprop_table && opt_state.no_revprops)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--with-revprop and --with-no-revprops "
                                "are mutually exclusive"));
    }

  /* --trust-* options can only be used with --non-interactive */
  if (!opt_state.non_interactive)
    {
      if (opt_state.trust_server_cert_unknown_ca
          || opt_state.trust_server_cert_cn_mismatch
          || opt_state.trust_server_cert_expired
          || opt_state.trust_server_cert_not_yet_valid
          || opt_state.trust_server_cert_other_failure)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-server-cert-failures requires "
                                  "--non-interactive"));
    }

  /* --password-from-stdin can only be used with --non-interactive */
  if (read_pass_from_stdin && !opt_state.non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--password-from-stdin requires "
                                "--non-interactive"));
    }

  /* Ensure that 'revision_ranges' has at least one item, and make
     'start_revision' and 'end_revision' match that item. */
  if (opt_state.revision_ranges->nelts == 0)
    {
      svn_opt_revision_range_t *range = apr_palloc(pool, sizeof(*range));
      range->start.kind = svn_opt_revision_unspecified;
      range->end.kind = svn_opt_revision_unspecified;
      APR_ARRAY_PUSH(opt_state.revision_ranges,
                     svn_opt_revision_range_t *) = range;
    }
  opt_state.start_revision = APR_ARRAY_IDX(opt_state.revision_ranges, 0,
                                           svn_opt_revision_range_t *)->start;
  opt_state.end_revision = APR_ARRAY_IDX(opt_state.revision_ranges, 0,
                                         svn_opt_revision_range_t *)->end;

  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  command_baton.ctx = ctx;

  /* Only a few commands can accept a revision range; the rest can take at
     most one revision number. */
  if (subcommand->cmd_func != svn_cl__null_blame
      && subcommand->cmd_func != svn_cl__null_log)
    {
      if (opt_state.end_revision.kind != svn_opt_revision_unspecified)
        {
          return svn_error_create(SVN_ERR_CLIENT_REVISION_RANGE, NULL, NULL);
        }
    }

  /* -N has a different meaning depending on the command */
  if (!descend)
    opt_state.depth = svn_depth_files;

  err = svn_config_get_config(&(ctx->config),
                              opt_state.config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svn: ");
          svn_error_clear(err);
        }
      else
        return err;
    }

  cfg_config = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                            APR_HASH_KEY_STRING);

  /* Update the options in the config */
  if (opt_state.config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(ctx->config,
                                            opt_state.config_options,
                                            "svn: ", "--config-option"));
    }

  /* Set up the notifier.

     In general, we use it any time we aren't in --quiet mode.  'svn
     status' is unique, though, in that we don't want it in --quiet mode
     unless we're also in --verbose mode.  When in --xml mode,
     though, we never want it.  */
  if (opt_state.quiet)
    use_notifier = FALSE;
  if (use_notifier)
    {
      SVN_ERR(svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2,
                                       pool));
    }

  /* Get password from stdin if necessary */
  if (read_pass_from_stdin)
    {
      SVN_ERR(svn_cmdline__stdin_readline(&opt_state.auth_password, pool, pool));
    }

  /* Set up our cancellation support. */
  svn_cl__check_cancel = svn_cmdline__setup_cancellation_handler();
  ctx->cancel_func = svn_cl__check_cancel;

  /* Set up Authentication stuff. */
  SVN_ERR(svn_cmdline_create_auth_baton2(
            &ab,
            opt_state.non_interactive,
            opt_state.auth_username,
            opt_state.auth_password,
            opt_state.config_dir,
            opt_state.no_auth_cache,
            opt_state.trust_server_cert_unknown_ca,
            opt_state.trust_server_cert_cn_mismatch,
            opt_state.trust_server_cert_expired,
            opt_state.trust_server_cert_not_yet_valid,
            opt_state.trust_server_cert_other_failure,
            cfg_config,
            ctx->cancel_func,
            ctx->cancel_baton,
            pool));

  ctx->auth_baton = ab;

  /* The new svn behavior is to postpone everything until after the operation
     completed */
  ctx->conflict_func = NULL;
  ctx->conflict_baton = NULL;
  ctx->conflict_func2 = NULL;
  ctx->conflict_baton2 = NULL;

  if (!opt_state.quiet)
    {
      ctx->progress_func = ra_progress_func;
      ctx->progress_baton = &ra_progress_baton;
    }

  /* And now we finally run the subcommand. */
  start_time = apr_time_now();
  err = (*subcommand->cmd_func)(os, &command_baton, pool);
  time_taken = apr_time_now() - start_time;

  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrapf(
                  err, _("Try 'svnbench help %s' for more information"),
                  subcommand->name);
        }
      if (err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
        {
          err = svn_error_quick_wrap(err,
                                     _("Please see the 'svn upgrade' command"));
        }

      /* Tell the user about 'svn cleanup' if any error on the stack
         was about locked working copies. */
      if (svn_error_find_cause(err, SVN_ERR_WC_LOCKED))
        {
          err = svn_error_quick_wrap(
                  err, _("Run 'svn cleanup' to remove locks "
                         "(type 'svn help cleanup' for details)"));
        }

      return err;
    }
  else if ((subcommand->cmd_func != svn_cl__help) && !opt_state.quiet)
    {
      /* This formatting lines up nicely with the output of our sub-commands
       * and gives musec resolution while not overflowing for 30 years. */
      SVN_ERR(svn_cmdline_printf(pool,
                                _("%15.6f seconds taken\n"),
                                time_taken / 1.0e6));

      /* Report how many bytes transferred over network if RA layer provided
         this information. */
      if (ra_progress_baton.bytes_transferred > 0)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("%15s bytes transferred over network\n"),
                                   svn__i64toa_sep(
                                     ra_progress_baton.bytes_transferred, ',',
                                     pool)));
    }

  return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("svnbench", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnbench: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
