// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Merrifield power button support
 *
 * (C) Copyright 2017 Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/sfi.h>

#include <asm/intel-mid.h>
#include <asm/intel_scu_ipc.h>

static struct resource mrfld_power_btn_resources[] = {
	{
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mrfld_power_btn_dev = {
	.name		= "msic_power_btn",
	.id		= PLATFORM_DEVID_NONE,
	.num_resources	= ARRAY_SIZE(mrfld_power_btn_resources),
	.resource	= mrfld_power_btn_resources,
};

static int mrfld_power_btn_scu_status_change(struct notifier_block *nb,
					     unsigned long code, void *data)
{
	if (code == SCU_DOWN) {
		platform_device_unregister(&mrfld_power_btn_dev);
		return 0;
	}

	return platform_device_register(&mrfld_power_btn_dev);
}

static struct notifier_block mrfld_power_btn_scu_notifier = {
	.notifier_call	= mrfld_power_btn_scu_status_change,
};

static int __init register_mrfld_power_btn(void)
{
	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	/*
	 * We need to be sure that the SCU IPC is ready before
	 * PMIC power button device can be registered:
	 */
	intel_scu_notifier_add(&mrfld_power_btn_scu_notifier);

	return 0;
}
arch_initcall(register_mrfld_power_btn);

static void __init *mrfld_power_btn_platform_data(void *info)
{
	struct resource *res = mrfld_power_btn_resources;
	struct sfi_device_table_entry *pentry = info;

	res->start = res->end = pentry->irq;
	return NULL;
}

static const struct devs_id mrfld_power_btn_dev_id __initconst = {
	.name			= "bcove_power_btn",
	.type			= SFI_DEV_TYPE_IPC,
	.delay			= 1,
	.msic			= 1,
	.get_platform_data	= &mrfld_power_btn_platform_data,
};

sfi_device(mrfld_power_btn_dev_id);
