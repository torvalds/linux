/* vi: set sw=4 ts=4: */
/*
 * printenv implementation for busybox
 *
 * Copyright (C) 2005 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Mike Frysinger <vapier@gentoo.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config PRINTENV
//config:	bool "printenv (1 kb)"
//config:	default y
//config:	help
//config:	printenv is used to print all or part of environment.

//applet:IF_PRINTENV(APPLET_NOFORK(printenv, printenv, BB_DIR_BIN, BB_SUID_DROP, printenv))

//kbuild:lib-$(CONFIG_PRINTENV) += printenv.o

//usage:#define printenv_trivial_usage
//usage:       "[VARIABLE]..."
//usage:#define printenv_full_usage "\n\n"
//usage:       "Print environment VARIABLEs.\n"
//usage:       "If no VARIABLE specified, print all."

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int printenv_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int printenv_main(int argc UNUSED_PARAM, char **argv)
{
	int exit_code = EXIT_SUCCESS;

	/* no variables specified, show whole env */
	if (!argv[1]) {
		char **e = environ;

		/* environ can be NULL! (for example, after clearenv())
		 * Check for that:
		 */
		if (e)
			while (*e)
				puts(*e++);
	} else {
		/* search for specified variables and print them out if found */
		char *arg, *env;

		while ((arg = *++argv) != NULL) {
			env = getenv(arg);
			if (env)
				puts(env);
			else
				exit_code = EXIT_FAILURE;
		}
	}

	fflush_stdout_and_exit(exit_code);
}
