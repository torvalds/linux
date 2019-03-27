/*
 * Copyright (c) 2012 Darren Tucker (dtucker at zip com au).
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>

#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include "log.h"

#if !defined(HAVE_SETRESGID) || defined(BROKEN_SETRESGID)
int
setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	int ret = 0, saved_errno;

	if (rgid != sgid) {
		errno = ENOSYS;
		return -1;
	}
#if defined(HAVE_SETREGID) && !defined(BROKEN_SETREGID)
	if (setregid(rgid, egid) < 0) {
		saved_errno = errno;
		error("setregid %u: %.100s", rgid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
#else
	if (setegid(egid) < 0) {
		saved_errno = errno;
		error("setegid %u: %.100s", (u_int)egid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
	if (setgid(rgid) < 0) {
		saved_errno = errno;
		error("setgid %u: %.100s", rgid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
#endif
	return ret;
}
#endif

#if !defined(HAVE_SETRESUID) || defined(BROKEN_SETRESUID)
int
setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	int ret = 0, saved_errno;

	if (ruid != suid) {
		errno = ENOSYS;
		return -1;
	}
#if defined(HAVE_SETREUID) && !defined(BROKEN_SETREUID)
	if (setreuid(ruid, euid) < 0) {
		saved_errno = errno;
		error("setreuid %u: %.100s", ruid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
#else

# ifndef SETEUID_BREAKS_SETUID
	if (seteuid(euid) < 0) {
		saved_errno = errno;
		error("seteuid %u: %.100s", euid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
# endif
	if (setuid(ruid) < 0) {
		saved_errno = errno;
		error("setuid %u: %.100s", ruid, strerror(errno));
		errno = saved_errno;
		ret = -1;
	}
#endif
	return ret;
}
#endif
