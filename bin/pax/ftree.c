/*	$OpenBSD: ftree.c,v 1.43 2024/08/15 00:47:44 guenther Exp $	*/
/*	$NetBSD: ftree.c,v 1.4 1995/03/21 09:07:21 cgd Exp $	*/

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
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pax.h"
#include "extern.h"

/*
 * Data structure used to store the file args to be handed to fts().
 * It keeps track of which args generated a "selected" member.
 */
typedef struct ftree {
	char		*fname;		/* file tree name */
	int		refcnt;		/* had a selected (or skipped) file? */
	int		chflg;		/* change directory flag */
	struct ftree	*fow;		/* pointer to next entry on list */
} FTREE;


/*
 * routines to interface with the fts library function.
 *
 * file args supplied to pax are stored on a single linked list (of type FTREE)
 * and given to fts to be processed one at a time. pax "selects" files from
 * the expansion of each arg into the corresponding file tree (if the arg is a
 * directory, otherwise the node itself is just passed to pax). The selection
 * is modified by the -n and -u flags. The user is informed when a specific
 * file arg does not generate any selected files. -n keeps expanding the file
 * tree arg until one of its files is selected, then skips to the next file
 * arg. when the user does not supply the file trees as command line args to
 * pax, they are read from stdin
 */

static FTS *ftsp = NULL;		/* current FTS handle */
static int ftsopts;			/* options to be used on fts_open */
static char *farray[2];			/* array for passing each arg to fts */
static FTREE *fthead = NULL;		/* head of linked list of file args */
static FTREE *fttail = NULL;		/* tail of linked list of file args */
static FTREE *ftcur = NULL;		/* current file arg being processed */
static FTSENT *ftent = NULL;		/* current file tree entry */
static int ftree_skip;			/* when set skip to next file arg */

static int ftree_arg(void);
static char *getpathname(char *, int);

/*
 * ftree_start()
 *	initialize the options passed to fts_open() during this run of pax
 *	options are based on the selection of pax options by the user
 *	fts_start() also calls fts_arg() to open the first valid file arg. We
 *	also attempt to reset directory access times when -t (tflag) is set.
 * Return:
 *	0 if there is at least one valid file arg to process, -1 otherwise
 */

int
ftree_start(void)
{
	/*
	 * set up the operation mode of fts, open the first file arg. We must
	 * use FTS_NOCHDIR, as the user may have to open multiple archives and
	 * if fts did a chdir off into the boondocks, we may create an archive
	 * volume in an place where the user did not expect to.
	 */
	ftsopts = FTS_NOCHDIR;

	/*
	 * optional user flags that effect file traversal
	 * -H command line symlink follow only (half follow)
	 * -L follow sylinks (logical)
	 * -P do not follow sylinks (physical). This is the default.
	 * -X do not cross over mount points
	 * -t preserve access times on files read.
	 * -n select only the first member of a file tree when a match is found
	 * -d do not extract subtrees rooted at a directory arg.
	 */
	if (Lflag)
		ftsopts |= FTS_LOGICAL;
	else
		ftsopts |= FTS_PHYSICAL;
	if (Hflag)
		ftsopts |= FTS_COMFOLLOW;
	if (Xflag)
		ftsopts |= FTS_XDEV;

	if ((fthead == NULL) && ((farray[0] = malloc(PAXPATHLEN+2)) == NULL)) {
		paxwarn(1, "Unable to allocate memory for file name buffer");
		return(-1);
	}

	if (ftree_arg() < 0)
		return(-1);
	if (tflag && (atdir_start() < 0))
		return(-1);
	return(0);
}

/*
 * ftree_add()
 *	add the arg to the linked list of files to process. Each will be
 *	processed by fts one at a time
 * Return:
 *	0 if added to the linked list, -1 if failed
 */

int
ftree_add(char *str, int chflg)
{
	FTREE *ft;
	int len;

	/*
	 * simple check for bad args
	 */
	if ((str == NULL) || (*str == '\0')) {
		paxwarn(0, "Invalid file name argument");
		return(-1);
	}

	/*
	 * allocate FTREE node and add to the end of the linked list (args are
	 * processed in the same order they were passed to pax). Get rid of any
	 * trailing / the user may pass us. (watch out for / by itself).
	 */
	if ((ft = malloc(sizeof(FTREE))) == NULL) {
		paxwarn(0, "Unable to allocate memory for filename");
		return(-1);
	}

	if (((len = strlen(str) - 1) > 0) && (str[len] == '/'))
		str[len] = '\0';
	ft->fname = str;
	ft->refcnt = 0;
	ft->chflg = chflg;
	ft->fow = NULL;
	if (fthead == NULL) {
		fttail = fthead = ft;
		return(0);
	}
	fttail->fow = ft;
	fttail = ft;
	return(0);
}

/*
 * ftree_sel()
 *	this entry has been selected by pax. bump up reference count and handle
 *	-n and -d processing.
 */

