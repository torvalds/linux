#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "debugfs.h"

static struct dentry *d_xen_debug;

struct dentry * __init xen_init_debugfs(void)
{
	if (!d_xen_debug) {
		d_xen_debug = debugfs_create_dir("xen", NULL);

		if (!d_xen_debug)
			pr_warning("Could not create 'xen' debugfs directory\n");
	}

	return d_xen_debug;
}

