/*
 * sharpslpart.c - MTD partition parser for NAND flash using the SHARP FTL
 * for logical addressing, as used on the PXA models of the SHARP SL Series.
 *
 * Copyright (C) 2017 Andrea Adami <andrea.adami@gmail.com>
 *
 * Based on SHARP GPL 2.4 sources:
 *   http://support.ezaurus.com/developer/source/source_dl.asp
 *     drivers/mtd/nand/sharp_sl_logical.c
 *     linux/include/asm-arm/sharp_nand_logical.h
 *
 * Copyright (C) 2002 SHARP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/sizes.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

/* oob structure */
#define NAND_NOOB_LOGADDR_00		8
#define NAND_NOOB_LOGADDR_01		9
#define NAND_NOOB_LOGADDR_10		10
#define NAND_NOOB_LOGADDR_11		11
#define NAND_NOOB_LOGADDR_20		12
#define NAND_NOOB_LOGADDR_21		13

#define BLOCK_IS_RESERVED		0xffff
#define BLOCK_UNMASK_COMPLEMENT		1

/* factory defaults */
#define SHARPSL_NAND_PARTS		3
#define SHARPSL_FTL_PART_SIZE		(7 * SZ_1M)
#define SHARPSL_PARTINFO1_LADDR		0x00060000
#define SHARPSL_PARTINFO2_LADDR		0x00064000

#define BOOT_MAGIC			0x424f4f54
#define FSRO_MAGIC			0x4653524f
#define FSRW_MAGIC			0x46535257

/**
 * struct sharpsl_ftl - Sharp FTL Logical Table
 * @logmax:		number of logical blocks
 * @log2phy:		the logical-to-physical table
 *
 * Structure containing the logical-to-physical translation table
 * used by the SHARP SL FTL.
 */
struct sharpsl_ftl {
	unsigned int logmax;
	unsigned int *log2phy;
};

/* verify that the OOB bytes 8 to 15 are free and available for the FTL */
static int sharpsl_nand_check_ooblayout(struct mtd_info *mtd)
{
	u8 freebytes = 0;
	int section = 0;

	while (true) {
		struct mtd_oob_region oobfree = { };
		int ret, i;

		ret = mtd_ooblayout_free(mtd, section++, &oobfree);
		if (ret)
			break;

		if (!oobfree.length || oobfree.offset > 15 ||
		    (oobfree.offset + oobfree.length) < 8)
			continue;

		i = oobfree.offset >= 8 ? oobfree.offset : 8;
		for (; i < oobfree.offset + oobfree.length && i < 16; i++)
			freebytes |= BIT(i - 8);

		if (freebytes == 0xff)
			return 0;
	}

	return -ENOTSUPP;
}

static int sharpsl_nand_read_oob(struct mtd_info *mtd, loff_t offs, u8 *buf)
{
	struct mtd_oob_ops ops = { };
	int ret;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = buf;

	ret = mtd_read_oob(mtd, offs, &ops);
	if (ret != 0 || mtd->oobsize != ops.oobretlen)
		return -1;

	return 0;
}

/*
 * The logical block number assigned to a physical block is stored in the OOB
 * of the first page, in 3 16-bit copies with the following layout:
 *
 * 01234567 89abcdef
 * -------- --------
 * ECC BB   xyxyxy
 *
 * When reading we check that the first two copies agree.
 * In case of error, matching is tried using the following pairs.
 * Reserved values 0xffff mean the block is kept for wear leveling.
 *
 * 01234567 89abcdef
 * -------- --------
 * ECC BB   xyxy    oob[8]==oob[10] && oob[9]==oob[11]   -> byte0=8   byte1=9
 * ECC BB     xyxy  oob[10]==oob[12] && oob[11]==oob[13] -> byte0=10  byte1=11
 * ECC BB   xy  xy  oob[12]==oob[8] && oob[13]==oob[9]   -> byte0=12  byte1=13
 */
