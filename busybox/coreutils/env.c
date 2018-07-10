/* vi: set sw=4 ts=4: */
/*
 * env implementation for busybox
 *
 * Copyright (c) 1988, 1993, 1994
 * The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Original copyright notice is retained at the end of this file.
 *
 * Modified for BusyBox by Erik Andersen <andersen@codepoet.org>
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Fixed bug involving exit return codes if execvp fails.  Also added
 * output error checking.
 */
/*
 * Modified by Vladimir Oleynik <dzo@simtreas.ru> (C) 2003
 * - correct "-" option usage
 * - multiple "-u unsetenv" support
 * - GNU long option support
 * - use xfunc_error_retval
 */
//config:config ENV
//config:	bool "env (3.8 kb)"
//config:	default y
//config:	help
//config:	env is used to set an environment variable and run
//config:	a command; without options it displays the current
//config:	environment.

//applet:IF_ENV(APPLET_NOEXEC(env, env, BB_DIR_USR_BIN, BB_SUID_DROP, env))

//kbuild:lib-$(CONFIG_ENV) += env.o

/* BB_AUDIT SUSv3 compliant */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/env.html */

//usage:#define env_trivial_usage
//usage:       "[-iu] [-] [name=value]... [PROG ARGS]"
//usage:#define env_full_usage "\n\n"
//usage:       "Print the current environment or run PROG after setting up\n"
//usage:       "the specified environment\n"
//usage:     "\n	-, -i	Start with an empty environment"
//usage:     "\n	-u	Remove variable from the environment"

#include "libbb.h"

int env_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int env_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	llist_t *unset_env = NULL;

	opts = getopt32long(argv, "+iu:*",
			"ignore-environment\0" No_argument       "i"
			"unset\0"              Required_argument "u"
			, &unset_env
	);
	argv += optind;
	if (argv[0] && LONE_DASH(argv[0])) {
		opts |= 1;
		++argv;
	}
	if (opts & 1) {
		clearenv();
	}
	while (unset_env) {
		char *var = llist_pop(&unset_env);
		/* This does not handle -uVAR=VAL
		 * (coreutils _sets_ the variable in that case): */
		/*unsetenv(var);*/
		/* This does, but uses somewhan undocumented feature that
		 * putenv("name_without_equal_sign") unsets the variable: */
		putenv(var);
	}

	while (*argv && (strchr(*argv, '=') != NULL)) {
		if (putenv(*argv) < 0) {
			bb_perror_msg_and_die("putenv");
		}
		++argv;
	}

	if (argv[0]) {
		BB_EXECVP_or_die(argv);
	}

	if (environ) { /* clearenv() may set environ == NULL! */
		char **ep;
		for (ep = environ; *ep; ep++) {
			puts(*ep);
		}
	}

	fflush_stdout_and_exit(EXIT_SUCCESS);
}

/*
 * Copyright (c) 1988, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
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
 * 3. BSD Advertising Clause omitted per the July 22, 1999 licensing change
 *    ftp://ftp.cs.berkeley.edu/pub/4bsd/README.Impt.License.Change
 *
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
