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
static char sccsid[] = "@(#)ar_subs.c	8.2 (Berkeley) 4/18/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "pax.h"
#include "extern.h"

static void wr_archive(ARCHD *, int is_app);
static int get_arc(void);
static int next_head(ARCHD *);

/*
 * Routines which control the overall operation modes of pax as specified by
 * the user: list, append, read ...
 */

static char hdbuf[BLKMULT];		/* space for archive header on read */
u_long flcnt;				/* number of files processed */

/*
 * list()
 *	list the contents of an archive which match user supplied pattern(s)
 *	(no pattern matches all).
 */

void
list(void)
{
	ARCHD *arcn;
	int res;
	ARCHD archd;
	time_t now;

	arcn = &archd;
	/*
	 * figure out archive type; pass any format specific options to the
	 * archive option processing routine; call the format init routine. We
	 * also save current time for ls_list() so we do not make a system
	 * call for each file we need to print. If verbose (vflag) start up
	 * the name and group caches.
	 */
	if ((get_arc() < 0) || ((*frmt->options)() < 0) ||
	    ((*frmt->st_rd)() < 0))
		return;

	if (vflag && ((uidtb_start() < 0) || (gidtb_start() < 0)))
		return;

	now = time(NULL);

	/*
	 * step through the archive until the format says it is done
	 */
	while (next_head(arcn) == 0) {
		/*
		 * check for pattern, and user specified options match.
		 * When all patterns are matched we are done.
		 */
		if ((res = pat_match(arcn)) < 0)
			break;

		if ((res == 0) && (sel_chk(arcn) == 0)) {
			/*
			 * pattern resulted in a selected file
			 */
			if (pat_sel(arcn) < 0)
				break;

			/*
			 * modify the name as requested by the user if name
			 * survives modification, do a listing of the file
			 */
			if ((res = mod_name(arcn)) < 0)
				break;
			if (res == 0)
				ls_list(arcn, now, stdout);
		}

		/*
		 * skip to next archive format header using values calculated
		 * by the format header read routine
		 */
		if (rd_skip(arcn->skip + arcn->pad) == 1)
			break;
	}

	/*
	 * all done, let format have a chance to cleanup, and make sure that
	 * the patterns supplied by the user were all matched
	 */
	(void)(*frmt->end_rd)();
	(void)sigprocmask(SIG_BLOCK, &s_mask, NULL);
	ar_close();
	pat_chk();
}

/*
 * extract()
 *	extract the member(s) of an archive as specified by user supplied
 *	pattern(s) (no patterns extracts all members)
 */

