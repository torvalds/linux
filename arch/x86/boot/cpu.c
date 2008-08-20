/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/x86/boot/cpu.c
 *
 * Check for obligatory CPU features and abort if the features are not
 * present.
 */

#include "boot.h"
#include "cpustr.h"

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

int validate_cpu(void)
{
	u32 *err_flags;
	int cpu_level, req_level;
	const unsigned char *msg_strs;

	check_cpu(&cpu_level, &req_level, &err_flags);

	if (cpu_level < req_level) {
		printf("This kernel requires an %s CPU, ",
		       cpu_name(req_level));
		printf("but only detected an %s CPU.\n",
		       cpu_name(cpu_level));
		return -1;
	}

	if (err_flags) {
		int i, j;
		puts("This kernel requires the following features "
		     "not present on the CPU:\n");

		msg_strs = (const unsigned char *)x86_cap_strs;

		for (i = 0; i < NCAPINTS; i++) {
			u32 e = err_flags[i];

			for (j = 0; j < 32; j++) {
				int n = (i << 5)+j;
				if (*msg_strs < n) {
					/* Skip to the next string */
					do {
						msg_strs++;
					} while (*msg_strs);
					msg_strs++;
				}
				if (e & 1) {
					if (*msg_strs == n && msg_strs[1])
						printf("%s ", msg_strs+1);
					else
						printf("%d:%d ", i, j);
				}
				e >>= 1;
			}
		}
		putchar('\n');
		return -1;
	} else {
		return 0;
	}
}
