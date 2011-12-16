/*
 * PowerNV nvram code.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>

#include <asm/opal.h>
#include <asm/machdep.h>

static unsigned int nvram_size;

static ssize_t opal_nvram_size(void)
{
	return nvram_size;
}

static ssize_t opal_nvram_read(char *buf, size_t count, loff_t *index)
{
	s64 rc;
	int off;

	if (*index >= nvram_size)
		return 0;
	off = *index;
	if ((off + count) > nvram_size)
		count = nvram_size - off;
	rc = opal_read_nvram(__pa(buf), count, off);
	if (rc != OPAL_SUCCESS)
		return -EIO;
	*index += count;
	return count;
}

static ssize_t opal_nvram_write(char *buf, size_t count, loff_t *index)
{
	s64 rc = OPAL_BUSY;
	int off;

	if (*index >= nvram_size)
		return 0;
	off = *index;
	if ((off + count) > nvram_size)
		count = nvram_size - off;

	while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
		rc = opal_write_nvram(__pa(buf), count, off);
		if (rc == OPAL_BUSY_EVENT)
			opal_poll_events(NULL);
	}
	*index += count;
	return count;
}

void __init opal_nvram_init(void)
{
	struct device_node *np;
	const u32 *nbytes_p;

	np = of_find_compatible_node(NULL, NULL, "ibm,opal-nvram");
	if (np == NULL)
		return;

	nbytes_p = of_get_property(np, "#bytes", NULL);
	if (!nbytes_p) {
		of_node_put(np);
		return;
	}
	nvram_size = *nbytes_p;

	printk(KERN_INFO "OPAL nvram setup, %u bytes\n", nvram_size);
	of_node_put(np);

	ppc_md.nvram_read = opal_nvram_read;
	ppc_md.nvram_write = opal_nvram_write;
	ppc_md.nvram_size = opal_nvram_size;
}