void
extract(void)
{
	ARCHD *arcn;
	int res;
	off_t cnt;
	ARCHD archd;
	struct stat sb;
	int fd;
	time_t now;

	arcn = &archd;
	/*
	 * figure out archive type; pass any format specific options to the
	 * archive option processing routine; call the format init routine;
	 * start up the directory modification time and access mode database
	 */
	if ((get_arc() < 0) || ((*frmt->options)() < 0) ||
	    ((*frmt->st_rd)() < 0) || (dir_start() < 0))
		return;

	/*
	 * When we are doing interactive rename, we store the mapping of names
	 * so we can fix up hard links files later in the archive.
	 */
	if (iflag && (name_start() < 0))
		return;

	now = time(NULL);

	/*
	 * step through each entry on the archive until the format read routine
	 * says it is done
	 */
	while (next_head(arcn) == 0) {

		/*
		 * check for pattern, and user specified options match. When
		 * all the patterns are matched we are done
		 */
		if ((res = pat_match(arcn)) < 0)
			break;

		if ((res > 0) || (sel_chk(arcn) != 0)) {
			/*
			 * file is not selected. skip past any file data and
			 * padding and go back for the next archive member
			 */
			(void)rd_skip(arcn->skip + arcn->pad);
			continue;
		}

		/*
		 * with -u or -D only extract when the archive member is newer
		 * than the file with the same name in the file system (nos
		 * test of being the same type is required).
		 * NOTE: this test is done BEFORE name modifications as
		 * specified by pax. this operation can be confusing to the
		 * user who might expect the test to be done on an existing
		 * file AFTER the name mod. In honesty the pax spec is probably
		 * flawed in this respect.
		 */
		if ((uflag || Dflag) && ((lstat(arcn->name, &sb) == 0))) {
			if (uflag && Dflag) {
				if ((arcn->sb.st_mtime <= sb.st_mtime) &&
				    (arcn->sb.st_ctime <= sb.st_ctime)) {
					(void)rd_skip(arcn->skip + arcn->pad);
					continue;
				}
			} else if (Dflag) {
				if (arcn->sb.st_ctime <= sb.st_ctime) {
					(void)rd_skip(arcn->skip + arcn->pad);
					continue;
				}
			} else if (arcn->sb.st_mtime <= sb.st_mtime) {
				(void)rd_skip(arcn->skip + arcn->pad);
				continue;
			}
		}

		/*
		 * this archive member is now been selected. modify the name.
		 */
		if ((pat_sel(arcn) < 0) || ((res = mod_name(arcn)) < 0))
			break;
		if (res > 0) {
			/*
			 * a bad name mod, skip and purge name from link table
			 */
			purg_lnk(arcn);
			(void)rd_skip(arcn->skip + arcn->pad);
			continue;
		}

		/*
		 * Non standard -Y and -Z flag. When the existing file is
		 * same age or newer skip
		 */
		if ((Yflag || Zflag) && ((lstat(arcn->name, &sb) == 0))) {
			if (Yflag && Zflag) {
				if ((arcn->sb.st_mtime <= sb.st_mtime) &&
				    (arcn->sb.st_ctime <= sb.st_ctime)) {
					(void)rd_skip(arcn->skip + arcn->pad);
					continue;
				}
			} else if (Yflag) {
				if (arcn->sb.st_ctime <= sb.st_ctime) {
					(void)rd_skip(arcn->skip + arcn->pad);
					continue;
				}
			} else if (arcn->sb.st_mtime <= sb.st_mtime) {
				(void)rd_skip(arcn->skip + arcn->pad);
				continue;
			}
		}

		if (vflag) {
			if (vflag > 1)
				ls_list(arcn, now, listf);
			else {
				(void)fputs(arcn->name, listf);
				vfpart = 1;
			}
		}

		/*
		 * if required, chdir around.
		 */
		if ((arcn->pat != NULL) && (arcn->pat->chdname != NULL))
			if (chdir(arcn->pat->chdname) != 0)
				syswarn(1, errno, "Cannot chdir to %s",
				    arcn->pat->chdname);

		/*
		 * all ok, extract this member based on type
		 */
		if ((arcn->type != PAX_REG) && (arcn->type != PAX_CTG)) {
			/*
			 * process archive members that are not regular files.
			 * throw out padding and any data that might follow the
			 * header (as determined by the format).
			 */
			if ((arcn->type == PAX_HLK) || (arcn->type == PAX_HRG))
				res = lnk_creat(arcn);
			else
				res = node_creat(arcn);

			(void)rd_skip(arcn->skip + arcn->pad);
			if (res < 0)
				purg_lnk(arcn);

			if (vflag && vfpart) {
				(void)putc('\n', listf);
				vfpart = 0;
			}
			continue;
		}
		/*
		 * we have a file with data here. If we can not create it, skip
		 * over the data and purge the name from hard link table
		 */
		if ((fd = file_creat(arcn)) < 0) {
			(void)rd_skip(arcn->skip + arcn->pad);
			purg_lnk(arcn);
			continue;
		}
		/*
		 * extract the file from the archive and skip over padding and
		 * any unprocessed data
		 */
		res = (*frmt->rd_data)(arcn, fd, &cnt);
		file_close(arcn, fd);
		if (vflag && vfpart) {
			(void)putc('\n', listf);
			vfpart = 0;
		}
		if (!res)
			(void)rd_skip(cnt + arcn->pad);

		/*
		 * if required, chdir around.
		 */
		if ((arcn->pat != NULL) && (arcn->pat->chdname != NULL))
			if (fchdir(cwdfd) != 0)
				syswarn(1, errno,
				    "Can't fchdir to starting directory");
	}

	/*
	 * all done, restore directory modes and times as required; make sure
	 * all patterns supplied by the user were matched; block off signals
	 * to avoid chance for multiple entry into the cleanup code.
	 */
	(void)(*frmt->end_rd)();
	(void)sigprocmask(SIG_BLOCK, &s_mask, NULL);
	ar_close();
	proc_dir();
	pat_chk();
}

