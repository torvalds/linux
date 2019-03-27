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
static char sccsid[] = "@(#)ar_io.c	8.2 (Berkeley) 4/18/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "pax.h"
#include "options.h"
#include "extern.h"

/*
 * Routines which deal directly with the archive I/O device/file.
 */

#define DMOD		0666		/* default mode of created archives */
#define EXT_MODE	O_RDONLY	/* open mode for list/extract */
#define AR_MODE		(O_WRONLY | O_CREAT | O_TRUNC)	/* mode for archive */
#define APP_MODE	O_RDWR		/* mode for append */

static char none[] = "<NONE>";		/* pseudo name for no file */
static char stdo[] = "<STDOUT>";	/* pseudo name for stdout */
static char stdn[] = "<STDIN>";		/* pseudo name for stdin */
static int arfd = -1;			/* archive file descriptor */
static int artyp = ISREG;		/* archive type: file/FIFO/tape */
static int arvol = 1;			/* archive volume number */
static int lstrval = -1;		/* return value from last i/o */
static int io_ok;			/* i/o worked on volume after resync */
static int did_io;			/* did i/o ever occur on volume? */
static int done;			/* set via tty termination */
static struct stat arsb;		/* stat of archive device at open */
static int invld_rec;			/* tape has out of spec record size */
static int wr_trail = 1;		/* trailer was rewritten in append */
static int can_unlnk = 0;		/* do we unlink null archives?  */
const char *arcname;		  	/* printable name of archive */
const char *gzip_program;		/* name of gzip program */
static pid_t zpid = -1; 		/* pid of child process */

static int get_phys(void);
static void ar_start_gzip(int, const char *, int);

/*
 * ar_open()
 *	Opens the next archive volume. Determines the type of the device and
 *	sets up block sizes as required by the archive device and the format.
 *	Note: we may be called with name == NULL on the first open only.
 * Return:
 *	-1 on failure, 0 otherwise
 */

