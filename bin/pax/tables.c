/*	$OpenBSD: tables.c,v 1.55 2023/11/26 16:04:17 espie Exp $	*/
/*	$NetBSD: tables.c,v 1.4 1995/03/21 09:07:45 cgd Exp $	*/

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
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pax.h"
#include "extern.h"
static u_int st_hash(const char *, int, int);

/*
 * Routines for controlling the contents of all the different databases pax
 * keeps. Tables are dynamically created only when they are needed. The
 * goal was speed and the ability to work with HUGE archives. The databases
 * were kept simple, but do have complex rules for when the contents change.
 * As of this writing, the posix library functions were more complex than
 * needed for this application (pax databases have very short lifetimes and
 * do not survive after pax is finished). Pax is required to handle very
 * large archives. These database routines carefully combine memory usage and
 * temporary file storage in ways which will not significantly impact runtime
 * performance while allowing the largest possible archives to be handled.
 * Trying to force the fit to the posix database routines was not considered
 * time well spent.
 */

/*
 * data structures and constants used by the different databases kept by pax
 */

/*
 * Hash Table Sizes MUST BE PRIME, if set too small performance suffers.
 * Probably safe to expect 500000 inodes per tape. Assuming good key
 * distribution (inodes) chains of under 50 long (worst case) is ok.
 */
#define L_TAB_SZ	2503		/* hard link hash table size */
#define F_TAB_SZ	50503		/* file time hash table size */
#define N_TAB_SZ	541		/* interactive rename hash table */
#define D_TAB_SZ	317		/* unique device mapping table */
#define A_TAB_SZ	317		/* ftree dir access time reset table */
#define SL_TAB_SZ	317		/* escape symlink tables */
#define MAXKEYLEN	64		/* max number of chars for hash */
#define DIRP_SIZE	64		/* initial size of created dir table */

/*
 * file hard link structure (hashed by dev/ino and chained) used to find the
 * hard links in a file system or with some archive formats (cpio)
 */
typedef struct hrdlnk {
	ino_t		ino;	/* files inode number */
	char		*name;	/* name of first file seen with this ino/dev */
	dev_t		dev;	/* files device number */
	u_long		nlink;	/* expected link count */
	struct hrdlnk	*fow;
} HRDLNK;

/*
 * Archive write update file time table (the -u, -C flag), hashed by filename.
 * Filenames are stored in a scratch file at seek offset into the file. The
 * file time (mod time) and the file name length (for a quick check) are
 * stored in a hash table node. We were forced to use a scratch file because
 * with -u, the mtime for every node in the archive must always be available
 * to compare against (and this data can get REALLY large with big archives).
 * By being careful to read only when we have a good chance of a match, the
 * performance loss is not measurable (and the size of the archive we can
 * handle is greatly increased).
 */
typedef struct ftm {
	off_t		seek;		/* location in scratch file */
	struct timespec	mtim;		/* files last modification time */
	struct ftm	*fow;
	int		namelen;	/* file name length */
} FTM;

/*
 * Interactive rename table (-i flag), hashed by orig filename.
 * We assume this will not be a large table as this mapping data can only be
 * obtained through interactive input by the user. Nobody is going to type in
 * changes for 500000 files? We use chaining to resolve collisions.
 */

typedef struct namt {
	char		*oname;		/* old name */
	char		*nname;		/* new name typed in by the user */
	struct namt	*fow;
} NAMT;

/*
 * Unique device mapping tables. Some protocols (e.g. cpio) require that the
 * <c_dev,c_ino> pair will uniquely identify a file in an archive unless they
 * are links to the same file. Appending to archives can break this. For those
 * protocols that have this requirement we map c_dev to a unique value not seen
 * in the archive when we append. We also try to handle inode truncation with
 * this table. (When the inode field in the archive header are too small, we
 * remap the dev on writes to remove accidental collisions).
 *
 * The list is hashed by device number using chain collision resolution. Off of
 * each DEVT are linked the various remaps for this device based on those bits
 * in the inode which were truncated. For example if we are just remapping to
 * avoid a device number during an update append, off the DEVT we would have
 * only a single DLIST that has a truncation id of 0 (no inode bits were
 * stripped for this device so far). When we spot inode truncation we create
 * a new mapping based on the set of bits in the inode which were stripped off.
 * so if the top four bits of the inode are stripped and they have a pattern of
 * 0110...... (where . are those bits not truncated) we would have a mapping
 * assigned for all inodes that has the same 0110.... pattern (with this dev
 * number of course). This keeps the mapping sparse and should be able to store
 * close to the limit of files which can be represented by the optimal
 * combination of dev and inode bits, and without creating a fouled up archive.
 * Note we also remap truncated devs in the same way (an exercise for the
 * dedicated reader; always wanted to say that...:)
 */

typedef struct devt {
	dev_t		dev;	/* the orig device number we now have to map */
	struct devt	*fow;	/* new device map list */
	struct dlist	*list;	/* map list based on inode truncation bits */
} DEVT;

typedef struct dlist {
	ino_t trunc_bits;	/* truncation pattern for a specific map */
	dev_t dev;		/* the new device id we use */
	struct dlist *fow;
} DLIST;

/*
 * ftree directory access time reset table. When we are done with a
 * subtree we reset the access and mod time of the directory when the tflag is
 * set. Not really explicitly specified in the pax spec, but easy and fast to
 * do (and this may have even been intended in the spec, it is not clear).
 * table is hashed by inode with chaining.
 */

typedef struct atdir {
	struct file_times ft;
	struct atdir *fow;
} ATDIR;

/*
 * created directory time and mode storage entry. After pax is finished during
 * extraction or copy, we must reset directory access modes and times that
 * may have been modified after creation (they no longer have the specified
 * times and/or modes). We must reset time in the reverse order of creation,
 * because entries are added  from the top of the file tree to the bottom.
 * We MUST reset times from leaf to root (it will not work the other
 * direction).
 */

typedef struct dirdata {
	struct file_times ft;
	u_int16_t mode;		/* file mode to restore */
	u_int16_t frc_mode;	/* do we force mode settings? */
} DIRDATA;

static HRDLNK **ltab = NULL;	/* hard link table for detecting hard links */
static FTM **ftab = NULL;	/* file time table for updating arch */
static NAMT **ntab = NULL;	/* interactive rename storage table */
#ifndef NOCPIO
static DEVT **dtab = NULL;	/* device/inode mapping tables */
#endif
static ATDIR **atab = NULL;	/* file tree directory time reset table */
static DIRDATA *dirp = NULL;	/* storage for setting created dir time/mode */
static size_t dirsize;		/* size of dirp table */
static size_t dircnt = 0;	/* entries in dir time/mode storage */
static int ffd = -1;		/* tmp file for file time table name storage */

