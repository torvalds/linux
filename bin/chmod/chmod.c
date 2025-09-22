/*	$OpenBSD: chmod.c,v 1.43 2018/09/16 02:44:06 millert Exp $	*/
/*	$NetBSD: chmod.c,v 1.12 1995/03/21 09:02:09 cgd Exp $	*/

/*
 * Copyright (c) 1989, 1993, 1994
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

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int ischflags, ischown, ischgrp, ischmod;
extern char *__progname;

gid_t a_gid(const char *);
uid_t a_uid(const char *, int);
static void __dead usage(void);

int
main(int argc, char *argv[])
{
	FTS *ftsp;
	FTSENT *p;
	void *set;
	unsigned long val;
	int oct;
	mode_t omode;
	int Hflag, Lflag, Rflag, ch, fflag, fts_options, hflag, rval, atflags;
	uid_t uid;
	gid_t gid;
	u_int32_t fclear, fset;
	char *ep, *mode, *cp, *flags;

	if (strlen(__progname) > 2) {
		ischown = __progname[2] == 'o';
		ischgrp = __progname[2] == 'g';
		ischmod = __progname[2] == 'm';
		ischflags = __progname[2] == 'f';
	}

	uid = (uid_t)-1;
	gid = (gid_t)-1;
	Hflag = Lflag = Rflag = fflag = hflag = 0;
	while ((ch = getopt(argc, argv, "HLPRXfghorstuwx")) != -1)
		switch (ch) {
		case 'H':
			Hflag = 1;
			Lflag = 0;
			break;
		case 'L':
			Lflag = 1;
			Hflag = 0;
			break;
		case 'P':
			Hflag = Lflag = 0;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'f':		/* no longer documented. */
			fflag = 1;
			break;
		case 'h':
			hflag = 1;
			break;
		/*
		 * If this is a symbolic mode argument rather than
		 * an option, we are done with option processing.
		 */
		case 'g': case 'o': case 'r': case 's':
		case 't': case 'u': case 'w': case 'X': case 'x':
			if (!ischmod)
				usage();
			/*
			 * If getopt() moved past the argument, back up.
			 * If the argument contains option letters before
			 * mode letters, setmode() will catch them.
			 */
			if (optind > 1) {
				cp = argv[optind - 1];
				if (cp[strlen(cp) - 1] == ch)
					--optind;
			}
			goto done;
		default:
			usage();
		}
