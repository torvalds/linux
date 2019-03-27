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
static char sccsid[] = "@(#)buf_subs.c	8.2 (Berkeley) 4/18/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "pax.h"
#include "extern.h"

/*
 * routines which implement archive and file buffering
 */

#define MINFBSZ		512		/* default block size for hole detect */
#define MAXFLT		10		/* default media read error limit */

/*
 * Need to change bufmem to dynamic allocation when the upper
 * limit on blocking size is removed (though that will violate pax spec)
 * MAXBLK define and tests will also need to be updated.
 */
static char bufmem[MAXBLK+BLKMULT];	/* i/o buffer + pushback id space */
static char *buf;			/* normal start of i/o buffer */
static char *bufend;			/* end or last char in i/o buffer */
static char *bufpt;			/* read/write point in i/o buffer */
int blksz = MAXBLK;			/* block input/output size in bytes */
int wrblksz;				/* user spec output size in bytes */
int maxflt = MAXFLT;			/* MAX consecutive media errors */
int rdblksz;				/* first read blksize (tapes only) */
off_t wrlimit;				/* # of bytes written per archive vol */
off_t wrcnt;				/* # of bytes written on current vol */
off_t rdcnt;				/* # of bytes read on current vol */

/*
 * wr_start()
 *	set up the buffering system to operate in a write mode
 * Return:
 *	0 if ok, -1 if the user specified write block size violates pax spec
 */

int
wr_start(void)
{
	buf = &(bufmem[BLKMULT]);
	/*
	 * Check to make sure the write block size meets pax specs. If the user
	 * does not specify a blocksize, we use the format default blocksize.
	 * We must be picky on writes, so we do not allow the user to create an
	 * archive that might be hard to read elsewhere. If all ok, we then
	 * open the first archive volume
	 */
	if (!wrblksz)
		wrblksz = frmt->bsz;
	if (wrblksz > MAXBLK) {
		paxwarn(1, "Write block size of %d too large, maximum is: %d",
			wrblksz, MAXBLK);
		return(-1);
	}
	if (wrblksz % BLKMULT) {
		paxwarn(1, "Write block size of %d is not a %d byte multiple",
		    wrblksz, BLKMULT);
		return(-1);
	}
	if (wrblksz > MAXBLK_POSIX) {
		paxwarn(0, "Write block size of %d larger than POSIX max %d, archive may not be portable",
			wrblksz, MAXBLK_POSIX);
		return(-1);
	}

	/*
	 * we only allow wrblksz to be used with all archive operations
	 */
	blksz = rdblksz = wrblksz;
	if ((ar_open(arcname) < 0) && (ar_next() < 0))
		return(-1);
	wrcnt = 0;
	bufend = buf + wrblksz;
	bufpt = buf;
	return(0);
}

/*
 * rd_start()
 *	set up buffering system to read an archive
 * Return:
 *	0 if ok, -1 otherwise
 */

int
rd_start(void)
{
	/*
	 * leave space for the header pushback (see get_arc()). If we are
	 * going to append and user specified a write block size, check it
	 * right away
	 */
	buf = &(bufmem[BLKMULT]);
	if ((act == APPND) && wrblksz) {
		if (wrblksz > MAXBLK) {
			paxwarn(1,"Write block size %d too large, maximum is: %d",
				wrblksz, MAXBLK);
			return(-1);
		}
		if (wrblksz % BLKMULT) {
			paxwarn(1, "Write block size %d is not a %d byte multiple",
			wrblksz, BLKMULT);
			return(-1);
		}
	}

	/*
	 * open the archive
	 */
	if ((ar_open(arcname) < 0) && (ar_next() < 0))
		return(-1);
	bufend = buf + rdblksz;
	bufpt = bufend;
	rdcnt = 0;
	return(0);
}

/*
 * cp_start()
 *	set up buffer system for copying within the file system
 */

void
cp_start(void)
{
	buf = &(bufmem[BLKMULT]);
	rdblksz = blksz = MAXBLK;
}

