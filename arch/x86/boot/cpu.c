// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/x86/boot/cpu.c
 *
 * Check for obligatory CPU features and abort if the features are not
 * present.
 */

#include "boot.h"
#ifdef CONFIG_X86_FEATURE_NAMES
#include "cpustr.h"
#endif

static char *cpu_name(int level)
{
	static char buf[6];

	if (level == 64) {
		return "x86-64";
	} else {
		if (level == 15)
			level = 6;
		sprintf(buf, "i%d86", level);
		return buf;
	}
}

static void show_cap_strs(u32 *err_flags)
{
	int i, j;
#ifdef CONFIG_X86_FEATURE_NAMES
	const unsigned char *msg_strs = (const unsigned char *)x86_cap_strs;
	for (i = 0; i < NCAPINTS; i++) {
		u32 e = err_flags[i];
		for (j = 0; j < 32; j++) {
			if (msg_strs[0] < i ||
			    (msg_strs[0] == i && msg_strs[1] < j)) {
				/* Skip to the next string */
				msg_strs += 2;
				while (*msg_strs++)
					;
			}
			if (e & 1) {
				if (msg_strs[0] == i &&
				    msg_strs[1] == j &&
				    msg_strs[2])
					printf("%s ", msg_strs+2);
				else
					printf("%d:%d ", i, j);
			}
			e >>= 1;
		}
	}
#else
	for (i = 0; i < NCAPINTS; i++) {
		u32 e = err_flags[i];
		for (j = 0; j < 32; j++) {
			if (e & 1)
				printf("%d:%d ", i, j);
			e >>= 1;
		}
	}
#endif
}

int validate_cpu(void)
{
	u32 *err_flags;
	int cpu_level, req_level;

	check_cpu(&cpu_level, &req_level, &err_flags);

	if (cpu_level < req_level) {
		printf("This kernel requires an %s CPU, ",
		       cpu_name(req_level));
		printf("but only detected an %s CPU.\n",
		       cpu_name(cpu_level));
		return -1;
	}

	if (err_flags) {
		puts("This kernel requires the following features "
		     "not present on the CPU:\n");
		show_cap_strs(err_flags);
		putchar('\n');
		return -1;
	} else if (check_knl_erratum()) {
		return -1;
	} else {
		return 0;
	}
}
