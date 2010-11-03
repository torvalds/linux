/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Hanumath Prasad <hanumath.prasad@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>

#include <plat/pincfg.h>
#include <mach/devices.h>
#include <mach/hardware.h>

#include "pins-db8500.h"
#include "board-mop500.h"

static pin_cfg_t mop500_sdi_pins[] = {
	/* SDI4 (on-board eMMC) */
	GPIO197_MC4_DAT3,
	GPIO198_MC4_DAT2,
	GPIO199_MC4_DAT1,
	GPIO200_MC4_DAT0,
	GPIO201_MC4_CMD,
	GPIO202_MC4_FBCLK,
	GPIO203_MC4_CLK,
	GPIO204_MC4_DAT7,
	GPIO205_MC4_DAT6,
	GPIO206_MC4_DAT5,
	GPIO207_MC4_DAT4,
};

static pin_cfg_t mop500_sdi2_pins[] = {
	/* SDI2 (POP eMMC) */
	GPIO128_MC2_CLK,
	GPIO129_MC2_CMD,
	GPIO130_MC2_FBCLK,
	GPIO131_MC2_DAT0,
	GPIO132_MC2_DAT1,
	GPIO133_MC2_DAT2,
	GPIO134_MC2_DAT3,
	GPIO135_MC2_DAT4,
	GPIO136_MC2_DAT5,
	GPIO137_MC2_DAT6,
	GPIO138_MC2_DAT7,
};

/*
 * SDI 2 (POP eMMC, not on DB8500ed)
 */

static struct mmci_platform_data mop500_sdi2_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
};

/*
 * SDI 4 (on-board eMMC)
 */

static struct mmci_platform_data mop500_sdi4_data = {
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA |
			  MMC_CAP_MMC_HIGHSPEED,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
};

void mop500_sdi_init(void)
{
	nmk_config_pins(mop500_sdi_pins, ARRAY_SIZE(mop500_sdi_pins));

	u8500_sdi2_device.dev.platform_data = &mop500_sdi2_data;
	u8500_sdi4_device.dev.platform_data = &mop500_sdi4_data;

	if (!cpu_is_u8500ed()) {
		nmk_config_pins(mop500_sdi2_pins, ARRAY_SIZE(mop500_sdi2_pins));
		amba_device_register(&u8500_sdi2_device, &iomem_resource);
	}

	/* On-board eMMC */
	amba_device_register(&u8500_sdi4_device, &iomem_resource);
}
