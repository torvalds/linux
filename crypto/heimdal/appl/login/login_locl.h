/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

#ifndef __LOGIN_LOCL_H__
#define __LOGIN_LOCL_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <termios.h>
#include <err.h>
#include <pwd.h>
#include <roken.h>
#include <getarg.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif
#ifdef HAVE_UDB_H
#include <udb.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_CATEGORY_H
#include <sys/category.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif
#ifdef HAVE_NETGROUP_H
#include <netgroup.h>
#endif
#ifdef HAVE_RPCSVC_YPCLNT_H
#include <rpcsvc/ypclnt.h>
#endif
#ifdef KRB5
#include <krb5.h>
#endif
#include <kafs.h>

#ifdef OTP
#include <otp.h>
#endif

#ifdef HAVE_OSFC2
#define getargs OSFgetargs
#include "/usr/include/prot.h"
#undef getargs
#endif

#ifndef _PATH_BSHELL
#define _PATH_BSHELL "/bin/sh"
#endif
#ifndef _PATH_TTY
#define _PATH_TTY "/dev/tty"
#endif
#ifndef _PATH_DEV
#define _PATH_DEV "/dev/"
#endif
#ifndef _PATH_WTMP
#ifdef WTMP_FILE
#define _PATH_WTMP WTMP_FILE
#else
#define _PATH_WTMP "/var/adm/wtmp"
#endif
#endif
#ifndef _PATH_UTMP
#ifdef UTMP_FILE
#define _PATH_UTMP UTMP_FILE
#else
#define _PATH_UTMP "/var/adm/utmp"
#endif
#endif

/* if cygwin doesnt have WTMPX_FILE, it uses wtmp for wtmpx
 * http://www.cygwin.com/ml/cygwin/2006-12/msg00630.html */
#ifdef __CYGWIN__
#ifndef WTMPX_FILE
#define WTMPX_FILE WTMP_FILE
#endif
#endif

#ifndef _PATH_LOGACCESS
#define _PATH_LOGACCESS SYSCONFDIR "/login.access"
#endif /* _PATH_LOGACCESS */

#ifndef _PATH_LOGIN_CONF
#define _PATH_LOGIN_CONF SYSCONFDIR "/login.conf"
#endif /* _PATH_LOGIN_CONF */

#ifndef _PATH_DEFPATH
#define _PATH_DEFPATH "/usr/bin:/bin"
#endif

#include "loginpaths.h"

struct spwd;

extern char **env;
extern int num_env;

#include "login-protos.h"

#endif /* __LOGIN_LOCL_H__ */
