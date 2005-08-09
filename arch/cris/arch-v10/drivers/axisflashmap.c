/*
 * Physical mapping layer for MTD using the Axis partitiontable format
 *
 * Copyright (c) 2001, 2002 Axis Communications AB
 *
 * This file is under the GPL.
 *
 * First partition is always sector 0 regardless of if we find a partitiontable
 * or not. In the start of the next sector, there can be a partitiontable that
 * tells us what other partitions to define. If there isn't, we use a default
 * partition split defined below.
 *
 * $Log: axisflashmap.c,v $
 * Revision 1.11  2004/11/15 10:27:14  starvik
 * Corrected typo (Thanks to Milton Miller <miltonm@bga.com>).
 *
 * Revision 1.10  2004/08/16 12:37:22  starvik
 * Merge of Linux 2.6.8
 *
 * Revision 1.8  2004/05/14 07:58:03  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.6  2003/07/04 08:27:37  starvik
 * Merge of Linux 2.5.74
 *
 * Revision 1.5  2002/12/11 13:13:57  starvik
 * Added arch/ to v10 specific includes
 * Added fix from Linux 2.4 in serial.c (flush_to_flip_buffer)
 *
 * Revision 1.4  2002/11/20 11:56:10  starvik
 * Merge of Linux 2.5.48
 *
 * Revision 1.3  2002/11/13 14:54:13  starvik
 * Copied from linux 2.4
 *
 * Revision 1.28  2002/10/01 08:08:43  jonashg
 * The first partition ends at the start of the partition table.
 *
 * Revision 1.27  2002/08/21 09:23:13  jonashg
 * Speling.
 *
 * Revision 1.26  2002/08/21 08:35:20  jonashg
 * Cosmetic change to printouts.
 *
 * Revision 1.25  2002/08/21 08:15:42  jonashg
 * Made it compile even without CONFIG_MTD_CONCAT defined.
 *
 * Revision 1.24  2002/08/20 13:12:35  jonashg
 * * New approach to probing. Probe cse0 and cse1 separately and (mtd)concat
 *   the results.
 * * Removed compile time tests concerning how the mtdram driver has been
 *   configured. The user will know about the misconfiguration at runtime
 *   instead. (The old approach made it impossible to use mtdram for anything
 *   else than RAM boot).
 *
 * Revision 1.23  2002/05/13 12:12:28  johana
 * Allow compile without CONFIG_MTD_MTDRAM but warn at compiletime and
 * be informative at runtime.
 *
 * Revision 1.22  2002/05/13 10:24:44  johana
 * Added #if checks on MTDRAM CONFIG
 *
 * Revision 1.21  2002/05/06 16:05:20  johana
 * Removed debug printout.
 *
 * Revision 1.20  2002/05/06 16:03:00  johana
 * No more cramfs as root hack in generic code.
 * It's handled by axisflashmap using mtdram.
 *
 * Revision 1.19  2002/03/15 17:10:28  bjornw
 * Changed comment about cached access since we changed this before
 *
 * Revision 1.18  2002/03/05 17:06:15  jonashg
 * Try amd_flash probe before cfi_probe since amd_flash driver can handle two
 * (or more) flash chips of different model and the cfi driver cannot.
 *
 * Revision 1.17  2001/11/12 19:42:38  pkj
 * Fixed compiler warnings.
 *
 * Revision 1.16  2001/11/08 11:18:58  jonashg
 * Always read from uncached address to avoid problems with flushing
 * cachelines after write and MTD-erase. No performance loss have been
 * seen yet.
 *
 * Revision 1.15  2001/10/19 12:41:04  jonashg
 * Name of probe has changed in MTD.
 *
 * Revision 1.14  2001/09/21 07:14:10  jonashg
 * Made root filesystem (cramfs) use mtdblock driver when booting from flash.
 *
 * Revision 1.13  2001/08/15 13:57:35  jonashg
 * Entire MTD updated to the linux 2.4.7 version.
 *
 * Revision 1.12  2001/06/11 09:50:30  jonashg
 * Oops, 2MB is 0x200000 bytes.
 *
 * Revision 1.11  2001/06/08 11:39:44  jonashg
 * Changed sizes and offsets in axis_default_partitions to use
 * CONFIG_ETRAX_PTABLE_SECTOR.
 *
 * Revision 1.10  2001/05/29 09:42:03  jonashg
 * Use macro for end marker length instead of sizeof.
 *
 * Revision 1.9  2001/05/29 08:52:52  jonashg
 * Gave names to the magic fours (size of the ptable end marker).
 *
 * Revision 1.8  2001/05/28 15:36:20  jonashg
 * * Removed old comment about ptable location in flash (it's a CONFIG_ option).
 * * Variable ptable was initialized twice to the same value.
 *
 * Revision 1.7  2001/04/05 13:41:46  markusl
 * Updated according to review remarks
 *
 * Revision 1.6  2001/03/07 09:21:21  bjornw
 * No need to waste .data
 *
 * Revision 1.5  2001/03/06 16:27:01  jonashg
 * Probe the entire flash area for flash devices.
 *
 * Revision 1.4  2001/02/23 12:47:15  bjornw
 * Uncached flash in LOW_MAP moved from 0xe to 0x8
 *
 * Revision 1.3  2001/02/16 12:11:45  jonashg
 * MTD driver amd_flash is now included in MTD CVS repository.
 * (It's now in drivers/mtd).
 *
 * Revision 1.2  2001/02/09 11:12:22  jonashg
 * Support for AMD compatible non-CFI flash chips.
 * Only tested with Toshiba TC58FVT160 so far.
 *
 * Revision 1.1  2001/01/12 17:01:18  bjornw
 * * Added axisflashmap.c, a physical mapping for MTD that reads and understands
 *   Axis partition-table format.
 *
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>

#include <linux/mtd/concat.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/mtdram.h>
#include <linux/mtd/partitions.h>

#include <asm/axisflashmap.h>
#include <asm/mmu.h>
#include <asm/arch/sv_addr_ag.h>

#ifdef CONFIG_CRIS_LOW_MAP
#define FLASH_UNCACHED_ADDR  KSEG_8
#define FLASH_CACHED_ADDR    KSEG_5
#else
#define FLASH_UNCACHED_ADDR  KSEG_E
#define FLASH_CACHED_ADDR    KSEG_F
#endif

#if CONFIG_ETRAX_FLASH_BUSWIDTH==1
#define flash_data __u8
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==2
#define flash_data __u16
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==4
#define flash_data __u32
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

#ifdef CONFIG_MTD_AMDSTD
	mtd_cs = do_map_probe("amd_flash", map_cs);
#endif
#ifdef CONFIG_MTD_CFI
	if (!mtd_cs) {
		mtd_cs = do_map_probe("cfi_probe", map_cs);
	}
#endif

	return mtd_cs;
}

/* 
 * Probe each chip select individually for flash chips. If there are chips on
 * both cse0 and cse1, the mtd_info structs will be concatenated to one struct
 * so that MTD partitions can cross chip boundries.
 *
 * The only known restriction to how you can mount your chips is that each
 * chip select must hold similar flash chips. But you need external hardware
 * to do that anyway and you can put totally different chips on cse0 and cse1
 * so it isn't really much of a restriction.
 */
