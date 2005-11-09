/*
 * Handle mapping of the flash memory access routines on the SBC8240 board.
 *
 * Carolyn Smith, Tektronix, Inc.
 *
 * This code is GPLed
 *
 * $Id: sbc8240.c,v 1.5 2005/11/07 11:14:28 gleixner Exp $
 *
 */

/*
 * The SBC8240 has 2 flash banks.
 * Bank 0 is a 512 KiB AMD AM29F040B; 8 x 64 KiB sectors.
 * It contains the U-Boot code (7 sectors) and the environment (1 sector).
 * Bank 1 is 4 x 1 MiB AMD AM29LV800BT; 15 x 64 KiB sectors, 1 x 32 KiB sector,
 * 2 x 8 KiB sectors, 1 x 16 KiB sectors.
 * Both parts are JEDEC compatible.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/partitions.h>
#endif

#define	DEBUG

#ifdef	DEBUG
# define debugk(fmt,args...)	printk(fmt ,##args)
#else
# define debugk(fmt,args...)
#endif


#define WINDOW_ADDR0	0xFFF00000		/* 512 KiB */
#define WINDOW_SIZE0	0x00080000
#define BUSWIDTH0	1

#define WINDOW_ADDR1	0xFF000000		/* 4 MiB */
#define WINDOW_SIZE1	0x00400000
#define BUSWIDTH1	8

#define MSG_PREFIX "sbc8240:"	/* prefix for our printk()'s */
#define MTDID	   "sbc8240-%d"	/* for mtdparts= partitioning */


static struct map_info sbc8240_map[2] = {
	{
		.name           = "sbc8240 Flash Bank #0",
		.size           = WINDOW_SIZE0,
		.bankwidth       = BUSWIDTH0,
	},
	{
		.name           = "sbc8240 Flash Bank #1",
		.size           = WINDOW_SIZE1,
		.bankwidth       = BUSWIDTH1,
	}
};

#define NUM_FLASH_BANKS	(sizeof(sbc8240_map) / sizeof(struct map_info))

/*
 * The following defines the partition layout of SBC8240 boards.
 *
 * See include/linux/mtd/partitions.h for definition of the
 * mtd_partition structure.
 *
 * The *_max_flash_size is the maximum possible mapped flash size
 * which is not necessarily the actual flash size. It must correspond
 * to the value specified in the mapping definition defined by the
 * "struct map_desc *_io_desc" for the corresponding machine.
 */

#ifdef CONFIG_MTD_PARTITIONS

static struct mtd_partition sbc8240_uboot_partitions [] = {
	/* Bank 0 */
	{
		.name =	"U-boot",			/* U-Boot Firmware	*/
		.offset =	0,
		.size =	0x00070000,			/*  7 x 64 KiB sectors 	*/
		.mask_flags = MTD_WRITEABLE,		/*  force read-only	*/
	},
	{
		.name =	"environment",			/* U-Boot environment	*/
		.offset =	0x00070000,
		.size =	0x00010000,			/*  1 x 64 KiB sector	*/
	},
};

static struct mtd_partition sbc8240_fs_partitions [] = {
	{
		.name =	"jffs",				/* JFFS  filesystem	*/
		.offset =	0,
		.size =	0x003C0000,			/*  4 * 15 * 64KiB	*/
	},
	{
		.name =	"tmp32",
		.offset =	0x003C0000,
		.size =	0x00020000,			/*  4 * 32KiB		*/
	},
	{
		.name =	"tmp8a",
		.offset =	0x003E0000,
		.size =	0x00008000,			/*  4 * 8KiB		*/
	},
	{
		.name =	"tmp8b",
		.offset =	0x003E8000,
		.size =	0x00008000,			/*  4 * 8KiB		*/
	},
	{
		.name =	"tmp16",
		.offset =	0x003F0000,
		.size =	0x00010000,			/*  4 * 16KiB		*/
	}
};

#define NB_OF(x) (sizeof (x) / sizeof (x[0]))

/* trivial struct to describe partition information */
struct mtd_part_def
{
	int nums;
	unsigned char *type;
	struct mtd_partition* mtd_part;
};

