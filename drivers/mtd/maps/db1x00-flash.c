/*
 * Flash memory access on Alchemy Db1xxx boards
 * 
 * $Id: db1x00-flash.c,v 1.6 2004/11/04 13:24:14 gleixner Exp $
 *
 * (C) 2003 Pete Popov <ppopov@embeddedalley.com>
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#ifdef 	DEBUG_RW
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)	
#endif

/* MTD CONFIG OPTIONS */
#if defined(CONFIG_MTD_DB1X00_BOOT) && defined(CONFIG_MTD_DB1X00_USER)
#define DB1X00_BOTH_BANKS
#elif defined(CONFIG_MTD_DB1X00_BOOT) && !defined(CONFIG_MTD_DB1X00_USER)
#define DB1X00_BOOT_ONLY
#elif !defined(CONFIG_MTD_DB1X00_BOOT) && defined(CONFIG_MTD_DB1X00_USER)
#define DB1X00_USER_ONLY
#endif

static unsigned long window_addr;
static unsigned long window_size;
static unsigned long flash_size;

static unsigned short *bcsr = (unsigned short *)0xAE000000;
static unsigned char flash_bankwidth = 4;

/* 
 * The Db1x boards support different flash densities. We setup
 * the mtd_partition structures below for default of 64Mbit 
 * flash densities, and override the partitions sizes, if
 * necessary, after we check the board status register.
 */

#ifdef DB1X00_BOTH_BANKS
/* both banks will be used. Combine the first bank and the first 
 * part of the second bank together into a single jffs/jffs2
 * partition.
 */
static struct mtd_partition db1x00_partitions[] = {
        {
                .name         =  "User FS",
                .size         =  0x1c00000,
                .offset       =  0x0000000
        },{
                .name         =  "yamon",
                .size         =  0x0100000,
		.offset       =  MTDPART_OFS_APPEND,
                .mask_flags   =  MTD_WRITEABLE
        },{
                .name         =  "raw kernel",
		.size         =  (0x300000-0x40000), /* last 256KB is env */
		.offset       =  MTDPART_OFS_APPEND,
        }
};
#elif defined(DB1X00_BOOT_ONLY)
static struct mtd_partition db1x00_partitions[] = {
        {
                .name         =  "User FS",
                .size         =  0x00c00000,
                .offset       =  0x0000000
        },{
                .name         =  "yamon",
                .size         =  0x0100000,
		.offset       =  MTDPART_OFS_APPEND,
                .mask_flags   =  MTD_WRITEABLE
        },{
                .name         =  "raw kernel",
		.size         =  (0x300000-0x40000), /* last 256KB is env */
		.offset       =  MTDPART_OFS_APPEND,
        }
};
#elif defined(DB1X00_USER_ONLY)
static struct mtd_partition db1x00_partitions[] = {
        {
                .name         =  "User FS",
                .size         =  0x0e00000,
                .offset       =  0x0000000
        },{
                .name         =  "raw kernel",
		.size         =  MTDPART_SIZ_FULL,
		.offset       =  MTDPART_OFS_APPEND,
        }
};
#else
#error MTD_DB1X00 define combo error /* should never happen */
#endif
#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

#define NAME     	"Db1x00 Linux Flash"

static struct map_info db1xxx_mtd_map = {
	.name		= NAME,
};

static struct mtd_partition *parsed_parts;
static struct mtd_info *db1xxx_mtd;

/*
 * Probe the flash density and setup window address and size
 * based on user CONFIG options. There are times when we don't
 * want the MTD driver to be probing the boot or user flash,
 * so having the option to enable only one bank is important.
 */
int setup_flash_params(void)
{
	switch ((bcsr[2] >> 14) & 0x3) {
		case 0: /* 64Mbit devices */
			flash_size = 0x800000; /* 8MB per part */
#if defined(DB1X00_BOTH_BANKS)
			window_addr = 0x1E000000;
			window_size = 0x2000000; 
#elif defined(DB1X00_BOOT_ONLY)
			window_addr = 0x1F000000;
			window_size = 0x1000000; 
#else /* USER ONLY */
			window_addr = 0x1E000000;
			window_size = 0x1000000; 
#endif
			break;
		case 1:
			/* 128 Mbit devices */
			flash_size = 0x1000000; /* 16MB per part */
#if defined(DB1X00_BOTH_BANKS)
			window_addr = 0x1C000000;
			window_size = 0x4000000;
			/* USERFS from 0x1C00 0000 to 0x1FC0 0000 */
			db1x00_partitions[0].size = 0x3C00000;
#elif defined(DB1X00_BOOT_ONLY)
			window_addr = 0x1E000000;
			window_size = 0x2000000;
			/* USERFS from 0x1E00 0000 to 0x1FC0 0000 */
			db1x00_partitions[0].size = 0x1C00000;
#else /* USER ONLY */
			window_addr = 0x1C000000;
			window_size = 0x2000000;
			/* USERFS from 0x1C00 0000 to 0x1DE00000 */
			db1x00_partitions[0].size = 0x1DE0000;
#endif
			break;
		case 2:
			/* 256 Mbit devices */
			flash_size = 0x4000000; /* 64MB per part */
#if defined(DB1X00_BOTH_BANKS)
			return 1;
#elif defined(DB1X00_BOOT_ONLY)
			/* Boot ROM flash bank only; no user bank */
			window_addr = 0x1C000000;
			window_size = 0x4000000;
			/* USERFS from 0x1C00 0000 to 0x1FC00000 */
			db1x00_partitions[0].size = 0x3C00000;
#else /* USER ONLY */
			return 1;
#endif
			break;
		default:
			return 1;
	}
	db1xxx_mtd_map.size = window_size;
	db1xxx_mtd_map.bankwidth = flash_bankwidth;
	db1xxx_mtd_map.phys = window_addr;
	db1xxx_mtd_map.bankwidth = flash_bankwidth;
	return 0;
}

int __init db1x00_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	
	if (setup_flash_params()) 
		return -ENXIO;

	/*
	 * Static partition definition selection
	 */
	parts = db1x00_partitions;
	nb_parts = NB_OF(db1x00_partitions);

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "Db1xxx flash: probing %d-bit flash bus\n", 
			db1xxx_mtd_map.bankwidth*8);
	db1xxx_mtd_map.virt = ioremap(window_addr, window_size);
	db1xxx_mtd = do_map_probe("cfi_probe", &db1xxx_mtd_map);
	if (!db1xxx_mtd) return -ENXIO;
	db1xxx_mtd->owner = THIS_MODULE;

	add_mtd_partitions(db1xxx_mtd, parts, nb_parts);
	return 0;
}

static void __exit db1x00_mtd_cleanup(void)
{
	if (db1xxx_mtd) {
		del_mtd_partitions(db1xxx_mtd);
		map_destroy(db1xxx_mtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
}

module_init(db1x00_mtd_init);
module_exit(db1x00_mtd_cleanup);

MODULE_AUTHOR("Pete Popov");
MODULE_DESCRIPTION("Db1x00 mtd map driver");
MODULE_LICENSE("GPL");
