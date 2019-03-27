/*	$NetBSD: compare.c,v 1.58 2013/11/21 18:39:50 christos Exp $	*/

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
static char sccsid[] = "@(#)compare.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: compare.c,v 1.58 2013/11/21 18:39:50 christos Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

#define	INDENTNAMELEN	8
#define MARK								\
do {									\
	if (flavor == F_FREEBSD9) {					\
		len = printf("%s changed\n", RP(p));			\
		tab = "\t";						\
	} else {							\
		len = printf("%s: ", RP(p));				\
		if (len > INDENTNAMELEN) {				\
			tab = "\t";					\
			printf("\n");					\
		} else {						\
			tab = "";					\
			printf("%*s", INDENTNAMELEN - (int)len, "");	\
		}							\
	}								\
} while (0)
#define	LABEL if (!label++) MARK

#if HAVE_STRUCT_STAT_ST_FLAGS


#define CHANGEFLAGS							\
	if (flags != p->fts_statp->st_flags) {				\
		char *sf;						\
		if (!label) {						\
			MARK;						\
			sf = flags_to_string(p->fts_statp->st_flags, "none"); \
			printf("%sflags (\"%s\"", tab, sf);		\
			free(sf);					\
		}							\
		if (lchflags(p->fts_accpath, flags)) {			\
			label++;					\
			printf(", not modified: %s)\n",			\
			    strerror(errno));				\
		} else {						\
			sf = flags_to_string(flags, "none");		\
			printf(", modified to \"%s\")\n", sf);		\
			free(sf);					\
		}							\
	}

/* SETFLAGS:
 * given pflags, additionally set those flags specified in s->st_flags and
 * selected by mask (the other flags are left unchanged).
 */
#define SETFLAGS(pflags, mask)						\
do {									\
	flags = (s->st_flags & (mask)) | (pflags);			\
	CHANGEFLAGS;							\
} while (0)

/* CLEARFLAGS:
 * given pflags, reset the flags specified in s->st_flags and selected by mask
 * (the other flags are left unchanged).
 */
#define CLEARFLAGS(pflags, mask)					\
do {									\
	flags = (~(s->st_flags & (mask)) & CH_MASK) & (pflags);		\
	CHANGEFLAGS;							\
} while (0)
#endif	/* HAVE_STRUCT_STAT_ST_FLAGS */

