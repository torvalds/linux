// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Free Electrons
 *
 * Authors:
 *	Boris Brezillon <boris.brezillon@free-electrons.com>
 *	Peter Pan <peterpandong@micron.com>
 */

#include <linux/mtd/mtd.h>
#include <linux/slab.h>

#include "sfc_nand.h"
#include "sfc_nand_mtd.h"

#ifdef CONFIG_MTD_NAND_BBT_USING_FLASH

#ifdef BBT_DEBUG
#define BBT_DBG pr_err
#else
#define BBT_DBG(args...)
#endif

#define BBT_VERSION_INVALID		(0xFFFFFFFFU)
#define BBT_VERSION_BLOCK_ABNORMAL	(BBT_VERSION_INVALID - 1)
#define BBT_VERSION_MAX			(BBT_VERSION_INVALID - 8)
struct nanddev_bbt_info {
	u8 pattern[4];
	unsigned int version;
	u32 hash;
};

static u8 bbt_pattern[] = {'B', 'b', 't', '0' };

#if defined(BBT_DEBUG) && defined(BBT_DEBUG_DUMP)
static void bbt_dbg_hex(char *s, void *buf, u32 len)
{
	print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET, 4, 4, buf, len, 0);
}
#endif

static u32 js_hash(u8 *buf, u32 len)
{
	u32 hash = 0x47C6A7E6;
	u32 i;

	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + buf[i] + (hash >> 2));

	return hash;
}

static bool bbt_check_hash(u8 *buf, u32 len, u32 hash_cmp)
{
	u32 hash;

	/* compatible with no-hash version */
	if (hash_cmp == 0 || hash_cmp == 0xFFFFFFFF)
		return 1;

	hash = js_hash(buf, len);
	if (hash != hash_cmp)
		return 0;

	return 1;
}

static u32 bbt_nand_isbad_bypass(struct snand_mtd_dev *nand, u32 block)
{
	struct mtd_info *mtd = snanddev_to_mtd(nand);

	return sfc_nand_isbad_mtd(mtd, block * mtd->erasesize);
}

static int bbt_mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	int i, ret = 0, bbt_page_num, page_addr, block;
	u8 *temp_buf;

	bbt_page_num = ops->len >> mtd->writesize_shift;
	block = from >> mtd->erasesize_shift;

	temp_buf = kzalloc(mtd->writesize + mtd->oobsize, GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	page_addr = (u32)(block << (mtd->erasesize_shift - mtd->writesize_shift));
	for (i = 0; i < bbt_page_num; i++) {
		ret = sfc_nand_read_page_raw(0, page_addr + i, (u32 *)temp_buf);
		if (ret < 0) {
			pr_err("%s fail %d\n", __func__, ret);
			ret = -EIO;
			goto out;
		}

		memcpy(ops->datbuf + i * mtd->writesize, temp_buf, mtd->writesize);
		memcpy(ops->oobbuf + i * mtd->oobsize, temp_buf + mtd->writesize, mtd->oobsize);
	}

out:
	kfree(temp_buf);

	return ret;
}

static int bbt_mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	int i, ret = 0, bbt_page_num, page_addr, block;
	u8 *temp_buf;

	bbt_page_num = ops->len >> mtd->writesize_shift;
	block = to >> mtd->erasesize_shift;

	temp_buf = kzalloc(mtd->writesize + mtd->oobsize, GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	page_addr = (u32)(block << (mtd->erasesize_shift - mtd->writesize_shift));
	for (i = 0; i < bbt_page_num; i++) {
		memcpy(temp_buf, ops->datbuf + i * mtd->writesize, mtd->writesize);
		memcpy(temp_buf + mtd->writesize, ops->oobbuf + i * mtd->oobsize, mtd->oobsize);

		ret = sfc_nand_prog_page_raw(0, page_addr + i, (u32 *)temp_buf);
		if (ret < 0) {
			pr_err("%s fail %d\n", __func__, ret);
			ret = -EIO;
			goto out;
		}
	}

out:
	kfree(temp_buf);

	return ret;
}

