/*	$OpenBSD: dd.c,v 1.29 2024/07/12 14:30:27 deraadt Exp $	*/
/*	$NetBSD: dd.c,v 1.6 1996/02/20 19:29:06 jtc Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego and Lance
 * Visser of Convex Computer Corporation.
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
#include <sys/ioctl.h>
#include <sys/mtio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dd.h"
#include "extern.h"

static void dd_close(void);
static void dd_in(void);
static void getfdtype(IO *);
static void setup(void);

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

IO	in, out;		/* input/output state */
STAT	st;			/* statistics */
void	(*cfunc)(void);		/* conversion function */
size_t	cpy_cnt;		/* # of blocks to copy */
u_int	ddflags;		/* conversion options */
size_t	cbsz;			/* conversion block size */
size_t	files_cnt = 1;		/* # of files to copy */
const	u_char	*ctab;		/* conversion table */

int
main(int argc, char *argv[])
{
	jcl(argv);
	setup();

	(void)signal(SIGINFO, sig_summary);
	(void)signal(SIGINT, sig_terminate);

	atexit(exit_summary);

	if (cpy_cnt != (size_t)-1) {
		while (files_cnt--)
			dd_in();
	}

	dd_close();
	exit(0);
}

static void
setup(void)
{
	if (in.name == NULL) {
		in.name = "stdin";
		in.fd = STDIN_FILENO;
	} else {
		in.fd = open(in.name, O_RDONLY);
		if (in.fd == -1)
			err(1, "%s", in.name);
	}

	getfdtype(&in);

	if (files_cnt > 1 && !(in.flags & ISTAPE))
		errx(1, "files is not supported for non-tape devices");

	if (out.name == NULL) {
		/* No way to check for read access here. */
		out.fd = STDOUT_FILENO;
		out.name = "stdout";
	} else {
#define	OFLAGS \
    (O_CREAT | (ddflags & (C_SEEK | C_NOTRUNC) ? 0 : O_TRUNC))
		out.fd = open(out.name, O_RDWR | OFLAGS, DEFFILEMODE);
		/*
		 * May not have read access, so try again with write only.
		 * Without read we may have a problem if output also does
		 * not support seeks.
		 */
		if (out.fd == -1) {
			out.fd = open(out.name, O_WRONLY | OFLAGS, DEFFILEMODE);
			out.flags |= NOREAD;
		}
		if (out.fd == -1)
			err(1, "%s", out.name);
	}

	getfdtype(&out);

	/*
	 * Allocate space for the input and output buffers.  If not doing
	 * record oriented I/O, only need a single buffer.
	 */
	if (!(ddflags & (C_BLOCK|C_UNBLOCK))) {
		if ((in.db = malloc(out.dbsz + in.dbsz - 1)) == NULL)
			err(1, "input buffer");
		out.db = in.db;
	} else {
		in.db = malloc(MAXIMUM(in.dbsz, cbsz) + cbsz);
		if (in.db == NULL)
			err(1, "input buffer");
		out.db = malloc(out.dbsz + cbsz);
		if (out.db == NULL)
			err(1, "output buffer");
	}
	in.dbp = in.db;
	out.dbp = out.db;

	/* Position the input/output streams. */
	if (in.offset)
		pos_in();
	if (out.offset)
		pos_out();

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	/*
	 * Truncate the output file; ignore errors because it fails on some
	 * kinds of output files, tapes, for example.
	 */
	if ((ddflags & (C_OF | C_SEEK | C_NOTRUNC)) == (C_OF | C_SEEK))
		(void)ftruncate(out.fd, out.offset * out.dbsz);

	/*
	 * If converting case at the same time as another conversion, build a
	 * table that does both at once.  If just converting case, use the
	 * built-in tables.
	 */
	if (ddflags & (C_LCASE|C_UCASE)) {
#ifdef	NO_CONV
		/* Should not get here, but just in case... */
		errx(1, "case conv and -DNO_CONV");
#else	/* NO_CONV */
		u_int cnt;
		if (ddflags & C_ASCII || ddflags & C_EBCDIC) {
			if (ddflags & C_LCASE) {
				for (cnt = 0; cnt < 0377; ++cnt)
					casetab[cnt] = tolower(ctab[cnt]);
			} else {
				for (cnt = 0; cnt < 0377; ++cnt)
					casetab[cnt] = toupper(ctab[cnt]);
			}
		} else {
			if (ddflags & C_LCASE) {
				for (cnt = 0; cnt < 0377; ++cnt)
					casetab[cnt] = tolower(cnt);
			} else {
				for (cnt = 0; cnt < 0377; ++cnt)
					casetab[cnt] = toupper(cnt);
			}
		}

		ctab = casetab;
#endif	/* NO_CONV */
	}

	/* Statistics timestamp. */
	clock_gettime(CLOCK_MONOTONIC, &st.start);
}

static void
getfdtype(IO *io)
{
	struct mtget mt;
	struct stat sb;

	if (fstat(io->fd, &sb))
		err(1, "%s", io->name);
	if (S_ISCHR(sb.st_mode))
		io->flags |= ioctl(io->fd, MTIOCGET, &mt) ? ISCHR : ISTAPE;
	if (S_ISFIFO(sb.st_mode) || S_ISSOCK(sb.st_mode))
		io->flags |= ISPIPE;
}

static void
swapbytes(void *v, size_t len)
{
	unsigned char *p = v;
	unsigned char t;

	while (len > 1) {
		t = p[0];
		p[0] = p[1];
		p[1] = t;
		p += 2;
		len -= 2;
	}
}


