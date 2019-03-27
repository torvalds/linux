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

#include "svn_client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


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

  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t non_interactive; /* do no interactive prompting */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t strict;          /* do strictly what was requested */
  const char *encoding;          /* the locale/encoding of the data*/
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */ /* UTF-8! */
  const char *auth_password;     /* auth password */ /* UTF-8! */
  apr_array_header_t *targets;   /* target list from file */ /* UTF-8! */
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  svn_boolean_t stop_on_copy;    /* don't cross copies during processing */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
  svn_boolean_t all_revprops;    /* retrieve all revprops */
  svn_boolean_t no_revprops;     /* retrieve no revprops */
  apr_hash_t *revprop_table;     /* table of revision properties to get/set */
  svn_boolean_t use_merge_history; /* use/display extra merge information */
  /* trust server SSL certs that would otherwise be rejected as "untrusted" */
  svn_boolean_t trust_server_cert_unknown_ca;
  svn_boolean_t trust_server_cert_cn_mismatch;
  svn_boolean_t trust_server_cert_expired;
  svn_boolean_t trust_server_cert_not_yet_valid;
  svn_boolean_t trust_server_cert_other_failure;
  apr_array_header_t* search_patterns; /* pattern arguments for --search */
} svn_cl__opt_state_t;


typedef struct svn_cl__cmd_baton_t
{
  svn_cl__opt_state_t *opt_state;
  svn_client_ctx_t *ctx;
} svn_cl__cmd_baton_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_cl__help,
  svn_cl__null_blame,
  svn_cl__null_export,
  svn_cl__null_list,
  svn_cl__null_log,
  svn_cl__null_info;


/* See definition in main.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_cl__cmd_table[];

/* See definition in main.c for documentation. */
extern const int svn_cl__global_options[];

/* See definition in main.c for documentation. */
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



/*** Notification functions to display results on the terminal. */

/* Set *NOTIFY_FUNC_P and *NOTIFY_BATON_P to a notifier/baton for all
 * operations, allocated in POOL.
 */
svn_error_t *
svn_cl__get_notifier(svn_wc_notify_func2_t *notify_func_p,
                     void **notify_baton_p,
                     apr_pool_t *pool);

/* Make the notifier for use with BATON print the appropriate summary
 * line at the end of the output.
 */
svn_error_t *
svn_cl__notifier_mark_export(void *baton);

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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */
