/*
 *  Atheros AR913X/AR933X SoC built-in WMAC device support
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
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
#include "dev-wmac.h"

static struct ath9k_platform_data ath79_wmac_data;

static struct resource ath79_wmac_resources[] = {
	{
		/* .start and .end fields are filled dynamically */
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= ATH79_CPU_IRQ_IP2,
		.end	= ATH79_CPU_IRQ_IP2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ath79_wmac_device = {
	.name		= "ath9k",
	.id		= -1,
	.resource	= ath79_wmac_resources,
	.num_resources	= ARRAY_SIZE(ath79_wmac_resources),
	.dev = {
		.platform_data = &ath79_wmac_data,
	},
};

static void __init ar913x_wmac_setup(void)
{
	/* reset the WMAC */
	ath79_device_reset_set(AR913X_RESET_AMBA2WMAC);
	mdelay(10);

	ath79_device_reset_clear(AR913X_RESET_AMBA2WMAC);
	mdelay(10);

	ath79_wmac_resources[0].start = AR913X_WMAC_BASE;
	ath79_wmac_resources[0].end = AR913X_WMAC_BASE + AR913X_WMAC_SIZE - 1;
}


static int ar933x_wmac_reset(void)
{
	ath79_device_reset_clear(AR933X_RESET_WMAC);
	ath79_device_reset_set(AR933X_RESET_WMAC);

	return 0;
}

static int ar933x_r1_get_wmac_revision(void)
{
	return ath79_soc_rev;
}

static void __init ar933x_wmac_setup(void)
{
	u32 t;

	ar933x_wmac_reset();

	ath79_wmac_device.name = "ar933x_wmac";

	ath79_wmac_resources[0].start = AR933X_WMAC_BASE;
	ath79_wmac_resources[0].end = AR933X_WMAC_BASE + AR933X_WMAC_SIZE - 1;

	t = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		ath79_wmac_data.is_clk_25mhz = false;
	else
		ath79_wmac_data.is_clk_25mhz = true;

	if (ath79_soc_rev == 1)
		ath79_wmac_data.get_mac_revision = ar933x_r1_get_wmac_revision;

	ath79_wmac_data.external_reset = ar933x_wmac_reset;
}

void __init ath79_register_wmac(u8 *cal_data)
{
	if (soc_is_ar913x())
		ar913x_wmac_setup();
	else if (soc_is_ar933x())
		ar933x_wmac_setup();
	else
		BUG();

	if (cal_data)
		memcpy(ath79_wmac_data.eeprom_data, cal_data,
		       sizeof(ath79_wmac_data.eeprom_data));

	platform_device_register(&ath79_wmac_device);
}
