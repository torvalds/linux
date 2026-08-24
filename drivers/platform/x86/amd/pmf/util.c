// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Util Layer
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sanket Goswami <Sanket.Goswami@amd.com>
 */

#include <linux/amd-pmf.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "pmf.h"

static struct amd_pmf_dev *pmf_dev_handle;
static DEFINE_MUTEX(pmf_util_lock);

static int amd_pmf_populate_data(struct amd_pmf_dev *pdev, struct amd_pmf_info *info)
{
	struct ta_pmf_shared_memory *ta_sm = NULL;
	struct ta_pmf_enact_table *in = NULL;
	int idx;

	if (!pdev || !info)
		return -EINVAL;

	if (!pdev->shbuf)
		return -EINVAL;

	ta_sm = pdev->shbuf;
	in = &ta_sm->pmf_input.enact_table;

	/* Set size */
	info->size = sizeof(*info);

	/* PMF Feature support flags */
	if (is_apmf_func_supported(pdev, APMF_FUNC_AUTO_MODE))
		info->features_supported |= AMD_PMF_FEAT_AUTO_MODE;
	if (is_apmf_func_supported(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		info->features_supported |= AMD_PMF_FEAT_STATIC_POWER_SLIDER;
	if (pdev->smart_pc_enabled)
		info->features_supported |= AMD_PMF_FEAT_POLICY_BUILDER;
	if (is_apmf_func_supported(pdev, APMF_FUNC_DYN_SLIDER_AC))
		info->features_supported |= AMD_PMF_FEAT_DYNAMIC_POWER_SLIDER_AC;
	if (is_apmf_func_supported(pdev, APMF_FUNC_DYN_SLIDER_DC))
		info->features_supported |= AMD_PMF_FEAT_DYNAMIC_POWER_SLIDER_DC;

	/* Device States */
	info->platform_type = in->ev_info.platform_type;
	info->laptop_placement = in->ev_info.device_state;
	info->lid_state = in->ev_info.lid_state;
	info->user_presence = in->ev_info.user_present;
	info->slider_position = in->ev_info.power_slider;

	/* Thermal and Power Metrics */
	info->power_source = in->ev_info.power_source;
	info->skin_temp = in->ev_info.skin_temperature;
	info->gfx_busy = in->ev_info.gfx_busy;
	info->ambient_light = in->ev_info.ambient_light;
	info->avg_c0_residency = in->ev_info.avg_c0residency;
	info->max_c0_residency = in->ev_info.max_c0residency;
	info->socket_power = in->ev_info.socket_power;

	/* Custom BIOS input parameters */
	for (idx = 0; idx < AMD_PMF_BIOS_PARAMS_MAX; idx++)
		info->bios_input[idx] = amd_pmf_get_ta_custom_bios_inputs(in, idx);

	/* BIOS output parameters */
	for (idx = 0; idx < AMD_PMF_BIOS_PARAMS_MAX; idx++)
		info->bios_output[idx] = pdev->bios_output[idx];

	return 0;
}

static long amd_pmf_set_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct amd_pmf_dev *pdev = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct amd_pmf_info info = {};
	size_t copy_size;
	__u64 user_size;
	int ret;

	if (cmd != IOCTL_AMD_PMF_POPULATE_DATA)
		return -ENOTTY;

	/* First read just the size field from userspace */
	if (copy_from_user(&user_size, argp, sizeof(user_size)))
		return -EFAULT;

	guard(mutex)(&pmf_util_lock);
	ret = amd_pmf_populate_data(pdev, &info);
	if (ret)
		return ret;

	copy_size = min_t(size_t, user_size, sizeof(info));

	/* Set actual size being copied */
	info.size = copy_size;

	if (copy_to_user(argp, &info, copy_size))
		return -EFAULT;

	return 0;
}

static int amd_pmf_open(struct inode *inode, struct file *filp)
{
	guard(mutex)(&pmf_util_lock);
	if (!pmf_dev_handle)
		return -ENODEV;

	filp->private_data = pmf_dev_handle;
	return 0;
}

static const struct file_operations pmf_if_ops = {
	.owner          = THIS_MODULE,
	.open           = amd_pmf_open,
	.unlocked_ioctl = amd_pmf_set_ioctl,
};

static struct miscdevice amd_pmf_util_if = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "amdpmf_interface",
	.fops		= &pmf_if_ops,
};

int amd_pmf_cdev_register(struct amd_pmf_dev *dev)
{
	int ret;

	guard(mutex)(&pmf_util_lock);
	pmf_dev_handle = dev;
	ret = misc_register(&amd_pmf_util_if);
	if (ret)
		pmf_dev_handle = NULL;

	return ret;
}

void amd_pmf_cdev_unregister(void)
{
	guard(mutex)(&pmf_util_lock);
	if (pmf_dev_handle)
		misc_deregister(&amd_pmf_util_if);
	pmf_dev_handle = NULL;
}
