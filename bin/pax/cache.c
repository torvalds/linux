/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)cache.c	8.1 (Berkeley) 5/31/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include "pax.h"
#include "cache.h"
#include "extern.h"

/*
 * routines that control user, group, uid and gid caches (for the archive
 * member print routine).
 * IMPORTANT:
 * these routines cache BOTH hits and misses, a major performance improvement
 */

static	int pwopn = 0;		/* is password file open */
static	int gropn = 0;		/* is group file open */
static UIDC **uidtb = NULL;	/* uid to name cache */
static GIDC **gidtb = NULL;	/* gid to name cache */
static UIDC **usrtb = NULL;	/* user name to uid cache */
static GIDC **grptb = NULL;	/* group name to gid cache */

/*
 * uidtb_start
 *	creates an an empty uidtb
 * Return:
 *	0 if ok, -1 otherwise
 */

int
uidtb_start(void)
{
	static int fail = 0;

	if (uidtb != NULL)
		return(0);
	if (fail)
		return(-1);
	if ((uidtb = (UIDC **)calloc(UID_SZ, sizeof(UIDC *))) == NULL) {
		++fail;
		paxwarn(1, "Unable to allocate memory for user id cache table");
		return(-1);
	}
	return(0);
}

/*
 * gidtb_start
 *	creates an an empty gidtb
 * Return:
 *	0 if ok, -1 otherwise
 */

int
gidtb_start(void)
{
	static int fail = 0;

	if (gidtb != NULL)
		return(0);
	if (fail)
		return(-1);
	if ((gidtb = (GIDC **)calloc(GID_SZ, sizeof(GIDC *))) == NULL) {
		++fail;
		paxwarn(1, "Unable to allocate memory for group id cache table");
		return(-1);
	}
	return(0);
}

/*
 * usrtb_start
 *	creates an an empty usrtb
 * Return:
 *	0 if ok, -1 otherwise
 */

int
usrtb_start(void)
{
	static int fail = 0;

	if (usrtb != NULL)
		return(0);
	if (fail)
		return(-1);
	if ((usrtb = (UIDC **)calloc(UNM_SZ, sizeof(UIDC *))) == NULL) {
		++fail;
		paxwarn(1, "Unable to allocate memory for user name cache table");
		return(-1);
	}
	return(0);
}

/*
 * grptb_start
 *	creates an an empty grptb
 * Return:
 *	0 if ok, -1 otherwise
 */

int
grptb_start(void)
{
	static int fail = 0;

	if (grptb != NULL)
		return(0);
	if (fail)
		return(-1);
	if ((grptb = (GIDC **)calloc(GNM_SZ, sizeof(GIDC *))) == NULL) {
		++fail;
		paxwarn(1,"Unable to allocate memory for group name cache table");
		return(-1);
	}
	return(0);
}

/*
 * name_uid()
 *	caches the name (if any) for the uid. If frc set, we always return the
 *	the stored name (if valid or invalid match). We use a simple hash table.
 * Return
 *	Pointer to stored name (or an empty string).
 */

const char *
name_uid(uid_t uid, int frc)
{
	struct passwd *pw;
	UIDC *ptr;

	if ((uidtb == NULL) && (uidtb_start() < 0))
		return("");

	/*
	 * see if we have this uid cached
	 */
	ptr = uidtb[uid % UID_SZ];
	if ((ptr != NULL) && (ptr->valid > 0) && (ptr->uid == uid)) {
		/*
		 * have an entry for this uid
		 */
		if (frc || (ptr->valid == VALID))
			return(ptr->name);
		return("");
	}

	/*
	 * No entry for this uid, we will add it
	 */
	if (!pwopn) {
		setpassent(1);
		++pwopn;
	}
	if (ptr == NULL)
		ptr = uidtb[uid % UID_SZ] = (UIDC *)malloc(sizeof(UIDC));

	if ((pw = getpwuid(uid)) == NULL) {
		/*
		 * no match for this uid in the local password file
		 * a string that is the uid in numeric format
		 */
		if (ptr == NULL)
			return("");
		ptr->uid = uid;
		ptr->valid = INVALID;
#		ifdef NET2_STAT
		(void)snprintf(ptr->name, sizeof(ptr->name), "%u", uid);
#		else
		(void)snprintf(ptr->name, sizeof(ptr->name), "%lu",
			       (unsigned long)uid);
#		endif
		if (frc == 0)
			return("");
	} else {
		/*
		 * there is an entry for this uid in the password file
		 */
		if (ptr == NULL)
			return(pw->pw_name);
		ptr->uid = uid;
		(void)strncpy(ptr->name, pw->pw_name, UNMLEN - 1);
		ptr->name[UNMLEN-1] = '\0';
		ptr->valid = VALID;
	}
	return(ptr->name);
}

/*
 * name_gid()
 *	caches the name (if any) for the gid. If frc set, we always return the
 *	the stored name (if valid or invalid match). We use a simple hash table.
 * Return
 *	Pointer to stored name (or an empty string).
 */

