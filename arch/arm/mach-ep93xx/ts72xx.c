/*
 * arch/arm/mach-ep93xx/ts72xx.c
 * Technologic Systems TS72xx SBC support.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mtd/platnand.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/spi/mmc_spi.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/spi-ep93xx.h>
#include <linux/gpio/machine.h>

#include <mach/gpio-ep93xx.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>

#include "soc.h"
#include "ts72xx.h"

/*************************************************************************
 * IO map
 *************************************************************************/
static struct map_desc ts72xx_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)TS72XX_MODEL_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_MODEL_PHYS_BASE),
		.length		= TS72XX_MODEL_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)TS72XX_OPTIONS_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_OPTIONS_PHYS_BASE),
		.length		= TS72XX_OPTIONS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)TS72XX_OPTIONS2_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_OPTIONS2_PHYS_BASE),
		.length		= TS72XX_OPTIONS2_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)TS72XX_CPLDVER_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_CPLDVER_PHYS_BASE),
		.length		= TS72XX_CPLDVER_SIZE,
		.type		= MT_DEVICE,
	}
};

static void __init ts72xx_map_io(void)
{
	ep93xx_map_io();
	iotable_init(ts72xx_io_desc, ARRAY_SIZE(ts72xx_io_desc));
}


/*************************************************************************
 * NAND flash
 *************************************************************************/
#define TS72XX_NAND_CONTROL_ADDR_LINE	22	/* 0xN0400000 */
#define TS72XX_NAND_BUSY_ADDR_LINE	23	/* 0xN0800000 */

static void ts72xx_nand_hwcontrol(struct nand_chip *chip,
				  int cmd, unsigned int ctrl)
{
	if (ctrl & NAND_CTRL_CHANGE) {
		void __iomem *addr = chip->legacy.IO_ADDR_R;
		unsigned char bits;

		addr += (1 << TS72XX_NAND_CONTROL_ADDR_LINE);

		bits = __raw_readb(addr) & ~0x07;
		bits |= (ctrl & NAND_NCE) << 2;	/* bit 0 -> bit 2 */
		bits |= (ctrl & NAND_CLE);	/* bit 1 -> bit 1 */
		bits |= (ctrl & NAND_ALE) >> 2;	/* bit 2 -> bit 0 */

		__raw_writeb(bits, addr);
	}

	if (cmd != NAND_CMD_NONE)
		__raw_writeb(cmd, chip->legacy.IO_ADDR_W);
}

static int ts72xx_nand_device_ready(struct nand_chip *chip)
{
	void __iomem *addr = chip->legacy.IO_ADDR_R;

	addr += (1 << TS72XX_NAND_BUSY_ADDR_LINE);

	return !!(__raw_readb(addr) & 0x20);
}

#define TS72XX_BOOTROM_PART_SIZE	(SZ_16K)
#define TS72XX_REDBOOT_PART_SIZE	(SZ_2M + SZ_1M)

