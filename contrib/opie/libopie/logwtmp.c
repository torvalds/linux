/* logwtmp.c: Put an entry in the wtmp file.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

	History:

	Modified by cmetz for OPIE 2.4. Set process to dead if name is null.
		Added support for ut_id and ut_syslen.
	Modified by cmetz for OPIE 2.32. Don't leave line=NULL, skip
		past /dev/ in line. Fill in ut_host on systems with UTMPX and
		ut_host.
	Modified by cmetz for OPIE 2.31. Move wtmp log functions here, to
		improve portability. Added DISABLE_WTMP.
	Modified by cmetz for OPIE 2.22. Call gettimeofday() properly.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
        	Ifdef around some headers. Added file close hook.
	Modified at NRL for OPIE 2.1. Set process type for HPUX.
	Modified at NRL for OPIE 2.0.
	Originally from BSD.
*/
/*
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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
 *
 */

#include "opie_cfg.h"

#include <sys/types.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#include <sys/stat.h>
#include <fcntl.h>
#include <utmp.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include "opie.h"

static int fd = -1;

#if DOUTMPX
static int fdx = -1;
#include <utmpx.h>
#endif	/* DOUTMPX */

#ifndef _PATH_WTMP
#ifdef WTMP_FILE
#define _PATH_WTMP WTMP_FILE
#else /* WTMP_FILE */
#ifdef PATH_WTMP_AC
#define _PATH_WTMP PATH_WTMP_AC
#endif /* PATH_WTMP_AC */
#endif /* WTMP_FILE */
#endif /* _PATH_WTMP */

#ifndef _PATH_WTMPX
#ifdef WTMPX_FILE
#define _PATH_WTMPX WTMPX_FILE
#else /* WTMPX_FILE */
#ifdef PATH_WTMPX_AC
#define _PATH_WTMPX PATH_WTMPX_AC
#endif /* PATH_WTMPX_AC */
#endif /* WTMPX_FILE */
#endif /* _PATH_WTMPX */

/*
 * Modified version of logwtmp that holds wtmp file open
 * after first call, for use with ftp (which may chroot
 * after login, but before logout).
 */
VOIDRET opielogwtmp FUNCTION((line, name, host), char *line AND char *name AND char *host AND char *id)
{
#if !DISABLE_WTMP
  struct utmp ut;

#if DOUTMPX && defined(_PATH_WTMPX)
  struct utmpx utx;
#endif /* DOUTMPX && defined(_PATH_WTMPX) */
  struct stat buf;

  memset(&ut, 0, sizeof(struct utmp));

  if (!line) {
    close(fd);
#if DOUTMPX && defined(_PATH_WTMPX)
    close(fdx);
#endif /* DOUTMPX && defined(_PATH_WTMPX) */
    line = "";
  } else
    if (!strncmp(line, "/dev/", 5))
      line += 5;

  if (fd < 0 && (fd = open(_PATH_WTMP, O_WRONLY | O_APPEND, 0)) < 0)
    return;
  if (fstat(fd, &buf) == 0) {
#if HAVE_UT_TYPE && defined(USER_PROCESS)
    if (name && *name)
      ut.ut_type = USER_PROCESS;
    else
      ut.ut_type = DEAD_PROCESS;
#endif /* HAVE_UT_TYPE && defined(USER_PROCESS) */
#if HAVE_UT_ID
    if (id)
      strncpy(ut.ut_id, id, sizeof(ut.ut_id));
#endif /* HAVE_UT_ID */
#if HAVE_UT_PID
    ut.ut_pid = getpid();
#endif /* HAVE_UT_PID */
    strncpy(ut.ut_line, line, sizeof(ut.ut_line));
    strncpy(ut.ut_name, name, sizeof(ut.ut_name));
#if HAVE_UT_HOST
    strncpy(ut.ut_host, host, sizeof(ut.ut_host));
#endif /* HAVE_UT_HOST */
    time(&ut.ut_time);
    if (write(fd, (char *) &ut, sizeof(struct utmp)) !=
	sizeof(struct utmp))
    ftruncate(fd, buf.st_size);
  }

#if DOUTMPX && defined(_PATH_WTMPX)
  memset(&utx, 0, sizeof(struct utmpx));

  if (fdx < 0 && (fdx = open(_PATH_WTMPX, O_WRONLY | O_APPEND, 0)) < 0)
    return;
  if (fstat(fdx, &buf) == 0) {
    strncpy(utx.ut_line, line, sizeof(utx.ut_line));
    strncpy(utx.ut_name, name, sizeof(utx.ut_name));
    strncpy(utx.ut_host, host, sizeof(utx.ut_host));
#ifdef USER_PROCESS
    if (name && *name)
      utx.ut_type = USER_PROCESS;
    else
      utx.ut_type = DEAD_PROCESS;
#endif /* USER_PROCESS */
    if (id)
      strncpy(utx.ut_id, id, sizeof(utx.ut_id));
    utx.ut_pid = getpid();
#if HAVE_UTX_SYSLEN
    utx.ut_syslen = strlen(utx.ut_host) + 1;
#endif /* HAVE_UTX_SYSLEN */
#if HAVE_GETTIMEOFDAY
#if HAVE_ONE_ARG_GETTIMEOFDAY
    gettimeofday(&utx.ut_tv);
#else /* HAVE_ONE_ARG_GETTIMEOFDAY */
    gettimeofday(&utx.ut_tv, NULL);
#endif /* HAVE_ONE_ARG_GETTIMEOFDAY */
#endif /* HAVE_GETTIMEOFDAY */
    if (write(fdx, (char *) &utx, sizeof(struct utmpx)) != sizeof(struct utmpx))
    ftruncate(fdx, buf.st_size);
  }
#endif /* DOUTMPX && defined(_PATH_WTMPX) */
#endif /* !DISABLE_WTMP */
}
