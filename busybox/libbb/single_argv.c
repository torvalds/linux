/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2009 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

char* FAST_FUNC single_argv(char **argv)
{
	if (argv[1] && strcmp(argv[1], "--") == 0)
		argv++;
	if (!argv[1] || argv[2])
		bb_show_usage();
	return argv[1];
}
