// SPDX-License-Identifier: GPL-2.0
/*
 * ConfigFS support
 *
 * Copyright (C) 2026, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/configfs.h>
#include <linux/export.h>

#include "tb.h"

static const struct config_item_type tb_root_group_type = {
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem tb_configfs = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "thunderbolt",
			.ci_type = &tb_root_group_type,
		},
	},
};

/**
 * tb_configfs_register_group() - Register Thunderbolt ConfigFS group
 * @group: Group to register.
 *
 * Registers the new @group under Thunderbolt subsystem ConfigFS.
 *
 * Return: 0% in case of success, negative errno otherwise.
 */
int tb_configfs_register_group(struct config_group *group)
{
	return configfs_register_group(&tb_configfs.su_group, group);
}
EXPORT_SYMBOL_GPL(tb_configfs_register_group);

/**
 * tb_configfs_unregister_group() - Unregister previously registered group
 * @group: Group to unregister.
 */
void tb_configfs_unregister_group(struct config_group *group)
{
	configfs_unregister_group(group);
}
EXPORT_SYMBOL_GPL(tb_configfs_unregister_group);

int tb_configfs_init(void)
{
	config_group_init(&tb_configfs.su_group);
	mutex_init(&tb_configfs.su_mutex);
	return configfs_register_subsystem(&tb_configfs);
}

void tb_configfs_exit(void)
{
	configfs_unregister_subsystem(&tb_configfs);
}
