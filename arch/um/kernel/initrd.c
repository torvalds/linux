/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/init.h"
#include "linux/bootmem.h"
#include "linux/initrd.h"
#include "asm/types.h"
#include "init.h"
#include "os.h"

/* Changed by uml_initrd_setup, which is a setup */
static char *initrd __initdata = NULL;
static int load_initrd(char *filename, void *buf, int size);

static int __init read_initrd(void)
{
	void *area;
	long long size;
	int err;

	if (initrd == NULL)
		return 0;

	err = os_file_size(initrd, &size);
	if (err)
		return 0;

	/*
	 * This is necessary because alloc_bootmem craps out if you
	 * ask for no memory.
	 */
	if (size == 0) {
		printk(KERN_ERR "\"%s\" is a zero-size initrd\n", initrd);
		return 0;
	}

	area = alloc_bootmem(size);
	if (area == NULL)
		return 0;

	if (load_initrd(initrd, area, size) == -1)
		return 0;

	initrd_start = (unsigned long) area;
	initrd_end = initrd_start + size;
	return 0;
}

__uml_postsetup(read_initrd);

static int __init uml_initrd_setup(char *line, int *add)
{
	initrd = line;
	return 0;
}

__uml_setup("initrd=", uml_initrd_setup,
"initrd=<initrd image>\n"
"    This is used to boot UML from an initrd image.  The argument is the\n"
"    name of the file containing the image.\n\n"
);

static int load_initrd(char *filename, void *buf, int size)
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
