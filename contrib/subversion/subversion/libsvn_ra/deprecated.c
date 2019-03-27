/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_compat.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "ra_loader.h"
#include "deprecated.h"

#include "svn_private_config.h"




/*** From ra_loader.c ***/
/*** Compatibility Wrappers ***/

/* Wrap @c svn_ra_reporter3_t in an interface that looks like
   @c svn_ra_reporter2_t, for compatibility with functions that take
   the latter.  This shields the ra-specific implementations from
   worrying about what kind of reporter they're dealing with.

   This code does not live in wrapper_template.h because that file is
   about the big changeover from a vtable-style to function-style
   interface, and does not contain the post-changeover interfaces
   that we are compatiblizing here.

   This code looks like it duplicates code in libsvn_wc/adm_crawler.c,
   but in fact it does not.  That code makes old things look like new
   things; this code makes a new thing look like an old thing. */

/* Baton for abovementioned wrapping. */
struct reporter_3in2_baton {
  const svn_ra_reporter3_t *reporter3;
  void *reporter3_baton;
};

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->set_path(b->reporter3_baton,
                                path, revision, svn_depth_infinity,
                                start_empty, lock_token, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->delete_path(b->reporter3_baton, path, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->link_path(b->reporter3_baton,
                                 path, url, revision, svn_depth_infinity,
                                 start_empty, lock_token, pool);

}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->finish_report(b->reporter3_baton, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->abort_report(b->reporter3_baton, pool);
}

/* Wrap svn_ra_reporter3_t calls in an svn_ra_reporter2_t interface.

   Note: For calls where the prototypes are exactly the same, we could
   avoid the pass-through overhead by using the function in the
   reporter returned from session->vtable->do_foo.  But the code would
   get a lot less readable, and the only benefit would be to shave a
   few instructions in a network-bound operation anyway.  So in
   delete_path(), finish_report(), and abort_report(), we cheerfully
   pass through to identical functions. */
static svn_ra_reporter2_t reporter_3in2_wrapper = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};

svn_error_t *svn_ra_open3(svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const char *uuid,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  return svn_ra_open4(session_p, NULL, repos_URL, uuid,
                      callbacks, callback_baton, config, pool);
}

svn_error_t *svn_ra_open2(svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  return svn_ra_open3(session_p, repos_URL, NULL,
                      callbacks, callback_baton, config, pool);
}

svn_error_t *svn_ra_open(svn_ra_session_t **session_p,
                         const char *repos_URL,
                         const svn_ra_callbacks_t *callbacks,
                         void *callback_baton,
                         apr_hash_t *config,
                         apr_pool_t *pool)
{
  /* Deprecated function. Copy the contents of the svn_ra_callbacks_t
     to a new svn_ra_callbacks2_t and call svn_ra_open2(). */
  svn_ra_callbacks2_t *callbacks2;
  SVN_ERR(svn_ra_create_callbacks(&callbacks2, pool));
  callbacks2->open_tmp_file = callbacks->open_tmp_file;
  callbacks2->auth_baton = callbacks->auth_baton;
  callbacks2->get_wc_prop = callbacks->get_wc_prop;
  callbacks2->set_wc_prop = callbacks->set_wc_prop;
  callbacks2->push_wc_prop = callbacks->push_wc_prop;
  callbacks2->invalidate_wc_props = callbacks->invalidate_wc_props;
  callbacks2->progress_func = NULL;
  callbacks2->progress_baton = NULL;
  return svn_ra_open2(session_p, repos_URL,
                      callbacks2, callback_baton,
                      config, pool);
}

svn_error_t *svn_ra_change_rev_prop(svn_ra_session_t *session,
                                    svn_revnum_t rev,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool)
{
  return svn_ra_change_rev_prop2(session, rev, name, NULL, value, pool);
}

svn_error_t *svn_ra_get_commit_editor2(svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *log_msg,
                                       svn_commit_callback2_t commit_callback,
                                       void *commit_baton,
                                       apr_hash_t *lock_tokens,
                                       svn_boolean_t keep_locks,
                                       apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  if (log_msg)
    svn_hash_sets(revprop_table, SVN_PROP_REVISION_LOG,
                  svn_string_create(log_msg, pool));
  return svn_ra_get_commit_editor3(session, editor, edit_baton, revprop_table,
                                   commit_callback, commit_baton,
                                   lock_tokens, keep_locks, pool);
}

