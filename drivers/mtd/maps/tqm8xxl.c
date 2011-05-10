/*
 * Handle mapping of the flash memory access routines
 * on TQM8xxL based devices.
 *
 * based on rpxlite.c
 *
 * Copyright(C) 2001 Kirk Lee <kirk@hpc.ee.ntu.edu.tw>
 *
 * This code is GPLed
 *
 */

/*
 * According to TQM8xxL hardware manual, TQM8xxL series have
 * following flash memory organisations:
 *	| capacity |	| chip type |	| bank0 |	| bank1 |
 *	    2MiB	   512Kx16	  2MiB		   0
 *	    4MiB	   1Mx16	  4MiB		   0
 *	    8MiB	   1Mx16	  4MiB		   4MiB
 * Thus, we choose CONFIG_MTD_CFI_I2 & CONFIG_MTD_CFI_B4 at
 * kernel configuration.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#define FLASH_ADDR 0x40000000
#define FLASH_SIZE 0x00800000
#define FLASH_BANK_MAX 4

// trivial struct to describe partition information
struct mtd_part_def
{
	int nums;
	unsigned char *type;
	struct mtd_partition* mtd_part;
};

//static struct mtd_info *mymtd;
static struct mtd_info* mtd_banks[FLASH_BANK_MAX];
static struct map_info* map_banks[FLASH_BANK_MAX];
static struct mtd_part_def part_banks[FLASH_BANK_MAX];
static unsigned long num_banks;
static void __iomem *start_scan_addr;

/*
 * Here are partition information for all known TQM8xxL series devices.
 * See include/linux/mtd/partitions.h for definition of the mtd_partition
 * structure.
 *
 * The *_max_flash_size is the maximum possible mapped flash size which
 * is not necessarily the actual flash size.  It must correspond to the
 * value specified in the mapping definition defined by the
 * "struct map_desc *_io_desc" for the corresponding machine.
 */

#ifdef CONFIG_MTD_PARTITIONS
/* Currently, TQM8xxL has up to 8MiB flash */
static unsigned long tqm8xxl_max_flash_size = 0x00800000;

/* partition definition for first flash bank
 * (cf. "drivers/char/flash_config.c")
 */
static struct mtd_partition tqm8xxl_partitions[] = {
	{
	  .name = "ppcboot",
	  .offset = 0x00000000,
	  .size = 0x00020000,           /* 128KB           */
	  .mask_flags = MTD_WRITEABLE,  /* force read-only */
	},
	{
	  .name = "kernel",             /* default kernel image */
	  .offset = 0x00020000,
	  .size = 0x000e0000,
	  .mask_flags = MTD_WRITEABLE,  /* force read-only */
	},
	{
	  .name = "user",
	  .offset = 0x00100000,
	  .size = 0x00100000,
	},
	{
	  .name = "initrd",
	  .offset = 0x00200000,
	  .size = 0x00200000,
	}
};
/* partition definition for second flash bank */
static struct mtd_partition tqm8xxl_fs_partitions[] = {
	{
	  .name = "cramfs",
	  .offset = 0x00000000,
	  .size = 0x00200000,
	},
	{
	  .name = "jffs",
	  .offset = 0x00200000,
	  .size = 0x00200000,
	  //.size = MTDPART_SIZ_FULL,
	}
};
#endif