/*
 * hard link table routines
 *
 * The hard link table tries to detect hard links to files using the device and
 * inode values. We do this when writing an archive, so we can tell the format
 * write routine that this file is a hard link to another file. The format
 * write routine then can store this file in whatever way it wants (as a hard
 * link if the format supports that like tar, or ignore this info like cpio).
 * (Actually a field in the format driver table tells us if the format wants
 * hard link info. if not, we do not waste time looking for them). We also use
 * the same table when reading an archive. In that situation, this table is
 * used by the format read routine to detect hard links from stored dev and
 * inode numbers (like cpio). This will allow pax to create a link when one
 * can be detected by the archive format.
 */

/*
 * lnk_start
 *	Creates the hard link table.
 * Return:
 *	0 if created, -1 if failure
 */

int
lnk_start(void)
{
	if (ltab != NULL)
		return(0);
	if ((ltab = calloc(L_TAB_SZ, sizeof(HRDLNK *))) == NULL) {
		paxwarn(1, "Cannot allocate memory for hard link table");
		return(-1);
	}
	return(0);
}

/*
 * chk_lnk()
 *	Looks up entry in hard link hash table. If found, it copies the name
 *	of the file it is linked to (we already saw that file) into ln_name.
 *	lnkcnt is decremented and if goes to 1 the node is deleted from the
 *	database. (We have seen all the links to this file). If not found,
 *	we add the file to the database if it has the potential for having
 *	hard links to other files we may process (it has a link count > 1)
 * Return:
 *	if found returns 1; if not found returns 0; -1 on error
 */

int
chk_lnk(ARCHD *arcn)
{
	HRDLNK *pt;
	HRDLNK **ppt;
	u_int indx;

	if (ltab == NULL)
		return(-1);
	/*
	 * ignore those nodes that cannot have hard links
	 */
	if ((arcn->type == PAX_DIR) || (arcn->sb.st_nlink <= 1))
		return(0);

	/*
	 * hash inode number and look for this file
	 */
	indx = ((unsigned)arcn->sb.st_ino) % L_TAB_SZ;
	if ((pt = ltab[indx]) != NULL) {
		/*
		 * its hash chain in not empty, walk down looking for it
		 */
		ppt = &(ltab[indx]);
		while (pt != NULL) {
			if ((pt->ino == arcn->sb.st_ino) &&
			    (pt->dev == arcn->sb.st_dev))
				break;
			ppt = &(pt->fow);
			pt = pt->fow;
		}

		if (pt != NULL) {
			/*
			 * found a link. set the node type and copy in the
			 * name of the file it is to link to. we need to
			 * handle hardlinks to regular files differently than
			 * other links.
			 */
			arcn->ln_nlen = strlcpy(arcn->ln_name, pt->name,
				sizeof(arcn->ln_name));
			/* XXX truncate? */
			if ((size_t)arcn->nlen >= sizeof(arcn->name))
				arcn->nlen = sizeof(arcn->name) - 1;
			if (arcn->type == PAX_REG)
				arcn->type = PAX_HRG;
			else
				arcn->type = PAX_HLK;

			/*
			 * if we have found all the links to this file, remove
			 * it from the database
			 */
			if (--pt->nlink <= 1) {
				*ppt = pt->fow;
				free(pt->name);
				free(pt);
			}
			return(1);
		}
	}

	/*
	 * we never saw this file before. It has links so we add it to the
	 * front of this hash chain
	 */
	if ((pt = malloc(sizeof(HRDLNK))) != NULL) {
		if ((pt->name = strdup(arcn->name)) != NULL) {
			pt->dev = arcn->sb.st_dev;
			pt->ino = arcn->sb.st_ino;
			pt->nlink = arcn->sb.st_nlink;
			pt->fow = ltab[indx];
			ltab[indx] = pt;
			return(0);
		}
		free(pt);
	}

	paxwarn(1, "Hard link table out of memory");
	return(-1);
}

/*
 * purg_lnk
 *	remove reference for a file that we may have added to the data base as
 *	a potential source for hard links. We ended up not using the file, so
 *	we do not want to accidently point another file at it later on.
 */

void
purg_lnk(ARCHD *arcn)
{
	HRDLNK *pt;
	HRDLNK **ppt;
	u_int indx;

	if (ltab == NULL)
		return;
	/*
	 * do not bother to look if it could not be in the database
	 */
	if ((arcn->sb.st_nlink <= 1) || (arcn->type == PAX_DIR) ||
	    PAX_IS_HARDLINK(arcn->type))
		return;

	/*
	 * find the hash chain for this inode value, if empty return
	 */
	indx = ((unsigned)arcn->sb.st_ino) % L_TAB_SZ;
	if ((pt = ltab[indx]) == NULL)
		return;

	/*
	 * walk down the list looking for the inode/dev pair, unlink and
	 * free if found
	 */
	ppt = &(ltab[indx]);
	while (pt != NULL) {
		if ((pt->ino == arcn->sb.st_ino) &&
		    (pt->dev == arcn->sb.st_dev))
			break;
		ppt = &(pt->fow);
		pt = pt->fow;
	}
	if (pt == NULL)
		return;

	/*
	 * remove and free it
	 */
	*ppt = pt->fow;
	free(pt->name);
	free(pt);
}

/*
 * lnk_end()
 *	pull apart a existing link table so we can reuse it. We do this between
 *	read and write phases of append with update. (The format may have
 *	used the link table, and we need to start with a fresh table for the
 *	write phase
 */

void
lnk_end(void)
{
	int i;
	HRDLNK *pt;
	HRDLNK *ppt;

	if (ltab == NULL)
		return;

	for (i = 0; i < L_TAB_SZ; ++i) {
		if (ltab[i] == NULL)
			continue;
		pt = ltab[i];
		ltab[i] = NULL;

		/*
		 * free up each entry on this chain
		 */
		while (pt != NULL) {
			ppt = pt;
			pt = ppt->fow;
			free(ppt->name);
			free(ppt);
		}
	}
}

