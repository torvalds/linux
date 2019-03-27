/*
 * ra_local.h : shared internal declarations for ra_local module
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

#ifndef SVN_LIBSVN_RA_LOCAL_H
#define SVN_LIBSVN_RA_LOCAL_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Structures **/

/* A baton which represents a single ra_local session. */
typedef struct svn_ra_local__session_baton_t
{
  /* The user accessing the repository. */
  const char *username;

  /* The URL of the session, split into two components. */
  const char *repos_url;
  svn_stringbuf_t *fs_path;  /* URI-decoded, always with a leading slash. */

  /* A repository object. */
  svn_repos_t *repos;

  /* The filesystem object associated with REPOS above (for
     convenience). */
  svn_fs_t *fs;

  /* The UUID associated with REPOS above (cached) */
  const char *uuid;

  /* Callbacks/baton passed to svn_ra_open. */
  const svn_ra_callbacks2_t *callbacks;
  void *callback_baton;

  /* Slave auth baton */
  svn_auth_baton_t *auth_baton;

  const char *useragent;
} svn_ra_local__session_baton_t;




/** Private routines **/




/* Given a `file://' URL, figure out which portion specifies a
   repository on local disk, and return that in REPOS_URL (if not
   NULL); URI-decode and return the remainder (the path *within* the
   repository's filesystem) in FS_PATH.  Open REPOS to the repository
   root (if not NULL).  Allocate the return values in POOL.
   Currently, we are not expecting to handle `file://hostname/'-type
   URLs; hostname, in this case, is expected to be the empty string or
   "localhost". */
svn_error_t *
svn_ra_local__split_URL(svn_repos_t **repos,
                        const char **repos_url,
                        const char **fs_path,
                        const char *URL,
                        apr_pool_t *pool);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_LOCAL_H */
