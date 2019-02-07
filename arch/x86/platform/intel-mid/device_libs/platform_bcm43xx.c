/*
 * platform_bcm43xx.c: bcm43xx platform data initialization file
 *
 * (C) Copyright 2016 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio/machine.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/sfi.h>

#include <asm/intel-mid.h>

#define WLAN_SFI_GPIO_IRQ_NAME		"WLAN-interrupt"
#define WLAN_SFI_GPIO_ENABLE_NAME	"WLAN-enable"

#define WLAN_DEV_NAME			"0000:00:01.3"

static struct regulator_consumer_supply bcm43xx_vmmc_supply = {
	.dev_name		= WLAN_DEV_NAME,
	.supply			= "vmmc",
};

static struct regulator_init_data bcm43xx_vmmc_data = {
	.constraints = {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &bcm43xx_vmmc_supply,
};

static struct fixed_voltage_config bcm43xx_vmmc = {
	.supply_name		= "bcm43xx-vmmc-regulator",
	/*
	 * Announce 2.0V here to be compatible with SDIO specification. The
	 * real voltage and signaling are still 1.8V.
	 */
	.microvolts		= 2000000,		/* 1.8V */
	.startup_delay		= 250 * 1000,		/* 250ms */
	.enable_high		= 1,			/* active high */
	.enabled_at_boot	= 0,			/* disabled at boot */
	.init_data		= &bcm43xx_vmmc_data,
};

static struct platform_device bcm43xx_vmmc_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= PLATFORM_DEVID_AUTO,
	.dev = {
		.platform_data	= &bcm43xx_vmmc,
	},
};

static struct gpiod_lookup_table bcm43xx_vmmc_gpio_table = {
	.dev_id	= "reg-fixed-voltage.0",
	.table	= {
		GPIO_LOOKUP("0000:00:0c.0", -1, NULL, GPIO_ACTIVE_LOW),
		{}
	},
};

static int __init bcm43xx_regulator_register(void)
{
	struct gpiod_lookup_table *table = &bcm43xx_vmmc_gpio_table;
	struct gpiod_lookup *lookup = table->table;
	int ret;

	lookup[0].chip_hwnum = get_gpio_by_name(WLAN_SFI_GPIO_ENABLE_NAME);
	gpiod_add_lookup_table(table);

	ret = platform_device_register(&bcm43xx_vmmc_regulator);
	if (ret) {
		pr_err("%s: vmmc regulator register failed\n", __func__);
		return ret;
	}

	return 0;
}

static void __init *bcm43xx_platform_data(void *info)
{
	int ret;

	ret = bcm43xx_regulator_register();
	if (ret)
		return NULL;

	pr_info("Using generic wifi platform data\n");

	/* For now it's empty */
	return NULL;
}

static const struct devs_id bcm43xx_clk_vmmc_dev_id __initconst = {
	.name			= "bcm43xx_clk_vmmc",
	.type			= SFI_DEV_TYPE_SD,
	.get_platform_data	= &bcm43xx_platform_data,
};

sfi_device(bcm43xx_clk_vmmc_dev_id);
