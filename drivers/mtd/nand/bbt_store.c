// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 */

#include <linux/mtd/bbt_store.h>
#include <linux/slab.h>

#ifdef BBT_DEBUG
#define BBT_DBG pr_err
#else
#define BBT_DBG(args...)
#endif

struct nanddev_bbt_info {
	u8 pattern[4];
	unsigned int version;
};

static u8 bbt_pattern[] = {'B', 'b', 't', '0' };

static int nanddev_read_bbt(struct nand_device *nand, u32 block, bool update)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nblocks = nanddev_neraseblocks(nand);
	unsigned int nbytes = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG) * sizeof(*nand->bbt.cache);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	u8 *data_buf, *oob_buf;
	struct nanddev_bbt_info *bbt_info;
	struct mtd_oob_ops ops;
	int bbt_page_num;
	int ret = 0;
	unsigned int version = 0;

	if (!nand->bbt.cache)
		return -ENOMEM;

	if (block >= nblocks)
		return -EINVAL;

	/* Aligned to page size, and even pages is better */
	bbt_page_num = (sizeof(struct nanddev_bbt_info) + nbytes +
		mtd->writesize - 1) >> (ffs(mtd->writesize) - 1);
	bbt_page_num = (bbt_page_num + 1) / 2 * 2;
	data_buf = kzalloc(bbt_page_num * mtd->writesize, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;
	oob_buf = kzalloc(bbt_page_num * mtd->oobsize, GFP_KERNEL);
	if (!oob_buf) {
		kfree(data_buf);

		return -ENOMEM;
	}

	bbt_info = (struct nanddev_bbt_info *)(data_buf + nbytes);

	memset(&ops, 0, sizeof(struct mtd_oob_ops));
	ops.mode = MTD_OPS_PLACE_OOB;
	ops.datbuf = data_buf;
	ops.len = bbt_page_num * mtd->writesize;
	ops.oobbuf = oob_buf;
	ops.ooblen = bbt_page_num * mtd->oobsize;
	ops.ooboffs = 0;

	ret = mtd_read_oob(mtd, block * mtd->erasesize, &ops);
	if (ret && ret != -EUCLEAN) {
		pr_err("%s fail %d\n", __func__, ret);
		ret = -EIO;
		goto out;
	} else {
		ret = 0;
	}

	if (oob_buf[0] != 0xff && !memcmp(bbt_pattern, bbt_info->pattern, 4))
		version = bbt_info->version;

	BBT_DBG("read_bbt from blk=%d tag=%d ver=%d\n", block, update, version);
	if (update && version > nand->bbt.version) {
		memcpy(nand->bbt.cache, data_buf, nbytes);
		nand->bbt.version = version;
	}

out:
	kfree(oob_buf);
	kfree(data_buf);

	return ret < 0 ? -EIO : version;
}

static int nanddev_write_bbt(struct nand_device *nand, u32 block)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nblocks = nanddev_neraseblocks(nand);
	unsigned int nbytes = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG) * sizeof(*nand->bbt.cache);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	u8 *data_buf, *oob_buf;
	struct nanddev_bbt_info *bbt_info;
	struct mtd_oob_ops ops;
	int bbt_page_num;
	int ret = 0;
	struct nand_pos pos;

	BBT_DBG("write_bbt to blk=%d ver=%d\n", block, nand->bbt.version);
	if (!nand->bbt.cache)
		return -ENOMEM;

	if (block >= nblocks)
		return -EINVAL;

	/* Aligned to page size, and even pages is better */
	bbt_page_num = (sizeof(struct nanddev_bbt_info) + nbytes +
		mtd->writesize - 1) >> (ffs(mtd->writesize) - 1);
	bbt_page_num = (bbt_page_num + 1) / 2 * 2;

	data_buf = kzalloc(bbt_page_num * mtd->writesize, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;
	oob_buf = kzalloc(bbt_page_num * mtd->oobsize, GFP_KERNEL);
	if (!oob_buf) {
		kfree(data_buf);

		return -ENOMEM;
	}

	bbt_info = (struct nanddev_bbt_info *)(data_buf + nbytes);

	memcpy(data_buf, nand->bbt.cache, nbytes);
	memcpy(bbt_info, bbt_pattern, 4);
	bbt_info->version = nand->bbt.version;

	nanddev_offs_to_pos(nand, block * mtd->erasesize, &pos);
	ret = nand->ops->erase(nand, &pos);
	if (ret)
		goto out;

	memset(&ops, 0, sizeof(struct mtd_oob_ops));
	ops.mode = MTD_OPS_PLACE_OOB;
	ops.datbuf = data_buf;
	ops.len = bbt_page_num * mtd->writesize;
	ops.oobbuf = oob_buf;
	ops.ooblen = bbt_page_num * mtd->oobsize;
	ops.ooboffs = 0;
	ret = mtd_write_oob(mtd, block * mtd->erasesize, &ops);

out:
	kfree(oob_buf);
	kfree(data_buf);

	return ret;
}

