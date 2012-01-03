/*
 *  linux/arch/arm/mach-mmp/brownstone.c
 *
 *  Support for the Marvell Brownstone Development Platform.
 *
 *  Copyright (C) 2009-2010 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max8649.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8925.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/addr-map.h>
#include <mach/mfp-mmp2.h>
#include <mach/mmp2.h>
#include <mach/irqs.h>

#include "common.h"

#define BROWNSTONE_NR_IRQS	(MMP_NR_IRQS + 40)

#define GPIO_5V_ENABLE		(89)

static unsigned long brownstone_pin_config[] __initdata = {
	/* UART1 */
	GPIO29_UART1_RXD,
	GPIO30_UART1_TXD,

	/* UART3 */
	GPIO51_UART3_RXD,
	GPIO52_UART3_TXD,

	/* DFI */
	GPIO168_DFI_D0,
	GPIO167_DFI_D1,
	GPIO166_DFI_D2,
	GPIO165_DFI_D3,
	GPIO107_DFI_D4,
	GPIO106_DFI_D5,
	GPIO105_DFI_D6,
	GPIO104_DFI_D7,
	GPIO111_DFI_D8,
	GPIO164_DFI_D9,
	GPIO163_DFI_D10,
	GPIO162_DFI_D11,
	GPIO161_DFI_D12,
	GPIO110_DFI_D13,
	GPIO109_DFI_D14,
	GPIO108_DFI_D15,
	GPIO143_ND_nCS0,
	GPIO144_ND_nCS1,
	GPIO147_ND_nWE,
	GPIO148_ND_nRE,
	GPIO150_ND_ALE,
	GPIO149_ND_CLE,
	GPIO112_ND_RDY0,
	GPIO160_ND_RDY1,

	/* PMIC */
	PMIC_PMIC_INT | MFP_LPM_EDGE_FALL,

	/* MMC0 */
	GPIO131_MMC1_DAT3 | MFP_PULL_HIGH,
	GPIO132_MMC1_DAT2 | MFP_PULL_HIGH,
	GPIO133_MMC1_DAT1 | MFP_PULL_HIGH,
	GPIO134_MMC1_DAT0 | MFP_PULL_HIGH,
	GPIO136_MMC1_CMD | MFP_PULL_HIGH,
	GPIO139_MMC1_CLK,
	GPIO140_MMC1_CD | MFP_PULL_LOW,
	GPIO141_MMC1_WP | MFP_PULL_LOW,

	/* MMC1 */
	GPIO37_MMC2_DAT3 | MFP_PULL_HIGH,
	GPIO38_MMC2_DAT2 | MFP_PULL_HIGH,
	GPIO39_MMC2_DAT1 | MFP_PULL_HIGH,
	GPIO40_MMC2_DAT0 | MFP_PULL_HIGH,
	GPIO41_MMC2_CMD | MFP_PULL_HIGH,
	GPIO42_MMC2_CLK,

	/* MMC2 */
	GPIO165_MMC3_DAT7 | MFP_PULL_HIGH,
	GPIO162_MMC3_DAT6 | MFP_PULL_HIGH,
	GPIO166_MMC3_DAT5 | MFP_PULL_HIGH,
	GPIO163_MMC3_DAT4 | MFP_PULL_HIGH,
	GPIO167_MMC3_DAT3 | MFP_PULL_HIGH,
	GPIO164_MMC3_DAT2 | MFP_PULL_HIGH,
	GPIO168_MMC3_DAT1 | MFP_PULL_HIGH,
	GPIO111_MMC3_DAT0 | MFP_PULL_HIGH,
	GPIO112_MMC3_CMD | MFP_PULL_HIGH,
	GPIO151_MMC3_CLK,

	/* 5V regulator */
	GPIO89_GPIO,
};

static struct regulator_consumer_supply max8649_supply[] = {
	REGULATOR_SUPPLY("vcc_core", NULL),
};

static struct regulator_init_data max8649_init_data = {
	.constraints	= {
		.name		= "vcc_core range",
		.min_uV		= 1150000,
		.max_uV		= 1280000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max8649_supply[0],
};

static struct max8649_platform_data brownstone_max8649_info = {
	.mode		= 2,	/* VID1 = 1, VID0 = 0 */
	.extclk		= 0,
	.ramp_timing	= MAX8649_RAMP_32MV,
	.regulator	= &max8649_init_data,
};

static struct regulator_consumer_supply brownstone_v_5vp_supplies[] = {
	REGULATOR_SUPPLY("v_5vp", NULL),
};

static struct regulator_init_data brownstone_v_5vp_data = {
	.constraints	= {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(brownstone_v_5vp_supplies),
	.consumer_supplies	= brownstone_v_5vp_supplies,
};

static struct fixed_voltage_config brownstone_v_5vp = {
	.supply_name		= "v_5vp",
	.microvolts		= 5000000,
	.gpio			= GPIO_5V_ENABLE,
	.enable_high		= 1,
	.enabled_at_boot	= 1,
	.init_data		= &brownstone_v_5vp_data,
};

static struct platform_device brownstone_v_5vp_device = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data = &brownstone_v_5vp,
	},
};

static struct max8925_platform_data brownstone_max8925_info = {
	.irq_base		= MMP_NR_IRQS,
};

static struct i2c_board_info brownstone_twsi1_info[] = {
	[0] = {
		.type		= "max8649",
		.addr		= 0x60,
		.platform_data	= &brownstone_max8649_info,
	},
	[1] = {
		.type		= "max8925",
		.addr		= 0x3c,
		.irq		= IRQ_MMP2_PMIC,
		.platform_data	= &brownstone_max8925_info,
	},
};

static struct sdhci_pxa_platdata mmp2_sdh_platdata_mmc0 = {
	.clk_delay_cycles = 0x1f,
};

static struct sdhci_pxa_platdata mmp2_sdh_platdata_mmc2 = {
	.clk_delay_cycles = 0x1f,
	.flags = PXA_FLAG_CARD_PERMANENT
		| PXA_FLAG_SD_8_BIT_CAPABLE_SLOT,
};

static struct sram_platdata mmp2_asram_platdata = {
	.pool_name	= "asram",
	.granularity	= SRAM_GRANULARITY,
};

static struct sram_platdata mmp2_isram_platdata = {
	.pool_name	= "isram",
	.granularity	= SRAM_GRANULARITY,
};

static void __init brownstone_init(void)
{
	mfp_config(ARRAY_AND_SIZE(brownstone_pin_config));

	/* on-chip devices */
	mmp2_add_uart(1);
	mmp2_add_uart(3);
	platform_device_register(&mmp2_device_gpio);
	mmp2_add_twsi(1, NULL, ARRAY_AND_SIZE(brownstone_twsi1_info));
	mmp2_add_sdhost(0, &mmp2_sdh_platdata_mmc0); /* SD/MMC */
	mmp2_add_sdhost(2, &mmp2_sdh_platdata_mmc2); /* eMMC */
	mmp2_add_asram(&mmp2_asram_platdata);
	mmp2_add_isram(&mmp2_isram_platdata);

	/* enable 5v regulator */
	platform_device_register(&brownstone_v_5vp_device);
}

MACHINE_START(BROWNSTONE, "Brownstone Development Platform")
	/* Maintainer: Haojian Zhuang <haojian.zhuang@marvell.com> */
	.map_io		= mmp_map_io,
	.nr_irqs	= BROWNSTONE_NR_IRQS,
	.init_irq	= mmp2_init_irq,
	.timer		= &mmp2_timer,
	.init_machine	= brownstone_init,
	.restart	= mmp_restart,
MACHINE_END