const char *
name_gid(gid_t gid, int frc)
{
	struct group *gr;
	GIDC *ptr;

	if ((gidtb == NULL) && (gidtb_start() < 0))
		return("");

	/*
	 * see if we have this gid cached
	 */
	ptr = gidtb[gid % GID_SZ];
	if ((ptr != NULL) && (ptr->valid > 0) && (ptr->gid == gid)) {
		/*
		 * have an entry for this gid
		 */
		if (frc || (ptr->valid == VALID))
			return(ptr->name);
		return("");
	}

	/*
	 * No entry for this gid, we will add it
	 */
	if (!gropn) {
		setgroupent(1);
		++gropn;
	}
	if (ptr == NULL)
		ptr = gidtb[gid % GID_SZ] = (GIDC *)malloc(sizeof(GIDC));

	if ((gr = getgrgid(gid)) == NULL) {
		/*
		 * no match for this gid in the local group file, put in
		 * a string that is the gid in numeric format
		 */
		if (ptr == NULL)
			return("");
		ptr->gid = gid;
		ptr->valid = INVALID;
#		ifdef NET2_STAT
		(void)snprintf(ptr->name, sizeof(ptr->name), "%u", gid);
#		else
		(void)snprintf(ptr->name, sizeof(ptr->name), "%lu",
			       (unsigned long)gid);
#		endif
		if (frc == 0)
			return("");
	} else {
		/*
		 * there is an entry for this group in the group file
		 */
		if (ptr == NULL)
			return(gr->gr_name);
		ptr->gid = gid;
		(void)strncpy(ptr->name, gr->gr_name, GNMLEN - 1);
		ptr->name[GNMLEN-1] = '\0';
		ptr->valid = VALID;
	}
	return(ptr->name);
}

/*
 * uid_name()
 *	caches the uid for a given user name. We use a simple hash table.
 * Return
 *	the uid (if any) for a user name, or a -1 if no match can be found
 */

int
uid_name(char *name, uid_t *uid)
{
	struct passwd *pw;
	UIDC *ptr;
	int namelen;

	/*
	 * return -1 for mangled names
	 */
	if (((namelen = strlen(name)) == 0) || (name[0] == '\0'))
		return(-1);
	if ((usrtb == NULL) && (usrtb_start() < 0))
		return(-1);

	/*
	 * look up in hash table, if found and valid return the uid,
	 * if found and invalid, return a -1
	 */
	ptr = usrtb[st_hash(name, namelen, UNM_SZ)];
	if ((ptr != NULL) && (ptr->valid > 0) && !strcmp(name, ptr->name)) {
		if (ptr->valid == INVALID)
			return(-1);
		*uid = ptr->uid;
		return(0);
	}

	if (!pwopn) {
		setpassent(1);
		++pwopn;
	}

	if (ptr == NULL)
		ptr = usrtb[st_hash(name, namelen, UNM_SZ)] =
		  (UIDC *)malloc(sizeof(UIDC));

	/*
	 * no match, look it up, if no match store it as an invalid entry,
	 * or store the matching uid
	 */
	if (ptr == NULL) {
		if ((pw = getpwnam(name)) == NULL)
			return(-1);
		*uid = pw->pw_uid;
		return(0);
	}
	(void)strncpy(ptr->name, name, UNMLEN - 1);
	ptr->name[UNMLEN-1] = '\0';
	if ((pw = getpwnam(name)) == NULL) {
		ptr->valid = INVALID;
		return(-1);
	}
	ptr->valid = VALID;
	*uid = ptr->uid = pw->pw_uid;
	return(0);
}

/*
 * gid_name()
 *	caches the gid for a given group name. We use a simple hash table.
 * Return
 *	the gid (if any) for a group name, or a -1 if no match can be found
 */

int
gid_name(char *name, gid_t *gid)
{
	struct group *gr;
	GIDC *ptr;
	int namelen;

	/*
	 * return -1 for mangled names
	 */
	if (((namelen = strlen(name)) == 0) || (name[0] == '\0'))
		return(-1);
	if ((grptb == NULL) && (grptb_start() < 0))
		return(-1);

	/*
	 * look up in hash table, if found and valid return the uid,
	 * if found and invalid, return a -1
	 */
	ptr = grptb[st_hash(name, namelen, GID_SZ)];
	if ((ptr != NULL) && (ptr->valid > 0) && !strcmp(name, ptr->name)) {
		if (ptr->valid == INVALID)
			return(-1);
		*gid = ptr->gid;
		return(0);
	}

	if (!gropn) {
		setgroupent(1);
		++gropn;
	}
	if (ptr == NULL)
		ptr = grptb[st_hash(name, namelen, GID_SZ)] =
		  (GIDC *)malloc(sizeof(GIDC));

	/*
	 * no match, look it up, if no match store it as an invalid entry,
	 * or store the matching gid
	 */
	if (ptr == NULL) {
		if ((gr = getgrnam(name)) == NULL)
			return(-1);
		*gid = gr->gr_gid;
		return(0);
	}

	(void)strncpy(ptr->name, name, GNMLEN - 1);
	ptr->name[GNMLEN-1] = '\0';
	if ((gr = getgrnam(name)) == NULL) {
		ptr->valid = INVALID;
		return(-1);
	}
	ptr->valid = VALID;
	*gid = ptr->gid = gr->gr_gid;
	return(0);
}
