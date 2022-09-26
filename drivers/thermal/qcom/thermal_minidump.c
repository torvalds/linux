// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thermal_minidump.h>

#include <trace/events/thermal.h>

/**
 * thermal_minidump_update_data - function to update thermal minidump data
 *
 * @md  :	The pointer of minidump data
 * @type:	sensor type to stored
 * @temp:	temperature to stored
 *
 * This function gives the ability for driver save temperature
 * data into miniudmps
 *
 * Return:
 *		zero on success.
 *		Negative error number on failures.
 */
int thermal_minidump_update_data(struct minidump_data *md,
	char *type, int *temp)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&md->update_md_lock, flags);

	if (md->md_count == MD_NUM)
		md->md_count = 0;

	strscpy(md->type[md->md_count].sensor_type, type,
		THERMAL_NAME_LENGTH);
	md->temp[md->md_count] = *temp;
	md->md_count++;

	ret = msm_minidump_update_region(md->region, &md->md_entry);

	spin_unlock_irqrestore(&md->update_md_lock, flags);

	if (ret < 0)
		pr_err("Failed to update data to minidump, ret:%d\n", ret);

	return ret;
}
EXPORT_SYMBOL(thermal_minidump_update_data);

/**
 * thermal_minidump_register - function to register thermal data region
 * into minidump
 *
 * @name:	The pointer of driver devicetree full name
 *
 * This function gives the ability for driver register an entry in
 * Minidump table
 *
 * Return:
 *		md: The pointer of minidump data.
 *		NULL : thermal minidump register fail.
 */
struct minidump_data *thermal_minidump_register(const char *name)
{
	struct minidump_data *md;

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (!md)
		return NULL;

	strscpy(md->md_entry.name, name, sizeof(md->md_entry.name));
	md->md_entry.virt_addr = (uintptr_t)md;
	md->md_entry.phys_addr = virt_to_phys(md);
	md->md_entry.size = sizeof(*md);
	md->region = msm_minidump_add_region(&md->md_entry);
	if (md->region < 0) {
		kfree(md);
		md = NULL;
		pr_err("Failed to add %s into minidump\n", name);
		goto exit;
	}

	spin_lock_init(&md->update_md_lock);

exit:
	return md;
}
EXPORT_SYMBOL(thermal_minidump_register);

/**
 * thermal_minidump_unregister - function to unregister thermal data region
 * in minidump
 *
 * @md  :	The pointer of minidump data
 *
 * This function gives the ability for driver unregister an entry in
 * Minidump table
 */
void thermal_minidump_unregister(struct minidump_data *md)
{
	if (md) {
		msm_minidump_remove_region(&md->md_entry);
		kfree(md);
	}
}
EXPORT_SYMBOL(thermal_minidump_unregister);

MODULE_DESCRIPTION("Qualcomm Technologies Inc. Thermal minidump driver");
MODULE_LICENSE("GPL");