int
ar_open(const char *name)
{
	struct mtget mb;

	if (arfd != -1)
		(void)close(arfd);
	arfd = -1;
	can_unlnk = did_io = io_ok = invld_rec = 0;
	artyp = ISREG;
	flcnt = 0;

	/*
	 * open based on overall operation mode
	 */
	switch (act) {
	case LIST:
	case EXTRACT:
		if (name == NULL) {
			arfd = STDIN_FILENO;
			arcname = stdn;
		} else if ((arfd = open(name, EXT_MODE, DMOD)) < 0)
			syswarn(0, errno, "Failed open to read on %s", name);
		if (arfd != -1 && gzip_program != NULL)
			ar_start_gzip(arfd, gzip_program, 0);
		break;
	case ARCHIVE:
		if (name == NULL) {
			arfd = STDOUT_FILENO;
			arcname = stdo;
		} else if ((arfd = open(name, AR_MODE, DMOD)) < 0)
			syswarn(0, errno, "Failed open to write on %s", name);
		else
			can_unlnk = 1;
		if (arfd != -1 && gzip_program != NULL)
			ar_start_gzip(arfd, gzip_program, 1);
		break;
	case APPND:
		if (name == NULL) {
			arfd = STDOUT_FILENO;
			arcname = stdo;
		} else if ((arfd = open(name, APP_MODE, DMOD)) < 0)
			syswarn(0, errno, "Failed open to read/write on %s",
				name);
		break;
	case COPY:
		/*
		 * arfd not used in COPY mode
		 */
		arcname = none;
		lstrval = 1;
		return(0);
	}
	if (arfd < 0)
		return(-1);

	if (chdname != NULL)
		if (chdir(chdname) != 0) {
			syswarn(1, errno, "Failed chdir to %s", chdname);
			return(-1);
		}
	/*
	 * set up is based on device type
	 */
	if (fstat(arfd, &arsb) < 0) {
		syswarn(0, errno, "Failed stat on %s", arcname);
		(void)close(arfd);
		arfd = -1;
		can_unlnk = 0;
		return(-1);
	}
	if (S_ISDIR(arsb.st_mode)) {
		paxwarn(0, "Cannot write an archive on top of a directory %s",
		    arcname);
		(void)close(arfd);
		arfd = -1;
		can_unlnk = 0;
		return(-1);
	}

	if (S_ISCHR(arsb.st_mode))
		artyp = ioctl(arfd, MTIOCGET, &mb) ? ISCHR : ISTAPE;
	else if (S_ISBLK(arsb.st_mode))
		artyp = ISBLK;
	else if ((lseek(arfd, (off_t)0L, SEEK_CUR) == -1) && (errno == ESPIPE))
		artyp = ISPIPE;
	else
		artyp = ISREG;

	/*
	 * make sure we beyond any doubt that we only can unlink regular files
	 * we created
	 */
	if (artyp != ISREG)
		can_unlnk = 0;
	/*
	 * if we are writing, we are done
	 */
	if (act == ARCHIVE) {
		blksz = rdblksz = wrblksz;
		lstrval = 1;
		return(0);
	}

	/*
	 * set default blksz on read. APPNDs writes rdblksz on the last volume
	 * On all new archive volumes, we shift to wrblksz (if the user
	 * specified one, otherwise we will continue to use rdblksz). We
	 * must to set blocksize based on what kind of device the archive is
	 * stored.
	 */
	switch(artyp) {
	case ISTAPE:
		/*
		 * Tape drives come in at least two flavors. Those that support
		 * variable sized records and those that have fixed sized
		 * records. They must be treated differently. For tape drives
		 * that support variable sized records, we must make large
		 * reads to make sure we get the entire record, otherwise we
		 * will just get the first part of the record (up to size we
		 * asked). Tapes with fixed sized records may or may not return
		 * multiple records in a single read. We really do not care
		 * what the physical record size is UNLESS we are going to
		 * append. (We will need the physical block size to rewrite
		 * the trailer). Only when we are appending do we go to the
		 * effort to figure out the true PHYSICAL record size.
		 */
		blksz = rdblksz = MAXBLK;
		break;
	case ISPIPE:
	case ISBLK:
	case ISCHR:
		/*
		 * Blocksize is not a major issue with these devices (but must
		 * be kept a multiple of 512). If the user specified a write
		 * block size, we use that to read. Under append, we must
		 * always keep blksz == rdblksz. Otherwise we go ahead and use
		 * the device optimal blocksize as (and if) returned by stat
		 * and if it is within pax specs.
		 */
		if ((act == APPND) && wrblksz) {
			blksz = rdblksz = wrblksz;
			break;
		}

		if ((arsb.st_blksize > 0) && (arsb.st_blksize < MAXBLK) &&
		    ((arsb.st_blksize % BLKMULT) == 0))
			rdblksz = arsb.st_blksize;
		else
			rdblksz = DEVBLK;
		/*
		 * For performance go for large reads when we can without harm
		 */
		if ((act == APPND) || (artyp == ISCHR))
			blksz = rdblksz;
		else
			blksz = MAXBLK;
		break;
	case ISREG:
		/*
		 * if the user specified wrblksz works, use it. Under appends
		 * we must always keep blksz == rdblksz
		 */
		if ((act == APPND) && wrblksz && ((arsb.st_size%wrblksz)==0)){
			blksz = rdblksz = wrblksz;
			break;
		}
		/*
		 * See if we can find the blocking factor from the file size
		 */
		for (rdblksz = MAXBLK; rdblksz > 0; rdblksz -= BLKMULT)
			if ((arsb.st_size % rdblksz) == 0)
				break;
		/*
		 * When we cannot find a match, we may have a flawed archive.
		 */
		if (rdblksz <= 0)
			rdblksz = FILEBLK;
		/*
		 * for performance go for large reads when we can
		 */
		if (act == APPND)
			blksz = rdblksz;
		else
			blksz = MAXBLK;
		break;
	default:
		/*
		 * should never happen, worse case, slow...
		 */
		blksz = rdblksz = BLKMULT;
		break;
	}
	lstrval = 1;
	return(0);
}

/*
 * ar_close()
 *	closes archive device, increments volume number, and prints i/o summary
 */
