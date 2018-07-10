/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

//kbuild:lib-y += getopt_allopts.o

void FAST_FUNC make_all_argv_opts(char **argv)
{
	/* Note: we skip argv[0] */
	while (*++argv) {
		char *p;

		if (argv[0][0] == '-')
			continue;
		/* Neither top nor ps care if "" arg turns into "-" */
		/*if (argv[0][0] == '\0')
			continue;*/
		p = xmalloc(strlen(*argv) + 2);
		*p = '-';
		strcpy(p + 1, *argv);
		*argv = p;
	}
}
