/*
 * Physical mapping layer for MTD using the Axis partitiontable format
 *
 * Copyright (c) 2001-2007 Axis Communications AB
 *
 * This file is under the GPL.
 *
 * First partition is always sector 0 regardless of if we find a partitiontable
 * or not. In the start of the next sector, there can be a partitiontable that
 * tells us what other partitions to define. If there isn't, we use a default
 * partition split defined below.
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

#include <linux/cramfs_fs.h>

#include <asm/axisflashmap.h>
#include <asm/mmu.h>

#define MEM_CSE0_SIZE (0x04000000)
#define MEM_CSE1_SIZE (0x04000000)

#define FLASH_UNCACHED_ADDR  KSEG_E
#define FLASH_CACHED_ADDR    KSEG_F

#define PAGESIZE (512)

#if CONFIG_ETRAX_FLASH_BUSWIDTH==1
#define flash_data __u8
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==2
#define flash_data __u16
#elif CONFIG_ETRAX_FLASH_BUSWIDTH==4
#define flash_data __u32
#endif

/* From head.S */
extern unsigned long romfs_in_flash; /* 1 when romfs_start, _length in flash */
extern unsigned long romfs_start, romfs_length;
extern unsigned long nand_boot; /* 1 when booted from nand flash */

struct partition_name {
	char name[6];
};

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

#define MAX_PARTITIONS			7
#ifdef CONFIG_ETRAX_NANDBOOT
#define NUM_DEFAULT_PARTITIONS		4
#define DEFAULT_ROOTFS_PARTITION_NO	2
#define DEFAULT_MEDIA_SIZE              0x2000000 /* 32 megs */
#else
#define NUM_DEFAULT_PARTITIONS		3
#define DEFAULT_ROOTFS_PARTITION_NO	(-1)
#define DEFAULT_MEDIA_SIZE              0x800000 /* 8 megs */
#endif

#if (MAX_PARTITIONS < NUM_DEFAULT_PARTITIONS)
#error MAX_PARTITIONS must be >= than NUM_DEFAULT_PARTITIONS
#endif

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


/* If no partition-table was found, we use this default-set.
 * Default flash size is 8MB (NOR). CONFIG_ETRAX_PTABLE_SECTOR is most
 * likely the size of one flash block and "filesystem"-partition needs
 * to be >=5 blocks to be able to use JFFS.
 */
static struct mtd_partition axis_default_partitions[NUM_DEFAULT_PARTITIONS] = {
	{
		.name = "boot firmware",
		.size = CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = 0
	},
	{
		.name = "kernel",
		.size = 10 * CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = CONFIG_ETRAX_PTABLE_SECTOR
	},
#define FILESYSTEM_SECTOR (11 * CONFIG_ETRAX_PTABLE_SECTOR)
#ifdef CONFIG_ETRAX_NANDBOOT
	{
		.name = "rootfs",
		.size = 10 * CONFIG_ETRAX_PTABLE_SECTOR,
		.offset = FILESYSTEM_SECTOR
	},
#undef FILESYSTEM_SECTOR
#define FILESYSTEM_SECTOR (21 * CONFIG_ETRAX_PTABLE_SECTOR)
#endif
	{
		.name = "rwfs",
		.size = DEFAULT_MEDIA_SIZE - FILESYSTEM_SECTOR,
		.offset = FILESYSTEM_SECTOR
	}
};

#ifdef CONFIG_ETRAX_AXISFLASHMAP_MTD0WHOLE
/* Main flash device */
static struct mtd_partition main_partition = {
	.name = "main",
	.size = 0,
	.offset = 0
};
#endif

/* Auxiliary partition if we find another flash */
static struct mtd_partition aux_partition = {
	.name = "aux",
	.size = 0,
	.offset = 0
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
 * so that MTD partitions can cross chip boundries.
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
	struct mtd_info *mtd_total;
	struct mtd_info *mtds[2];
	int count = 0;

	if ((mtd_cse0 = probe_cs(&map_cse0)) != NULL)
		mtds[count++] = mtd_cse0;
	if ((mtd_cse1 = probe_cs(&map_cse1)) != NULL)
		mtds[count++] = mtd_cse1;

	if (!mtd_cse0 && !mtd_cse1) {
		/* No chip found. */
		return NULL;
	}

