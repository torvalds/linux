// SPDX-License-Identifier: GPL-2.0
/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Volker Sameske <sameske@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2012
 */

#include <linux/buffer_head.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <asm/dasd.h>
#include <asm/ebcdic.h>
#include <linux/uaccess.h>
#include <asm/vtoc.h>
#include <linux/module.h>
#include <linux/dasd_mod.h>

#include "check.h"

union label_t {
	struct vtoc_volume_label_cdl vol;
	struct vtoc_volume_label_ldl lnx;
	struct vtoc_cms_label cms;
};

/*
 * compute the block number from a
 * cyl-cyl-head-head structure
 */
static sector_t cchh2blk(struct vtoc_cchh *ptr, struct hd_geometry *geo)
{
	sector_t cyl;
	__u16 head;

	/* decode cylinder and heads for large volumes */
	cyl = ptr->hh & 0xFFF0;
	cyl <<= 12;
	cyl |= ptr->cc;
	head = ptr->hh & 0x000F;
	return cyl * geo->heads * geo->sectors +
	       head * geo->sectors;
}

/*
 * compute the block number from a
 * cyl-cyl-head-head-block structure
 */
static sector_t cchhb2blk(struct vtoc_cchhb *ptr, struct hd_geometry *geo)
{
	sector_t cyl;
	__u16 head;

	/* decode cylinder and heads for large volumes */
	cyl = ptr->hh & 0xFFF0;
	cyl <<= 12;
	cyl |= ptr->cc;
	head = ptr->hh & 0x000F;
	return	cyl * geo->heads * geo->sectors +
		head * geo->sectors +
		ptr->b;
}

/* Volume Label Type/ID Length */
#define DASD_VOL_TYPE_LEN	4
#define DASD_VOL_ID_LEN		6

/* Volume Label Types */
#define DASD_VOLLBL_TYPE_VOL1 0
#define DASD_VOLLBL_TYPE_LNX1 1
#define DASD_VOLLBL_TYPE_CMS1 2

struct dasd_vollabel {
	char *type;
	int idx;
};

static struct dasd_vollabel dasd_vollabels[] = {
	[DASD_VOLLBL_TYPE_VOL1] = {
		.type = "VOL1",
		.idx = DASD_VOLLBL_TYPE_VOL1,
	},
	[DASD_VOLLBL_TYPE_LNX1] = {
		.type = "LNX1",
		.idx = DASD_VOLLBL_TYPE_LNX1,
	},
	[DASD_VOLLBL_TYPE_CMS1] = {
		.type = "CMS1",
		.idx = DASD_VOLLBL_TYPE_CMS1,
	},
};

static int get_label_by_type(const char *type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dasd_vollabels); i++) {
		if (!memcmp(type, dasd_vollabels[i].type, DASD_VOL_TYPE_LEN))
			return dasd_vollabels[i].idx;
	}

	return -1;
}

static int find_label(struct parsed_partitions *state,
		      dasd_information2_t *info,
		      struct hd_geometry *geo,
		      int blocksize,
		      sector_t *labelsect,
		      char name[],
		      char type[],
		      union label_t *label)
{
	sector_t testsect[3];
	int i, testcount;
	Sector sect;
	void *data;

	/* There a three places where we may find a valid label:
	 * - on an ECKD disk it's block 2
	 * - on an FBA disk it's block 1
	 * - on an CMS formatted FBA disk it is sector 1, even if the block size
	 *   is larger than 512 bytes (possible if the DIAG discipline is used)
	 * If we have a valid info structure, then we know exactly which case we
	 * have, otherwise we just search through all possebilities.
	 */
	if (info) {
		if ((info->cu_type == 0x6310 && info->dev_type == 0x9336) ||
		    (info->cu_type == 0x3880 && info->dev_type == 0x3370))
			testsect[0] = info->label_block;
		else
			testsect[0] = info->label_block * (blocksize >> 9);
		testcount = 1;
	} else {
		testsect[0] = 1;
		testsect[1] = (blocksize >> 9);
		testsect[2] = 2 * (blocksize >> 9);
		testcount = 3;
	}
	for (i = 0; i < testcount; ++i) {
		data = read_part_sector(state, testsect[i], &sect);
		if (data == NULL)
			continue;
		memcpy(label, data, sizeof(*label));
		memcpy(type, data, DASD_VOL_TYPE_LEN);
		EBCASC(type, DASD_VOL_TYPE_LEN);
		put_dev_sector(sect);
		switch (get_label_by_type(type)) {
		case DASD_VOLLBL_TYPE_VOL1:
			memcpy(name, label->vol.volid, DASD_VOL_ID_LEN);
			EBCASC(name, DASD_VOL_ID_LEN);
			*labelsect = testsect[i];
			return 1;
		case DASD_VOLLBL_TYPE_LNX1:
		case DASD_VOLLBL_TYPE_CMS1:
			memcpy(name, label->lnx.volid, DASD_VOL_ID_LEN);
			EBCASC(name, DASD_VOL_ID_LEN);
			*labelsect = testsect[i];
			return 1;
		default:
			break;
		}
	}

