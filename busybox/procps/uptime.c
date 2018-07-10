/* vi: set sw=4 ts=4: */
/*
 * Mini uptime implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* 2011		Pere Orga <gotrunks@gmail.com>
 *
 * Added FEATURE_UPTIME_UTMP_SUPPORT flag.
 */
//config:config UPTIME
//config:	bool "uptime (632 bytes)"
//config:	default y
//config:	select PLATFORM_LINUX #sysinfo()
//config:	help
//config:	uptime gives a one line display of the current time, how long
//config:	the system has been running, how many users are currently logged
//config:	on, and the system load averages for the past 1, 5, and 15 minutes.
//config:
//config:config FEATURE_UPTIME_UTMP_SUPPORT
//config:	bool "Show the number of users"
//config:	default y
//config:	depends on UPTIME && FEATURE_UTMP
//config:	help
//config:	Display the number of users currently logged on.

//applet:IF_UPTIME(APPLET_NOEXEC(uptime, uptime, BB_DIR_USR_BIN, BB_SUID_DROP, uptime))

//kbuild:lib-$(CONFIG_UPTIME) += uptime.o

//usage:#define uptime_trivial_usage
//usage:       ""
//usage:#define uptime_full_usage "\n\n"
//usage:       "Display the time since the last boot"
//usage:
//usage:#define uptime_example_usage
//usage:       "$ uptime\n"
//usage:       "  1:55pm  up  2:30, load average: 0.09, 0.04, 0.00\n"

#include "libbb.h"
#ifdef __linux__
# include <sys/sysinfo.h>
#endif


#ifndef FSHIFT
# define FSHIFT 16              /* nr of bits of precision */
#endif
#define FIXED_1      (1 << FSHIFT)     /* 1.0 as fixed-point */
#define LOAD_INT(x)  (unsigned)((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1 - 1)) * 100)


int uptime_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int uptime_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	unsigned updays, uphours, upminutes;
	struct sysinfo info;
	struct tm *current_time;
	time_t current_secs;

	time(&current_secs);
	current_time = localtime(&current_secs);

	sysinfo(&info);

	printf(" %02u:%02u:%02u up ",
			current_time->tm_hour, current_time->tm_min, current_time->tm_sec);
	updays = (unsigned) info.uptime / (unsigned)(60*60*24);
	if (updays)
		printf("%u day%s, ", updays, (updays != 1) ? "s" : "");
	upminutes = (unsigned) info.uptime / (unsigned)60;
	uphours = (upminutes / (unsigned)60) % (unsigned)24;
	upminutes %= 60;
	if (uphours)
		printf("%2u:%02u", uphours, upminutes);
	else
		printf("%u min", upminutes);

#if ENABLE_FEATURE_UPTIME_UTMP_SUPPORT
	{
		struct utmpx *ut;
		unsigned users = 0;
		while ((ut = getutxent()) != NULL) {
			if ((ut->ut_type == USER_PROCESS) && (ut->ut_user[0] != '\0'))
				users++;
		}
		printf(",  %u users", users);
	}
#endif

	printf(",  load average: %u.%02u, %u.%02u, %u.%02u\n",
			LOAD_INT(info.loads[0]), LOAD_FRAC(info.loads[0]),
			LOAD_INT(info.loads[1]), LOAD_FRAC(info.loads[1]),
			LOAD_INT(info.loads[2]), LOAD_FRAC(info.loads[2]));

	return EXIT_SUCCESS;
}
