/*	$NetBSD: create.c,v 1.73 2014/04/24 17:22:41 christos Exp $	*/

/*-
 * Copyright (c) 1989, 1993
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

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#if defined(__RCSID) && !defined(lint)
#if 0
static char sccsid[] = "@(#)create.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: create.c,v 1.73 2014/04/24 17:22:41 christos Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#if ! HAVE_NBTOOL_CONFIG_H
#include <dirent.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NO_MD5
#include <md5.h>
#endif
#ifndef NO_RMD160
#include <rmd160.h>
#endif
#ifndef NO_SHA1
#include <sha1.h>
#endif
#ifndef NO_SHA2
#include <sha2.h>
#endif

#include "extern.h"

#define	INDENTNAMELEN	15
#define	MAXLINELEN	80

static gid_t gid;
static uid_t uid;
static mode_t mode;
static u_long flags;

#ifdef __FreeBSD__
#define	FTS_CONST const
#else
#define	FTS_CONST
#endif

static int	dcmp(const FTSENT *FTS_CONST *, const FTSENT *FTS_CONST *);
static void	output(FILE *, int, int *, const char *, ...)
    __printflike(4, 5);
static int	statd(FILE *, FTS *, FTSENT *, uid_t *, gid_t *, mode_t *,
    u_long *);
static void	statf(FILE *, int, FTSENT *);

void
cwalk(FILE *fp)
{
	FTS *t;
	FTSENT *p;
	time_t clocktime;
	char host[MAXHOSTNAMELEN + 1];
	const char *user;
	char *argv[2];
	char  dot[] = ".";
	int indent = 0;

	argv[0] = dot;
	argv[1] = NULL;

	time(&clocktime);
	gethostname(host, sizeof(host));
	host[sizeof(host) - 1] = '\0';
	if ((user = getlogin()) == NULL) {
		struct passwd *pw;
		user = (pw = getpwuid(getuid())) == NULL ? pw->pw_name :
		    "<unknown>";
	}

	if (!nflag)
		fprintf(fp,
	    	    "#\t   user: %s\n#\tmachine: %s\n#\t   tree: %s\n"
		    "#\t   date: %s",
		    user, host, fullpath, ctime(&clocktime));

	if ((t = fts_open(argv, ftsoptions, dcmp)) == NULL)
		mtree_err("fts_open: %s", strerror(errno));
	while ((p = fts_read(t)) != NULL) {
		if (jflag)
			indent = p->fts_level * 4;
		if (check_excludes(p->fts_name, p->fts_path)) {
			fts_set(t, p, FTS_SKIP);
			continue;
		}
		if (!find_only(p->fts_path)) {
			fts_set(t, p, FTS_SKIP);
			continue;
		}
		switch(p->fts_info) {
		case FTS_D:
			if (!bflag)
				fprintf(fp, "\n");
			if (!nflag)
				fprintf(fp, "# %s\n", p->fts_path);
			statd(fp, t, p, &uid, &gid, &mode, &flags);
			statf(fp, indent, p);
			break;
		case FTS_DP:
			if (p->fts_level > 0)
				if (!nflag)
					fprintf(fp, "%*s# %s\n", indent, "",
					    p->fts_path);
			if (p->fts_level > 0 || flavor == F_FREEBSD9) {
				fprintf(fp, "%*s..\n", indent, "");
				if (!bflag)
					fprintf(fp, "\n");
			}
			break;
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			mtree_err("%s: %s",
			    p->fts_path, strerror(p->fts_errno));
			break;
		default:
			if (!dflag)
				statf(fp, indent, p);
			break;

		}
	}
	fts_close(t);
	if (sflag && keys & F_CKSUM)
		mtree_err("%s checksum: %u", fullpath, crc_total);
}

static void
statf(FILE *fp, int indent, FTSENT *p)
{
	u_int32_t len, val;
	int fd, offset;
	const char *name = NULL;
#if !defined(NO_MD5) || !defined(NO_RMD160) || !defined(NO_SHA1) || !defined(NO_SHA2)
	char *digestbuf;
#endif

	offset = fprintf(fp, "%*s%s%s", indent, "",
	    S_ISDIR(p->fts_statp->st_mode) ? "" : "    ", vispath(p->fts_name));

	if (offset > (INDENTNAMELEN + indent))
		offset = MAXLINELEN;
	else
		offset += fprintf(fp, "%*s",
		    (INDENTNAMELEN + indent) - offset, "");

	if (!S_ISREG(p->fts_statp->st_mode) && (flavor == F_NETBSD6 || !dflag))
		output(fp, indent, &offset, "type=%s",
		    inotype(p->fts_statp->st_mode));
	if (keys & (F_UID | F_UNAME) && p->fts_statp->st_uid != uid) {
		if (keys & F_UNAME &&
		    (name = user_from_uid(p->fts_statp->st_uid, 1)) != NULL)
			output(fp, indent, &offset, "uname=%s", name);
		if (keys & F_UID || (keys & F_UNAME && name == NULL))
			output(fp, indent, &offset, "uid=%u",
			    p->fts_statp->st_uid);
	}
	if (keys & (F_GID | F_GNAME) && p->fts_statp->st_gid != gid) {
		if (keys & F_GNAME &&
		    (name = group_from_gid(p->fts_statp->st_gid, 1)) != NULL)
			output(fp, indent, &offset, "gname=%s", name);
		if (keys & F_GID || (keys & F_GNAME && name == NULL))
			output(fp, indent, &offset, "gid=%u",
			    p->fts_statp->st_gid);
	}
	if (keys & F_MODE && (p->fts_statp->st_mode & MBITS) != mode)
		output(fp, indent, &offset, "mode=%#o",
		    p->fts_statp->st_mode & MBITS);
	if (keys & F_DEV &&
	    (S_ISBLK(p->fts_statp->st_mode) || S_ISCHR(p->fts_statp->st_mode)))
		output(fp, indent, &offset, "device=%#jx",
		    (uintmax_t)p->fts_statp->st_rdev);
	if (keys & F_NLINK && p->fts_statp->st_nlink != 1)
		output(fp, indent, &offset, "nlink=%ju",
		    (uintmax_t)p->fts_statp->st_nlink);
	if (keys & F_SIZE &&
	    (flavor == F_FREEBSD9 || S_ISREG(p->fts_statp->st_mode)))
		output(fp, indent, &offset, "size=%ju",
		    (uintmax_t)p->fts_statp->st_size);
	if (keys & F_TIME)
#if defined(BSD4_4) && !defined(HAVE_NBTOOL_CONFIG_H)
		output(fp, indent, &offset, "time=%jd.%09ld",
		    (intmax_t)p->fts_statp->st_mtimespec.tv_sec,
		    p->fts_statp->st_mtimespec.tv_nsec);
#else
		output(fp, indent, &offset, "time=%jd.%09ld",
		    (intmax_t)p->fts_statp->st_mtime, (long)0);
#endif
	if (keys & F_CKSUM && S_ISREG(p->fts_statp->st_mode)) {
		if ((fd = open(p->fts_accpath, O_RDONLY, 0)) < 0 ||
		    crc(fd, &val, &len))
			mtree_err("%s: %s", p->fts_accpath, strerror(errno));
		close(fd);
		output(fp, indent, &offset, "cksum=%lu", (long)val);
	}
#ifndef NO_MD5
	if (keys & F_MD5 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = MD5File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: MD5File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", MD5KEY, digestbuf);
		free(digestbuf);
	}
#endif	/* ! NO_MD5 */
#ifndef NO_RMD160
	if (keys & F_RMD160 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = RMD160File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: RMD160File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", RMD160KEY, digestbuf);
		free(digestbuf);
	}
