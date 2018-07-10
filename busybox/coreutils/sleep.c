/* vi: set sw=4 ts=4: */
/*
 * sleep implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Rewritten to do proper arg and error checking.
 * Also, added a 'fancy' configuration to accept multiple args with
 * time suffixes for seconds, minutes, hours, and days.
 */
//config:config SLEEP
//config:	bool "sleep (1.7 kb)"
//config:	default y
//config:	help
//config:	sleep is used to pause for a specified number of seconds.
//config:	It comes in 3 versions:
//config:	- small: takes one integer parameter
//config:	- fancy: takes multiple integer arguments with suffixes:
//config:		sleep 1d 2h 3m 15s
//config:	- fancy with fractional numbers:
//config:		sleep 2.3s 4.5h sleeps for 16202.3 seconds
//config:	Last one is "the most compatible" with coreutils sleep,
//config:	but it adds around 1k of code.
//config:
//config:config FEATURE_FANCY_SLEEP
//config:	bool "Enable multiple arguments and s/m/h/d suffixes"
//config:	default y
//config:	depends on SLEEP
//config:	help
//config:	Allow sleep to pause for specified minutes, hours, and days.
//config:
//config:config FEATURE_FLOAT_SLEEP
//config:	bool "Enable fractional arguments"
//config:	default y
//config:	depends on FEATURE_FANCY_SLEEP
//config:	help
//config:	Allow for fractional numeric parameters.

/* Do not make this applet NOFORK. It breaks ^C-ing of pauses in shells */
//applet:IF_SLEEP(APPLET(sleep, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SLEEP) += sleep.o

/* BB_AUDIT SUSv3 compliant */
/* BB_AUDIT GNU issues -- fancy version matches except args must be ints. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/sleep.html */

//usage:#define sleep_trivial_usage
//usage:	IF_FEATURE_FANCY_SLEEP("[") "N" IF_FEATURE_FANCY_SLEEP("]...")
//usage:#define sleep_full_usage "\n\n"
//usage:	IF_NOT_FEATURE_FANCY_SLEEP("Pause for N seconds")
//usage:	IF_FEATURE_FANCY_SLEEP(
//usage:       "Pause for a time equal to the total of the args given, where each arg can\n"
//usage:       "have an optional suffix of (s)econds, (m)inutes, (h)ours, or (d)ays")
//usage:
//usage:#define sleep_example_usage
//usage:       "$ sleep 2\n"
//usage:       "[2 second delay results]\n"
//usage:	IF_FEATURE_FANCY_SLEEP(
//usage:       "$ sleep 1d 3h 22m 8s\n"
//usage:       "[98528 second delay results]\n")

#include "libbb.h"

#if ENABLE_FEATURE_FANCY_SLEEP || ENABLE_FEATURE_FLOAT_SLEEP
static const struct suffix_mult sfx[] = {
	{ "s", 1 },
	{ "m", 60 },
	{ "h", 60*60 },
	{ "d", 24*60*60 },
	{ "", 0 }
};
#endif

int sleep_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sleep_main(int argc UNUSED_PARAM, char **argv)
{
#if ENABLE_FEATURE_FLOAT_SLEEP
	double duration;
	struct timespec ts;
#else
	unsigned duration;
#endif

	++argv;
	if (!*argv)
		bb_show_usage();

#if ENABLE_FEATURE_FLOAT_SLEEP

# if ENABLE_LOCALE_SUPPORT
	/* undo busybox.c setlocale */
	setlocale(LC_NUMERIC, "C");
# endif
	duration = 0;
	do {
		char *arg = *argv;
		if (strchr(arg, '.')) {
			double d;
			char *pp;
			int len = strspn(arg, "0123456789.");
			char sv = arg[len];
			arg[len] = '\0';
			errno = 0;
			d = strtod(arg, &pp);
			if (errno || *pp)
				bb_show_usage();
			arg += len;
			*arg-- = sv;
			sv = *arg;
			*arg = '1';
			duration += d * xatoul_sfx(arg, sfx);
			*arg = sv;
		} else {
			duration += xatoul_sfx(arg, sfx);
		}
	} while (*++argv);

	ts.tv_sec = MAXINT(typeof(ts.tv_sec));
	ts.tv_nsec = 0;
	if (duration >= 0 && duration < ts.tv_sec) {
		ts.tv_sec = duration;
		ts.tv_nsec = (duration - ts.tv_sec) * 1000000000;
	}
	do {
		errno = 0;
		nanosleep(&ts, &ts);
	} while (errno == EINTR);

#elif ENABLE_FEATURE_FANCY_SLEEP

	duration = 0;
	do {
		duration += xatou_range_sfx(*argv, 0, UINT_MAX - duration, sfx);
	} while (*++argv);
	sleep(duration);

#else /* simple */

	duration = xatou(*argv);
	sleep(duration);
	// Off. If it's really needed, provide example why
	//if (sleep(duration)) {
	//	bb_perror_nomsg_and_die();
	//}

#endif

	return EXIT_SUCCESS;
}
