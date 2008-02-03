/*
 * Physical mapping layer for MTD using the Axis partitiontable format
 *
 * Copyright (c) 2001, 2002, 2003 Axis Communications AB
 *
 * This file is under the GPL.
 *
 * First partition is always sector 0 regardless of if we find a partitiontable
 * or not. In the start of the next sector, there can be a partitiontable that
 * tells us what other partitions to define. If there isn't, we use a default
 * partition split defined below.
 *
 * Copy of os/lx25/arch/cris/arch-v10/drivers/axisflashmap.c 1.5
 * with minor changes.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/mtd/concat.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/mtdram.h>
#include <linux/mtd/partitions.h>

#include <asm/arch/hwregs/config_defs.h>
#include <asm/axisflashmap.h>
#include <asm/mmu.h>

#define MEM_CSE0_SIZE (0x04000000)
#define MEM_CSE1_SIZE (0x04000000)

#define FLASH_UNCACHED_ADDR  KSEG_E
#define FLASH_CACHED_ADDR    KSEG_F

#if CONFIG_ETRAX_FLASH_BUSWIDTH==1
#define flash_data __u8
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==2
#define flash_data __u16
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==4
#define flash_data __u16
#endif

/* From head.S */
extern unsigned long romfs_start, romfs_length, romfs_in_flash;

/* The master mtd for the entire flash. */
struct mtd_info* axisflash_mtd = NULL;

/* Map driver functions. */

static map_word flash_read(struct map_info *map, unsigned long ofs)
{
	map_word tmp;
	tmp.x[0] = *(flash_data *)(map->map_priv_1 + ofs);
	return tmp;
}

static void flash_copy_from(struct map_info *map, void *to,
			    unsigned long from, ssize_t len)
{
	memcpy(to, (void *)(map->map_priv_1 + from), len);
}

static void flash_write(struct map_info *map, map_word d, unsigned long adr)
{
	*(flash_data *)(map->map_priv_1 + adr) = (flash_data)d.x[0];
}

/*
 * The map for chip select e0.
 *
 * We run into tricky coherence situations if we mix cached with uncached
 * accesses to we only use the uncached version here.
 *
 * The size field is the total size where the flash chips may be mapped on the
 * chip select. MTD probes should find all devices there and it does not matter
 * if there are unmapped gaps or aliases (mirrors of flash devices). The MTD
 * probes will ignore them.
 *
 * The start address in map_priv_1 is in virtual memory so we cannot use
 * MEM_CSE0_START but must rely on that FLASH_UNCACHED_ADDR is the start
 * address of cse0.
 */
static struct map_info map_cse0 = {
	.name = "cse0",
	.size = MEM_CSE0_SIZE,
	.bankwidth = CONFIG_ETRAX_FLASH_BUSWIDTH,
	.read = flash_read,
	.copy_from = flash_copy_from,
	.write = flash_write,
	.map_priv_1 = FLASH_UNCACHED_ADDR
};

/*
 * The map for chip select e1.
 *
 * If there was a gap between cse0 and cse1, map_priv_1 would get the wrong
 * address, but there isn't.
 */
static struct map_info map_cse1 = {
	.name = "cse1",
	.size = MEM_CSE1_SIZE,
	.bankwidth = CONFIG_ETRAX_FLASH_BUSWIDTH,
	.read = flash_read,
	.copy_from = flash_copy_from,
	.write = flash_write,
	.map_priv_1 = FLASH_UNCACHED_ADDR + MEM_CSE0_SIZE
};

/* If no partition-table was found, we use this default-set. */
#define MAX_PARTITIONS         7
#define NUM_DEFAULT_PARTITIONS 3

/*
 * Default flash size is 2MB. CONFIG_ETRAX_PTABLE_SECTOR is most likely the
 * size of one flash block and "filesystem"-partition needs 5 blocks to be able
 * to use JFFS.
 */
static struct mtd_partition axis_default_partitions[NUM_DEFAULT_PARTITIONS] = {
	{
		.name = "boot firmware",
		.size = CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0
	},
	{
		.name = "kernel",
		.size = 0x200000 - (6 * CONFIG_ETRAX_PTABLE_SECTOR),
		.offset = CONFIG_ETRAX_PTABLE_SECTOR
	},
	{
		.name = "filesystem",
		.size = 5 * CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0x200000 - (5 * CONFIG_ETRAX_PTABLE_SECTOR)
	}
};

/* Initialize the ones normally used. */
static struct mtd_partition axis_partitions[MAX_PARTITIONS] = {
	{
		.name = "part0",
		.size = CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0
	},
	{
		.name = "part1",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part2",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part3",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part4",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part5",
		.size = 0,
		.offset = 0
	},
	{
		.name = "part6",
		.size = 0,
		.offset = 0
	},
};

/*
 * Probe a chip select for AMD-compatible (JEDEC) or CFI-compatible flash
 * chips in that order (because the amd_flash-driver is faster).
 */
static struct mtd_info *probe_cs(struct map_info *map_cs)
{
	struct mtd_info *mtd_cs = NULL;

