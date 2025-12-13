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

#define PSP_CAPABILITY_PSP_SECURITY_OFFSET	8

struct hsti_request {
	struct psp_req_buffer_hdr header;
	u32 hsti;
} __packed;

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

static int psp_populate_hsti(struct psp_device *psp)
{
	struct hsti_request *req;
	int ret;

	/* Are the security attributes already reported? */
	if (psp->capability.security_reporting)
		return 0;

	/* Allocate command-response buffer */
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->header.payload_size = sizeof(*req);

	ret = psp_send_platform_access_msg(PSP_CMD_HSTI_QUERY, (struct psp_request *)req);
	if (ret)
		goto out;

	if (req->header.status != 0) {
		dev_dbg(psp->dev, "failed to populate HSTI state: %d\n", req->header.status);
		ret = -EINVAL;
		goto out;
	}

	psp->capability.security_reporting = 1;
	psp->capability.raw |= req->hsti << PSP_CAPABILITY_PSP_SECURITY_OFFSET;

out:
	kfree(req);

	return ret;
}

int psp_init_hsti(struct psp_device *psp)
{
	int ret;

	if (PSP_FEATURE(psp, HSTI)) {
		ret = psp_populate_hsti(psp);
		if (ret)
			return ret;
	}

	/*
	 * At this stage, if security information hasn't been populated by
	 * either the PSP or by the driver through the platform command,
	 * then there is nothing more to do.
	 */
	if (!psp->capability.security_reporting)
		return 0;

	if (psp->capability.tsme_status) {
		if (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
			dev_notice(psp->dev, "psp: Both TSME and SME are active, SME is unnecessary when TSME is active.\n");
		else
			dev_notice(psp->dev, "psp: TSME enabled\n");
	}

	return 0;
}