svn_error_t *svn_ra_get_commit_editor(svn_ra_session_t *session,
                                      const svn_delta_editor_t **editor,
                                      void **edit_baton,
                                      const char *log_msg,
                                      svn_commit_callback_t callback,
                                      void *callback_baton,
                                      apr_hash_t *lock_tokens,
                                      svn_boolean_t keep_locks,
                                      apr_pool_t *pool)
{
  svn_commit_callback2_t callback2;
  void *callback2_baton;

  svn_compat_wrap_commit_callback(&callback2, &callback2_baton,
                                  callback, callback_baton,
                                  pool);

  return svn_ra_get_commit_editor2(session, editor, edit_baton,
                                   log_msg, callback2,
                                   callback2_baton, lock_tokens,
                                   keep_locks, pool);
}

svn_error_t *svn_ra_do_diff2(svn_ra_session_t *session,
                             const svn_ra_reporter2_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision,
                             const char *diff_target,
                             svn_boolean_t recurse,
                             svn_boolean_t ignore_ancestry,
                             svn_boolean_t text_deltas,
                             const char *versus_url,
                             const svn_delta_editor_t *diff_editor,
                             void *diff_baton,
                             apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  SVN_ERR_ASSERT(svn_path_is_empty(diff_target)
                 || svn_path_is_single_path_component(diff_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_diff(session,
                                  &(b->reporter3), &(b->reporter3_baton),
                                  revision, diff_target,
                                  SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                  ignore_ancestry, text_deltas, versus_url,
                                  diff_editor, diff_baton, pool);
}

svn_error_t *svn_ra_do_diff(svn_ra_session_t *session,
                            const svn_ra_reporter2_t **reporter,
                            void **report_baton,
                            svn_revnum_t revision,
                            const char *diff_target,
                            svn_boolean_t recurse,
                            svn_boolean_t ignore_ancestry,
                            const char *versus_url,
                            const svn_delta_editor_t *diff_editor,
                            void *diff_baton,
                            apr_pool_t *pool)
{
  SVN_ERR_ASSERT(svn_path_is_empty(diff_target)
                 || svn_path_is_single_path_component(diff_target));
  return svn_ra_do_diff2(session, reporter, report_baton, revision,
                         diff_target, recurse, ignore_ancestry, TRUE,
                         versus_url, diff_editor, diff_baton, pool);
}

svn_error_t *svn_ra_get_log(svn_ra_session_t *session,
                            const apr_array_header_t *paths,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            int limit,
                            svn_boolean_t discover_changed_paths,
                            svn_boolean_t strict_node_history,
                            svn_log_message_receiver_t receiver,
                            void *receiver_baton,
                            apr_pool_t *pool)
{
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(paths, i, const char *);
          SVN_ERR_ASSERT(*path != '/');
        }
    }

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_ra_get_log2(session, paths, start, end, limit,
                         discover_changed_paths, strict_node_history,
                         FALSE, svn_compat_log_revprops_in(pool),
                         receiver2, receiver2_baton, pool);
}

svn_error_t *svn_ra_get_file_revs(svn_ra_session_t *session,
                                  const char *path,
                                  svn_revnum_t start,
                                  svn_revnum_t end,
                                  svn_ra_file_rev_handler_t handler,
                                  void *handler_baton,
                                  apr_pool_t *pool)
{
  svn_file_rev_handler_t handler2;
  void *handler2_baton;

  SVN_ERR_ASSERT(*path != '/');

  svn_compat_wrap_file_rev_handler(&handler2, &handler2_baton,
                                   handler, handler_baton,
                                   pool);

  return svn_ra_get_file_revs2(session, path, start, end, FALSE, handler2,
                               handler2_baton, pool);
}