/**
 * nanddev_read_bbt() - Read the BBT (Bad Block Table)
 * @nand: NAND device
 * @block: bbt block address
 * @update: true - get version and overwrite bbt.cache with new version;
 *	false - get bbt version only;
 *
 * Initialize the in-memory BBT.
 *
 * Return: positive value means success, 0 means abnornal data, a negative error code otherwise.
 */
static int nanddev_read_bbt(struct snand_mtd_dev *nand, u32 block, bool update)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nblocks = snanddev_neraseblocks(nand);
	unsigned int nbytes = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG) * sizeof(*nand->bbt.cache);
	struct mtd_info *mtd = snanddev_to_mtd(nand);
	u8 *data_buf, *oob_buf;
	struct nanddev_bbt_info *bbt_info;
	struct mtd_oob_ops ops;
	u32 bbt_page_num;
	int ret = 0;
	unsigned int version = 0;

	if (!nand->bbt.cache)
		return -ENOMEM;

	if (block >= nblocks)
		return -EINVAL;

	/* aligned to page size, and even pages is better */
	bbt_page_num = (sizeof(struct nanddev_bbt_info) + nbytes +
		mtd->writesize - 1) >> mtd->writesize_shift;
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

	/* Store one entry for each block */
	ret = bbt_mtd_read_oob(mtd, block * mtd->erasesize, &ops);
	if (ret && ret != -EUCLEAN) {
		pr_err("read_bbt blk=%d fail=%d update=%d\n", block, ret, update);
		ret = 0;
		version = BBT_VERSION_BLOCK_ABNORMAL;
		goto out;
	} else {
		ret = 0;
	}
	/* bad block or good block without bbt */
	if (memcmp(bbt_pattern, bbt_info->pattern, 4)) {
		ret = 0;
		goto out;
	}

	/* good block with abnornal bbt */
	if (oob_buf[0] == 0xff ||
	    !bbt_check_hash(data_buf, nbytes + sizeof(struct nanddev_bbt_info) - 4, bbt_info->hash)) {
		pr_err("read_bbt check fail blk=%d ret=%d update=%d\n", block, ret, update);
		ret = 0;
		version = BBT_VERSION_BLOCK_ABNORMAL;
		goto out;
	}

	/* good block with good bbt */
	version = bbt_info->version;
	BBT_DBG("read_bbt from blk=%d ver=%d update=%d\n", block, version, update);
	if (update && version > nand->bbt.version) {
		memcpy(nand->bbt.cache, data_buf, nbytes);
		nand->bbt.version = version;
	}

#if defined(BBT_DEBUG) && defined(BBT_DEBUG_DUMP)
	bbt_dbg_hex("bbt", data_buf, nbytes + sizeof(struct nanddev_bbt_info));
	if (version) {
		u8 *temp_buf = kzalloc(bbt_page_num * mtd->writesize, GFP_KERNEL);
		bool in_scan = nand->bbt.option & NANDDEV_BBT_SCANNED;

		if (!temp_buf)
			goto out;

		memcpy(temp_buf, nand->bbt.cache, nbytes);
		memcpy(nand->bbt.cache, data_buf, nbytes);

		if (!in_scan)
			nand->bbt.option |= NANDDEV_BBT_SCANNED;
		for (block = 0; block < nblocks; block++) {
			ret = snanddev_bbt_get_block_status(nand, block);
			if (ret != NAND_BBT_BLOCK_GOOD)
				BBT_DBG("bad block[0x%x], ret=%d\n", block, ret);
		}
		if (!in_scan)
			nand->bbt.option &= ~NANDDEV_BBT_SCANNED;
		memcpy(nand->bbt.cache, temp_buf, nbytes);
		kfree(temp_buf);
		ret = 0;
	}
