/* repos.c : repository creation; shared and exclusive repository locking
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

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_time.h"
#include "svn_fs.h"
#include "svn_ra.h"  /* for SVN_RA_CAPABILITY_* */
#include "svn_repos.h"
#include "svn_hash.h"
#include "svn_version.h"
#include "svn_config.h"

#include "private/svn_repos_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h" /* for SVN_TEMPLATE_ROOT_DIR */

#include "repos.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR


/* Path accessor functions. */


const char *
svn_repos_path(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->path);
}


const char *
svn_repos_db_env(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->db_path);
}


const char *
svn_repos_conf_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->conf_path);
}


const char *
svn_repos_svnserve_conf(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->conf_path, SVN_REPOS__CONF_SVNSERVE_CONF, pool);
}


const char *
svn_repos_lock_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->lock_path);
}


const char *
svn_repos_db_lockfile(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->lock_path, SVN_REPOS__DB_LOCKFILE, pool);
}


const char *
svn_repos_db_logs_lockfile(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->lock_path, SVN_REPOS__DB_LOGS_LOCKFILE, pool);
}

const char *
svn_repos_hook_dir(svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup(pool, repos->hook_path);
}


const char *
svn_repos_start_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_START_COMMIT, pool);
}


const char *
svn_repos_pre_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_PRE_COMMIT, pool);
}


const char *
svn_repos_pre_lock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_PRE_LOCK, pool);
}


const char *
svn_repos_pre_unlock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_PRE_UNLOCK, pool);
}

const char *
svn_repos_post_lock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_POST_LOCK, pool);
}


const char *
svn_repos_post_unlock_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_POST_UNLOCK, pool);
}


const char *
svn_repos_post_commit_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_POST_COMMIT, pool);
}


const char *
svn_repos_pre_revprop_change_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_PRE_REVPROP_CHANGE,
                       pool);
}


const char *
svn_repos_post_revprop_change_hook(svn_repos_t *repos, apr_pool_t *pool)
{
  return svn_dirent_join(repos->hook_path, SVN_REPOS__HOOK_POST_REVPROP_CHANGE,
                       pool);
}

static svn_error_t *
create_repos_dir(const char *path, apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
  if (err && (APR_STATUS_IS_EEXIST(err->apr_err)))
    {
      svn_boolean_t is_empty;

      svn_error_clear(err);

      SVN_ERR(svn_io_dir_empty(&is_empty, path, pool));

      if (is_empty)
        err = NULL;
      else
        err = svn_error_createf(SVN_ERR_DIR_NOT_EMPTY, 0,
                                _("'%s' exists and is non-empty"),
                                svn_dirent_local_style(path, pool));
    }

  return svn_error_trace(err);
}

static const char * bdb_lock_file_contents =
  "DB lock file, representing locks on the versioned filesystem."            NL
  ""                                                                         NL
  "All accessors -- both readers and writers -- of the repository's"         NL
  "Berkeley DB environment take out shared locks on this file, and"          NL
  "each accessor removes its lock when done.  If and when the DB"            NL
  "recovery procedure is run, the recovery code takes out an"                NL
  "exclusive lock on this file, so we can be sure no one else is"            NL
  "using the DB during the recovery."                                        NL
  ""                                                                         NL
  "You should never have to edit or remove this file."                       NL;

static const char * bdb_logs_lock_file_contents =
  "DB logs lock file, representing locks on the versioned filesystem logs."  NL
  ""                                                                         NL
  "All log manipulators of the repository's Berkeley DB environment"         NL
  "take out exclusive locks on this file to ensure that only one"            NL
  "accessor manipulates the logs at a time."                                 NL
  ""                                                                         NL
  "You should never have to edit or remove this file."                       NL;

static const char * pre12_compat_unneeded_file_contents =
  "This file is not used by Subversion 1.3.x or later."                      NL
  "However, its existence is required for compatibility with"                NL
  "Subversion 1.2.x or earlier."                                             NL;

/* Create the DB logs lockfile. */
static svn_error_t *
create_db_logs_lock(svn_repos_t *repos, apr_pool_t *pool) {
  const char *contents;
  const char *lockfile_path;

  lockfile_path = svn_repos_db_logs_lockfile(repos, pool);
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    contents = bdb_logs_lock_file_contents;
  else
    contents = pre12_compat_unneeded_file_contents;

  SVN_ERR_W(svn_io_file_create(lockfile_path, contents, pool),
            _("Creating db logs lock file"));

  return SVN_NO_ERROR;
}

/* Create the DB lockfile. */
static svn_error_t *
create_db_lock(svn_repos_t *repos, apr_pool_t *pool) {
  const char *contents;
  const char *lockfile_path;

  lockfile_path = svn_repos_db_lockfile(repos, pool);
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    contents = bdb_lock_file_contents;
  else
    contents = pre12_compat_unneeded_file_contents;

  SVN_ERR_W(svn_io_file_create(lockfile_path, contents, pool),
            _("Creating db lock file"));

  return SVN_NO_ERROR;
}

static svn_error_t *
create_locks(svn_repos_t *repos, apr_pool_t *pool)
{
  /* Create the locks directory. */
  SVN_ERR_W(create_repos_dir(repos->lock_path, pool),
            _("Creating lock dir"));

  SVN_ERR(create_db_lock(repos, pool));
  return create_db_logs_lock(repos, pool);
}


#define HOOKS_ENVIRONMENT_TEXT                                                \
  "# The hook program runs in an empty environment, unless the server is"  NL \
  "# explicitly configured otherwise.  For example, a common problem is for" NL \
  "# the PATH environment variable to not be set to its usual value, so"   NL \
  "# that subprograms fail to launch unless invoked via absolute path."    NL \
  "# If you're having unexpected problems with a hook program, the"        NL \
  "# culprit may be unusual (or missing) environment variables."           NL

#define PREWRITTEN_HOOKS_TEXT                                                 \
  "# For more examples and pre-written hooks, see those in"                NL \
  "# the Subversion repository at"                                         NL \
  "# http://svn.apache.org/repos/asf/subversion/trunk/tools/hook-scripts/ and"        NL \
  "# http://svn.apache.org/repos/asf/subversion/trunk/contrib/hook-scripts/"          NL

#define HOOKS_QUOTE_ARGUMENTS_TEXT                                            \
  "# CAUTION:"                                                             NL \
  "# For security reasons, you MUST always properly quote arguments when"  NL \
  "# you use them, as those arguments could contain whitespace or other"   NL \
  "# problematic characters. Additionally, you should delimit the list"    NL \
  "# of options with \"--\" before passing the arguments, so malicious"    NL \
  "# clients cannot bootleg unexpected options to the commands your"       NL \
  "# script aims to execute."                                              NL \
  "# For similar reasons, you should also add a trailing @ to URLs which"  NL \
  "# are passed to SVN commands accepting URLs with peg revisions."        NL

/* Return template text for a hook script named SCRIPT_NAME. Include
 * DESCRIPTION and SCRIPT in the template text.
 */
static const char *
hook_template_text(const char *script_name,
                   const char *description,
                   const char *script,
                   apr_pool_t *result_pool)
{
  return apr_pstrcat(result_pool,
"#!/bin/sh"                                                                  NL
""                                                                           NL,
                     description,
"#"                                                                          NL
"# The default working directory for the invocation is undefined, so"        NL
"# the program should set one explicitly if it cares."                       NL
"#"                                                                          NL
"# On a Unix system, the normal procedure is to have '", script_name, "'"    NL
"# invoke other programs to do the real work, though it may do the"          NL
"# work itself too."                                                         NL
"#"                                                                          NL
"# Note that '", script_name, "' must be executable by the user(s) who will" NL
"# invoke it (typically the user httpd runs as), and that user must"         NL
"# have filesystem-level permission to access the repository."               NL
"#"                                                                          NL
"# On a Windows system, you should name the hook program"                    NL
"# '", script_name, ".bat' or '", script_name, ".exe',"                                                    NL
"# but the basic idea is the same."                                          NL
"#"                                                                          NL
HOOKS_ENVIRONMENT_TEXT
"#"                                                                          NL
HOOKS_QUOTE_ARGUMENTS_TEXT
"#"                                                                          NL
"# Here is an example hook script, for a Unix /bin/sh interpreter."          NL
PREWRITTEN_HOOKS_TEXT
""                                                                           NL
""                                                                           NL,
                     script,
                     SVN_VA_NULL);
}

/* Write a template file for a hook script named SCRIPT_NAME (appending
 * '.tmpl' to that name) in REPOS. Include DESCRIPTION and SCRIPT in the
 * template text.
 */
static svn_error_t *
write_hook_template_file(svn_repos_t *repos, const char *script_name,
                         const char *description,
                         const char *script,
                         apr_pool_t *pool)
{
  const char *template_path
    = svn_dirent_join(repos->hook_path,
                      apr_psprintf(pool, "%s%s",
                                   script_name, SVN_REPOS__HOOK_DESC_EXT),
                      pool);
  const char *contents
    = hook_template_text(script_name, description, script, pool);

  SVN_ERR(svn_io_file_create(template_path, contents, pool));
  SVN_ERR(svn_io_set_file_executable(template_path, TRUE, FALSE, pool));
  return SVN_NO_ERROR;
}

