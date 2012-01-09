/*
 * Linux driver for SSFDC Flash Translation Layer (Read only)
 * © 2005 Eptar srl
 * Author: Claudio Lanconelli <lanconelli.claudio@eptar.com>
 *
 * Based on NTFL and MTDBLOCK_RO drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/blktrans.h>

struct ssfdcr_record {
	struct mtd_blktrans_dev mbd;
	int usecount;
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	int cis_block;			/* block n. containing CIS/IDI */
	int erase_size;			/* phys_block_size */
	unsigned short *logic_block_map; /* all zones (max 8192 phys blocks on
					    the 128MiB) */
	int map_len;			/* n. phys_blocks on the card */
};

#define SSFDCR_MAJOR		257
#define SSFDCR_PARTN_BITS	3

#define SECTOR_SIZE		512
#define SECTOR_SHIFT		9
#define OOB_SIZE		16

#define MAX_LOGIC_BLK_PER_ZONE	1000
#define MAX_PHYS_BLK_PER_ZONE	1024

#define KiB(x)	( (x) * 1024L )
#define MiB(x)	( KiB(x) * 1024L )

/** CHS Table
		1MiB	2MiB	4MiB	8MiB	16MiB	32MiB	64MiB	128MiB
NCylinder	125	125	250	250	500	500	500	500
NHead		4	4	4	4	4	8	8	16
NSector		4	8	8	16	16	16	32	32
SumSector	2,000	4,000	8,000	16,000	32,000	64,000	128,000	256,000
SectorSize	512	512	512	512	512	512	512	512
**/

typedef struct {
	unsigned long size;
	unsigned short cyl;
	unsigned char head;
	unsigned char sec;
} chs_entry_t;

/* Must be ordered by size */
static const chs_entry_t chs_table[] = {
	{ MiB(  1), 125,  4,  4 },
	{ MiB(  2), 125,  4,  8 },
	{ MiB(  4), 250,  4,  8 },
	{ MiB(  8), 250,  4, 16 },
	{ MiB( 16), 500,  4, 16 },
	{ MiB( 32), 500,  8, 16 },
	{ MiB( 64), 500,  8, 32 },
	{ MiB(128), 500, 16, 32 },
	{ 0 },
};

static int get_chs(unsigned long size, unsigned short *cyl, unsigned char *head,
			unsigned char *sec)
{
	int k;
	int found = 0;

	k = 0;
	while (chs_table[k].size > 0 && size > chs_table[k].size)
		k++;

	if (chs_table[k].size > 0) {
		if (cyl)
			*cyl = chs_table[k].cyl;
		if (head)
			*head = chs_table[k].head;
		if (sec)
			*sec = chs_table[k].sec;
		found = 1;
	}

	return found;
}

/* These bytes are the signature for the CIS/IDI sector */
static const uint8_t cis_numbers[] = {
	0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02, 0xDF, 0x01, 0x20
};

/* Read and check for a valid CIS sector */
static int get_valid_cis_sector(struct mtd_info *mtd)
{
	int ret, k, cis_sector;
	size_t retlen;
	loff_t offset;
	uint8_t *sect_buf;

	cis_sector = -1;

	sect_buf = kmalloc(SECTOR_SIZE, GFP_KERNEL);
	if (!sect_buf)
		goto out;

	/*
	 * Look for CIS/IDI sector on the first GOOD block (give up after 4 bad
	 * blocks). If the first good block doesn't contain CIS number the flash
	 * is not SSFDC formatted
	 */
	for (k = 0, offset = 0; k < 4; k++, offset += mtd->erasesize) {
		if (!mtd->block_isbad(mtd, offset)) {
			ret = mtd->read(mtd, offset, SECTOR_SIZE, &retlen,
				sect_buf);

			/* CIS pattern match on the sector buffer */
			if (ret < 0 || retlen != SECTOR_SIZE) {
				printk(KERN_WARNING
					"SSFDC_RO:can't read CIS/IDI sector\n");
			} else if (!memcmp(sect_buf, cis_numbers,
					sizeof(cis_numbers))) {
				/* Found */
				cis_sector = (int)(offset >> SECTOR_SHIFT);
			} else {
				pr_debug("SSFDC_RO: CIS/IDI sector not found"
					" on %s (mtd%d)\n", mtd->name,
					mtd->index);
			}
			break;
		}
	}

	kfree(sect_buf);
 out:
	return cis_sector;
}

/* Read physical sector (wrapper to MTD_READ) */
static int read_physical_sector(struct mtd_info *mtd, uint8_t *sect_buf,
				int sect_no)
{
	int ret;
	size_t retlen;
	loff_t offset = (loff_t)sect_no << SECTOR_SHIFT;

	ret = mtd->read(mtd, offset, SECTOR_SIZE, &retlen, sect_buf);
	if (ret < 0 || retlen != SECTOR_SIZE)
		return -1;

	return 0;
}