/*
 * modification time table routines
 *
 * The modification time table keeps track of last modification times for all
 * files stored in an archive during a write phase when -u is set. We only
 * add a file to the archive if it is newer than a file with the same name
 * already stored on the archive (if there is no other file with the same
 * name on the archive it is added). This applies to writes and appends.
 * An append with an -u must read the archive and store the modification time
 * for every file on that archive before starting the write phase. It is clear
 * that this is one HUGE database. To save memory space, the actual file names
 * are stored in a scratch file and indexed by an in-memory hash table. The
 * hash table is indexed by hashing the file path. The nodes in the table store
 * the length of the filename and the lseek offset within the scratch file
 * where the actual name is stored. Since there are never any deletions from
 * this table, fragmentation of the scratch file is never a issue. Lookups
 * seem to not exhibit any locality at all (files in the database are rarely
 * looked up more than once...), so caching is just a waste of memory. The
 * only limitation is the amount of scratch file space available to store the
 * path names.
 */

/*
 * ftime_start()
 *	create the file time hash table and open for read/write the scratch
 *	file. (after created it is unlinked, so when we exit we leave
 *	no witnesses).
 * Return:
 *	0 if the table and file was created ok, -1 otherwise
 */

int
ftime_start(void)
{

	if (ftab != NULL)
		return(0);
	if ((ftab = calloc(F_TAB_SZ, sizeof(FTM *))) == NULL) {
		paxwarn(1, "Cannot allocate memory for file time table");
		return(-1);
	}

	/*
	 * get random name and create temporary scratch file, unlink name
	 * so it will get removed on exit
	 */
	memcpy(tempbase, _TFILE_BASE, sizeof(_TFILE_BASE));
	if ((ffd = mkstemp(tempfile)) == -1) {
		syswarn(1, errno, "Unable to create temporary file: %s",
		    tempfile);
		return(-1);
	}
	(void)unlink(tempfile);

	return(0);
}

/*
 * chk_ftime()
 *	looks up entry in file time hash table. If not found, the file is
 *	added to the hash table and the file named stored in the scratch file.
 *	If a file with the same name is found, the file times are compared and
 *	the most recent file time is retained. If the new file was younger (or
 *	was not in the database) the new file is selected for storage.
 * Return:
 *	0 if file should be added to the archive, 1 if it should be skipped,
 *	-1 on error
 */

int
chk_ftime(ARCHD *arcn)
{
	FTM *pt;
	int namelen;
	u_int indx;
	char ckname[PAXPATHLEN+1];

	/*
	 * no info, go ahead and add to archive
	 */
	if (ftab == NULL)
		return(0);

	/*
	 * hash the pathname and look up in table
	 */
	namelen = arcn->nlen;
	indx = st_hash(arcn->name, namelen, F_TAB_SZ);
	if ((pt = ftab[indx]) != NULL) {
		/*
		 * the hash chain is not empty, walk down looking for match
		 * only read up the path names if the lengths match, speeds
		 * up the search a lot
		 */
		while (pt != NULL) {
			if (pt->namelen == namelen) {
				/*
				 * potential match, have to read the name
				 * from the scratch file.
				 */
				if (lseek(ffd,pt->seek,SEEK_SET) != pt->seek) {
					syswarn(1, errno,
					    "Failed ftime table seek");
					return(-1);
				}
				if (read(ffd, ckname, namelen) != namelen) {
					syswarn(1, errno,
					    "Failed ftime table read");
					return(-1);
				}

				/*
				 * if the names match, we are done
				 */
				if (!strncmp(ckname, arcn->name, namelen))
					break;
			}

			/*
			 * try the next entry on the chain
			 */
			pt = pt->fow;
		}

		if (pt != NULL) {
			/*
			 * found the file, compare the times, save the newer
			 */
			if (timespeccmp(&arcn->sb.st_mtim, &pt->mtim, >)) {
				/*
				 * file is newer
				 */
				pt->mtim = arcn->sb.st_mtim;
				return(0);
			}
			/*
			 * file is older
			 */
			return(1);
		}
	}

	/*
	 * not in table, add it
	 */
	if ((pt = malloc(sizeof(FTM))) != NULL) {
		/*
		 * add the name at the end of the scratch file, saving the
		 * offset. add the file to the head of the hash chain
		 */
		if ((pt->seek = lseek(ffd, 0, SEEK_END)) >= 0) {
			if (write(ffd, arcn->name, namelen) == namelen) {
				pt->mtim = arcn->sb.st_mtim;
				pt->namelen = namelen;
				pt->fow = ftab[indx];
				ftab[indx] = pt;
				return(0);
			}
			syswarn(1, errno, "Failed write to file time table");
		} else
			syswarn(1, errno, "Failed seek on file time table");
	} else
		paxwarn(1, "File time table ran out of memory");

	if (pt != NULL)
		free(pt);
	return(-1);
}

/*
 * escaping (absolute or w/"..") symlink table routines
 *
 * By default, an archive shouldn't be able extract to outside of the
 * current directory.  What should we do if the archive contains a symlink
 * whose value is either absolute or contains ".." components?  What we'll
 * do is initially create the path as an empty file (to block attempts to
 * reference _through_ it) and instead record its path and desired
 * final value and mode.  Then once all the other archive
 * members are created (but before the pass to set timestamps on
 * directories) we'll process those records, replacing the placeholder with
 * the correct symlink and setting them to the correct mode, owner, group,
 * and timestamps.
 *
 * Note: we also need to handle hardlinks to symlinks (barf) as well as
 * hardlinks whose target is replaced by a later entry in the archive (barf^2).
 *
 * So we track things by dev+ino of the placeholder file, associating with
 * that the value and mode of the final symlink and a list of paths that
 * should all be hardlinks of that.  We'll 'store' the symlink's desired
 * timestamps, owner, and group by setting them on the placeholder file.
 *
 * The operations are:
 * a) create an escaping symlink: create the placeholder file and add an entry
 *    for the new link
 * b) create a hardlink: do the link.  If the target turns out to be a
 *    zero-length file whose dev+ino are in the symlink table, then add this
 *    path to the list of names for that link
 * c) perform deferred processing: for each entry, check each associated path:
 *    if it's a zero-length file with the correct dev+ino then recreate it as
 *    the specified symlink or hardlink to the first such
 */

struct slpath {
	char	*sp_path;
	struct	slpath *sp_next;
};
struct slinode {
	ino_t	sli_ino;
	char	*sli_value;
	struct	slpath sli_paths;
	struct	slinode *sli_fow;		/* hash table chain */
	dev_t	sli_dev;
	mode_t	sli_mode;
};

static struct slinode **slitab = NULL;

