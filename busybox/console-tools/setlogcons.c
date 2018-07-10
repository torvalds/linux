/* vi: set sw=4 ts=4: */
/*
 * setlogcons: Send kernel messages to the current console or to console N
 *
 * Copyright (C) 2006 by Jan Kiszka <jan.kiszka@web.de>
 *
 * Based on setlogcons (kbd-1.12) by Andries E. Brouwer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SETLOGCONS
//config:	bool "setlogcons (1.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program redirects the output console of kernel messages.

//applet:IF_SETLOGCONS(APPLET_NOEXEC(setlogcons, setlogcons, BB_DIR_USR_SBIN, BB_SUID_DROP, setlogcons))

//kbuild:lib-$(CONFIG_SETLOGCONS) += setlogcons.o

//usage:#define setlogcons_trivial_usage
//usage:       "[N]"
//usage:#define setlogcons_full_usage "\n\n"
//usage:       "Pin kernel output to VT console N. Default:0 (do not pin)"

// Comment from kernel source:
/* ...
 * By default, the kernel messages are always printed on the current virtual
 * console. However, the user may modify that default with the
 * TIOCL_SETKMSGREDIRECT ioctl call.
 *
 * This function sets the kernel message console to be @new. It returns the old
 * virtual console number. The virtual terminal number 0 (both as parameter and
 * return value) means no redirection (i.e. always printed on the currently
 * active console).
 */

#include "libbb.h"

int setlogcons_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setlogcons_main(int argc UNUSED_PARAM, char **argv)
{
	char *devname;
	struct {
		char fn;
		char subarg;
	} arg = {
		11, /* redirect kernel messages (TIOCL_SETKMSGREDIRECT) */
		0
	};

	if (argv[1])
		arg.subarg = xatou_range(argv[1], 0, 63);

	/* Can just call it on "/dev/tty1" always, but...
	 * in my testing, inactive (never opened) VTs are not
	 * redirected to, despite ioctl not failing.
	 *
	 * By using "/dev/ttyN", ensure it is activated.
	 */
	devname = xasprintf("/dev/tty%u", arg.subarg);
	xioctl(xopen(devname, O_RDONLY), TIOCLINUX, &arg);

	return EXIT_SUCCESS;
}
