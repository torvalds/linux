/* vi: set sw=4 ts=4: */
/*
 * setconsole.c - redirect system console output
 *
 * Copyright (C) 2004,2005  Enrik Berkhan <Enrik.Berkhan@inka.de>
 * Copyright (C) 2008 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SETCONSOLE
//config:	bool "setconsole (3.7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Redirect writes to /dev/console to another device,
//config:	like the current tty while logged in via telnet.
//config:	This does not redirect kernel log, only writes
//config:	from user space.
//config:
//config:config FEATURE_SETCONSOLE_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on SETCONSOLE && LONG_OPTS

//applet:IF_SETCONSOLE(APPLET_NOEXEC(setconsole, setconsole, BB_DIR_SBIN, BB_SUID_DROP, setconsole))

//kbuild:lib-$(CONFIG_SETCONSOLE) += setconsole.o

//usage:#define setconsole_trivial_usage
//usage:       "[-r] [DEVICE]"
//usage:#define setconsole_full_usage "\n\n"
//usage:       "Make writes to /dev/console appear on DEVICE (default: /dev/tty)."
//usage:   "\n""Does not redirect kernel log output or reads from /dev/console."
//usage:   "\n"
//usage:   "\n""	-r	Reset: writes to /dev/console go to kernel log tty(s)"

/* It was a bbox-specific invention, but SUSE does have a similar utility.
 * SUSE has no -r option, though.
 */

#include "libbb.h"

int setconsole_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setconsole_main(int argc UNUSED_PARAM, char **argv)
{
	const char *device = CURRENT_TTY;
	int reset;

	/* at most one non-option argument */
	reset = getopt32(argv, "^" "r" "\0" "?1");

	argv += 1 + reset;
	if (*argv) {
		device = *argv;
	} else {
		if (reset)
			device = DEV_CONSOLE;
	}

//TODO: fails if TIOCCONS redir is already active to some tty.
//I think SUSE version first does TIOCCONS on /dev/console fd (iow: resets)
//then TIOCCONS to new tty?
	xioctl(xopen(device, O_WRONLY), TIOCCONS, NULL);
	return EXIT_SUCCESS;
}
