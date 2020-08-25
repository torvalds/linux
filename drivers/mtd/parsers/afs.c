// SPDX-License-Identifier: GPL-2.0-or-later
/*======================================================================

    drivers/mtd/afs.c: ARM Flash Layout/Partitioning

    Copyright © 2000 ARM Limited
    Copyright (C) 2019 Linus Walleij


   This is access code for flashes using ARM's flash partitioning
   standards.

======================================================================*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define AFSV1_FOOTER_MAGIC 0xA0FFFF9F
#define AFSV2_FOOTER_MAGIC1 0x464C5348 /* "FLSH" */
#define AFSV2_FOOTER_MAGIC2 0x464F4F54 /* "FOOT" */

struct footer_v1 {
	u32 image_info_base;	/* Address of first word of ImageFooter  */
	u32 image_start;	/* Start of area reserved by this footer */
	u32 signature;		/* 'Magic' number proves it's a footer   */
	u32 type;		/* Area type: ARM Image, SIB, customer   */
	u32 checksum;		/* Just this structure                   */
};

struct image_info_v1 {
	u32 bootFlags;		/* Boot flags, compression etc.          */
	u32 imageNumber;	/* Unique number, selects for boot etc.  */
	u32 loadAddress;	/* Address program should be loaded to   */
	u32 length;		/* Actual size of image                  */
	u32 address;		/* Image is executed from here           */
	char name[16];		/* Null terminated                       */
	u32 headerBase;		/* Flash Address of any stripped header  */
	u32 header_length;	/* Length of header in memory            */
	u32 headerType;		/* AIF, RLF, s-record etc.               */
	u32 checksum;		/* Image checksum (inc. this struct)     */
};

static u32 word_sum(void *words, int num)
{
	u32 *p = words;
	u32 sum = 0;

	while (num--)
		sum += *p++;

	return sum;
}

static u32 word_sum_v2(u32 *p, u32 num)
{
	u32 sum = 0;
	int i;

	for (i = 0; i < num; i++) {
		u32 val;

		val = p[i];
		if (val > ~sum)
			sum++;
		sum += val;
	}
	return ~sum;
}

static bool afs_is_v1(struct mtd_info *mtd, u_int off)
{
	/* The magic is 12 bytes from the end of the erase block */
	u_int ptr = off + mtd->erasesize - 12;
	u32 magic;
	size_t sz;
	int ret;

	ret = mtd_read(mtd, ptr, 4, &sz, (u_char *)&magic);
	if (ret < 0) {
		printk(KERN_ERR "AFS: mtd read failed at 0x%x: %d\n",
		       ptr, ret);
		return false;
	}
	if (ret >= 0 && sz != 4)
		return false;

	return (magic == AFSV1_FOOTER_MAGIC);
}

static bool afs_is_v2(struct mtd_info *mtd, u_int off)
{
	/* The magic is the 8 last bytes of the erase block */
	u_int ptr = off + mtd->erasesize - 8;
	u32 foot[2];
	size_t sz;
	int ret;

	ret = mtd_read(mtd, ptr, 8, &sz, (u_char *)foot);
	if (ret < 0) {
		printk(KERN_ERR "AFS: mtd read failed at 0x%x: %d\n",
		       ptr, ret);
		return false;
	}
	if (ret >= 0 && sz != 8)
		return false;

	return (foot[0] == AFSV2_FOOTER_MAGIC1 &&
		foot[1] == AFSV2_FOOTER_MAGIC2);
}