#endif	/* ! NO_RMD160 */
#ifndef NO_SHA1
	if (keys & F_SHA1 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = SHA1File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: SHA1File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", SHA1KEY, digestbuf);
		free(digestbuf);
	}
#endif	/* ! NO_SHA1 */
#ifndef NO_SHA2
	if (keys & F_SHA256 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = SHA256_File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: SHA256_File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", SHA256KEY, digestbuf);
		free(digestbuf);
	}
#ifdef SHA384_BLOCK_LENGTH
	if (keys & F_SHA384 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = SHA384_File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: SHA384_File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", SHA384KEY, digestbuf);
		free(digestbuf);
	}
#endif
	if (keys & F_SHA512 && S_ISREG(p->fts_statp->st_mode)) {
		if ((digestbuf = SHA512_File(p->fts_accpath, NULL)) == NULL)
			mtree_err("%s: SHA512_File failed: %s", p->fts_accpath,
			    strerror(errno));
		output(fp, indent, &offset, "%s=%s", SHA512KEY, digestbuf);
		free(digestbuf);
	}
#endif	/* ! NO_SHA2 */
	if (keys & F_SLINK &&
	    (p->fts_info == FTS_SL || p->fts_info == FTS_SLNONE))
		output(fp, indent, &offset, "link=%s",
		    vispath(rlink(p->fts_accpath)));
#if HAVE_STRUCT_STAT_ST_FLAGS
	if (keys & F_FLAGS && p->fts_statp->st_flags != flags) {
		char *str = flags_to_string(p->fts_statp->st_flags, "none");
		output(fp, indent, &offset, "flags=%s", str);
		free(str);
	}
#endif
	putchar('\n');
}

/* XXX
 * FLAGS2INDEX will fail once the user and system settable bits need more
 * than one byte, respectively.
 */
#define FLAGS2INDEX(x)  (((x >> 8) & 0x0000ff00) | (x & 0x000000ff))

#define	MTREE_MAXGID	5000
#define	MTREE_MAXUID	5000
#define	MTREE_MAXMODE	(MBITS + 1)
#if HAVE_STRUCT_STAT_ST_FLAGS
#define	MTREE_MAXFLAGS  (FLAGS2INDEX(CH_MASK) + 1)   /* 1808 */
#else
#define MTREE_MAXFLAGS	1
#endif
#define	MTREE_MAXS 16

