/*
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

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_auth.h"
#include "svn_opt.h"
#include "svn_ra.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_string.h"
#include "svn_version.h"

#include "private/svn_opt_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_cmdline_private.h"

#include "sync.h"

#include "svn_private_config.h"

#include <apr_uuid.h>

static svn_opt_subcommand_t initialize_cmd,
                            synchronize_cmd,
                            copy_revprops_cmd,
                            info_cmd,
                            help_cmd;

enum svnsync__opt {
  svnsync_opt_non_interactive = SVN_OPT_FIRST_LONGOPT_ID,
  svnsync_opt_force_interactive,
  svnsync_opt_no_auth_cache,
  svnsync_opt_auth_username,
  svnsync_opt_auth_password,
  svnsync_opt_source_username,
  svnsync_opt_source_password,
  svnsync_opt_sync_username,
  svnsync_opt_sync_password,
  svnsync_opt_config_dir,
  svnsync_opt_config_options,
  svnsync_opt_source_prop_encoding,
  svnsync_opt_disable_locking,
  svnsync_opt_version,
  svnsync_opt_trust_server_cert,
  svnsync_opt_trust_server_cert_failures_src,
  svnsync_opt_trust_server_cert_failures_dst,
  svnsync_opt_allow_non_empty,
  svnsync_opt_skip_unchanged,
  svnsync_opt_steal_lock
};

#define SVNSYNC_OPTS_DEFAULT svnsync_opt_non_interactive, \
                             svnsync_opt_force_interactive, \
                             svnsync_opt_no_auth_cache, \
                             svnsync_opt_auth_username, \
                             svnsync_opt_auth_password, \
                             svnsync_opt_trust_server_cert, \
                             svnsync_opt_trust_server_cert_failures_src, \
                             svnsync_opt_trust_server_cert_failures_dst, \
                             svnsync_opt_source_username, \
                             svnsync_opt_source_password, \
                             svnsync_opt_sync_username, \
                             svnsync_opt_sync_password, \
                             svnsync_opt_config_dir, \
                             svnsync_opt_config_options

static const svn_opt_subcommand_desc2_t svnsync_cmd_table[] =
  {
    { "initialize", initialize_cmd, { "init" },
      N_("usage: svnsync initialize DEST_URL SOURCE_URL\n"
         "\n"
         "Initialize a destination repository for synchronization from\n"
         "another repository.\n"
         "\n"
         "If the source URL is not the root of a repository, only the\n"
         "specified part of the repository will be synchronized.\n"
         "\n"
         "The destination URL must point to the root of a repository which\n"
         "has been configured to allow revision property changes.  In\n"
         "the general case, the destination repository must contain no\n"
         "committed revisions.  Use --allow-non-empty to override this\n"
         "restriction, which will cause svnsync to assume that any revisions\n"
         "already present in the destination repository perfectly mirror\n"
         "their counterparts in the source repository.  (This is useful\n"
         "when initializing a copy of a repository as a mirror of that same\n"
         "repository, for example.)\n"
         "\n"
         "You should not commit to, or make revision property changes in,\n"
         "the destination repository by any method other than 'svnsync'.\n"
         "In other words, the destination repository should be a read-only\n"
         "mirror of the source repository.\n"),
      { SVNSYNC_OPTS_DEFAULT, svnsync_opt_source_prop_encoding, 'q',
        svnsync_opt_allow_non_empty, svnsync_opt_disable_locking,
        svnsync_opt_steal_lock, 'M' } },
    { "synchronize", synchronize_cmd, { "sync" },
      N_("usage: svnsync synchronize DEST_URL [SOURCE_URL]\n"
         "\n"
         "Transfer all pending revisions to the destination from the source\n"
         "with which it was initialized.\n"
         "\n"
         "If SOURCE_URL is provided, use that as the source repository URL,\n"
         "ignoring what is recorded in the destination repository as the\n"
         "source URL.  Specifying SOURCE_URL is recommended in particular\n"
         "if untrusted users/administrators may have write access to the\n"
         "DEST_URL repository.\n"),
      { SVNSYNC_OPTS_DEFAULT, svnsync_opt_source_prop_encoding, 'q',
        svnsync_opt_disable_locking, svnsync_opt_steal_lock, 'M' } },
    { "copy-revprops", copy_revprops_cmd, { 0 },
      N_("usage:\n"
         "\n"
         "    1. svnsync copy-revprops DEST_URL [SOURCE_URL]\n"
         "    2. svnsync copy-revprops DEST_URL REV[:REV2]\n"
         "\n"
         "Copy the revision properties in a given range of revisions to the\n"
         "destination from the source with which it was initialized.  If the\n"
         "revision range is not specified, it defaults to all revisions in\n"
         "the DEST_URL repository.  Note also that the 'HEAD' revision is the\n"
         "latest in DEST_URL, not necessarily the latest in SOURCE_URL.\n"
         "\n"
         "If SOURCE_URL is provided, use that as the source repository URL,\n"
         "ignoring what is recorded in the destination repository as the\n"
         "source URL.  Specifying SOURCE_URL is recommended in particular\n"
         "if untrusted users/administrators may have write access to the\n"
         "DEST_URL repository.\n"
         "\n"
         "Unless you need to trigger the destination repositoy's revprop\n"
         "change hooks for all revision properties, it is recommended to use\n"
         "the --skip-unchanged option for best performance.\n"
         "\n"
         "Form 2 is deprecated syntax, equivalent to specifying \"-rREV[:REV2]\".\n"),
      { SVNSYNC_OPTS_DEFAULT, svnsync_opt_source_prop_encoding, 'q', 'r',
        svnsync_opt_disable_locking, svnsync_opt_steal_lock,
        svnsync_opt_skip_unchanged, 'M' } },
    { "info", info_cmd, { 0 },
      N_("usage: svnsync info DEST_URL\n"
         "\n"
         "Print information about the synchronization destination repository\n"
         "located at DEST_URL.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "help", help_cmd, { "?", "h" },
      N_("usage: svnsync help [SUBCOMMAND...]\n"
         "\n"
         "Describe the usage of this program or its subcommands.\n"),
      { 0 } },
    { NULL, NULL, { 0 }, NULL, { 0 } }
  };

static const apr_getopt_option_t svnsync_options[] =
  {
    {"quiet",          'q', 0,
                       N_("print as little as possible") },
    {"revision",       'r', 1,
                       N_("operate on revision ARG (or range ARG1:ARG2)\n"
                          "                             "
                          "A revision argument can be one of:\n"
                          "                             "
                          "    NUMBER       revision number\n"
                          "                             "
                          "    'HEAD'       latest in repository") },
    {"allow-non-empty", svnsync_opt_allow_non_empty, 0,
                       N_("allow a non-empty destination repository") },
    {"skip-unchanged", svnsync_opt_skip_unchanged, 0,
                       N_("don't copy unchanged revision properties") },
    {"non-interactive", svnsync_opt_non_interactive, 0,
                       N_("do no interactive prompting (default is to prompt\n"
                          "                             "
                          "only if standard input is a terminal device)")},
    {"force-interactive", svnsync_opt_force_interactive, 0,
                      N_("do interactive prompting even if standard input\n"
                         "                             "
                         "is not a terminal device")},
    {"no-auth-cache",  svnsync_opt_no_auth_cache, 0,
                       N_("do not cache authentication tokens") },
    {"username",       svnsync_opt_auth_username, 1,
                       N_("specify a username ARG (deprecated;\n"
                          "                             "
                          "see --source-username and --sync-username)") },
    {"password",       svnsync_opt_auth_password, 1,
                       N_("specify a password ARG (deprecated;\n"
                          "                             "
                          "see --source-password and --sync-password)") },
    {"trust-server-cert", svnsync_opt_trust_server_cert, 0,
                      N_("deprecated; same as\n"
                         "                             "
                         "--source-trust-server-cert-failures=unknown-ca\n"
                         "                             "
                         "--sync-trust-server-cert-failures=unknown-ca")},
    {"source-trust-server-cert-failures", svnsync_opt_trust_server_cert_failures_src, 1,
                      N_("with --non-interactive, accept SSL\n"
                         "                             "
                         "server certificates with failures.\n"
                         "                             "
                         "ARG is a comma-separated list of:\n"
                         "                             "
                         "- 'unknown-ca' (Unknown Authority)\n"
                         "                             "
                         "- 'cn-mismatch' (Hostname mismatch)\n"
                         "                             "
                         "- 'expired' (Expired certificate)\n"
                         "                             "
                         "- 'not-yet-valid' (Not yet valid certificate)\n"
                         "                             "
                         "- 'other' (all other not separately classified\n"
                         "                             "
                         "  certificate errors).\n"
                         "                             "
                         "Applied to the source URL.")},
    {"sync-trust-server-cert-failures", svnsync_opt_trust_server_cert_failures_dst, 1,
                       N_("Like\n"
                          "                             "
                          "--source-trust-server-cert-failures,\n"
                          "                             "
                          "but applied to the destination URL.")},
    {"source-username", svnsync_opt_source_username, 1,
                       N_("connect to source repository with username ARG") },
    {"source-password", svnsync_opt_source_password, 1,
                       N_("connect to source repository with password ARG") },
    {"sync-username",  svnsync_opt_sync_username, 1,
                       N_("connect to sync repository with username ARG") },
    {"sync-password",  svnsync_opt_sync_password, 1,
                       N_("connect to sync repository with password ARG") },
    {"config-dir",     svnsync_opt_config_dir, 1,
                       N_("read user configuration files from directory ARG")},
    {"config-option",  svnsync_opt_config_options, 1,
                       N_("set user configuration option in the format:\n"
                          "                             "
                          "    FILE:SECTION:OPTION=[VALUE]\n"
                          "                             "
                          "For example:\n"
                          "                             "
                          "    servers:global:http-library=serf")},
    {"source-prop-encoding", svnsync_opt_source_prop_encoding, 1,
                       N_("convert translatable properties from encoding ARG\n"
                          "                             "
                          "to UTF-8. If not specified, then properties are\n"
                          "                             "
                          "presumed to be encoded in UTF-8.")},
    {"disable-locking",  svnsync_opt_disable_locking, 0,
                       N_("Disable built-in locking.  Use of this option can\n"
                          "                             "
                          "corrupt the mirror unless you ensure that no other\n"
                          "                             "
                          "instance of svnsync is running concurrently.")},
    {"steal-lock",     svnsync_opt_steal_lock, 0,
                       N_("Steal locks as necessary.  Use, with caution,\n"
                          "                             "
                          "if your mirror repository contains stale locks\n"
                          "                             "
                          "and is not being concurrently accessed by another\n"
                          "                             "
                          "svnsync instance.")},
    {"memory-cache-size", 'M', 1,
                       N_("size of the extra in-memory cache in MB used to\n"
                          "                             "
                          "minimize operations for local 'file' scheme.\n")},
    {"version",        svnsync_opt_version, 0,
                       N_("show program version information")},
    {"help",           'h', 0,
                       N_("show help on a subcommand")},
    {NULL,             '?', 0,
                       N_("show help on a subcommand")},
    { 0, 0, 0, 0 }
  };

typedef struct opt_baton_t {
  svn_boolean_t non_interactive;
  struct { 
    svn_boolean_t trust_server_cert_unknown_ca;
    svn_boolean_t trust_server_cert_cn_mismatch;
    svn_boolean_t trust_server_cert_expired;
    svn_boolean_t trust_server_cert_not_yet_valid;
    svn_boolean_t trust_server_cert_other_failure;
  } src_trust, dst_trust;
  svn_boolean_t no_auth_cache;
  svn_auth_baton_t *source_auth_baton;
  svn_auth_baton_t *sync_auth_baton;
  const char *source_username;
  const char *source_password;
  const char *sync_username;
  const char *sync_password;
  const char *config_dir;
  apr_hash_t *config;
  const char *source_prop_encoding;
  svn_boolean_t disable_locking;
  svn_boolean_t steal_lock;
  svn_boolean_t quiet;
  svn_boolean_t allow_non_empty;
  svn_boolean_t skip_unchanged;
  svn_boolean_t version;
  svn_boolean_t help;
  svn_opt_revision_t start_rev;
  svn_opt_revision_t end_rev;
} opt_baton_t;




/*** Helper functions ***/


