/*
 * debug_reporter.c :  An reporter that writes the operations it does to stderr.
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


/*** ***/
#include "debug_reporter.h"

struct report_baton
{
  const svn_ra_reporter3_t *wrapped_reporter;
  void *wrapped_report_baton;

  svn_stream_t *out;
};

#define BOOLEAN_TO_WORD(condition)  ((condition) ? "True" : "False")


/*** Wrappers. ***/

static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_depth_t depth,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  struct report_baton *rb = report_baton;
  SVN_ERR(svn_stream_printf(rb->out, pool, "set_path(%s, %ld, %s, %s, %s)\n",
                            path, revision, svn_depth_to_word(depth),
                            BOOLEAN_TO_WORD(start_empty), lock_token));
  SVN_ERR(rb->wrapped_reporter->set_path(rb->wrapped_report_baton, path,
                                         revision, depth,
                                         start_empty, lock_token, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  struct report_baton *rb = report_baton;
  SVN_ERR(svn_stream_printf(rb->out, pool, "delete_path(%s)\n", path));
  SVN_ERR(rb->wrapped_reporter->delete_path(rb->wrapped_report_baton,
                                            path, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_depth_t depth,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  struct report_baton *rb = report_baton;
  SVN_ERR(svn_stream_printf(rb->out, pool,
                            "link_path(%s, %s, %ld, %s, %s, %s)\n",
                            path, url, revision, svn_depth_to_word(depth),
                            BOOLEAN_TO_WORD(start_empty), lock_token));
  SVN_ERR(rb->wrapped_reporter->link_path(rb->wrapped_report_baton, path, url,
                                          revision, depth, start_empty,
                                          lock_token, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  struct report_baton *rb = report_baton;
  SVN_ERR(svn_stream_puts(rb->out, "finish_report()\n"));
  SVN_ERR(rb->wrapped_reporter->finish_report(rb->wrapped_report_baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
  struct report_baton *rb = report_baton;
  SVN_ERR(svn_stream_puts(rb->out, "abort_report()\n"));
  SVN_ERR(rb->wrapped_reporter->abort_report(rb->wrapped_report_baton, pool));
  return SVN_NO_ERROR;
}


/*** Public interfaces. ***/
svn_error_t *
svn_ra__get_debug_reporter(const svn_ra_reporter3_t **reporter,
                           void **report_baton,
                           const svn_ra_reporter3_t *wrapped_reporter,
                           void *wrapped_report_baton,
                           apr_pool_t *pool)
{
  svn_ra_reporter3_t *tree_reporter;
  struct report_baton *rb;
  apr_file_t *errfp;
  svn_stream_t *out;

  apr_status_t apr_err = apr_file_open_stderr(&errfp, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Problem opening stderr");

  out = svn_stream_from_aprfile2(errfp, TRUE, pool);

  /* ### svn_delta_default_editor() */
  tree_reporter = apr_palloc(pool, sizeof(*tree_reporter));
  rb = apr_palloc(pool, sizeof(*rb));

  tree_reporter->set_path = set_path;
  tree_reporter->delete_path = delete_path;
  tree_reporter->link_path = link_path;
  tree_reporter->finish_report = finish_report;
  tree_reporter->abort_report = abort_report;

  rb->wrapped_reporter = wrapped_reporter;
  rb->wrapped_report_baton = wrapped_report_baton;
  rb->out = out;

  *reporter = tree_reporter;
  *report_baton = rb;

  return SVN_NO_ERROR;
}