	if (count > 1) {
		/* Since the concatenation layer adds a small overhead we
		 * could try to figure out if the chips in cse0 and cse1 are
		 * identical and reprobe the whole cse0+cse1 window. But since
		 * flash chips are slow, the overhead is relatively small.
		 * So we use the MTD concatenation layer instead of further
		 * complicating the probing procedure.
		 */
		mtd_total = mtd_concat_create(mtds, count, "cse0+cse1");
		if (!mtd_total) {
			printk(KERN_ERR "%s and %s: Concatenation failed!\n",
				map_cse0.name, map_cse1.name);

			/* The best we can do now is to only use what we found
			 * at cse0. */
			mtd_total = mtd_cse0;
			map_destroy(mtd_cse1);
		}
	} else
		mtd_total = mtd_cse0 ? mtd_cse0 : mtd_cse1;

	return mtd_total;
}

/*
 * Probe the flash chip(s) and, if it succeeds, read the partition-table
 * and register the partitions with MTD.
 */
static int __init init_axis_flash(void)
{
	struct mtd_info *main_mtd;
	struct mtd_info *aux_mtd = NULL;
	int err = 0;
	int pidx = 0;
	struct partitiontable_head *ptable_head = NULL;
	struct partitiontable_entry *ptable;
	int ptable_ok = 0;
	static char page[PAGESIZE];
	size_t len;
	int ram_rootfs_partition = -1; /* -1 => no RAM rootfs partition */
	int part;

	/* We need a root fs. If it resides in RAM, we need to use an
	 * MTDRAM device, so it must be enabled in the kernel config,
	 * but its size must be configured as 0 so as not to conflict
	 * with our usage.
	 */
#if !defined(CONFIG_MTD_MTDRAM) || (CONFIG_MTDRAM_TOTAL_SIZE != 0) || (CONFIG_MTDRAM_ABS_POS != 0)
	if (!romfs_in_flash && !nand_boot) {
		printk(KERN_EMERG "axisflashmap: Cannot create an MTD RAM "
		       "device; configure CONFIG_MTD_MTDRAM with size = 0!\n");
		panic("This kernel cannot boot from RAM!\n");
	}
#endif

#ifndef CONFIG_ETRAX_VCS_SIM
	main_mtd = flash_probe();
	if (main_mtd)
		printk(KERN_INFO "%s: 0x%08x bytes of NOR flash memory.\n",
		       main_mtd->name, main_mtd->size);

#ifdef CONFIG_ETRAX_NANDFLASH
	aux_mtd = crisv32_nand_flash_probe();
	if (aux_mtd)
		printk(KERN_INFO "%s: 0x%08x bytes of NAND flash memory.\n",
			aux_mtd->name, aux_mtd->size);

#ifdef CONFIG_ETRAX_NANDBOOT
	{
		struct mtd_info *tmp_mtd;

		printk(KERN_INFO "axisflashmap: Set to boot from NAND flash, "
		       "making NAND flash primary device.\n");
		tmp_mtd = main_mtd;
		main_mtd = aux_mtd;
		aux_mtd = tmp_mtd;
	}
#endif /* CONFIG_ETRAX_NANDBOOT */
#endif /* CONFIG_ETRAX_NANDFLASH */

	if (!main_mtd && !aux_mtd) {
		/* There's no reason to use this module if no flash chip can
		 * be identified. Make sure that's understood.
		 */
		printk(KERN_INFO "axisflashmap: Found no flash chip.\n");
	}

#if 0 /* Dump flash memory so we can see what is going on */
	if (main_mtd) {
		int sectoraddr, i;
		for (sectoraddr = 0; sectoraddr < 2*65536+4096;
				sectoraddr += PAGESIZE) {
			main_mtd->read(main_mtd, sectoraddr, PAGESIZE, &len,
				page);
			printk(KERN_INFO
			       "Sector at %d (length %d):\n",
			       sectoraddr, len);
			for (i = 0; i < PAGESIZE; i += 16) {
				printk(KERN_INFO
				       "%02x %02x %02x %02x "
				       "%02x %02x %02x %02x "
				       "%02x %02x %02x %02x "
				       "%02x %02x %02x %02x\n",
				       page[i] & 255, page[i+1] & 255,
				       page[i+2] & 255, page[i+3] & 255,
				       page[i+4] & 255, page[i+5] & 255,
				       page[i+6] & 255, page[i+7] & 255,
				       page[i+8] & 255, page[i+9] & 255,
				       page[i+10] & 255, page[i+11] & 255,
				       page[i+12] & 255, page[i+13] & 255,
				       page[i+14] & 255, page[i+15] & 255);
			}
		}
	}
#endif

	if (main_mtd) {
		main_mtd->owner = THIS_MODULE;
		axisflash_mtd = main_mtd;

		loff_t ptable_sector = CONFIG_ETRAX_PTABLE_SECTOR;

		/* First partition (rescue) is always set to the default. */
		pidx++;
#ifdef CONFIG_ETRAX_NANDBOOT
		/* We know where the partition table should be located,
		 * it will be in first good block after that.
		 */
		int blockstat;
		do {
			blockstat = main_mtd->block_isbad(main_mtd,
				ptable_sector);
			if (blockstat < 0)
				ptable_sector = 0; /* read error */
			else if (blockstat)
				ptable_sector += main_mtd->erasesize;
		} while (blockstat && ptable_sector);
#endif
		if (ptable_sector) {
			main_mtd->read(main_mtd, ptable_sector, PAGESIZE,
				&len, page);
			ptable_head = &((struct partitiontable *) page)->head;
		}

#if 0 /* Dump partition table so we can see what is going on */
		printk(KERN_INFO
		       "axisflashmap: flash read %d bytes at 0x%08x, data: "
		       "%02x %02x %02x %02x %02x %02x %02x %02x\n",
		       len, CONFIG_ETRAX_PTABLE_SECTOR,
		       page[0] & 255, page[1] & 255,
		       page[2] & 255, page[3] & 255,
		       page[4] & 255, page[5] & 255,
		       page[6] & 255, page[7] & 255);
		printk(KERN_INFO
		       "axisflashmap: partition table offset %d, data: "
		       "%02x %02x %02x %02x %02x %02x %02x %02x\n",
		       PARTITION_TABLE_OFFSET,
		       page[PARTITION_TABLE_OFFSET+0] & 255,
		       page[PARTITION_TABLE_OFFSET+1] & 255,
		       page[PARTITION_TABLE_OFFSET+2] & 255,
		       page[PARTITION_TABLE_OFFSET+3] & 255,
		       page[PARTITION_TABLE_OFFSET+4] & 255,
		       page[PARTITION_TABLE_OFFSET+5] & 255,
		       page[PARTITION_TABLE_OFFSET+6] & 255,
		       page[PARTITION_TABLE_OFFSET+7] & 255);
#endif
	}

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
		printk(KERN_INFO "axisflashmap: "
		       "Found a%s partition table at 0x%p-0x%p.\n",
		       (ptable_ok ? " valid" : "n invalid"), ptable_head,
		       max_addr);

		/* We have found a working bootblock.  Now read the
		 * partition table.  Scan the table.  It ends with 0xffffffff.
		 */
		while (ptable_ok
		       && ptable->offset != PARTITIONTABLE_END_MARKER
		       && ptable < max_addr
		       && pidx < MAX_PARTITIONS - 1) {

			axis_partitions[pidx].offset = offset + ptable->offset;
#ifdef CONFIG_ETRAX_NANDFLASH
			if (main_mtd->type == MTD_NANDFLASH) {
				axis_partitions[pidx].size =
					(((ptable+1)->offset ==
					  PARTITIONTABLE_END_MARKER) ?
					  main_mtd->size :
					  ((ptable+1)->offset + offset)) -
					(ptable->offset + offset);

			} else
#endif /* CONFIG_ETRAX_NANDFLASH */
				axis_partitions[pidx].size = ptable->size;
#ifdef CONFIG_ETRAX_NANDBOOT
			/* Save partition number of jffs2 ro partition.
			 * Needed if RAM booting or root file system in RAM.
			 */
			if (!nand_boot &&
			    ram_rootfs_partition < 0 && /* not already set */
			    ptable->type == PARTITION_TYPE_JFFS2 &&
			    (ptable->flags & PARTITION_FLAGS_READONLY_MASK) ==
				PARTITION_FLAGS_READONLY)
				ram_rootfs_partition = pidx;
#endif /* CONFIG_ETRAX_NANDBOOT */
			pidx++;
			ptable++;
		}
	}

	/* Decide whether to use default partition table. */
	/* Only use default table if we actually have a device (main_mtd) */

	struct mtd_partition *partition = &axis_partitions[0];
	if (main_mtd && !ptable_ok) {
		memcpy(axis_partitions, axis_default_partitions,
		       sizeof(axis_default_partitions));
		pidx = NUM_DEFAULT_PARTITIONS;
		ram_rootfs_partition = DEFAULT_ROOTFS_PARTITION_NO;
	}

	/* Add artificial partitions for rootfs if necessary */
	if (romfs_in_flash) {
		/* rootfs is in directly accessible flash memory = NOR flash.
		   Add an overlapping device for the rootfs partition. */
		printk(KERN_INFO "axisflashmap: Adding partition for "
		       "overlapping root file system image\n");
		axis_partitions[pidx].size = romfs_length;
		axis_partitions[pidx].offset = romfs_start - FLASH_CACHED_ADDR;
		axis_partitions[pidx].name = "romfs";
		axis_partitions[pidx].mask_flags |= MTD_WRITEABLE;
		ram_rootfs_partition = -1;
		pidx++;
	} else if (romfs_length && !nand_boot) {
		/* romfs exists in memory, but not in flash, so must be in RAM.
		 * Configure an MTDRAM partition. */
		if (ram_rootfs_partition < 0) {
			/* None set yet, put it at the end */
			ram_rootfs_partition = pidx;
			pidx++;
		}
		printk(KERN_INFO "axisflashmap: Adding partition for "
		       "root file system image in RAM\n");
		axis_partitions[ram_rootfs_partition].size = romfs_length;
		axis_partitions[ram_rootfs_partition].offset = romfs_start;
		axis_partitions[ram_rootfs_partition].name = "romfs";
		axis_partitions[ram_rootfs_partition].mask_flags |=
			MTD_WRITEABLE;
	}

