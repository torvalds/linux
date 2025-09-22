/*	$OpenBSD: options.c,v 1.115 2024/05/10 20:28:31 millert Exp $	*/
/*	$NetBSD: options.c,v 1.6 1996/03/26 23:54:18 mrg Exp $	*/

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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pax.h"
#include "cpio.h"
#include "tar.h"
#include "extern.h"

static int bad_opt(void);
static int opt_add(const char *);
/*
 * argv[0] names. Used for tar and cpio emulation
 */

#define NM_TAR  "tar"
#define NM_CPIO "cpio"
#define NM_PAX  "pax"

/*
 * Constants used to specify the legal sets of flags in pax. For each major
 * operation mode of pax, a set of illegal flags is defined. If any one of
 * those illegal flags are found set, we scream and exit
 */

/*
 * flags (one for each option).
 */
#define	AF	0x00000001
#define	BF	0x00000002
#define	CF	0x00000004
#define	DF	0x00000008
#define	FF	0x00000010
#define	IF	0x00000020
#define	KF	0x00000040
#define	LF	0x00000080
#define	NF	0x00000100
#define	OF	0x00000200
#define	PF	0x00000400
#define	RF	0x00000800
#define	SF	0x00001000
#define	TF	0x00002000
#define	UF	0x00004000
#define	VF	0x00008000
#define	WF	0x00010000
#define	XF	0x00020000
#define	CBF	0x00040000	/* nonstandard extension */
#define	CDF	0x00080000	/* nonstandard extension */
#define	CEF	0x00100000	/* nonstandard extension */
#define	CGF	0x00200000	/* nonstandard extension */
#define	CHF	0x00400000	/* nonstandard extension */
#define	CLF	0x00800000	/* nonstandard extension */
#define	CPF	0x01000000	/* nonstandard extension */
#define	CTF	0x02000000	/* nonstandard extension */
#define	CUF	0x04000000	/* nonstandard extension */
#define	CXF	0x08000000
#define	CYF	0x10000000	/* nonstandard extension */
#define	CZF	0x20000000	/* nonstandard extension */
#define	C0F	0x40000000	/* nonstandard extension */

/*
 * ascii string indexed by bit position above (alter the above and you must
 * alter this string) used to tell the user what flags caused us to complain
 */
#define FLGCH	"abcdfiklnoprstuvwxBDEGHLPTUXYZ0"

/*
 * legal pax operation bit patterns
 */

#define ISLIST(x)	(((x) & (RF|WF)) == 0)
#define	ISEXTRACT(x)	(((x) & (RF|WF)) == RF)
#define ISARCHIVE(x)	(((x) & (AF|RF|WF)) == WF)
#define ISAPPND(x)	(((x) & (AF|RF|WF)) == (AF|WF))
#define	ISCOPY(x)	(((x) & (RF|WF)) == (RF|WF))
#define	ISWRITE(x)	(((x) & (RF|WF)) == WF)

/*
 * Illegal option flag subsets based on pax operation
 */

#define	BDEXTR	(AF|BF|LF|TF|WF|XF|CBF|CHF|CLF|CPF|CXF)
#define	BDARCH	(CF|KF|LF|NF|PF|RF|CDF|CEF|CYF|CZF)
#define	BDCOPY	(AF|BF|FF|OF|XF|CBF|CEF)
#define	BDLIST (AF|BF|IF|KF|LF|OF|PF|RF|TF|UF|WF|XF|CBF|CDF|CHF|CLF|CPF|CXF|CYF|CZF)


/*
 * Routines which handle command line options
 */

static char flgch[] = FLGCH;	/* list of all possible flags */
static OPLIST *ophead = NULL;	/* head for format specific options -x */
static OPLIST *optail = NULL;	/* option tail */

static int no_op(void);
static void printflg(unsigned int);
static off_t str_offt(char *);
static char *get_line(FILE *fp);
static void pax_options(int, char **);
static void pax_usage(void);
static void tar_options(int, char **);
static void tar_usage(void);
#ifndef NOCPIO
static void cpio_options(int, char **);
static void cpio_usage(void);
#endif

static int compress_id(char *_blk, int _size);
static int gzip_id(char *_blk, int _size);
static int bzip2_id(char *_blk, int _size);
static int xz_id(char *_blk, int _size);

#define GZIP_CMD	"gzip"		/* command to run as gzip */
#define COMPRESS_CMD	"compress"	/* command to run as compress */
#define BZIP2_CMD	"bzip2"		/* command to run as bzip2 */

/*
 *	Format specific routine table
 *	(see pax.h for description of each function)
 *
 *	name, blksz, hdsz, udev, hlk, blkagn, inhead, id, st_read,
 *	read, end_read, st_write, write, end_write, trail,
 *	rd_data, wr_data, options
 */

