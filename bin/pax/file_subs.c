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
static char sccsid[] = "@(#)file_subs.c	8.1 (Berkeley) 5/31/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/uio.h>
#include "pax.h"
#include "options.h"
#include "extern.h"

static int
mk_link(char *,struct stat *,char *, int);

/*
 * routines that deal with file operations such as: creating, removing;
 * and setting access modes, uid/gid and times of files
 */

#define FILEBITS		(S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)
#define SETBITS			(S_ISUID | S_ISGID)
#define ABITS			(FILEBITS | SETBITS)

/*
 * file_creat()
 *	Create and open a file.
 * Return:
 *	file descriptor or -1 for failure
 */

int
file_creat(ARCHD *arcn)
{
	int fd = -1;
	mode_t file_mode;
	int oerrno;

	/*
	 * assume file doesn't exist, so just try to create it, most times this
	 * works. We have to take special handling when the file does exist. To
	 * detect this, we use O_EXCL. For example when trying to create a
	 * file and a character device or fifo exists with the same name, we
	 * can accidentally open the device by mistake (or block waiting to
	 * open). If we find that the open has failed, then spend the effort
	 * to figure out why. This strategy was found to have better average
	 * performance in common use than checking the file (and the path)
	 * first with lstat.
	 */
	file_mode = arcn->sb.st_mode & FILEBITS;
	if ((fd = open(arcn->name, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL,
	    file_mode)) >= 0)
		return(fd);

	/*
	 * the file seems to exist. First we try to get rid of it (found to be
	 * the second most common failure when traced). If this fails, only
	 * then we go to the expense to check and create the path to the file
	 */
	if (unlnk_exist(arcn->name, arcn->type) != 0)
		return(-1);

	for (;;) {
		/*
		 * try to open it again, if this fails, check all the nodes in
		 * the path and give it a final try. if chk_path() finds that
		 * it cannot fix anything, we will skip the last attempt
		 */
		if ((fd = open(arcn->name, O_WRONLY | O_CREAT | O_TRUNC,
		    file_mode)) >= 0)
			break;
		oerrno = errno;
		if (nodirs || chk_path(arcn->name,arcn->sb.st_uid,arcn->sb.st_gid) < 0) {
			syswarn(1, oerrno, "Unable to create %s", arcn->name);
			return(-1);
		}
	}
	return(fd);
}

/*
 * file_close()
 *	Close file descriptor to a file just created by pax. Sets modes,
 *	ownership and times as required.
 * Return:
 *	0 for success, -1 for failure
 */

void
file_close(ARCHD *arcn, int fd)
{
	int res = 0;

	if (fd < 0)
		return;
	if (close(fd) < 0)
		syswarn(0, errno, "Unable to close file descriptor on %s",
		    arcn->name);

	/*
	 * set owner/groups first as this may strip off mode bits we want
	 * then set file permission modes. Then set file access and
	 * modification times.
	 */
	if (pids)
		res = set_ids(arcn->name, arcn->sb.st_uid, arcn->sb.st_gid);

	/*
	 * IMPORTANT SECURITY NOTE:
	 * if not preserving mode or we cannot set uid/gid, then PROHIBIT
	 * set uid/gid bits
	 */
	if (!pmode || res)
		arcn->sb.st_mode &= ~(SETBITS);
	if (pmode)
		set_pmode(arcn->name, arcn->sb.st_mode);
	if (patime || pmtime)
		set_ftime(arcn->name, arcn->sb.st_mtime, arcn->sb.st_atime, 0);
}

/*
 * lnk_creat()
 *	Create a hard link to arcn->ln_name from arcn->name. arcn->ln_name
 *	must exist;
 * Return:
 *	0 if ok, -1 otherwise
 */

int
lnk_creat(ARCHD *arcn)
{
	struct stat sb;

	/*
	 * we may be running as root, so we have to be sure that link target
	 * is not a directory, so we lstat and check
	 */
	if (lstat(arcn->ln_name, &sb) < 0) {
		syswarn(1,errno,"Unable to link to %s from %s", arcn->ln_name,
		    arcn->name);
		return(-1);
	}

	if (S_ISDIR(sb.st_mode)) {
		paxwarn(1, "A hard link to the directory %s is not allowed",
		    arcn->ln_name);
		return(-1);
	}

	return(mk_link(arcn->ln_name, &sb, arcn->name, 0));
}

