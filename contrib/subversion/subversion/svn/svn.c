/*
 * svn.c:  Subversion command line client main file.
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

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_auth.h"
#include "svn_hash.h"
#include "svn_version.h"
#include "cl.h"

#include "private/svn_opt_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_subr_private.h"
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
  opt_autoprops,
  opt_changelist,
  opt_config_dir,
  opt_config_options,
  /* diff options */
  opt_diff_cmd,
  opt_internal_diff,
  opt_no_diff_added,
  opt_no_diff_deleted,
  opt_show_copies_as_adds,
  opt_notice_ancestry,
  opt_summarize,
  opt_use_git_diff_format,
  opt_ignore_properties,
  opt_properties_only,
  opt_patch_compatible,
  /* end of diff options */
  opt_dry_run,
  opt_editor_cmd,
  opt_encoding,
  opt_force_log,
  opt_force,
  opt_keep_changelists,
  opt_ignore_ancestry,
  opt_ignore_externals,
  opt_incremental,
  opt_merge_cmd,
  opt_native_eol,
  opt_new_cmd,
  opt_no_auth_cache,
  opt_no_autoprops,
  opt_no_ignore,
  opt_no_unlock,
  opt_non_interactive,
  opt_force_interactive,
  opt_old_cmd,
  opt_record_only,
  opt_relocate,
  opt_remove,
  opt_revprop,
  opt_stop_on_copy,
  opt_strict,                   /* ### DEPRECATED */
  opt_targets,
  opt_depth,
  opt_set_depth,
  opt_version,
  opt_xml,
  opt_keep_local,
  opt_with_revprop,
  opt_with_all_revprops,
  opt_with_no_revprops,
  opt_parents,
  opt_accept,
  opt_show_revs,
  opt_reintegrate,
  opt_trust_server_cert,
  opt_trust_server_cert_failures,
  opt_strip,
  opt_ignore_keywords,
  opt_reverse_diff,
  opt_ignore_whitespace,
  opt_diff,
  opt_allow_mixed_revisions,
  opt_include_externals,
  opt_show_inherited_props,
  opt_search,
  opt_search_and,
  opt_mergeinfo_log,
  opt_remove_unversioned,
  opt_remove_ignored,
  opt_no_newline,
  opt_show_passwords,
  opt_pin_externals,
  opt_show_item,
  opt_adds_as_modification,
  opt_vacuum_pristines,
  opt_delete,
  opt_keep_shelved,
  opt_list
} svn_cl__longopt_t;


/* Option codes and descriptions for the command line client.
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_cl__options[] =
{
  {"force",         opt_force, 0, N_("force operation to run")},
  {"force-log",     opt_force_log, 0,
                    N_("force validity of log message source")},
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"message",       'm', 1, N_("specify log message ARG")},
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
  {"file",          'F', 1, N_("read log message from file ARG")},
  {"incremental",   opt_incremental, 0,
                    N_("give output suitable for concatenation")},
  {"encoding",      opt_encoding, 1,
                    N_("treat value as being in charset encoding ARG")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"show-updates",  'u', 0, N_("display update information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1,
                    N_("specify a password ARG (caution: on many operating\n"
                       "                             "
                       "systems, other users will be able to see this)")},
  {"password-from-stdin",
                    opt_auth_password_from_stdin, 0,
                    N_("read password from stdin")},
  {"extensions",    'x', 1,
                    N_("Specify differencing options for external diff or\n"
                       "                             "
                       "internal diff or blame. Default: '-u'. Options are\n"
                       "                             "
                       "separated by spaces. Internal diff and blame take:\n"
                       "                             "
                       "  -u, --unified: Show 3 lines of unified context\n"
                       "                             "
                       "  -b, --ignore-space-change: Ignore changes in\n"
                       "                             "
                       "    amount of white space\n"
                       "                             "
                       "  -w, --ignore-all-space: Ignore all white space\n"
                       "                             "
                       "  --ignore-eol-style: Ignore changes in EOL style\n"
                       "                             "
                       "  -U ARG, --context ARG: Show ARG lines of context\n"
                       "                             "
                       "  -p, --show-c-function: Show C function name")},
  {"targets",       opt_targets, 1,
                    N_("pass contents of file ARG as additional args")},
  {"depth",         opt_depth, 1,
                    N_("limit operation by depth ARG ('empty', 'files',\n"
                       "                             "
                       "'immediates', or 'infinity')")},
  {"set-depth",     opt_set_depth, 1,
                    N_("set new working copy depth to ARG ('exclude',\n"
                       "                             "
                       "'empty', 'files', 'immediates', or 'infinity')")},
  {"xml",           opt_xml, 0, N_("output in XML")},
  {"strict",        opt_strict, 0, N_("DEPRECATED")},
  {"stop-on-copy",  opt_stop_on_copy, 0,
                    N_("do not cross copies while traversing history")},
  {"no-ignore",     opt_no_ignore, 0,
                    N_("disregard default and svn:ignore and\n"
                       "                             "
                       "svn:global-ignores property ignores")},
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
                    N_("do no interactive prompting (default is to prompt\n"
                       "                             "
                       "only if standard input is a terminal device)")},
  {"force-interactive", opt_force_interactive, 0,
                    N_("do interactive prompting even if standard input\n"
                       "                             "
                       "is not a terminal device")},
  {"dry-run",       opt_dry_run, 0,
                    N_("try operation but make no changes")},
  {"ignore-ancestry", opt_ignore_ancestry, 0,
                    N_("disable merge tracking; diff nodes as if related")},
  {"ignore-externals", opt_ignore_externals, 0,
                    N_("ignore externals definitions")},
  {"diff3-cmd",     opt_merge_cmd, 1, N_("use ARG as merge command")},
  {"editor-cmd",    opt_editor_cmd, 1, N_("use ARG as external editor")},
  {"record-only",   opt_record_only, 0,
                    N_("merge only mergeinfo differences")},
  {"old",           opt_old_cmd, 1, N_("use ARG as the older target")},
  {"new",           opt_new_cmd, 1, N_("use ARG as the newer target")},
  {"revprop",       opt_revprop, 0,
                    N_("operate on a revision property (use with -r)")},
  {"relocate",      opt_relocate, 0, N_("relocate via URL-rewriting")},
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
  {"auto-props",    opt_autoprops, 0, N_("enable automatic properties")},
  {"no-auto-props", opt_no_autoprops, 0, N_("disable automatic properties")},
  {"native-eol",    opt_native_eol, 1,
                    N_("use a different EOL marker than the standard\n"
                       "                             "
                       "system marker for files with the svn:eol-style\n"
                       "                             "
                       "property set to 'native'.\n"
                       "                             "
                       "ARG may be one of 'LF', 'CR', 'CRLF'")},
  {"limit",         'l', 1, N_("maximum number of log entries")},
  {"no-unlock",     opt_no_unlock, 0, N_("don't unlock the targets")},
  {"remove",         opt_remove, 0, N_("remove changelist association")},
  {"changelist",    opt_changelist, 1,
                    N_("operate only on members of changelist ARG")},
  {"keep-changelists", opt_keep_changelists, 0,
                    N_("don't delete changelists after commit")},
  {"keep-local",    opt_keep_local, 0, N_("keep path in working copy")},
  {"with-all-revprops",  opt_with_all_revprops, 0,
                    N_("retrieve all revision properties")},
  {"with-no-revprops",  opt_with_no_revprops, 0,
                    N_("retrieve no revision properties")},
  {"with-revprop",  opt_with_revprop, 1,
                    N_("set revision property ARG in new revision\n"
                       "                             "
                       "using the name[=value] format")},
  {"parents",       opt_parents, 0, N_("make intermediate directories")},
  {"use-merge-history", 'g', 0,
                    N_("use/display additional information from merge\n"
                       "                             "
                       "history")},
  {"accept",        opt_accept, 1,
                    N_("specify automatic conflict resolution action\n"
                       "                             "
                       "('postpone', 'working', 'base', 'mine-conflict',\n"
                       "                             "
                       "'theirs-conflict', 'mine-full', 'theirs-full',\n"
                       "                             "
                       "'edit', 'launch', 'recommended') (shorthand:\n"
                       "                             "
                       "'p', 'mc', 'tc', 'mf', 'tf', 'e', 'l', 'r')"
                       )},
  {"show-revs",     opt_show_revs, 1,
                    N_("specify which collection of revisions to display\n"
                       "                             "
                       "('merged', 'eligible')")},
  {"reintegrate",   opt_reintegrate, 0,
                    N_("deprecated")},
  {"strip",         opt_strip, 1,
                    N_("number of leading path components to strip from\n"
                       "                             "
                       "paths parsed from the patch file. --strip 0\n"
                       "                             "
                       "is the default and leaves paths unmodified.\n"
                       "                             "
                       "--strip 1 would change the path\n"
                       "                             "
                       "'doc/fudge/crunchy.html' to 'fudge/crunchy.html'.\n"
                       "                             "
                       "--strip 2 would leave just 'crunchy.html'\n"
                       "                             "
                       "The expected component separator is '/' on all\n"
                       "                             "
                       "platforms. A leading '/' counts as one component.")},
  {"ignore-keywords", opt_ignore_keywords, 0,
                    N_("don't expand keywords")},
  {"reverse-diff", opt_reverse_diff, 0,
                    N_("apply the unidiff in reverse")},
  {"ignore-whitespace", opt_ignore_whitespace, 0,
                       N_("ignore whitespace during pattern matching")},
  {"diff", opt_diff, 0, N_("produce diff output")}, /* maps to show_diff */
  /* diff options */
  {"diff-cmd",      opt_diff_cmd, 1, N_("use ARG as diff command")},
  {"internal-diff", opt_internal_diff, 0,
                       N_("override diff-cmd specified in config file")},
  {"no-diff-added", opt_no_diff_added, 0,
                    N_("do not print differences for added files")},
  {"no-diff-deleted", opt_no_diff_deleted, 0,
                    N_("do not print differences for deleted files")},
  {"show-copies-as-adds", opt_show_copies_as_adds, 0,
                    N_("don't diff copied or moved files with their source")},
  {"notice-ancestry", opt_notice_ancestry, 0,
                    N_("diff unrelated nodes as delete and add")},
  {"summarize",     opt_summarize, 0, N_("show a summary of the results")},
  {"git", opt_use_git_diff_format, 0,
                       N_("use git's extended diff format")},
  {"ignore-properties", opt_ignore_properties, 0,
                    N_("ignore properties during the operation")},
  {"properties-only", opt_properties_only, 0,
                       N_("show only properties during the operation")},
  {"patch-compatible", opt_patch_compatible, 0,
                       N_("generate diff suitable for generic third-party\n"
                       "                             "
                       "patch tools; currently the same as\n"
                       "                             "
                       "--show-copies-as-adds --ignore-properties"
                       )},
  /* end of diff options */
  {"allow-mixed-revisions", opt_allow_mixed_revisions, 0,
                       N_("Allow operation on mixed-revision working copy.\n"
                       "                             "
                       "Use of this option is not recommended!\n"
                       "                             "
                       "Please run 'svn update' instead.")},
  {"include-externals", opt_include_externals, 0,
                       N_("also operate on externals defined by\n"
                       "                             "
                       "svn:externals properties")},
  {"show-inherited-props", opt_show_inherited_props, 0,
                       N_("retrieve properties set on parents of the target")},
  {"search", opt_search, 1,
                       N_("use ARG as search pattern (glob syntax, case-\n"
                       "                             "
                       "and accent-insensitive, may require quotation marks\n"
                       "                             "
                       "to prevent shell expansion)")},
  {"search-and", opt_search_and, 1,
                       N_("combine ARG with the previous search pattern")},
  {"log", opt_mergeinfo_log, 0,
                       N_("show revision log message, author and date")},
  {"remove-unversioned", opt_remove_unversioned, 0,
                       N_("remove unversioned items")},
  {"remove-ignored", opt_remove_ignored, 0, N_("remove ignored items")},
  {"no-newline", opt_no_newline, 0, N_("do not output the trailing newline")},
  {"show-passwords", opt_show_passwords, 0, N_("show cached passwords")},
  {"pin-externals", opt_pin_externals, 0,
                       N_("pin externals with no explicit revision to their\n"
                          "                             "
                          "current revision (recommended when tagging)")},
  {"show-item", opt_show_item, 1,
                       N_("print only the item identified by ARG:\n"
                          "                             "
                          "   'kind'       node kind of TARGET\n"
                          "                             "
                          "   'url'        URL of TARGET in the repository\n"
                          "                             "
                          "   'relative-url'\n"
                          "                             "
                          "                repository-relative URL of TARGET\n"
                          "                             "
                          "   'repos-root-url'\n"
                          "                             "
                          "                root URL of repository\n"
                          "                             "
                          "   'repos-uuid' UUID of repository\n"
                          "                             "
                          "   'revision'   specified or implied revision\n"
                          "                             "
                          "   'last-changed-revision'\n"
                          "                             "
                          "                last change of TARGET at or before\n"
                          "                             "
                          "                'revision'\n"
                          "                             "
                          "   'last-changed-date'\n"
                          "                             "
                          "                date of 'last-changed-revision'\n"
                          "                             "
                          "   'last-changed-author'\n"
                          "                             "
                          "                author of 'last-changed-revision'\n"
                          "                             "
                          "   'wc-root'    root of TARGET's working copy")},

  {"adds-as-modification", opt_adds_as_modification, 0,
                       N_("Local additions are merged with incoming additions\n"
                       "                             "
                       "instead of causing a tree conflict. Use of this\n"
                       "                             "
                       "option is not recommended! Use 'svn resolve' to\n"
                       "                             "
                       "resolve tree conflicts instead.")},

  {"vacuum-pristines", opt_vacuum_pristines, 0,
                       N_("remove unreferenced pristines from .svn directory")},

  {"list", opt_list, 0, N_("list shelved patches")},
  {"keep-shelved", opt_keep_shelved, 0, N_("do not delete the shelved patch")},
  {"delete", opt_delete, 0, N_("delete the shelved patch")},

  /* Long-opt Aliases
   *
   * These have NULL desriptions, but an option code that matches some
   * other option (whose description should probably mention its aliases).
  */

  {"cl",            opt_changelist, 1, NULL},

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
  opt_force_interactive, opt_trust_server_cert,
  opt_trust_server_cert_failures,
  opt_config_dir, opt_config_options, 0
};

/* Options for giving a log message.  (Some of these also have other uses.)
 */
#define SVN_CL__LOG_MSG_OPTIONS 'm', 'F', \
                                opt_force_log, \
                                opt_editor_cmd, \
                                opt_encoding, \
                                opt_with_revprop

const svn_opt_subcommand_desc2_t svn_cl__cmd_table[] =
{
  { "add", svn_cl__add, {0}, N_
    ("Put files and directories under version control, scheduling\n"
     "them for addition to repository.  They will be added in next commit.\n"
     "usage: add PATH...\n"),
    {opt_targets, 'N', opt_depth, 'q', opt_force, opt_no_ignore, opt_autoprops,
     opt_no_autoprops, opt_parents },
     {{opt_parents, N_("add intermediate parents")}} },

  { "auth", svn_cl__auth, {0}, N_
   ("Manage cached authentication credentials.\n"
    "usage: 1. svn auth [PATTERN ...]\n"
    "usage: 2. svn auth --remove PATTERN [PATTERN ...]\n"
    "\n"
    "  With no arguments, list all cached authentication credentials.\n"
    "  Authentication credentials include usernames, passwords,\n"
    "  SSL certificates, and SSL client-certificate passphrases.\n"
    "  If PATTERN is specified, only list credentials with attributes matching one\n"
    "  or more patterns. With the --remove option, remove cached authentication\n"
    "  credentials matching one or more patterns.\n"
    "\n"
    "  If more than one pattern is specified credentials are considered only if they\n"
    "  match all specified patterns. Patterns are matched case-sensitively and may\n"
    "  contain glob wildcards:\n"
    "    ?      matches any single character\n"
    "    *      matches a sequence of arbitrary characters\n"
    "    [abc]  matches any of the characters listed inside the brackets\n"
    "  Note that wildcards will usually need to be quoted or escaped on the\n"
    "  command line because many command shells will interfere by trying to\n"
    "  expand them.\n"),
    { opt_remove, opt_show_passwords },
    { {opt_remove, N_("remove matching authentication credentials")} }

    },

  { "blame", svn_cl__blame, {"praise", "annotate", "ann"}, N_
    ("Show when each line of a file was last (or\n"
     "next) changed.\n"
     "usage: blame [-rM:N] TARGET[@REV]...\n"
     "\n"
     "  Annotate each line of a file with the revision number and author of the\n"
     "  last change (or optionally the next change) to that line.\n"
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
    {'r', 'v', 'g', opt_incremental, opt_xml, 'x', opt_force} },

  { "cat", svn_cl__cat, {0}, N_
    ("Output the content of specified files or URLs.\n"
     "usage: cat TARGET[@REV]...\n"
     "\n"
     "  If specified, REV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', opt_ignore_keywords} },

  { "changelist", svn_cl__changelist, {"cl"}, N_
    ("Associate (or dissociate) changelist CLNAME with the named files.\n"
     "usage: 1. changelist CLNAME PATH...\n"
     "       2. changelist --remove PATH...\n"),
    { 'q', 'R', opt_depth, opt_remove, opt_targets, opt_changelist} },

  { "checkout", svn_cl__checkout, {"co"}, N_
    ("Check out a working copy from a repository.\n"
     "usage: checkout URL[@REV]... [PATH]\n"
     "\n"
     "  If specified, REV determines in which revision the URL is first\n"
     "  looked up.\n"
     "\n"
     "  If PATH is omitted, the basename of the URL will be used as\n"
     "  the destination. If multiple URLs are given each will be checked\n"
     "  out into a sub-directory of PATH, with the name of the sub-directory\n"
     "  being the basename of the URL.\n"
     "\n"
     "  If --force is used, unversioned obstructing paths in the working\n"
     "  copy destination do not automatically cause the check out to fail.\n"
     "  If the obstructing path is the same type (file or directory) as the\n"
     "  corresponding path in the repository it becomes versioned but its\n"
     "  contents are left 'as-is' in the working copy.  This means that an\n"
     "  obstructing directory's unversioned children may also obstruct and\n"
     "  become versioned.  For files, any content differences between the\n"
     "  obstruction and the repository are treated like a local modification\n"
     "  to the working copy.  All properties from the repository are applied\n"
     "  to the obstructing path.\n"
     "\n"
     "  See also 'svn help update' for a list of possible characters\n"
     "  reporting the action taken.\n"),
    {'r', 'q', 'N', opt_depth, opt_force, opt_ignore_externals} },

  { "cleanup", svn_cl__cleanup, {0}, N_
    ("Either recover from an interrupted operation that left the working copy locked,\n"
     "or remove unwanted files.\n"
     "usage: 1. cleanup [WCPATH...]\n"
     "       2. cleanup --remove-unversioned [WCPATH...]\n"
     "          cleanup --remove-ignored [WCPATH...]\n"
     "       3. cleanup --vacuum-pristines [WCPATH...]\n"
     "\n"
     "  1. When none of the options --remove-unversioned, --remove-ignored, and\n"
     "    --vacuum-pristines is specified, remove all write locks (shown as 'L' by\n"
     "    the 'svn status' command) from the working copy.  Usually, this is only\n"
     "    necessary if a Subversion client has crashed while using the working copy,\n"
     "    leaving it in an unusable state.\n"
     "\n"
     "    WARNING: There is no mechanism that will protect write locks still\n"
     "             being used by other Subversion clients. Running this command\n"
     "             without any options while another client is using the working\n"
     "             copy can corrupt the working copy beyond repair!\n"
     "\n"
     "  2. If the --remove-unversioned option or the --remove-ignored option\n"
     "    is given, remove any unversioned or ignored items within WCPATH.\n"
     "    Note that the 'svn status' command shows unversioned items as '?',\n"
     "    and ignored items as 'I' if the --no-ignore option is given to it.\n"
     "\n"
     "  3. If the --vacuum-pristines option is given, remove pristine copies of\n"
     "    files which are stored inside the .svn directory and which are no longer\n"
     "    referenced by any file in the working copy.\n"),
    { opt_remove_unversioned, opt_remove_ignored, opt_vacuum_pristines,
      opt_include_externals, 'q', opt_merge_cmd }, 
    { { opt_merge_cmd, N_("deprecated and ignored") } } },
      
  { "commit", svn_cl__commit, {"ci"},
    N_("Send changes from your working copy to the repository.\n"
       "usage: commit [PATH...]\n"
       "\n"
       "  A log message must be provided, but it can be empty.  If it is not\n"
       "  given by a --message or --file option, an editor will be started.\n"
       "\n"
       "  If any targets are (or contain) locked items, those will be\n"
       "  unlocked after a successful commit, unless --no-unlock is given.\n"
       "\n"
       "  If --include-externals is given, also commit file and directory\n"
       "  externals reached by recursion. Do not commit externals with a\n"
       "  fixed revision.\n"),
    {'q', 'N', opt_depth, opt_targets, opt_no_unlock, SVN_CL__LOG_MSG_OPTIONS,
     opt_changelist, opt_keep_changelists, opt_include_externals} },

  { "copy", svn_cl__copy, {"cp"}, N_
    ("Copy files and directories in a working copy or repository.\n"
     "usage: copy SRC[@REV]... DST\n"
     "\n"
     "  SRC and DST can each be either a working copy (WC) path or URL:\n"
     "    WC  -> WC:   copy and schedule for addition (with history)\n"
     "    WC  -> URL:  immediately commit a copy of WC to URL\n"
     "    URL -> WC:   check out URL into WC, schedule for addition\n"
     "    URL -> URL:  complete server-side copy;  used to branch and tag\n"
     "  All the SRCs must be of the same type. If DST is an existing directory,\n"
     "  the sources will be added as children of DST. When copying multiple\n"
     "  sources, DST must be an existing directory.\n"
     "\n"
     "  WARNING: For compatibility with previous versions of Subversion,\n"
     "  copies performed using two working copy paths (WC -> WC) will not\n"
     "  contact the repository.  As such, they may not, by default, be able\n"
     "  to propagate merge tracking information from the source of the copy\n"
     "  to the destination.\n"),
    {'r', 'q', opt_ignore_externals, opt_parents, SVN_CL__LOG_MSG_OPTIONS,
     opt_pin_externals} },

  { "delete", svn_cl__delete, {"del", "remove", "rm"}, N_
    ("Remove files and directories from version control.\n"
     "usage: 1. delete PATH...\n"
     "       2. delete URL...\n"
     "\n"
     "  1. Each item specified by a PATH is scheduled for deletion upon\n"
     "    the next commit.  Files, and directories that have not been\n"
     "    committed, are immediately removed from the working copy\n"
     "    unless the --keep-local option is given.\n"
     "    PATHs that are, or contain, unversioned or modified items will\n"
     "    not be removed unless the --force or --keep-local option is given.\n"
     "\n"
     "  2. Each item specified by a URL is deleted from the repository\n"
     "    via an immediate commit.\n"),
    {opt_force, 'q', opt_targets, SVN_CL__LOG_MSG_OPTIONS, opt_keep_local} },

  { "diff", svn_cl__diff, {"di"}, N_
    ("Display local changes or differences between two revisions or paths.\n"
     "usage: 1. diff\n"
     "       2. diff [-c M | -r N[:M]] [TARGET[@REV]...]\n"
     "       3. diff [-r N[:M]] --old=OLD-TGT[@OLDREV] [--new=NEW-TGT[@NEWREV]] \\\n"
     "               [PATH...]\n"
     "       4. diff OLD-URL[@OLDREV] NEW-URL[@NEWREV]\n"
     "       5. diff OLD-URL[@OLDREV] NEW-PATH[@NEWREV]\n"
     "       6. diff OLD-PATH[@OLDREV] NEW-URL[@NEWREV]\n"
     "\n"
     "  1. Use just 'svn diff' to display local modifications in a working copy.\n"
     "\n"
     "  2. Display the changes made to TARGETs as they are seen in REV between\n"
     "     two revisions.  TARGETs may be all working copy paths or all URLs.\n"
     "     If TARGETs are working copy paths, N defaults to BASE and M to the\n"
     "     working copy; if URLs, N must be specified and M defaults to HEAD.\n"
     "     The '-c M' option is equivalent to '-r N:M' where N = M-1.\n"
     "     Using '-c -M' does the reverse: '-r M:N' where N = M-1.\n"
     "\n"
     "  3. Display the differences between OLD-TGT as it was seen in OLDREV and\n"
     "     NEW-TGT as it was seen in NEWREV.  PATHs, if given, are relative to\n"
     "     OLD-TGT and NEW-TGT and restrict the output to differences for those\n"
     "     paths.  OLD-TGT and NEW-TGT may be working copy paths or URL[@REV].\n"
     "     NEW-TGT defaults to OLD-TGT if not specified.  -r N makes OLDREV default\n"
     "     to N, -r N:M makes OLDREV default to N and NEWREV default to M.\n"
     "     If OLDREV or NEWREV are not specified, they default to WORKING for\n"
     "     working copy targets and to HEAD for URL targets.\n"
     "\n"
     "     Either or both OLD-TGT and NEW-TGT may also be paths to unversioned\n"
     "     targets. Revisions cannot be specified for unversioned targets.\n"
     "     Both targets must be of the same node kind (file or directory).\n"
     "     Diffing unversioned targets against URL targets is not supported.\n"
     "\n"
     "  4. Shorthand for 'svn diff --old=OLD-URL[@OLDREV] --new=NEW-URL[@NEWREV]'\n"
     "  5. Shorthand for 'svn diff --old=OLD-URL[@OLDREV] --new=NEW-PATH[@NEWREV]'\n"
     "  6. Shorthand for 'svn diff --old=OLD-PATH[@OLDREV] --new=NEW-URL[@NEWREV]'\n"),
    {'r', 'c', opt_old_cmd, opt_new_cmd, 'N', opt_depth, opt_diff_cmd,
     opt_internal_diff, 'x', opt_no_diff_added, opt_no_diff_deleted,
     opt_ignore_properties, opt_properties_only,
     opt_show_copies_as_adds, opt_notice_ancestry, opt_summarize, opt_changelist,
     opt_force, opt_xml, opt_use_git_diff_format, opt_patch_compatible} },
  { "export", svn_cl__export, {0}, N_
    ("Create an unversioned copy of a tree.\n"
     "usage: 1. export [-r REV] URL[@PEGREV] [PATH]\n"
     "       2. export [-r REV] PATH1[@PEGREV] [PATH2]\n"
     "\n"
     "  1. Exports a clean directory tree from the repository specified by\n"
     "     URL, at revision REV if it is given, otherwise at HEAD, into\n"
     "     PATH. If PATH is omitted, the last component of the URL is used\n"
     "     for the local directory name.\n"
     "\n"
     "  2. Exports a clean directory tree from the working copy specified by\n"
     "     PATH1, at revision REV if it is given, otherwise at WORKING, into\n"
     "     PATH2.  If PATH2 is omitted, the last component of the PATH1 is used\n"
     "     for the local directory name. If REV is not specified, all local\n"
     "     changes will be preserved.  Files not under version control will\n"
     "     not be copied.\n"
     "\n"
     "  If specified, PEGREV determines in which revision the target is first\n"
     "  looked up.\n"),
    {'r', 'q', 'N', opt_depth, opt_force, opt_native_eol, opt_ignore_externals,
     opt_ignore_keywords} },

  { "help", svn_cl__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },
  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "import", svn_cl__import, {0}, N_
    ("Commit an unversioned file or tree into the repository.\n"
     "usage: import [PATH] URL\n"
     "\n"
     "  Recursively commit a copy of PATH to URL.\n"
     "  If PATH is omitted '.' is assumed.\n"
     "  Parent directories are created as necessary in the repository.\n"
     "  If PATH is a directory, the contents of the directory are added\n"
     "  directly under URL.\n"
     "  Unversionable items such as device files and pipes are ignored\n"
     "  if --force is specified.\n"),
    {'q', 'N', opt_depth, opt_autoprops, opt_force, opt_no_autoprops,
     SVN_CL__LOG_MSG_OPTIONS, opt_no_ignore} },

  { "info", svn_cl__info, {0}, N_
    ("Display information about a local or remote item.\n"
     "usage: info [TARGET[@REV]...]\n"
     "\n"
     "  Print information about each TARGET (default: '.').\n"
     "  TARGET may be either a working-copy path or a URL.  If specified, REV\n"
     "  determines in which revision the target is first looked up; the default\n"
     "  is HEAD for a URL or BASE for a WC path.\n"
     "\n"
     "  With --show-item, print only the value of one item of information\n"
     "  about TARGET.\n"),
    {'r', 'R', opt_depth, opt_targets, opt_incremental, opt_xml,
     opt_changelist, opt_include_externals, opt_show_item, opt_no_newline}
  },

  { "list", svn_cl__list, {"ls"},
#if defined(WIN32)
    N_
    ("List directory entries in the repository.\n"
     "usage: list [TARGET[@REV]...]\n"
     "\n"
     "  List each TARGET file and the contents of each TARGET directory as\n"
     "  they exist in the repository.  If TARGET is a working copy path, the\n"
     "  corresponding repository URL will be used. If specified, REV determines\n"
     "  in which revision the target is first looked up.\n"
     "\n"
     "  The default TARGET is '.', meaning the repository URL of the current\n"
     "  working directory.\n"
     "\n"
     "  Multiple --search patterns may be specified and the output will be\n"
     "  reduced to those paths whose last segment - i.e. the file or directory\n"
     "  name - contains a sub-string matching at least one of these patterns\n"
     "  (Windows only).\n"
     "\n"
     "  With --verbose, the following fields will be shown for each item:\n"
     "\n"
     "    Revision number of the last commit\n"
     "    Author of the last commit\n"
     "    If locked, the letter 'O'.  (Use 'svn info URL' to see details)\n"
     "    Size (in bytes)\n"
     "    Date and time of the last commit\n"),
#else
    N_
    ("List directory entries in the repository.\n"
     "usage: list [TARGET[@REV]...]\n"
     "\n"
     "  List each TARGET file and the contents of each TARGET directory as\n"
     "  they exist in the repository.  If TARGET is a working copy path, the\n"
     "  corresponding repository URL will be used. If specified, REV determines\n"
     "  in which revision the target is first looked up.\n"
     "\n"
     "  The default TARGET is '.', meaning the repository URL of the current\n"
     "  working directory.\n"
     "\n"
     "  Multiple --search patterns may be specified and the output will be\n"
     "  reduced to those paths whose last segment - i.e. the file or directory\n"
     "  name - matches at least one of these patterns.\n"
     "\n"
     "  With --verbose, the following fields will be shown for each item:\n"
     "\n"
     "    Revision number of the last commit\n"
     "    Author of the last commit\n"
     "    If locked, the letter 'O'.  (Use 'svn info URL' to see details)\n"
     "    Size (in bytes)\n"
     "    Date and time of the last commit\n"),
#endif
    {'r', 'v', 'R', opt_depth, opt_incremental, opt_xml,
     opt_include_externals, opt_search}, },

  { "lock", svn_cl__lock, {0}, N_
    ("Lock working copy paths or URLs in the repository, so that\n"
     "no other user can commit changes to them.\n"
     "usage: lock TARGET...\n"
     "\n"
     "  Use --force to steal a lock from another user or working copy.\n"),
    { opt_targets, 'm', 'F', opt_force_log, opt_encoding, opt_force, 'q' },
    {{'F', N_("read lock comment from file ARG")},
     {'m', N_("specify lock comment ARG")},
     {opt_force_log, N_("force validity of lock comment source")},
     {opt_force, N_("steal locks")}} },

  { "log", svn_cl__log, {0}, N_
    ("Show the log messages for a set of revision(s) and/or path(s).\n"
     "usage: 1. log [PATH][@REV]\n"
     "       2. log URL[@REV] [PATH...]\n"
     "\n"
     "  1. Print the log messages for the URL corresponding to PATH\n"
     "     (default: '.'). If specified, REV is the revision in which the\n"
     "     URL is first looked up, and the default revision range is REV:1.\n"
     "     If REV is not specified, the default revision range is BASE:1,\n"
     "     since the URL might not exist in the HEAD revision.\n"
     "\n"
     "  2. Print the log messages for the PATHs (default: '.') under URL.\n"
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
     "  Each changed path is preceded with a symbol describing the change:\n"
     "    A: The path was added or copied.\n"
     "    D: The path was deleted.\n"
     "    R: The path was replaced (deleted and re-added in the same revision).\n"
     "    M: The path's file and/or property content was modified.\n"
     "  If an added or replaced path was copied from somewhere else, the copy\n"
     "  source path and revision are shown in parentheses.\n"
     "  If a file or directory was moved from one path to another with 'svn move'\n"
     "  the old path will be listed as deleted and the new path will be listed\n"
     "  as copied from the old path at a prior revision.\n"
     "\n"
     "  With -q, don't print the log message body itself (note that this is\n"
     "  compatible with -v).\n"
     "\n"
     "  Each log message is printed just once, even if more than one of the\n"
     "  affected paths for that revision were explicitly requested.  Logs\n"
     "  follow copy history by default.  Use --stop-on-copy to disable this\n"
     "  behavior, which can be useful for determining branchpoints.\n"
     "\n"
     "  The --depth option is only valid in combination with the --diff option\n"
     "  and limits the scope of the displayed diff to the specified depth.\n"
     "\n"
     "  If the --search option is used, log messages are displayed only if the\n"
     "  provided search pattern matches any of the author, date, log message\n"
     "  text (unless --quiet is used), or, if the --verbose option is also\n"
     "  provided, a changed path.\n"
     "  The search pattern may include \"glob syntax\" wildcards:\n"
     "      ?      matches any single character\n"
     "      *      matches a sequence of arbitrary characters\n"
     "      [abc]  matches any of the characters listed inside the brackets\n"
     "  If multiple --search options are provided, a log message is shown if\n"
     "  it matches any of the provided search patterns. If the --search-and\n"
     "  option is used, that option's argument is combined with the pattern\n"
     "  from the previous --search or --search-and option, and a log message\n"
     "  is shown only if it matches the combined search pattern.\n"
     "  If --limit is used in combination with --search, --limit restricts the\n"
     "  number of log messages searched, rather than restricting the output\n"
     "  to a particular number of matching log messages.\n"
     "\n"
     "  Examples:\n"
     "\n"
     "    Show the latest 5 log messages for the current working copy\n"
     "    directory and display paths changed in each commit:\n"
     "      svn log -l 5 -v\n"
     "\n"
     "    Show the log for bar.c as of revision 42:\n"
     "      svn log bar.c@42\n"
     "\n"
     "    Show log messages and diffs for each commit to foo.c:\n"
     "      svn log --diff http://www.example.com/repo/project/foo.c\n"
     "    (Because the above command uses a full URL it does not require\n"
     "     a working copy.)\n"
     "\n"
     "    Show log messages for the children foo.c and bar.c of the directory\n"
     "    '/trunk' as it appeared in revision 50, using the ^/ URL shortcut:\n"
     "      svn log ^/trunk@50 foo.c bar.c\n"
     "\n"
     "    Show the log messages for any incoming changes to foo.c during the\n"
     "    next 'svn update':\n"
     "      svn log -r BASE:HEAD foo.c\n"
     "\n"
     "    Show the log message for the revision in which /branches/foo\n"
     "    was created:\n"
     "      svn log --stop-on-copy --limit 1 -r0:HEAD ^/branches/foo\n"
     "\n"
     "    If ^/trunk/foo.c was moved to ^/trunk/bar.c' in revision 22, 'svn log -v'\n"
     "    shows a deletion and a copy in its changed paths list, such as:\n"
     "       D /trunk/foo.c\n"
     "       A /trunk/bar.c (from /trunk/foo.c:21)\n"),
    {'r', 'c', 'q', 'v', 'g', opt_targets, opt_stop_on_copy, opt_incremental,
     opt_xml, 'l', opt_with_all_revprops, opt_with_no_revprops,
     opt_with_revprop, opt_depth, opt_diff, opt_diff_cmd,
     opt_internal_diff, 'x', opt_search, opt_search_and },
    {{opt_with_revprop, N_("retrieve revision property ARG")},
     {'c', N_("the change made in revision ARG")},
     {'v', N_("also print all affected paths")},
     {'q', N_("do not print the log message")}} },

  { "merge", svn_cl__merge, {0}, N_
    ( /* For this large section, let's keep it unindented for easier
       * viewing/editing. It has been vim-treated with a textwidth=75 and 'gw'
       * (with quotes and newlines removed). */
"Merge changes into a working copy.\n"
"usage: 1. merge SOURCE[@REV] [TARGET_WCPATH]\n"
"          (the 'complete' merge)\n"
"       2. merge [-c M[,N...] | -r N:M ...] SOURCE[@REV] [TARGET_WCPATH]\n"
"          (the 'cherry-pick' merge)\n"
"       3. merge SOURCE1[@REV1] SOURCE2[@REV2] [TARGET_WCPATH]\n"
"          (the '2-URL' merge)\n"
"\n"
"  1. This form, with one source path and no revision range, is called\n"
"     a 'complete' merge:\n"
"\n"
"       svn merge SOURCE[@REV] [TARGET_WCPATH]\n"
"\n"
"     The complete merge is used for the 'sync' and 'reintegrate' merges\n"
"     in the 'feature branch' pattern described below. It finds all the\n"
"     changes on the source branch that have not already been merged to the\n"
"     target branch, and merges them into the working copy. Merge tracking\n"
"     is used to know which changes have already been merged.\n"
"\n"
"     SOURCE specifies the branch from where the changes will be pulled, and\n"
"     TARGET_WCPATH specifies a working copy of the target branch to which\n"
"     the changes will be applied. Normally SOURCE and TARGET_WCPATH should\n"
"     each correspond to the root of a branch. (If you want to merge only a\n"
"     subtree, then the subtree path must be included in both SOURCE and\n"
"     TARGET_WCPATH; this is discouraged, to avoid subtree mergeinfo.)\n"
"\n"
"     SOURCE is usually a URL. The optional '@REV' specifies both the peg\n"
"     revision of the URL and the latest revision that will be considered\n"
"     for merging; if REV is not specified, the HEAD revision is assumed. If\n"
"     SOURCE is a working copy path, the corresponding URL of the path is\n"
"     used, and the default value of 'REV' is the base revision (usually the\n"
"     revision last updated to).\n"
"\n"
"     TARGET_WCPATH is a working copy path; if omitted, '.' is generally\n"
"     assumed. There are some special cases:\n"
"\n"
"       - If SOURCE is a URL:\n"
"\n"
"           - If the basename of the URL and the basename of '.' are the\n"
"             same, then the differences are applied to '.'. Otherwise,\n"
"             if a file with the same basename as that of the URL is found\n"
"             within '.', then the differences are applied to that file.\n"
"             In all other cases, the target defaults to '.'.\n"
"\n"
"       - If SOURCE is a working copy path:\n"
"\n"
"           - If the source is a file, then differences are applied to that\n"
"             file (useful for reverse-merging earlier changes). Otherwise,\n"
"             if the source is a directory, then the target defaults to '.'.\n"
"\n"
"     In normal usage the working copy should be up to date, at a single\n"
"     revision, with no local modifications and no switched subtrees.\n"
"\n"
"       - The 'Feature Branch' Merging Pattern -\n"
"\n"
"     In this commonly used work flow, known also as the 'development\n"
"     branch' pattern, a developer creates a branch and commits a series of\n"
"     changes that implement a new feature. The developer periodically\n"
"     merges all the latest changes from the parent branch so as to keep the\n"
"     development branch up to date with those changes. When the feature is\n"
"     complete, the developer performs a merge from the feature branch to\n"
"     the parent branch to re-integrate the changes.\n"
"\n"
"         parent --+----------o------o-o-------------o--\n"
"                   \\            \\           \\      /\n"
"                    \\          merge      merge  merge\n"
"                     \\            \\           \\  /\n"
"         feature      +--o-o-------o----o-o----o-------\n"
"\n"
"     A merge from the parent branch to the feature branch is called a\n"
"     'sync' or 'catch-up' merge, and a merge from the feature branch to the\n"
"     parent branch is called a 'reintegrate' merge.\n"
"\n"
"       - Sync Merge Example -\n"
"                                 ............\n"
"                                .            .\n"
"         trunk  --+------------L--------------R------\n"
"                   \\                           \\\n"
"                    \\                          |\n"
"                     \\                         v\n"
"         feature      +------------------------o-----\n"
"                             r100            r200\n"
"\n"
"     Subversion will locate all the changes on 'trunk' that have not yet\n"
"     been merged into the 'feature' branch. In this case that is a single\n"
"     range, r100:200. In the diagram above, L marks the left side (trunk@100)\n"
"     and R marks the right side (trunk@200) of the merge source. The\n"
"     difference between L and R will be applied to the target working copy\n"
"     path. In this case, the working copy is a clean checkout of the entire\n"
"     'feature' branch.\n"
"\n"
"     To perform this sync merge, have a clean working copy of the feature\n"
"     branch and run the following command in its top-level directory:\n"
"\n"
"         svn merge ^/trunk\n"
"\n"
"     Note that the merge is now only in your local working copy and still\n"
"     needs to be committed to the repository so that it can be seen by\n"
"     others. You can review the changes and you may have to resolve\n"
"     conflicts before you commit the merge.\n"
"\n"
"       - Reintegrate Merge Example -\n"
"\n"
"     The feature branch was last synced with trunk up to revision X. So the\n"
"     difference between trunk@X and feature@HEAD contains the complete set\n"
"     of changes that implement the feature, and no other changes. These\n"
"     changes are applied to trunk.\n"
"\n"
"                    rW                   rX\n"
"         trunk ------+--------------------L------------------o\n"
"                      \\                    .                 ^\n"
"                       \\                    .............   /\n"
"                        \\                                . /\n"
"         feature         +--------------------------------R\n"
"\n"
"     In the diagram above, L marks the left side (trunk@X) and R marks the\n"
"     right side (feature@HEAD) of the merge. The difference between the\n"
"     left and right side is merged into trunk, the target.\n"
"\n"
"     To perform the merge, have a clean working copy of trunk and run the\n"
"     following command in its top-level directory:\n"
"\n"
"         svn merge ^/feature\n"
"\n"
"     To prevent unnecessary merge conflicts, a reintegrate merge requires\n"
"     that TARGET_WCPATH is not a mixed-revision working copy, has no local\n"
"     modifications, and has no switched subtrees.\n"
"\n"
"     A reintegrate merge also requires that the source branch is coherently\n"
"     synced with the target -- in the above example, this means that all\n"
"     revisions between the branch point W and the last merged revision X\n"
"     are merged to the feature branch, so that there are no unmerged\n"
"     revisions in-between.\n"
"\n"
"\n"
"  2. This form is called a 'cherry-pick' merge:\n"
"\n"
"       svn merge [-c M[,N...] | -r N:M ...] SOURCE[@REV] [TARGET_WCPATH]\n"
"\n"
"     A cherry-pick merge is used to merge specific revisions (or revision\n"
"     ranges) from one branch to another. By default, this uses merge\n"
"     tracking to automatically skip any revisions that have already been\n"
"     merged to the target; you can use the --ignore-ancestry option to\n"
"     disable such skipping.\n"
"\n"
"     SOURCE is usually a URL. The optional '@REV' specifies only the peg\n"
"     revision of the URL and does not affect the merge range; if REV is not\n"
"     specified, the HEAD revision is assumed. If SOURCE is a working copy\n"
"     path, the corresponding URL of the path is used, and the default value\n"
"     of 'REV' is the base revision (usually the revision last updated to).\n"
"\n"
"     TARGET_WCPATH is a working copy path; if omitted, '.' is generally\n"
"     assumed. The special cases noted above in the 'complete' merge form\n"
"     also apply here.\n"
"\n"
"     The revision ranges to be merged are specified by the '-r' and/or '-c'\n"
"     options. '-r N:M' refers to the difference in the history of the\n"
"     source branch between revisions N and M. You can use '-c M' to merge\n"
"     single revisions: '-c M' is equivalent to '-r <M-1>:M'. Each such\n"
"     difference is applied to TARGET_WCPATH.\n"
"\n"
"     If the mergeinfo in TARGET_WCPATH indicates that revisions within the\n"
"     range were already merged, changes made in those revisions are not\n"
"     merged again. If needed, the range is broken into multiple sub-ranges,\n"
"     and each sub-range is merged separately.\n"
"\n"
"     A 'reverse range' can be used to undo changes. For example, when\n"
"     source and target refer to the same branch, a previously committed\n"
"     revision can be 'undone'. In a reverse range, N is greater than M in\n"
"     '-r N:M', or the '-c' option is used with a negative number: '-c -M'\n"
"     is equivalent to '-r M:<M-1>'. Undoing changes like this is also known\n"
"     as performing a 'reverse merge'.\n"
"\n"
"     Multiple '-c' and/or '-r' options may be specified and mixing of\n"
"     forward and reverse ranges is allowed.\n"
"\n"
"       - Cherry-pick Merge Example -\n"
"\n"
"     A bug has been fixed on trunk in revision 50. This fix needs to\n"
"     be merged from trunk onto the release branch.\n"
"\n"
"            1.x-release  +-----------------------o-----\n"
"                        /                        ^\n"
"                       /                         |\n"
"                      /                          |\n"
"         trunk ------+--------------------------LR-----\n"
"                                                r50\n"
"\n"
"     In the above diagram, L marks the left side (trunk@49) and R marks the\n"
"     right side (trunk@50) of the merge. The difference between the left\n"
"     and right side is applied to the target working copy path.\n"
"\n"
"     Note that the difference between revision 49 and 50 is exactly those\n"
"     changes that were committed in revision 50, not including changes\n"
"     committed in revision 49.\n"
"\n"
"     To perform the merge, have a clean working copy of the release branch\n"
"     and run the following command in its top-level directory; remember\n"
"     that the default target is '.':\n"
"\n"
"         svn merge -c50 ^/trunk\n"
"\n"
"     You can also cherry-pick several revisions and/or revision ranges:\n"
"\n"
"         svn merge -c50,54,60 -r65:68 ^/trunk\n"
"\n"
"\n"
"  3. This form is called a '2-URL merge':\n"
"\n"
"       svn merge SOURCE1[@REV1] SOURCE2[@REV2] [TARGET_WCPATH]\n"
"\n"
"     You should use this merge variant only if the other variants do not\n"
"     apply to your situation, as this variant can be quite complex to\n"
"     master.\n"
"\n"
"     Two source URLs are specified, identifying two trees on the same\n"
"     branch or on different branches. The trees are compared and the\n"
"     difference from SOURCE1@REV1 to SOURCE2@REV2 is applied to the\n"
"     working copy of the target branch at TARGET_WCPATH. The target\n"
"     branch may be the same as one or both sources, or different again.\n"
"     The three branches involved can be completely unrelated.\n"
"\n"
"     TARGET_WCPATH is a working copy path; if omitted, '.' is generally\n"
"     assumed. The special cases noted above in the 'complete' merge form\n"
"     also apply here.\n"
"\n"
"     SOURCE1 and/or SOURCE2 can also be specified as a working copy path,\n"
"     in which case the merge source URL is derived from the working copy.\n"
"\n"
"       - 2-URL Merge Example -\n"
"\n"
"     Two features have been developed on separate branches called 'foo' and\n"
"     'bar'. It has since become clear that 'bar' should be combined with\n"
"     the 'foo' branch for further development before reintegration.\n"
"\n"
"     Although both feature branches originate from trunk, they are not\n"
"     directly related -- one is not a direct copy of the other. A 2-URL\n"
"     merge is necessary.\n"
"\n"
"     The 'bar' branch has been synced with trunk up to revision 500.\n"
"     (If this revision number is not known, it can be located using the\n"
"     'svn log' and/or 'svn mergeinfo' commands.)\n"
"     The difference between trunk@500 and bar@HEAD contains the complete\n"
"     set of changes related to feature 'bar', and no other changes. These\n"
"     changes are applied to the 'foo' branch.\n"
"\n"
"                           foo  +-----------------------------------o\n"
"                               /                                    ^\n"
"                              /                                    /\n"
"                             /              r500                  /\n"
"         trunk ------+------+-----------------L--------->        /\n"
"                      \\                        .                /\n"
"                       \\                        ............   /\n"
"                        \\                                   . /\n"
"                    bar  +-----------------------------------R\n"
"\n"
"     In the diagram above, L marks the left side (trunk@500) and R marks\n"
"     the right side (bar@HEAD) of the merge. The difference between the\n"
"     left and right side is applied to the target working copy path, in\n"
"     this case a working copy of the 'foo' branch.\n"
"\n"
"     To perform the merge, have a clean working copy of the 'foo' branch\n"
"     and run the following command in its top-level directory:\n"
"\n"
"         svn merge ^/trunk@500 ^/bar\n"
"\n"
"     The exact changes applied by a 2-URL merge can be previewed with svn's\n"
"     diff command, which is a good idea to verify if you do not have the\n"
"     luxury of a clean working copy to merge to. In this case:\n"
"\n"
"         svn diff ^/trunk@500 ^/bar@HEAD\n"
"\n"
"\n"
"  The following applies to all types of merges:\n"
"\n"
"  To prevent unnecessary merge conflicts, svn merge requires that\n"
"  TARGET_WCPATH is not a mixed-revision working copy. Running 'svn update'\n"
"  before starting a merge ensures that all items in the working copy are\n"
"  based on the same revision.\n"
"\n"
"  If possible, you should have no local modifications in the merge's target\n"
"  working copy prior to the merge, to keep things simpler. It will be\n"
"  easier to revert the merge and to understand the branch's history.\n"
"\n"
"  Switched sub-paths should also be avoided during merging, as they may\n"
"  cause incomplete merges and create subtree mergeinfo.\n"
"\n"
"  For each merged item a line will be printed with characters reporting the\n"
"  action taken. These characters have the following meaning:\n"
"\n"
"    A  Added\n"
"    D  Deleted\n"
"    U  Updated\n"
"    C  Conflict\n"
"    G  Merged\n"
"    E  Existed\n"
"    R  Replaced\n"
"\n"
"  Characters in the first column report about the item itself.\n"
"  Characters in the second column report about properties of the item.\n"
"  A 'C' in the third column indicates a tree conflict, while a 'C' in\n"
"  the first and second columns indicate textual conflicts in files\n"
"  and in property values, respectively.\n"
"\n"
"    - Merge Tracking -\n"
"\n"
"  Subversion uses the svn:mergeinfo property to track merge history. This\n"
"  property is considered at the start of a merge to determine what to merge\n"
"  and it is updated at the conclusion of the merge to describe the merge\n"
"  that took place. Mergeinfo is used only if the two sources are on the\n"
"  same line of history -- if the first source is an ancestor of the second,\n"
"  or vice-versa (i.e. if one has originally been created by copying the\n"
"  other). This is verified and enforced when using sync merges and\n"
"  reintegrate merges.\n"
"\n"
"  The --ignore-ancestry option prevents merge tracking and thus ignores\n"
"  mergeinfo, neither considering it nor recording it.\n"
"\n"
"    - Merging from foreign repositories -\n"
"\n"
"  Subversion does support merging from foreign repositories.\n"
"  While all merge source URLs must point to the same repository, the merge\n"
"  target working copy may come from a different repository than the source.\n"
"  However, there are some caveats. Most notably, copies made in the\n"
"  merge source will be transformed into plain additions in the merge\n"
"  target. Also, merge-tracking is not supported for merges from foreign\n"
"  repositories.\n"),
    {'r', 'c', 'N', opt_depth, 'q', opt_force, opt_dry_run, opt_merge_cmd,
     opt_record_only, 'x', opt_ignore_ancestry, opt_accept, opt_reintegrate,
     opt_allow_mixed_revisions, 'v'},
    { { opt_force, N_("force deletions even if deleted contents don't match") } }
  },

  { "mergeinfo", svn_cl__mergeinfo, {0}, N_
    ("Display merge-related information.\n"
     "usage: 1. mergeinfo SOURCE[@REV] [TARGET[@REV]]\n"
     "       2. mergeinfo --show-revs=WHICH SOURCE[@REV] [TARGET[@REV]]\n"
     "\n"
     "  1. Summarize the history of merging between SOURCE and TARGET. The graph\n"
     "     shows, from left to right:\n"
     "       the youngest common ancestor of the branches;\n"
     "       the latest full merge in either direction, and thus the common base\n"
     "         that will be used for the next complete merge;\n"
     "       the repository path and revision number of the tip of each branch.\n"
     "\n"
     "  2. Print the revision numbers on SOURCE that have been merged to TARGET\n"
     "     (with --show-revs=merged), or that have not been merged to TARGET\n"
     "     (with --show-revs=eligible). Print only revisions in which there was\n"
     "     at least one change in SOURCE.\n"
     "\n"
     "     If --revision (-r) is provided, filter the displayed information to\n"
     "     show only that which is associated with the revisions within the\n"
     "     specified range.  Revision numbers, dates, and the 'HEAD' keyword are\n"
     "     valid range values.\n"
     "\n"
     "  SOURCE and TARGET are the source and target branch URLs, respectively.\n"
     "  (If a WC path is given, the corresponding base URL is used.) The default\n"
     "  TARGET is the current working directory ('.'). REV specifies the revision\n"
     "  to be considered the tip of the branch; the default for SOURCE is HEAD,\n"
     "  and the default for TARGET is HEAD for a URL or BASE for a WC path.\n"
     "\n"
     "  The depth can be 'empty' or 'infinity'; the default is 'empty'.\n"),
    {'r', 'R', 'q', 'v', opt_depth, opt_show_revs, opt_mergeinfo_log,
      opt_incremental } },

  { "mkdir", svn_cl__mkdir, {0}, N_
    ("Create a new directory under version control.\n"
     "usage: 1. mkdir PATH...\n"
     "       2. mkdir URL...\n"
     "\n"
     "  Create version controlled directories.\n"
     "\n"
     "  1. Each directory specified by a working copy PATH is created locally\n"
     "    and scheduled for addition upon the next commit.\n"
     "\n"
     "  2. Each directory specified by a URL is created in the repository via\n"
     "    an immediate commit.\n"
     "\n"
     "  In both cases, all the intermediate directories must already exist,\n"
     "  unless the --parents option is given.\n"),
    {'q', opt_parents, SVN_CL__LOG_MSG_OPTIONS} },

  { "move", svn_cl__move, {"mv", "rename", "ren"}, N_
    ("Move (rename) an item in a working copy or repository.\n"
     "usage: move SRC... DST\n"
     "\n"
     "  SRC and DST can both be working copy (WC) paths or URLs:\n"
     "    WC  -> WC:  move an item in a working copy, as a local change to\n"
     "                be committed later (with or without further changes)\n"
     "    URL -> URL: move an item in the repository directly, immediately\n"
     "                creating a new revision in the repository\n"
     "  All the SRCs must be of the same type. If DST is an existing directory,\n"
     "  the sources will be added as children of DST. When moving multiple\n"
     "  sources, DST must be an existing directory.\n"
     "\n"
     "  SRC and DST of WC -> WC moves must be committed in the same revision.\n"
     "  Furthermore, WC -> WC moves will refuse to move a mixed-revision subtree.\n"
     "  To avoid unnecessary conflicts, it is recommended to run 'svn update'\n"
     "  to update the subtree to a single revision before moving it.\n"
     "  The --allow-mixed-revisions option is provided for backward compatibility.\n"),
    {'q', opt_force, opt_parents, opt_allow_mixed_revisions,
     SVN_CL__LOG_MSG_OPTIONS, 'r'},
    {{'r', "deprecated and ignored"}} },

  { "patch", svn_cl__patch, {0}, N_
    ("Apply a patch to a working copy.\n"
     "usage: patch PATCHFILE [WCPATH]\n"
     "\n"
     "  Apply a unidiff patch in PATCHFILE to the working copy WCPATH.\n"
     "  If WCPATH is omitted, '.' is assumed.\n"
     "\n"
     "  A unidiff patch suitable for application to a working copy can be\n"
     "  produced with the 'svn diff' command or third-party diffing tools.\n"
     "  Any non-unidiff content of PATCHFILE is ignored, except for Subversion\n"
     "  property diffs as produced by 'svn diff'.\n"
     "\n"
     "  Changes listed in the patch will either be applied or rejected.\n"
     "  If a change does not match at its exact line offset, it may be applied\n"
     "  earlier or later in the file if a match is found elsewhere for the\n"
     "  surrounding lines of context provided by the patch.\n"
     "  A change may also be applied with fuzz, which means that one\n"
     "  or more lines of context are ignored when matching the change.\n"
     "  If no matching context can be found for a change, the change conflicts\n"
     "  and will be written to a reject file with the extension .svnpatch.rej.\n"
     "\n"
     "  For each patched file a line will be printed with characters reporting\n"
     "  the action taken. These characters have the following meaning:\n"
     "\n"
     "    A  Added\n"
     "    D  Deleted\n"
     "    U  Updated\n"
     "    C  Conflict\n"
     "    G  Merged (with local uncommitted changes)\n"
     "\n"
     "  Changes applied with an offset or fuzz are reported on lines starting\n"
     "  with the '>' symbol. You should review such changes carefully.\n"
     "\n"
     "  If the patch removes all content from a file, that file is scheduled\n"
     "  for deletion. If the patch creates a new file, that file is scheduled\n"
     "  for addition. Use 'svn revert' to undo deletions and additions you\n"
     "  do not agree with.\n"
     "\n"
     "  Hint: If the patch file was created with Subversion, it will contain\n"
     "        the number of a revision N the patch will cleanly apply to\n"
     "        (look for lines like '--- foo/bar.txt        (revision N)').\n"
     "        To avoid rejects, first update to the revision N using\n"
     "        'svn update -r N', apply the patch, and then update back to the\n"
     "        HEAD revision. This way, conflicts can be resolved interactively.\n"
     ),
    {'q', opt_dry_run, opt_strip, opt_reverse_diff,
     opt_ignore_whitespace} },

  { "propdel", svn_cl__propdel, {"pdel", "pd"}, N_
    ("Remove a property from files, dirs, or revisions.\n"
     "usage: 1. propdel PROPNAME [PATH...]\n"
     "       2. propdel PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Removes versioned props in working copy.\n"
     "  2. Removes unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  See 'svn help propset' for descriptions of the svn:* special properties.\n"),
    {'q', 'R', opt_depth, 'r', opt_revprop, opt_changelist} },

  { "propedit", svn_cl__propedit, {"pedit", "pe"}, N_
    ("Edit a property with an external editor.\n"
     "usage: 1. propedit PROPNAME TARGET...\n"
     "       2. propedit PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Edits versioned prop in working copy or repository.\n"
     "  2. Edits unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  See 'svn help propset' for descriptions of the svn:* special properties.\n"),
    {'r', opt_revprop, SVN_CL__LOG_MSG_OPTIONS, opt_force} },

  { "propget", svn_cl__propget, {"pget", "pg"}, N_
    ("Print the value of a property on files, dirs, or revisions.\n"
     "usage: 1. propget PROPNAME [TARGET[@REV]...]\n"
     "       2. propget PROPNAME --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Prints versioned props. If specified, REV determines in which\n"
     "     revision the target is first looked up.\n"
     "  2. Prints unversioned remote prop on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  With --verbose, the target path and the property name are printed on\n"
     "  separate lines before each value, like 'svn proplist --verbose'.\n"
     "  Otherwise, if there is more than one TARGET or a depth other than\n"
     "  'empty', the target path is printed on the same line before each value.\n"
     "\n"
     "  By default, an extra newline is printed after the property value so that\n"
     "  the output looks pretty.  With a single TARGET, depth 'empty' and without\n"
     "  --show-inherited-props, you can use the --no-newline option to disable this\n"
     "  (useful when redirecting a binary property value to a file, for example).\n"
     "\n"
     "  See 'svn help propset' for descriptions of the svn:* special properties.\n"),
    {'v', 'R', opt_depth, 'r', opt_revprop, opt_strict, opt_no_newline, opt_xml,
     opt_changelist, opt_show_inherited_props },
    {{'v', N_("print path, name and value on separate lines")},
     {opt_strict, N_("(deprecated; use --no-newline)")}} },

  { "proplist", svn_cl__proplist, {"plist", "pl"}, N_
    ("List all properties on files, dirs, or revisions.\n"
     "usage: 1. proplist [TARGET[@REV]...]\n"
     "       2. proplist --revprop -r REV [TARGET]\n"
     "\n"
     "  1. Lists versioned props. If specified, REV determines in which\n"
     "     revision the target is first looked up.\n"
     "  2. Lists unversioned remote props on repos revision.\n"
     "     TARGET only determines which repository to access.\n"
     "\n"
     "  With --verbose, the property values are printed as well, like 'svn propget\n"
     "  --verbose'.  With --quiet, the paths are not printed.\n"
     "\n"
     "  See 'svn help propset' for descriptions of the svn:* special properties.\n"),
    {'v', 'R', opt_depth, 'r', 'q', opt_revprop, opt_xml, opt_changelist,
     opt_show_inherited_props },
    {{'v', N_("print path, name and value on separate lines")},
     {'q', N_("don't print the path")}} },

  { "propset", svn_cl__propset, {"pset", "ps"}, N_
    ("Set the value of a property on files, dirs, or revisions.\n"
     "usage: 1. propset PROPNAME PROPVAL PATH...\n"
     "       2. propset PROPNAME --revprop -r REV PROPVAL [TARGET]\n"
     "\n"
     "  1. Changes a versioned file or directory property in a working copy.\n"
     "  2. Changes an unversioned property on a repository revision.\n"
     "     (TARGET only determines which repository to access.)\n"
     "\n"
     "  The value may be provided with the --file option instead of PROPVAL.\n"
     "\n"
     "  Property names starting with 'svn:' are reserved.  Subversion recognizes\n"
     "  the following special versioned properties on a file:\n"
     "    svn:keywords   - Keywords to be expanded.  Valid keywords are:\n"
     "      URL, HeadURL             - The URL for the head version of the file.\n"
     "      Author, LastChangedBy    - The last person to modify the file.\n"
     "      Date, LastChangedDate    - The date/time the file was last modified.\n"
     "      Rev, Revision,           - The last revision the file changed.\n"
     "        LastChangedRevision\n"
     "      Id                       - A compressed summary of the previous four.\n"
     "      Header                   - Similar to Id but includes the full URL.\n"
     "\n"
     "      Custom keywords can be defined with a format string separated from\n"
     "      the keyword name with '='. Valid format substitutions are:\n"
     "        %a   - The author of the revision given by %r.\n"
     "        %b   - The basename of the URL of the file.\n"
     "        %d   - Short format of the date of the revision given by %r.\n"
     "        %D   - Long format of the date of the revision given by %r.\n"
     "        %P   - The file's path, relative to the repository root.\n"
     "        %r   - The number of the revision which last changed the file.\n"
     "        %R   - The URL to the root of the repository.\n"
     "        %u   - The URL of the file.\n"
     "        %_   - A space (keyword definitions cannot contain a literal space).\n"
     "        %%   - A literal '%'.\n"
     "        %H   - Equivalent to %P%_%r%_%d%_%a.\n"
     "        %I   - Equivalent to %b%_%r%_%d%_%a.\n"
     "      Example custom keyword definition: MyKeyword=%r%_%a%_%P\n"
     "      Once a custom keyword has been defined for a file, it can be used\n"
     "      within the file like any other keyword: $MyKeyword$\n"
     "\n"
     "    svn:executable - If present, make the file executable.  Use\n"
     "      'svn propdel svn:executable PATH...' to clear.\n"
     "    svn:eol-style  - One of 'native', 'LF', 'CR', 'CRLF'.\n"
     "    svn:mime-type  - The mimetype of the file.  Used to determine\n"
     "      whether to merge the file, and how to serve it from Apache.\n"
     "      A mimetype beginning with 'text/' (or an absent mimetype) is\n"
     "      treated as text.  Anything else is treated as binary.\n"
     "    svn:needs-lock - If present, indicates that the file should be locked\n"
     "      before it is modified.  Makes the working copy file read-only\n"
     "      when it is not locked.  Use 'svn propdel svn:needs-lock PATH...'\n"
     "      to clear.\n"
     "\n"
     "  Subversion recognizes the following special versioned properties on a\n"
     "  directory:\n"
     "    svn:ignore         - A list of file glob patterns to ignore, one per line.\n"
     "    svn:global-ignores - Like svn:ignore, but inheritable.\n"
     "    svn:auto-props     - Automatically set properties on files when they are\n"
     "      added or imported. Contains key-value pairs, one per line, in the format:\n"
     "        PATTERN = PROPNAME=VALUE[;PROPNAME=VALUE ...]\n"
     "      Example (where a literal ';' is escaped by adding another ';'):\n"
     "        *.html = svn:eol-style=native;svn:mime-type=text/html;; charset=UTF8\n"
     "      Applies recursively to all files added or imported under the directory\n"
     "      it is set on.  See also [auto-props] in the client configuration file.\n"
     "    svn:externals      - A list of module specifiers, one per line, in the\n"
     "      following format similar to the syntax of 'svn checkout':\n"
     "        [-r REV] URL[@PEG] LOCALPATH\n"
     "      Example:\n"
     "        http://example.com/repos/zig foo/bar\n"
     "      The LOCALPATH is relative to the directory having this property.\n"
     "      To pin the external to a known revision, specify the optional REV:\n"
     "        -r25 http://example.com/repos/zig foo/bar\n"
     "      To unambiguously identify an element at a path which may have been\n"
     "      subsequently deleted or renamed, specify the optional PEG revision:\n"
     "        -r25 http://example.com/repos/zig@42 foo/bar\n"
     "      The URL may be a full URL or a relative URL starting with one of:\n"
     "        ../  to the parent directory of the extracted external\n"
     "        ^/   to the repository root\n"
     "        /    to the server root\n"
     "        //   to the URL scheme\n"
     "      ^/../  to a sibling repository beneath the same SVNParentPath location\n"
     "      Use of the following format is discouraged but is supported for\n"
     "      interoperability with Subversion 1.4 and earlier clients:\n"
     "        LOCALPATH [-r PEG] URL\n"
     "      The ambiguous format 'relative_path relative_path' is taken as\n"
     "      'relative_url relative_path' with peg revision support.\n"
     "      Lines starting with a '#' character are ignored.\n"),
    {'F', opt_encoding, 'q', 'r', opt_targets, 'R', opt_depth, opt_revprop,
     opt_force, opt_changelist },
    {{'F', N_("read property value from file ARG")}} },

  { "relocate", svn_cl__relocate, {0}, N_
    ("Relocate the working copy to point to a different repository root URL.\n"
     "usage: 1. relocate FROM-PREFIX TO-PREFIX [PATH...]\n"
     "       2. relocate TO-URL [PATH]\n"
     "\n"
     "  Rewrite working copy URL metadata to reflect a syntactic change only.\n"
     "  This is used when a repository's root URL changes (such as a scheme\n"
     "  or hostname change) but your working copy still reflects the same\n"
     "  directory within the same repository.\n"
     "\n"
     "  1. FROM-PREFIX and TO-PREFIX are initial substrings of the working\n"
     "     copy's current and new URLs, respectively.  (You may specify the\n"
     "     complete old and new URLs if you wish.)  Use 'svn info' to determine\n"
     "     the current working copy URL.\n"
     "\n"
     "  2. TO-URL is the (complete) new repository URL to use for PATH.\n"
     "\n"
     "  Examples:\n"
     "    svn relocate http:// svn:// project1 project2\n"
     "    svn relocate http://www.example.com/repo/project \\\n"
     "                 svn://svn.example.com/repo/project\n"),
    {opt_ignore_externals} },

  { "resolve", svn_cl__resolve, {0}, N_
    ("Resolve conflicts on working copy files or directories.\n"
     "usage: resolve [PATH...]\n"
     "\n"
     "  By default, perform interactive conflict resolution on PATH.\n"
     "  In this mode, the command is recursive by default (depth 'infinity').\n"
     "\n"
     "  The --accept=ARG option prevents interactive prompting and forces\n"
     "  conflicts on PATH to be resolved in the manner specified by ARG.\n"
     "  In this mode, the command is not recursive by default (depth 'empty').\n"
     "\n"
     "  A conflicted path cannot be committed with 'svn commit' until it\n"
     "  has been marked as resolved with 'svn resolve'.\n"
     "\n"
     "  Subversion knows three types of conflicts:\n"
     "  Text conflicts, Property conflicts, and Tree conflicts.\n"
     "\n"
     "  Text conflicts occur when overlapping changes to file contents were\n"
     "  made. Text conflicts are usually resolved by editing the conflicted\n"
     "  file or by using a merge tool (which may be an external program).\n"
     "  'svn resolve' provides options which can be used to automatically\n"
     "  edit files (such as 'mine-full' or 'theirs-conflict'), but these are\n"
     "  only useful in situations where it is acceptable to discard local or\n"
     "  incoming changes altogether.\n"
     "\n"
     "  Property conflicts are usually resolved by editing the value of the\n"
     "  conflicted property (either from the interactive prompt, or with\n"
     "  'svn propedit'). As with text conflicts, options exist to edit a\n"
     "  property automatically, discarding some changes in favour of others.\n"
     "\n"
     "  Tree conflicts occur when a change to the directory structure was\n"
     "  made, and when this change cannot be applied to the working copy\n"
     "  without affecting other changes (text changes, property changes,\n"
     "  or other changes to the directory structure). Brief information about\n"
     "  tree conflicts is shown by the 'svn status' and 'svn info' commands.\n"
     "  In interactive mode, 'svn resolve' will attempt to describe tree conflicts\n"
     "  in detail, and may offer options to resolve the conflict automatically.\n"
     "  It is recommended to use these automatic options whenever possible,\n"
     "  rather than attempting manual tree conflict resolution.\n"
     "\n"
     "  If a tree conflict cannot be resolved automatically, it is recommended\n"
     "  to figure out why the conflict occurred before attempting to resolve it.\n"
     "  The 'svn log -v' command can be used to inspect structural changes\n"
     "  made in past revisions, and perhaps even on other branches.\n"
     "  'svn help log' describes how these structural changes are presented.\n"
     "  Once the conflicting \"incoming\" change has been identified with 'svn log'\n"
     "  the current \"local\" working copy state should be examined and adjusted\n"
     "  in a way such that the conflict is resolved. This may involve editing\n"
     "  files manually or with 'svn merge'. It may be necessary to discard some\n"
     "  local changes with 'svn revert'. Files or directories might have to be\n"
     "  copied, deleted, or moved.\n"),
    {opt_targets, 'R', opt_depth, 'q', opt_accept},
    {{opt_accept, N_("specify automatic conflict resolution source\n"
                     "                             "
                     "('base', 'working', 'mine-conflict',\n"
                     "                             "
                     "'theirs-conflict', 'mine-full', 'theirs-full')")}} },

  { "resolved", svn_cl__resolved, {0}, N_
    ("Remove 'conflicted' state on working copy files or directories.\n"
     "usage: resolved PATH...\n"
     "\n"
     "  Note:  this subcommand does not semantically resolve conflicts or\n"
     "  remove conflict markers; it merely removes the conflict-related\n"
     "  artifact files and allows PATH to be committed again.  It has been\n"
     "  deprecated in favor of running 'svn resolve --accept working'.\n"),
    {opt_targets, 'R', opt_depth, 'q'} },

  { "revert", svn_cl__revert, {0}, N_
    ("Restore pristine working copy state (undo local changes).\n"
     "usage: revert PATH...\n"
     "\n"
     "  Revert changes in the working copy at or within PATH, and remove\n"
     "  conflict markers as well, if any.\n"
     "\n"
     "  This subcommand does not revert already committed changes.\n"
     "  For information about undoing already committed changes, search\n"
     "  the output of 'svn help merge' for 'undo'.\n"),
    {opt_targets, 'R', opt_depth, 'q', opt_changelist} },

  { "status", svn_cl__status, {"stat", "st"}, N_
    ("Print the status of working copy files and directories.\n"
     "usage: status [PATH...]\n"
     "\n"
     "  With no args, print only locally modified items (no network access).\n"
     "  With -q, print only summary information about locally modified items.\n"
     "  With -u, add working revision and server out-of-date information.\n"
     "  With -v, print full revision information on every item.\n"
     "\n"
     "  The first seven columns in the output are each one character wide:\n"
     "    First column: Says if item was added, deleted, or otherwise changed\n"
     "      ' ' no modifications\n"
     "      'A' Added\n"
     "      'C' Conflicted\n"
     "      'D' Deleted\n"
     "      'I' Ignored\n"
     "      'M' Modified\n"
     "      'R' Replaced\n"
     "      'X' an unversioned directory created by an externals definition\n"
     "      '?' item is not under version control\n"
     "      '!' item is missing (removed by non-svn command) or incomplete\n"
     "      '~' versioned item obstructed by some item of a different kind\n"
     "    Second column: Modifications of a file's or directory's properties\n"
     "      ' ' no modifications\n"
     "      'C' Conflicted\n"
     "      'M' Modified\n"
     "    Third column: Whether the working copy is locked for writing by\n"
     "                  another Subversion client modifying the working copy\n"
     "      ' ' not locked for writing\n"
     "      'L' locked for writing\n"
     "    Fourth column: Scheduled commit will create a copy (addition-with-history)\n"
     "      ' ' no history scheduled with commit (item was newly added)\n"
     "      '+' history scheduled with commit (item was copied)\n"
     "    Fifth column: Whether the item is switched or a file external\n"
     "      ' ' normal\n"
     "      'S' the item has a Switched URL relative to the parent\n"
     "      'X' a versioned file created by an eXternals definition\n"
     "    Sixth column: Whether the item is locked in repository for exclusive commit\n"
     "      (without -u)\n"
     "      ' ' not locked by this working copy\n"
     "      'K' locked by this working copy, but lock might be stolen or broken\n"
     "      (with -u)\n"
     "      ' ' not locked in repository, not locked by this working copy\n"
     "      'K' locked in repository, lock owned by this working copy\n"
     "      'O' locked in repository, lock owned by another working copy\n"
     "      'T' locked in repository, lock owned by this working copy was stolen\n"
     "      'B' not locked in repository, lock owned by this working copy is broken\n"
     "    Seventh column: Whether the item is the victim of a tree conflict\n"
     "      ' ' normal\n"
     "      'C' tree-Conflicted\n"
     "    If the item is a tree conflict victim, an additional line is printed\n"
     "    after the item's status line, explaining the nature of the conflict.\n"
     "\n"
     "  The out-of-date information appears in the ninth column (with -u):\n"
     "      '*' a newer revision exists on the server\n"
     "      ' ' the working copy is up to date\n"
     "\n"
     "  Remaining fields are variable width and delimited by spaces:\n"
     "    The working revision (with -u or -v; '-' if the item is copied)\n"
     "    The last committed revision and last committed author (with -v)\n"
     "    The working copy path is always the final field, so it can\n"
     "      include spaces.\n"
     "\n"
     "  The presence of a question mark ('?') where a working revision, last\n"
     "  committed revision, or last committed author was expected indicates\n"
     "  that the information is unknown or irrelevant given the state of the\n"
     "  item (for example, when the item is the result of a copy operation).\n"
     "  The question mark serves as a visual placeholder to facilitate parsing.\n"
     "\n"
     "  Example output:\n"
     "    svn status wc\n"
     "     M      wc/bar.c\n"
     "    A  +    wc/qax.c\n"
     "\n"
     "    svn status -u wc\n"
     "     M             965   wc/bar.c\n"
     "            *      965   wc/foo.c\n"
     "    A  +             -   wc/qax.c\n"
     "    Status against revision:   981\n"
     "\n"
     "    svn status --show-updates --verbose wc\n"
     "     M             965      938 kfogel       wc/bar.c\n"
     "            *      965      922 sussman      wc/foo.c\n"
     "    A  +             -      687 joe          wc/qax.c\n"
     "                   965      687 joe          wc/zig.c\n"
     "    Status against revision:   981\n"
     "\n"
     "    svn status\n"
     "     M      wc/bar.c\n"
     "    !     C wc/qaz.c\n"
     "          >   local missing, incoming edit upon update\n"
     "    D       wc/qax.c\n"),
    { 'u', 'v', 'N', opt_depth, 'r', 'q', opt_no_ignore, opt_incremental,
      opt_xml, opt_ignore_externals, opt_changelist},
    {{'q', N_("don't print unversioned items")}} },

  { "switch", svn_cl__switch, {"sw"}, N_
    ("Update the working copy to a different URL within the same repository.\n"
     "usage: 1. switch URL[@PEGREV] [PATH]\n"
     "       2. switch --relocate FROM-PREFIX TO-PREFIX [PATH...]\n"
     "\n"
     "  1. Update the working copy to mirror a new URL within the repository.\n"
     "     This behavior is similar to 'svn update', and is the way to\n"
     "     move a working copy to a branch or tag within the same repository.\n"
     "     If specified, PEGREV determines in which revision the target is first\n"
     "     looked up.\n"
     "\n"
     "     If --force is used, unversioned obstructing paths in the working\n"
     "     copy do not automatically cause a failure if the switch attempts to\n"
     "     add the same path.  If the obstructing path is the same type (file\n"
     "     or directory) as the corresponding path in the repository it becomes\n"
     "     versioned but its contents are left 'as-is' in the working copy.\n"
     "     This means that an obstructing directory's unversioned children may\n"
     "     also obstruct and become versioned.  For files, any content differences\n"
     "     between the obstruction and the repository are treated like a local\n"
     "     modification to the working copy.  All properties from the repository\n"
     "     are applied to the obstructing path.\n"
     "\n"
     "     Use the --set-depth option to set a new working copy depth on the\n"
     "     targets of this operation.\n"
     "\n"
     "     By default, Subversion will refuse to switch a working copy path to\n"
     "     a new URL with which it shares no common version control ancestry.\n"
     "     Use the '--ignore-ancestry' option to override this sanity check.\n"
     "\n"
     "  2. The '--relocate' option is deprecated. This syntax is equivalent to\n"
     "     'svn relocate FROM-PREFIX TO-PREFIX [PATH]'.\n"
     "\n"
     "  See also 'svn help update' for a list of possible characters\n"
     "  reporting the action taken.\n"
     "\n"
     "  Examples:\n"
     "    svn switch ^/branches/1.x-release\n"
     "    svn switch --relocate http:// svn://\n"
     "    svn switch --relocate http://www.example.com/repo/project \\\n"
     "                          svn://svn.example.com/repo/project\n"),
    { 'r', 'N', opt_depth, opt_set_depth, 'q', opt_merge_cmd,
      opt_ignore_externals, opt_ignore_ancestry, opt_force, opt_accept,
      opt_relocate },
    {{opt_ignore_ancestry,
     N_("allow switching to a node with no common ancestor")},
     {opt_force,
      N_("handle unversioned obstructions as changes")},
     {opt_relocate,N_("deprecated; use 'svn relocate'")}}
  },

  { "unlock", svn_cl__unlock, {0}, N_
    ("Unlock working copy paths or URLs.\n"
     "usage: unlock TARGET...\n"
     "\n"
     "  Use --force to break a lock held by another user or working copy.\n"),
    { opt_targets, opt_force, 'q' },
    {{opt_force, N_("break locks")}} },

  { "update", svn_cl__update, {"up"},  N_
    ("Bring changes from the repository into the working copy.\n"
     "usage: update [PATH...]\n"
     "\n"
     "  If no revision is given, bring working copy up-to-date with HEAD rev.\n"
     "  Else synchronize working copy to revision given by -r.\n"
     "\n"
     "  For each updated item a line will be printed with characters reporting\n"
     "  the action taken. These characters have the following meaning:\n"
     "\n"
     "    A  Added\n"
     "    D  Deleted\n"
     "    U  Updated\n"
     "    C  Conflict\n"
     "    G  Merged\n"
     "    E  Existed\n"
     "    R  Replaced\n"
     "\n"
     "  Characters in the first column report about the item itself.\n"
     "  Characters in the second column report about properties of the item.\n"
     "  A 'B' in the third column signifies that the lock for the file has\n"
     "  been broken or stolen.\n"
     "  A 'C' in the fourth column indicates a tree conflict, while a 'C' in\n"
     "  the first and second columns indicate textual conflicts in files\n"
     "  and in property values, respectively.\n"
     "\n"
     "  If --force is used, unversioned obstructing paths in the working\n"
     "  copy do not automatically cause a failure if the update attempts to\n"
     "  add the same path.  If the obstructing path is the same type (file\n"
     "  or directory) as the corresponding path in the repository it becomes\n"
     "  versioned but its contents are left 'as-is' in the working copy.\n"
     "  This means that an obstructing directory's unversioned children may\n"
     "  also obstruct and become versioned.  For files, any content differences\n"
     "  between the obstruction and the repository are treated like a local\n"
     "  modification to the working copy.  All properties from the repository\n"
     "  are applied to the obstructing path.  Obstructing paths are reported\n"
     "  in the first column with code 'E'.\n"
     "\n"
     "  If the specified update target is missing from the working copy but its\n"
     "  immediate parent directory is present, checkout the target into its\n"
     "  parent directory at the specified depth.  If --parents is specified,\n"
     "  create any missing parent directories of the target by checking them\n"
     "  out, too, at depth=empty.\n"
     "\n"
     "  Use the --set-depth option to set a new working copy depth on the\n"
     "  targets of this operation.\n"),
    {'r', 'N', opt_depth, opt_set_depth, 'q', opt_merge_cmd, opt_force,
     opt_ignore_externals, opt_changelist, opt_editor_cmd, opt_accept,
     opt_parents, opt_adds_as_modification},
    { {opt_force,
       N_("handle unversioned obstructions as changes")} } },

  { "upgrade", svn_cl__upgrade, {0}, N_
    ("Upgrade the metadata storage format for a working copy.\n"
     "usage: upgrade [WCPATH...]\n"
     "\n"
     "  Local modifications are preserved.\n"),
    { 'q' } },

  { "x-shelve", svn_cl__shelve, {"shelve"}, N_
    ("Put a local change aside, as if putting it on a shelf.\n"
     "usage: 1. x-shelve [--keep-local] NAME [PATH...]\n"
     "       2. x-shelve --delete NAME\n"
     "       3. x-shelve --list\n"
     "\n"
     "  1. Save the local change in the given PATHs to a patch file, and\n"
     "     revert that change from the WC unless '--keep-local' is given.\n"
     "     If a log message is given with '-m' or '-F', include it at the\n"
     "     beginning of the patch file.\n"
     "\n"
     "  2. Delete the shelved change NAME.\n"
     "     (A backup is kept, named with a '.bak' extension.)\n"
     "\n"
     "  3. List shelved changes. Include the first line of any log message\n"
     "     and some details about the contents of the change, unless '-q' is\n"
     "     given.\n"
     "\n"
     "  The kinds of change you can shelve are those supported by 'svn diff'\n"
     "  and 'svn patch'. The following are currently NOT supported:\n"
     "     mergeinfo changes, copies, moves, mkdir, rmdir,\n"
     "     'binary' content, uncommittable states\n"
     "\n"
     "  To bring back a shelved change, use 'svn x-unshelve NAME'.\n"
     "\n"
     "  Shelved changes are stored in <WC>/.svn/shelves/\n"
     "\n"
     "  The shelving feature is EXPERIMENTAL. This command is likely to change\n"
     "  in the next release, and there is no promise of backward compatibility.\n"
    ),
    {opt_delete, opt_list, 'q', opt_dry_run, opt_keep_local,
     opt_depth, opt_targets, opt_changelist,
     /* almost SVN_CL__LOG_MSG_OPTIONS but not currently opt_with_revprop: */
     'm', 'F', opt_force_log, opt_editor_cmd, opt_encoding,
    } },

  { "x-unshelve", svn_cl__unshelve, {"unshelve"}, N_
    ("Bring a shelved change back to a local change in the WC.\n"
     "usage: 1. x-unshelve [--keep-shelved] [NAME]\n"
     "       2. x-unshelve --list\n"
     "\n"
     "  1. Apply the shelved change NAME to the working copy.\n"
     "     Delete the patch unless the '--keep-shelved' option is given.\n"
     "     (A backup is kept, named with a '.bak' extension.)\n"
     "     NAME defaults to the most recent shelved change.\n"
     "\n"
     "  2. List shelved changes. Include the first line of any log message\n"
     "     and some details about the contents of the change, unless '-q' is\n"
     "     given.\n"
     "\n"
     "  Any conflict between the change being unshelved and a change\n"
     "  already in the WC is handled the same way as by 'svn patch',\n"
     "  creating a 'reject' file.\n"
     "\n"
     "  The shelving feature is EXPERIMENTAL. This command is likely to change\n"
     "  in the next release, and there is no promise of backward compatibility.\n"
    ),
    {opt_keep_shelved, opt_list, 'q', opt_dry_run} },

  { "x-shelves", svn_cl__shelves, {"shelves"}, N_
    ("List shelved changes.\n"
     "usage: x-shelves\n"
     "\n"
     "  The shelving feature is EXPERIMENTAL. This command is likely to change\n"
     "  in the next release, and there is no promise of backward compatibility.\n"
    ),
    {'q'} },

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
      { "svn_diff",   svn_diff_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}

/* The cancelation handler setup by the cmdline library. */
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

/* Add a --search-and argument to OPT_STATE.
 * These patterns are added to an existing pattern group, if any. */
static void
add_search_pattern_to_latest_group(svn_cl__opt_state_t *opt_state,
                                   const char *pattern,
                                   apr_pool_t *result_pool)
{
  apr_array_header_t *group;

  if (opt_state->search_patterns == NULL)
    {
      add_search_pattern_group(opt_state, pattern, result_pool);
      return;
    }

  group = APR_ARRAY_IDX(opt_state->search_patterns,
                        opt_state->search_patterns->nelts - 1,
                        apr_array_header_t *);
  APR_ARRAY_PUSH(group, const char *) = pattern;
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
  const char *dash_F_arg = NULL;
  svn_cl__cmd_baton_t command_baton;
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config;
  svn_boolean_t descend = TRUE;
  svn_boolean_t interactive_conflicts = FALSE;
  svn_boolean_t force_interactive = FALSE;
  svn_cl__conflict_stats_t *conflict_stats
    = svn_cl__conflict_stats_create(pool);
  svn_boolean_t use_notifier = TRUE;
  svn_boolean_t reading_file_from_stdin = FALSE;
  apr_hash_t *changelists;
  apr_hash_t *cfg_hash;
  svn_membuf_t buf;
  svn_boolean_t read_pass_from_stdin = FALSE;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

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

  /* Init our changelists hash. */
  changelists = apr_hash_make(pool);

  /* Init the temporary buffer. */
  svn_membuf__create(&buf, 0, pool);

  /* Begin processing arguments. */
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;
  opt_state.revision_ranges =
    apr_array_make(pool, 0, sizeof(svn_opt_revision_range_t *));
  opt_state.depth = svn_depth_unknown;
  opt_state.set_depth = svn_depth_unknown;
  opt_state.accept_which = svn_cl__accept_unspecified;
  opt_state.show_revs = svn_cl__show_revs_invalid;

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
          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          err = svn_cstring_atoi(&opt_state.limit, utf8_opt_arg);
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
      case 'm':
        /* We store the raw message here.  We will convert it to UTF-8
         * later, according to the value of the '--encoding' option. */
        opt_state.message = apr_pstrdup(pool, opt_arg);
        break;
      case 'c':
        {
          apr_array_header_t *change_revs;

          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          change_revs = svn_cstring_split(utf8_opt_arg, ", \n\r\t\v", TRUE,
                                          pool);

          if (opt_state.old_target)
            {
              return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                      _("Can't specify -c with --old"));
            }

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
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        if (svn_opt_parse_revision_to_range(opt_state.revision_ranges,
                                            utf8_opt_arg, pool) != 0)
          {
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Syntax error in revision argument '%s'"),
                 utf8_opt_arg);
          }
        break;
      case 'v':
        opt_state.verbose = TRUE;
        break;
      case 'u':
        opt_state.update = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case opt_incremental:
        opt_state.incremental = TRUE;
        break;
      case 'F':
        /* We read the raw file content here.  We will convert it to UTF-8
         * later (if it's a log/lock message or an svn:* prop value),
         * according to the value of the '--encoding' option. */
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_stringbuf_from_file2(&(opt_state.filedata),
                                         utf8_opt_arg, pool));
        reading_file_from_stdin = (strcmp(utf8_opt_arg, "-") == 0);
        dash_F_arg = utf8_opt_arg;
        break;
      case opt_targets:
        {
          svn_stringbuf_t *buffer, *buffer_utf8;

          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          SVN_ERR(svn_stringbuf_from_file2(&buffer, utf8_opt_arg, pool));
          SVN_ERR(svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool));
          opt_state.targets = svn_cstring_split(buffer_utf8->data, "\n\r",
                                                TRUE, pool);
        }
        break;
      case opt_force:
        opt_state.force = TRUE;
        break;
      case opt_force_log:
        opt_state.force_log = TRUE;
        break;
      case opt_dry_run:
        opt_state.dry_run = TRUE;
        break;
      case opt_list:
        opt_state.list = TRUE;
        break;
      case opt_revprop:
        opt_state.revprop = TRUE;
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
      case opt_set_depth:
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (err)
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                   _("Error converting depth "
                                     "from locale to UTF-8"));
        opt_state.set_depth = svn_depth_from_word(utf8_opt_arg);
        /* svn_depth_exclude is okay for --set-depth. */
        if (opt_state.set_depth == svn_depth_unknown)
          {
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("'%s' is not a valid depth; try "
                                       "'exclude', 'empty', 'files', "
                                       "'immediates', or 'infinity'"),
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
      case opt_encoding:
        opt_state.encoding = apr_pstrdup(pool, opt_arg);
        break;
      case opt_xml:
        opt_state.xml = TRUE;
        break;
      case opt_stop_on_copy:
        opt_state.stop_on_copy = TRUE;
        break;
      case opt_no_ignore:
        opt_state.no_ignore = TRUE;
        break;
      case opt_no_auth_cache:
        opt_state.no_auth_cache = TRUE;
        break;
      case opt_non_interactive:
        opt_state.non_interactive = TRUE;
        break;
      case opt_force_interactive:
        force_interactive = TRUE;
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
      case opt_no_diff_added:
        opt_state.diff.no_diff_added = TRUE;
        break;
      case opt_no_diff_deleted:
        opt_state.diff.no_diff_deleted = TRUE;
        break;
      case opt_ignore_properties:
        opt_state.diff.ignore_properties = TRUE;
        break;
      case opt_show_copies_as_adds:
        opt_state.diff.show_copies_as_adds = TRUE;
        break;
      case opt_notice_ancestry:
        opt_state.diff.notice_ancestry = TRUE;
        break;
      case opt_ignore_ancestry:
        opt_state.ignore_ancestry = TRUE;
        break;
      case opt_ignore_externals:
        opt_state.ignore_externals = TRUE;
        break;
      case opt_relocate:
        opt_state.relocate = TRUE;
        break;
      case 'x':
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.extensions,
                                        opt_arg, pool));
        break;
      case opt_diff_cmd:
        opt_state.diff.diff_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_merge_cmd:
        opt_state.merge_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_record_only:
        opt_state.record_only = TRUE;
        break;
      case opt_editor_cmd:
        opt_state.editor_cmd = apr_pstrdup(pool, opt_arg);
        break;
      case opt_old_cmd:
        if (opt_state.used_change_arg)
          {
            return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                    _("Can't specify -c with --old"));
          }
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.old_target = apr_pstrdup(pool, utf8_opt_arg);
        break;
      case opt_new_cmd:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.new_target = apr_pstrdup(pool, utf8_opt_arg);
        break;
      case opt_config_dir:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.config_dir = svn_dirent_internal_style(utf8_opt_arg, pool);
        break;
      case opt_config_options:
        if (!opt_state.config_options)
          opt_state.config_options =
                   apr_array_make(pool, 1,
                                  sizeof(svn_cmdline__config_argument_t*));

        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_cmdline__parse_config_option(opt_state.config_options,
                                                 utf8_opt_arg, "svn: ", pool));
        break;
      case opt_autoprops:
        opt_state.autoprops = TRUE;
        break;
      case opt_no_autoprops:
        opt_state.no_autoprops = TRUE;
        break;
      case opt_native_eol:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        if ( !strcmp("LF", utf8_opt_arg) || !strcmp("CR", utf8_opt_arg) ||
             !strcmp("CRLF", utf8_opt_arg))
          opt_state.native_eol = utf8_opt_arg;
        else
          {
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Syntax error in native-eol argument '%s'"),
                 utf8_opt_arg);
          }
        break;
      case opt_no_unlock:
        opt_state.no_unlock = TRUE;
        break;
      case opt_summarize:
        opt_state.diff.summarize = TRUE;
        break;
      case opt_remove:
      case opt_delete:
        opt_state.remove = TRUE;
        break;
      case opt_changelist:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        if (utf8_opt_arg[0] == '\0')
          {
            return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                    _("Changelist names must not be empty"));
          }
        svn_hash_sets(changelists, utf8_opt_arg, (void *)1);
        break;
      case opt_keep_changelists:
        opt_state.keep_changelists = TRUE;
        break;
      case opt_keep_local:
      case opt_keep_shelved:
        opt_state.keep_local = TRUE;
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
      case opt_parents:
        opt_state.parents = TRUE;
        break;
      case 'g':
        opt_state.use_merge_history = TRUE;
        break;
      case opt_accept:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.accept_which = svn_cl__accept_from_word(utf8_opt_arg);
        if (opt_state.accept_which == svn_cl__accept_invalid)
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("'%s' is not a valid --accept value"),
                                   utf8_opt_arg);
        break;
      case opt_show_revs:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.show_revs = svn_cl__show_revs_from_word(utf8_opt_arg);
        if (opt_state.show_revs == svn_cl__show_revs_invalid)
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("'%s' is not a valid --show-revs value"),
                                   utf8_opt_arg);
        break;
      case opt_mergeinfo_log:
        opt_state.mergeinfo_log = TRUE;
        break;
      case opt_reintegrate:
        opt_state.reintegrate = TRUE;
        break;
      case opt_strip:
        {
          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          err = svn_cstring_atoi(&opt_state.strip, utf8_opt_arg);
          if (err)
            {
              return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                       _("Invalid strip count '%s'"),
                                       utf8_opt_arg);
            }
          if (opt_state.strip < 0)
            {
              return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                      _("Argument to --strip must be positive"));
            }
        }
        break;
      case opt_ignore_keywords:
        opt_state.ignore_keywords = TRUE;
        break;
      case opt_reverse_diff:
        opt_state.reverse_diff = TRUE;
        break;
      case opt_ignore_whitespace:
          opt_state.ignore_whitespace = TRUE;
          break;
      case opt_diff:
          opt_state.show_diff = TRUE;
          break;
      case opt_internal_diff:
        opt_state.diff.internal_diff = TRUE;
        break;
      case opt_patch_compatible:
        opt_state.diff.patch_compatible = TRUE;
        break;
      case opt_use_git_diff_format:
        opt_state.diff.use_git_diff_format = TRUE;
        break;
      case opt_allow_mixed_revisions:
        opt_state.allow_mixed_rev = TRUE;
        break;
      case opt_include_externals:
        opt_state.include_externals = TRUE;
        break;
      case opt_show_inherited_props:
        opt_state.show_inherited_props = TRUE;
        break;
      case opt_properties_only:
        opt_state.diff.properties_only = TRUE;
        break;
      case opt_search:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_utf__xfrm(&utf8_opt_arg, utf8_opt_arg,
                              strlen(utf8_opt_arg), TRUE, TRUE, &buf));
        add_search_pattern_group(&opt_state,
                                 apr_pstrdup(pool, utf8_opt_arg),
                                 pool);
        break;
      case opt_search_and:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_utf__xfrm(&utf8_opt_arg, utf8_opt_arg,
                              strlen(utf8_opt_arg), TRUE, TRUE, &buf));
        add_search_pattern_to_latest_group(&opt_state,
                                           apr_pstrdup(pool, utf8_opt_arg),
                                           pool);
        break;
      case opt_remove_unversioned:
        opt_state.remove_unversioned = TRUE;
        break;
      case opt_remove_ignored:
        opt_state.remove_ignored = TRUE;
        break;
      case opt_no_newline:
      case opt_strict:          /* ### DEPRECATED */
        opt_state.no_newline = TRUE;
        break;
      case opt_show_passwords:
        opt_state.show_passwords = TRUE;
        break;
      case opt_pin_externals:
        opt_state.pin_externals = TRUE;
        break;
      case opt_show_item:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.show_item = utf8_opt_arg;
        break;
      case opt_adds_as_modification:
        opt_state.adds_as_modification = TRUE;
        break;
      case opt_vacuum_pristines:
        opt_state.vacuum_pristines = TRUE;
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;
      }
    }

  /* The --non-interactive and --force-interactive options are mutually
   * exclusive. */
  if (opt_state.non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }
  else
    opt_state.non_interactive = !svn_cmdline__be_interactive(
                                  opt_state.non_interactive,
                                  force_interactive);

  /* Turn our hash of changelists into an array of unique ones. */
  SVN_ERR(svn_hash_keys(&(opt_state.changelists), changelists, pool));

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
              svn_error_clear(svn_cl__help(NULL, NULL, pool));
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
              svn_error_clear(svn_cl__help(NULL, NULL, pool));

              /* Be kind to people who try 'svn undo'. */
              if (strcmp(first_arg, "undo") == 0)
                {
                  svn_error_clear
                    (svn_cmdline_fprintf(stderr, pool,
                                         _("Undo is done using either the "
                                           "'svn revert' or the 'svn merge' "
                                           "command.\n")));
                }

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
            svn_error_clear(svn_cl__help(NULL, NULL, pool));
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svn help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* Only merge and log support multiple revisions/revision ranges. */
  if (subcommand->cmd_func != svn_cl__merge
      && subcommand->cmd_func != svn_cl__log)
    {
      if (opt_state.revision_ranges->nelts > 1)
        {
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  _("Multiple revision arguments "
                                    "encountered; can't specify -c twice, "
                                    "or both -c and -r"));
        }
    }

  /* Disallow simultaneous use of both --depth and --set-depth. */
  if ((opt_state.depth != svn_depth_unknown)
      && (opt_state.set_depth != svn_depth_unknown))
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--depth and --set-depth are mutually "
                                "exclusive"));
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

#ifdef SVN_CL__OPTION_WITH_REVPROP_CAN_SET_PROPERTIES_IN_SVN_NAMESPACE
  /* XXX This is incomplete, since we do not yet check for --force, nor
     do all the commands that accept --with-revprop also accept --force. */

  /* Check the spelling of the revision properties given by --with-revprop. */
  if (opt_state.revprop_table)
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first(pool, opt_state.revprop_table);
           hi; hi = apr_hash_next(hi))
        {
          SVN_ERR(svn_cl__check_svn_prop_name(apr_hash_this_key(hi),
                                              TRUE, svn_cl__prop_use_use,
                                              pool));
        }
    }
#endif /* SVN_CL__OPTION_WITH_REVPROP_CAN_SET_PROPERTIES_IN_SVN_NAMESPACE */

  /* Disallow simultaneous use of both -m and -F, when they are
     both used to pass a commit message or lock comment.  ('propset'
     takes the property value, not a commit message, from -F.)
   */
  if (opt_state.filedata && opt_state.message
      && subcommand->cmd_func != svn_cl__propset)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--message (-m) and --file (-F) "
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

  /* Disallow simultaneous use of both --diff-cmd and
     --internal-diff.  */
  if (opt_state.diff.diff_cmd && opt_state.diff.internal_diff)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--diff-cmd and --internal-diff "
                                "are mutually exclusive"));
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

  err = svn_config_get_config(&cfg_hash, opt_state.config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svn: ");
          svn_error_clear(err);

          SVN_ERR(svn_config__get_default_config(&cfg_hash, pool));
        }
      else
        return err;
    }

  /* Relocation is infinite-depth only. */
  if (opt_state.relocate)
    {
      if (opt_state.depth != svn_depth_unknown)
        {
          return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                  _("--relocate and --depth are mutually "
                                    "exclusive"));
        }
      if (! descend)
        {
          return svn_error_create(
                    SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                    _("--relocate and --non-recursive (-N) are mutually "
                      "exclusive"));
        }
    }

  /* Only a few commands can accept a revision range; the rest can take at
     most one revision number. */
  if (subcommand->cmd_func != svn_cl__blame
      && subcommand->cmd_func != svn_cl__diff
      && subcommand->cmd_func != svn_cl__log
      && subcommand->cmd_func != svn_cl__mergeinfo
      && subcommand->cmd_func != svn_cl__merge)
    {
      if (opt_state.end_revision.kind != svn_opt_revision_unspecified)
        {
          return svn_error_create(SVN_ERR_CLIENT_REVISION_RANGE, NULL, NULL);
        }
    }

  /* -N has a different meaning depending on the command */
  if (!descend)
    {
      if (subcommand->cmd_func == svn_cl__status)
        {
          opt_state.depth = svn_depth_immediates;
        }
      else if (subcommand->cmd_func == svn_cl__revert
               || subcommand->cmd_func == svn_cl__add
               || subcommand->cmd_func == svn_cl__commit)
        {
          /* In pre-1.5 Subversion, some commands treated -N like
             --depth=empty, so force that mapping here.  Anyway, with
             revert it makes sense to be especially conservative,
             since revert can lose data. */
          opt_state.depth = svn_depth_empty;
        }
      else
        {
          opt_state.depth = svn_depth_files;
        }
    }

  /* Update the options in the config */
  if (opt_state.config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(cfg_hash,
                                            opt_state.config_options,
                                            "svn: ", "--config-option"));
    }

  cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);
