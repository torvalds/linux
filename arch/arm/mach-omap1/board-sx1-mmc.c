// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/board-sx1-mmc.c
 *
 * Copyright (C) 2007 Instituto Nokia de Tecnologia - INdT
 * Author: Carlos Eduardo Aguiar <carlos.aguiar@indt.org.br>
 *
 * This code is based on linux/arch/arm/mach-omap1/board-h2-mmc.c, which is:
 * Copyright (C) 2007 Instituto Nokia de Tecnologia - INdT
 */

#include <linux/platform_device.h>

#include "hardware.h"
#include "board-sx1.h"
#include "mmc.h"

#if IS_ENABLED(CONFIG_MMC_OMAP)

static int mmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	int err;
	u8 dat = 0;

	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;

	if (power_on)
		dat |= SOFIA_MMC_POWER;
	else
		dat &= ~SOFIA_MMC_POWER;

	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

/* Cover switch is at OMAP_MPUIO(3) */
static struct omap_mmc_platform_data mmc1_data = {
	.nr_slots                       = 1,
	.slots[0]       = {
		.set_power              = mmc_set_power,
		.ocr_mask               = MMC_VDD_32_33 | MMC_VDD_33_34,
		.name                   = "mmcblk",
	},
};

static struct omap_mmc_platform_data *mmc_data[OMAP15XX_NR_MMC];

void __init sx1_mmc_init(void)
{
	mmc_data[0] = &mmc1_data;
	omap1_init_mmc(mmc_data, OMAP15XX_NR_MMC);
}

#else

void __init sx1_mmc_init(void)
{
}

#endif
