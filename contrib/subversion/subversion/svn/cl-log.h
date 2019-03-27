/*
 * cl-log.h: Log entry receiver
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



#ifndef SVN_CL_LOG_H
#define SVN_CL_LOG_H

/*** Includes. ***/
#include <apr_pools.h>

#include "svn_types.h"

#include "private/svn_string_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* The separator between log messages. */
#define SVN_CL__LOG_SEP_STRING \
  "------------------------------------------------------------------------\n"

/* Baton for log_entry_receiver() and log_entry_receiver_xml(). */
typedef struct svn_cl__log_receiver_baton
{
  /* Client context. */
  svn_client_ctx_t *ctx;

  /* The target of the log operation. */
  const char *target_path_or_url;
  svn_opt_revision_t target_peg_revision;

  /* Don't print log message body nor its line count. */
  svn_boolean_t omit_log_message;

  /* Whether to show diffs in the log. (maps to --diff) */
  svn_boolean_t show_diff;

  /* Depth applied to diff output. */
  svn_depth_t depth;

  /* Diff arguments received from command line. */
  const char *diff_extensions;

  /* Stack which keeps track of merge revision nesting, using svn_revnum_t's */
  apr_array_header_t *merge_stack;

  /* Log message search patterns. Log entries will only be shown if the author,
   * the log message, or a changed path matches one of these patterns. */
  apr_array_header_t *search_patterns;

  /* Buffer for Unicode normalization and case folding. */
  svn_membuf_t buffer;

  /* Pool for persistent allocations. */
  apr_pool_t *pool;
} svn_cl__log_receiver_baton;

/* Implement `svn_log_entry_receiver_t', printing the logs in
 * a human-readable and machine-parseable format.
 *
 * BATON is of type `struct svn_cl__log_receiver_baton'.
 *
 * First, print a header line.  Then if CHANGED_PATHS is non-null,
 * print all affected paths in a list headed "Changed paths:\n",
 * immediately following the header line.  Then print a newline
 * followed by the message body, unless BATON->omit_log_message is true.
 */
svn_error_t *
svn_cl__log_entry_receiver(void *baton,
                           svn_log_entry_t *log_entry,
                           apr_pool_t *pool);

/* This implements `svn_log_entry_receiver_t', printing the logs in XML.
 *
 * BATON is of type `struct svn_cl__log_receiver_baton'.
 */
svn_error_t *
svn_cl__log_entry_receiver_xml(void *baton,
                               svn_log_entry_t *log_entry,
                               apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_LOG_H */
