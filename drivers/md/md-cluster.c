/*
 * Copyright (C) 2015, SUSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 */


#include <linux/module.h>

static int __init cluster_init(void)
{
	pr_warn("md-cluster: EXPERIMENTAL. Use with caution\n");
	pr_info("Registering Cluster MD functions\n");
	return 0;
}

static void cluster_exit(void)
{
}

module_init(cluster_init);
module_exit(cluster_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clustering support for MD");