static struct mtd_info *flash_probe(void)
{
	struct mtd_info *mtd_cse0;
	struct mtd_info *mtd_cse1;
	struct mtd_info *mtd_cse;

	mtd_cse0 = probe_cs(&map_cse0);
	mtd_cse1 = probe_cs(&map_cse1);

	if (!mtd_cse0 && !mtd_cse1) {
		/* No chip found. */
		return NULL;
	}

	if (mtd_cse0 && mtd_cse1) {
#ifdef CONFIG_MTD_CONCAT
		struct mtd_info *mtds[] = { mtd_cse0, mtd_cse1 };
		
		/* Since the concatenation layer adds a small overhead we
		 * could try to figure out if the chips in cse0 and cse1 are
		 * identical and reprobe the whole cse0+cse1 window. But since
		 * flash chips are slow, the overhead is relatively small.
		 * So we use the MTD concatenation layer instead of further
		 * complicating the probing procedure.
		 */
		mtd_cse = mtd_concat_create(mtds,
					    sizeof(mtds) / sizeof(mtds[0]),
					    "cse0+cse1");
#else
		printk(KERN_ERR "%s and %s: Cannot concatenate due to kernel "
		       "(mis)configuration!\n", map_cse0.name, map_cse1.name);
		mtd_cse = NULL;
#endif
		if (!mtd_cse) {
			printk(KERN_ERR "%s and %s: Concatenation failed!\n",
			       map_cse0.name, map_cse1.name);

			/* The best we can do now is to only use what we found
			 * at cse0.
			 */ 
			mtd_cse = mtd_cse0;
			map_destroy(mtd_cse1);
		}
	} else {
		mtd_cse = mtd_cse0? mtd_cse0 : mtd_cse1;
	}

	return mtd_cse;
}

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
	const char *pmsg = "  /dev/flash%d at 0x%08x, size 0x%08x\n";

	if (!(mymtd = flash_probe())) {
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
		ptable_head = (struct partitiontable_head *)(FLASH_CACHED_ADDR +
			      CONFIG_ETRAX_PTABLE_SECTOR +
			      PARTITION_TABLE_OFFSET);
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

			axis_partitions[pidx].offset = offset + ptable->offset;
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
		axis_partitions[pidx].size = romfs_length;
		axis_partitions[pidx].offset = romfs_start - FLASH_CACHED_ADDR;
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

	if (!romfs_in_flash) {
		/* Create an RAM device for the root partition (romfs). */

#if !defined(CONFIG_MTD_MTDRAM) || (CONFIG_MTDRAM_TOTAL_SIZE != 0) || (CONFIG_MTDRAM_ABS_POS != 0)
		/* No use trying to boot this kernel from RAM. Panic! */
		printk(KERN_EMERG "axisflashmap: Cannot create an MTD RAM "
		       "device due to kernel (mis)configuration!\n");
		panic("This kernel cannot boot from RAM!\n");
#else
		struct mtd_info *mtd_ram;

		mtd_ram = (struct mtd_info *)kmalloc(sizeof(struct mtd_info),
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
