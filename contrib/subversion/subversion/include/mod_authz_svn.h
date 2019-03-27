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
 * @file mod_authz_svn.h
 * @brief Subversion authorization extensions for mod_dav_svn
 */

#ifndef MOD_AUTHZ_SVN_H
#define MOD_AUTHZ_SVN_H

#include <httpd.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * mod_dav_svn to mod_authz_svn bypass mechanism
 */
/** Provider group for subrequest bypass */
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_GRP "dav2authz_subreq_bypass"
/** Provider name for subrequest bypass */
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_NAME "mod_authz_svn_subreq_bypass"
/** Provider version for subrequest bypass */
#define AUTHZ_SVN__SUBREQ_BYPASS_PROV_VER "00.00a"
/** Provider to allow mod_dav_svn to bypass the generation of an apache
 * request when checking GET access from "mod_dav_svn/auth.c".
 *
 * Uses @a r @a repos_path and @a repos_name to determine if the user
 * making the request is authorized.
 *
 * If the access is allowed returns @c OK or @c HTTP_FORBIDDEN if it is not.
 */
typedef int (*authz_svn__subreq_bypass_func_t)(request_rec *r,
                                              const char *repos_path,
                                              const char *repos_name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
