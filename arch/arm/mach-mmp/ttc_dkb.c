/*
 *  linux/arch/arm/mach-mmp/ttc_dkb.c
 *
 *  Support for the Marvell PXA910-based TTC_DKB Development Platform.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/onenand.h>
#include <linux/interrupt.h>
#include <linux/i2c/pca953x.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <mach/addr-map.h>
#include <mach/mfp-pxa910.h>
#include <mach/pxa910.h>
#include <mach/irqs.h>

#include "common.h"

#define TTCDKB_GPIO_EXT0(x)	(MMP_NR_BUILTIN_GPIO + ((x < 0) ? 0 :	\
				((x < 16) ? x : 15)))
#define TTCDKB_GPIO_EXT1(x)	(MMP_NR_BUILTIN_GPIO + 16 + ((x < 0) ? 0 : \
				((x < 16) ? x : 15)))

/*
 * 16 board interrupts -- MAX7312 GPIO expander
 * 16 board interrupts -- PCA9575 GPIO expander
 * 24 board interrupts -- 88PM860x PMIC
 */
#define TTCDKB_NR_IRQS		(MMP_NR_IRQS + 16 + 16 + 24)

static unsigned long ttc_dkb_pin_config[] __initdata = {
	/* UART2 */
	GPIO47_UART2_RXD,
	GPIO48_UART2_TXD,

	/* DFI */
	DF_IO0_ND_IO0,
	DF_IO1_ND_IO1,
	DF_IO2_ND_IO2,
	DF_IO3_ND_IO3,
	DF_IO4_ND_IO4,
	DF_IO5_ND_IO5,
	DF_IO6_ND_IO6,
	DF_IO7_ND_IO7,
	DF_IO8_ND_IO8,
	DF_IO9_ND_IO9,
	DF_IO10_ND_IO10,
	DF_IO11_ND_IO11,
	DF_IO12_ND_IO12,
	DF_IO13_ND_IO13,
	DF_IO14_ND_IO14,
	DF_IO15_ND_IO15,
	DF_nCS0_SM_nCS2_nCS0,
	DF_ALE_SM_WEn_ND_ALE,
	DF_CLE_SM_OEn_ND_CLE,
	DF_WEn_DF_WEn,
	DF_REn_DF_REn,
	DF_RDY0_DF_RDY0,
};

static struct mtd_partition ttc_dkb_onenand_partitions[] = {
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_8M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= (SZ_2M + SZ_1M),
		.mask_flags	= 0,
	}, {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_32M + SZ_16M,
		.mask_flags	= 0,
	}
};

static struct onenand_platform_data ttc_dkb_onenand_info = {
	.parts		= ttc_dkb_onenand_partitions,
	.nr_parts	= ARRAY_SIZE(ttc_dkb_onenand_partitions),
};

static struct resource ttc_dkb_resource_onenand[] = {
	[0] = {
		.start	= SMC_CS0_PHYS_BASE,
		.end	= SMC_CS0_PHYS_BASE + SZ_1M,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ttc_dkb_device_onenand = {
	.name		= "onenand-flash",
	.id		= -1,
	.resource	= ttc_dkb_resource_onenand,
	.num_resources	= ARRAY_SIZE(ttc_dkb_resource_onenand),
	.dev		= {
		.platform_data	= &ttc_dkb_onenand_info,
	},
};

static struct platform_device *ttc_dkb_devices[] = {
	&pxa910_device_gpio,
	&pxa910_device_rtc,
	&ttc_dkb_device_onenand,
};

static struct pca953x_platform_data max7312_data[] = {
	{
		.gpio_base	= TTCDKB_GPIO_EXT0(0),
		.irq_base	= MMP_NR_IRQS,
	},
};

static struct i2c_board_info ttc_dkb_i2c_info[] = {
	{
		.type		= "max7312",
		.addr		= 0x23,
		.irq		= MMP_GPIO_TO_IRQ(80),
		.platform_data	= &max7312_data,
	},
};

static void __init ttc_dkb_init(void)
{
	mfp_config(ARRAY_AND_SIZE(ttc_dkb_pin_config));

	/* on-chip devices */
	pxa910_add_uart(1);

	/* off-chip devices */
	pxa910_add_twsi(0, NULL, ARRAY_AND_SIZE(ttc_dkb_i2c_info));
	platform_add_devices(ARRAY_AND_SIZE(ttc_dkb_devices));
}

MACHINE_START(TTC_DKB, "PXA910-based TTC_DKB Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= TTCDKB_NR_IRQS,
	.init_irq       = pxa910_init_irq,
	.timer          = &pxa910_timer,
	.init_machine   = ttc_dkb_init,
	.restart	= mmp_restart,
MACHINE_END
