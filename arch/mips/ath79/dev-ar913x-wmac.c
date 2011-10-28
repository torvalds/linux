/*
 *  Atheros AR913X SoC built-in WMAC device support
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "dev-ar913x-wmac.h"

static struct ath9k_platform_data ar913x_wmac_data;

static struct resource ar913x_wmac_resources[] = {
	{
		.start	= AR913X_WMAC_BASE,
		.end	= AR913X_WMAC_BASE + AR913X_WMAC_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= ATH79_CPU_IRQ_IP2,
		.end	= ATH79_CPU_IRQ_IP2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ar913x_wmac_device = {
	.name		= "ath9k",
	.id		= -1,
	.resource	= ar913x_wmac_resources,
	.num_resources	= ARRAY_SIZE(ar913x_wmac_resources),
	.dev = {
		.platform_data = &ar913x_wmac_data,
	},
};

void __init ath79_register_ar913x_wmac(u8 *cal_data)
{
	if (cal_data)
		memcpy(ar913x_wmac_data.eeprom_data, cal_data,
		       sizeof(ar913x_wmac_data.eeprom_data));

	/* reset the WMAC */
	ath79_device_reset_set(AR913X_RESET_AMBA2WMAC);
	mdelay(10);

	ath79_device_reset_clear(AR913X_RESET_AMBA2WMAC);
	mdelay(10);

	platform_device_register(&ar913x_wmac_device);
}