/* Write the hook template files in REPOS.
 */
static svn_error_t *
create_hooks(svn_repos_t *repos, apr_pool_t *pool)
{
  const char *description, *script;

  /* Create the hook directory. */
  SVN_ERR_W(create_repos_dir(repos->hook_path, pool),
            _("Creating hook directory"));

  /*** Write a default template for each standard hook file. */

  /* Start-commit hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_START_COMMIT

  description =
"# START-COMMIT HOOK"                                                        NL
"#"                                                                          NL
"# The start-commit hook is invoked immediately after a Subversion txn is"   NL
"# created and populated with initial revprops in the process of doing a"    NL
"# commit. Subversion runs this hook by invoking a program (script, "        NL
"# executable, binary, etc.) named '"SCRIPT_NAME"' (for which this file"     NL
"# is a template) with the following ordered arguments:"                     NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] USER         (the authenticated user attempting to commit)"         NL
"#   [3] CAPABILITIES (a colon-separated list of capabilities reported"      NL
"#                     by the client; see note below)"                       NL
"#   [4] TXN-NAME     (the name of the commit txn just created)"             NL
"#"                                                                          NL
"# Note: The CAPABILITIES parameter is new in Subversion 1.5, and 1.5"       NL
"# clients will typically report at least the \""                            \
   SVN_RA_CAPABILITY_MERGEINFO "\" capability."                              NL
"# If there are other capabilities, then the list is colon-separated,"       NL
"# e.g.: \"" SVN_RA_CAPABILITY_MERGEINFO ":some-other-capability\" "         \
  "(the order is undefined)."                                                NL
"#"                                                                          NL
"# The list is self-reported by the client.  Therefore, you should not"      NL
"# make security assumptions based on the capabilities list, nor should"     NL
"# you assume that clients reliably report every capability they have."      NL
"#"                                                                          NL
"# Note: The TXN-NAME parameter is new in Subversion 1.8.  Prior to version" NL
"# 1.8, the start-commit hook was invoked before the commit txn was even"    NL
"# created, so the ability to inspect the commit txn and its metadata from"  NL
"# within the start-commit hook was not possible."                           NL
"# "                                                                         NL
"# If the hook program exits with success, the commit continues; but"        NL
"# if it exits with failure (non-zero), the commit is stopped before"        NL
"# a Subversion txn is created, and STDERR is returned to the client."       NL;
  script =
"REPOS=\"$1\""                                                               NL
"USER=\"$2\""                                                                NL
""                                                                           NL
"commit-allower.pl --repository \"$REPOS\" --user \"$USER\" || exit 1"       NL
"special-auth-check.py --user \"$USER\" --auth-level 3 || exit 1"            NL
""                                                                           NL
"# All checks passed, so allow the commit."                                  NL
"exit 0"                                                                     NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating start-commit hook"));

#undef SCRIPT_NAME


  /* Pre-commit hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_PRE_COMMIT

  description =
"# PRE-COMMIT HOOK"                                                          NL
"#"                                                                          NL
"# The pre-commit hook is invoked before a Subversion txn is"                NL
"# committed.  Subversion runs this hook by invoking a program"              NL
"# (script, executable, binary, etc.) named '"SCRIPT_NAME"' (for which"      NL
"# this file is a template), with the following ordered arguments:"          NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] TXN-NAME     (the name of the txn about to be committed)"           NL
"#"                                                                          NL
"#   [STDIN] LOCK-TOKENS ** the lock tokens are passed via STDIN."           NL
"#"                                                                          NL
"#   If STDIN contains the line \"LOCK-TOKENS:\\n\" (the \"\\n\" denotes a"  NL
"#   single newline), the lines following it are the lock tokens for"        NL
"#   this commit.  The end of the list is marked by a line containing"       NL
"#   only a newline character."                                              NL
"#"                                                                          NL
"#   Each lock token line consists of a URI-escaped path, followed"          NL
"#   by the separator character '|', followed by the lock token string,"     NL
"#   followed by a newline."                                                 NL
"#"                                                                          NL
"# If the hook program exits with success, the txn is committed; but"        NL
"# if it exits with failure (non-zero), the txn is aborted, no commit"       NL
"# takes place, and STDERR is returned to the client.   The hook"            NL
"# program can use the 'svnlook' utility to help it examine the txn."        NL
"#"                                                                          NL
"#   ***  NOTE: THE HOOK PROGRAM MUST NOT MODIFY THE TXN, EXCEPT  ***"       NL
"#   ***  FOR REVISION PROPERTIES (like svn:log or svn:author).   ***"       NL
"#"                                                                          NL
"#   This is why we recommend using the read-only 'svnlook' utility."        NL
"#   In the future, Subversion may enforce the rule that pre-commit"         NL
"#   hooks should not modify the versioned data in txns, or else come"       NL
"#   up with a mechanism to make it safe to do so (by informing the"         NL
"#   committing client of the changes).  However, right now neither"         NL
"#   mechanism is implemented, so hook writers just have to be careful."     NL;
  script =
"REPOS=\"$1\""                                                               NL
"TXN=\"$2\""                                                                 NL
""                                                                           NL
"# Make sure that the log message contains some text."                       NL
"SVNLOOK=" SVN_BINDIR "/svnlook"                                             NL
"$SVNLOOK log -t \"$TXN\" \"$REPOS\" | \\"                                   NL
"   grep \"[a-zA-Z0-9]\" > /dev/null || exit 1"                              NL
""                                                                           NL
"# Check that the author of this commit has the rights to perform"           NL
"# the commit on the files and directories being modified."                  NL
"commit-access-control.pl \"$REPOS\" \"$TXN\" commit-access-control.cfg || exit 1"
                                                                             NL
""                                                                           NL
"# All checks passed, so allow the commit."                                  NL
"exit 0"                                                                     NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating pre-commit hook"));

#undef SCRIPT_NAME


  /* Pre-revprop-change hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_PRE_REVPROP_CHANGE

  description =
"# PRE-REVPROP-CHANGE HOOK"                                                  NL
"#"                                                                          NL
"# The pre-revprop-change hook is invoked before a revision property"        NL
"# is added, modified or deleted.  Subversion runs this hook by invoking"    NL
"# a program (script, executable, binary, etc.) named '"SCRIPT_NAME"'"       NL
"# (for which this file is a template), with the following ordered"          NL
"# arguments:"                                                               NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] REV          (the revision being tweaked)"                          NL
"#   [3] USER         (the username of the person tweaking the property)"    NL
"#   [4] PROPNAME     (the property being set on the revision)"              NL
"#   [5] ACTION       (the property is being 'A'dded, 'M'odified, or 'D'eleted)"
                                                                             NL
"#"                                                                          NL
"#   [STDIN] PROPVAL  ** the new property value is passed via STDIN."        NL
"#"                                                                          NL
"# If the hook program exits with success, the propchange happens; but"      NL
"# if it exits with failure (non-zero), the propchange doesn't happen."      NL
"# The hook program can use the 'svnlook' utility to examine the "           NL
"# existing value of the revision property."                                 NL
"#"                                                                          NL
"# WARNING: unlike other hooks, this hook MUST exist for revision"           NL
"# properties to be changed.  If the hook does not exist, Subversion "       NL
"# will behave as if the hook were present, but failed.  The reason"         NL
"# for this is that revision properties are UNVERSIONED, meaning that"       NL
"# a successful propchange is destructive;  the old value is gone"           NL
"# forever.  We recommend the hook back up the old value somewhere."         NL;
  script =
"REPOS=\"$1\""                                                               NL
"REV=\"$2\""                                                                 NL
"USER=\"$3\""                                                                NL
"PROPNAME=\"$4\""                                                            NL
"ACTION=\"$5\""                                                              NL
""                                                                           NL
"if [ \"$ACTION\" = \"M\" -a \"$PROPNAME\" = \"svn:log\" ]; then exit 0; fi" NL
""                                                                           NL
"echo \"Changing revision properties other than svn:log is prohibited\" >&2" NL
"exit 1"                                                                     NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating pre-revprop-change hook"));

#undef SCRIPT_NAME


  /* Pre-lock hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_PRE_LOCK

  description =
"# PRE-LOCK HOOK"                                                            NL
"#"                                                                          NL
"# The pre-lock hook is invoked before an exclusive lock is"                 NL
"# created.  Subversion runs this hook by invoking a program "               NL
"# (script, executable, binary, etc.) named '"SCRIPT_NAME"' (for which"      NL
"# this file is a template), with the following ordered arguments:"          NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] PATH         (the path in the repository about to be locked)"       NL
"#   [3] USER         (the user creating the lock)"                          NL
"#   [4] COMMENT      (the comment of the lock)"                             NL
"#   [5] STEAL-LOCK   (1 if the user is trying to steal the lock, else 0)"   NL
"#"                                                                          NL
"# If the hook program outputs anything on stdout, the output string will"   NL
"# be used as the lock token for this lock operation.  If you choose to use" NL
"# this feature, you must guarantee the tokens generated are unique across"  NL
"# the repository each time."                                                NL
"#"                                                                          NL
"# If the hook program exits with success, the lock is created; but"         NL
"# if it exits with failure (non-zero), the lock action is aborted"          NL
"# and STDERR is returned to the client."                                    NL;
  script =
"REPOS=\"$1\""                                                               NL
"PATH=\"$2\""                                                                NL
"USER=\"$3\""                                                                NL
"COMMENT=\"$4\""                                                             NL
"STEAL=\"$5\""                                                               NL
""                                                                           NL
"# If a lock exists and is owned by a different person, don't allow it"      NL
"# to be stolen (e.g., with 'svn lock --force ...')."                        NL
""                                                                           NL
"# (Maybe this script could send email to the lock owner?)"                  NL
"SVNLOOK=" SVN_BINDIR "/svnlook"                                             NL
"GREP=/bin/grep"                                                             NL
"SED=/bin/sed"                                                               NL
""                                                                           NL
"LOCK_OWNER=`$SVNLOOK lock \"$REPOS\" \"$PATH\" | \\"                        NL
"            $GREP '^Owner: ' | $SED 's/Owner: //'`"                         NL
""                                                                           NL
"# If we get no result from svnlook, there's no lock, allow the lock to"     NL
"# happen:"                                                                  NL
"if [ \"$LOCK_OWNER\" = \"\" ]; then"                                        NL
"  exit 0"                                                                   NL
"fi"                                                                         NL
""                                                                           NL
"# If the person locking matches the lock's owner, allow the lock to"        NL
"# happen:"                                                                  NL
"if [ \"$LOCK_OWNER\" = \"$USER\" ]; then"                                   NL
"  exit 0"                                                                   NL
"fi"                                                                         NL
""                                                                           NL
"# Otherwise, we've got an owner mismatch, so return failure:"               NL
"echo \"Error: $PATH already locked by ${LOCK_OWNER}.\" 1>&2"                NL
"exit 1"                                                                     NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating pre-lock hook"));

#undef SCRIPT_NAME


  /* Pre-unlock hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_PRE_UNLOCK

  description =
"# PRE-UNLOCK HOOK"                                                          NL
"#"                                                                          NL
"# The pre-unlock hook is invoked before an exclusive lock is"               NL
"# destroyed.  Subversion runs this hook by invoking a program "             NL
"# (script, executable, binary, etc.) named '"SCRIPT_NAME"' (for which"      NL
"# this file is a template), with the following ordered arguments:"          NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] PATH         (the path in the repository about to be unlocked)"     NL
"#   [3] USER         (the user destroying the lock)"                        NL
"#   [4] TOKEN        (the lock token to be destroyed)"                      NL
"#   [5] BREAK-UNLOCK (1 if the user is breaking the lock, else 0)"          NL
"#"                                                                          NL
"# If the hook program exits with success, the lock is destroyed; but"       NL
"# if it exits with failure (non-zero), the unlock action is aborted"        NL
"# and STDERR is returned to the client."                                    NL;
  script =
"REPOS=\"$1\""                                                               NL
"PATH=\"$2\""                                                                NL
"USER=\"$3\""                                                                NL
"TOKEN=\"$4\""                                                               NL
"BREAK=\"$5\""                                                               NL
""                                                                           NL
"# If a lock is owned by a different person, don't allow it be broken."      NL
"# (Maybe this script could send email to the lock owner?)"                  NL
""                                                                           NL
"SVNLOOK=" SVN_BINDIR "/svnlook"                                             NL
"GREP=/bin/grep"                                                             NL
"SED=/bin/sed"                                                               NL
""                                                                           NL
"LOCK_OWNER=`$SVNLOOK lock \"$REPOS\" \"$PATH\" | \\"                        NL
"            $GREP '^Owner: ' | $SED 's/Owner: //'`"                         NL
""                                                                           NL
"# If we get no result from svnlook, there's no lock, return success:"       NL
"if [ \"$LOCK_OWNER\" = \"\" ]; then"                                        NL
"  exit 0"                                                                   NL
"fi"                                                                         NL
""                                                                           NL
"# If the person unlocking matches the lock's owner, return success:"        NL
"if [ \"$LOCK_OWNER\" = \"$USER\" ]; then"                                   NL
"  exit 0"                                                                   NL
"fi"                                                                         NL
""                                                                           NL
"# Otherwise, we've got an owner mismatch, so return failure:"               NL
"echo \"Error: $PATH locked by ${LOCK_OWNER}.\" 1>&2"                        NL
"exit 1"                                                                     NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating pre-unlock hook"));

#undef SCRIPT_NAME


  /* Post-commit hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_POST_COMMIT

  description =
"# POST-COMMIT HOOK"                                                         NL
"#"                                                                          NL
"# The post-commit hook is invoked after a commit.  Subversion runs"         NL
"# this hook by invoking a program (script, executable, binary, etc.)"       NL
"# named '"SCRIPT_NAME"' (for which this file is a template) with the "      NL
"# following ordered arguments:"                                             NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] REV          (the number of the revision just committed)"           NL
"#   [3] TXN-NAME     (the name of the transaction that has become REV)"     NL
"#"                                                                          NL
"# Because the commit has already completed and cannot be undone,"           NL
"# the exit code of the hook program is ignored.  The hook program"          NL
"# can use the 'svnlook' utility to help it examine the"                     NL
"# newly-committed tree."                                                    NL;
  script =
"REPOS=\"$1\""                                                               NL
"REV=\"$2\""                                                                 NL
"TXN_NAME=\"$3\""                                                            NL
                                                                             NL
"mailer.py commit \"$REPOS\" \"$REV\" /path/to/mailer.conf"                  NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating post-commit hook"));

#undef SCRIPT_NAME


  /* Post-lock hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_POST_LOCK

  description =
"# POST-LOCK HOOK"                                                           NL
"#"                                                                          NL
"# The post-lock hook is run after a path is locked.  Subversion runs"       NL
"# this hook by invoking a program (script, executable, binary, etc.)"       NL
"# named '"SCRIPT_NAME"' (for which this file is a template) with the "      NL
"# following ordered arguments:"                                             NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] USER         (the user who created the lock)"                       NL
"#"                                                                          NL
"# The paths that were just locked are passed to the hook via STDIN."        NL
"#"                                                                          NL
"# Because the locks have already been created and cannot be undone,"        NL
"# the exit code of the hook program is ignored.  The hook program"          NL
"# can use the 'svnlook' utility to examine the paths in the repository"     NL
"# but since the hook is invoked asynchronously the newly-created locks"     NL
"# may no longer be present."                                                NL;
  script =
"REPOS=\"$1\""                                                               NL
"USER=\"$2\""                                                                NL
""                                                                           NL
"# Send email to interested parties, let them know a lock was created:"      NL
"mailer.py lock \"$REPOS\" \"$USER\" /path/to/mailer.conf"                   NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating post-lock hook"));

#undef SCRIPT_NAME


  /* Post-unlock hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_POST_UNLOCK

  description =
"# POST-UNLOCK HOOK"                                                         NL
"#"                                                                          NL
"# The post-unlock hook runs after a path is unlocked.  Subversion runs"     NL
"# this hook by invoking a program (script, executable, binary, etc.)"       NL
"# named '"SCRIPT_NAME"' (for which this file is a template) with the "      NL
"# following ordered arguments:"                                             NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] USER         (the user who destroyed the lock)"                     NL
"#"                                                                          NL
"# The paths that were just unlocked are passed to the hook via STDIN."      NL
"#"                                                                          NL
"# Because the lock has already been destroyed and cannot be undone,"        NL
"# the exit code of the hook program is ignored."                            NL;
  script =
"REPOS=\"$1\""                                                               NL
"USER=\"$2\""                                                                NL
""                                                                           NL
"# Send email to interested parties, let them know a lock was removed:"      NL
"mailer.py unlock \"$REPOS\" \"$USER\" /path/to/mailer.conf"                 NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating post-unlock hook"));

#undef SCRIPT_NAME


  /* Post-revprop-change hook. */
#define SCRIPT_NAME SVN_REPOS__HOOK_POST_REVPROP_CHANGE

  description =
"# POST-REVPROP-CHANGE HOOK"                                                 NL
"#"                                                                          NL
"# The post-revprop-change hook is invoked after a revision property"        NL
"# has been added, modified or deleted.  Subversion runs this hook by"       NL
"# invoking a program (script, executable, binary, etc.) named"              NL
"# '"SCRIPT_NAME"' (for which this file is a template), with the"            NL
"# following ordered arguments:"                                             NL
"#"                                                                          NL
"#   [1] REPOS-PATH   (the path to this repository)"                         NL
"#   [2] REV          (the revision that was tweaked)"                       NL
"#   [3] USER         (the username of the person tweaking the property)"    NL
"#   [4] PROPNAME     (the property that was changed)"                       NL
"#   [5] ACTION       (the property was 'A'dded, 'M'odified, or 'D'eleted)"  NL
"#"                                                                          NL
"#   [STDIN] PROPVAL  ** the old property value is passed via STDIN."        NL
"#"                                                                          NL
"# Because the propchange has already completed and cannot be undone,"       NL
"# the exit code of the hook program is ignored.  The hook program"          NL
"# can use the 'svnlook' utility to help it examine the"                     NL
"# new property value."                                                      NL;
  script =
"REPOS=\"$1\""                                                               NL
"REV=\"$2\""                                                                 NL
"USER=\"$3\""                                                                NL
"PROPNAME=\"$4\""                                                            NL
"ACTION=\"$5\""                                                              NL
""                                                                           NL
"mailer.py propchange2 \"$REPOS\" \"$REV\" \"$USER\" \"$PROPNAME\" "
"\"$ACTION\" /path/to/mailer.conf"                                           NL;

  SVN_ERR_W(write_hook_template_file(repos, SCRIPT_NAME,
                                     description, script, pool),
            _("Creating post-revprop-change hook"));

#undef SCRIPT_NAME

  return SVN_NO_ERROR;
}

