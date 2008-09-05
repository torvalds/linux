/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * This is a host program to preprocess the CPU strings into a
 * compact format suitable for the setup code.
 */

#include <stdio.h>

#include "../kernel/cpu/capflags.c"

#if NCAPFLAGS > 8
# error "Need to adjust the boot code handling of CPUID strings"
#endif

int main(void)
{
	int i;
	const char *str;

	printf("static const char x86_cap_strs[] = \n");

	for (i = 0; i < NCAPINTS*32; i++) {
		str = x86_cap_flags[i];

		if (i == NCAPINTS*32-1) {
			/* The last entry must be unconditional; this
			   also consumes the compiler-added null character */
			if (!str)
				str = "";
			printf("\t\"\\x%02x\"\"%s\"\n", i, str);
		} else if (str) {
			printf("#if REQUIRED_MASK%d & (1 << %d)\n"
			       "\t\"\\x%02x\"\"%s\\0\"\n"
			       "#endif\n",
			       i >> 5, i & 31, i, str);
		}
	}
	printf("\t;\n");
	return 0;
}
