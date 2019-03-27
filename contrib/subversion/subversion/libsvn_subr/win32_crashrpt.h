/*
 * win32_crashrpt.h : shares the win32 crashhandler functions in libsvn_subr.
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

#ifndef SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H
#define SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H

#ifdef WIN32
#ifdef SVN_USE_WIN32_CRASHHANDLER

LONG WINAPI svn__unhandled_exception_filter(PEXCEPTION_POINTERS ptrs);

#endif /* SVN_USE_WIN32_CRASHHANDLER */
#endif /* WIN32 */

#endif /* SVN_LIBSVN_SUBR_WIN32_CRASHRPT_H */
