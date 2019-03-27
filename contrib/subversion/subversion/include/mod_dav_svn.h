/**
 * @copyright
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
 * @endcopyright
 *
 * @file mod_dav_svn.h
 * @brief Subversion's backend for Apache's mod_dav module
 */


#ifndef MOD_DAV_SVN_H
#define MOD_DAV_SVN_H

#include <httpd.h>
#include <mod_dav.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
   Given an apache request @a r, a @a uri, and a @a root_path to the svn
   location block, process @a uri and return many things, allocated in
   @a pool:

   - @a cleaned_uri:    The uri with duplicate and trailing slashes removed.

   - @a trailing_slash: Whether the uri had a trailing slash on it.

   Three special substrings of the uri are returned for convenience:

   - @a repos_basename: The single path component that is the directory
                      which contains the repository.  (Don't confuse
                      this with the "repository name" as optionally
                      defined via the SVNReposName directive!)

   - @a relative_path:  The remaining imaginary path components.

   - @a repos_path:     The actual path within the repository filesystem, or
                      NULL if no part of the uri refers to a path in
                      the repository (e.g. "!svn/vcc/default" or
                      "!svn/bln/25").


   For example, consider the uri

       /svn/repos/proj1/!svn/blah/13//A/B/alpha

   In the SVNPath case, this function would receive a @a root_path of
   '/svn/repos/proj1', and in the SVNParentPath case would receive a
   @a root_path of '/svn/repos'.  But either way, we would get back:

     - @a cleaned_uri:    /svn/repos/proj1/!svn/blah/13/A/B/alpha
     - @a repos_basename: proj1
     - @a relative_path:  /!svn/blah/13/A/B/alpha
     - @a repos_path:     A/B/alpha
     - @a trailing_slash: FALSE

   NOTE: The returned dav_error will be also allocated in @a pool, not
         in @a r->pool.

   @since New in 1.9
*/
AP_MODULE_DECLARE(dav_error *) dav_svn_split_uri2(request_rec *r,
                                                  const char *uri_to_split,
                                                  const char *root_path,
                                                  const char **cleaned_uri,
                                                  int *trailing_slash,
                                                  const char **repos_basename,
                                                  const char **relative_path,
                                                  const char **repos_path,
                                                  apr_pool_t *pool);

/**
 * Same as dav_svn_split_uri2() but allocates the result in @a r->pool.
 */
AP_MODULE_DECLARE(dav_error *) dav_svn_split_uri(request_rec *r,
                                                 const char *uri,
                                                 const char *root_path,
                                                 const char **cleaned_uri,
                                                 int *trailing_slash,
                                                 const char **repos_basename,
                                                 const char **relative_path,
                                                 const char **repos_path);


/**
 * Given an apache request @a r and a @a root_path to the svn location
 * block, set @a *repos_path to the path of the repository on disk.
 * Perform all allocations in @a pool.
 *
 * NOTE: The returned dav_error will be also allocated in @a pool, not
 *       in @a r->pool.
 *
 * @since New in 1.9
 */
AP_MODULE_DECLARE(dav_error *) dav_svn_get_repos_path2(request_rec *r,
                                                       const char *root_path,
                                                       const char **repos_path,
                                                       apr_pool_t *pool);

/**
 * Same as dav_svn_get_repos_path2() but allocates the result in@a r->pool.
 */
AP_MODULE_DECLARE(dav_error *) dav_svn_get_repos_path(request_rec *r,
                                                      const char *root_path,
                                                      const char **repos_path);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MOD_DAV_SVN_H */
