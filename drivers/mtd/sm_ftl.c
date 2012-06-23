/*
 * Copyright © 2009 - Maxim Levitsky
 * SmartMedia/xD translation layer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/hdreg.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/sysfs.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/mtd/nand_ecc.h>
#include "nand/sm_common.h"
#include "sm_ftl.h"



struct workqueue_struct *cache_flush_workqueue;

static int cache_timeout = 1000;
module_param(cache_timeout, bool, S_IRUGO);
MODULE_PARM_DESC(cache_timeout,
	"Timeout (in ms) for cache flush (1000 ms default");

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level (0-2)");


/* ------------------- sysfs attributtes ---------------------------------- */
struct sm_sysfs_attribute {
	struct device_attribute dev_attr;
	char *data;
	int len;
};

ssize_t sm_attr_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct sm_sysfs_attribute *sm_attr =
		container_of(attr, struct sm_sysfs_attribute, dev_attr);

	strncpy(buf, sm_attr->data, sm_attr->len);
	return sm_attr->len;
}


#define NUM_ATTRIBUTES 1
#define SM_CIS_VENDOR_OFFSET 0x59
struct attribute_group *sm_create_sysfs_attributes(struct sm_ftl *ftl)
{
	struct attribute_group *attr_group;
	struct attribute **attributes;
	struct sm_sysfs_attribute *vendor_attribute;

	int vendor_len = strnlen(ftl->cis_buffer + SM_CIS_VENDOR_OFFSET,
					SM_SMALL_PAGE - SM_CIS_VENDOR_OFFSET);

	char *vendor = kmalloc(vendor_len, GFP_KERNEL);
	if (!vendor)
		goto error1;
	memcpy(vendor, ftl->cis_buffer + SM_CIS_VENDOR_OFFSET, vendor_len);
	vendor[vendor_len] = 0;

	/* Initialize sysfs attributes */
	vendor_attribute =
		kzalloc(sizeof(struct sm_sysfs_attribute), GFP_KERNEL);
	if (!vendor_attribute)
		goto error2;

	sysfs_attr_init(&vendor_attribute->dev_attr.attr);

	vendor_attribute->data = vendor;
	vendor_attribute->len = vendor_len;
	vendor_attribute->dev_attr.attr.name = "vendor";
	vendor_attribute->dev_attr.attr.mode = S_IRUGO;
	vendor_attribute->dev_attr.show = sm_attr_show;


	/* Create array of pointers to the attributes */
	attributes = kzalloc(sizeof(struct attribute *) * (NUM_ATTRIBUTES + 1),
								GFP_KERNEL);
	if (!attributes)
		goto error3;
	attributes[0] = &vendor_attribute->dev_attr.attr;

	/* Finally create the attribute group */
	attr_group = kzalloc(sizeof(struct attribute_group), GFP_KERNEL);
	if (!attr_group)
		goto error4;
	attr_group->attrs = attributes;
	return attr_group;
error4:
	kfree(attributes);
error3:
	kfree(vendor_attribute);
error2:
	kfree(vendor);
error1:
	return NULL;
}

void sm_delete_sysfs_attributes(struct sm_ftl *ftl)
{
	struct attribute **attributes = ftl->disk_attributes->attrs;
	int i;

	for (i = 0; attributes[i] ; i++) {

		struct device_attribute *dev_attr = container_of(attributes[i],
			struct device_attribute, attr);

		struct sm_sysfs_attribute *sm_attr =
			container_of(dev_attr,
				struct sm_sysfs_attribute, dev_attr);

		kfree(sm_attr->data);
		kfree(sm_attr);
	}

	kfree(ftl->disk_attributes->attrs);
	kfree(ftl->disk_attributes);
}


/* ----------------------- oob helpers -------------------------------------- */

static int sm_get_lba(uint8_t *lba)
{
	/* check fixed bits */
	if ((lba[0] & 0xF8) != 0x10)
		return -2;

	/* check parity - endianess doesn't matter */
	if (hweight16(*(uint16_t *)lba) & 1)
		return -2;

	return (lba[1] >> 1) | ((lba[0] & 0x07) << 7);
}


/*
 * Read LBA asscociated with block
 * returns -1, if block is erased
 * returns -2 if error happens
 */