/* Read redundancy area (wrapper to MTD_READ_OOB */
static int read_raw_oob(struct mtd_info *mtd, loff_t offs, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int ret;

	ops.mode = MTD_OPS_RAW;
	ops.ooboffs = 0;
	ops.ooblen = OOB_SIZE;
	ops.oobbuf = buf;
	ops.datbuf = NULL;

	ret = mtd->read_oob(mtd, offs, &ops);
	if (ret < 0 || ops.oobretlen != OOB_SIZE)
		return -1;

	return 0;
}

/* Parity calculator on a word of n bit size */
static int get_parity(int number, int size)
{
 	int k;
	int parity;

	parity = 1;
	for (k = 0; k < size; k++) {
		parity += (number >> k);
		parity &= 1;
	}
	return parity;
}

/* Read and validate the logical block address field stored in the OOB */
static int get_logical_address(uint8_t *oob_buf)
{
	int block_address, parity;
	int offset[2] = {6, 11}; /* offset of the 2 address fields within OOB */
	int j;
	int ok = 0;

	/*
	 * Look for the first valid logical address
	 * Valid address has fixed pattern on most significant bits and
	 * parity check
	 */
	for (j = 0; j < ARRAY_SIZE(offset); j++) {
		block_address = ((int)oob_buf[offset[j]] << 8) |
			oob_buf[offset[j]+1];

		/* Check for the signature bits in the address field (MSBits) */
		if ((block_address & ~0x7FF) == 0x1000) {
			parity = block_address & 0x01;
			block_address &= 0x7FF;
			block_address >>= 1;

			if (get_parity(block_address, 10) != parity) {
				pr_debug("SSFDC_RO: logical address field%d"
					"parity error(0x%04X)\n", j+1,
					block_address);
			} else {
				ok = 1;
				break;
			}
		}
	}

	if (!ok)
		block_address = -2;

	pr_debug("SSFDC_RO: get_logical_address() %d\n",
		block_address);

	return block_address;
}

/* Build the logic block map */
static int build_logical_block_map(struct ssfdcr_record *ssfdc)
{
	unsigned long offset;
	uint8_t oob_buf[OOB_SIZE];
	int ret, block_address, phys_block;
	struct mtd_info *mtd = ssfdc->mbd.mtd;

	pr_debug("SSFDC_RO: build_block_map() nblks=%d (%luK)\n",
	      ssfdc->map_len,
	      (unsigned long)ssfdc->map_len * ssfdc->erase_size / 1024);

	/* Scan every physical block, skip CIS block */
	for (phys_block = ssfdc->cis_block + 1; phys_block < ssfdc->map_len;
			phys_block++) {
		offset = (unsigned long)phys_block * ssfdc->erase_size;
		if (mtd->block_isbad(mtd, offset))
			continue;	/* skip bad blocks */

		ret = read_raw_oob(mtd, offset, oob_buf);
		if (ret < 0) {
			pr_debug("SSFDC_RO: mtd read_oob() failed at %lu\n",
				offset);
			return -1;
		}
		block_address = get_logical_address(oob_buf);

		/* Skip invalid addresses */
		if (block_address >= 0 &&
				block_address < MAX_LOGIC_BLK_PER_ZONE) {
			int zone_index;

			zone_index = phys_block / MAX_PHYS_BLK_PER_ZONE;
			block_address += zone_index * MAX_LOGIC_BLK_PER_ZONE;
			ssfdc->logic_block_map[block_address] =
				(unsigned short)phys_block;

			pr_debug("SSFDC_RO: build_block_map() phys_block=%d,"
				"logic_block_addr=%d, zone=%d\n",
				phys_block, block_address, zone_index);
		}
	}
	return 0;
}

