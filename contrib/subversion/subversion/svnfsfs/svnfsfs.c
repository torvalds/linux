/*
 * svnfsfs.c: FSFS repository manipulation tool main file.
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

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_path.h"
#include "svn_dirent_uri.h"
#include "svn_repos.h"
#include "svn_cache_config.h"
#include "svn_version.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"

#include "svnfsfs.h"


/*** Code. ***/

svn_cancel_func_t check_cancel = NULL;

/* Custom filesystem warning function. */
static void
warning_func(void *baton,
             svn_error_t *err)
{
  if (! err)
    return;
  svn_handle_warning2(stderr, err, "svnfsfs: ");
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_repos", svn_repos_version },
      { "svn_fs",    svn_fs_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}



/** Subcommands. **/

enum svnfsfs__cmdline_options_t
  {
    svnfsfs__version = SVN_OPT_FIRST_LONGOPT_ID
  };

/* Option codes and descriptions.
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
  {
    {"help",          'h', 0,
     N_("show help on a subcommand")},

    {NULL,            '?', 0,
     N_("show help on a subcommand")},

    {"version",       svnfsfs__version, 0,
     N_("show program version information")},

    {"quiet",         'q', 0,
     N_("no progress (only errors to stderr)")},

    {"revision",      'r', 1,
     N_("specify revision number ARG (or X:Y range)")},

    {"memory-cache-size",     'M', 1,
     N_("size of the extra in-memory cache in MB used to\n"
        "                             minimize redundant operations. Default: 16.")},

    {NULL}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc2_t cmd_table[] =
{
  {"help", subcommand__help, {"?", "h"}, N_
   ("usage: svnfsfs help [SUBCOMMAND...]\n\n"
    "Describe the usage of this program or its subcommands.\n"),
   {0} },

  {"dump-index", subcommand__dump_index, {0}, N_
   ("usage: svnfsfs dump-index REPOS_PATH -r REV\n\n"
    "Dump the index contents for the revision / pack file containing revision REV\n"
    "to console.  This is only available for FSFS format 7 (SVN 1.9+) repositories.\n"
    "The table produced contains a header in the first line followed by one line\n"
    "per index entry, ordered by location in the revision / pack file.  Columns:\n\n"
    "   * Byte offset (hex) at which the item starts\n"
    "   * Length (hex) of the item in bytes\n"
    "   * Item type (string) is one of the following:\n\n"
    "        none ... Unused section.  File contents shall be NULs.\n"
    "        frep ... File representation.\n"
    "        drep ... Directory representation.\n"
    "        fprop .. File property.\n"
    "        dprop .. Directory property.\n"
    "        node ... Node revision.\n"
    "        chgs ... Changed paths list.\n"
    "        rep .... Representation of unknown type.  Should not be used.\n"
    "        ??? .... Invalid.  Index data is corrupt.\n\n"
    "        The distinction between frep, drep, fprop and dprop is a mere internal\n"
    "        classification used for various optimizations and does not affect the\n"
    "        operational correctness.\n\n"
    "   * Revision that the item belongs to (decimal)\n"
    "   * Item number (decimal) within that revision\n"
    "   * Modified FNV1a checksum (8 hex digits)\n"),
   {'r', 'M'} },

  {"load-index", subcommand__load_index, {0}, N_
   ("usage: svnfsfs load-index REPOS_PATH\n\n"
    "Read index contents from console.  The format is the same as produced by the\n"
    "dump-index command, except that checksum as well as header are optional and will\n"
    "be ignored.  The data must cover the full revision / pack file;  the revision\n"
    "number is automatically extracted from input stream.  No ordering is required.\n"),
   {'M'} },

  {"stats", subcommand__stats, {0}, N_
   ("usage: svnfsfs stats REPOS_PATH\n\n"
    "Write object size statistics to console.\n"),
   {'M'} },

  { NULL, NULL, {0}, NULL, {0} }
};


svn_error_t *
open_fs(svn_fs_t **fs,
        const char *path,
        apr_pool_t *pool)
{
  const char *fs_type;

  /* Verify that we can handle the repository type. */
  path = svn_dirent_join(path, "db", pool);
  SVN_ERR(svn_fs_type(&fs_type, path, pool));
  if (strcmp(fs_type, SVN_FS_TYPE_FSFS))
    return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_TYPE, NULL,
                             _("%s repositories are not supported"),
                             fs_type);

  /* Now open it. */
  SVN_ERR(svn_fs_open2(fs, path, NULL, pool, pool));
  svn_fs_set_warning_func(*fs, warning_func, NULL);

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand__help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnfsfs__opt_state *opt_state = baton;
  const char *header =
    _("general usage: svnfsfs SUBCOMMAND REPOS_PATH  [ARGS & OPTIONS ...]\n"
      "Subversion FSFS repository manipulation tool.\n"
      "Type 'svnfsfs help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnfsfs --version' to see the program version.\n"
      "\n"
      "Available subcommands:\n");

  SVN_ERR(svn_opt_print_help4(os, "svnfsfs",
                              opt_state ? opt_state->version : FALSE,
                              opt_state ? opt_state->quiet : FALSE,
                              /*###opt_state ? opt_state->verbose :*/ FALSE,
                              NULL,
                              header, cmd_table, options_table, NULL, NULL,
                              pool));

  return SVN_NO_ERROR;
}