void
ar_close(void)
{
	int status;

	if (arfd < 0) {
		did_io = io_ok = flcnt = 0;
		return;
	}

	/*
	 * Close archive file. This may take a LONG while on tapes (we may be
	 * forced to wait for the rewind to complete) so tell the user what is
	 * going on (this avoids the user hitting control-c thinking pax is
	 * broken).
	 */
	if (vflag && (artyp == ISTAPE)) {
		if (vfpart)
			(void)putc('\n', listf);
		(void)fprintf(listf,
			"%s: Waiting for tape drive close to complete...",
			argv0);
		(void)fflush(listf);
	}

	/*
	 * if nothing was written to the archive (and we created it), we remove
	 * it
	 */
	if (can_unlnk && (fstat(arfd, &arsb) == 0) && (S_ISREG(arsb.st_mode)) &&
	    (arsb.st_size == 0)) {
		(void)unlink(arcname);
		can_unlnk = 0;
	}

	/*
	 * for a quick extract/list, pax frequently exits before the child
	 * process is done
	 */
	if ((act == LIST || act == EXTRACT) && nflag && zpid > 0)
		kill(zpid, SIGINT);

	(void)close(arfd);

	/* Do not exit before child to ensure data integrity */
	if (zpid > 0)
		waitpid(zpid, &status, 0);

	if (vflag && (artyp == ISTAPE)) {
		(void)fputs("done.\n", listf);
		vfpart = 0;
		(void)fflush(listf);
	}
	arfd = -1;

	if (!io_ok && !did_io) {
		flcnt = 0;
		return;
	}
	did_io = io_ok = 0;

	/*
	 * The volume number is only increased when the last device has data
	 * and we have already determined the archive format.
	 */
	if (frmt != NULL)
		++arvol;

	if (!vflag) {
		flcnt = 0;
		return;
	}

	/*
	 * Print out a summary of I/O for this archive volume.
	 */
	if (vfpart) {
		(void)putc('\n', listf);
		vfpart = 0;
	}

	/*
	 * If we have not determined the format yet, we just say how many bytes
	 * we have skipped over looking for a header to id. There is no way we
	 * could have written anything yet.
	 */
	if (frmt == NULL) {
#	ifdef NET2_STAT
		(void)fprintf(listf, "%s: unknown format, %lu bytes skipped.\n",
		    argv0, rdcnt);
#	else
		(void)fprintf(listf, "%s: unknown format, %ju bytes skipped.\n",
		    argv0, (uintmax_t)rdcnt);
#	endif
		(void)fflush(listf);
		flcnt = 0;
		return;
	}

	if (strcmp(NM_CPIO, argv0) == 0)
		(void)fprintf(listf, "%llu blocks\n",
		    (unsigned long long)((rdcnt ? rdcnt : wrcnt) / 5120));
	else if (strcmp(NM_TAR, argv0) != 0)
		(void)fprintf(listf,
#	ifdef NET2_STAT
		    "%s: %s vol %d, %lu files, %lu bytes read, %lu bytes written.\n",
		    argv0, frmt->name, arvol-1, flcnt, rdcnt, wrcnt);
#	else
		    "%s: %s vol %d, %ju files, %ju bytes read, %ju bytes written.\n",
		    argv0, frmt->name, arvol-1, (uintmax_t)flcnt,
		    (uintmax_t)rdcnt, (uintmax_t)wrcnt);
#	endif
	(void)fflush(listf);
	flcnt = 0;
}

/*
 * ar_drain()
 *	drain any archive format independent padding from an archive read
 *	from a socket or a pipe. This is to prevent the process on the
 *	other side of the pipe from getting a SIGPIPE (pax will stop
 *	reading an archive once a format dependent trailer is detected).
 */
void
ar_drain(void)
{
	int res;
	char drbuf[MAXBLK];

	/*
	 * we only drain from a pipe/socket. Other devices can be closed
	 * without reading up to end of file. We sure hope that pipe is closed
	 * on the other side so we will get an EOF.
	 */
	if ((artyp != ISPIPE) || (lstrval <= 0))
		return;

	/*
	 * keep reading until pipe is drained
	 */
	while ((res = read(arfd, drbuf, sizeof(drbuf))) > 0)
		;
	lstrval = res;
}

/*
 * ar_set_wr()
 *	Set up device right before switching from read to write in an append.
 *	device dependent code (if required) to do this should be added here.
 *	For all archive devices we are already positioned at the place we want
 *	to start writing when this routine is called.
 * Return:
 *	0 if all ready to write, -1 otherwise
 */

int
ar_set_wr(void)
{
	off_t cpos;

	/*
	 * we must make sure the trailer is rewritten on append, ar_next()
	 * will stop us if the archive containing the trailer was not written
	 */
	wr_trail = 0;

	/*
	 * Add any device dependent code as required here
	 */
	if (artyp != ISREG)
		return(0);
	/*
	 * Ok we have an archive in a regular file. If we were rewriting a
	 * file, we must get rid of all the stuff after the current offset
	 * (it was not written by pax).
	 */
	if (((cpos = lseek(arfd, (off_t)0L, SEEK_CUR)) < 0) ||
	    (ftruncate(arfd, cpos) < 0)) {
		syswarn(1, errno, "Unable to truncate archive file");
		return(-1);
	}
	return(0);
}

/*
 * ar_app_ok()
 *	check if the last volume in the archive allows appends. We cannot check
 *	this until we are ready to write since there is no spec that says all
 *	volumes in a single archive have to be of the same type...
 * Return:
 *	0 if we can append, -1 otherwise.
 */

