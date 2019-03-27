/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"        /* configuration data */
/**
 * @file apr_want.h
 * @brief APR Standard Headers Support
 *
 * <PRE>
 * Features:
 *
 *   APR_WANT_STRFUNC:  strcmp, strcat, strcpy, etc
 *   APR_WANT_MEMFUNC:  memcmp, memcpy, etc
 *   APR_WANT_STDIO:    <stdio.h> and related bits
 *   APR_WANT_IOVEC:    struct iovec
 *   APR_WANT_BYTEFUNC: htons, htonl, ntohl, ntohs
 *
 * Typical usage:
 *
 *   \#define APR_WANT_STRFUNC
 *   \#define APR_WANT_MEMFUNC
 *   \#include "apr_want.h"
 *
 * The appropriate headers will be included.
 *
 * Note: it is safe to use this in a header (it won't interfere with other
 *       headers' or source files' use of apr_want.h)
 * </PRE>
 */

/* --------------------------------------------------------------------- */

#ifdef APR_WANT_STRFUNC

#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_STRINGS_H
#include <strings.h>
#endif

#undef APR_WANT_STRFUNC
#endif

/* --------------------------------------------------------------------- */

#ifdef APR_WANT_MEMFUNC

#if APR_HAVE_STRING_H
#include <string.h>
#endif

#undef APR_WANT_MEMFUNC
#endif

/* --------------------------------------------------------------------- */

#ifdef APR_WANT_STDIO

#if APR_HAVE_STDIO_H
#include <stdio.h>
#endif

#undef APR_WANT_STDIO
#endif

/* --------------------------------------------------------------------- */

#ifdef APR_WANT_IOVEC

#if APR_HAVE_IOVEC

#if APR_HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#else

#ifndef APR_IOVEC_DEFINED
#define APR_IOVEC_DEFINED
struct iovec
{
    void *iov_base;
    size_t iov_len;
};
#endif /* !APR_IOVEC_DEFINED */

#endif /* APR_HAVE_IOVEC */

#undef APR_WANT_IOVEC
#endif

/* --------------------------------------------------------------------- */

#ifdef APR_WANT_BYTEFUNC

/* Single Unix says they are in arpa/inet.h.  Linux has them in
 * netinet/in.h.  FreeBSD has them in arpa/inet.h but requires that
 * netinet/in.h be included first.
 */
#if APR_HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if APR_HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#undef APR_WANT_BYTEFUNC
#endif

/* --------------------------------------------------------------------- */
