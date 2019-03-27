/* repos.h : interface to Subversion repository, private to libsvn_repos
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

#ifndef SVN_LIBSVN_REPOS_H
#define SVN_LIBSVN_REPOS_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_fs.h"
#include "svn_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Repository format number.

   Formats 0, 1 and 2 were pre-1.0.

   Format 3 was current for 1.0 through to 1.3.

   Format 4 was an abortive experiment during the development of the
   locking feature in the lead up to 1.2.

   Format 5 was new in 1.4, and is the first format which may contain
   BDB or FSFS filesystems with a FS format other than 1, since prior
   formats are accepted by some versions of Subversion which do not
   pay attention to the FS format number.
*/
#define SVN_REPOS__FORMAT_NUMBER         SVN_REPOS__FORMAT_NUMBER_1_4
#define SVN_REPOS__FORMAT_NUMBER_1_4     5
#define SVN_REPOS__FORMAT_NUMBER_LEGACY  3


/*** Repository layout. ***/

/* The top-level repository dir contains a README and various
   subdirectories.  */
#define SVN_REPOS__README      "README.txt" /* Explanation for trespassers. */
#define SVN_REPOS__FORMAT      "format"     /* Stores the current version
                                               of the repository. */
#define SVN_REPOS__DB_DIR      "db"         /* Where Berkeley lives. */
#define SVN_REPOS__DAV_DIR     "dav"        /* DAV sandbox, for pre-1.5 */
#define SVN_REPOS__LOCK_DIR    "locks"      /* Lock files live here. */
#define SVN_REPOS__HOOK_DIR    "hooks"      /* Hook programs. */
#define SVN_REPOS__CONF_DIR    "conf"       /* Configuration files. */

/* Things for which we keep lockfiles. */
#define SVN_REPOS__DB_LOCKFILE "db.lock" /* Our Berkeley lockfile. */
#define SVN_REPOS__DB_LOGS_LOCKFILE "db-logs.lock" /* BDB logs lockfile. */

/* In the repository hooks directory, look for these files. */
#define SVN_REPOS__HOOK_START_COMMIT    "start-commit"
#define SVN_REPOS__HOOK_PRE_COMMIT      "pre-commit"
#define SVN_REPOS__HOOK_POST_COMMIT     "post-commit"
#define SVN_REPOS__HOOK_READ_SENTINEL   "read-sentinels"
#define SVN_REPOS__HOOK_WRITE_SENTINEL  "write-sentinels"
#define SVN_REPOS__HOOK_PRE_REVPROP_CHANGE  "pre-revprop-change"
#define SVN_REPOS__HOOK_POST_REVPROP_CHANGE "post-revprop-change"
#define SVN_REPOS__HOOK_PRE_LOCK        "pre-lock"
#define SVN_REPOS__HOOK_POST_LOCK       "post-lock"
#define SVN_REPOS__HOOK_PRE_UNLOCK      "pre-unlock"
#define SVN_REPOS__HOOK_POST_UNLOCK     "post-unlock"


/* The extension added to the names of example hook scripts. */
#define SVN_REPOS__HOOK_DESC_EXT        ".tmpl"

/* The file which contains a custom set of environment variables
 * passed inherited to hook scripts, in the repository conf directory. */
#define SVN_REPOS__CONF_HOOKS_ENV "hooks-env"
/* The name of the default section in the hooks-env config file. */
#define SVN_REPOS__HOOKS_ENV_DEFAULT_SECTION "default"

/* The configuration file for svnserve, in the repository conf directory. */
#define SVN_REPOS__CONF_SVNSERVE_CONF "svnserve.conf"

/* In the svnserve default configuration, these are the suggested
   locations for the passwd, authz and groups files (in the repository
   conf directory), and we put example templates there. */
#define SVN_REPOS__CONF_PASSWD "passwd"
#define SVN_REPOS__CONF_AUTHZ "authz"
#define SVN_REPOS__CONF_GROUPS "groups"

/* The Repository object, created by svn_repos_open2() and
   svn_repos_create(). */