/*
 * wr_archive()
 *	Write an archive. used in both creating a new archive and appends on
 *	previously written archive.
 */

static void
wr_archive(ARCHD *arcn, int is_app)
{
	int res;
	int hlk;
	int wr_one;
	off_t cnt;
	int (*wrf)(ARCHD *);
	int fd = -1;
	time_t now;

	/*
	 * if this format supports hard link storage, start up the database
	 * that detects them.
	 */
	if (((hlk = frmt->hlk) == 1) && (lnk_start() < 0))
		return;

	/*
	 * start up the file traversal code and format specific write
	 */
	if ((ftree_start() < 0) || ((*frmt->st_wr)() < 0))
		return;
	wrf = frmt->wr;

	/*
	 * When we are doing interactive rename, we store the mapping of names
	 * so we can fix up hard links files later in the archive.
	 */
	if (iflag && (name_start() < 0))
		return;

	/*
	 * if this not append, and there are no files, we do no write a trailer
	 */
	wr_one = is_app;

	now = time(NULL);

	/*
	 * while there are files to archive, process them one at at time
	 */
	while (next_file(arcn) == 0) {
		/*
		 * check if this file meets user specified options match.
		 */
		if (sel_chk(arcn) != 0) {
			ftree_notsel();
			continue;
		}
		fd = -1;
		if (uflag) {
			/*
			 * only archive if this file is newer than a file with
			 * the same name that is already stored on the archive
			 */
			if ((res = chk_ftime(arcn)) < 0)
				break;
			if (res > 0)
				continue;
		}

		/*
		 * this file is considered selected now. see if this is a hard
		 * link to a file already stored
		 */
		ftree_sel(arcn);
		if (hlk && (chk_lnk(arcn) < 0))
			break;

		if ((arcn->type == PAX_REG) || (arcn->type == PAX_HRG) ||
		    (arcn->type == PAX_CTG)) {
			/*
			 * we will have to read this file. by opening it now we
			 * can avoid writing a header to the archive for a file
			 * we were later unable to read (we also purge it from
			 * the link table).
			 */
			if ((fd = open(arcn->org_name, O_RDONLY, 0)) < 0) {
				syswarn(1,errno, "Unable to open %s to read",
					arcn->org_name);
				purg_lnk(arcn);
				continue;
			}
		}

		/*
		 * Now modify the name as requested by the user
		 */
		if ((res = mod_name(arcn)) < 0) {
			/*
			 * name modification says to skip this file, close the
			 * file and purge link table entry
			 */
			rdfile_close(arcn, &fd);
			purg_lnk(arcn);
			break;
		}

		if ((res > 0) || (docrc && (set_crc(arcn, fd) < 0))) {
			/*
			 * unable to obtain the crc we need, close the file,
			 * purge link table entry
			 */
			rdfile_close(arcn, &fd);
			purg_lnk(arcn);
			continue;
		}

		if (vflag) {
			if (vflag > 1)
				ls_list(arcn, now, listf);
			else {
				(void)fputs(arcn->name, listf);
				vfpart = 1;
			}
		}
		++flcnt;

		/*
		 * looks safe to store the file, have the format specific
		 * routine write routine store the file header on the archive
		 */
		if ((res = (*wrf)(arcn)) < 0) {
			rdfile_close(arcn, &fd);
			break;
		}
		wr_one = 1;
		if (res > 0) {
			/*
			 * format write says no file data needs to be stored
			 * so we are done messing with this file
			 */
			if (vflag && vfpart) {
				(void)putc('\n', listf);
				vfpart = 0;
			}
			rdfile_close(arcn, &fd);
			continue;
		}

		/*
		 * Add file data to the archive, quit on write error. if we
		 * cannot write the entire file contents to the archive we
		 * must pad the archive to replace the missing file data
		 * (otherwise during an extract the file header for the file
		 * which FOLLOWS this one will not be where we expect it to
		 * be).
		 */
		res = (*frmt->wr_data)(arcn, fd, &cnt);
		rdfile_close(arcn, &fd);
		if (vflag && vfpart) {
			(void)putc('\n', listf);
			vfpart = 0;
		}
		if (res < 0)
			break;

		/*
		 * pad as required, cnt is number of bytes not written
		 */
		if (((cnt > 0) && (wr_skip(cnt) < 0)) ||
		    ((arcn->pad > 0) && (wr_skip(arcn->pad) < 0)))
			break;
	}

	/*
	 * tell format to write trailer; pad to block boundary; reset directory
	 * mode/access times, and check if all patterns supplied by the user
	 * were matched. block off signals to avoid chance for multiple entry
	 * into the cleanup code
	 */
	if (wr_one) {
		(*frmt->end_wr)();
		wr_fin();
	}
	(void)sigprocmask(SIG_BLOCK, &s_mask, NULL);
	ar_close();
	if (tflag)
		proc_dir();
	ftree_chk();
}