FSUB fsub[] = {
#ifdef NOCPIO
/* 0: OLD BINARY CPIO */
	{ },
/* 1: OLD OCTAL CHARACTER CPIO */
	{ },
/* 2: SVR4 HEX CPIO */
	{ },
/* 3: SVR4 HEX CPIO WITH CRC */
	{ },
#else
/* 0: OLD BINARY CPIO */
	{"bcpio", 5120, sizeof(HD_BCPIO), 1, 0, 0, 1, bcpio_id, cpio_strd,
	bcpio_rd, bcpio_endrd, cpio_stwr, bcpio_wr, cpio_endwr, cpio_trail,
	bad_opt},

/* 1: OLD OCTAL CHARACTER CPIO */
	{"cpio", 5120, sizeof(HD_CPIO), 1, 0, 0, 1, cpio_id, cpio_strd,
	cpio_rd, cpio_endrd, cpio_stwr, cpio_wr, cpio_endwr, cpio_trail,
	bad_opt},

/* 2: SVR4 HEX CPIO */
	{"sv4cpio", 5120, sizeof(HD_VCPIO), 1, 0, 0, 1, vcpio_id, cpio_strd,
	vcpio_rd, vcpio_endrd, cpio_stwr, vcpio_wr, cpio_endwr, cpio_trail,
	bad_opt},

/* 3: SVR4 HEX CPIO WITH CRC */
	{"sv4crc", 5120, sizeof(HD_VCPIO), 1, 0, 0, 1, crc_id, crc_strd,
	vcpio_rd, vcpio_endrd, crc_stwr, vcpio_wr, cpio_endwr, cpio_trail,
	bad_opt},
#endif
/* 4: OLD TAR */
	{"tar", 10240, BLKMULT, 0, 1, BLKMULT, 0, tar_id, no_op,
	tar_rd, tar_endrd, no_op, tar_wr, tar_endwr, tar_trail,
	tar_opt},

/* 5: POSIX USTAR */
	{"ustar", 10240, BLKMULT, 0, 1, BLKMULT, 0, ustar_id, no_op,
	ustar_rd, tar_endrd, no_op, ustar_wr, tar_endwr, tar_trail,
	tar_opt},

#ifdef SMALL
/* 6: compress, to detect failure to use -Z */
	{ },
/* 7: xz, to detect failure to decompress it */
	{ },
/* 8: bzip2, to detect failure to use -j */
	{ },
/* 9: gzip, to detect failure to use -z */
	{ },
/* 10: POSIX PAX */
	{ },
#else
/* 6: compress, to detect failure to use -Z */
	{NULL, 0, 4, 0, 0, 0, 0, compress_id},
/* 7: xz, to detect failure to decompress it */
	{NULL, 0, 4, 0, 0, 0, 0, xz_id},
/* 8: bzip2, to detect failure to use -j */
	{NULL, 0, 4, 0, 0, 0, 0, bzip2_id},
/* 9: gzip, to detect failure to use -z */
	{NULL, 0, 4, 0, 0, 0, 0, gzip_id},
/* 10: POSIX PAX */
	{"pax", 5120, BLKMULT, 0, 1, BLKMULT, 0, pax_id, no_op,
	ustar_rd, tar_endrd, no_op, pax_wr, tar_endwr, tar_trail,
	pax_opt},
#endif
};
#define	F_OCPIO	0	/* format when called as cpio -6 */
#define	F_ACPIO	1	/* format when called as cpio -c */
#define	F_CPIO	3	/* format when called as cpio */
#define F_OTAR	4	/* format when called as tar -o */
#ifdef SMALL
# define F_TAR	5	/* default write format when called as tar: ustar */
# define DEFLT	5	/* default write format when called as pax: ustar */
#else
# define F_TAR	10	/* default write format when called as tar: pax */
# define DEFLT	10	/* default write format when called as pax: pax */
#endif

/*
 * ford is the archive search order used by get_arc() to determine what kind
 * of archive we are dealing with. This helps to properly id archive formats
 * some formats may be subsets of others....
 */
int ford[] = {10, 5, 4, 9, 8, 7, 6, 3, 2, 1, 0, -1};

/*
 * Do we have -C anywhere and what is it?
 */
int havechd = 0;
char *chdname = NULL;

/*
 * options()
 *	figure out if we are pax, tar or cpio. Call the appropriate options
 *	parser
 */

void
options(int argc, char **argv)
{
	extern char *__progname;

	/*
	 * Are we acting like pax, tar or cpio (based on argv[0])
	 */
	argv0 = __progname;

	if (strcmp(NM_TAR, argv0) == 0) {
		op_mode = OP_TAR;
		tar_options(argc, argv);
#ifndef NOCPIO
	} else if (strcmp(NM_CPIO, argv0) == 0) {
		op_mode = OP_CPIO;
		cpio_options(argc, argv);
#endif /* !NOCPIO */
	} else {
		argv0 = NM_PAX;
		op_mode = OP_PAX;
		pax_options(argc, argv);
	}

	/* Line-buffer the file list output as needed. */
	if (listf != stderr)
		setvbuf(listf, NULL, _IOLBF, 0);
}

/*
 * pax_options()
 *	look at the user specified flags. set globals as required and check if
 *	the user specified a legal set of flags. If not, complain and exit
 */

