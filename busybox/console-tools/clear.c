/* vi: set sw=4 ts=4: */
/*
 * Mini clear implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CLEAR
//config:	bool "clear (tiny)"
//config:	default y
//config:	help
//config:	This program clears the terminal screen.

//applet:IF_CLEAR(APPLET_NOFORK(clear, clear, BB_DIR_USR_BIN, BB_SUID_DROP, clear))

//kbuild:lib-$(CONFIG_CLEAR) += clear.o

//usage:#define clear_trivial_usage
//usage:       ""
//usage:#define clear_full_usage "\n\n"
//usage:       "Clear screen"

#include "libbb.h"

#define ESC "\033"

int clear_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int clear_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	/* home; clear to the end of screen */
	return full_write1_str(ESC"[H" ESC"[J") != 6;
}
