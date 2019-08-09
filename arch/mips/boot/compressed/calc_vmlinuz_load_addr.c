// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 "Wu Zhangjin" <wuzhangjin@gmail.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/sizes.h>

int main(int argc, char *argv[])
{
	unsigned long long vmlinux_size, vmlinux_load_addr, vmlinuz_load_addr;
	struct stat sb;

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
	 * Align with 64KB: KEXEC needs load sections to be aligned to PAGE_SIZE,
	 * which may be as large as 64KB depending on the kernel configuration.
	 */

	vmlinuz_load_addr += (SZ_64K - vmlinux_size % SZ_64K);

	printf("0x%llx\n", vmlinuz_load_addr);

	return EXIT_SUCCESS;
}