static int afs_parse_v1_partition(struct mtd_info *mtd,
				  u_int off, struct mtd_partition *part)
{
	struct footer_v1 fs;
	struct image_info_v1 iis;
	u_int mask;
	/*
	 * Static checks cannot see that we bail out if we have an error
	 * reading the footer.
	 */
	u_int iis_ptr;
	u_int img_ptr;
	u_int ptr;
	size_t sz;
	int ret;
	int i;

	/*
	 * This is the address mask; we use this to mask off out of
	 * range address bits.
	 */
	mask = mtd->size - 1;

	ptr = off + mtd->erasesize - sizeof(fs);
	ret = mtd_read(mtd, ptr, sizeof(fs), &sz, (u_char *)&fs);
	if (ret >= 0 && sz != sizeof(fs))
		ret = -EINVAL;
	if (ret < 0) {
		printk(KERN_ERR "AFS: mtd read failed at 0x%x: %d\n",
		       ptr, ret);
		return ret;
	}
	/*
	 * Check the checksum.
	 */
	if (word_sum(&fs, sizeof(fs) / sizeof(u32)) != 0xffffffff)
		return -EINVAL;

	/*
	 * Hide the SIB (System Information Block)
	 */
	if (fs.type == 2)
		return 0;

	iis_ptr = fs.image_info_base & mask;
	img_ptr = fs.image_start & mask;

	/*
	 * Check the image info base.  This can not
	 * be located after the footer structure.
	 */
	if (iis_ptr >= ptr)
		return 0;

	/*
	 * Check the start of this image.  The image
	 * data can not be located after this block.
	 */
	if (img_ptr > off)
		return 0;

	/* Read the image info block */
	memset(&iis, 0, sizeof(iis));
	ret = mtd_read(mtd, iis_ptr, sizeof(iis), &sz, (u_char *)&iis);
	if (ret < 0) {
		printk(KERN_ERR "AFS: mtd read failed at 0x%x: %d\n",
		       iis_ptr, ret);
		return -EINVAL;
	}

	if (sz != sizeof(iis))
		return -EINVAL;

	/*
	 * Validate the name - it must be NUL terminated.
	 */
	for (i = 0; i < sizeof(iis.name); i++)
		if (iis.name[i] == '\0')
			break;
	if (i > sizeof(iis.name))
		return -EINVAL;

	part->name = kstrdup(iis.name, GFP_KERNEL);
	if (!part->name)
		return -ENOMEM;

	part->size = (iis.length + mtd->erasesize - 1) & ~(mtd->erasesize - 1);
	part->offset = img_ptr;
	part->mask_flags = 0;

	printk("  mtd: at 0x%08x, %5lluKiB, %8u, %s\n",
	       img_ptr, part->size / 1024,
	       iis.imageNumber, part->name);

	return 0;
}