static struct mtd_partition ts72xx_nand_parts[] = {
	{
		.name		= "TS-BOOTROM",
		.offset		= 0,
		.size		= TS72XX_BOOTROM_PART_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	}, {
		.name		= "Linux",
		.offset		= MTDPART_OFS_RETAIN,
		.size		= TS72XX_REDBOOT_PART_SIZE,
				/* leave so much for last partition */
	}, {
		.name		= "RedBoot",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
};

static struct platform_nand_data ts72xx_nand_data = {
	.chip = {
		.nr_chips	= 1,
		.chip_offset	= 0,
		.chip_delay	= 15,
	},
	.ctrl = {
		.cmd_ctrl	= ts72xx_nand_hwcontrol,
		.dev_ready	= ts72xx_nand_device_ready,
	},
};

static struct resource ts72xx_nand_resource[] = {
	{
		.start		= 0,			/* filled in later */
		.end		= 0,			/* filled in later */
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device ts72xx_nand_flash = {
	.name			= "gen_nand",
	.id			= -1,
	.dev.platform_data	= &ts72xx_nand_data,
	.resource		= ts72xx_nand_resource,
	.num_resources		= ARRAY_SIZE(ts72xx_nand_resource),
};

void __init ts72xx_register_flash(struct mtd_partition *parts, int n,
				  resource_size_t start)
{
	/*
	 * TS7200 has NOR flash all other TS72xx board have NAND flash.
	 */
	if (board_is_ts7200()) {
		ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_16M);
	} else {
		ts72xx_nand_resource[0].start = start;
		ts72xx_nand_resource[0].end = start + SZ_16M - 1;

		ts72xx_nand_data.chip.partitions = parts;
		ts72xx_nand_data.chip.nr_partitions = n;

		platform_device_register(&ts72xx_nand_flash);
	}
}

/*************************************************************************
 * RTC M48T86
 *************************************************************************/
#define TS72XX_RTC_INDEX_PHYS_BASE	(EP93XX_CS1_PHYS_BASE + 0x00800000)
#define TS72XX_RTC_DATA_PHYS_BASE	(EP93XX_CS1_PHYS_BASE + 0x01700000)

static struct resource ts72xx_rtc_resources[] = {
	DEFINE_RES_MEM(TS72XX_RTC_INDEX_PHYS_BASE, 0x01),
	DEFINE_RES_MEM(TS72XX_RTC_DATA_PHYS_BASE, 0x01),
};

static struct platform_device ts72xx_rtc_device = {
	.name		= "rtc-m48t86",
	.id		= -1,
	.resource	= ts72xx_rtc_resources,
	.num_resources 	= ARRAY_SIZE(ts72xx_rtc_resources),
};

/*************************************************************************
 * Watchdog (in CPLD)
 *************************************************************************/
#define TS72XX_WDT_CONTROL_PHYS_BASE	(EP93XX_CS2_PHYS_BASE + 0x03800000)
#define TS72XX_WDT_FEED_PHYS_BASE	(EP93XX_CS2_PHYS_BASE + 0x03c00000)

static struct resource ts72xx_wdt_resources[] = {
	DEFINE_RES_MEM(TS72XX_WDT_CONTROL_PHYS_BASE, 0x01),
	DEFINE_RES_MEM(TS72XX_WDT_FEED_PHYS_BASE, 0x01),
};

static struct platform_device ts72xx_wdt_device = {
	.name		= "ts72xx-wdt",
	.id		= -1,
	.resource	= ts72xx_wdt_resources,
	.num_resources	= ARRAY_SIZE(ts72xx_wdt_resources),
};

/*************************************************************************
 * ETH
 *************************************************************************/
static struct ep93xx_eth_data __initdata ts72xx_eth_data = {
	.phy_id		= 1,
};

/*************************************************************************
 * SPI SD/MMC host
 *************************************************************************/
#define BK3_EN_SDCARD_PHYS_BASE         0x12400000
#define BK3_EN_SDCARD_PWR 0x0
#define BK3_DIS_SDCARD_PWR 0x0C
static void bk3_mmc_spi_setpower(struct device *dev, unsigned int vdd)
{
	void __iomem *pwr_sd = ioremap(BK3_EN_SDCARD_PHYS_BASE, SZ_4K);

	if (!pwr_sd) {
		pr_err("Failed to enable SD card power!");
		return;
	}

	pr_debug("%s: SD card pwr %s VDD:0x%x\n", __func__,
		 !!vdd ? "ON" : "OFF", vdd);

	if (!!vdd)
		__raw_writeb(BK3_EN_SDCARD_PWR, pwr_sd);
	else
		__raw_writeb(BK3_DIS_SDCARD_PWR, pwr_sd);

	iounmap(pwr_sd);
}

static struct mmc_spi_platform_data bk3_spi_mmc_data = {
	.detect_delay	= 500,
	.powerup_msecs	= 100,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps		= MMC_CAP_NONREMOVABLE,
	.setpower       = bk3_mmc_spi_setpower,
};

/*************************************************************************
 * SPI Bus - SD card access
 *************************************************************************/
static struct spi_board_info bk3_spi_board_info[] __initdata = {
	{
		.modalias		= "mmc_spi",
		.platform_data		= &bk3_spi_mmc_data,
		.max_speed_hz		= 7.4E6,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
	},
};

/*
 * This is a stub -> the FGPIO[3] pin is not connected on the schematic
 * The all work is performed automatically by !SPI_FRAME (SFRM1) and
 * goes through CPLD
 */
static struct gpiod_lookup_table bk3_spi_cs_gpio_table = {
	.dev_id = "ep93xx-spi.0",
	.table = {
		GPIO_LOOKUP("F", 3, "cs", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct ep93xx_spi_info bk3_spi_master __initdata = {
	.use_dma	= 1,
};

/*************************************************************************
 * TS72XX support code
 *************************************************************************/
#if IS_ENABLED(CONFIG_FPGA_MGR_TS73XX)

/* Relative to EP93XX_CS1_PHYS_BASE */
#define TS73XX_FPGA_LOADER_BASE		0x03c00000

static struct resource ts73xx_fpga_resources[] = {
	{
		.start	= EP93XX_CS1_PHYS_BASE + TS73XX_FPGA_LOADER_BASE,
		.end	= EP93XX_CS1_PHYS_BASE + TS73XX_FPGA_LOADER_BASE + 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ts73xx_fpga_device = {
	.name	= "ts73xx-fpga-mgr",
	.id	= -1,
	.resource = ts73xx_fpga_resources,
	.num_resources = ARRAY_SIZE(ts73xx_fpga_resources),
};

#endif

/*************************************************************************
 * SPI Bus
 *************************************************************************/
static struct spi_board_info ts72xx_spi_devices[] __initdata = {
	{
		.modalias		= "tmp122",
		.max_speed_hz		= 2 * 1000 * 1000,
		.bus_num		= 0,
		.chip_select		= 0,
	},
};

static struct gpiod_lookup_table ts72xx_spi_cs_gpio_table = {
	.dev_id = "ep93xx-spi.0",
	.table = {
		/* DIO_17 */
		GPIO_LOOKUP("F", 2, "cs", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct ep93xx_spi_info ts72xx_spi_info __initdata = {
	/* Intentionally left blank */
};

static void __init ts72xx_init_machine(void)
{
	ep93xx_init_devices();
	ts72xx_register_flash(ts72xx_nand_parts, ARRAY_SIZE(ts72xx_nand_parts),
			      is_ts9420_installed() ?
			      EP93XX_CS7_PHYS_BASE : EP93XX_CS6_PHYS_BASE);
	platform_device_register(&ts72xx_rtc_device);
	platform_device_register(&ts72xx_wdt_device);

	ep93xx_register_eth(&ts72xx_eth_data, 1);
#if IS_ENABLED(CONFIG_FPGA_MGR_TS73XX)
	if (board_is_ts7300())
		platform_device_register(&ts73xx_fpga_device);
#endif
	gpiod_add_lookup_table(&ts72xx_spi_cs_gpio_table);
	ep93xx_register_spi(&ts72xx_spi_info, ts72xx_spi_devices,
			    ARRAY_SIZE(ts72xx_spi_devices));
}

MACHINE_START(TS72XX, "Technologic Systems TS-72xx SBC")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.map_io		= ts72xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= ts72xx_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END

/*************************************************************************
 * EP93xx I2S audio peripheral handling
 *************************************************************************/
static struct resource ep93xx_i2s_resource[] = {
	DEFINE_RES_MEM(EP93XX_I2S_PHYS_BASE, 0x100),
	DEFINE_RES_IRQ_NAMED(IRQ_EP93XX_SAI, "spilink i2s slave"),
};

static struct platform_device ep93xx_i2s_device = {
	.name		= "ep93xx-spilink-i2s",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_i2s_resource),
	.resource	= ep93xx_i2s_resource,
};

/*************************************************************************
 * BK3 support code
 *************************************************************************/
static struct mtd_partition bk3_nand_parts[] = {
	{
		.name		= "System",
		.offset	= 0x00000000,
		.size		= 0x01e00000,
	}, {
		.name		= "Data",
		.offset	= 0x01e00000,
		.size		= 0x05f20000
	}, {
		.name		= "RedBoot",
		.offset	= 0x07d20000,
		.size		= 0x002e0000,
		.mask_flags	= MTD_WRITEABLE,	/* force RO */
	},
};

static void __init bk3_init_machine(void)
{
	ep93xx_init_devices();

	ts72xx_register_flash(bk3_nand_parts, ARRAY_SIZE(bk3_nand_parts),
			      EP93XX_CS6_PHYS_BASE);

	ep93xx_register_eth(&ts72xx_eth_data, 1);

	gpiod_add_lookup_table(&bk3_spi_cs_gpio_table);
	ep93xx_register_spi(&bk3_spi_master, bk3_spi_board_info,
			    ARRAY_SIZE(bk3_spi_board_info));

	/* Configure ep93xx's I2S to use AC97 pins */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_I2SONAC97);
	platform_device_register(&ep93xx_i2s_device);
}

MACHINE_START(BK3, "Liebherr controller BK3.1")
	/* Maintainer: Lukasz Majewski <lukma@denx.de> */
	.atag_offset	= 0x100,
	.map_io		= ts72xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= bk3_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
