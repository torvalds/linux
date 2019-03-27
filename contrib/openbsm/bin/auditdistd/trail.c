/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config/config.h>

#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <compat/compat.h>
#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif
#ifndef HAVE_FACCESSAT
#include "faccessat.h"
#endif
#ifndef HAVE_FSTATAT
#include "fstatat.h"
#endif
#ifndef HAVE_OPENAT
#include "openat.h"
#endif
#ifndef HAVE_UNLINKAT
#include "unlinkat.h"
#endif

#include "pjdlog.h"
#include "trail.h"

#define	TRAIL_MAGIC	0x79a11
struct trail {
	int	 tr_magic;
	/* Path usually to /var/audit/dist/ directory. */
	char	 tr_dirname[PATH_MAX];
	/* Descriptor to td_dirname directory. */
	DIR	*tr_dirfp;
	/* Path to audit trail file. */
	char	 tr_filename[PATH_MAX];
	/* Descriptor to audit trail file. */
	int	 tr_filefd;
};

#define	HALF_LEN	14

bool
trail_is_not_terminated(const char *filename)
{

	return (strcmp(filename + HALF_LEN, ".not_terminated") == 0);
}

bool
trail_is_crash_recovery(const char *filename)
{

	return (strcmp(filename + HALF_LEN, ".crash_recovery") == 0);
}

struct trail *
trail_new(const char *dirname, bool create)
{
	struct trail *trail;

	trail = calloc(1, sizeof(*trail));

	if (strlcpy(trail->tr_dirname, dirname, sizeof(trail->tr_dirname)) >=
	    sizeof(trail->tr_dirname)) {
		free(trail);
		pjdlog_error("Directory name too long (\"%s\").", dirname);
		errno = ENAMETOOLONG;
		return (NULL);
	}
	trail->tr_dirfp = opendir(dirname);
	if (trail->tr_dirfp == NULL) {
		if (create && errno == ENOENT) {
			if (mkdir(dirname, 0700) == -1) {
				pjdlog_errno(LOG_ERR,
				    "Unable to create directory \"%s\"",
				    dirname);
				free(trail);
				return (NULL);
			}
			/* TODO: Set directory ownership. */
		} else {
			pjdlog_errno(LOG_ERR,
			    "Unable to open directory \"%s\"",
			    dirname);
			free(trail);
			return (NULL);
		}
		trail->tr_dirfp = opendir(dirname);
		if (trail->tr_dirfp == NULL) {
			pjdlog_errno(LOG_ERR,
			    "Unable to open directory \"%s\"",
			    dirname);
			free(trail);
			return (NULL);
		}
	}
	trail->tr_filefd = -1;
	trail->tr_magic = TRAIL_MAGIC;
	return (trail);
}

void
trail_free(struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);

	if (trail->tr_filefd != -1)
		trail_close(trail);
	closedir(trail->tr_dirfp);
	bzero(trail, sizeof(*trail));
	trail->tr_magic = 0;
	trail->tr_filefd = -1;
	free(trail);
}

static uint8_t
trail_type(DIR *dirfp, const char *filename)
{
	struct stat sb;
	int dfd;

	PJDLOG_ASSERT(dirfp != NULL);

	dfd = dirfd(dirfp);
	PJDLOG_ASSERT(dfd >= 0);
	if (fstatat(dfd, filename, &sb, AT_SYMLINK_NOFOLLOW) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to stat \"%s\"", filename);
		return (DT_UNKNOWN);
	}
	return (IFTODT(sb.st_mode));
}

/*
 * Find trail file by first part of the name in case it was renamed.
 * First part of the trail file name never changes, but trail file
 * can be renamed when hosts are disconnected from .not_terminated
 * to .[0-9]{14} or to .crash_recovery.
 */
static bool
trail_find(struct trail *trail)
{
	struct dirent *dp;

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(trail_is_not_terminated(trail->tr_filename));

	rewinddir(trail->tr_dirfp);
	while ((dp = readdir(trail->tr_dirfp)) != NULL) {
		if (strncmp(dp->d_name, trail->tr_filename, HALF_LEN + 1) == 0)
			break;
	}
	if (dp == NULL)
		return (false);
	PJDLOG_VERIFY(strlcpy(trail->tr_filename, dp->d_name,
	    sizeof(trail->tr_filename)) < sizeof(trail->tr_filename));
	return (true);
}

/*
 * Open the given trail file and move pointer at the given offset, as this is
 * where receiver finished the last time.
 * If the file doesn't exist or the given offset is equal to the file size,
 * move to the next trail file.
 */