/* Cancellation callback function. */
static svn_cancel_func_t check_cancel = 0;

/* Check that the version of libraries in use match what we expect. */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_ra",    svn_ra_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}


/* Implements `svn_ra__lock_retry_func_t'. */
static svn_error_t *
lock_retry_func(void *baton,
                const svn_string_t *reposlocktoken,
                apr_pool_t *pool)
{
  return svn_cmdline_printf(pool,
                            _("Failed to get lock on destination "
                              "repos, currently held by '%s'\n"),
                            reposlocktoken->data);
}

/* Acquire a lock (of sorts) on the repository associated with the
 * given RA SESSION. This lock is just a revprop change attempt in a
 * time-delay loop. This function is duplicated by svnrdump in
 * svnrdump/load_editor.c
 */
static svn_error_t *
get_lock(const svn_string_t **lock_string_p,
         svn_ra_session_t *session,
         svn_boolean_t steal_lock,
         apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t be_atomic;
  const svn_string_t *stolen_lock;

  SVN_ERR(svn_ra_has_capability(session, &be_atomic,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                pool));
  if (! be_atomic)
    {
      /* Pre-1.7 server.  Can't lock without a race condition.
         See issue #3546.
       */
      err = svn_error_create(
              SVN_ERR_UNSUPPORTED_FEATURE, NULL,
              _("Target server does not support atomic revision property "
                "edits; consider upgrading it to 1.7 or using an external "
                "locking program"));
      svn_handle_warning2(stderr, err, "svnsync: ");
      svn_error_clear(err);
    }

  err = svn_ra__get_operational_lock(lock_string_p, &stolen_lock, session,
                                     SVNSYNC_PROP_LOCK, steal_lock,
                                     10 /* retries */, lock_retry_func, NULL,
                                     check_cancel, NULL, pool);
  if (!err && stolen_lock)
    {
      return svn_cmdline_printf(pool,
                                _("Stole lock previously held by '%s'\n"),
                                stolen_lock->data);
    }
  return err;
}


/* Baton for the various subcommands to share. */
typedef struct subcommand_baton_t {
  /* common to all subcommands */
  apr_hash_t *config;
  svn_ra_callbacks2_t source_callbacks;
  svn_ra_callbacks2_t sync_callbacks;
  svn_boolean_t quiet;
  svn_boolean_t allow_non_empty;
  svn_boolean_t skip_unchanged; /* Enable optimization for revprop changes. */
  const char *to_url;

  /* initialize, synchronize, and copy-revprops only */
  const char *source_prop_encoding;

  /* initialize only */
  const char *from_url;

  /* synchronize only */
  svn_revnum_t committed_rev;

  /* copy-revprops only */
  svn_revnum_t start_rev;
  svn_revnum_t end_rev;

} subcommand_baton_t;

typedef svn_error_t *(*with_locked_func_t)(svn_ra_session_t *session,
                                           subcommand_baton_t *baton,
                                           apr_pool_t *pool);


/* Lock the repository associated with RA SESSION, then execute the
 * given FUNC/BATON pair while holding the lock.  Finally, drop the
 * lock once it finishes.
 */
static svn_error_t *
with_locked(svn_ra_session_t *session,
            with_locked_func_t func,
            subcommand_baton_t *baton,
            svn_boolean_t steal_lock,
            apr_pool_t *pool)
{
  const svn_string_t *lock_string;
  svn_error_t *err;

  SVN_ERR(get_lock(&lock_string, session, steal_lock, pool));

  err = func(session, baton, pool);
  return svn_error_compose_create(err,
             svn_ra__release_operational_lock(session, SVNSYNC_PROP_LOCK,
                                              lock_string, pool));
}


/* Callback function for the RA session's open_tmp_file()
 * requirements.
 */
static svn_error_t *
open_tmp_file(apr_file_t **fp, void *callback_baton, apr_pool_t *pool)
{
  return svn_io_open_unique_file3(fp, NULL, NULL,
                                  svn_io_file_del_on_pool_cleanup,
                                  pool, pool);
}


/* Return SVN_NO_ERROR iff URL identifies the root directory of the
 * repository associated with RA session SESS.
 */
static svn_error_t *
check_if_session_is_at_repos_root(svn_ra_session_t *sess,
                                  const char *url,
                                  apr_pool_t *pool)
{
  const char *sess_root;

  SVN_ERR(svn_ra_get_repos_root2(sess, &sess_root, pool));

  if (strcmp(url, sess_root) == 0)
    return SVN_NO_ERROR;
  else
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Session is rooted at '%s' but the repos root is '%s'"),
       url, sess_root);
}


/* Remove the properties in TARGET_PROPS but not in SOURCE_PROPS from
 * revision REV of the repository associated with RA session SESSION.
 *
 * For REV zero, don't remove properties with the "svn:sync-" prefix.
 *
 * All allocations will be done in a subpool of POOL.
 */