#endif

out:
	kfree(data_buf);
	kfree(oob_buf);

	return ret < 0 ? -EIO : (int)version;
}

static int nanddev_write_bbt(struct snand_mtd_dev *nand, u32 block)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nblocks = snanddev_neraseblocks(nand);
	unsigned int nbytes = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG) * sizeof(*nand->bbt.cache);
	struct mtd_info *mtd = snanddev_to_mtd(nand);
	u8 *data_buf, *oob_buf;
	struct nanddev_bbt_info *bbt_info;
	struct mtd_oob_ops ops;
	u32 bbt_page_num;
	int ret = 0, version;

	BBT_DBG("write_bbt to blk=%d ver=%d\n", block, nand->bbt.version);
	if (!nand->bbt.cache)
		return -ENOMEM;

	if (block >= nblocks)
		return -EINVAL;

	/* aligned to page size, and even pages is better */
	bbt_page_num = (sizeof(struct nanddev_bbt_info) + nbytes +
		mtd->writesize - 1) >> mtd->writesize_shift;
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
	bbt_info->hash = js_hash(data_buf, nbytes + sizeof(struct nanddev_bbt_info) - 4);

	/* Store one entry for each block */
	ret = sfc_nand_erase_mtd(mtd, block * mtd->erasesize);
	if (ret)
		goto out;

	memset(&ops, 0, sizeof(struct mtd_oob_ops));
	ops.mode = MTD_OPS_PLACE_OOB;
	ops.datbuf = data_buf;
	ops.len = bbt_page_num * mtd->writesize;
	ops.oobbuf = oob_buf;
	ops.ooblen = bbt_page_num * mtd->oobsize;
	ops.ooboffs = 0;
	ret = bbt_mtd_write_oob(mtd, block * mtd->erasesize, &ops);
	if (ret) {
		sfc_nand_erase_mtd(mtd, block * mtd->erasesize);
		goto out;
	}

	version = nanddev_read_bbt(nand, block, false);
	if (version != bbt_info->version) {
		pr_err("bbt_write fail, blk=%d recheck fail %d-%d\n",
		       block, version, bbt_info->version);
		sfc_nand_erase_mtd(mtd, block * mtd->erasesize);
		ret = -EIO;
	} else {
		ret = 0;
	}
out:
	kfree(data_buf);
	kfree(oob_buf);

	return ret;
}

static int nanddev_bbt_format(struct snand_mtd_dev *nand)
{
	unsigned int nblocks = snanddev_neraseblocks(nand);
	struct mtd_info *mtd = snanddev_to_mtd(nand);
	u32 start_block, block;
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nwords = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG);

	start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;

	for (block = 0; block < nblocks; block++) {
		if (sfc_nand_isbad_mtd(mtd, block * mtd->erasesize)) {
			if (bbt_nand_isbad_bypass(nand, 0)) {
				memset(nand->bbt.cache, 0, nwords * sizeof(*nand->bbt.cache));
				pr_err("bbt_format fail, test good block %d fail\n", 0);
				return -EIO;
			}

			if (!bbt_nand_isbad_bypass(nand, block)) {
				memset(nand->bbt.cache, 0, nwords * sizeof(*nand->bbt.cache));
				pr_err("bbt_format fail, test bad block %d fail\n", block);
				return -EIO;
			}

			snanddev_bbt_set_block_status(nand, block,
						     NAND_BBT_BLOCK_FACTORY_BAD);
		}
	}

	for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
		if (snanddev_bbt_get_block_status(nand, start_block + block) ==
			NAND_BBT_BLOCK_GOOD)
			snanddev_bbt_set_block_status(nand, start_block + block,
						      NAND_BBT_BLOCK_WORN);
	}

	return 0;
}

