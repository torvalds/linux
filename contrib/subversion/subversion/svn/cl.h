/*
 * cl.h:  shared stuff in the command line program
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



#ifndef SVN_CL_H
#define SVN_CL_H

/*** Includes. ***/
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_opt.h"
#include "svn_auth.h"
#include "svn_cmdline.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Option processing ***/

/* --accept actions */
typedef enum svn_cl__accept_t
{
  /* invalid accept action */
  svn_cl__accept_invalid = -2,

  /* unspecified accept action */
  svn_cl__accept_unspecified = -1,

  /* Leave conflicts alone, for later resolution. */
  svn_cl__accept_postpone,

  /* Resolve the conflict with the pre-conflict base file. */
  svn_cl__accept_base,

  /* Resolve the conflict with the current working file. */
  svn_cl__accept_working,

  /* Resolve the conflicted hunks by choosing the corresponding text
     from the pre-conflict working copy file. */
  svn_cl__accept_mine_conflict,

  /* Resolve the conflicted hunks by choosing the corresponding text
     from the post-conflict base copy file. */
  svn_cl__accept_theirs_conflict,

  /* Resolve the conflict by taking the entire pre-conflict working
     copy file. */
  svn_cl__accept_mine_full,

  /* Resolve the conflict by taking the entire post-conflict base file. */
  svn_cl__accept_theirs_full,

  /* Launch user's editor and resolve conflict with edited file. */
  svn_cl__accept_edit,

  /* Launch user's resolver and resolve conflict with edited file. */
  svn_cl__accept_launch,

  /* Use recommended resolution if available, else leave the conflict alone. */
  svn_cl__accept_recommended

} svn_cl__accept_t;

/* --accept action user input words */
#define SVN_CL__ACCEPT_POSTPONE "postpone"
#define SVN_CL__ACCEPT_BASE "base"
#define SVN_CL__ACCEPT_WORKING "working"
#define SVN_CL__ACCEPT_MINE_CONFLICT "mine-conflict"
#define SVN_CL__ACCEPT_THEIRS_CONFLICT "theirs-conflict"
#define SVN_CL__ACCEPT_MINE_FULL "mine-full"
#define SVN_CL__ACCEPT_THEIRS_FULL "theirs-full"
#define SVN_CL__ACCEPT_EDIT "edit"
#define SVN_CL__ACCEPT_LAUNCH "launch"
#define SVN_CL__ACCEPT_RECOMMENDED "recommended"

/* Return the svn_cl__accept_t value corresponding to WORD, using exact
 * case-sensitive string comparison. Return svn_cl__accept_invalid if WORD
 * is empty or is not one of the known values. */
svn_cl__accept_t
svn_cl__accept_from_word(const char *word);


/*** Mergeinfo flavors. ***/

/* --show-revs values */
typedef enum svn_cl__show_revs_t {
  svn_cl__show_revs_invalid = -1,
  svn_cl__show_revs_merged,
  svn_cl__show_revs_eligible
} svn_cl__show_revs_t;

/* --show-revs user input words */
#define SVN_CL__SHOW_REVS_MERGED   "merged"
#define SVN_CL__SHOW_REVS_ELIGIBLE "eligible"

/* Return svn_cl__show_revs_t value corresponding to word. */
svn_cl__show_revs_t
svn_cl__show_revs_from_word(const char *word);