static svn_error_t *
remove_props_not_in_source(svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t *source_props,
                           apr_hash_t *target_props,
                           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, target_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);

      svn_pool_clear(subpool);

      if (rev == 0 && !strncmp(propname, SVNSYNC_PROP_PREFIX,
                               sizeof(SVNSYNC_PROP_PREFIX) - 1))
        continue;

      /* Delete property if the name can't be found in SOURCE_PROPS. */
      if (! svn_hash_gets(source_props, propname))
        SVN_ERR(svn_ra_change_rev_prop2(session, rev, propname, NULL,
                                        NULL, subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Filter callback function.
 * Takes a property name KEY, and is expected to return TRUE if the property
 * should be filtered out (ie. not be copied to the target list), or FALSE if
 * not.
 */
typedef svn_boolean_t (*filter_func_t)(const char *key);

/* Make a new set of properties, by copying those properties in PROPS for which
 * the filter FILTER returns FALSE.
 *
 * The number of properties not copied will be stored in FILTERED_COUNT.
 *
 * The returned set of properties is allocated from POOL.
 */
static apr_hash_t *
filter_props(int *filtered_count, apr_hash_t *props,
             filter_func_t filter,
             apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *filtered = apr_hash_make(pool);
  *filtered_count = 0;

  for (hi = apr_hash_first(pool, props); hi ; hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);
      void *propval = apr_hash_this_val(hi);

      /* Copy all properties:
          - not matching the exclude pattern if provided OR
          - matching the include pattern if provided */
      if (!filter || !filter(propname))
        {
          svn_hash_sets(filtered, propname, propval);
        }
      else
        {
          *filtered_count += 1;
        }
    }

  return filtered;
}


/* Write the set of revision properties REV_PROPS to revision REV to the
 * repository associated with RA session SESSION.
 * Omit any properties whose names are in the svnsync property name space,
 * and set *FILTERED_COUNT to the number of properties thus omitted.
 * REV_PROPS is a hash mapping (char *)propname to (svn_string_t *)propval.
 *
 * If OLD_REV_PROPS is not NULL, skip all properties that did not change.
 * Note that this implies that hook scripts won't be triggered anymore for
 * those revprops that did not change.
 *
 * All allocations will be done in a subpool of POOL.
 */
static svn_error_t *
write_revprops(int *filtered_count,
               svn_ra_session_t *session,
               svn_revnum_t rev,
               apr_hash_t *rev_props,
               apr_hash_t *old_rev_props,
               apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  *filtered_count = 0;

  for (hi = apr_hash_first(pool, rev_props); hi; hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);
      const svn_string_t *propval = apr_hash_this_val(hi);

      svn_pool_clear(subpool);

      if (strncmp(propname, SVNSYNC_PROP_PREFIX,
                  sizeof(SVNSYNC_PROP_PREFIX) - 1) != 0)
        {
          if (old_rev_props)
            {
              /* Skip the RA call for any no-op propset. */
              const svn_string_t *old_value = svn_hash_gets(old_rev_props,
                                                            propname);
              if ((!old_value && !propval)
                  || (old_value && propval
                      && svn_string_compare(old_value, propval)))
                continue;
            }

          SVN_ERR(svn_ra_change_rev_prop2(session, rev, propname, NULL,
                                          propval, subpool));
        }
      else
        {
          *filtered_count += 1;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
log_properties_copied(svn_boolean_t syncprops_found,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  if (syncprops_found)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("Copied properties for revision %ld "
                                 "(%s* properties skipped).\n"),
                               rev, SVNSYNC_PROP_PREFIX));
  else
    SVN_ERR(svn_cmdline_printf(pool,
                               _("Copied properties for revision %ld.\n"),
                               rev));

  return SVN_NO_ERROR;
}

/* Print a notification that NORMALIZED_REV_PROPS_COUNT rev-props and
 * NORMALIZED_NODE_PROPS_COUNT node-props were normalized to LF line
 * endings, if either of those numbers is non-zero. */
static svn_error_t *
log_properties_normalized(int normalized_rev_props_count,
                          int normalized_node_props_count,
                          apr_pool_t *pool)
{
  if (normalized_rev_props_count > 0 || normalized_node_props_count > 0)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("NOTE: Normalized %s* properties "
                                 "to LF line endings (%d rev-props, "
                                 "%d node-props).\n"),
                               SVN_PROP_PREFIX,
                               normalized_rev_props_count,
                               normalized_node_props_count));
  return SVN_NO_ERROR;
}


/* Copy all the revision properties, except for those that have the
 * "svn:sync-" prefix, from revision REV of the repository associated
 * with RA session FROM_SESSION, to the repository associated with RA
 * session TO_SESSION.
 *
 * If SYNC is TRUE, then properties on the destination revision that
 * do not exist on the source revision will be removed.
 *
 * If SKIP_UNCHANGED is TRUE, skip any no-op revprop changes. This also
 * prevents hook scripts from firing for those unchanged revprops.  Has
 * no effect if SYNC is FALSE.
 *
 * If QUIET is FALSE, then log_properties_copied() is called to log that
 * properties were copied for revision REV.
 *
 * Make sure the values of svn:* revision properties use only LF (\n)
 * line ending style, correcting their values as necessary. The number
 * of properties that were normalized is returned in *NORMALIZED_COUNT.
 */
static svn_error_t *
copy_revprops(svn_ra_session_t *from_session,
              svn_ra_session_t *to_session,
              svn_revnum_t rev,
              svn_boolean_t sync,
              svn_boolean_t skip_unchanged,
              svn_boolean_t quiet,
              const char *source_prop_encoding,
              int *normalized_count,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *existing_props, *rev_props;
  int filtered_count = 0;

  /* Get the list of revision properties on REV of TARGET. We're only interested
     in the property names, but we'll get the values 'for free'. */
  if (sync)
    SVN_ERR(svn_ra_rev_proplist(to_session, rev, &existing_props, subpool));
  else
    existing_props = NULL;

  /* Get the list of revision properties on REV of SOURCE. */
  SVN_ERR(svn_ra_rev_proplist(from_session, rev, &rev_props, subpool));

  /* If necessary, normalize encoding and line ending style and return the count
     of EOL-normalized properties in int *NORMALIZED_COUNT. */
  SVN_ERR(svnsync_normalize_revprops(rev_props, normalized_count,
                                     source_prop_encoding, pool));

  /* Copy all but the svn:svnsync properties. */
  SVN_ERR(write_revprops(&filtered_count, to_session, rev, rev_props,
                         skip_unchanged ? existing_props : NULL, pool));

  /* Delete those properties that were in TARGET but not in SOURCE */
  if (sync)
    SVN_ERR(remove_props_not_in_source(to_session, rev,
                                       rev_props, existing_props, pool));

  if (! quiet)
    SVN_ERR(log_properties_copied(filtered_count > 0, rev, pool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* Return a subcommand baton allocated from POOL and populated with
   data from the provided parameters, which include the global
   OPT_BATON options structure and a handful of other options.  Not
   all parameters are used in all subcommands -- see
   subcommand_baton_t's definition for details. */
static subcommand_baton_t *
make_subcommand_baton(opt_baton_t *opt_baton,
                      const char *to_url,
                      const char *from_url,
                      svn_revnum_t start_rev,
                      svn_revnum_t end_rev,
                      apr_pool_t *pool)
{
  subcommand_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  b->config = opt_baton->config;
  b->source_callbacks.open_tmp_file = open_tmp_file;
  b->source_callbacks.auth_baton = opt_baton->source_auth_baton;
  b->sync_callbacks.open_tmp_file = open_tmp_file;
  b->sync_callbacks.auth_baton = opt_baton->sync_auth_baton;
  b->quiet = opt_baton->quiet;
  b->skip_unchanged = opt_baton->skip_unchanged;
  b->allow_non_empty = opt_baton->allow_non_empty;
  b->to_url = to_url;
  b->source_prop_encoding = opt_baton->source_prop_encoding;
  b->from_url = from_url;
  b->start_rev = start_rev;
  b->end_rev = end_rev;
  return b;
}

static svn_error_t *
open_target_session(svn_ra_session_t **to_session_p,
                    subcommand_baton_t *baton,
                    apr_pool_t *pool);


/*** `svnsync init' ***/

/* Initialize the repository associated with RA session TO_SESSION,
 * using information found in BATON.
 *
 * Implements `with_locked_func_t' interface.  The caller has
 * acquired a lock on the repository if locking is needed.
 */
static svn_error_t *
do_initialize(svn_ra_session_t *to_session,
              subcommand_baton_t *baton,
              apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  svn_string_t *from_url;
  svn_revnum_t latest, from_latest;
  const char *uuid, *root_url;
  int normalized_rev_props_count;

  /* First, sanity check to see that we're copying into a brand new
     repos.  If we aren't, and we aren't being asked to forcibly
     complete this initialization, that's a bad news.  */
  SVN_ERR(svn_ra_get_latest_revnum(to_session, &latest, pool));
  if ((latest != 0) && (! baton->allow_non_empty))
    return svn_error_create
      (APR_EINVAL, NULL,
       _("Destination repository already contains revision history; consider "
         "using --allow-non-empty if the repository's revisions are known "
         "to mirror their respective revisions in the source repository"));

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  if (from_url && (! baton->allow_non_empty))
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Destination repository is already synchronizing from '%s'"),
       from_url->data);

  /* Now fill in our bookkeeping info in the dest repository. */

  SVN_ERR(svn_ra_open4(&from_session, NULL, baton->from_url, NULL,
                       &(baton->source_callbacks), baton,
                       baton->config, pool));
  SVN_ERR(svn_ra_get_repos_root2(from_session, &root_url, pool));

  /* If we're doing a partial replay, we have to check first if the server
     supports this. */
  if (strcmp(root_url, baton->from_url) != 0)
    {
      svn_boolean_t server_supports_partial_replay;
      svn_error_t *err = svn_ra_has_capability(from_session,
                                               &server_supports_partial_replay,
                                               SVN_RA_CAPABILITY_PARTIAL_REPLAY,
                                               pool);
      if (err && err->apr_err != SVN_ERR_UNKNOWN_CAPABILITY)
        return svn_error_trace(err);

      if (err || !server_supports_partial_replay)
        return svn_error_create(SVN_ERR_RA_PARTIAL_REPLAY_NOT_SUPPORTED, err,
                                NULL);
    }

  /* If we're initializing a non-empty destination, we'll make sure
     that it at least doesn't have more revisions than the source. */
  if (latest != 0)
    {
      SVN_ERR(svn_ra_get_latest_revnum(from_session, &from_latest, pool));
      if (from_latest < latest)
        return svn_error_create
          (APR_EINVAL, NULL,
           _("Destination repository has more revisions than source "
             "repository"));
    }

  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_FROM_URL, NULL,
                                  svn_string_create(baton->from_url, pool),
                                  pool));

  SVN_ERR(svn_ra_get_uuid2(from_session, &uuid, pool));
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_FROM_UUID, NULL,
                                  svn_string_create(uuid, pool), pool));

  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                                  NULL, svn_string_createf(pool, "%ld", latest),
                                  pool));

  /* Copy all non-svnsync revprops from the LATEST rev in the source
     repository into the destination, notifying about normalized
     props, if any.  When LATEST is 0, this serves the practical
     purpose of initializing data that would otherwise be overlooked
     by the sync process (which is going to begin with r1).  When
     LATEST is not 0, this really serves merely aesthetic and
     informational purposes, keeping the output of this command
     consistent while allowing folks to see what the latest revision is.  */
  SVN_ERR(copy_revprops(from_session, to_session, latest, FALSE, FALSE,
                        baton->quiet, baton->source_prop_encoding,
                        &normalized_rev_props_count, pool));

  SVN_ERR(log_properties_normalized(normalized_rev_props_count, 0, pool));

  /* TODO: It would be nice if we could set the dest repos UUID to be
     equal to the UUID of the source repos, at least optionally.  That
     way people could check out/log/diff using a local fast mirror,
     but switch --relocate to the actual final repository in order to
     make changes...  But at this time, the RA layer doesn't have a
     way to set a UUID. */

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: init */
static svn_error_t *
initialize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  const char *to_url, *from_url;
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  to_url = APR_ARRAY_IDX(targets, 0, const char *);
  from_url = APR_ARRAY_IDX(targets, 1, const char *);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);
  if (! svn_path_is_url(from_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), from_url);

  baton = make_subcommand_baton(opt_baton, to_url, from_url, 0, 0, pool);
  SVN_ERR(open_target_session(&to_session, baton, pool));
  if (opt_baton->disable_locking)
    SVN_ERR(do_initialize(to_session, baton, pool));
  else
    SVN_ERR(with_locked(to_session, do_initialize, baton,
                        opt_baton->steal_lock, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync sync' ***/

/* Implements `svn_commit_callback2_t' interface. */
static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  subcommand_baton_t *sb = baton;

  if (! sb->quiet)
    {
      SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld.\n"),
                                 commit_info->revision));
    }

  sb->committed_rev = commit_info->revision;

  return SVN_NO_ERROR;
}


