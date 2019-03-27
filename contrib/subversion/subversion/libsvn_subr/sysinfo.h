/*
 * sysinfo.h:  share svn_sysinfo__* functions
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

#ifndef SVN_LIBSVN_SUBR_SYSINFO_H
#define SVN_LIBSVN_SUBR_SYSINFO_H

#include <apr_pools.h>
#include <apr_tables.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Return a canonical name similar to the output of config.guess,
 * identifying the running system.
 *
 * All allocations are done in POOL.
 */
const char *svn_sysinfo__canonical_host(apr_pool_t *pool);

/* Return the release name (i.e., marketing name) of the running
 * system, or NULL if it's not available.
 *
 * All allocations are done in POOL.
 */
const char *svn_sysinfo__release_name(apr_pool_t *pool);

/* Return an array of svn_version_linked_lib_t of descriptions of the
 * link-time and run-time versions of dependent libraries, or NULL of
 * the info is not available.
 *
 * All allocations are done in POOL.
 */
const apr_array_header_t *svn_sysinfo__linked_libs(apr_pool_t *pool);

/* Return an array of svn_version_loaded_lib_t of descriptions of
 * shared libraries loaded by the running process, including their
 * versions where applicable, or NULL if the information is not
 * available.
 *
 * All allocations are done in POOL.
 */
const apr_array_header_t *svn_sysinfo__loaded_libs(apr_pool_t *pool);

#ifdef WIN32
/* Obtain the Windows version information as OSVERSIONINFOEXW structure.
 *
 * !!! Unlike other apis the caller is expected to pre-allocate the buffer
 * !!! to allow using this api from the crash handler.
 */
svn_boolean_t
svn_sysinfo___fill_windows_version(OSVERSIONINFOEXW *version_info);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_SYSINFO_H */
