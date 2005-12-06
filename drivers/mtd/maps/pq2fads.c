/*
 * drivers/mtd/maps/pq2fads.c
 *
 * Mapping for the flash SIMM on 8272ADS and PQ2FADS board
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/ppcboot.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

/*
  NOTE: bank width and interleave relative to the installed flash
  should have been chosen within MTD_CFI_GEOMETRY options.
  */
#define PQ2FADS_BANK_WIDTH 4

static struct mtd_partition pq2fads_partitions[] = {
	{
#ifdef CONFIG_ADS8272
		.name		= "HRCW",
		.size		= 0x40000,
		.offset 	= 0,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "User FS",
		.size		= 0x5c0000,
		.offset 	= 0x40000,
#else
		.name		= "User FS",
		.size		= 0x600000,
		.offset 	= 0,
#endif
	}, {
		.name		= "uImage",
		.size		= 0x100000,
		.offset 	= 0x600000,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "bootloader",
		.size		= 0x40000,
		.offset		= 0x700000,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "bootloader env",
		.size		= 0x40000,
		.offset		= 0x740000,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}
};


/* pointer to MPC885ADS board info data */
extern unsigned char __res[];

static int __init init_pq2fads_mtd(void)
{
	bd_t *bd = (bd_t *)__res;
	physmap_configure(bd->bi_flashstart, bd->bi_flashsize, PQ2FADS_BANK_WIDTH, NULL);

	physmap_set_partitions(pq2fads_partitions,
				sizeof (pq2fads_partitions) /
				sizeof (pq2fads_partitions[0]));
	return 0;
}

static void __exit cleanup_pq2fads_mtd(void)
{
}

module_init(init_pq2fads_mtd);
module_exit(cleanup_pq2fads_mtd);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map and partitions for MPC8272ADS boards");
