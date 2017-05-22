/*
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/mutex.h>
#include <linux/notifier.h>

static unsigned long system_status;
static unsigned long ref_count[32] = {0};

static DEFINE_MUTEX(system_status_mutex);

static BLOCKING_NOTIFIER_HEAD(system_status_notifier_list);

int rockchip_register_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&system_status_notifier_list,
						nb);
}
EXPORT_SYMBOL_GPL(rockchip_register_system_status_notifier);

int rockchip_unregister_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&system_status_notifier_list,
						  nb);
}
EXPORT_SYMBOL_GPL(rockchip_unregister_system_status_notifier);

static int rockchip_system_status_notifier_call_chain(unsigned long val)
{
	int ret = blocking_notifier_call_chain(&system_status_notifier_list,
					       val, NULL);

	return notifier_to_errno(ret);
}

void rockchip_set_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_status_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_status_offset = fls(status) - 1;
		status &= ~(1 << single_status_offset);
		if (ref_count[single_status_offset] == 0)
			system_status |= 1 << single_status_offset;
		ref_count[single_status_offset]++;
	}

	if (old_system_status != system_status)
		rockchip_system_status_notifier_call_chain(system_status);

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL_GPL(rockchip_set_system_status);

void rockchip_clear_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_status_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_status_offset = fls(status) - 1;
		status &= ~(1 << single_status_offset);
		if (ref_count[single_status_offset] == 0) {
			continue;
		} else {
			if (ref_count[single_status_offset] == 1)
				system_status &= ~(1 << single_status_offset);
			ref_count[single_status_offset]--;
		}
	}

	if (old_system_status != system_status)
		rockchip_system_status_notifier_call_chain(system_status);

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL_GPL(rockchip_clear_system_status);

unsigned long rockchip_get_system_status(void)
{
	return system_status;
}
EXPORT_SYMBOL_GPL(rockchip_get_system_status);
