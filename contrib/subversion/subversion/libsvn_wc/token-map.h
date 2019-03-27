/**
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
 *
 * This header is parsed by transform-sql.py to allow SQLite
 * statements to refer to string values by symbolic names.
 */

#ifndef SVN_WC_TOKEN_MAP_H
#define SVN_WC_TOKEN_MAP_H

#include "svn_types.h"
#include "wc_db.h"
#include "private/svn_token.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The kind values used on NODES */
static const svn_token_map_t kind_map[] = {
  { "file", svn_node_file }, /* MAP_FILE */
  { "dir", svn_node_dir }, /* MAP_DIR */
  { "symlink", svn_node_symlink }, /* MAP_SYMLINK */
  { "unknown", svn_node_unknown }, /* MAP_UNKNOWN */
  { NULL }
};

/* Like kind_map, but also supports 'none' */
static const svn_token_map_t kind_map_none[] = {
  { "none", svn_node_none },
  { "file", svn_node_file },
  { "dir", svn_node_dir },
  { "symlink", svn_node_symlink },
  { "unknown", svn_node_unknown },
  { NULL }
};

/* Note: we only decode presence values from the database. These are a
   subset of all the status values. */
static const svn_token_map_t presence_map[] = {
  { "normal", svn_wc__db_status_normal }, /* MAP_NORMAL */
  { "server-excluded", svn_wc__db_status_server_excluded }, /* MAP_SERVER_EXCLUDED */
  { "excluded", svn_wc__db_status_excluded }, /* MAP_EXCLUDED */
  { "not-present", svn_wc__db_status_not_present }, /* MAP_NOT_PRESENT */
  { "incomplete", svn_wc__db_status_incomplete }, /* MAP_INCOMPLETE */
  { "base-deleted", svn_wc__db_status_base_deleted }, /* MAP_BASE_DELETED */
  { NULL }
};

/* The subset of svn_depth_t used in the database. */
static const svn_token_map_t depth_map[] = {
  { "unknown", svn_depth_unknown }, /* MAP_DEPTH_UNKNOWN */
  { "empty", svn_depth_empty },
  { "files", svn_depth_files },
  { "immediates", svn_depth_immediates },
  { "infinity", svn_depth_infinity }, /* MAP_DEPTH_INFINITY */
  { NULL }
};

#ifdef __cplusplus
}
#endif

#endif