static int sharpsl_nand_get_logical_num(u8 *oob)
{
	u16 us;
	int good0, good1;

	if (oob[NAND_NOOB_LOGADDR_00] == oob[NAND_NOOB_LOGADDR_10] &&
	    oob[NAND_NOOB_LOGADDR_01] == oob[NAND_NOOB_LOGADDR_11]) {
		good0 = NAND_NOOB_LOGADDR_00;
		good1 = NAND_NOOB_LOGADDR_01;
	} else if (oob[NAND_NOOB_LOGADDR_10] == oob[NAND_NOOB_LOGADDR_20] &&
		   oob[NAND_NOOB_LOGADDR_11] == oob[NAND_NOOB_LOGADDR_21]) {
		good0 = NAND_NOOB_LOGADDR_10;
		good1 = NAND_NOOB_LOGADDR_11;
	} else if (oob[NAND_NOOB_LOGADDR_20] == oob[NAND_NOOB_LOGADDR_00] &&
		   oob[NAND_NOOB_LOGADDR_21] == oob[NAND_NOOB_LOGADDR_01]) {
		good0 = NAND_NOOB_LOGADDR_20;
		good1 = NAND_NOOB_LOGADDR_21;
	} else {
		return -EINVAL;
	}

	us = oob[good0] | oob[good1] << 8;

	/* parity check */
	if (hweight16(us) & BLOCK_UNMASK_COMPLEMENT)
		return -EINVAL;

	/* reserved */
	if (us == BLOCK_IS_RESERVED)
		return BLOCK_IS_RESERVED;

	return (us >> 1) & GENMASK(9, 0);
}

static int sharpsl_nand_init_ftl(struct mtd_info *mtd, struct sharpsl_ftl *ftl)
{
	unsigned int block_num, log_num, phymax;
	loff_t block_adr;
	u8 *oob;
	int i, ret;

	oob = kzalloc(mtd->oobsize, GFP_KERNEL);
	if (!oob)
		return -ENOMEM;

	phymax = mtd_div_by_eb(SHARPSL_FTL_PART_SIZE, mtd);

	/* FTL reserves 5% of the blocks + 1 spare  */
	ftl->logmax = ((phymax * 95) / 100) - 1;

	ftl->log2phy = kmalloc_array(ftl->logmax, sizeof(*ftl->log2phy),
				     GFP_KERNEL);
	if (!ftl->log2phy) {
		ret = -ENOMEM;
		goto exit;
	}

	/* initialize ftl->log2phy */
	for (i = 0; i < ftl->logmax; i++)
		ftl->log2phy[i] = UINT_MAX;

	/* create physical-logical table */
	for (block_num = 0; block_num < phymax; block_num++) {
		block_adr = (loff_t)block_num * mtd->erasesize;

		if (mtd_block_isbad(mtd, block_adr))
			continue;

		if (sharpsl_nand_read_oob(mtd, block_adr, oob))
			continue;

		/* get logical block */
		log_num = sharpsl_nand_get_logical_num(oob);

		/* cut-off errors and skip the out-of-range values */
		if (log_num > 0 && log_num < ftl->logmax) {
			if (ftl->log2phy[log_num] == UINT_MAX)
				ftl->log2phy[log_num] = block_num;
		}
	}

	pr_info("Sharp SL FTL: %d blocks used (%d logical, %d reserved)\n",
		phymax, ftl->logmax, phymax - ftl->logmax);

	ret = 0;
exit:
	kfree(oob);
	return ret;
}

static void sharpsl_nand_cleanup_ftl(struct sharpsl_ftl *ftl)
{
	kfree(ftl->log2phy);
}

static int sharpsl_nand_read_laddr(struct mtd_info *mtd,
				   loff_t from,
				   size_t len,
				   void *buf,
				   struct sharpsl_ftl *ftl)
{
	unsigned int log_num, final_log_num;
	unsigned int block_num;
	loff_t block_adr;
	loff_t block_ofs;
	size_t retlen;
	int err;

	log_num = mtd_div_by_eb((u32)from, mtd);
	final_log_num = mtd_div_by_eb(((u32)from + len - 1), mtd);

	if (len <= 0 || log_num >= ftl->logmax || final_log_num > log_num)
		return -EINVAL;

	block_num = ftl->log2phy[log_num];
	block_adr = (loff_t)block_num * mtd->erasesize;
	block_ofs = mtd_mod_by_eb((u32)from, mtd);

	err = mtd_read(mtd, block_adr + block_ofs, len, &retlen, buf);
	/* Ignore corrected ECC errors */
	if (mtd_is_bitflip(err))
		err = 0;

	if (!err && retlen != len)
		err = -EIO;

	if (err)
		pr_err("sharpslpart: error, read failed at %#llx\n",
		       block_adr + block_ofs);

	return err;
}

/*
 * MTD Partition Parser
 *
 * Sample values read from SL-C860
 *
 * # cat /proc/mtd
 * dev:    size   erasesize  name
 * mtd0: 006d0000 00020000 "Filesystem"
 * mtd1: 00700000 00004000 "smf"
 * mtd2: 03500000 00004000 "root"
 * mtd3: 04400000 00004000 "home"
 *
 * PARTITIONINFO1
 * 0x00060000: 00 00 00 00 00 00 70 00 42 4f 4f 54 00 00 00 00  ......p.BOOT....
 * 0x00060010: 00 00 70 00 00 00 c0 03 46 53 52 4f 00 00 00 00  ..p.....FSRO....
 * 0x00060020: 00 00 c0 03 00 00 00 04 46 53 52 57 00 00 00 00  ........FSRW....
 */
