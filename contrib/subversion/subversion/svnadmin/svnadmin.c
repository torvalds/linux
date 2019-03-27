/*
 * svnadmin.c: Subversion server administration tool main file.
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


#include <apr_file_io.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_error.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_repos.h"
#include "svn_cache_config.h"
#include "svn_version.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_user.h"
#include "svn_xml.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_opt_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_fspath.h"

#include "svn_private_config.h"


/*** Code. ***/

/* FSFS format 7's "block-read" feature performs poorly with small caches.
 * Enable it only if caches above this threshold have been configured.
 * The current threshold is 64MB. */
#define BLOCK_READ_CACHE_THRESHOLD (0x40 * 0x100000)

static svn_cancel_func_t check_cancel = NULL;

/* Custom filesystem warning function. */
static void
warning_func(void *baton,
             svn_error_t *err)
{
  if (! err)
    return;
  svn_handle_warning2(stderr, err, "svnadmin: ");
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

static svn_opt_subcommand_t
  subcommand_crashtest,
  subcommand_create,
  subcommand_delrevprop,
  subcommand_deltify,
  subcommand_dump,
  subcommand_dump_revprops,
  subcommand_freeze,
  subcommand_help,
  subcommand_hotcopy,
  subcommand_info,
  subcommand_load,
  subcommand_load_revprops,
  subcommand_list_dblogs,
  subcommand_list_unused_dblogs,
  subcommand_lock,
  subcommand_lslocks,
  subcommand_lstxns,
  subcommand_pack,
  subcommand_recover,
  subcommand_rmlocks,
  subcommand_rmtxns,
  subcommand_setlog,
  subcommand_setrevprop,
  subcommand_setuuid,
  subcommand_unlock,
  subcommand_upgrade,
  subcommand_verify;

enum svnadmin__cmdline_options_t
  {
    svnadmin__version = SVN_OPT_FIRST_LONGOPT_ID,
    svnadmin__incremental,
    svnadmin__keep_going,
    svnadmin__deltas,
    svnadmin__ignore_uuid,
    svnadmin__force_uuid,
    svnadmin__fs_type,
    svnadmin__parent_dir,
    svnadmin__bdb_txn_nosync,
    svnadmin__bdb_log_keep,
    svnadmin__config_dir,
    svnadmin__bypass_hooks,
    svnadmin__bypass_prop_validation,
    svnadmin__ignore_dates,
    svnadmin__use_pre_commit_hook,
    svnadmin__use_post_commit_hook,
    svnadmin__use_pre_revprop_change_hook,
    svnadmin__use_post_revprop_change_hook,
    svnadmin__clean_logs,
    svnadmin__wait,
    svnadmin__pre_1_4_compatible,
    svnadmin__pre_1_5_compatible,
    svnadmin__pre_1_6_compatible,
    svnadmin__compatible_version,
    svnadmin__check_normalization,
    svnadmin__metadata_only,
    svnadmin__no_flush_to_disk,
    svnadmin__normalize_props,
    svnadmin__exclude,
    svnadmin__include,
    svnadmin__glob
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

    {"version",       svnadmin__version, 0,
     N_("show program version information")},

    {"revision",      'r', 1,
     N_("specify revision number ARG (or X:Y range)")},

    {"transaction",       't', 1,
     N_("specify transaction name ARG")},

    {"incremental",   svnadmin__incremental, 0,
     N_("dump or hotcopy incrementally")},

    {"deltas",        svnadmin__deltas, 0,
     N_("use deltas in dump output")},

    {"bypass-hooks",  svnadmin__bypass_hooks, 0,
     N_("bypass the repository hook system")},

    {"bypass-prop-validation",  svnadmin__bypass_prop_validation, 0,
     N_("bypass property validation logic")},

    {"ignore-dates",  svnadmin__ignore_dates, 0,
     N_("ignore revision datestamps found in the stream")},

    {"quiet",         'q', 0,
     N_("no progress (only errors to stderr)")},

    {"ignore-uuid",   svnadmin__ignore_uuid, 0,
     N_("ignore any repos UUID found in the stream")},

    {"force-uuid",    svnadmin__force_uuid, 0,
     N_("set repos UUID to that found in stream, if any")},

    {"fs-type",       svnadmin__fs_type, 1,
     N_("type of repository:\n"
        "                             'fsfs' (default), 'bdb' or 'fsx'\n"
        "                             CAUTION: FSX is for EXPERIMENTAL use only!")},

    {"parent-dir",    svnadmin__parent_dir, 1,
     N_("load at specified directory in repository")},

    {"bdb-txn-nosync", svnadmin__bdb_txn_nosync, 0,
     N_("disable fsync at transaction commit [Berkeley DB]")},

    {"bdb-log-keep",  svnadmin__bdb_log_keep, 0,
     N_("disable automatic log file removal [Berkeley DB]")},

    {"config-dir",    svnadmin__config_dir, 1,
     N_("read user configuration files from directory ARG")},

    {"clean-logs",    svnadmin__clean_logs, 0,
     N_("remove redundant Berkeley DB log files\n"
        "                             from source repository [Berkeley DB]")},

    {"use-pre-commit-hook", svnadmin__use_pre_commit_hook, 0,
     N_("call pre-commit hook before committing revisions")},

    {"use-post-commit-hook", svnadmin__use_post_commit_hook, 0,
     N_("call post-commit hook after committing revisions")},

    {"use-pre-revprop-change-hook", svnadmin__use_pre_revprop_change_hook, 0,
     N_("call hook before changing revision property")},

    {"use-post-revprop-change-hook", svnadmin__use_post_revprop_change_hook, 0,
     N_("call hook after changing revision property")},

    {"wait",          svnadmin__wait, 0,
     N_("wait instead of exit if the repository is in\n"
        "                             use by another process")},

    {"pre-1.4-compatible",     svnadmin__pre_1_4_compatible, 0,
     N_("deprecated; see --compatible-version")},

    {"pre-1.5-compatible",     svnadmin__pre_1_5_compatible, 0,
     N_("deprecated; see --compatible-version")},

    {"pre-1.6-compatible",     svnadmin__pre_1_6_compatible, 0,
     N_("deprecated; see --compatible-version")},

    {"keep-going",    svnadmin__keep_going, 0,
     N_("continue verification after detecting a corruption")},

    {"memory-cache-size",     'M', 1,
     N_("size of the extra in-memory cache in MB used to\n"
        "                             minimize redundant operations. Default: 16.\n"
        "                             [used for FSFS repositories only]")},

    {"compatible-version",     svnadmin__compatible_version, 1,
     N_("use repository format compatible with Subversion\n"
        "                             version ARG (\"1.5.5\", \"1.7\", etc.)")},

    {"file", 'F', 1, N_("read repository paths from file ARG")},

    {"check-normalization", svnadmin__check_normalization, 0,
     N_("report any names within the same directory or\n"
        "                             svn:mergeinfo property value that differ only\n"
        "                             in character representation, but are otherwise\n"
        "                             identical")},

    {"metadata-only", svnadmin__metadata_only, 0,
     N_("verify metadata only (ignored for BDB),\n"
        "                             checking against external corruption in\n"
        "                             Subversion 1.9+ format repositories.\n")},

    {"no-flush-to-disk", svnadmin__no_flush_to_disk, 0,
     N_("disable flushing to disk during the operation\n"
        "                             (faster, but unsafe on power off)")},

    {"normalize-props", svnadmin__normalize_props, 0,
     N_("normalize property values found in the dumpstream\n"
        "                             (currently, only translates non-LF line endings)")},

    {"exclude", svnadmin__exclude, 1,
     N_("filter out nodes with given prefix(es) from dump")},

    {"include", svnadmin__include, 1,
     N_("filter out nodes without given prefix(es) from dump")},

    {"pattern", svnadmin__glob, 0,
     N_("treat the path prefixes as file glob patterns.\n"
        "                             Glob special characters are '*' '?' '[]' and '\\'.\n"
        "                             Character '/' is not treated specially, so\n"
        "                             pattern /*/foo matches paths /a/foo and /a/b/foo.") },

    {NULL}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc2_t cmd_table[] =
{
  {"crashtest", subcommand_crashtest, {0}, N_
   ("usage: svnadmin crashtest REPOS_PATH\n\n"
    "Open the repository at REPOS_PATH, then abort, thus simulating\n"
    "a process that crashes while holding an open repository handle.\n"),
   {0} },

  {"create", subcommand_create, {0}, N_
   ("usage: svnadmin create REPOS_PATH\n\n"
    "Create a new, empty repository at REPOS_PATH.\n"),
   {svnadmin__bdb_txn_nosync, svnadmin__bdb_log_keep,
    svnadmin__config_dir, svnadmin__fs_type, svnadmin__compatible_version,
    svnadmin__pre_1_4_compatible, svnadmin__pre_1_5_compatible,
    svnadmin__pre_1_6_compatible
    } },

  {"delrevprop", subcommand_delrevprop, {0}, N_
   ("usage: 1. svnadmin delrevprop REPOS_PATH -r REVISION NAME\n"
    "                   2. svnadmin delrevprop REPOS_PATH -t TXN NAME\n\n"
    "1. Delete the property NAME on revision REVISION.\n\n"
    "Use --use-pre-revprop-change-hook/--use-post-revprop-change-hook to\n"
    "trigger the revision property-related hooks (for example, if you want\n"
    "an email notification sent from your post-revprop-change hook).\n\n"
    "NOTE: Revision properties are not versioned, so this command will\n"
    "irreversibly destroy the previous value of the property.\n\n"
    "2. Delete the property NAME on transaction TXN.\n"),
   {'r', 't', svnadmin__use_pre_revprop_change_hook,
    svnadmin__use_post_revprop_change_hook} },

  {"deltify", subcommand_deltify, {0}, N_
   ("usage: svnadmin deltify [-r LOWER[:UPPER]] REPOS_PATH\n\n"
    "Run over the requested revision range, performing predecessor delti-\n"
    "fication on the paths changed in those revisions.  Deltification in\n"
    "essence compresses the repository by only storing the differences or\n"
    "delta from the preceding revision.  If no revisions are specified,\n"
    "this will simply deltify the HEAD revision.\n"),
   {'r', 'q', 'M'} },

  {"dump", subcommand_dump, {0}, N_
   ("usage: svnadmin dump REPOS_PATH [-r LOWER[:UPPER] [--incremental]]\n\n"
    "Dump the contents of filesystem to stdout in a 'dumpfile'\n"
    "portable format, sending feedback to stderr.  Dump revisions\n"
    "LOWER rev through UPPER rev.  If no revisions are given, dump all\n"
    "revision trees.  If only LOWER is given, dump that one revision tree.\n"
    "If --incremental is passed, the first revision dumped will describe\n"
    "only the paths changed in that revision; otherwise it will describe\n"
    "every path present in the repository as of that revision.  (In either\n"
    "case, the second and subsequent revisions, if any, describe only paths\n"
    "changed in those revisions.)\n"
    "\n"
    "Using --exclude or --include gives results equivalent to authz-based\n"
    "path exclusions. In particular, when the source of a copy is\n"
    "excluded, the copy is transformed into an add (unlike in 'svndumpfilter').\n"),
  {'r', svnadmin__incremental, svnadmin__deltas, 'q', 'M', 'F',
   svnadmin__exclude, svnadmin__include, svnadmin__glob },
  {{'F', N_("write to file ARG instead of stdout")}} },

  {"dump-revprops", subcommand_dump_revprops, {0}, N_
   ("usage: svnadmin dump-revprops REPOS_PATH [-r LOWER[:UPPER]]\n\n"
    "Dump the revision properties of filesystem to stdout in a 'dumpfile'\n"
    "portable format, sending feedback to stderr.  Dump revisions\n"
    "LOWER rev through UPPER rev.  If no revisions are given, dump the\n"
    "properties for all revisions.  If only LOWER is given, dump the\n"
    "properties for that one revision.\n"),
  {'r', 'q', 'F'},
  {{'F', N_("write to file ARG instead of stdout")}} },

  {"freeze", subcommand_freeze, {0}, N_
   ("usage: 1. svnadmin freeze REPOS_PATH PROGRAM [ARG...]\n"
    "               2. svnadmin freeze -F FILE PROGRAM [ARG...]\n\n"
    "1. Run PROGRAM passing ARGS while holding a write-lock on REPOS_PATH.\n"
    "   Allows safe use of third-party backup tools on a live repository.\n"
    "\n"
    "2. Like 1 except all repositories listed in FILE are locked. The file\n"
    "   format is repository paths separated by newlines.  Repositories are\n"
    "   locked in the same order as they are listed in the file.\n"),
   {'F'},
   {{'F', N_("read repository paths from file ARG")}} },

  {"help", subcommand_help, {"?", "h"}, N_
   ("usage: svnadmin help [SUBCOMMAND...]\n\n"
    "Describe the usage of this program or its subcommands.\n"),
   {0} },

  {"hotcopy", subcommand_hotcopy, {0}, N_
   ("usage: svnadmin hotcopy REPOS_PATH NEW_REPOS_PATH\n\n"
    "Make a hot copy of a repository.\n"
    "If --incremental is passed, data which already exists at the destination\n"
    "is not copied again.  Incremental mode is implemented for FSFS repositories.\n"),
   {svnadmin__clean_logs, svnadmin__incremental, 'q'} },

  {"info", subcommand_info, {0}, N_
   ("usage: svnadmin info REPOS_PATH\n\n"
    "Print information about the repository at REPOS_PATH.\n"),
   {0} },

  {"list-dblogs", subcommand_list_dblogs, {0}, N_
   ("usage: svnadmin list-dblogs REPOS_PATH\n\n"
    "List all Berkeley DB log files.\n\n"
    "WARNING: Modifying or deleting logfiles which are still in use\n"
    "will cause your repository to be corrupted.\n"),
   {0} },

  {"list-unused-dblogs", subcommand_list_unused_dblogs, {0}, N_
   ("usage: svnadmin list-unused-dblogs REPOS_PATH\n\n"
    "List unused Berkeley DB log files.\n\n"),
   {0} },

  {"load", subcommand_load, {0}, N_
   ("usage: svnadmin load REPOS_PATH\n\n"
    "Read a 'dumpfile'-formatted stream from stdin, committing\n"
    "new revisions into the repository's filesystem.  If the repository\n"
    "was previously empty, its UUID will, by default, be changed to the\n"
    "one specified in the stream.  Progress feedback is sent to stdout.\n"
    "If --revision is specified, limit the loaded revisions to only those\n"
    "in the dump stream whose revision numbers match the specified range.\n"),
   {'q', 'r', svnadmin__ignore_uuid, svnadmin__force_uuid,
    svnadmin__ignore_dates,
    svnadmin__use_pre_commit_hook, svnadmin__use_post_commit_hook,
    svnadmin__parent_dir, svnadmin__normalize_props,
    svnadmin__bypass_prop_validation, 'M',
    svnadmin__no_flush_to_disk, 'F'},
   {{'F', N_("read from file ARG instead of stdin")}} },

  {"load-revprops", subcommand_load_revprops, {0}, N_
   ("usage: svnadmin load-revprops REPOS_PATH\n\n"
    "Read a 'dumpfile'-formatted stream from stdin, setting the revision\n"
    "properties in the repository's filesystem.  Revisions not found in the\n"
    "repository will cause an error.  Progress feedback is sent to stdout.\n"
    "If --revision is specified, limit the loaded revisions to only those\n"
    "in the dump stream whose revision numbers match the specified range.\n"),
   {'q', 'r', svnadmin__force_uuid, svnadmin__normalize_props,
    svnadmin__bypass_prop_validation, svnadmin__no_flush_to_disk, 'F'},
   {{'F', N_("read from file ARG instead of stdin")}} },

  {"lock", subcommand_lock, {0}, N_
   ("usage: svnadmin lock REPOS_PATH PATH USERNAME COMMENT-FILE [TOKEN]\n\n"
    "Lock PATH by USERNAME setting comments from COMMENT-FILE.\n"
    "If provided, use TOKEN as lock token.  Use --bypass-hooks to avoid\n"
    "triggering the pre-lock and post-lock hook scripts.\n"),
  {svnadmin__bypass_hooks, 'q'} },

  {"lslocks", subcommand_lslocks, {0}, N_
   ("usage: svnadmin lslocks REPOS_PATH [PATH-IN-REPOS]\n\n"
    "Print descriptions of all locks on or under PATH-IN-REPOS (which,\n"
    "if not provided, is the root of the repository).\n"),
   {0} },

  {"lstxns", subcommand_lstxns, {0}, N_
   ("usage: svnadmin lstxns REPOS_PATH\n\n"
    "Print the names of uncommitted transactions. With -rN skip the output\n"
    "of those that have a base revision more recent than rN.  Transactions\n"
    "with base revisions much older than HEAD are likely to have been\n"
    "abandonded and are candidates to be removed.\n"),
   {'r'},
   { {'r', "transaction base revision ARG"} } },

  {"pack", subcommand_pack, {0}, N_
   ("usage: svnadmin pack REPOS_PATH\n\n"
    "Possibly compact the repository into a more efficient storage model.\n"
    "This may not apply to all repositories, in which case, exit.\n"),
   {'q', 'M'} },

  {"recover", subcommand_recover, {0}, N_
   ("usage: svnadmin recover REPOS_PATH\n\n"
    "Run the recovery procedure on a repository.  Do this if you've\n"
    "been getting errors indicating that recovery ought to be run.\n"
    "Berkeley DB recovery requires exclusive access and will\n"
    "exit if the repository is in use by another process.\n"),
   {svnadmin__wait} },

  {"rmlocks", subcommand_rmlocks, {0}, N_
   ("usage: svnadmin rmlocks REPOS_PATH LOCKED_PATH...\n\n"
    "Unconditionally remove lock from each LOCKED_PATH.\n"),
   {'q'} },

  {"rmtxns", subcommand_rmtxns, {0}, N_
   ("usage: svnadmin rmtxns REPOS_PATH TXN_NAME...\n\n"
    "Delete the named transaction(s).\n"),
   {'q'} },

  {"setlog", subcommand_setlog, {0}, N_
   ("usage: svnadmin setlog REPOS_PATH -r REVISION FILE\n\n"
    "Set the log-message on revision REVISION to the contents of FILE.  Use\n"
    "--bypass-hooks to avoid triggering the revision-property-related hooks\n"
    "(for example, if you do not want an email notification sent\n"
    "from your post-revprop-change hook, or because the modification of\n"
    "revision properties has not been enabled in the pre-revprop-change\n"
    "hook).\n\n"
    "NOTE: Revision properties are not versioned, so this command will\n"
    "overwrite the previous log message.\n"),
   {'r', svnadmin__bypass_hooks} },

  {"setrevprop", subcommand_setrevprop, {0}, N_
   ("usage: 1. svnadmin setrevprop REPOS_PATH -r REVISION NAME FILE\n"
    "                   2. svnadmin setrevprop REPOS_PATH -t TXN NAME FILE\n\n"
    "1. Set the property NAME on revision REVISION to the contents of FILE.\n\n"
    "Use --use-pre-revprop-change-hook/--use-post-revprop-change-hook to\n"
    "trigger the revision property-related hooks (for example, if you want\n"
    "an email notification sent from your post-revprop-change hook).\n\n"
    "NOTE: Revision properties are not versioned, so this command will\n"
    "overwrite the previous value of the property.\n\n"
    "2. Set the property NAME on transaction TXN to the contents of FILE.\n"),
   {'r', 't', svnadmin__use_pre_revprop_change_hook,
    svnadmin__use_post_revprop_change_hook} },

  {"setuuid", subcommand_setuuid, {0}, N_
   ("usage: svnadmin setuuid REPOS_PATH [NEW_UUID]\n\n"
    "Reset the repository UUID for the repository located at REPOS_PATH.  If\n"
    "NEW_UUID is provided, use that as the new repository UUID; otherwise,\n"
    "generate a brand new UUID for the repository.\n"),
   {0} },

  {"unlock", subcommand_unlock, {0}, N_
   ("usage: svnadmin unlock REPOS_PATH LOCKED_PATH USERNAME TOKEN\n\n"
    "Unlock LOCKED_PATH (as USERNAME) after verifying that the token\n"
    "associated with the lock matches TOKEN.  Use --bypass-hooks to avoid\n"
    "triggering the pre-unlock and post-unlock hook scripts.\n"),
   {svnadmin__bypass_hooks, 'q'} },

  {"upgrade", subcommand_upgrade, {0}, N_
   ("usage: svnadmin upgrade REPOS_PATH\n\n"
    "Upgrade the repository located at REPOS_PATH to the latest supported\n"
    "schema version.\n\n"
    "This functionality is provided as a convenience for repository\n"
    "administrators who wish to make use of new Subversion functionality\n"
    "without having to undertake a potentially costly full repository dump\n"
    "and load operation.  As such, the upgrade performs only the minimum\n"
    "amount of work needed to accomplish this while still maintaining the\n"
    "integrity of the repository.  It does not guarantee the most optimized\n"
    "repository state as a dump and subsequent load would.\n"),
   {0} },

  {"verify", subcommand_verify, {0}, N_
   ("usage: svnadmin verify REPOS_PATH\n\n"
    "Verify the data stored in the repository.\n"),
   {'t', 'r', 'q', svnadmin__keep_going, 'M',
    svnadmin__check_normalization, svnadmin__metadata_only} },

  { NULL, NULL, {0}, NULL, {0} }
};


/* Baton for passing option/argument state to a subcommand function. */
struct svnadmin_opt_state
{
  const char *repository_path;
  const char *fs_type;                              /* --fs-type */
  svn_version_t *compatible_version;                /* --compatible-version */
  svn_opt_revision_t start_revision, end_revision;  /* -r X[:Y] */
  const char *txn_id;                               /* -t TXN */
  svn_boolean_t help;                               /* --help or -? */
  svn_boolean_t version;                            /* --version */
  svn_boolean_t incremental;                        /* --incremental */
  svn_boolean_t use_deltas;                         /* --deltas */
  svn_boolean_t use_pre_commit_hook;                /* --use-pre-commit-hook */
  svn_boolean_t use_post_commit_hook;               /* --use-post-commit-hook */
  svn_boolean_t use_pre_revprop_change_hook;        /* --use-pre-revprop-change-hook */
  svn_boolean_t use_post_revprop_change_hook;       /* --use-post-revprop-change-hook */
  svn_boolean_t quiet;                              /* --quiet */
  svn_boolean_t bdb_txn_nosync;                     /* --bdb-txn-nosync */
  svn_boolean_t bdb_log_keep;                       /* --bdb-log-keep */
  svn_boolean_t clean_logs;                         /* --clean-logs */
  svn_boolean_t bypass_hooks;                       /* --bypass-hooks */
  svn_boolean_t wait;                               /* --wait */
  svn_boolean_t keep_going;                         /* --keep-going */
  svn_boolean_t check_normalization;                /* --check-normalization */
  svn_boolean_t metadata_only;                      /* --metadata-only */
  svn_boolean_t bypass_prop_validation;             /* --bypass-prop-validation */
  svn_boolean_t ignore_dates;                       /* --ignore-dates */
  svn_boolean_t no_flush_to_disk;                   /* --no-flush-to-disk */
  svn_boolean_t normalize_props;                    /* --normalize_props */
  enum svn_repos_load_uuid uuid_action;             /* --ignore-uuid,
                                                       --force-uuid */
  apr_uint64_t memory_cache_size;                   /* --memory-cache-size M */
  const char *parent_dir;                           /* --parent-dir */
  const char *file;                                 /* --file */
  apr_array_header_t *exclude;                      /* --exclude */
  apr_array_header_t *include;                      /* --include */
  svn_boolean_t glob;                               /* --pattern */

  const char *config_dir;    /* Overriding Configuration Directory */
};


/* Helper to open a repository and set a warning func (so we don't
 * SEGFAULT when libsvn_fs's default handler gets run).  */
static svn_error_t *
open_repos(svn_repos_t **repos,
           const char *path,
           struct svnadmin_opt_state *opt_state,
           apr_pool_t *pool)
{
  /* Enable the "block-read" feature (where it applies)? */
  svn_boolean_t use_block_read
    = svn_cache_config_get()->cache_size > BLOCK_READ_CACHE_THRESHOLD;

  /* construct FS configuration parameters: enable caches for r/o data */
  apr_hash_t *fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_DELTAS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NODEPROPS, "1");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "2");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_BLOCK_READ,
                           use_block_read ? "1" : "0");
  svn_hash_sets(fs_config, SVN_FS_CONFIG_NO_FLUSH_TO_DISK,
                           opt_state->no_flush_to_disk ? "1" : "0");

  /* now, open the requested repository */
  SVN_ERR(svn_repos_open3(repos, path, fs_config, pool, pool));
  svn_fs_set_warning_func(svn_repos_fs(*repos), warning_func, NULL);
  return SVN_NO_ERROR;
}