/* Set *FROM_SESSION to an RA session associated with the source
 * repository of the synchronization.  If FROM_URL is non-NULL, use it
 * as the source repository URL; otherwise, determine the source
 * repository URL by reading svn:sync- properties from the destination
 * repository (associated with TO_SESSION).  Set LAST_MERGED_REV to
 * the value of the property which records the most recently
 * synchronized revision.
 *
 * CALLBACKS is a vtable of RA callbacks to provide when creating
 * *FROM_SESSION.  CONFIG is a configuration hash.
 */
static svn_error_t *
open_source_session(svn_ra_session_t **from_session,
                    svn_string_t **last_merged_rev,
                    const char *from_url,
                    svn_ra_session_t *to_session,
                    svn_ra_callbacks2_t *callbacks,
                    apr_hash_t *config,
                    void *baton,
                    apr_pool_t *pool)
{
  apr_hash_t *props;
  svn_string_t *from_url_str, *from_uuid_str;

  SVN_ERR(svn_ra_rev_proplist(to_session, 0, &props, pool));

  from_url_str = svn_hash_gets(props, SVNSYNC_PROP_FROM_URL);
  from_uuid_str = svn_hash_gets(props, SVNSYNC_PROP_FROM_UUID);
  *last_merged_rev = svn_hash_gets(props, SVNSYNC_PROP_LAST_MERGED_REV);

  if (! from_url_str || ! from_uuid_str || ! *last_merged_rev)
    return svn_error_create
      (APR_EINVAL, NULL,
       _("Destination repository has not been initialized"));

  /* ### TODO: Should we validate that FROM_URL_STR->data matches any
     provided FROM_URL here?  */
  if (! from_url)
    SVN_ERR(svn_opt__arg_canonicalize_url(&from_url, from_url_str->data,
                                          pool));

  /* Open the session to copy the revision data. */
  SVN_ERR(svn_ra_open4(from_session, NULL, from_url, from_uuid_str->data,
                       callbacks, baton, config, pool));

  return SVN_NO_ERROR;
}

/* Set *TARGET_SESSION_P to an RA session associated with the target
 * repository of the synchronization.
 */
static svn_error_t *
open_target_session(svn_ra_session_t **target_session_p,
                    subcommand_baton_t *baton,
                    apr_pool_t *pool)
{
  svn_ra_session_t *target_session;
  SVN_ERR(svn_ra_open4(&target_session, NULL, baton->to_url, NULL,
                       &(baton->sync_callbacks), baton, baton->config, pool));
  SVN_ERR(check_if_session_is_at_repos_root(target_session, baton->to_url, pool));

  *target_session_p = target_session;
  return SVN_NO_ERROR;
}

/* Replay baton, used during synchronization. */
typedef struct replay_baton_t {
  svn_ra_session_t *from_session;
  svn_ra_session_t *to_session;
  svn_revnum_t current_revision;
  subcommand_baton_t *sb;
  svn_boolean_t has_commit_revprops_capability;
  svn_boolean_t has_atomic_revprops_capability;
  int normalized_rev_props_count;
  int normalized_node_props_count;
  const char *to_root;

#ifdef ENABLE_EV2_SHIMS
  /* Extra 'backdoor' session for fetching data *from* the target repo. */
  svn_ra_session_t *extra_to_session;
#endif
} replay_baton_t;

/* Return a replay baton allocated from POOL and populated with
   data from the provided parameters. */
static svn_error_t *
make_replay_baton(replay_baton_t **baton_p,
                  svn_ra_session_t *from_session,
                  svn_ra_session_t *to_session,
                  subcommand_baton_t *sb, apr_pool_t *pool)
{
  replay_baton_t *rb = apr_pcalloc(pool, sizeof(*rb));
  rb->from_session = from_session;
  rb->to_session = to_session;
  rb->sb = sb;

  SVN_ERR(svn_ra_get_repos_root2(to_session, &rb->to_root, pool));

#ifdef ENABLE_EV2_SHIMS
  /* Open up the extra baton.  Only needed for Ev2 shims. */
  SVN_ERR(open_target_session(&rb->extra_to_session, sb, pool));
#endif

  *baton_p = rb;
  return SVN_NO_ERROR;
}

/* Return TRUE iff KEY is the name of an svn:date or svn:author or any svnsync
 * property. Implements filter_func_t. Use with filter_props() to filter out
 * svn:date and svn:author and svnsync properties.
 */
static svn_boolean_t
filter_exclude_date_author_sync(const char *key)
{
  if (strcmp(key, SVN_PROP_REVISION_AUTHOR) == 0)
    return TRUE;
  else if (strcmp(key, SVN_PROP_REVISION_DATE) == 0)
    return TRUE;
  else if (strncmp(key, SVNSYNC_PROP_PREFIX,
                   sizeof(SVNSYNC_PROP_PREFIX) - 1) == 0)
    return TRUE;

  return FALSE;
}

/* Return FALSE iff KEY is the name of an svn:date or svn:author or any svnsync
 * property. Implements filter_func_t. Use with filter_props() to filter out
 * all properties except svn:date and svn:author and svnsync properties.
 */
static svn_boolean_t
filter_include_date_author_sync(const char *key)
{
  return ! filter_exclude_date_author_sync(key);
}


/* Return TRUE iff KEY is the name of the svn:log property.
 * Implements filter_func_t. Use with filter_props() to only exclude svn:log.
 */
static svn_boolean_t
filter_exclude_log(const char *key)
{
  if (strcmp(key, SVN_PROP_REVISION_LOG) == 0)
    return TRUE;
  else
    return FALSE;
}

/* Return FALSE iff KEY is the name of the svn:log property.
 * Implements filter_func_t. Use with filter_props() to only include svn:log.
 */
static svn_boolean_t
filter_include_log(const char *key)
{
  return ! filter_exclude_log(key);
}

