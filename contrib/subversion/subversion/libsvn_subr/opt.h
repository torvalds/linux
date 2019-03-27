/*
 * opt.h: share svn_opt__* functions
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

#ifndef SVN_LIBSVN_SUBR_OPT_H
#define SVN_LIBSVN_SUBR_OPT_H

#include "svn_version.h"
#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Print version version info for PGM_NAME to the console.  If QUIET is
 * true, print in brief.  Else if QUIET is not true, print the version
 * more verbosely, and if FOOTER is non-null, print it following the
 * version information. If VERBOSE is true, print running system info.
 *
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_opt__print_version_info(const char *pgm_name,
                            const char *footer,
                            const svn_version_extended_t *info,
                            svn_boolean_t quiet,
                            svn_boolean_t verbose,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_OPT_H */
