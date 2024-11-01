// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/jasper.c
 *
 *  Support for the Marvell Jasper Development Platform.
 *
 *  Copyright (C) 2009-2010 Marvell International Ltd.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio-pxa.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max8649.h>
#include <linux/mfd/max8925.h>
#include <linux/interrupt.h>

#include "irqs.h"
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "addr-map.h"
#include "mfp-mmp2.h"
#include "mmp2.h"

#include "common.h"

#define JASPER_NR_IRQS		(MMP_NR_IRQS + 48)

static unsigned long jasper_pin_config[] __initdata = {
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

	/* MMC1 */
	GPIO131_MMC1_DAT3,
	GPIO132_MMC1_DAT2,
	GPIO133_MMC1_DAT1,
	GPIO134_MMC1_DAT0,
	GPIO136_MMC1_CMD,
	GPIO139_MMC1_CLK,
	GPIO140_MMC1_CD,
	GPIO141_MMC1_WP,

	/* MMC2 */
	GPIO37_MMC2_DAT3,
	GPIO38_MMC2_DAT2,
	GPIO39_MMC2_DAT1,
	GPIO40_MMC2_DAT0,
	GPIO41_MMC2_CMD,
	GPIO42_MMC2_CLK,

	/* MMC3 */
	GPIO165_MMC3_DAT7,
	GPIO162_MMC3_DAT6,
	GPIO166_MMC3_DAT5,
	GPIO163_MMC3_DAT4,
	GPIO167_MMC3_DAT3,
	GPIO164_MMC3_DAT2,
	GPIO168_MMC3_DAT1,
	GPIO111_MMC3_DAT0,
	GPIO112_MMC3_CMD,
	GPIO151_MMC3_CLK,
};

static struct pxa_gpio_platform_data mmp2_gpio_pdata = {
	.irq_base	= MMP_GPIO_TO_IRQ(0),
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

static struct max8649_platform_data jasper_max8649_info = {
	.mode		= 2,	/* VID1 = 1, VID0 = 0 */
	.extclk		= 0,
	.ramp_timing	= MAX8649_RAMP_32MV,
	.regulator	= &max8649_init_data,
};

static struct max8925_backlight_pdata jasper_backlight_data = {
	.dual_string	= 0,
};

static struct max8925_power_pdata jasper_power_data = {
	.batt_detect		= 0,	/* can't detect battery by ID pin */
	.topoff_threshold	= MAX8925_TOPOFF_THR_10PER,
	.fast_charge		= MAX8925_FCHG_1000MA,
};

static struct max8925_platform_data jasper_max8925_info = {
	.backlight		= &jasper_backlight_data,
	.power			= &jasper_power_data,
	.irq_base		= MMP_NR_IRQS,
};

static struct i2c_board_info jasper_twsi1_info[] = {
	[0] = {
		.type		= "max8649",
		.addr		= 0x60,
		.platform_data	= &jasper_max8649_info,
	},
	[1] = {
		.type		= "max8925",
		.addr		= 0x3c,
		.irq		= IRQ_MMP2_PMIC,
		.platform_data	= &jasper_max8925_info,
	},
};

static struct sdhci_pxa_platdata mmp2_sdh_platdata_mmc0 = {
	.clk_delay_cycles = 0x1f,
};

static void __init jasper_init(void)
{
	mfp_config(ARRAY_AND_SIZE(jasper_pin_config));

	/* on-chip devices */
	mmp2_add_uart(1);
	mmp2_add_uart(3);
	mmp2_add_twsi(1, NULL, ARRAY_AND_SIZE(jasper_twsi1_info));
	platform_device_add_data(&mmp2_device_gpio, &mmp2_gpio_pdata,
				 sizeof(struct pxa_gpio_platform_data));
	platform_device_register(&mmp2_device_gpio);
	mmp2_add_sdhost(0, &mmp2_sdh_platdata_mmc0); /* SD/MMC */

	regulator_has_full_constraints();
}

MACHINE_START(MARVELL_JASPER, "Jasper Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= JASPER_NR_IRQS,
	.init_irq       = mmp2_init_irq,
	.init_time	= mmp2_timer_init,
	.init_machine   = jasper_init,
	.restart	= mmp_restart,
MACHINE_END