struct svn_repos_t
{
  /* A Subversion filesystem object. */
  svn_fs_t *fs;

  /* The path to the repository's top-level directory. */
  char *path;

  /* The path to the repository's conf directory. */
  char *conf_path;

  /* The path to the repository's hooks directory. */
  char *hook_path;

  /* The path to the repository's locks directory. */
  char *lock_path;

  /* The path to the Berkeley DB filesystem environment. */
  char *db_path;

  /* The format number of this repository. */
  int format;

  /* The path to the repository's hooks enviroment file. If NULL, hooks run
   * in an empty environment. */
  const char *hooks_env_path;

  /* The FS backend in use within this repository. */
  const char *fs_type;

  /* If non-null, a list of all the capabilities the client (on the
     current connection) has self-reported.  Each element is a
     'const char *', one of SVN_RA_CAPABILITY_*.

     Note: it is somewhat counterintuitive that we store the client's
     capabilities, which are session-specific, on the repository
     object.  You'd think the capabilities here would represent the
     *repository's* capabilities, but no, they represent the
     client's -- we just don't have any other place to persist them. */
  const apr_array_header_t *client_capabilities;

  /* Maps SVN_REPOS_CAPABILITY_foo keys to "yes" or "no" values.
     If a capability is not yet discovered, it is absent from the table.
     Most likely the keys and values are constants anyway (and
     sufficiently well-informed internal code may just compare against
     those constants' addresses, therefore). */
  apr_hash_t *repository_capabilities;

  /* Pool from which this structure was allocated.  Also used for
     auxiliary repository-related data that requires a matching
     lifespan.  (As the svn_repos_t structure tends to be relatively
     long-lived, please be careful regarding this pool's usage.)  */
  apr_pool_t *pool;
};


/*** Hook-running Functions ***/

/* Set *HOOKS_ENV_P to the parsed contents of the hooks-env file
   LOCAL_ABSPATH, allocated in RESULT_POOL.  (This result is suitable
   for delivery to the various hook wrapper functions which accept a
   'hooks_env' parameter.)  If LOCAL_ABSPATH is NULL, set *HOOKS_ENV_P
   to NULL.

   Use SCRATCH_POOL for temporary allocations.  */