#ifdef ENABLE_EV2_SHIMS
static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct replay_baton_t *rb = baton;
  svn_stream_t *fstream;
  svn_error_t *err;

  if (svn_path_is_url(path))
    path = svn_uri_skip_ancestor(rb->to_root, path, scratch_pool);
  else if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = rb->current_revision - 1;

  SVN_ERR(svn_stream_open_unique(&fstream, filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));

  err = svn_ra_get_file(rb->extra_to_session, path, base_revision,
                        fstream, NULL, NULL, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      SVN_ERR(svn_stream_close(fstream));

      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  SVN_ERR(svn_stream_close(fstream));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct replay_baton_t *rb = baton;
  svn_node_kind_t node_kind;

  if (svn_path_is_url(path))
    path = svn_uri_skip_ancestor(rb->to_root, path, scratch_pool);
  else if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = rb->current_revision - 1;

  SVN_ERR(svn_ra_check_path(rb->extra_to_session, path, base_revision,
                            &node_kind, scratch_pool));

  if (node_kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(rb->extra_to_session, path, base_revision,
                              NULL, NULL, props, result_pool));
    }
  else if (node_kind == svn_node_dir)
    {
      apr_array_header_t *tmp_props;

      SVN_ERR(svn_ra_get_dir2(rb->extra_to_session, NULL, NULL, props, path,
                              base_revision, 0 /* Dirent fields */,
                              result_pool));
      tmp_props = svn_prop_hash_to_array(*props, result_pool);
      SVN_ERR(svn_categorize_props(tmp_props, NULL, NULL, &tmp_props,
                                   result_pool));
      *props = svn_prop_array_to_hash(tmp_props, result_pool);
    }
  else
    {
      *props = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct replay_baton_t *rb = baton;

  if (svn_path_is_url(path))
    path = svn_uri_skip_ancestor(rb->to_root, path, scratch_pool);
  else if (path[0] == '/')
    path += 1;

  if (! SVN_IS_VALID_REVNUM(base_revision))
    base_revision = rb->current_revision - 1;

  SVN_ERR(svn_ra_check_path(rb->extra_to_session, path, base_revision,
                            kind, scratch_pool));

  return SVN_NO_ERROR;
}


static svn_delta_shim_callbacks_t *
get_shim_callbacks(replay_baton_t *rb,
                   apr_pool_t *result_pool)
{
  svn_delta_shim_callbacks_t *callbacks =
                            svn_delta_shim_callbacks_default(result_pool);

  callbacks->fetch_props_func = fetch_props_func;
  callbacks->fetch_kind_func = fetch_kind_func;
  callbacks->fetch_base_func = fetch_base_func;
  callbacks->fetch_baton = rb;

  return callbacks;
}
#endif


/* Callback function for svn_ra_replay_range, invoked when starting to parse
 * a replay report.
 */
static svn_error_t *
replay_rev_started(svn_revnum_t revision,
                   void *replay_baton,
                   const svn_delta_editor_t **editor,
                   void **edit_baton,
                   apr_hash_t *rev_props,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *commit_editor;
  const svn_delta_editor_t *cancel_editor;
  const svn_delta_editor_t *sync_editor;
  void *commit_baton;
  void *cancel_baton;
  void *sync_baton;
  replay_baton_t *rb = replay_baton;
  apr_hash_t *filtered;
  int filtered_count;
  int normalized_count;

  /* We set this property so that if we error out for some reason
     we can later determine where we were in the process of
     merging a revision.  If we had committed the change, but we
     hadn't finished copying the revprops we need to know that, so
     we can go back and finish the job before we move on.

     NOTE: We have to set this before we start the commit editor,
     because ra_svn doesn't let you change rev props during a
     commit. */
  SVN_ERR(svn_ra_change_rev_prop2(rb->to_session, 0,
                                  SVNSYNC_PROP_CURRENTLY_COPYING,
                                  NULL,
                                  svn_string_createf(pool, "%ld", revision),
                                  pool));

  /* The actual copy is just a replay hooked up to a commit.  Include
     all the revision properties from the source repositories, except
     'svn:author' and 'svn:date', those are not guaranteed to get
     through the editor anyway.
     If we're syncing to an non-commit-revprops capable server, filter
     out all revprops except svn:log and add them later in
     revplay_rev_finished. */
  filtered = filter_props(&filtered_count, rev_props,
                          (rb->has_commit_revprops_capability
                            ? filter_exclude_date_author_sync
                            : filter_include_log),
                          pool);

  /* svn_ra_get_commit_editor3 requires the log message to be
     set. It's possible that we didn't receive 'svn:log' here, so we
     have to set it to at least the empty string. If there's a svn:log
     property on this revision, we will write the actual value in the
     replay_rev_finished callback. */
  if (! svn_hash_gets(filtered, SVN_PROP_REVISION_LOG))
    svn_hash_sets(filtered, SVN_PROP_REVISION_LOG,
                  svn_string_create_empty(pool));

  /* If necessary, normalize encoding and line ending style. Add the number
     of properties that required EOL normalization to the overall count
     in the replay baton. */
  SVN_ERR(svnsync_normalize_revprops(filtered, &normalized_count,
                                     rb->sb->source_prop_encoding, pool));
  rb->normalized_rev_props_count += normalized_count;

#ifdef ENABLE_EV2_SHIMS
  SVN_ERR(svn_ra__register_editor_shim_callbacks(rb->to_session,
                                get_shim_callbacks(rb, pool)));
#endif
  SVN_ERR(svn_ra_get_commit_editor3(rb->to_session, &commit_editor,
                                    &commit_baton,
                                    filtered,
                                    commit_callback, rb->sb,
                                    NULL, FALSE, pool));

  /* There's one catch though, the diff shows us props we can't send
     over the RA interface, so we need an editor that's smart enough
     to filter those out for us.  */
  SVN_ERR(svnsync_get_sync_editor(commit_editor, commit_baton, revision - 1,
                                  rb->sb->to_url, rb->sb->source_prop_encoding,
                                  rb->sb->quiet, &sync_editor, &sync_baton,
                                  &(rb->normalized_node_props_count), pool));

  SVN_ERR(svn_delta_get_cancellation_editor(check_cancel, NULL,
                                            sync_editor, sync_baton,
                                            &cancel_editor,
                                            &cancel_baton,
                                            pool));
  *editor = cancel_editor;
  *edit_baton = cancel_baton;

  rb->current_revision = revision;
  return SVN_NO_ERROR;
}

/* Callback function for svn_ra_replay_range, invoked when finishing parsing
 * a replay report.
 */
