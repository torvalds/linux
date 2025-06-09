// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Hardware Feedback Interface Driver
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Authors: Perry Yuan <Perry.Yuan@amd.com>
 *          Mario Limonciello <mario.limonciello@amd.com>
 */

#define pr_fmt(fmt)  "amd-hfi: " fmt

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/smp.h>

#define AMD_HFI_DRIVER		"amd_hfi"

#define AMD_HETERO_CPUID_27	0x80000027

static struct platform_device *device;

struct amd_hfi_data {
	const char	*name;
	struct device	*dev;
};

struct amd_hfi_classes {
	u32	perf;
	u32	eff;
};

/**
 * struct amd_hfi_cpuinfo - HFI workload class info per CPU
 * @cpu:		CPU index
 * @class_index:	workload class ID index
 * @nr_class:		max number of workload class supported
 * @amd_hfi_classes:	current CPU workload class ranking data
 *
 * Parameters of a logical processor linked with hardware feedback class.
 */
struct amd_hfi_cpuinfo {
	int		cpu;
	s16		class_index;
	u8		nr_class;
	struct amd_hfi_classes	*amd_hfi_classes;
};

static DEFINE_PER_CPU(struct amd_hfi_cpuinfo, amd_hfi_cpuinfo) = {.class_index = -1};

static int amd_hfi_alloc_class_data(struct platform_device *pdev)
{
	struct amd_hfi_cpuinfo *hfi_cpuinfo;
	struct device *dev = &pdev->dev;
	u32 nr_class_id;
	int idx;

	nr_class_id = cpuid_eax(AMD_HETERO_CPUID_27);
	if (nr_class_id > 255) {
		dev_err(dev, "number of supported classes too large: %d\n",
			nr_class_id);
		return -EINVAL;
	}

	for_each_possible_cpu(idx) {
		struct amd_hfi_classes *classes;

		classes = devm_kcalloc(dev,
				       nr_class_id,
				       sizeof(struct amd_hfi_classes),
				       GFP_KERNEL);
		if (!classes)
			return -ENOMEM;
		hfi_cpuinfo = per_cpu_ptr(&amd_hfi_cpuinfo, idx);
		hfi_cpuinfo->amd_hfi_classes = classes;
		hfi_cpuinfo->nr_class = nr_class_id;
	}

	return 0;
}

static const struct acpi_device_id amd_hfi_platform_match[] = {
	{"AMDI0104", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_hfi_platform_match);

static int amd_hfi_probe(struct platform_device *pdev)
{
	struct amd_hfi_data *amd_hfi_data;
	int ret;

	if (!acpi_match_device(amd_hfi_platform_match, &pdev->dev))
		return -ENODEV;

	amd_hfi_data = devm_kzalloc(&pdev->dev, sizeof(*amd_hfi_data), GFP_KERNEL);
	if (!amd_hfi_data)
		return -ENOMEM;

	amd_hfi_data->dev = &pdev->dev;
	platform_set_drvdata(pdev, amd_hfi_data);

	ret = amd_hfi_alloc_class_data(pdev);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver amd_hfi_driver = {
	.driver = {
		.name = AMD_HFI_DRIVER,
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(amd_hfi_platform_match),
	},
	.probe = amd_hfi_probe,
};

static int __init amd_hfi_init(void)
{
	int ret;

	if (acpi_disabled ||
	    !cpu_feature_enabled(X86_FEATURE_AMD_HTR_CORES) ||
	    !cpu_feature_enabled(X86_FEATURE_AMD_WORKLOAD_CLASS))
		return -ENODEV;

	device = platform_device_register_simple(AMD_HFI_DRIVER, -1, NULL, 0);
	if (IS_ERR(device)) {
		pr_err("unable to register HFI platform device\n");
		return PTR_ERR(device);
	}

	ret = platform_driver_register(&amd_hfi_driver);
	if (ret)
		pr_err("failed to register HFI driver\n");

	return ret;
}

static __exit void amd_hfi_exit(void)
{
	platform_driver_unregister(&amd_hfi_driver);
	platform_device_unregister(device);
}
module_init(amd_hfi_init);
module_exit(amd_hfi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Hardware Feedback Interface Driver");