/*
 * cross_lnk()
 *	Create a hard link to arcn->org_name from arcn->name. Only used in copy
 *	with the -l flag. No warning or error if this does not succeed (we will
 *	then just create the file)
 * Return:
 *	1 if copy() should try to create this file node
 *	0 if cross_lnk() ok, -1 for fatal flaw (like linking to self).
 */

int
cross_lnk(ARCHD *arcn)
{
	/*
	 * try to make a link to original file (-l flag in copy mode). make sure
	 * we do not try to link to directories in case we are running as root
	 * (and it might succeed).
	 */
	if (arcn->type == PAX_DIR)
		return(1);
	return(mk_link(arcn->org_name, &(arcn->sb), arcn->name, 1));
}

/*
 * chk_same()
 *	In copy mode if we are not trying to make hard links between the src
 *	and destinations, make sure we are not going to overwrite ourselves by
 *	accident. This slows things down a little, but we have to protect all
 *	those people who make typing errors.
 * Return:
 *	1 the target does not exist, go ahead and copy
 *	0 skip it file exists (-k) or may be the same as source file
 */

int
chk_same(ARCHD *arcn)
{
	struct stat sb;

	/*
	 * if file does not exist, return. if file exists and -k, skip it
	 * quietly
	 */
	if (lstat(arcn->name, &sb) < 0)
		return(1);
	if (kflag)
		return(0);

	/*
	 * better make sure the user does not have src == dest by mistake
	 */
	if ((arcn->sb.st_dev == sb.st_dev) && (arcn->sb.st_ino == sb.st_ino)) {
		paxwarn(1, "Unable to copy %s, file would overwrite itself",
		    arcn->name);
		return(0);
	}
	return(1);
}

/*
 * mk_link()
 *	try to make a hard link between two files. if ign set, we do not
 *	complain.
 * Return:
 *	0 if successful (or we are done with this file but no error, such as
 *	finding the from file exists and the user has set -k).
 *	1 when ign was set to indicates we could not make the link but we
 *	should try to copy/extract the file as that might work (and is an
 *	allowed option). -1 an error occurred.
 */

static int
mk_link(char *to, struct stat *to_sb, char *from,
	int ign)
{
	struct stat sb;
	int oerrno;

	/*
	 * if from file exists, it has to be unlinked to make the link. If the
	 * file exists and -k is set, skip it quietly
	 */
	if (lstat(from, &sb) == 0) {
		if (kflag)
			return(0);

		/*
		 * make sure it is not the same file, protect the user
		 */
		if ((to_sb->st_dev==sb.st_dev)&&(to_sb->st_ino == sb.st_ino)) {
			paxwarn(1, "Unable to link file %s to itself", to);
			return(-1);
		}

		/*
		 * try to get rid of the file, based on the type
		 */
		if (S_ISDIR(sb.st_mode)) {
			if (rmdir(from) < 0) {
				syswarn(1, errno, "Unable to remove %s", from);
				return(-1);
			}
		} else if (unlink(from) < 0) {
			if (!ign) {
				syswarn(1, errno, "Unable to remove %s", from);
				return(-1);
			}
			return(1);
		}
	}

	/*
	 * from file is gone (or did not exist), try to make the hard link.
	 * if it fails, check the path and try it again (if chk_path() says to
	 * try again)
	 */
	for (;;) {
		if (link(to, from) == 0)
			break;
		oerrno = errno;
		if (!nodirs && chk_path(from, to_sb->st_uid, to_sb->st_gid) == 0)
			continue;
		if (!ign) {
			syswarn(1, oerrno, "Could not link to %s from %s", to,
			    from);
			return(-1);
		}
		return(1);
	}

	/*
	 * all right the link was made
	 */
	return(0);
}

/*
 * node_creat()
 *	create an entry in the file system (other than a file or hard link).
 *	If successful, sets uid/gid modes and times as required.
 * Return:
 *	0 if ok, -1 otherwise
 */

