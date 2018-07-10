/* vi: set sw=4 ts=4: */
/*
 * Mini fgconsole implementation for busybox
 *
 * Copyright (C) 2010 by Grigory Batalov <bga@altlinux.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config FGCONSOLE
//config:	bool "fgconsole (1.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program prints active (foreground) console number.

//applet:IF_FGCONSOLE(APPLET_NOEXEC(fgconsole, fgconsole, BB_DIR_USR_BIN, BB_SUID_DROP, fgconsole))

//kbuild:lib-$(CONFIG_FGCONSOLE) += fgconsole.o

//usage:#define fgconsole_trivial_usage
//usage:	""
//usage:#define fgconsole_full_usage "\n\n"
//usage:	"Get active console"

#include "libbb.h"

/* From <linux/vt.h> */
struct vt_stat {
	unsigned short v_active;        /* active vt */
	unsigned short v_signal;        /* signal to send */
	unsigned short v_state;         /* vt bitmask */
};
enum { VT_GETSTATE = 0x5603 }; /* get global vt state info */

int fgconsole_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fgconsole_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	struct vt_stat vtstat;

	vtstat.v_active = 0;
	xioctl(get_console_fd_or_die(), VT_GETSTATE, &vtstat);
	printf("%d\n", vtstat.v_active);

	return EXIT_SUCCESS;
}