	printk(KERN_INFO
	       "%s: Probing a 0x%08lx bytes large window at 0x%08lx.\n",
	       map_cs->name, map_cs->size, map_cs->map_priv_1);

#ifdef CONFIG_MTD_CFI
		mtd_cs = do_map_probe("cfi_probe", map_cs);
#endif
#ifdef CONFIG_MTD_JEDECPROBE
	if (!mtd_cs)
		mtd_cs = do_map_probe("jedec_probe", map_cs);
#endif

	return mtd_cs;
}

/*
 * Probe each chip select individually for flash chips. If there are chips on
 * both cse0 and cse1, the mtd_info structs will be concatenated to one struct
 * so that MTD partitions can cross chip boundaries.
 *
 * The only known restriction to how you can mount your chips is that each
 * chip select must hold similar flash chips. But you need external hardware
 * to do that anyway and you can put totally different chips on cse0 and cse1
 * so it isn't really much of a restriction.
 */
extern struct mtd_info* __init crisv32_nand_flash_probe (void);
static struct mtd_info *flash_probe(void)
{
	struct mtd_info *mtd_cse0;
	struct mtd_info *mtd_cse1;
	struct mtd_info *mtd_nand = NULL;
	struct mtd_info *mtd_total;
	struct mtd_info *mtds[3];
	int count = 0;

	if ((mtd_cse0 = probe_cs(&map_cse0)) != NULL)
		mtds[count++] = mtd_cse0;
	if ((mtd_cse1 = probe_cs(&map_cse1)) != NULL)
		mtds[count++] = mtd_cse1;

#ifdef CONFIG_ETRAX_NANDFLASH
	if ((mtd_nand = crisv32_nand_flash_probe()) != NULL)
		mtds[count++] = mtd_nand;
#endif

	if (!mtd_cse0 && !mtd_cse1 && !mtd_nand) {
		/* No chip found. */
		return NULL;
	}

	if (count > 1) {
#ifdef CONFIG_MTD_CONCAT
		/* Since the concatenation layer adds a small overhead we
		 * could try to figure out if the chips in cse0 and cse1 are
		 * identical and reprobe the whole cse0+cse1 window. But since
		 * flash chips are slow, the overhead is relatively small.
		 * So we use the MTD concatenation layer instead of further
		 * complicating the probing procedure.
		 */
		mtd_total = mtd_concat_create(mtds,
		                              count,
		                              "cse0+cse1+nand");
#else
		printk(KERN_ERR "%s and %s: Cannot concatenate due to kernel "
		       "(mis)configuration!\n", map_cse0.name, map_cse1.name);
		mtd_toal = NULL;
#endif
		if (!mtd_total) {
			printk(KERN_ERR "%s and %s: Concatenation failed!\n",
			       map_cse0.name, map_cse1.name);

			/* The best we can do now is to only use what we found
			 * at cse0.
			 */
			mtd_total = mtd_cse0;
			map_destroy(mtd_cse1);
		}
	} else {
		mtd_total = mtd_cse0? mtd_cse0 : mtd_cse1 ? mtd_cse1 : mtd_nand;

	}

	return mtd_total;
}

extern unsigned long crisv32_nand_boot;
extern unsigned long crisv32_nand_cramfs_offset;

/*
 * Probe the flash chip(s) and, if it succeeds, read the partition-table
 * and register the partitions with MTD.
 */