void
trail_start(struct trail *trail, const char *filename, off_t offset)
{
	struct stat sb;
	int dfd, fd;

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);

	PJDLOG_VERIFY(strlcpy(trail->tr_filename, filename,
	    sizeof(trail->tr_filename)) < sizeof(trail->tr_filename));
	trail->tr_filefd = -1;

	if (trail->tr_filename[0] == '\0') {
		PJDLOG_ASSERT(offset == 0);
		trail_next(trail);
		return;
	}

	dfd = dirfd(trail->tr_dirfp);
	PJDLOG_ASSERT(dfd >= 0);
again:
	fd = openat(dfd, trail->tr_filename, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT &&
		    trail_is_not_terminated(trail->tr_filename) &&
		    trail_find(trail)) {
			/* File was renamed. Retry with new name. */
			pjdlog_debug(1,
			   "Trail file was renamed since last connection to \"%s/%s\".",
			   trail->tr_dirname, trail->tr_filename);
			goto again;
		} else if (errno == ENOENT) {
			/* File disappeared. */
			pjdlog_debug(1, "File \"%s/%s\" doesn't exist.",
			    trail->tr_dirname, trail->tr_filename);
		} else {
			pjdlog_errno(LOG_ERR,
			    "Unable to open file \"%s/%s\", skipping",
			    trail->tr_dirname, trail->tr_filename);
		}
		trail_next(trail);
		return;
	}
	if (fstat(fd, &sb) == -1) {
		pjdlog_errno(LOG_ERR,
		    "Unable to stat file \"%s/%s\", skipping",
		    trail->tr_dirname, trail->tr_filename);
		close(fd);
		trail_next(trail);
		return;
	}
	if (!S_ISREG(sb.st_mode)) {
		pjdlog_warning("File \"%s/%s\" is not a regular file, skipping.",
		    trail->tr_dirname, trail->tr_filename);
		close(fd);
		trail_next(trail);
		return;
	}
	/*
	 * We continue sending requested file if:
	 * 1. It is not fully sent yet, or
	 * 2. It is fully sent, but is not terminated, so new data can be
	 *    appended still, or
	 * 3. It is fully sent but file name has changed.
	 *    There are two cases here:
	 *    3a. Sender has crashed and the name has changed from
	 *        .not_terminated to .crash_recovery.
	 *    3b. Sender was disconnected, no new data was added to the file,
	 *        but its name has changed from .not_terminated to terminated
	 *        name.
	 *
	 * Note that we are fine if our .not_terminated or .crash_recovery file
	 * is smaller than the one on the receiver side, as it is possible that
	 * more data was send to the receiver than was safely stored on disk.
	 * We accept .not_terminated only because auditdistd can start before
	 * auditd manage to rename it to .crash_recovery.
	 */
	if (offset < sb.st_size ||
	    (offset >= sb.st_size &&
	     trail_is_not_terminated(trail->tr_filename)) ||
	    (offset >= sb.st_size && trail_is_not_terminated(filename) &&
	     !trail_is_not_terminated(trail->tr_filename))) {
		/* File was not fully send. Let's finish it. */
		if (lseek(fd, offset, SEEK_SET) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to move to offset %jd within file \"%s/%s\", skipping",
			    (intmax_t)offset, trail->tr_dirname,
			    trail->tr_filename);
			close(fd);
			trail_next(trail);
			return;
		}
		if (!trail_is_crash_recovery(trail->tr_filename)) {
			pjdlog_debug(1,
			    "Restarting file \"%s/%s\" at offset %jd.",
			    trail->tr_dirname, trail->tr_filename,
			    (intmax_t)offset);
		}
		trail->tr_filefd = fd;
		return;
	}
	close(fd);
	if (offset > sb.st_size) {
		pjdlog_warning("File \"%s/%s\" shrinked, removing it.",
		    trail->tr_dirname, trail->tr_filename);
	} else {
		pjdlog_debug(1, "File \"%s/%s\" is already sent, removing it.",
		    trail->tr_dirname, trail->tr_filename);
	}
	/* Entire file is already sent or it shirnked, we can remove it. */
	if (unlinkat(dfd, trail->tr_filename, 0) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to remove file \"%s/%s\"",
		    trail->tr_dirname, trail->tr_filename);
	}
	trail_next(trail);
}

/*
 * Set next file in the trail->tr_dirname directory and open it for reading.
 */
