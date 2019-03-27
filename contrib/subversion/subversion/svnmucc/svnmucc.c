/*
 * svnmucc.c: Subversion Multiple URL Client
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
 *
 */

/*  Multiple URL Command Client

    Combine a list of mv, cp and rm commands on URLs into a single commit.

    How it works: the command line arguments are parsed into an array of
    action structures.  The action structures are interpreted to build a
    tree of operation structures.  The tree of operation structures is
    used to drive an RA commit editor to produce a single commit.

    To build this client, type 'make svnmucc' from the root of your
    Subversion source directory.
*/

#include <stdio.h>
#include <string.h>

#include <apr_lib.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_client.h"
#include "private/svn_client_mtcc.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_subst.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_subr_private.h"

/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_client", svn_client_version },
      { "svn_subr",   svn_subr_version },
      { "svn_ra",     svn_ra_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}

/* Implements svn_commit_callback2_t */
static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, "r%ld committed by %s at %s\n",
                             commit_info->revision,
                             (commit_info->author
                              ? commit_info->author : "(no author)"),
                             commit_info->date));

  /* Writing to stdout, as there maybe systems that consider the
   * presence of stderr as an indication of commit failure.
   * OTOH, this is only of informational nature to the user as
   * the commit has succeeded. */
  if (commit_info->post_commit_err)
    SVN_ERR(svn_cmdline_printf(pool, _("\nWarning: %s\n"),
                               commit_info->post_commit_err));

  return SVN_NO_ERROR;
}

typedef enum action_code_t {
  ACTION_MV,
  ACTION_MKDIR,
  ACTION_CP,
  ACTION_PROPSET,
  ACTION_PROPSETF,
  ACTION_PROPDEL,
  ACTION_PUT,
  ACTION_RM
} action_code_t;

/* Return the portion of URL that is relative to ANCHOR (URI-decoded). */
static const char *
subtract_anchor(const char *anchor, const char *url, apr_pool_t *pool)
{
  return svn_uri_skip_ancestor(anchor, url, pool);
}


struct action {
  action_code_t action;

  /* revision (copy-from-rev of path[0] for cp; base-rev for put) */
  svn_revnum_t rev;

  /* action  path[0]  path[1]
   * ------  -------  -------
   * mv      source   target
   * mkdir   target   (null)
   * cp      source   target
   * put     target   source
   * rm      target   (null)
   * propset target   (null)
   */
  const char *path[2];

  /* property name/value */
  const char *prop_name;
  const svn_string_t *prop_value;
};