int
ar_app_ok(void)
{
	if (artyp == ISPIPE) {
		paxwarn(1, "Cannot append to an archive obtained from a pipe.");
		return(-1);
	}

	if (!invld_rec)
		return(0);
	paxwarn(1,"Cannot append, device record size %d does not support %s spec",
		rdblksz, argv0);
	return(-1);
}

/*
 * ar_read()
 *	read up to a specified number of bytes from the archive into the
 *	supplied buffer. When dealing with tapes we may not always be able to
 *	read what we want.
 * Return:
 *	Number of bytes in buffer. 0 for end of file, -1 for a read error.
 */

int
ar_read(char *buf, int cnt)
{
	int res = 0;

	/*
	 * if last i/o was in error, no more reads until reset or new volume
	 */
	if (lstrval <= 0)
		return(lstrval);

	/*
	 * how we read must be based on device type
	 */
	switch (artyp) {
	case ISTAPE:
		if ((res = read(arfd, buf, cnt)) > 0) {
			/*
			 * CAUTION: tape systems may not always return the same
			 * sized records so we leave blksz == MAXBLK. The
			 * physical record size that a tape drive supports is
			 * very hard to determine in a uniform and portable
			 * manner.
			 */
			io_ok = 1;
			if (res != rdblksz) {
				/*
				 * Record size changed. If this is happens on
				 * any record after the first, we probably have
				 * a tape drive which has a fixed record size
				 * we are getting multiple records in a single
				 * read). Watch out for record blocking that
				 * violates pax spec (must be a multiple of
				 * BLKMULT).
				 */
				rdblksz = res;
				if (rdblksz % BLKMULT)
					invld_rec = 1;
			}
			return(res);
		}
		break;
	case ISREG:
	case ISBLK:
	case ISCHR:
	case ISPIPE:
	default:
		/*
		 * Files are so easy to deal with. These other things cannot
		 * be trusted at all. So when we are dealing with character
		 * devices and pipes we just take what they have ready for us
		 * and return. Trying to do anything else with them runs the
		 * risk of failure.
		 */
		if ((res = read(arfd, buf, cnt)) > 0) {
			io_ok = 1;
			return(res);
		}
		break;
	}

	/*
	 * We are in trouble at this point, something is broken...
	 */
	lstrval = res;
	if (res < 0)
		syswarn(1, errno, "Failed read on archive volume %d", arvol);
	else
		paxwarn(0, "End of archive volume %d reached", arvol);
	return(res);
}

/*
 * ar_write()
 *	Write a specified number of bytes in supplied buffer to the archive
 *	device so it appears as a single "block". Deals with errors and tries
 *	to recover when faced with short writes.
 * Return:
 *	Number of bytes written. 0 indicates end of volume reached and with no
 *	flaws (as best that can be detected). A -1 indicates an unrecoverable
 *	error in the archive occurred.
 */

int
ar_write(char *buf, int bsz)
{
	int res;
	off_t cpos;

	/*
	 * do not allow pax to create a "bad" archive. Once a write fails on
	 * an archive volume prevent further writes to it.
	 */
	if (lstrval <= 0)
		return(lstrval);

	if ((res = write(arfd, buf, bsz)) == bsz) {
		wr_trail = 1;
		io_ok = 1;
		return(bsz);
	}
	/*
	 * write broke, see what we can do with it. We try to send any partial
	 * writes that may violate pax spec to the next archive volume.
	 */
	if (res < 0)
		lstrval = res;
	else
		lstrval = 0;

	switch (artyp) {
	case ISREG:
		if ((res > 0) && (res % BLKMULT)) {
			/*
		 	 * try to fix up partial writes which are not BLKMULT
			 * in size by forcing the runt record to next archive
			 * volume
		 	 */
			if ((cpos = lseek(arfd, (off_t)0L, SEEK_CUR)) < 0)
				break;
			cpos -= (off_t)res;
			if (ftruncate(arfd, cpos) < 0)
				break;
			res = lstrval = 0;
			break;
		}
		if (res >= 0)
			break;
		/*
		 * if file is out of space, handle it like a return of 0
		 */
		if ((errno == ENOSPC) || (errno == EFBIG) || (errno == EDQUOT))
			res = lstrval = 0;
		break;
	case ISTAPE:
	case ISCHR:
	case ISBLK:
		if (res >= 0)
			break;
		if (errno == EACCES) {
			paxwarn(0, "Write failed, archive is write protected.");
			res = lstrval = 0;
			return(0);
		}
		/*
		 * see if we reached the end of media, if so force a change to
		 * the next volume
		 */
		if ((errno == ENOSPC) || (errno == EIO) || (errno == ENXIO))
			res = lstrval = 0;
		break;
	case ISPIPE:
	default:
		/*
		 * we cannot fix errors to these devices
		 */
		break;
	}

	/*
	 * Better tell the user the bad news...
	 * if this is a block aligned archive format, we may have a bad archive
	 * if the format wants the header to start at a BLKMULT boundary. While
	 * we can deal with the mis-aligned data, it violates spec and other
	 * archive readers will likely fail. If the format is not block
	 * aligned, the user may be lucky (and the archive is ok).
	 */
	if (res >= 0) {
		if (res > 0)
			wr_trail = 1;
		io_ok = 1;
	}

	/*
	 * If we were trying to rewrite the trailer and it didn't work, we
	 * must quit right away.
	 */
	if (!wr_trail && (res <= 0)) {
		paxwarn(1,"Unable to append, trailer re-write failed. Quitting.");
		return(res);
	}

	if (res == 0)
		paxwarn(0, "End of archive volume %d reached", arvol);
	else if (res < 0)
		syswarn(1, errno, "Failed write to archive volume: %d", arvol);
	else if (!frmt->blkalgn || ((res % frmt->blkalgn) == 0))
		paxwarn(0,"WARNING: partial archive write. Archive MAY BE FLAWED");
	else
		paxwarn(1,"WARNING: partial archive write. Archive IS FLAWED");
	return(res);
}