svn_error_t *
svn_ra_do_update2(svn_ra_session_t *session,
                  const svn_ra_reporter3_t **reporter,
                  void **report_baton,
                  svn_revnum_t revision_to_update_to,
                  const char *update_target,
                  svn_depth_t depth,
                  svn_boolean_t send_copyfrom_args,
                  const svn_delta_editor_t *update_editor,
                  void *update_baton,
                  apr_pool_t *pool)
{
  return svn_error_trace(
            svn_ra_do_update3(session,
                              reporter, report_baton,
                              revision_to_update_to, update_target,
                              depth,
                              send_copyfrom_args,
                              FALSE /* ignore_ancestry */,
                              update_editor, update_baton,
                              pool, pool));
}

svn_error_t *svn_ra_do_update(svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              svn_revnum_t revision_to_update_to,
                              const char *update_target,
                              svn_boolean_t recurse,
                              const svn_delta_editor_t *update_editor,
                              void *update_baton,
                              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  SVN_ERR_ASSERT(svn_path_is_empty(update_target)
                 || svn_path_is_single_path_component(update_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_update(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    revision_to_update_to, update_target,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    FALSE, /* no copyfrom args */
                                    FALSE /* ignore_ancestry */,
                                    update_editor, update_baton,
                                    pool, pool);
}


svn_error_t *
svn_ra_do_switch2(svn_ra_session_t *session,
                  const svn_ra_reporter3_t **reporter,
                  void **report_baton,
                  svn_revnum_t revision_to_switch_to,
                  const char *switch_target,
                  svn_depth_t depth,
                  const char *switch_url,
                  const svn_delta_editor_t *switch_editor,
                  void *switch_baton,
                  apr_pool_t *pool)
{
  return svn_error_trace(
            svn_ra_do_switch3(session,
                              reporter, report_baton,
                              revision_to_switch_to, switch_target,
                              depth,
                              switch_url,
                              FALSE /* send_copyfrom_args */,
                              TRUE /* ignore_ancestry */,
                              switch_editor, switch_baton,
                              pool, pool));
}

svn_error_t *svn_ra_do_switch(svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              svn_revnum_t revision_to_switch_to,
                              const char *switch_target,
                              svn_boolean_t recurse,
                              const char *switch_url,
                              const svn_delta_editor_t *switch_editor,
                              void *switch_baton,
                              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  SVN_ERR_ASSERT(svn_path_is_empty(switch_target)
                 || svn_path_is_single_path_component(switch_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_switch(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    revision_to_switch_to, switch_target,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    switch_url,
                                    FALSE /* send_copyfrom_args */,
                                    TRUE /* ignore_ancestry */,
                                    switch_editor, switch_baton,
                                    pool, pool);
}

svn_error_t *svn_ra_do_status(svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              const char *status_target,
                              svn_revnum_t revision,
                              svn_boolean_t recurse,
                              const svn_delta_editor_t *status_editor,
                              void *status_baton,
                              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  SVN_ERR_ASSERT(svn_path_is_empty(status_target)
                 || svn_path_is_single_path_component(status_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_status(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    status_target, revision,
                                    SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                    status_editor, status_baton, pool);
}

svn_error_t *svn_ra_get_dir(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t revision,
                            apr_hash_t **dirents,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props,
                            apr_pool_t *pool)
{
  SVN_ERR_ASSERT(*path != '/');
  return session->vtable->get_dir(session, dirents, fetched_rev, props,
                                  path, revision, SVN_DIRENT_ALL, pool);
}

svn_error_t *
svn_ra_local__deprecated_init(int abi_version,
                              apr_pool_t *pool,
                              apr_hash_t *hash)
{
  return svn_error_trace(svn_ra_local_init(abi_version, pool, hash));
}

svn_error_t *
svn_ra_svn__deprecated_init(int abi_version,
                            apr_pool_t *pool,
                            apr_hash_t *hash)
{
  return svn_error_trace(svn_ra_svn_init(abi_version, pool, hash));
}

svn_error_t *
svn_ra_serf__deprecated_init(int abi_version,
                             apr_pool_t *pool,
                             apr_hash_t *hash)
{
  return svn_error_trace(svn_ra_serf_init(abi_version, pool, hash));
}
