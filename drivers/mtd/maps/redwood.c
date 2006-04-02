/*
 * $Id: redwood.c,v 1.11 2005/11/07 11:14:28 gleixner Exp $
 *
 * drivers/mtd/maps/redwood.c
 *
 * FLASH map for the IBM Redwood 4/5/6 boards.
 *
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 * 2001-2003 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#if !defined (CONFIG_REDWOOD_6)

#define WINDOW_ADDR 0xffc00000
#define WINDOW_SIZE 0x00400000

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x10000
#define RW_PART1_OF	RW_PART0_SZ
#define RW_PART1_SZ	0x200000 - 0x10000
#define RW_PART2_OF	0x200000
#define RW_PART2_SZ	0x10000
#define RW_PART3_OF	0x210000
#define RW_PART3_SZ	0x200000 - (0x10000 + 0x20000)
#define RW_PART4_OF	0x3e0000
#define RW_PART4_SZ	0x20000

static struct mtd_partition redwood_flash_partitions[] = {
	{
		.name = "Redwood OpenBIOS Vital Product Data",
		.offset = RW_PART0_OF,
		.size = RW_PART0_SZ,
		.mask_flags = MTD_WRITEABLE	/* force read-only */
	},
	{
		.name = "Redwood kernel",
		.offset = RW_PART1_OF,
		.size = RW_PART1_SZ
	},
	{
		.name = "Redwood OpenBIOS non-volatile storage",
		.offset = RW_PART2_OF,
		.size = RW_PART2_SZ,
		.mask_flags = MTD_WRITEABLE	/* force read-only */
	},
	{
		.name = "Redwood filesystem",
		.offset = RW_PART3_OF,
		.size = RW_PART3_SZ
	},
	{
		.name = "Redwood OpenBIOS",
		.offset = RW_PART4_OF,
		.size = RW_PART4_SZ,
		.mask_flags = MTD_WRITEABLE	/* force read-only */
	}
};

#else /* CONFIG_REDWOOD_6 */
/* FIXME: the window is bigger - armin */
#define WINDOW_ADDR 0xff800000
#define WINDOW_SIZE 0x00800000

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x400000	/* 4 MiB data */
#define RW_PART1_OF	RW_PART0_OF + RW_PART0_SZ
#define RW_PART1_SZ	0x10000		/* 64K VPD */
#define RW_PART2_OF	RW_PART1_OF + RW_PART1_SZ
#define RW_PART2_SZ	0x400000 - (0x10000 + 0x20000)
#define RW_PART3_OF	RW_PART2_OF + RW_PART2_SZ
#define RW_PART3_SZ	0x20000

static struct mtd_partition redwood_flash_partitions[] = {
	{
		.name = "Redwood filesystem",
		.offset = RW_PART0_OF,
		.size = RW_PART0_SZ
	},
	{
		.name = "Redwood OpenBIOS Vital Product Data",
		.offset = RW_PART1_OF,
		.size = RW_PART1_SZ,
		.mask_flags = MTD_WRITEABLE	/* force read-only */
	},
	{
		.name = "Redwood kernel",
		.offset = RW_PART2_OF,
		.size = RW_PART2_SZ
	},
	{
		.name = "Redwood OpenBIOS",
		.offset = RW_PART3_OF,
		.size = RW_PART3_SZ,
		.mask_flags = MTD_WRITEABLE	/* force read-only */
	}
};

#endif /* CONFIG_REDWOOD_6 */

struct map_info redwood_flash_map = {
	.name = "IBM Redwood",
	.size = WINDOW_SIZE,
	.bankwidth = 2,
	.phys = WINDOW_ADDR,
};


#define NUM_REDWOOD_FLASH_PARTITIONS ARRAY_SIZE(redwood_flash_partitions)

static struct mtd_info *redwood_mtd;

int __init init_redwood_flash(void)
{
	printk(KERN_NOTICE "redwood: flash mapping: %x at %x\n",
			WINDOW_SIZE, WINDOW_ADDR);

	redwood_flash_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!redwood_flash_map.virt) {
		printk("init_redwood_flash: failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&redwood_flash_map);

	redwood_mtd = do_map_probe("cfi_probe",&redwood_flash_map);

	if (redwood_mtd) {
		redwood_mtd->owner = THIS_MODULE;
		return add_mtd_partitions(redwood_mtd,
				redwood_flash_partitions,
				NUM_REDWOOD_FLASH_PARTITIONS);
	}

	return -ENXIO;
}

static void __exit cleanup_redwood_flash(void)
{
	if (redwood_mtd) {
		del_mtd_partitions(redwood_mtd);
		/* moved iounmap after map_destroy - armin */
		map_destroy(redwood_mtd);
		iounmap((void *)redwood_flash_map.virt);
	}
}

module_init(init_redwood_flash);
module_exit(cleanup_redwood_flash);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MontaVista Software <source@mvista.com>");
MODULE_DESCRIPTION("MTD map driver for the IBM Redwood reference boards");