/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  /* An array of svn_opt_revision_range_t *'s representing revisions
     ranges indicated on the command-line via the -r and -c options.
     For each range in the list, if only one revision was provided
     (-rN), its 'end' member remains 'svn_opt_revision_unspecified'.
     This array always has at least one element, even if that is a
     null range in which both ends are 'svn_opt_revision_unspecified'. */
  apr_array_header_t *revision_ranges;

  /* These are simply a copy of the range start and end values present
     in the first item of the revision_ranges list. */
  svn_opt_revision_t start_revision;
  svn_opt_revision_t end_revision;

  /* Flag which is only set if the '-c' option was used. */
  svn_boolean_t used_change_arg;

  /* Flag which is only set if the '-r' option was used. */
  svn_boolean_t used_revision_arg;

  /* Max number of log messages to get back from svn_client_log2. */
  int limit;

  /* After option processing is done, reflects the switch actually
     given on the command line, or svn_depth_unknown if none. */
  svn_depth_t depth;

  /* Was --no-unlock specified? */
  svn_boolean_t no_unlock;

  const char *message;           /* log message (not converted to UTF-8) */
  svn_boolean_t force;           /* be more forceful, as in "svn rm -f ..." */
  svn_boolean_t force_log;       /* force validity of a suspect log msg file */
  svn_boolean_t incremental;     /* yield output suitable for concatenation */
  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t non_interactive; /* do no interactive prompting */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t update;          /* contact the server for the full story */
  svn_stringbuf_t *filedata;     /* contents of file used as option data
                                    (not converted to UTF-8) */
  const char *encoding;          /* the locale/encoding of 'message' and of
                                    'filedata' (not converted to UTF-8) */
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */
  const char *auth_password;     /* auth password */
  svn_boolean_t auth_password_from_stdin; /* read password from stdin */
  const char *extensions;        /* subprocess extension args */
  apr_array_header_t *targets;   /* target list from file */
  svn_boolean_t xml;             /* output in xml, e.g., "svn log --xml" */
  svn_boolean_t no_ignore;       /* disregard default ignores & svn:ignore's */
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  struct
    {
  const char *diff_cmd;              /* the external diff command to use
                                        (not converted to UTF-8) */
  svn_boolean_t internal_diff;       /* override diff_cmd in config file */
  svn_boolean_t no_diff_added;       /* do not show diffs for deleted files */
  svn_boolean_t no_diff_deleted;     /* do not show diffs for deleted files */
  svn_boolean_t show_copies_as_adds; /* do not diff copies with their source */
  svn_boolean_t notice_ancestry;     /* notice ancestry for diff-y operations */
  svn_boolean_t summarize;           /* create a summary of a diff */
  svn_boolean_t use_git_diff_format; /* Use git's extended diff format */
  svn_boolean_t ignore_properties;   /* ignore properties */
  svn_boolean_t properties_only;     /* Show properties only */
  svn_boolean_t patch_compatible;    /* Output compatible with GNU patch */
    } diff;
  svn_boolean_t ignore_ancestry; /* ignore ancestry for merge-y operations */
  svn_boolean_t ignore_externals;/* ignore externals definitions */
  svn_boolean_t stop_on_copy;    /* don't cross copies during processing */
  svn_boolean_t dry_run;         /* try operation but make no changes */
  svn_boolean_t revprop;         /* operate on a revision property */
  const char *merge_cmd;         /* the external merge command to use
                                    (not converted to UTF-8) */
  const char *editor_cmd;        /* the external editor command to use
                                    (not converted to UTF-8) */
  svn_boolean_t record_only;     /* whether to record mergeinfo */
  const char *old_target;        /* diff target */
  const char *new_target;        /* diff target */
  svn_boolean_t relocate;        /* rewrite urls (svn switch) */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
  svn_boolean_t autoprops;       /* enable automatic properties */
  svn_boolean_t no_autoprops;    /* disable automatic properties */
  const char *native_eol;        /* override system standard eol marker */
  svn_boolean_t remove;          /* deassociate a changelist */
  apr_array_header_t *changelists; /* changelist filters */
  svn_boolean_t keep_changelists;/* don't remove changelists after commit */
  svn_boolean_t keep_local;      /* delete path only from repository */
  svn_boolean_t all_revprops;    /* retrieve all revprops */
  svn_boolean_t no_revprops;     /* retrieve no revprops */
  apr_hash_t *revprop_table;     /* table of revision properties to get/set
                                    (not converted to UTF-8) */
  svn_boolean_t parents;         /* create intermediate directories */
  svn_boolean_t use_merge_history; /* use/display extra merge information */
  svn_cl__accept_t accept_which;   /* how to handle conflicts */
  svn_cl__show_revs_t show_revs;   /* mergeinfo flavor */
  svn_depth_t set_depth;           /* new sticky ambient depth value */
  svn_boolean_t reintegrate;      /* use "reintegrate" merge-source heuristic */
  /* trust server SSL certs that would otherwise be rejected as "untrusted" */
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
  int strip; /* number of leading path components to strip */
  svn_boolean_t ignore_keywords;   /* do not expand keywords */
  svn_boolean_t reverse_diff;      /* reverse a diff (e.g. when patching) */
  svn_boolean_t ignore_whitespace; /* don't account for whitespace when
                                      patching */
  svn_boolean_t show_diff;         /* produce diff output (maps to --diff) */
  svn_boolean_t allow_mixed_rev;   /* Allow operation on mixed-revision WC */
  svn_boolean_t include_externals; /* Recurses (in)to file & dir externals */
  svn_boolean_t show_inherited_props;  /* get inherited properties */
  apr_array_header_t* search_patterns; /* pattern arguments for --search */
  svn_boolean_t mergeinfo_log;     /* show log message in mergeinfo command */
  svn_boolean_t remove_unversioned;/* remove unversioned items */
  svn_boolean_t remove_ignored;    /* remove ignored items */
  svn_boolean_t no_newline;        /* do not output the trailing newline */
  svn_boolean_t show_passwords;    /* show cached passwords */
  svn_boolean_t pin_externals;     /* pin externals to last-changed revisions */
  const char *show_item;           /* print only the given item */
  svn_boolean_t adds_as_modification; /* update 'add vs add' no tree conflict */
  svn_boolean_t vacuum_pristines; /* remove unreferenced pristines */
  svn_boolean_t list;
} svn_cl__opt_state_t;

