/*	$NetBSD: mtree.h,v 1.31 2012/10/05 09:17:29 wiz Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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
 *
 *	@(#)mtree.h	8.1 (Berkeley) 6/6/93
 */

#ifndef _MTREE_H_
#define	_MTREE_H_

#define	KEYDEFAULT	(F_GID | F_MODE | F_NLINK | F_SIZE | F_SLINK | \
			F_TIME | F_TYPE | F_UID | F_FLAGS)

#define	MISMATCHEXIT	2

typedef struct _node {
	struct _node	*parent, *child;	/* up, down */
	struct _node	*prev, *next;		/* left, right */
	off_t	st_size;			/* size */
	struct timespec	st_mtimespec;		/* last modification time */
	char	*slink;				/* symbolic link reference */
	uid_t	st_uid;				/* uid */
	gid_t	st_gid;				/* gid */
#define	MBITS	(S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
	mode_t	st_mode;			/* mode */
	dev_t	st_rdev;			/* device type */
	u_long	st_flags;			/* flags */
	nlink_t	st_nlink;			/* link count */
	u_long	cksum;				/* check sum */
	char	*md5digest;			/* MD5 digest */
	char	*rmd160digest;			/* RMD-160 digest */
	char	*sha1digest;			/* SHA1 digest */
	char	*sha256digest;			/* SHA256 digest */
	char	*sha384digest;			/* SHA384 digest */
	char	*sha512digest;			/* SHA512 digest */
	char	*tags;				/* tags, comma delimited,
						 * also with leading and
						 * trailing commas */
	size_t	lineno;				/* line # entry came from */

#define	F_CKSUM		0x00000001		/* cksum(1) check sum */
#define	F_DEV		0x00000002		/* device type */
#define	F_DONE		0x00000004		/* directory done */
#define	F_FLAGS		0x00000008		/* file flags */
#define	F_GID		0x00000010		/* gid */
#define	F_GNAME		0x00000020		/* group name */
#define	F_IGN		0x00000040		/* ignore */
#define	F_MAGIC		0x00000080		/* name has magic chars */
#define	F_MD5		0x00000100		/* MD5 digest */
#define	F_MODE		0x00000200		/* mode */
#define	F_NLINK		0x00000400		/* number of links */
#define	F_OPT		0x00000800		/* existence optional */
#define	F_RMD160	0x00001000		/* RMD-160 digest */
#define	F_SHA1		0x00002000		/* SHA1 digest */
#define	F_SIZE		0x00004000		/* size */
#define	F_SLINK		0x00008000		/* symbolic link */
#define	F_TAGS		0x00010000		/* tags */
#define	F_TIME		0x00020000		/* modification time */
#define	F_TYPE		0x00040000		/* file type */
#define	F_UID		0x00080000		/* uid */
#define	F_UNAME		0x00100000		/* user name */
#define	F_VISIT		0x00200000		/* file visited */
#define	F_NOCHANGE	0x00400000		/* check existence, but not */
						/* other properties */
#define	F_SHA256	0x00800000		/* SHA256 digest */
#define	F_SHA384	0x01000000		/* SHA384 digest */
#define	F_SHA512	0x02000000		/* SHA512 digest */

	int	flags;				/* items set */

#define	F_BLOCK	0x001				/* block special */
#define	F_CHAR	0x002				/* char special */
#define	F_DIR	0x004				/* directory */
#define	F_FIFO	0x008				/* fifo */
#define	F_FILE	0x010				/* regular file */
#define	F_LINK	0x020				/* symbolic link */
#define	F_SOCK	0x040				/* socket */
#define	F_DOOR	0x080				/* door */
	int	type;				/* file type */

	char	name[1];			/* file name (must be last) */
} NODE;


typedef struct {
	char  **list;
	int	count;
} slist_t;


/*
 * prototypes for functions published to other programs which want to use
 * the specfile parser but don't want to pull in all of "extern.h"
 */
const char	*inotype(u_int);
u_int		 nodetoino(u_int);
int		 setup_getid(const char *);
NODE		*spec(FILE *);
int		 mtree_specspec(FILE *, FILE *);
void		 free_nodes(NODE *);
char		*vispath(const char *);

#ifdef __FreeBSD__
#define KEY_DIGEST "digest"
#else
#define KEY_DIGEST
#endif

#define	MD5KEY		"md5"		KEY_DIGEST
#ifdef __FreeBSD__
#define	RMD160KEY	"ripemd160"	KEY_DIGEST
#else
#define	RMD160KEY	"rmd160"	KEY_DIGEST
#endif
#define	SHA1KEY		"sha1"		KEY_DIGEST
#define	SHA256KEY	"sha256"	KEY_DIGEST
#define	SHA384KEY	"sha384"
#define	SHA512KEY	"sha512"

#define	RP(p)	\
	((p)->fts_path[0] == '.' && (p)->fts_path[1] == '/' ? \
	    (p)->fts_path + 2 : (p)->fts_path)

#define	UF_MASK ((UF_NODUMP | UF_IMMUTABLE |   \
                  UF_APPEND | UF_OPAQUE)       \
                    & UF_SETTABLE)              /* user settable flags */
#define	SF_MASK ((SF_ARCHIVED | SF_IMMUTABLE | \
                  SF_APPEND) & SF_SETTABLE)     /* root settable flags */
#define	CH_MASK  (UF_MASK | SF_MASK)            /* all settable flags */
#define	SP_FLGS  (SF_IMMUTABLE | SF_APPEND)     /* special flags */

#endif /* _MTREE_H_ */