svn_error_t *
svn_repos__parse_hooks_env(apr_hash_t **hooks_env_p,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Run the start-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   USER is the authenticated name of the user starting the commit.

   CAPABILITIES is a list of 'const char *' capability names (using
   SVN_RA_CAPABILITY_*) that the client has self-reported.  Note that
   there is no guarantee the client is telling the truth: the hook
   should not make security assumptions based on the capabilities.

   TXN_NAME is the name of the commit transaction that's just been
   created. */
svn_error_t *
svn_repos__hooks_start_commit(svn_repos_t *repos,
                              apr_hash_t *hooks_env,
                              const char *user,
                              const apr_array_header_t *capabilities,
                              const char *txn_name,
                              apr_pool_t *pool);

/* Run the pre-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   TXN_NAME is the name of the transaction that is being committed.  */
svn_error_t *
svn_repos__hooks_pre_commit(svn_repos_t *repos,
                            apr_hash_t *hooks_env,
                            const char *txn_name,
                            apr_pool_t *pool);

/* Run the post-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, run SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   REV is the revision that was created as a result of the commit.  */
svn_error_t *
svn_repos__hooks_post_commit(svn_repos_t *repos,
                             apr_hash_t *hooks_env,
                             svn_revnum_t rev,
                             const char *txn_name,
                             apr_pool_t *pool);

/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   REV is the revision whose property is being changed.
   AUTHOR is the authenticated name of the user changing the prop.
   NAME is the name of the property being changed.
   NEW_VALUE is the new value of the property.
   ACTION is indicates if the property is being 'A'dded, 'M'odified,
   or 'D'eleted.

   The pre-revprop-change hook will have the new property value
   written to its stdin.  If the property is being deleted, no data
   will be written. */
svn_error_t *
svn_repos__hooks_pre_revprop_change(svn_repos_t *repos,
                                    apr_hash_t *hooks_env,
                                    svn_revnum_t rev,
                                    const char *author,
                                    const char *name,
                                    const svn_string_t *new_value,
                                    char action,
                                    apr_pool_t *pool);

/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   REV is the revision whose property was changed.
   AUTHOR is the authenticated name of the user who changed the prop.
   NAME is the name of the property that was changed, and OLD_VALUE is
   that property's value immediately before the change, or null if
   none.  ACTION indicates if the property was 'A'dded, 'M'odified,
   or 'D'eleted.

   The old value will be passed to the post-revprop hook on stdin.  If
   the property is being created, no data will be written. */
svn_error_t *
svn_repos__hooks_post_revprop_change(svn_repos_t *repos,
                                     apr_hash_t *hooks_env,
                                     svn_revnum_t rev,
                                     const char *author,
                                     const char *name,
                                     const svn_string_t *old_value,
                                     char action,
                                     apr_pool_t *pool);

/* Run the pre-lock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   PATH is the path being locked, USERNAME is the person doing it,
   COMMENT is the comment of the lock, and is treated as an empty
   string when NULL is given.  STEAL-LOCK is a flag if the user is
   stealing the lock.

   If TOKEN is non-null, set *TOKEN to a new lock token generated by
   the pre-lock hook, if any (see the pre-lock hook template for more
   information).  If TOKEN is non-null but the hook does not return
   any token, then set *TOKEN to empty string. */

svn_error_t *
svn_repos__hooks_pre_lock(svn_repos_t *repos,
                          apr_hash_t *hooks_env,
                          const char **token,
                          const char *path,
                          const char *username,
                          const char *comment,
                          svn_boolean_t steal_lock,
                          apr_pool_t *pool);

/* Run the post-lock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   PATHS is an array of paths being locked, USERNAME is the person
   who did it.  */
svn_error_t *
svn_repos__hooks_post_lock(svn_repos_t *repos,
                           apr_hash_t *hooks_env,
                           const apr_array_header_t *paths,
                           const char *username,
                           apr_pool_t *pool);

/* Run the pre-unlock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   PATH is the path being unlocked, USERNAME is the person doing it,
   TOKEN is the lock token to be unlocked which should not be NULL,
   and BREAK-LOCK is a flag if the user is breaking the lock.  */
svn_error_t *
svn_repos__hooks_pre_unlock(svn_repos_t *repos,
                            apr_hash_t *hooks_env,
                            const char *path,
                            const char *username,
                            const char *token,
                            svn_boolean_t break_lock,
                            apr_pool_t *pool);

/* Run the post-unlock hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.

   HOOKS_ENV is a hash of hook script environment information returned
   via svn_repos__parse_hooks_env() (or NULL if no such information is
   available).

   PATHS is an array of paths being unlocked, USERNAME is the person
   who did it.  */
svn_error_t *
svn_repos__hooks_post_unlock(svn_repos_t *repos,
                             apr_hash_t *hooks_env,
                             const apr_array_header_t *paths,
                             const char *username,
                             apr_pool_t *pool);


/*** Utility Functions ***/

/* Set *PREV_PATH and *PREV_REV to the path and revision which
   represent the location at which PATH in FS was located immediately
   prior to REVISION iff there was a copy operation (to PATH or one of
   its parent directories) between that previous location and
   PATH@REVISION, and set *APPEARED_REV to the first revision in which
   PATH@REVISION appeared at PATH as a result of that copy operation.

   If there was no such copy operation in that portion
   of PATH's history, set *PREV_PATH to NULL, and set *PREV_REV and
   *APPEARED_REV to SVN_INVALID_REVNUM.

   NOTE: Any of PREV_PATH, PREV_REV, and APPEARED_REV may be NULL to
   if that information is of no interest to the caller.  */
svn_error_t *
svn_repos__prev_location(svn_revnum_t *appeared_rev,
                         const char **prev_path,
                         svn_revnum_t *prev_rev,
                         svn_fs_t *fs,
                         svn_revnum_t revision,
                         const char *path,
                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_REPOS_H */
