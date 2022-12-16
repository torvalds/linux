// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

#include <asm/cpu_device_id.h>

#include "ifs.h"

#define X86_MATCH(model)				\
	X86_MATCH_VENDOR_FAM_MODEL_FEATURE(INTEL, 6,	\
		INTEL_FAM6_##model, X86_FEATURE_CORE_CAPABILITIES, NULL)

static const struct x86_cpu_id ifs_cpu_ids[] __initconst = {
	X86_MATCH(SAPPHIRERAPIDS_X),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, ifs_cpu_ids);

static struct ifs_device ifs_device = {
	.data = {
		.integrity_cap_bit = MSR_INTEGRITY_CAPS_PERIODIC_BIST_BIT,
		.test_num = 0,
	},
	.misc = {
		.name = "intel_ifs_0",
		.nodename = "intel_ifs/0",
		.minor = MISC_DYNAMIC_MINOR,
	},
};

static int __init ifs_init(void)
{
	const struct x86_cpu_id *m;
	u64 msrval;
	int ret;

	m = x86_match_cpu(ifs_cpu_ids);
	if (!m)
		return -ENODEV;

	if (rdmsrl_safe(MSR_IA32_CORE_CAPS, &msrval))
		return -ENODEV;

	if (!(msrval & MSR_IA32_CORE_CAPS_INTEGRITY_CAPS))
		return -ENODEV;

	if (rdmsrl_safe(MSR_INTEGRITY_CAPS, &msrval))
		return -ENODEV;

	ifs_device.misc.groups = ifs_get_groups();

	if (!(msrval & BIT(ifs_device.data.integrity_cap_bit)))
		return -ENODEV;

	ifs_device.data.pkg_auth = kmalloc_array(topology_max_packages(), sizeof(bool), GFP_KERNEL);
	if (!ifs_device.data.pkg_auth)
		return -ENOMEM;

	ret = misc_register(&ifs_device.misc);
	if (ret) {
		kfree(ifs_device.data.pkg_auth);
		return ret;
	}

	return 0;
}

static void __exit ifs_exit(void)
{
	misc_deregister(&ifs_device.misc);
	kfree(ifs_device.data.pkg_auth);
}

module_init(ifs_init);
module_exit(ifs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel In Field Scan (IFS) device");
