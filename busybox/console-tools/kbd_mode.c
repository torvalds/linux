/* vi: set sw=4 ts=4: */
/*
 * Mini kbd_mode implementation for busybox
 *
 * Copyright (C) 2007 Loic Grenie <loic.grenie@gmail.com>
 *   written using Andries Brouwer <aeb@cwi.nl>'s kbd_mode from
 *   console-utils v0.2.3, licensed under GNU GPLv2
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config KBD_MODE
//config:	bool "kbd_mode (4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program reports and sets keyboard mode.

//applet:IF_KBD_MODE(APPLET_NOEXEC(kbd_mode, kbd_mode, BB_DIR_BIN, BB_SUID_DROP, kbd_mode))

//kbuild:lib-$(CONFIG_KBD_MODE) += kbd_mode.o

//usage:#define kbd_mode_trivial_usage
//usage:       "[-a|k|s|u] [-C TTY]"
//usage:#define kbd_mode_full_usage "\n\n"
//usage:       "Report or set VT console keyboard mode\n"
//usage:     "\n	-a	Default (ASCII)"
//usage:     "\n	-k	Medium-raw (keycode)"
//usage:     "\n	-s	Raw (scancode)"
//usage:     "\n	-u	Unicode (utf-8)"
//usage:     "\n	-C TTY	Affect TTY"

#include "libbb.h"
#include <linux/kd.h>

int kbd_mode_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int kbd_mode_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		SCANCODE  = (1 << 0),
		ASCII     = (1 << 1),
		MEDIUMRAW = (1 << 2),
		UNICODE   = (1 << 3),
	};
	int fd;
	unsigned opt;
	const char *tty_name;

	opt = getopt32(argv, "sakuC:", &tty_name);
	if (opt & 0x10) {
		opt &= 0xf; /* clear -C bit, see (*) */
		fd = xopen_nonblocking(tty_name);
	} else {
		/* kbd-2.0.3 tries in sequence:
		 * fd#0, /dev/tty, /dev/tty0.
		 * get_console_fd_or_die: /dev/console, /dev/tty0, /dev/tty.
		 * kbd-2.0.3 checks KDGKBTYPE, get_console_fd_or_die checks too.
		 */
		fd = get_console_fd_or_die();
	}

	if (!opt) { /* print current setting */
		const char *mode = "unknown";
		int m;

		xioctl(fd, KDGKBMODE, &m);
		if (m == K_RAW)
			mode = "raw (scancode)";
		else if (m == K_XLATE)
			mode = "default (ASCII)";
		else if (m == K_MEDIUMRAW)
			mode = "mediumraw (keycode)";
		else if (m == K_UNICODE)
			mode = "Unicode (UTF-8)";
		else if (m == 4 /*K_OFF*/) /* kbd-2.0.3 does not show this mode, says "unknown" */
			mode = "off";
		printf("The keyboard is in %s mode\n", mode);
	} else {
		/* here we depend on specific bits assigned to options (*)
		 * KDSKBMODE constants have these values:
		 * #define K_RAW           0x00
		 * #define K_XLATE         0x01
		 * #define K_MEDIUMRAW     0x02
		 * #define K_UNICODE       0x03
		 * #define K_OFF           0x04
		 * (looks like "-ak" together would cause the same effect as -u)
		 */
		opt = opt & UNICODE ? 3 : opt >> 1;
		/* double cast prevents warnings about widening conversion */
		xioctl(fd, KDSKBMODE, (void*)(ptrdiff_t)opt);
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);
	return EXIT_SUCCESS;
}
