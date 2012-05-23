/*
 * File...........: linux/fs/partitions/ibm.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Volker Sameske <sameske@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 */

#include <linux/buffer_head.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <asm/dasd.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/vtoc.h>

#include "check.h"
#include "ibm.h"

/*
 * compute the block number from a
 * cyl-cyl-head-head structure
 */
static sector_t
cchh2blk (struct vtoc_cchh *ptr, struct hd_geometry *geo) {

	sector_t cyl;
	__u16 head;

	/*decode cylinder and heads for large volumes */
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
static sector_t
cchhb2blk (struct vtoc_cchhb *ptr, struct hd_geometry *geo) {

	sector_t cyl;
	__u16 head;

	/*decode cylinder and heads for large volumes */
	cyl = ptr->hh & 0xFFF0;
	cyl <<= 12;
	cyl |= ptr->cc;
	head = ptr->hh & 0x000F;
	return	cyl * geo->heads * geo->sectors +
		head * geo->sectors +
		ptr->b;
}

/*
 */
int ibm_partition(struct parsed_partitions *state)
{
	struct block_device *bdev = state->bdev;
	int blocksize, res;
	loff_t i_size, offset, size, fmt_size;
	dasd_information2_t *info;
	struct hd_geometry *geo;
	char type[5] = {0,};
	char name[7] = {0,};
	union label_t {
		struct vtoc_volume_label_cdl vol;
		struct vtoc_volume_label_ldl lnx;
		struct vtoc_cms_label cms;
	} *label;
	unsigned char *data;
	Sector sect;
	sector_t labelsect;
	char tmp[64];

	res = 0;
	blocksize = bdev_logical_block_size(bdev);
	if (blocksize <= 0)
		goto out_exit;
	i_size = i_size_read(bdev->bd_inode);
	if (i_size == 0)
		goto out_exit;

	info = kmalloc(sizeof(dasd_information2_t), GFP_KERNEL);
	if (info == NULL)
		goto out_exit;
	geo = kmalloc(sizeof(struct hd_geometry), GFP_KERNEL);
	if (geo == NULL)
		goto out_nogeo;
	label = kmalloc(sizeof(union label_t), GFP_KERNEL);
	if (label == NULL)
		goto out_nolab;

	if (ioctl_by_bdev(bdev, BIODASDINFO2, (unsigned long)info) != 0 ||
	    ioctl_by_bdev(bdev, HDIO_GETGEO, (unsigned long)geo) != 0)
		goto out_freeall;

	/*
	 * Special case for FBA disks: label sector does not depend on
	 * blocksize.
	 */
	if ((info->cu_type == 0x6310 && info->dev_type == 0x9336) ||
	    (info->cu_type == 0x3880 && info->dev_type == 0x3370))
		labelsect = info->label_block;
	else
		labelsect = info->label_block * (blocksize >> 9);

	/*
	 * Get volume label, extract name and type.
	 */
	data = read_part_sector(state, labelsect, &sect);
	if (data == NULL)
		goto out_readerr;

	memcpy(label, data, sizeof(union label_t));
	put_dev_sector(sect);

	if ((!info->FBA_layout) && (!strcmp(info->type, "ECKD"))) {
		strncpy(type, label->vol.vollbl, 4);
		strncpy(name, label->vol.volid, 6);
	} else {
		strncpy(type, label->lnx.vollbl, 4);
		strncpy(name, label->lnx.volid, 6);
	}
	EBCASC(type, 4);
	EBCASC(name, 6);

	res = 1;

	/*
	 * Three different formats: LDL, CDL and unformated disk
	 *
	 * identified by info->format
	 *
	 * unformated disks we do not have to care about
	 */
	if (info->format == DASD_FORMAT_LDL) {
		if (strncmp(type, "CMS1", 4) == 0) {
			/*
			 * VM style CMS1 labeled disk
			 */
			blocksize = label->cms.block_size;
			if (label->cms.disk_offset != 0) {
				snprintf(tmp, sizeof(tmp), "CMS1/%8s(MDSK):", name);
				strlcat(state->pp_buf, tmp, PAGE_SIZE);
				/* disk is reserved minidisk */
				offset = label->cms.disk_offset;
				size = (label->cms.block_count - 1)
					* (blocksize >> 9);
			} else {
				snprintf(tmp, sizeof(tmp), "CMS1/%8s:", name);
				strlcat(state->pp_buf, tmp, PAGE_SIZE);
				offset = (info->label_block + 1);
				size = label->cms.block_count
					* (blocksize >> 9);
			}
			put_partition(state, 1, offset*(blocksize >> 9),
				      size-offset*(blocksize >> 9));
		} else {
			if (strncmp(type, "LNX1", 4) == 0) {
				snprintf(tmp, sizeof(tmp), "LNX1/%8s:", name);
				strlcat(state->pp_buf, tmp, PAGE_SIZE);
				if (label->lnx.ldl_version == 0xf2) {
					fmt_size = label->lnx.formatted_blocks
						* (blocksize >> 9);
				} else if (!strcmp(info->type, "ECKD")) {
					/* formated w/o large volume support */
					fmt_size = geo->cylinders * geo->heads
					      * geo->sectors * (blocksize >> 9);
				} else {
					/* old label and no usable disk geometry
					 * (e.g. DIAG) */
					fmt_size = i_size >> 9;
				}
				size = i_size >> 9;
				if (fmt_size < size)
					size = fmt_size;
				offset = (info->label_block + 1);
			} else {
				/* unlabeled disk */
				strlcat(state->pp_buf, "(nonl)", PAGE_SIZE);
				size = i_size >> 9;
				offset = (info->label_block + 1);
			}
			put_partition(state, 1, offset*(blocksize >> 9),
				      size-offset*(blocksize >> 9));
		}
	} else if (info->format == DASD_FORMAT_CDL) {
		/*
		 * New style CDL formatted disk
		 */
		sector_t blk;
		int counter;

		/*
		 * check if VOL1 label is available
		 * if not, something is wrong, skipping partition detection
		 */
		if (strncmp(type, "VOL1",  4) == 0) {
			snprintf(tmp, sizeof(tmp), "VOL1/%8s:", name);
			strlcat(state->pp_buf, tmp, PAGE_SIZE);
			/*
			 * get block number and read then go through format1
			 * labels
			 */
			blk = cchhb2blk(&label->vol.vtoc, geo) + 1;
			counter = 0;
			data = read_part_sector(state, blk * (blocksize/512),
						&sect);
			while (data != NULL) {
				struct vtoc_format1_label f1;

				memcpy(&f1, data,
				       sizeof(struct vtoc_format1_label));
				put_dev_sector(sect);

				/* skip FMT4 / FMT5 / FMT7 labels */
				if (f1.DS1FMTID == _ascebc['4']
				    || f1.DS1FMTID == _ascebc['5']
				    || f1.DS1FMTID == _ascebc['7']
				    || f1.DS1FMTID == _ascebc['9']) {
					blk++;
					data = read_part_sector(state,
						blk * (blocksize/512), &sect);
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
				if (counter >= state->limit)
					break;
				put_partition(state, counter + 1,
					      offset * (blocksize >> 9),
					      size * (blocksize >> 9));
				counter++;
				blk++;
				data = read_part_sector(state,
						blk * (blocksize/512), &sect);
			}

			if (!data)
				/* Are we not supposed to report this ? */
				goto out_readerr;
		} else
			printk(KERN_INFO "Expected Label VOL1 not "
			       "found, treating as CDL formated Disk");

	}

	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	goto out_freeall;


out_readerr:
	res = -1;
out_freeall:
	kfree(label);
out_nolab:
	kfree(geo);
out_nogeo:
	kfree(info);
out_exit:
	return res;
}