int
compare(NODE *s, FTSENT *p)
{
	u_int32_t len, val, flags;
	int fd, label;
	const char *cp, *tab;
#if !defined(NO_MD5) || !defined(NO_RMD160) || !defined(NO_SHA1) || !defined(NO_SHA2)
	char *digestbuf;
#endif

	tab = NULL;
	label = 0;
	switch(s->type) {
	case F_BLOCK:
		if (!S_ISBLK(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_CHAR:
		if (!S_ISCHR(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_DIR:
		if (!S_ISDIR(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_FIFO:
		if (!S_ISFIFO(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_FILE:
		if (!S_ISREG(p->fts_statp->st_mode))
			goto typeerr;
		break;
	case F_LINK:
		if (!S_ISLNK(p->fts_statp->st_mode))
			goto typeerr;
		break;
#ifdef S_ISSOCK
	case F_SOCK:
		if (!S_ISSOCK(p->fts_statp->st_mode))
			goto typeerr;
		break;
#endif
typeerr:		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "\ttype expected %s found %s\n" : "\ttype (%s, %s)\n",
		    nodetype(s->type), inotype(p->fts_statp->st_mode));
		return (label);
	}
	if (mtree_Wflag)
		goto afterpermwhack;
#if HAVE_STRUCT_STAT_ST_FLAGS
	if (iflag && !uflag) {
		if (s->flags & F_FLAGS)
		    SETFLAGS(p->fts_statp->st_flags, SP_FLGS);
		return (label);
        }
	if (mflag && !uflag) {
		if (s->flags & F_FLAGS)
		    CLEARFLAGS(p->fts_statp->st_flags, SP_FLGS);
		return (label);
        }
#endif
	if (s->flags & F_DEV &&
	    (s->type == F_BLOCK || s->type == F_CHAR) &&
	    s->st_rdev != p->fts_statp->st_rdev) {
		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%sdevice expected %#jx found %#jx" :
		    "%sdevice (%#jx, %#jx",
		    tab, (uintmax_t)s->st_rdev,
		    (uintmax_t)p->fts_statp->st_rdev);
		if (uflag) {
			if ((unlink(p->fts_accpath) == -1) ||
			    (mknod(p->fts_accpath,
			      s->st_mode | nodetoino(s->type),
			      s->st_rdev) == -1) ||
			    (lchown(p->fts_accpath, p->fts_statp->st_uid,
			      p->fts_statp->st_gid) == -1) )
				printf(", not modified: %s%s\n",
				    strerror(errno),
				    flavor == F_FREEBSD9 ? "" : ")");
			 else
				printf(", modified%s\n",
				    flavor == F_FREEBSD9 ? "" : ")");
		} else
			printf(")\n");
		tab = "\t";
	}
	/* Set the uid/gid first, then set the mode. */
	if (s->flags & (F_UID | F_UNAME) && s->st_uid != p->fts_statp->st_uid) {
		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%suser expected %lu found %lu" : "%suser (%lu, %lu",
		    tab, (u_long)s->st_uid, (u_long)p->fts_statp->st_uid);
		if (uflag) {
			if (lchown(p->fts_accpath, s->st_uid, -1))
				printf(", not modified: %s%s\n",
				    strerror(errno),
				    flavor == F_FREEBSD9 ? "" : ")");
			else
				printf(", modified%s\n",
				    flavor == F_FREEBSD9 ? "" : ")");
		} else
			printf(")\n");
		tab = "\t";
	}
	if (s->flags & (F_GID | F_GNAME) && s->st_gid != p->fts_statp->st_gid) {
		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%sgid expected %lu found %lu" : "%sgid (%lu, %lu",
		    tab, (u_long)s->st_gid, (u_long)p->fts_statp->st_gid);
		if (uflag) {
			if (lchown(p->fts_accpath, -1, s->st_gid))
				printf(", not modified: %s%s\n",
				    strerror(errno),
				    flavor == F_FREEBSD9 ? "" : ")");
			else
				printf(", modified%s\n",
				    flavor == F_FREEBSD9 ? "" : ")");
		}
		else
			printf(")\n");
		tab = "\t";
	}
	if (s->flags & F_MODE &&
	    s->st_mode != (p->fts_statp->st_mode & MBITS)) {
		if (lflag) {
			mode_t tmode, mode;

			tmode = s->st_mode;
			mode = p->fts_statp->st_mode & MBITS;
			/*
			 * if none of the suid/sgid/etc bits are set,
			 * then if the mode is a subset of the target,
			 * skip.
			 */
			if (!((tmode & ~(S_IRWXU|S_IRWXG|S_IRWXO)) ||
			    (mode & ~(S_IRWXU|S_IRWXG|S_IRWXO))))
				if ((mode | tmode) == tmode)
					goto skip;
		}

		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%spermissions expcted %#lo found %#lo" :
		    "%spermissions (%#lo, %#lo",
		    tab, (u_long)s->st_mode,
		    (u_long)p->fts_statp->st_mode & MBITS);
		if (uflag) {
			if (lchmod(p->fts_accpath, s->st_mode))
				printf(", not modified: %s%s\n",
				    strerror(errno),
				    flavor == F_FREEBSD9 ? "" : ")");
			else
				printf(", modified%s\n",
				    flavor == F_FREEBSD9 ? "" : ")");
		}
		else
			printf(")\n");
		tab = "\t";
	skip:	;
	}
	if (s->flags & F_NLINK && s->type != F_DIR &&
	    s->st_nlink != p->fts_statp->st_nlink) {
		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%slink count expected %lu found %lu\n" :
		    "%slink count (%lu, %lu)\n",
		    tab, (u_long)s->st_nlink, (u_long)p->fts_statp->st_nlink);
		tab = "\t";
	}
	if (s->flags & F_SIZE && s->st_size != p->fts_statp->st_size) {
		LABEL;
		printf(flavor == F_FREEBSD9 ?
		    "%ssize expected %ju found %ju\n" : "%ssize (%ju, %ju)\n",
		    tab, (uintmax_t)s->st_size,
		    (uintmax_t)p->fts_statp->st_size);
		tab = "\t";
	}
	/*
	 * XXX
	 * Since utimes(2) only takes a timeval, there's no point in
	 * comparing the low bits of the timespec nanosecond field.  This
	 * will only result in mismatches that we can never fix.
	 *
	 * Doesn't display microsecond differences.
	 */
	if (s->flags & F_TIME) {
		struct timeval tv[2];
		struct stat *ps = p->fts_statp;
		time_t smtime = s->st_mtimespec.tv_sec;

#if defined(BSD4_4) && !defined(HAVE_NBTOOL_CONFIG_H)
		time_t pmtime = ps->st_mtimespec.tv_sec;

		TIMESPEC_TO_TIMEVAL(&tv[0], &s->st_mtimespec);
		TIMESPEC_TO_TIMEVAL(&tv[1], &ps->st_mtimespec);
#else
		time_t pmtime = (time_t)ps->st_mtime;

		tv[0].tv_sec = smtime;
		tv[0].tv_usec = 0;
		tv[1].tv_sec = pmtime;
		tv[1].tv_usec = 0;
#endif

		if (tv[0].tv_sec != tv[1].tv_sec ||
		    tv[0].tv_usec != tv[1].tv_usec) {
			LABEL;
			printf(flavor == F_FREEBSD9 ?
			    "%smodification time expected %.24s found " :
			    "%smodification time (%.24s, ",
			    tab, ctime(&smtime));
			printf("%.24s", ctime(&pmtime));
			if (tflag) {
				tv[1] = tv[0];
				if (utimes(p->fts_accpath, tv))
					printf(", not modified: %s%s\n",
					    strerror(errno),
					    flavor == F_FREEBSD9 ? "" : ")");
				else
					printf(", modified%s\n",
					    flavor == F_FREEBSD9 ? "" : ")");
			} else
				printf("%s\n", flavor == F_FREEBSD9 ? "" : ")");
			tab = "\t";
		}
	}
#if HAVE_STRUCT_STAT_ST_FLAGS
	/*
	 * XXX
	 * since lchflags(2) will reset file times, the utimes() above
	 * may have been useless!  oh well, we'd rather have correct
	 * flags, rather than times?
	 */
        if ((s->flags & F_FLAGS) && ((s->st_flags != p->fts_statp->st_flags)
	    || mflag || iflag)) {
		if (s->st_flags != p->fts_statp->st_flags) {
			char *f_s;
			LABEL;
			f_s = flags_to_string(s->st_flags, "none");
			printf(flavor == F_FREEBSD9 ?
			    "%sflags expected \"%s\" found " :
			    "%sflags (\"%s\" is not ", tab, f_s);
			free(f_s);
			f_s = flags_to_string(p->fts_statp->st_flags, "none");
			printf("\"%s\"", f_s);
			free(f_s);
		}
		if (uflag) {
			if (iflag)
				SETFLAGS(0, CH_MASK);
			else if (mflag)
				CLEARFLAGS(0, SP_FLGS);
			else
				SETFLAGS(0, (~SP_FLGS & CH_MASK));
		} else
			printf("%s\n", flavor == F_FREEBSD9 ? "" : ")");
		tab = "\t";
	}
#endif	/* HAVE_STRUCT_STAT_ST_FLAGS */

	/*
	 * from this point, no more permission checking or whacking
	 * occurs, only checking of stuff like checksums and symlinks.
	 */
 afterpermwhack:
	if (s->flags & F_CKSUM) {
		if ((fd = open(p->fts_accpath, O_RDONLY, 0)) < 0) {
			LABEL;
			printf("%scksum: %s: %s\n",
			    tab, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else if (crc(fd, &val, &len)) {
			close(fd);
			LABEL;
			printf("%scksum: %s: %s\n",
			    tab, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			close(fd);
			if (s->cksum != val) {
				LABEL;
				printf(flavor == F_FREEBSD9 ?
				    "%scksum expected %lu found %lu\n" :
				    "%scksum (%lu, %lu)\n",
				    tab, s->cksum, (unsigned long)val);
			}
			tab = "\t";
		}
	}
#ifndef NO_MD5
	if (s->flags & F_MD5) {
		if ((digestbuf = MD5File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, MD5KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->md5digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ?
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, MD5KEY, s->md5digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#endif	/* ! NO_MD5 */
#ifndef NO_RMD160
	if (s->flags & F_RMD160) {
		if ((digestbuf = RMD160File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, RMD160KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->rmd160digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ?
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, RMD160KEY, s->rmd160digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#endif	/* ! NO_RMD160 */
#ifndef NO_SHA1
	if (s->flags & F_SHA1) {
		if ((digestbuf = SHA1File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, SHA1KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->sha1digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ? 
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, SHA1KEY, s->sha1digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#endif	/* ! NO_SHA1 */
#ifndef NO_SHA2
	if (s->flags & F_SHA256) {
		if ((digestbuf = SHA256_File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, SHA256KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->sha256digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ? 
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, SHA256KEY, s->sha256digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#ifdef SHA384_BLOCK_LENGTH
	if (s->flags & F_SHA384) {
		if ((digestbuf = SHA384_File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, SHA384KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->sha384digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ? 
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, SHA384KEY, s->sha384digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#endif
	if (s->flags & F_SHA512) {
		if ((digestbuf = SHA512_File(p->fts_accpath, NULL)) == NULL) {
			LABEL;
			printf("%s%s: %s: %s\n",
			    tab, SHA512KEY, p->fts_accpath, strerror(errno));
			tab = "\t";
		} else {
			if (strcmp(s->sha512digest, digestbuf)) {
				LABEL;
				printf(flavor == F_FREEBSD9 ? 
				    "%s%s expected %s found %s\n" :
				    "%s%s (0x%s, 0x%s)\n",
				    tab, SHA512KEY, s->sha512digest, digestbuf);
			}
			tab = "\t";
			free(digestbuf);
		}
	}
#endif	/* ! NO_SHA2 */
	if (s->flags & F_SLINK &&
	    strcmp(cp = rlink(p->fts_accpath), s->slink)) {
		LABEL;
		printf(flavor == F_FREEBSD9 ? 
		    "%slink ref expected %s found %s" :
		    "%slink ref (%s, %s", tab, cp, s->slink);
		if (uflag) {
			if ((unlink(p->fts_accpath) == -1) ||
			    (symlink(s->slink, p->fts_accpath) == -1) )
				printf(", not modified: %s%s\n",
				    strerror(errno),
				    flavor == F_FREEBSD9 ? "" : ")");
			else
				printf(", modified%s\n",
				    flavor == F_FREEBSD9 ? "" : ")");
		} else
			printf("%s\n", flavor == F_FREEBSD9 ? "" : ")");
	}
	return (label);
}

const char *
rlink(const char *name)
{
	static char lbuf[MAXPATHLEN];
	int len;

	if ((len = readlink(name, lbuf, sizeof(lbuf) - 1)) == -1)
		mtree_err("%s: %s", name, strerror(errno));
	lbuf[len] = '\0';
	return (lbuf);
}