/*
 * appnd_start()
 *	Set up the buffering system to append new members to an archive that
 *	was just read. The last block(s) of an archive may contain a format
 *	specific trailer. To append a new member, this trailer has to be
 *	removed from the archive. The first byte of the trailer is replaced by
 *	the start of the header of the first file added to the archive. The
 *	format specific end read function tells us how many bytes to move
 *	backwards in the archive to be positioned BEFORE the trailer. Two
 *	different positions have to be adjusted, the O.S. file offset (e.g. the
 *	position of the tape head) and the write point within the data we have
 *	stored in the read (soon to become write) buffer. We may have to move
 *	back several records (the number depends on the size of the archive
 *	record and the size of the format trailer) to read up the record where
 *	the first byte of the trailer is recorded. Trailers may span (and
 *	overlap) record boundaries.
 *	We first calculate which record has the first byte of the trailer. We
 *	move the OS file offset back to the start of this record and read it
 *	up. We set the buffer write pointer to be at this byte (the byte where
 *	the trailer starts). We then move the OS file pointer back to the
 *	start of this record so a flush of this buffer will replace the record
 *	in the archive.
 *	A major problem is rewriting this last record. For archives stored
 *	on disk files, this is trivial. However, many devices are really picky
 *	about the conditions under which they will allow a write to occur.
 *	Often devices restrict the conditions where writes can be made writes,
 *	so it may not be feasible to append archives stored on all types of
 *	devices.
 * Return:
 *	0 for success, -1 for failure
 */

int
appnd_start(off_t skcnt)
{
	int res;
	off_t cnt;

	if (exit_val != 0) {
		paxwarn(0, "Cannot append to an archive that may have flaws.");
		return(-1);
	}
	/*
	 * if the user did not specify a write blocksize, inherit the size used
	 * in the last archive volume read. (If a is set we still use rdblksz
	 * until next volume, cannot shift sizes within a single volume).
	 */
	if (!wrblksz)
		wrblksz = blksz = rdblksz;
	else
		blksz = rdblksz;

	/*
	 * make sure that this volume allows appends
	 */
	if (ar_app_ok() < 0)
		return(-1);

	/*
	 * Calculate bytes to move back and move in front of record where we
	 * need to start writing from. Remember we have to add in any padding
	 * that might be in the buffer after the trailer in the last block. We
	 * travel skcnt + padding ROUNDED UP to blksize.
	 */
	skcnt += bufend - bufpt;
	if ((cnt = (skcnt/blksz) * blksz) < skcnt)
		cnt += blksz;
	if (ar_rev((off_t)cnt) < 0)
		goto out;

	/*
	 * We may have gone too far if there is valid data in the block we are
	 * now in front of, read up the block and position the pointer after
	 * the valid data.
	 */
	if ((cnt -= skcnt) > 0) {
		/*
		 * watch out for stupid tape drives. ar_rev() will set rdblksz
		 * to be real physical blocksize so we must loop until we get
		 * the old rdblksz (now in blksz). If ar_rev() fouls up the
		 * determination of the physical block size, we will fail.
		 */
		bufpt = buf;
		bufend = buf + blksz;
		while (bufpt < bufend) {
			if ((res = ar_read(bufpt, rdblksz)) <= 0)
				goto out;
			bufpt += res;
		}
		if (ar_rev((off_t)(bufpt - buf)) < 0)
			goto out;
		bufpt = buf + cnt;
		bufend = buf + blksz;
	} else {
		/*
		 * buffer is empty
		 */
		bufend = buf + blksz;
		bufpt = buf;
	}
	rdblksz = blksz;
	rdcnt -= skcnt;
	wrcnt = 0;

	/*
	 * At this point we are ready to write. If the device requires special
	 * handling to write at a point were previously recorded data resides,
	 * that is handled in ar_set_wr(). From now on we operate under normal
	 * ARCHIVE mode (write) conditions
	 */
	if (ar_set_wr() < 0)
		return(-1);
	act = ARCHIVE;
	return(0);

    out:
	paxwarn(1, "Unable to rewrite archive trailer, cannot append.");
	return(-1);
}
	
/*
 * rd_sync()
 *	A read error occurred on this archive volume. Resync the buffer and
 *	try to reset the device (if possible) so we can continue to read. Keep
 *	trying to do this until we get a valid read, or we reach the limit on
 *	consecutive read faults (at which point we give up). The user can
 *	adjust the read error limit through a command line option.
 * Returns:
 *	0 on success, and -1 on failure
 */

int
rd_sync(void)
{
	int errcnt = 0;
	int res;

	/*
	 * if the user says bail out on first fault, we are out of here...
	 */
	if (maxflt == 0)
		return(-1);
	if (act == APPND) {
		paxwarn(1, "Unable to append when there are archive read errors.");
		return(-1);
	}

	/*
	 * poke at device and try to get past media error
	 */
	if (ar_rdsync() < 0) {
		if (ar_next() < 0)
			return(-1);
		else
			rdcnt = 0;
	}

	for (;;) {
		if ((res = ar_read(buf, blksz)) > 0) {
			/*
			 * All right! got some data, fill that buffer
			 */
			bufpt = buf;
			bufend = buf + res;
			rdcnt += res;
			return(0);
		}

		/*
		 * Oh well, yet another failed read...
		 * if error limit reached, ditch. o.w. poke device to move past
		 * bad media and try again. if media is badly damaged, we ask
		 * the poor (and upset user at this point) for the next archive
		 * volume. remember the goal on reads is to get the most we
		 * can extract out of the archive.
		 */
		if ((maxflt > 0) && (++errcnt > maxflt))
			paxwarn(0,"Archive read error limit (%d) reached",maxflt);
		else if (ar_rdsync() == 0)
			continue;
		if (ar_next() < 0)
			break;
		rdcnt = 0;
		errcnt = 0;
	}
	return(-1);
}

/*
 * pback()
 *	push the data used during the archive id phase back into the I/O
 *	buffer. This is required as we cannot be sure that the header does NOT
 *	overlap a block boundary (as in the case we are trying to recover a
 *	flawed archived). This was not designed to be used for any other
 *	purpose. (What software engineering, HA!)
 *	WARNING: do not even THINK of pback greater than BLKMULT, unless the
 *	pback space is increased.
 */

void
pback(char *pt, int cnt)
{
	bufpt -= cnt;
	memcpy(bufpt, pt, cnt);
	return;
}

/*
 * rd_skip()
 *	skip forward in the archive during an archive read. Used to get quickly
 *	past file data and padding for files the user did NOT select.
 * Return:
 *	0 if ok, -1 failure, and 1 when EOF on the archive volume was detected.
 */

int
rd_skip(off_t skcnt)
{
	off_t res;
	off_t cnt;
	off_t skipped = 0;

	/*
	 * consume what data we have in the buffer. If we have to move forward
	 * whole records, we call the low level skip function to see if we can
	 * move within the archive without doing the expensive reads on data we
	 * do not want.
	 */
	if (skcnt == 0)
		return(0);
	res = MIN((bufend - bufpt), skcnt);
	bufpt += res;
	skcnt -= res;

	/*
	 * if skcnt is now 0, then no additional i/o is needed
	 */
	if (skcnt == 0)
		return(0);

	/*
	 * We have to read more, calculate complete and partial record reads
	 * based on rdblksz. we skip over "cnt" complete records
	 */
	res = skcnt%rdblksz;
	cnt = (skcnt/rdblksz) * rdblksz;

	/*
	 * if the skip fails, we will have to resync. ar_fow will tell us
	 * how much it can skip over. We will have to read the rest.
	 */
	if (ar_fow(cnt, &skipped) < 0)
		return(-1);
	res += cnt - skipped;
	rdcnt += skipped;

	/*
	 * what is left we have to read (which may be the whole thing if
	 * ar_fow() told us the device can only read to skip records);
	 */
	while (res > 0L) {
		cnt = bufend - bufpt;
		/*
		 * if the read fails, we will have to resync
		 */
		if ((cnt <= 0) && ((cnt = buf_fill()) < 0))
			return(-1);
		if (cnt == 0)
			return(1);
		cnt = MIN(cnt, res);
		bufpt += cnt;
		res -= cnt;
	}
	return(0);
}

/*
 * wr_fin()
 *	flush out any data (and pad if required) the last block. We always pad
 *	with zero (even though we do not have to). Padding with 0 makes it a
 *	lot easier to recover if the archive is damaged. zero padding SHOULD
 *	BE a requirement....
 */

void
wr_fin(void)
{
	if (bufpt > buf) {
		memset(bufpt, 0, bufend - bufpt);
		bufpt = bufend;
		(void)buf_flush(blksz);
	}
}

/*
 * wr_rdbuf()
 *	fill the write buffer from data passed to it in a buffer (usually used
 *	by format specific write routines to pass a file header). On failure we
 *	punt. We do not allow the user to continue to write flawed archives.
 *	We assume these headers are not very large (the memory copy we use is
 *	a bit expensive).
 * Return:
 *	0 if buffer was filled ok, -1 o.w. (buffer flush failure)
 */

int
wr_rdbuf(char *out, int outcnt)
{
	int cnt;

	/*
	 * while there is data to copy into the write buffer. when the
	 * write buffer fills, flush it to the archive and continue
	 */
	while (outcnt > 0) {
		cnt = bufend - bufpt;
		if ((cnt <= 0) && ((cnt = buf_flush(blksz)) < 0))
			return(-1);
		/*
		 * only move what we have space for
		 */
		cnt = MIN(cnt, outcnt);
		memcpy(bufpt, out, cnt);
		bufpt += cnt;
		out += cnt;
		outcnt -= cnt;
	}
	return(0);
}

/*
 * rd_wrbuf()
 *	copy from the read buffer into a supplied buffer a specified number of
 *	bytes. If the read buffer is empty fill it and continue to copy.
 *	usually used to obtain a file header for processing by a format
 *	specific read routine.
 * Return
 *	number of bytes copied to the buffer, 0 indicates EOF on archive volume,
 *	-1 is a read error
 */

int
rd_wrbuf(char *in, int cpcnt)
{
	int res;
	int cnt;
	int incnt = cpcnt;

	/*
	 * loop until we fill the buffer with the requested number of bytes
	 */
	while (incnt > 0) {
		cnt = bufend - bufpt;
		if ((cnt <= 0) && ((cnt = buf_fill()) <= 0)) {
			/*
			 * read error, return what we got (or the error if
			 * no data was copied). The caller must know that an
			 * error occurred and has the best knowledge what to
			 * do with it
			 */
			if ((res = cpcnt - incnt) > 0)
				return(res);
			return(cnt);
		}

		/*
		 * calculate how much data to copy based on whats left and
		 * state of buffer
		 */
		cnt = MIN(cnt, incnt);
		memcpy(in, bufpt, cnt);
		bufpt += cnt;
		incnt -= cnt;
		in += cnt;
	}
	return(cpcnt);
}

/*
 * wr_skip()
 *	skip forward during a write. In other words add padding to the file.
 *	we add zero filled padding as it makes flawed archives much easier to
 *	recover from. the caller tells us how many bytes of padding to add
 *	This routine was not designed to add HUGE amount of padding, just small
 *	amounts (a few 512 byte blocks at most)
 * Return:
 *	0 if ok, -1 if there was a buf_flush failure
 */

int
wr_skip(off_t skcnt)
{
	int cnt;

	/*
	 * loop while there is more padding to add
	 */
	while (skcnt > 0L) {
		cnt = bufend - bufpt;
		if ((cnt <= 0) && ((cnt = buf_flush(blksz)) < 0))
			return(-1);
		cnt = MIN(cnt, skcnt);
		memset(bufpt, 0, cnt);
		bufpt += cnt;
		skcnt -= cnt;
	}
	return(0);
}

/*
 * wr_rdfile()
 *	fill write buffer with the contents of a file. We are passed an open
 *	file descriptor to the file and the archive structure that describes the
 *	file we are storing. The variable "left" is modified to contain the
 *	number of bytes of the file we were NOT able to write to the archive.
 *	it is important that we always write EXACTLY the number of bytes that
 *	the format specific write routine told us to. The file can also get
 *	bigger, so reading to the end of file would create an improper archive,
 *	we just detect this case and warn the user. We never create a bad
 *	archive if we can avoid it. Of course trying to archive files that are
 *	active is asking for trouble. It we fail, we pass back how much we
 *	could NOT copy and let the caller deal with it.
 * Return:
 *	0 ok, -1 if archive write failure. a short read of the file returns a
 *	0, but "left" is set to be greater than zero.
 */

int
wr_rdfile(ARCHD *arcn, int ifd, off_t *left)
{
	int cnt;
	int res = 0;
	off_t size = arcn->sb.st_size;
	struct stat sb;

	/*
	 * while there are more bytes to write
	 */
	while (size > 0L) {
		cnt = bufend - bufpt;
		if ((cnt <= 0) && ((cnt = buf_flush(blksz)) < 0)) {
			*left = size;
			return(-1);
		}
		cnt = MIN(cnt, size);
		if ((res = read(ifd, bufpt, cnt)) <= 0)
			break;
		size -= res;
		bufpt += res;
	}

	/*
	 * better check the file did not change during this operation
	 * or the file read failed.
	 */
	if (res < 0)
		syswarn(1, errno, "Read fault on %s", arcn->org_name);
	else if (size != 0L)
		paxwarn(1, "File changed size during read %s", arcn->org_name);
	else if (fstat(ifd, &sb) < 0)
		syswarn(1, errno, "Failed stat on %s", arcn->org_name);
	else if (arcn->sb.st_mtime != sb.st_mtime)
		paxwarn(1, "File %s was modified during copy to archive",
			arcn->org_name);
	*left = size;
	return(0);
}

/*
 * rd_wrfile()
 *	extract the contents of a file from the archive. If we are unable to
 *	extract the entire file (due to failure to write the file) we return
 *	the numbers of bytes we did NOT process. This way the caller knows how
 *	many bytes to skip past to find the next archive header. If the failure
 *	was due to an archive read, we will catch that when we try to skip. If
 *	the format supplies a file data crc value, we calculate the actual crc
 *	so that it can be compared to the value stored in the header
 * NOTE:
 *	We call a special function to write the file. This function attempts to
 *	restore file holes (blocks of zeros) into the file. When files are
 *	sparse this saves space, and is a LOT faster. For non sparse files
 *	the performance hit is small. As of this writing, no archive supports
 *	information on where the file holes are.
 * Return:
 *	0 ok, -1 if archive read failure. if we cannot write the entire file,
 *	we return a 0 but "left" is set to be the amount unwritten
 */

int
rd_wrfile(ARCHD *arcn, int ofd, off_t *left)
{
	int cnt = 0;
	off_t size = arcn->sb.st_size;
	int res = 0;
	char *fnm = arcn->name;
	int isem = 1;
	int rem;
	int sz = MINFBSZ;
	struct stat sb;
	u_long crc = 0L;

	/*
	 * pass the blocksize of the file being written to the write routine,
	 * if the size is zero, use the default MINFBSZ
	 */
	if (fstat(ofd, &sb) == 0) {
		if (sb.st_blksize > 0)
			sz = (int)sb.st_blksize;
	} else
		syswarn(0,errno,"Unable to obtain block size for file %s",fnm);
	rem = sz;
	*left = 0L;

	/*
	 * Copy the archive to the file the number of bytes specified. We have
	 * to assume that we want to recover file holes as none of the archive
	 * formats can record the location of file holes.
	 */
	while (size > 0L) {
		cnt = bufend - bufpt;
		/*
		 * if we get a read error, we do not want to skip, as we may
		 * miss a header, so we do not set left, but if we get a write
		 * error, we do want to skip over the unprocessed data.
		 */
		if ((cnt <= 0) && ((cnt = buf_fill()) <= 0))
			break;
		cnt = MIN(cnt, size);
		if ((res = file_write(ofd,bufpt,cnt,&rem,&isem,sz,fnm)) <= 0) {
			*left = size;
			break;
		}

		if (docrc) {
			/*
			 * update the actual crc value
			 */
			cnt = res;
			while (--cnt >= 0)
				crc += *bufpt++ & 0xff;
		} else
			bufpt += res;
		size -= res;
	}

	/*
	 * if the last block has a file hole (all zero), we must make sure this
	 * gets updated in the file. We force the last block of zeros to be
	 * written. just closing with the file offset moved forward may not put
	 * a hole at the end of the file.
	 */
	if (isem && (arcn->sb.st_size > 0L))
		file_flush(ofd, fnm, isem);

	/*
	 * if we failed from archive read, we do not want to skip
	 */
	if ((size > 0L) && (*left == 0L))
		return(-1);

	/*
	 * some formats record a crc on file data. If so, then we compare the
	 * calculated crc to the crc stored in the archive
	 */
	if (docrc && (size == 0L) && (arcn->crc != crc))
		paxwarn(1,"Actual crc does not match expected crc %s",arcn->name);
	return(0);
}

/*
 * cp_file()
 *	copy the contents of one file to another. used during -rw phase of pax
 *	just as in rd_wrfile() we use a special write function to write the
 *	destination file so we can properly copy files with holes.
 */

void
cp_file(ARCHD *arcn, int fd1, int fd2)
{
	int cnt;
	off_t cpcnt = 0L;
	int res = 0;
	char *fnm = arcn->name;
	int no_hole = 0;
	int isem = 1;
	int rem;
	int sz = MINFBSZ;
	struct stat sb;

	/*
	 * check for holes in the source file. If none, we will use regular
	 * write instead of file write.
	 */
	 if (((off_t)(arcn->sb.st_blocks * BLKMULT)) >= arcn->sb.st_size)
		++no_hole;

	/*
	 * pass the blocksize of the file being written to the write routine,
	 * if the size is zero, use the default MINFBSZ
	 */
	if (fstat(fd2, &sb) == 0) {
		if (sb.st_blksize > 0)
			sz = sb.st_blksize;
	} else
		syswarn(0,errno,"Unable to obtain block size for file %s",fnm);
	rem = sz;

	/*
	 * read the source file and copy to destination file until EOF
	 */
	for(;;) {
		if ((cnt = read(fd1, buf, blksz)) <= 0)
			break;
		if (no_hole)
			res = write(fd2, buf, cnt);
		else
			res = file_write(fd2, buf, cnt, &rem, &isem, sz, fnm);
		if (res != cnt)
			break;
		cpcnt += cnt;
	}

	/*
	 * check to make sure the copy is valid.
	 */
	if (res < 0)
		syswarn(1, errno, "Failed write during copy of %s to %s",
			arcn->org_name, arcn->name);
	else if (cpcnt != arcn->sb.st_size)
		paxwarn(1, "File %s changed size during copy to %s",
			arcn->org_name, arcn->name);
	else if (fstat(fd1, &sb) < 0)
		syswarn(1, errno, "Failed stat of %s", arcn->org_name);
	else if (arcn->sb.st_mtime != sb.st_mtime)
		paxwarn(1, "File %s was modified during copy to %s",
			arcn->org_name, arcn->name);

	/*
	 * if the last block has a file hole (all zero), we must make sure this
	 * gets updated in the file. We force the last block of zeros to be
	 * written. just closing with the file offset moved forward may not put
	 * a hole at the end of the file.
	 */
	if (!no_hole && isem && (arcn->sb.st_size > 0L))
		file_flush(fd2, fnm, isem);
	return;
}

/*
 * buf_fill()
 *	fill the read buffer with the next record (or what we can get) from
 *	the archive volume.
 * Return:
 *	Number of bytes of data in the read buffer, -1 for read error, and
 *	0 when finished (user specified termination in ar_next()).
 */

int
buf_fill(void)
{
	int cnt;
	static int fini = 0;

	if (fini)
		return(0);

	for(;;) {
		/*
		 * try to fill the buffer. on error the next archive volume is
		 * opened and we try again.
		 */
		if ((cnt = ar_read(buf, blksz)) > 0) {
			bufpt = buf;
			bufend = buf + cnt;
			rdcnt += cnt;
			return(cnt);
		}

		/*
		 * errors require resync, EOF goes to next archive
		 * but in case we have not determined yet the format,
		 * this means that we have a very short file, so we
		 * are done again.
		 */
		if (cnt < 0)
			break;
		if (frmt == NULL || ar_next() < 0) {
			fini = 1;
			return(0);
		}
		rdcnt = 0;
	}
	exit_val = 1;
	return(-1);
}

/*
 * buf_flush()
 *	force the write buffer to the archive. We are passed the number of
 *	bytes in the buffer at the point of the flush. When we change archives
 *	the record size might change. (either larger or smaller).
 * Return:
 *	0 if all is ok, -1 when a write error occurs.
 */

int
buf_flush(int bufcnt)
{
	int cnt;
	int push = 0;
	int totcnt = 0;

	/*
	 * if we have reached the user specified byte count for each archive
	 * volume, prompt for the next volume. (The non-standard -R flag).
	 * NOTE: If the wrlimit is smaller than wrcnt, we will always write
	 * at least one record. We always round limit UP to next blocksize.
	 */
	if ((wrlimit > 0) && (wrcnt > wrlimit)) {
		paxwarn(0, "User specified archive volume byte limit reached.");
		if (ar_next() < 0) {
			wrcnt = 0;
			exit_val = 1;
			return(-1);
		}
		wrcnt = 0;

		/*
		 * The new archive volume might have changed the size of the
		 * write blocksize. if so we figure out if we need to write
		 * (one or more times), or if there is now free space left in
		 * the buffer (it is no longer full). bufcnt has the number of
		 * bytes in the buffer, (the blocksize, at the point we were
		 * CALLED). Push has the amount of "extra" data in the buffer
		 * if the block size has shrunk from a volume change.
		 */
		bufend = buf + blksz;
		if (blksz > bufcnt)
			return(0);
		if (blksz < bufcnt)
			push = bufcnt - blksz;
	}

	/*
	 * We have enough data to write at least one archive block
	 */
	for (;;) {
		/*
		 * write a block and check if it all went out ok
		 */
		cnt = ar_write(buf, blksz);
		if (cnt == blksz) {
			/*
			 * the write went ok
			 */
			wrcnt += cnt;
			totcnt += cnt;
			if (push > 0) {
				/* we have extra data to push to the front.
				 * check for more than 1 block of push, and if
				 * so we loop back to write again
				 */
				memcpy(buf, bufend, push);
				bufpt = buf + push;
				if (push >= blksz) {
					push -= blksz;
					continue;
				}
			} else
				bufpt = buf;
			return(totcnt);
		} else if (cnt > 0) {
			/*
			 * Oh drat we got a partial write!
			 * if format doesn't care about alignment let it go,
			 * we warned the user in ar_write().... but this means
			 * the last record on this volume violates pax spec....
			 */
			totcnt += cnt;
			wrcnt += cnt;
			bufpt = buf + cnt;
			cnt = bufcnt - cnt;
			memcpy(buf, bufpt, cnt);
			bufpt = buf + cnt;
			if (!frmt->blkalgn || ((cnt % frmt->blkalgn) == 0))
				return(totcnt);
			break;
		}

		/*
		 * All done, go to next archive
		 */
		wrcnt = 0;
		if (ar_next() < 0)
			break;

		/*
		 * The new archive volume might also have changed the block
		 * size. if so, figure out if we have too much or too little
		 * data for using the new block size
		 */
		bufend = buf + blksz;
		if (blksz > bufcnt)
			return(0);
		if (blksz < bufcnt)
			push = bufcnt - blksz;
	}

	/*
	 * write failed, stop pax. we must not create a bad archive!
	 */
	exit_val = 1;
	return(-1);
}