/** Main. **/

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;

  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  svnfsfs__opt_state opt_state = { 0 };
  apr_getopt_t *os;
  int opt_id;
  apr_array_header_t *received_opts;
  int i;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  /* Initialize the FS library. */
  SVN_ERR(svn_fs_initialize(pool));

  if (argc <= 1)
    {
      SVN_ERR(subcommand__help(NULL, NULL, pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  /* Initialize opt_state. */
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
  opt_state.memory_cache_size = svn_cache_config_get()->cache_size;

  /* Parse options. */
  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;

  while (1)
    {
      const char *opt_arg;
      const char *utf8_opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, options_table, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(subcommand__help(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id) {
      case 'r':
        {
          if (opt_state.start_revision.kind != svn_opt_revision_unspecified)
            {
              return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                        _("Multiple revision arguments encountered; "
                          "try '-r N:M' instead of '-r N -r M'"));
            }
          if (svn_opt_parse_revision(&(opt_state.start_revision),
                                     &(opt_state.end_revision),
                                     opt_arg, pool) != 0)
            {
              SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));

              return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                        _("Syntax error in revision argument '%s'"),
                        utf8_opt_arg);
            }
        }
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'M':
        {
          apr_uint64_t sz_val;
          SVN_ERR(svn_cstring_atoui64(&sz_val, opt_arg));

          opt_state.memory_cache_size = 0x100000 * sz_val;
        }
        break;
      case svnfsfs__version:
        opt_state.version = TRUE;
        break;
      default:
        {
          SVN_ERR(subcommand__help(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
      }  /* close `switch' */
    }  /* close `while' */

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is subcommand_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "help");

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
                { "--version", subcommand__help, {0}, "",
                  {svnfsfs__version,  /* must accept its own option */
                   'q',  /* --quiet */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                        _("subcommand argument required\n")));
              SVN_ERR(subcommand__help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(cmd_table, first_arg);
          if (subcommand == NULL)
            {
              svn_error_clear(
                svn_cmdline_fprintf(stderr, pool,
                                    _("Unknown subcommand: '%s'\n"),
                                    first_arg));
              SVN_ERR(subcommand__help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Every subcommand except `help' requires a second argument -- the
     repository path.  Parse it out here and store it in opt_state. */
  if (!(subcommand->cmd_func == subcommand__help))
    {
      const char *repos_path = NULL;

      if (os->ind >= os->argc)
        {
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("Repository argument required"));
        }

      SVN_ERR(svn_utf_cstring_to_utf8(&repos_path, os->argv[os->ind++], pool));

      if (svn_path_is_url(repos_path))
        {
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("'%s' is a URL when it should be a "
                                     "local path"), repos_path);
        }

      opt_state.repository_path = svn_dirent_internal_style(repos_path, pool);
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

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, options_table, subcommand,
                                          pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            SVN_ERR(subcommand__help(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf(stderr, pool
                            , _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnfsfs help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* Set up our cancellation support. */
  check_cancel = svn_cmdline__setup_cancellation_handler();

  /* Configure FSFS caches for maximum efficiency with svnfsfs.
   * Also, apply the respective command line parameters, if given. */
  {
    svn_cache_config_t settings = *svn_cache_config_get();

    settings.cache_size = opt_state.memory_cache_size;
    settings.single_threaded = TRUE;

    svn_cache_config_set(&settings);
  }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func)(os, &opt_state, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svnfsfs help' for more info"));
        }
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
  if (svn_cmdline_init("svnfsfs", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnfsfs: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