/* Set *REVNUM to the revision specified by REVISION (or to
   SVN_INVALID_REVNUM if that has the type 'unspecified'),
   possibly making use of the YOUNGEST revision number in REPOS. */
static svn_error_t *
get_revnum(svn_revnum_t *revnum, const svn_opt_revision_t *revision,
           svn_revnum_t youngest, svn_repos_t *repos, apr_pool_t *pool)
{
  if (revision->kind == svn_opt_revision_number)
    *revnum = revision->value.number;
  else if (revision->kind == svn_opt_revision_head)
    *revnum = youngest;
  else if (revision->kind == svn_opt_revision_date)
    SVN_ERR(svn_repos_dated_revision(revnum, repos, revision->value.date,
                                     pool));
  else if (revision->kind == svn_opt_revision_unspecified)
    *revnum = SVN_INVALID_REVNUM;
  else
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Invalid revision specifier"));

  if (*revnum > youngest)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       _("Revisions must not be greater than the youngest revision (%ld)"),
       youngest);

  return SVN_NO_ERROR;
}

/* Set *FSPATH to an internal-style fspath parsed from ARG. */
static svn_error_t *
target_arg_to_fspath(const char **fspath,
                     const char *arg,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  /* ### Using a private API.  This really shouldn't be needed. */
  *fspath = svn_fspath__canonicalize(arg, result_pool);
  return SVN_NO_ERROR;
}

/* Set *DIRENT to an internal-style, local dirent path
   allocated from POOL and parsed from PATH. */
static svn_error_t *
target_arg_to_dirent(const char **dirent,
                     const char *path,
                     apr_pool_t *pool)
{
  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a local path"), path);
  *dirent = svn_dirent_internal_style(path, pool);
  return SVN_NO_ERROR;
}

/* Parse the remaining command-line arguments from OS, returning them
   in a new array *ARGS (allocated from POOL) and optionally verifying
   that we got the expected number thereof.  If MIN_EXPECTED is not
   negative, return an error if the function would return fewer than
   MIN_EXPECTED arguments.  If MAX_EXPECTED is not negative, return an
   error if the function would return more than MAX_EXPECTED
   arguments.

   As a special case, when MIN_EXPECTED and MAX_EXPECTED are both 0,
   allow ARGS to be NULL.  */
static svn_error_t *
parse_args(apr_array_header_t **args,
           apr_getopt_t *os,
           int min_expected,
           int max_expected,
           apr_pool_t *pool)
{
  int num_args = os ? (os->argc - os->ind) : 0;

  if (min_expected || max_expected)
    SVN_ERR_ASSERT(args);

  if ((min_expected >= 0) && (num_args < min_expected))
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                            _("Not enough arguments"));
  if ((max_expected >= 0) && (num_args > max_expected))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("Too many arguments"));
  if (args)
    {
      *args = apr_array_make(pool, num_args, sizeof(const char *));

      if (num_args)
        while (os->ind < os->argc)
          {
            const char *arg;

            SVN_ERR(svn_utf_cstring_to_utf8(&arg, os->argv[os->ind++], pool));
            APR_ARRAY_PUSH(*args, const char *) = arg;
          }
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_error_malfunction_handler_t. */
static svn_error_t *
crashtest_malfunction_handler(svn_boolean_t can_return,
                              const char *file,
                              int line,
                              const char *expr)
{
  abort();
  return SVN_NO_ERROR; /* Not reached. */
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_crashtest(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;

  (void)svn_error_set_malfunction_handler(crashtest_malfunction_handler);
  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  SVN_ERR(svn_cmdline_printf(pool,
                             _("Successfully opened repository '%s'.\n"
                               "Will now crash to simulate a crashing "
                               "server process.\n"),
                             svn_dirent_local_style(opt_state->repository_path,
                                                    pool)));
  SVN_ERR_MALFUNCTION();

  /* merely silence a compiler warning (this will never be executed) */
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_create(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  apr_hash_t *fs_config = apr_hash_make(pool);

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  svn_hash_sets(fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
                (opt_state->bdb_txn_nosync ? "1" :"0"));

  svn_hash_sets(fs_config, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE,
                (opt_state->bdb_log_keep ? "0" :"1"));

  if (opt_state->fs_type)
    {
      /* With 1.8 we are announcing that BDB is deprecated.  No support
       * has been removed and it will continue to work until some future
       * date.  The purpose here is to discourage people from creating
       * new BDB repositories which they will need to dump/load into
       * FSFS or some new FS type in the future. */
      if (0 == strcmp(opt_state->fs_type, SVN_FS_TYPE_BDB))
        {
          SVN_ERR(svn_cmdline_fprintf(
                      stderr, pool,
                      _("%swarning:"
                        " The \"%s\" repository back-end is deprecated,"
                        " consider using \"%s\" instead.\n"),
                      "svnadmin: ", SVN_FS_TYPE_BDB, SVN_FS_TYPE_FSFS));
          fflush(stderr);
        }
      svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, opt_state->fs_type);
    }

  if (opt_state->compatible_version)
    {
      if (! svn_version__at_least(opt_state->compatible_version, 1, 4, 0))
        svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE, "1");
      if (! svn_version__at_least(opt_state->compatible_version, 1, 5, 0))
        svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_5_COMPATIBLE, "1");
      if (! svn_version__at_least(opt_state->compatible_version, 1, 6, 0))
        svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_6_COMPATIBLE, "1");
      if (! svn_version__at_least(opt_state->compatible_version, 1, 8, 0))
        svn_hash_sets(fs_config, SVN_FS_CONFIG_PRE_1_8_COMPATIBLE, "1");
      /* In 1.9, we figured out that we didn't have to keep extending this
         madness indefinitely. */
      svn_hash_sets(fs_config, SVN_FS_CONFIG_COMPATIBLE_VERSION,
                    apr_psprintf(pool, "%d.%d.%d%s%s",
                                 opt_state->compatible_version->major,
                                 opt_state->compatible_version->minor,
                                 opt_state->compatible_version->patch,
                                 opt_state->compatible_version->tag
                                 ? "-" : "",
                                 opt_state->compatible_version->tag
                                 ? opt_state->compatible_version->tag : ""));
    }

  if (opt_state->compatible_version)
    {
      if (! svn_version__at_least(opt_state->compatible_version, 1, 1, 0)
          /* ### TODO: this NULL check hard-codes knowledge of the library's
                       default fs-type value */
          && (opt_state->fs_type == NULL
              || !strcmp(opt_state->fs_type, SVN_FS_TYPE_FSFS)))
        {
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("Repositories compatible with 1.0.x must "
                                    "use --fs-type=bdb"));
        }

      if (! svn_version__at_least(opt_state->compatible_version, 1, 9, 0)
          && opt_state->fs_type && !strcmp(opt_state->fs_type, SVN_FS_TYPE_FSX))
        {
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("Repositories compatible with 1.8.x or "
                                     "earlier cannot use --fs-type=%s"),
                                   SVN_FS_TYPE_FSX);
        }
    }

  SVN_ERR(svn_repos_create(&repos, opt_state->repository_path,
                           NULL, NULL, NULL, fs_config, pool));
  svn_fs_set_warning_func(svn_repos_fs(repos), warning_func, NULL);
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_deltify(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_revnum_t start = SVN_INVALID_REVNUM, end = SVN_INVALID_REVNUM;
  svn_revnum_t youngest, revision;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));

  /* Find the revision numbers at which to start and end. */
  SVN_ERR(get_revnum(&start, &opt_state->start_revision,
                     youngest, repos, pool));
  SVN_ERR(get_revnum(&end, &opt_state->end_revision,
                     youngest, repos, pool));

  /* Fill in implied revisions if necessary. */
  if (start == SVN_INVALID_REVNUM)
    start = youngest;
  if (end == SVN_INVALID_REVNUM)
    end = start;

  if (start > end)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       _("First revision cannot be higher than second"));

  /* Loop over the requested revision range, performing the
     predecessor deltification on paths changed in each. */
  for (revision = start; revision <= end; revision++)
    {
      svn_pool_clear(subpool);
      SVN_ERR(check_cancel(NULL));
      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(subpool, _("Deltifying revision %ld..."),
                                   revision));
      SVN_ERR(svn_fs_deltify_revision(fs, revision, subpool));
      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(subpool, _("done.\n")));
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Structure for errors encountered during 'svnadmin verify --keep-going'. */
struct verification_error
{
  svn_revnum_t rev;
  svn_error_t *err;
};

/* Pool cleanup function to clear an svn_error_t *. */
static apr_status_t
err_cleanup(void *data)
{
  svn_error_t *err = data;

  svn_error_clear(err);

  return APR_SUCCESS;
}

struct repos_verify_callback_baton
{
  /* Should we continue after receiving a first verification error? */
  svn_boolean_t keep_going;

  /* List of errors encountered during 'svnadmin verify --keep-going'. */
  apr_array_header_t *error_summary;

  /* Pool for data collected during callback invocations. */
  apr_pool_t *result_pool;
};

/* Implementation of svn_repos_verify_callback_t to handle errors coming
   from svn_repos_verify_fs3(). */
static svn_error_t *
repos_verify_callback(void *baton,
                      svn_revnum_t revision,
                      svn_error_t *verify_err,
                      apr_pool_t *scratch_pool)
{
  struct repos_verify_callback_baton *b = baton;

  if (revision == SVN_INVALID_REVNUM)
    {
      SVN_ERR(svn_cmdline_fputs(_("* Error verifying repository metadata.\n"),
                                stderr, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                  _("* Error verifying revision %ld.\n"),
                                  revision));
    }

  if (b->keep_going)
    {
      struct verification_error *verr;

      svn_handle_error2(verify_err, stderr, FALSE, "svnadmin: ");

      /* Remember the error in B->ERROR_SUMMARY. */
      verr = apr_palloc(b->result_pool, sizeof(*verr));
      verr->rev = revision;
      verr->err = svn_error_dup(verify_err);
      apr_pool_cleanup_register(b->result_pool, verr->err, err_cleanup,
                                apr_pool_cleanup_null);
      APR_ARRAY_PUSH(b->error_summary, struct verification_error *) = verr;

      return SVN_NO_ERROR;
    }
  else
    return svn_error_trace(svn_error_dup(verify_err));
}

/* Implementation of svn_repos_notify_func_t to wrap the output to a
   response stream for svn_repos_dump_fs2(), svn_repos_verify_fs(),
   svn_repos_hotcopy3() and others. */
static void
repos_notify_handler(void *baton,
                     const svn_repos_notify_t *notify,
                     apr_pool_t *scratch_pool)
{
  svn_stream_t *feedback_stream = baton;

  switch (notify->action)
  {
    case svn_repos_notify_warning:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                        "WARNING 0x%04x: %s\n", notify->warning,
                                        notify->warning_str));
      return;

    case svn_repos_notify_dump_rev_end:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                        _("* Dumped revision %ld.\n"),
                                        notify->revision));
      return;

    case svn_repos_notify_verify_rev_end:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                        _("* Verified revision %ld.\n"),
                                        notify->revision));
      return;

    case svn_repos_notify_verify_rev_structure:
      if (notify->revision == SVN_INVALID_REVNUM)
        svn_error_clear(svn_stream_puts(feedback_stream,
                                _("* Verifying repository metadata ...\n")));
      else
        svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                        _("* Verifying metadata at revision %ld ...\n"),
                        notify->revision));
      return;

    case svn_repos_notify_pack_shard_start:
      {
        const char *shardstr = apr_psprintf(scratch_pool,
                                            "%" APR_INT64_T_FMT,
                                            notify->shard);
        svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                          _("Packing revisions in shard %s..."),
                                          shardstr));
      }
      return;

    case svn_repos_notify_pack_shard_end:
      svn_error_clear(svn_stream_puts(feedback_stream, _("done.\n")));
      return;

    case svn_repos_notify_pack_shard_start_revprop:
      {
        const char *shardstr = apr_psprintf(scratch_pool,
                                            "%" APR_INT64_T_FMT,
                                            notify->shard);
        svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                          _("Packing revprops in shard %s..."),
                                          shardstr));
      }
      return;

    case svn_repos_notify_pack_shard_end_revprop:
      svn_error_clear(svn_stream_puts(feedback_stream, _("done.\n")));
      return;

    case svn_repos_notify_load_txn_committed:
      if (notify->old_revision == SVN_INVALID_REVNUM)
        {
          svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                            _("\n------- Committed revision %ld >>>\n\n"),
                            notify->new_revision));
        }
      else
        {
          svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                            _("\n------- Committed new rev %ld"
                              " (loaded from original rev %ld"
                              ") >>>\n\n"), notify->new_revision,
                              notify->old_revision));
        }
      return;

    case svn_repos_notify_load_node_start:
      {
        switch (notify->node_action)
        {
          case svn_node_action_change:
            svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                  _("     * editing path : %s ..."),
                                  notify->path));
            break;

          case svn_node_action_delete:
            svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                  _("     * deleting path : %s ..."),
                                  notify->path));
            break;

          case svn_node_action_add:
            svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                  _("     * adding path : %s ..."),
                                  notify->path));
            break;

          case svn_node_action_replace:
            svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                  _("     * replacing path : %s ..."),
                                  notify->path));
            break;

        }
      }
      return;

    case svn_repos_notify_load_node_done:
      svn_error_clear(svn_stream_puts(feedback_stream, _(" done.\n")));
      return;

    case svn_repos_notify_load_copied_node:
      svn_error_clear(svn_stream_puts(feedback_stream, "COPIED..."));
      return;

    case svn_repos_notify_load_txn_start:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                _("<<< Started new transaction, based on "
                                  "original revision %ld\n"),
                                notify->old_revision));
      return;

    case svn_repos_notify_load_skipped_rev:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                _("<<< Skipped original revision %ld\n"),
                                notify->old_revision));
      return;

    case svn_repos_notify_load_normalized_mergeinfo:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                _(" removing '\\r' from %s ..."),
                                SVN_PROP_MERGEINFO));
      return;

    case svn_repos_notify_mutex_acquired:
      svn_cmdline__setup_cancellation_handler();
      return;

    case svn_repos_notify_recover_start:
      svn_error_clear(svn_stream_puts(feedback_stream,
                             _("Repository lock acquired.\n"
                               "Please wait; recovering the"
                               " repository may take some time...\n")));
      return;

    case svn_repos_notify_upgrade_start:
      svn_error_clear(svn_stream_puts(feedback_stream,
                             _("Repository lock acquired.\n"
                               "Please wait; upgrading the"
                               " repository may take some time...\n")));
      return;

    case svn_repos_notify_pack_revprops:
      {
        const char *shardstr = apr_psprintf(scratch_pool,
                                            "%" APR_INT64_T_FMT,
                                            notify->shard);
        svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                              _("Packed revision properties in shard %s\n"),
                              shardstr));
        return;
      }

    case svn_repos_notify_cleanup_revprops:
      {
        const char *shardstr = apr_psprintf(scratch_pool,
                                            "%" APR_INT64_T_FMT,
                                            notify->shard);
        svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                              _("Removed non-packed revision properties"
                                " in shard %s\n"),
                              shardstr));
        return;
      }

    case svn_repos_notify_format_bumped:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                            _("Bumped repository format to %ld\n"),
                            notify->revision));
      return;

    case svn_repos_notify_hotcopy_rev_range:
      if (notify->start_revision == notify->end_revision)
        {
          svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                                            _("* Copied revision %ld.\n"),
                                            notify->start_revision));
        }
      else
        {
          svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                               _("* Copied revisions from %ld to %ld.\n"),
                               notify->start_revision, notify->end_revision));
        }
      return;

    case svn_repos_notify_pack_noop:
      /* For best backward compatibility, we keep silent if there were just
         no more shards to pack. */
      if (notify->shard == -1)
        {
          svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                     _("svnadmin: Warning - this repository is not sharded."
                       " Packing has no effect.\n")));
        }
      return;

    case svn_repos_notify_load_revprop_set:
      svn_error_clear(svn_stream_printf(feedback_stream, scratch_pool,
                        _("Properties set on revision %ld.\n"),
                        notify->new_revision));
      return;

    default:
      return;
  }
}


/* Baton for recode_write(). */
struct recode_write_baton
{
  apr_pool_t *pool;
  FILE *out;
};

/* This implements the 'svn_write_fn_t' interface.

   Write DATA to ((struct recode_write_baton *) BATON)->out, in the
   console encoding, using svn_cmdline_fprintf().  DATA is a
   UTF8-encoded C string, therefore ignore LEN.

   ### This recoding mechanism might want to be abstracted into
   ### svn_io.h or svn_cmdline.h, if it proves useful elsewhere. */
static svn_error_t *recode_write(void *baton,
                                 const char *data,
                                 apr_size_t *len)
{
  struct recode_write_baton *rwb = baton;
  svn_pool_clear(rwb->pool);
  return svn_cmdline_fputs(data, rwb->out, rwb->pool);
}

/* Create a stream, to write to STD_STREAM, that uses recode_write()
   to perform UTF-8 to console encoding translation. */
static svn_stream_t *
recode_stream_create(FILE *std_stream, apr_pool_t *pool)
{
  struct recode_write_baton *std_stream_rwb =
    apr_palloc(pool, sizeof(struct recode_write_baton));

  svn_stream_t *rw_stream = svn_stream_create(std_stream_rwb, pool);
  std_stream_rwb->pool = svn_pool_create(pool);
  std_stream_rwb->out = std_stream;
  svn_stream_set_write(rw_stream, recode_write);
  return rw_stream;
}

/* Read the min / max revision from the OPT_STATE, verify them against REPOS
   and return them in *LOWER and *UPPER, respectively.  Use SCRATCH_POOL
   for temporary allocations. */
static svn_error_t *
get_dump_range(svn_revnum_t *lower,
               svn_revnum_t *upper,
               svn_repos_t *repos,
               struct svnadmin_opt_state *opt_state,
               apr_pool_t *scratch_pool)
{
  svn_fs_t *fs;
  svn_revnum_t youngest;

  *lower = SVN_INVALID_REVNUM;
  *upper = SVN_INVALID_REVNUM;

  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, scratch_pool));

  /* Find the revision numbers at which to start and end. */
  SVN_ERR(get_revnum(lower, &opt_state->start_revision,
                     youngest, repos, scratch_pool));
  SVN_ERR(get_revnum(upper, &opt_state->end_revision,
                     youngest, repos, scratch_pool));

  /* Fill in implied revisions if necessary. */
  if (*lower == SVN_INVALID_REVNUM)
    {
      *lower = 0;
      *upper = youngest;
    }
  else if (*upper == SVN_INVALID_REVNUM)
    {
      *upper = *lower;
    }

  if (*lower > *upper)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       _("First revision cannot be higher than second"));

  return SVN_NO_ERROR;
}

/* Compare the node-path PATH with the (const char *) prefixes in PFXLIST.
 * Return TRUE if any prefix is a prefix of PATH (matching whole path
 * components); FALSE otherwise.
 * PATH starts with a '/', as do the (const char *) paths in PREFIXES. */
/* This function is a duplicate of svndumpfilter.c:ary_prefix_match(). */
static svn_boolean_t
ary_prefix_match(const apr_array_header_t *pfxlist, const char *path)
{
  int i;
  size_t path_len = strlen(path);

  for (i = 0; i < pfxlist->nelts; i++)
    {
      const char *pfx = APR_ARRAY_IDX(pfxlist, i, const char *);
      size_t pfx_len = strlen(pfx);

      if (path_len < pfx_len)
        continue;
      if (strncmp(path, pfx, pfx_len) == 0
          && (pfx_len == 1 || path[pfx_len] == '\0' || path[pfx_len] == '/'))
        return TRUE;
    }

  return FALSE;
}

/* Baton for dump_filter_func(). */
struct dump_filter_baton_t
{
  apr_array_header_t *prefixes;
  svn_boolean_t glob;
  svn_boolean_t do_exclude;
};

