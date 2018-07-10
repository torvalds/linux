/* vi: set sw=4 ts=4: */
/*
 * Replacement for "stty size", which is awkward for shell script use.
 * - Allows to request width, height, or both, in any order.
 * - Does not complain on error, but returns width 80, height 24.
 * - Size: less than 200 bytes
 *
 * Copyright (C) 2007 by Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config TTYSIZE
//config:	bool "ttysize (372 bytes)"
//config:	default y
//config:	help
//config:	A replacement for "stty size". Unlike stty, can report only width,
//config:	only height, or both, in any order. It also does not complain on
//config:	error, but returns default 80x24.
//config:	Usage in shell scripts: width=`ttysize w`.

//applet:IF_TTYSIZE(APPLET_NOFORK(ttysize, ttysize, BB_DIR_USR_BIN, BB_SUID_DROP, ttysize))

//kbuild:lib-$(CONFIG_TTYSIZE) += ttysize.o

//usage:#define ttysize_trivial_usage
//usage:       "[w] [h]"
//usage:#define ttysize_full_usage "\n\n"
//usage:       "Print dimensions of stdin tty, or 80x24"

#include "libbb.h"

int ttysize_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ttysize_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned w, h;
	struct winsize wsz;

	w = 80;
	h = 24;
	if (ioctl(0, TIOCGWINSZ, &wsz) == 0
	 || ioctl(1, TIOCGWINSZ, &wsz) == 0
	 || ioctl(2, TIOCGWINSZ, &wsz) == 0
	) {
		w = wsz.ws_col;
		h = wsz.ws_row;
	}

	if (!argv[1]) {
		printf("%u %u", w, h);
	} else {
		const char *fmt, *arg;

		fmt = "%u %u" + 3; /* "%u" */
		while ((arg = *++argv) != NULL) {
			char c = arg[0];
			if (c == 'w')
				printf(fmt, w);
			if (c == 'h')
				printf(fmt, h);
			fmt = "%u %u" + 2; /* " %u" */
		}
	}
	bb_putchar('\n');
	return 0;
}