static int
statd(FILE *fp, FTS *t, FTSENT *parent, uid_t *puid, gid_t *pgid, mode_t *pmode,
    u_long *pflags)
{
	FTSENT *p;
	gid_t sgid;
	uid_t suid;
	mode_t smode;
	u_long sflags = 0;
	const char *name = NULL;
	gid_t savegid;
	uid_t saveuid;
	mode_t savemode;
	u_long saveflags;
	u_short maxgid, maxuid, maxmode, maxflags;
	u_short g[MTREE_MAXGID], u[MTREE_MAXUID],
		m[MTREE_MAXMODE], f[MTREE_MAXFLAGS];
	static int first = 1;

	savegid = *pgid;
	saveuid = *puid;
	savemode = *pmode;
	saveflags = *pflags;
	if ((p = fts_children(t, 0)) == NULL) {
		if (errno)
			mtree_err("%s: %s", RP(parent), strerror(errno));
		return (1);
	}

	memset(g, 0, sizeof(g));
	memset(u, 0, sizeof(u));
	memset(m, 0, sizeof(m));
	memset(f, 0, sizeof(f));

	maxuid = maxgid = maxmode = maxflags = 0;
	for (; p; p = p->fts_link) {
		if (flavor == F_NETBSD6 || !dflag ||
		    (dflag && S_ISDIR(p->fts_statp->st_mode))) {
			smode = p->fts_statp->st_mode & MBITS;
			if (smode < MTREE_MAXMODE && ++m[smode] > maxmode) {
				savemode = smode;
				maxmode = m[smode];
			}
			sgid = p->fts_statp->st_gid;
			if (sgid < MTREE_MAXGID && ++g[sgid] > maxgid) {
				savegid = sgid;
				maxgid = g[sgid];
			}
			suid = p->fts_statp->st_uid;
			if (suid < MTREE_MAXUID && ++u[suid] > maxuid) {
				saveuid = suid;
				maxuid = u[suid];
			}

#if HAVE_STRUCT_STAT_ST_FLAGS
			sflags = FLAGS2INDEX(p->fts_statp->st_flags);
			if (sflags < MTREE_MAXFLAGS && ++f[sflags] > maxflags) {
				saveflags = p->fts_statp->st_flags;
				maxflags = f[sflags];
			}
#endif
		}
	}
	/*
	 * If the /set record is the same as the last one we do not need to
	 * output a new one.  So first we check to see if anything changed.
	 * Note that we always output a /set record for the first directory.
	 */
	if (((keys & (F_UNAME | F_UID)) && (*puid != saveuid)) ||
	    ((keys & (F_GNAME | F_GID)) && (*pgid != savegid)) ||
	    ((keys & F_MODE) && (*pmode != savemode)) || 
	    ((keys & F_FLAGS) && (*pflags != saveflags)) ||
	    first) {
		first = 0;
		if (flavor != F_NETBSD6 && dflag)
			fprintf(fp, "/set type=dir");
		else
			fprintf(fp, "/set type=file");
		if (keys & (F_UID | F_UNAME)) {
			if (keys & F_UNAME &&
			    (name = user_from_uid(saveuid, 1)) != NULL)
				fprintf(fp, " uname=%s", name);
			if (keys & F_UID || (keys & F_UNAME && name == NULL))
				fprintf(fp, " uid=%lu", (u_long)saveuid);
		}
		if (keys & (F_GID | F_GNAME)) {
			if (keys & F_GNAME &&
			    (name = group_from_gid(savegid, 1)) != NULL)
				fprintf(fp, " gname=%s", name);
			if (keys & F_GID || (keys & F_GNAME && name == NULL))
				fprintf(fp, " gid=%lu", (u_long)savegid);
		}
		if (keys & F_MODE)
			fprintf(fp, " mode=%#lo", (u_long)savemode);
		if (keys & F_NLINK)
			fprintf(fp, " nlink=1");
		if (keys & F_FLAGS) {
			char *str = flags_to_string(saveflags, "none");
			fprintf(fp, " flags=%s", str);
			free(str);
		}
		fprintf(fp, "\n");
		*puid = saveuid;
		*pgid = savegid;
		*pmode = savemode;
		*pflags = saveflags;
	}
	return (0);
}

/*
 * dcmp --
 *	used as a comparison function passed to fts_open() to control
 *	the order in which fts_read() returns results.	We make
 *	directories sort after non-directories, but otherwise sort in
 *	strcmp() order.
 *
 * Keep this in sync with nodecmp() in spec.c.
 */
static int
dcmp(const FTSENT *FTS_CONST *a, const FTSENT *FTS_CONST *b)
{

	if (S_ISDIR((*a)->fts_statp->st_mode)) {
		if (!S_ISDIR((*b)->fts_statp->st_mode))
			return (1);
	} else if (S_ISDIR((*b)->fts_statp->st_mode))
		return (-1);
	return (strcmp((*a)->fts_name, (*b)->fts_name));
}

void
output(FILE *fp, int indent, int *offset, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (*offset + strlen(buf) > MAXLINELEN - 3) {
		fprintf(fp, " \\\n%*s", INDENTNAMELEN + indent, "");
		*offset = INDENTNAMELEN + indent;
	}
	*offset += fprintf(fp, " %s", buf) + 1;
}
