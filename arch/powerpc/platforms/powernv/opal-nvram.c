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
#include <asm/nvram.h>
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

static int __init opal_nvram_init_log_partitions(void)
{
	/* Scan nvram for partitions */
	nvram_scan_partitions();
	nvram_init_oops_partition(0);
	return 0;
}
machine_arch_initcall(powernv, opal_nvram_init_log_partitions);

void __init opal_nvram_init(void)
{
	struct device_node *np;
	const __be32 *nbytes_p;

	np = of_find_compatible_node(NULL, NULL, "ibm,opal-nvram");
	if (np == NULL)
		return;

	nbytes_p = of_get_property(np, "#bytes", NULL);
	if (!nbytes_p) {
		of_node_put(np);
		return;
	}
	nvram_size = be32_to_cpup(nbytes_p);

	pr_info("OPAL nvram setup, %u bytes\n", nvram_size);
	of_node_put(np);

	ppc_md.nvram_read = opal_nvram_read;
	ppc_md.nvram_write = opal_nvram_write;
	ppc_md.nvram_size = opal_nvram_size;
}

