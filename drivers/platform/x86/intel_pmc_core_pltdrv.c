// SPDX-License-Identifier: GPL-2.0

/*
 * Intel PMC Core platform init
 * Copyright (c) 2019, Google Inc.
 * Author - Rajat Jain
 *
 * This code instantiates platform devices for intel_pmc_core driver, only
 * on supported platforms that may not have the ACPI devices in the ACPI tables.
 * No new platforms should be added here, because we expect that new platforms
 * should all have the ACPI device, which is the preferred way of enumeration.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

static void intel_pmc_core_release(struct device *dev)
{
	/* Nothing to do. */
}

static struct platform_device pmc_core_device = {
	.name = "intel_pmc_core",
	.dev  = {
		.release = intel_pmc_core_release,
	},
};

/*
 * intel_pmc_core_platform_ids is the list of platforms where we want to
 * instantiate the platform_device if not already instantiated. This is
 * different than intel_pmc_core_ids in intel_pmc_core.c which is the
 * list of platforms that the driver supports for pmc_core device. The
 * other list may grow, but this list should not.
 */
static const struct x86_cpu_id intel_pmc_core_platform_ids[] = {
	INTEL_CPU_FAM6(SKYLAKE_MOBILE, pmc_core_device),
	INTEL_CPU_FAM6(SKYLAKE_DESKTOP, pmc_core_device),
	INTEL_CPU_FAM6(KABYLAKE_MOBILE, pmc_core_device),
	INTEL_CPU_FAM6(KABYLAKE_DESKTOP, pmc_core_device),
	INTEL_CPU_FAM6(CANNONLAKE_MOBILE, pmc_core_device),
	INTEL_CPU_FAM6(ICELAKE_MOBILE, pmc_core_device),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_platform_ids);

static int __init pmc_core_platform_init(void)
{
	/* Skip creating the platform device if ACPI already has a device */
	if (acpi_dev_present("INT33A1", NULL, -1))
		return -ENODEV;

	if (!x86_match_cpu(intel_pmc_core_platform_ids))
		return -ENODEV;

	return platform_device_register(&pmc_core_device);
}

static void __exit pmc_core_platform_exit(void)
{
	platform_device_unregister(&pmc_core_device);
}

module_init(pmc_core_platform_init);
module_exit(pmc_core_platform_exit);
MODULE_LICENSE("GPL v2");