static svn_error_t *
execute(const apr_array_header_t *actions,
        const char *anchor,
        apr_hash_t *revprops,
        svn_revnum_t base_revision,
        svn_client_ctx_t *ctx,
        apr_pool_t *pool)
{
  svn_client__mtcc_t *mtcc;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_error_t *err;
  int i;

  SVN_ERR(svn_client__mtcc_create(&mtcc, anchor,
                                  SVN_IS_VALID_REVNUM(base_revision)
                                     ? base_revision
                                     : SVN_INVALID_REVNUM,
                                  ctx, pool, iterpool));

  for (i = 0; i < actions->nelts; ++i)
    {
      struct action *action = APR_ARRAY_IDX(actions, i, struct action *);
      const char *path1, *path2;
      svn_node_kind_t kind;

      svn_pool_clear(iterpool);

      switch (action->action)
        {
        case ACTION_MV:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          path2 = subtract_anchor(anchor, action->path[1], pool);
          SVN_ERR(svn_client__mtcc_add_move(path1, path2, mtcc, iterpool));
          break;
        case ACTION_CP:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          path2 = subtract_anchor(anchor, action->path[1], pool);
          SVN_ERR(svn_client__mtcc_add_copy(path1, action->rev, path2,
                                            mtcc, iterpool));
          break;
        case ACTION_RM:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          SVN_ERR(svn_client__mtcc_add_delete(path1, mtcc, iterpool));
          break;
        case ACTION_MKDIR:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          SVN_ERR(svn_client__mtcc_add_mkdir(path1, mtcc, iterpool));
          break;
        case ACTION_PUT:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          SVN_ERR(svn_client__mtcc_check_path(&kind, path1, TRUE, mtcc, pool));

          if (kind == svn_node_dir)
            {
              SVN_ERR(svn_client__mtcc_add_delete(path1, mtcc, pool));
              kind = svn_node_none;
            }

          {
            svn_stream_t *src;

            if (strcmp(action->path[1], "-") != 0)
              SVN_ERR(svn_stream_open_readonly(&src, action->path[1],
                                               pool, iterpool));
            else
              SVN_ERR(svn_stream_for_stdin2(&src, TRUE, pool));


            if (kind == svn_node_file)
              SVN_ERR(svn_client__mtcc_add_update_file(path1, src, NULL,
                                                       NULL, NULL,
                                                       mtcc, iterpool));
            else if (kind == svn_node_none)
              SVN_ERR(svn_client__mtcc_add_add_file(path1, src, NULL,
                                                    mtcc, iterpool));
          }
          break;
        case ACTION_PROPSET:
        case ACTION_PROPDEL:
          path1 = subtract_anchor(anchor, action->path[0], pool);
          SVN_ERR(svn_client__mtcc_add_propset(path1, action->prop_name,
                                               action->prop_value, FALSE,
                                               mtcc, iterpool));
          break;
        case ACTION_PROPSETF:
        default:
          SVN_ERR_MALFUNCTION_NO_RETURN();
        }
    }

  err = svn_client__mtcc_commit(revprops, commit_callback, NULL,
                                mtcc, iterpool);

  svn_pool_destroy(iterpool);
  return svn_error_trace(err);
}

static svn_error_t *
read_propvalue_file(const svn_string_t **value_p,
                    const char *filename,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *value;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(svn_stringbuf_from_file2(&value, filename, scratch_pool));
  *value_p = svn_string_create_from_buf(value, pool);
  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

/* Perform the typical suite of manipulations for user-provided URLs
   on URL, returning the result (allocated from POOL): IRI-to-URI
   conversion, auto-escaping, and canonicalization. */
static const char *
sanitize_url(const char *url,
             apr_pool_t *pool)
{
  url = svn_path_uri_from_iri(url, pool);
  url = svn_path_uri_autoescape(url, pool);
  return svn_uri_canonicalize(url, pool);
}

static void
usage(apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fprintf
                  (stderr, pool, _("Type 'svnmucc --help' for usage.\n")));
}

/* Print a usage message on STREAM. */
static void
help(FILE *stream, apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fputs(
    _("usage: svnmucc ACTION...\n"
      "Subversion multiple URL command client.\n"
      "Type 'svnmucc --version' to see the program version and RA modules.\n"
      "\n"
      "  Perform one or more Subversion repository URL-based ACTIONs, committing\n"
      "  the result as a (single) new revision.\n"
      "\n"
      "Actions:\n"
      "  cp REV SRC-URL DST-URL : copy SRC-URL@REV to DST-URL\n"
      "  mkdir URL              : create new directory URL\n"
      "  mv SRC-URL DST-URL     : move SRC-URL to DST-URL\n"
      "  rm URL                 : delete URL\n"
      "  put SRC-FILE URL       : add or modify file URL with contents copied from\n"
      "                           SRC-FILE (use \"-\" to read from standard input)\n"
      "  propset NAME VALUE URL : set property NAME on URL to VALUE\n"
      "  propsetf NAME FILE URL : set property NAME on URL to value read from FILE\n"
      "  propdel NAME URL       : delete property NAME from URL\n"
      "\n"
      "Valid options:\n"
      "  -h, -? [--help]        : display this text\n"
      "  -m [--message] ARG     : use ARG as a log message\n"
      "  -F [--file] ARG        : read log message from file ARG\n"
      "  -u [--username] ARG    : commit the changes as username ARG\n"
      "  -p [--password] ARG    : use ARG as the password\n"
      "  --password-from-stdin  : read password from stdin\n"
      "  -U [--root-url] ARG    : interpret all action URLs relative to ARG\n"
      "  -r [--revision] ARG    : use revision ARG as baseline for changes\n"
      "  --with-revprop ARG     : set revision property in the following format:\n"
      "                               NAME[=VALUE]\n"
      "  --non-interactive      : do no interactive prompting (default is to\n"
      "                           prompt only if standard input is a terminal)\n"
      "  --force-interactive    : do interactive prompting even if standard\n"
      "                           input is not a terminal\n"
      "  --trust-server-cert    : deprecated;\n"
      "                           same as --trust-server-cert-failures=unknown-ca\n"
      "  --trust-server-cert-failures ARG\n"
      "                           with --non-interactive, accept SSL server\n"
      "                           certificates with failures; ARG is comma-separated\n"
      "                           list of 'unknown-ca' (Unknown Authority),\n"
      "                           'cn-mismatch' (Hostname mismatch), 'expired'\n"
      "                           (Expired certificate),'not-yet-valid' (Not yet\n"
      "                           valid certificate) and 'other' (all other not\n"
      "                           separately classified certificate errors).\n"
      "  -X [--extra-args] ARG  : append arguments from file ARG (one per line;\n"
      "                           use \"-\" to read from standard input)\n"
      "  --config-dir ARG       : use ARG to override the config directory\n"
      "  --config-option ARG    : use ARG to override a configuration option\n"
      "  --no-auth-cache        : do not cache authentication tokens\n"
      "  --version              : print version information\n"),
                  stream, pool));
}

