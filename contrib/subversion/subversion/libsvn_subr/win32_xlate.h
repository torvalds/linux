/*
 * win32_xlate.h : Windows xlate stuff.
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

#ifndef SVN_LIBSVN_SUBR_WIN32_XLATE_H
#define SVN_LIBSVN_SUBR_WIN32_XLATE_H

#ifdef WIN32

/* Opaque translation buffer. */
typedef struct svn_subr__win32_xlate_t svn_subr__win32_xlate_t;

/* Set *XLATE_P to a handle node for converting from FROMPAGE to TOPAGE.
   Returns APR_EINVAL or APR_ENOTIMPL, if a conversion isn't supported.
   If fail for any other reason, return the error.

   Allocate *RET in POOL. */
apr_status_t
svn_subr__win32_xlate_open(svn_subr__win32_xlate_t **xlate_p,
                           const char *topage,
                           const char *frompage,
                           apr_pool_t *pool);

/* Convert SRC_LENGTH bytes of SRC_DATA in NODE->handle, store the result
   in *DEST, which is allocated in POOL. */
apr_status_t
svn_subr__win32_xlate_to_stringbuf(svn_subr__win32_xlate_t *handle,
                                   const char *src_data,
                                   apr_size_t src_length,
                                   svn_stringbuf_t **dest,
                                   apr_pool_t *pool);

#endif /* WIN32 */

#endif /* SVN_LIBSVN_SUBR_WIN32_XLATE_H */
