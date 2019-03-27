/*
 * Copyright (c) 1995 - 2000 Kungliga Tekniska Högskolan
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

#ifdef HAVE_CONFIG_H
#include <config.h>
RCSID("$Id$");
#endif

#include <stdio.h>
#include <string.h>
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#elif defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#ifdef HAVE_ASL_H
#include <asl.h>
#endif
#include <roken.h>
#include "extern.h"

#ifndef HAVE_UTMPX_H
#ifndef WTMP_FILE
#ifdef _PATH_WTMP
#define WTMP_FILE _PATH_WTMP
#else
#define WTMP_FILE "/var/adm/wtmp"
#endif
#endif
#endif

#ifdef HAVE_ASL_H

#ifndef ASL_KEY_FACILITY
#define ASL_KEY_FACILITY "Facility"
#endif

static void
ftpd_logwtmp_asl(char *line, char *name, char *host)
{
    static aslmsg m = NULL;
    static int init = 0;

    if (!init) {
	init = 1;
	m = asl_new(ASL_TYPE_MSG);
	if (m == NULL)
	    return;
	asl_set(m, ASL_KEY_FACILITY, "org.h5l.ftpd");
    }
    if (m)
	asl_log(NULL, m, ASL_LEVEL_NOTICE,
		"host %s/%s user %s%sconnected pid %d",
		host, line, name, name[0] ? " " : "dis", (int)getpid());
}

#endif

#ifndef HAVE_ASL_H

static void
ftpd_logwtmp_wtmp(char *line, char *name, char *host)
{
    static int init = 0;
    static int fd;
#ifdef WTMPX_FILE
    static int fdx;
#endif
#ifdef HAVE_UTMP_H
    struct utmp ut;
#endif
#if defined(WTMPX_FILE) || defined(HAVE_UTMPX_H)
    struct utmpx utx;
#endif

#ifdef HAVE_UTMPX_H
    memset(&utx, 0, sizeof(struct utmpx));
#endif
#ifdef HAVE_UTMP_H
    memset(&ut, 0, sizeof(struct utmp));
#ifdef HAVE_STRUCT_UTMP_UT_TYPE
    if(name[0])
	ut.ut_type = USER_PROCESS;
    else
	ut.ut_type = DEAD_PROCESS;
#endif
    strncpy(ut.ut_line, line, sizeof(ut.ut_line));
    strncpy(ut.ut_name, name, sizeof(ut.ut_name));
#ifdef HAVE_STRUCT_UTMP_UT_PID
    ut.ut_pid = getpid();
#endif
#ifdef HAVE_STRUCT_UTMP_UT_HOST
    strncpy(ut.ut_host, host, sizeof(ut.ut_host));
#endif
    ut.ut_time = time(NULL);
#endif

#if defined(WTMPX_FILE) || defined(HAVE_UTMPX_H)
    strncpy(utx.ut_line, line, sizeof(utx.ut_line));
    strncpy(utx.ut_user, name, sizeof(utx.ut_user));
    strncpy(utx.ut_host, host, sizeof(utx.ut_host));
#ifdef HAVE_STRUCT_UTMPX_UT_SYSLEN
    utx.ut_syslen = strlen(host) + 1;
    if (utx.ut_syslen > sizeof(utx.ut_host))
        utx.ut_syslen = sizeof(utx.ut_host);
#endif
    {
	struct timeval tv;

	gettimeofday (&tv, 0);
	utx.ut_tv.tv_sec = tv.tv_sec;
	utx.ut_tv.tv_usec = tv.tv_usec;
    }

    if(name[0])
	utx.ut_type = USER_PROCESS;
    else
	utx.ut_type = DEAD_PROCESS;
#endif

#ifdef HAVE_UTMPX_H
    pututxline(&utx);
#endif

    if(!init){
#ifdef WTMP_FILE
	fd = open(WTMP_FILE, O_WRONLY|O_APPEND, 0);
#endif
#ifdef WTMPX_FILE
	fdx = open(WTMPX_FILE, O_WRONLY|O_APPEND, 0);
#endif
	init = 1;
    }
    if(fd >= 0) {
#ifdef WTMP_FILE
	write(fd, &ut, sizeof(struct utmp)); /* XXX */
#endif
#ifdef WTMPX_FILE
	write(fdx, &utx, sizeof(struct utmpx));
#endif
    }
}

#endif /* !HAVE_ASL_H */

void
ftpd_logwtmp(char *line, char *name, char *host)
{
#ifdef HAVE_ASL_H
    ftpd_logwtmp_asl(line, name, host);
#else
    ftpd_logwtmp_wtmp(line, name, host);
#endif
}
