/* vi: set sw=4 ts=4: */
/*
 * last implementation for busybox
 *
 * Copyright (C) 2003-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config LAST
//config:	bool "last (6.2 kb)"
//config:	default y
//config:	depends on FEATURE_WTMP
//config:	help
//config:	'last' displays a list of the last users that logged into the system.
//config:
//config:config FEATURE_LAST_FANCY
//config:	bool "Output extra information"
//config:	default y
//config:	depends on LAST
//config:	help
//config:	'last' displays detailed information about the last users that
//config:	logged into the system (mimics sysvinit last). +900 bytes.

//applet:IF_LAST(APPLET(last, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:ifeq ($(CONFIG_FEATURE_LAST_FANCY),y)
//kbuild:lib-$(CONFIG_FEATURE_LAST_FANCY) += last_fancy.o
//kbuild:else
//kbuild:lib-$(CONFIG_LAST) += last.o
//kbuild:endif

//usage:#define last_trivial_usage
//usage:       ""IF_FEATURE_LAST_FANCY("[-HW] [-f FILE]")
//usage:#define last_full_usage "\n\n"
//usage:       "Show listing of the last users that logged into the system"
//usage:	IF_FEATURE_LAST_FANCY( "\n"
/* //usage:  "\n	-H	Show header line" */
//usage:     "\n	-W	Display with no host column truncation"
//usage:     "\n	-f FILE Read from FILE instead of /var/log/wtmp"
//usage:	)

#include "libbb.h"

/* NB: ut_name and ut_user are the same field, use only one name (ut_user)
 * to reduce confusion */

#ifndef SHUTDOWN_TIME
#  define SHUTDOWN_TIME 254
#endif

/* Grr... utmp char[] members do not have to be nul-terminated.
 * Do what we can while still keeping this reasonably small.
 * Note: We are assuming the ut_id[] size is fixed at 4. */

#if defined UT_LINESIZE \
	&& ((UT_LINESIZE != 32) || (UT_NAMESIZE != 32) || (UT_HOSTSIZE != 256))
#error struct utmpx member char[] size(s) have changed!
#elif defined __UT_LINESIZE \
	&& ((__UT_LINESIZE != 32) || (__UT_NAMESIZE != 32) || (__UT_HOSTSIZE != 256))
/* __UT_NAMESIZE was checked with 64 above, but glibc-2.11 definitely uses 32! */
#error struct utmpx member char[] size(s) have changed!
#endif

#if EMPTY != 0 || RUN_LVL != 1 || BOOT_TIME != 2 || NEW_TIME != 3 || \
	OLD_TIME != 4
#error Values for the ut_type field of struct utmpx changed
#endif

int last_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int last_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	struct utmpx ut;
	int n, file = STDIN_FILENO;
	time_t t_tmp;
	off_t pos;
	static const char _ut_usr[] ALIGN1 =
			"runlevel\0" "reboot\0" "shutdown\0";
	static const char _ut_lin[] ALIGN1 =
			"~\0" "{\0" "|\0" /* "LOGIN\0" "date\0" */;
	enum {
		TYPE_RUN_LVL = RUN_LVL,         /* 1 */
		TYPE_BOOT_TIME = BOOT_TIME,     /* 2 */
		TYPE_SHUTDOWN_TIME = SHUTDOWN_TIME
	};
	enum {
		_TILDE = EMPTY, /* 0 */
		TYPE_NEW_TIME,  /* NEW_TIME, 3 */
		TYPE_OLD_TIME   /* OLD_TIME, 4 */
	};

	if (argv[1]) {
		bb_show_usage();
	}
	file = xopen(bb_path_wtmp_file, O_RDONLY);

	printf("%-10s %-14s %-18s %-12.12s %s\n",
		"USER", "TTY", "HOST", "LOGIN", "TIME");
	/* yikes. We reverse over the file and that is a not too elegant way */
	pos = xlseek(file, 0, SEEK_END);
	pos = lseek(file, pos - sizeof(ut), SEEK_SET);
	while ((n = full_read(file, &ut, sizeof(ut))) > 0) {
		if (n != sizeof(ut)) {
			bb_perror_msg_and_die("short read");
		}
		n = index_in_strings(_ut_lin, ut.ut_line);
		if (n == _TILDE) { /* '~' */
#if 1
/* do we really need to be cautious here? */
			n = index_in_strings(_ut_usr, ut.ut_user);
			if (++n > 0)
				ut.ut_type = n != 3 ? n : SHUTDOWN_TIME;
#else
			if (is_prefixed_with(ut.ut_user, "shutdown"))
				ut.ut_type = SHUTDOWN_TIME;
			else if (is_prefixed_with(ut.ut_user, "reboot"))
				ut.ut_type = BOOT_TIME;
			else if (is_prefixed_with(ut.ut_user, "runlevel"))
				ut.ut_type = RUN_LVL;
#endif
		} else {
			if (ut.ut_user[0] == '\0' || strcmp(ut.ut_user, "LOGIN") == 0) {
				/* Don't bother.  This means we can't find how long
				 * someone was logged in for.  Oh well. */
				goto next;
			}
			if (ut.ut_type != DEAD_PROCESS
			 && ut.ut_user[0]
			 && ut.ut_line[0]
			) {
				ut.ut_type = USER_PROCESS;
			}
			if (strcmp(ut.ut_user, "date") == 0) {
				if (n == TYPE_OLD_TIME) { /* '|' */
					ut.ut_type = OLD_TIME;
				}
				if (n == TYPE_NEW_TIME) { /* '{' */
					ut.ut_type = NEW_TIME;
				}
			}
		}

		if (ut.ut_type != USER_PROCESS) {
			switch (ut.ut_type) {
				case OLD_TIME:
				case NEW_TIME:
				case RUN_LVL:
				case SHUTDOWN_TIME:
					goto next;
				case BOOT_TIME:
					strcpy(ut.ut_line, "system boot");
			}
		}
		/* manpages say ut_tv.tv_sec *is* time_t,
		 * but some systems have it wrong */
		t_tmp = (time_t)ut.ut_tv.tv_sec;
		printf("%-10s %-14s %-18s %-12.12s\n",
			ut.ut_user, ut.ut_line, ut.ut_host, ctime(&t_tmp) + 4);
 next:
		pos -= sizeof(ut);
		if (pos <= 0)
			break; /* done. */
		xlseek(file, pos, SEEK_SET);
	}

	fflush_stdout_and_exit(EXIT_SUCCESS);
}
