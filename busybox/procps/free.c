/* vi: set sw=4 ts=4: */
/*
 * Mini free implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FREE
//config:	bool "free (2.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX #sysinfo()
//config:	help
//config:	free displays the total amount of free and used physical and swap
//config:	memory in the system, as well as the buffers used by the kernel.
//config:	The shared memory column should be ignored; it is obsolete.

//applet:IF_FREE(APPLET_NOFORK(free, free, BB_DIR_USR_BIN, BB_SUID_DROP, free))

//kbuild:lib-$(CONFIG_FREE) += free.o

//usage:#define free_trivial_usage
//usage:       "" IF_DESKTOP("[-b/k/m/g]")
//usage:#define free_full_usage "\n\n"
//usage:       "Display the amount of free and used system memory"
//usage:
//usage:#define free_example_usage
//usage:       "$ free\n"
//usage:       "              total         used         free       shared      buffers\n"
//usage:       "  Mem:       257628       248724         8904        59644        93124\n"
//usage:       " Swap:       128516         8404       120112\n"
//usage:       "Total:       386144       257128       129016\n"

#include "libbb.h"
#ifdef __linux__
# include <sys/sysinfo.h>
#endif

struct globals {
	unsigned mem_unit;
#if ENABLE_DESKTOP
	uint8_t unit_steps;
# define G_unit_steps g->unit_steps
#else
# define G_unit_steps 10
#endif
};
/* Because of NOFORK, "globals" are not in global data */

static unsigned long long scale(struct globals *g, unsigned long d)
{
	return ((unsigned long long)d * g->mem_unit) >> G_unit_steps;
}

/* NOINLINE reduces main() stack usage, which makes code smaller (on x86 at least) */
static NOINLINE unsigned long parse_cached_kb(void)
{
	char buf[60]; /* actual lines we expect are ~30 chars or less */
	FILE *fp;
	unsigned long cached = 0;

	fp = xfopen_for_read("/proc/meminfo");
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "Cached: %lu %*s\n", &cached) == 1)
			break;
	}
	/* Have to close because of NOFORK */
	fclose(fp);

	return cached;
}

int free_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int free_main(int argc UNUSED_PARAM, char **argv IF_NOT_DESKTOP(UNUSED_PARAM))
{
	struct globals G;
	struct sysinfo info;
	unsigned long long cached;

#if ENABLE_DESKTOP
	G.unit_steps = 10;
	if (argv[1] && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'b':
			G.unit_steps = 0;
			break;
		case 'k': /* 2^10 */
			/* G.unit_steps = 10; - already is */
			break;
		case 'm': /* 2^(2*10) */
			G.unit_steps = 20;
			break;
		case 'g': /* 2^(3*10) */
			G.unit_steps = 30;
			break;
		default:
			bb_show_usage();
		}
	}
#endif
	printf("       %11s%11s%11s%11s%11s%11s\n"
	"Mem:   ",
		"total",
		"used",
		"free",
		"shared", "buffers", "cached" /* swap and total don't have these columns */
	);

	sysinfo(&info);
	/* Kernels prior to 2.4.x will return info.mem_unit==0, so cope... */
	G.mem_unit = (info.mem_unit ? info.mem_unit : 1);
	/* Extract cached from /proc/meminfo and convert to mem_units */
	cached = ((unsigned long long) parse_cached_kb() * 1024) / G.mem_unit;

#define FIELDS_6 "%11llu%11llu%11llu%11llu%11llu%11llu\n"
#define FIELDS_3 (FIELDS_6 + 3*6)
#define FIELDS_2 (FIELDS_6 + 4*6)

	printf(FIELDS_6,
		scale(&G, info.totalram),                //total
		scale(&G, info.totalram - info.freeram), //used
		scale(&G, info.freeram),                 //free
		scale(&G, info.sharedram),               //shared
		scale(&G, info.bufferram),               //buffers
		scale(&G, cached)                        //cached
	);
	/* Show alternate, more meaningful busy/free numbers by counting
	 * buffer cache as free memory. */
	printf("-/+ buffers/cache:");
	cached += info.freeram;
	cached += info.bufferram;
	printf(FIELDS_2,
		scale(&G, info.totalram - cached), //used
		scale(&G, cached)                  //free
	);
#if BB_MMU
	printf("Swap:  ");
	printf(FIELDS_3,
		scale(&G, info.totalswap),                 //total
		scale(&G, info.totalswap - info.freeswap), //used
		scale(&G, info.freeswap)                   //free
	);
#endif
	return EXIT_SUCCESS;
}
