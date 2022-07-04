// SPDX-License-Identifier: GPL-2.0
/*
 * Ultravisor high level interfaces
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/of_fdt.h>
#include <linux/of.h>

#include <asm/ultravisor.h>
#include <asm/firmware.h>
#include <asm/machdep.h>

#include "powernv.h"

static struct kobject *ultravisor_kobj;

int __init early_init_dt_scan_ultravisor(unsigned long node, const char *uname,
					 int depth, void *data)
{
	if (!of_flat_dt_is_compatible(node, "ibm,ultravisor"))
		return 0;

	powerpc_firmware_features |= FW_FEATURE_ULTRAVISOR;
	pr_debug("Ultravisor detected!\n");
	return 1;
}

static struct memcons *uv_memcons;

static ssize_t uv_msglog_read(struct file *file, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *to,
			      loff_t pos, size_t count)
{
	return memcons_copy(uv_memcons, to, pos, count);
}

static struct bin_attribute uv_msglog_attr = {
	.attr = {.name = "msglog", .mode = 0400},
	.read = uv_msglog_read
};

static int __init uv_init(void)
{
	struct device_node *node;

	if (!firmware_has_feature(FW_FEATURE_ULTRAVISOR))
		return 0;

	node = of_find_compatible_node(NULL, NULL, "ibm,uv-firmware");
	if (!node)
		return -ENODEV;

	uv_memcons = memcons_init(node, "memcons");
	of_node_put(node);
	if (!uv_memcons)
		return -ENOENT;

	uv_msglog_attr.size = memcons_get_size(uv_memcons);

	ultravisor_kobj = kobject_create_and_add("ultravisor", firmware_kobj);
	if (!ultravisor_kobj)
		return -ENOMEM;

	return sysfs_create_bin_file(ultravisor_kobj, &uv_msglog_attr);
}
machine_subsys_initcall(powernv, uv_init);