/*
 * sltab_start()
 *	create the hash table
 * Return:
 *	0 if the table and file was created ok, -1 otherwise
 */

int
sltab_start(void)
{

	if ((slitab = calloc(SL_TAB_SZ, sizeof *slitab)) == NULL) {
		syswarn(1, errno, "symlink table");
		return(-1);
	}

	return(0);
}

/*
 * sltab_add_sym()
 *	Create the placeholder and tracking info for an escaping symlink.
 * Return:
 *	0 on success, -1 otherwise
 */

int
sltab_add_sym(const char *path0, const char *value0, mode_t mode)
{
	struct stat sb;
	struct slinode *s;
	struct slpath *p;
	char *path, *value;
	u_int indx;
	int fd;

	/* create the placeholder */
	fd = open(path0, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd == -1)
		return (-1);
	if (fstat(fd, &sb) == -1) {
		unlink(path0);
		close(fd);
		return (-1);
	}
	close(fd);

	if (havechd && *path0 != '/') {
		if ((path = realpath(path0, NULL)) == NULL) {
			syswarn(1, errno, "Cannot canonicalize %s", path0);
			unlink(path0);
			return (-1);
		}
	} else if ((path = strdup(path0)) == NULL) {
		syswarn(1, errno, "defered symlink path");
		unlink(path0);
		return (-1);
	}
	if ((value = strdup(value0)) == NULL) {
		syswarn(1, errno, "defered symlink value");
		unlink(path);
		free(path);
		return (-1);
	}

	/* now check the hash table for conflicting entry */
	indx = (sb.st_ino ^ sb.st_dev) % SL_TAB_SZ;
	for (s = slitab[indx]; s != NULL; s = s->sli_fow) {
		if (s->sli_ino != sb.st_ino || s->sli_dev != sb.st_dev)
			continue;

		/*
		 * One of our placeholders got removed behind our back and
		 * we've reused the inode.  Weird, but clean up the mess.
		 */
		free(s->sli_value);
		free(s->sli_paths.sp_path);
		p = s->sli_paths.sp_next;
		while (p != NULL) {
			struct slpath *next_p = p->sp_next;

			free(p->sp_path);
			free(p);
			p = next_p;
		}
		goto set_value;
	}

	/* Normal case: create a new node */
	if ((s = malloc(sizeof *s)) == NULL) {
		syswarn(1, errno, "defered symlink");
		unlink(path);
		free(path);
		free(value);
		return (-1);
	}
	s->sli_ino = sb.st_ino;
	s->sli_dev = sb.st_dev;
	s->sli_fow = slitab[indx];
	slitab[indx] = s;

set_value:
	s->sli_paths.sp_path = path;
	s->sli_paths.sp_next = NULL;
	s->sli_value = value;
	s->sli_mode = mode;
	return (0);
}

/*
 * sltab_add_link()
 *	A hardlink was created; if it looks like a placeholder, handle the
 *	tracking.
 * Return:
 *	0 if things are ok, -1 if something went wrong
 */

int
sltab_add_link(const char *path, const struct stat *sb)
{
	struct slinode *s;
	struct slpath *p;
	u_int indx;

	if (!S_ISREG(sb->st_mode) || sb->st_size != 0)
		return (1);

	/* find the hash table entry for this hardlink */
	indx = (sb->st_ino ^ sb->st_dev) % SL_TAB_SZ;
	for (s = slitab[indx]; s != NULL; s = s->sli_fow) {
		if (s->sli_ino != sb->st_ino || s->sli_dev != sb->st_dev)
			continue;

		if ((p = malloc(sizeof *p)) == NULL) {
			syswarn(1, errno, "deferred symlink hardlink");
			return (-1);
		}
		if (havechd && *path != '/') {
			if ((p->sp_path = realpath(path, NULL)) == NULL) {
				syswarn(1, errno, "Cannot canonicalize %s",
				    path);
				free(p);
				return (-1);
			}
		} else if ((p->sp_path = strdup(path)) == NULL) {
			syswarn(1, errno, "defered symlink hardlink path");
			free(p);
			return (-1);
		}

		/* link it in */
		p->sp_next = s->sli_paths.sp_next;
		s->sli_paths.sp_next = p;
		return (0);
	}

	/* not found */
	return (1);
}


static int
sltab_process_one(struct slinode *s, struct slpath *p, const char *first,
    int in_sig)
{
	struct stat sb;
	char *path = p->sp_path;
	mode_t mode;
	int err;

	/*
	 * is it the expected placeholder?  This can fail legimately
	 * if the archive overwrote the link with another, later entry,
	 * so don't warn.
	 */
	if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size != 0 ||
	    sb.st_ino != s->sli_ino || sb.st_dev != s->sli_dev)
		return (0);

	if (unlink(path) && errno != ENOENT) {
		if (!in_sig)
			syswarn(1, errno, "deferred symlink removal");
		return (0);
	}

	err = 0;
	if (first != NULL) {
		/* add another hardlink to the existing symlink */
		if (linkat(AT_FDCWD, first, AT_FDCWD, path, 0) == 0)
			return (0);

		/*
		 * Couldn't hardlink the symlink for some reason, so we'll
		 * try creating it as its own symlink, but save the error
		 * for reporting if that fails.
		 */
		err = errno;
	}

	if (symlink(s->sli_value, path)) {
		if (!in_sig) {
			const char *qualifier = "";
			if (err)
				qualifier = " hardlink";
			else
				err = errno;

			syswarn(1, err, "deferred symlink%s: %s",
			    qualifier, path);
		}
		return (0);
	}

	/* success, so set the id, mode, and times */
	mode = s->sli_mode;
	if (pids) {
		/* if can't set the ids, force the set[ug]id bits off */
		if (set_ids(path, sb.st_uid, sb.st_gid))
			mode &= ~(SETBITS);
	}

	if (pmode)
		set_pmode(path, mode);

	if (patime || pmtime)
		set_ftime(path, &sb.st_mtim, &sb.st_atim, 0);

	/*
	 * If we tried to link to first but failed, then this new symlink
	 * might be a better one to try in the future.  Guess from the errno.
	 */
	if (err == 0 || err == ENOENT || err == EMLINK || err == EOPNOTSUPP)
		return (1);
	return (0);
}

/*
 * sltab_process()
 *	Do all the delayed process for escape symlinks
 */

