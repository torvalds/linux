/* vi: set sw=4 ts=4: */
/*
 * Mini true implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config TRUE
//config:	bool "true (tiny)"
//config:	default y
//config:	help
//config:	true returns an exit code of TRUE (0).

//applet:IF_TRUE(APPLET_NOFORK(true, true, BB_DIR_BIN, BB_SUID_DROP, true))

//kbuild:lib-$(CONFIG_TRUE) += true.o

/* BB_AUDIT SUSv3 compliant */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/true.html */

/* "true --help" is special-cased to ignore --help */
//usage:#define true_trivial_usage NOUSAGE_STR
//usage:#define true_full_usage ""
//usage:#define true_example_usage
//usage:       "$ true\n"
//usage:       "$ echo $?\n"
//usage:       "0\n"

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

int true_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int true_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	return EXIT_SUCCESS;
}
