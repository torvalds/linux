/* vi: set sw=4 ts=4: */
/*
 * beep implementation for busybox
 *
 * Copyright (C) 2009 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config BEEP
//config:	bool "beep (3 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The beep applets beeps in a given freq/Hz.
//config:
//config:config FEATURE_BEEP_FREQ
//config:	int "default frequency"
//config:	range 20 50000	# allowing 0 here breaks the build
//config:	default 4000
//config:	depends on BEEP
//config:	help
//config:	Frequency for default beep.
//config:
//config:config FEATURE_BEEP_LENGTH_MS
//config:	int "default length"
//config:	range 0 2147483647
//config:	default 30
//config:	depends on BEEP
//config:	help
//config:	Length in ms for default beep.

//applet:IF_BEEP(APPLET(beep, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_BEEP) += beep.o

//usage:#define beep_trivial_usage
//usage:       "-f FREQ -l LEN -d DELAY -r COUNT -n"
//usage:#define beep_full_usage "\n\n"
//usage:       "	-f	Frequency in Hz"
//usage:     "\n	-l	Length in ms"
//usage:     "\n	-d	Delay in ms"
//usage:     "\n	-r	Repetitions"
//usage:     "\n	-n	Start new tone"

#include "libbb.h"

#include <linux/kd.h>
#ifndef CLOCK_TICK_RATE
# define CLOCK_TICK_RATE 1193180
#endif

/* defaults */
#ifndef CONFIG_FEATURE_BEEP_FREQ
# define FREQ (4000)
#else
# define FREQ (CONFIG_FEATURE_BEEP_FREQ)
#endif
#ifndef CONFIG_FEATURE_BEEP_LENGTH_MS
# define LENGTH (30)
#else
# define LENGTH (CONFIG_FEATURE_BEEP_LENGTH_MS)
#endif
#define DELAY (0)
#define REPETITIONS (1)

int beep_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int beep_main(int argc, char **argv)
{
	int speaker = get_console_fd_or_die();
	unsigned tickrate_div_freq = tickrate_div_freq; /* for compiler */
	unsigned length = length;
	unsigned delay = delay;
	unsigned rep = rep;
	int c;

	c = 'n';
	while (c != -1) {
		if (c == 'n') {
			tickrate_div_freq = CLOCK_TICK_RATE / FREQ;
			length = LENGTH;
			delay = DELAY;
			rep = REPETITIONS;
		}
		c = getopt(argc, argv, "f:l:d:r:n");
/* TODO: -s, -c:
 * pipe stdin to stdout, but also beep after each line (-s) or char (-c)
 */
		switch (c) {
		case 'f':
/* TODO: what "-f 0" should do? */
			tickrate_div_freq = (unsigned)CLOCK_TICK_RATE / xatou(optarg);
			continue;
		case 'l':
			length = xatou(optarg);
			continue;
		case 'd':
/* TODO:
 * -d N, -D N
 * specify a delay of N milliseconds between repetitions.
 * -d specifies that this delay should only occur between beeps,
 * that is, it should not occur after the last repetition.
 * -D indicates that the delay should occur after every repetition
 */
			delay = xatou(optarg);
			continue;
		case 'r':
			rep = xatou(optarg);
			continue;
		case 'n':
		case -1:
			break;
		default:
			bb_show_usage();
		}
		while (rep) {
//bb_error_msg("rep[%d] freq=%d, length=%d, delay=%d", rep, freq, length, delay);
			xioctl(speaker, KIOCSOUND, (void*)(uintptr_t)tickrate_div_freq);
			usleep(1000 * length);
			ioctl(speaker, KIOCSOUND, (void*)0);
			if (--rep)
				usleep(1000 * delay);
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		close(speaker);
	return EXIT_SUCCESS;
}
/*
 * so, e.g. Beethoven's 9th symphony "Ode an die Freude" would be
 * something like:
a=$((220*3))
b=$((247*3))
c=$((262*3))
d=$((294*3))
e=$((329*3))
f=$((349*3))
g=$((392*3))
#./beep -f$d -l200 -r2 -n -f$e -l100 -d 10 -n -f$c -l400 -f$g -l200
./beep -f$e -l200 -r2 \
        -n -d 100 -f$f -l200 \
        -n -f$g -l200 -r2 \
        -n -f$f -l200 \
        -n -f$e -l200 \
        -n -f$d -l200 \
        -n -f$c -l200 -r2 \
        -n -f$d -l200 \
        -n -f$e -l200 \
        -n -f$e -l400 \
        -n -f$d -l100 \
        -n -f$d -l200 \
*/