void
sltab_process(int in_sig)
{
	struct slinode *s;
	struct slpath *p;
	char *first;
	u_int indx;

	if (slitab == NULL)
		return;

	/* walk across the entire hash table */
	for (indx = 0; indx < SL_TAB_SZ; indx++) {
		while ((s = slitab[indx]) != NULL) {
			/* pop this entry */
			slitab[indx] = s->sli_fow;

			first = NULL;
			p = &s->sli_paths;
			while (1) {
				struct slpath *next_p;

				if (sltab_process_one(s, p, first, in_sig)) {
					if (!in_sig)
						free(first);
					first = p->sp_path;
				} else if (!in_sig)
					free(p->sp_path);

				if ((next_p = p->sp_next) == NULL)
					break;
				*p = *next_p;
				if (!in_sig)
					free(next_p);
			}
			if (!in_sig) {
				free(first);
				free(s->sli_value);
				free(s);
			}
		}
	}
	if (!in_sig)
		free(slitab);
	slitab = NULL;
}


/*
 * Interactive rename table routines
 *
 * The interactive rename table keeps track of the new names that the user
 * assigns to files from tty input. Since this map is unique for each file
 * we must store it in case there is a reference to the file later in archive
 * (a link). Otherwise we will be unable to find the file we know was
 * extracted. The remapping of these files is stored in a memory based hash
 * table (it is assumed since input must come from /dev/tty, it is unlikely to
 * be a very large table).
 */

/*
 * name_start()
 *	create the interactive rename table
 * Return:
 *	0 if successful, -1 otherwise
 */

int
name_start(void)
{
	if (ntab != NULL)
		return(0);
	if ((ntab = calloc(N_TAB_SZ, sizeof(NAMT *))) == NULL) {
		paxwarn(1, "Cannot allocate memory for interactive rename table");
		return(-1);
	}
	return(0);
}

/*
 * add_name()
 *	add the new name to old name mapping just created by the user.
 *	If an old name mapping is found (there may be duplicate names on an
 *	archive) only the most recent is kept.
 * Return:
 *	0 if added, -1 otherwise
 */

int
add_name(char *oname, int onamelen, char *nname)
{
	NAMT *pt;
	u_int indx;

	if (ntab == NULL) {
		/*
		 * should never happen
		 */
		paxwarn(0, "No interactive rename table, links may fail");
		return(0);
	}

	/*
	 * look to see if we have already mapped this file, if so we
	 * will update it
	 */
	indx = st_hash(oname, onamelen, N_TAB_SZ);
	if ((pt = ntab[indx]) != NULL) {
		/*
		 * look down the has chain for the file
		 */
		while ((pt != NULL) && (strcmp(oname, pt->oname) != 0))
			pt = pt->fow;

		if (pt != NULL) {
			/*
			 * found an old mapping, replace it with the new one
			 * the user just input (if it is different)
			 */
			if (strcmp(nname, pt->nname) == 0)
				return(0);

			free(pt->nname);
			if ((pt->nname = strdup(nname)) == NULL) {
				paxwarn(1, "Cannot update rename table");
				return(-1);
			}
			return(0);
		}
	}

	/*
	 * this is a new mapping, add it to the table
	 */
	if ((pt = malloc(sizeof(NAMT))) != NULL) {
		if ((pt->oname = strdup(oname)) != NULL) {
			if ((pt->nname = strdup(nname)) != NULL) {
				pt->fow = ntab[indx];
				ntab[indx] = pt;
				return(0);
			}
			free(pt->oname);
		}
		free(pt);
	}
	paxwarn(1, "Interactive rename table out of memory");
	return(-1);
}

/*
 * sub_name()
 *	look up a link name to see if it points at a file that has been
 *	remapped by the user. If found, the link is adjusted to contain the
 *	new name (oname is the link to name)
 */

void
sub_name(char *oname, int *onamelen, int onamesize)
{
	NAMT *pt;
	u_int indx;

	if (ntab == NULL)
		return;
	/*
	 * look the name up in the hash table
	 */
	indx = st_hash(oname, *onamelen, N_TAB_SZ);
	if ((pt = ntab[indx]) == NULL)
		return;

	while (pt != NULL) {
		/*
		 * walk down the hash chain looking for a match
		 */
		if (strcmp(oname, pt->oname) == 0) {
			/*
			 * found it, replace it with the new name
			 * and return (we know that oname has enough space)
			 */
			*onamelen = strlcpy(oname, pt->nname, onamesize);
			if (*onamelen >= onamesize)
				*onamelen = onamesize - 1; /* XXX truncate? */
			return;
		}
		pt = pt->fow;
	}

	/*
	 * no match, just return
	 */
}

#ifndef NOCPIO
/*
 * device/inode mapping table routines
 * (used with formats that store device and inodes fields)
 *
 * device/inode mapping tables remap the device field in a archive header. The
 * device/inode fields are used to determine when files are hard links to each
 * other. However these values have very little meaning outside of that. This
 * database is used to solve one of two different problems.
 *
 * 1) when files are appended to an archive, while the new files may have hard
 * links to each other, you cannot determine if they have hard links to any
 * file already stored on the archive from a prior run of pax. We must assume
 * that these inode/device pairs are unique only within a SINGLE run of pax
 * (which adds a set of files to an archive). So we have to make sure the
 * inode/dev pairs we add each time are always unique. We do this by observing
 * while the inode field is very dense, the use of the dev field is fairly
 * sparse. Within each run of pax, we remap any device number of a new archive
 * member that has a device number used in a prior run and already stored in a
 * file on the archive. During the read phase of the append, we store the
 * device numbers used and mark them to not be used by any file during the
 * write phase. If during write we go to use one of those old device numbers,
 * we remap it to a new value.
 *
 * 2) Often the fields in the archive header used to store these values are
 * too small to store the entire value. The result is an inode or device value
 * which can be truncated. This really can foul up an archive. With truncation
 * we end up creating links between files that are really not links (after
 * truncation the inodes are the same value). We address that by detecting
 * truncation and forcing a remap of the device field to split truncated
 * inodes away from each other. Each truncation creates a pattern of bits that
 * are removed. We use this pattern of truncated bits to partition the inodes
 * on a single device to many different devices (each one represented by the
 * truncated bit pattern). All inodes on the same device that have the same
 * truncation pattern are mapped to the same new device. Two inodes that
 * truncate to the same value clearly will always have different truncation
 * bit patterns, so they will be split from away each other. When we spot
 * device truncation we remap the device number to a non truncated value.
 * (for more info see table.h for the data structures involved).
 */

static DEVT *chk_dev(dev_t, int);

/*
 * dev_start()
 *	create the device mapping table
 * Return:
 *	0 if successful, -1 otherwise
 */