static int sm_read_lba(struct sm_oob *oob)
{
	static const uint32_t erased_pattern[4] = {
		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

	uint16_t lba_test;
	int lba;

	/* First test for erased block */
	if (!memcmp(oob, erased_pattern, SM_OOB_SIZE))
		return -1;

	/* Now check is both copies of the LBA differ too much */
	lba_test = *(uint16_t *)oob->lba_copy1 ^ *(uint16_t*)oob->lba_copy2;
	if (lba_test && !is_power_of_2(lba_test))
		return -2;

	/* And read it */
	lba = sm_get_lba(oob->lba_copy1);

	if (lba == -2)
		lba = sm_get_lba(oob->lba_copy2);

	return lba;
}

static void sm_write_lba(struct sm_oob *oob, uint16_t lba)
{
	uint8_t tmp[2];

	WARN_ON(lba >= 1000);

	tmp[0] = 0x10 | ((lba >> 7) & 0x07);
	tmp[1] = (lba << 1) & 0xFF;

	if (hweight16(*(uint16_t *)tmp) & 0x01)
		tmp[1] |= 1;

	oob->lba_copy1[0] = oob->lba_copy2[0] = tmp[0];
	oob->lba_copy1[1] = oob->lba_copy2[1] = tmp[1];
}


/* Make offset from parts */
static loff_t sm_mkoffset(struct sm_ftl *ftl, int zone, int block, int boffset)
{
	WARN_ON(boffset & (SM_SECTOR_SIZE - 1));
	WARN_ON(zone < 0 || zone >= ftl->zone_count);
	WARN_ON(block >= ftl->zone_size);
	WARN_ON(boffset >= ftl->block_size);

	if (block == -1)
		return -1;

	return (zone * SM_MAX_ZONE_SIZE + block) * ftl->block_size + boffset;
}

/* Breaks offset into parts */
static void sm_break_offset(struct sm_ftl *ftl, loff_t offset,
			    int *zone, int *block, int *boffset)
{
	*boffset = do_div(offset, ftl->block_size);
	*block = do_div(offset, ftl->max_lba);
	*zone = offset >= ftl->zone_count ? -1 : offset;
}

/* ---------------------- low level IO ------------------------------------- */

static int sm_correct_sector(uint8_t *buffer, struct sm_oob *oob)
{
	uint8_t ecc[3];

	__nand_calculate_ecc(buffer, SM_SMALL_PAGE, ecc);
	if (__nand_correct_data(buffer, ecc, oob->ecc1, SM_SMALL_PAGE) < 0)
		return -EIO;

	buffer += SM_SMALL_PAGE;

	__nand_calculate_ecc(buffer, SM_SMALL_PAGE, ecc);
	if (__nand_correct_data(buffer, ecc, oob->ecc2, SM_SMALL_PAGE) < 0)
		return -EIO;
	return 0;
}

/* Reads a sector + oob*/
static int sm_read_sector(struct sm_ftl *ftl,
			  int zone, int block, int boffset,
			  uint8_t *buffer, struct sm_oob *oob)
{
	struct mtd_info *mtd = ftl->trans->mtd;
	struct mtd_oob_ops ops;
	struct sm_oob tmp_oob;
	int ret = -EIO;
	int try = 0;

	/* FTL can contain -1 entries that are by default filled with bits */
	if (block == -1) {
		memset(buffer, 0xFF, SM_SECTOR_SIZE);
		return 0;
	}

	/* User might not need the oob, but we do for data vertification */
	if (!oob)
		oob = &tmp_oob;

	ops.mode = ftl->smallpagenand ? MTD_OOB_RAW : MTD_OOB_PLACE;
	ops.ooboffs = 0;
	ops.ooblen = SM_OOB_SIZE;
	ops.oobbuf = (void *)oob;
	ops.len = SM_SECTOR_SIZE;
	ops.datbuf = buffer;

again:
	if (try++) {
		/* Avoid infinite recursion on CIS reads, sm_recheck_media
			won't help anyway */
		if (zone == 0 && block == ftl->cis_block && boffset ==
			ftl->cis_boffset)
			return ret;

		/* Test if media is stable */
		if (try == 3 || sm_recheck_media(ftl))
			return ret;
	}

	/* Unfortunelly, oob read will _always_ succeed,
		despite card removal..... */
	ret = mtd->read_oob(mtd, sm_mkoffset(ftl, zone, block, boffset), &ops);

	/* Test for unknown errors */
	if (ret != 0 && ret != -EUCLEAN && ret != -EBADMSG) {
		dbg("read of block %d at zone %d, failed due to error (%d)",
			block, zone, ret);
		goto again;
	}

	/* Do a basic test on the oob, to guard against returned garbage */
	if (oob->reserved != 0xFFFFFFFF && !is_power_of_2(~oob->reserved))
		goto again;

	/* This should never happen, unless there is a bug in the mtd driver */
	WARN_ON(ops.oobretlen != SM_OOB_SIZE);
	WARN_ON(buffer && ops.retlen != SM_SECTOR_SIZE);

	if (!buffer)
		return 0;

	/* Test if sector marked as bad */
	if (!sm_sector_valid(oob)) {
		dbg("read of block %d at zone %d, failed because it is marked"
			" as bad" , block, zone);
		goto again;
	}

	/* Test ECC*/
	if (ret == -EBADMSG ||
		(ftl->smallpagenand && sm_correct_sector(buffer, oob))) {

		dbg("read of block %d at zone %d, failed due to ECC error",
			block, zone);
		goto again;
	}

	return 0;
}

/* Writes a sector to media */
static int sm_write_sector(struct sm_ftl *ftl,
			   int zone, int block, int boffset,
			   uint8_t *buffer, struct sm_oob *oob)
{
	struct mtd_oob_ops ops;
	struct mtd_info *mtd = ftl->trans->mtd;
	int ret;

	BUG_ON(ftl->readonly);

	if (zone == 0 && (block == ftl->cis_block || block == 0)) {
		dbg("attempted to write the CIS!");
		return -EIO;
	}

	if (ftl->unstable)
		return -EIO;

	ops.mode = ftl->smallpagenand ? MTD_OOB_RAW : MTD_OOB_PLACE;
	ops.len = SM_SECTOR_SIZE;
	ops.datbuf = buffer;
	ops.ooboffs = 0;
	ops.ooblen = SM_OOB_SIZE;
	ops.oobbuf = (void *)oob;

	ret = mtd->write_oob(mtd, sm_mkoffset(ftl, zone, block, boffset), &ops);

	/* Now we assume that hardware will catch write bitflip errors */
	/* If you are paranoid, use CONFIG_MTD_NAND_VERIFY_WRITE */

	if (ret) {
		dbg("write to block %d at zone %d, failed with error %d",
			block, zone, ret);

		sm_recheck_media(ftl);
		return ret;
	}

	/* This should never happen, unless there is a bug in the driver */
	WARN_ON(ops.oobretlen != SM_OOB_SIZE);
	WARN_ON(buffer && ops.retlen != SM_SECTOR_SIZE);

	return 0;
}

/* ------------------------ block IO ------------------------------------- */

/* Write a block using data and lba, and invalid sector bitmap */
static int sm_write_block(struct sm_ftl *ftl, uint8_t *buf,
			  int zone, int block, int lba,
			  unsigned long invalid_bitmap)
{
	struct sm_oob oob;
	int boffset;
	int retry = 0;

	/* Initialize the oob with requested values */
	memset(&oob, 0xFF, SM_OOB_SIZE);
	sm_write_lba(&oob, lba);
restart:
	if (ftl->unstable)
		return -EIO;

	for (boffset = 0; boffset < ftl->block_size;
				boffset += SM_SECTOR_SIZE) {

		oob.data_status = 0xFF;

		if (test_bit(boffset / SM_SECTOR_SIZE, &invalid_bitmap)) {

			sm_printk("sector %d of block at LBA %d of zone %d"
				" coudn't be read, marking it as invalid",
				boffset / SM_SECTOR_SIZE, lba, zone);

			oob.data_status = 0;
		}

		if (ftl->smallpagenand) {
			__nand_calculate_ecc(buf + boffset,
						SM_SMALL_PAGE, oob.ecc1);

			__nand_calculate_ecc(buf + boffset + SM_SMALL_PAGE,
						SM_SMALL_PAGE, oob.ecc2);
		}
		if (!sm_write_sector(ftl, zone, block, boffset,
							buf + boffset, &oob))
			continue;

		if (!retry) {

			/* If write fails. try to erase the block */
			/* This is safe, because we never write in blocks
				that contain valuable data.
			This is intended to repair block that are marked
			as erased, but that isn't fully erased*/

			if (sm_erase_block(ftl, zone, block, 0))
				return -EIO;

			retry = 1;
			goto restart;
		} else {
			sm_mark_block_bad(ftl, zone, block);
			return -EIO;
		}
	}
	return 0;
}


/* Mark whole block at offset 'offs' as bad. */
static void sm_mark_block_bad(struct sm_ftl *ftl, int zone, int block)
{
	struct sm_oob oob;
	int boffset;

	memset(&oob, 0xFF, SM_OOB_SIZE);
	oob.block_status = 0xF0;

	if (ftl->unstable)
		return;

	if (sm_recheck_media(ftl))
		return;

	sm_printk("marking block %d of zone %d as bad", block, zone);

	/* We aren't checking the return value, because we don't care */
	/* This also fails on fake xD cards, but I guess these won't expose
		any bad blocks till fail completly */
	for (boffset = 0; boffset < ftl->block_size; boffset += SM_SECTOR_SIZE)
		sm_write_sector(ftl, zone, block, boffset, NULL, &oob);
}

/*
 * Erase a block within a zone
 * If erase succedes, it updates free block fifo, otherwise marks block as bad
 */
static int sm_erase_block(struct sm_ftl *ftl, int zone_num, uint16_t block,
			  int put_free)
{
	struct ftl_zone *zone = &ftl->zones[zone_num];
	struct mtd_info *mtd = ftl->trans->mtd;
	struct erase_info erase;

	erase.mtd = mtd;
	erase.callback = sm_erase_callback;
	erase.addr = sm_mkoffset(ftl, zone_num, block, 0);
	erase.len = ftl->block_size;
	erase.priv = (u_long)ftl;

	if (ftl->unstable)
		return -EIO;

	BUG_ON(ftl->readonly);

	if (zone_num == 0 && (block == ftl->cis_block || block == 0)) {
		sm_printk("attempted to erase the CIS!");
		return -EIO;
	}

	if (mtd->erase(mtd, &erase)) {
		sm_printk("erase of block %d in zone %d failed",
							block, zone_num);
		goto error;
	}

	if (erase.state == MTD_ERASE_PENDING)
		wait_for_completion(&ftl->erase_completion);

	if (erase.state != MTD_ERASE_DONE) {
		sm_printk("erase of block %d in zone %d failed after wait",
			block, zone_num);
		goto error;
	}

	if (put_free)
		kfifo_in(&zone->free_sectors,
			(const unsigned char *)&block, sizeof(block));

	return 0;
error:
	sm_mark_block_bad(ftl, zone_num, block);
	return -EIO;
}

static void sm_erase_callback(struct erase_info *self)
{
	struct sm_ftl *ftl = (struct sm_ftl *)self->priv;
	complete(&ftl->erase_completion);
}

/* Throughtly test that block is valid. */
static int sm_check_block(struct sm_ftl *ftl, int zone, int block)
{
	int boffset;
	struct sm_oob oob;
	int lbas[] = { -3, 0, 0, 0 };
	int i = 0;
	int test_lba;


	/* First just check that block doesn't look fishy */
	/* Only blocks that are valid or are sliced in two parts, are
		accepted */
	for (boffset = 0; boffset < ftl->block_size;
					boffset += SM_SECTOR_SIZE) {

		/* This shoudn't happen anyway */
		if (sm_read_sector(ftl, zone, block, boffset, NULL, &oob))
			return -2;

		test_lba = sm_read_lba(&oob);

		if (lbas[i] != test_lba)
			lbas[++i] = test_lba;

		/* If we found three different LBAs, something is fishy */
		if (i == 3)
			return -EIO;
	}

	/* If the block is sliced (partially erased usually) erase it */
	if (i == 2) {
		sm_erase_block(ftl, zone, block, 1);
		return 1;
	}

	return 0;
}

/* ----------------- media scanning --------------------------------- */
static const struct chs_entry chs_table[] = {
	{ 1,    125,  4,  4  },
	{ 2,    125,  4,  8  },
	{ 4,    250,  4,  8  },
	{ 8,    250,  4,  16 },
	{ 16,   500,  4,  16 },
	{ 32,   500,  8,  16 },
	{ 64,   500,  8,  32 },
	{ 128,  500,  16, 32 },
	{ 256,  1000, 16, 32 },
	{ 512,  1015, 32, 63 },
	{ 1024, 985,  33, 63 },
	{ 2048, 985,  33, 63 },
	{ 0 },
};


static const uint8_t cis_signature[] = {
	0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02, 0xDF, 0x01, 0x20
};
/* Find out media parameters.
 * This ideally has to be based on nand id, but for now device size is enough */
int sm_get_media_info(struct sm_ftl *ftl, struct mtd_info *mtd)
{
	int i;
	int size_in_megs = mtd->size / (1024 * 1024);

	ftl->readonly = mtd->type == MTD_ROM;

	/* Manual settings for very old devices */
	ftl->zone_count = 1;
	ftl->smallpagenand = 0;

	switch (size_in_megs) {
	case 1:
		/* 1 MiB flash/rom SmartMedia card (256 byte pages)*/
		ftl->zone_size = 256;
		ftl->max_lba = 250;
		ftl->block_size = 8 * SM_SECTOR_SIZE;
		ftl->smallpagenand = 1;

		break;
	case 2:
		/* 2 MiB flash SmartMedia (256 byte pages)*/
		if (mtd->writesize == SM_SMALL_PAGE) {
			ftl->zone_size = 512;
			ftl->max_lba = 500;
			ftl->block_size = 8 * SM_SECTOR_SIZE;
			ftl->smallpagenand = 1;
		/* 2 MiB rom SmartMedia */
		} else {

			if (!ftl->readonly)
				return -ENODEV;

			ftl->zone_size = 256;
			ftl->max_lba = 250;
			ftl->block_size = 16 * SM_SECTOR_SIZE;
		}
		break;
	case 4:
		/* 4 MiB flash/rom SmartMedia device */
		ftl->zone_size = 512;
		ftl->max_lba = 500;
		ftl->block_size = 16 * SM_SECTOR_SIZE;
		break;
	case 8:
		/* 8 MiB flash/rom SmartMedia device */
		ftl->zone_size = 1024;
		ftl->max_lba = 1000;
		ftl->block_size = 16 * SM_SECTOR_SIZE;
	}

	/* Minimum xD size is 16MiB. Also, all xD cards have standard zone
	   sizes. SmartMedia cards exist up to 128 MiB and have same layout*/
	if (size_in_megs >= 16) {
		ftl->zone_count = size_in_megs / 16;
		ftl->zone_size = 1024;
		ftl->max_lba = 1000;
		ftl->block_size = 32 * SM_SECTOR_SIZE;
	}

	/* Test for proper write,erase and oob sizes */
	if (mtd->erasesize > ftl->block_size)
		return -ENODEV;

	if (mtd->writesize > SM_SECTOR_SIZE)
		return -ENODEV;

	if (ftl->smallpagenand && mtd->oobsize < SM_SMALL_OOB_SIZE)
		return -ENODEV;

	if (!ftl->smallpagenand && mtd->oobsize < SM_OOB_SIZE)
		return -ENODEV;

	/* We use these functions for IO */
	if (!mtd->read_oob || !mtd->write_oob)
		return -ENODEV;

	/* Find geometry information */
	for (i = 0 ; i < ARRAY_SIZE(chs_table) ; i++) {
		if (chs_table[i].size == size_in_megs) {
			ftl->cylinders = chs_table[i].cyl;
			ftl->heads = chs_table[i].head;
			ftl->sectors = chs_table[i].sec;
			return 0;
		}
	}

	sm_printk("media has unknown size : %dMiB", size_in_megs);
	ftl->cylinders = 985;
	ftl->heads =  33;
	ftl->sectors = 63;
	return 0;
}

/* Validate the CIS */
static int sm_read_cis(struct sm_ftl *ftl)
{
	struct sm_oob oob;

	if (sm_read_sector(ftl,
		0, ftl->cis_block, ftl->cis_boffset, ftl->cis_buffer, &oob))
			return -EIO;

	if (!sm_sector_valid(&oob) || !sm_block_valid(&oob))
		return -EIO;

	if (!memcmp(ftl->cis_buffer + ftl->cis_page_offset,
			cis_signature, sizeof(cis_signature))) {
		return 0;
	}

	return -EIO;
}

/* Scan the media for the CIS */
static int sm_find_cis(struct sm_ftl *ftl)
{
	struct sm_oob oob;
	int block, boffset;
	int block_found = 0;
	int cis_found = 0;

	/* Search for first valid block */
	for (block = 0 ; block < ftl->zone_size - ftl->max_lba ; block++) {

		if (sm_read_sector(ftl, 0, block, 0, NULL, &oob))
			continue;

		if (!sm_block_valid(&oob))
			continue;
		block_found = 1;
		break;
	}

	if (!block_found)
		return -EIO;

	/* Search for first valid sector in this block */
	for (boffset = 0 ; boffset < ftl->block_size;
						boffset += SM_SECTOR_SIZE) {

		if (sm_read_sector(ftl, 0, block, boffset, NULL, &oob))
			continue;

		if (!sm_sector_valid(&oob))
			continue;
		break;
	}

	if (boffset == ftl->block_size)
		return -EIO;

	ftl->cis_block = block;
	ftl->cis_boffset = boffset;
	ftl->cis_page_offset = 0;

	cis_found = !sm_read_cis(ftl);

	if (!cis_found) {
		ftl->cis_page_offset = SM_SMALL_PAGE;
		cis_found = !sm_read_cis(ftl);
	}

	if (cis_found) {
		dbg("CIS block found at offset %x",
			block * ftl->block_size +
				boffset + ftl->cis_page_offset);
		return 0;
	}
	return -EIO;
}

/* Basic test to determine if underlying mtd device if functional */
static int sm_recheck_media(struct sm_ftl *ftl)
{
	if (sm_read_cis(ftl)) {

		if (!ftl->unstable) {
			sm_printk("media unstable, not allowing writes");
			ftl->unstable = 1;
		}
		return -EIO;
	}
	return 0;
}

/* Initialize a FTL zone */
static int sm_init_zone(struct sm_ftl *ftl, int zone_num)
{
	struct ftl_zone *zone = &ftl->zones[zone_num];
	struct sm_oob oob;
	uint16_t block;
	int lba;
	int i = 0;
	int len;

	dbg("initializing zone %d", zone_num);

	/* Allocate memory for FTL table */
	zone->lba_to_phys_table = kmalloc(ftl->max_lba * 2, GFP_KERNEL);

	if (!zone->lba_to_phys_table)
		return -ENOMEM;
	memset(zone->lba_to_phys_table, -1, ftl->max_lba * 2);


	/* Allocate memory for free sectors FIFO */
	if (kfifo_alloc(&zone->free_sectors, ftl->zone_size * 2, GFP_KERNEL)) {
		kfree(zone->lba_to_phys_table);
		return -ENOMEM;
	}

	/* Now scan the zone */
	for (block = 0 ; block < ftl->zone_size ; block++) {

		/* Skip blocks till the CIS (including) */
		if (zone_num == 0 && block <= ftl->cis_block)
			continue;

		/* Read the oob of first sector */
		if (sm_read_sector(ftl, zone_num, block, 0, NULL, &oob))
			return -EIO;

		/* Test to see if block is erased. It is enough to test
			first sector, because erase happens in one shot */
		if (sm_block_erased(&oob)) {
			kfifo_in(&zone->free_sectors,
				(unsigned char *)&block, 2);
			continue;
		}

		/* If block is marked as bad, skip it */
		/* This assumes we can trust first sector*/
		/* However the way the block valid status is defined, ensures
			very low probability of failure here */
		if (!sm_block_valid(&oob)) {
			dbg("PH %04d <-> <marked bad>", block);
			continue;
		}


		lba = sm_read_lba(&oob);

		/* Invalid LBA means that block is damaged. */
		/* We can try to erase it, or mark it as bad, but
			lets leave that to recovery application */
		if (lba == -2 || lba >= ftl->max_lba) {
			dbg("PH %04d <-> LBA %04d(bad)", block, lba);
			continue;
		}


		/* If there is no collision,
			just put the sector in the FTL table */
		if (zone->lba_to_phys_table[lba] < 0) {
			dbg_verbose("PH %04d <-> LBA %04d", block, lba);
			zone->lba_to_phys_table[lba] = block;
			continue;
		}

		sm_printk("collision"
			" of LBA %d between blocks %d and %d in zone %d",
			lba, zone->lba_to_phys_table[lba], block, zone_num);

		/* Test that this block is valid*/
		if (sm_check_block(ftl, zone_num, block))
			continue;

		/* Test now the old block */
		if (sm_check_block(ftl, zone_num,
					zone->lba_to_phys_table[lba])) {
			zone->lba_to_phys_table[lba] = block;
			continue;
		}

		/* If both blocks are valid and share same LBA, it means that
			they hold different versions of same data. It not
			known which is more recent, thus just erase one of them
		*/
		sm_printk("both blocks are valid, erasing the later");
		sm_erase_block(ftl, zone_num, block, 1);
	}

	dbg("zone initialized");
	zone->initialized = 1;

	/* No free sectors, means that the zone is heavily damaged, write won't
		work, but it can still can be (partially) read */
	if (!kfifo_len(&zone->free_sectors)) {
		sm_printk("no free blocks in zone %d", zone_num);
		return 0;
	}

	/* Randomize first block we write to */
	get_random_bytes(&i, 2);
	i %= (kfifo_len(&zone->free_sectors) / 2);

	while (i--) {
		len = kfifo_out(&zone->free_sectors,
					(unsigned char *)&block, 2);
		WARN_ON(len != 2);
		kfifo_in(&zone->free_sectors, (const unsigned char *)&block, 2);
	}
	return 0;
}

/* Get and automatically initialize an FTL mapping for one zone */
struct ftl_zone *sm_get_zone(struct sm_ftl *ftl, int zone_num)
{
	struct ftl_zone *zone;
	int error;

	BUG_ON(zone_num >= ftl->zone_count);
	zone = &ftl->zones[zone_num];

	if (!zone->initialized) {
		error = sm_init_zone(ftl, zone_num);

		if (error)
			return ERR_PTR(error);
	}
	return zone;
}


/* ----------------- cache handling ------------------------------------------*/

/* Initialize the one block cache */
void sm_cache_init(struct sm_ftl *ftl)
{
	ftl->cache_data_invalid_bitmap = 0xFFFFFFFF;
	ftl->cache_clean = 1;
	ftl->cache_zone = -1;
	ftl->cache_block = -1;
	/*memset(ftl->cache_data, 0xAA, ftl->block_size);*/
}

/* Put sector in one block cache */
void sm_cache_put(struct sm_ftl *ftl, char *buffer, int boffset)
{
	memcpy(ftl->cache_data + boffset, buffer, SM_SECTOR_SIZE);
	clear_bit(boffset / SM_SECTOR_SIZE, &ftl->cache_data_invalid_bitmap);
	ftl->cache_clean = 0;
}

/* Read a sector from the cache */
int sm_cache_get(struct sm_ftl *ftl, char *buffer, int boffset)
{
	if (test_bit(boffset / SM_SECTOR_SIZE,
		&ftl->cache_data_invalid_bitmap))
			return -1;

	memcpy(buffer, ftl->cache_data + boffset, SM_SECTOR_SIZE);
	return 0;
}

/* Write the cache to hardware */
int sm_cache_flush(struct sm_ftl *ftl)
{
	struct ftl_zone *zone;

	int sector_num;
	uint16_t write_sector;
	int zone_num = ftl->cache_zone;
	int block_num;

	if (ftl->cache_clean)
		return 0;

	if (ftl->unstable)
		return -EIO;

	BUG_ON(zone_num < 0);
	zone = &ftl->zones[zone_num];
	block_num = zone->lba_to_phys_table[ftl->cache_block];


	/* Try to read all unread areas of the cache block*/
	for_each_set_bit(sector_num, &ftl->cache_data_invalid_bitmap,
		ftl->block_size / SM_SECTOR_SIZE) {

		if (!sm_read_sector(ftl,
			zone_num, block_num, sector_num * SM_SECTOR_SIZE,
			ftl->cache_data + sector_num * SM_SECTOR_SIZE, NULL))
				clear_bit(sector_num,
					&ftl->cache_data_invalid_bitmap);
	}
restart:

	if (ftl->unstable)
		return -EIO;

	/* If there are no spare blocks, */
	/* we could still continue by erasing/writing the current block,
		but for such worn out media it doesn't worth the trouble,
			and the dangers */
	if (kfifo_out(&zone->free_sectors,
				(unsigned char *)&write_sector, 2) != 2) {
		dbg("no free sectors for write!");
		return -EIO;
	}


	if (sm_write_block(ftl, ftl->cache_data, zone_num, write_sector,
		ftl->cache_block, ftl->cache_data_invalid_bitmap))
			goto restart;

	/* Update the FTL table */
	zone->lba_to_phys_table[ftl->cache_block] = write_sector;

	/* Write succesfull, so erase and free the old block */
	if (block_num > 0)
		sm_erase_block(ftl, zone_num, block_num, 1);

	sm_cache_init(ftl);
	return 0;
}


/* flush timer, runs a second after last write */
static void sm_cache_flush_timer(unsigned long data)
{
	struct sm_ftl *ftl = (struct sm_ftl *)data;
	queue_work(cache_flush_workqueue, &ftl->flush_work);
}

/* cache flush work, kicked by timer */
static void sm_cache_flush_work(struct work_struct *work)
{
	struct sm_ftl *ftl = container_of(work, struct sm_ftl, flush_work);
	mutex_lock(&ftl->mutex);
	sm_cache_flush(ftl);
	mutex_unlock(&ftl->mutex);
	return;
}

/* ---------------- outside interface -------------------------------------- */

/* outside interface: read a sector */
static int sm_read(struct mtd_blktrans_dev *dev,
		   unsigned long sect_no, char *buf)
{
	struct sm_ftl *ftl = dev->priv;
	struct ftl_zone *zone;
	int error = 0, in_cache = 0;
	int zone_num, block, boffset;

	sm_break_offset(ftl, sect_no << 9, &zone_num, &block, &boffset);
	mutex_lock(&ftl->mutex);


	zone = sm_get_zone(ftl, zone_num);
	if (IS_ERR(zone)) {
		error = PTR_ERR(zone);
		goto unlock;
	}

	/* Have to look at cache first */
	if (ftl->cache_zone == zone_num && ftl->cache_block == block) {
		in_cache = 1;
		if (!sm_cache_get(ftl, buf, boffset))
			goto unlock;
	}

	/* Translate the block and return if doesn't exist in the table */
	block = zone->lba_to_phys_table[block];

	if (block == -1) {
		memset(buf, 0xFF, SM_SECTOR_SIZE);
		goto unlock;
	}

	if (sm_read_sector(ftl, zone_num, block, boffset, buf, NULL)) {
		error = -EIO;
		goto unlock;
	}

	if (in_cache)
		sm_cache_put(ftl, buf, boffset);
unlock:
	mutex_unlock(&ftl->mutex);
	return error;
}

/* outside interface: write a sector */
static int sm_write(struct mtd_blktrans_dev *dev,
				unsigned long sec_no, char *buf)
{
	struct sm_ftl *ftl = dev->priv;
	struct ftl_zone *zone;
	int error, zone_num, block, boffset;

	BUG_ON(ftl->readonly);
	sm_break_offset(ftl, sec_no << 9, &zone_num, &block, &boffset);

	/* No need in flush thread running now */
	del_timer(&ftl->timer);
	mutex_lock(&ftl->mutex);

	zone = sm_get_zone(ftl, zone_num);
	if (IS_ERR(zone)) {
		error = PTR_ERR(zone);
		goto unlock;
	}

	/* If entry is not in cache, flush it */
	if (ftl->cache_block != block || ftl->cache_zone != zone_num) {

		error = sm_cache_flush(ftl);
		if (error)
			goto unlock;

		ftl->cache_block = block;
		ftl->cache_zone = zone_num;
	}

	sm_cache_put(ftl, buf, boffset);
unlock:
	mod_timer(&ftl->timer, jiffies + msecs_to_jiffies(cache_timeout));
	mutex_unlock(&ftl->mutex);
	return error;
}

/* outside interface: flush everything */
static int sm_flush(struct mtd_blktrans_dev *dev)
{
	struct sm_ftl *ftl = dev->priv;
	int retval;

	mutex_lock(&ftl->mutex);
	retval =  sm_cache_flush(ftl);
	mutex_unlock(&ftl->mutex);
	return retval;
}

/* outside interface: device is released */
static int sm_release(struct mtd_blktrans_dev *dev)
{
	struct sm_ftl *ftl = dev->priv;

	mutex_lock(&ftl->mutex);
	del_timer_sync(&ftl->timer);
	cancel_work_sync(&ftl->flush_work);
	sm_cache_flush(ftl);
	mutex_unlock(&ftl->mutex);
	return 0;
}

/* outside interface: get geometry */
static int sm_getgeo(struct mtd_blktrans_dev *dev, struct hd_geometry *geo)
{
	struct sm_ftl *ftl = dev->priv;
	geo->heads = ftl->heads;
	geo->sectors = ftl->sectors;
	geo->cylinders = ftl->cylinders;
	return 0;
}

/* external interface: main initialization function */
static void sm_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct mtd_blktrans_dev *trans;
	struct sm_ftl *ftl;

	/* Allocate & initialize our private structure */
	ftl = kzalloc(sizeof(struct sm_ftl), GFP_KERNEL);
	if (!ftl)
		goto error1;


	mutex_init(&ftl->mutex);
	setup_timer(&ftl->timer, sm_cache_flush_timer, (unsigned long)ftl);
	INIT_WORK(&ftl->flush_work, sm_cache_flush_work);
	init_completion(&ftl->erase_completion);

	/* Read media information */
	if (sm_get_media_info(ftl, mtd)) {
		dbg("found unsupported mtd device, aborting");
		goto error2;
	}


	/* Allocate temporary CIS buffer for read retry support */
	ftl->cis_buffer = kzalloc(SM_SECTOR_SIZE, GFP_KERNEL);
	if (!ftl->cis_buffer)
		goto error2;

	/* Allocate zone array, it will be initialized on demand */
	ftl->zones = kzalloc(sizeof(struct ftl_zone) * ftl->zone_count,
								GFP_KERNEL);
	if (!ftl->zones)
		goto error3;

	/* Allocate the cache*/
	ftl->cache_data = kzalloc(ftl->block_size, GFP_KERNEL);

	if (!ftl->cache_data)
		goto error4;

	sm_cache_init(ftl);


	/* Allocate upper layer structure and initialize it */
	trans = kzalloc(sizeof(struct mtd_blktrans_dev), GFP_KERNEL);
	if (!trans)
		goto error5;

	ftl->trans = trans;
	trans->priv = ftl;

	trans->tr = tr;
	trans->mtd = mtd;
	trans->devnum = -1;
	trans->size = (ftl->block_size * ftl->max_lba * ftl->zone_count) >> 9;
	trans->readonly = ftl->readonly;

	if (sm_find_cis(ftl)) {
		dbg("CIS not found on mtd device, aborting");
		goto error6;
	}

	ftl->disk_attributes = sm_create_sysfs_attributes(ftl);
	if (!ftl->disk_attributes)
		goto error6;
	trans->disk_attributes = ftl->disk_attributes;

	sm_printk("Found %d MiB xD/SmartMedia FTL on mtd%d",
		(int)(mtd->size / (1024 * 1024)), mtd->index);

	dbg("FTL layout:");
	dbg("%d zone(s), each consists of %d blocks (+%d spares)",
		ftl->zone_count, ftl->max_lba,
		ftl->zone_size - ftl->max_lba);
	dbg("each block consists of %d bytes",
		ftl->block_size);


	/* Register device*/
	if (add_mtd_blktrans_dev(trans)) {
		dbg("error in mtdblktrans layer");
		goto error6;
	}
	return;
error6:
	kfree(trans);
error5:
	kfree(ftl->cache_data);
error4:
	kfree(ftl->zones);
error3:
	kfree(ftl->cis_buffer);
error2:
	kfree(ftl);
error1:
	return;
}

