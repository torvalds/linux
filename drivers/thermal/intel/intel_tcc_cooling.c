// SPDX-License-Identifier: GPL-2.0-only
/*
 * cooling device driver that activates the processor throttling by
 * programming the TCC Offset register.
 * Copyright (c) 2021, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/intel_tcc.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <asm/cpu_device_id.h>

#define TCC_PROGRAMMABLE	BIT(30)
#define TCC_LOCKED		BIT(31)

static struct thermal_cooling_device *tcc_cdev;

static int tcc_get_max_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	*state = 0x3f;
	return 0;
}

static int tcc_get_cur_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	int offset = intel_tcc_get_offset(-1);

	if (offset < 0)
		return offset;

	*state = offset;
	return 0;
}

static int tcc_set_cur_state(struct thermal_cooling_device *cdev, unsigned long
			     state)
{
	return intel_tcc_set_offset(-1, (int)state);
}

static const struct thermal_cooling_device_ops tcc_cooling_ops = {
	.get_max_state = tcc_get_max_state,
	.get_cur_state = tcc_get_cur_state,
	.set_cur_state = tcc_set_cur_state,
};

static const struct x86_cpu_id tcc_ids[] __initconst = {
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GRACEMONT, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_S, NULL),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, tcc_ids);

static int __init tcc_cooling_init(void)
{
	int ret;
	u64 val;
	const struct x86_cpu_id *id;

	int err;

	id = x86_match_cpu(tcc_ids);
	if (!id)
		return -ENODEV;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, &val);
	if (err)
		return err;

	if (!(val & TCC_PROGRAMMABLE))
		return -ENODEV;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	if (val & TCC_LOCKED) {
		pr_info("TCC Offset locked\n");
		return -ENODEV;
	}

	pr_info("Programmable TCC Offset detected\n");

	tcc_cdev =
	    thermal_cooling_device_register("TCC Offset", NULL,
					    &tcc_cooling_ops);
	if (IS_ERR(tcc_cdev)) {
		ret = PTR_ERR(tcc_cdev);
		return ret;
	}
	return 0;
}

module_init(tcc_cooling_init)

static void __exit tcc_cooling_exit(void)
{
	thermal_cooling_device_unregister(tcc_cdev);
}

module_exit(tcc_cooling_exit)

MODULE_IMPORT_NS(INTEL_TCC);
MODULE_DESCRIPTION("TCC offset cooling device Driver");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL v2");