/* Implements svn_repos_dump_filter_func_t. */
static svn_error_t *
dump_filter_func(svn_boolean_t *include,
                 svn_fs_root_t *root,
                 const char *path,
                 void *baton,
                 apr_pool_t *scratch_pool)
{
  struct dump_filter_baton_t *b = baton;
  const svn_boolean_t matches =
    (b->glob
     ? svn_cstring_match_glob_list(path, b->prefixes)
     : ary_prefix_match(b->prefixes, path));

  *include = b->do_exclude ? !matches : matches;
  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_dump(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_stream_t *out_stream;
  svn_revnum_t lower, upper;
  svn_stream_t *feedback_stream = NULL;
  struct dump_filter_baton_t filter_baton = {0};

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  SVN_ERR(get_dump_range(&lower, &upper, repos, opt_state, pool));

  /* Open the file or STDOUT, depending on whether -F was specified. */
  if (opt_state->file)
    {
      apr_file_t *file;

      /* Overwrite existing files, same as with > redirection. */
      SVN_ERR(svn_io_file_open(&file, opt_state->file,
                               APR_WRITE | APR_CREATE | APR_TRUNCATE
                               | APR_BUFFERED, APR_OS_DEFAULT, pool));
      out_stream = svn_stream_from_aprfile2(file, FALSE, pool);
    }
  else
    SVN_ERR(svn_stream_for_stdout(&out_stream, pool));

  /* Progress feedback goes to STDERR, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stderr, pool);

  /* Initialize the filter baton. */
  filter_baton.glob = opt_state->glob;

  if (opt_state->exclude && !opt_state->include)
    {
      filter_baton.prefixes = opt_state->exclude;
      filter_baton.do_exclude = TRUE;
    }
  else if (opt_state->include && !opt_state->exclude)
    {
      filter_baton.prefixes = opt_state->include;
      filter_baton.do_exclude = FALSE;
    }
  else if (opt_state->include && opt_state->exclude)
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("'--exclude' and '--include' options "
                                 "cannot be used simultaneously"));
    }

  SVN_ERR(svn_repos_dump_fs4(repos, out_stream, lower, upper,
                             opt_state->incremental, opt_state->use_deltas,
                             TRUE, TRUE,
                             !opt_state->quiet ? repos_notify_handler : NULL,
                             feedback_stream,
                             filter_baton.prefixes ? dump_filter_func : NULL,
                             &filter_baton,
                             check_cancel, NULL, pool));

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_dump_revprops(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_stream_t *out_stream;
  svn_revnum_t lower, upper;
  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  SVN_ERR(get_dump_range(&lower, &upper, repos, opt_state, pool));

  /* Open the file or STDOUT, depending on whether -F was specified. */
  if (opt_state->file)
    {
      apr_file_t *file;

      /* Overwrite existing files, same as with > redirection. */
      SVN_ERR(svn_io_file_open(&file, opt_state->file,
                               APR_WRITE | APR_CREATE | APR_TRUNCATE
                               | APR_BUFFERED, APR_OS_DEFAULT, pool));
      out_stream = svn_stream_from_aprfile2(file, FALSE, pool);
    }
  else
    SVN_ERR(svn_stream_for_stdout(&out_stream, pool));

  /* Progress feedback goes to STDERR, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stderr, pool);

  SVN_ERR(svn_repos_dump_fs4(repos, out_stream, lower, upper,
                             FALSE, FALSE, TRUE, FALSE,
                             !opt_state->quiet ? repos_notify_handler : NULL,
                             feedback_stream, NULL, NULL,
                             check_cancel, NULL, pool));

  return SVN_NO_ERROR;
}

struct freeze_baton_t {
  const char *command;
  const char **args;
  int status;
};

/* Implements svn_repos_freeze_func_t */
static svn_error_t *
freeze_body(void *baton,
            apr_pool_t *pool)
{
  struct freeze_baton_t *b = baton;
  apr_status_t apr_err;
  apr_file_t *infile, *outfile, *errfile;

  apr_err = apr_file_open_stdin(&infile, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Can't open stdin");
  apr_err = apr_file_open_stdout(&outfile, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Can't open stdout");
  apr_err = apr_file_open_stderr(&errfile, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Can't open stderr");

  SVN_ERR(svn_io_run_cmd(NULL, b->command, b->args, &b->status,
                         NULL, TRUE,
                         infile, outfile, errfile, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
subcommand_freeze(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *paths;
  apr_array_header_t *args;
  int i;
  struct freeze_baton_t b;

  SVN_ERR(parse_args(&args, os, -1, -1, pool));

  if (!args->nelts)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("No program provided"));

  if (!opt_state->file)
    {
      /* One repository on the command line. */
      paths = apr_array_make(pool, 1, sizeof(const char *));
      APR_ARRAY_PUSH(paths, const char *) = opt_state->repository_path;
    }
  else
    {
      svn_stringbuf_t *buf;
      const char *utf8;
      /* Read repository paths from the -F file. */
      SVN_ERR(svn_stringbuf_from_file2(&buf, opt_state->file, pool));
      SVN_ERR(svn_utf_cstring_to_utf8(&utf8, buf->data, pool));
      paths = svn_cstring_split(utf8, "\r\n", FALSE, pool);
    }

  b.command = APR_ARRAY_IDX(args, 0, const char *);
  b.args = apr_palloc(pool, sizeof(char *) * (args->nelts + 1));
  for (i = 0; i < args->nelts; ++i)
    b.args[i] = APR_ARRAY_IDX(args, i, const char *);
  b.args[args->nelts] = NULL;

  SVN_ERR(svn_repos_freeze(paths, freeze_body, &b, pool));

  /* Make any non-zero status visible to the user. */
  if (b.status)
    exit(b.status);

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  const char *header =
    _("general usage: svnadmin SUBCOMMAND REPOS_PATH  [ARGS & OPTIONS ...]\n"
      "Subversion repository administration tool.\n"
      "Type 'svnadmin help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnadmin --version' to see the program version and FS modules.\n"
      "\n"
      "Available subcommands:\n");

  const char *fs_desc_start
    = _("The following repository back-end (FS) modules are available:\n\n");

  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(fs_desc_start, pool);
  SVN_ERR(svn_fs_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help4(os, "svnadmin",
                              opt_state ? opt_state->version : FALSE,
                              opt_state ? opt_state->quiet : FALSE,
                              /*###opt_state ? opt_state->verbose :*/ FALSE,
                              version_footer->data,
                              header, cmd_table, options_table, NULL, NULL,
                              pool));

  return SVN_NO_ERROR;
}


/* Set *REVNUM to the revision number of a numeric REV, or to
   SVN_INVALID_REVNUM if REV is unspecified. */
static svn_error_t *
optrev_to_revnum(svn_revnum_t *revnum, const svn_opt_revision_t *opt_rev)
{
  if (opt_rev->kind == svn_opt_revision_number)
    {
      *revnum = opt_rev->value.number;
      if (! SVN_IS_VALID_REVNUM(*revnum))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Invalid revision number (%ld) specified"),
                                 *revnum);
    }
  else if (opt_rev->kind == svn_opt_revision_unspecified)
    {
      *revnum = SVN_INVALID_REVNUM;
    }
  else
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Non-numeric revision specified"));
    }
  return SVN_NO_ERROR;
}

/* Read the min / max revision from the OPT_STATE, verify them and return
   them in *LOWER and *UPPER, respectively. */
static svn_error_t *
get_load_range(svn_revnum_t *lower,
               svn_revnum_t *upper,
               struct svnadmin_opt_state *opt_state)
{
  /* Find the revision numbers at which to start and end.  We only
     support a limited set of revision kinds: number and unspecified. */
  SVN_ERR(optrev_to_revnum(lower, &opt_state->start_revision));
  SVN_ERR(optrev_to_revnum(upper, &opt_state->end_revision));

  /* Fill in implied revisions if necessary. */
  if ((*upper == SVN_INVALID_REVNUM) && (*lower != SVN_INVALID_REVNUM))
    {
      *upper = *lower;
    }
  else if ((*upper != SVN_INVALID_REVNUM) && (*lower == SVN_INVALID_REVNUM))
    {
      *lower = *upper;
    }

  /* Ensure correct range ordering. */
  if (*lower > *upper)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("First revision cannot be higher than second"));
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_load(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_error_t *err;
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_revnum_t lower, upper;
  svn_stream_t *in_stream;
  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  /* Find the revision numbers at which to start and end.  We only
     support a limited set of revision kinds: number and unspecified. */
  SVN_ERR(get_load_range(&lower, &upper, opt_state));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));

  /* Open the file or STDIN, depending on whether -F was specified. */
  if (opt_state->file)
    SVN_ERR(svn_stream_open_readonly(&in_stream, opt_state->file,
                                     pool, pool));
  else
    SVN_ERR(svn_stream_for_stdin2(&in_stream, TRUE, pool));

  /* Progress feedback goes to STDOUT, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stdout, pool);

  err = svn_repos_load_fs6(repos, in_stream, lower, upper,
                           opt_state->uuid_action, opt_state->parent_dir,
                           opt_state->use_pre_commit_hook,
                           opt_state->use_post_commit_hook,
                           !opt_state->bypass_prop_validation,
                           opt_state->ignore_dates,
                           opt_state->normalize_props,
                           opt_state->quiet ? NULL : repos_notify_handler,
                           feedback_stream, check_cancel, NULL, pool);

  if (svn_error_find_cause(err, SVN_ERR_BAD_PROPERTY_VALUE_EOL))
    {
      return svn_error_quick_wrap(err,
                                  _("A property with invalid line ending "
                                    "found in dumpstream; consider using "
                                    "--normalize-props while loading."));
    }
  else if (err && err->apr_err == SVN_ERR_BAD_PROPERTY_VALUE)
    {
      return svn_error_quick_wrap(err,
                                  _("Invalid property value found in "
                                    "dumpstream; consider repairing the "
                                    "source or using --bypass-prop-validation "
                                    "while loading."));
    }

  return err;
}

static svn_error_t *
subcommand_load_revprops(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_error_t *err;
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_revnum_t lower, upper;
  svn_stream_t *in_stream;

  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  /* Find the revision numbers at which to start and end.  We only
     support a limited set of revision kinds: number and unspecified. */
  SVN_ERR(get_load_range(&lower, &upper, opt_state));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));

  /* Open the file or STDIN, depending on whether -F was specified. */
  if (opt_state->file)
    SVN_ERR(svn_stream_open_readonly(&in_stream, opt_state->file,
                                     pool, pool));
  else
    SVN_ERR(svn_stream_for_stdin2(&in_stream, TRUE, pool));

  /* Progress feedback goes to STDOUT, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stdout, pool);

  err = svn_repos_load_fs_revprops(repos, in_stream, lower, upper,
                                   !opt_state->bypass_prop_validation,
                                   opt_state->ignore_dates,
                                   opt_state->normalize_props,
                                   opt_state->quiet ? NULL
                                                    : repos_notify_handler,
                                   feedback_stream, check_cancel, NULL, pool);

  if (svn_error_find_cause(err, SVN_ERR_BAD_PROPERTY_VALUE_EOL))
    {
      return svn_error_quick_wrap(err,
                                  _("A property with invalid line ending "
                                    "found in dumpstream; consider using "
                                    "--normalize-props while loading."));
    }
  else if (err && err->apr_err == SVN_ERR_BAD_PROPERTY_VALUE)
    {
      return svn_error_quick_wrap(err,
                                  _("Invalid property value found in "
                                    "dumpstream; consider repairing the "
                                    "source or using --bypass-prop-validation "
                                    "while loading."));
    }

  return err;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_lstxns(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  apr_array_header_t *txns;
  apr_pool_t *iterpool;
  svn_revnum_t youngest, limit;
  int i;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));
  if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Revision range is not allowed"));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_list_transactions(&txns, fs, pool));

  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));
  SVN_ERR(get_revnum(&limit, &opt_state->start_revision, youngest, repos,
                     pool));
  
  iterpool = svn_pool_create(pool);
  for (i = 0; i < txns->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(txns, i, const char *);
      svn_boolean_t show = TRUE;

      svn_pool_clear(iterpool);
      if (limit != SVN_INVALID_REVNUM)
        {
          svn_fs_txn_t *txn;
          svn_revnum_t base;

          SVN_ERR(svn_fs_open_txn(&txn, fs, name, iterpool));
          base = svn_fs_txn_base_revision(txn);

          if (base > limit)
            show = FALSE;
        }
      if (show)
        SVN_ERR(svn_cmdline_printf(pool, "%s\n", name));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_recover(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_revnum_t youngest_rev;
  svn_repos_t *repos;
  svn_error_t *err;
  struct svnadmin_opt_state *opt_state = baton;
  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(svn_stream_for_stdout(&feedback_stream, pool));

  /* Restore default signal handlers until after we have acquired the
   * exclusive lock so that the user interrupt before we actually
   * touch the repository. */
  svn_cmdline__disable_cancellation_handler();

  err = svn_repos_recover4(opt_state->repository_path, TRUE,
                           repos_notify_handler, feedback_stream,
                           check_cancel, NULL, pool);
  if (err)
    {
      if (! APR_STATUS_IS_EAGAIN(err->apr_err))
        return err;
      svn_error_clear(err);
      if (! opt_state->wait)
        return svn_error_create(SVN_ERR_REPOS_LOCKED, NULL,
                                _("Failed to get exclusive repository "
                                  "access; perhaps another process\n"
                                  "such as httpd, svnserve or svn "
                                  "has it open?"));
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("Waiting on repository lock; perhaps"
                                   " another process has it open?\n")));
      SVN_ERR(svn_cmdline_fflush(stdout));
      SVN_ERR(svn_repos_recover4(opt_state->repository_path, FALSE,
                                 repos_notify_handler, feedback_stream,
                                 check_cancel, NULL, pool));
    }

  SVN_ERR(svn_cmdline_printf(pool, _("\nRecovery completed.\n")));

  /* Since db transactions may have been replayed, it's nice to tell
     people what the latest revision is.  It also proves that the
     recovery actually worked. */
  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, svn_repos_fs(repos), pool));
  SVN_ERR(svn_cmdline_printf(pool, _("The latest repos revision is %ld.\n"),
                             youngest_rev));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