static int afs_parse_v2_partition(struct mtd_info *mtd,
				  u_int off, struct mtd_partition *part)
{
	u_int ptr;
	u32 footer[12];
	u32 imginfo[36];
	char *name;
	u32 version;
	u32 entrypoint;
	u32 attributes;
	u32 region_count;
	u32 block_start;
	u32 block_end;
	u32 crc;
	size_t sz;
	int ret;
	int i;
	int pad = 0;

	pr_debug("Parsing v2 partition @%08x-%08x\n",
		 off, off + mtd->erasesize);

	/* First read the footer */
	ptr = off + mtd->erasesize - sizeof(footer);
	ret = mtd_read(mtd, ptr, sizeof(footer), &sz, (u_char *)footer);
	if ((ret < 0) || (ret >= 0 && sz != sizeof(footer))) {
		pr_err("AFS: mtd read failed at 0x%x: %d\n",
		       ptr, ret);
		return -EIO;
	}
	name = (char *) &footer[0];
	version = footer[9];
	ptr = off + mtd->erasesize - sizeof(footer) - footer[8];

	pr_debug("found image \"%s\", version %08x, info @%08x\n",
		 name, version, ptr);

	/* Then read the image information */
	ret = mtd_read(mtd, ptr, sizeof(imginfo), &sz, (u_char *)imginfo);
	if ((ret < 0) || (ret >= 0 && sz != sizeof(imginfo))) {
		pr_err("AFS: mtd read failed at 0x%x: %d\n",
		       ptr, ret);
		return -EIO;
	}

	/* 32bit platforms have 4 bytes padding */
	crc = word_sum_v2(&imginfo[1], 34);
	if (!crc) {
		pr_debug("Padding 1 word (4 bytes)\n");
		pad = 1;
	} else {
		/* 64bit platforms have 8 bytes padding */
		crc = word_sum_v2(&imginfo[2], 34);
		if (!crc) {
			pr_debug("Padding 2 words (8 bytes)\n");
			pad = 2;
		}
	}
	if (crc) {
		pr_err("AFS: bad checksum on v2 image info: %08x\n", crc);
		return -EINVAL;
	}
	entrypoint = imginfo[pad];
	attributes = imginfo[pad+1];
	region_count = imginfo[pad+2];
	block_start = imginfo[20];
	block_end = imginfo[21];

	pr_debug("image entry=%08x, attr=%08x, regions=%08x, "
		 "bs=%08x, be=%08x\n",
		 entrypoint, attributes, region_count,
		 block_start, block_end);

	for (i = 0; i < region_count; i++) {
		u32 region_load_addr = imginfo[pad + 3 + i*4];
		u32 region_size = imginfo[pad + 4 + i*4];
		u32 region_offset = imginfo[pad + 5 + i*4];
		u32 region_start;
		u32 region_end;

		pr_debug("  region %d: address: %08x, size: %08x, "
			 "offset: %08x\n",
			 i,
			 region_load_addr,
			 region_size,
			 region_offset);

		region_start = off + region_offset;
		region_end = region_start + region_size;
		/* Align partition to end of erase block */
		region_end += (mtd->erasesize - 1);
		region_end &= ~(mtd->erasesize -1);
		pr_debug("   partition start = %08x, partition end = %08x\n",
			 region_start, region_end);

		/* Create one partition per region */
		part->name = kstrdup(name, GFP_KERNEL);
		if (!part->name)
			return -ENOMEM;
		part->offset = region_start;
		part->size = region_end - region_start;
		part->mask_flags = 0;
	}

	return 0;
}

static int parse_afs_partitions(struct mtd_info *mtd,
				const struct mtd_partition **pparts,
				struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	u_int off, sz;
	int ret = 0;
	int i;

	/* Count the partitions by looping over all erase blocks */
	for (i = off = sz = 0; off < mtd->size; off += mtd->erasesize) {
		if (afs_is_v1(mtd, off)) {
			sz += sizeof(struct mtd_partition);
			i += 1;
		}
		if (afs_is_v2(mtd, off)) {
			sz += sizeof(struct mtd_partition);
			i += 1;
		}
	}

	if (!i)
		return 0;

	parts = kzalloc(sz, GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	/*
	 * Identify the partitions
	 */
	for (i = off = 0; off < mtd->size; off += mtd->erasesize) {
		if (afs_is_v1(mtd, off)) {
			ret = afs_parse_v1_partition(mtd, off, &parts[i]);
			if (ret)
				goto out_free_parts;
			i++;
		}
		if (afs_is_v2(mtd, off)) {
			ret = afs_parse_v2_partition(mtd, off, &parts[i]);
			if (ret)
				goto out_free_parts;
			i++;
		}
	}

	*pparts = parts;
	return i;

out_free_parts:
	while (i >= 0) {
		kfree(parts[i].name);
		i--;
	}
	kfree(parts);
	*pparts = NULL;
	return ret;
}

static const struct of_device_id mtd_parser_afs_of_match_table[] = {
	{ .compatible = "arm,arm-firmware-suite" },
	{},
};
MODULE_DEVICE_TABLE(of, mtd_parser_afs_of_match_table);

static struct mtd_part_parser afs_parser = {
	.parse_fn = parse_afs_partitions,
	.name = "afs",
	.of_match_table = mtd_parser_afs_of_match_table,
};
module_mtd_part_parser(afs_parser);

MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("ARM Firmware Suite partition parser");
MODULE_LICENSE("GPL");