static int nanddev_scan_bbt(struct snand_mtd_dev *nand)
{
	unsigned int nblocks = snanddev_neraseblocks(nand);
	u32 start_block, block;
	int ret = 0;

	nand->bbt.version = 0;
	start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;
	for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++)
		nanddev_read_bbt(nand, start_block + block, true);

	nand->bbt.option |= NANDDEV_BBT_SCANNED;
	if (nand->bbt.version == 0) {
		ret = nanddev_bbt_format(nand);
		if (ret) {
			nand->bbt.option = 0;
			pr_err("%s format fail\n", __func__);

			return ret;
		}

		ret = snanddev_bbt_update(nand);
		if (ret) {
			nand->bbt.option = 0;
			pr_err("%s update fail\n", __func__);

			return ret;
		}
	}

#if defined(BBT_DEBUG)
	pr_err("scan_bbt success\n");
	if (nand->bbt.version) {
		for (block = 0; block < nblocks; block++) {
			ret = snanddev_bbt_get_block_status(nand, block);
			if (ret != NAND_BBT_BLOCK_GOOD)
				BBT_DBG("bad block[0x%x], ret=%d\n", block, ret);
		}
	}
#endif

	return ret;
}

#endif

/**
 * nanddev_bbt_init() - Initialize the BBT (Bad Block Table)
 * @nand: NAND device
 *
 * Initialize the in-memory BBT.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int snanddev_bbt_init(struct snand_mtd_dev *nand)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned int nblocks = snanddev_neraseblocks(nand);
	unsigned int nwords = DIV_ROUND_UP(nblocks * bits_per_block,
					   BITS_PER_LONG);

	nand->bbt.cache = kcalloc(nwords, sizeof(*nand->bbt.cache),
				  GFP_KERNEL);
	if (!nand->bbt.cache)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(snanddev_bbt_init);

/**
 * nanddev_bbt_cleanup() - Cleanup the BBT (Bad Block Table)
 * @nand: NAND device
 *
 * Undoes what has been done in nanddev_bbt_init()
 */
void snanddev_bbt_cleanup(struct snand_mtd_dev *nand)
{
	kfree(nand->bbt.cache);
}
EXPORT_SYMBOL_GPL(snanddev_bbt_cleanup);

/**
 * nanddev_bbt_update() - Update a BBT
 * @nand: nand device
 *
 * Update the BBT. Currently a NOP function since on-flash bbt is not yet
 * supported.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int snanddev_bbt_update(struct snand_mtd_dev *nand)
{
#ifdef CONFIG_MTD_NAND_BBT_USING_FLASH
	struct mtd_info *mtd = snanddev_to_mtd(nand);

	if (nand->bbt.cache &&
	    nand->bbt.option & NANDDEV_BBT_USE_FLASH) {
		unsigned int nblocks = snanddev_neraseblocks(nand);
		u32 bbt_version[NANDDEV_BBT_SCAN_MAXBLOCKS];
		int start_block, block;
		u32 min_version, block_des;
		int ret, count = 0, status;

		start_block = nblocks - NANDDEV_BBT_SCAN_MAXBLOCKS;
		for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
			status = snanddev_bbt_get_block_status(nand, start_block + block);
			ret = nanddev_read_bbt(nand, start_block + block, false);

			if (ret == 0 && status == NAND_BBT_BLOCK_FACTORY_BAD)
				bbt_version[block] = BBT_VERSION_INVALID;
			else if (ret == -EIO)
				bbt_version[block] = BBT_VERSION_INVALID;
			else if (ret == BBT_VERSION_BLOCK_ABNORMAL)
				bbt_version[block] = ret;
			else
				bbt_version[block] = ret;
		}
get_min_ver:
		min_version = BBT_VERSION_MAX;
		block_des = 0;
		for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
			if (bbt_version[block] < min_version) {
				min_version = bbt_version[block];
				block_des = start_block + block;
			}
		}

		/* Overwrite the BBT_VERSION_BLOCK_ABNORMAL block */
		if (nand->bbt.version < min_version)
			nand->bbt.version = min_version + 4;

		if (block_des > 0) {
			nand->bbt.version++;
			ret = nanddev_write_bbt(nand, block_des);
			if (ret) {
				pr_err("bbt_update fail, blk=%d ret= %d\n", block_des, ret);

				return -1;
			}

			bbt_version[block_des - start_block] = BBT_VERSION_INVALID;
			count++;
			if (count < 2)
				goto get_min_ver;
			BBT_DBG("bbt_update success\n");
		} else {
			pr_err("bbt_update failed\n");
			ret = -1;
		}

		for (block = 0; block < NANDDEV_BBT_SCAN_MAXBLOCKS; block++) {
			if (bbt_version[block] == BBT_VERSION_BLOCK_ABNORMAL) {
				block_des = start_block + block;
				sfc_nand_erase_mtd(mtd, block_des * mtd->erasesize);
			}
		}

		return ret;
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(snanddev_bbt_update);

