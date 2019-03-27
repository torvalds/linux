/*
 * svn_log.h: Functions for assembling entries for server-side logs.
 *            See also tools/server-side/svn_server_log_parse.py .
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

#ifndef SVN_LOG_H
#define SVN_LOG_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_mergeinfo.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Return a log string for a reparent action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__reparent(const char *path, apr_pool_t *pool);

/**
 * Return a log string for a change-rev-prop action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__change_rev_prop(svn_revnum_t rev, const char *name, apr_pool_t *pool);

/**
 * Return a log string for a rev-proplist action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__rev_proplist(svn_revnum_t rev, apr_pool_t *pool);

/**
 * Return a log string for a rev-prop action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__rev_prop(svn_revnum_t rev, const char *name, apr_pool_t *pool);

/**
 * Return a log string for a commit action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__commit(svn_revnum_t rev, apr_pool_t *pool);

/**
 * Return a log string for a get-file action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_file(const char *path, svn_revnum_t rev,
                  svn_boolean_t want_contents, svn_boolean_t want_props,
                  apr_pool_t *pool);

/**
 * Return a log string for a get-dir action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_dir(const char *path, svn_revnum_t rev,
                 svn_boolean_t want_contents, svn_boolean_t want_props,
                 apr_uint32_t dirent_fields,
                 apr_pool_t *pool);

/**
 * Return a log string for a get-mergeinfo action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_mergeinfo(const apr_array_header_t *paths,
                       svn_mergeinfo_inheritance_t inherit,
                       svn_boolean_t include_descendants,
                       apr_pool_t *pool);

/**
 * Return a log string for a checkout action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__checkout(const char *path, svn_revnum_t rev, svn_depth_t depth,
                  apr_pool_t *pool);

/**
 * Return a log string for an update action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__update(const char *path, svn_revnum_t rev, svn_depth_t depth,
                svn_boolean_t send_copyfrom_args,
                apr_pool_t *pool);

/**
 * Return a log string for a switch action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__switch(const char *path, const char *dst_path, svn_revnum_t revnum,
                svn_depth_t depth, apr_pool_t *pool);

/**
 * Return a log string for a status action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__status(const char *path, svn_revnum_t rev, svn_depth_t depth,
                apr_pool_t *pool);

/**
 * Return a log string for a diff action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__diff(const char *path, svn_revnum_t from_revnum,
              const char *dst_path, svn_revnum_t revnum,
              svn_depth_t depth, svn_boolean_t ignore_ancestry,
              apr_pool_t *pool);

/**
 * Return a log string for a log action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__log(const apr_array_header_t *paths,
             svn_revnum_t start, svn_revnum_t end,
             int limit, svn_boolean_t discover_changed_paths,
             svn_boolean_t strict_node_history,
             svn_boolean_t include_merged_revisions,
             const apr_array_header_t *revprops, apr_pool_t *pool);

/**
 * Return a log string for a get-locations action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_locations(const char *path, svn_revnum_t peg_revision,
                       const apr_array_header_t *location_revisions,
                       apr_pool_t *pool);

/**
 * Return a log string for a get-location-segments action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_location_segments(const char *path, svn_revnum_t peg_revision,
                               svn_revnum_t start, svn_revnum_t end,
                               apr_pool_t *pool);

/**
 * Return a log string for a get-file-revs action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__get_file_revs(const char *path, svn_revnum_t start, svn_revnum_t end,
                       svn_boolean_t include_merged_revisions,
                       apr_pool_t *pool);

/**
 * Return a log string for a lock action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__lock(apr_hash_t *targets, svn_boolean_t steal,
              apr_pool_t *pool);

/**
 * Return a log string for an unlock action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__unlock(apr_hash_t *targets, svn_boolean_t break_lock,
                apr_pool_t *pool);

/**
 * Return a log string for a lock action on only one path; this is
 * just a convenience wrapper around svn_log__lock().
 *
 * @since New in 1.6.
 */
const char *
svn_log__lock_one_path(const char *path, svn_boolean_t steal,
                       apr_pool_t *pool);

/**
 * Return a log string for an unlock action on only one path; this is
 * just a convenience wrapper around svn_log__unlock().
 *
 * @since New in 1.6.
 */
const char *
svn_log__unlock_one_path(const char *path, svn_boolean_t break_lock,
                         apr_pool_t *pool);

/**
 * Return a log string for a replay action.
 *
 * @since New in 1.6.
 */
const char *
svn_log__replay(const char *path, svn_revnum_t rev, apr_pool_t *pool);

/**
 * Return a log string for a get-inherited-props action.
 *
 * @since New in 1.8.
 */
const char *
svn_log__get_inherited_props(const char *path,
                             svn_revnum_t rev,
                             apr_pool_t *pool);

/**
 * Return a log string for a list action.
 *
 * @since New in 1.10.
 */
const char *
svn_log__list(const char *path, svn_revnum_t revision,
              apr_array_header_t *patterns, svn_depth_t depth,
              apr_uint32_t dirent_fields, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LOG_H */
