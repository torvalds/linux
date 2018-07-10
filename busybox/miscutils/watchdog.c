/* vi: set sw=4 ts=4: */
/*
 * Mini watchdog implementation for busybox
 *
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 * Copyright (C) 2006  Bernhard Reutner-Fischer <busybox@busybox.net>
 * Copyright (C) 2008  Darius Augulis <augulis.darius@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config WATCHDOG
//config:	bool "watchdog (5.1 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The watchdog utility is used with hardware or software watchdog
//config:	device drivers. It opens the specified watchdog device special file
//config:	and periodically writes a magic character to the device. If the
//config:	watchdog applet ever fails to write the magic character within a
//config:	certain amount of time, the watchdog device assumes the system has
//config:	hung, and will cause the hardware to reboot.

//applet:IF_WATCHDOG(APPLET(watchdog, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_WATCHDOG) += watchdog.o

//usage:#define watchdog_trivial_usage
//usage:       "[-t N[ms]] [-T N[ms]] [-F] DEV"
//usage:#define watchdog_full_usage "\n\n"
//usage:       "Periodically write to watchdog device DEV\n"
//usage:     "\n	-T N	Reboot after N seconds if not reset (default 60)"
//usage:     "\n	-t N	Reset every N seconds (default 30)"
//usage:     "\n	-F	Run in foreground"
//usage:     "\n"
//usage:     "\nUse 500ms to specify period in milliseconds"

#include "libbb.h"
#include <linux/types.h> /* for __u32 */
#include <linux/watchdog.h>

#ifndef WDIOC_SETOPTIONS
# define WDIOC_SETOPTIONS 0x5704
#endif
#ifndef WDIOC_SETTIMEOUT
# define WDIOC_SETTIMEOUT 0x5706
#endif
#ifndef WDIOC_GETTIMEOUT
# define WDIOC_GETTIMEOUT 0x5707
#endif
#ifndef WDIOS_ENABLECARD
# define WDIOS_ENABLECARD 2
#endif

#define OPT_FOREGROUND  (1 << 0)
#define OPT_STIMER      (1 << 1)
#define OPT_HTIMER      (1 << 2)

static void shutdown_watchdog(void)
{
	static const char V = 'V';
	write(3, &V, 1);  /* Magic, see watchdog-api.txt in kernel */
	close(3);
}

static void shutdown_on_signal(int sig UNUSED_PARAM)
{
	remove_pidfile(CONFIG_PID_FILE_PATH "/watchdog.pid");
	shutdown_watchdog();
	_exit(EXIT_SUCCESS);
}

static void watchdog_open(const char* device)
{
	/* Use known fd # - avoid needing global 'int fd' */
	xmove_fd(xopen(device, O_WRONLY), 3);

	/* If the watchdog driver can do something other than cause a reboot
	 * on a timeout, then it's possible this program may be starting from
	 * a state when the watchdog hadn't been previously stopped with
	 * the magic write followed by a close.  In this case the driver may
	 * not start properly, so always do the proper stop first just in case.
	 */
	shutdown_watchdog();

	xmove_fd(xopen(device, O_WRONLY), 3);
}

int watchdog_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int watchdog_main(int argc UNUSED_PARAM, char **argv)
{
	static const int enable = WDIOS_ENABLECARD;
	static const struct suffix_mult suffixes[] = {
		{ "ms", 1 },
		{ "", 1000 },
		{ "", 0 }
	};

	unsigned opts;
	unsigned stimer_duration; /* how often to restart */
	unsigned htimer_duration = 60000; /* reboots after N ms if not restarted */
	char *st_arg;
	char *ht_arg;

	opts = getopt32(argv, "^" "Ft:T:" "\0" "=1"/*must have exactly 1 arg*/,
				&st_arg, &ht_arg
	);

	/* We need to daemonize *before* opening the watchdog as many drivers
	 * will only allow one process at a time to do so.  Since daemonizing
	 * is not perfect (child may run before parent finishes exiting), we
	 * can't rely on parent exiting before us (let alone *cleanly* releasing
	 * the watchdog fd -- something else that may not even be allowed).
	 */
	if (!(opts & OPT_FOREGROUND))
		bb_daemonize_or_rexec(DAEMON_CHDIR_ROOT, argv);

	/* maybe bb_logenv_override(); here for LOGGING=syslog to work? */

	if (opts & OPT_HTIMER)
		htimer_duration = xatou_sfx(ht_arg, suffixes);
	stimer_duration = htimer_duration / 2;
	if (opts & OPT_STIMER)
		stimer_duration = xatou_sfx(st_arg, suffixes);

	bb_signals(BB_FATAL_SIGS, shutdown_on_signal);

	watchdog_open(argv[optind]);

	/* WDIOC_SETTIMEOUT takes seconds, not milliseconds */
	htimer_duration = htimer_duration / 1000;
	ioctl_or_warn(3, WDIOC_SETOPTIONS, (void*) &enable);
	ioctl_or_warn(3, WDIOC_SETTIMEOUT, &htimer_duration);
#if 0
	ioctl_or_warn(3, WDIOC_GETTIMEOUT, &htimer_duration);
	printf("watchdog: SW timer is %dms, HW timer is %ds\n",
		stimer_duration, htimer_duration * 1000);
#endif

	write_pidfile(CONFIG_PID_FILE_PATH "/watchdog.pid");

	while (1) {
		/*
		 * Make sure we clear the counter before sleeping,
		 * as the counter value is undefined at this point -- PFM
		 */
		write(3, "", 1); /* write zero byte */
		usleep(stimer_duration * 1000L);
	}
	return EXIT_SUCCESS; /* - not reached, but gcc 4.2.1 is too dumb! */
}