int
dev_start(void)
{
	if (dtab != NULL)
		return(0);
	if ((dtab = calloc(D_TAB_SZ, sizeof(DEVT *))) == NULL) {
		paxwarn(1, "Cannot allocate memory for device mapping table");
		return(-1);
	}
	return(0);
}

/*
 * add_dev()
 *	add a device number to the table. this will force the device to be
 *	remapped to a new value if it be used during a write phase. This
 *	function is called during the read phase of an append to prohibit the
 *	use of any device number already in the archive.
 * Return:
 *	0 if added ok, -1 otherwise
 */

int
add_dev(ARCHD *arcn)
{
	if (chk_dev(arcn->sb.st_dev, 1) == NULL)
		return(-1);
	return(0);
}

/*
 * chk_dev()
 *	check for a device value in the device table. If not found and the add
 *	flag is set, it is added. This does NOT assign any mapping values, just
 *	adds the device number as one that need to be remapped. If this device
 *	is already mapped, just return with a pointer to that entry.
 * Return:
 *	pointer to the entry for this device in the device map table. Null
 *	if the add flag is not set and the device is not in the table (it is
 *	not been seen yet). If add is set and the device cannot be added, null
 *	is returned (indicates an error).
 */

static DEVT *
chk_dev(dev_t dev, int add)
{
	DEVT *pt;
	u_int indx;

	if (dtab == NULL)
		return(NULL);
	/*
	 * look to see if this device is already in the table
	 */
	indx = ((unsigned)dev) % D_TAB_SZ;
	if ((pt = dtab[indx]) != NULL) {
		while ((pt != NULL) && (pt->dev != dev))
			pt = pt->fow;

		/*
		 * found it, return a pointer to it
		 */
		if (pt != NULL)
			return(pt);
	}

	/*
	 * not in table, we add it only if told to as this may just be a check
	 * to see if a device number is being used.
	 */
	if (add == 0)
		return(NULL);

	/*
	 * allocate a node for this device and add it to the front of the hash
	 * chain. Note we do not assign remaps values here, so the pt->list
	 * list must be NULL.
	 */
	if ((pt = malloc(sizeof(DEVT))) == NULL) {
		paxwarn(1, "Device map table out of memory");
		return(NULL);
	}
	pt->dev = dev;
	pt->list = NULL;
	pt->fow = dtab[indx];
	dtab[indx] = pt;
	return(pt);
}
/*
 * map_dev()
 *	given an inode and device storage mask (the mask has a 1 for each bit
 *	the archive format is able to store in a header), we check for inode
 *	and device truncation and remap the device as required. Device mapping
 *	can also occur when during the read phase of append a device number was
 *	seen (and was marked as do not use during the write phase). WE ASSUME
 *	that unsigned longs are the same size or bigger than the fields used
 *	for ino_t and dev_t. If not the types will have to be changed.
 * Return:
 *	0 if all ok, -1 otherwise.
 */

int
map_dev(ARCHD *arcn, u_long dev_mask, u_long ino_mask)
{
	DEVT *pt;
	DLIST *dpt;
	static dev_t lastdev = 0;	/* next device number to try */
	int trc_ino = 0;
	int trc_dev = 0;
	ino_t trunc_bits = 0;
	ino_t nino;

	if (dtab == NULL)
		return(0);
	/*
	 * check for device and inode truncation, and extract the truncated
	 * bit pattern.
	 */
	if ((arcn->sb.st_dev & (dev_t)dev_mask) != arcn->sb.st_dev)
		++trc_dev;
	if ((nino = arcn->sb.st_ino & (ino_t)ino_mask) != arcn->sb.st_ino) {
		++trc_ino;
		trunc_bits = arcn->sb.st_ino & (ino_t)(~ino_mask);
	}

	/*
	 * see if this device is already being mapped, look up the device
	 * then find the truncation bit pattern which applies
	 */
	if ((pt = chk_dev(arcn->sb.st_dev, 0)) != NULL) {
		/*
		 * this device is already marked to be remapped
		 */
		for (dpt = pt->list; dpt != NULL; dpt = dpt->fow)
			if (dpt->trunc_bits == trunc_bits)
				break;

		if (dpt != NULL) {
			/*
			 * we are being remapped for this device and pattern
			 * change the device number to be stored and return
			 */
			arcn->sb.st_dev = dpt->dev;
			arcn->sb.st_ino = nino;
			return(0);
		}
	} else {
		/*
		 * this device is not being remapped YET. if we do not have any
		 * form of truncation, we do not need a remap
		 */
		if (!trc_ino && !trc_dev)
			return(0);

		/*
		 * we have truncation, have to add this as a device to remap
		 */
		if ((pt = chk_dev(arcn->sb.st_dev, 1)) == NULL)
			goto bad;

		/*
		 * if we just have a truncated inode, we have to make sure that
		 * all future inodes that do not truncate (they have the
		 * truncation pattern of all 0's) continue to map to the same
		 * device number. We probably have already written inodes with
		 * this device number to the archive with the truncation
		 * pattern of all 0's. So we add the mapping for all 0's to the
		 * same device number.
		 */
		if (!trc_dev && (trunc_bits != 0)) {
			if ((dpt = malloc(sizeof(DLIST))) == NULL)
				goto bad;
			dpt->trunc_bits = 0;
			dpt->dev = arcn->sb.st_dev;
			dpt->fow = pt->list;
			pt->list = dpt;
		}
	}

	/*
	 * look for a device number not being used. We must watch for wrap
	 * around on lastdev (so we do not get stuck looking forever!)
	 */
	while (++lastdev > 0) {
		if (chk_dev(lastdev, 0) != NULL)
			continue;
		/*
		 * found an unused value. If we have reached truncation point
		 * for this format we are hosed, so we give up. Otherwise we
		 * mark it as being used.
		 */
		if (((lastdev & ((dev_t)dev_mask)) != lastdev) ||
		    (chk_dev(lastdev, 1) == NULL))
			goto bad;
		break;
	}

	if ((lastdev <= 0) || ((dpt = malloc(sizeof(DLIST))) == NULL))
		goto bad;

	/*
	 * got a new device number, store it under this truncation pattern.
	 * change the device number this file is being stored with.
	 */
	dpt->trunc_bits = trunc_bits;
	dpt->dev = lastdev;
	dpt->fow = pt->list;
	pt->list = dpt;
	arcn->sb.st_dev = lastdev;
	arcn->sb.st_ino = nino;
	return(0);

    bad:
	paxwarn(1, "Unable to fix truncated inode/device field when storing %s",
	    arcn->name);
	paxwarn(0, "Archive may create improper hard links when extracted");
	return(0);
}
#endif /* NOCPIO */