#if !defined(SVN_CL_NO_EXCLUSIVE_LOCK)
  {
    const char *exclusive_clients_option;
    apr_array_header_t *exclusive_clients;

    svn_config_get(cfg_config, &exclusive_clients_option,
                   SVN_CONFIG_SECTION_WORKING_COPY,
                   SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE_CLIENTS,
                   NULL);
    exclusive_clients = svn_cstring_split(exclusive_clients_option,
                                          " ,", TRUE, pool);
    for (i = 0; i < exclusive_clients->nelts; ++i)
      {
        const char *exclusive_client = APR_ARRAY_IDX(exclusive_clients, i,
                                                     const char *);

        /* This blocks other clients from accessing the wc.db so it must
           be explicitly enabled.*/
        if (!strcmp(exclusive_client, "svn"))
          svn_config_set(cfg_config,
                         SVN_CONFIG_SECTION_WORKING_COPY,
                         SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE,
                         "true");
      }
  }
#endif

  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  command_baton.conflict_stats = conflict_stats;
  SVN_ERR(svn_client_create_context2(&ctx, cfg_hash, pool));
  command_baton.ctx = ctx;

  /* If we're running a command that could result in a commit, verify
     that any log message we were given on the command line makes
     sense (unless we've also been instructed not to care).  This may
     access the working copy so do it after setting the locking mode. */
  if ((! opt_state.force_log)
      && (subcommand->cmd_func == svn_cl__commit
          || subcommand->cmd_func == svn_cl__copy
          || subcommand->cmd_func == svn_cl__delete
          || subcommand->cmd_func == svn_cl__import
          || subcommand->cmd_func == svn_cl__mkdir
          || subcommand->cmd_func == svn_cl__move
          || subcommand->cmd_func == svn_cl__lock
          || subcommand->cmd_func == svn_cl__propedit
          || subcommand->cmd_func == svn_cl__shelve))
    {
      /* If the -F argument is a file that's under revision control,
         that's probably not what the user intended. */
      if (dash_F_arg)
        {
          svn_node_kind_t kind;
          const char *local_abspath;
          const char *fname = svn_dirent_internal_style(dash_F_arg, pool);

          err = svn_dirent_get_absolute(&local_abspath, fname, pool);

          if (!err)
            {
              err = svn_wc_read_kind2(&kind, ctx->wc_ctx, local_abspath, TRUE,
                                      FALSE, pool);

              if (!err && kind != svn_node_none && kind != svn_node_unknown)
                {
                  if (subcommand->cmd_func != svn_cl__lock)
                    {
                      return svn_error_create(
                         SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE, NULL,
                         _("Log message file is a versioned file; "
                           "use '--force-log' to override"));
                    }
                  else
                    {
                      return svn_error_create(
                         SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE, NULL,
                         _("Lock comment file is a versioned file; "
                           "use '--force-log' to override"));
                    }
                }
            }
          svn_error_clear(err);
        }

      /* If the -m argument is a file at all, that's probably not what
         the user intended. */
      if (opt_state.message)
        {
          apr_finfo_t finfo;
          if (apr_stat(&finfo, opt_state.message /* not converted to UTF-8 */,
                       APR_FINFO_MIN, pool) == APR_SUCCESS)
            {
              if (subcommand->cmd_func != svn_cl__lock)
                {
                  return svn_error_create
                    (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME, NULL,
                     _("The log message is a pathname "
                       "(was -F intended?); use '--force-log' to override"));
                }
              else
                {
                  return svn_error_create
                    (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME, NULL,
                     _("The lock comment is a pathname "
                       "(was -F intended?); use '--force-log' to override"));
                }
            }
        }
    }

  /* XXX: Only diff_cmd for now, overlay rest later and stop passing
     opt_state altogether? */
  if (opt_state.diff.diff_cmd)
    svn_config_set(cfg_config, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF_CMD, opt_state.diff.diff_cmd);
  if (opt_state.merge_cmd)
    svn_config_set(cfg_config, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF3_CMD, opt_state.merge_cmd);
  if (opt_state.diff.internal_diff)
    svn_config_set(cfg_config, SVN_CONFIG_SECTION_HELPERS,
                   SVN_CONFIG_OPTION_DIFF_CMD, NULL);

  /* Check for mutually exclusive args --auto-props and --no-auto-props */
  if (opt_state.autoprops && opt_state.no_autoprops)
    {
      return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                              _("--auto-props and --no-auto-props are "
                                "mutually exclusive"));
    }

  /* Update auto-props-enable option, and populate the MIME types map,
     for add/import commands */
  if (subcommand->cmd_func == svn_cl__add
      || subcommand->cmd_func == svn_cl__import)
    {
      const char *mimetypes_file;
      svn_config_get(cfg_config, &mimetypes_file,
                     SVN_CONFIG_SECTION_MISCELLANY,
                     SVN_CONFIG_OPTION_MIMETYPES_FILE, FALSE);
      if (mimetypes_file && *mimetypes_file)
        {
          SVN_ERR(svn_io_parse_mimetypes_file(&(ctx->mimetypes_map),
                                              mimetypes_file, pool));
        }

      if (opt_state.autoprops)
        {
          svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, TRUE);
        }
      if (opt_state.no_autoprops)
        {
          svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS, FALSE);
        }
    }

  /* Update the 'keep-locks' runtime option */
  if (opt_state.no_unlock)
    svn_config_set_bool(cfg_config, SVN_CONFIG_SECTION_MISCELLANY,
                        SVN_CONFIG_OPTION_NO_UNLOCK, TRUE);

  /* Set the log message callback function.  Note that individual
     subcommands will populate the ctx->log_msg_baton3. */
  ctx->log_msg_func3 = svn_cl__get_log_message;

  /* Set up the notifier.

     In general, we use it any time we aren't in --quiet mode.  'svn
     status' is unique, though, in that we don't want it in --quiet mode
     unless we're also in --verbose mode.  When in --xml mode,
     though, we never want it.  */
  if (opt_state.quiet)
    use_notifier = FALSE;
  if ((subcommand->cmd_func == svn_cl__status) && opt_state.verbose)
    use_notifier = TRUE;
  if (opt_state.xml)
    use_notifier = FALSE;
  if (use_notifier)
    {
      SVN_ERR(svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2,
                                   conflict_stats, pool));
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

  if (opt_state.non_interactive)
    {
      if (opt_state.accept_which == svn_cl__accept_edit)
        {
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("--accept=%s incompatible with"
                                     " --non-interactive"),
                                   SVN_CL__ACCEPT_EDIT);
        }
      if (opt_state.accept_which == svn_cl__accept_launch)
        {
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("--accept=%s incompatible with"
                                     " --non-interactive"),
                                   SVN_CL__ACCEPT_LAUNCH);
        }

      /* The default action when we're non-interactive is to use the
       * recommended conflict resolution (this will postpone conflicts
       * for which no recommended resolution is available). */
      if (opt_state.accept_which == svn_cl__accept_unspecified)
        opt_state.accept_which = svn_cl__accept_recommended;
    }

  /* Check whether interactive conflict resolution is disabled by
   * the configuration file. If no --accept option was specified
   * we postpone all conflicts in this case. */
  SVN_ERR(svn_config_get_bool(cfg_config, &interactive_conflicts,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_INTERACTIVE_CONFLICTS,
                              TRUE));
  if (!interactive_conflicts)
    {
      /* Make 'svn resolve' non-interactive. */
      if (subcommand->cmd_func == svn_cl__resolve)
        opt_state.non_interactive = TRUE;

      /* We're not resolving conflicts interactively. If no --accept option
       * was provided the default behaviour is to postpone all conflicts. */
      if (opt_state.accept_which == svn_cl__accept_unspecified)
        opt_state.accept_which = svn_cl__accept_postpone;
    }

  /* We don't use legacy libsvn_wc conflict handlers by default. */
  {
    ctx->conflict_func = NULL;
    ctx->conflict_baton = NULL;
    ctx->conflict_func2 = NULL;
    ctx->conflict_baton2 = NULL;
  }

  /* And now we finally run the subcommand. */
  err = (*subcommand->cmd_func)(os, &command_baton, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrapf(
                  err, _("Try 'svn help %s' for more information"),
                  subcommand->name);
        }
      if (err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
        {
          err = svn_error_quick_wrap(err,
                                     _("Please see the 'svn upgrade' command"));
        }

      if (err->apr_err == SVN_ERR_AUTHN_FAILED && opt_state.non_interactive)
        {
          err = svn_error_quick_wrap(err,
                                     _("Authentication failed and interactive"
                                       " prompting is disabled; see the"
                                       " --force-interactive option"));
          if (reading_file_from_stdin)
            err = svn_error_quick_wrap(err,
                                       _("Reading file from standard input "
                                         "because of -F option; this can "
                                         "interfere with interactive "
                                         "prompting"));
        }

      /* Tell the user about 'svn cleanup' if any error on the stack
         was about locked working copies. */
      if (svn_error_find_cause(err, SVN_ERR_WC_LOCKED))
        {
          err = svn_error_quick_wrap(
                  err, _("Run 'svn cleanup' to remove locks "
                         "(type 'svn help cleanup' for details)"));
        }

      if (err->apr_err == SVN_ERR_SQLITE_BUSY)
        {
          err = svn_error_quick_wrap(err,
                                     _("Another process is blocking the "
                                       "working copy database, or the "
                                       "underlying filesystem does not "
                                       "support file locking; if the working "
                                       "copy is on a network filesystem, make "
                                       "sure file locking has been enabled "
                                       "on the file server"));
        }

      if (svn_error_find_cause(err, SVN_ERR_RA_CANNOT_CREATE_TUNNEL) &&
          (opt_state.auth_username || opt_state.auth_password))
        {
          err = svn_error_quick_wrap(
                  err, _("When using svn+ssh:// URLs, keep in mind that the "
                         "--username and --password options are ignored "
                         "because authentication is performed by SSH, not "
                         "Subversion"));
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
  if (svn_cmdline_init("svn", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svn: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