/*
 * append()
 *	Add file to previously written archive. Archive format specified by the
 *	user must agree with archive. The archive is read first to collect
 *	modification times (if -u) and locate the archive trailer. The archive
 *	is positioned in front of the record with the trailer and wr_archive()
 *	is called to add the new members.
 *	PAX IMPLEMENTATION DETAIL NOTE:
 *	-u is implemented by adding the new members to the end of the archive.
 *	Care is taken so that these do not end up as links to the older
 *	version of the same file already stored in the archive. It is expected
 *	when extraction occurs these newer versions will over-write the older
 *	ones stored "earlier" in the archive (this may be a bad assumption as
 *	it depends on the implementation of the program doing the extraction).
 *	It is really difficult to splice in members without either re-writing
 *	the entire archive (from the point were the old version was), or having
 *	assistance of the format specification in terms of a special update
 *	header that invalidates a previous archive record. The POSIX spec left
 *	the method used to implement -u unspecified. This pax is able to
 *	over write existing files that it creates.
 */

void
append(void)
{
	ARCHD *arcn;
	int res;
	ARCHD archd;
	FSUB *orgfrmt;
	int udev;
	off_t tlen;

	arcn = &archd;
	orgfrmt = frmt;

	/*
	 * Do not allow an append operation if the actual archive is of a
	 * different format than the user specified format.
	 */
	if (get_arc() < 0)
		return;
	if ((orgfrmt != NULL) && (orgfrmt != frmt)) {
		paxwarn(1, "Cannot mix current archive format %s with %s",
		    frmt->name, orgfrmt->name);
		return;
	}

	/*
	 * pass the format any options and start up format
	 */
	if (((*frmt->options)() < 0) || ((*frmt->st_rd)() < 0))
		return;

	/*
	 * if we only are adding members that are newer, we need to save the
	 * mod times for all files we see.
	 */
	if (uflag && (ftime_start() < 0))
		return;

	/*
	 * some archive formats encode hard links by recording the device and
	 * file serial number (inode) but copy the file anyway (multiple times)
	 * to the archive. When we append, we run the risk that newly added
	 * files may have the same device and inode numbers as those recorded
	 * on the archive but during a previous run. If this happens, when the
	 * archive is extracted we get INCORRECT hard links. We avoid this by
	 * remapping the device numbers so that newly added files will never
	 * use the same device number as one found on the archive. remapping
	 * allows new members to safely have links among themselves. remapping
	 * also avoids problems with file inode (serial number) truncations
	 * when the inode number is larger than storage space in the archive
	 * header. See the remap routines for more details.
	 */
	if ((udev = frmt->udev) && (dev_start() < 0))
		return;

	/*
	 * reading the archive may take a long time. If verbose tell the user
	 */
	if (vflag) {
		(void)fprintf(listf,
			"%s: Reading archive to position at the end...", argv0);
		vfpart = 1;
	}

	/*
	 * step through the archive until the format says it is done
	 */
	while (next_head(arcn) == 0) {
		/*
		 * check if this file meets user specified options.
		 */
		if (sel_chk(arcn) != 0) {
			if (rd_skip(arcn->skip + arcn->pad) == 1)
				break;
			continue;
		}

		if (uflag) {
			/*
			 * see if this is the newest version of this file has
			 * already been seen, if so skip.
			 */
			if ((res = chk_ftime(arcn)) < 0)
				break;
			if (res > 0) {
				if (rd_skip(arcn->skip + arcn->pad) == 1)
					break;
				continue;
			}
		}

		/*
		 * Store this device number. Device numbers seen during the
		 * read phase of append will cause newly appended files with a
		 * device number seen in the old part of the archive to be
		 * remapped to an unused device number.
		 */
		if ((udev && (add_dev(arcn) < 0)) ||
		    (rd_skip(arcn->skip + arcn->pad) == 1))
			break;
	}

	/*
	 * done, finish up read and get the number of bytes to back up so we
	 * can add new members. The format might have used the hard link table,
	 * purge it.
	 */
	tlen = (*frmt->end_rd)();
	lnk_end();

	/*
	 * try to position for write, if this fails quit. if any error occurs,
	 * we will refuse to write
	 */
	if (appnd_start(tlen) < 0)
		return;

	/*
	 * tell the user we are done reading.
	 */
	if (vflag && vfpart) {
		(void)fputs("done.\n", listf);
		vfpart = 0;
	}

	/*
	 * go to the writing phase to add the new members
	 */
	wr_archive(arcn, 1);
}

