/*-
 * Copyright (c) 2007 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: ar.h 3629 2018-09-30 19:26:28Z jkoshy $
 */

#include <libelf.h>

#include "_elftc.h"

/*
 * ar(1) options.
 */
#define AR_A	0x0001		/* position-after */
#define AR_B	0x0002		/* position-before */
#define AR_C	0x0004		/* creating new archive */
#define AR_CC	0x0008		/* do not overwrite when extracting */
#define AR_J	0x0010		/* bzip2 compression */
#define AR_O	0x0020		/* preserve original mtime when extracting */
#define AR_S	0x0040		/* write archive symbol table */
#define AR_SS	0x0080		/* do not write archive symbol table */
#define AR_TR	0x0100		/* only keep first 15 chars for member name */
#define AR_U	0x0200		/* only extract or update newer members.*/
#define AR_V	0x0400		/* verbose mode */
#define AR_Z	0x0800		/* gzip compression */
#define AR_D	0x1000		/* insert dummy mode, mtime, uid and gid */
#define AR_BSD	0x2000		/* use the BSD archive format */

#define DEF_BLKSZ 10240		/* default block size */

/* Special names. */

#define	AR_STRINGTAB_NAME_SVR4	"//"
#define	AR_SYMTAB_NAME_BSD	"__.SYMDEF"
#define	AR_SYMTAB_NAME_SVR4	"/"

/*
 * Convenient wrapper for general libarchive error handling.
 */
#define	AC(CALL) do {					\
	if ((CALL))					\
		bsdar_errc(bsdar, 0, "%s",		\
		    archive_error_string(a));		\
} while (0)

/*
 * The 'ACV' wrapper is used for libarchive APIs that changed from
 * returning 'void' to returning an 'int' in later versions of libarchive.
 */
#if	ARCHIVE_VERSION_NUMBER >= 2000000
#define	ACV(CALL)	AC(CALL)
#else
#define	ACV(CALL)	do {				\
		(CALL);					\
	} while (0)
#endif

/*
 * In-memory representation of archive member(object).
 */
struct ar_obj {
	Elf		 *elf;		/* object file descriptor */
	char		 *name;		/* member name */
	uid_t		  uid;		/* user id */
	gid_t		  gid;		/* group id */
	mode_t		  md;		/* octal file permissions */
	size_t		  size;		/* member size */
	time_t		  mtime;	/* modification time */
	dev_t		  dev;		/* inode's device */
	ino_t		  ino;		/* inode's number */

	TAILQ_ENTRY(ar_obj) objs;
};

/*
 * Structure encapsulates the "global" data for "ar" program.
 */
struct bsdar {
	const char	 *filename;	/* archive name. */
	const char	 *addlib;	/* target of ADDLIB. */
	const char	 *posarg;	/* position arg for modifiers -a, -b. */
	char		  mode;		/* program mode */
	int		  options;	/* command line options */
	FILE		 *output;	/* default output stream */

	const char	 *progname;	/* program name */
	int		  argc;
	char		**argv;

	dev_t		  ar_dev;	/* archive device. */
	ino_t		  ar_ino;	/* archive inode. */

	/*
	 * Fields for the archive string table.
	 */
	char		 *as;		/* buffer for archive string table. */
	size_t		  as_sz;	/* current size of as table. */
	size_t		  as_cap;	/* capacity of as table buffer. */

	/*
	 * Fields for the archive symbol table.
	 */
	uint32_t	  s_cnt;	/* current number of symbols. */
	uint32_t	 *s_so;		/* symbol offset table. */
	size_t		  s_so_cap;	/* capacity of so table buffer. */
	char		 *s_sn;		/* symbol name table */
	size_t		  s_sn_cap;	/* capacity of sn table buffer. */
	size_t		  s_sn_sz;	/* current size of sn table. */
	/* Current member's offset (relative to the end of pseudo members.) */
	off_t		  rela_off;

	TAILQ_HEAD(, ar_obj) v_obj;	/* object(member) list */
};

void	ar_mode_script(struct bsdar *ar);
int	ar_read_archive(struct bsdar *_ar, int _mode);
int	ar_write_archive(struct bsdar *_ar, int _mode);
void	bsdar_errc(struct bsdar *, int _code, const char *fmt, ...);
int	bsdar_is_pseudomember(struct bsdar *_ar, const char *_name);
const char *bsdar_strmode(mode_t m);
void	bsdar_warnc(struct bsdar *, int _code, const char *fmt, ...);