void
trail_next(struct trail *trail)
{
	char curfile[PATH_MAX];
	struct dirent *dp;
	int dfd;

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(trail->tr_filefd == -1);

again:
	curfile[0] = '\0';

	rewinddir(trail->tr_dirfp);
	while ((dp = readdir(trail->tr_dirfp)) != NULL) {
		if (dp->d_name[0] < '0' || dp->d_name[0] > '9')
			continue;
		if (dp->d_type == DT_UNKNOWN)
			dp->d_type = trail_type(trail->tr_dirfp, dp->d_name);
		/* We are only interested in regular files, skip the rest. */
		if (dp->d_type != DT_REG) {
			pjdlog_debug(1,
			    "File \"%s/%s\" is not a regular file, skipping.",
			    trail->tr_dirname, dp->d_name);
			continue;
		}
		/* Skip all files "greater" than curfile. */
		if (curfile[0] != '\0' && strcmp(dp->d_name, curfile) > 0)
			continue;
		/* Skip all files "smaller" than the current trail_filename. */
		if (trail->tr_filename[0] != '\0' &&
		    strcmp(dp->d_name, trail->tr_filename) <= 0) {
			continue;
		}
		PJDLOG_VERIFY(strlcpy(curfile, dp->d_name, sizeof(curfile)) <
		    sizeof(curfile));
	}
	if (curfile[0] == '\0') {
		/*
		 * There are no new trail files, so we return.
		 * We don't clear trail_filename string, to know where to
		 * start when new file appears.
		 */
		PJDLOG_ASSERT(trail->tr_filefd == -1);
		pjdlog_debug(1, "No new trail files.");
		return;
	}
	dfd = dirfd(trail->tr_dirfp);
	PJDLOG_ASSERT(dfd >= 0);
	trail->tr_filefd = openat(dfd, curfile, O_RDONLY);
	if (trail->tr_filefd == -1) {
		if (errno == ENOENT && trail_is_not_terminated(curfile)) {
			/*
			 * The .not_terminated file was most likely renamed.
			 * Keep trail->tr_filename as a starting point and
			 * search again.
			 */
			pjdlog_debug(1,
			    "Unable to open \"%s/%s\", most likely renamed in the meantime, retrying.",
			    trail->tr_dirname, curfile);
		} else {
			/*
			 * We were unable to open the file, but not because of
			 * the above. This shouldn't happen, but it did.
			 * We don't know why it happen, so the best we can do
			 * is to just skip this file - this is why we copy the
			 * name, so we can start and the next entry.
			 */
			PJDLOG_VERIFY(strlcpy(trail->tr_filename, curfile,
			    sizeof(trail->tr_filename)) <
			    sizeof(trail->tr_filename));
			pjdlog_errno(LOG_ERR,
			    "Unable to open file \"%s/%s\", skipping",
			    trail->tr_dirname, curfile);
		}
		goto again;
	}
	PJDLOG_VERIFY(strlcpy(trail->tr_filename, curfile,
	    sizeof(trail->tr_filename)) < sizeof(trail->tr_filename));
	pjdlog_debug(1, "Found next trail file: \"%s/%s\".", trail->tr_dirname,
	    trail->tr_filename);
}

/*
 * Close current trial file.
 */
void
trail_close(struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(trail->tr_filefd >= 0);
	PJDLOG_ASSERT(trail->tr_filename[0] != '\0');

	PJDLOG_VERIFY(close(trail->tr_filefd) == 0);
	trail->tr_filefd = -1;
}

/*
 * Reset trail state. Used when connection is disconnected and we will
 * need to start over after reconnect. Trail needs to be already closed.
 */
void
trail_reset(struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(trail->tr_filefd == -1);

	trail->tr_filename[0] = '\0';
}

/*
 * Unlink current trial file.
 */
void
trail_unlink(struct trail *trail, const char *filename)
{
	int dfd;

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(filename != NULL);
	PJDLOG_ASSERT(filename[0] != '\0');

	dfd = dirfd(trail->tr_dirfp);
	PJDLOG_ASSERT(dfd >= 0);

	if (unlinkat(dfd, filename, 0) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to remove \"%s/%s\"",
		    trail->tr_dirname, filename);
	} else {
		pjdlog_debug(1, "Trail file \"%s/%s\" removed.",
		    trail->tr_dirname, filename);
	}
}

/*
 * Return true if we should switch to next trail file.
 * We don't switch if our file name ends with ".not_terminated" and it
 * exists (ie. wasn't renamed).
 */
bool
trail_switch(struct trail *trail)
{
	char filename[PATH_MAX];
	int fd;

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);
	PJDLOG_ASSERT(trail->tr_filefd >= 0);

	if (!trail_is_not_terminated(trail->tr_filename))
		return (true);
	fd = dirfd(trail->tr_dirfp);
	PJDLOG_ASSERT(fd >= 0);
	if (faccessat(fd, trail->tr_filename, F_OK, 0) == 0)
		return (false);
	if (errno != ENOENT) {
		pjdlog_errno(LOG_ERR, "Unable to access file \"%s/%s\"",
		    trail->tr_dirname, trail->tr_filename);
	}
	strlcpy(filename, trail->tr_filename, sizeof(filename));
	if (!trail_find(trail)) {
		pjdlog_error("Trail file \"%s/%s\" disappeared.",
		    trail->tr_dirname, trail->tr_filename);
		return (true);
	}
	pjdlog_debug(1, "Trail file \"%s/%s\" was renamed to \"%s/%s\".",
	    trail->tr_dirname, filename, trail->tr_dirname,
	    trail->tr_filename);
	return (true);
}