	return 0;
}

static int find_vol1_partitions(struct parsed_partitions *state,
				struct hd_geometry *geo,
				int blocksize,
				char name[],
				union label_t *label)
{
	sector_t blk;
	int counter;
	char tmp[64];
	Sector sect;
	unsigned char *data;
	loff_t offset, size;
	struct vtoc_format1_label f1;
	int secperblk;

	snprintf(tmp, sizeof(tmp), "VOL1/%8s:", name);
	strlcat(state->pp_buf, tmp, PAGE_SIZE);
	/*
	 * get start of VTOC from the disk label and then search for format1
	 * and format8 labels
	 */
	secperblk = blocksize >> 9;
	blk = cchhb2blk(&label->vol.vtoc, geo) + 1;
	counter = 0;
	data = read_part_sector(state, blk * secperblk, &sect);
	while (data != NULL) {
		memcpy(&f1, data, sizeof(struct vtoc_format1_label));
		put_dev_sector(sect);
		/* skip FMT4 / FMT5 / FMT7 labels */
		if (f1.DS1FMTID == _ascebc['4']
		    || f1.DS1FMTID == _ascebc['5']
		    || f1.DS1FMTID == _ascebc['7']
		    || f1.DS1FMTID == _ascebc['9']) {
			blk++;
			data = read_part_sector(state, blk * secperblk, &sect);
			continue;
		}
		/* only FMT1 and 8 labels valid at this point */
		if (f1.DS1FMTID != _ascebc['1'] &&
		    f1.DS1FMTID != _ascebc['8'])
			break;
		/* OK, we got valid partition data */
		offset = cchh2blk(&f1.DS1EXT1.llimit, geo);
		size  = cchh2blk(&f1.DS1EXT1.ulimit, geo) -
			offset + geo->sectors;
		offset *= secperblk;
		size *= secperblk;
		if (counter >= state->limit)
			break;
		put_partition(state, counter + 1, offset, size);
		counter++;
		blk++;
		data = read_part_sector(state, blk * secperblk, &sect);
	}
	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	if (!data)
		return -1;

	return 1;
}

static int find_lnx1_partitions(struct parsed_partitions *state,
				struct hd_geometry *geo,
				int blocksize,
				char name[],
				union label_t *label,
				sector_t labelsect,
				sector_t nr_sectors,
				dasd_information2_t *info)
{
	loff_t offset, geo_size, size;
	char tmp[64];
	int secperblk;

	snprintf(tmp, sizeof(tmp), "LNX1/%8s:", name);
	strlcat(state->pp_buf, tmp, PAGE_SIZE);
	secperblk = blocksize >> 9;
	if (label->lnx.ldl_version == 0xf2) {
		size = label->lnx.formatted_blocks * secperblk;
	} else {
		/*
		 * Formated w/o large volume support. If the sanity check
		 * 'size based on geo == size based on nr_sectors' is true, then
		 * we can safely assume that we know the formatted size of
		 * the disk, otherwise we need additional information
		 * that we can only get from a real DASD device.
		 */
		geo_size = geo->cylinders * geo->heads
			* geo->sectors * secperblk;
		size = nr_sectors;
		if (size != geo_size) {
			if (!info) {
				strlcat(state->pp_buf, "\n", PAGE_SIZE);
				return 1;
			}
			if (!strcmp(info->type, "ECKD"))
				if (geo_size < size)
					size = geo_size;
			/* else keep size based on nr_sectors */
		}
	}
	/* first and only partition starts in the first block after the label */
	offset = labelsect + secperblk;
	put_partition(state, 1, offset, size - offset);
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	return 1;
}

