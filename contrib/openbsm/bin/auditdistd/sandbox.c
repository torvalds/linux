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
#ifdef HAVE_JAIL
#include <sys/jail.h>
#endif
#ifdef HAVE_CAP_ENTER
#include <sys/capsicum.h>
#endif

#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "pjdlog.h"
#include "sandbox.h"

static int
groups_compare(const void *grp0, const void *grp1)
{
	gid_t gr0 = *(const gid_t *)grp0;
	gid_t gr1 = *(const gid_t *)grp1;

	return (gr0 <= gr1 ? (gr0 < gr1 ? -1 : 0) : 1);

}

int
sandbox(const char *user, bool capsicum, const char *fmt, ...)
{
#ifdef HAVE_JAIL
	struct jail jailst;
	char *jailhost;
	va_list ap;
#endif
	struct passwd *pw;
	uid_t ruid, euid;
	gid_t rgid, egid;
#ifdef HAVE_GETRESUID
	uid_t suid;
#endif
#ifdef HAVE_GETRESGID
	gid_t sgid;
#endif
	gid_t *groups, *ggroups;
	bool jailed;
	int ngroups, ret;

	PJDLOG_ASSERT(user != NULL);
	PJDLOG_ASSERT(fmt != NULL);

	ret = -1;
	groups = NULL;
	ggroups = NULL;

	/*
	 * According to getpwnam(3) we have to clear errno before calling the
	 * function to be able to distinguish between an error and missing
	 * entry (with is not treated as error by getpwnam(3)).
	 */
	errno = 0;
	pw = getpwnam(user);
	if (pw == NULL) {
		if (errno != 0) {
			pjdlog_errno(LOG_ERR,
			    "Unable to find info about '%s' user", user);
			goto out;
		} else {
			pjdlog_error("'%s' user doesn't exist.", user);
			errno = ENOENT;
			goto out;
		}
	}

	ngroups = sysconf(_SC_NGROUPS_MAX);
	if (ngroups == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to obtain maximum number of groups");
		ngroups = NGROUPS_MAX;
	}
	ngroups++;	/* For base gid. */
	groups = malloc(sizeof(groups[0]) * ngroups);
	if (groups == NULL) {
		pjdlog_error("Unable to allocate memory for %d groups.",
		    ngroups);
		goto out;
	}
	if (getgrouplist(user, pw->pw_gid, groups, &ngroups) == -1) {
		pjdlog_error("Unable to obtain groups of user %s.", user);
		goto out;
	}

#ifdef HAVE_JAIL
	va_start(ap, fmt);
	(void)vasprintf(&jailhost, fmt, ap);
	va_end(ap);
	if (jailhost == NULL) {
		pjdlog_error("Unable to allocate memory for jail host name.");
		goto out;
	}
	bzero(&jailst, sizeof(jailst));
	jailst.version = JAIL_API_VERSION;
	jailst.path = pw->pw_dir;
	jailst.hostname = jailhost;
	if (jail(&jailst) >= 0) {
		jailed = true;
	} else {
		jailed = false;
		pjdlog_errno(LOG_WARNING,
		    "Unable to jail to directory %s", pw->pw_dir);
	}
	free(jailhost);
#else	/* !HAVE_JAIL */
	jailed = false;
#endif	/* !HAVE_JAIL */

	if (!jailed) {
		if (chroot(pw->pw_dir) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to change root directory to %s",
			    pw->pw_dir);
			goto out;
		}
	}
	PJDLOG_VERIFY(chdir("/") == 0);

	if (setgroups(ngroups, groups) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set groups");
		goto out;
	}
	if (setgid(pw->pw_gid) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set gid to %u",
		    (unsigned int)pw->pw_gid);
		goto out;
	}
	if (setuid(pw->pw_uid) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set uid to %u",
		    (unsigned int)pw->pw_uid);
		goto out;
	}

#ifdef HAVE_CAP_ENTER
	if (capsicum) {
		capsicum = (cap_enter() == 0);
		if (!capsicum) {
			pjdlog_common(LOG_DEBUG, 1, errno,
			    "Unable to sandbox using capsicum");
		}
	}
#else	/* !HAVE_CAP_ENTER */
	capsicum = false;
#endif	/* !HAVE_CAP_ENTER */

	/*
	 * Better be sure that everything succeeded.
	 */
#ifdef HAVE_GETRESUID
	PJDLOG_VERIFY(getresuid(&ruid, &euid, &suid) == 0);
	PJDLOG_VERIFY(suid == pw->pw_uid);
#else
	ruid = getuid();
	euid = geteuid();
#endif
	PJDLOG_VERIFY(ruid == pw->pw_uid);
	PJDLOG_VERIFY(euid == pw->pw_uid);
#ifdef HAVE_GETRESGID
	PJDLOG_VERIFY(getresgid(&rgid, &egid, &sgid) == 0);
	PJDLOG_VERIFY(sgid == pw->pw_gid);
#else
	rgid = getgid();
	egid = getegid();
#endif
	PJDLOG_VERIFY(rgid == pw->pw_gid);
	PJDLOG_VERIFY(egid == pw->pw_gid);
	PJDLOG_VERIFY(getgroups(0, NULL) == ngroups);
	ggroups = malloc(sizeof(ggroups[0]) * ngroups);
	if (ggroups == NULL) {
		pjdlog_error("Unable to allocate memory for %d groups.",
		    ngroups);
		goto out;
	}
	PJDLOG_VERIFY(getgroups(ngroups, ggroups) == ngroups);
	qsort(groups, (size_t)ngroups, sizeof(groups[0]), groups_compare);
	qsort(ggroups, (size_t)ngroups, sizeof(ggroups[0]), groups_compare);
	PJDLOG_VERIFY(bcmp(groups, ggroups, sizeof(groups[0]) * ngroups) == 0);

	pjdlog_debug(1,
	    "Privileges successfully dropped using %s%s+setgid+setuid.",
	    capsicum ? "capsicum+" : "", jailed ? "jail" : "chroot");

	ret = 0;
out:
	if (groups != NULL)
		free(groups);
	if (ggroups != NULL)
		free(ggroups);
	return (ret);
}