static svn_error_t *
insufficient(void)
{
  return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                          "insufficient arguments");
}

static svn_error_t *
display_version(apr_pool_t *pool)
{
  const char *ra_desc_start
    = "The following repository access (RA) modules are available:\n\n";
  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(ra_desc_start, pool);
  SVN_ERR(svn_ra_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help4(NULL, "svnmucc", TRUE, FALSE, FALSE,
                              version_footer->data,
                              NULL, NULL, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* Return an error about the mutual exclusivity of the -m, -F, and
   --with-revprop=svn:log command-line options. */
static svn_error_t *
mutually_exclusive_logs_error(void)
{
  return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("--message (-m), --file (-F), and "
                            "--with-revprop=svn:log are mutually "
                            "exclusive"));
}

/* Obtain the log message from multiple sources, producing an error
   if there are multiple sources. Store the result in *FINAL_MESSAGE.  */
static svn_error_t *
sanitize_log_sources(const char **final_message,
                     const char *message,
                     apr_hash_t *revprops,
                     svn_stringbuf_t *filedata,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_string_t *msg;

  *final_message = NULL;
  /* If we already have a log message in the revprop hash, then just
     make sure the user didn't try to also use -m or -F.  Otherwise,
     we need to consult -m or -F to find a log message, if any. */
  msg = svn_hash_gets(revprops, SVN_PROP_REVISION_LOG);
  if (msg)
    {
      if (filedata || message)
        return mutually_exclusive_logs_error();

      *final_message = apr_pstrdup(result_pool, msg->data);

      /* Will be re-added by libsvn_client */
      svn_hash_sets(revprops, SVN_PROP_REVISION_LOG, NULL);
    }
  else if (filedata)
    {
      if (message)
        return mutually_exclusive_logs_error();

      *final_message = apr_pstrdup(result_pool, filedata->data);
    }
  else if (message)
    {
      *final_message = apr_pstrdup(result_pool, message);
    }

  return SVN_NO_ERROR;
}

/* Baton for log_message_func */
struct log_message_baton
{
  svn_boolean_t non_interactive;
  const char *log_message;
  svn_client_ctx_t *ctx;
};

/* Implements svn_client_get_commit_log3_t */
static svn_error_t *
log_message_func(const char **log_msg,
                 const char **tmp_file,
                 const apr_array_header_t *commit_items,
                 void *baton,
                 apr_pool_t *pool)
{
  struct log_message_baton *lmb = baton;

  *tmp_file = NULL;

  if (lmb->log_message)
    {
      svn_string_t *message = svn_string_create(lmb->log_message, pool);

      SVN_ERR_W(svn_subst_translate_string2(&message, NULL, NULL,
                                            message, NULL, FALSE,
                                            pool, pool),
                _("Error normalizing log message to internal format"));

      *log_msg = message->data;

      return SVN_NO_ERROR;
    }

  if (lmb->non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                              _("Cannot invoke editor to get log message "
                                "when non-interactive"));
    }
  else
    {
      svn_string_t *msg = svn_string_create("", pool);

      SVN_ERR(svn_cmdline__edit_string_externally(
                      &msg, NULL, NULL, "", msg, "svnmucc-commit",
                      lmb->ctx->config, TRUE, NULL, pool));

      if (msg && msg->data)
        *log_msg = msg->data;
      else
        *log_msg = NULL;

      return SVN_NO_ERROR;
    }
}

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  apr_array_header_t *actions = apr_array_make(pool, 1,
                                               sizeof(struct action *));
  const char *anchor = NULL;
  svn_error_t *err = SVN_NO_ERROR;
  apr_getopt_t *opts;
  enum {
    config_dir_opt = SVN_OPT_FIRST_LONGOPT_ID,
    config_inline_opt,
    no_auth_cache_opt,
    version_opt,
    with_revprop_opt,
    non_interactive_opt,
    force_interactive_opt,
    trust_server_cert_opt,
    trust_server_cert_failures_opt,
    password_from_stdin_opt
  };
  static const apr_getopt_option_t options[] = {
    {"message", 'm', 1, ""},
    {"file", 'F', 1, ""},
    {"username", 'u', 1, ""},
    {"password", 'p', 1, ""},
    {"password-from-stdin", password_from_stdin_opt, 0, ""},
    {"root-url", 'U', 1, ""},
    {"revision", 'r', 1, ""},
    {"with-revprop",  with_revprop_opt, 1, ""},
    {"extra-args", 'X', 1, ""},
    {"help", 'h', 0, ""},
    {NULL, '?', 0, ""},
    {"non-interactive", non_interactive_opt, 0, ""},
    {"force-interactive", force_interactive_opt, 0, ""},
    {"trust-server-cert", trust_server_cert_opt, 0, ""},
    {"trust-server-cert-failures", trust_server_cert_failures_opt, 1, ""},
    {"config-dir", config_dir_opt, 1, ""},
    {"config-option",  config_inline_opt, 1, ""},
    {"no-auth-cache",  no_auth_cache_opt, 0, ""},
    {"version", version_opt, 0, ""},
    {NULL, 0, 0, NULL}
  };
  const char *message = NULL;
  svn_stringbuf_t *filedata = NULL;
  const char *username = NULL, *password = NULL;
  const char *root_url = NULL, *extra_args_file = NULL;
  const char *config_dir = NULL;
  apr_array_header_t *config_options;
  svn_boolean_t non_interactive = FALSE;
  svn_boolean_t force_interactive = FALSE;
  svn_boolean_t trust_unknown_ca = FALSE;
  svn_boolean_t trust_cn_mismatch = FALSE;
  svn_boolean_t trust_expired = FALSE;
  svn_boolean_t trust_not_yet_valid = FALSE;
  svn_boolean_t trust_other_failure = FALSE;
  svn_boolean_t no_auth_cache = FALSE;
  svn_boolean_t show_version = FALSE;
  svn_boolean_t show_help = FALSE;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;
  apr_array_header_t *action_args;
  apr_hash_t *revprops = apr_hash_make(pool);
  apr_hash_t *cfg_hash;
  svn_config_t *cfg_config;
  svn_client_ctx_t *ctx;
  struct log_message_baton lmb;
  int i;
  svn_boolean_t read_pass_from_stdin = FALSE;

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  /* Initialize the RA library. */
  SVN_ERR(svn_ra_initialize(pool));

  config_options = apr_array_make(pool, 0,
                                  sizeof(svn_cmdline__config_argument_t*));

  apr_getopt_init(&opts, pool, argc, argv);
  opts->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      const char *opt_arg;

      apr_status_t status = apr_getopt_long(opts, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          usage(pool);
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
      switch(opt)
        {
        case 'm':
          SVN_ERR(svn_utf_cstring_to_utf8(&message, arg, pool));
          break;
        case 'F':
          {
            const char *filename;
            SVN_ERR(svn_utf_cstring_to_utf8(&filename, arg, pool));
            SVN_ERR(svn_stringbuf_from_file2(&filedata, filename, pool));
          }
          break;
        case 'u':
          username = apr_pstrdup(pool, arg);
          break;
        case 'p':
          password = apr_pstrdup(pool, arg);
          break;
        case password_from_stdin_opt:
          read_pass_from_stdin = TRUE;
          break;
        case 'U':
          SVN_ERR(svn_utf_cstring_to_utf8(&root_url, arg, pool));
          if (! svn_path_is_url(root_url))
            return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                     "'%s' is not a URL\n", root_url);
          root_url = sanitize_url(root_url, pool);
          break;
        case 'r':
          {
            const char *saved_arg = arg;
            char *digits_end = NULL;
            while (*arg == 'r')
              arg++;
            base_revision = strtol(arg, &digits_end, 10);
            if ((! SVN_IS_VALID_REVNUM(base_revision))
                || (! digits_end)
                || *digits_end)
              return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                       _("Invalid revision number '%s'"),
                                       saved_arg);
          }
          break;
        case with_revprop_opt:
          SVN_ERR(svn_opt_parse_revprop(&revprops, arg, pool));
          break;
        case 'X':
          SVN_ERR(svn_utf_cstring_to_utf8(&extra_args_file, arg, pool));
          break;
        case non_interactive_opt:
          non_interactive = TRUE;
          break;
        case force_interactive_opt:
          force_interactive = TRUE;
          break;
        case trust_server_cert_opt: /* backward compat */
          trust_unknown_ca = TRUE;
          break;
        case trust_server_cert_failures_opt:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, arg, pool));
          SVN_ERR(svn_cmdline__parse_trust_options(
                      &trust_unknown_ca,
                      &trust_cn_mismatch,
                      &trust_expired,
                      &trust_not_yet_valid,
                      &trust_other_failure,
                      opt_arg, pool));
          break;
        case config_dir_opt:
          SVN_ERR(svn_utf_cstring_to_utf8(&config_dir, arg, pool));
          break;
        case config_inline_opt:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, arg, pool));
          SVN_ERR(svn_cmdline__parse_config_option(config_options, opt_arg,
                                                   "svnmucc: ", 
                                                   pool));
          break;
        case no_auth_cache_opt:
          no_auth_cache = TRUE;
          break;
        case version_opt:
          show_version = TRUE;
          break;
        case 'h':
        case '?':
          show_help = TRUE;
          break;
        }
    }

  if (show_help)
    {
      help(stdout, pool);
      return SVN_NO_ERROR;
    }

  if (show_version)
    {
      SVN_ERR(display_version(pool));
      return SVN_NO_ERROR;
    }

  if (non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }
  else
    non_interactive = !svn_cmdline__be_interactive(non_interactive,
                                                   force_interactive);

  if (!non_interactive)
    {
      if (trust_unknown_ca || trust_cn_mismatch || trust_expired
          || trust_not_yet_valid || trust_other_failure)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-server-cert-failures requires "
                                  "--non-interactive"));
    }

  /* --password-from-stdin can only be used with --non-interactive */
  if (read_pass_from_stdin && !non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--password-from-stdin requires "
                                "--non-interactive"));
    }


  /* Copy the rest of our command-line arguments to an array,
     UTF-8-ing them along the way. */
  action_args = apr_array_make(pool, opts->argc, sizeof(const char *));
  while (opts->ind < opts->argc)
    {
      const char *arg;

      SVN_ERR(svn_utf_cstring_to_utf8(&arg, opts->argv[opts->ind++], pool));
      APR_ARRAY_PUSH(action_args, const char *) = arg;
    }

  /* If there are extra arguments in a supplementary file, tack those
     on, too (again, in UTF8 form). */
  if (extra_args_file)
    {
      svn_stringbuf_t *contents, *contents_utf8;

      SVN_ERR(svn_stringbuf_from_file2(&contents, extra_args_file, pool));
      SVN_ERR(svn_utf_stringbuf_to_utf8(&contents_utf8, contents, pool));
      svn_cstring_split_append(action_args, contents_utf8->data, "\n\r",
                               FALSE, pool);
    }

  /* Now initialize the client context */

  err = svn_config_get_config(&cfg_hash, config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svnmucc: ");
          svn_error_clear(err);

          SVN_ERR(svn_config__get_default_config(&cfg_hash, pool));
        }
      else
        return err;
    }

  if (config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(cfg_hash, config_options,
                                            "svnmucc: ", "--config-option"));
    }

  /* Get password from stdin if necessary */
  if (read_pass_from_stdin)
    {
      SVN_ERR(svn_cmdline__stdin_readline(&password, pool, pool));
    }

  SVN_ERR(svn_client_create_context2(&ctx, cfg_hash, pool));

  cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);
  SVN_ERR(svn_cmdline_create_auth_baton2(
            &ctx->auth_baton,
            non_interactive,
            username,
            password,
            config_dir,
            no_auth_cache,
            trust_unknown_ca,
            trust_cn_mismatch,
            trust_expired,
            trust_not_yet_valid,
            trust_other_failure,
            cfg_config,
            ctx->cancel_func,
            ctx->cancel_baton,
            pool));

  lmb.non_interactive = non_interactive;
  lmb.ctx = ctx;
    /* Make sure we have a log message to use. */
  SVN_ERR(sanitize_log_sources(&lmb.log_message, message, revprops, filedata,
                               pool, pool));

  ctx->log_msg_func3 = log_message_func;
  ctx->log_msg_baton3 = &lmb;

  /* Now, we iterate over the combined set of arguments -- our actions. */
  for (i = 0; i < action_args->nelts; )
    {
      int j, num_url_args;
      const char *action_string = APR_ARRAY_IDX(action_args, i, const char *);
      struct action *action = apr_pcalloc(pool, sizeof(*action));

      /* First, parse the action. */
      if (! strcmp(action_string, "mv"))
        action->action = ACTION_MV;
      else if (! strcmp(action_string, "cp"))
        action->action = ACTION_CP;
      else if (! strcmp(action_string, "mkdir"))
        action->action = ACTION_MKDIR;
      else if (! strcmp(action_string, "rm"))
        action->action = ACTION_RM;
      else if (! strcmp(action_string, "put"))
        action->action = ACTION_PUT;
      else if (! strcmp(action_string, "propset"))
        action->action = ACTION_PROPSET;
      else if (! strcmp(action_string, "propsetf"))
        action->action = ACTION_PROPSETF;
      else if (! strcmp(action_string, "propdel"))
        action->action = ACTION_PROPDEL;
      else if (! strcmp(action_string, "?") || ! strcmp(action_string, "h")
               || ! strcmp(action_string, "help"))
        {
          help(stdout, pool);
          return SVN_NO_ERROR;
        }
      else
        return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                 "'%s' is not an action\n",
                                 action_string);
      if (++i == action_args->nelts)
        return insufficient();

      /* For copies, there should be a revision number next. */
      if (action->action == ACTION_CP)
        {
          const char *rev_str = APR_ARRAY_IDX(action_args, i, const char *);
          if (strcmp(rev_str, "head") == 0)
            action->rev = SVN_INVALID_REVNUM;
          else if (strcmp(rev_str, "HEAD") == 0)
            action->rev = SVN_INVALID_REVNUM;
          else
            {
              char *end;

              while (*rev_str == 'r')
                ++rev_str;

              action->rev = strtol(rev_str, &end, 0);
              if (*end)
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "'%s' is not a revision\n",
                                         rev_str);
            }
          if (++i == action_args->nelts)
            return insufficient();
        }
      else
        {
          action->rev = SVN_INVALID_REVNUM;
        }

      /* For puts, there should be a local file next. */
      if (action->action == ACTION_PUT)
        {
          action->path[1] =
            svn_dirent_internal_style(APR_ARRAY_IDX(action_args, i,
                                                    const char *), pool);
          if (++i == action_args->nelts)
            return insufficient();
        }

      /* For propset, propsetf, and propdel, a property name (and
         maybe a property value or file which contains one) comes next. */
      if ((action->action == ACTION_PROPSET)
          || (action->action == ACTION_PROPSETF)
          || (action->action == ACTION_PROPDEL))
        {
          action->prop_name = APR_ARRAY_IDX(action_args, i, const char *);
          if (++i == action_args->nelts)
            return insufficient();

          if (action->action == ACTION_PROPDEL)
            {
              action->prop_value = NULL;
            }
          else if (action->action == ACTION_PROPSET)
            {
              action->prop_value =
                svn_string_create(APR_ARRAY_IDX(action_args, i,
                                                const char *), pool);
              if (++i == action_args->nelts)
                return insufficient();
            }
          else
            {
              const char *propval_file =
                svn_dirent_internal_style(APR_ARRAY_IDX(action_args, i,
                                                        const char *), pool);

              if (++i == action_args->nelts)
                return insufficient();

              SVN_ERR(read_propvalue_file(&(action->prop_value),
                                          propval_file, pool));

              action->action = ACTION_PROPSET;
            }

          if (action->prop_value
              && svn_prop_needs_translation(action->prop_name))
            {
              svn_string_t *translated_value;
              SVN_ERR_W(svn_subst_translate_string2(&translated_value, NULL,
                                                    NULL, action->prop_value,
                                                    NULL, FALSE, pool, pool),
                        "Error normalizing property value");
              action->prop_value = translated_value;
            }
        }

      /* How many URLs does this action expect? */
      if (action->action == ACTION_RM
          || action->action == ACTION_MKDIR
          || action->action == ACTION_PUT
          || action->action == ACTION_PROPSET
          || action->action == ACTION_PROPSETF /* shouldn't see this one */
          || action->action == ACTION_PROPDEL)
        num_url_args = 1;
      else
        num_url_args = 2;

      /* Parse the required number of URLs. */
      for (j = 0; j < num_url_args; ++j)
        {
          const char *url = APR_ARRAY_IDX(action_args, i, const char *);

          /* If there's a ROOT_URL, we expect URL to be a path
             relative to ROOT_URL (and we build a full url from the
             combination of the two).  Otherwise, it should be a full
             url. */
          if (! svn_path_is_url(url))
            {
              if (! root_url)
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "'%s' is not a URL, and "
                                         "--root-url (-U) not provided\n",
                                         url);
              /* ### These relpaths are already URI-encoded. */
              url = apr_pstrcat(pool, root_url, "/",
                                svn_relpath_canonicalize(url, pool),
                                SVN_VA_NULL);
            }
          url = sanitize_url(url, pool);
          action->path[j] = url;

          /* The first URL arguments to 'cp', 'pd', 'ps' could be the anchor,
             but the other URLs should be children of the anchor. */
          if (! (action->action == ACTION_CP && j == 0)
              && action->action != ACTION_PROPDEL
              && action->action != ACTION_PROPSET
              && action->action != ACTION_PROPSETF)
            url = svn_uri_dirname(url, pool);
          if (! anchor)
            anchor = url;
          else
            {
              anchor = svn_uri_get_longest_ancestor(anchor, url, pool);
              if (!anchor || !anchor[0])
                return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                         "URLs in the action list do not "
                                         "share a common ancestor");
            }

          if ((++i == action_args->nelts) && (j + 1 < num_url_args))
            return insufficient();
        }

      APR_ARRAY_PUSH(actions, struct action *) = action;
    }

  if (! actions->nelts)
    {
      *exit_code = EXIT_FAILURE;
      help(stderr, pool);
      return SVN_NO_ERROR;
    }

  if ((err = execute(actions, anchor, revprops, base_revision, ctx, pool)))
    {
      if (err->apr_err == SVN_ERR_AUTHN_FAILED && non_interactive)
        err = svn_error_quick_wrap(err,
                                   _("Authentication failed and interactive"
                                     " prompting is disabled; see the"
                                     " --force-interactive option"));
      return err;
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
  if (svn_cmdline_init("svnmucc", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnmucc: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
