/*	$NetBSD: getid.c,v 1.10 2014/10/27 21:46:45 christos Exp $	*/
/*	from: NetBSD: getpwent.c,v 1.48 2000/10/03 03:22:26 enami Exp */
/*	from: NetBSD: getgrent.c,v 1.41 2002/01/12 23:51:30 lukem Exp */

/*
 * Copyright (c) 1987, 1988, 1989, 1993, 1994, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn of Wasabi Systems.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
__RCSID("$NetBSD: getid.c,v 1.10 2014/10/27 21:46:45 christos Exp $");

#include <sys/param.h>

#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

static	struct group *	gi_getgrnam(const char *);
static	struct group *	gi_getgrgid(gid_t);
static	int		gi_setgroupent(int);
static	void		gi_endgrent(void);
static	int		grstart(void);
static	int		grscan(int, gid_t, const char *);
static	int		grmatchline(int, gid_t, const char *);

static	struct passwd *	gi_getpwnam(const char *);
static	struct passwd *	gi_getpwuid(uid_t);
static	int		gi_setpassent(int);
static	void		gi_endpwent(void);
static	int		pwstart(void);
static	int		pwscan(int, uid_t, const char *);
static	int		pwmatchline(int, uid_t, const char *);

#define	MAXGRP		200
#define	MAXLINELENGTH	1024

static	FILE		*_gr_fp;
static	struct group	_gr_group;
static	int		_gr_stayopen;
static	int		_gr_filesdone;
static	FILE		*_pw_fp;
static	struct passwd	_pw_passwd;	/* password structure */
static	int		_pw_stayopen;	/* keep fd's open */
static	int		_pw_filesdone;

static	char		grfile[MAXPATHLEN];
static	char		pwfile[MAXPATHLEN];

static	char		*members[MAXGRP];
static	char		grline[MAXLINELENGTH];
static	char		pwline[MAXLINELENGTH];

int
setup_getid(const char *dir)
{
	if (dir == NULL)
		return (0);

				/* close existing databases */
	gi_endgrent();
	gi_endpwent();

				/* build paths to new databases */
	snprintf(grfile, sizeof(grfile), "%s/group", dir);
	snprintf(pwfile, sizeof(pwfile), "%s/master.passwd", dir);

				/* try to open new databases */
	if (!grstart() || !pwstart())
		return (0);

				/* switch pwcache(3) lookup functions */
	if (pwcache_groupdb(gi_setgroupent, gi_endgrent,
			    gi_getgrnam, gi_getgrgid) == -1
	    || pwcache_userdb(gi_setpassent, gi_endpwent,
			    gi_getpwnam, gi_getpwuid) == -1)
		return (0);

	return (1);
}


/*
 * group lookup functions
 */

static struct group *
gi_getgrnam(const char *name)
{
	int rval;

	if (!grstart())
		return NULL;
	rval = grscan(1, 0, name);
	if (!_gr_stayopen)
		endgrent();
	return (rval) ? &_gr_group : NULL;
}

static struct group *
gi_getgrgid(gid_t gid)
{
	int rval;

	if (!grstart())
		return NULL;
	rval = grscan(1, gid, NULL);
	if (!_gr_stayopen)
		endgrent();
	return (rval) ? &_gr_group : NULL;
}

static int
gi_setgroupent(int stayopen)
{

	if (!grstart())
		return 0;
	_gr_stayopen = stayopen;
	return 1;
}

static void
gi_endgrent(void)
{

	_gr_filesdone = 0;
	if (_gr_fp) {
		(void)fclose(_gr_fp);
		_gr_fp = NULL;
	}
}

static int
grstart(void)
{

	_gr_filesdone = 0;
	if (_gr_fp) {
		rewind(_gr_fp);
		return 1;
	}
	if (grfile[0] == '\0')			/* sanity check */
		return 0;

	_gr_fp = fopen(grfile, "r");
	if (_gr_fp != NULL)
		return 1;
	warn("Can't open `%s'", grfile);
	return 0;
}


static int
grscan(int search, gid_t gid, const char *name)
{

	if (_gr_filesdone)
		return 0;
	for (;;) {
		if (!fgets(grline, sizeof(grline), _gr_fp)) {
			if (!search)
				_gr_filesdone = 1;
			return 0;
		}
		/* skip lines that are too big */
		if (!strchr(grline, '\n')) {
			int ch;

			while ((ch = getc(_gr_fp)) != '\n' && ch != EOF)
				;
			continue;
		}
		/* skip comments */
		if (grline[0] == '#')
			continue;
		if (grmatchline(search, gid, name))
			return 1;
	}
	/* NOTREACHED */
}

