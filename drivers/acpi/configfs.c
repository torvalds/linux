/*
 * ACPI configfs support
 *
 * Copyright (c) 2016 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/configfs.h>
#include <linux/acpi.h>

static struct config_item_type acpi_root_group_type = {
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem acpi_configfs = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "acpi",
			.ci_type = &acpi_root_group_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(acpi_configfs.su_mutex),
};

static int __init acpi_configfs_init(void)
{
	int ret;
	struct config_group *root = &acpi_configfs.su_group;

	config_group_init(root);

	ret = configfs_register_subsystem(&acpi_configfs);
	if (ret)
		return ret;

	return 0;
}
module_init(acpi_configfs_init);

static void __exit acpi_configfs_exit(void)
{
	configfs_unregister_subsystem(&acpi_configfs);
}
module_exit(acpi_configfs_exit);

MODULE_AUTHOR("Octavian Purdila <octavian.purdila@intel.com>");
MODULE_DESCRIPTION("ACPI configfs support");
MODULE_LICENSE("GPL v2");