/*
 * archive()
 *	write a new archive
 */

void
archive(void)
{
	ARCHD archd;

	/*
	 * if we only are adding members that are newer, we need to save the
	 * mod times for all files; set up for writing; pass the format any
	 * options write the archive
	 */
	if ((uflag && (ftime_start() < 0)) || (wr_start() < 0))
		return;
	if ((*frmt->options)() < 0)
		return;

	wr_archive(&archd, 0);
}

/*
 * copy()
 *	copy files from one part of the file system to another. this does not
 *	use any archive storage. The EFFECT OF THE COPY IS THE SAME as if an
 *	archive was written and then extracted in the destination directory
 *	(except the files are forced to be under the destination directory).
 */

void
copy(void)
{
	ARCHD *arcn;
	int res;
	int fddest;
	char *dest_pt;
	int dlen;
	int drem;
	int fdsrc = -1;
	struct stat sb;
	ARCHD archd;
	char dirbuf[PAXPATHLEN+1];

	arcn = &archd;
	/*
	 * set up the destination dir path and make sure it is a directory. We
	 * make sure we have a trailing / on the destination
	 */
	dlen = l_strncpy(dirbuf, dirptr, sizeof(dirbuf) - 1);
	dest_pt = dirbuf + dlen;
	if (*(dest_pt-1) != '/') {
		*dest_pt++ = '/';
		++dlen;
	}
	*dest_pt = '\0';
	drem = PAXPATHLEN - dlen;

	if (stat(dirptr, &sb) < 0) {
		syswarn(1, errno, "Cannot access destination directory %s",
			dirptr);
		return;
	}
	if (!S_ISDIR(sb.st_mode)) {
		paxwarn(1, "Destination is not a directory %s", dirptr);
		return;
	}

	/*
	 * start up the hard link table; file traversal routines and the
	 * modification time and access mode database
	 */
	if ((lnk_start() < 0) || (ftree_start() < 0) || (dir_start() < 0))
		return;

	/*
	 * When we are doing interactive rename, we store the mapping of names
	 * so we can fix up hard links files later in the archive.
	 */
	if (iflag && (name_start() < 0))
		return;

	/*
	 * set up to cp file trees
	 */
	cp_start();

	/*
	 * while there are files to archive, process them
	 */
	while (next_file(arcn) == 0) {
		fdsrc = -1;

		/*
		 * check if this file meets user specified options
		 */
		if (sel_chk(arcn) != 0) {
			ftree_notsel();
			continue;
		}

		/*
		 * if there is already a file in the destination directory with
		 * the same name and it is newer, skip the one stored on the
		 * archive.
		 * NOTE: this test is done BEFORE name modifications as
		 * specified by pax. this can be confusing to the user who
		 * might expect the test to be done on an existing file AFTER
		 * the name mod. In honesty the pax spec is probably flawed in
		 * this respect
		 */
		if (uflag || Dflag) {
			/*
			 * create the destination name
			 */
			if (*(arcn->name) == '/')
				res = 1;
			else
				res = 0;
			if ((arcn->nlen - res) > drem) {
				paxwarn(1, "Destination pathname too long %s",
					arcn->name);
				continue;
			}
			(void)strncpy(dest_pt, arcn->name + res, drem);
			dirbuf[PAXPATHLEN] = '\0';

			/*
			 * if existing file is same age or newer skip
			 */
			res = lstat(dirbuf, &sb);
			*dest_pt = '\0';

		    	if (res == 0) {
				if (uflag && Dflag) {
					if ((arcn->sb.st_mtime<=sb.st_mtime) &&
			    		    (arcn->sb.st_ctime<=sb.st_ctime))
						continue;
				} else if (Dflag) {
					if (arcn->sb.st_ctime <= sb.st_ctime)
						continue;
				} else if (arcn->sb.st_mtime <= sb.st_mtime)
					continue;
			}
		}

		/*
		 * this file is considered selected. See if this is a hard link
		 * to a previous file; modify the name as requested by the
		 * user; set the final destination.
		 */
		ftree_sel(arcn);
		if ((chk_lnk(arcn) < 0) || ((res = mod_name(arcn)) < 0))
			break;
		if ((res > 0) || (set_dest(arcn, dirbuf, dlen) < 0)) {
			/*
			 * skip file, purge from link table
			 */
			purg_lnk(arcn);
			continue;
		}

		/*
		 * Non standard -Y and -Z flag. When the existing file is
		 * same age or newer skip
		 */
		if ((Yflag || Zflag) && ((lstat(arcn->name, &sb) == 0))) {
			if (Yflag && Zflag) {
				if ((arcn->sb.st_mtime <= sb.st_mtime) &&
				    (arcn->sb.st_ctime <= sb.st_ctime))
					continue;
			} else if (Yflag) {
				if (arcn->sb.st_ctime <= sb.st_ctime)
					continue;
			} else if (arcn->sb.st_mtime <= sb.st_mtime)
				continue;
		}

		if (vflag) {
			(void)fputs(arcn->name, listf);
			vfpart = 1;
		}
		++flcnt;

		/*
		 * try to create a hard link to the src file if requested
		 * but make sure we are not trying to overwrite ourselves.
		 */
		if (lflag)
			res = cross_lnk(arcn);
		else
			res = chk_same(arcn);
		if (res <= 0) {
			if (vflag && vfpart) {
				(void)putc('\n', listf);
				vfpart = 0;
			}
			continue;
		}

		/*
		 * have to create a new file
		 */
		if ((arcn->type != PAX_REG) && (arcn->type != PAX_CTG)) {
			/*
			 * create a link or special file
			 */
			if ((arcn->type == PAX_HLK) || (arcn->type == PAX_HRG))
				res = lnk_creat(arcn);
			else
				res = node_creat(arcn);
			if (res < 0)
				purg_lnk(arcn);
			if (vflag && vfpart) {
				(void)putc('\n', listf);
				vfpart = 0;
			}
			continue;
		}

		/*
		 * have to copy a regular file to the destination directory.
		 * first open source file and then create the destination file
		 */
		if ((fdsrc = open(arcn->org_name, O_RDONLY, 0)) < 0) {
			syswarn(1, errno, "Unable to open %s to read",
			    arcn->org_name);
			purg_lnk(arcn);
			continue;
		}
		if ((fddest = file_creat(arcn)) < 0) {
			rdfile_close(arcn, &fdsrc);
			purg_lnk(arcn);
			continue;
		}

		/*
		 * copy source file data to the destination file
		 */
		cp_file(arcn, fdsrc, fddest);
		file_close(arcn, fddest);
		rdfile_close(arcn, &fdsrc);

		if (vflag && vfpart) {
			(void)putc('\n', listf);
			vfpart = 0;
		}
	}

	/*
	 * restore directory modes and times as required; make sure all
	 * patterns were selected block off signals to avoid chance for
	 * multiple entry into the cleanup code.
	 */
	(void)sigprocmask(SIG_BLOCK, &s_mask, NULL);
	ar_close();
	proc_dir();
	ftree_chk();
}