/* main interface: device {surprise,} removal */
static void sm_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct sm_ftl *ftl = dev->priv;
	int i;

	del_mtd_blktrans_dev(dev);
	ftl->trans = NULL;

	for (i = 0 ; i < ftl->zone_count; i++) {

		if (!ftl->zones[i].initialized)
			continue;

		kfree(ftl->zones[i].lba_to_phys_table);
		kfifo_free(&ftl->zones[i].free_sectors);
	}

	sm_delete_sysfs_attributes(ftl);
	kfree(ftl->cis_buffer);
	kfree(ftl->zones);
	kfree(ftl->cache_data);
	kfree(ftl);
}

static struct mtd_blktrans_ops sm_ftl_ops = {
	.name		= "smblk",
	.major		= 0,
	.part_bits	= SM_FTL_PARTN_BITS,
	.blksize	= SM_SECTOR_SIZE,
	.getgeo		= sm_getgeo,

	.add_mtd	= sm_add_mtd,
	.remove_dev	= sm_remove_dev,

	.readsect	= sm_read,
	.writesect	= sm_write,

	.flush		= sm_flush,
	.release	= sm_release,

	.owner		= THIS_MODULE,
};

static __init int sm_module_init(void)
{
	int error = 0;
	cache_flush_workqueue = create_freezable_workqueue("smflush");

	if (IS_ERR(cache_flush_workqueue))
		return PTR_ERR(cache_flush_workqueue);

	error = register_mtd_blktrans(&sm_ftl_ops);
	if (error)
		destroy_workqueue(cache_flush_workqueue);
	return error;

}

static void __exit sm_module_exit(void)
{
	destroy_workqueue(cache_flush_workqueue);
	deregister_mtd_blktrans(&sm_ftl_ops);
}

module_init(sm_module_init);
module_exit(sm_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxim Levitsky <maximlevitsky@gmail.com>");
MODULE_DESCRIPTION("Smartmedia/xD mtd translation layer");
