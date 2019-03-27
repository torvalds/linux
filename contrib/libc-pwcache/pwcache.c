/*	$NetBSD: pwcache.c,v 1.31 2010/03/23 20:28:59 drochner Exp $	*/

/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
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
/*
 * XXX Undefine the renames of these functions so that we don't
 * XXX rename the versions found in the host's <pwd.h> by mistake!
 */
#undef group_from_gid
#undef user_from_uid
#endif

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)cache.c	8.1 (Berkeley) 5/31/93";
#else
__RCSID("$NetBSD: pwcache.c,v 1.31 2010/03/23 20:28:59 drochner Exp $");
#endif
#endif /* LIBC_SCCS and not lint */
__FBSDID("$FreeBSD$");

#include "namespace.h"

#include <sys/types.h>
#include <sys/param.h>

#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	_DIAGASSERT(x)	assert((x))

#if HAVE_NBTOOL_CONFIG_H
/* XXX Now, re-apply the renaming that we undid above. */
#define	group_from_gid	__nbcompat_group_from_gid
#define	user_from_uid	__nbcompat_user_from_uid
#endif

#ifdef __weak_alias
__weak_alias(user_from_uid,_user_from_uid)
__weak_alias(group_from_gid,_group_from_gid)
__weak_alias(pwcache_groupdb,_pwcache_groupdb)
#endif

#if !HAVE_PWCACHE_USERDB || HAVE_NBTOOL_CONFIG_H
#include "pwcache.h"

/*
 * routines that control user, group, uid and gid caches (for the archive
 * member print routine).
 * IMPORTANT:
 * these routines cache BOTH hits and misses, a major performance improvement
 */

/*
 * function pointers to various name lookup routines.
 * these may be changed as necessary.
 */
static	int		(*_pwcache_setgroupent)(int)		= setgroupent;
static	void		(*_pwcache_endgrent)(void)		= endgrent;
static	struct group *	(*_pwcache_getgrnam)(const char *)	= getgrnam;
static	struct group *	(*_pwcache_getgrgid)(gid_t)		= getgrgid;
static	int		(*_pwcache_setpassent)(int)		= setpassent;
static	void		(*_pwcache_endpwent)(void)		= endpwent;
static	struct passwd *	(*_pwcache_getpwnam)(const char *)	= getpwnam;
static	struct passwd *	(*_pwcache_getpwuid)(uid_t)		= getpwuid;

/*
 * internal state
 */
static	int	pwopn;		/* is password file open */
static	int	gropn;		/* is group file open */
static	UIDC	**uidtb;	/* uid to name cache */
static	GIDC	**gidtb;	/* gid to name cache */
static	UIDC	**usrtb;	/* user name to uid cache */
static	GIDC	**grptb;	/* group name to gid cache */

static	int	uidtb_fail;	/* uidtb_start() failed ? */
static	int	gidtb_fail;	/* gidtb_start() failed ? */
static	int	usrtb_fail;	/* usrtb_start() failed ? */
static	int	grptb_fail;	/* grptb_start() failed ? */


static	u_int	st_hash(const char *, size_t, int);
static	int	uidtb_start(void);
static	int	gidtb_start(void);
static	int	usrtb_start(void);
static	int	grptb_start(void);


static u_int
st_hash(const char *name, size_t len, int tabsz)
{
	u_int key = 0;

	_DIAGASSERT(name != NULL);

	while (len--) {
		key += *name++;
		key = (key << 8) | (key >> 24);
	}

	return (key % tabsz);
}

/*
 * uidtb_start
 *	creates an an empty uidtb
 * Return:
 *	0 if ok, -1 otherwise
 */
static int
uidtb_start(void)
{

	if (uidtb != NULL)
		return (0);
	if (uidtb_fail)
		return (-1);
	if ((uidtb = (UIDC **)calloc(UID_SZ, sizeof(UIDC *))) == NULL) {
		++uidtb_fail;
		return (-1);
	}
	return (0);
}

/*
 * gidtb_start
 *	creates an an empty gidtb
 * Return:
 *	0 if ok, -1 otherwise
 */
static int
gidtb_start(void)
{

	if (gidtb != NULL)
		return (0);
	if (gidtb_fail)
		return (-1);
	if ((gidtb = (GIDC **)calloc(GID_SZ, sizeof(GIDC *))) == NULL) {
		++gidtb_fail;
		return (-1);
	}
	return (0);
}