/*
 * next_head()
 *	try to find a valid header in the archive. Uses format specific
 *	routines to extract the header and id the trailer. Trailers may be
 *	located within a valid header or in an invalid header (the location
 *	is format specific. The inhead field from the option table tells us
 *	where to look for the trailer).
 *	We keep reading (and resyncing) until we get enough contiguous data
 *	to check for a header. If we cannot find one, we shift by a byte
 *	add a new byte from the archive to the end of the buffer and try again.
 *	If we get a read error, we throw out what we have (as we must have
 *	contiguous data) and start over again.
 *	ASSUMED: headers fit within a BLKMULT header.
 * Return:
 *	0 if we got a header, -1 if we are unable to ever find another one
 *	(we reached the end of input, or we reached the limit on retries. see
 *	the specs for rd_wrbuf() for more details)
 */

static int
next_head(ARCHD *arcn)
{
	int ret;
	char *hdend;
	int res;
	int shftsz;
	int hsz;
	int in_resync = 0; 	/* set when we are in resync mode */
	int cnt = 0;			/* counter for trailer function */
	int first = 1;			/* on 1st read, EOF isn't premature. */

	/*
	 * set up initial conditions, we want a whole frmt->hsz block as we
	 * have no data yet.
	 */
	res = hsz = frmt->hsz;
	hdend = hdbuf;
	shftsz = hsz - 1;
	for(;;) {
		/*
		 * keep looping until we get a contiguous FULL buffer
		 * (frmt->hsz is the proper size)
		 */
		for (;;) {
			if ((ret = rd_wrbuf(hdend, res)) == res)
				break;

			/*
			 * If we read 0 bytes (EOF) from an archive when we
			 * expect to find a header, we have stepped upon
			 * an archive without the customary block of zeroes
			 * end marker.  It's just stupid to error out on
			 * them, so exit gracefully.
			 */
			if (first && ret == 0)
				return(-1);
			first = 0;

			/*
			 * some kind of archive read problem, try to resync the
			 * storage device, better give the user the bad news.
			 */
			if ((ret == 0) || (rd_sync() < 0)) {
				paxwarn(1,"Premature end of file on archive read");
				return(-1);
			}
			if (!in_resync) {
				if (act == APPND) {
					paxwarn(1,
					  "Archive I/O error, cannot continue");
					return(-1);
				}
				paxwarn(1,"Archive I/O error. Trying to recover.");
				++in_resync;
			}

			/*
			 * oh well, throw it all out and start over
			 */
			res = hsz;
			hdend = hdbuf;
		}

		/*
		 * ok we have a contiguous buffer of the right size. Call the
		 * format read routine. If this was not a valid header and this
		 * format stores trailers outside of the header, call the
		 * format specific trailer routine to check for a trailer. We
		 * have to watch out that we do not mis-identify file data or
		 * block padding as a header or trailer. Format specific
		 * trailer functions must NOT check for the trailer while we
		 * are running in resync mode. Some trailer functions may tell
		 * us that this block cannot contain a valid header either, so
		 * we then throw out the entire block and start over.
		 */
		if ((*frmt->rd)(arcn, hdbuf) == 0)
			break;

		if (!frmt->inhead) {
			/*
			 * this format has trailers outside of valid headers
			 */
			if ((ret = (*frmt->trail_tar)(hdbuf,in_resync,&cnt)) == 0){
				/*
				 * valid trailer found, drain input as required
				 */
				ar_drain();
				return(-1);
			}

			if (ret == 1) {
				/*
				 * we are in resync and we were told to throw
				 * the whole block out because none of the
				 * bytes in this block can be used to form a
				 * valid header
				 */
				res = hsz;
				hdend = hdbuf;
				continue;
			}
		}

		/*
		 * Brute force section.
		 * not a valid header. We may be able to find a header yet. So
		 * we shift over by one byte, and set up to read one byte at a
		 * time from the archive and place it at the end of the buffer.
		 * We will keep moving byte at a time until we find a header or
		 * get a read error and have to start over.
		 */
		if (!in_resync) {
			if (act == APPND) {
				paxwarn(1,"Unable to append, archive header flaw");
				return(-1);
			}
			paxwarn(1,"Invalid header, starting valid header search.");
			++in_resync;
		}
		memmove(hdbuf, hdbuf+1, shftsz);
		res = 1;
		hdend = hdbuf + shftsz;
	}

	/*
	 * ok got a valid header, check for trailer if format encodes it in
	 * the header.
	 */
	if (frmt->inhead && ((*frmt->trail_cpio)(arcn) == 0)) {
		/*
		 * valid trailer found, drain input as required
		 */
		ar_drain();
		return(-1);
	}

	++flcnt;
	return(0);
}