#ifdef CONFIG_ETRAX_AXISFLASHMAP_MTD0WHOLE
	if (main_mtd) {
		main_partition.size = main_mtd->size;
		err = mtd_device_register(main_mtd, &main_partition, 1);
		if (err)
			panic("axisflashmap: Could not initialize "
			      "partition for whole main mtd device!\n");
	}
#endif

	/* Now, register all partitions with mtd.
	 * We do this one at a time so we can slip in an MTDRAM device
	 * in the proper place if required. */

	for (part = 0; part < pidx; part++) {
		if (part == ram_rootfs_partition) {
			/* add MTDRAM partition here */
			struct mtd_info *mtd_ram;

			mtd_ram = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
			if (!mtd_ram)
				panic("axisflashmap: Couldn't allocate memory "
				      "for mtd_info!\n");
			printk(KERN_INFO "axisflashmap: Adding RAM partition "
			       "for rootfs image.\n");
			err = mtdram_init_device(mtd_ram,
						 (void *)partition[part].offset,
						 partition[part].size,
						 partition[part].name);
			if (err)
				panic("axisflashmap: Could not initialize "
				      "MTD RAM device!\n");
			/* JFFS2 likes to have an erasesize. Keep potential
			 * JFFS2 rootfs happy by providing one. Since image
			 * was most likely created for main mtd, use that
			 * erasesize, if available. Otherwise, make a guess. */
			mtd_ram->erasesize = (main_mtd ? main_mtd->erasesize :
				CONFIG_ETRAX_PTABLE_SECTOR);
		} else {
			err = mtd_device_register(main_mtd, &partition[part],
						  1);
			if (err)
				panic("axisflashmap: Could not add mtd "
					"partition %d\n", part);
		}
	}
