/*
 * Flash memory access on Alchemy Pb1550 board
 * 
 * $Id: pb1550-flash.c,v 1.6 2004/11/04 13:24:15 gleixner Exp $
 *
 * (C) 2004 Embedded Edge, LLC, based on pb1550-flash.c:
 * (C) 2003 Pete Popov <ppopov@pacbell.net>
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
#include <asm/au1000.h>
#include <asm/pb1550.h>

#ifdef 	DEBUG_RW
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)	
#endif

static unsigned long window_addr;
static unsigned long window_size;


static struct map_info pb1550_map = {
	.name =	"Pb1550 flash",
};

static unsigned char flash_bankwidth = 4;

/* 
 * Support only 64MB NOR Flash parts
 */

#ifdef PB1550_BOTH_BANKS
/* both banks will be used. Combine the first bank and the first 
 * part of the second bank together into a single jffs/jffs2
 * partition.
 */
static struct mtd_partition pb1550_partitions[] = {
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
#elif defined(PB1550_BOOT_ONLY)
static struct mtd_partition pb1550_partitions[] = {
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
#elif defined(PB1550_USER_ONLY)
static struct mtd_partition pb1550_partitions[] = {
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
#error MTD_PB1550 define combo error /* should never happen */
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
	u16 boot_swapboot;
	boot_swapboot = (au_readl(MEM_STSTAT) & (0x7<<1)) | 
		((bcsr->status >> 6)  & 0x1);
	printk("Pb1550 MTD: boot:swap %d\n", boot_swapboot);

	switch (boot_swapboot) {
		case 0: /* 512Mbit devices, both enabled */
		case 1: 
		case 8:
		case 9: 
#if defined(PB1550_BOTH_BANKS)
			window_addr = 0x18000000;
			window_size = 0x8000000; 
#elif defined(PB1550_BOOT_ONLY)
			window_addr = 0x1C000000;
			window_size = 0x4000000; 
#else /* USER ONLY */
			window_addr = 0x1E000000;
			window_size = 0x4000000; 
#endif
			break;
		case 0xC:
		case 0xD:
		case 0xE:
		case 0xF: 
			/* 64 MB Boot NOR Flash is disabled */
			/* and the start address is moved to 0x0C00000 */
			window_addr = 0x0C000000;
			window_size = 0x4000000; 
		default:
			printk("Pb1550 MTD: unsupported boot:swap setting\n");
			return 1;
	}
	return 0;
}

int __init pb1550_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	
	/* Default flash bankwidth */
	pb1550_map.bankwidth = flash_bankwidth;

	if (setup_flash_params()) 
		return -ENXIO;

	/*
	 * Static partition definition selection
	 */
	parts = pb1550_partitions;
	nb_parts = NB_OF(pb1550_partitions);
	pb1550_map.size = window_size;

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "Pb1550 flash: probing %d-bit flash bus\n", 
			pb1550_map.bankwidth*8);
	pb1550_map.virt = ioremap(window_addr, window_size);
	mymtd = do_map_probe("cfi_probe", &pb1550_map);
	if (!mymtd) return -ENXIO;
	mymtd->owner = THIS_MODULE;

	add_mtd_partitions(mymtd, parts, nb_parts);
	return 0;
}

static void __exit pb1550_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
}

module_init(pb1550_mtd_init);
module_exit(pb1550_mtd_cleanup);

MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Pb1550 mtd map driver");
MODULE_LICENSE("GPL");
