/*
 * Copyright (c) 1995 - 2003 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

/*
 * This section is for machines using single entry point AFS syscalls!
 * and/or
 * This section is for machines using multiple entry point AFS syscalls!
 *
 * SunOS 4 is an example of single entry point and sgi of multiple
 * entry point syscalls.
 */

#if SunOS == 40
#define AFS_SYSCALL	31
#endif

#if SunOS >= 50 && SunOS < 57
#define AFS_SYSCALL	105
#endif

#if SunOS == 57
#define AFS_SYSCALL	73
#endif

#if SunOS >= 58
#define AFS_SYSCALL	65
#endif

#if defined(__hpux)
#define AFS_SYSCALL	50
#define AFS_SYSCALL2	49
#define AFS_SYSCALL3	48
#endif

#if defined(_AIX)
/* _AIX is too weird */
#endif

#if defined(__sgi)
#define AFS_PIOCTL      (64+1000)
#define AFS_SETPAG      (65+1000)
#endif

#if defined(__osf__)
#define AFS_SYSCALL	232
#define AFS_SYSCALL2	258
#endif

#if defined(__ultrix)
#define AFS_SYSCALL	31
#endif

#if defined(__FreeBSD__)
#if __FreeBSD_version >= 500000
#define AFS_SYSCALL 339
#else
#define AFS_SYSCALL 210
#endif
#endif /* __FreeBSD__ */

#ifdef __DragonFly__
#ifndef AFS_SYSCALL
#define AFS_SYSCALL 339
#endif
#endif

#ifdef __OpenBSD__
#define AFS_SYSCALL 208
#endif

#if defined(__NetBSD__)
#define AFS_SYSCALL 210
#endif

#ifdef __APPLE__		/* MacOS X */
#define AFS_SYSCALL 230
#endif

#ifdef SYS_afs_syscall
#define AFS_SYSCALL3	SYS_afs_syscall
#endif