static void ssfdcr_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct ssfdcr_record *ssfdc;
	int cis_sector;

	/* Check for small page NAND flash */
	if (mtd->type != MTD_NANDFLASH || mtd->oobsize != OOB_SIZE ||
	    mtd->size > UINT_MAX)
		return;

	/* Check for SSDFC format by reading CIS/IDI sector */
	cis_sector = get_valid_cis_sector(mtd);
	if (cis_sector == -1)
		return;

	ssfdc = kzalloc(sizeof(struct ssfdcr_record), GFP_KERNEL);
	if (!ssfdc)
		return;

	ssfdc->mbd.mtd = mtd;
	ssfdc->mbd.devnum = -1;
	ssfdc->mbd.tr = tr;
	ssfdc->mbd.readonly = 1;

	ssfdc->cis_block = cis_sector / (mtd->erasesize >> SECTOR_SHIFT);
	ssfdc->erase_size = mtd->erasesize;
	ssfdc->map_len = (u32)mtd->size / mtd->erasesize;

	pr_debug("SSFDC_RO: cis_block=%d,erase_size=%d,map_len=%d,n_zones=%d\n",
		ssfdc->cis_block, ssfdc->erase_size, ssfdc->map_len,
		DIV_ROUND_UP(ssfdc->map_len, MAX_PHYS_BLK_PER_ZONE));

	/* Set geometry */
	ssfdc->heads = 16;
	ssfdc->sectors = 32;
	get_chs(mtd->size, NULL, &ssfdc->heads, &ssfdc->sectors);
	ssfdc->cylinders = (unsigned short)(((u32)mtd->size >> SECTOR_SHIFT) /
			((long)ssfdc->sectors * (long)ssfdc->heads));

	pr_debug("SSFDC_RO: using C:%d H:%d S:%d == %ld sects\n",
		ssfdc->cylinders, ssfdc->heads , ssfdc->sectors,
		(long)ssfdc->cylinders * (long)ssfdc->heads *
		(long)ssfdc->sectors);

	ssfdc->mbd.size = (long)ssfdc->heads * (long)ssfdc->cylinders *
				(long)ssfdc->sectors;

	/* Allocate logical block map */
	ssfdc->logic_block_map = kmalloc(sizeof(ssfdc->logic_block_map[0]) *
					 ssfdc->map_len, GFP_KERNEL);
	if (!ssfdc->logic_block_map)
		goto out_err;
	memset(ssfdc->logic_block_map, 0xff, sizeof(ssfdc->logic_block_map[0]) *
		ssfdc->map_len);

	/* Build logical block map */
	if (build_logical_block_map(ssfdc) < 0)
		goto out_err;

	/* Register device + partitions */
	if (add_mtd_blktrans_dev(&ssfdc->mbd))
		goto out_err;

	printk(KERN_INFO "SSFDC_RO: Found ssfdc%c on mtd%d (%s)\n",
		ssfdc->mbd.devnum + 'a', mtd->index, mtd->name);
	return;

out_err:
	kfree(ssfdc->logic_block_map);
        kfree(ssfdc);
}

static void ssfdcr_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct ssfdcr_record *ssfdc = (struct ssfdcr_record *)dev;

	pr_debug("SSFDC_RO: remove_dev (i=%d)\n", dev->devnum);

	del_mtd_blktrans_dev(dev);
	kfree(ssfdc->logic_block_map);
}

static int ssfdcr_readsect(struct mtd_blktrans_dev *dev,
				unsigned long logic_sect_no, char *buf)
{
	struct ssfdcr_record *ssfdc = (struct ssfdcr_record *)dev;
	int sectors_per_block, offset, block_address;

	sectors_per_block = ssfdc->erase_size >> SECTOR_SHIFT;
	offset = (int)(logic_sect_no % sectors_per_block);
	block_address = (int)(logic_sect_no / sectors_per_block);

	pr_debug("SSFDC_RO: ssfdcr_readsect(%lu) sec_per_blk=%d, ofst=%d,"
		" block_addr=%d\n", logic_sect_no, sectors_per_block, offset,
		block_address);

	if (block_address >= ssfdc->map_len)
		BUG();

	block_address = ssfdc->logic_block_map[block_address];

	pr_debug("SSFDC_RO: ssfdcr_readsect() phys_block_addr=%d\n",
		block_address);

	if (block_address < 0xffff) {
		unsigned long sect_no;

		sect_no = (unsigned long)block_address * sectors_per_block +
				offset;

		pr_debug("SSFDC_RO: ssfdcr_readsect() phys_sect_no=%lu\n",
			sect_no);

		if (read_physical_sector(ssfdc->mbd.mtd, buf, sect_no) < 0)
			return -EIO;
	} else {
		memset(buf, 0xff, SECTOR_SIZE);
	}

	return 0;
}

static int ssfdcr_getgeo(struct mtd_blktrans_dev *dev,  struct hd_geometry *geo)
{
	struct ssfdcr_record *ssfdc = (struct ssfdcr_record *)dev;

	pr_debug("SSFDC_RO: ssfdcr_getgeo() C=%d, H=%d, S=%d\n",
			ssfdc->cylinders, ssfdc->heads, ssfdc->sectors);

	geo->heads = ssfdc->heads;
	geo->sectors = ssfdc->sectors;
	geo->cylinders = ssfdc->cylinders;

	return 0;
}

/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

static struct mtd_blktrans_ops ssfdcr_tr = {
	.name		= "ssfdc",
	.major		= SSFDCR_MAJOR,
	.part_bits	= SSFDCR_PARTN_BITS,
	.blksize	= SECTOR_SIZE,
	.getgeo		= ssfdcr_getgeo,
	.readsect	= ssfdcr_readsect,
	.add_mtd	= ssfdcr_add_mtd,
	.remove_dev	= ssfdcr_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_ssfdcr(void)
{
	printk(KERN_INFO "SSFDC read-only Flash Translation layer\n");

	return register_mtd_blktrans(&ssfdcr_tr);
}

static void __exit cleanup_ssfdcr(void)
{
	deregister_mtd_blktrans(&ssfdcr_tr);
}

module_init(init_ssfdcr);
module_exit(cleanup_ssfdcr);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Claudio Lanconelli <lanconelli.claudio@eptar.com>");
MODULE_DESCRIPTION("Flash Translation Layer for read-only SSFDC SmartMedia card");