/*
 * usrtb_start
 *	creates an an empty usrtb
 * Return:
 *	0 if ok, -1 otherwise
 */
static int
usrtb_start(void)
{

	if (usrtb != NULL)
		return (0);
	if (usrtb_fail)
		return (-1);
	if ((usrtb = (UIDC **)calloc(UNM_SZ, sizeof(UIDC *))) == NULL) {
		++usrtb_fail;
		return (-1);
	}
	return (0);
}

/*
 * grptb_start
 *	creates an an empty grptb
 * Return:
 *	0 if ok, -1 otherwise
 */
static int
grptb_start(void)
{

	if (grptb != NULL)
		return (0);
	if (grptb_fail)
		return (-1);
	if ((grptb = (GIDC **)calloc(GNM_SZ, sizeof(GIDC *))) == NULL) {
		++grptb_fail;
		return (-1);
	}
	return (0);
}

/*
 * user_from_uid()
 *	caches the name (if any) for the uid. If noname clear, we always
 *	return the stored name (if valid or invalid match).
 *	We use a simple hash table.
 * Return
 *	Pointer to stored name (or a empty string)
 */
const char *
user_from_uid(uid_t uid, int noname)
{
	struct passwd *pw;
	UIDC *ptr, **pptr;

	if ((uidtb == NULL) && (uidtb_start() < 0))
		return (NULL);

	/*
	 * see if we have this uid cached
	 */
	pptr = uidtb + (uid % UID_SZ);
	ptr = *pptr;

	if ((ptr != NULL) && (ptr->valid > 0) && (ptr->uid == uid)) {
		/*
		 * have an entry for this uid
		 */
		if (!noname || (ptr->valid == VALID))
			return (ptr->name);
		return (NULL);
	}

	/*
	 * No entry for this uid, we will add it
	 */
	if (!pwopn) {
		if (_pwcache_setpassent != NULL)
			(*_pwcache_setpassent)(1);
		++pwopn;
	}

	if (ptr == NULL)
		*pptr = ptr = (UIDC *)malloc(sizeof(UIDC));

	if ((pw = (*_pwcache_getpwuid)(uid)) == NULL) {
		/*
		 * no match for this uid in the local password file
		 * a string that is the uid in numeric format
		 */
		if (ptr == NULL)
			return (NULL);
		ptr->uid = uid;
		(void)snprintf(ptr->name, UNMLEN, "%lu", (long) uid);
		ptr->valid = INVALID;
		if (noname)
			return (NULL);
	} else {
		/*
		 * there is an entry for this uid in the password file
		 */
		if (ptr == NULL)
			return (pw->pw_name);
		ptr->uid = uid;
		(void)strlcpy(ptr->name, pw->pw_name, UNMLEN);
		ptr->valid = VALID;
	}
	return (ptr->name);
}

/*
 * group_from_gid()
 *	caches the name (if any) for the gid. If noname clear, we always
 *	return the stored name (if valid or invalid match).
 *	We use a simple hash table.
 * Return
 *	Pointer to stored name (or a empty string)
 */
const char *
group_from_gid(gid_t gid, int noname)
{
	struct group *gr;
	GIDC *ptr, **pptr;

	if ((gidtb == NULL) && (gidtb_start() < 0))
		return (NULL);

	/*
	 * see if we have this gid cached
	 */
	pptr = gidtb + (gid % GID_SZ);
	ptr = *pptr;

	if ((ptr != NULL) && (ptr->valid > 0) && (ptr->gid == gid)) {
		/*
		 * have an entry for this gid
		 */
		if (!noname || (ptr->valid == VALID))
			return (ptr->name);
		return (NULL);
	}

	/*
	 * No entry for this gid, we will add it
	 */
	if (!gropn) {
		if (_pwcache_setgroupent != NULL)
			(*_pwcache_setgroupent)(1);
		++gropn;
	}

	if (ptr == NULL)
		*pptr = ptr = (GIDC *)malloc(sizeof(GIDC));

	if ((gr = (*_pwcache_getgrgid)(gid)) == NULL) {
		/*
		 * no match for this gid in the local group file, put in
		 * a string that is the gid in numberic format
		 */
		if (ptr == NULL)
			return (NULL);
		ptr->gid = gid;
		(void)snprintf(ptr->name, GNMLEN, "%lu", (long) gid);
		ptr->valid = INVALID;
		if (noname)
			return (NULL);
	} else {
		/*
		 * there is an entry for this group in the group file
		 */
		if (ptr == NULL)
			return (gr->gr_name);
		ptr->gid = gid;
		(void)strlcpy(ptr->name, gr->gr_name, GNMLEN);
		ptr->valid = VALID;
	}
	return (ptr->name);
}