/* Conflict stats for operations such as update and merge. */
typedef struct svn_cl__conflict_stats_t svn_cl__conflict_stats_t;

typedef struct svn_cl__cmd_baton_t
{
  svn_cl__opt_state_t *opt_state;
  svn_cl__conflict_stats_t *conflict_stats;
  svn_client_ctx_t *ctx;
} svn_cl__cmd_baton_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_cl__add,
  svn_cl__auth,
  svn_cl__blame,
  svn_cl__cat,
  svn_cl__changelist,
  svn_cl__checkout,
  svn_cl__cleanup,
  svn_cl__commit,
  svn_cl__copy,
  svn_cl__delete,
  svn_cl__diff,
  svn_cl__export,
  svn_cl__help,
  svn_cl__import,
  svn_cl__info,
  svn_cl__lock,
  svn_cl__log,
  svn_cl__list,
  svn_cl__merge,
  svn_cl__mergeinfo,
  svn_cl__mkdir,
  svn_cl__move,
  svn_cl__patch,
  svn_cl__propdel,
  svn_cl__propedit,
  svn_cl__propget,
  svn_cl__proplist,
  svn_cl__propset,
  svn_cl__relocate,
  svn_cl__revert,
  svn_cl__resolve,
  svn_cl__resolved,
  svn_cl__shelve,
  svn_cl__unshelve,
  svn_cl__shelves,
  svn_cl__status,
  svn_cl__switch,
  svn_cl__unlock,
  svn_cl__update,
  svn_cl__upgrade;


/* See definition in svn.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_cl__cmd_table[];

/* See definition in svn.c for documentation. */
extern const int svn_cl__global_options[];

/* See definition in svn.c for documentation. */
extern const apr_getopt_option_t svn_cl__options[];


/* A helper for the many subcommands that wish to merely warn when
 * invoked on an unversioned, nonexistent, or otherwise innocuously
 * errorful resource.  Meant to be wrapped with SVN_ERR().
 *
 * If ERR is null, return SVN_NO_ERROR.
 *
 * Else if ERR->apr_err is one of the error codes supplied in varargs,
 * then handle ERR as a warning (unless QUIET is true), clear ERR, and
 * return SVN_NO_ERROR, and push the value of ERR->apr_err into the
 * ERRORS_SEEN array, if ERRORS_SEEN is not NULL.
 *
 * Else return ERR.
 *
 * Typically, error codes like SVN_ERR_UNVERSIONED_RESOURCE,
 * SVN_ERR_ENTRY_NOT_FOUND, etc, are supplied in varargs.  Don't
 * forget to terminate the argument list with 0 (or APR_SUCCESS).
 */
