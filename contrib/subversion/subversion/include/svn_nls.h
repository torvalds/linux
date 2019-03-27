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
 * @file svn_nls.h
 * @brief Support functions for NLS programs
 */



#ifndef SVN_NLS_H
#define SVN_NLS_H

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Set up the NLS.
 * Return the error @c APR_EINVAL or @c APR_INCOMPLETE if an
 * error occurs.
 *
 * @note This function is for bindings. You should usually
 *       use svn_cmdline_init() instead of calling this
 *       function directly. This function should be called
 *       after initializing APR.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_nls_init(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_NLS_H */
