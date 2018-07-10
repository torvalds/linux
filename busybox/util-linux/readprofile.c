/* vi: set sw=4 ts=4: */
/*
 * readprofile.c - used to read /proc/profile
 *
 * Copyright (C) 1994,1996 Alessandro Rubini (rubini@ipvvis.unipv.it)
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/*
 * 1999-02-22 Arkadiusz Mickiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-09-01 Stephane Eranian <eranian@cello.hpl.hp.com>
 * - 64bit clean patch
 * 3Feb2001 Andrew Morton <andrewm@uow.edu.au>
 * - -M option to write profile multiplier.
 * 2001-11-07 Werner Almesberger <wa@almesberger.net>
 * - byte order auto-detection and -n option
 * 2001-11-09 Werner Almesberger <wa@almesberger.net>
 * - skip step size (index 0)
 * 2002-03-09 John Levon <moz@compsoc.man.ac.uk>
 * - make maplineno do something
 * 2002-11-28 Mads Martin Joergensen +
 * - also try /boot/System.map-`uname -r`
 * 2003-04-09 Werner Almesberger <wa@almesberger.net>
 * - fixed off-by eight error and improved heuristics in byte order detection
 * 2003-08-12 Nikita Danilov <Nikita@Namesys.COM>
 * - added -s option; example of use:
 * "readprofile -s -m /boot/System.map-test | grep __d_lookup | sort -n -k3"
 *
 * Taken from util-linux and adapted for busybox by
 * Paul Mundt <lethal@linux-sh.org>.
 */
//config:config READPROFILE
//config:	bool "readprofile (7.2 kb)"
//config:	default y
//config:	#select PLATFORM_LINUX
//config:	help
//config:	This allows you to parse /proc/profile for basic profiling.

//applet:IF_READPROFILE(APPLET(readprofile, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_READPROFILE) += readprofile.o

//usage:#define readprofile_trivial_usage
//usage:       "[OPTIONS]"
//usage:#define readprofile_full_usage "\n\n"
//usage:       "	-m mapfile	(Default: /boot/System.map)"
//usage:     "\n	-p profile	(Default: /proc/profile)"
//usage:     "\n	-M NUM		Set the profiling multiplier to NUM"
//usage:     "\n	-i		Print only info about the sampling step"
//usage:     "\n	-v		Verbose"
//usage:     "\n	-a		Print all symbols, even if count is 0"
//usage:     "\n	-b		Print individual histogram-bin counts"
//usage:     "\n	-s		Print individual counters within functions"
//usage:     "\n	-r		Reset all the counters (root only)"
//usage:     "\n	-n		Disable byte order auto-detection"

#include "libbb.h"
#include <sys/utsname.h>

#define S_LEN 128

