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
 * @file svn_error_private.h
 * @brief Subversion-internal error APIs.
 */

#ifndef SVN_ERROR_PRIVATE_H
#define SVN_ERROR_PRIVATE_H

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Returns if @a err is a "tracing" error.
 */
svn_boolean_t
svn_error__is_tracing_link(const svn_error_t *err);

/**
 * Converts a zlib error to an svn_error_t. zerr is the error code,
 * function is the function name, message is an optional extra part
 * of the error message and may be NULL.
 */
svn_error_t *
svn_error__wrap_zlib(int zerr, const char *function, const char *message);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ERROR_PRIVATE_H */