static svn_error_t *
replay_rev_finished(svn_revnum_t revision,
                    void *replay_baton,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    apr_hash_t *rev_props,
                    apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  replay_baton_t *rb = replay_baton;
  apr_hash_t *filtered, *existing_props;
  int filtered_count;
  int normalized_count;
  const svn_string_t *rev_str;

  SVN_ERR(editor->close_edit(edit_baton, pool));

  /* Sanity check that we actually committed the revision we meant to. */
  if (rb->sb->committed_rev != revision)
    return svn_error_createf
             (APR_EINVAL, NULL,
              _("Commit created r%ld but should have created r%ld"),
              rb->sb->committed_rev, revision);

  SVN_ERR(svn_ra_rev_proplist(rb->to_session, revision, &existing_props,
                              subpool));


  /* Ok, we're done with the data, now we just need to copy the remaining
     'svn:date' and 'svn:author' revprops and we're all set.
     If the server doesn't support revprops-in-a-commit, we still have to
     set all revision properties except svn:log. */
  filtered = filter_props(&filtered_count, rev_props,
                          (rb->has_commit_revprops_capability
                            ? filter_include_date_author_sync
                            : filter_exclude_log),
                          subpool);

  /* If necessary, normalize encoding and line ending style, and add the number
     of EOL-normalized properties to the overall count in the replay baton. */
  SVN_ERR(svnsync_normalize_revprops(filtered, &normalized_count,
                                     rb->sb->source_prop_encoding, pool));
  rb->normalized_rev_props_count += normalized_count;

  SVN_ERR(write_revprops(&filtered_count, rb->to_session, revision, filtered,
                         NULL, subpool));

  /* Remove all extra properties in TARGET. */
  SVN_ERR(remove_props_not_in_source(rb->to_session, revision,
                                     rev_props, existing_props, subpool));

  svn_pool_clear(subpool);

  rev_str = svn_string_createf(subpool, "%ld", revision);

  /* Ok, we're done, bring the last-merged-rev property up to date. */
  SVN_ERR(svn_ra_change_rev_prop2(
           rb->to_session,
           0,
           SVNSYNC_PROP_LAST_MERGED_REV,
           NULL,
           rev_str,
           subpool));

  /* And finally drop the currently copying prop, since we're done
     with this revision. */
  SVN_ERR(svn_ra_change_rev_prop2(rb->to_session, 0,
                                  SVNSYNC_PROP_CURRENTLY_COPYING,
                                  rb->has_atomic_revprops_capability
                                    ? &rev_str : NULL,
                                  NULL, subpool));

  /* Notify the user that we copied revision properties. */
  if (! rb->sb->quiet)
    SVN_ERR(log_properties_copied(filtered_count > 0, revision, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Synchronize the repository associated with RA session TO_SESSION,
 * using information found in BATON.
 *
 * Implements `with_locked_func_t' interface.  The caller has
 * acquired a lock on the repository if locking is needed.
 */
static svn_error_t *
do_synchronize(svn_ra_session_t *to_session,
               subcommand_baton_t *baton, apr_pool_t *pool)
{
  svn_string_t *last_merged_rev;
  svn_revnum_t from_latest;
  svn_ra_session_t *from_session;
  svn_string_t *currently_copying;
  svn_revnum_t to_latest, copying, last_merged;
  svn_revnum_t start_revision, end_revision;
  replay_baton_t *rb;
  int normalized_rev_props_count = 0;

  SVN_ERR(open_source_session(&from_session, &last_merged_rev,
                              baton->from_url, to_session,
                              &(baton->source_callbacks), baton->config,
                              baton, pool));

  /* Check to see if we have revprops that still need to be copied for
     a prior revision we didn't finish copying.  But first, check for
     state sanity.  Remember, mirroring is not an atomic action,
     because revision properties are copied separately from the
     revision's contents.

     So, any time that currently-copying is not set, then
     last-merged-rev should be the HEAD revision of the destination
     repository.  That is, if we didn't fall over in the middle of a
     previous synchronization, then our destination repository should
     have exactly as many revisions in it as we've synchronized.

     Alternately, if currently-copying *is* set, it must
     be either last-merged-rev or last-merged-rev + 1, and the HEAD
     revision must be equal to either last-merged-rev or
     currently-copying. If this is not the case, somebody has meddled
     with the destination without using svnsync.
  */

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_CURRENTLY_COPYING,
                          &currently_copying, pool));

  SVN_ERR(svn_ra_get_latest_revnum(to_session, &to_latest, pool));

  last_merged = SVN_STR_TO_REV(last_merged_rev->data);

  if (currently_copying)
    {
      copying = SVN_STR_TO_REV(currently_copying->data);

      if ((copying < last_merged)
          || (copying > (last_merged + 1))
          || ((to_latest != last_merged) && (to_latest != copying)))
        {
          return svn_error_createf
            (APR_EINVAL, NULL,
             _("Revision being currently copied (%ld), last merged revision "
               "(%ld), and destination HEAD (%ld) are inconsistent; have you "
               "committed to the destination without using svnsync?"),
             copying, last_merged, to_latest);
        }
      else if (copying == to_latest)
        {
          if (copying > last_merged)
            {
              SVN_ERR(copy_revprops(from_session, to_session, to_latest, TRUE,
                                    baton->skip_unchanged, baton->quiet,
                                    baton->source_prop_encoding,
                                    &normalized_rev_props_count, pool));
              last_merged = copying;
              last_merged_rev = svn_string_create
                (apr_psprintf(pool, "%ld", last_merged), pool);
            }

          /* Now update last merged rev and drop currently changing.
             Note that the order here is significant, if we do them
             in the wrong order there are race conditions where we
             end up not being able to tell if there have been bogus
             (i.e. non-svnsync) commits to the dest repository. */

          SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                          SVNSYNC_PROP_LAST_MERGED_REV,
                                          NULL, last_merged_rev, pool));
          SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                          SVNSYNC_PROP_CURRENTLY_COPYING,
                                          NULL, NULL, pool));
        }
      /* If copying > to_latest, then we just fall through to
         attempting to copy the revision again. */
    }
  else
    {
      if (to_latest != last_merged)
        return svn_error_createf(APR_EINVAL, NULL,
                                 _("Destination HEAD (%ld) is not the last "
                                   "merged revision (%ld); have you "
                                   "committed to the destination without "
                                   "using svnsync?"),
                                 to_latest, last_merged);
    }

  /* Now check to see if there are any revisions to copy. */
  SVN_ERR(svn_ra_get_latest_revnum(from_session, &from_latest, pool));

  if (from_latest <= last_merged)
    return SVN_NO_ERROR;

  /* Ok, so there are new revisions, iterate over them copying them
     into the destination repository. */
  SVN_ERR(make_replay_baton(&rb, from_session, to_session, baton, pool));

  /* For compatibility with older svnserve versions, check first if we
     support adding revprops to the commit. */
  SVN_ERR(svn_ra_has_capability(rb->to_session,
                                &rb->has_commit_revprops_capability,
                                SVN_RA_CAPABILITY_COMMIT_REVPROPS,
                                pool));

  SVN_ERR(svn_ra_has_capability(rb->to_session,
                                &rb->has_atomic_revprops_capability,
                                SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                pool));

  start_revision = last_merged + 1;
  end_revision = from_latest;

  SVN_ERR(check_cancel(NULL));

  SVN_ERR(svn_ra_replay_range(from_session, start_revision, end_revision,
                              0, TRUE, replay_rev_started,
                              replay_rev_finished, rb, pool));

  SVN_ERR(log_properties_normalized(rb->normalized_rev_props_count
                                      + normalized_rev_props_count,
                                    rb->normalized_node_props_count,
                                    pool));


  return SVN_NO_ERROR;
}


/* SUBCOMMAND: sync */
static svn_error_t *
synchronize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url, *from_url;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  to_url = APR_ARRAY_IDX(targets, 0, const char *);
  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  if (targets->nelts == 2)
    {
      from_url = APR_ARRAY_IDX(targets, 1, const char *);
      if (! svn_path_is_url(from_url))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Path '%s' is not a URL"), from_url);
    }
  else
    {
      from_url = NULL; /* we'll read it from the destination repos */
    }

  baton = make_subcommand_baton(opt_baton, to_url, from_url, 0, 0, pool);
  SVN_ERR(open_target_session(&to_session, baton, pool));
  if (opt_baton->disable_locking)
    SVN_ERR(do_synchronize(to_session, baton, pool));
  else
    SVN_ERR(with_locked(to_session, do_synchronize, baton,
                        opt_baton->steal_lock, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync copy-revprops' ***/

/* Copy revision properties to the repository associated with RA
 * session TO_SESSION, using information found in BATON.
 *
 * Implements `with_locked_func_t' interface.  The caller has
 * acquired a lock on the repository if locking is needed.
 */
static svn_error_t *
do_copy_revprops(svn_ra_session_t *to_session,
                 subcommand_baton_t *baton, apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  svn_string_t *last_merged_rev;
  svn_revnum_t i;
  svn_revnum_t step = 1;
  int normalized_rev_props_count = 0;

  SVN_ERR(open_source_session(&from_session, &last_merged_rev,
                              baton->from_url, to_session,
                              &(baton->source_callbacks), baton->config,
                              baton, pool));

  /* An invalid revision means "last-synced" */
  if (! SVN_IS_VALID_REVNUM(baton->start_rev))
    baton->start_rev = SVN_STR_TO_REV(last_merged_rev->data);
  if (! SVN_IS_VALID_REVNUM(baton->end_rev))
    baton->end_rev = SVN_STR_TO_REV(last_merged_rev->data);

  /* Make sure we have revisions within the valid range. */
  if (baton->start_rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Cannot copy revprops for a revision (%ld) that has not "
         "been synchronized yet"), baton->start_rev);
  if (baton->end_rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Cannot copy revprops for a revision (%ld) that has not "
         "been synchronized yet"), baton->end_rev);

  /* Now, copy all the requested revisions, in the requested order. */
  step = (baton->start_rev > baton->end_rev) ? -1 : 1;
  for (i = baton->start_rev; i != baton->end_rev + step; i = i + step)
    {
      int normalized_count;
      SVN_ERR(check_cancel(NULL));
      SVN_ERR(copy_revprops(from_session, to_session, i, TRUE,
                            baton->skip_unchanged, baton->quiet,
                            baton->source_prop_encoding, &normalized_count,
                            pool));
      normalized_rev_props_count += normalized_count;
    }

  /* Notify about normalized props, if any. */
  SVN_ERR(log_properties_normalized(normalized_rev_props_count, 0, pool));

  return SVN_NO_ERROR;
}


/* Set *START_REVNUM to the revision number associated with
   START_REVISION, or to SVN_INVALID_REVNUM if START_REVISION
   represents "HEAD"; if END_REVISION is specified, set END_REVNUM to
   the revision number associated with END_REVISION or to
   SVN_INVALID_REVNUM if END_REVISION represents "HEAD"; otherwise set
   END_REVNUM to the same value as START_REVNUM.

   As a special case, if neither START_REVISION nor END_REVISION is
   specified, set *START_REVNUM to 0 and set *END_REVNUM to
   SVN_INVALID_REVNUM.

   Freak out if either START_REVISION or END_REVISION represents an
   explicit but invalid revision number. */
static svn_error_t *
resolve_revnums(svn_revnum_t *start_revnum,
                svn_revnum_t *end_revnum,
                svn_opt_revision_t start_revision,
                svn_opt_revision_t end_revision)
{
  svn_revnum_t start_rev, end_rev;

  /* Special case: neither revision is specified?  This is like
     -r0:HEAD. */
  if ((start_revision.kind == svn_opt_revision_unspecified) &&
      (end_revision.kind == svn_opt_revision_unspecified))
    {
      *start_revnum = 0;
      *end_revnum = SVN_INVALID_REVNUM;
      return SVN_NO_ERROR;
    }

  /* Get the start revision, which must be either HEAD or a number
     (which is required to be a valid one). */
  if (start_revision.kind == svn_opt_revision_head)
    {
      start_rev = SVN_INVALID_REVNUM;
    }
  else
    {
      start_rev = start_revision.value.number;
      if (! SVN_IS_VALID_REVNUM(start_rev))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Invalid revision number (%ld)"),
                                 start_rev);
    }

  /* Get the end revision, which must be unspecified (meaning,
     "same as the start_rev"), HEAD, or a number (which is
     required to be a valid one). */
  if (end_revision.kind == svn_opt_revision_unspecified)
    {
      end_rev = start_rev;
    }
  else if (end_revision.kind == svn_opt_revision_head)
    {
      end_rev = SVN_INVALID_REVNUM;
    }
  else
    {
      end_rev = end_revision.value.number;
      if (! SVN_IS_VALID_REVNUM(end_rev))
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Invalid revision number (%ld)"),
                                 end_rev);
    }

  *start_revnum = start_rev;
  *end_revnum = end_rev;
  return SVN_NO_ERROR;
}


