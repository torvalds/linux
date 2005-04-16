/*
 * Flash on MPC-1211
 *
 * $Id: mpc1211.c,v 1.4 2004/09/16 23:27:13 gleixner Exp $
 *
 * (C) 2002 Interface, Saito.K & Jeanne
 *
 * GPL'd
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>

static struct mtd_info *flash_mtd;
static struct mtd_partition *parsed_parts;

struct map_info mpc1211_flash_map = {
	.name		= "MPC-1211 FLASH",
	.size		= 0x80000,
	.bankwidth	= 1,
};

static struct mtd_partition mpc1211_partitions[] = {
	{
		.name	= "IPL & ETH-BOOT",
		.offset	= 0x00000000,
		.size	= 0x10000,
	},
	{
		.name	= "Flash FS",
		.offset	= 0x00010000,
		.size	= MTDPART_SIZ_FULL,
	}
};

static int __init init_mpc1211_maps(void)
{
	int nr_parts;

	mpc1211_flash_map.phys = 0;
	mpc1211_flash_map.virt = (void __iomem *)P2SEGADDR(0);

	simple_map_init(&mpc1211_flash_map);

	printk(KERN_NOTICE "Probing for flash chips at 0x00000000:\n");
	flash_mtd = do_map_probe("jedec_probe", &mpc1211_flash_map);
	if (!flash_mtd) {
		printk(KERN_NOTICE "Flash chips not detected at either possible location.\n");
		return -ENXIO;
	}
	printk(KERN_NOTICE "MPC-1211: Flash at 0x%08lx\n", mpc1211_flash_map.virt & 0x1fffffff);
	flash_mtd->module = THIS_MODULE;

	parsed_parts = mpc1211_partitions;
	nr_parts = ARRAY_SIZE(mpc1211_partitions);

	add_mtd_partitions(flash_mtd, parsed_parts, nr_parts);
	return 0;
}

static void __exit cleanup_mpc1211_maps(void)
{
	if (parsed_parts)
		del_mtd_partitions(flash_mtd);
	else
		del_mtd_device(flash_mtd);
	map_destroy(flash_mtd);
}

module_init(init_mpc1211_maps);
module_exit(cleanup_mpc1211_maps);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Saito.K & Jeanne <ksaito@interface.co.jp>");
MODULE_DESCRIPTION("MTD map driver for MPC-1211 boards. Interface");
