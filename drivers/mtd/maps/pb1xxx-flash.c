/*
 * Flash memory access on Alchemy Pb1xxx boards
 * 
 * (C) 2001 Pete Popov <ppopov@mvista.com>
 * 
 * $Id: pb1xxx-flash.c,v 1.14 2004/11/04 13:24:15 gleixner Exp $
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

#ifdef CONFIG_MIPS_PB1000

#define WINDOW_ADDR 0x1F800000
#define WINDOW_SIZE 0x800000

static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name         =  "yamon env",
                .size         =   0x00020000,
                .offset       =   0,
                .mask_flags   =   MTD_WRITEABLE},
	{
                .name         =   "User FS",
                .size         =   0x003e0000,
                .offset       =   0x20000,},
	{
                .name         =   "boot code",
                .size         =   0x100000,
                .offset       =   0x400000,
                .mask_flags   =   MTD_WRITEABLE},
	{
                .name         =   "raw/kernel",
                .size         =   0x300000,
                .offset       =   0x500000}
};

#elif defined(CONFIG_MIPS_PB1500) || defined(CONFIG_MIPS_PB1100)

#if defined(CONFIG_MTD_PB1500_BOOT) && defined(CONFIG_MTD_PB1500_USER)
/* both 32MB banks will be used. Combine the first 32MB bank and the
 * first 28MB of the second bank together into a single jffs/jffs2
 * partition.
 */
#define WINDOW_ADDR 0x1C000000
#define WINDOW_SIZE 0x4000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name         =   "User FS",
                .size         =   0x3c00000,
                .offset       =   0x0000000
        },{
                .name         =   "yamon",
                .size         =   0x0100000,
                .offset       =   0x3c00000,
                .mask_flags   =   MTD_WRITEABLE
        },{
                .name         =   "raw kernel",
                .size         =   0x02c0000,
                .offset       =   0x3d00000
        }
};
#elif defined(CONFIG_MTD_PB1500_BOOT) && !defined(CONFIG_MTD_PB1500_USER)
#define WINDOW_ADDR 0x1E000000
#define WINDOW_SIZE 0x2000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name         =   "User FS",
                .size         =   0x1c00000,
                .offset       =   0x0000000
        },{
                .name         =   "yamon",
                .size         =   0x0100000,
                .offset       =   0x1c00000,
                .mask_flags   =   MTD_WRITEABLE
        },{
                .name         =   "raw kernel",
                .size         =   0x02c0000,
                .offset       =   0x1d00000
        }
};
#elif !defined(CONFIG_MTD_PB1500_BOOT) && defined(CONFIG_MTD_PB1500_USER)
#define WINDOW_ADDR 0x1C000000
#define WINDOW_SIZE 0x2000000
static struct mtd_partition pb1xxx_partitions[] = {
        {
                .name         =   "User FS",
                .size         =    0x1e00000,
                .offset       =    0x0000000
        },{
                .name         =    "raw kernel",
                .size         =    0x0200000,
                .offset       =    0x1e00000,
        }
};
#else
#error MTD_PB1500 define combo error /* should never happen */
#endif
#else
#error Unsupported board
#endif

#define NAME     	"Pb1x00 Linux Flash"
#define PADDR    	WINDOW_ADDR
#define BUSWIDTH	4
#define SIZE		WINDOW_SIZE
#define PARTITIONS	4

static struct map_info pb1xxx_mtd_map = {
	.name		= NAME,
	.size		= SIZE,
	.bankwidth	= BUSWIDTH,
	.phys		= PADDR,
};

static struct mtd_info *pb1xxx_mtd;

int __init pb1xxx_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	char *part_type;
	
	/*
	 * Static partition definition selection
	 */
	part_type = "static";
	parts = pb1xxx_partitions;
	nb_parts = ARRAY_SIZE(pb1xxx_partitions);

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "Pb1xxx flash: probing %d-bit flash bus\n", 
			BUSWIDTH*8);
	pb1xxx_mtd_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	simple_map_init(&pb1xxx_mtd_map);

	pb1xxx_mtd = do_map_probe("cfi_probe", &pb1xxx_mtd_map);
	if (!pb1xxx_mtd) return -ENXIO;
	pb1xxx_mtd->owner = THIS_MODULE;

	add_mtd_partitions(pb1xxx_mtd, parts, nb_parts);
	return 0;
}

static void __exit pb1xxx_mtd_cleanup(void)
{
	if (pb1xxx_mtd) {
		del_mtd_partitions(pb1xxx_mtd);
		map_destroy(pb1xxx_mtd);
		iounmap((void *) pb1xxx_mtd_map.virt);
	}
}

module_init(pb1xxx_mtd_init);
module_exit(pb1xxx_mtd_cleanup);

MODULE_AUTHOR("Pete Popov");
MODULE_DESCRIPTION("Pb1xxx CFI map driver");
MODULE_LICENSE("GPL");