svn_error_t *
svn_cl__try(svn_error_t *err,
            apr_array_header_t *errors_seen,
            svn_boolean_t quiet,
            ...);


/* Our cancellation callback. */
extern svn_cancel_func_t svn_cl__check_cancel;



/* Various conflict-resolution callbacks. */

/* Opaque baton type for svn_cl__conflict_func_interactive(). */
typedef struct svn_cl__interactive_conflict_baton_t
  svn_cl__interactive_conflict_baton_t;

/* Return a new, initialized, conflict stats structure, allocated in
 * POOL. */
svn_cl__conflict_stats_t *
svn_cl__conflict_stats_create(apr_pool_t *pool);

/* Update CONFLICT_STATS to reflect that a conflict on PATH_LOCAL of kind
 * CONFLICT_KIND is resolved.  (There is no support for updating the
 * 'skipped paths' stats, since skips cannot be 'resolved'.) */
void
svn_cl__conflict_stats_resolved(svn_cl__conflict_stats_t *conflict_stats,
                                const char *path_local,
                                svn_wc_conflict_kind_t conflict_kind);

/* Set *CONFLICTED_PATHS to the conflicted paths contained in CONFLICT_STATS.
 * If no conflicted path exists, set *CONFLICTED_PATHS to NULL. */
svn_error_t *
svn_cl__conflict_stats_get_paths(apr_array_header_t **conflicted_paths,
                                 svn_cl__conflict_stats_t *conflict_stats,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Print the conflict stats accumulated in CONFLICT_STATS.
 *
 * Return any error encountered during printing.
 * See also svn_cl__notifier_print_conflict_stats().
 */
svn_error_t *
svn_cl__print_conflict_stats(svn_cl__conflict_stats_t *conflict_stats,
                             apr_pool_t *scratch_pool);

/* 
 * Interactively resolve the conflict a @a CONFLICT.
 * TODO: more docs
 */
svn_error_t *
svn_cl__resolve_conflict(svn_boolean_t *quit,
                         svn_boolean_t *external_failed,
                         svn_boolean_t *printed_summary,
                         svn_client_conflict_t *conflict,
                         svn_cl__accept_t accept_which,
                         const char *editor_cmd,
                         const char *path_prefix,
                         svn_cmdline_prompt_baton_t *pb,
                         svn_cl__conflict_stats_t *conflict_stats,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool);

/* 
 * Interactively resolve conflicts for all TARGETS.
 * TODO: more docs
 */
svn_error_t *
svn_cl__walk_conflicts(apr_array_header_t *targets,
                       svn_cl__conflict_stats_t *conflict_stats,
                       svn_cl__opt_state_t *opt_state,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool);


/*** Command-line output functions -- printing to the user. ***/

/* Print out commit information found in COMMIT_INFO to the console.
 * POOL is used for temporay allocations.
 * COMMIT_INFO should not be NULL.
 *
 * This function implements svn_commit_callback2_t.
 */
svn_error_t *
svn_cl__print_commit_info(const svn_commit_info_t *commit_info,
                          void *baton,
                          apr_pool_t *pool);


/* Convert the date in DATA to a human-readable UTF-8-encoded string
 * *HUMAN_CSTRING, or set the latter to "(invalid date)" if DATA is not
 * a valid date.  DATA should be as expected by svn_time_from_cstring().
 *
 * Do all allocations in POOL.
 */
svn_error_t *
svn_cl__time_cstring_to_human_cstring(const char **human_cstring,
                                      const char *data,
                                      apr_pool_t *pool);


/* Print STATUS for PATH to stdout for human consumption.  Prints in
   abbreviated format by default, or DETAILED format if flag is set.

   When SUPPRESS_EXTERNALS_PLACEHOLDERS is set, avoid printing
   externals placeholder lines ("X lines").

   When DETAILED is set, use SHOW_LAST_COMMITTED to toggle display of
   the last-committed-revision and last-committed-author.

   If SKIP_UNRECOGNIZED is TRUE, this function will not print out
   unversioned items found in the working copy.

   When DETAILED is set, and REPOS_LOCKS is set, treat missing repository locks
   as broken WC locks.

   Increment *TEXT_CONFLICTS, *PROP_CONFLICTS, or *TREE_CONFLICTS if
   a conflict was encountered.

   Use TARGET_ABSPATH and TARGET_PATH to shorten PATH into something
   relative to the target as necessary.
*/
svn_error_t *
svn_cl__print_status(const char *target_abspath,
                     const char *target_path,
                     const char *path,
                     const svn_client_status_t *status,
                     svn_boolean_t suppress_externals_placeholders,
                     svn_boolean_t detailed,
                     svn_boolean_t show_last_committed,
                     svn_boolean_t skip_unrecognized,
                     svn_boolean_t repos_locks,
                     unsigned int *text_conflicts,
                     unsigned int *prop_conflicts,
                     unsigned int *tree_conflicts,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool);


/* Print STATUS for PATH in XML to stdout.  Use POOL for temporary
   allocations.

   Use TARGET_ABSPATH and TARGET_PATH to shorten PATH into something
   relative to the target as necessary.
 */
svn_error_t *
svn_cl__print_status_xml(const char *target_abspath,
                         const char *target_path,
                         const char *path,
                         const svn_client_status_t *status,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool);

/* Output a commit xml element to *OUTSTR.  If *OUTSTR is NULL, allocate it
   first from POOL, otherwise append to it.  If AUTHOR or DATE is
   NULL, it will be omitted. */
void
svn_cl__print_xml_commit(svn_stringbuf_t **outstr,
                         svn_revnum_t revision,
                         const char *author,
                         const char *date,
                         apr_pool_t *pool);

/* Output an XML "<lock>" element describing LOCK to *OUTSTR.  If *OUTSTR is
   NULL, allocate it first from POOL, otherwise append to it. */
void
svn_cl__print_xml_lock(svn_stringbuf_t **outstr,
                       const svn_lock_t *lock,
                       apr_pool_t *pool);

/* Do the following things that are commonly required before accessing revision
   properties.  Ensure that REVISION is specified explicitly and is not
   relative to a working-copy item.  Ensure that exactly one target is
   specified in TARGETS.  Set *URL to the URL of the target.  Return an
   appropriate error if any of those checks or operations fail. Use CTX for
   accessing the working copy
 */
svn_error_t *
svn_cl__revprop_prepare(const svn_opt_revision_t *revision,
                        const apr_array_header_t *targets,
                        const char **URL,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool);

/* Search for a merge tool command in environment variables,
   and use it to perform the merge of the four given files.
   WC_PATH is the path of the file that is in conflict, relative
   to the merge target.
   Use POOL for all allocations.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.

   Upon success, set *REMAINS_IN_CONFLICT to indicate whether the
   merge result contains conflict markers.
   */
svn_error_t *
svn_cl__merge_file_externally(const char *base_path,
                              const char *their_path,
                              const char *my_path,
                              const char *merged_path,
                              const char *wc_path,
                              apr_hash_t *config,
                              svn_boolean_t *remains_in_conflict,
                              apr_pool_t *pool);

/* Like svn_cl__merge_file_externally, but using a built-in merge tool
 * with help from an external editor specified by EDITOR_CMD. */
svn_error_t *
svn_cl__merge_file(svn_boolean_t *remains_in_conflict,
                   const char *base_path,
                   const char *their_path,
                   const char *my_path,
                   const char *merged_path,
                   const char *wc_path,
                   const char *path_prefix,
                   const char *editor_cmd,
                   apr_hash_t *config,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool);


/*** Notification functions to display results on the terminal. */

/* Set *NOTIFY_FUNC_P and *NOTIFY_BATON_P to a notifier/baton for all
 * operations, allocated in POOL.
 */
svn_error_t *
svn_cl__get_notifier(svn_wc_notify_func2_t *notify_func_p,
                     void **notify_baton_p,
                     svn_cl__conflict_stats_t *conflict_stats,
                     apr_pool_t *pool);

/* Make the notifier for use with BATON print the appropriate summary
 * line at the end of the output.
 */
svn_error_t *
svn_cl__notifier_mark_checkout(void *baton);

/* Make the notifier for use with BATON print the appropriate summary
 * line at the end of the output.
 */
svn_error_t *
svn_cl__notifier_mark_export(void *baton);

/* Make the notifier for use with BATON print the appropriate notifications
 * for a wc to repository copy
 */
svn_error_t *
svn_cl__notifier_mark_wc_to_repos_copy(void *baton);

/* Baton for use with svn_cl__check_externals_failed_notify_wrapper(). */
struct svn_cl__check_externals_failed_notify_baton
{
  svn_wc_notify_func2_t wrapped_func; /* The "real" notify_func2. */
  void *wrapped_baton;                /* The "real" notify_func2 baton. */
  svn_boolean_t had_externals_error;  /* Did something fail in an external? */
};

/* Notification function wrapper (implements `svn_wc_notify_func2_t').
   Use with an svn_cl__check_externals_failed_notify_baton BATON. */
void
svn_cl__check_externals_failed_notify_wrapper(void *baton,
                                              const svn_wc_notify_t *n,
                                              apr_pool_t *pool);

/* Print the conflict stats accumulated in BATON, which is the
 * notifier baton from svn_cl__get_notifier().  This is just like
 * calling svn_cl__print_conflict_stats().
 *
 * Return any error encountered during printing.
 */
svn_error_t *
svn_cl__notifier_print_conflict_stats(void *baton, apr_pool_t *scratch_pool);


/*** Log message callback stuffs. ***/

/* Allocate in POOL a baton for use with svn_cl__get_log_message().

   OPT_STATE is the set of command-line options given.

   BASE_DIR is a directory in which to create temporary files if an
   external editor is used to edit the log message.  If BASE_DIR is
   NULL, the current working directory (`.') will be used, and
   therefore the user must have the proper permissions on that
   directory.  ### todo: What *should* happen in the NULL case is that
   we ask APR to tell us where a suitable tmp directory is (like, /tmp
   on Unix and C:\Windows\Temp on Win32 or something), and use it.
   But APR doesn't yet have that capability.

   CONFIG is a client configuration hash of svn_config_t * items keyed
   on config categories, and may be NULL.

   NOTE: While the baton itself will be allocated from POOL, the items
   add to it are added by reference, not duped into POOL!*/
svn_error_t *
svn_cl__make_log_msg_baton(void **baton,
                           svn_cl__opt_state_t *opt_state,
                           const char *base_dir,
                           apr_hash_t *config,
                           apr_pool_t *pool);

/* A function of type svn_client_get_commit_log3_t. */
svn_error_t *
svn_cl__get_log_message(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        void *baton,
                        apr_pool_t *pool);

/* Handle the cleanup of a log message, using the data in the
   LOG_MSG_BATON, in the face of COMMIT_ERR.  This may mean removing a
   temporary file left by an external editor, or it may be a complete
   no-op.  COMMIT_ERR may be NULL to indicate to indicate that the
   function should act as though no commit error occurred. Use POOL
   for temporary allocations.

   All error returns from this function are guaranteed to at least
   include COMMIT_ERR, and perhaps additional errors attached to the
   end of COMMIT_ERR's chain.  */
svn_error_t *
svn_cl__cleanup_log_msg(void *log_msg_baton,
                        svn_error_t *commit_err,
                        apr_pool_t *pool);

/* Add a message about --force if appropriate */
svn_error_t *
svn_cl__may_need_force(svn_error_t *err);

/* Write the STRING to the stdio STREAM, returning an error if it fails.

   This function is equal to svn_cmdline_fputs() minus the utf8->local
   encoding translation.  */
svn_error_t *
svn_cl__error_checked_fputs(const char *string, FILE* stream);

/* If STRING is non-null, append it, wrapped in a simple XML CDATA element
   named TAGNAME, to the string SB.  Use POOL for temporary allocations. */
void
svn_cl__xml_tagged_cdata(svn_stringbuf_t **sb,
                         apr_pool_t *pool,
                         const char *tagname,
                         const char *string);

/* Print the XML prolog and document root element start-tag to stdout, using
   TAGNAME as the root element name.  Use POOL for temporary allocations. */
svn_error_t *
svn_cl__xml_print_header(const char *tagname, apr_pool_t *pool);

/* Print the XML document root element end-tag to stdout, using TAGNAME as the
   root element name.  Use POOL for temporary allocations. */
svn_error_t *
svn_cl__xml_print_footer(const char *tagname, apr_pool_t *pool);


/* For use in XML output, return a non-localised string representation
 * of KIND, being "none" or "dir" or "file" or, in any other case,
 * the empty string. */
const char *
svn_cl__node_kind_str_xml(svn_node_kind_t kind);

/* Return a (possibly localised) string representation of KIND, being "none" or
   "dir" or "file" or, in any other case, the empty string. */
const char *
svn_cl__node_kind_str_human_readable(svn_node_kind_t kind);


/** Provides an XML name for a given OPERATION.
 * Note: POOL is currently not used.
 */
const char *
svn_cl__operation_str_xml(svn_wc_operation_t operation, apr_pool_t *pool);

/** Return a possibly localized human readable string for
 * a given OPERATION.
 * Note: POOL is currently not used.
 */
const char *
svn_cl__operation_str_human_readable(svn_wc_operation_t operation,
                                     apr_pool_t *pool);


/* What use is a property name intended for.
   Used by svn_cl__check_svn_prop_name to customize error messages. */
typedef enum svn_cl__prop_use_e
  {
    svn_cl__prop_use_set,       /* setting the property */
    svn_cl__prop_use_edit,      /* editing the property */
    svn_cl__prop_use_use        /* using the property name */
  }
svn_cl__prop_use_t;

/* If PROPNAME looks like but is not identical to one of the svn:
 * poperties, raise an error and suggest a better spelling. Names that
 * raise errors look like this:
 *
 *   - start with svn: but do not exactly match a known property; or,
 *   - start with a 3-letter prefix that differs in only one letter
 *     from "svn:", and the rest exactly matches a known propery.
 *
 * If REVPROP is TRUE, only check revision property names; otherwise
 * only check node property names.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_cl__check_svn_prop_name(const char *propname,
                            svn_boolean_t revprop,
                            svn_cl__prop_use_t prop_use,
                            apr_pool_t *scratch_pool);

/* If PROPNAME is one of the svn: properties with a boolean value, and
 * PROPVAL looks like an attempt to turn the property off (i.e., it's
 * "off", "no", "false", or ""), then print a warning to the user that
 * setting the property to this value might not do what they expect.
 * Perform temporary allocations in POOL.
 */
void
svn_cl__check_boolean_prop_val(const char *propname,
                               const char *propval,
                               apr_pool_t *pool);

/* De-streamifying wrapper around svn_client_get_changelists(), which
   is called for each target in TARGETS to populate *PATHS (a list of
   paths assigned to one of the CHANGELISTS.
   If all targets are to be included, may set *PATHS to TARGETS without
   reallocating. */
svn_error_t *
svn_cl__changelist_paths(apr_array_header_t **paths,
                         const apr_array_header_t *changelists,
                         const apr_array_header_t *targets,
                         svn_depth_t depth,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/* Like svn_client_args_to_target_array() but, if the only error is that some
 * arguments are reserved file names, then print warning messages for those
 * targets, store the rest of the targets in TARGETS_P and return success. */
svn_error_t *
svn_cl__args_to_target_array_print_reserved(apr_array_header_t **targets_p,
                                            apr_getopt_t *os,
                                            const apr_array_header_t *known_targets,
                                            svn_client_ctx_t *ctx,
                                            svn_boolean_t keep_dest_origpath_on_truepath_collision,
                                            apr_pool_t *pool);

/* Return a string showing a conflicted node's kind, URL and revision,
 * to the extent that that information is available. If REPOS_ROOT_URL or
 * REPOS_RELPATH are NULL, this prints just a 'none' node kind.
 * WC_REPOS_ROOT_URL should reflect the target working copy's repository
 * root URL. If the node is from that same URL, the printed URL is abbreviated
 * to caret notation (^/). WC_REPOS_ROOT_URL may be NULL, in which case
 * this function tries to print the conflicted node's complete URL. */
const char *
svn_cl__node_description(const char *repos_root_url,
                         const char *repos_relpath,
                         svn_revnum_t peg_rev,
                         svn_node_kind_t node_kind,
                         const char *wc_repos_root_URL,
                         apr_pool_t *pool);

/* Return, in @a *true_targets_p, a shallow copy of @a targets with any
 * empty peg revision specifier snipped off the end of each element.  If any
 * target has a non-empty peg revision specifier, throw an error.  The user
 * may have specified a peg revision where it doesn't make sense to do so,
 * or may have forgotten to escape an '@' character in a filename.
 *
 * This function is useful for subcommands for which peg revisions
 * do not make any sense. Such subcommands still need to allow an empty
 * peg revision to be specified on the command line so that users of
 * the command line client can consistently escape '@' characters
 * in filenames by appending an '@' character, regardless of the
 * subcommand being used.
 *
 * It is safe to pass the address of @a targets as @a true_targets_p.
 *
 * Do all allocations in @a pool. */
svn_error_t *
svn_cl__eat_peg_revisions(apr_array_header_t **true_targets_p,
                          const apr_array_header_t *targets,
                          apr_pool_t *pool);

/* Return an error if TARGETS contains a mixture of URLs and paths; otherwise
 * return SVN_NO_ERROR. */
svn_error_t *
svn_cl__assert_homogeneous_target_type(const apr_array_header_t *targets);

/* Return an error if TARGETS contains a URL; otherwise return SVN_NO_ERROR. */
svn_error_t *
svn_cl__check_targets_are_local_paths(const apr_array_header_t *targets);

/* Return an error if TARGET is a URL; otherwise return SVN_NO_ERROR. */
svn_error_t *
svn_cl__check_target_is_local_path(const char *target);

/* Return a copy of PATH, converted to the local path style, skipping
 * PARENT_PATH if it is non-null and is a parent of or equal to PATH.
 *
 * This function assumes PARENT_PATH and PATH are both absolute "dirents"
 * or both relative "dirents". */
const char *
svn_cl__local_style_skip_ancestor(const char *parent_path,
                                  const char *path,
                                  apr_pool_t *pool);

/* If the user is setting a mime-type to mark one of the TARGETS as binary,
 * as determined by property name PROPNAME and value PROPVAL, then check
 * whether Subversion's own binary-file detection recognizes the target as
 * a binary file. If Subversion doesn't consider the target to be a binary
 * file, assume the user is making an error and print a warning to inform
 * the user that some operations might fail on the file in the future. */
svn_error_t *
svn_cl__propset_print_binary_mime_type_warning(apr_array_header_t *targets,
                                               const char *propname,
                                               const svn_string_t *propval,
                                               apr_pool_t *scratch_pool);

/* A wrapper around the deprecated svn_client_merge_reintegrate. */
svn_error_t *
svn_cl__deprecated_merge_reintegrate(const char *source_path_or_url,
                                     const svn_opt_revision_t *src_peg_revision,
                                     const char *target_wcpath,
                                     svn_boolean_t dry_run,
                                     const apr_array_header_t *merge_options,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);


/* Forward declaration of the similarity check context. */
typedef struct svn_cl__simcheck_context_t svn_cl__simcheck_context_t;

/* Token definition for the similarity check. */
typedef struct svn_cl__simcheck_t
{
  /* The token we're checking for similarity. */
  svn_string_t token;

  /* User data associated with this token. */
  const void *data;

  /*
   * The following fields are populated by svn_cl__similarity_check.
   */

  /* Similarity score [0..SVN_STRING__SIM_RANGE_MAX] */
  apr_size_t score;

  /* Number of characters of difference from the key. */
  apr_size_t diff;

  /* Similarity check context (private) */
  svn_cl__simcheck_context_t *context;
} svn_cl__simcheck_t;

/* Find the entries in TOKENS that are most similar to KEY.
 * TOKEN_COUNT is the number of entries in the (mutable) TOKENS array.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * On return, the TOKENS array will be sorted according to similarity
 * to KEY, in descending order. The return value will be zero if the
 * first token is an exact match; otherwise, it will be one more than
 * the number of tokens that are at least two-thirds similar to KEY.
 */
apr_size_t
svn_cl__similarity_check(const char *key,
                         svn_cl__simcheck_t **tokens,
                         apr_size_t token_count,
                         apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */
