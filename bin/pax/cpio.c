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
static char sccsid[] = "@(#)cpio.c	8.1 (Berkeley) 5/31/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "pax.h"
#include "cpio.h"
#include "extern.h"

static int rd_nm(ARCHD *, int);
static int rd_ln_nm(ARCHD *);
static int com_rd(ARCHD *);

/*
 * Routines which support the different cpio versions
 */

static int swp_head;		/* binary cpio header byte swap */

/*
 * Routines common to all versions of cpio
 */

/*
 * cpio_strd()
 *	Fire up the hard link detection code
 * Return:
 *      0 if ok -1 otherwise (the return values of lnk_start())
 */

int
cpio_strd(void)
{
	return(lnk_start());
}

/*
 * cpio_trail()
 *	Called to determine if a header block is a valid trailer. We are
 *	passed the block, the in_sync flag (which tells us we are in resync
 *	mode; looking for a valid header), and cnt (which starts at zero)
 *	which is used to count the number of empty blocks we have seen so far.
 * Return:
 *	0 if a valid trailer, -1 if not a valid trailer,
 */

int
cpio_trail(ARCHD *arcn)
{
	/*
	 * look for trailer id in file we are about to process
	 */
	if ((strcmp(arcn->name, TRAILER) == 0) && (arcn->sb.st_size == 0))
		return(0);
	return(-1);
}

/*
 * com_rd()
 *	operations common to all cpio read functions.
 * Return:
 *	0
 */

static int
com_rd(ARCHD *arcn)
{
	arcn->skip = 0;
	arcn->pat = NULL;
	arcn->org_name = arcn->name;
	switch(arcn->sb.st_mode & C_IFMT) {
	case C_ISFIFO:
		arcn->type = PAX_FIF;
		break;
	case C_ISDIR:
		arcn->type = PAX_DIR;
		break;
	case C_ISBLK:
		arcn->type = PAX_BLK;
		break;
	case C_ISCHR:
		arcn->type = PAX_CHR;
		break;
	case C_ISLNK:
		arcn->type = PAX_SLK;
		break;
	case C_ISOCK:
		arcn->type = PAX_SCK;
		break;
	case C_ISCTG:
	case C_ISREG:
	default:
		/*
		 * we have file data, set up skip (pad is set in the format
		 * specific sections)
		 */
		arcn->sb.st_mode = (arcn->sb.st_mode & 0xfff) | C_ISREG;
		arcn->type = PAX_REG;
		arcn->skip = arcn->sb.st_size;
		break;
	}
	if (chk_lnk(arcn) < 0)
		return(-1);
	return(0);
}

/*
 * cpio_end_wr()
 *	write the special file with the name trailer in the proper format
 * Return:
 *	result of the write of the trailer from the cpio specific write func
 */

int
cpio_endwr(void)
{
	ARCHD last;

	/*
	 * create a trailer request and call the proper format write function
	 */
	memset(&last, 0, sizeof(last));
	last.nlen = sizeof(TRAILER) - 1;
	last.type = PAX_REG;
	last.sb.st_nlink = 1;
	(void)strcpy(last.name, TRAILER);
	return((*frmt->wr)(&last));
}

/*
 * rd_nam()
 *	read in the file name which follows the cpio header
 * Return:
 *	0 if ok, -1 otherwise
 */

static int
rd_nm(ARCHD *arcn, int nsz)
{
	/*
	 * do not even try bogus values
	 */
	if ((nsz == 0) || (nsz > (int)sizeof(arcn->name))) {
		paxwarn(1, "Cpio file name length %d is out of range", nsz);
		return(-1);
	}

	/*
	 * read the name and make sure it is not empty and is \0 terminated
	 */
	if ((rd_wrbuf(arcn->name,nsz) != nsz) || (arcn->name[nsz-1] != '\0') ||
	    (arcn->name[0] == '\0')) {
		paxwarn(1, "Cpio file name in header is corrupted");
		return(-1);
	}
	return(0);
}

/*
 * rd_ln_nm()
 *	read in the link name for a file with links. The link name is stored
 *	like file data (and is NOT \0 terminated!)
 * Return:
 *	0 if ok, -1 otherwise
 */

static int
rd_ln_nm(ARCHD *arcn)
{
	/*
	 * check the length specified for bogus values
	 */
	if ((arcn->sb.st_size == 0) ||
	    ((size_t)arcn->sb.st_size >= sizeof(arcn->ln_name))) {
#		ifdef NET2_STAT
		paxwarn(1, "Cpio link name length is invalid: %lu",
		    arcn->sb.st_size);
#		else
		paxwarn(1, "Cpio link name length is invalid: %ju",
		    (uintmax_t)arcn->sb.st_size);
#		endif
		return(-1);
	}

	/*
	 * read in the link name and \0 terminate it
	 */
	if (rd_wrbuf(arcn->ln_name, (int)arcn->sb.st_size) !=
	    (int)arcn->sb.st_size) {
		paxwarn(1, "Cpio link name read error");
		return(-1);
	}
	arcn->ln_nlen = arcn->sb.st_size;
	arcn->ln_name[arcn->ln_nlen] = '\0';

	/*
	 * watch out for those empty link names
	 */
	if (arcn->ln_name[0] == '\0') {
		paxwarn(1, "Cpio link name is corrupt");
		return(-1);
	}
	return(0);
}

/*
 * Routines common to the extended byte oriented cpio format
 */

/*
 * cpio_id()
 *      determine if a block given to us is a valid extended byte oriented
 *	cpio header
 * Return:
 *      0 if a valid header, -1 otherwise
 */

int
cpio_id(char *blk, int size)
{
	if ((size < (int)sizeof(HD_CPIO)) ||
	    (strncmp(blk, AMAGIC, sizeof(AMAGIC) - 1) != 0))
		return(-1);
	return(0);
}

/*
 * cpio_rd()
 *	determine if a buffer is a byte oriented extended cpio archive entry.
 *	convert and store the values in the ARCHD parameter.
 * Return:
 *	0 if a valid header, -1 otherwise.
 */

int
cpio_rd(ARCHD *arcn, char *buf)
{
	int nsz;
	HD_CPIO *hd;

	/*
	 * check that this is a valid header, if not return -1
	 */
	if (cpio_id(buf, sizeof(HD_CPIO)) < 0)
		return(-1);
	hd = (HD_CPIO *)buf;

	/*
	 * byte oriented cpio (posix) does not have padding! extract the octal
	 * ascii fields from the header
	 */
	arcn->pad = 0L;
	arcn->sb.st_dev = (dev_t)asc_ul(hd->c_dev, sizeof(hd->c_dev), OCT);
	arcn->sb.st_ino = (ino_t)asc_ul(hd->c_ino, sizeof(hd->c_ino), OCT);
	arcn->sb.st_mode = (mode_t)asc_ul(hd->c_mode, sizeof(hd->c_mode), OCT);
	arcn->sb.st_uid = (uid_t)asc_ul(hd->c_uid, sizeof(hd->c_uid), OCT);
	arcn->sb.st_gid = (gid_t)asc_ul(hd->c_gid, sizeof(hd->c_gid), OCT);
	arcn->sb.st_nlink = (nlink_t)asc_ul(hd->c_nlink, sizeof(hd->c_nlink),
	    OCT);
	arcn->sb.st_rdev = (dev_t)asc_ul(hd->c_rdev, sizeof(hd->c_rdev), OCT);
#ifdef NET2_STAT
	arcn->sb.st_mtime = (time_t)asc_ul(hd->c_mtime, sizeof(hd->c_mtime),
	    OCT);
#else
	arcn->sb.st_mtime = (time_t)asc_uqd(hd->c_mtime, sizeof(hd->c_mtime),
	    OCT);
#endif
	arcn->sb.st_ctime = arcn->sb.st_atime = arcn->sb.st_mtime;
#ifdef NET2_STAT
	arcn->sb.st_size = (off_t)asc_ul(hd->c_filesize,sizeof(hd->c_filesize),
	    OCT);
#else
	arcn->sb.st_size = (off_t)asc_uqd(hd->c_filesize,sizeof(hd->c_filesize),
	    OCT);
#endif

	/*
	 * check name size and if valid, read in the name of this entry (name
	 * follows header in the archive)
	 */
	if ((nsz = (int)asc_ul(hd->c_namesize,sizeof(hd->c_namesize),OCT)) < 2)
		return(-1);
	arcn->nlen = nsz - 1;
	if (rd_nm(arcn, nsz) < 0)
		return(-1);

	if (((arcn->sb.st_mode&C_IFMT) != C_ISLNK)||(arcn->sb.st_size == 0)) {
		/*
	 	 * no link name to read for this file
	 	 */
		arcn->ln_nlen = 0;
		arcn->ln_name[0] = '\0';
		return(com_rd(arcn));
	}

	/*
	 * check link name size and read in the link name. Link names are
	 * stored like file data.
	 */
	if (rd_ln_nm(arcn) < 0)
		return(-1);

	/*
	 * we have a valid header (with a link)
	 */
	return(com_rd(arcn));
}

/*
 * cpio_endrd()
 *      no cleanup needed here, just return size of the trailer (for append)
 * Return:
 *      size of trailer header in this format
 */

off_t
cpio_endrd(void)
{
	return((off_t)(sizeof(HD_CPIO) + sizeof(TRAILER)));
}

/*
 * cpio_stwr()
 *	start up the device mapping table
 * Return:
 *	0 if ok, -1 otherwise (what dev_start() returns)
 */

int
cpio_stwr(void)
{
	return(dev_start());
}

/*
 * cpio_wr()
 *	copy the data in the ARCHD to buffer in extended byte oriented cpio
 *	format.
 * Return
 *      0 if file has data to be written after the header, 1 if file has NO
 *	data to write after the header, -1 if archive write failed
 */

int
cpio_wr(ARCHD *arcn)
{
	HD_CPIO *hd;
	int nsz;
	HD_CPIO hdblk;

	/*
	 * check and repair truncated device and inode fields in the header
	 */
	if (map_dev(arcn, (u_long)CPIO_MASK, (u_long)CPIO_MASK) < 0)
		return(-1);

	arcn->pad = 0L;
	nsz = arcn->nlen + 1;
	hd = &hdblk;
	if ((arcn->type != PAX_BLK) && (arcn->type != PAX_CHR))
		arcn->sb.st_rdev = 0;

	switch(arcn->type) {
	case PAX_CTG:
	case PAX_REG:
	case PAX_HRG:
		/*
		 * set data size for file data
		 */
#		ifdef NET2_STAT
		if (ul_asc((u_long)arcn->sb.st_size, hd->c_filesize,
		    sizeof(hd->c_filesize), OCT)) {
#		else
		if (uqd_asc((u_quad_t)arcn->sb.st_size, hd->c_filesize,
		    sizeof(hd->c_filesize), OCT)) {
#		endif
			paxwarn(1,"File is too large for cpio format %s",
			    arcn->org_name);
			return(1);
		}
		break;
	case PAX_SLK:
		/*
		 * set data size to hold link name
		 */
		if (ul_asc((u_long)arcn->ln_nlen, hd->c_filesize,
		    sizeof(hd->c_filesize), OCT))
			goto out;
		break;
	default:
		/*
		 * all other file types have no file data
		 */
		if (ul_asc((u_long)0, hd->c_filesize, sizeof(hd->c_filesize),
		     OCT))
			goto out;
		break;
	}

	/*
	 * copy the values to the header using octal ascii
	 */
	if (ul_asc((u_long)MAGIC, hd->c_magic, sizeof(hd->c_magic), OCT) ||
	    ul_asc((u_long)arcn->sb.st_dev, hd->c_dev, sizeof(hd->c_dev),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_ino, hd->c_ino, sizeof(hd->c_ino),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_mode, hd->c_mode, sizeof(hd->c_mode),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_uid, hd->c_uid, sizeof(hd->c_uid),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_gid, hd->c_gid, sizeof(hd->c_gid),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_nlink, hd->c_nlink, sizeof(hd->c_nlink),
		 OCT) ||
	    ul_asc((u_long)arcn->sb.st_rdev, hd->c_rdev, sizeof(hd->c_rdev),
		OCT) ||
	    ul_asc((u_long)arcn->sb.st_mtime,hd->c_mtime,sizeof(hd->c_mtime),
		OCT) ||
	    ul_asc((u_long)nsz, hd->c_namesize, sizeof(hd->c_namesize), OCT))
		goto out;

	/*
	 * write the file name to the archive
	 */
	if ((wr_rdbuf((char *)&hdblk, (int)sizeof(HD_CPIO)) < 0) ||
	    (wr_rdbuf(arcn->name, nsz) < 0)) {
		paxwarn(1, "Unable to write cpio header for %s", arcn->org_name);
		return(-1);
	}

	/*
	 * if this file has data, we are done. The caller will write the file
	 * data, if we are link tell caller we are done, go to next file
	 */
	if ((arcn->type == PAX_CTG) || (arcn->type == PAX_REG) ||
	    (arcn->type == PAX_HRG))
		return(0);
	if (arcn->type != PAX_SLK)
		return(1);

	/*
	 * write the link name to the archive, tell the caller to go to the
	 * next file as we are done.
	 */
	if (wr_rdbuf(arcn->ln_name, arcn->ln_nlen) < 0) {
		paxwarn(1,"Unable to write cpio link name for %s",arcn->org_name);
		return(-1);
	}
	return(1);

    out:
	/*
	 * header field is out of range
	 */
	paxwarn(1, "Cpio header field is too small to store file %s",
	    arcn->org_name);
	return(1);
}

/*
 * Routines common to the system VR4 version of cpio (with/without file CRC)
 */

/*
 * vcpio_id()
 *      determine if a block given to us is a valid system VR4 cpio header
 *	WITHOUT crc. WATCH it the magic cookies are in OCTAL, the header
 *	uses HEX
 * Return:
 *      0 if a valid header, -1 otherwise
 */

int
vcpio_id(char *blk, int size)
{
	if ((size < (int)sizeof(HD_VCPIO)) ||
	    (strncmp(blk, AVMAGIC, sizeof(AVMAGIC) - 1) != 0))
		return(-1);
	return(0);
}

/*
 * crc_id()
 *      determine if a block given to us is a valid system VR4 cpio header
 *	WITH crc. WATCH it the magic cookies are in OCTAL the header uses HEX
 * Return:
 *      0 if a valid header, -1 otherwise
 */

int
crc_id(char *blk, int size)
{
	if ((size < (int)sizeof(HD_VCPIO)) ||
	    (strncmp(blk, AVCMAGIC, (int)sizeof(AVCMAGIC) - 1) != 0))
		return(-1);
	return(0);
}

/*
 * crc_strd()
 w	set file data CRC calculations. Fire up the hard link detection code
 * Return:
 *      0 if ok -1 otherwise (the return values of lnk_start())
 */

int
crc_strd(void)
{
	docrc = 1;
	return(lnk_start());
}

/*
 * vcpio_rd()
 *	determine if a buffer is a system VR4 archive entry. (with/without CRC)
 *	convert and store the values in the ARCHD parameter.
 * Return:
 *	0 if a valid header, -1 otherwise.
 */

int
vcpio_rd(ARCHD *arcn, char *buf)
{
	HD_VCPIO *hd;
	dev_t devminor;
	dev_t devmajor;
	int nsz;

	/*
	 * during the id phase it was determined if we were using CRC, use the
	 * proper id routine.
	 */
	if (docrc) {
		if (crc_id(buf, sizeof(HD_VCPIO)) < 0)
			return(-1);
	} else {
		if (vcpio_id(buf, sizeof(HD_VCPIO)) < 0)
			return(-1);
	}

	hd = (HD_VCPIO *)buf;
	arcn->pad = 0L;

	/*
	 * extract the hex ascii fields from the header
	 */
	arcn->sb.st_ino = (ino_t)asc_ul(hd->c_ino, sizeof(hd->c_ino), HEX);
	arcn->sb.st_mode = (mode_t)asc_ul(hd->c_mode, sizeof(hd->c_mode), HEX);
	arcn->sb.st_uid = (uid_t)asc_ul(hd->c_uid, sizeof(hd->c_uid), HEX);
	arcn->sb.st_gid = (gid_t)asc_ul(hd->c_gid, sizeof(hd->c_gid), HEX);
#ifdef NET2_STAT
	arcn->sb.st_mtime = (time_t)asc_ul(hd->c_mtime,sizeof(hd->c_mtime),HEX);
#else
	arcn->sb.st_mtime = (time_t)asc_uqd(hd->c_mtime,sizeof(hd->c_mtime),HEX);
#endif
	arcn->sb.st_ctime = arcn->sb.st_atime = arcn->sb.st_mtime;
#ifdef NET2_STAT
	arcn->sb.st_size = (off_t)asc_ul(hd->c_filesize,
	    sizeof(hd->c_filesize), HEX);
#else
	arcn->sb.st_size = (off_t)asc_uqd(hd->c_filesize,
	    sizeof(hd->c_filesize), HEX);
#endif
	arcn->sb.st_nlink = (nlink_t)asc_ul(hd->c_nlink, sizeof(hd->c_nlink),
	    HEX);
	devmajor = (dev_t)asc_ul(hd->c_maj, sizeof(hd->c_maj), HEX);
	devminor = (dev_t)asc_ul(hd->c_min, sizeof(hd->c_min), HEX);
	arcn->sb.st_dev = TODEV(devmajor, devminor);
	devmajor = (dev_t)asc_ul(hd->c_rmaj, sizeof(hd->c_maj), HEX);
	devminor = (dev_t)asc_ul(hd->c_rmin, sizeof(hd->c_min), HEX);
	arcn->sb.st_rdev = TODEV(devmajor, devminor);
	arcn->crc = asc_ul(hd->c_chksum, sizeof(hd->c_chksum), HEX);

	/*
	 * check the length of the file name, if ok read it in, return -1 if
	 * bogus
	 */
	if ((nsz = (int)asc_ul(hd->c_namesize,sizeof(hd->c_namesize),HEX)) < 2)
		return(-1);
	arcn->nlen = nsz - 1;
	if (rd_nm(arcn, nsz) < 0)
		return(-1);

	/*
	 * skip padding. header + filename is aligned to 4 byte boundaries
	 */
	if (rd_skip((off_t)(VCPIO_PAD(sizeof(HD_VCPIO) + nsz))) < 0)
		return(-1);

	/*
	 * if not a link (or a file with no data), calculate pad size (for
	 * padding which follows the file data), clear the link name and return
	 */
	if (((arcn->sb.st_mode&C_IFMT) != C_ISLNK)||(arcn->sb.st_size == 0)) {
		/*
		 * we have a valid header (not a link)
		 */
		arcn->ln_nlen = 0;
		arcn->ln_name[0] = '\0';
		arcn->pad = VCPIO_PAD(arcn->sb.st_size);
		return(com_rd(arcn));
	}

	/*
	 * read in the link name and skip over the padding
	 */
	if ((rd_ln_nm(arcn) < 0) ||
	    (rd_skip((off_t)(VCPIO_PAD(arcn->sb.st_size))) < 0))
		return(-1);

	/*
	 * we have a valid header (with a link)
	 */
	return(com_rd(arcn));
}

/*
 * vcpio_endrd()
 *      no cleanup needed here, just return size of the trailer (for append)
 * Return:
 *      size of trailer header in this format
 */

off_t
vcpio_endrd(void)
{
	return((off_t)(sizeof(HD_VCPIO) + sizeof(TRAILER) +
		(VCPIO_PAD(sizeof(HD_VCPIO) + sizeof(TRAILER)))));
}

/*
 * crc_stwr()
 *	start up the device mapping table, enable crc file calculation
 * Return:
 *	0 if ok, -1 otherwise (what dev_start() returns)
 */

int
crc_stwr(void)
{
	docrc = 1;
	return(dev_start());
}

/*
 * vcpio_wr()
 *	copy the data in the ARCHD to buffer in system VR4 cpio
 *	(with/without crc) format.
 * Return
 *	0 if file has data to be written after the header, 1 if file has
 *	NO data to write after the header, -1 if archive write failed
 */

int
vcpio_wr(ARCHD *arcn)
{
	HD_VCPIO *hd;
	unsigned int nsz;
	HD_VCPIO hdblk;

	/*
	 * check and repair truncated device and inode fields in the cpio
	 * header
	 */
	if (map_dev(arcn, (u_long)VCPIO_MASK, (u_long)VCPIO_MASK) < 0)
		return(-1);
	nsz = arcn->nlen + 1;
	hd = &hdblk;
	if ((arcn->type != PAX_BLK) && (arcn->type != PAX_CHR))
		arcn->sb.st_rdev = 0;

	/*
	 * add the proper magic value depending whether we were asked for
	 * file data crc's, and the crc if needed.
	 */
	if (docrc) {
		if (ul_asc((u_long)VCMAGIC, hd->c_magic, sizeof(hd->c_magic),
	    		OCT) ||
		    ul_asc((u_long)arcn->crc,hd->c_chksum,sizeof(hd->c_chksum),
	    		HEX))
			goto out;
	} else {
		if (ul_asc((u_long)VMAGIC, hd->c_magic, sizeof(hd->c_magic),
	    		OCT) ||
		    ul_asc((u_long)0L, hd->c_chksum, sizeof(hd->c_chksum),HEX))
			goto out;
	}

	switch(arcn->type) {
	case PAX_CTG:
	case PAX_REG:
	case PAX_HRG:
		/*
		 * caller will copy file data to the archive. tell him how
		 * much to pad.
		 */
		arcn->pad = VCPIO_PAD(arcn->sb.st_size);
#		ifdef NET2_STAT
		if (ul_asc((u_long)arcn->sb.st_size, hd->c_filesize,
		    sizeof(hd->c_filesize), HEX)) {
#		else
		if (uqd_asc((u_quad_t)arcn->sb.st_size, hd->c_filesize,
		    sizeof(hd->c_filesize), HEX)) {
#		endif
			paxwarn(1,"File is too large for sv4cpio format %s",
			    arcn->org_name);
			return(1);
		}
		break;
	case PAX_SLK:
		/*
		 * no file data for the caller to process, the file data has
		 * the size of the link
		 */
		arcn->pad = 0L;
		if (ul_asc((u_long)arcn->ln_nlen, hd->c_filesize,
		    sizeof(hd->c_filesize), HEX))
			goto out;
		break;
	default:
		/*
		 * no file data for the caller to process
		 */
		arcn->pad = 0L;
		if (ul_asc((u_long)0L, hd->c_filesize, sizeof(hd->c_filesize),
		    HEX))
			goto out;
		break;
	}

	/*
	 * set the other fields in the header
	 */
	if (ul_asc((u_long)arcn->sb.st_ino, hd->c_ino, sizeof(hd->c_ino),
		HEX) ||
	    ul_asc((u_long)arcn->sb.st_mode, hd->c_mode, sizeof(hd->c_mode),
		HEX) ||
	    ul_asc((u_long)arcn->sb.st_uid, hd->c_uid, sizeof(hd->c_uid),
		HEX) ||
	    ul_asc((u_long)arcn->sb.st_gid, hd->c_gid, sizeof(hd->c_gid),
    		HEX) ||
	    ul_asc((u_long)arcn->sb.st_mtime, hd->c_mtime, sizeof(hd->c_mtime),
    		HEX) ||
	    ul_asc((u_long)arcn->sb.st_nlink, hd->c_nlink, sizeof(hd->c_nlink),
    		HEX) ||
	    ul_asc((u_long)MAJOR(arcn->sb.st_dev),hd->c_maj, sizeof(hd->c_maj),
		HEX) ||
	    ul_asc((u_long)MINOR(arcn->sb.st_dev),hd->c_min, sizeof(hd->c_min),
		HEX) ||
	    ul_asc((u_long)MAJOR(arcn->sb.st_rdev),hd->c_rmaj,sizeof(hd->c_maj),
		HEX) ||
	    ul_asc((u_long)MINOR(arcn->sb.st_rdev),hd->c_rmin,sizeof(hd->c_min),
		HEX) ||
	    ul_asc((u_long)nsz, hd->c_namesize, sizeof(hd->c_namesize), HEX))
		goto out;

	/*
	 * write the header, the file name and padding as required.
	 */
	if ((wr_rdbuf((char *)&hdblk, (int)sizeof(HD_VCPIO)) < 0) ||
	    (wr_rdbuf(arcn->name, (int)nsz) < 0)  ||
	    (wr_skip((off_t)(VCPIO_PAD(sizeof(HD_VCPIO) + nsz))) < 0)) {
		paxwarn(1,"Could not write sv4cpio header for %s",arcn->org_name);
		return(-1);
	}

	/*
	 * if we have file data, tell the caller we are done, copy the file
	 */
	if ((arcn->type == PAX_CTG) || (arcn->type == PAX_REG) ||
	    (arcn->type == PAX_HRG))
		return(0);

	/*
	 * if we are not a link, tell the caller we are done, go to next file
	 */
	if (arcn->type != PAX_SLK)
		return(1);

	/*
	 * write the link name, tell the caller we are done.
	 */
	if ((wr_rdbuf(arcn->ln_name, arcn->ln_nlen) < 0) ||
	    (wr_skip((off_t)(VCPIO_PAD(arcn->ln_nlen))) < 0)) {
		paxwarn(1,"Could not write sv4cpio link name for %s",
		    arcn->org_name);
		return(-1);
	}
	return(1);

    out:
	/*
	 * header field is out of range
	 */
	paxwarn(1,"Sv4cpio header field is too small for file %s",arcn->org_name);
	return(1);
}

/*
 * Routines common to the old binary header cpio
 */

/*
 * bcpio_id()
 *      determine if a block given to us is an old binary cpio header
 *	(with/without header byte swapping)
 * Return:
 *      0 if a valid header, -1 otherwise
 */

int
bcpio_id(char *blk, int size)
{
	if (size < (int)sizeof(HD_BCPIO))
		return(-1);

	/*
	 * check both normal and byte swapped magic cookies
	 */
	if (((u_short)SHRT_EXT(blk)) == MAGIC)
		return(0);
	if (((u_short)RSHRT_EXT(blk)) == MAGIC) {
		if (!swp_head)
			++swp_head;
		return(0);
	}
	return(-1);
}

/*
 * bcpio_rd()
 *	determine if a buffer is an old binary archive entry. (It may have byte
 *	swapped header) convert and store the values in the ARCHD parameter.
 *	This is a very old header format and should not really be used.
 * Return:
 *	0 if a valid header, -1 otherwise.
 */

int
bcpio_rd(ARCHD *arcn, char *buf)
{
	HD_BCPIO *hd;
	int nsz;

	/*
	 * check the header
	 */
	if (bcpio_id(buf, sizeof(HD_BCPIO)) < 0)
		return(-1);

	arcn->pad = 0L;
	hd = (HD_BCPIO *)buf;
	if (swp_head) {
		/*
		 * header has swapped bytes on 16 bit boundaries
		 */
		arcn->sb.st_dev = (dev_t)(RSHRT_EXT(hd->h_dev));
		arcn->sb.st_ino = (ino_t)(RSHRT_EXT(hd->h_ino));
		arcn->sb.st_mode = (mode_t)(RSHRT_EXT(hd->h_mode));
		arcn->sb.st_uid = (uid_t)(RSHRT_EXT(hd->h_uid));
		arcn->sb.st_gid = (gid_t)(RSHRT_EXT(hd->h_gid));
		arcn->sb.st_nlink = (nlink_t)(RSHRT_EXT(hd->h_nlink));
		arcn->sb.st_rdev = (dev_t)(RSHRT_EXT(hd->h_rdev));
		arcn->sb.st_mtime = (time_t)(RSHRT_EXT(hd->h_mtime_1));
		arcn->sb.st_mtime =  (arcn->sb.st_mtime << 16) |
			((time_t)(RSHRT_EXT(hd->h_mtime_2)));
		arcn->sb.st_size = (off_t)(RSHRT_EXT(hd->h_filesize_1));
		arcn->sb.st_size = (arcn->sb.st_size << 16) |
			((off_t)(RSHRT_EXT(hd->h_filesize_2)));
		nsz = (int)(RSHRT_EXT(hd->h_namesize));
	} else {
		arcn->sb.st_dev = (dev_t)(SHRT_EXT(hd->h_dev));
		arcn->sb.st_ino = (ino_t)(SHRT_EXT(hd->h_ino));
		arcn->sb.st_mode = (mode_t)(SHRT_EXT(hd->h_mode));
		arcn->sb.st_uid = (uid_t)(SHRT_EXT(hd->h_uid));
		arcn->sb.st_gid = (gid_t)(SHRT_EXT(hd->h_gid));
		arcn->sb.st_nlink = (nlink_t)(SHRT_EXT(hd->h_nlink));
		arcn->sb.st_rdev = (dev_t)(SHRT_EXT(hd->h_rdev));
		arcn->sb.st_mtime = (time_t)(SHRT_EXT(hd->h_mtime_1));
		arcn->sb.st_mtime =  (arcn->sb.st_mtime << 16) |
			((time_t)(SHRT_EXT(hd->h_mtime_2)));
		arcn->sb.st_size = (off_t)(SHRT_EXT(hd->h_filesize_1));
		arcn->sb.st_size = (arcn->sb.st_size << 16) |
			((off_t)(SHRT_EXT(hd->h_filesize_2)));
		nsz = (int)(SHRT_EXT(hd->h_namesize));
	}
	arcn->sb.st_ctime = arcn->sb.st_atime = arcn->sb.st_mtime;

	/*
	 * check the file name size, if bogus give up. otherwise read the file
	 * name
	 */
	if (nsz < 2)
		return(-1);
	arcn->nlen = nsz - 1;
	if (rd_nm(arcn, nsz) < 0)
		return(-1);

	/*
	 * header + file name are aligned to 2 byte boundaries, skip if needed
	 */
	if (rd_skip((off_t)(BCPIO_PAD(sizeof(HD_BCPIO) + nsz))) < 0)
		return(-1);

	/*
	 * if not a link (or a file with no data), calculate pad size (for
	 * padding which follows the file data), clear the link name and return
	 */
	if (((arcn->sb.st_mode & C_IFMT) != C_ISLNK)||(arcn->sb.st_size == 0)){
		/*
		 * we have a valid header (not a link)
		 */
		arcn->ln_nlen = 0;
		arcn->ln_name[0] = '\0';
		arcn->pad = BCPIO_PAD(arcn->sb.st_size);
		return(com_rd(arcn));
	}

	if ((rd_ln_nm(arcn) < 0) ||
	    (rd_skip((off_t)(BCPIO_PAD(arcn->sb.st_size))) < 0))
		return(-1);

	/*
	 * we have a valid header (with a link)
	 */
	return(com_rd(arcn));
}

/*
 * bcpio_endrd()
 *      no cleanup needed here, just return size of the trailer (for append)
 * Return:
 *      size of trailer header in this format
 */

off_t
bcpio_endrd(void)
{
	return((off_t)(sizeof(HD_BCPIO) + sizeof(TRAILER) +
		(BCPIO_PAD(sizeof(HD_BCPIO) + sizeof(TRAILER)))));
}

/*
 * bcpio_wr()
 *	copy the data in the ARCHD to buffer in old binary cpio format
 *	There is a real chance of field overflow with this critter. So we
 *	always check that the conversion is ok. nobody in their right mind
 *	should write an archive in this format...
 * Return
 *      0 if file has data to be written after the header, 1 if file has NO
 *	data to write after the header, -1 if archive write failed
 */

int
bcpio_wr(ARCHD *arcn)
{
	HD_BCPIO *hd;
	int nsz;
	HD_BCPIO hdblk;
	off_t t_offt;
	int t_int;
	time_t t_timet;

	/*
	 * check and repair truncated device and inode fields in the cpio
	 * header
	 */
	if (map_dev(arcn, (u_long)BCPIO_MASK, (u_long)BCPIO_MASK) < 0)
		return(-1);

	if ((arcn->type != PAX_BLK) && (arcn->type != PAX_CHR))
		arcn->sb.st_rdev = 0;
	hd = &hdblk;

	switch(arcn->type) {
	case PAX_CTG:
	case PAX_REG:
	case PAX_HRG:
		/*
		 * caller will copy file data to the archive. tell him how
		 * much to pad.
		 */
		arcn->pad = BCPIO_PAD(arcn->sb.st_size);
		hd->h_filesize_1[0] = CHR_WR_0(arcn->sb.st_size);
		hd->h_filesize_1[1] = CHR_WR_1(arcn->sb.st_size);
		hd->h_filesize_2[0] = CHR_WR_2(arcn->sb.st_size);
		hd->h_filesize_2[1] = CHR_WR_3(arcn->sb.st_size);
		t_offt = (off_t)(SHRT_EXT(hd->h_filesize_1));
		t_offt = (t_offt<<16) | ((off_t)(SHRT_EXT(hd->h_filesize_2)));
		if (arcn->sb.st_size != t_offt) {
			paxwarn(1,"File is too large for bcpio format %s",
			    arcn->org_name);
			return(1);
		}
		break;
	case PAX_SLK:
		/*
		 * no file data for the caller to process, the file data has
		 * the size of the link
		 */
		arcn->pad = 0L;
		hd->h_filesize_1[0] = CHR_WR_0(arcn->ln_nlen);
		hd->h_filesize_1[1] = CHR_WR_1(arcn->ln_nlen);
		hd->h_filesize_2[0] = CHR_WR_2(arcn->ln_nlen);
		hd->h_filesize_2[1] = CHR_WR_3(arcn->ln_nlen);
		t_int = (int)(SHRT_EXT(hd->h_filesize_1));
		t_int = (t_int << 16) | ((int)(SHRT_EXT(hd->h_filesize_2)));
		if (arcn->ln_nlen != t_int)
			goto out;
		break;
	default:
		/*
		 * no file data for the caller to process
		 */
		arcn->pad = 0L;
		hd->h_filesize_1[0] = (char)0;
		hd->h_filesize_1[1] = (char)0;
		hd->h_filesize_2[0] = (char)0;
		hd->h_filesize_2[1] = (char)0;
		break;
	}

	/*
	 * build up the rest of the fields
	 */
	hd->h_magic[0] = CHR_WR_2(MAGIC);
	hd->h_magic[1] = CHR_WR_3(MAGIC);
	hd->h_dev[0] = CHR_WR_2(arcn->sb.st_dev);
	hd->h_dev[1] = CHR_WR_3(arcn->sb.st_dev);
	if (arcn->sb.st_dev != (dev_t)(SHRT_EXT(hd->h_dev)))
		goto out;
	hd->h_ino[0] = CHR_WR_2(arcn->sb.st_ino);
	hd->h_ino[1] = CHR_WR_3(arcn->sb.st_ino);
	if (arcn->sb.st_ino != (ino_t)(SHRT_EXT(hd->h_ino)))
		goto out;
	hd->h_mode[0] = CHR_WR_2(arcn->sb.st_mode);
	hd->h_mode[1] = CHR_WR_3(arcn->sb.st_mode);
	if (arcn->sb.st_mode != (mode_t)(SHRT_EXT(hd->h_mode)))
		goto out;
	hd->h_uid[0] = CHR_WR_2(arcn->sb.st_uid);
	hd->h_uid[1] = CHR_WR_3(arcn->sb.st_uid);
	if (arcn->sb.st_uid != (uid_t)(SHRT_EXT(hd->h_uid)))
		goto out;
	hd->h_gid[0] = CHR_WR_2(arcn->sb.st_gid);
	hd->h_gid[1] = CHR_WR_3(arcn->sb.st_gid);
	if (arcn->sb.st_gid != (gid_t)(SHRT_EXT(hd->h_gid)))
		goto out;
	hd->h_nlink[0] = CHR_WR_2(arcn->sb.st_nlink);
	hd->h_nlink[1] = CHR_WR_3(arcn->sb.st_nlink);
	if (arcn->sb.st_nlink != (nlink_t)(SHRT_EXT(hd->h_nlink)))
		goto out;
	hd->h_rdev[0] = CHR_WR_2(arcn->sb.st_rdev);
	hd->h_rdev[1] = CHR_WR_3(arcn->sb.st_rdev);
	if (arcn->sb.st_rdev != (dev_t)(SHRT_EXT(hd->h_rdev)))
		goto out;
	hd->h_mtime_1[0] = CHR_WR_0(arcn->sb.st_mtime);
	hd->h_mtime_1[1] = CHR_WR_1(arcn->sb.st_mtime);
	hd->h_mtime_2[0] = CHR_WR_2(arcn->sb.st_mtime);
	hd->h_mtime_2[1] = CHR_WR_3(arcn->sb.st_mtime);
	t_timet = (time_t)(SHRT_EXT(hd->h_mtime_1));
	t_timet =  (t_timet << 16) | ((time_t)(SHRT_EXT(hd->h_mtime_2)));
	if (arcn->sb.st_mtime != t_timet)
		goto out;
	nsz = arcn->nlen + 1;
	hd->h_namesize[0] = CHR_WR_2(nsz);
	hd->h_namesize[1] = CHR_WR_3(nsz);
	if (nsz != (int)(SHRT_EXT(hd->h_namesize)))
		goto out;

	/*
	 * write the header, the file name and padding as required.
	 */
	if ((wr_rdbuf((char *)&hdblk, (int)sizeof(HD_BCPIO)) < 0) ||
	    (wr_rdbuf(arcn->name, nsz) < 0) ||
	    (wr_skip((off_t)(BCPIO_PAD(sizeof(HD_BCPIO) + nsz))) < 0)) {
		paxwarn(1, "Could not write bcpio header for %s", arcn->org_name);
		return(-1);
	}

	/*
	 * if we have file data, tell the caller we are done
	 */
	if ((arcn->type == PAX_CTG) || (arcn->type == PAX_REG) ||
	    (arcn->type == PAX_HRG))
		return(0);

	/*
	 * if we are not a link, tell the caller we are done, go to next file
	 */
	if (arcn->type != PAX_SLK)
		return(1);

	/*
	 * write the link name, tell the caller we are done.
	 */
	if ((wr_rdbuf(arcn->ln_name, arcn->ln_nlen) < 0) ||
	    (wr_skip((off_t)(BCPIO_PAD(arcn->ln_nlen))) < 0)) {
		paxwarn(1,"Could not write bcpio link name for %s",arcn->org_name);
		return(-1);
	}
	return(1);

    out:
	/*
	 * header field is out of range
	 */
	paxwarn(1,"Bcpio header field is too small for file %s", arcn->org_name);
	return(1);
}
