/* vi: set sw=4 ts=4: */
/*
 * parse_mode implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 5, 2003    Manuel Novoa III
 *
 * This is the main work function for the 'mkdir' applet.  As such, it
 * strives to be SUSv3 compliant in it's behaviour when recursively
 * making missing parent dirs, and in it's mode setting of the final
 * directory 'path'.
 *
 * To recursively build all missing intermediate directories, make
 * sure that (flags & FILEUTILS_RECUR) is non-zero.  Newly created
 * intermediate directories will have at least u+wx perms.
 *
 * To set specific permissions on 'path', pass the appropriate 'mode'
 * val.  Otherwise, pass -1 to get default permissions.
 */
#include "libbb.h"

/* This function is used from NOFORK applets. It must not allocate anything */

int FAST_FUNC bb_make_directory(char *path, long mode, int flags)
{
	mode_t cur_mask;
	mode_t org_mask;
	const char *fail_msg;
	char *s;
	char c;
	struct stat st;

	/* "path" can be a result of dirname().
	 * dirname("no_slashes") returns ".", possibly read-only.
	 * musl dirname() can return read-only "/" too.
	 * We need writable string. And for "/", "." (and ".."?)
	 * nothing needs to be created anyway.
	 */
	if (LONE_CHAR(path, '/'))
		return 0;
	if (path[0] == '.') {
		if (path[1] == '\0')
			return 0; /* "." */
//		if (path[1] == '.' && path[2] == '\0')
//			return 0; /* ".." */
	}

	org_mask = cur_mask = (mode_t)-1L;
	s = path;
	while (1) {
		c = '\0';

		if (flags & FILEUTILS_RECUR) {  /* Get the parent */
			/* Bypass leading non-'/'s and then subsequent '/'s */
			while (*s) {
				if (*s == '/') {
					do {
						++s;
					} while (*s == '/');
					c = *s; /* Save the current char */
					*s = '\0'; /* and replace it with nul */
					break;
				}
				++s;
			}
		}

		if (c != '\0') {
			/* Intermediate dirs: must have wx for user */
			if (cur_mask == (mode_t)-1L) { /* wasn't done yet? */
				mode_t new_mask;
				org_mask = umask(0);
				cur_mask = 0;
				/* Clear u=wx in umask - this ensures
				 * they won't be cleared on mkdir */
				new_mask = (org_mask & ~(mode_t)0300);
				//bb_error_msg("org_mask:%o cur_mask:%o", org_mask, new_mask);
				if (new_mask != cur_mask) {
					cur_mask = new_mask;
					umask(new_mask);
				}
			}
		} else {
			/* Last component: uses original umask */
			//bb_error_msg("1 org_mask:%o", org_mask);
			if (org_mask != cur_mask) {
				cur_mask = org_mask;
				umask(org_mask);
			}
		}

		//bb_error_msg("mkdir '%s'", path);
		if (mkdir(path, 0777) < 0) {
			/* If we failed for any other reason than the directory
			 * already exists, output a diagnostic and return -1 */
			if ((errno != EEXIST && errno != EISDIR)
			 || !(flags & FILEUTILS_RECUR)
			 || ((stat(path, &st) < 0) || !S_ISDIR(st.st_mode))
			) {
				fail_msg = "create";
				break;
			}
			/* Since the directory exists, don't attempt to change
			 * permissions if it was the full target.  Note that
			 * this is not an error condition. */
			if (!c) {
				goto ret0;
			}
		} else {
			if (flags & FILEUTILS_VERBOSE) {
				printf("created directory: '%s'\n", path);
			}
		}

		if (!c) {
			/* Done.  If necessary, update perms on the newly
			 * created directory.  Failure to update here _is_
			 * an error. */
			if (mode != -1) {
				//bb_error_msg("chmod 0%03lo mkdir '%s'", mode, path);
				if (chmod(path, mode) < 0) {
					fail_msg = "set permissions of";
					if (flags & FILEUTILS_IGNORE_CHMOD_ERR) {
						flags = 0;
						goto print_err;
					}
					break;
				}
			}
			goto ret0;
		}

		/* Remove any inserted nul from the path (recursive mode) */
		*s = c;
	} /* while (1) */

	flags = -1;
 print_err:
	bb_perror_msg("can't %s directory '%s'", fail_msg, path);
	goto ret;
 ret0:
	flags = 0;
 ret:
	//bb_error_msg("2 org_mask:%o", org_mask);
	if (org_mask != cur_mask)
		umask(org_mask);
	return flags;
}
