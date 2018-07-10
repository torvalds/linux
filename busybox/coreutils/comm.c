/* vi: set sw=4 ts=4: */
/*
 * Mini comm implementation for busybox
 *
 * Copyright (C) 2005 by Robert Sullivan <cogito.ergo.cogito@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config COMM
//config:	bool "comm (3.9 kb)"
//config:	default y
//config:	help
//config:	comm is used to compare two files line by line and return
//config:	a three-column output.

//applet:IF_COMM(APPLET(comm, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_COMM) += comm.o

//usage:#define comm_trivial_usage
//usage:       "[-123] FILE1 FILE2"
//usage:#define comm_full_usage "\n\n"
//usage:       "Compare FILE1 with FILE2\n"
//usage:     "\n	-1	Suppress lines unique to FILE1"
//usage:     "\n	-2	Suppress lines unique to FILE2"
//usage:     "\n	-3	Suppress lines common to both files"

#include "libbb.h"

#define COMM_OPT_1 (1 << 0)
#define COMM_OPT_2 (1 << 1)
#define COMM_OPT_3 (1 << 2)

/* writeline outputs the input given, appropriately aligned according to class */
static void writeline(char *line, int class)
{
	int flags = option_mask32;
	if (class == 0) {
		if (flags & COMM_OPT_1)
			return;
	} else if (class == 1) {
		if (flags & COMM_OPT_2)
			return;
		if (!(flags & COMM_OPT_1))
			putchar('\t');
	} else /*if (class == 2)*/ {
		if (flags & COMM_OPT_3)
			return;
		if (!(flags & COMM_OPT_1))
			putchar('\t');
		if (!(flags & COMM_OPT_2))
			putchar('\t');
	}
	puts(line);
}

int comm_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int comm_main(int argc UNUSED_PARAM, char **argv)
{
	char *thisline[2];
	FILE *stream[2];
	int i;
	int order;

	getopt32(argv, "^" "123" "\0" "=2");
	argv += optind;

	for (i = 0; i < 2; ++i) {
		stream[i] = xfopen_stdin(argv[i]);
	}

	order = 0;
	thisline[1] = thisline[0] = NULL;
	while (1) {
		if (order <= 0) {
			free(thisline[0]);
			thisline[0] = xmalloc_fgetline(stream[0]);
		}
		if (order >= 0) {
			free(thisline[1]);
			thisline[1] = xmalloc_fgetline(stream[1]);
		}

		i = !thisline[0] + (!thisline[1] << 1);
		if (i)
			break;
		order = strcmp(thisline[0], thisline[1]);

		if (order >= 0)
			writeline(thisline[1], order ? 1 : 2);
		else
			writeline(thisline[0], 0);
	}

	/* EOF at least on one of the streams */
	i &= 1;
	if (thisline[i]) {
		/* stream[i] is not at EOF yet */
		/* we did not print thisline[i] yet */
		char *p = thisline[i];
		writeline(p, i);
		while (1) {
			free(p);
			p = xmalloc_fgetline(stream[i]);
			if (!p)
				break;
			writeline(p, i);
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		fclose(stream[0]);
		fclose(stream[1]);
	}

	return EXIT_SUCCESS;
}
