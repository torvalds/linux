// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. */

#include <linux/module.h>
#include <linux/kdev_t.h>

#include <asm/cpu_device_id.h>

#define X86_MATCH(model)				\
	X86_MATCH_VENDOR_FAM_MODEL_FEATURE(INTEL, 6,	\
		INTEL_FAM6_##model, X86_FEATURE_CORE_CAPABILITIES, NULL)

static const struct x86_cpu_id ifs_cpu_ids[] __initconst = {
	X86_MATCH(SAPPHIRERAPIDS_X),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, ifs_cpu_ids);

static int __init ifs_init(void)
{
	const struct x86_cpu_id *m;
	u64 msrval;

	m = x86_match_cpu(ifs_cpu_ids);
	if (!m)
		return -ENODEV;

	if (rdmsrl_safe(MSR_IA32_CORE_CAPS, &msrval))
		return -ENODEV;

	if (!(msrval & MSR_IA32_CORE_CAPS_INTEGRITY_CAPS))
		return -ENODEV;

	if (rdmsrl_safe(MSR_INTEGRITY_CAPS, &msrval))
		return -ENODEV;

	return 0;
}

static void __exit ifs_exit(void)
{
}

module_init(ifs_init);
module_exit(ifs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel In Field Scan (IFS) device");