/*
 * ar_rdsync()
 *	Try to move past a bad spot on a flawed archive as needed to continue
 *	I/O. Clears error flags to allow I/O to continue.
 * Return:
 *	0 when ok to try i/o again, -1 otherwise.
 */

int
ar_rdsync(void)
{
	long fsbz;
	off_t cpos;
	off_t mpos;
	struct mtop mb;

	/*
	 * Fail resync attempts at user request (done) or this is going to be
	 * an update/append to an existing archive. If last i/o hit media end,
	 * we need to go to the next volume not try a resync.
	 */
	if ((done > 0) || (lstrval == 0))
		return(-1);

	if ((act == APPND) || (act == ARCHIVE)) {
		paxwarn(1, "Cannot allow updates to an archive with flaws.");
		return(-1);
	}
	if (io_ok)
		did_io = 1;

	switch(artyp) {
	case ISTAPE:
		/*
		 * if the last i/o was a successful data transfer, we assume
		 * the fault is just a bad record on the tape that we are now
		 * past. If we did not get any data since the last resync try
		 * to move the tape forward one PHYSICAL record past any
		 * damaged tape section. Some tape drives are stubborn and need
		 * to be pushed.
		 */
		if (io_ok) {
			io_ok = 0;
			lstrval = 1;
			break;
		}
		mb.mt_op = MTFSR;
		mb.mt_count = 1;
		if (ioctl(arfd, MTIOCTOP, &mb) < 0)
			break;
		lstrval = 1;
		break;
	case ISREG:
	case ISCHR:
	case ISBLK:
		/*
		 * try to step over the bad part of the device.
		 */
		io_ok = 0;
		if (((fsbz = arsb.st_blksize) <= 0) || (artyp != ISREG))
			fsbz = BLKMULT;
		if ((cpos = lseek(arfd, (off_t)0L, SEEK_CUR)) < 0)
			break;
		mpos = fsbz - (cpos % (off_t)fsbz);
		if (lseek(arfd, mpos, SEEK_CUR) < 0)
			break;
		lstrval = 1;
		break;
	case ISPIPE:
	default:
		/*
		 * cannot recover on these archive device types
		 */
		io_ok = 0;
		break;
	}
	if (lstrval <= 0) {
		paxwarn(1, "Unable to recover from an archive read failure.");
		return(-1);
	}
	paxwarn(0, "Attempting to recover from an archive read failure.");
	return(0);
}

/*
 * ar_fow()
 *	Move the I/O position within the archive forward the specified number of
 *	bytes as supported by the device. If we cannot move the requested
 *	number of bytes, return the actual number of bytes moved in skipped.
 * Return:
 *	0 if moved the requested distance, -1 on complete failure, 1 on
 *	partial move (the amount moved is in skipped)
 */