#endif /* CONFIG_EXTRAX_VCS_SIM */

#ifdef CONFIG_ETRAX_VCS_SIM
	/* For simulator, always use a RAM partition.
	 * The rootfs will be found after the kernel in RAM,
	 * with romfs_start and romfs_end indicating location and size.
	 */
	struct mtd_info *mtd_ram;

	mtd_ram = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd_ram) {
		panic("axisflashmap: Couldn't allocate memory for "
		      "mtd_info!\n");
	}

	printk(KERN_INFO "axisflashmap: Adding RAM partition for romfs, "
	       "at %u, size %u\n",
	       (unsigned) romfs_start, (unsigned) romfs_length);

	err = mtdram_init_device(mtd_ram, (void *)romfs_start,
				 romfs_length, "romfs");
	if (err) {
		panic("axisflashmap: Could not initialize MTD RAM "
		      "device!\n");
	}
#endif /* CONFIG_EXTRAX_VCS_SIM */

#ifndef CONFIG_ETRAX_VCS_SIM
	if (aux_mtd) {
		aux_partition.size = aux_mtd->size;
		err = mtd_device_register(aux_mtd, &aux_partition, 1);
		if (err)
			panic("axisflashmap: Could not initialize "
			      "aux mtd device!\n");

	}
#endif /* CONFIG_EXTRAX_VCS_SIM */

	return err;
}

/* This adds the above to the kernels init-call chain. */
module_init(init_axis_flash);

EXPORT_SYMBOL(axisflash_mtd);
