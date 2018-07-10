/* vi: set sw=4 ts=4: */
/*
 * setkeycodes
 *
 * Copyright (C) 1994-1998 Andries E. Brouwer <aeb@cwi.nl>
 *
 * Adjusted for BusyBox by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SETKEYCODES
//config:	bool "setkeycodes (1.7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program loads entries into the kernel's scancode-to-keycode
//config:	map, allowing unusual keyboards to generate usable keycodes.

//applet:IF_SETKEYCODES(APPLET_NOEXEC(setkeycodes, setkeycodes, BB_DIR_USR_BIN, BB_SUID_DROP, setkeycodes))

//kbuild:lib-$(CONFIG_SETKEYCODES) += setkeycodes.o

//usage:#define setkeycodes_trivial_usage
//usage:       "{ SCANCODE KEYCODE }..."
//usage:#define setkeycodes_full_usage "\n\n"
//usage:       "Modify kernel's scancode-to-keycode map,\n"
//usage:       "allowing unusual keyboards to generate usable keycodes.\n\n"
//usage:       "SCANCODE is either xx or e0xx (hexadecimal), KEYCODE is decimal."
//usage:
//usage:#define setkeycodes_example_usage
//usage:       "$ setkeycodes e030 127\n"

#include "libbb.h"

/* From <linux/kd.h> */
struct kbkeycode {
	unsigned scancode, keycode;
};
enum {
	KDSETKEYCODE = 0x4B4D  /* write kernel keycode table entry */
};

int setkeycodes_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setkeycodes_main(int argc, char **argv)
{
	int fd;

	if (!(argc & 1) /* if even */ || argc < 2) {
		bb_show_usage();
	}

	fd = get_console_fd_or_die();

	while (argv[1]) {
		struct kbkeycode a;
		int sc;

		sc = xstrtoul_range(argv[1], 16, 0, 0xe07f);
		if (sc >= 0xe000) {
			sc -= 0xe000;
			sc += 0x0080;
		}
		a.scancode = sc;
		a.keycode = xatou_range(argv[2], 0, 255);
		ioctl_or_perror_and_die(fd, KDSETKEYCODE, &a,
			"can't set SCANCODE %x to KEYCODE %d",
			sc, a.keycode);
		argv += 2;
	}
	return EXIT_SUCCESS;
}
