/*
 * linux/arch/arm/mach-omap2/gpmc-smsc911x.c
 *
 * Copyright (C) 2009 Li-Pro.Net
 * Stephan Linz <linz@li-pro.net>
 *
 * Modified from linux/arch/arm/mach-omap2/gpmc-smc91x.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/smsc911x.h>

#include <plat/board.h>
#include <plat/gpmc.h>
#include <plat/gpmc-smsc911x.h>

static struct omap_smsc911x_platform_data *gpmc_cfg;

static struct resource gpmc_smsc911x_resources[] = {
	[0] = {
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config gpmc_smsc911x_config = {
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_16BIT,
};

static struct platform_device gpmc_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(gpmc_smsc911x_resources),
	.resource	= gpmc_smsc911x_resources,
	.dev		= {
		.platform_data = &gpmc_smsc911x_config,
	},
};

/*
 * Initialize smsc911x device connected to the GPMC. Note that we
 * assume that pin multiplexing is done in the board-*.c file,
 * or in the bootloader.
 */
void __init gpmc_smsc911x_init(struct omap_smsc911x_platform_data *board_data)
{
	unsigned long cs_mem_base;
	int ret;

	gpmc_cfg = board_data;

	if (gpmc_cs_request(gpmc_cfg->cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smsc911x\n");
		return;
	}

	gpmc_smsc911x_resources[0].start = cs_mem_base + 0x0;
	gpmc_smsc911x_resources[0].end = cs_mem_base + 0xff;

	if (gpio_request(gpmc_cfg->gpio_irq, "smsc911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smsc911x IRQ\n",
				gpmc_cfg->gpio_irq);
		goto free1;
	}

	gpio_direction_input(gpmc_cfg->gpio_irq);
	gpmc_smsc911x_resources[1].start = gpio_to_irq(gpmc_cfg->gpio_irq);
	gpmc_smsc911x_resources[1].flags |=
					(gpmc_cfg->flags & IRQF_TRIGGER_MASK);

	if (gpio_is_valid(gpmc_cfg->gpio_reset)) {
		ret = gpio_request(gpmc_cfg->gpio_reset, "smsc911x reset");
		if (ret) {
			printk(KERN_ERR "Failed to request GPIO%d for smsc911x reset\n",
					gpmc_cfg->gpio_reset);
			goto free2;
		}

		gpio_direction_output(gpmc_cfg->gpio_reset, 1);
		gpio_set_value(gpmc_cfg->gpio_reset, 0);
		msleep(100);
		gpio_set_value(gpmc_cfg->gpio_reset, 1);
	}

	if (platform_device_register(&gpmc_smsc911x_device) < 0) {
		printk(KERN_ERR "Unable to register smsc911x device\n");
		gpio_free(gpmc_cfg->gpio_reset);
		goto free2;
	}

	return;

free2:
	gpio_free(gpmc_cfg->gpio_irq);
free1:
	gpmc_cs_free(gpmc_cfg->cs);

	printk(KERN_ERR "Could not initialize smsc911x\n");
}