struct sharpsl_nand_partinfo {
	__le32 start;
	__le32 end;
	__be32 magic;
	u32 reserved;
};

static int sharpsl_nand_read_partinfo(struct mtd_info *master,
				      loff_t from,
				      size_t len,
				      struct sharpsl_nand_partinfo *buf,
				      struct sharpsl_ftl *ftl)
{
	int ret;

	ret = sharpsl_nand_read_laddr(master, from, len, buf, ftl);
	if (ret)
		return ret;

	/* check for magics */
	if (be32_to_cpu(buf[0].magic) != BOOT_MAGIC ||
	    be32_to_cpu(buf[1].magic) != FSRO_MAGIC ||
	    be32_to_cpu(buf[2].magic) != FSRW_MAGIC) {
		pr_err("sharpslpart: magic values mismatch\n");
		return -EINVAL;
	}

	/* fixup for hardcoded value 64 MiB (for older models) */
	buf[2].end = cpu_to_le32(master->size);

	/* extra sanity check */
	if (le32_to_cpu(buf[0].end) <= le32_to_cpu(buf[0].start) ||
	    le32_to_cpu(buf[1].start) < le32_to_cpu(buf[0].end) ||
	    le32_to_cpu(buf[1].end) <= le32_to_cpu(buf[1].start) ||
	    le32_to_cpu(buf[2].start) < le32_to_cpu(buf[1].end) ||
	    le32_to_cpu(buf[2].end) <= le32_to_cpu(buf[2].start)) {
		pr_err("sharpslpart: partition sizes mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int sharpsl_parse_mtd_partitions(struct mtd_info *master,
					const struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	struct sharpsl_ftl ftl;
	struct sharpsl_nand_partinfo buf[SHARPSL_NAND_PARTS];
	struct mtd_partition *sharpsl_nand_parts;
	int err;

	/* check that OOB bytes 8 to 15 used by the FTL are actually free */
	err = sharpsl_nand_check_ooblayout(master);
	if (err)
		return err;

	/* init logical mgmt (FTL) */
	err = sharpsl_nand_init_ftl(master, &ftl);
	if (err)
		return err;

	/* read and validate first partition table */
	pr_info("sharpslpart: try reading first partition table\n");
	err = sharpsl_nand_read_partinfo(master,
					 SHARPSL_PARTINFO1_LADDR,
					 sizeof(buf), buf, &ftl);
	if (err) {
		/* fallback: read second partition table */
		pr_warn("sharpslpart: first partition table is invalid, retry using the second\n");
		err = sharpsl_nand_read_partinfo(master,
						 SHARPSL_PARTINFO2_LADDR,
						 sizeof(buf), buf, &ftl);
	}

	/* cleanup logical mgmt (FTL) */
	sharpsl_nand_cleanup_ftl(&ftl);

	if (err) {
		pr_err("sharpslpart: both partition tables are invalid\n");
		return err;
	}

	sharpsl_nand_parts = kzalloc(sizeof(*sharpsl_nand_parts) *
				     SHARPSL_NAND_PARTS, GFP_KERNEL);
	if (!sharpsl_nand_parts)
		return -ENOMEM;

	/* original names */
	sharpsl_nand_parts[0].name = "smf";
	sharpsl_nand_parts[0].offset = le32_to_cpu(buf[0].start);
	sharpsl_nand_parts[0].size = le32_to_cpu(buf[0].end) -
				     le32_to_cpu(buf[0].start);

	sharpsl_nand_parts[1].name = "root";
	sharpsl_nand_parts[1].offset = le32_to_cpu(buf[1].start);
	sharpsl_nand_parts[1].size = le32_to_cpu(buf[1].end) -
				     le32_to_cpu(buf[1].start);

	sharpsl_nand_parts[2].name = "home";
	sharpsl_nand_parts[2].offset = le32_to_cpu(buf[2].start);
	sharpsl_nand_parts[2].size = le32_to_cpu(buf[2].end) -
				     le32_to_cpu(buf[2].start);

	*pparts = sharpsl_nand_parts;
	return SHARPSL_NAND_PARTS;
}

static struct mtd_part_parser sharpsl_mtd_parser = {
	.parse_fn = sharpsl_parse_mtd_partitions,
	.name = "sharpslpart",
};
module_mtd_part_parser(sharpsl_mtd_parser);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Adami <andrea.adami@gmail.com>");
MODULE_DESCRIPTION("MTD partitioning for NAND flash on Sharp SL Series");