int
ar_fow(off_t sksz, off_t *skipped)
{
	off_t cpos;
	off_t mpos;

	*skipped = 0;
	if (sksz <= 0)
		return(0);

	/*
	 * we cannot move forward at EOF or error
	 */
	if (lstrval <= 0)
		return(lstrval);

	/*
	 * Safer to read forward on devices where it is hard to find the end of
	 * the media without reading to it. With tapes we cannot be sure of the
	 * number of physical blocks to skip (we do not know physical block
	 * size at this point), so we must only read forward on tapes!
	 */
	if (artyp != ISREG)
		return(0);

	/*
	 * figure out where we are in the archive
	 */
	if ((cpos = lseek(arfd, (off_t)0L, SEEK_CUR)) >= 0) {
		/*
	 	 * we can be asked to move farther than there are bytes in this
		 * volume, if so, just go to file end and let normal buf_fill()
		 * deal with the end of file (it will go to next volume by
		 * itself)
	 	 */
		if ((mpos = cpos + sksz) > arsb.st_size) {
			*skipped = arsb.st_size - cpos;
			mpos = arsb.st_size;
		} else
			*skipped = sksz;
		if (lseek(arfd, mpos, SEEK_SET) >= 0)
			return(0);
	}
	syswarn(1, errno, "Forward positioning operation on archive failed");
	lstrval = -1;
	return(-1);
}

/*
 * ar_rev()
 *	move the i/o position within the archive backwards the specified byte
 *	count as supported by the device. With tapes drives we RESET rdblksz to
 *	the PHYSICAL blocksize.
 *	NOTE: We should only be called to move backwards so we can rewrite the
 *	last records (the trailer) of an archive (APPEND).
 * Return:
 *	0 if moved the requested distance, -1 on complete failure
 */

int
ar_rev(off_t sksz)
{
	off_t cpos;
	struct mtop mb;
	int phyblk;

	/*
	 * make sure we do not have try to reverse on a flawed archive
	 */
	if (lstrval < 0)
		return(lstrval);

	switch(artyp) {
	case ISPIPE:
		if (sksz <= 0)
			break;
		/*
		 * cannot go backwards on these critters
		 */
		paxwarn(1, "Reverse positioning on pipes is not supported.");
		lstrval = -1;
		return(-1);
	case ISREG:
	case ISBLK:
	case ISCHR:
	default:
		if (sksz <= 0)
			break;

		/*
		 * For things other than files, backwards movement has a very
		 * high probability of failure as we really do not know the
		 * true attributes of the device we are talking to (the device
		 * may not even have the ability to lseek() in any direction).
		 * First we figure out where we are in the archive.
		 */
		if ((cpos = lseek(arfd, (off_t)0L, SEEK_CUR)) < 0) {
			syswarn(1, errno,
			   "Unable to obtain current archive byte offset");
			lstrval = -1;
			return(-1);
		}

		/*
		 * we may try to go backwards past the start when the archive
		 * is only a single record. If this happens and we are on a
		 * multi volume archive, we need to go to the end of the
		 * previous volume and continue our movement backwards from
		 * there.
		 */
		if ((cpos -= sksz) < (off_t)0L) {
			if (arvol > 1) {
				/*
				 * this should never happen
				 */
				paxwarn(1,"Reverse position on previous volume.");
				lstrval = -1;
				return(-1);
			}
			cpos = (off_t)0L;
		}
		if (lseek(arfd, cpos, SEEK_SET) < 0) {
			syswarn(1, errno, "Unable to seek archive backwards");
			lstrval = -1;
			return(-1);
		}
		break;
	case ISTAPE:
		/*
	 	 * Calculate and move the proper number of PHYSICAL tape
		 * blocks. If the sksz is not an even multiple of the physical
		 * tape size, we cannot do the move (this should never happen).
		 * (We also cannot handler trailers spread over two vols).
		 * get_phys() also makes sure we are in front of the filemark.
	 	 */
		if ((phyblk = get_phys()) <= 0) {
			lstrval = -1;
			return(-1);
		}

		/*
		 * make sure future tape reads only go by physical tape block
		 * size (set rdblksz to the real size).
		 */
		rdblksz = phyblk;

		/*
		 * if no movement is required, just return (we must be after
		 * get_phys() so the physical blocksize is properly set)
		 */
		if (sksz <= 0)
			break;

		/*
		 * ok we have to move. Make sure the tape drive can do it.
		 */
		if (sksz % phyblk) {
			paxwarn(1,
			    "Tape drive unable to backspace requested amount");
			lstrval = -1;
			return(-1);
		}

		/*
		 * move backwards the requested number of bytes
		 */
		mb.mt_op = MTBSR;
		mb.mt_count = sksz/phyblk;
		if (ioctl(arfd, MTIOCTOP, &mb) < 0) {
			syswarn(1,errno, "Unable to backspace tape %d blocks.",
			    mb.mt_count);
			lstrval = -1;
			return(-1);
		}
		break;
	}
	lstrval = 1;
	return(0);
}

