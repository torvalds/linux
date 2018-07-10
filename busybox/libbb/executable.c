/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2006 Gabriel Somlo <somlo at cmu.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* check if path points to an executable file;
 * return 1 if found;
 * return 0 otherwise;
 */
int FAST_FUNC file_is_executable(const char *name)
{
	struct stat s;
	return (!access(name, X_OK) && !stat(name, &s) && S_ISREG(s.st_mode));
}

/* search (*PATHp) for an executable file;
 * return allocated string containing full path if found;
 *  PATHp points to the component after the one where it was found
 *  (or NULL),
 *  you may call find_executable again with this PATHp to continue
 *  (if it's not NULL).
 * return NULL otherwise; (PATHp is undefined)
 * in all cases (*PATHp) contents are temporarily modified
 * but are restored on return (s/:/NUL/ and back).
 */
char* FAST_FUNC find_executable(const char *filename, char **PATHp)
{
	/* About empty components in $PATH:
	 * http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html
	 * 8.3 Other Environment Variables - PATH
	 * A zero-length prefix is a legacy feature that indicates the current
	 * working directory. It appears as two adjacent colons ( "::" ), as an
	 * initial colon preceding the rest of the list, or as a trailing colon
	 * following the rest of the list.
	 */
	char *p, *n;

	p = *PATHp;
	while (p) {
		int ex;

		n = strchr(p, ':');
		if (n) *n = '\0';
		p = concat_path_file(
			p[0] ? p : ".", /* handle "::" case */
			filename
		);
		ex = file_is_executable(p);
		if (n) *n++ = ':';
		if (ex) {
			*PATHp = n;
			return p;
		}
		free(p);
		p = n;
	} /* on loop exit p == NULL */
	return p;
}

/* search $PATH for an executable file;
 * return 1 if found;
 * return 0 otherwise;
 */
int FAST_FUNC executable_exists(const char *filename)
{
	char *path = getenv("PATH");
	char *ret = find_executable(filename, &path);
	free(ret);
	return ret != NULL;
}

#if ENABLE_FEATURE_PREFER_APPLETS
/* just like the real execvp, but try to launch an applet named 'file' first */
int FAST_FUNC BB_EXECVP(const char *file, char *const argv[])
{
	if (find_applet_by_name(file) >= 0)
		execvp(bb_busybox_exec_path, argv);
	return execvp(file, argv);
}
#endif

void FAST_FUNC BB_EXECVP_or_die(char **argv)
{
	BB_EXECVP(argv[0], argv);
	/* SUSv3-mandated exit codes */
	xfunc_error_retval = (errno == ENOENT) ? 127 : 126;
	bb_perror_msg_and_die("can't execute '%s'", argv[0]);
}

/* Typical idiom for applets which exec *optional* PROG [ARGS] */
void FAST_FUNC exec_prog_or_SHELL(char **argv)
{
	if (argv[0]) {
		BB_EXECVP_or_die(argv);
	}
	run_shell(getenv("SHELL"), /*login:*/ 1, NULL);
}