static svn_error_t *
create_conf(svn_repos_t *repos, apr_pool_t *pool)
{
  SVN_ERR_W(create_repos_dir(repos->conf_path, pool),
            _("Creating conf directory"));

  /* Write a default template for svnserve.conf. */
  {
    static const char * const svnserve_conf_contents =
"### This file controls the configuration of the svnserve daemon, if you"    NL
"### use it to allow access to this repository.  (If you only allow"         NL
"### access through http: and/or file: URLs, then this file is"              NL
"### irrelevant.)"                                                           NL
""                                                                           NL
"### Visit http://subversion.apache.org/ for more information."              NL
""                                                                           NL
"[general]"                                                                  NL
"### The anon-access and auth-access options control access to the"          NL
"### repository for unauthenticated (a.k.a. anonymous) users and"            NL
"### authenticated users, respectively."                                     NL
"### Valid values are \"write\", \"read\", and \"none\"."                    NL
"### Setting the value to \"none\" prohibits both reading and writing;"      NL
"### \"read\" allows read-only access, and \"write\" allows complete "       NL
"### read/write access to the repository."                                   NL
"### The sample settings below are the defaults and specify that anonymous"  NL
"### users have read-only access to the repository, while authenticated"     NL
"### users have read and write access to the repository."                    NL
"# anon-access = read"                                                       NL
"# auth-access = write"                                                      NL
"### The password-db option controls the location of the password"           NL
"### database file.  Unless you specify a path starting with a /,"           NL
"### the file's location is relative to the directory containing"            NL
"### this configuration file."                                               NL
"### If SASL is enabled (see below), this file will NOT be used."            NL
"### Uncomment the line below to use the default password file."             NL
"# password-db = passwd"                                                     NL
"### The authz-db option controls the location of the authorization"         NL
"### rules for path-based access control.  Unless you specify a path"        NL
"### starting with a /, the file's location is relative to the"              NL
"### directory containing this file.  The specified path may be a"           NL
"### repository relative URL (^/) or an absolute file:// URL to a text"      NL
"### file in a Subversion repository.  If you don't specify an authz-db,"    NL
"### no path-based access control is done."                                  NL
"### Uncomment the line below to use the default authorization file."        NL
"# authz-db = " SVN_REPOS__CONF_AUTHZ                                        NL
"### The groups-db option controls the location of the file with the"        NL
"### group definitions and allows maintaining groups separately from the"    NL
"### authorization rules.  The groups-db file is of the same format as the"  NL
"### authz-db file and should contain a single [groups] section with the"    NL
"### group definitions.  If the option is enabled, the authz-db file cannot" NL
"### contain a [groups] section.  Unless you specify a path starting with"   NL
"### a /, the file's location is relative to the directory containing this"  NL
"### file.  The specified path may be a repository relative URL (^/) or an"  NL
"### absolute file:// URL to a text file in a Subversion repository."        NL
"### This option is not being used by default."                              NL
"# groups-db = " SVN_REPOS__CONF_GROUPS                                      NL
"### This option specifies the authentication realm of the repository."      NL
"### If two repositories have the same authentication realm, they should"    NL
"### have the same password database, and vice versa.  The default realm"    NL
"### is repository's uuid."                                                  NL
"# realm = My First Repository"                                              NL
"### The force-username-case option causes svnserve to case-normalize"       NL
"### usernames before comparing them against the authorization rules in the" NL
"### authz-db file configured above.  Valid values are \"upper\" (to upper-" NL
"### case the usernames), \"lower\" (to lowercase the usernames), and"       NL
"### \"none\" (to compare usernames as-is without case conversion, which"    NL
"### is the default behavior)."                                              NL
"# force-username-case = none"                                               NL
"### The hooks-env options specifies a path to the hook script environment " NL
"### configuration file. This option overrides the per-repository default"   NL
"### and can be used to configure the hook script environment for multiple " NL
"### repositories in a single file, if an absolute path is specified."       NL
"### Unless you specify an absolute path, the file's location is relative"   NL
"### to the directory containing this file."                                 NL
"# hooks-env = " SVN_REPOS__CONF_HOOKS_ENV                                   NL
""                                                                           NL
"[sasl]"                                                                     NL
"### This option specifies whether you want to use the Cyrus SASL"           NL
"### library for authentication. Default is false."                          NL
"### Enabling this option requires svnserve to have been built with Cyrus"   NL
"### SASL support; to check, run 'svnserve --version' and look for a line"   NL
"### reading 'Cyrus SASL authentication is available.'"                      NL
"# use-sasl = true"                                                          NL
"### These options specify the desired strength of the security layer"       NL
"### that you want SASL to provide. 0 means no encryption, 1 means"          NL
"### integrity-checking only, values larger than 1 are correlated"           NL
"### to the effective key length for encryption (e.g. 128 means 128-bit"     NL
"### encryption). The values below are the defaults."                        NL
"# min-encryption = 0"                                                       NL
"# max-encryption = 256"                                                     NL;

    SVN_ERR_W(svn_io_file_create(svn_repos_svnserve_conf(repos, pool),
                                 svnserve_conf_contents, pool),
              _("Creating svnserve.conf file"));
  }

  {
    static const char * const passwd_contents =
"### This file is an example password file for svnserve."                    NL
"### Its format is similar to that of svnserve.conf. As shown in the"        NL
"### example below it contains one section labelled [users]."                NL
"### The name and password for each user follow, one account per line."      NL
""                                                                           NL
"[users]"                                                                    NL
"# harry = harryssecret"                                                     NL
"# sally = sallyssecret"                                                     NL;

    SVN_ERR_W(svn_io_file_create(svn_dirent_join(repos->conf_path,
                                                 SVN_REPOS__CONF_PASSWD,
                                                 pool),
                                 passwd_contents, pool),
              _("Creating passwd file"));
  }

  {
    static const char * const authz_contents =
"### This file is an example authorization file for svnserve."               NL
"### Its format is identical to that of mod_authz_svn authorization"         NL
"### files."                                                                 NL
"### As shown below each section defines authorizations for the path and"    NL
"### (optional) repository specified by the section name."                   NL
"### The authorizations follow. An authorization line can refer to:"         NL
"###  - a single user,"                                                      NL
"###  - a group of users defined in a special [groups] section,"             NL
"###  - an alias defined in a special [aliases] section,"                    NL
"###  - all authenticated users, using the '$authenticated' token,"          NL
"###  - only anonymous users, using the '$anonymous' token,"                 NL
"###  - anyone, using the '*' wildcard."                                     NL
"###"                                                                        NL
"### A match can be inverted by prefixing the rule with '~'. Rules can"      NL
"### grant read ('r') access, read-write ('rw') access, or no access"        NL
"### ('')."                                                                  NL
""                                                                           NL
"[aliases]"                                                                  NL
"# joe = /C=XZ/ST=Dessert/L=Snake City/O=Snake Oil, Ltd./OU=Research Institute/CN=Joe Average"  NL
""                                                                           NL
"[groups]"                                                                   NL
"# harry_and_sally = harry,sally"                                            NL
"# harry_sally_and_joe = harry,sally,&joe"                                   NL
""                                                                           NL
"# [/foo/bar]"                                                               NL
"# harry = rw"                                                               NL
"# &joe = r"                                                                 NL
"# * ="                                                                      NL
""                                                                           NL
"# [repository:/baz/fuz]"                                                    NL
"# @harry_and_sally = rw"                                                    NL
"# * = r"                                                                    NL;

    SVN_ERR_W(svn_io_file_create(svn_dirent_join(repos->conf_path,
                                                 SVN_REPOS__CONF_AUTHZ,
                                                 pool),
                                 authz_contents, pool),
              _("Creating authz file"));
  }

  {
    static const char * const hooks_env_contents =
"### This file is an example hook script environment configuration file."    NL
"### Hook scripts run in an empty environment by default."                   NL
"### As shown below each section defines environment variables for a"        NL
"### particular hook script. The [default] section defines environment"      NL
"### variables for all hook scripts, unless overridden by a hook-specific"   NL
"### section."                                                               NL
""                                                                           NL
"### This example configures a UTF-8 locale for all hook scripts, so that "  NL
"### special characters, such as umlauts, may be printed to stderr."         NL
"### If UTF-8 is used with a mod_dav_svn server, the SVNUseUTF8 option must" NL
"### also be set to 'yes' in httpd.conf."                                    NL
"### With svnserve, the LANG environment variable of the svnserve process"   NL
"### must be set to the same value as given here."                           NL
"[default]"                                                                  NL
"LANG = en_US.UTF-8"                                                         NL
""                                                                           NL
"### This sets the PATH environment variable for the pre-commit hook."       NL
"[pre-commit]"                                                               NL
"PATH = /usr/local/bin:/usr/bin:/usr/sbin"                                   NL;

    SVN_ERR_W(svn_io_file_create(svn_dirent_join(repos->conf_path,
                                                 SVN_REPOS__CONF_HOOKS_ENV \
                                                 SVN_REPOS__HOOK_DESC_EXT,
                                                 pool),
                                 hooks_env_contents, pool),
              _("Creating hooks-env file"));
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_hooks_setenv(svn_repos_t *repos,
                       const char *hooks_env_path,
                       apr_pool_t *scratch_pool)
{
  if (hooks_env_path == NULL)
    repos->hooks_env_path = svn_dirent_join(repos->conf_path,
                                            SVN_REPOS__CONF_HOOKS_ENV,
                                            repos->pool);
  else if (!svn_dirent_is_absolute(hooks_env_path))
    repos->hooks_env_path = svn_dirent_join(repos->conf_path,
                                            hooks_env_path,
                                            repos->pool);
  else
    repos->hooks_env_path = apr_pstrdup(repos->pool, hooks_env_path);

  return SVN_NO_ERROR;
}

/* Allocate and return a new svn_repos_t * object, initializing the
   directory pathname members based on PATH, and initializing the
   REPOSITORY_CAPABILITIES member.
   The members FS, FORMAT, and FS_TYPE are *not* initialized (they are null),
   and it is the caller's responsibility to fill them in if needed.  */
static svn_repos_t *
create_svn_repos_t(const char *path, apr_pool_t *pool)
{
  svn_repos_t *repos = apr_pcalloc(pool, sizeof(*repos));

  repos->path = apr_pstrdup(pool, path);
  repos->db_path = svn_dirent_join(path, SVN_REPOS__DB_DIR, pool);
  repos->conf_path = svn_dirent_join(path, SVN_REPOS__CONF_DIR, pool);
  repos->hook_path = svn_dirent_join(path, SVN_REPOS__HOOK_DIR, pool);
  repos->lock_path = svn_dirent_join(path, SVN_REPOS__LOCK_DIR, pool);
  repos->hooks_env_path = NULL;
  repos->repository_capabilities = apr_hash_make(pool);
  repos->pool = pool;

  return repos;
}


static svn_error_t *
create_repos_structure(svn_repos_t *repos,
                       const char *path,
                       apr_hash_t *fs_config,
                       apr_pool_t *pool)
{
  /* Create the top-level repository directory. */
  SVN_ERR_W(create_repos_dir(path, pool),
            _("Could not create top-level directory"));

  /* Create the DAV sandbox directory if pre-1.4 or pre-1.5-compatible. */
  if (fs_config
      && (svn_hash_gets(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE)
          || svn_hash_gets(fs_config, SVN_FS_CONFIG_PRE_1_5_COMPATIBLE)))
    {
      const char *dav_path = svn_dirent_join(repos->path,
                                             SVN_REPOS__DAV_DIR, pool);
      SVN_ERR_W(create_repos_dir(dav_path, pool),
                _("Creating DAV sandbox dir"));
    }

  /* Create the lock directory.  */
  SVN_ERR(create_locks(repos, pool));

  /* Create the hooks directory.  */
  SVN_ERR(create_hooks(repos, pool));

  /* Create the conf directory.  */
  SVN_ERR(create_conf(repos, pool));

  /* Write the top-level README file. */
  {
    const char * const readme_header =
      "This is a Subversion repository; use the 'svnadmin' and 'svnlook' "   NL
      "tools to examine it.  Do not add, delete, or modify files here "      NL
      "unless you know how to avoid corrupting the repository."              NL
      ""                                                                     NL;
    const char * const readme_bdb_insert =
      "The directory \"" SVN_REPOS__DB_DIR "\" contains a Berkeley DB environment."  NL
      "you may need to tweak the values in \"" SVN_REPOS__DB_DIR "/DB_CONFIG\" to match the"  NL
      "requirements of your site."                                           NL
      ""                                                                     NL;
    const char * const readme_footer =
      "Visit http://subversion.apache.org/ for more information."            NL;
    apr_file_t *f;
    apr_size_t written;

    SVN_ERR(svn_io_file_open(&f,
                             svn_dirent_join(path, SVN_REPOS__README, pool),
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT, pool));

    SVN_ERR(svn_io_file_write_full(f, readme_header, strlen(readme_header),
                                   &written, pool));
    if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
      SVN_ERR(svn_io_file_write_full(f, readme_bdb_insert,
                                     strlen(readme_bdb_insert),
                                     &written, pool));
    SVN_ERR(svn_io_file_write_full(f, readme_footer, strlen(readme_footer),
                                   &written, pool));

    return svn_io_file_close(f, pool);
  }
}


/* There is, at present, nothing within the direct responsibility
   of libsvn_repos which requires locking.  For historical compatibility
   reasons, the BDB libsvn_fs backend does not do its own locking, expecting
   libsvn_repos to do the locking for it.  Here we take care of that
   backend-specific requirement.
   The kind of lock is controlled by EXCLUSIVE and NONBLOCKING.
   The lock is scoped to POOL.  */
static svn_error_t *
lock_repos(svn_repos_t *repos,
           svn_boolean_t exclusive,
           svn_boolean_t nonblocking,
           apr_pool_t *pool)
{
  if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
    {
      svn_error_t *err;
      const char *lockfile_path = svn_repos_db_lockfile(repos, pool);

      err = svn_io_file_lock2(lockfile_path, exclusive, nonblocking, pool);
      if (err != NULL && APR_STATUS_IS_EAGAIN(err->apr_err))
        return svn_error_trace(err);
      SVN_ERR_W(err, _("Error opening db lockfile"));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_create(svn_repos_t **repos_p,
                 const char *path,
                 const char *unused_1,
                 const char *unused_2,
                 apr_hash_t *config,
                 apr_hash_t *fs_config,
                 apr_pool_t *result_pool)
{
  svn_repos_t *repos;
  svn_error_t *err;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  const char *root_path;
  const char *local_abspath;

  /* Allocate a repository object, filling in the format we will create. */
  repos = create_svn_repos_t(path, result_pool);
  repos->format = SVN_REPOS__FORMAT_NUMBER;

  /* Discover the type of the filesystem we are about to create. */
  repos->fs_type = svn_hash__get_cstring(fs_config, SVN_FS_CONFIG_FS_TYPE,
                                         DEFAULT_FS_TYPE);
  if (svn_hash__get_bool(fs_config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE, FALSE))
    repos->format = SVN_REPOS__FORMAT_NUMBER_LEGACY;

  /* Don't create a repository inside another repository. */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  root_path = svn_repos_find_root_path(local_abspath, scratch_pool);
  if (root_path != NULL)
    {
      if (strcmp(root_path, local_abspath) == 0)
        return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                                 _("'%s' is an existing repository"),
                                 svn_dirent_local_style(root_path,
                                                        scratch_pool));
      else
        return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                                 _("'%s' is a subdirectory of an existing "
                                   "repository " "rooted at '%s'"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool),
                                 svn_dirent_local_style(root_path,
                                                        scratch_pool));
    }

  /* Create the various files and subdirectories for the repository. */
  SVN_ERR_W(create_repos_structure(repos, path, fs_config, scratch_pool),
            _("Repository creation failed"));

  /* Lock if needed. */
  SVN_ERR(lock_repos(repos, FALSE, FALSE, scratch_pool));

  /* Create an environment for the filesystem. */
  if ((err = svn_fs_create2(&repos->fs, repos->db_path, fs_config,
                            result_pool, scratch_pool)))
    {
      /* If there was an error making the filesytem, e.g. unknown/supported
       * filesystem type.  Clean up after ourselves.  Yes this is safe because
       * create_repos_structure will fail if the path existed before we started
       * so we can't accidentally remove a directory that previously existed.
       */
      svn_pool_destroy(scratch_pool); /* Release lock to allow deleting dir */

      return svn_error_trace(
                   svn_error_compose_create(
                        err,
                        svn_io_remove_dir2(path, FALSE, NULL, NULL,
                                           result_pool)));
    }

  /* This repository is ready.  Stamp it with a format number. */
  SVN_ERR(svn_io_write_version_file
          (svn_dirent_join(path, SVN_REPOS__FORMAT, scratch_pool),
           repos->format, scratch_pool));

  svn_pool_destroy(scratch_pool); /* Release lock */

  *repos_p = repos;
  return SVN_NO_ERROR;
}


/* Check if @a path is the root of a repository by checking if the
 * path contains the expected files and directories.  Return TRUE
 * on errors (which would be permission errors, probably) so that
 * we the user will see them after we try to open the repository
 * for real.  */
static svn_boolean_t
check_repos_path(const char *path,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;

  err = svn_io_check_path(svn_dirent_join(path, SVN_REPOS__FORMAT, pool),
                          &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return TRUE;
    }
  if (kind != svn_node_file)
    return FALSE;

  /* Check the db/ subdir, but allow it to be a symlink (Subversion
     works just fine if it's a symlink). */
  err = svn_io_check_resolved_path
    (svn_dirent_join(path, SVN_REPOS__DB_DIR, pool), &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return TRUE;
    }
  if (kind != svn_node_dir)
    return FALSE;

  return TRUE;
}


/* Verify that REPOS's format is suitable.
   Use POOL for temporary allocation. */
static svn_error_t *
check_repos_format(svn_repos_t *repos,
                   apr_pool_t *pool)
{
  int format;
  const char *format_path;

  format_path = svn_dirent_join(repos->path, SVN_REPOS__FORMAT, pool);
  SVN_ERR(svn_io_read_version_file(&format, format_path, pool));

  if (format != SVN_REPOS__FORMAT_NUMBER &&
      format != SVN_REPOS__FORMAT_NUMBER_LEGACY)
    {
      return svn_error_createf
        (SVN_ERR_REPOS_UNSUPPORTED_VERSION, NULL,
         _("Expected repository format '%d' or '%d'; found format '%d'"),
         SVN_REPOS__FORMAT_NUMBER_LEGACY, SVN_REPOS__FORMAT_NUMBER,
         format);
    }

  repos->format = format;

  return SVN_NO_ERROR;
}


/* Set *REPOS_P to a repository at PATH which has been opened.
   See lock_repos() above regarding EXCLUSIVE and NONBLOCKING.
   OPEN_FS indicates whether the Subversion filesystem should be opened,
   the handle being placed into repos->fs.
   Do all allocation in POOL.  */
static svn_error_t *
get_repos(svn_repos_t **repos_p,
          const char *path,
          svn_boolean_t exclusive,
          svn_boolean_t nonblocking,
          svn_boolean_t open_fs,
          apr_hash_t *fs_config,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_repos_t *repos;
  const char *fs_type;

  /* Allocate a repository object. */
  repos = create_svn_repos_t(path, result_pool);

  /* Verify the validity of our repository format. */
  SVN_ERR(check_repos_format(repos, scratch_pool));

  /* Discover the FS type. */
  SVN_ERR(svn_fs_type(&fs_type, repos->db_path, scratch_pool));
  repos->fs_type = apr_pstrdup(result_pool, fs_type);

  /* Lock if needed. */
  SVN_ERR(lock_repos(repos, exclusive, nonblocking, result_pool));

  /* Open up the filesystem only after obtaining the lock. */
  if (open_fs)
    SVN_ERR(svn_fs_open2(&repos->fs, repos->db_path, fs_config,
                         result_pool, scratch_pool));

#ifdef SVN_DEBUG_CRASH_AT_REPOS_OPEN
  /* If $PATH/config/debug-abort exists, crash the server here.
     This debugging feature can be used to test client recovery
     when the server crashes.

     See: Issue #4274 */
  {
    svn_node_kind_t kind;
    svn_error_t *err = svn_io_check_path(
        svn_dirent_join(repos->conf_path, "debug-abort", scratch_pool),
        &kind, scratch_pool);
    svn_error_clear(err);
    if (!err && kind == svn_node_file)
      SVN_ERR_MALFUNCTION_NO_RETURN();
  }
#endif /* SVN_DEBUG_CRASH_AT_REPOS_OPEN */

  *repos_p = repos;
  return SVN_NO_ERROR;
}



const char *
svn_repos_find_root_path(const char *path,
                         apr_pool_t *pool)
{
  const char *candidate = path;
  const char *decoded;
  svn_error_t *err;

  while (1)
    {
      /* Try to decode the path, so we don't fail if it contains characters
         that aren't supported by the OS filesystem.  The subversion fs
         isn't restricted by the OS filesystem character set. */
      err = svn_path_cstring_from_utf8(&decoded, candidate, pool);
      if (!err && check_repos_path(candidate, pool))
        break;
      svn_error_clear(err);

      if (svn_path_is_empty(candidate) ||
          svn_dirent_is_root(candidate, strlen(candidate)))
        return NULL;

      candidate = svn_dirent_dirname(candidate, pool);
    }

  return candidate;
}

svn_error_t *
svn_repos_open3(svn_repos_t **repos_p,
                const char *path,
                apr_hash_t *fs_config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  /* Fetch a repository object initialized with a shared read/write
     lock on the database. */

  return get_repos(repos_p, path, FALSE, FALSE, TRUE, fs_config,
                   result_pool, scratch_pool);
}

/* Baton used with fs_upgrade_notify, specifying the svn_repos layer
 * notification parameters.
 */
struct fs_upgrade_notify_baton_t
{
  svn_repos_notify_func_t notify_func;
  void *notify_baton;
};

/* Implements svn_fs_upgrade_notify_t as forwarding to a
 * svn_repos_notify_func_t passed in a fs_upgrade_notify_baton_t* BATON.
 */
static svn_error_t *
fs_upgrade_notify(void *baton,
                  apr_uint64_t number,
                  svn_fs_upgrade_notify_action_t action,
                  apr_pool_t *pool)
{
  struct fs_upgrade_notify_baton_t *fs_baton = baton;

  svn_repos_notify_t *notify = svn_repos_notify_create(
                                svn_repos_notify_mutex_acquired, pool);
  switch(action)
    {
      case svn_fs_upgrade_pack_revprops:
        notify->shard = number;
        notify->action = svn_repos_notify_pack_revprops;
        break;

      case svn_fs_upgrade_cleanup_revprops:
        notify->shard = number;
        notify->action = svn_repos_notify_cleanup_revprops;
        break;

      case svn_fs_upgrade_format_bumped:
        notify->revision = number;
        notify->action = svn_repos_notify_format_bumped;
        break;

      default:
        /* unknown notification */
        SVN_ERR_MALFUNCTION();
    }

  fs_baton->notify_func(fs_baton->notify_baton, notify, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_upgrade2(const char *path,
                   svn_boolean_t nonblocking,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  const char *format_path;
  int format;
  apr_pool_t *subpool = svn_pool_create(pool);

  struct fs_upgrade_notify_baton_t fs_notify_baton;
  fs_notify_baton.notify_func = notify_func;
  fs_notify_baton.notify_baton = notify_baton;

  /* Fetch a repository object; for the Berkeley DB backend, it is
     initialized with an EXCLUSIVE lock on the database.  This will at
     least prevent others from trying to read or write to it while we
     run recovery. (Other backends should do their own locking; see
     lock_repos.) */
  SVN_ERR(get_repos(&repos, path, TRUE, nonblocking, FALSE, NULL, subpool,
                    subpool));

  if (notify_func)
    {
      /* We notify *twice* here, because there are two different logistical
         actions occuring. */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                    svn_repos_notify_mutex_acquired, subpool);
      notify_func(notify_baton, notify, subpool);

      notify->action = svn_repos_notify_upgrade_start;
      notify_func(notify_baton, notify, subpool);
    }

  /* Try to overwrite with its own contents.  We do this only to
     verify that we can, because we don't want to actually bump the
     format of the repository until our underlying filesystem claims
     to have been upgraded correctly. */
  format_path = svn_dirent_join(repos->path, SVN_REPOS__FORMAT, subpool);
  SVN_ERR(svn_io_read_version_file(&format, format_path, subpool));
  SVN_ERR(svn_io_write_version_file(format_path, format, subpool));

  /* Try to upgrade the filesystem. */
  SVN_ERR(svn_fs_upgrade2(repos->db_path,
                          notify_func ? fs_upgrade_notify : NULL,
                          &fs_notify_baton, NULL, NULL, subpool));

  /* Now overwrite our format file with the latest version. */
  SVN_ERR(svn_io_write_version_file(format_path, SVN_REPOS__FORMAT_NUMBER,
                                    subpool));

  /* Close shop and free the subpool, to release the exclusive lock. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete(const char *path,
                 apr_pool_t *pool)
{
  const char *db_path = svn_dirent_join(path, SVN_REPOS__DB_DIR, pool);

  /* Delete the filesystem environment... */
  SVN_ERR(svn_fs_delete_fs(db_path, pool));

  /* ...then blow away everything else.  */
  return svn_error_trace(svn_io_remove_dir2(path, FALSE, NULL, NULL, pool));
}


/* Repository supports the capability. */
static const char *capability_yes = "yes";
/* Repository does not support the capability. */
static const char *capability_no = "no";

static svn_error_t *
dummy_mergeinfo_receiver(const char *path,
                         svn_mergeinfo_t mergeinfo,
                         void *baton,
                         apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_has_capability(svn_repos_t *repos,
                         svn_boolean_t *has,
                         const char *capability,
                         apr_pool_t *pool)
{
  const char *val = svn_hash_gets(repos->repository_capabilities, capability);

  if (val == capability_yes)
    {
      *has = TRUE;
    }
  else if (val == capability_no)
    {
      *has = FALSE;
    }
  /* Else don't know, so investigate. */
  else if (strcmp(capability, SVN_REPOS_CAPABILITY_MERGEINFO) == 0)
    {
      svn_error_t *err;
      svn_fs_root_t *root;
      apr_array_header_t *paths = apr_array_make(pool, 1,
                                                 sizeof(char *));

      SVN_ERR(svn_fs_revision_root(&root, repos->fs, 0, pool));
      APR_ARRAY_PUSH(paths, const char *) = "";
      err = svn_fs_get_mergeinfo3(root, paths, FALSE, FALSE, TRUE,
                                  dummy_mergeinfo_receiver, NULL, pool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
            {
              svn_error_clear(err);
              svn_hash_sets(repos->repository_capabilities,
                            SVN_REPOS_CAPABILITY_MERGEINFO, capability_no);
              *has = FALSE;
            }
          else if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
            {
              /* Mergeinfo requests use relative paths, and anyway we're
                 in r0, so we're likely to get this error -- but it
                 means the repository supports mergeinfo! */
              svn_error_clear(err);
              svn_hash_sets(repos->repository_capabilities,
                            SVN_REPOS_CAPABILITY_MERGEINFO, capability_yes);
              *has = TRUE;
            }
          else
            {
              return svn_error_trace(err);
            }
        }
      else
        {
          svn_hash_sets(repos->repository_capabilities,
                        SVN_REPOS_CAPABILITY_MERGEINFO, capability_yes);
          *has = TRUE;
        }
    }
  else
    {
      return svn_error_createf(SVN_ERR_UNKNOWN_CAPABILITY, 0,
                               _("unknown capability '%s'"), capability);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_capabilities(apr_hash_t **capabilities,
                       svn_repos_t *repos,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  static const char *const queries[] = {
    SVN_REPOS_CAPABILITY_MERGEINFO,
    NULL
  };
  const char *const *i;

  *capabilities = apr_hash_make(result_pool);

  for (i = queries; *i; i++)
    {
      svn_boolean_t has;
      SVN_ERR(svn_repos_has_capability(repos, &has, *i, scratch_pool));
      if (has)
        svn_hash_sets(*capabilities, *i, *i);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_info_format(int *repos_format,
                      svn_version_t **supports_version,
                      svn_repos_t *repos,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  *repos_format = repos->format;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 0;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  switch (repos->format)
    {
    case SVN_REPOS__FORMAT_NUMBER_LEGACY:
      break;
    case SVN_REPOS__FORMAT_NUMBER_1_4:
      (*supports_version)->minor = 4;
      break;
#ifdef SVN_DEBUG
# if SVN_REPOS__FORMAT_NUMBER != SVN_REPOS__FORMAT_NUMBER_1_4
#  error "Need to add a 'case' statement here"
# endif
#endif
    }

  return SVN_NO_ERROR;
}

svn_fs_t *
svn_repos_fs(svn_repos_t *repos)
{
  if (! repos)
    return NULL;
  return repos->fs;
}

const char *
svn_repos_fs_type(svn_repos_t *repos,
                  apr_pool_t *result_pool)
{
  return apr_pstrdup(result_pool, repos->fs_type);
}

/* For historical reasons, for the Berkeley DB backend, this code uses
 * repository locking, which is motivated by the need to support the
 * Berkeley DB error DB_RUN_RECOVERY.  (FSFS takes care of locking
 * itself, inside its implementation of svn_fs_recover.)  Here's how
 * it works:
 *
 * Every accessor of a repository's database takes out a shared lock
 * on the repository -- both readers and writers get shared locks, and
 * there can be an unlimited number of shared locks simultaneously.
 *
 * Sometimes, a db access returns the error DB_RUN_RECOVERY.  When
 * this happens, we need to run svn_fs_recover() on the db
 * with no other accessors present.  So we take out an exclusive lock
 * on the repository.  From the moment we request the exclusive lock,
 * no more shared locks are granted, and when the last shared lock
 * disappears, the exclusive lock is granted.  As soon as we get it,
 * we can run recovery.
 *
 * We assume that once any berkeley call returns DB_RUN_RECOVERY, they
 * all do, until recovery is run.
 */

svn_error_t *
svn_repos_recover4(const char *path,
                   svn_boolean_t nonblocking,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   svn_cancel_func_t cancel_func,
                   void * cancel_baton,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Fetch a repository object; for the Berkeley DB backend, it is
     initialized with an EXCLUSIVE lock on the database.  This will at
     least prevent others from trying to read or write to it while we
     run recovery. (Other backends should do their own locking; see
     lock_repos.) */
  SVN_ERR(get_repos(&repos, path, TRUE, nonblocking,
                    FALSE,    /* don't try to open the db yet. */
                    NULL,
                    subpool, subpool));

  if (notify_func)
    {
      /* We notify *twice* here, because there are two different logistical
         actions occuring. */
      svn_repos_notify_t *notify = svn_repos_notify_create(
                                    svn_repos_notify_mutex_acquired, subpool);
      notify_func(notify_baton, notify, subpool);

      notify->action = svn_repos_notify_recover_start;
      notify_func(notify_baton, notify, subpool);
    }

  /* Recover the database to a consistent state. */
  SVN_ERR(svn_fs_recover(repos->db_path, cancel_func, cancel_baton, subpool));

  /* Close shop and free the subpool, to release the exclusive lock. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

struct freeze_baton_t {
  apr_array_header_t *paths;
  int counter;
  svn_repos_freeze_func_t freeze_func;
  void *freeze_baton;

  /* Scratch pool used for every freeze callback invocation. */
  apr_pool_t *scratch_pool;
};

static svn_error_t *
multi_freeze(void *baton,
             apr_pool_t *pool)
{
  struct freeze_baton_t *fb = baton;

  svn_pool_clear(fb->scratch_pool);
  if (fb->counter == fb->paths->nelts)
    {
      SVN_ERR(fb->freeze_func(fb->freeze_baton, pool));
      return SVN_NO_ERROR;
    }
  else
    {
      /* Using a subpool as the only way to unlock the repos lock used
         by BDB is to clear the pool used to take the lock. */
      apr_pool_t *subpool = svn_pool_create(pool);
      const char *path = APR_ARRAY_IDX(fb->paths, fb->counter, const char *);
      svn_repos_t *repos;

      ++fb->counter;

      SVN_ERR(get_repos(&repos, path,
                        TRUE  /* exclusive (only applies to BDB) */,
                        FALSE /* non-blocking */,
                        FALSE /* open-fs */,
                        NULL, subpool, fb->scratch_pool));


      if (strcmp(repos->fs_type, SVN_FS_TYPE_BDB) == 0)
        {
          svn_error_t *err = multi_freeze(fb, subpool);

          svn_pool_destroy(subpool);

          return err;
        }
      else
        {
          SVN_ERR(svn_fs_open2(&repos->fs, repos->db_path, NULL, subpool,
                               fb->scratch_pool));
          SVN_ERR(svn_fs_freeze(svn_repos_fs(repos), multi_freeze, fb,
                                subpool));
        }

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}

/* For BDB we fall back on BDB's repos layer lock which means that the
   repository is unreadable while frozen.

   For FSFS we delegate to the FS layer which uses the FSFS write-lock
   and an SQLite reserved lock which means the repository is readable
   while frozen. */
svn_error_t *
svn_repos_freeze(apr_array_header_t *paths,
                 svn_repos_freeze_func_t freeze_func,
                 void *freeze_baton,
                 apr_pool_t *pool)
{
  struct freeze_baton_t fb;

  fb.paths = paths;
  fb.counter = 0;
  fb.freeze_func = freeze_func;
  fb.freeze_baton = freeze_baton;
  fb.scratch_pool = svn_pool_create(pool);

  SVN_ERR(multi_freeze(&fb, pool));

  svn_pool_destroy(fb.scratch_pool);
  return SVN_NO_ERROR;
}

svn_error_t *svn_repos_db_logfiles(apr_array_header_t **logfiles,
                                   const char *path,
                                   svn_boolean_t only_unused,
                                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  int i;

  SVN_ERR(get_repos(&repos, path,
                    FALSE, FALSE,
                    FALSE,     /* Do not open fs. */
                    NULL,
                    pool, pool));

  SVN_ERR(svn_fs_berkeley_logfiles(logfiles,
                                   svn_repos_db_env(repos, pool),
                                   only_unused,
                                   pool));

  /* Loop, printing log files. */
  for (i = 0; i < (*logfiles)->nelts; i++)
    {
      const char ** log_file = &(APR_ARRAY_IDX(*logfiles, i, const char *));
      *log_file = svn_dirent_join(SVN_REPOS__DB_DIR, *log_file, pool);
    }

  return SVN_NO_ERROR;
}

/* Baton for hotcopy_structure(). */
struct hotcopy_ctx_t {
  const char *dest;     /* target location to construct */
  size_t src_len; /* len of the source path*/

  /* As in svn_repos_hotcopy2() */
  svn_boolean_t incremental;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

/* Copy the repository structure of PATH to BATON->DEST, with exception of
 * @c SVN_REPOS__DB_DIR, @c SVN_REPOS__LOCK_DIR and @c SVN_REPOS__FORMAT;
 * those directories and files are handled separately.
 *
 * BATON is a (struct hotcopy_ctx_t *).  BATON->SRC_LEN is the length
 * of PATH.
 *
 * Implements svn_io_walk_func_t.
 */
static svn_error_t *hotcopy_structure(void *baton,
                                      const char *path,
                                      const apr_finfo_t *finfo,
                                      apr_pool_t *pool)
{
  const struct hotcopy_ctx_t *ctx = baton;
  const char *sub_path;
  const char *target;

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  if (strlen(path) == ctx->src_len)
    {
      sub_path = "";
    }
  else
    {
      sub_path = &path[ctx->src_len+1];

      /* Check if we are inside db directory and if so skip it */
      if (svn_path_compare_paths
          (svn_dirent_get_longest_ancestor(SVN_REPOS__DB_DIR, sub_path, pool),
           SVN_REPOS__DB_DIR) == 0)
        return SVN_NO_ERROR;

      if (svn_path_compare_paths
          (svn_dirent_get_longest_ancestor(SVN_REPOS__LOCK_DIR, sub_path,
                                           pool),
           SVN_REPOS__LOCK_DIR) == 0)
        return SVN_NO_ERROR;

      if (svn_path_compare_paths
          (svn_dirent_get_longest_ancestor(SVN_REPOS__FORMAT, sub_path, pool),
           SVN_REPOS__FORMAT) == 0)
        return SVN_NO_ERROR;
    }

  target = svn_dirent_join(ctx->dest, sub_path, pool);

  if (finfo->filetype == APR_DIR)
    {
      svn_error_t *err;

      err = create_repos_dir(target, pool);
      if (ctx->incremental && err && err->apr_err == SVN_ERR_DIR_NOT_EMPTY)
        {
          svn_error_clear(err);
          err = SVN_NO_ERROR;
        }
      return svn_error_trace(err);
    }
  else if (finfo->filetype == APR_REG)
    return svn_io_copy_file(path, target, TRUE, pool);
  else if (finfo->filetype == APR_LNK)
    return svn_io_copy_link(path, target, pool);
  else
    return SVN_NO_ERROR;
}


/** Obtain a lock on db logs lock file. Create one if it does not exist.
 */
static svn_error_t *
lock_db_logs_file(svn_repos_t *repos,
                  svn_boolean_t exclusive,
                  apr_pool_t *pool)
{
  const char * lock_file = svn_repos_db_logs_lockfile(repos, pool);

  /* Try to create a lock file, in case if it is missing. As in case of the
     repositories created before hotcopy functionality.  */
  svn_error_clear(create_db_logs_lock(repos, pool));

  return svn_io_file_lock2(lock_file, exclusive, FALSE, pool);
}


/* Baton used with fs_hotcopy_notify(), specifying the svn_repos layer
 * notification parameters.
 */
struct fs_hotcopy_notify_baton_t
{
  svn_repos_notify_func_t notify_func;
  void *notify_baton;
};

/* Implements svn_fs_hotcopy_notify_t as forwarding to a
 * svn_repos_notify_func_t passed in a fs_hotcopy_notify_baton_t* BATON.
 */
static void
fs_hotcopy_notify(void *baton,
                  svn_revnum_t start_revision,
                  svn_revnum_t end_revision,
                  apr_pool_t *pool)
{
  struct fs_hotcopy_notify_baton_t *fs_baton = baton;
  svn_repos_notify_t *notify;

  notify = svn_repos_notify_create(svn_repos_notify_hotcopy_rev_range, pool);
  notify->start_revision = start_revision;
  notify->end_revision = end_revision;

  fs_baton->notify_func(fs_baton->notify_baton, notify, pool);
}

/* Make a copy of a repository with hot backup of fs. */
svn_error_t *
svn_repos_hotcopy3(const char *src_path,
                   const char *dst_path,
                   svn_boolean_t clean_logs,
                   svn_boolean_t incremental,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  svn_fs_hotcopy_notify_t fs_notify_func;
  struct fs_hotcopy_notify_baton_t fs_notify_baton;
  struct hotcopy_ctx_t hotcopy_context;
  const char *src_abspath;
  const char *dst_abspath;
  svn_repos_t *src_repos;
  svn_repos_t *dst_repos;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&src_abspath, src_path, scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&dst_abspath, dst_path, scratch_pool));
  if (strcmp(src_abspath, dst_abspath) == 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Hotcopy source and destination are equal"));

  /* Try to open original repository */
  SVN_ERR(get_repos(&src_repos, src_abspath,
                    FALSE, FALSE,
                    FALSE,    /* don't try to open the db yet. */
                    NULL,
                    scratch_pool, scratch_pool));

  /* If we are going to clean logs, then get an exclusive lock on
     db-logs.lock, to ensure that no one else will work with logs.

     If we are just copying, then get a shared lock to ensure that
     no one else will clean logs while we copying them */

  SVN_ERR(lock_db_logs_file(src_repos, clean_logs, scratch_pool));

  /* Copy the repository to a new path, with exception of
     specially handled directories */

  hotcopy_context.dest = dst_abspath;
  hotcopy_context.src_len = strlen(src_abspath);
  hotcopy_context.incremental = incremental;
  hotcopy_context.cancel_func = cancel_func;
  hotcopy_context.cancel_baton = cancel_baton;
  SVN_ERR(svn_io_dir_walk2(src_abspath,
                           0,
                           hotcopy_structure,
                           &hotcopy_context,
                           scratch_pool));

  /* Prepare dst_repos object so that we may create locks,
     so that we may open repository */

  dst_repos = create_svn_repos_t(dst_abspath, scratch_pool);
  dst_repos->fs_type = src_repos->fs_type;
  dst_repos->format = src_repos->format;

  err = create_locks(dst_repos, scratch_pool);
  if (err)
    {
      if (incremental && err->apr_err == SVN_ERR_DIR_NOT_EMPTY)
        svn_error_clear(err);
      else
        return svn_error_trace(err);
    }

  err = svn_io_dir_make_sgid(dst_repos->db_path, APR_OS_DEFAULT,
                             scratch_pool);
  if (err)
    {
      if (incremental && APR_STATUS_IS_EEXIST(err->apr_err))
        svn_error_clear(err);
      else
        return svn_error_trace(err);
    }

  /* Exclusively lock the new repository.
     No one should be accessing it at the moment */
  SVN_ERR(lock_repos(dst_repos, TRUE, FALSE, scratch_pool));

  fs_notify_func = notify_func ? fs_hotcopy_notify : NULL;
  fs_notify_baton.notify_func = notify_func;
  fs_notify_baton.notify_baton = notify_baton;

  SVN_ERR(svn_fs_hotcopy3(src_repos->db_path, dst_repos->db_path,
                          clean_logs, incremental,
                          fs_notify_func, &fs_notify_baton,
                          cancel_func, cancel_baton, scratch_pool));

  /* Destination repository is ready.  Stamp it with a format number. */
  return svn_io_write_version_file
          (svn_dirent_join(dst_repos->path, SVN_REPOS__FORMAT, scratch_pool),
           dst_repos->format, scratch_pool);
}

/* Return the library version number. */
const svn_version_t *
svn_repos_version(void)
{
  SVN_VERSION_BODY;
}

svn_error_t *
svn_repos_remember_client_capabilities(svn_repos_t *repos,
                                       const apr_array_header_t *capabilities)
{
  repos->client_capabilities = capabilities;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__fs_type(const char **fs_type,
                   const char *repos_path,
                   apr_pool_t *pool)
{
  svn_repos_t repos;
  repos.path = (char*)repos_path;

  SVN_ERR(check_repos_format(&repos, pool));

  return svn_fs_type(fs_type,
                     svn_dirent_join(repos_path, SVN_REPOS__DB_DIR, pool),
                     pool);
}