static void
dd_in(void)
{
	ssize_t n;

	for (;;) {
		if (cpy_cnt && (st.in_full + st.in_part) >= cpy_cnt)
			return;

		/*
		 * Zero the buffer first if sync; if doing block operations
		 * use spaces.
		 */
		if (ddflags & C_SYNC) {
			if (ddflags & (C_BLOCK|C_UNBLOCK))
				(void)memset(in.dbp, ' ', in.dbsz);
			else
				(void)memset(in.dbp, 0, in.dbsz);
		}

		n = read(in.fd, in.dbp, in.dbsz);
		if (n == 0) {
			in.dbrcnt = 0;
			return;
		}

		/* Read error. */
		if (n == -1) {
			/*
			 * If noerror not specified, die.  POSIX requires that
			 * the warning message be followed by an I/O display.
			 */
			if (!(ddflags & C_NOERROR))
				err(1, "%s", in.name);
			warn("%s", in.name);
			sig_summary(0);

			/*
			 * If it's not a tape drive or a pipe, seek past the
			 * error.  If your OS doesn't do the right thing for
			 * raw disks this section should be modified to re-read
			 * in sector size chunks.
			 */
			if (!(in.flags & (ISPIPE|ISTAPE)) &&
			    lseek(in.fd, (off_t)in.dbsz, SEEK_CUR))
				warn("%s", in.name);

			/* If sync not specified, omit block and continue. */
			if (!(ddflags & C_SYNC))
				continue;

			/* Read errors count as full blocks. */
			in.dbcnt += in.dbrcnt = in.dbsz;
			++st.in_full;

		/* Handle full input blocks. */
		} else if (n == in.dbsz) {
			in.dbcnt += in.dbrcnt = n;
			++st.in_full;

		/* Handle partial input blocks. */
		} else {
			/* If sync, use the entire block. */
			if (ddflags & C_SYNC)
				in.dbcnt += in.dbrcnt = in.dbsz;
			else
				in.dbcnt += in.dbrcnt = n;
			++st.in_part;
		}

		/*
		 * POSIX states that if bs is set and no other conversions
		 * than noerror, notrunc or sync are specified, the block
		 * is output without buffering as it is read.
		 */
		if (ddflags & C_BS) {
			out.dbcnt = in.dbcnt;
			dd_out(1);
			in.dbcnt = 0;
			continue;
		}

		if (ddflags & C_SWAB) {
			if ((n = in.dbrcnt) & 1) {
				++st.swab;
				--n;
			}
			swapbytes(in.dbp, n);
		}

		in.dbp += in.dbrcnt;
		(*cfunc)();
	}
}

/*
 * Cleanup any remaining I/O and flush output.  If necessary, output file
 * is truncated.
 */
static void
dd_close(void)
{
	if (cfunc == def)
		def_close();
	else if (cfunc == block)
		block_close();
	else if (cfunc == unblock)
		unblock_close();
	if (ddflags & C_OSYNC && out.dbcnt && out.dbcnt < out.dbsz) {
		if (ddflags & (C_BLOCK|C_UNBLOCK))
			memset(out.dbp, ' ', out.dbsz - out.dbcnt);
		else
			memset(out.dbp, 0, out.dbsz - out.dbcnt);
		out.dbcnt = out.dbsz;
	}
	if (out.dbcnt)
		dd_out(1);
	if (ddflags & C_FSYNC) {
		if (fsync(out.fd) == -1)
			err(1, "fsync %s", out.name);
	}
}

void
dd_out(int force)
{
	static int warned;
	size_t cnt, n;
	ssize_t nw;
	u_char *outp;

	/*
	 * Write one or more blocks out.  The common case is writing a full
	 * output block in a single write; increment the full block stats.
	 * Otherwise, we're into partial block writes.  If a partial write,
	 * and it's a character device, just warn.  If a tape device, quit.
	 *
	 * The partial writes represent two cases.  1: Where the input block
	 * was less than expected so the output block was less than expected.
	 * 2: Where the input block was the right size but we were forced to
	 * write the block in multiple chunks.  The original versions of dd(1)
	 * never wrote a block in more than a single write, so the latter case
	 * never happened.
	 *
	 * One special case is if we're forced to do the write -- in that case
	 * we play games with the buffer size, and it's usually a partial write.
	 */
	outp = out.db;
	for (n = force ? out.dbcnt : out.dbsz;; n = out.dbsz) {
		for (cnt = n;; cnt -= nw) {
			nw = write(out.fd, outp, cnt);
			if (nw == 0)
				errx(1, "%s: end of device", out.name);
			if (nw == -1) {
				if (errno != EINTR)
					err(1, "%s", out.name);
				nw = 0;
			}
			outp += nw;
			st.bytes += nw;
			if (nw == n) {
				if (n != out.dbsz)
					++st.out_part;
				else
					++st.out_full;
				break;
			}
			++st.out_part;
			if (nw == cnt)
				break;
			if (out.flags & ISCHR && !warned) {
				warned = 1;
				warnx("%s: short write on character device",
				    out.name);
			}
			if (out.flags & ISTAPE)
				errx(1, "%s: short write on tape device", out.name);
		}
		if ((out.dbcnt -= n) < out.dbsz)
			break;
	}

	/* Reassemble the output block. */
	if (out.dbcnt)
		(void)memmove(out.db, out.dbp - out.dbcnt, out.dbcnt);
	out.dbp = out.db + out.dbcnt;
}