const char *
trail_filename(const struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);

	return (trail->tr_filename);
}

int
trail_filefd(const struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);

	return (trail->tr_filefd);
}

int
trail_dirfd(const struct trail *trail)
{

	PJDLOG_ASSERT(trail->tr_magic == TRAIL_MAGIC);

	return (dirfd(trail->tr_dirfp));
}

/*
 * Find the last file in the directory opened under dirfp.
 */
void
trail_last(DIR *dirfp, char *filename, size_t filenamesize)
{
	char curfile[PATH_MAX];
	struct dirent *dp;

	PJDLOG_ASSERT(dirfp != NULL);

	curfile[0] = '\0';

	rewinddir(dirfp);
	while ((dp = readdir(dirfp)) != NULL) {
		if (dp->d_name[0] < '0' || dp->d_name[0] > '9')
			continue;
		if (dp->d_type == DT_UNKNOWN)
			dp->d_type = trail_type(dirfp, dp->d_name);
		/* We are only interested in regular files, skip the rest. */
		if (dp->d_type != DT_REG)
			continue;
		/* Skip all files "greater" than curfile. */
		if (curfile[0] != '\0' && strcmp(dp->d_name, curfile) < 0)
			continue;
		PJDLOG_VERIFY(strlcpy(curfile, dp->d_name, sizeof(curfile)) <
		    sizeof(curfile));
	}
	if (curfile[0] == '\0') {
		/*
		 * There are no trail files, so we return.
		 */
		pjdlog_debug(1, "No trail files.");
		bzero(filename, filenamesize);
		return;
	}
	PJDLOG_VERIFY(strlcpy(filename, curfile, filenamesize) < filenamesize);
	pjdlog_debug(1, "Found the most recent trail file: \"%s\".", filename);
}

/*
 * Check if the given file name is a valid audit trail file name.
 * Possible names:
 * 20120106132657.20120106132805
 * 20120106132657.not_terminated
 * 20120106132657.crash_recovery
 * If two names are given, check if the first name can be renamed
 * to the second name. When renaming, first part of the name has
 * to be identical and only the following renames are valid:
 * 20120106132657.not_terminated -> 20120106132657.20120106132805
 * 20120106132657.not_terminated -> 20120106132657.crash_recovery
 */
bool
trail_validate_name(const char *srcname, const char *dstname)
{
	int i;

	PJDLOG_ASSERT(srcname != NULL);

	if (strlen(srcname) != 2 * HALF_LEN + 1)
		return (false);
	if (srcname[HALF_LEN] != '.')
		return (false);
	for (i = 0; i < HALF_LEN; i++) {
		if (srcname[i] < '0' || srcname[i] > '9')
			return (false);
	}
	for (i = HALF_LEN + 1; i < 2 * HALF_LEN - 1; i++) {
		if (srcname[i] < '0' || srcname[i] > '9')
			break;
	}
	if (i < 2 * HALF_LEN - 1 &&
	    strcmp(srcname + HALF_LEN + 1, "not_terminated") != 0 &&
	    strcmp(srcname + HALF_LEN + 1, "crash_recovery") != 0) {
		return (false);
	}

	if (dstname == NULL)
		return (true);

	/* We tolarate if both names are identical. */
	if (strcmp(srcname, dstname) == 0)
		return (true);

	/* We can only rename not_terminated files. */
	if (strcmp(srcname + HALF_LEN + 1, "not_terminated") != 0)
		return (false);
	if (strlen(dstname) != 2 * HALF_LEN + 1)
		return (false);
	if (strncmp(srcname, dstname, HALF_LEN + 1) != 0)
		return (false);
	for (i = HALF_LEN + 1; i < 2 * HALF_LEN - 1; i++) {
		if (dstname[i] < '0' || dstname[i] > '9')
			break;
	}
	if (i < 2 * HALF_LEN - 1 &&
	    strcmp(dstname + HALF_LEN + 1, "crash_recovery") != 0) {
		return (false);
	}

	return (true);
}

int
trail_name_compare(const char *name0, const char *name1)
{
	int ret;

	ret = strcmp(name0, name1);
	if (ret == 0)
		return (TRAIL_IDENTICAL);
	if (strncmp(name0, name1, HALF_LEN + 1) == 0)
		return (TRAIL_RENAMED);
	return (ret < 0 ? TRAIL_OLDER : TRAIL_NEWER);
}