int
node_creat(ARCHD *arcn)
{
	int res;
	int ign = 0;
	int oerrno;
	int pass = 0;
	mode_t file_mode;
	struct stat sb;

	/*
	 * create node based on type, if that fails try to unlink the node and
	 * try again. finally check the path and try again. As noted in the
	 * file and link creation routines, this method seems to exhibit the
	 * best performance in general use workloads.
	 */
	file_mode = arcn->sb.st_mode & FILEBITS;

	for (;;) {
		switch(arcn->type) {
		case PAX_DIR:
			res = mkdir(arcn->name, file_mode);
			if (ign)
				res = 0;
			break;
		case PAX_CHR:
			file_mode |= S_IFCHR;
			res = mknod(arcn->name, file_mode, arcn->sb.st_rdev);
			break;
		case PAX_BLK:
			file_mode |= S_IFBLK;
			res = mknod(arcn->name, file_mode, arcn->sb.st_rdev);
			break;
		case PAX_FIF:
			res = mkfifo(arcn->name, file_mode);
			break;
		case PAX_SCK:
			/*
			 * Skip sockets, operation has no meaning under BSD
			 */
			paxwarn(0,
			    "%s skipped. Sockets cannot be copied or extracted",
			    arcn->name);
			return(-1);
		case PAX_SLK:
			res = symlink(arcn->ln_name, arcn->name);
			break;
		case PAX_CTG:
		case PAX_HLK:
		case PAX_HRG:
		case PAX_REG:
		default:
			/*
			 * we should never get here
			 */
			paxwarn(0, "%s has an unknown file type, skipping",
				arcn->name);
			return(-1);
		}

		/*
		 * if we were able to create the node break out of the loop,
		 * otherwise try to unlink the node and try again. if that
		 * fails check the full path and try a final time.
		 */
		if (res == 0)
			break;

		/*
		 * we failed to make the node
		 */
		oerrno = errno;
		if ((ign = unlnk_exist(arcn->name, arcn->type)) < 0)
			return(-1);

		if (++pass <= 1)
			continue;

		if (nodirs || chk_path(arcn->name,arcn->sb.st_uid,arcn->sb.st_gid) < 0) {
			syswarn(1, oerrno, "Could not create: %s", arcn->name);
			return(-1);
		}
	}

	/*
	 * we were able to create the node. set uid/gid, modes and times
	 */
	if (pids)
		res = set_ids(arcn->name, arcn->sb.st_uid, arcn->sb.st_gid);
	else
		res = 0;

	/*
	 * IMPORTANT SECURITY NOTE:
	 * if not preserving mode or we cannot set uid/gid, then PROHIBIT any
	 * set uid/gid bits
	 */
	if (!pmode || res)
		arcn->sb.st_mode &= ~(SETBITS);
	if (pmode)
		set_pmode(arcn->name, arcn->sb.st_mode);

	if (arcn->type == PAX_DIR && strcmp(NM_CPIO, argv0) != 0) {
		/*
		 * Dirs must be processed again at end of extract to set times
		 * and modes to agree with those stored in the archive. However
		 * to allow extract to continue, we may have to also set owner
		 * rights. This allows nodes in the archive that are children
		 * of this directory to be extracted without failure. Both time
		 * and modes will be fixed after the entire archive is read and
		 * before pax exits.
		 */
		if (access(arcn->name, R_OK | W_OK | X_OK) < 0) {
			if (lstat(arcn->name, &sb) < 0) {
				syswarn(0, errno,"Could not access %s (stat)",
				    arcn->name);
				set_pmode(arcn->name,file_mode | S_IRWXU);
			} else {
				/*
				 * We have to add rights to the dir, so we make
				 * sure to restore the mode. The mode must be
				 * restored AS CREATED and not as stored if
				 * pmode is not set.
				 */
				set_pmode(arcn->name,
				    ((sb.st_mode & FILEBITS) | S_IRWXU));
				if (!pmode)
					arcn->sb.st_mode = sb.st_mode;
			}

			/*
			 * we have to force the mode to what was set here,
			 * since we changed it from the default as created.
			 */
			add_dir(arcn->name, arcn->nlen, &(arcn->sb), 1);
		} else if (pmode || patime || pmtime)
			add_dir(arcn->name, arcn->nlen, &(arcn->sb), 0);
	}

	if (patime || pmtime)
		set_ftime(arcn->name, arcn->sb.st_mtime, arcn->sb.st_atime, 0);
	return(0);
}

/*
 * unlnk_exist()
 *	Remove node from file system with the specified name. We pass the type
 *	of the node that is going to replace it. When we try to create a
 *	directory and find that it already exists, we allow processing to
 *	continue as proper modes etc will always be set for it later on.
 * Return:
 *	0 is ok to proceed, no file with the specified name exists
 *	-1 we were unable to remove the node, or we should not remove it (-k)
 *	1 we found a directory and we were going to create a directory.
 */

