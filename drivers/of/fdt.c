/*
 * Functions for working with the Flattened Device Tree data format
 *
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_fdt.h>

struct boot_param_header *initial_boot_params;

char *find_flat_dt_string(u32 offset)
{
	return ((char *)initial_boot_params) +
		initial_boot_params->off_dt_strings + offset;
}

/**
 * of_scan_flat_dt - scan flattened tree blob and call callback on each.
 * @it: callback function
 * @data: context data pointer
 *
 * This function is used to scan the flattened device-tree, it is
 * used to extract the memory information at boot before we can
 * unflatten the tree
 */
int __init of_scan_flat_dt(int (*it)(unsigned long node,
				     const char *uname, int depth,
				     void *data),
			   void *data)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	int rc = 0;
	int depth = -1;

	do {
		u32 tag = *((u32 *)p);
		char *pathp;

		p += 4;
		if (tag == OF_DT_END_NODE) {
			depth--;
			continue;
		}
		if (tag == OF_DT_NOP)
			continue;
		if (tag == OF_DT_END)
			break;
		if (tag == OF_DT_PROP) {
			u32 sz = *((u32 *)p);
			p += 8;
			if (initial_boot_params->version < 0x10)
				p = _ALIGN(p, sz >= 8 ? 8 : 4);
			p += sz;
			p = _ALIGN(p, 4);
			continue;
		}
		if (tag != OF_DT_BEGIN_NODE) {
			pr_err("Invalid tag %x in flat device tree!\n", tag);
			return -EINVAL;
		}
		depth++;
		pathp = (char *)p;
		p = _ALIGN(p + strlen(pathp) + 1, 4);
		if ((*pathp) == '/') {
			char *lp, *np;
			for (lp = NULL, np = pathp; *np; np++)
				if ((*np) == '/')
					lp = np+1;
			if (lp != NULL)
				pathp = lp;
		}
		rc = it(p, pathp, depth, data);
		if (rc != 0)
			break;
	} while (1);

	return rc;
}
