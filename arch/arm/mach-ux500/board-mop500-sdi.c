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
	/* SDI0 (MicroSD slot) */
	GPIO18_MC0_CMDDIR,
	GPIO19_MC0_DAT0DIR,
	GPIO20_MC0_DAT2DIR,
	GPIO21_MC0_DAT31DIR,
	GPIO22_MC0_FBCLK,
	GPIO23_MC0_CLK,
	GPIO24_MC0_CMD,
	GPIO25_MC0_DAT0,
	GPIO26_MC0_DAT1,
	GPIO27_MC0_DAT2,
	GPIO28_MC0_DAT3,

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
 * SDI 0 (MicroSD slot)
 */

/* MMCIPOWER bits */
#define MCI_DATA2DIREN		(1 << 2)
#define MCI_CMDDIREN		(1 << 3)
#define MCI_DATA0DIREN		(1 << 4)
#define MCI_DATA31DIREN		(1 << 5)
#define MCI_FBCLKEN		(1 << 7)

static u32 mop500_sdi0_vdd_handler(struct device *dev, unsigned int vdd,
				   unsigned char power_mode)
{
	if (power_mode == MMC_POWER_UP)
		gpio_set_value(GPIO_SDMMC_EN, 1);
	else if (power_mode == MMC_POWER_OFF)
		gpio_set_value(GPIO_SDMMC_EN, 0);

	return MCI_FBCLKEN | MCI_CMDDIREN | MCI_DATA0DIREN |
	       MCI_DATA2DIREN | MCI_DATA31DIREN;
}

static struct mmci_platform_data mop500_sdi0_data = {
	.vdd_handler	= mop500_sdi0_vdd_handler,
	.ocr_mask	= MMC_VDD_29_30,
	.f_max		= 100000000,
	.capabilities	= MMC_CAP_4_BIT_DATA,
	.gpio_cd	= GPIO_SDMMC_CD,
	.gpio_wp	= -1,
};

void mop500_sdi_tc35892_init(void)
{
	int ret;

	ret = gpio_request(GPIO_SDMMC_EN, "SDMMC_EN");
	if (!ret)
		ret = gpio_request(GPIO_SDMMC_1V8_3V_SEL,
				   "GPIO_SDMMC_1V8_3V_SEL");
	if (ret)
		return;

	gpio_direction_output(GPIO_SDMMC_1V8_3V_SEL, 1);
	gpio_direction_output(GPIO_SDMMC_EN, 0);

	amba_device_register(&u8500_sdi0_device, &iomem_resource);
}

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

	u8500_sdi0_device.dev.platform_data = &mop500_sdi0_data;
	u8500_sdi2_device.dev.platform_data = &mop500_sdi2_data;
	u8500_sdi4_device.dev.platform_data = &mop500_sdi4_data;

	if (!cpu_is_u8500ed()) {
		nmk_config_pins(mop500_sdi2_pins, ARRAY_SIZE(mop500_sdi2_pins));
		amba_device_register(&u8500_sdi2_device, &iomem_resource);
	}

	/* On-board eMMC */
	amba_device_register(&u8500_sdi4_device, &iomem_resource);
}