/*
 * uid_from_user()
 *	caches the uid for a given user name. We use a simple hash table.
 * Return
 *	the uid (if any) for a user name, or a -1 if no match can be found
 */
int
uid_from_user(const char *name, uid_t *uid)
{
	struct passwd *pw;
	UIDC *ptr, **pptr;
	size_t namelen;

	/*
	 * return -1 for mangled names
	 */
	if (name == NULL || ((namelen = strlen(name)) == 0))
		return (-1);
	if ((usrtb == NULL) && (usrtb_start() < 0))
		return (-1);

	/*
	 * look up in hash table, if found and valid return the uid,
	 * if found and invalid, return a -1
	 */
	pptr = usrtb + st_hash(name, namelen, UNM_SZ);
	ptr = *pptr;

	if ((ptr != NULL) && (ptr->valid > 0) && !strcmp(name, ptr->name)) {
		if (ptr->valid == INVALID)
			return (-1);
		*uid = ptr->uid;
		return (0);
	}

	if (!pwopn) {
		if (_pwcache_setpassent != NULL)
			(*_pwcache_setpassent)(1);
		++pwopn;
	}

	if (ptr == NULL)
		*pptr = ptr = (UIDC *)malloc(sizeof(UIDC));

	/*
	 * no match, look it up, if no match store it as an invalid entry,
	 * or store the matching uid
	 */
	if (ptr == NULL) {
		if ((pw = (*_pwcache_getpwnam)(name)) == NULL)
			return (-1);
		*uid = pw->pw_uid;
		return (0);
	}
	(void)strlcpy(ptr->name, name, UNMLEN);
	if ((pw = (*_pwcache_getpwnam)(name)) == NULL) {
		ptr->valid = INVALID;
		return (-1);
	}
	ptr->valid = VALID;
	*uid = ptr->uid = pw->pw_uid;
	return (0);
}

/*
 * gid_from_group()
 *	caches the gid for a given group name. We use a simple hash table.
 * Return
 *	the gid (if any) for a group name, or a -1 if no match can be found
 */
int
gid_from_group(const char *name, gid_t *gid)
{
	struct group *gr;
	GIDC *ptr, **pptr;
	size_t namelen;

	/*
	 * return -1 for mangled names
	 */
	if (name == NULL || ((namelen = strlen(name)) == 0))
		return (-1);
	if ((grptb == NULL) && (grptb_start() < 0))
		return (-1);

	/*
	 * look up in hash table, if found and valid return the uid,
	 * if found and invalid, return a -1
	 */
	pptr = grptb + st_hash(name, namelen, GID_SZ);
	ptr = *pptr;

	if ((ptr != NULL) && (ptr->valid > 0) && !strcmp(name, ptr->name)) {
		if (ptr->valid == INVALID)
			return (-1);
		*gid = ptr->gid;
		return (0);
	}

	if (!gropn) {
		if (_pwcache_setgroupent != NULL)
			(*_pwcache_setgroupent)(1);
		++gropn;
	}

	if (ptr == NULL)
		*pptr = ptr = (GIDC *)malloc(sizeof(GIDC));

	/*
	 * no match, look it up, if no match store it as an invalid entry,
	 * or store the matching gid
	 */
	if (ptr == NULL) {
		if ((gr = (*_pwcache_getgrnam)(name)) == NULL)
			return (-1);
		*gid = gr->gr_gid;
		return (0);
	}

	(void)strlcpy(ptr->name, name, GNMLEN);
	if ((gr = (*_pwcache_getgrnam)(name)) == NULL) {
		ptr->valid = INVALID;
		return (-1);
	}
	ptr->valid = VALID;
	*gid = ptr->gid = gr->gr_gid;
	return (0);
}