/**
 * nanddev_bbt_get_block_status() - Return the status of an eraseblock
 * @nand: nand device
 * @entry: the BBT entry
 *
 * Return: a positive number nand_bbt_block_status status or -%ERANGE if @entry
 *	   is bigger than the BBT size.
 */
int snanddev_bbt_get_block_status(const struct snand_mtd_dev *nand,
				  unsigned int entry)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned long *pos = nand->bbt.cache +
			     ((entry * bits_per_block) / BITS_PER_LONG);
	unsigned int offs = (entry * bits_per_block) % BITS_PER_LONG;
	unsigned long status;

#ifdef CONFIG_MTD_NAND_BBT_USING_FLASH
	if (nand->bbt.option & NANDDEV_BBT_USE_FLASH &&
	    !(nand->bbt.option & NANDDEV_BBT_SCANNED))
		nanddev_scan_bbt((struct snand_mtd_dev *)nand);
#endif

	if (entry >= snanddev_neraseblocks(nand))
		return -ERANGE;

	status = pos[0] >> offs;
	if (bits_per_block + offs > BITS_PER_LONG)
		status |= pos[1] << (BITS_PER_LONG - offs);

	return status & GENMASK(bits_per_block - 1, 0);
}
EXPORT_SYMBOL_GPL(snanddev_bbt_get_block_status);

/**
 * nanddev_bbt_set_block_status() - Update the status of an eraseblock in the
 *				    in-memory BBT
 * @nand: nand device
 * @entry: the BBT entry to update
 * @status: the new status
 *
 * Update an entry of the in-memory BBT. If you want to push the updated BBT
 * the NAND you should call nanddev_bbt_update().
 *
 * Return: 0 in case of success or -%ERANGE if @entry is bigger than the BBT
 *	   size.
 */
int snanddev_bbt_set_block_status(struct snand_mtd_dev *nand,
				  unsigned int entry,
				  enum nand_bbt_block_status status)
{
	unsigned int bits_per_block = fls(NAND_BBT_BLOCK_NUM_STATUS);
	unsigned long *pos = nand->bbt.cache +
			     ((entry * bits_per_block) / BITS_PER_LONG);
	unsigned int offs = (entry * bits_per_block) % BITS_PER_LONG;
	unsigned long val = status & GENMASK(bits_per_block - 1, 0);

	if (entry >= snanddev_neraseblocks(nand))
		return -ERANGE;

	if (offs + bits_per_block - 1 > (BITS_PER_LONG - 1))
		pos[0] &= ~GENMASK(BITS_PER_LONG - 1, offs);
	else
		pos[0] &= ~GENMASK(offs + bits_per_block - 1, offs);
	pos[0] |= val << offs;

	if (bits_per_block + offs > BITS_PER_LONG) {
		unsigned int rbits = BITS_PER_LONG - offs;

		pos[1] &= ~GENMASK(bits_per_block - rbits - 1, 0);
		pos[1] |= val >> rbits;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snanddev_bbt_set_block_status);
