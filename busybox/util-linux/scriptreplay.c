/* vi: set sw=4 ts=4: */
/*
 * scriptreplay - play back typescripts, using timing information
 *
 * pascal.bellard@ads-lu.com
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SCRIPTREPLAY
//config:	bool "scriptreplay (2.6 kb)"
//config:	default y
//config:	help
//config:	This program replays a typescript, using timing information
//config:	given by script -t.

//applet:IF_SCRIPTREPLAY(APPLET(scriptreplay, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SCRIPTREPLAY) += scriptreplay.o

//usage:#define scriptreplay_trivial_usage
//usage:       "TIMINGFILE [TYPESCRIPT [DIVISOR]]"
//usage:#define scriptreplay_full_usage "\n\n"
//usage:       "Play back typescripts, using timing information"

#include "libbb.h"

int scriptreplay_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int scriptreplay_main(int argc UNUSED_PARAM, char **argv)
{
	const char *script = "typescript";
	double delay, factor = 1000000.0;
	int fd;
	unsigned long count;
	FILE *tfp;

	if (!argv[1])
		bb_show_usage();

	if (argv[2]) {
		script = argv[2];
		if (argv[3])
			factor /= atof(argv[3]);
	}

	tfp = xfopen_for_read(argv[1]);
	fd = xopen(script, O_RDONLY);
	while (fscanf(tfp, "%lf %lu\n", &delay, &count) == 2) {
		usleep(delay * factor);
		bb_copyfd_exact_size(fd, STDOUT_FILENO, count);
	}
	if (ENABLE_FEATURE_CLEAN_UP) {
		close(fd);
		fclose(tfp);
	}
	return EXIT_SUCCESS;
}