/*
 * directory access/mod time reset table routines (for directories READ by pax)
 *
 * The pax -t flag requires that access times of archive files be the same
 * before being read by pax. For regular files, access time is restored after
 * the file has been copied. This database provides the same functionality for
 * directories read during file tree traversal. Restoring directory access time
 * is more complex than files since directories may be read several times until
 * all the descendants in their subtree are visited by fts. Directory access
 * and modification times are stored during the fts pre-order visit (done
 * before any descendants in the subtree are visited) and restored after the
 * fts post-order visit (after all the descendants have been visited). In the
 * case of premature exit from a subtree (like from the effects of -n), any
 * directory entries left in this database are reset during final cleanup
 * operations of pax. Entries are hashed by inode number for fast lookup.
 */

/*
 * atdir_start()
 *	create the directory access time database for directories READ by pax.
 * Return:
 *	0 is created ok, -1 otherwise.
 */

int
atdir_start(void)
{
	if (atab != NULL)
		return(0);
	if ((atab = calloc(A_TAB_SZ, sizeof(ATDIR *))) == NULL) {
		paxwarn(1,"Cannot allocate space for directory access time table");
		return(-1);
	}
	return(0);
}


/*
 * atdir_end()
 *	walk through the directory access time table and reset the access time
 *	of any directory who still has an entry left in the database. These
 *	entries are for directories READ by pax
 */

void
atdir_end(void)
{
	ATDIR *pt;
	int i;

	if (atab == NULL)
		return;
	/*
	 * for each non-empty hash table entry reset all the directories
	 * chained there.
	 */
	for (i = 0; i < A_TAB_SZ; ++i) {
		if ((pt = atab[i]) == NULL)
			continue;
		/*
		 * remember to force the times, set_ftime() looks at pmtime
		 * and patime, which only applies to things CREATED by pax,
		 * not read by pax. Read time reset is controlled by -t.
		 */
		for (; pt != NULL; pt = pt->fow)
			set_attr(&pt->ft, 1, 0, 0, 0);
	}
}

/*
 * add_atdir()
 *	add a directory to the directory access time table. Table is hashed
 *	and chained by inode number. This is for directories READ by pax
 */

void
add_atdir(char *fname, dev_t dev, ino_t ino, const struct timespec *mtimp,
    const struct timespec *atimp)
{
	ATDIR *pt;
	sigset_t allsigs, savedsigs;
	u_int indx;

	if (atab == NULL)
		return;

	/*
	 * make sure this directory is not already in the table, if so just
	 * return (the older entry always has the correct time). The only
	 * way this will happen is when the same subtree can be traversed by
	 * different args to pax and the -n option is aborting fts out of a
	 * subtree before all the post-order visits have been made.
	 */
	indx = ((unsigned)ino) % A_TAB_SZ;
	if ((pt = atab[indx]) != NULL) {
		while (pt != NULL) {
			if ((pt->ft.ft_ino == ino) && (pt->ft.ft_dev == dev))
				break;
			pt = pt->fow;
		}

		/*
		 * oops, already there. Leave it alone.
		 */
		if (pt != NULL)
			return;
	}

	/*
	 * add it to the front of the hash chain
	 */
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &savedsigs);
	if ((pt = malloc(sizeof *pt)) != NULL) {
		if ((pt->ft.ft_name = strdup(fname)) != NULL) {
			pt->ft.ft_dev = dev;
			pt->ft.ft_ino = ino;
			pt->ft.ft_mtim = *mtimp;
			pt->ft.ft_atim = *atimp;
			pt->fow = atab[indx];
			atab[indx] = pt;
			sigprocmask(SIG_SETMASK, &savedsigs, NULL);
			return;
		}
		free(pt);
	}

	sigprocmask(SIG_SETMASK, &savedsigs, NULL);
	paxwarn(1, "Directory access time reset table ran out of memory");
}

/*
 * get_atdir()
 *	look up a directory by inode and device number to obtain the access
 *	and modification time you want to set to. If found, the modification
 *	and access time parameters are set and the entry is removed from the
 *	table (as it is no longer needed). These are for directories READ by
 *	pax
 * Return:
 *	0 if found, -1 if not found.
 */

int
do_atdir(const char *name, dev_t dev, ino_t ino)
{
	ATDIR *pt;
	ATDIR **ppt;
	sigset_t allsigs, savedsigs;
	u_int indx;

	if (atab == NULL)
		return(-1);
	/*
	 * hash by inode and search the chain for an inode and device match
	 */
	indx = ((unsigned)ino) % A_TAB_SZ;
	if ((pt = atab[indx]) == NULL)
		return(-1);

	ppt = &(atab[indx]);
	while (pt != NULL) {
		if ((pt->ft.ft_ino == ino) && (pt->ft.ft_dev == dev))
			break;
		/*
		 * no match, go to next one
		 */
		ppt = &(pt->fow);
		pt = pt->fow;
	}

	/*
	 * return if we did not find it.
	 */
	if (pt == NULL || pt->ft.ft_name == NULL ||
	    strcmp(name, pt->ft.ft_name) == 0)
		return(-1);

	/*
	 * found it. set the times and remove the entry from the table.
	 */
	set_attr(&pt->ft, 1, 0, 0, 0);
	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, &savedsigs);
	*ppt = pt->fow;
	sigprocmask(SIG_SETMASK, &savedsigs, NULL);
	free(pt->ft.ft_name);
	free(pt);
	return(0);
}

/*
 * directory access mode and time storage routines (for directories CREATED
 * by pax).
 *
 * Pax requires that extracted directories, by default, have their access/mod
 * times and permissions set to the values specified in the archive. During the
 * actions of extracting (and creating the destination subtree during -rw copy)
 * directories extracted may be modified after being created. Even worse is
 * that these directories may have been created with file permissions which
 * prohibits any descendants of these directories from being extracted. When
 * directories are created by pax, access rights may be added to permit the
 * creation of files in their subtree. Every time pax creates a directory, the
 * times and file permissions specified by the archive are stored. After all
 * files have been extracted (or copied), these directories have their times
 * and file modes reset to the stored values. The directory info is restored in
 * reverse order as entries were added from root to leaf: to restore atime
 * properly, we must go backwards.
 */