void
ftree_sel(ARCHD *arcn)
{
	/*
	 * set reference bit for this pattern. This linked list is only used
	 * when file trees are supplied pax as args. The list is not used when
	 * the trees are read from stdin.
	 */
	if (ftcur != NULL)
		ftcur->refcnt = 1;

	/*
	 * if -n we are done with this arg, force a skip to the next arg when
	 * pax asks for the next file in next_file().
	 * if -d we tell fts only to match the directory (if the arg is a dir)
	 * and not the entire file tree rooted at that point.
	 */
	if (nflag)
		ftree_skip = 1;

	if (!dflag || (arcn->type != PAX_DIR))
		return;

	if (ftent != NULL)
		(void)fts_set(ftsp, ftent, FTS_SKIP);
}

/*
 * ftree_skipped_newer()
 *	file has been skipped because a newer file exists and -u/-D given
 */

void
ftree_skipped_newer(ARCHD *arcn)
{
	/* skipped due to -u/-D, mark accordingly */
	if (ftcur != NULL)
		ftcur->refcnt = 1;
}

/*
 * ftree_chk()
 *	called at end on pax execution. Prints all those file args that did not
 *	have a selected member (reference count still 0)
 */

void
ftree_chk(void)
{
	FTREE *ft;
	int wban = 0;

	/*
	 * make sure all dir access times were reset.
	 */
	if (tflag)
		atdir_end();

	/*
	 * walk down list and check reference count. Print out those members
	 * that never had a match
	 */
	for (ft = fthead; ft != NULL; ft = ft->fow) {
		if ((ft->refcnt > 0) || ft->chflg)
			continue;
		if (wban == 0) {
			paxwarn(1,"WARNING! These file names were not selected:");
			++wban;
		}
		(void)fprintf(stderr, "%s\n", ft->fname);
	}
}

/*
 * ftree_arg()
 *	Get the next file arg for fts to process. Can be from either the linked
 *	list or read from stdin when the user did not them as args to pax. Each
 *	arg is processed until the first successful fts_open().
 * Return:
 *	0 when the next arg is ready to go, -1 if out of file args (or EOF on
 *	stdin).
 */

static int
ftree_arg(void)
{

	/*
	 * close off the current file tree
	 */
	if (ftsp != NULL) {
		(void)fts_close(ftsp);
		ftsp = NULL;
	}

	/*
	 * keep looping until we get a valid file tree to process. Stop when we
	 * reach the end of the list (or get an eof on stdin)
	 */
	for (;;) {
		if (fthead == NULL) {
			/*
			 * the user didn't supply any args, get the file trees
			 * to process from stdin;
			 */
			if (getpathname(farray[0], PAXPATHLEN+1) == NULL)
				return(-1);
		} else {
			/*
			 * the user supplied the file args as arguments to pax
			 */
			if (ftcur == NULL)
				ftcur = fthead;
			else if ((ftcur = ftcur->fow) == NULL)
				return(-1);
			if (ftcur->chflg) {
				/* First fchdir() back... */
				if (fchdir(cwdfd) == -1) {
					syswarn(1, errno,
					  "Can't fchdir to starting directory");
					return(-1);
				}
				if (chdir(ftcur->fname) == -1) {
					syswarn(1, errno, "Can't chdir to %s",
					    ftcur->fname);
					return(-1);
				}
				continue;
			} else
				farray[0] = ftcur->fname;
		}

		/*
		 * watch it, fts wants the file arg stored in a array of char
		 * ptrs, with the last one a null. we use a two element array
		 * and set farray[0] to point at the buffer with the file name
		 * in it. We cannot pass all the file args to fts at one shot
		 * as we need to keep a handle on which file arg generates what
		 * files (the -n and -d flags need this). If the open is
		 * successful, return a 0.
		 */
		if ((ftsp = fts_open(farray, ftsopts, NULL)) != NULL)
			break;
	}
	return(0);
}

/*
 * next_file()
 *	supplies the next file to process in the supplied archd structure.
 * Return:
 *	0 when contents of arcn have been set with the next file, -1 when done.
 */