static int __init init_tqm_mtd(void)
{
	int idx = 0, ret = 0;
	unsigned long flash_addr, flash_size, mtd_size = 0;
	/* pointer to TQM8xxL board info data */
	bd_t *bd = (bd_t *)__res;

	flash_addr = bd->bi_flashstart;
	flash_size = bd->bi_flashsize;

	//request maximum flash size address space
	start_scan_addr = ioremap(flash_addr, flash_size);
	if (!start_scan_addr) {
		printk(KERN_WARNING "%s:Failed to ioremap address:0x%x\n", __func__, flash_addr);
		return -EIO;
	}

	for (idx = 0 ; idx < FLASH_BANK_MAX ; idx++) {
		if(mtd_size >= flash_size)
			break;

		printk(KERN_INFO "%s: chip probing count %d\n", __func__, idx);

		map_banks[idx] = kzalloc(sizeof(struct map_info), GFP_KERNEL);
		if(map_banks[idx] == NULL) {
			ret = -ENOMEM;
			/* FIXME: What if some MTD devices were probed already? */
			goto error_mem;
		}

		map_banks[idx]->name = kmalloc(16, GFP_KERNEL);

		if (!map_banks[idx]->name) {
			ret = -ENOMEM;
			/* FIXME: What if some MTD devices were probed already? */
			goto error_mem;
		}
		sprintf(map_banks[idx]->name, "TQM8xxL%d", idx);

		map_banks[idx]->size = flash_size;
		map_banks[idx]->bankwidth = 4;

		simple_map_init(map_banks[idx]);

		map_banks[idx]->virt = start_scan_addr;
		map_banks[idx]->phys = flash_addr;
		/* FIXME: This looks utterly bogus, but I'm trying to
		   preserve the behaviour of the original (shown here)...

		map_banks[idx]->map_priv_1 =
		start_scan_addr + ((idx > 0) ?
		(mtd_banks[idx-1] ? mtd_banks[idx-1]->size : 0) : 0);
		*/

		if (idx && mtd_banks[idx-1]) {
			map_banks[idx]->virt += mtd_banks[idx-1]->size;
			map_banks[idx]->phys += mtd_banks[idx-1]->size;
		}

		//start to probe flash chips
		mtd_banks[idx] = do_map_probe("cfi_probe", map_banks[idx]);

		if (mtd_banks[idx]) {
			mtd_banks[idx]->owner = THIS_MODULE;
			mtd_size += mtd_banks[idx]->size;
			num_banks++;

			printk(KERN_INFO "%s: bank%d, name:%s, size:%dbytes \n", __func__, num_banks,
			mtd_banks[idx]->name, mtd_banks[idx]->size);
		}
	}

	/* no supported flash chips found */
	if (!num_banks) {
		printk(KERN_NOTICE "TQM8xxL: No support flash chips found!\n");
		ret = -ENXIO;
		goto error_mem;
	}

#ifdef CONFIG_MTD_PARTITIONS
	/*
	 * Select Static partition definitions
	 */
	part_banks[0].mtd_part = tqm8xxl_partitions;
	part_banks[0].type = "Static image";
	part_banks[0].nums = ARRAY_SIZE(tqm8xxl_partitions);

	part_banks[1].mtd_part = tqm8xxl_fs_partitions;
	part_banks[1].type = "Static file system";
	part_banks[1].nums = ARRAY_SIZE(tqm8xxl_fs_partitions);

	for(idx = 0; idx < num_banks ; idx++) {
		if (part_banks[idx].nums == 0) {
			printk(KERN_NOTICE "TQM flash%d: no partition info available, registering whole flash at once\n", idx);
			add_mtd_device(mtd_banks[idx]);
		} else {
			printk(KERN_NOTICE "TQM flash%d: Using %s partition definition\n",
					idx, part_banks[idx].type);
			add_mtd_partitions(mtd_banks[idx], part_banks[idx].mtd_part,
								part_banks[idx].nums);
		}
	}
#else
	printk(KERN_NOTICE "TQM flash: registering %d whole flash banks at once\n", num_banks);
	for(idx = 0 ; idx < num_banks ; idx++)
		add_mtd_device(mtd_banks[idx]);
#endif
	return 0;
error_mem:
	for(idx = 0 ; idx < FLASH_BANK_MAX ; idx++) {
		if(map_banks[idx] != NULL) {
			kfree(map_banks[idx]->name);
			map_banks[idx]->name = NULL;
			kfree(map_banks[idx]);
			map_banks[idx] = NULL;
		}
	}
error:
	iounmap(start_scan_addr);
	return ret;
}

static void __exit cleanup_tqm_mtd(void)
{
	unsigned int idx = 0;
	for(idx = 0 ; idx < num_banks ; idx++) {
		/* destroy mtd_info previously allocated */
		if (mtd_banks[idx]) {
			del_mtd_partitions(mtd_banks[idx]);
			map_destroy(mtd_banks[idx]);
		}
		/* release map_info not used anymore */
		kfree(map_banks[idx]->name);
		kfree(map_banks[idx]);
	}

	if (start_scan_addr) {
		iounmap(start_scan_addr);
		start_scan_addr = 0;
	}
}

module_init(init_tqm_mtd);
module_exit(cleanup_tqm_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kirk Lee <kirk@hpc.ee.ntu.edu.tw>");
MODULE_DESCRIPTION("MTD map driver for TQM8xxL boards");
