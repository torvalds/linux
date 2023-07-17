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
	X86_MATCH(EMERALDRAPIDS_X),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, ifs_cpu_ids);

ATTRIBUTE_GROUPS(plat_ifs);
ATTRIBUTE_GROUPS(plat_ifs_array);

bool *ifs_pkg_auth;

static const struct ifs_test_caps scan_test = {
	.integrity_cap_bit = MSR_INTEGRITY_CAPS_PERIODIC_BIST_BIT,
	.test_num = IFS_TYPE_SAF,
};

static const struct ifs_test_caps array_test = {
	.integrity_cap_bit = MSR_INTEGRITY_CAPS_ARRAY_BIST_BIT,
	.test_num = IFS_TYPE_ARRAY_BIST,
};

static struct ifs_device ifs_devices[] = {
	[IFS_TYPE_SAF] = {
		.test_caps = &scan_test,
		.misc = {
			.name = "intel_ifs_0",
			.minor = MISC_DYNAMIC_MINOR,
			.groups = plat_ifs_groups,
		},
	},
	[IFS_TYPE_ARRAY_BIST] = {
		.test_caps = &array_test,
		.misc = {
			.name = "intel_ifs_1",
			.minor = MISC_DYNAMIC_MINOR,
			.groups = plat_ifs_array_groups,
		},
	},
};

#define IFS_NUMTESTS ARRAY_SIZE(ifs_devices)

static void ifs_cleanup(void)
{
	int i;

	for (i = 0; i < IFS_NUMTESTS; i++) {
		if (ifs_devices[i].misc.this_device)
			misc_deregister(&ifs_devices[i].misc);
	}
	kfree(ifs_pkg_auth);
}

static int __init ifs_init(void)
{
	const struct x86_cpu_id *m;
	u64 msrval;
	int i, ret;

	m = x86_match_cpu(ifs_cpu_ids);
	if (!m)
		return -ENODEV;

	if (rdmsrl_safe(MSR_IA32_CORE_CAPS, &msrval))
		return -ENODEV;

	if (!(msrval & MSR_IA32_CORE_CAPS_INTEGRITY_CAPS))
		return -ENODEV;

	if (rdmsrl_safe(MSR_INTEGRITY_CAPS, &msrval))
		return -ENODEV;

	ifs_pkg_auth = kmalloc_array(topology_max_packages(), sizeof(bool), GFP_KERNEL);
	if (!ifs_pkg_auth)
		return -ENOMEM;

	for (i = 0; i < IFS_NUMTESTS; i++) {
		if (!(msrval & BIT(ifs_devices[i].test_caps->integrity_cap_bit)))
			continue;
		ret = misc_register(&ifs_devices[i].misc);
		if (ret)
			goto err_exit;
	}
	return 0;

err_exit:
	ifs_cleanup();
	return ret;
}

static void __exit ifs_exit(void)
{
	ifs_cleanup();
}

module_init(ifs_init);
module_exit(ifs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel In Field Scan (IFS) device");
