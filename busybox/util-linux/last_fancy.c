/* vi: set sw=4 ts=4: */
/*
 * (sysvinit like) last implementation
 *
 * Copyright (C) 2008 by Patricia Muscalu <patricia.muscalu@axis.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include "libbb.h"

/* NB: ut_name and ut_user are the same field, use only one name (ut_user)
 * to reduce confusion */

#ifndef SHUTDOWN_TIME
#  define SHUTDOWN_TIME 254
#endif

#define HEADER_FORMAT     "%-8.8s %-12.12s %-*.*s %-16.16s %-7.7s %s\n"
#define HEADER_LINE       "USER", "TTY", \
	INET_ADDRSTRLEN, INET_ADDRSTRLEN, "HOST", "LOGIN", "  TIME", ""
#define HEADER_LINE_WIDE  "USER", "TTY", \
	INET6_ADDRSTRLEN, INET6_ADDRSTRLEN, "HOST", "LOGIN", "  TIME", ""

#if !defined __UT_LINESIZE && defined UT_LINESIZE
# define __UT_LINESIZE UT_LINESIZE
#endif

enum {
	NORMAL,
	LOGGED,
	DOWN,
	REBOOT,
	CRASH,
	GONE
};

enum {
	LAST_OPT_W = (1 << 0),  /* -W wide            */
	LAST_OPT_f = (1 << 1),  /* -f input file      */
	LAST_OPT_H = (1 << 2),  /* -H header          */
};

#define show_wide (option_mask32 & LAST_OPT_W)

static void show_entry(struct utmpx *ut, int state, time_t dur_secs)
{
	unsigned days, hours, mins;
	char duration[sizeof("(%u+02:02)") + sizeof(int)*3];
	char login_time[17];
	char logout_time[8];
	const char *logout_str;
	const char *duration_str;
	time_t tmp;

	/* manpages say ut_tv.tv_sec *is* time_t,
	 * but some systems have it wrong */
	tmp = ut->ut_tv.tv_sec;
	safe_strncpy(login_time, ctime(&tmp), 17);
	tmp = dur_secs;
	snprintf(logout_time, 8, "- %s", ctime(&tmp) + 11);

	dur_secs = MAX(dur_secs - (time_t)ut->ut_tv.tv_sec, (time_t)0);
	/* unsigned int is easier to divide than time_t (which may be signed long) */
	mins = dur_secs / 60;
	days = mins / (24*60);
	mins = mins % (24*60);
	hours = mins / 60;
	mins = mins % 60;

//	if (days) {
		sprintf(duration, "(%u+%02u:%02u)", days, hours, mins);
//	} else {
//		sprintf(duration, " (%02u:%02u)", hours, mins);
//	}

	logout_str = logout_time;
	duration_str = duration;
	switch (state) {
	case NORMAL:
		break;
	case LOGGED:
		logout_str = "  still";
		duration_str = "logged in";
		break;
	case DOWN:
		logout_str = "- down ";
		break;
	case REBOOT:
		break;
	case CRASH:
		logout_str = "- crash";
		break;
	case GONE:
		logout_str = "   gone";
		duration_str = "- no logout";
		break;
	}

	printf(HEADER_FORMAT,
		ut->ut_user,
		ut->ut_line,
		show_wide ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN,
		show_wide ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN,
		ut->ut_host,
		login_time,
		logout_str,
		duration_str);
}

static int get_ut_type(struct utmpx *ut)
{
	if (ut->ut_line[0] == '~') {
		if (strcmp(ut->ut_user, "shutdown") == 0) {
			return SHUTDOWN_TIME;
		}
		if (strcmp(ut->ut_user, "reboot") == 0) {
			return BOOT_TIME;
		}
		if (strcmp(ut->ut_user, "runlevel") == 0) {
			return RUN_LVL;
		}
		return ut->ut_type;
	}

	if (ut->ut_user[0] == 0) {
		return DEAD_PROCESS;
	}

	if ((ut->ut_type != DEAD_PROCESS)
	 && (strcmp(ut->ut_user, "LOGIN") != 0)
	 && ut->ut_user[0]
	 && ut->ut_line[0]
	) {
		ut->ut_type = USER_PROCESS;
	}

	if (strcmp(ut->ut_user, "date") == 0) {
		if (ut->ut_line[0] == '|') {
			return OLD_TIME;
		}
		if (ut->ut_line[0] == '{') {
			return NEW_TIME;
		}
	}
	return ut->ut_type;
}