#define FLUSHTB(arr, len, fail)				\
	do {						\
		if (arr != NULL) {			\
			for (i = 0; i < len; i++)	\
				if (arr[i] != NULL)	\
					free(arr[i]);	\
			arr = NULL;			\
		}					\
		fail = 0;				\
	} while (/* CONSTCOND */0);

int
pwcache_userdb(
	int		(*a_setpassent)(int),
	void		(*a_endpwent)(void),
	struct passwd *	(*a_getpwnam)(const char *),
	struct passwd *	(*a_getpwuid)(uid_t))
{
	int i;

		/* a_setpassent and a_endpwent may be NULL */
	if (a_getpwnam == NULL || a_getpwuid == NULL)
		return (-1);

	if (_pwcache_endpwent != NULL)
		(*_pwcache_endpwent)();
	FLUSHTB(uidtb, UID_SZ, uidtb_fail);
	FLUSHTB(usrtb, UNM_SZ, usrtb_fail);
	pwopn = 0;
	_pwcache_setpassent = a_setpassent;
	_pwcache_endpwent = a_endpwent;
	_pwcache_getpwnam = a_getpwnam;
	_pwcache_getpwuid = a_getpwuid;

	return (0);
}

int
pwcache_groupdb(
	int		(*a_setgroupent)(int),
	void		(*a_endgrent)(void),
	struct group *	(*a_getgrnam)(const char *),
	struct group *	(*a_getgrgid)(gid_t))
{
	int i;

		/* a_setgroupent and a_endgrent may be NULL */
	if (a_getgrnam == NULL || a_getgrgid == NULL)
		return (-1);

	if (_pwcache_endgrent != NULL)
		(*_pwcache_endgrent)();
	FLUSHTB(gidtb, GID_SZ, gidtb_fail);
	FLUSHTB(grptb, GNM_SZ, grptb_fail);
	gropn = 0;
	_pwcache_setgroupent = a_setgroupent;
	_pwcache_endgrent = a_endgrent;
	_pwcache_getgrnam = a_getgrnam;
	_pwcache_getgrgid = a_getgrgid;

	return (0);
}


#ifdef TEST_PWCACHE

struct passwd *
test_getpwnam(const char *name)
{
	static struct passwd foo;

	memset(&foo, 0, sizeof(foo));
	if (strcmp(name, "toor") == 0) {
		foo.pw_uid = 666;
		return &foo;
	}
	return (getpwnam(name));
}

int
main(int argc, char *argv[])
{
	uid_t	u;
	int	r, i;

	printf("pass 1 (default userdb)\n");
	for (i = 1; i < argc; i++) {
		printf("i: %d, pwopn %d usrtb_fail %d usrtb %p\n",
		    i, pwopn, usrtb_fail, usrtb);
		r = uid_from_user(argv[i], &u);
		if (r == -1)
			printf("  uid_from_user %s: failed\n", argv[i]);
		else
			printf("  uid_from_user %s: %d\n", argv[i], u);
	}
	printf("pass 1 finish: pwopn %d usrtb_fail %d usrtb %p\n",
		    pwopn, usrtb_fail, usrtb);

	puts("");
	printf("pass 2 (replacement userdb)\n");
	printf("pwcache_userdb returned %d\n",
	    pwcache_userdb(setpassent, test_getpwnam, getpwuid));
	printf("pwopn %d usrtb_fail %d usrtb %p\n", pwopn, usrtb_fail, usrtb);

	for (i = 1; i < argc; i++) {
		printf("i: %d, pwopn %d usrtb_fail %d usrtb %p\n",
		    i, pwopn, usrtb_fail, usrtb);
		u = -1;
		r = uid_from_user(argv[i], &u);
		if (r == -1)
			printf("  uid_from_user %s: failed\n", argv[i]);
		else
			printf("  uid_from_user %s: %d\n", argv[i], u);
	}
	printf("pass 2 finish: pwopn %d usrtb_fail %d usrtb %p\n",
		    pwopn, usrtb_fail, usrtb);

	puts("");
	printf("pass 3 (null pointers)\n");
	printf("pwcache_userdb returned %d\n",
	    pwcache_userdb(NULL, NULL, NULL));

	return (0);
}
#endif	/* TEST_PWCACHE */
#endif	/* !HAVE_PWCACHE_USERDB */