/*
 * dir_start()
 *	set up the directory time and file mode storage for directories CREATED
 *	by pax.
 * Return:
 *	0 if ok, -1 otherwise
 */

int
dir_start(void)
{
	if (dirp != NULL)
		return(0);

	dirsize = DIRP_SIZE;
	if ((dirp = reallocarray(NULL, dirsize, sizeof(DIRDATA))) == NULL) {
		paxwarn(1, "Unable to allocate memory for directory times");
		return(-1);
	}
	return(0);
}

/*
 * add_dir()
 *	add the mode and times for a newly CREATED directory
 *	name is name of the directory, psb the stat buffer with the data in it,
 *	frc_mode is a flag that says whether to force the setting of the mode
 *	(ignoring the user set values for preserving file mode). Frc_mode is
 *	for the case where we created a file and found that the resulting
 *	directory was not writeable and the user asked for file modes to NOT
 *	be preserved. (we have to preserve what was created by default, so we
 *	have to force the setting at the end. this is stated explicitly in the
 *	pax spec)
 */

void
add_dir(char *name, struct stat *psb, int frc_mode)
{
	DIRDATA *dblk;
	sigset_t allsigs, savedsigs;
	char realname[PATH_MAX], *rp;

	if (dirp == NULL)
		return;

	if (havechd && *name != '/') {
		if ((rp = realpath(name, realname)) == NULL) {
			paxwarn(1, "Cannot canonicalize %s", name);
			return;
		}
		name = rp;
	}
	if (dircnt == dirsize) {
		dblk = reallocarray(dirp, dirsize * 2, sizeof(DIRDATA));
		if (dblk == NULL) {
			paxwarn(1, "Unable to store mode and times for created"
			    " directory: %s", name);
			return;
		}
		sigprocmask(SIG_BLOCK, &allsigs, &savedsigs);
		dirp = dblk;
		dirsize *= 2;
		sigprocmask(SIG_SETMASK, &savedsigs, NULL);
	}
	dblk = &dirp[dircnt];
	if ((dblk->ft.ft_name = strdup(name)) == NULL) {
		paxwarn(1, "Unable to store mode and times for created"
		    " directory: %s", name);
		return;
	}
	dblk->ft.ft_mtim = psb->st_mtim;
	dblk->ft.ft_atim = psb->st_atim;
	dblk->ft.ft_ino = psb->st_ino;
	dblk->ft.ft_dev = psb->st_dev;
	dblk->mode = psb->st_mode & ABITS;
	dblk->frc_mode = frc_mode;
	sigprocmask(SIG_BLOCK, &allsigs, &savedsigs);
	++dircnt;
	sigprocmask(SIG_SETMASK, &savedsigs, NULL);
}

/*
 * delete_dir()
 *	When we rmdir a directory, we may want to make sure we don't
 *	later warn about being unable to set its mode and times.
 */

void
delete_dir(dev_t dev, ino_t ino)
{
	DIRDATA *dblk;
	char *name;
	size_t i;

	if (dirp == NULL)
		return;
	for (i = 0; i < dircnt; i++) {
		dblk = &dirp[i];

		if (dblk->ft.ft_name == NULL)
			continue;
		if (dblk->ft.ft_dev == dev && dblk->ft.ft_ino == ino) {
			name = dblk->ft.ft_name;
			dblk->ft.ft_name = NULL;
			free(name);
			break;
		}
	}
}

/*
 * proc_dir(int in_sig)
 *	process all file modes and times stored for directories CREATED
 *	by pax.  If in_sig is set, we're in a signal handler and can't
 *	free stuff.
 */

void
proc_dir(int in_sig)
{
	DIRDATA *dblk;
	size_t cnt;

	if (dirp == NULL)
		return;
	/*
	 * read backwards through the file and process each directory
	 */
	cnt = dircnt;
	while (cnt-- > 0) {
		dblk = &dirp[cnt];
		/*
		 * If we remove a directory we created, we replace the
		 * ft_name with NULL.  Ignore those.
		 */
		if (dblk->ft.ft_name == NULL)
			continue;

		/*
		 * frc_mode set, make sure we set the file modes even if
		 * the user didn't ask for it (see file_subs.c for more info)
		 */
		set_attr(&dblk->ft, 0, dblk->mode, pmode || dblk->frc_mode,
		    in_sig);
		if (!in_sig)
			free(dblk->ft.ft_name);
	}

	if (!in_sig)
		free(dirp);
	dirp = NULL;
	dircnt = 0;
}

/*
 * database independent routines
 */

/*
 * st_hash()
 *	hashes filenames to a u_int for hashing into a table. Looks at the tail
 *	end of file, as this provides far better distribution than any other
 *	part of the name. For performance reasons we only care about the last
 *	MAXKEYLEN chars (should be at LEAST large enough to pick off the file
 *	name). Was tested on 500,000 name file tree traversal from the root
 *	and gave almost a perfectly uniform distribution of keys when used with
 *	prime sized tables (MAXKEYLEN was 128 in test). Hashes (sizeof int)
 *	chars at a time and pads with 0 for last addition.
 * Return:
 *	the hash value of the string MOD (%) the table size.
 */

static u_int
st_hash(const char *name, int len, int tabsz)
{
	const char *pt;
	char *dest;
	const char *end;
	int i;
	u_int key = 0;
	int steps;
	int res;
	u_int val;

	/*
	 * only look at the tail up to MAXKEYLEN, we do not need to waste
	 * time here (remember these are pathnames, the tail is what will
	 * spread out the keys)
	 */
	if (len > MAXKEYLEN) {
		pt = &(name[len - MAXKEYLEN]);
		len = MAXKEYLEN;
	} else
		pt = name;

	/*
	 * calculate the number of u_int size steps in the string and if
	 * there is a runt to deal with
	 */
	steps = len/sizeof(u_int);
	res = len % sizeof(u_int);

	/*
	 * add up the value of the string in unsigned integer sized pieces
	 * too bad we cannot have unsigned int aligned strings, then we
	 * could avoid the expensive copy.
	 */
	for (i = 0; i < steps; ++i) {
		end = pt + sizeof(u_int);
		dest = (char *)&val;
		while (pt < end)
			*dest++ = *pt++;
		key += val;
	}

	/*
	 * add in the runt padded with zero to the right
	 */
	if (res) {
		val = 0;
		end = pt + res;
		dest = (char *)&val;
		while (pt < end)
			*dest++ = *pt++;
		key += val;
	}

	/*
	 * return the result mod the table size
	 */
	return(key % tabsz);
}
