/*
 * linux/arch/arm/mach-omap1/board-h3-mmc.c
 *
 * Copyright (C) 2007 Instituto Nokia de Tecnologia - INdT
 * Author: Felipe Balbi <felipe.lima@indt.org.br>
 *
 * This code is based on linux/arch/arm/mach-omap2/board-n800-mmc.c, which is:
 * Copyright (C) 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <linux/mfd/tps65010.h>

#include "common.h"
#include "board-h3.h"
#include "mmc.h"

#if IS_ENABLED(CONFIG_MMC_OMAP)

static int mmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	gpio_set_value(H3_TPS_GPIO_MMC_PWR_EN, power_on);
	return 0;
}

/*
 * H3 could use the following functions tested:
 * - mmc_get_cover_state that uses OMAP_MPUIO(1)
 * - mmc_get_wp that maybe uses OMAP_MPUIO(3)
 */
static struct omap_mmc_platform_data mmc1_data = {
	.nr_slots                       = 1,
	.slots[0]       = {
		.set_power              = mmc_set_power,
		.ocr_mask               = MMC_VDD_32_33 | MMC_VDD_33_34,
		.name                   = "mmcblk",
	},
};

static struct omap_mmc_platform_data *mmc_data[OMAP16XX_NR_MMC];

void __init h3_mmc_init(void)
{
	int ret;

	ret = gpio_request(H3_TPS_GPIO_MMC_PWR_EN, "MMC power");
	if (ret < 0)
		return;
	gpio_direction_output(H3_TPS_GPIO_MMC_PWR_EN, 0);

	mmc_data[0] = &mmc1_data;
	omap1_init_mmc(mmc_data, OMAP16XX_NR_MMC);
}

#else

void __init h3_mmc_init(void)
{
}

#endif
