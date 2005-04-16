/*
 * Flash memory access on Alchemy Db1550 board
 * 
 * $Id: db1550-flash.c,v 1.7 2004/11/04 13:24:14 gleixner Exp $
 *
 * (C) 2004 Embedded Edge, LLC, based on db1550-flash.c:
 * (C) 2003, 2004 Pete Popov <ppopov@embeddedalley.com>
 * 
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
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

static unsigned long window_addr;
static unsigned long window_size;


static struct map_info db1550_map = {
	.name =	"Db1550 flash",
};

static unsigned char flash_bankwidth = 4;

/* 
 * Support only 64MB NOR Flash parts
 */

#if defined(CONFIG_MTD_DB1550_BOOT) && defined(CONFIG_MTD_DB1550_USER)
#define DB1550_BOTH_BANKS
#elif defined(CONFIG_MTD_DB1550_BOOT) && !defined(CONFIG_MTD_DB1550_USER)
#define DB1550_BOOT_ONLY
#elif !defined(CONFIG_MTD_DB1550_BOOT) && defined(CONFIG_MTD_DB1550_USER)
#define DB1550_USER_ONLY
#endif

#ifdef DB1550_BOTH_BANKS
/* both banks will be used. Combine the first bank and the first 
 * part of the second bank together into a single jffs/jffs2
 * partition.
 */
static struct mtd_partition db1550_partitions[] = {
	/* assume boot[2:0]:swap is '0000' or '1000', which translates to:
	 * 1C00 0000 1FFF FFFF CE0 64MB Boot NOR Flash
	 * 1800 0000 1BFF FFFF CE0 64MB Param NOR Flash
	 */
        {
                .name = "User FS",
                .size =   (0x1FC00000 - 0x18000000),
                .offset = 0x0000000
        },{
                .name = "yamon",
                .size = 0x0100000,
		.offset = MTDPART_OFS_APPEND,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw kernel",
		.size = (0x300000 - 0x40000), /* last 256KB is yamon env */
		.offset = MTDPART_OFS_APPEND,
        }
};
#elif defined(DB1550_BOOT_ONLY)
static struct mtd_partition db1550_partitions[] = {
	/* assume boot[2:0]:swap is '0000' or '1000', which translates to:
	 * 1C00 0000 1FFF FFFF CE0 64MB Boot NOR Flash
	 */
        {
                .name = "User FS",
                .size =   0x03c00000,
                .offset = 0x0000000
        },{
                .name = "yamon",
                .size = 0x0100000,
		.offset = MTDPART_OFS_APPEND,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw kernel",
		.size = (0x300000-0x40000), /* last 256KB is yamon env */
		.offset = MTDPART_OFS_APPEND,
        }
};
#elif defined(DB1550_USER_ONLY)
static struct mtd_partition db1550_partitions[] = {
	/* assume boot[2:0]:swap is '0000' or '1000', which translates to:
	 * 1800 0000 1BFF FFFF CE0 64MB Param NOR Flash
	 */
        {
                .name = "User FS",
                .size = (0x4000000 - 0x200000), /* reserve 2MB for raw kernel */
                .offset = 0x0000000
        },{
                .name = "raw kernel",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
        }
};
#else
#error MTD_DB1550 define combo error /* should never happen */
#endif

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

static struct mtd_info *mymtd;

/*
 * Probe the flash density and setup window address and size
 * based on user CONFIG options. There are times when we don't
 * want the MTD driver to be probing the boot or user flash,
 * so having the option to enable only one bank is important.
 */
int setup_flash_params(void)
{
#if defined(DB1550_BOTH_BANKS)
			window_addr = 0x18000000;
			window_size = 0x8000000; 
#elif defined(DB1550_BOOT_ONLY)
			window_addr = 0x1C000000;
			window_size = 0x4000000; 
#else /* USER ONLY */
			window_addr = 0x18000000;
			window_size = 0x4000000; 
#endif
	return 0;
}

int __init db1550_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	
	/* Default flash bankwidth */
	db1550_map.bankwidth = flash_bankwidth;

	if (setup_flash_params()) 
		return -ENXIO;

	/*
	 * Static partition definition selection
	 */
	parts = db1550_partitions;
	nb_parts = NB_OF(db1550_partitions);
	db1550_map.size = window_size;

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "Db1550 flash: probing %d-bit flash bus\n", 
			db1550_map.bankwidth*8);
	db1550_map.virt = ioremap(window_addr, window_size);
	mymtd = do_map_probe("cfi_probe", &db1550_map);
	if (!mymtd) return -ENXIO;
	mymtd->owner = THIS_MODULE;

	add_mtd_partitions(mymtd, parts, nb_parts);
	return 0;
}

static void __exit db1550_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		iounmap((void *) db1550_map.virt);
	}
}

module_init(db1550_mtd_init);
module_exit(db1550_mtd_cleanup);

MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Db1550 mtd map driver");
MODULE_LICENSE("GPL");
