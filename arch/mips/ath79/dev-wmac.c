/*
 *  Atheros AR913X/AR933X SoC built-in WMAC device support
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros 2.6.15/2.6.31 BSP
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
		/* .start and .end fields are filled dynamically */
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
	ath79_wmac_resources[1].start = ATH79_CPU_IRQ(2);
	ath79_wmac_resources[1].end = ATH79_CPU_IRQ(2);
}


static int ar933x_wmac_reset(void)
{
	ath79_device_reset_set(AR933X_RESET_WMAC);
	ath79_device_reset_clear(AR933X_RESET_WMAC);

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
	ath79_wmac_resources[1].start = ATH79_CPU_IRQ(2);
	ath79_wmac_resources[1].end = ATH79_CPU_IRQ(2);

	t = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		ath79_wmac_data.is_clk_25mhz = false;
	else
		ath79_wmac_data.is_clk_25mhz = true;

	if (ath79_soc_rev == 1)
		ath79_wmac_data.get_mac_revision = ar933x_r1_get_wmac_revision;

	ath79_wmac_data.external_reset = ar933x_wmac_reset;
}

static void ar934x_wmac_setup(void)
{
	u32 t;

	ath79_wmac_device.name = "ar934x_wmac";

	ath79_wmac_resources[0].start = AR934X_WMAC_BASE;
	ath79_wmac_resources[0].end = AR934X_WMAC_BASE + AR934X_WMAC_SIZE - 1;
	ath79_wmac_resources[1].start = ATH79_IP2_IRQ(1);
	ath79_wmac_resources[1].end = ATH79_IP2_IRQ(1);

	t = ath79_reset_rr(AR934X_RESET_REG_BOOTSTRAP);
	if (t & AR934X_BOOTSTRAP_REF_CLK_40)
		ath79_wmac_data.is_clk_25mhz = false;
	else
		ath79_wmac_data.is_clk_25mhz = true;
}

static void qca955x_wmac_setup(void)
{
	u32 t;

	ath79_wmac_device.name = "qca955x_wmac";

	ath79_wmac_resources[0].start = QCA955X_WMAC_BASE;
	ath79_wmac_resources[0].end = QCA955X_WMAC_BASE + QCA955X_WMAC_SIZE - 1;
	ath79_wmac_resources[1].start = ATH79_IP2_IRQ(1);
	ath79_wmac_resources[1].end = ATH79_IP2_IRQ(1);

	t = ath79_reset_rr(QCA955X_RESET_REG_BOOTSTRAP);
	if (t & QCA955X_BOOTSTRAP_REF_CLK_40)
		ath79_wmac_data.is_clk_25mhz = false;
	else
		ath79_wmac_data.is_clk_25mhz = true;
}

void __init ath79_register_wmac(u8 *cal_data)
{
	if (soc_is_ar913x())
		ar913x_wmac_setup();
	else if (soc_is_ar933x())
		ar933x_wmac_setup();
	else if (soc_is_ar934x())
		ar934x_wmac_setup();
	else if (soc_is_qca955x())
		qca955x_wmac_setup();
	else
		BUG();

	if (cal_data)
		memcpy(ath79_wmac_data.eeprom_data, cal_data,
		       sizeof(ath79_wmac_data.eeprom_data));

	platform_device_register(&ath79_wmac_device);
}
