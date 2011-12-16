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
#include <linux/m48t86.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <mach/hardware.h>
#include <mach/ts72xx.h>

#include <asm/hardware/vic.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>


static struct map_desc ts72xx_io_desc[] __initdata = {
	{
		.virtual	= TS72XX_MODEL_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_MODEL_PHYS_BASE),
		.length		= TS72XX_MODEL_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= TS72XX_OPTIONS_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_OPTIONS_PHYS_BASE),
		.length		= TS72XX_OPTIONS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= TS72XX_OPTIONS2_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_OPTIONS2_PHYS_BASE),
		.length		= TS72XX_OPTIONS2_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= TS72XX_RTC_INDEX_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_RTC_INDEX_PHYS_BASE),
		.length		= TS72XX_RTC_INDEX_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= TS72XX_RTC_DATA_VIRT_BASE,
		.pfn		= __phys_to_pfn(TS72XX_RTC_DATA_PHYS_BASE),
		.length		= TS72XX_RTC_DATA_SIZE,
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

static void ts72xx_nand_hwcontrol(struct mtd_info *mtd,
				  int cmd, unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;

	if (ctrl & NAND_CTRL_CHANGE) {
		void __iomem *addr = chip->IO_ADDR_R;
		unsigned char bits;

		addr += (1 << TS72XX_NAND_CONTROL_ADDR_LINE);

		bits = __raw_readb(addr) & ~0x07;
		bits |= (ctrl & NAND_NCE) << 2;	/* bit 0 -> bit 2 */
		bits |= (ctrl & NAND_CLE);	/* bit 1 -> bit 1 */
		bits |= (ctrl & NAND_ALE) >> 2;	/* bit 2 -> bit 0 */

		__raw_writeb(bits, addr);
	}

	if (cmd != NAND_CMD_NONE)
		__raw_writeb(cmd, chip->IO_ADDR_W);
}

static int ts72xx_nand_device_ready(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	void __iomem *addr = chip->IO_ADDR_R;

	addr += (1 << TS72XX_NAND_BUSY_ADDR_LINE);

	return !!(__raw_readb(addr) & 0x20);
}

static const char *ts72xx_nand_part_probes[] = { "cmdlinepart", NULL };

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
		.part_probe_types = ts72xx_nand_part_probes,
		.partitions	= ts72xx_nand_parts,
		.nr_partitions	= ARRAY_SIZE(ts72xx_nand_parts),
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


static void __init ts72xx_register_flash(void)
{
	/*
	 * TS7200 has NOR flash all other TS72xx board have NAND flash.
	 */
	if (board_is_ts7200()) {
		ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_16M);
	} else {
		resource_size_t start;

		if (is_ts9420_installed())
			start = EP93XX_CS7_PHYS_BASE;
		else
			start = EP93XX_CS6_PHYS_BASE;

		ts72xx_nand_resource[0].start = start;
		ts72xx_nand_resource[0].end = start + SZ_16M - 1;

		platform_device_register(&ts72xx_nand_flash);
	}
}


static unsigned char ts72xx_rtc_readbyte(unsigned long addr)
{
	__raw_writeb(addr, TS72XX_RTC_INDEX_VIRT_BASE);
	return __raw_readb(TS72XX_RTC_DATA_VIRT_BASE);
}

static void ts72xx_rtc_writebyte(unsigned char value, unsigned long addr)
{
	__raw_writeb(addr, TS72XX_RTC_INDEX_VIRT_BASE);
	__raw_writeb(value, TS72XX_RTC_DATA_VIRT_BASE);
}

static struct m48t86_ops ts72xx_rtc_ops = {
	.readbyte	= ts72xx_rtc_readbyte,
	.writebyte	= ts72xx_rtc_writebyte,
};

static struct platform_device ts72xx_rtc_device = {
	.name		= "rtc-m48t86",
	.id		= -1,
	.dev		= {
		.platform_data	= &ts72xx_rtc_ops,
	},
	.num_resources	= 0,
};

static struct resource ts72xx_wdt_resources[] = {
	{
		.start	= TS72XX_WDT_CONTROL_PHYS_BASE,
		.end	= TS72XX_WDT_CONTROL_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= TS72XX_WDT_FEED_PHYS_BASE,
		.end	= TS72XX_WDT_FEED_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ts72xx_wdt_device = {
	.name		= "ts72xx-wdt",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(ts72xx_wdt_resources),
	.resource	= ts72xx_wdt_resources,
};

static struct ep93xx_eth_data __initdata ts72xx_eth_data = {
	.phy_id		= 1,
};

static void __init ts72xx_init_machine(void)
{
	ep93xx_init_devices();
	ts72xx_register_flash();
	platform_device_register(&ts72xx_rtc_device);
	platform_device_register(&ts72xx_wdt_device);

	ep93xx_register_eth(&ts72xx_eth_data, 1);
}

MACHINE_START(TS72XX, "Technologic Systems TS-72xx SBC")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.map_io		= ts72xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.handle_irq	= vic_handle_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= ts72xx_init_machine,
MACHINE_END
