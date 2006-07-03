/*
 * Flash memory access on AMD Alchemy evaluation boards
 *
 * $Id: alchemy-flash.c,v 1.2 2005/11/07 11:14:26 gleixner Exp $
 *
 * (C) 2003, 2004 Pete Popov <ppopov@embeddedalley.com>
 *
 */

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

#ifdef CONFIG_MIPS_PB1000
#define BOARD_MAP_NAME "Pb1000 Flash"
#define BOARD_FLASH_SIZE 0x00800000 /* 8MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_PB1500
#define BOARD_MAP_NAME "Pb1500 Flash"
#define BOARD_FLASH_SIZE 0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_PB1100
#define BOARD_MAP_NAME "Pb1100 Flash"
#define BOARD_FLASH_SIZE 0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_PB1550
#define BOARD_MAP_NAME "Pb1550 Flash"
#define BOARD_FLASH_SIZE 0x08000000 /* 128MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_PB1200
#define BOARD_MAP_NAME "Pb1200 Flash"
#define BOARD_FLASH_SIZE 0x08000000 /* 128MB */
#define BOARD_FLASH_WIDTH 2 /* 16-bits */
#endif

#ifdef CONFIG_MIPS_DB1000
#define BOARD_MAP_NAME "Db1000 Flash"
#define BOARD_FLASH_SIZE 0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_DB1500
#define BOARD_MAP_NAME "Db1500 Flash"
#define BOARD_FLASH_SIZE 0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_DB1100
#define BOARD_MAP_NAME "Db1100 Flash"
#define BOARD_FLASH_SIZE 0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_DB1550
#define BOARD_MAP_NAME "Db1550 Flash"
#define BOARD_FLASH_SIZE 0x08000000 /* 128MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#endif

#ifdef CONFIG_MIPS_DB1200
#define BOARD_MAP_NAME "Db1200 Flash"
#define BOARD_FLASH_SIZE 0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH 2 /* 16-bits */
#endif

#ifdef CONFIG_MIPS_HYDROGEN3
#define BOARD_MAP_NAME "Hydrogen3 Flash"
#define BOARD_FLASH_SIZE 0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#define USE_LOCAL_ACCESSORS /* why? */
#endif

#ifdef CONFIG_MIPS_BOSPORUS
#define BOARD_MAP_NAME "Bosporus Flash"
#define BOARD_FLASH_SIZE 0x01000000 /* 16MB */
#define BOARD_FLASH_WIDTH 2 /* 16-bits */
#endif

#ifdef CONFIG_MIPS_MIRAGE
#define BOARD_MAP_NAME "Mirage Flash"
#define BOARD_FLASH_SIZE 0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH 4 /* 32-bits */
#define USE_LOCAL_ACCESSORS /* why? */
#endif

static struct map_info alchemy_map = {
	.name =	BOARD_MAP_NAME,
};

static struct mtd_partition alchemy_partitions[] = {
        {
                .name = "User FS",
                .size = BOARD_FLASH_SIZE - 0x00400000,
                .offset = 0x0000000
        },{
                .name = "YAMON",
                .size = 0x0100000,
		.offset = MTDPART_OFS_APPEND,
                .mask_flags = MTD_WRITEABLE
        },{
                .name = "raw kernel",
		.size = (0x300000 - 0x40000), /* last 256KB is yamon env */
		.offset = MTDPART_OFS_APPEND,
        }
};

static struct mtd_info *mymtd;

int __init alchemy_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	unsigned long window_addr;
	unsigned long window_size;

	/* Default flash buswidth */
	alchemy_map.bankwidth = BOARD_FLASH_WIDTH;

	window_addr = 0x20000000 - BOARD_FLASH_SIZE;
	window_size = BOARD_FLASH_SIZE;
#ifdef CONFIG_MIPS_MIRAGE_WHY
	/* Boot ROM flash bank only; no user bank */
	window_addr = 0x1C000000;
	window_size = 0x04000000;
	/* USERFS from 0x1C00 0000 to 0x1FC00000 */
	alchemy_partitions[0].size = 0x03C00000;
#endif

	/*
	 * Static partition definition selection
	 */
	parts = alchemy_partitions;
	nb_parts = ARRAY_SIZE(alchemy_partitions);
	alchemy_map.size = window_size;

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE BOARD_MAP_NAME ": probing %d-bit flash bus\n",
			alchemy_map.bankwidth*8);
	alchemy_map.virt = ioremap(window_addr, window_size);
	mymtd = do_map_probe("cfi_probe", &alchemy_map);
	if (!mymtd) {
		iounmap(alchemy_map.virt);
		return -ENXIO;
	}
	mymtd->owner = THIS_MODULE;

	add_mtd_partitions(mymtd, parts, nb_parts);
	return 0;
}

static void __exit alchemy_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		iounmap(alchemy_map.virt);
	}
}

module_init(alchemy_mtd_init);
module_exit(alchemy_mtd_cleanup);

MODULE_AUTHOR("Embedded Alley Solutions, Inc");
MODULE_DESCRIPTION(BOARD_MAP_NAME " MTD driver");
MODULE_LICENSE("GPL");