static int find_cms1_partitions(struct parsed_partitions *state,
				struct hd_geometry *geo,
				int blocksize,
				char name[],
				union label_t *label,
				sector_t labelsect)
{
	loff_t offset, size;
	char tmp[64];
	int secperblk;

	/*
	 * VM style CMS1 labeled disk
	 */
	blocksize = label->cms.block_size;
	secperblk = blocksize >> 9;
	if (label->cms.disk_offset != 0) {
		snprintf(tmp, sizeof(tmp), "CMS1/%8s(MDSK):", name);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		/* disk is reserved minidisk */
		offset = label->cms.disk_offset * secperblk;
		size = (label->cms.block_count - 1) * secperblk;
	} else {
		snprintf(tmp, sizeof(tmp), "CMS1/%8s:", name);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		/*
		 * Special case for FBA devices:
		 * If an FBA device is CMS formatted with blocksize > 512 byte
		 * and the DIAG discipline is used, then the CMS label is found
		 * in sector 1 instead of block 1. However, the partition is
		 * still supposed to start in block 2.
		 */
		if (labelsect == 1)
			offset = 2 * secperblk;
		else
			offset = labelsect + secperblk;
		size = label->cms.block_count * secperblk;
	}

	put_partition(state, 1, offset, size-offset);
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	return 1;
}


/*
 * This is the main function, called by check.c
 */
int ibm_partition(struct parsed_partitions *state)
{
	int (*fn)(struct gendisk *disk, dasd_information2_t *info);
	struct gendisk *disk = state->disk;
	struct block_device *bdev = disk->part0;
	int blocksize, res;
	loff_t offset, size;
	sector_t nr_sectors;
	dasd_information2_t *info;
	struct hd_geometry *geo;
	char type[DASD_VOL_TYPE_LEN + 1] = "";
	char name[DASD_VOL_ID_LEN + 1] = "";
	sector_t labelsect;
	union label_t *label;

	res = 0;
	if (!disk->fops->getgeo)
		goto out_exit;
	fn = symbol_get(dasd_biodasdinfo);
	blocksize = bdev_logical_block_size(bdev);
	if (blocksize <= 0)
		goto out_symbol;
	nr_sectors = bdev_nr_sectors(bdev);
	if (nr_sectors == 0)
		goto out_symbol;
	info = kmalloc(sizeof(dasd_information2_t), GFP_KERNEL);
	if (info == NULL)
		goto out_symbol;
	geo = kmalloc(sizeof(struct hd_geometry), GFP_KERNEL);
	if (geo == NULL)
		goto out_nogeo;
	label = kmalloc(sizeof(union label_t), GFP_KERNEL);
	if (label == NULL)
		goto out_nolab;
	/* set start if not filled by getgeo function e.g. virtblk */
	geo->start = get_start_sect(bdev);
	if (disk->fops->getgeo(bdev, geo))
		goto out_freeall;
	if (!fn || fn(disk, info)) {
		kfree(info);
		info = NULL;
	}

	if (find_label(state, info, geo, blocksize, &labelsect, name, type, label)) {
		switch (get_label_by_type(type)) {
		case DASD_VOLLBL_TYPE_VOL1:
			res = find_vol1_partitions(state, geo, blocksize, name,
						   label);
			break;
		case DASD_VOLLBL_TYPE_LNX1:
			res = find_lnx1_partitions(state, geo, blocksize, name,
						   label, labelsect, nr_sectors,
						   info);
			break;
		case DASD_VOLLBL_TYPE_CMS1:
			res = find_cms1_partitions(state, geo, blocksize, name,
						   label, labelsect);
			break;
		}
	} else if (info) {
		/*
		 * ugly but needed for backward compatibility:
		 * If the block device is a DASD (i.e. BIODASDINFO2 works),
		 * then we claim it in any case, even though it has no valid
		 * label. If it has the LDL format, then we simply define a
		 * partition as if it had an LNX1 label.
		 */
		res = 1;
		if (info->format == DASD_FORMAT_LDL) {
			strlcat(state->pp_buf, "(nonl)", PAGE_SIZE);
			size = nr_sectors;
			offset = (info->label_block + 1) * (blocksize >> 9);
			put_partition(state, 1, offset, size-offset);
			strlcat(state->pp_buf, "\n", PAGE_SIZE);
		}
	} else
		res = 0;

out_freeall:
	kfree(label);
out_nolab:
	kfree(geo);
out_nogeo:
	kfree(info);
out_symbol:
	if (fn)
		symbol_put(dasd_biodasdinfo);
out_exit:
	return res;
}
