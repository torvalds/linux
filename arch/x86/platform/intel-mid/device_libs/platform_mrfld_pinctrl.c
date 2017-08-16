/*
 * Intel Merrifield FLIS platform device initialization file
 *
 * Copyright (C) 2016, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/intel-mid.h>

#define FLIS_BASE_ADDR			0xff0c0000
#define FLIS_LENGTH			0x8000

static struct resource mrfld_pinctrl_mmio_resource = {
	.start		= FLIS_BASE_ADDR,
	.end		= FLIS_BASE_ADDR + FLIS_LENGTH - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device mrfld_pinctrl_device = {
	.name		= "pinctrl-merrifield",
	.id		= PLATFORM_DEVID_NONE,
	.resource	= &mrfld_pinctrl_mmio_resource,
	.num_resources	= 1,
};

static int __init mrfld_pinctrl_init(void)
{
	if (intel_mid_identify_cpu() == INTEL_MID_CPU_CHIP_TANGIER)
		return platform_device_register(&mrfld_pinctrl_device);

	return -ENODEV;
}
arch_initcall(mrfld_pinctrl_init);
