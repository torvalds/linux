/*
 * pmap implementation for busybox
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Written by Alexander Shishkin <virtuoso@slind.org>
 *
 * Licensed under GPLv2 or later, see the LICENSE file in this source tree
 * for details.
 */
//config:config PMAP
//config:	bool "pmap (6 kb)"
//config:	default y
//config:	help
//config:	Display processes' memory mappings.

//applet:IF_PMAP(APPLET(pmap, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_PMAP) += pmap.o

//usage:#define pmap_trivial_usage
//usage:       "[-xq] PID..."
//usage:#define pmap_full_usage "\n\n"
//usage:       "Display process memory usage"
//usage:     "\n"
//usage:     "\n	-x	Show details"
//usage:     "\n	-q	Quiet"

#include "libbb.h"

#if ULONG_MAX == 0xffffffff
# define TABS "\t"
# define AFMT "8"
# define DASHES ""
#else
# define TABS "\t\t"
# define AFMT "16"
# define DASHES "--------"
#endif

enum {
	OPT_x = 1 << 0,
	OPT_q = 1 << 1,
};

static void print_smaprec(struct smaprec *currec, void *data)
{
	unsigned opt = (uintptr_t)data;

	printf("%0" AFMT "lx ", currec->smap_start);

	if (opt & OPT_x)
		printf("%7lu %7lu %7lu %7lu ",
			currec->smap_size,
			currec->smap_pss,
			currec->private_dirty,
			currec->smap_swap);
	else
		printf("%7luK", currec->smap_size);

	printf(" %.4s  %s\n", currec->smap_mode, currec->smap_name);
}

static int procps_get_maps(pid_t pid, unsigned opt)
{
	struct smaprec total;
	int ret;
	char buf[256];

	read_cmdline(buf, sizeof(buf), pid, NULL);
	printf("%u: %s\n", (int)pid, buf);

	if (!(opt & OPT_q) && (opt & OPT_x))
		puts("Address" TABS "  Kbytes     PSS   Dirty    Swap  Mode  Mapping");

	memset(&total, 0, sizeof(total));

	ret = procps_read_smaps(pid, &total, print_smaprec, (void*)(uintptr_t)opt);
	if (ret)
		return ret;

	if (!(opt & OPT_q)) {
		if (opt & OPT_x)
			printf("--------" DASHES "  ------  ------  ------  ------\n"
				"total" TABS " %7lu %7lu %7lu %7lu\n",
				total.smap_size, total.smap_pss, total.private_dirty, total.smap_swap);
		else
			printf("mapped: %luK\n", total.smap_size);
	}

	return 0;
}

int pmap_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int pmap_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	int ret;

	opts = getopt32(argv, "^" "xq" "\0" "-1"); /* min one arg */
	argv += optind;

	ret = 0;
	while (*argv) {
		pid_t pid = xatoi_positive(*argv++);
		/* GNU pmap returns 42 if any of the pids failed */
		if (procps_get_maps(pid, opts) != 0)
			ret = 42;
	}

	return ret;
}