int
unlnk_exist(char *name, int type)
{
	struct stat sb;

	/*
	 * the file does not exist, or -k we are done
	 */
	if (lstat(name, &sb) < 0)
		return(0);
	if (kflag)
		return(-1);

	if (S_ISDIR(sb.st_mode)) {
		/*
		 * try to remove a directory, if it fails and we were going to
		 * create a directory anyway, tell the caller (return a 1)
		 */
		if (rmdir(name) < 0) {
			if (type == PAX_DIR)
				return(1);
			syswarn(1,errno,"Unable to remove directory %s", name);
			return(-1);
		}
		return(0);
	}

	/*
	 * try to get rid of all non-directory type nodes
	 */
	if (unlink(name) < 0) {
		syswarn(1, errno, "Could not unlink %s", name);
		return(-1);
	}
	return(0);
}

/*
 * chk_path()
 *	We were trying to create some kind of node in the file system and it
 *	failed. chk_path() makes sure the path up to the node exists and is
 *	writeable. When we have to create a directory that is missing along the
 *	path somewhere, the directory we create will be set to the same
 *	uid/gid as the file has (when uid and gid are being preserved).
 *	NOTE: this routine is a real performance loss. It is only used as a
 *	last resort when trying to create entries in the file system.
 * Return:
 *	-1 when it could find nothing it is allowed to fix.
 *	0 otherwise
 */

int
chk_path( char *name, uid_t st_uid, gid_t st_gid)
{
	char *spt = name;
	struct stat sb;
	int retval = -1;

	/*
	 * watch out for paths with nodes stored directly in / (e.g. /bozo)
	 */
	if (*spt == '/')
		++spt;

	for(;;) {
		/*
		 * work forward from the first / and check each part of the path
		 */
		spt = strchr(spt, '/');
		if (spt == NULL)
			break;
		*spt = '\0';

		/*
		 * if it exists we assume it is a directory, it is not within
		 * the spec (at least it seems to read that way) to alter the
		 * file system for nodes NOT EXPLICITLY stored on the archive.
		 * If that assumption is changed, you would test the node here
		 * and figure out how to get rid of it (probably like some
		 * recursive unlink()) or fix up the directory permissions if
		 * required (do an access()).
		 */
		if (lstat(name, &sb) == 0) {
			*(spt++) = '/';
			continue;
		}

		/*
		 * the path fails at this point, see if we can create the
		 * needed directory and continue on
		 */
		if (mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
			*spt = '/';
			retval = -1;
			break;
		}

		/*
		 * we were able to create the directory. We will tell the
		 * caller that we found something to fix, and it is ok to try
		 * and create the node again.
		 */
		retval = 0;
		if (pids)
			(void)set_ids(name, st_uid, st_gid);

		/*
		 * make sure the user doesn't have some strange umask that
		 * causes this newly created directory to be unusable. We fix
		 * the modes and restore them back to the creation default at
		 * the end of pax
		 */
		if ((access(name, R_OK | W_OK | X_OK) < 0) &&
		    (lstat(name, &sb) == 0)) {
			set_pmode(name, ((sb.st_mode & FILEBITS) | S_IRWXU));
			add_dir(name, spt - name, &sb, 1);
		}
		*(spt++) = '/';
		continue;
	}
	return(retval);
}

/*
 * set_ftime()
 *	Set the access time and modification time for a named file. If frc is
 *	non-zero we force these times to be set even if the user did not
 *	request access and/or modification time preservation (this is also
 *	used by -t to reset access times).
 *	When ign is zero, only those times the user has asked for are set, the
 *	other ones are left alone. We do not assume the un-documented feature
 *	of many lutimes() implementations that consider a 0 time value as a do
 *	not set request.
 */

void
set_ftime(char *fnm, time_t mtime, time_t atime, int frc)
{
	static struct timeval tv[2] = {{0L, 0L}, {0L, 0L}};
	struct stat sb;

	tv[0].tv_sec = atime;
	tv[1].tv_sec = mtime;
	if (!frc && (!patime || !pmtime)) {
		/*
		 * if we are not forcing, only set those times the user wants
		 * set. We get the current values of the times if we need them.
		 */
		if (lstat(fnm, &sb) == 0) {
			if (!patime)
				tv[0].tv_sec = sb.st_atime;
			if (!pmtime)
				tv[1].tv_sec = sb.st_mtime;
		} else
			syswarn(0,errno,"Unable to obtain file stats %s", fnm);
	}

	/*
	 * set the times
	 */
	if (lutimes(fnm, tv) < 0)
		syswarn(1, errno, "Access/modification time set failed on: %s",
		    fnm);
	return;
}

