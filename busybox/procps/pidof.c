/* vi: set sw=4 ts=4: */
/*
 * pidof implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config PIDOF
//config:	bool "pidof (6.6 kb)"
//config:	default y
//config:	help
//config:	Pidof finds the process id's (pids) of the named programs. It prints
//config:	those id's on the standard output.
//config:
//config:config FEATURE_PIDOF_SINGLE
//config:	bool "Enable single shot (-s)"
//config:	default y
//config:	depends on PIDOF
//config:	help
//config:	Support '-s' for returning only the first pid found.
//config:
//config:config FEATURE_PIDOF_OMIT
//config:	bool "Enable omitting pids (-o PID)"
//config:	default y
//config:	depends on PIDOF
//config:	help
//config:	Support '-o PID' for omitting the given pid(s) in output.
//config:	The special pid %PPID can be used to name the parent process
//config:	of the pidof, in other words the calling shell or shell script.

//applet:IF_PIDOF(APPLET(pidof, BB_DIR_BIN, BB_SUID_DROP))
/* can't be noexec: can find _itself_ under wrong name, since after fork only,
 * /proc/PID/cmdline and comm are wrong! Can fix comm (prctl(PR_SET_NAME)),
 * but cmdline?
 */

//kbuild:lib-$(CONFIG_PIDOF) += pidof.o

//usage:#if (ENABLE_FEATURE_PIDOF_SINGLE || ENABLE_FEATURE_PIDOF_OMIT)
//usage:#define pidof_trivial_usage
//usage:       "[OPTIONS] [NAME]..."
//usage:#define USAGE_PIDOF "\n"
//usage:#else
//usage:#define pidof_trivial_usage
//usage:       "[NAME]..."
//usage:#define USAGE_PIDOF /* none */
//usage:#endif
//usage:#define pidof_full_usage "\n\n"
//usage:       "List PIDs of all processes with names that match NAMEs"
//usage:	USAGE_PIDOF
//usage:	IF_FEATURE_PIDOF_SINGLE(
//usage:     "\n	-s	Show only one PID"
//usage:	)
//usage:	IF_FEATURE_PIDOF_OMIT(
//usage:     "\n	-o PID	Omit given pid"
//usage:     "\n		Use %PPID to omit pid of pidof's parent"
//usage:	)
//usage:
//usage:#define pidof_example_usage
//usage:       "$ pidof init\n"
//usage:       "1\n"
//usage:	IF_FEATURE_PIDOF_OMIT(
//usage:       "$ pidof /bin/sh\n20351 5973 5950\n")
//usage:	IF_FEATURE_PIDOF_OMIT(
//usage:       "$ pidof /bin/sh -o %PPID\n20351 5950")

#include "libbb.h"

enum {
	IF_FEATURE_PIDOF_SINGLE(OPTBIT_SINGLE,)
	IF_FEATURE_PIDOF_OMIT(  OPTBIT_OMIT  ,)
	OPT_SINGLE = IF_FEATURE_PIDOF_SINGLE((1<<OPTBIT_SINGLE)) + 0,
	OPT_OMIT   = IF_FEATURE_PIDOF_OMIT(  (1<<OPTBIT_OMIT  )) + 0,
};

int pidof_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pidof_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned first = 1;
	unsigned opt;
#if ENABLE_FEATURE_PIDOF_OMIT
	llist_t *omits = NULL; /* list of pids to omit */
#endif

	/* do unconditional option parsing */
	opt = getopt32(argv, ""
			IF_FEATURE_PIDOF_SINGLE ("s")
			IF_FEATURE_PIDOF_OMIT("o:*", &omits));

#if ENABLE_FEATURE_PIDOF_OMIT
	/* fill omit list.  */
	{
		llist_t *omits_p = omits;
		while (1) {
			omits_p = llist_find_str(omits_p, "%PPID");
			if (!omits_p)
				break;
			/* are we asked to exclude the parent's process ID?  */
			omits_p->data = utoa((unsigned)getppid());
		}
	}
#endif
	/* Looks like everything is set to go.  */
	argv += optind;
	while (*argv) {
		pid_t *pidList;
		pid_t *pl;

		/* reverse the pidlist like GNU pidof does.  */
		pidList = pidlist_reverse(find_pid_by_name(*argv));
		for (pl = pidList; *pl; pl++) {
#if ENABLE_FEATURE_PIDOF_OMIT
			if (opt & OPT_OMIT) {
				llist_t *omits_p = omits;
				while (omits_p) {
					if (xatoul(omits_p->data) == (unsigned long)(*pl)) {
						goto omitting;
					}
					omits_p = omits_p->link;
				}
			}
#endif
			printf(" %u" + first, (unsigned)*pl);
			first = 0;
			if (ENABLE_FEATURE_PIDOF_SINGLE && (opt & OPT_SINGLE))
				break;
#if ENABLE_FEATURE_PIDOF_OMIT
 omitting: ;
#endif
		}
		free(pidList);
		argv++;
	}
	if (!first)
		bb_putchar('\n');

#if ENABLE_FEATURE_PIDOF_OMIT
	if (ENABLE_FEATURE_CLEAN_UP)
		llist_free(omits, NULL);
#endif
	return first; /* 1 (failure) - no processes found */
}