int readprofile_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int readprofile_main(int argc UNUSED_PARAM, char **argv)
{
	FILE *map;
	const char *mapFile, *proFile;
	unsigned long indx;
	size_t len;
	uint64_t add0;
	unsigned int step;
	unsigned int *buf, total, fn_len;
	unsigned long long fn_add, next_add;     /* current and next address */
	char fn_name[S_LEN], next_name[S_LEN];   /* current and next name */
	char mapline[S_LEN];
	char mode[8];
	int maplineno;
	int multiplier;
	unsigned opt;
	enum {
		OPT_M = (1 << 0),
		OPT_m = (1 << 1),
		OPT_p = (1 << 2),
		OPT_n = (1 << 3),
		OPT_a = (1 << 4),
		OPT_b = (1 << 5),
		OPT_s = (1 << 6),
		OPT_i = (1 << 7),
		OPT_r = (1 << 8),
		OPT_v = (1 << 9),
	};
#define optMult    (opt & OPT_M)
#define optNative  (opt & OPT_n)
#define optAll     (opt & OPT_a)
#define optBins    (opt & OPT_b)
#define optSub     (opt & OPT_s)
#define optInfo    (opt & OPT_i)
#define optReset   (opt & OPT_r)
#define optVerbose (opt & OPT_v)

#define next (current^1)

	proFile = "/proc/profile";
	mapFile = "/boot/System.map";
	multiplier = 0;

	opt = getopt32(argv, "M:+m:p:nabsirv", &multiplier, &mapFile, &proFile);

	if (opt & (OPT_M|OPT_r)) { /* mult or reset, or both */
		int fd, to_write;

		/*
		 * When writing the multiplier, if the length of the write is
		 * not sizeof(int), the multiplier is not changed
		 */
		to_write = sizeof(int);
		if (!optMult)
			to_write = 1;  /* sth different from sizeof(int) */

		fd = xopen("/proc/profile", O_WRONLY);
		xwrite(fd, &multiplier, to_write);
		close(fd);
		return EXIT_SUCCESS;
	}

	/*
	 * Use an fd for the profiling buffer, to skip stdio overhead
	 */
	len = MAXINT(ssize_t);
	buf = xmalloc_xopen_read_close(proFile, &len);
	len /= sizeof(*buf);

	if (!optNative) {
		int big = 0, small = 0;
		unsigned *p;

		for (p = buf+1; p < buf+len; p++) {
			if (*p & ~0U << (sizeof(*buf)*4))
				big++;
			if (*p & ((1 << (sizeof(*buf)*4))-1))
				small++;
		}
		if (big > small) {
			bb_error_msg("assuming reversed byte order, "
				"use -n to force native byte order");
			BUILD_BUG_ON(sizeof(*p) > 8);
			for (p = buf; p < buf+len; p++) {
				if (sizeof(*p) == 2)
					*p = bswap_16(*p);
				if (sizeof(*p) == 4)
					*p = bswap_32(*p);
				if (sizeof(*p) == 8)
					*p = bb_bswap_64(*p);
			}
		}
	}

	step = buf[0];
	if (optInfo) {
		printf("Sampling_step: %u\n", step);
		return EXIT_SUCCESS;
	}

	map = xfopen_for_read(mapFile);
	add0 = 0;
	maplineno = 1;
	while (fgets(mapline, S_LEN, map)) {
		if (sscanf(mapline, "%llx %s %s", &fn_add, mode, fn_name) != 3)
			bb_error_msg_and_die("%s(%i): wrong map line",
					mapFile, maplineno);

		if (strcmp(fn_name, "_stext") == 0) /* only elf works like this */ {
			add0 = fn_add;
			break;
		}
		maplineno++;
	}

	if (!add0)
		bb_error_msg_and_die("can't find \"_stext\" in %s", mapFile);

	/*
	 * Main loop.
	 */
	total = 0;
	indx = 1;
	while (fgets(mapline, S_LEN, map)) {
		bool header_printed;
		unsigned int this;

		if (sscanf(mapline, "%llx %s %s", &next_add, mode, next_name) != 3)
			bb_error_msg_and_die("%s(%i): wrong map line",
					mapFile, maplineno);

		header_printed = 0;

		/* ignore any LEADING (before a '[tT]' symbol is found)
		   Absolute symbols */
		if ((mode[0] == 'A' || mode[0] == '?') && total == 0)
			continue;
		if ((mode[0]|0x20) != 't' && (mode[0]|0x20) != 'w') {
			break;  /* only text is profiled */
		}

		if (indx >= len)
			bb_error_msg_and_die("profile address out of range. "
					"Wrong map file?");

		this = 0;
		while (indx < (next_add-add0)/step) {
			if (optBins && (buf[indx] || optAll)) {
				if (!header_printed) {
					printf("%s:\n", fn_name);
					header_printed = 1;
				}
				printf("\t%"PRIx64"\t%u\n", (indx - 1)*step + add0, buf[indx]);
			}
			this += buf[indx++];
		}
		total += this;

		if (optBins) {
			if (optVerbose || this > 0)
				printf("  total\t\t\t\t%u\n", this);
		} else
		if ((this || optAll)
		 && (fn_len = next_add-fn_add) != 0
		) {
			if (optVerbose)
				printf("%016llx %-40s %6u %8.4f\n", fn_add,
					fn_name, this, this/(double)fn_len);
			else
				printf("%6u %-40s %8.4f\n",
					this, fn_name, this/(double)fn_len);
			if (optSub) {
				unsigned long long scan;

				for (scan = (fn_add-add0)/step + 1;
				     scan < (next_add-add0)/step; scan++) {
					unsigned long long addr;

					addr = (scan - 1)*step + add0;
					printf("\t%#llx\t%s+%#llx\t%u\n",
						addr, fn_name, addr - fn_add,
						buf[scan]);
				}
			}
		}

		fn_add = next_add;
		strcpy(fn_name, next_name);

		maplineno++;
	}

	/* clock ticks, out of kernel text - probably modules */
	printf("%6u *unknown*\n", buf[len-1]);

	/* trailer */
	if (optVerbose)
		printf("%016x %-40s %6u %8.4f\n",
			0, "total", total, total/(double)(fn_add-add0));
	else
		printf("%6u %-40s %8.4f\n",
			total, "total", total/(double)(fn_add-add0));

	if (ENABLE_FEATURE_CLEAN_UP) {
		fclose(map);
		free(buf);
	}

	return EXIT_SUCCESS;
}
