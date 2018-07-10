/* vi: set sw=4 ts=4: */
/*
 * xreadlink.c - safe implementation of readlink.
 * Returns a NULL on failure.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Some systems (eg Hurd) do not have MAXSYMLINKS definition,
 * set it to some reasonable value if it isn't defined */
#ifndef MAXSYMLINKS
# define MAXSYMLINKS 20
#endif

/*
 * NOTE: This function returns a malloced char* that you will have to free
 * yourself.
 */
char* FAST_FUNC xmalloc_readlink(const char *path)
{
	enum { GROWBY = 80 }; /* how large we will grow strings by */

	char *buf = NULL;
	int bufsize = 0, readsize = 0;

	do {
		bufsize += GROWBY;
		buf = xrealloc(buf, bufsize);
		readsize = readlink(path, buf, bufsize);
		if (readsize == -1) {
			free(buf);
			return NULL;
		}
	} while (bufsize < readsize + 1);

	buf[readsize] = '\0';

	return buf;
}

/*
 * This routine is not the same as realpath(), which
 * canonicalizes the given path completely. This routine only
 * follows trailing symlinks until a real file is reached and
 * returns its name. If the path ends in a dangling link or if
 * the target doesn't exist, the path is returned in any case.
 * Intermediate symlinks in the path are not expanded -- only
 * those at the tail.
 * A malloced char* is returned, which must be freed by the caller.
 */
char* FAST_FUNC xmalloc_follow_symlinks(const char *path)
{
	char *buf;
	char *lpc;
	char *linkpath;
	int bufsize;
	int looping = MAXSYMLINKS + 1;

	buf = xstrdup(path);
	goto jump_in;

	while (1) {
		linkpath = xmalloc_readlink(buf);
		if (!linkpath) {
			/* not a symlink, or doesn't exist */
			if (errno == EINVAL || errno == ENOENT)
				return buf;
			goto free_buf_ret_null;
		}

		if (!--looping) {
			free(linkpath);
 free_buf_ret_null:
			free(buf);
			return NULL;
		}

		if (*linkpath != '/') {
			bufsize += strlen(linkpath);
			buf = xrealloc(buf, bufsize);
			lpc = bb_get_last_path_component_strip(buf);
			strcpy(lpc, linkpath);
			free(linkpath);
		} else {
			free(buf);
			buf = linkpath;
 jump_in:
			bufsize = strlen(buf) + 1;
		}
	}
}

char* FAST_FUNC xmalloc_readlink_or_warn(const char *path)
{
	char *buf = xmalloc_readlink(path);
	if (!buf) {
		/* EINVAL => "file: Invalid argument" => puzzled user */
		const char *errmsg = "not a symlink";
		int err = errno;
		if (err != EINVAL)
			errmsg = strerror(err);
		bb_error_msg("%s: cannot read link: %s", path, errmsg);
	}
	return buf;
}

char* FAST_FUNC xmalloc_realpath(const char *path)
{
/* NB: uclibc also defines __GLIBC__
 * Therefore the test "if glibc, or uclibc >= 0.9.31" looks a bit weird:
 */
#if defined(__GLIBC__) && \
    (!defined(__UCLIBC__) || UCLIBC_VERSION >= KERNEL_VERSION(0, 9, 31))
	/* glibc provides a non-standard extension */
	/* new: POSIX.1-2008 specifies this behavior as well */
	return realpath(path, NULL);
#else
	char buf[PATH_MAX+1];

	/* on error returns NULL (xstrdup(NULL) == NULL) */
	return xstrdup(realpath(path, buf));
#endif
}

char* FAST_FUNC xmalloc_realpath_coreutils(const char *path)
{
	char *buf;

	errno = 0;
	buf = xmalloc_realpath(path);
	/*
	 * There is one case when "readlink -f" and
	 * "realpath" from coreutils succeed,
	 * even though file does not exist, such as:
	 *     /tmp/file_does_not_exist
	 * (the directory must exist).
	 */
	if (!buf && errno == ENOENT) {
		char *last_slash = strrchr(path, '/');
		if (last_slash) {
			*last_slash++ = '\0';
			buf = xmalloc_realpath(path);
			if (buf) {
				unsigned len = strlen(buf);
				buf = xrealloc(buf, len + strlen(last_slash) + 2);
				buf[len++] = '/';
				strcpy(buf + len, last_slash);
			}
		}
	}

	return buf;
}