static void
pax_options(int argc, char **argv)
{
	int c;
	unsigned i;
	unsigned int flg = 0;
	unsigned int bflg = 0;
	const char *errstr;
	char *pt;

	/*
	 * process option flags
	 */
	while ((c=getopt(argc,argv,"ab:cdf:ijklno:p:rs:tuvwx:zB:DE:G:HLOPT:U:XYZ0"))
	    != -1) {
		switch (c) {
		case 'a':
			/*
			 * append
			 */
			flg |= AF;
			break;
		case 'b':
			/*
			 * specify blocksize
			 */
			flg |= BF;
			if ((wrblksz = (int)str_offt(optarg)) <= 0) {
				paxwarn(1, "Invalid block size %s", optarg);
				pax_usage();
			}
			break;
		case 'c':
			/*
			 * inverse match on patterns
			 */
			cflag = 1;
			flg |= CF;
			break;
		case 'd':
			/*
			 * match only dir on extract, not the subtree at dir
			 */
			dflag = 1;
			flg |= DF;
			break;
		case 'f':
			/*
			 * filename where the archive is stored
			 */
			arcname = optarg;
			flg |= FF;
			break;
		case 'i':
			/*
			 * interactive file rename
			 */
			iflag = 1;
			flg |= IF;
			break;
		case 'j':
			/*
			 * use bzip2.  Non standard option.
			 */
			gzip_program = BZIP2_CMD;
			break;
		case 'k':
			/*
			 * do not clobber files that exist
			 */
			kflag = 1;
			flg |= KF;
			break;
		case 'l':
			/*
			 * try to link src to dest with copy (-rw)
			 */
			lflag = 1;
			flg |= LF;
			break;
		case 'n':
			/*
			 * select first match for a pattern only
			 */
			nflag = 1;
			flg |= NF;
			break;
		case 'o':
			/*
			 * pass format specific options
			 */
			flg |= OF;
			if (opt_add(optarg) < 0)
				pax_usage();
			break;
		case 'p':
			/*
			 * specify file characteristic options
			 */
			for (pt = optarg; *pt != '\0'; ++pt) {
				switch (*pt) {
				case 'a':
					/*
					 * do not preserve access time
					 */
					patime = 0;
					break;
				case 'e':
					/*
					 * preserve user id, group id, file
					 * mode, access/modification times
					 */
					pids = 1;
					pmode = 1;
					patime = 1;
					pmtime = 1;
					break;
				case 'm':
					/*
					 * do not preserve modification time
					 */
					pmtime = 0;
					break;
				case 'o':
					/*
					 * preserve uid/gid
					 */
					pids = 1;
					break;
				case 'p':
					/*
					 * preserve file mode bits
					 */
					pmode = 1;
					break;
				default:
					paxwarn(1, "Invalid -p string: %c", *pt);
					pax_usage();
					break;
				}
			}
			flg |= PF;
			break;
		case 'r':
			/*
			 * read the archive
			 */
			flg |= RF;
			break;
		case 's':
			/*
			 * file name substitution name pattern
			 */
			if (rep_add(optarg) < 0) {
				pax_usage();
				break;
			}
			flg |= SF;
			break;
		case 't':
			/*
			 * preserve access time on filesystem nodes we read
			 */
			tflag = 1;
			flg |= TF;
			break;
		case 'u':
			/*
			 * ignore those older files
			 */
			uflag = 1;
			flg |= UF;
			break;
		case 'v':
			/*
			 * verbose operation mode
			 */
			vflag = 1;
			flg |= VF;
			break;
		case 'w':
			/*
			 * write an archive
			 */
			flg |= WF;
			break;
		case 'x':
			/*
			 * specify an archive format on write
			 */
			for (i = 0; i < sizeof(fsub)/sizeof(FSUB); ++i)
				if (fsub[i].name != NULL &&
				    strcmp(fsub[i].name, optarg) == 0)
					break;
			if (i < sizeof(fsub)/sizeof(FSUB)) {
				frmt = &fsub[i];
				flg |= XF;
				break;
			}
			paxwarn(1, "Unknown -x format: %s", optarg);
			(void)fputs("pax: Known -x formats are:", stderr);
			for (i = 0; i < (sizeof(fsub)/sizeof(FSUB)); ++i)
				if (fsub[i].name != NULL)
					(void)fprintf(stderr, " %s",
					    fsub[i].name);
			(void)fputs("\n\n", stderr);
			pax_usage();
			break;
		case 'z':
			/*
			 * use gzip.  Non standard option.
			 */
			gzip_program = GZIP_CMD;
			break;
		case 'B':
			/*
			 * non-standard option on number of bytes written on a
			 * single archive volume.
			 */
			if ((wrlimit = str_offt(optarg)) <= 0) {
				paxwarn(1, "Invalid write limit %s", optarg);
				pax_usage();
			}
			if (wrlimit % BLKMULT) {
				paxwarn(1, "Write limit is not a %d byte multiple",
				    BLKMULT);
				pax_usage();
			}
			flg |= CBF;
			break;
		case 'D':
			/*
			 * On extraction check file inode change time before the
			 * modification of the file name. Non standard option.
			 */
			Dflag = 1;
			flg |= CDF;
			break;
		case 'E':
			/*
			 * non-standard limit on read faults
			 * 0 indicates stop after first error, values
			 * indicate a limit
			 */
			flg |= CEF;
			maxflt = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr) {
				paxwarn(1, "Error count value: %s", errstr);
				pax_usage();
			}
			break;
		case 'G':
			/*
			 * non-standard option for selecting files within an
			 * archive by group (gid or name)
			 */
			if (grp_add(optarg) < 0) {
				pax_usage();
				break;
			}
			flg |= CGF;
			break;
		case 'H':
			/*
			 * follow command line symlinks only
			 */
			Hflag = 1;
			flg |= CHF;
			break;
		case 'L':
			/*
			 * follow symlinks
			 */
			Lflag = 1;
			flg |= CLF;
			break;
		case 'O':
			/*
			 * Force one volume.  Non standard option.
			 */
			force_one_volume = 1;
			break;
		case 'P':
			/*
			 * do NOT follow symlinks (default)
			 */
			Lflag = 0;
			flg |= CPF;
			break;
		case 'T':
			/*
			 * non-standard option for selecting files within an
			 * archive by modification time range (lower,upper)
			 */
			if (trng_add(optarg) < 0) {
				pax_usage();
				break;
			}
			flg |= CTF;
			break;
		case 'U':
			/*
			 * non-standard option for selecting files within an
			 * archive by user (uid or name)
			 */
			if (usr_add(optarg) < 0) {
				pax_usage();
				break;
			}
			flg |= CUF;
			break;
		case 'X':
			/*
			 * do not pass over mount points in the file system
			 */
			Xflag = 1;
			flg |= CXF;
			break;
		case 'Y':
			/*
			 * On extraction check file inode change time after the
			 * modification of the file name. Non standard option.
			 */
			Yflag = 1;
			flg |= CYF;
			break;
		case 'Z':
			/*
			 * On extraction check modification time after the
			 * modification of the file name. Non standard option.
			 */
			Zflag = 1;
			flg |= CZF;
			break;
		case '0':
			/*
			 * Use \0 as pathname terminator.
			 * (For use with the -print0 option of find(1).)
			 */
			zeroflag = 1;
			flg |= C0F;
			break;
		default:
			pax_usage();
			break;
		}
	}

	/*
	 * figure out the operation mode of pax read,write,extract,copy,append
	 * or list. check that we have not been given a bogus set of flags
	 * for the operation mode.
	 */
	if (ISLIST(flg)) {
		act = LIST;
		listf = stdout;
		bflg = flg & BDLIST;
	} else if (ISEXTRACT(flg)) {
		act = EXTRACT;
		bflg = flg & BDEXTR;
	} else if (ISARCHIVE(flg)) {
		act = ARCHIVE;
		bflg = flg & BDARCH;
	} else if (ISAPPND(flg)) {
		act = APPND;
		bflg = flg & BDARCH;
	} else if (ISCOPY(flg)) {
		act = COPY;
		bflg = flg & BDCOPY;
	} else
		pax_usage();
	if (bflg) {
		printflg(flg);
		pax_usage();
	}

	/*
	 * if we are writing (ARCHIVE) we use the default format if the user
	 * did not specify a format. when we write during an APPEND, we will
	 * adopt the format of the existing archive if none was supplied.
	 */
	if (!(flg & XF) && (act == ARCHIVE))
		frmt = &(fsub[DEFLT]);

	/*
	 * process the args as they are interpreted by the operation mode
	 */
	switch (act) {
	case LIST:
	case EXTRACT:
		for (; optind < argc; optind++)
			if (pat_add(argv[optind], NULL) < 0)
				pax_usage();
		break;
	case COPY:
		if (optind >= argc) {
			paxwarn(0, "Destination directory was not supplied");
			pax_usage();
		}
		--argc;
		dirptr = argv[argc];
		/* FALL THROUGH */
	case ARCHIVE:
	case APPND:
		for (; optind < argc; optind++)
			if (ftree_add(argv[optind], 0) < 0)
				pax_usage();
		/*
		 * no read errors allowed on updates/append operation!
		 */
		maxflt = 0;
		break;
	}
}