/*
 * get_arc()
 *	Figure out what format an archive is. Handles archive with flaws by
 *	brute force searches for a legal header in any supported format. The
 *	format id routines have to be careful to NOT mis-identify a format.
 *	ASSUMED: headers fit within a BLKMULT header.
 * Return:
 *	0 if archive found -1 otherwise
 */

static int
get_arc(void)
{
	int i;
	int hdsz = 0;
	int res;
	int minhd = BLKMULT;
	char *hdend;
	int notice = 0;

	/*
	 * find the smallest header size in all archive formats and then set up
	 * to read the archive.
	 */
	for (i = 0; ford[i] >= 0; ++i) {
		if (fsub[ford[i]].hsz < minhd)
			minhd = fsub[ford[i]].hsz;
	}
	if (rd_start() < 0)
		return(-1);
	res = BLKMULT;
	hdsz = 0;
	hdend = hdbuf;
	for(;;) {
		for (;;) {
			/*
			 * fill the buffer with at least the smallest header
			 */
			i = rd_wrbuf(hdend, res);
			if (i > 0)
				hdsz += i;
			if (hdsz >= minhd)
				break;

			/*
			 * if we cannot recover from a read error quit
			 */
			if ((i == 0) || (rd_sync() < 0))
				goto out;

			/*
			 * when we get an error none of the data we already
			 * have can be used to create a legal header (we just
			 * got an error in the middle), so we throw it all out
			 * and refill the buffer with fresh data.
			 */
			res = BLKMULT;
			hdsz = 0;
			hdend = hdbuf;
			if (!notice) {
				if (act == APPND)
					return(-1);
				paxwarn(1,"Cannot identify format. Searching...");
				++notice;
			}
		}

		/*
		 * we have at least the size of the smallest header in any
		 * archive format. Look to see if we have a match. The array
		 * ford[] is used to specify the header id order to reduce the
		 * chance of incorrectly id'ing a valid header (some formats
		 * may be subsets of each other and the order would then be
		 * important).
		 */
		for (i = 0; ford[i] >= 0; ++i) {
			if ((*fsub[ford[i]].id)(hdbuf, hdsz) < 0)
				continue;
			frmt = &(fsub[ford[i]]);
			/*
			 * yuck, to avoid slow special case code in the extract
			 * routines, just push this header back as if it was
			 * not seen. We have left extra space at start of the
			 * buffer for this purpose. This is a bit ugly, but
			 * adding all the special case code is far worse.
			 */
			pback(hdbuf, hdsz);
			return(0);
		}

		/*
		 * We have a flawed archive, no match. we start searching, but
		 * we never allow additions to flawed archives
		 */
		if (!notice) {
			if (act == APPND)
				return(-1);
			paxwarn(1, "Cannot identify format. Searching...");
			++notice;
		}

		/*
		 * brute force search for a header that we can id.
		 * we shift through byte at a time. this is slow, but we cannot
		 * determine the nature of the flaw in the archive in a
		 * portable manner
		 */
		if (--hdsz > 0) {
			memmove(hdbuf, hdbuf+1, hdsz);
			res = BLKMULT - hdsz;
			hdend = hdbuf + hdsz;
		} else {
			res = BLKMULT;
			hdend = hdbuf;
			hdsz = 0;
		}
	}

    out:
	/*
	 * we cannot find a header, bow, apologize and quit
	 */
	paxwarn(1, "Sorry, unable to determine archive format.");
	return(-1);
}
