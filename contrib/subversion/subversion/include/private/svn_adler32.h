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
 * @file svn_adler32.h
 * @brief Subversion's take on Adler-32 calculation
 */

#ifndef SVN_ADLER32_H
#define SVN_ADLER32_H

#include <apr.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Return an adler32 checksum based on CHECKSUM, updated with
 * DATA of size LEN.
 *
 * @since New in 1.7.
 */
apr_uint32_t
svn__adler32(apr_uint32_t checksum, const char *data, apr_off_t len);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_ADLER32_H */
