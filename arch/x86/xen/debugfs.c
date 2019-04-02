// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/defs.h>
#include <linux/slab.h>

#include "defs.h"

static struct dentry *d_xen_de;

struct dentry * __init xen_init_defs(void)
{
	if (!d_xen_de) {
		d_xen_de = defs_create_dir("xen", NULL);

		if (!d_xen_de)
			pr_warning("Could not create 'xen' defs directory\n");
	}

	return d_xen_de;
}

