/* Minimal wrapper to build an individual busybox applet.
 *
 * Copyright 2005 Rob Landley <rob@landley.net
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

const char *applet_name;

#include <stdio.h>
#include <stdlib.h>
#include "usage.h"

int main(int argc, char **argv)
{
	applet_name = argv[0];
	return APPLET_main(argc, argv);
}

void bb_show_usage(void)
{
	fputs(APPLET_full_usage "\n", stdout);
	exit(EXIT_FAILURE);
}
