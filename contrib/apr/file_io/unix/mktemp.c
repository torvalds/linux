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
/*
 * Copyright (c) 1987, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "apr_private.h"
#include "apr_file_io.h" /* prototype of apr_mkstemp() */
#include "apr_strings.h" /* prototype of apr_mkstemp() */
#include "apr_arch_file_io.h" /* prototype of apr_mkstemp() */
#include "apr_portable.h" /* for apr_os_file_put() */
#include "apr_arch_inherit.h"

#ifndef HAVE_MKSTEMP

#if defined(SVR4) || defined(WIN32) || defined(NETWARE)
#ifdef SVR4
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif
#define arc4random() rand()
#define seedrandom(a) srand(a)
#else
#if APR_HAVE_STDINT_H
#include <stdint.h>
#endif
#define arc4random() random()
#define seedrandom(a) srandom(a)
#endif

#if APR_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if APR_HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif

static const unsigned char padchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static apr_uint32_t randseed=0;

static int gettemp(char *path, apr_file_t **doopen, apr_int32_t flags, apr_pool_t *p)
{
    register char *start, *trv, *suffp;
    char *pad;
    apr_finfo_t sbuf;
    apr_status_t rv;
    apr_uint32_t randnum;

    if (randseed==0) {
        randseed = (int)apr_time_now();
        seedrandom(randseed);
    }

    for (trv = path; *trv; ++trv)
        ;
    suffp = trv;
    --trv;
    if (trv < path) {
        return APR_EINVAL;
    }

    /* Fill space with random characters */
    while (*trv == 'X') {
        randnum = arc4random() % (sizeof(padchar) - 1);
        *trv-- = padchar[randnum];
    }
    start = trv + 1;

    /*
     * check the target directory.
     */
    for (;; --trv) {
        if (trv <= path)
            break;
        if (*trv == '/') {
            *trv = '\0';
            rv = apr_stat(&sbuf, path, APR_FINFO_TYPE, p);
            *trv = '/';
            if (rv != APR_SUCCESS)
                return rv;
            if (sbuf.filetype != APR_DIR) {
                return APR_ENOTDIR;
            }
            break;
        }
    }

    for (;;) {
        if ((rv = apr_file_open(doopen, path, flags,
                                APR_UREAD | APR_UWRITE, p)) == APR_SUCCESS)
            return APR_SUCCESS;
        if (!APR_STATUS_IS_EEXIST(rv))
            return rv;

        /* If we have a collision, cycle through the space of filenames */
        for (trv = start;;) {
            if (*trv == '\0' || trv == suffp)
                return APR_EINVAL; /* XXX: is this the correct return code? */
            pad = strchr((char *)padchar, *trv);
            if (pad == NULL || !*++pad) {
                *trv++ = padchar[0];
            }
            else {
                *trv++ = *pad;
                break;
            }
        }
    }
    /*NOTREACHED*/
}

#else

#if APR_HAVE_STDLIB_H
#include <stdlib.h> /* for mkstemp() - Single Unix */
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h> /* for mkstemp() - FreeBSD */
#endif
#endif /* !defined(HAVE_MKSTEMP) */

APR_DECLARE(apr_status_t) apr_file_mktemp(apr_file_t **fp, char *template, apr_int32_t flags, apr_pool_t *p)
{
#ifdef HAVE_MKSTEMP
    int fd;
#endif
    flags = (!flags) ? APR_FOPEN_CREATE | APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_EXCL |
                       APR_FOPEN_DELONCLOSE : flags;
#ifndef HAVE_MKSTEMP
    return gettemp(template, fp, flags, p);
#else

#ifdef HAVE_MKSTEMP64
    fd = mkstemp64(template);
#else
    fd = mkstemp(template);
#endif
    
    if (fd == -1) {
        return errno;
    }
    /* XXX: We must reset several flags values as passed-in, since
     * mkstemp didn't subscribe to our preference flags.
     *
     * We either have to unset the flags, or fix up the fd and other
     * xthread and inherit bits appropriately.  Since gettemp() above
     * calls apr_file_open, our flags are respected in that code path.
     */
    apr_os_file_put(fp, &fd, flags, p);
    (*fp)->fname = apr_pstrdup(p, template);

    if (!(flags & APR_FOPEN_NOCLEANUP)) {
        int flags;

        if ((flags = fcntl(fd, F_GETFD)) == -1)
            return errno;

        flags |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, flags) == -1)
            return errno;

        apr_pool_cleanup_register((*fp)->pool, (void *)(*fp),
                                  apr_unix_file_cleanup,
                                  apr_unix_child_file_cleanup);
    }
#endif
    return APR_SUCCESS;
}

