// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */
#include <linux/memblock.h>
#include <os.h>

#include "um_arch.h"

static int __init __uml_load_file(const char *filename, void *buf, int size)
{
	int fd, n;

	fd = os_open_file(filename, of_read(OPENFLAGS()), 0);
	if (fd < 0) {
		printk(KERN_ERR "Opening '%s' failed - err = %d\n", filename,
		       -fd);
		return -1;
	}
	n = os_read_file(fd, buf, size);
	if (n != size) {
		printk(KERN_ERR "Read of %d bytes from '%s' failed, "
		       "err = %d\n", size,
		       filename, -n);
		return -1;
	}

	os_close_file(fd);
	return 0;
}

void *uml_load_file(const char *filename, unsigned long long *size)
{
	void *area;
	int err;

	*size = 0;

	if (!filename)
		return NULL;

	err = os_file_size(filename, size);
	if (err)
		return NULL;

	if (*size == 0) {
		printk(KERN_ERR "\"%s\" is empty\n", filename);
		return NULL;
	}

	area = memblock_alloc_or_panic(*size, SMP_CACHE_BYTES);

	if (__uml_load_file(filename, area, *size)) {
		memblock_free(area, *size);
		return NULL;
	}

	return area;
}