list_dblogs(apr_getopt_t *os, void *baton, svn_boolean_t only_unused,
            apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *logfiles;
  int i;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(svn_repos_db_logfiles(&logfiles,
                                opt_state->repository_path,
                                only_unused,
                                pool));

  /* Loop, printing log files.  We append the log paths to the
     repository path, making sure to return everything to the native
     style before printing. */
  for (i = 0; i < logfiles->nelts; i++)
    {
      const char *log_path;
      log_path = svn_dirent_join(opt_state->repository_path,
                                 APR_ARRAY_IDX(logfiles, i, const char *),
                                 pool);
      log_path = svn_dirent_local_style(log_path, pool);
      SVN_ERR(svn_cmdline_printf(pool, "%s\n", log_path));
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_list_dblogs(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  SVN_ERR(list_dblogs(os, baton, FALSE, pool));
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_list_unused_dblogs(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(list_dblogs(os, baton, TRUE, pool));
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_rmtxns(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_array_header_t *args;
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(parse_args(&args, os, -1, -1, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);

  /* All the rest of the arguments are transaction names. */
  for (i = 0; i < args->nelts; i++)
    {
      const char *txn_name = APR_ARRAY_IDX(args, i, const char *);
      svn_error_t *err;

      svn_pool_clear(subpool);

      /* Try to open the txn.  If that succeeds, try to abort it. */
      err = svn_fs_open_txn(&txn, fs, txn_name, subpool);
      if (! err)
        err = svn_fs_abort_txn(txn, subpool);

      /* If either the open or the abort of the txn fails because that
         transaction is dead, just try to purge the thing.  Else,
         there was either an error worth reporting, or not error at
         all.  */
      if (err && (err->apr_err == SVN_ERR_FS_TRANSACTION_DEAD))
        {
          svn_error_clear(err);
          err = svn_fs_purge_txn(fs, txn_name, subpool);
        }

      /* If we had a real from the txn open, abort, or purge, we clear
         that error and just report to the user that we had an issue
         with this particular txn. */
      if (err)
        {
          svn_handle_error2(err, stderr, FALSE /* non-fatal */, "svnadmin: ");
          svn_error_clear(err);
        }
      else if (! opt_state->quiet)
        {
          SVN_ERR(svn_cmdline_printf(subpool, _("Transaction '%s' removed.\n"),
                                     txn_name));
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* A helper for the 'setrevprop' and 'setlog' commands.  Expects
   OPT_STATE->txn_id, OPT_STATE->use_pre_revprop_change_hook and
   OPT_STATE->use_post_revprop_change_hook to be set appropriately.
   If FILENAME is NULL, delete property PROP_NAME.  */
static svn_error_t *
set_revprop(const char *prop_name, const char *filename,
            struct svnadmin_opt_state *opt_state, apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_string_t *prop_value;

  if (filename)
    {
      svn_stringbuf_t *file_contents;

      SVN_ERR(svn_stringbuf_from_file2(&file_contents, filename, pool));

      prop_value = svn_string_create_empty(pool);
      prop_value->data = file_contents->data;
      prop_value->len = file_contents->len;

      SVN_ERR(svn_subst_translate_string2(&prop_value, NULL, NULL, prop_value,
                                          NULL, FALSE, pool, pool));
    }
  else
    {
      prop_value = NULL;
    }

  /* Open the filesystem  */
  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));

  if (opt_state->txn_id)
    {
      svn_fs_t *fs = svn_repos_fs(repos);
      svn_fs_txn_t *txn;

      SVN_ERR(svn_fs_open_txn(&txn, fs, opt_state->txn_id, pool));
      SVN_ERR(svn_fs_change_txn_prop(txn, prop_name, prop_value, pool));
    }
  else
    SVN_ERR(svn_repos_fs_change_rev_prop4(
              repos, opt_state->start_revision.value.number,
              NULL, prop_name, NULL, prop_value,
              opt_state->use_pre_revprop_change_hook,
              opt_state->use_post_revprop_change_hook,
              NULL, NULL, pool));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_setrevprop(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *args;
  const char *prop_name, *filename;

  /* Expect two more arguments: NAME FILE */
  SVN_ERR(parse_args(&args, os, 2, 2, pool));
  prop_name = APR_ARRAY_IDX(args, 0, const char *);
  filename = APR_ARRAY_IDX(args, 1, const char *);
  SVN_ERR(target_arg_to_dirent(&filename, filename, pool));

  if (opt_state->txn_id)
    {
      if (opt_state->start_revision.kind != svn_opt_revision_unspecified
          || opt_state->end_revision.kind != svn_opt_revision_unspecified)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("--revision (-r) and --transaction (-t) "
                                   "are mutually exclusive"));

      if (opt_state->use_pre_revprop_change_hook
          || opt_state->use_post_revprop_change_hook)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Calling hooks is incompatible with "
                                   "--transaction (-t)"));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_number)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Missing revision"));
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Only one revision allowed"));

  return set_revprop(prop_name, filename, opt_state, pool);
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_setuuid(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *args;
  svn_repos_t *repos;
  svn_fs_t *fs;
  const char *uuid = NULL;

  /* Expect zero or one more arguments: [UUID] */
  SVN_ERR(parse_args(&args, os, 0, 1, pool));
  if (args->nelts == 1)
    uuid = APR_ARRAY_IDX(args, 0, const char *);

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  return svn_fs_set_uuid(fs, uuid, pool);
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_setlog(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *args;
  const char *filename;

  /* Expect one more argument: FILE */
  SVN_ERR(parse_args(&args, os, 1, 1, pool));
  filename = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(target_arg_to_dirent(&filename, filename, pool));

  if (opt_state->start_revision.kind != svn_opt_revision_number)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Missing revision"));
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Only one revision allowed"));

  /* set_revprop() responds only to pre-/post-revprop-change opts. */
  if (!opt_state->bypass_hooks)
    {
      opt_state->use_pre_revprop_change_hook = TRUE;
      opt_state->use_post_revprop_change_hook = TRUE;
    }

  return set_revprop(SVN_PROP_REVISION_LOG, filename, opt_state, pool);
}


/* This implements 'svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_pack(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));

  /* Progress feedback goes to STDOUT, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stdout, pool);

  return svn_error_trace(
    svn_repos_fs_pack2(repos, !opt_state->quiet ? repos_notify_handler : NULL,
                       feedback_stream, check_cancel, NULL, pool));
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_verify(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_revnum_t youngest, lower, upper;
  svn_stream_t *feedback_stream = NULL;
  struct repos_verify_callback_baton verify_baton = { 0 };

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  if (opt_state->txn_id
      && (opt_state->start_revision.kind != svn_opt_revision_unspecified
          || opt_state->end_revision.kind != svn_opt_revision_unspecified))
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("--revision (-r) and --transaction (-t) "
                                 "are mutually exclusive"));
    }

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));

  /* Usage 2. */
  if (opt_state->txn_id)
    {
      svn_fs_txn_t *txn;
      svn_fs_root_t *root;

      SVN_ERR(svn_fs_open_txn(&txn, fs, opt_state->txn_id, pool));
      SVN_ERR(svn_fs_txn_root(&root, txn, pool));
      SVN_ERR(svn_fs_verify_root(root, pool));
      return SVN_NO_ERROR;
    }
  else
    /* Usage 1. */
    ;

  /* Find the revision numbers at which to start and end. */
  SVN_ERR(get_revnum(&lower, &opt_state->start_revision,
                     youngest, repos, pool));
  SVN_ERR(get_revnum(&upper, &opt_state->end_revision,
                     youngest, repos, pool));

  if (upper == SVN_INVALID_REVNUM)
    {
      upper = lower;
    }

  if (!opt_state->quiet)
    feedback_stream = recode_stream_create(stdout, pool);

  verify_baton.keep_going = opt_state->keep_going;
  verify_baton.error_summary =
    apr_array_make(pool, 0, sizeof(struct verification_error *));
  verify_baton.result_pool = pool;

  SVN_ERR(svn_repos_verify_fs3(repos, lower, upper,
                               opt_state->check_normalization,
                               opt_state->metadata_only,
                               !opt_state->quiet
                                 ? repos_notify_handler : NULL,
                               feedback_stream,
                               repos_verify_callback, &verify_baton,
                               check_cancel, NULL, pool));

  /* Show the --keep-going error summary. */
  if (!opt_state->quiet
      && opt_state->keep_going
      && verify_baton.error_summary->nelts > 0)
    {
      int rev_maxlength;
      svn_revnum_t end_revnum;
      apr_pool_t *iterpool;
      int i;

      svn_error_clear(
        svn_stream_puts(feedback_stream,
                          _("\n-----Summary of corrupt revisions-----\n")));

      /* The standard column width for the revision number is 6 characters.
         If the revision number can potentially be larger (i.e. if end_revnum
         is larger than 1000000), we increase the column width as needed. */
      rev_maxlength = 6;
      end_revnum = APR_ARRAY_IDX(verify_baton.error_summary,
                                 verify_baton.error_summary->nelts - 1,
                                 struct verification_error *)->rev;
      while (end_revnum >= 1000000)
        {
          rev_maxlength++;
          end_revnum = end_revnum / 10;
        }

      iterpool = svn_pool_create(pool);
      for (i = 0; i < verify_baton.error_summary->nelts; i++)
        {
          struct verification_error *verr;
          svn_error_t *err;
          const char *rev_str;

          svn_pool_clear(iterpool);

          verr = APR_ARRAY_IDX(verify_baton.error_summary, i,
                               struct verification_error *);

          if (verr->rev != SVN_INVALID_REVNUM)
            {
              rev_str = apr_psprintf(iterpool, "r%ld", verr->rev);
              rev_str = apr_psprintf(iterpool, "%*s", rev_maxlength, rev_str);
              for (err = svn_error_purge_tracing(verr->err);
                   err != SVN_NO_ERROR; err = err->child)
                {
                  char buf[512];
                  const char *message;

                  message = svn_err_best_message(err, buf, sizeof(buf));
                  svn_error_clear(svn_stream_printf(feedback_stream, iterpool,
                                                    "%s: E%06d: %s\n",
                                                    rev_str, err->apr_err,
                                                    message));
                }
            }
        }

       svn_pool_destroy(iterpool);
    }

  if (verify_baton.error_summary->nelts > 0)
    {
      return svn_error_createf(SVN_ERR_CL_REPOS_VERIFY_FAILED, NULL,
                               _("Failed to verify repository '%s'"),
                               svn_dirent_local_style(
                                 opt_state->repository_path, pool));
    }

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand_hotcopy(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_stream_t *feedback_stream = NULL;
  apr_array_header_t *targets;
  const char *new_repos_path;

  /* Expect one more argument: NEW_REPOS_PATH */
  SVN_ERR(parse_args(&targets, os, 1, 1, pool));
  new_repos_path = APR_ARRAY_IDX(targets, 0, const char *);
  SVN_ERR(target_arg_to_dirent(&new_repos_path, new_repos_path, pool));

  /* Progress feedback goes to STDOUT, unless they asked to suppress it. */
  if (! opt_state->quiet)
    feedback_stream = recode_stream_create(stdout, pool);

  return svn_repos_hotcopy3(opt_state->repository_path, new_repos_path,
                            opt_state->clean_logs, opt_state->incremental,
                            !opt_state->quiet ? repos_notify_handler : NULL,
                            feedback_stream, check_cancel, NULL, pool);
}

svn_error_t *
subcommand_info(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  int fs_format;
  const char *uuid;
  svn_revnum_t head_rev;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_cmdline_printf(pool, _("Path: %s\n"),
                             svn_dirent_local_style(svn_repos_path(repos, pool),
                                                    pool)));

  SVN_ERR(svn_fs_get_uuid(fs, &uuid, pool));
  SVN_ERR(svn_cmdline_printf(pool, _("UUID: %s\n"), uuid));

  SVN_ERR(svn_fs_youngest_rev(&head_rev, fs, pool));
  SVN_ERR(svn_cmdline_printf(pool, _("Revisions: %ld\n"), head_rev));
  {
    int repos_format, minor;
    svn_version_t *repos_version, *fs_version;
    SVN_ERR(svn_repos_info_format(&repos_format, &repos_version,
                                  repos, pool, pool));
    SVN_ERR(svn_cmdline_printf(pool, _("Repository Format: %d\n"),
                               repos_format));

    SVN_ERR(svn_fs_info_format(&fs_format, &fs_version,
                               fs, pool, pool));
    /* fs_format will be printed later. */

    SVN_ERR_ASSERT(repos_version->major == SVN_VER_MAJOR);
    SVN_ERR_ASSERT(fs_version->major == SVN_VER_MAJOR);
    SVN_ERR_ASSERT(repos_version->patch == 0);
    SVN_ERR_ASSERT(fs_version->patch == 0);

    minor = (repos_version->minor > fs_version->minor)
            ? repos_version->minor : fs_version->minor;
    SVN_ERR(svn_cmdline_printf(pool, _("Compatible With Version: %d.%d.0\n"),
                               SVN_VER_MAJOR, minor));
  }

  {
    apr_hash_t *capabilities_set;
    apr_array_header_t *capabilities;
    int i;

    SVN_ERR(svn_repos_capabilities(&capabilities_set, repos, pool, pool));
    capabilities = svn_sort__hash(capabilities_set,
                                  svn_sort_compare_items_lexically,
                                  pool);

    for (i = 0; i < capabilities->nelts; i++)
      {
        svn_sort__item_t *item = &APR_ARRAY_IDX(capabilities, i,
                                                svn_sort__item_t);
        const char *capability = item->key;
        SVN_ERR(svn_cmdline_printf(pool, _("Repository Capability: %s\n"),
                                   capability));
      }
  }

  {
    const svn_fs_info_placeholder_t *info;

    SVN_ERR(svn_fs_info(&info, fs, pool, pool));
    SVN_ERR(svn_cmdline_printf(pool, _("Filesystem Type: %s\n"),
                               info->fs_type));
    SVN_ERR(svn_cmdline_printf(pool, _("Filesystem Format: %d\n"),
                               fs_format));
    if (!strcmp(info->fs_type, SVN_FS_TYPE_FSFS))
      {
        const svn_fs_fsfs_info_t *fsfs_info = (const void *)info;

        if (fsfs_info->shard_size)
          SVN_ERR(svn_cmdline_printf(pool, _("FSFS Sharded: yes\n")));
        else
          SVN_ERR(svn_cmdline_printf(pool, _("FSFS Sharded: no\n")));

        if (fsfs_info->shard_size)
          SVN_ERR(svn_cmdline_printf(pool, _("FSFS Shard Size: %d\n"),
                                     fsfs_info->shard_size));

        /* Print packing statistics, if enabled on the FS. */
        if (fsfs_info->shard_size)
          {
            const int shard_size = fsfs_info->shard_size;
            const long shards_packed = fsfs_info->min_unpacked_rev / shard_size;
            const long shards_full = (head_rev + 1) / shard_size;
            SVN_ERR(svn_cmdline_printf(pool, _("FSFS Shards Packed: %ld/%ld\n"),
                                       shards_packed, shards_full));
          }

        if (fsfs_info->log_addressing)
          SVN_ERR(svn_cmdline_printf(pool, _("FSFS Logical Addressing: yes\n")));
        else
          SVN_ERR(svn_cmdline_printf(pool, _("FSFS Logical Addressing: no\n")));
      }
    else if (!strcmp(info->fs_type, SVN_FS_TYPE_FSX))
      {
        const svn_fs_fsx_info_t *fsx_info = (const void *)info;

        const int shard_size = fsx_info->shard_size;
        const long shards_packed = fsx_info->min_unpacked_rev / shard_size;
        long shards_full = (head_rev + 1) / shard_size;

        SVN_ERR(svn_cmdline_printf(pool, _("FSX Shard Size: %d\n"),
                                   shard_size));
        SVN_ERR(svn_cmdline_printf(pool, _("FSX Shards Packed: %ld/%ld\n"),
                                   shards_packed, shards_full));
      }
  }

  {
    apr_array_header_t *files;
    int i;

    SVN_ERR(svn_fs_info_config_files(&files, fs, pool, pool));
    for (i = 0; i < files->nelts; i++)
      SVN_ERR(svn_cmdline_printf(pool, _("Configuration File: %s\n"),
                                 svn_dirent_local_style(
                                   APR_ARRAY_IDX(files, i, const char *),
                                   pool)));
  }

  /* 'svn info' prints an extra newline here, to support multiple targets.
     We'll do the same. */
  SVN_ERR(svn_cmdline_printf(pool, "\n"));

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_lock(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_access_t *access;
  apr_array_header_t *args;
  const char *username;
  const char *lock_path;
  const char *comment_file_name;
  svn_stringbuf_t *file_contents;
  svn_lock_t *lock;
  const char *lock_token = NULL;

  /* Expect three more arguments: PATH USERNAME COMMENT-FILE */
  SVN_ERR(parse_args(&args, os, 3, 4, pool));
  lock_path = APR_ARRAY_IDX(args, 0, const char *);
  username = APR_ARRAY_IDX(args, 1, const char *);
  comment_file_name = APR_ARRAY_IDX(args, 2, const char *);

  /* Expect one more optional argument: TOKEN */
  if (args->nelts == 4)
    lock_token = APR_ARRAY_IDX(args, 3, const char *);

  SVN_ERR(target_arg_to_dirent(&comment_file_name, comment_file_name, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);

  /* Create an access context describing the user. */
  SVN_ERR(svn_fs_create_access(&access, username, pool));

  /* Attach the access context to the filesystem. */
  SVN_ERR(svn_fs_set_access(fs, access));

  SVN_ERR(svn_stringbuf_from_file2(&file_contents, comment_file_name, pool));

  SVN_ERR(target_arg_to_fspath(&lock_path, lock_path, pool, pool));

  if (opt_state->bypass_hooks)
    SVN_ERR(svn_fs_lock(&lock, fs, lock_path,
                        lock_token,
                        file_contents->data, /* comment */
                        0,                   /* is_dav_comment */
                        0,                   /* no expiration time. */
                        SVN_INVALID_REVNUM,
                        FALSE, pool));
  else
    SVN_ERR(svn_repos_fs_lock(&lock, repos, lock_path,
                              lock_token,
                              file_contents->data, /* comment */
                              0,                   /* is_dav_comment */
                              0,                   /* no expiration time. */
                              SVN_INVALID_REVNUM,
                              FALSE, pool));

  if (! opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, _("'%s' locked by user '%s'.\n"),
                               lock_path, username));

  return SVN_NO_ERROR;
}

static svn_error_t *
subcommand_lslocks(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *targets;
  svn_repos_t *repos;
  const char *fs_path;
  apr_hash_t *locks;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts > 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("Too many arguments given"));
  if (targets->nelts)
    fs_path = APR_ARRAY_IDX(targets, 0, const char *);
  else
    fs_path = "/";
  SVN_ERR(target_arg_to_fspath(&fs_path, fs_path, pool, pool));

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));

  /* Fetch all locks on or below the root directory. */
  SVN_ERR(svn_repos_fs_get_locks2(&locks, repos, fs_path, svn_depth_infinity,
                                  NULL, NULL, pool));

  for (hi = apr_hash_first(pool, locks); hi; hi = apr_hash_next(hi))
    {
      const char *cr_date, *exp_date = "";
      const char *path = apr_hash_this_key(hi);
      svn_lock_t *lock = apr_hash_this_val(hi);
      int comment_lines = 0;

      svn_pool_clear(iterpool);

      SVN_ERR(check_cancel(NULL));

      cr_date = svn_time_to_human_cstring(lock->creation_date, iterpool);

      if (lock->expiration_date)
        exp_date = svn_time_to_human_cstring(lock->expiration_date, iterpool);

      if (lock->comment)
        comment_lines = svn_cstring_count_newlines(lock->comment) + 1;

      SVN_ERR(svn_cmdline_printf(iterpool, _("Path: %s\n"), path));
      SVN_ERR(svn_cmdline_printf(iterpool, _("UUID Token: %s\n"), lock->token));
      SVN_ERR(svn_cmdline_printf(iterpool, _("Owner: %s\n"), lock->owner));
      SVN_ERR(svn_cmdline_printf(iterpool, _("Created: %s\n"), cr_date));
      SVN_ERR(svn_cmdline_printf(iterpool, _("Expires: %s\n"), exp_date));
      SVN_ERR(svn_cmdline_printf(iterpool,
                                 Q_("Comment (%i line):\n%s\n\n",
                                    "Comment (%i lines):\n%s\n\n",
                                    comment_lines),
                                 comment_lines,
                                 lock->comment ? lock->comment : ""));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}



static svn_error_t *
subcommand_rmlocks(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_error_t *err;
  apr_array_header_t *args;
  int i;
  const char *username;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);

  /* svn_fs_unlock() demands that some username be associated with the
     filesystem, so just use the UID of the person running 'svnadmin'.*/
  username = svn_user_get_name(pool);
  if (! username)
    username = "administrator";

  /* Create an access context describing the current user. */
  SVN_ERR(svn_fs_create_access(&access, username, pool));

  /* Attach the access context to the filesystem. */
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Parse out any options. */
  SVN_ERR(parse_args(&args, os, -1, -1, pool));

  /* Our usage requires at least one FS path. */
  if (args->nelts == 0)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("No paths to unlock provided"));

  /* All the rest of the arguments are paths from which to remove locks. */
  for (i = 0; i < args->nelts; i++)
    {
      const char *lock_path = APR_ARRAY_IDX(args, i, const char *);
      svn_lock_t *lock;

      SVN_ERR(target_arg_to_fspath(&lock_path, lock_path, subpool, subpool));

      /* Fetch the path's svn_lock_t. */
      err = svn_fs_get_lock(&lock, fs, lock_path, subpool);
      if (err)
        goto move_on;
      if (! lock)
        {
          if (! opt_state->quiet)
            SVN_ERR(svn_cmdline_printf(subpool,
                                       _("Path '%s' isn't locked.\n"),
                                       lock_path));
          continue;
        }
      lock = NULL; /* Don't access LOCK after this point. */

      /* Now forcibly destroy the lock. */
      err = svn_fs_unlock(fs, lock_path,
                          NULL, 1 /* force */, subpool);
      if (err)
        goto move_on;

      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(subpool,
                                   _("Removed lock on '%s'.\n"),
                                   lock_path));

    move_on:
      if (err)
        {
          /* Print the error, but move on to the next lock. */
          svn_handle_error2(err, stderr, FALSE /* non-fatal */, "svnadmin: ");
          svn_error_clear(err);
        }

      svn_pool_clear(subpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_unlock(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_access_t *access;
  apr_array_header_t *args;
  const char *username;
  const char *lock_path;
  const char *lock_token = NULL;

  /* Expect three more arguments: PATH USERNAME TOKEN */
  SVN_ERR(parse_args(&args, os, 3, 3, pool));
  lock_path = APR_ARRAY_IDX(args, 0, const char *);
  username = APR_ARRAY_IDX(args, 1, const char *);
  lock_token = APR_ARRAY_IDX(args, 2, const char *);

  /* Open the repos/FS, and associate an access context containing
     USERNAME. */
  SVN_ERR(open_repos(&repos, opt_state->repository_path, opt_state, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_create_access(&access, username, pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  SVN_ERR(target_arg_to_fspath(&lock_path, lock_path, pool, pool));
  if (opt_state->bypass_hooks)
    SVN_ERR(svn_fs_unlock(fs, lock_path, lock_token,
                          FALSE, pool));
  else
    SVN_ERR(svn_repos_fs_unlock(repos, lock_path, lock_token,
                                FALSE, pool));

  if (! opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, _("'%s' unlocked by user '%s'.\n"),
                               lock_path, username));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_upgrade(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_error_t *err;
  struct svnadmin_opt_state *opt_state = baton;
  svn_stream_t *feedback_stream = NULL;

  /* Expect no more arguments. */
  SVN_ERR(parse_args(NULL, os, 0, 0, pool));

  SVN_ERR(svn_stream_for_stdout(&feedback_stream, pool));

  /* Restore default signal handlers. */
  svn_cmdline__disable_cancellation_handler();

  err = svn_repos_upgrade2(opt_state->repository_path, TRUE,
                           repos_notify_handler, feedback_stream, pool);
  if (err)
    {
      if (APR_STATUS_IS_EAGAIN(err->apr_err))
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          if (! opt_state->wait)
            return svn_error_create(SVN_ERR_REPOS_LOCKED, NULL,
                                    _("Failed to get exclusive repository "
                                      "access; perhaps another process\n"
                                      "such as httpd, svnserve or svn "
                                      "has it open?"));
          SVN_ERR(svn_cmdline_printf(pool,
                                     _("Waiting on repository lock; perhaps"
                                       " another process has it open?\n")));
          SVN_ERR(svn_cmdline_fflush(stdout));
          SVN_ERR(svn_repos_upgrade2(opt_state->repository_path, FALSE,
                                     repos_notify_handler, feedback_stream,
                                     pool));
        }
      else if (err->apr_err == SVN_ERR_FS_UNSUPPORTED_UPGRADE)
        {
          return svn_error_quick_wrap(err,
                    _("Upgrade of this repository's underlying versioned "
                    "filesystem is not supported; consider "
                    "dumping and loading the data elsewhere"));
        }
      else if (err->apr_err == SVN_ERR_REPOS_UNSUPPORTED_UPGRADE)
        {
          return svn_error_quick_wrap(err,
                    _("Upgrade of this repository is not supported; consider "
                    "dumping and loading the data elsewhere"));
        }
    }
  SVN_ERR(err);

  SVN_ERR(svn_cmdline_printf(pool, _("\nUpgrade completed.\n")));
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_delrevprop(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *args;
  const char *prop_name;

  /* Expect one more argument: NAME */
  SVN_ERR(parse_args(&args, os, 1, 1, pool));
  prop_name = APR_ARRAY_IDX(args, 0, const char *);

  if (opt_state->txn_id)
    {
      if (opt_state->start_revision.kind != svn_opt_revision_unspecified
          || opt_state->end_revision.kind != svn_opt_revision_unspecified)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("--revision (-r) and --transaction (-t) "
                                   "are mutually exclusive"));

      if (opt_state->use_pre_revprop_change_hook
          || opt_state->use_post_revprop_change_hook)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Calling hooks is incompatible with "
                                   "--transaction (-t)"));
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_number)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Missing revision"));
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Only one revision allowed"));

  return set_revprop(prop_name, NULL, opt_state, pool);
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
  struct svnadmin_opt_state opt_state = { 0 };
  apr_getopt_t *os;
  int opt_id;
  apr_array_header_t *received_opts;
  int i;
  svn_boolean_t dash_F_arg = FALSE;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  /* Initialize the FS library. */
  SVN_ERR(svn_fs_initialize(pool));

  if (argc <= 1)
    {
      SVN_ERR(subcommand_help(NULL, NULL, pool));
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
          SVN_ERR(subcommand_help(NULL, NULL, pool));
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
      case 't':
        opt_state.txn_id = opt_arg;
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
      case 'F':
        SVN_ERR(svn_utf_cstring_to_utf8(&(opt_state.file), opt_arg, pool));
        dash_F_arg = TRUE;
        break;
      case svnadmin__version:
        opt_state.version = TRUE;
        break;
      case svnadmin__incremental:
        opt_state.incremental = TRUE;
        break;
      case svnadmin__deltas:
        opt_state.use_deltas = TRUE;
        break;
      case svnadmin__ignore_uuid:
        opt_state.uuid_action = svn_repos_load_uuid_ignore;
        break;
      case svnadmin__force_uuid:
        opt_state.uuid_action = svn_repos_load_uuid_force;
        break;
      case svnadmin__pre_1_4_compatible:
        opt_state.compatible_version = apr_pcalloc(pool, sizeof(svn_version_t));
        opt_state.compatible_version->major = 1;
        opt_state.compatible_version->minor = 3;
        break;
      case svnadmin__pre_1_5_compatible:
        opt_state.compatible_version = apr_pcalloc(pool, sizeof(svn_version_t));
        opt_state.compatible_version->major = 1;
        opt_state.compatible_version->minor = 4;
        break;
      case svnadmin__pre_1_6_compatible:
        opt_state.compatible_version = apr_pcalloc(pool, sizeof(svn_version_t));
        opt_state.compatible_version->major = 1;
        opt_state.compatible_version->minor = 5;
        break;
      case svnadmin__compatible_version:
        {
          svn_version_t latest = { SVN_VER_MAJOR, SVN_VER_MINOR,
                                   SVN_VER_PATCH, NULL };
          svn_version_t *compatible_version;

          /* Parse the version string which carries our target
             compatibility. */
          SVN_ERR(svn_version__parse_version_string(&compatible_version,
                                                        opt_arg, pool));

          /* We can't create repository with a version older than 1.0.0.  */
          if (! svn_version__at_least(compatible_version, 1, 0, 0))
            {
              return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                       _("Cannot create pre-1.0-compatible "
                                         "repositories"));
            }

          /* We can't create repository with a version newer than what
             the running version of Subversion supports. */
          if (! svn_version__at_least(&latest,
                                      compatible_version->major,
                                      compatible_version->minor,
                                      compatible_version->patch))
            {
              return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                       _("Cannot guarantee compatibility "
                                         "beyond the current running version "
                                         "(%s)"),
                                       SVN_VER_NUM);
            }

          opt_state.compatible_version = compatible_version;
        }
        break;
      case svnadmin__keep_going:
        opt_state.keep_going = TRUE;
        break;
      case svnadmin__check_normalization:
        opt_state.check_normalization = TRUE;
        break;
      case svnadmin__metadata_only:
        opt_state.metadata_only = TRUE;
        break;
      case svnadmin__fs_type:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.fs_type, opt_arg, pool));
        break;
      case svnadmin__parent_dir:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.parent_dir, opt_arg,
                                            pool));
        opt_state.parent_dir
          = svn_dirent_internal_style(opt_state.parent_dir, pool);
        break;
      case svnadmin__use_pre_commit_hook:
        opt_state.use_pre_commit_hook = TRUE;
        break;
      case svnadmin__use_post_commit_hook:
        opt_state.use_post_commit_hook = TRUE;
        break;
      case svnadmin__use_pre_revprop_change_hook:
        opt_state.use_pre_revprop_change_hook = TRUE;
        break;
      case svnadmin__use_post_revprop_change_hook:
        opt_state.use_post_revprop_change_hook = TRUE;
        break;
      case svnadmin__bdb_txn_nosync:
        opt_state.bdb_txn_nosync = TRUE;
        break;
      case svnadmin__bdb_log_keep:
        opt_state.bdb_log_keep = TRUE;
        break;
      case svnadmin__bypass_hooks:
        opt_state.bypass_hooks = TRUE;
        break;
      case svnadmin__bypass_prop_validation:
        opt_state.bypass_prop_validation = TRUE;
        break;
      case svnadmin__ignore_dates:
        opt_state.ignore_dates = TRUE;
        break;
      case svnadmin__clean_logs:
        opt_state.clean_logs = TRUE;
        break;
      case svnadmin__config_dir:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.config_dir =
            apr_pstrdup(pool, svn_dirent_canonicalize(utf8_opt_arg, pool));
        break;
      case svnadmin__wait:
        opt_state.wait = TRUE;
        break;
      case svnadmin__no_flush_to_disk:
        opt_state.no_flush_to_disk = TRUE;
        break;
      case svnadmin__normalize_props:
        opt_state.normalize_props = TRUE;
        break;
      case svnadmin__exclude:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));

        if (! opt_state.exclude)
          opt_state.exclude = apr_array_make(pool, 1, sizeof(const char *));
        APR_ARRAY_PUSH(opt_state.exclude, const char *) = utf8_opt_arg;
        break;
      case svnadmin__include:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));

        if (! opt_state.include)
          opt_state.include = apr_array_make(pool, 1, sizeof(const char *));
        APR_ARRAY_PUSH(opt_state.include, const char *) = utf8_opt_arg;
        break;
      case svnadmin__glob:
        opt_state.glob = TRUE;
        break;
      default:
        {
          SVN_ERR(subcommand_help(NULL, NULL, pool));
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
                { "--version", subcommand_help, {0}, "",
                  {svnadmin__version,  /* must accept its own option */
                   'q',  /* --quiet */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                        _("subcommand argument required\n")));
              SVN_ERR(subcommand_help(NULL, NULL, pool));
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
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Every subcommand except `help' and `freeze' with '-F' require a
     second argument -- the repository path.  Parse it out here and
     store it in opt_state. */
  if (!(subcommand->cmd_func == subcommand_help
        || (subcommand->cmd_func == subcommand_freeze && dash_F_arg)))
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
            SVN_ERR(subcommand_help(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf(stderr, pool
                            , _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnadmin help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  check_cancel = svn_cmdline__setup_cancellation_handler();

  /* Configure FSFS caches for maximum efficiency with svnadmin.
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
                                     _("Try 'svnadmin help' for more info"));
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
  if (svn_cmdline_init("svnadmin", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnadmin: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