static struct mtd_info *sbc8240_mtd[NUM_FLASH_BANKS];
static struct mtd_part_def sbc8240_part_banks[NUM_FLASH_BANKS];


#endif	/* CONFIG_MTD_PARTITIONS */


int __init init_sbc8240_mtd (void)
{
	static struct _cjs {
		u_long addr;
		u_long size;
	} pt[NUM_FLASH_BANKS] = {
		{
			.addr = WINDOW_ADDR0,
			.size = WINDOW_SIZE0
		},
		{
			.addr = WINDOW_ADDR1,
			.size = WINDOW_SIZE1
		},
	};

	int devicesfound = 0;
	int i;

	for (i = 0; i < NUM_FLASH_BANKS; i++) {
		printk (KERN_NOTICE MSG_PREFIX
			"Probing 0x%08lx at 0x%08lx\n", pt[i].size, pt[i].addr);

		sbc8240_map[i].map_priv_1 =
			(unsigned long) ioremap (pt[i].addr, pt[i].size);
		if (!sbc8240_map[i].map_priv_1) {
			printk (MSG_PREFIX "failed to ioremap\n");
			return -EIO;
		}
		simple_map_init(&sbc8240_mtd[i]);

		sbc8240_mtd[i] = do_map_probe("jedec_probe", &sbc8240_map[i]);

		if (sbc8240_mtd[i]) {
			sbc8240_mtd[i]->module = THIS_MODULE;
			devicesfound++;
		}
	}

	if (!devicesfound) {
		printk(KERN_NOTICE MSG_PREFIX
		       "No suppported flash chips found!\n");
		return -ENXIO;
	}

#ifdef CONFIG_MTD_PARTITIONS
	sbc8240_part_banks[0].mtd_part   = sbc8240_uboot_partitions;
	sbc8240_part_banks[0].type       = "static image";
	sbc8240_part_banks[0].nums       = NB_OF(sbc8240_uboot_partitions);
	sbc8240_part_banks[1].mtd_part   = sbc8240_fs_partitions;
	sbc8240_part_banks[1].type       = "static file system";
	sbc8240_part_banks[1].nums       = NB_OF(sbc8240_fs_partitions);

	for (i = 0; i < NUM_FLASH_BANKS; i++) {

		if (!sbc8240_mtd[i]) continue;
		if (sbc8240_part_banks[i].nums == 0) {
			printk (KERN_NOTICE MSG_PREFIX
				"No partition info available, registering whole device\n");
			add_mtd_device(sbc8240_mtd[i]);
		} else {
			printk (KERN_NOTICE MSG_PREFIX
				"Using %s partition definition\n", sbc8240_part_banks[i].mtd_part->name);
			add_mtd_partitions (sbc8240_mtd[i],
					    sbc8240_part_banks[i].mtd_part,
					    sbc8240_part_banks[i].nums);
		}
	}
#else
	printk(KERN_NOTICE MSG_PREFIX
	       "Registering %d flash banks at once\n", devicesfound);

	for (i = 0; i < devicesfound; i++) {
		add_mtd_device(sbc8240_mtd[i]);
	}
#endif	/* CONFIG_MTD_PARTITIONS */

	return devicesfound == 0 ? -ENXIO : 0;
}

static void __exit cleanup_sbc8240_mtd (void)
{
	int i;

	for (i = 0; i < NUM_FLASH_BANKS; i++) {
		if (sbc8240_mtd[i]) {
			del_mtd_device (sbc8240_mtd[i]);
			map_destroy (sbc8240_mtd[i]);
		}
		if (sbc8240_map[i].map_priv_1) {
			iounmap ((void *) sbc8240_map[i].map_priv_1);
			sbc8240_map[i].map_priv_1 = 0;
		}
	}
}

module_init (init_sbc8240_mtd);
module_exit (cleanup_sbc8240_mtd);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Carolyn Smith <carolyn.smith@tektronix.com>");
MODULE_DESCRIPTION ("MTD map driver for SBC8240 boards");

