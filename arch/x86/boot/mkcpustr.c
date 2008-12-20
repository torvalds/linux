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

int main(void)
{
	int i, j;
	const char *str;

	printf("static const char x86_cap_strs[] = \n");

	for (i = 0; i < NCAPINTS; i++) {
		for (j = 0; j < 32; j++) {
			str = x86_cap_flags[i*32+j];

			if (i == NCAPINTS-1 && j == 31) {
				/* The last entry must be unconditional; this
				   also consumes the compiler-added null
				   character */
				if (!str)
					str = "";
				printf("\t\"\\x%02x\\x%02x\"\"%s\"\n",
				       i, j, str);
			} else if (str) {
				printf("#if REQUIRED_MASK%d & (1 << %d)\n"
				       "\t\"\\x%02x\\x%02x\"\"%s\\0\"\n"
				       "#endif\n",
				       i, j, i, j, str);
			}
		}
	}
	printf("\t;\n");
	return 0;
}
