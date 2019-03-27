/* config_file.h : authz parsing and searching, private to libsvn_repos
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

#ifndef SVN_REPOS_CONFIG_FILE_H
#define SVN_REPOS_CONFIG_FILE_H

#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_config.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_repos.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* An opaque struct that helps making config data access resource efficient. */
typedef struct config_access_t config_access_t;

/* Return a new config access struct allocated in RESULT_POOL.
 * Try to access REPOS_HINT first when resolving URLs; may be NULL. */
config_access_t *
svn_repos__create_config_access(svn_repos_t *repos_hint,
                                apr_pool_t *result_pool);

/* Release all resources allocated while using ACCESS. */
void 
svn_repos__destroy_config_access(config_access_t *access);

/* Using ACCESS as a helper object, access the textual configuration at PATH,
 * which may be an URL or a local path.  Return content's checksum in
 * *CHECKSUM and provide its content in *STREAM.
 *
 * The access will fail if the item does not exist and MUST_EXIST is set.
 * The result has the same lifetime as ACCESS.  Use SCRATCH_POOL for
 * temporary allocations.
 */
svn_error_t *
svn_repos__get_config(svn_stream_t **stream,
                      svn_checksum_t **checksum,
                      config_access_t *access,
                      const char *path,
                      svn_boolean_t must_exist,
                      apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_CONFIG_FILE_H */
