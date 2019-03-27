/*
 * Copyright (c) 2008-2014, Simon Schubert <2@0x2c.org>.
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Simon Schubert <2@0x2c.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/file.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "dma.h"

const char *
hostname(void)
{
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX	255
#endif
	static char name[HOST_NAME_MAX+1];
	static int initialized = 0;
	char *s;

	if (initialized)
		return (name);

	if (config.mailname == NULL || !*config.mailname)
		goto local;

	if (config.mailname[0] == '/') {
		/*
		 * If the mailname looks like an absolute path,
		 * treat it as a file.
		 */
		FILE *fp;

		fp = fopen(config.mailname, "r");
		if (fp == NULL)
			goto local;

		s = fgets(name, sizeof(name), fp);
		fclose(fp);
		if (s == NULL)
			goto local;

		for (s = name; *s != 0 && (isalnum(*s) || strchr("_.-", *s)); ++s)
			/* NOTHING */;
		*s = 0;

		if (!*name)
			goto local;

		initialized = 1;
		return (name);
	} else {
		snprintf(name, sizeof(name), "%s", config.mailname);
		initialized = 1;
		return (name);
	}

local:
	if (gethostname(name, sizeof(name)) != 0)
		*name = 0;
	/*
	 * gethostname() is allowed to truncate name without NUL-termination
	 * and at the same time not return an error.
	 */
	name[sizeof(name) - 1] = 0;

	for (s = name; *s != 0 && (isalnum(*s) || strchr("_.-", *s)); ++s)
		/* NOTHING */;
	*s = 0;

	if (!*name)
		snprintf(name, sizeof(name), "unknown-hostname");

	initialized = 1;
	return (name);
}

void
setlogident(const char *fmt, ...)
{
	static char tag[50];

	snprintf(tag, sizeof(tag), "%s", logident_base);
	if (fmt != NULL) {
		va_list ap;
		char sufx[50];

		va_start(ap, fmt);
		vsnprintf(sufx, sizeof(sufx), fmt, ap);
		va_end(ap);
		snprintf(tag, sizeof(tag), "%s[%s]", logident_base, sufx);
	}
	closelog();
	openlog(tag, 0, LOG_MAIL);
}

void
errlog(int exitcode, const char *fmt, ...)
{
	int oerrno = errno;
	va_list ap;
	char outs[ERRMSG_SIZE];

	outs[0] = 0;
	if (fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(outs, sizeof(outs), fmt, ap);
		va_end(ap);
	}

	errno = oerrno;
	if (*outs != 0) {
		syslog(LOG_ERR, "%s: %m", outs);
		fprintf(stderr, "%s: %s: %s\n", getprogname(), outs, strerror(oerrno));
	} else {
		syslog(LOG_ERR, "%m");
		fprintf(stderr, "%s: %s\n", getprogname(), strerror(oerrno));
	}

	exit(exitcode);
}

void
errlogx(int exitcode, const char *fmt, ...)
{
	va_list ap;
	char outs[ERRMSG_SIZE];

	outs[0] = 0;
	if (fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(outs, sizeof(outs), fmt, ap);
		va_end(ap);
	}

	if (*outs != 0) {
		syslog(LOG_ERR, "%s", outs);
		fprintf(stderr, "%s: %s\n", getprogname(), outs);
	} else {
		syslog(LOG_ERR, "Unknown error");
		fprintf(stderr, "%s: Unknown error\n", getprogname());
	}

	exit(exitcode);
}

static int
check_username(const char *name, uid_t ckuid)
{
	struct passwd *pwd;

	if (name == NULL)
		return (0);
	pwd = getpwnam(name);
	if (pwd == NULL || pwd->pw_uid != ckuid)
		return (0);
	snprintf(username, sizeof(username), "%s", name);
	return (1);
}

void
set_username(void)
{
	struct passwd *pwd;

	useruid = getuid();
	if (check_username(getlogin(), useruid))
		return;
	if (check_username(getenv("LOGNAME"), useruid))
		return;
	if (check_username(getenv("USER"), useruid))
		return;
	pwd = getpwuid(useruid);
	if (pwd != NULL && pwd->pw_name != NULL && pwd->pw_name[0] != '\0') {
		if (check_username(pwd->pw_name, useruid))
			return;
	}
	snprintf(username, sizeof(username), "uid=%ld", (long)useruid);
}

void
deltmp(void)
{
	struct stritem *t;

	SLIST_FOREACH(t, &tmpfs, next) {
		unlink(t->str);
	}
}

static sigjmp_buf sigbuf;
static int sigbuf_valid;

static void
sigalrm_handler(int signo)
{
	(void)signo;	/* so that gcc doesn't complain */
	if (sigbuf_valid)
		siglongjmp(sigbuf, 1);
}

int
do_timeout(int timeout, int dojmp)
{
	struct sigaction act;
	int ret = 0;

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	if (timeout) {
		act.sa_handler = sigalrm_handler;
		if (sigaction(SIGALRM, &act, NULL) != 0)
			syslog(LOG_WARNING, "can not set signal handler: %m");
		if (dojmp) {
			ret = sigsetjmp(sigbuf, 1);
			if (ret)
				goto disable;
			/* else just programmed */
			sigbuf_valid = 1;
		}

		alarm(timeout);
	} else {
disable:
		alarm(0);

		act.sa_handler = SIG_IGN;
		if (sigaction(SIGALRM, &act, NULL) != 0)
			syslog(LOG_WARNING, "can not remove signal handler: %m");
		sigbuf_valid = 0;
	}

	return (ret);
}

int
open_locked(const char *fname, int flags, ...)
{
	int mode = 0;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

#ifndef O_EXLOCK
	int fd, save_errno;

	fd = open(fname, flags, mode);
	if (fd < 0)
		return(fd);
	if (flock(fd, LOCK_EX|((flags & O_NONBLOCK)? LOCK_NB: 0)) < 0) {
		save_errno = errno;
		close(fd);
		errno = save_errno;
		return(-1);
	}
	return(fd);
#else
	return(open(fname, flags|O_EXLOCK, mode));
#endif
}

char *
rfc822date(void)
{
	static char str[50];
	size_t error;
	time_t now;

	now = time(NULL);
	error = strftime(str, sizeof(str), "%a, %d %b %Y %T %z",
		       localtime(&now));
	if (error == 0)
		strcpy(str, "(date fail)");
	return (str);
}

int
strprefixcmp(const char *str, const char *prefix)
{
	return (strncasecmp(str, prefix, strlen(prefix)));
}

void
init_random(void)
{
	unsigned int seed;
	int rf;

	rf = open("/dev/urandom", O_RDONLY);
	if (rf == -1)
		rf = open("/dev/random", O_RDONLY);

	if (!(rf != -1 && read(rf, &seed, sizeof(seed)) == sizeof(seed)))
		seed = (time(NULL) ^ getpid()) + (uintptr_t)&seed;

	srandom(seed);

	if (rf != -1)
		close(rf);
}