done:
	argv += optind;
	argc -= optind;

	if (argc < 2)
		usage();

	/*
	 * We alter the symlink itself if doing -h or -RP, or
	 * if doing -RH and the symlink wasn't a command line arg.
	 */
	atflags = AT_SYMLINK_NOFOLLOW;

	fts_options = FTS_PHYSICAL;
	if (Rflag) {
		if (hflag)
			errx(1,
		"the -R and -h options may not be specified together.");
		if (Hflag)
			fts_options |= FTS_COMFOLLOW;
		if (Lflag) {
			fts_options &= ~FTS_PHYSICAL;
			fts_options |= FTS_LOGICAL;
			atflags = 0;
		}
	} else if (!hflag) {
		fts_options |= FTS_COMFOLLOW;
		atflags = 0;
	}

	if (ischflags) {
		if (pledge("stdio rpath fattr", NULL) == -1)
			err(1, "pledge");

		flags = *argv;
		if (*flags >= '0' && *flags <= '7') {
			errno = 0;
			val = strtoul(flags, &ep, 8);
			if (val > UINT_MAX)
				errno = ERANGE;
			if (errno)
				err(1, "invalid flags: %s", flags);
			if (*ep)
				errx(1, "invalid flags: %s", flags);
			fset = val;
			oct = 1;
		} else {
			if (strtofflags(&flags, &fset, &fclear))
				errx(1, "invalid flag: %s", flags);
			fclear = ~fclear;
			oct = 0;
		}
	} else if (ischmod) {
		mode = *argv;
		if (*mode >= '0' && *mode <= '7') {
			errno = 0;
			val = strtoul(mode, &ep, 8);
			if (val > INT_MAX)
				errno = ERANGE;
			if (errno)
				err(1, "invalid file mode: %s", mode);
			if (*ep)
				errx(1, "invalid file mode: %s", mode);
			omode = val;
			oct = 1;
		} else {
			if ((set = setmode(mode)) == NULL)
				errx(1, "invalid file mode: %s", mode);
			oct = 0;
		}
	} else if (ischown) {
		/* Both UID and GID are given. */
		if ((cp = strchr(*argv, ':')) != NULL) {
			*cp++ = '\0';
			gid = a_gid(cp);
		}
		/*
		 * UID and GID are separated by a dot and UID exists.
		 * required for backwards compatibility pre-dating POSIX.2
		 * likely to stay here forever
		 */
		else if ((cp = strchr(*argv, '.')) != NULL &&
		    (uid = a_uid(*argv, 1)) == (uid_t)-1) {
			*cp++ = '\0';
			gid = a_gid(cp);
		}
		if (uid == (uid_t)-1)
			uid = a_uid(*argv, 0);
	} else
		gid = a_gid(*argv);

	if ((ftsp = fts_open(++argv, fts_options, 0)) == NULL)
		err(1, NULL);
	for (rval = 0; (p = fts_read(ftsp)) != NULL;) {
		switch (p->fts_info) {
		case FTS_D:
			if (!Rflag)
				fts_set(ftsp, p, FTS_SKIP);
			if (ischmod)
				break;
			else
				continue;
		case FTS_DNR:			/* Warn, chmod, continue. */
			warnc(p->fts_errno, "%s", p->fts_path);
			rval = 1;
			break;
		case FTS_DP:			/* Already changed at FTS_D. */
			if (ischmod)
				continue;
			else
				break;
		case FTS_ERR:			/* Warn, continue. */
		case FTS_NS:
			warnc(p->fts_errno, "%s", p->fts_path);
			rval = 1;
			continue;
		case FTS_SL:			/* Ignore. */
		case FTS_SLNONE:
			/*
			 * The only symlinks that end up here are ones that
			 * don't point to anything or that loop and ones
			 * that we found doing a physical walk.
			 */
			if (!hflag && (fts_options & FTS_LOGICAL))
				continue;
			break;
		default:
			break;
		}

		/*
		 * For -RH, the decision of how to handle symlinks depends
		 * on the level: follow it iff it's a command line arg.
		 */
		if (fts_options & FTS_COMFOLLOW) {
			atflags = p->fts_level == FTS_ROOTLEVEL ? 0 : 
			    AT_SYMLINK_NOFOLLOW;
		}

		if (ischmod) {
			if (!fchmodat(AT_FDCWD, p->fts_accpath, oct ? omode :
			    getmode(set, p->fts_statp->st_mode), atflags)
			    || fflag)
				continue;
		} else if (!ischflags) {
			if (!fchownat(AT_FDCWD, p->fts_accpath, uid, gid,
			    atflags) || fflag)
				continue;
		} else {
			if (!chflagsat(AT_FDCWD, p->fts_accpath, oct ? fset :
			    (p->fts_statp->st_flags | fset) & fclear, atflags))
				continue;
		}

		/* error case */
		warn("%s", p->fts_path);
		rval = 1;
	}
	if (errno)
		err(1, "fts_read");
	fts_close(ftsp);
	return (rval);
}

/*
 * Given a UID or user name in a string, return the UID.  If an empty string
 * was given, returns -1.  If silent is 0, exits on invalid user names/UIDs;
 * otherwise, returns -1.
 */
uid_t
a_uid(const char *s, int silent)
{
	const char *errstr;
	uid_t uid;

	if (*s == '\0')			/* Argument was "[:.]gid". */
		return ((uid_t)-1);

	/* User name was given. */
	if (uid_from_user(s, &uid) != -1)
		return (uid);

	/* UID was given. */
	uid = (uid_t)strtonum(s, 0, UID_MAX, &errstr);
	if (errstr) {
		if (silent)
			return ((uid_t)-1);
		else
			errx(1, "user is %s: %s", errstr, s);
	}

	return (uid);
}

/*
 * Given a GID or group name in a string, return the GID.  If an empty string
 * was given, returns -1.  Exits on invalid user names/UIDs.
 */
gid_t
a_gid(const char *s)
{
	const char *errstr;
	gid_t gid;

	if (*s == '\0')			/* Argument was "uid[:.]". */
		return ((gid_t)-1);

	/* Group name was given. */
	if (gid_from_group(s, &gid) != -1)
		return (gid);

	/* GID was given. */
	gid = (gid_t)strtonum(s, 0, GID_MAX, &errstr);
	if (errstr)
		errx(1, "group is %s: %s", errstr, s);

	return (gid);
}

static void __dead
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-h] [-R [-H | -L | -P]] %s file ...\n",
	    __progname, ischmod ? "mode" : ischflags ? "flags" :
	    ischown ? "owner[:group]" : "group");
	if (ischown)
		fprintf(stderr,
		    "       %s [-h] [-R [-H | -L | -P]] :group file ...\n",
		    __progname);
	exit(1);
}
