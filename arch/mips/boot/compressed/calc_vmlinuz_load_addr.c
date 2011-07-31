/*
 * Copyright (C) 2010 "Wu Zhangjin" <wuzhangjin@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	struct stat sb;
	uint64_t vmlinux_size, vmlinux_load_addr, vmlinuz_load_addr;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <pathname> <vmlinux_load_addr>\n",
				argv[0]);
		return EXIT_FAILURE;
	}

	if (stat(argv[1], &sb) == -1) {
		perror("stat");
		return EXIT_FAILURE;
	}

	/* Convert hex characters to dec number */
	errno = 0;
	if (sscanf(argv[2], "%llx", &vmlinux_load_addr) != 1) {
		if (errno != 0)
			perror("sscanf");
		else
			fprintf(stderr, "No matching characters\n");

		return EXIT_FAILURE;
	}

	vmlinux_size = (uint64_t)sb.st_size;
	vmlinuz_load_addr = vmlinux_load_addr + vmlinux_size;

	/*
	 * Align with 16 bytes: "greater than that used for any standard data
	 * types by a MIPS compiler." -- See MIPS Run Linux (Second Edition).
	 */

	vmlinuz_load_addr += (16 - vmlinux_size % 16);

	printf("0x%llx\n", vmlinuz_load_addr);

	return EXIT_SUCCESS;
}
