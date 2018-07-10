/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/*
In Linux we have three ways to determine "process name":
1. /proc/PID/stat has "...(name)...", among other things. It's so-called "comm" field.
2. /proc/PID/cmdline's first NUL-terminated string. It's argv[0] from exec syscall.
3. /proc/PID/exe symlink. Points to the running executable file.

kernel threads:
 comm: thread name
 cmdline: empty
 exe: <readlink fails>

executable
 comm: first 15 chars of base name
 (if executable is a symlink, then first 15 chars of symlink name are used)
 cmdline: argv[0] from exec syscall
 exe: points to executable (resolves symlink, unlike comm)

script (an executable with #!/path/to/interpreter):
 comm: first 15 chars of script's base name (symlinks are not resolved)
 cmdline: /path/to/interpreter (symlinks are not resolved)
 (script name is in argv[1], args are pushed into argv[2] etc)
 exe: points to interpreter's executable (symlinks are resolved)

If FEATURE_PREFER_APPLETS=y (and more so if FEATURE_SH_STANDALONE=y),
some commands started from busybox shell, xargs or find are started by
execXXX("/proc/self/exe", applet_name, params....)
and therefore comm field contains "exe".
*/

static int comm_match(procps_status_t *p, const char *procName)
{
	int argv1idx;
	const char *argv1;

	if (strncmp(p->comm, procName, 15) != 0)
		return 0; /* comm does not match */

	/* In Linux, if comm is 15 chars, it is truncated.
	 * (or maybe the name was exactly 15 chars, but there is
	 * no way to know that) */
	if (p->comm[14] == '\0')
		return 1; /* comm is not truncated - matches */

	/* comm is truncated, but first 15 chars match.
	 * This can be crazily_long_script_name.sh!
	 * The telltale sign is basename(argv[1]) == procName */

	if (!p->argv0)
		return 0;

	argv1idx = strlen(p->argv0) + 1;
	if (argv1idx >= p->argv_len)
		return 0;
	argv1 = p->argv0 + argv1idx;

	if (strcmp(bb_basename(argv1), procName) != 0)
		return 0;

	return 1;
}

/* This finds the pid of the specified process.
 * Currently, it's implemented by rummaging through
 * the proc filesystem.
 *
 * Returns a list of all matching PIDs
 * It is the caller's duty to free the returned pidlist.
 *
 * Modified by Vladimir Oleynik for use with libbb/procps.c
 */
pid_t* FAST_FUNC find_pid_by_name(const char *procName)
{
	pid_t* pidList;
	int i = 0;
	procps_status_t* p = NULL;

	pidList = xzalloc(sizeof(*pidList));
	while ((p = procps_scan(p, PSSCAN_PID|PSSCAN_COMM|PSSCAN_ARGVN|PSSCAN_EXE))) {
		if (comm_match(p, procName)
		/* or we require argv0 to match (essential for matching reexeced /proc/self/exe)*/
		 || (p->argv0 && strcmp(bb_basename(p->argv0), procName) == 0)
		/* or we require /proc/PID/exe link to match */
		 || (p->exe && strcmp(bb_basename(p->exe), procName) == 0)
		) {
			pidList = xrealloc_vector(pidList, 2, i);
			pidList[i++] = p->pid;
		}
	}

	pidList[i] = 0;
	return pidList;
}

pid_t* FAST_FUNC pidlist_reverse(pid_t *pidList)
{
	int i = 0;
	while (pidList[i])
		i++;
	if (--i >= 0) {
		pid_t k;
		int j;
		for (j = 0; i > j; i--, j++) {
			k = pidList[i];
			pidList[i] = pidList[j];
			pidList[j] = k;
		}
	}
	return pidList;
}
