/*	$OpenBSD: file_subs.c,v 1.57 2024/07/14 14:32:02 jca Exp $	*/
/*	$NetBSD: file_subs.c,v 1.4 1995/03/21 09:07:18 cgd Exp $	*/

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

#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pax.h"
#include "extern.h"

static int fset_ids(char *, int, uid_t, gid_t);
static int unlnk_exist(char *, int);
static int chk_path(char *, uid_t, gid_t, int);
static int mk_link(char *, struct stat *, char *, int);
static void fset_ftime(const char *, int, const struct timespec *,
    const struct timespec *, int);
static void fset_pmode(char *, int, mode_t);

/*
 * routines that deal with file operations such as: creating, removing;
 * and setting access modes, uid/gid and times of files
 */

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
	 * Assume file doesn't exist, so just try to create it, most times this
	 * works. We have to take special handling when the file does exist. To
	 * detect this, we use O_EXCL. For example when trying to create a
	 * file and a character device or fifo exists with the same name, we
	 * can accidently open the device by mistake (or block waiting to open).
	 * If we find that the open has failed, then spend the effort to
	 * figure out why. This strategy was found to have better average
	 * performance in common use than checking the file (and the path)
	 * first with lstat.
	 */
	file_mode = arcn->sb.st_mode & FILEBITS;
	if ((fd = open(arcn->name, O_WRONLY | O_CREAT | O_EXCL,
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
		if (nodirs || chk_path(arcn->name,arcn->sb.st_uid,arcn->sb.st_gid, 0) < 0) {
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

	/*
	 * set owner/groups first as this may strip off mode bits we want
	 * then set file permission modes. Then set file access and
	 * modification times.
	 */
	if (pids)
		res = fset_ids(arcn->name, fd, arcn->sb.st_uid,
		    arcn->sb.st_gid);

	/*
	 * IMPORTANT SECURITY NOTE:
	 * if not preserving mode or we cannot set uid/gid, then PROHIBIT
	 * set uid/gid bits
	 */
	if (!pmode || res)
		arcn->sb.st_mode &= ~(SETBITS);
	if (pmode)
		fset_pmode(arcn->name, fd, arcn->sb.st_mode);
	if (patime || pmtime)
		fset_ftime(arcn->name, fd, &arcn->sb.st_mtim,
		    &arcn->sb.st_atim, 0);
	if (close(fd) == -1)
		syswarn(0, errno, "Unable to close file descriptor on %s",
		    arcn->name);
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
	int res;

	/*
	 * we may be running as root, so we have to be sure that link target
	 * is not a directory, so we lstat and check
	 */
	if (lstat(arcn->ln_name, &sb) == -1) {
		syswarn(1,errno,"Unable to link to %s from %s", arcn->ln_name,
		    arcn->name);
		return(-1);
	}

	if (S_ISDIR(sb.st_mode)) {
		paxwarn(1, "A hard link to the directory %s is not allowed",
		    arcn->ln_name);
		return(-1);
	}

	res = mk_link(arcn->ln_name, &sb, arcn->name, 0);
	if (res == 0) {
		/* check for a hardlink to a placeholder symlink */
		res = sltab_add_link(arcn->name, &sb);

		if (res < 0) {
			/* arrgh, it failed, clean up */
			unlink(arcn->name);
		}
	}

	return (res);
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
	 * try to make a link to original file (-l flag in copy mode). make
	 * sure we do not try to link to directories in case we are running as
	 * root (and it might succeed).
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
	if (lstat(arcn->name, &sb) == -1)
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
mk_link(char *to, struct stat *to_sb, char *from, int ign)
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
			if (rmdir(from) == -1) {
				syswarn(1, errno, "Unable to remove %s", from);
				return(-1);
			}
			delete_dir(sb.st_dev, sb.st_ino);
		} else if (unlink(from) == -1) {
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
		if (linkat(AT_FDCWD, to, AT_FDCWD, from, 0) == 0)
			break;
		oerrno = errno;
		if (!nodirs && chk_path(from, to_sb->st_uid, to_sb->st_gid, ign) == 0)
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
	char target[PATH_MAX];
	char *nm = arcn->name;
	int len, defer_pmode = 0;

	/*
	 * create node based on type, if that fails try to unlink the node and
	 * try again. finally check the path and try again. As noted in the
	 * file and link creation routines, this method seems to exhibit the
	 * best performance in general use workloads.
	 */
	file_mode = arcn->sb.st_mode & FILEBITS;

	for (;;) {
		switch (arcn->type) {
		case PAX_DIR:
			/*
			 * If -h (or -L) was given in tar-mode, follow the
			 * potential symlink chain before trying to create the
			 * directory.
			 */
			if (op_mode == OP_TAR && Lflag) {
				while (lstat(nm, &sb) == 0 &&
				    S_ISLNK(sb.st_mode)) {
					len = readlink(nm, target,
					    sizeof target - 1);
					if (len == -1) {
						syswarn(0, errno,
						   "cannot follow symlink %s in chain for %s",
						    nm, arcn->name);
						res = -1;
						goto badlink;
					}
					target[len] = '\0';
					nm = target;
				}
			}
			res = mkdir(nm, file_mode);

badlink:
			if (ign)
				res = 0;
			break;
		case PAX_CHR:
			file_mode |= S_IFCHR;
			res = mknod(nm, file_mode, arcn->sb.st_rdev);
			break;
		case PAX_BLK:
			file_mode |= S_IFBLK;
			res = mknod(nm, file_mode, arcn->sb.st_rdev);
			break;
		case PAX_FIF:
			res = mkfifo(nm, file_mode);
			break;
		case PAX_SCK:
			/*
			 * Skip sockets, operation has no meaning under BSD
			 */
			paxwarn(0,
			    "%s skipped. Sockets cannot be copied or extracted",
			    nm);
			return(-1);
		case PAX_SLK:
			if (arcn->ln_name[0] != '/' &&
			    !has_dotdot(arcn->ln_name))
				res = symlink(arcn->ln_name, nm);
			else {
				/*
				 * absolute symlinks and symlinks with ".."
				 * have to be deferred to prevent the archive
				 * from bootstrapping itself to outside the
				 * working directory.
				 */
				res = sltab_add_sym(nm, arcn->ln_name,
				    arcn->sb.st_mode);
				if (res == 0)
					defer_pmode = 1;
			}
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
				nm);
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
		if ((ign = unlnk_exist(nm, arcn->type)) < 0)
			return(-1);

		if (++pass <= 1)
			continue;

		if (nodirs || chk_path(nm,arcn->sb.st_uid,arcn->sb.st_gid, 0) < 0) {
			syswarn(1, oerrno, "Could not create: %s", nm);
			return(-1);
		}
	}

	/*
	 * we were able to create the node. set uid/gid, modes and times
	 */
	if (pids)
		res = set_ids(nm, arcn->sb.st_uid, arcn->sb.st_gid);
	else
		res = 0;

	/*
	 * IMPORTANT SECURITY NOTE:
	 * if not preserving mode or we cannot set uid/gid, then PROHIBIT any
	 * set uid/gid bits
	 */
	if (!pmode || res)
		arcn->sb.st_mode &= ~(SETBITS);
	if (pmode && !defer_pmode)
		set_pmode(nm, arcn->sb.st_mode);

	if (arcn->type == PAX_DIR && op_mode != OP_CPIO) {
		/*
		 * Dirs must be processed again at end of extract to set times
		 * and modes to agree with those stored in the archive. However
		 * to allow extract to continue, we may have to also set owner
		 * rights. This allows nodes in the archive that are children
		 * of this directory to be extracted without failure. Both time
		 * and modes will be fixed after the entire archive is read and
		 * before pax exits.  To do that safely, we want the dev+ino
		 * of the directory we created.
		 */
		if (lstat(nm, &sb) == -1) {
			syswarn(0, errno,"Could not access %s (stat)", nm);
		} else if (access(nm, R_OK | W_OK | X_OK) == -1) {
			/*
			 * We have to add rights to the dir, so we make
			 * sure to restore the mode. The mode must be
			 * restored AS CREATED and not as stored if
			 * pmode is not set.
			 */
			set_pmode(nm,
			    ((sb.st_mode & FILEBITS) | S_IRWXU));
			if (!pmode)
				arcn->sb.st_mode = sb.st_mode;

			/*
			 * we have to force the mode to what was set
			 * here, since we changed it from the default
			 * as created.
			 */
			arcn->sb.st_dev = sb.st_dev;
			arcn->sb.st_ino = sb.st_ino;
			add_dir(nm, &(arcn->sb), 1);
		} else if (pmode || patime || pmtime) {
			arcn->sb.st_dev = sb.st_dev;
			arcn->sb.st_ino = sb.st_ino;
			add_dir(nm, &(arcn->sb), 0);
		}
	} else if (patime || pmtime)
		set_ftime(nm, &arcn->sb.st_mtim, &arcn->sb.st_atim, 0);
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

static int
unlnk_exist(char *name, int type)
{
	struct stat sb;

	/*
	 * the file does not exist, or -k we are done
	 */
	if (lstat(name, &sb) == -1)
		return(0);
	if (kflag)
		return(-1);

	if (S_ISDIR(sb.st_mode)) {
		/*
		 * try to remove a directory, if it fails and we were going to
		 * create a directory anyway, tell the caller (return a 1)
		 */
		if (rmdir(name) == -1) {
			if (type == PAX_DIR)
				return(1);
			syswarn(1,errno,"Unable to remove directory %s", name);
			return(-1);
		}
		delete_dir(sb.st_dev, sb.st_ino);
		return(0);
	}

	/*
	 * try to get rid of all non-directory type nodes
	 */
	if (unlink(name) == -1) {
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
chk_path(char *name, uid_t st_uid, gid_t st_gid, int ign)
{
	char *spt = name;
	char *next;
	struct stat sb;
	int retval = -1;

	/*
	 * watch out for paths with nodes stored directly in / (e.g. /bozo)
	 */
	while (*spt == '/')
		++spt;

	for (;;) {
		/*
		 * work forward from the first / and check each part of the path
		 */
		spt = strchr(spt, '/');
		if (spt == NULL)
			break;

		/*
		 * skip over duplicate slashes; stop if there're only
		 * trailing slashes left
		 */
		next = spt + 1;
		while (*next == '/')
			next++;
		if (*next == '\0')
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
			*spt = '/';
			spt = next;
			continue;
		}

		/*
		 * the path fails at this point, see if we can create the
		 * needed directory and continue on
		 */
		if (mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
			if (!ign)
				syswarn(1, errno, "Unable to mkdir %s", name);
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
		if ((access(name, R_OK | W_OK | X_OK) == -1) &&
		    (lstat(name, &sb) == 0)) {
			set_pmode(name, ((sb.st_mode & FILEBITS) | S_IRWXU));
			add_dir(name, &sb, 1);
		}
		*spt = '/';
		spt = next;
		continue;
	}
	return(retval);
}

/*
 * set_ftime()
 *	Set the access time and modification time for a named file. If frc
 *	is non-zero we force these times to be set even if the user did not
 *	request access and/or modification time preservation (this is also
 *	used by -t to reset access times).
 *	When ign is zero, only those times the user has asked for are set, the
 *	other ones are left alone.
 */

void
set_ftime(const char *fnm, const struct timespec *mtimp,
    const struct timespec *atimp, int frc)
{
	struct timespec tv[2];

	tv[0] = *atimp;
	tv[1] = *mtimp;

	if (!frc) {
		/*
		 * if we are not forcing, only set those times the user wants
		 * set.
		 */
		if (!patime)
			tv[0].tv_nsec = UTIME_OMIT;
		if (!pmtime)
			tv[1].tv_nsec = UTIME_OMIT;
	}

	/*
	 * set the times
	 */
	if (utimensat(AT_FDCWD, fnm, tv, AT_SYMLINK_NOFOLLOW) < 0)
		syswarn(1, errno, "Access/modification time set failed on: %s",
		    fnm);
}

static void
fset_ftime(const char *fnm, int fd, const struct timespec *mtimp,
    const struct timespec *atimp, int frc)
{
	struct timespec tv[2];


	tv[0] = *atimp;
	tv[1] = *mtimp;

	if (!frc) {
		/*
		 * if we are not forcing, only set those times the user wants
		 * set.
		 */
		if (!patime)
			tv[0].tv_nsec = UTIME_OMIT;
		if (!pmtime)
			tv[1].tv_nsec = UTIME_OMIT;
	}
	/*
	 * set the times
	 */
	if (futimens(fd, tv) == -1)
		syswarn(1, errno, "Access/modification time set failed on: %s",
		    fnm);
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
	if (fchownat(AT_FDCWD, fnm, uid, gid, AT_SYMLINK_NOFOLLOW) == -1) {
		/*
		 * ignore EPERM unless in verbose mode or being run by root.
		 * if running as pax, POSIX requires a warning.
		 */
		if (op_mode == OP_PAX || errno != EPERM || vflag ||
		    geteuid() == 0)
			syswarn(1, errno, "Unable to set file uid/gid of %s",
			    fnm);
		return(-1);
	}
	return(0);
}

int
fset_ids(char *fnm, int fd, uid_t uid, gid_t gid)
{
	if (fchown(fd, uid, gid) == -1) {
		/*
		 * ignore EPERM unless in verbose mode or being run by root.
		 * if running as pax, POSIX requires a warning.
		 */
		if (op_mode == OP_PAX || errno != EPERM || vflag ||
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
	if (fchmodat(AT_FDCWD, fnm, mode, AT_SYMLINK_NOFOLLOW) == -1)
		syswarn(1, errno, "Could not set permissions on %s", fnm);
}

static void
fset_pmode(char *fnm, int fd, mode_t mode)
{
	mode &= ABITS;
	if (fchmod(fd, mode) == -1)
		syswarn(1, errno, "Could not set permissions on %s", fnm);
}

/*
 * set_attr()
 *	Given a DIRDATA, restore the mode and times as indicated, but
 *	only after verifying that it's the directory that we wanted.
 */
int
set_attr(const struct file_times *ft, int force_times, mode_t mode,
    int do_mode, int in_sig)
{
	struct stat sb;
	int fd, r;

	if (!do_mode && !force_times && !patime && !pmtime)
		return (0);

	/*
	 * We could legitimately go through a symlink here,
	 * so do *not* use O_NOFOLLOW.  The dev+ino check will
	 * protect us from evil.
	 */
	fd = open(ft->ft_name, O_RDONLY | O_DIRECTORY);
	if (fd == -1) {
		if (!in_sig)
			syswarn(1, errno, "Unable to restore mode and times"
			    " for directory: %s", ft->ft_name);
		return (-1);
	}

	if (fstat(fd, &sb) == -1) {
		if (!in_sig)
			syswarn(1, errno, "Unable to stat directory: %s",
			    ft->ft_name);
		r = -1;
	} else if (ft->ft_ino != sb.st_ino || ft->ft_dev != sb.st_dev) {
		if (!in_sig)
			paxwarn(1, "Directory vanished before restoring"
			    " mode and times: %s", ft->ft_name);
		r = -1;
	} else {
		/* Whew, it's a match!  Is there anything to change? */
		if (do_mode && (mode & ABITS) != (sb.st_mode & ABITS))
			fset_pmode(ft->ft_name, fd, mode);
		if (((force_times || patime) &&
		    timespeccmp(&ft->ft_atim, &sb.st_atim, !=)) ||
		    ((force_times || pmtime) &&
		    timespeccmp(&ft->ft_mtim, &sb.st_mtim, !=)))
			fset_ftime(ft->ft_name, fd, &ft->ft_mtim,
			    &ft->ft_atim, force_times);
		r = 0;
	}
	close(fd);

	return (r);
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
	char **strp;

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
		wcnt = MINIMUM(cnt, *rem);
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
				if (fd > -1 &&
				    lseek(fd, wcnt, SEEK_CUR) < 0) {
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
		switch (fd) {
		case -1:
			strp = &gnu_name_string;
			break;
		case -2:
			strp = &gnu_link_string;
			break;
		default:
			strp = NULL;
			break;
		}
		if (strp) {
			if (*strp)
				err(1, "WARNING! Major Internal Error! GNU hack Failing!");
			*strp = malloc(wcnt + 1);
			if (*strp == NULL) {
				paxwarn(1, "Out of memory");
				return(-1);
			}
			memcpy(*strp, st, wcnt);
			(*strp)[wcnt] = '\0';
			break;
		} else if (write(fd, st, wcnt) != wcnt) {
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
	if (lseek(fd, -1, SEEK_CUR) < 0) {
		syswarn(1, errno, "Failed seek on file %s", fname);
		return;
	}

	if (write(fd, blnk, 1) == -1)
		syswarn(1, errno, "Failed write to file %s", fname);
}

/*
 * rdfile_close()
 *	close a file we have been reading (to copy or archive). If we have to
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

	/*
	 * user wants last access time reset
	 */
	if (tflag)
		fset_ftime(arcn->org_name, *fd, &arcn->sb.st_mtim,
		    &arcn->sb.st_atim, 1);

	(void)close(*fd);
	*fd = -1;
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
	off_t cpcnt = 0;
	size_t size;
	u_int32_t crc = 0;
	char tbuf[FILEBLK];
	struct stat sb;

	if (fd < 0) {
		/*
		 * hmm, no fd, should never happen. well no crc then.
		 */
		arcn->crc = 0;
		return(0);
	}

	if ((size = arcn->sb.st_blksize) > sizeof(tbuf))
		size = sizeof(tbuf);

	/*
	 * read all the bytes we think that there are in the file. If the user
	 * is trying to archive an active file, forget this file.
	 */
	for (;;) {
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
	else if (fstat(fd, &sb) == -1)
		syswarn(1, errno, "Failed stat on %s", arcn->org_name);
	else if (timespeccmp(&arcn->sb.st_mtim, &sb.st_mtim, !=))
		paxwarn(1, "File %s was modified during read", arcn->org_name);
	else if (lseek(fd, 0, SEEK_SET) < 0)
		syswarn(1, errno, "File rewind failed on: %s", arcn->org_name);
	else {
		arcn->crc = crc;
		return(0);
	}
	return(-1);
}