/*
 * set_ids()
 *	set the uid and gid of a file system node
 * Return:
 *	0 when set, -1 on failure
 */

int
set_ids(char *fnm, uid_t uid, gid_t gid)
{
	if (lchown(fnm, uid, gid) < 0) {
		/*
		 * ignore EPERM unless in verbose mode or being run by root.
		 * if running as pax, POSIX requires a warning.
		 */
		if (strcmp(NM_PAX, argv0) == 0 || errno != EPERM || vflag ||
		    geteuid() == 0)
			syswarn(1, errno, "Unable to set file uid/gid of %s",
			    fnm);
		return(-1);
	}
	return(0);
}

/*
 * set_pmode()
 *	Set file access mode
 */

void
set_pmode(char *fnm, mode_t mode)
{
	mode &= ABITS;
	if (lchmod(fnm, mode) < 0)
		syswarn(1, errno, "Could not set permissions on %s", fnm);
	return;
}

/*
 * file_write()
 *	Write/copy a file (during copy or archive extract). This routine knows
 *	how to copy files with lseek holes in it. (Which are read as file
 *	blocks containing all 0's but do not have any file blocks associated
 *	with the data). Typical examples of these are files created by dbm
 *	variants (.pag files). While the file size of these files are huge, the
 *	actual storage is quite small (the files are sparse). The problem is
 *	the holes read as all zeros so are probably stored on the archive that
 *	way (there is no way to determine if the file block is really a hole,
 *	we only know that a file block of all zero's can be a hole).
 *	At this writing, no major archive format knows how to archive files
 *	with holes. However, on extraction (or during copy, -rw) we have to
 *	deal with these files. Without detecting the holes, the files can
 *	consume a lot of file space if just written to disk. This replacement
 *	for write when passed the basic allocation size of a file system block,
 *	uses lseek whenever it detects the input data is all 0 within that
 *	file block. In more detail, the strategy is as follows:
 *	While the input is all zero keep doing an lseek. Keep track of when we
 *	pass over file block boundaries. Only write when we hit a non zero
 *	input. once we have written a file block, we continue to write it to
 *	the end (we stop looking at the input). When we reach the start of the
 *	next file block, start checking for zero blocks again. Working on file
 *	block boundaries significantly reduces the overhead when copying files
 *	that are NOT very sparse. This overhead (when compared to a write) is
 *	almost below the measurement resolution on many systems. Without it,
 *	files with holes cannot be safely copied. It does has a side effect as
 *	it can put holes into files that did not have them before, but that is
 *	not a problem since the file contents are unchanged (in fact it saves
 *	file space). (Except on paging files for diskless clients. But since we
 *	cannot determine one of those file from here, we ignore them). If this
 *	ever ends up on a system where CTG files are supported and the holes
 *	are not desired, just do a conditional test in those routines that
 *	call file_write() and have it call write() instead. BEFORE CLOSING THE
 *	FILE, make sure to call file_flush() when the last write finishes with
 *	an empty block. A lot of file systems will not create an lseek hole at
 *	the end. In this case we drop a single 0 at the end to force the
 *	trailing 0's in the file.
 *	---Parameters---
 *	rem: how many bytes left in this file system block
 *	isempt: have we written to the file block yet (is it empty)
 *	sz: basic file block allocation size
 *	cnt: number of bytes on this write
 *	str: buffer to write
 * Return:
 *	number of bytes written, -1 on write (or lseek) error.
 */

