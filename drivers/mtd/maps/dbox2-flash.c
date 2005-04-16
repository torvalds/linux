/*
 * $Id: dbox2-flash.c,v 1.13 2004/11/04 13:24:14 gleixner Exp $
 *
 * D-Box 2 flash driver
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <linux/errno.h>

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]= {
	{
	.name		= "BR bootloader",
	.size		= 128 * 1024, 
	.offset		= 0,                  
	.mask_flags	= MTD_WRITEABLE
	},
	{
	.name		= "FLFS (U-Boot)",
	.size		= 128 * 1024, 
	.offset		= MTDPART_OFS_APPEND, 
	.mask_flags	= 0
	},
	{
	.name		= "Root (SquashFS)",	
	.size		= 7040 * 1024, 
	.offset		= MTDPART_OFS_APPEND, 
	.mask_flags	= 0
	},
	{
	.name		= "var (JFFS2)",
	.size		= 896 * 1024, 
	.offset		= MTDPART_OFS_APPEND, 
	.mask_flags	= 0
	},
	{
	.name		= "Flash without bootloader",	
	.size		= MTDPART_SIZ_FULL, 
	.offset		= 128 * 1024, 
	.mask_flags	= 0
	},
	{
	.name		= "Complete Flash",	
	.size		= MTDPART_SIZ_FULL, 
	.offset		= 0, 
	.mask_flags	= MTD_WRITEABLE
	}
};

#define NUM_PARTITIONS (sizeof(partition_info) / sizeof(partition_info[0]))

#define WINDOW_ADDR 0x10000000
#define WINDOW_SIZE 0x800000

static struct mtd_info *mymtd;


struct map_info dbox2_flash_map = {
	.name		= "D-Box 2 flash memory",
	.size		= WINDOW_SIZE,
	.bankwidth	= 4,
	.phys		= WINDOW_ADDR,
};

int __init init_dbox2_flash(void)
{
       	printk(KERN_NOTICE "D-Box 2 flash driver (size->0x%X mem->0x%X)\n", WINDOW_SIZE, WINDOW_ADDR);
	dbox2_flash_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!dbox2_flash_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&dbox2_flash_map);

	// Probe for dual Intel 28F320 or dual AMD
	mymtd = do_map_probe("cfi_probe", &dbox2_flash_map);
	if (!mymtd) {
	    // Probe for single Intel 28F640
	    dbox2_flash_map.bankwidth = 2;
	
	    mymtd = do_map_probe("cfi_probe", &dbox2_flash_map);
	}
	    
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

                /* Create MTD devices for each partition. */
	        add_mtd_partitions(mymtd, partition_info, NUM_PARTITIONS);
		
		return 0;
	}

	iounmap((void *)dbox2_flash_map.virt);
	return -ENXIO;
}

static void __exit cleanup_dbox2_flash(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
	if (dbox2_flash_map.virt) {
		iounmap((void *)dbox2_flash_map.virt);
		dbox2_flash_map.virt = 0;
	}
}

module_init(init_dbox2_flash);
module_exit(cleanup_dbox2_flash);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kári Davíðsson <kd@flaga.is>, Bastian Blank <waldi@tuxbox.org>, Alexander Wild <wild@te-elektronik.com>");
MODULE_DESCRIPTION("MTD map driver for D-Box 2 board");