static int is_runlevel_shutdown(struct utmpx *ut)
{
	if (((ut->ut_pid & 255) == '0') || ((ut->ut_pid & 255) == '6')) {
		return 1;
	}

	return 0;
}

int last_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int last_main(int argc UNUSED_PARAM, char **argv)
{
	struct utmpx ut;
	const char *filename = _PATH_WTMP;
	llist_t *zlist;
	off_t pos;
	time_t start_time;
	time_t boot_time;
	time_t down_time;
	int file;
	smallint going_down;
	smallint boot_down;

	/*opt =*/ getopt32(argv, "Wf:" /* "H" */, &filename);
#ifdef BUT_UTIL_LINUX_LAST_HAS_NO_SUCH_OPT
	if (opt & LAST_OPT_H) {
		/* Print header line */
		if (opt & LAST_OPT_W) {
			printf(HEADER_FORMAT, HEADER_LINE_WIDE);
		} else {
			printf(HEADER_FORMAT, HEADER_LINE);
		}
	}
#endif

	file = xopen(filename, O_RDONLY);
	{
		/* in case the file is empty... */
		struct stat st;
		fstat(file, &st);
		start_time = st.st_ctime;
	}

	time(&down_time);
	going_down = 0;
	boot_down = NORMAL; /* 0 */
	zlist = NULL;
	boot_time = 0;
	/* get file size, rounding down to last full record */
	pos = xlseek(file, 0, SEEK_END) / sizeof(ut) * sizeof(ut);
	for (;;) {
		pos -= (off_t)sizeof(ut);
		if (pos < 0) {
			/* Beyond the beginning of the file boundary =>
			 * the whole file has been read. */
			break;
		}
		xlseek(file, pos, SEEK_SET);
		xread(file, &ut, sizeof(ut));
		/* rewritten by each record, eventially will have
		 * first record's ut_tv.tv_sec: */
		start_time = ut.ut_tv.tv_sec;

		switch (get_ut_type(&ut)) {
		case SHUTDOWN_TIME:
			down_time = ut.ut_tv.tv_sec;
			boot_down = DOWN;
			going_down = 1;
			break;
		case RUN_LVL:
			if (is_runlevel_shutdown(&ut)) {
				down_time = ut.ut_tv.tv_sec;
				going_down = 1;
				boot_down = DOWN;
			}
			break;
		case BOOT_TIME:
			strcpy(ut.ut_line, "system boot");
			show_entry(&ut, REBOOT, down_time);
			boot_down = CRASH;
			going_down = 1;
			break;
		case DEAD_PROCESS:
			if (!ut.ut_line[0]) {
				break;
			}
			/* add_entry */
			llist_add_to(&zlist, xmemdup(&ut, sizeof(ut)));
			break;
		case USER_PROCESS: {
			int show;

			if (!ut.ut_line[0]) {
				break;
			}
			/* find_entry */
			show = 1;
			{
				llist_t *el, *next;
				for (el = zlist; el; el = next) {
					struct utmpx *up = (struct utmpx *)el->data;
					next = el->link;
					if (strncmp(up->ut_line, ut.ut_line, __UT_LINESIZE) == 0) {
						if (show) {
							show_entry(&ut, NORMAL, up->ut_tv.tv_sec);
							show = 0;
						}
						llist_unlink(&zlist, el);
						free(el->data);
						free(el);
					}
				}
			}

			if (show) {
				int state = boot_down;

				if (boot_time == 0) {
					state = LOGGED;
					/* Check if the process is alive */
					if ((ut.ut_pid > 0)
					 && (kill(ut.ut_pid, 0) != 0)
					 && (errno == ESRCH)) {
						state = GONE;
					}
				}
				show_entry(&ut, state, boot_time);
			}
			/* add_entry */
			llist_add_to(&zlist, xmemdup(&ut, sizeof(ut)));
			break;
		}
		}

		if (going_down) {
			boot_time = ut.ut_tv.tv_sec;
			llist_free(zlist, free);
			zlist = NULL;
			going_down = 0;
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		llist_free(zlist, free);
	}

	printf("\nwtmp begins %s", ctime(&start_time));

	if (ENABLE_FEATURE_CLEAN_UP)
		close(file);
	fflush_stdout_and_exit(EXIT_SUCCESS);
}