/* SUBCOMMAND: copy-revprops */
static svn_error_t *
copy_revprops_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url = NULL;
  const char *from_url = NULL;
  svn_opt_revision_t start_revision, end_revision;
  svn_revnum_t start_rev = 0, end_rev = SVN_INVALID_REVNUM;

  /* There should be either one or two arguments left to parse. */
  if (os->argc - os->ind > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
  if (os->argc - os->ind < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* If there are two args, the last one is either a revision range or
     the source URL.  */
  if (os->argc - os->ind == 2)
    {
      const char *arg_str;

      SVN_ERR(svn_utf_cstring_to_utf8(&arg_str, os->argv[os->argc - 1],
                                      pool));

      if (! svn_path_is_url(arg_str))
        {
          /* This is the old "... TO_URL REV[:REV2]" syntax.
             Revisions come only from this argument.  (We effectively
             pop that last argument from the end of the argument list
             so svn_opt__args_to_target_array() can do its thang.) */
          os->argc--;

          if ((opt_baton->start_rev.kind != svn_opt_revision_unspecified)
              || (opt_baton->end_rev.kind != svn_opt_revision_unspecified))
            return svn_error_create(
                SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                _("Cannot specify revisions via both command-line arguments "
                  "and the --revision (-r) option"));

          start_revision.kind = svn_opt_revision_unspecified;
          end_revision.kind = svn_opt_revision_unspecified;
          if (svn_opt_parse_revision(&start_revision, &end_revision,
                                     arg_str, pool) != 0)
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("Invalid revision range '%s' provided"),
                                     arg_str);

          SVN_ERR(resolve_revnums(&start_rev, &end_rev,
                                  start_revision, end_revision));

          SVN_ERR(svn_opt__args_to_target_array(
                      &targets, os,
                      apr_array_make(pool, 1, sizeof(const char *)), pool));
          if (targets->nelts != 1)
            return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
          to_url = APR_ARRAY_IDX(targets, 0, const char *);
          from_url = NULL;
        }
    }

  if (! to_url)
    {
      /* This is the "... TO_URL SOURCE_URL" syntax.  Revisions
         come only from the --revision parameter.  */
      SVN_ERR(resolve_revnums(&start_rev, &end_rev,
                              opt_baton->start_rev, opt_baton->end_rev));

      SVN_ERR(svn_opt__args_to_target_array(
                  &targets, os,
                  apr_array_make(pool, 2, sizeof(const char *)), pool));
      if (targets->nelts < 1)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
      if (targets->nelts > 2)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
      to_url = APR_ARRAY_IDX(targets, 0, const char *);
      if (targets->nelts == 2)
        from_url = APR_ARRAY_IDX(targets, 1, const char *);
      else
        from_url = NULL;
    }

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);
  if (from_url && (! svn_path_is_url(from_url)))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), from_url);

  baton = make_subcommand_baton(opt_baton, to_url, from_url,
                                start_rev, end_rev, pool);
  SVN_ERR(open_target_session(&to_session, baton, pool));
  if (opt_baton->disable_locking)
    SVN_ERR(do_copy_revprops(to_session, baton, pool));
  else
    SVN_ERR(with_locked(to_session, do_copy_revprops, baton,
                        opt_baton->steal_lock, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync info' ***/


/* SUBCOMMAND: info */
static svn_error_t *
info_cmd(apr_getopt_t *os, void *b, apr_pool_t * pool)
{
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *targets;
  subcommand_baton_t *baton;
  const char *to_url;
  apr_hash_t *props;
  svn_string_t *from_url, *from_uuid, *last_merged_rev;

  SVN_ERR(svn_opt__args_to_target_array(&targets, os,
                                        apr_array_make(pool, 0,
                                                       sizeof(const char *)),
                                        pool));
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Get the mirror repository URL, and verify that it is URL-ish. */
  to_url = APR_ARRAY_IDX(targets, 0, const char *);
  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  /* Open an RA session to the mirror repository URL. */
  baton = make_subcommand_baton(opt_baton, to_url, NULL, 0, 0, pool);
  SVN_ERR(open_target_session(&to_session, baton, pool));

  SVN_ERR(svn_ra_rev_proplist(to_session, 0, &props, pool));

  from_url = svn_hash_gets(props, SVNSYNC_PROP_FROM_URL);

  if (! from_url)
    return svn_error_createf
      (SVN_ERR_BAD_URL, NULL,
       _("Repository '%s' is not initialized for synchronization"), to_url);

  from_uuid = svn_hash_gets(props, SVNSYNC_PROP_FROM_UUID);
  last_merged_rev = svn_hash_gets(props, SVNSYNC_PROP_LAST_MERGED_REV);

  /* Print the info. */
  SVN_ERR(svn_cmdline_printf(pool, _("Source URL: %s\n"), from_url->data));
  if (from_uuid)
    SVN_ERR(svn_cmdline_printf(pool, _("Source Repository UUID: %s\n"),
                               from_uuid->data));
  if (last_merged_rev)
    SVN_ERR(svn_cmdline_printf(pool, _("Last Merged Revision: %s\n"),
                               last_merged_rev->data));
  return SVN_NO_ERROR;
}



/*** `svnsync help' ***/


/* SUBCOMMAND: help */
static svn_error_t *
help_cmd(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;

  const char *header =
    _("general usage: svnsync SUBCOMMAND DEST_URL  [ARGS & OPTIONS ...]\n"
      "Subversion repository replication tool.\n"
      "Type 'svnsync help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnsync --version' to see the program version and RA modules.\n"
      "\n"
      "Available subcommands:\n");

  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");

  svn_stringbuf_t *version_footer = svn_stringbuf_create(ra_desc_start,
                                                         pool);

  SVN_ERR(svn_ra_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help4(os, "svnsync",
                              opt_baton ? opt_baton->version : FALSE,
                              opt_baton ? opt_baton->quiet : FALSE,
                              /*###opt_state ? opt_state->verbose :*/ FALSE,
                              version_footer->data, header,
                              svnsync_cmd_table, svnsync_options, NULL,
                              NULL, pool));

  return SVN_NO_ERROR;
}



/*** Main ***/

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  apr_array_header_t *received_opts;
  opt_baton_t opt_baton;
  svn_config_t *config;
  apr_status_t apr_err;
  apr_getopt_t *os;
  svn_error_t *err;
  int opt_id, i;
  const char *username = NULL, *source_username = NULL, *sync_username = NULL;
  const char *password = NULL, *source_password = NULL, *sync_password = NULL;
  apr_array_header_t *config_options = NULL;
  const char *source_prop_encoding = NULL;
  svn_boolean_t force_interactive = FALSE;

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  SVN_ERR(svn_ra_initialize(pool));

  /* Initialize the option baton. */
  memset(&opt_baton, 0, sizeof(opt_baton));
  opt_baton.start_rev.kind = svn_opt_revision_unspecified;
  opt_baton.end_rev.kind = svn_opt_revision_unspecified;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  if (argc <= 1)
    {
      SVN_ERR(help_cmd(NULL, NULL, pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;

  for (;;)
    {
      const char *opt_arg;
      svn_error_t* opt_err = NULL;

      apr_err = apr_getopt_long(os, svnsync_options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(help_cmd(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id)
        {
          case svnsync_opt_non_interactive:
            opt_baton.non_interactive = TRUE;
            break;

          case svnsync_opt_force_interactive:
            force_interactive = TRUE;
            break;

          case svnsync_opt_trust_server_cert: /* backwards compat */
            opt_baton.src_trust.trust_server_cert_unknown_ca = TRUE;
            opt_baton.dst_trust.trust_server_cert_unknown_ca = TRUE;
            break;

          case svnsync_opt_trust_server_cert_failures_src:
            SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVN_ERR(svn_cmdline__parse_trust_options(
                      &opt_baton.src_trust.trust_server_cert_unknown_ca,
                      &opt_baton.src_trust.trust_server_cert_cn_mismatch,
                      &opt_baton.src_trust.trust_server_cert_expired,
                      &opt_baton.src_trust.trust_server_cert_not_yet_valid,
                      &opt_baton.src_trust.trust_server_cert_other_failure,
                      opt_arg, pool));
            break;

          case svnsync_opt_trust_server_cert_failures_dst:
            SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVN_ERR(svn_cmdline__parse_trust_options(
                      &opt_baton.dst_trust.trust_server_cert_unknown_ca,
                      &opt_baton.dst_trust.trust_server_cert_cn_mismatch,
                      &opt_baton.dst_trust.trust_server_cert_expired,
                      &opt_baton.dst_trust.trust_server_cert_not_yet_valid,
                      &opt_baton.dst_trust.trust_server_cert_other_failure,
                      opt_arg, pool));
            break;

          case svnsync_opt_no_auth_cache:
            opt_baton.no_auth_cache = TRUE;
            break;

          case svnsync_opt_auth_username:
            opt_err = svn_utf_cstring_to_utf8(&username, opt_arg, pool);
            break;

          case svnsync_opt_auth_password:
            opt_err = svn_utf_cstring_to_utf8(&password, opt_arg, pool);
            break;

          case svnsync_opt_source_username:
            opt_err = svn_utf_cstring_to_utf8(&source_username, opt_arg, pool);
            break;

          case svnsync_opt_source_password:
            opt_err = svn_utf_cstring_to_utf8(&source_password, opt_arg, pool);
            break;

          case svnsync_opt_sync_username:
            opt_err = svn_utf_cstring_to_utf8(&sync_username, opt_arg, pool);
            break;

          case svnsync_opt_sync_password:
            opt_err = svn_utf_cstring_to_utf8(&sync_password, opt_arg, pool);
            break;

          case svnsync_opt_config_dir:
            {
              const char *path;
              opt_err = svn_utf_cstring_to_utf8(&path, opt_arg, pool);

              if (!opt_err)
                opt_baton.config_dir = svn_dirent_internal_style(path, pool);
            }
            break;
          case svnsync_opt_config_options:
            if (!config_options)
              config_options =
                    apr_array_make(pool, 1,
                                   sizeof(svn_cmdline__config_argument_t*));

            SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVN_ERR(svn_cmdline__parse_config_option(config_options,
                                                     opt_arg, "svnsync: ",
                                                     pool));
            break;

          case svnsync_opt_source_prop_encoding:
            opt_err = svn_utf_cstring_to_utf8(&source_prop_encoding, opt_arg,
                                              pool);
            break;

          case svnsync_opt_disable_locking:
            opt_baton.disable_locking = TRUE;
            break;

          case svnsync_opt_steal_lock:
            opt_baton.steal_lock = TRUE;
            break;

          case svnsync_opt_version:
            opt_baton.version = TRUE;
            break;

          case svnsync_opt_allow_non_empty:
            opt_baton.allow_non_empty = TRUE;
            break;

          case svnsync_opt_skip_unchanged:
            opt_baton.skip_unchanged = TRUE;
            break;

          case 'q':
            opt_baton.quiet = TRUE;
            break;

          case 'r':
            if (svn_opt_parse_revision(&opt_baton.start_rev,
                                       &opt_baton.end_rev,
                                       opt_arg, pool) != 0)
              {
                const char *utf8_opt_arg;
                SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
                return svn_error_createf(
                            SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Syntax error in revision argument '%s'"),
                            utf8_opt_arg);
              }

            /* We only allow numbers and 'HEAD'. */
            if (((opt_baton.start_rev.kind != svn_opt_revision_number) &&
                 (opt_baton.start_rev.kind != svn_opt_revision_head))
                || ((opt_baton.end_rev.kind != svn_opt_revision_number) &&
                    (opt_baton.end_rev.kind != svn_opt_revision_head) &&
                    (opt_baton.end_rev.kind != svn_opt_revision_unspecified)))
              {
                return svn_error_createf(
                          SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                          _("Invalid revision range '%s' provided"), opt_arg);
              }
            break;

          case 'M':
            if (!config_options)
              config_options =
                    apr_array_make(pool, 1,
                                   sizeof(svn_cmdline__config_argument_t*));

            SVN_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVN_ERR(svn_cmdline__parse_config_option(
                      config_options,
                      apr_psprintf(pool,
                                   "config:miscellany:memory-cache-size=%s",
                                   opt_arg),
                      NULL /* won't be used */,
                      pool));
            break;

          case '?':
          case 'h':
            opt_baton.help = TRUE;
            break;

          default:
            {
              SVN_ERR(help_cmd(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }

      if (opt_err)
        return opt_err;
    }

  if (opt_baton.help)
    subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table, "help");

  /* The --non-interactive and --force-interactive options are mutually
   * exclusive. */
  if (opt_baton.non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }
  else
    opt_baton.non_interactive = !svn_cmdline__be_interactive(
                                  opt_baton.non_interactive,
                                  force_interactive);

  /* Disallow the mixing --username/password with their --source- and
     --sync- variants.  Treat "--username FOO" as "--source-username
     FOO --sync-username FOO"; ditto for "--password FOO". */
  if ((username || password)
      && (source_username || sync_username
          || source_password || sync_password))
    {
      return svn_error_create
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot use --username or --password with any of "
           "--source-username, --source-password, --sync-username, "
           "or --sync-password.\n"));
    }
  if (username)
    {
      source_username = username;
      sync_username = username;
    }
  if (password)
    {
      source_password = password;
      sync_password = password;
    }
  opt_baton.source_username = source_username;
  opt_baton.source_password = source_password;
  opt_baton.sync_username = sync_username;
  opt_baton.sync_password = sync_password;

  /* Disallow mixing of --steal-lock and --disable-locking. */
  if (opt_baton.steal_lock && opt_baton.disable_locking)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--disable-locking and --steal-lock are "
                                "mutually exclusive"));
    }

  /* --trust-* can only be used with --non-interactive */
  if (!opt_baton.non_interactive)
    {
      if (opt_baton.src_trust.trust_server_cert_unknown_ca
          || opt_baton.src_trust.trust_server_cert_cn_mismatch
          || opt_baton.src_trust.trust_server_cert_expired
          || opt_baton.src_trust.trust_server_cert_not_yet_valid
          || opt_baton.src_trust.trust_server_cert_other_failure
          || opt_baton.dst_trust.trust_server_cert_unknown_ca
          || opt_baton.dst_trust.trust_server_cert_cn_mismatch
          || opt_baton.dst_trust.trust_server_cert_expired
          || opt_baton.dst_trust.trust_server_cert_not_yet_valid
          || opt_baton.dst_trust.trust_server_cert_other_failure)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--source-trust-server-cert-failures "
                                  "and "
                                  "--sync-trust-server-cert-failures require "
                                  "--non-interactive"));
    }

  SVN_ERR(svn_config_ensure(opt_baton.config_dir, pool));

  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_baton.version)
            {
              /* Use the "help" subcommand to handle "--version". */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", help_cmd, {0}, "",
                  {svnsync_opt_version,  /* must accept its own option */
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
          subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              SVN_ERR(help_cmd(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  for (i = 0; i < received_opts->nelts; ++i)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svnsync_options, subcommand,
                                          pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            {
              SVN_ERR(help_cmd(NULL, NULL, pool));
            }
          else
            {
              return svn_error_createf
                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                 _("Subcommand '%s' doesn't accept option '%s'\n"
                   "Type 'svnsync help %s' for usage.\n"),
                 subcommand->name, optstr, subcommand->name);
            }
        }
    }

  SVN_ERR(svn_config_get_config(&opt_baton.config, opt_baton.config_dir, pool));

  /* Update the options in the config */
  if (config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(opt_baton.config, config_options,
                                            "svnsync: ", "--config-option"));
    }

  config = svn_hash_gets(opt_baton.config, SVN_CONFIG_CATEGORY_CONFIG);

  opt_baton.source_prop_encoding = source_prop_encoding;

  check_cancel = svn_cmdline__setup_cancellation_handler();

  err = svn_cmdline_create_auth_baton2(
          &opt_baton.source_auth_baton,
          opt_baton.non_interactive,
          opt_baton.source_username,
          opt_baton.source_password,
          opt_baton.config_dir,
          opt_baton.no_auth_cache,
          opt_baton.src_trust.trust_server_cert_unknown_ca,
          opt_baton.src_trust.trust_server_cert_cn_mismatch,
          opt_baton.src_trust.trust_server_cert_expired,
          opt_baton.src_trust.trust_server_cert_not_yet_valid,
          opt_baton.src_trust.trust_server_cert_other_failure,
          config,
          check_cancel, NULL,
          pool);
  if (! err)
    err = svn_cmdline_create_auth_baton2(
            &opt_baton.sync_auth_baton,
            opt_baton.non_interactive,
            opt_baton.sync_username,
            opt_baton.sync_password,
            opt_baton.config_dir,
            opt_baton.no_auth_cache,
            opt_baton.dst_trust.trust_server_cert_unknown_ca,
            opt_baton.dst_trust.trust_server_cert_cn_mismatch,
            opt_baton.dst_trust.trust_server_cert_expired,
            opt_baton.dst_trust.trust_server_cert_not_yet_valid,
            opt_baton.dst_trust.trust_server_cert_other_failure,
            config,
            check_cancel, NULL,
            pool);
  if (! err)
    err = (*subcommand->cmd_func)(os, &opt_baton, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svnsync help' for more info"));
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
  if (svn_cmdline_init("svnsync", stderr) != EXIT_SUCCESS)
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
      svn_cmdline_handle_exit_error(err, NULL, "svnsync: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
