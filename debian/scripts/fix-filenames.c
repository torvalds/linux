/*
 * fix-filenames: find a specified pathname prefix and remove it from
 *                C strings.
 *
 * Copyright (C) 2018 Canonical Ltd.
 * Author: Andy Whitcroft <apw@canonical.com>
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int rc;
	char *in_name;
	char *prefix;
	int prefix_len;
	int in_fd;
	struct stat in_info;
	char *in;
	off_t size;
	int length;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <file> <prefix>\n", argv[0]);
		exit(1);
	}
	in_name    = argv[1];
	prefix     = argv[2];
	prefix_len = strlen(prefix);

	in_fd = open(in_name, O_RDWR);
	if (in_fd < 0) {
		perror("open input failed");
		exit(1);
	}

	rc = fstat(in_fd, &in_info);
	if (rc < 0) {
		perror("fstat input failed");
		exit(1);
	}
	size = in_info.st_size;
	printf("%s %ld bytes\n", in_name + prefix_len + 1, size);

	in = mmap((void *)0, size, PROT_READ|PROT_WRITE, MAP_SHARED, in_fd, (off_t)0);
	if (!in) {
		perror("mmap failed");
		exit(1);
	}

	for (; size > 0; size--, in++) {
		if (*in != *prefix)
			continue;
		if (strncmp(in, prefix, prefix_len) != 0)
			continue;
		length = strlen(in + prefix_len + 1) + 1;

		/*
		 * Copy the suffix portion down to the start and clear
		 * the remainder of the space to 0.
		 */
		memmove(in, in + prefix_len + 1, length);
		memset(in + length, '_', prefix_len);
	}
}