/*
 * get_phys()
 *	Determine the physical block size on a tape drive. We need the physical
 *	block size so we know how many bytes we skip over when we move with
 *	mtio commands. We also make sure we are BEFORE THE TAPE FILEMARK when
 *	return.
 *	This is one really SLOW routine...
 * Return:
 *	physical block size if ok (ok > 0), -1 otherwise
 */

static int
get_phys(void)
{
	int padsz = 0;
	int res;
	int phyblk;
	struct mtop mb;
	char scbuf[MAXBLK];

	/*
	 * move to the file mark, and then back up one record and read it.
	 * this should tell us the physical record size the tape is using.
	 */
	if (lstrval == 1) {
		/*
		 * we know we are at file mark when we get back a 0 from
		 * read()
		 */
		while ((res = read(arfd, scbuf, sizeof(scbuf))) > 0)
			padsz += res;
		if (res < 0) {
			syswarn(1, errno, "Unable to locate tape filemark.");
			return(-1);
		}
	}

	/*
	 * move backwards over the file mark so we are at the end of the
	 * last record.
	 */
	mb.mt_op = MTBSF;
	mb.mt_count = 1;
	if (ioctl(arfd, MTIOCTOP, &mb) < 0) {
		syswarn(1, errno, "Unable to backspace over tape filemark.");
		return(-1);
	}

	/*
	 * move backwards so we are in front of the last record and read it to
	 * get physical tape blocksize.
	 */
	mb.mt_op = MTBSR;
	mb.mt_count = 1;
	if (ioctl(arfd, MTIOCTOP, &mb) < 0) {
		syswarn(1, errno, "Unable to backspace over last tape block.");
		return(-1);
	}
	if ((phyblk = read(arfd, scbuf, sizeof(scbuf))) <= 0) {
		syswarn(1, errno, "Cannot determine archive tape blocksize.");
		return(-1);
	}

	/*
	 * read forward to the file mark, then back up in front of the filemark
	 * (this is a bit paranoid, but should be safe to do).
	 */
	while ((res = read(arfd, scbuf, sizeof(scbuf))) > 0)
		;
	if (res < 0) {
		syswarn(1, errno, "Unable to locate tape filemark.");
		return(-1);
	}
	mb.mt_op = MTBSF;
	mb.mt_count = 1;
	if (ioctl(arfd, MTIOCTOP, &mb) < 0) {
		syswarn(1, errno, "Unable to backspace over tape filemark.");
		return(-1);
	}

	/*
	 * set lstrval so we know that the filemark has not been seen
	 */
	lstrval = 1;

	/*
	 * return if there was no padding
	 */
	if (padsz == 0)
		return(phyblk);

	/*
	 * make sure we can move backwards over the padding. (this should
	 * never fail).
	 */
	if (padsz % phyblk) {
		paxwarn(1, "Tape drive unable to backspace requested amount");
		return(-1);
	}

	/*
	 * move backwards over the padding so the head is where it was when
	 * we were first called (if required).
	 */
	mb.mt_op = MTBSR;
	mb.mt_count = padsz/phyblk;
	if (ioctl(arfd, MTIOCTOP, &mb) < 0) {
		syswarn(1,errno,"Unable to backspace tape over %d pad blocks",
		    mb.mt_count);
		return(-1);
	}
	return(phyblk);
}

/*
 * ar_next()
 *	prompts the user for the next volume in this archive. For some devices
 *	we may allow the media to be changed. Otherwise a new archive is
 *	prompted for. By pax spec, if there is no controlling tty or an eof is
 *	read on tty input, we must quit pax.
 * Return:
 *	0 when ready to continue, -1 when all done
 */