/*
 * tar_options()
 *	look at the user specified flags. set globals as required and check if
 *	the user specified a legal set of flags. If not, complain and exit
 */

static void
tar_options(int argc, char **argv)
{
	int c;
	int nincfiles = 0;
	int incfiles_max = 0;
	unsigned int i;
	unsigned int format = F_TAR;
	struct incfile {
		char *file;
		char *dir;
	};
	struct incfile *incfiles = NULL;

	/*
	 * Set default values.
	 */
	rmleadslash = 1;

	/*
	 * process option flags
	 */
	while ((c = getoldopt(argc, argv,
	    "b:cef:hjmopqruts:vwxzBC:F:HI:LNOPXZ014578")) != -1) {
		switch (c) {
		case 'b':
			/*
			 * specify blocksize in 512-byte blocks
			 */
			if ((wrblksz = (int)str_offt(optarg)) <= 0) {
				paxwarn(1, "Invalid block size %s", optarg);
				tar_usage();
			}
			wrblksz *= 512;		/* XXX - check for int oflow */
			break;
		case 'c':
			/*
			 * create an archive
			 */
			act = ARCHIVE;
			break;
		case 'e':
			/*
			 * stop after first error
			 */
			maxflt = 0;
			break;
		case 'f':
			/*
			 * filename where the archive is stored
			 */
			arcname = optarg;
			break;
		case 'h':
			/*
			 * follow symlinks
			 */
			Lflag = 1;
			break;
		case 'j':
			/*
			 * use bzip2.  Non standard option.
			 */
			gzip_program = BZIP2_CMD;
			break;
		case 'm':
			/*
			 * do not preserve modification time
			 */
			pmtime = 0;
			break;
		case 'O':
			format = F_OTAR;
			break;
		case 'o':
			format = F_OTAR;
			tar_nodir = 1;
			break;
		case 'p':
			/*
			 * preserve uid/gid and file mode, regardless of umask
			 */
			pmode = 1;
			pids = 1;
			break;
		case 'q':
			/*
			 * select first match for a pattern only
			 */
			nflag = 1;
			break;
		case 'r':
		case 'u':
			/*
			 * append to the archive
			 */
			act = APPND;
			break;
		case 's':
			/*
			 * file name substitution name pattern
			 */
			if (rep_add(optarg) < 0) {
				tar_usage();
				break;
			}
			break;
		case 't':
			/*
			 * list contents of the tape
			 */
			act = LIST;
			break;
		case 'v':
			/*
			 * verbose operation mode
			 */
			vflag++;
			break;
		case 'w':
			/*
			 * interactive file rename
			 */
			iflag = 1;
			break;
		case 'x':
			/*
			 * extract an archive, preserving mode,
			 * and mtime if possible.
			 */
			act = EXTRACT;
			pmtime = 1;
			break;
		case 'z':
			/*
			 * use gzip.  Non standard option.
			 */
			gzip_program = GZIP_CMD;
			break;
		case 'B':
			/*
			 * Nothing to do here, this is pax default
			 */
			break;
		case 'C':
			havechd++;
			chdname = optarg;
			break;
		case 'F':
			for (i = 0; i < sizeof(fsub)/sizeof(FSUB); ++i)
				if (fsub[i].name != NULL &&
				    strcmp(fsub[i].name, optarg) == 0)
					break;
			if (i < sizeof(fsub)/sizeof(FSUB)) {
				format = i;
				break;
			}
			paxwarn(1, "Unknown -F format: %s", optarg);
			(void)fputs("tar: Known -F formats are:", stderr);
			for (i = 0; i < (sizeof(fsub)/sizeof(FSUB)); ++i)
				if (fsub[i].name != NULL)
					(void)fprintf(stderr, " %s",
					    fsub[i].name);
			(void)fputs("\n\n", stderr);
			tar_usage();
			break;
		case 'H':
			/*
			 * follow command line symlinks only
			 */
			Hflag = 1;
			break;
		case 'I':
			if (++nincfiles > incfiles_max) {
				size_t n = nincfiles + 3;
				struct incfile *p;

				p = reallocarray(incfiles, n,
				    sizeof(*incfiles));
				if (p == NULL) {
					paxwarn(0, "Unable to allocate space "
					    "for option list");
					exit(1);
				}
				incfiles = p;
				incfiles_max = n;
			}
			incfiles[nincfiles - 1].file = optarg;
			incfiles[nincfiles - 1].dir = chdname;
			break;
		case 'L':
			/*
			 * follow symlinks
			 */
			Lflag = 1;
			break;
		case 'N':
			/* numeric uid and gid only */
			Nflag = 1;
			break;
		case 'P':
			/*
			 * do not remove leading '/' from pathnames
			 */
			rmleadslash = 0;
			break;
		case 'X':
			/*
			 * do not pass over mount points in the file system
			 */
			Xflag = 1;
			break;
		case 'Z':
			/*
			 * use compress.
			 */
			gzip_program = COMPRESS_CMD;
			break;
		case '0':
			arcname = DEV_0;
			break;
		case '1':
			arcname = DEV_1;
			break;
		case '4':
			arcname = DEV_4;
			break;
		case '5':
			arcname = DEV_5;
			break;
		case '7':
			arcname = DEV_7;
			break;
		case '8':
			arcname = DEV_8;
			break;
		default:
			tar_usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if ((arcname == NULL) || (*arcname == '\0')) {
		arcname = getenv("TAPE");
		if ((arcname == NULL) || (*arcname == '\0'))
			arcname = _PATH_DEFTAPE;
	}
	if ((arcname[0] == '-') && (arcname[1]== '\0'))
		arcname = NULL;

	/*
	 * Traditional tar behaviour: list-like output goes to stdout unless
	 * writing the archive there.  (pax uses stderr unless in list mode)
	 */
        if (act == LIST || act == EXTRACT || arcname != NULL)
		listf = stdout;

	/* Traditional tar behaviour (pax wants to read file list from stdin) */
	if ((act == ARCHIVE || act == APPND) && argc == 0 && nincfiles == 0)
		exit(0);

	/*
	 * process the args as they are interpreted by the operation mode
	 */
	switch (act) {
	case LIST:
	case EXTRACT:
	default:
		{
			int sawpat = 0;
			char *file, *dir;

			while (nincfiles || *argv != NULL) {
				/*
				 * If we queued up any include files,
				 * pull them in now.  Otherwise, check
				 * for -I and -C positional flags.
				 * Anything else must be a file to
				 * extract.
				 */
				if (nincfiles) {
					file = incfiles->file;
					dir = incfiles->dir;
					incfiles++;
					nincfiles--;
				} else if (strcmp(*argv, "-I") == 0) {
					if (*++argv == NULL)
						break;
					file = *argv++;
					dir = chdname;
				} else
					file = NULL;
				if (file != NULL) {
					FILE *fp;
					char *str;

					if (strcmp(file, "-") == 0)
						fp = stdin;
					else if ((fp = fopen(file, "r")) == NULL) {
						syswarn(1, errno,
						    "Unable to open %s", file);
						tar_usage();
					}
					while ((str = get_line(fp)) != NULL) {
						if (pat_add(str, dir) < 0)
							tar_usage();
						sawpat = 1;
					}
					if (ferror(fp)) {
						syswarn(1, errno,
						    "Unable to read from %s",
						    strcmp(file, "-") ? file :
						    "stdin");
						tar_usage();
					}
					if (strcmp(file, "-") != 0)
						fclose(fp);
				} else if (strcmp(*argv, "-C") == 0) {
					if (*++argv == NULL)
						break;
					chdname = *argv++;
					havechd++;
				} else if (pat_add(*argv++, chdname) < 0)
					tar_usage();
				else
					sawpat = 1;
			}
			/*
			 * if patterns were added, we are doing	chdir()
			 * on a file-by-file basis, else, just one
			 * global chdir (if any) after opening input.
			 */
			if (sawpat > 0)
				chdname = NULL;
		}
		break;
	case ARCHIVE:
	case APPND:
		frmt = &fsub[format];

		if (chdname != NULL) {	/* initial chdir() */
			if (ftree_add(chdname, 1) < 0)
				tar_usage();
		}

		while (nincfiles || *argv != NULL) {
			char *file, *dir;

			/*
			 * If we queued up any include files, pull them in
			 * now.  Otherwise, check for -I and -C positional
			 * flags.  Anything else must be a file to include
			 * in the archive.
			 */
			if (nincfiles) {
				file = incfiles->file;
				dir = incfiles->dir;
				incfiles++;
				nincfiles--;
			} else if (strcmp(*argv, "-I") == 0) {
				if (*++argv == NULL)
					break;
				file = *argv++;
				dir = NULL;
			} else
				file = NULL;
			if (file != NULL) {
				FILE *fp;
				char *str;

				/* Set directory if needed */
				if (dir) {
					if (ftree_add(dir, 1) < 0)
						tar_usage();
				}

				if (strcmp(file, "-") == 0)
					fp = stdin;
				else if ((fp = fopen(file, "r")) == NULL) {
					syswarn(1, errno, "Unable to open %s",
					    file);
					tar_usage();
				}
				while ((str = get_line(fp)) != NULL) {
					if (ftree_add(str, 0) < 0)
						tar_usage();
				}
				if (ferror(fp)) {
					syswarn(1, errno,
					    "Unable to read from %s",
					    strcmp(file, "-") ? file : "stdin");
					tar_usage();
				}
				if (strcmp(file, "-") != 0)
					fclose(fp);
			} else if (strcmp(*argv, "-C") == 0) {
				if (*++argv == NULL)
					break;
				if (ftree_add(*argv++, 1) < 0)
					tar_usage();
				havechd++;
			} else if (ftree_add(*argv++, 0) < 0)
				tar_usage();
		}
		/*
		 * no read errors allowed on updates/append operation!
		 */
		maxflt = 0;
		break;
	}
}

static int mkpath(char *);

static int
mkpath(char *path)
{
	struct stat sb;
	char *slash;
	int done = 0;

	slash = path;

	while (!done) {
		slash += strspn(slash, "/");
		slash += strcspn(slash, "/");

		done = (*slash == '\0');
		*slash = '\0';

		if (stat(path, &sb)) {
			if (errno != ENOENT || mkdir(path, 0777)) {
				paxwarn(1, "%s", path);
				return (-1);
			}
		} else if (!S_ISDIR(sb.st_mode)) {
			syswarn(1, ENOTDIR, "%s", path);
			return (-1);
		}

		if (!done)
			*slash = '/';
	}

	return (0);
}

#ifndef NOCPIO
/*
 * cpio_options()
 *	look at the user specified flags. set globals as required and check if
 *	the user specified a legal set of flags. If not, complain and exit
 */

static void
cpio_options(int argc, char **argv)
{
	const char *errstr;
	int c, list_only = 0;
	unsigned i;
	char *str;
	FILE *fp;

	kflag = 1;
	pids = 1;
	pmode = 1;
	pmtime = 0;
	arcname = NULL;
	dflag = 1;
	act = -1;
	nodirs = 1;
	while ((c=getopt(argc,argv,"abcdfijklmoprstuvzABC:E:F:H:I:LO:SZ6")) != -1)
		switch (c) {
			case 'a':
				/*
				 * preserve access time on files read
				 */
				tflag = 1;
				break;
			case 'b':
				/*
				 * swap bytes and half-words when reading data
				 */
				break;
			case 'c':
				/*
				 * ASCII cpio header
				 */
				frmt = &(fsub[F_ACPIO]);
				break;
			case 'd':
				/*
				 * create directories as needed
				 */
				nodirs = 0;
				break;
			case 'f':
				/*
				 * invert meaning of pattern list
				 */
				cflag = 1;
				break;
			case 'i':
				/*
				 * restore an archive
				 */
				act = EXTRACT;
				break;
			case 'j':
				/*
				 * use bzip2.  Non standard option.
				 */
				gzip_program = BZIP2_CMD;
				break;
			case 'k':
				break;
			case 'l':
				/*
				 * use links instead of copies when possible
				 */
				lflag = 1;
				break;
			case 'm':
				/*
				 * preserve modification time
				 */
				pmtime = 1;
				break;
			case 'o':
				/*
				 * create an archive
				 */
				act = ARCHIVE;
				if (frmt == NULL)
					frmt = &(fsub[F_CPIO]);
				break;
			case 'p':
				/*
				 * copy-pass mode
				 */
				act = COPY;
				break;
			case 'r':
				/*
				 * interactively rename files
				 */
				iflag = 1;
				break;
			case 's':
				/*
				 * swap bytes after reading data
				 */
				break;
			case 't':
				/*
				 * list contents of archive
				 */
				list_only = 1;
				break;
			case 'u':
				/*
				 * replace newer files
				 */
				kflag = 0;
				break;
			case 'v':
				/*
				 * verbose operation mode
				 */
				vflag = 1;
				break;
			case 'z':
				/*
				 * use gzip.  Non standard option.
				 */
				gzip_program = GZIP_CMD;
				break;
			case 'A':
				/*
				 * append mode
				 */
				act = APPND;
				break;
			case 'B':
				/*
				 * Use 5120 byte block size
				 */
				wrblksz = 5120;
				break;
			case 'C':
				/*
				 * set block size in bytes
				 */
				wrblksz = strtonum(optarg, 0, INT_MAX, &errstr);
				if (errstr) {
					paxwarn(1, "Invalid block size %s: %s",
					    optarg, errstr);
					pax_usage();
				}
				break;
			case 'E':
				/*
				 * file with patterns to extract or list
				 */
				if ((fp = fopen(optarg, "r")) == NULL) {
					syswarn(1, errno, "Unable to open %s",
					    optarg);
					cpio_usage();
				}
				while ((str = get_line(fp)) != NULL) {
					pat_add(str, NULL);
				}
				if (ferror(fp)) {
					syswarn(1, errno,
					    "Unable to read from %s", optarg);
					cpio_usage();
				}
				fclose(fp);
				break;
			case 'F':
			case 'I':
			case 'O':
				/*
				 * filename where the archive is stored
				 */
				if ((optarg[0] == '-') && (optarg[1]== '\0')) {
					/*
					 * treat a - as stdin
					 */
					arcname = NULL;
					break;
				}
				arcname = optarg;
				break;
			case 'H':
				/*
				 * specify an archive format on write
				 */
				for (i = 0; i < sizeof(fsub)/sizeof(FSUB); ++i)
					if (fsub[i].name != NULL &&
					    strcmp(fsub[i].name, optarg) == 0)
						break;
				if (i < sizeof(fsub)/sizeof(FSUB)) {
					frmt = &fsub[i];
					break;
				}
				paxwarn(1, "Unknown -H format: %s", optarg);
				(void)fputs("cpio: Known -H formats are:", stderr);
				for (i = 0; i < (sizeof(fsub)/sizeof(FSUB)); ++i)
					if (fsub[i].name != NULL)
						(void)fprintf(stderr, " %s",
						    fsub[i].name);
				(void)fputs("\n\n", stderr);
				cpio_usage();
				break;
			case 'L':
				/*
				 * follow symbolic links
				 */
				Lflag = 1;
				break;
			case 'S':
				/*
				 * swap halfwords after reading data
				 */
				break;
			case 'Z':
				/*
				 * use compress.  Non standard option.
				 */
				gzip_program = COMPRESS_CMD;
				break;
			case '6':
				/*
				 * process Version 6 cpio format
				 */
				frmt = &(fsub[F_OCPIO]);
				break;
			default:
				cpio_usage();
				break;
		}
	argc -= optind;
	argv += optind;

	/*
	 * process the args as they are interpreted by the operation mode
	 */
	switch (act) {
		case EXTRACT:
			if (list_only) {
				act = LIST;

				/*
				 * cpio is like pax: list to stderr
				 * unless in list mode
				 */
				listf = stdout;
			}
			while (*argv != NULL)
				if (pat_add(*argv++, NULL) < 0)
					cpio_usage();
			break;
		case COPY:
			if (*argv == NULL) {
				paxwarn(0, "Destination directory was not supplied");
				cpio_usage();
			}
			dirptr = *argv;
			if (mkpath(dirptr) < 0)
				cpio_usage();
			--argc;
			++argv;
			/* FALL THROUGH */
		case ARCHIVE:
		case APPND:
			if (*argv != NULL)
				cpio_usage();
			/*
			 * no read errors allowed on updates/append operation!
			 */
			maxflt = 0;
			while ((str = get_line(stdin)) != NULL) {
				ftree_add(str, 0);
			}
			if (ferror(stdin)) {
				syswarn(1, errno, "Unable to read from %s",
				    "stdin");
				cpio_usage();
			}
			break;
		default:
			cpio_usage();
			break;
	}
}
#endif /* !NOCPIO */

/*
 * printflg()
 *	print out those invalid flag sets found to the user
 */

static void
printflg(unsigned int flg)
{
	int nxt;
	int pos = 0;

	(void)fprintf(stderr,"%s: Invalid combination of options:", argv0);
	while ((nxt = ffs(flg)) != 0) {
		flg >>= nxt;
		pos += nxt;
		(void)fprintf(stderr, " -%c", flgch[pos-1]);
	}
	(void)putc('\n', stderr);
}

/*
 * opt_next()
 *	called by format specific options routines to get each format specific
 *	flag and value specified with -o
 * Return:
 *	pointer to next OPLIST entry or NULL (end of list).
 */

OPLIST *
opt_next(void)
{
	OPLIST *opt;

	if ((opt = ophead) != NULL)
		ophead = ophead->fow;
	return(opt);
}

/*
 * bad_opt()
 *	generic routine used to complain about a format specific options
 *	when the format does not support options.
 */

static int
bad_opt(void)
{
	OPLIST *opt;

	if (ophead == NULL)
		return(0);
	/*
	 * print all we were given
	 */
	paxwarn(1,"These format options are not supported");
	while ((opt = opt_next()) != NULL)
		(void)fprintf(stderr, "\t%s = %s\n", opt->name, opt->value);
	pax_usage();
	return(0);
}

/*
 * opt_add()
 *	breaks the value supplied to -o into a option name and value. options
 *	are given to -o in the form -o name-value,name=value
 *	multiple -o may be specified.
 * Return:
 *	0 if format in name=value format, -1 if -o is passed junk
 */

static int
opt_add(const char *str)
{
	OPLIST *opt;
	char *frpt;
	char *pt;
	char *endpt;
	char *dstr;

	if ((str == NULL) || (*str == '\0')) {
		paxwarn(0, "Invalid option name");
		return(-1);
	}
	if ((dstr = strdup(str)) == NULL) {
		paxwarn(0, "Unable to allocate space for option list");
		return(-1);
	}
	frpt = endpt = dstr;

	/*
	 * break into name and values pieces and stuff each one into a
	 * OPLIST structure. When we know the format, the format specific
	 * option function will go through this list
	 */
	while ((frpt != NULL) && (*frpt != '\0')) {
		if ((endpt = strchr(frpt, ',')) != NULL)
			*endpt = '\0';
		if ((pt = strchr(frpt, '=')) == NULL) {
			paxwarn(0, "Invalid options format");
			free(dstr);
			return(-1);
		}
		if ((opt = malloc(sizeof(OPLIST))) == NULL) {
			paxwarn(0, "Unable to allocate space for option list");
			free(dstr);
			return(-1);
		}
		dstr = NULL;	/* parts of string going onto the OPLIST */
		*pt++ = '\0';
		opt->name = frpt;
		opt->value = pt;
		opt->fow = NULL;
		if (endpt != NULL)
			frpt = endpt + 1;
		else
			frpt = NULL;
		if (ophead == NULL) {
			optail = ophead = opt;
			continue;
		}
		optail->fow = opt;
		optail = opt;
	}
	free(dstr);
	return(0);
}

/*
 * str_offt()
 *	Convert an expression of the following forms to an off_t > 0.
 *	1) A positive decimal number.
 *	2) A positive decimal number followed by a b (mult by 512).
 *	3) A positive decimal number followed by a k (mult by 1024).
 *	4) A positive decimal number followed by a m (mult by 512).
 *	5) A positive decimal number followed by a w (mult by sizeof int)
 *	6) Two or more positive decimal numbers (with/without k,b or w).
 *	   separated by x (also * for backwards compatibility), specifying
 *	   the product of the indicated values.
 * Return:
 *	0 for an error, a positive value o.w.
 */

static off_t
str_offt(char *val)
{
	char *expr;
	off_t num, t;

	num = strtoll(val, &expr, 0);
	if ((num == LLONG_MAX) || (num <= 0) || (expr == val))
		return(0);

	switch (*expr) {
	case 'b':
		t = num;
		num *= 512;
		if (t > num)
			return(0);
		++expr;
		break;
	case 'k':
		t = num;
		num *= 1024;
		if (t > num)
			return(0);
		++expr;
		break;
	case 'm':
		t = num;
		num *= 1048576;
		if (t > num)
			return(0);
		++expr;
		break;
	case 'w':
		t = num;
		num *= sizeof(int);
		if (t > num)
			return(0);
		++expr;
		break;
	}

	switch (*expr) {
		case '\0':
			break;
		case '*':
		case 'x':
			t = num;
			num *= str_offt(expr + 1);
			if (t > num)
				return(0);
			break;
		default:
			return(0);
	}
	return(num);
}

char *
get_line(FILE *f)
{
	char *str = NULL;
	size_t size = 0;
	ssize_t len;

	do {
		len = getline(&str, &size, f);
		if (len == -1) {
			free(str);
			return NULL;
		}
		if (str[len - 1] == '\n')
			str[len - 1] = '\0';
	} while (str[0] == '\0');
	return str;
}

/*
 * no_op()
 *	for those option functions where the archive format has nothing to do.
 * Return:
 *	0
 */

static int
no_op(void)
{
	return(0);
}

/*
 * pax_usage()
 *	print the usage summary to the user
 */

void
pax_usage(void)
{
	(void)fputs(
	    "usage: pax [-0cdjnOvz] [-E limit] [-f archive] [-G group] [-s replstr]\n"
	    "           [-T range] [-U user] [pattern ...]\n"
	    "       pax -r [-0cDdijknOuvYZz] [-E limit] [-f archive] [-G group] [-o options]\n"
	    "           [-p string] [-s replstr] [-T range] [-U user] [pattern ...]\n"
	    "       pax -w [-0adHijLOPtuvXz] [-B bytes] [-b blocksize] [-f archive]\n"
	    "           [-G group] [-o options] [-s replstr] [-T range] [-U user]\n"
	    "           [-x format] [file ...]\n"
	    "       pax -rw [-0DdHikLlnOPtuvXYZ] [-G group] [-p string] [-s replstr]\n"
	    "           [-T range] [-U user] [file ...] directory\n",
	    stderr);
	exit(1);
}

/*
 * tar_usage()
 *	print the usage summary to the user
 */

void
tar_usage(void)
{
	(void)fputs(
	    "usage: tar {crtux}[014578beFfHhjLmNOoPpqsvwXZz]\n"
	    "           [blocking-factor | format | archive | replstr]\n"
	    "           [-C directory] [-I file] [file ...]\n"
	    "       tar {-crtux} [-014578eHhjLmNOoPpqvwXZz] [-b blocking-factor]\n"
	    "           [-C directory] [-F format] [-f archive] [-I file]\n"
	    "           [-s replstr] [file ...]\n",
	    stderr);
	exit(1);
}

#ifndef NOCPIO
/*
 * cpio_usage()
 *	print the usage summary to the user
 */

void
cpio_usage(void)
{
	(void)fputs(
	    "usage: cpio -o [-AaBcjLvZz] [-C bytes] [-F archive] [-H format]\n"
	    "            [-O archive] < name-list [> archive]\n"
	    "       cpio -i [-6BbcdfjmrSstuvZz] [-C bytes] [-E file] [-F archive] [-H format]\n"
	    "            [-I archive] [pattern ...] [< archive]\n"
	    "       cpio -p [-adLlmuv] destination-directory < name-list\n",
	    stderr);
	exit(1);
}
#endif /* !NOCPIO */

#ifndef SMALL
static int
compress_id(char *blk, int size)
{
	if (size >= 2 && blk[0] == '\037' && blk[1] == '\235') {
		paxwarn(0, "input compressed with %s; use the -%c option"
		    " to decompress it", "compress", 'Z');
		exit(1);
	}
	return (-1);
}

static int
gzip_id(char *blk, int size)
{
	if (size >= 2 && blk[0] == '\037' && blk[1] == '\213') {
		paxwarn(0, "input compressed with %s; use the -%c option"
		    " to decompress it", "gzip", 'z');
		exit(1);
	}
	return (-1);
}

static int
bzip2_id(char *blk, int size)
{
	if (size >= 3 && blk[0] == 'B' && blk[1] == 'Z' && blk[2] == 'h') {
		paxwarn(0, "input compressed with %s; use the -%c option"
		    " to decompress it", "bzip2", 'j');
		exit(1);
	}
	return (-1);
}

static int
xz_id(char *blk, int size)
{
	if (size >= 6 && memcmp(blk, "\xFD\x37\x7A\x58\x5A", 6) == 0) {
		paxwarn(0, "input compressed with xz");
		exit(1);
	}
	return (-1);
}
#endif /* !SMALL */
