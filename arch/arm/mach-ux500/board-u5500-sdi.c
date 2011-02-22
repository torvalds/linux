/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Hanumath Prasad <ulf.hansson@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/amba/mmci.h>
#include <linux/mmc/host.h>
#include <linux/gpio.h>

#include <plat/pincfg.h>
#include <mach/db5500-regs.h>
#include <plat/ste_dma40.h>

#include "pins-db5500.h"
#include "devices-db5500.h"
#include "ste-dma40-db5500.h"

static pin_cfg_t u5500_sdi_pins[] = {
	/* SDI0 (POP eMMC) */
	GPIO5_MC0_DAT0		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO6_MC0_DAT1		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO7_MC0_DAT2		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO8_MC0_DAT3		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO9_MC0_DAT4		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO10_MC0_DAT5		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO11_MC0_DAT6		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO12_MC0_DAT7		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO13_MC0_CMD		| PIN_DIR_INPUT | PIN_PULL_UP,
	GPIO14_MC0_CLK		| PIN_DIR_OUTPUT | PIN_VAL_LOW,
};

static struct mmci_platform_data u5500_sdi0_data = {
	.ocr_mask	= MMC_VDD_165_195,
	.f_max		= 50000000,
	.capabilities	= MMC_CAP_4_BIT_DATA |
				MMC_CAP_8_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED,
	.gpio_cd	= -1,
	.gpio_wp	= -1,
};

void __init u5500_sdi_init(void)
{
	nmk_config_pins(u5500_sdi_pins, ARRAY_SIZE(u5500_sdi_pins));

	db5500_add_sdi0(&u5500_sdi0_data);
}
