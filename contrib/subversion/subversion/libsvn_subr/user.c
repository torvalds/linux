/*
 * user.c: APR wrapper functions for Subversion
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


#include <apr_pools.h>
#include <apr_user.h>
#include <apr_env.h>

#include "svn_user.h"
#include "svn_utf.h"
#include "svn_dirent_uri.h"

/* Get the current user's name from the OS */
static const char *
get_os_username(apr_pool_t *pool)
{
#if APR_HAS_USER
  char *username;
  apr_uid_t uid;
  apr_gid_t gid;

  if (apr_uid_current(&uid, &gid, pool) == APR_SUCCESS &&
      apr_uid_name_get(&username, uid, pool) == APR_SUCCESS)
    return username;
#endif

  return NULL;
}

/* Return a UTF8 version of STR, or NULL on error.
   Use POOL for any necessary allocation. */
static const char *
utf8_or_nothing(const char *str, apr_pool_t *pool) {
  if (str)
    {
      const char *utf8_str;
      svn_error_t *err = svn_utf_cstring_to_utf8(&utf8_str, str, pool);
      if (! err)
        return utf8_str;
      svn_error_clear(err);
    }
  return NULL;
}

const char *
svn_user_get_name(apr_pool_t *pool)
{
  const char *username = get_os_username(pool);
  return utf8_or_nothing(username, pool);
}

/* Most of the guts of svn_user_get_homedir(): everything except
 * canonicalizing the path.
 */
static const char *
user_get_homedir(apr_pool_t *pool)
{
  const char *username;
  char *homedir;

  if (apr_env_get(&homedir, "HOME", pool) == APR_SUCCESS)
    return utf8_or_nothing(homedir, pool);

  username = get_os_username(pool);
  if (username != NULL &&
      apr_uid_homepath_get(&homedir, username, pool) == APR_SUCCESS)
    return utf8_or_nothing(homedir, pool);

  return NULL;
}

const char *
svn_user_get_homedir(apr_pool_t *pool)
{
  const char *homedir = user_get_homedir(pool);

  if (homedir)
    return svn_dirent_canonicalize(homedir, pool);

  return NULL;
}