static int
grmatchline(int search, gid_t gid, const char *name)
{
	unsigned long	id;
	char		**m;
	char		*cp, *bp, *ep;

	/* name may be NULL if search is nonzero */

	bp = grline;
	memset(&_gr_group, 0, sizeof(_gr_group));
	_gr_group.gr_name = strsep(&bp, ":\n");
	if (search && name && strcmp(_gr_group.gr_name, name))
		return 0;
	_gr_group.gr_passwd = strsep(&bp, ":\n");
	if (!(cp = strsep(&bp, ":\n")))
		return 0;
	id = strtoul(cp, &ep, 10);
	if (id > GID_MAX || *ep != '\0')
		return 0;
	_gr_group.gr_gid = (gid_t)id;
	if (search && name == NULL && _gr_group.gr_gid != gid)
		return 0;
	cp = NULL;
	if (bp == NULL)
		return 0;
	for (_gr_group.gr_mem = m = members;; bp++) {
		if (m == &members[MAXGRP - 1])
			break;
		if (*bp == ',') {
			if (cp) {
				*bp = '\0';
				*m++ = cp;
				cp = NULL;
			}
		} else if (*bp == '\0' || *bp == '\n' || *bp == ' ') {
			if (cp) {
				*bp = '\0';
				*m++ = cp;
			}
			break;
		} else if (cp == NULL)
			cp = bp;
	}
	*m = NULL;
	return 1;
}


/*
 * user lookup functions
 */

static struct passwd *
gi_getpwnam(const char *name)
{
	int rval;

	if (!pwstart())
		return NULL;
	rval = pwscan(1, 0, name);
	if (!_pw_stayopen)
		endpwent();
	return (rval) ? &_pw_passwd : NULL;
}

static struct passwd *
gi_getpwuid(uid_t uid)
{
	int rval;

	if (!pwstart())
		return NULL;
	rval = pwscan(1, uid, NULL);
	if (!_pw_stayopen)
		endpwent();
	return (rval) ? &_pw_passwd : NULL;
}

static int
gi_setpassent(int stayopen)
{

	if (!pwstart())
		return 0;
	_pw_stayopen = stayopen;
	return 1;
}

static void
gi_endpwent(void)
{

	_pw_filesdone = 0;
	if (_pw_fp) {
		(void)fclose(_pw_fp);
		_pw_fp = NULL;
	}
}

static int
pwstart(void)
{

	_pw_filesdone = 0;
	if (_pw_fp) {
		rewind(_pw_fp);
		return 1;
	}
	if (pwfile[0] == '\0')			/* sanity check */
		return 0;
	_pw_fp = fopen(pwfile, "r");
	if (_pw_fp != NULL)
		return 1;
	warn("Can't open `%s'", pwfile);
	return 0;
}


static int
pwscan(int search, uid_t uid, const char *name)
{

	if (_pw_filesdone)
		return 0;
	for (;;) {
		if (!fgets(pwline, sizeof(pwline), _pw_fp)) {
			if (!search)
				_pw_filesdone = 1;
			return 0;
		}
		/* skip lines that are too big */
		if (!strchr(pwline, '\n')) {
			int ch;

			while ((ch = getc(_pw_fp)) != '\n' && ch != EOF)
				;
			continue;
		}
		/* skip comments */
		if (pwline[0] == '#')
			continue;
		if (pwmatchline(search, uid, name))
			return 1;
	}
	/* NOTREACHED */
}

static int
pwmatchline(int search, uid_t uid, const char *name)
{
	unsigned long	id;
	char		*cp, *bp, *ep;

	/* name may be NULL if search is nonzero */

	bp = pwline;
	memset(&_pw_passwd, 0, sizeof(_pw_passwd));
	_pw_passwd.pw_name = strsep(&bp, ":\n");		/* name */
	if (search && name && strcmp(_pw_passwd.pw_name, name))
		return 0;

	_pw_passwd.pw_passwd = strsep(&bp, ":\n");		/* passwd */

	if (!(cp = strsep(&bp, ":\n")))				/* uid */
		return 0;
	id = strtoul(cp, &ep, 10);
	if (id > UID_MAX || *ep != '\0')
		return 0;
	_pw_passwd.pw_uid = (uid_t)id;
	if (search && name == NULL && _pw_passwd.pw_uid != uid)
		return 0;

	if (!(cp = strsep(&bp, ":\n")))				/* gid */
		return 0;
	id = strtoul(cp, &ep, 10);
	if (id > GID_MAX || *ep != '\0')
		return 0;
	_pw_passwd.pw_gid = (gid_t)id;

	if (!(ep = strsep(&bp, ":")))				/* class */
		return 0;
	if (!(ep = strsep(&bp, ":")))				/* change */
		return 0;
	if (!(ep = strsep(&bp, ":")))				/* expire */
		return 0;

	if (!(_pw_passwd.pw_gecos = strsep(&bp, ":\n")))	/* gecos */
		return 0;
	if (!(_pw_passwd.pw_dir = strsep(&bp, ":\n")))		/* directory */
		return 0;
	if (!(_pw_passwd.pw_shell = strsep(&bp, ":\n")))	/* shell */
		return 0;

	if (strchr(bp, ':') != NULL)
		return 0;

	return 1;
}

