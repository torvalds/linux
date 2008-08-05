/*
 * linux/arch/arm/mach-omap2/board-2430sdp.c
 *
 * Copyright (C) 2006 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial Code : Based on a patch from Komal Shah and Richard Woodruff
 * Updated the Code for 2430 SDP : Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>

#include <asm/io.h>


#define	SDP2430_FLASH_CS	0
#define	SDP2430_SMC91X_CS	5

static struct mtd_partition sdp2430_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	 },
	/* bootloader params in the next sector */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= 0,
	 },
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct flash_platform_data sdp2430_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= sdp2430_partitions,
	.nr_parts	= ARRAY_SIZE(sdp2430_partitions),
};

static struct resource sdp2430_flash_resource = {
	.start		= SDP2430_CS0_BASE,
	.end		= SDP2430_CS0_BASE + SZ_64M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device sdp2430_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev = {
		.platform_data	= &sdp2430_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sdp2430_flash_resource,
};

static struct resource sdp2430_smc91x_resources[] = {
	[0] = {
		.start	= SDP2430_CS0_BASE,
		.end	= SDP2430_CS0_BASE + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(OMAP24XX_ETHR_GPIO_IRQ),
		.end	= OMAP_GPIO_IRQ(OMAP24XX_ETHR_GPIO_IRQ),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device sdp2430_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sdp2430_smc91x_resources),
	.resource	= sdp2430_smc91x_resources,
};

static struct platform_device *sdp2430_devices[] __initdata = {
	&sdp2430_smc91x_device,
	&sdp2430_flash_device,
};

static inline void __init sdp2430_init_smc91x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	unsigned int rate;
	struct clk *gpmc_fck;

	eth_cs = SDP2430_SMC91X_CS;

	gpmc_fck = clk_get(NULL, "gpmc_fck");	/* Always on ENABLE_ON_INIT */
	if (IS_ERR(gpmc_fck)) {
		WARN_ON(1);
		return;
	}

	clk_enable(gpmc_fck);
	rate = clk_get_rate(gpmc_fck);

	/* Make sure CS1 timings are correct, for 2430 always muxed */
	gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG1, 0x00011200);

	if (rate >= 160000000) {
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f01);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080803);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1c0b1c0a);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x041f1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000004C4);
	} else if (rate >= 130000000) {
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f00);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080802);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1C091C09);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x041f1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000004C4);
	} else { /* rate = 100000000 */
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f00);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080802);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1C091C09);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x031A1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000003C2);
	}

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		goto out;
	}

	sdp2430_smc91x_resources[0].start = cs_mem_base + 0x300;
	sdp2430_smc91x_resources[0].end = cs_mem_base + 0x30f;
	udelay(100);

	if (omap_request_gpio(OMAP24XX_ETHR_GPIO_IRQ) < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			OMAP24XX_ETHR_GPIO_IRQ);
		gpmc_cs_free(eth_cs);
		goto out;
	}
	omap_set_gpio_direction(OMAP24XX_ETHR_GPIO_IRQ, 1);

out:
	clk_disable(gpmc_fck);
	clk_put(gpmc_fck);
}

static void __init omap_2430sdp_init_irq(void)
{
	omap2_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
	sdp2430_init_smc91x();
}

static struct omap_uart_config sdp2430_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel sdp2430_config[] = {
	{OMAP_TAG_UART, &sdp2430_uart_config},
};

static void __init omap_2430sdp_init(void)
{
	platform_add_devices(sdp2430_devices, ARRAY_SIZE(sdp2430_devices));
	omap_board_config = sdp2430_config;
	omap_board_config_size = ARRAY_SIZE(sdp2430_config);
	omap_serial_init();
}

static void __init omap_2430sdp_map_io(void)
{
	omap2_set_globals_243x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_2430SDP, "OMAP2430 sdp2430 board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_2430sdp_map_io,
	.init_irq	= omap_2430sdp_init_irq,
	.init_machine	= omap_2430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