int
next_file(ARCHD *arcn)
{
	int cnt;

	/*
	 * ftree_sel() might have set the ftree_skip flag if the user has the
	 * -n option and a file was selected from this file arg tree. (-n says
	 * only one member is matched for each pattern) ftree_skip being 1
	 * forces us to go to the next arg now.
	 */
	if (ftree_skip) {
		/*
		 * clear and go to next arg
		 */
		ftree_skip = 0;
		if (ftree_arg() < 0)
			return(-1);
	}

	/*
	 * loop until we get a valid file to process
	 */
	for (;;) {
		if ((ftent = fts_read(ftsp)) == NULL) {
			if (errno)
				syswarn(1, errno, "next_file");
			/*
			 * out of files in this tree, go to next arg, if none
			 * we are done
			 */
			if (ftree_arg() < 0)
				return(-1);
			continue;
		}

		/*
		 * handle each type of fts_read() flag
		 */
		switch (ftent->fts_info) {
		case FTS_D:
		case FTS_DEFAULT:
		case FTS_F:
		case FTS_SL:
		case FTS_SLNONE:
			/*
			 * these are all ok
			 */
			break;
		case FTS_DP:
			/*
			 * already saw this directory. If the user wants file
			 * access times reset, we use this to restore the
			 * access time for this directory since this is the
			 * last time we will see it in this file subtree
			 * remember to force the time (this is -t on a read
			 * directory, not a created directory).
			 */
			if (!tflag)
				continue;
			do_atdir(ftent->fts_path, ftent->fts_statp->st_dev,
			    ftent->fts_statp->st_ino);
			continue;
		case FTS_DC:
			/*
			 * fts claims a file system cycle
			 */
			paxwarn(1,"File system cycle found at %s",ftent->fts_path);
			continue;
		case FTS_DNR:
			syswarn(1, ftent->fts_errno,
			    "Unable to read directory %s", ftent->fts_path);
			continue;
		case FTS_ERR:
			syswarn(1, ftent->fts_errno,
			    "File system traversal error");
			continue;
		case FTS_NS:
		case FTS_NSOK:
			syswarn(1, ftent->fts_errno,
			    "Unable to access %s", ftent->fts_path);
			continue;
		}

		/*
		 * ok got a file tree node to process. copy info into arcn
		 * structure (initialize as required)
		 */
		arcn->skip = 0;
		arcn->pad = 0;
		arcn->ln_nlen = 0;
		arcn->ln_name[0] = '\0';
		memcpy(&arcn->sb, ftent->fts_statp, sizeof(arcn->sb));

		/*
		 * file type based set up and copy into the arcn struct
		 * SIDE NOTE:
		 * we try to reset the access time on all files and directories
		 * we may read when the -t flag is specified. files are reset
		 * when we close them after copying. we reset the directories
		 * when we are done with their file tree (we also clean up at
		 * end in case we cut short a file tree traversal). However
		 * there is no way to reset access times on symlinks.
		 */
		switch (S_IFMT & arcn->sb.st_mode) {
		case S_IFDIR:
			arcn->type = PAX_DIR;
			if (!tflag)
				break;
			add_atdir(ftent->fts_path, arcn->sb.st_dev,
			    arcn->sb.st_ino, &arcn->sb.st_mtim,
			    &arcn->sb.st_atim);
			break;
		case S_IFCHR:
			arcn->type = PAX_CHR;
			break;
		case S_IFBLK:
			arcn->type = PAX_BLK;
			break;
		case S_IFREG:
			/*
			 * only regular files with have data to store on the
			 * archive. all others will store a zero length skip.
			 * the skip field is used by pax for actual data it has
			 * to read (or skip over).
			 */
			arcn->type = PAX_REG;
			arcn->skip = arcn->sb.st_size;
			break;
		case S_IFLNK:
			arcn->type = PAX_SLK;
			/*
			 * have to read the symlink path from the file
			 */
			if ((cnt = readlink(ftent->fts_path, arcn->ln_name,
			    PAXPATHLEN)) == -1) {
				syswarn(1, errno, "Unable to read symlink %s",
				    ftent->fts_path);
				continue;
			}
			/*
			 * set link name length, watch out readlink does not
			 * NUL terminate the link path
			 */
			arcn->ln_name[cnt] = '\0';
			arcn->ln_nlen = cnt;
			break;
		case S_IFSOCK:
			/*
			 * under BSD storing a socket is senseless but we will
			 * let the format specific write function make the
			 * decision of what to do with it.
			 */
			arcn->type = PAX_SCK;
			break;
		case S_IFIFO:
			arcn->type = PAX_FIF;
			break;
		}
		break;
	}

	/*
	 * copy file name, set file name length
	 */
	arcn->nlen = strlcpy(arcn->name, ftent->fts_path, sizeof(arcn->name));
	if ((size_t)arcn->nlen >= sizeof(arcn->name))
		arcn->nlen = sizeof(arcn->name) - 1; /* XXX truncate? */
	arcn->org_name = ftent->fts_path;
	return(0);
}

/*
 * getpathname()
 *	Reads a pathname from stdin, handling NUL- or newline-termination.
 * Return:
 *	NULL at end of file, otherwise the NUL-terminated buffer.
 */

static char *
getpathname(char *buf, int buflen)
{
	char *bp, *ep;
	int ch, term;

	if (zeroflag) {
		/*
		 * Read a NUL-terminated pathname, being especially
		 * paranoid about proper termination and pathname length.
		 */
		for (bp = buf, ep = buf + buflen; bp < ep; bp++) {
			if ((ch = getchar()) == EOF) {
				if (bp != buf)
					paxwarn(1, "Ignoring unterminated "
					    "pathname at EOF");
				return(NULL);
			}
			if ((*bp = ch) == '\0')
				return(buf);
		}
		/* Too long - skip this path */
		*--bp = '\0';
		term = '\0';
	} else {
		if (fgets(buf, buflen, stdin) == NULL)
			return(NULL);
		if ((bp = strchr(buf, '\n')) != NULL || feof(stdin)) {
			if (bp != NULL)
				*bp = '\0';
			return(buf);
		}
		/* Too long - skip this path */
		term = '\n';
	}
	while ((ch = getchar()) != term && ch != EOF)
		continue;
	paxwarn(1, "Ignoring too-long pathname: %s", buf);
	return(NULL);
}
