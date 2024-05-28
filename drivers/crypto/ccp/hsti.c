// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Processor device driver, security attributes
 *
 * Copyright (C) 2023-2024 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#include <linux/device.h>

#include "psp-dev.h"
#include "hsti.h"

#define security_attribute_show(name)						\
static ssize_t name##_show(struct device *d, struct device_attribute *attr,	\
			   char *buf)						\
{										\
	struct sp_device *sp = dev_get_drvdata(d);				\
	struct psp_device *psp = sp->psp_data;					\
	return sysfs_emit(buf, "%d\n", psp->capability.name);		\
}

security_attribute_show(fused_part)
static DEVICE_ATTR_RO(fused_part);
security_attribute_show(debug_lock_on)
static DEVICE_ATTR_RO(debug_lock_on);
security_attribute_show(tsme_status)
static DEVICE_ATTR_RO(tsme_status);
security_attribute_show(anti_rollback_status)
static DEVICE_ATTR_RO(anti_rollback_status);
security_attribute_show(rpmc_production_enabled)
static DEVICE_ATTR_RO(rpmc_production_enabled);
security_attribute_show(rpmc_spirom_available)
static DEVICE_ATTR_RO(rpmc_spirom_available);
security_attribute_show(hsp_tpm_available)
static DEVICE_ATTR_RO(hsp_tpm_available);
security_attribute_show(rom_armor_enforced)
static DEVICE_ATTR_RO(rom_armor_enforced);

static struct attribute *psp_security_attrs[] = {
	&dev_attr_fused_part.attr,
	&dev_attr_debug_lock_on.attr,
	&dev_attr_tsme_status.attr,
	&dev_attr_anti_rollback_status.attr,
	&dev_attr_rpmc_production_enabled.attr,
	&dev_attr_rpmc_spirom_available.attr,
	&dev_attr_hsp_tpm_available.attr,
	&dev_attr_rom_armor_enforced.attr,
	NULL
};

static umode_t psp_security_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sp_device *sp = dev_get_drvdata(dev);
	struct psp_device *psp = sp->psp_data;

	if (psp && psp->capability.security_reporting)
		return 0444;

	return 0;
}

struct attribute_group psp_security_attr_group = {
	.attrs = psp_security_attrs,
	.is_visible = psp_security_is_visible,
};