int
ar_next(void)
{
	static char *arcbuf;
	char buf[PAXPATHLEN+2];
	sigset_t o_mask;

	/*
	 * WE MUST CLOSE THE DEVICE. A lot of devices must see last close, (so
	 * things like writing EOF etc will be done) (Watch out ar_close() can
	 * also be called via a signal handler, so we must prevent a race.
	 */
	if (sigprocmask(SIG_BLOCK, &s_mask, &o_mask) < 0)
		syswarn(0, errno, "Unable to set signal mask");
	ar_close();
	if (sigprocmask(SIG_SETMASK, &o_mask, NULL) < 0)
		syswarn(0, errno, "Unable to restore signal mask");

	if (done || !wr_trail || Oflag || strcmp(NM_TAR, argv0) == 0)
		return(-1);

	tty_prnt("\nATTENTION! %s archive volume change required.\n", argv0);

	/*
	 * if i/o is on stdin or stdout, we cannot reopen it (we do not know
	 * the name), the user will be forced to type it in.
	 */
	if (strcmp(arcname, stdo) && strcmp(arcname, stdn) && (artyp != ISREG)
	    && (artyp != ISPIPE)) {
		if (artyp == ISTAPE) {
			tty_prnt("%s ready for archive tape volume: %d\n",
				arcname, arvol);
			tty_prnt("Load the NEXT TAPE on the tape drive");
		} else {
			tty_prnt("%s ready for archive volume: %d\n",
				arcname, arvol);
			tty_prnt("Load the NEXT STORAGE MEDIA (if required)");
		}

		if ((act == ARCHIVE) || (act == APPND))
			tty_prnt(" and make sure it is WRITE ENABLED.\n");
		else
			tty_prnt("\n");

		for(;;) {
			tty_prnt("Type \"y\" to continue, \".\" to quit %s,",
				argv0);
			tty_prnt(" or \"s\" to switch to new device.\nIf you");
			tty_prnt(" cannot change storage media, type \"s\"\n");
			tty_prnt("Is the device ready and online? > ");

			if ((tty_read(buf,sizeof(buf))<0) || !strcmp(buf,".")){
				done = 1;
				lstrval = -1;
				tty_prnt("Quitting %s!\n", argv0);
				vfpart = 0;
				return(-1);
			}

			if ((buf[0] == '\0') || (buf[1] != '\0')) {
				tty_prnt("%s unknown command, try again\n",buf);
				continue;
			}

			switch (buf[0]) {
			case 'y':
			case 'Y':
				/*
				 * we are to continue with the same device
				 */
				if (ar_open(arcname) >= 0)
					return(0);
				tty_prnt("Cannot re-open %s, try again\n",
					arcname);
				continue;
			case 's':
			case 'S':
				/*
				 * user wants to open a different device
				 */
				tty_prnt("Switching to a different archive\n");
				break;
			default:
				tty_prnt("%s unknown command, try again\n",buf);
				continue;
			}
			break;
		}
	} else
		tty_prnt("Ready for archive volume: %d\n", arvol);

	/*
	 * have to go to a different archive
	 */
	for (;;) {
		tty_prnt("Input archive name or \".\" to quit %s.\n", argv0);
		tty_prnt("Archive name > ");

		if ((tty_read(buf, sizeof(buf)) < 0) || !strcmp(buf, ".")) {
			done = 1;
			lstrval = -1;
			tty_prnt("Quitting %s!\n", argv0);
			vfpart = 0;
			return(-1);
		}
		if (buf[0] == '\0') {
			tty_prnt("Empty file name, try again\n");
			continue;
		}
		if (!strcmp(buf, "..")) {
			tty_prnt("Illegal file name: .. try again\n");
			continue;
		}
		if (strlen(buf) > PAXPATHLEN) {
			tty_prnt("File name too long, try again\n");
			continue;
		}

		/*
		 * try to open new archive
		 */
		if (ar_open(buf) >= 0) {
			free(arcbuf);
			if ((arcbuf = strdup(buf)) == NULL) {
				done = 1;
				lstrval = -1;
				paxwarn(0, "Cannot save archive name.");
				return(-1);
			}
			arcname = arcbuf;
			break;
		}
		tty_prnt("Cannot open %s, try again\n", buf);
		continue;
	}
	return(0);
}

/*
 * ar_start_gzip()
 * starts the gzip compression/decompression process as a child, using magic
 * to keep the fd the same in the calling function (parent).
 */
void
ar_start_gzip(int fd, const char *gzip_prog, int wr)
{
	int fds[2];
	const char *gzip_flags;

	if (pipe(fds) < 0)
		err(1, "could not pipe");
	zpid = fork();
	if (zpid < 0)
		err(1, "could not fork");

	/* parent */
	if (zpid) {
		if (wr)
			dup2(fds[1], fd);
		else
			dup2(fds[0], fd);
		close(fds[0]);
		close(fds[1]);
	} else {
		if (wr) {
			dup2(fds[0], STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			gzip_flags = "-c";
		} else {
			dup2(fds[1], STDOUT_FILENO);
			dup2(fd, STDIN_FILENO);
			gzip_flags = "-dc";
		}
		close(fds[0]);
		close(fds[1]);
		if (execlp(gzip_prog, gzip_prog, gzip_flags,
		    (char *)NULL) < 0)
			err(1, "could not exec");
		/* NOTREACHED */
	}
}
