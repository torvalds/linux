/*-
 * Copyright (c) 2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_check_owner_perms.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"

/*
 * OpenPAM internal
 *
 * Verify that the file or directory referenced by the given descriptor is
 * owned by either root or the arbitrator and that it is not writable by
 * group or other.
 */

int
openpam_check_desc_owner_perms(const char *name, int fd)
{
	uid_t root, arbitrator;
	struct stat sb;
	int serrno;

	root = 0;
	arbitrator = geteuid();
	if (fstat(fd, &sb) != 0) {
		serrno = errno;
		openpam_log(PAM_LOG_ERROR, "%s: %m", name);
		errno = serrno;
		return (-1);
	}
	if (!S_ISREG(sb.st_mode)) {
		openpam_log(PAM_LOG_ERROR,
		    "%s: not a regular file", name);
		errno = EINVAL;
		return (-1);
	}
	if ((sb.st_uid != root && sb.st_uid != arbitrator) ||
	    (sb.st_mode & (S_IWGRP|S_IWOTH)) != 0) {
		openpam_log(PAM_LOG_ERROR,
		    "%s: insecure ownership or permissions", name);
		errno = EPERM;
		return (-1);
	}
	return (0);
}

/*
 * OpenPAM internal
 *
 * Verify that a file or directory and all components of the path leading
 * up to it are owned by either root or the arbitrator and that they are
 * not writable by group or other.
 *
 * Note that openpam_check_desc_owner_perms() should be used instead if
 * possible to avoid a race between the ownership / permission check and
 * the actual open().
 */

int
openpam_check_path_owner_perms(const char *path)
{
	uid_t root, arbitrator;
	char pathbuf[PATH_MAX];
	struct stat sb;
	int len, serrno, tip;

	tip = 1;
	root = 0;
	arbitrator = geteuid();
	if (realpath(path, pathbuf) == NULL)
		return (-1);
	len = strlen(pathbuf);
	while (len > 0) {
		if (stat(pathbuf, &sb) != 0) {
			if (errno != ENOENT) {
				serrno = errno;
				openpam_log(PAM_LOG_ERROR, "%s: %m", pathbuf);
				errno = serrno;
			}
			return (-1);
		}
		if (tip && !S_ISREG(sb.st_mode)) {
			openpam_log(PAM_LOG_ERROR,
			    "%s: not a regular file", pathbuf);
			errno = EINVAL;
			return (-1);
		}
		if ((sb.st_uid != root && sb.st_uid != arbitrator) ||
		    (sb.st_mode & (S_IWGRP|S_IWOTH)) != 0) {
			openpam_log(PAM_LOG_ERROR,
			    "%s: insecure ownership or permissions", pathbuf);
			errno = EPERM;
			return (-1);
		}
		while (--len > 0 && pathbuf[len] != '/')
			pathbuf[len] = '\0';
		tip = 0;
	}
	return (0);
}

/*
 * NOPARSE
 */
