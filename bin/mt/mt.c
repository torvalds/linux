/*	$OpenBSD: mt.c,v 1.41 2019/06/28 13:34:59 deraadt Exp $	*/
/*	$NetBSD: mt.c,v 1.14.2.1 1996/05/27 15:12:11 mrg Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

/*
 * mt --
 *   magnetic tape manipulation program
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/stat.h>
#include <sys/disklabel.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>
#include <limits.h>

#include "mt.h"

struct commands {
	char *c_name;
	int c_code;
	int c_ronly;
	int c_mincount;
} com[] = {
	{ "blocksize",	MTSETBSIZ,  1, 0 },
	{ "bsf",	MTBSF,      1, 1 },
	{ "bsr",	MTBSR,      1, 1 },
	{ "density",	MTSETDNSTY, 1, 1 },
	{ "eof",	MTWEOF,     0, 1 },
	{ "eom",	MTEOM,      1, 1 },
	{ "erase",	MTERASE,    0, 1 },
	{ "fsf",	MTFSF,      1, 1 },
	{ "fsr",	MTFSR,      1, 1 },
	{ "offline",	MTOFFL,     1, 1 },
#define COM_EJECT	9	/* element in the above array */
	{ "rewind",	MTREW,      1, 1 },
	{ "rewoffl",	MTOFFL,     1, 1 },
	{ "status",	MTNOP,      1, 1 },
	{ "retension",	MTRETEN,    1, 1 },
#define COM_RETEN	13	/* element in the above array */
	{ "weof",	MTWEOF,     0, 1 },
	{ NULL }
};

void printreg(char *, u_int, char *);
void status(struct mtget *);
void usage(void);

int		_rmtopendev(char *path, int oflags, int dflags, char **realp);
int		_rmtmtioctop(int fd, struct mtop *cmd);
struct mtget	*_rmtstatus(int fd);
void		_rmtclose(void);

extern char	*__progname;

char	*host = NULL;	/* remote host (if any) */

int
_rmtopendev(char *path, int oflags, int dflags, char **realp)
{
#ifdef RMT
	if (host)
		return rmtopen(path, oflags);
#endif
	return opendev(path, oflags, dflags, realp);
}

int
_rmtmtioctop(int fd, struct mtop *cmd)
{
#ifdef RMT
	if (host)
		return rmtioctl(cmd->mt_op, cmd->mt_count);
#endif
	return ioctl(fd, MTIOCTOP, cmd);
}

struct mtget *
_rmtstatus(int fd)
{
	static struct mtget mt_status;

#ifdef RMT
	if (host)
		return rmtstatus();
#endif
	if (ioctl(fd, MTIOCGET, &mt_status) == -1)
		err(2, "ioctl MTIOCGET");
	return &mt_status;
}

void
_rmtclose(void)
{
#ifdef RMT
	if (host)
		rmtclose();
#endif
}

int	eject = 0;

int
main(int argc, char *argv[])
{
	struct commands *comp;
	struct mtop mt_com;
	int ch, mtfd, flags, insert = 0;
	char *p, *tape, *realtape, *opts;
	size_t len;

	if (strcmp(__progname, "eject") == 0) {
		opts = "t";
		eject = 1;
		tape = NULL;
	} else {
		opts = "f:";
		if ((tape = getenv("TAPE")) == NULL)
			tape = _PATH_DEFTAPE;
	}

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 't':
			insert = 1;
			break;
		case 'f':
			tape = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (eject) {
		if (argc == 1) {
			tape = *argv++;
			argc--;
		}
		if (argc != 0)
			usage();
	} else if (argc < 1 || argc > 2)
		usage();

	if (tape == NULL)
		usage();

	if (strchr(tape, ':')) {
#ifdef RMT
		host = tape;
		tape = strchr(host, ':');
		*tape++ = '\0';
		if (rmthost(host) == 0)
			exit(X_ABORT);
#else
		err(1, "no remote support");
#endif
	}

	if (strlen(tape) >= PATH_MAX)
		err(1, "tape name too long for protocol");

	if (eject) {
		if (insert)
			comp = &com[COM_RETEN];
		else
			comp = &com[COM_EJECT];
	} else {
		len = strlen(p = *argv++);
		for (comp = com;; comp++) {
			if (comp->c_name == NULL)
				errx(1, "%s: unknown command", p);
			if (strncmp(p, comp->c_name, len) == 0)
				break;
		}
	}

	flags = comp->c_ronly ? O_RDONLY : O_WRONLY | O_CREAT;
	/* NOTE: OPENDEV_PART required since cd(4) devices go through here. */
	if ((mtfd = _rmtopendev(tape, flags, OPENDEV_PART, &realtape)) == -1) {
		if (errno != 0)
			warn("%s", host ? tape : realtape);
		exit(2);
	}
	if (comp->c_code != MTNOP) {
		mt_com.mt_op = comp->c_code;
		if (*argv) {
			mt_com.mt_count = strtol(*argv, &p, 10);
			if (mt_com.mt_count < comp->c_mincount || *p)
				errx(2, "%s: illegal count", *argv);
		}
		else
			mt_com.mt_count = 1;
		if (_rmtmtioctop(mtfd, &mt_com) == -1) {
			if (eject)
				err(2, "%s", tape);
			else
				err(2, "%s: %s", tape, comp->c_name);
		}
	} else {
		status(_rmtstatus(mtfd));
	}

	_rmtclose();

	exit(X_FINOK);
	/* NOTREACHED */
}

struct tape_desc {
	short	t_type;		/* type of magtape device */
	char	*t_name;	/* printing name */
	char	*t_dsbits;	/* "drive status" register */
	char	*t_erbits;	/* "error" register */
} tapes[] = {
#define SCSI_DS_BITS	"\20\5WriteProtect\2Mounted"
	{ 0x7,		"SCSI",		SCSI_DS_BITS,	"76543210" },
	{ 0 }
};

/*
 * Interpret the status buffer returned
 */
void
status(struct mtget *bp)
{
	struct tape_desc *mt;

	for (mt = tapes;; mt++) {
		if (mt->t_type == 0) {
			(void)printf("%d: unknown tape drive type\n",
			    bp->mt_type);
			return;
		}
		if (mt->t_type == bp->mt_type)
			break;
	}
	(void)printf("%s tape drive, residual=%d\n", mt->t_name, bp->mt_resid);
	printreg("ds", bp->mt_dsreg, mt->t_dsbits);
	printreg("\ner", bp->mt_erreg, mt->t_erbits);
	(void)putchar('\n');
	(void)printf("blocksize: %d (%d)\n", bp->mt_blksiz, bp->mt_mblksiz);
	(void)printf("density: %d (%d)\n", bp->mt_density, bp->mt_mdensity);
	(void)printf("current file number: %d\n", bp->mt_fileno);
	(void)printf("current block number: %d\n", bp->mt_blkno);
}

/*
 * Print a register a la the %b format of the kernel's printf.
 */
void
printreg(char *s, u_int v, char *bits)
{
	int i, any = 0;
	char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);
	if (!bits)
		return;
	bits++;
	if (v && *bits) {
		putchar('<');
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

void
usage(void)
{
	if (eject)
		(void)fprintf(stderr, "usage: %s [-t] device\n", __progname);
	else
		(void)fprintf(stderr,
		    "usage: %s [-f device] command [count]\n", __progname);
	exit(X_USAGE);
}