static int __init init_axis_flash(void)
{
	struct mtd_info *mymtd;
	int err = 0;
	int pidx = 0;
	struct partitiontable_head *ptable_head = NULL;
	struct partitiontable_entry *ptable;
	int use_default_ptable = 1; /* Until proven otherwise. */
	const char *pmsg = KERN_INFO "  /dev/flash%d at 0x%08x, size 0x%08x\n";
	static char page[512];
	size_t len;

#ifndef CONFIG_ETRAXFS_SIM
	mymtd = flash_probe();
	mymtd->read(mymtd, CONFIG_ETRAX_PTABLE_SECTOR, 512, &len, page);
	ptable_head = (struct partitiontable_head *)(page + PARTITION_TABLE_OFFSET);

	if (!mymtd) {
		/* There's no reason to use this module if no flash chip can
		 * be identified. Make sure that's understood.
		 */
		printk(KERN_INFO "axisflashmap: Found no flash chip.\n");
	} else {
		printk(KERN_INFO "%s: 0x%08x bytes of flash memory.\n",
		       mymtd->name, mymtd->size);
		axisflash_mtd = mymtd;
	}

	if (mymtd) {
		mymtd->owner = THIS_MODULE;
	}
	pidx++;  /* First partition is always set to the default. */

	if (ptable_head && (ptable_head->magic == PARTITION_TABLE_MAGIC)
	    && (ptable_head->size <
		(MAX_PARTITIONS * sizeof(struct partitiontable_entry) +
		PARTITIONTABLE_END_MARKER_SIZE))
	    && (*(unsigned long*)((void*)ptable_head + sizeof(*ptable_head) +
				  ptable_head->size -
				  PARTITIONTABLE_END_MARKER_SIZE)
		== PARTITIONTABLE_END_MARKER)) {
		/* Looks like a start, sane length and end of a
		 * partition table, lets check csum etc.
		 */
		int ptable_ok = 0;
		struct partitiontable_entry *max_addr =
			(struct partitiontable_entry *)
			((unsigned long)ptable_head + sizeof(*ptable_head) +
			 ptable_head->size);
		unsigned long offset = CONFIG_ETRAX_PTABLE_SECTOR;
		unsigned char *p;
		unsigned long csum = 0;

		ptable = (struct partitiontable_entry *)
			((unsigned long)ptable_head + sizeof(*ptable_head));

		/* Lets be PARANOID, and check the checksum. */
		p = (unsigned char*) ptable;

		while (p <= (unsigned char*)max_addr) {
			csum += *p++;
			csum += *p++;
			csum += *p++;
			csum += *p++;
		}
		ptable_ok = (csum == ptable_head->checksum);

		/* Read the entries and use/show the info.  */
		printk(KERN_INFO " Found a%s partition table at 0x%p-0x%p.\n",
		       (ptable_ok ? " valid" : "n invalid"), ptable_head,
		       max_addr);

		/* We have found a working bootblock.  Now read the
		 * partition table.  Scan the table.  It ends when
		 * there is 0xffffffff, that is, empty flash.
		 */
		while (ptable_ok
		       && ptable->offset != 0xffffffff
		       && ptable < max_addr
		       && pidx < MAX_PARTITIONS) {

			axis_partitions[pidx].offset = offset + ptable->offset + (crisv32_nand_boot ? 16384 : 0);
			axis_partitions[pidx].size = ptable->size;

			printk(pmsg, pidx, axis_partitions[pidx].offset,
			       axis_partitions[pidx].size);
			pidx++;
			ptable++;
		}
		use_default_ptable = !ptable_ok;
	}

	if (romfs_in_flash) {
		/* Add an overlapping device for the root partition (romfs). */

		axis_partitions[pidx].name = "romfs";
		if (crisv32_nand_boot) {
			char* data = kmalloc(1024, GFP_KERNEL);
			int len;
			int offset = crisv32_nand_cramfs_offset & ~(1024-1);
			char* tmp;

			mymtd->read(mymtd, offset, 1024, &len, data);
			tmp = &data[crisv32_nand_cramfs_offset % 512];
			axis_partitions[pidx].size = *(unsigned*)(tmp + 4);
			axis_partitions[pidx].offset = crisv32_nand_cramfs_offset;
			kfree(data);
		} else {
			axis_partitions[pidx].size = romfs_length;
			axis_partitions[pidx].offset = romfs_start - FLASH_CACHED_ADDR;
		}

		axis_partitions[pidx].mask_flags |= MTD_WRITEABLE;

		printk(KERN_INFO
                       " Adding readonly flash partition for romfs image:\n");
		printk(pmsg, pidx, axis_partitions[pidx].offset,
		       axis_partitions[pidx].size);
		pidx++;
	}

        if (mymtd) {
		if (use_default_ptable) {
			printk(KERN_INFO " Using default partition table.\n");
			err = add_mtd_partitions(mymtd, axis_default_partitions,
						 NUM_DEFAULT_PARTITIONS);
		} else {
			err = add_mtd_partitions(mymtd, axis_partitions, pidx);
		}

		if (err) {
			panic("axisflashmap could not add MTD partitions!\n");
		}
	}
/* CONFIG_EXTRAXFS_SIM */
#endif

	if (!romfs_in_flash) {
		/* Create an RAM device for the root partition (romfs). */

#if !defined(CONFIG_MTD_MTDRAM) || (CONFIG_MTDRAM_TOTAL_SIZE != 0) || (CONFIG_MTDRAM_ABS_POS != 0)
		/* No use trying to boot this kernel from RAM. Panic! */
		printk(KERN_EMERG "axisflashmap: Cannot create an MTD RAM "
		       "device due to kernel (mis)configuration!\n");
		panic("This kernel cannot boot from RAM!\n");
#else
		struct mtd_info *mtd_ram;

		mtd_ram = kmalloc(sizeof(struct mtd_info),
						     GFP_KERNEL);
		if (!mtd_ram) {
			panic("axisflashmap couldn't allocate memory for "
			      "mtd_info!\n");
		}

		printk(KERN_INFO " Adding RAM partition for romfs image:\n");
		printk(pmsg, pidx, romfs_start, romfs_length);

		err = mtdram_init_device(mtd_ram, (void*)romfs_start,
		                         romfs_length, "romfs");
		if (err) {
			panic("axisflashmap could not initialize MTD RAM "
			      "device!\n");
		}
#endif
	}

	return err;
}

/* This adds the above to the kernels init-call chain. */
module_init(init_axis_flash);

EXPORT_SYMBOL(axisflash_mtd);