int
file_write(int fd, char *str, int cnt, int *rem, int *isempt, int sz,
	char *name)
{
	char *pt;
	char *end;
	int wcnt;
	char *st = str;

	/*
	 * while we have data to process
	 */
	while (cnt) {
		if (!*rem) {
			/*
			 * We are now at the start of file system block again
			 * (or what we think one is...). start looking for
			 * empty blocks again
			 */
			*isempt = 1;
			*rem = sz;
		}

		/*
		 * only examine up to the end of the current file block or
		 * remaining characters to write, whatever is smaller
		 */
		wcnt = MIN(cnt, *rem);
		cnt -= wcnt;
		*rem -= wcnt;
		if (*isempt) {
			/*
			 * have not written to this block yet, so we keep
			 * looking for zero's
			 */
			pt = st;
			end = st + wcnt;

			/*
			 * look for a zero filled buffer
			 */
			while ((pt < end) && (*pt == '\0'))
				++pt;

			if (pt == end) {
				/*
				 * skip, buf is empty so far
				 */
				if (lseek(fd, (off_t)wcnt, SEEK_CUR) < 0) {
					syswarn(1,errno,"File seek on %s",
					    name);
					return(-1);
				}
				st = pt;
				continue;
			}
			/*
			 * drat, the buf is not zero filled
			 */
			*isempt = 0;
		}

		/*
		 * have non-zero data in this file system block, have to write
		 */
		if (write(fd, st, wcnt) != wcnt) {
			syswarn(1, errno, "Failed write to file %s", name);
			return(-1);
		}
		st += wcnt;
	}
	return(st - str);
}

/*
 * file_flush()
 *	when the last file block in a file is zero, many file systems will not
 *	let us create a hole at the end. To get the last block with zeros, we
 *	write the last BYTE with a zero (back up one byte and write a zero).
 */

void
file_flush(int fd, char *fname, int isempt)
{
	static char blnk[] = "\0";

	/*
	 * silly test, but make sure we are only called when the last block is
	 * filled with all zeros.
	 */
	if (!isempt)
		return;

	/*
	 * move back one byte and write a zero
	 */
	if (lseek(fd, (off_t)-1, SEEK_CUR) < 0) {
		syswarn(1, errno, "Failed seek on file %s", fname);
		return;
	}

	if (write(fd, blnk, 1) < 0)
		syswarn(1, errno, "Failed write to file %s", fname);
	return;
}

/*
 * rdfile_close()
 *	close a file we have beed reading (to copy or archive). If we have to
 *	reset access time (tflag) do so (the times are stored in arcn).
 */

void
rdfile_close(ARCHD *arcn, int *fd)
{
	/*
	 * make sure the file is open
	 */
	if (*fd < 0)
		return;

	(void)close(*fd);
	*fd = -1;
	if (!tflag)
		return;

	/*
	 * user wants last access time reset
	 */
	set_ftime(arcn->org_name, arcn->sb.st_mtime, arcn->sb.st_atime, 1);
	return;
}

/*
 * set_crc()
 *	read a file to calculate its crc. This is a real drag. Archive formats
 *	that have this, end up reading the file twice (we have to write the
 *	header WITH the crc before writing the file contents. Oh well...
 * Return:
 *	0 if was able to calculate the crc, -1 otherwise
 */

int
set_crc(ARCHD *arcn, int fd)
{
	int i;
	int res;
	off_t cpcnt = 0L;
	u_long size;
	unsigned long crc = 0L;
	char tbuf[FILEBLK];
	struct stat sb;

	if (fd < 0) {
		/*
		 * hmm, no fd, should never happen. well no crc then.
		 */
		arcn->crc = 0L;
		return(0);
	}

	if ((size = (u_long)arcn->sb.st_blksize) > (u_long)sizeof(tbuf))
		size = (u_long)sizeof(tbuf);

	/*
	 * read all the bytes we think that there are in the file. If the user
	 * is trying to archive an active file, forget this file.
	 */
	for(;;) {
		if ((res = read(fd, tbuf, size)) <= 0)
			break;
		cpcnt += res;
		for (i = 0; i < res; ++i)
			crc += (tbuf[i] & 0xff);
	}

	/*
	 * safety check. we want to avoid archiving files that are active as
	 * they can create inconsistent archive copies.
	 */
	if (cpcnt != arcn->sb.st_size)
		paxwarn(1, "File changed size %s", arcn->org_name);
	else if (fstat(fd, &sb) < 0)
		syswarn(1, errno, "Failed stat on %s", arcn->org_name);
	else if (arcn->sb.st_mtime != sb.st_mtime)
		paxwarn(1, "File %s was modified during read", arcn->org_name);
	else if (lseek(fd, (off_t)0L, SEEK_SET) < 0)
		syswarn(1, errno, "File rewind failed on: %s", arcn->org_name);
	else {
		arcn->crc = crc;
		return(0);
	}
	return(-1);
}