static int nanddev_bbt_format(struct nand_device *nand)
{
	unsigned int nblocks = nanddev_neraseblocks(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct nand_pos pos;
	u32 start_block, block;

	start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;

	for (block = 0; block < nblocks; block++) {
		nanddev_offs_to_pos(nand, block * mtd->erasesize, &pos);
		if (nanddev_isbad(nand, &pos))
			nanddev_bbt_set_block_status(nand, block,
						     NAND_BBT_BLOCK_FACTORY_BAD);
	}

	for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
		if (nanddev_bbt_get_block_status(nand, start_block + block) ==
			NAND_BBT_BLOCK_GOOD)
			nanddev_bbt_set_block_status(nand, start_block + block,
						     NAND_BBT_BLOCK_WORN);
	}

	return 0;
}

/**
 * nanddev_scan_bbt_in_flash() - Scan for a BBT in the flash
 * @nand: nand device
 *
 * Scan a bbt in flash, if not exist, format one.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int nanddev_scan_bbt_in_flash(struct nand_device *nand)
{
	unsigned int nblocks = nanddev_neraseblocks(nand);
	u32 start_block, block;
	int ret = 0;

	nand->bbt.version = 0;
	start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;
	for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++)
		nanddev_read_bbt(nand, start_block + block, true);

	if (nand->bbt.version == 0) {
		nanddev_bbt_format(nand);
		ret = nanddev_bbt_in_flash_update(nand);
		if (ret) {
			nand->bbt.option = 0;
			pr_err("%s fail\n", __func__);
		}
	}

	nand->bbt.option |= NANDDEV_BBT_SCANNED;

	return ret;
}
EXPORT_SYMBOL_GPL(nanddev_scan_bbt_in_flash);

/**
 * nanddev_bbt_in_flash_update() - Update a BBT
 * @nand: nand device
 *
 * Update the BBT to flash.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int nanddev_bbt_in_flash_update(struct nand_device *nand)
{
	if (nand->bbt.option & NANDDEV_BBT_SCANNED) {
		unsigned int nblocks = nanddev_neraseblocks(nand);
		u32 bbt_version[NANDDEV_BBT_SCAN_MAXBLOCKS];
		int start_block, block;
		u32 min_version, block_des;
		int ret, count = 0;

		start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;
		for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
			ret = nanddev_bbt_get_block_status(nand, start_block + block);
			if (ret == NAND_BBT_BLOCK_FACTORY_BAD) {
				bbt_version[block] = 0xFFFFFFFF;
				continue;
			}
			ret = nanddev_read_bbt(nand, start_block + block,
					       false);
			if (ret < 0)
				bbt_version[block] = 0xFFFFFFFF;
			else if (ret == 0)
				bbt_version[block] = 0;
			else
				bbt_version[block] = ret;
		}
get_min_ver:
		min_version = 0xFFFFFFFF;
		block_des = 0;
		for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
			if (bbt_version[block] < min_version) {
				min_version = bbt_version[block];
				block_des = start_block + block;
			}
		}

		if (block_des > 0) {
			nand->bbt.version++;
			ret = nanddev_write_bbt(nand, block_des);
			bbt_version[block_des - start_block] = 0xFFFFFFFF;
			if (ret) {
				pr_err("%s blk= %d ret= %d\n", __func__,
				       block_des, ret);
				goto get_min_ver;
			} else {
				count++;
				if (count < 2)
					goto get_min_ver;
				BBT_DBG("%s success\n", __func__);
			}
		} else {
			pr_err("%s failed\n", __func__);

			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nanddev_bbt_in_flash_update);
