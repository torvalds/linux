// SPDX-License-Identifier: GPL-2.0

#define dev_fmt(fmt) "mtdoops-pstore: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pstore_blk.h>
#include <linux/mtd/mtd.h>
#include <linux/bitops.h>

static struct mtdpstore_context {
	int index;
	struct pstore_blk_config info;
	struct pstore_device_info dev;
	struct mtd_info *mtd;
	unsigned long *rmmap;		/* removed bit map */
	unsigned long *usedmap;		/* used bit map */
	/*
	 * used for panic write
	 * As there are no block_isbad for panic case, we should keep this
	 * status before panic to ensure panic_write not failed.
	 */
	unsigned long *badmap;		/* bad block bit map */
} oops_cxt;

static int mtdpstore_block_isbad(struct mtdpstore_context *cxt, loff_t off)
{
	int ret;
	struct mtd_info *mtd = cxt->mtd;
	u64 blknum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	blknum = div_u64(off, mtd->erasesize);

	if (test_bit(blknum, cxt->badmap))
		return true;
	ret = mtd_block_isbad(mtd, off);
	if (ret < 0) {
		dev_err(&mtd->dev, "mtd_block_isbad failed, aborting\n");
		return ret;
	} else if (ret > 0) {
		set_bit(blknum, cxt->badmap);
		return true;
	}
	return false;
}

static inline int mtdpstore_panic_block_isbad(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u64 blknum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	blknum = div_u64(off, mtd->erasesize);
	return test_bit(blknum, cxt->badmap);
}

static inline void mtdpstore_mark_used(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u64 zonenum = div_u64(off, cxt->info.kmsg_size);

	dev_dbg(&mtd->dev, "mark zone %llu used\n", zonenum);
	set_bit(zonenum, cxt->usedmap);
}

static inline void mtdpstore_mark_unused(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u64 zonenum = div_u64(off, cxt->info.kmsg_size);

	dev_dbg(&mtd->dev, "mark zone %llu unused\n", zonenum);
	clear_bit(zonenum, cxt->usedmap);
}

static inline void mtdpstore_block_mark_unused(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u32 zonecnt = mtd->erasesize / cxt->info.kmsg_size;
	u64 zonenum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	zonenum = div_u64(off, cxt->info.kmsg_size);
	while (zonecnt > 0) {
		dev_dbg(&mtd->dev, "mark zone %llu unused\n", zonenum);
		clear_bit(zonenum, cxt->usedmap);
		zonenum++;
		zonecnt--;
	}
}

static inline int mtdpstore_is_used(struct mtdpstore_context *cxt, loff_t off)
{
	u64 zonenum = div_u64(off, cxt->info.kmsg_size);
	u64 blknum = div_u64(off, cxt->mtd->erasesize);

	if (test_bit(blknum, cxt->badmap))
		return true;
	return test_bit(zonenum, cxt->usedmap);
}

static int mtdpstore_block_is_used(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u32 zonecnt = mtd->erasesize / cxt->info.kmsg_size;
	u64 zonenum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	zonenum = div_u64(off, cxt->info.kmsg_size);
	while (zonecnt > 0) {
		if (test_bit(zonenum, cxt->usedmap))
			return true;
		zonenum++;
		zonecnt--;
	}
	return false;
}

static int mtdpstore_is_empty(struct mtdpstore_context *cxt, char *buf,
		size_t size)
{
	struct mtd_info *mtd = cxt->mtd;
	size_t sz;
	int i;

	sz = min_t(uint32_t, size, mtd->writesize / 4);
	for (i = 0; i < sz; i++) {
		if (buf[i] != (char)0xFF)
			return false;
	}
	return true;
}

static void mtdpstore_mark_removed(struct mtdpstore_context *cxt, loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u64 zonenum = div_u64(off, cxt->info.kmsg_size);

	dev_dbg(&mtd->dev, "mark zone %llu removed\n", zonenum);
	set_bit(zonenum, cxt->rmmap);
}

static void mtdpstore_block_clear_removed(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u32 zonecnt = mtd->erasesize / cxt->info.kmsg_size;
	u64 zonenum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	zonenum = div_u64(off, cxt->info.kmsg_size);
	while (zonecnt > 0) {
		clear_bit(zonenum, cxt->rmmap);
		zonenum++;
		zonecnt--;
	}
}

static int mtdpstore_block_is_removed(struct mtdpstore_context *cxt,
		loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	u32 zonecnt = mtd->erasesize / cxt->info.kmsg_size;
	u64 zonenum;

	off = ALIGN_DOWN(off, mtd->erasesize);
	zonenum = div_u64(off, cxt->info.kmsg_size);
	while (zonecnt > 0) {
		if (test_bit(zonenum, cxt->rmmap))
			return true;
		zonenum++;
		zonecnt--;
	}
	return false;
}

static int mtdpstore_erase_do(struct mtdpstore_context *cxt, loff_t off)
{
	struct mtd_info *mtd = cxt->mtd;
	struct erase_info erase;
	int ret;

	off = ALIGN_DOWN(off, cxt->mtd->erasesize);
	dev_dbg(&mtd->dev, "try to erase off 0x%llx\n", off);
	erase.len = cxt->mtd->erasesize;
	erase.addr = off;
	ret = mtd_erase(cxt->mtd, &erase);
	if (!ret)
		mtdpstore_block_clear_removed(cxt, off);
	else
		dev_err(&mtd->dev, "erase of region [0x%llx, 0x%llx] on \"%s\" failed\n",
		       (unsigned long long)erase.addr,
		       (unsigned long long)erase.len, cxt->info.device);
	return ret;
}

/*
 * called while removing file
 *
 * Avoiding over erasing, do erase block only when the whole block is unused.
 * If the block contains valid log, do erase lazily on flush_removed() when
 * unregister.
 */
static ssize_t mtdpstore_erase(size_t size, loff_t off)
{
	struct mtdpstore_context *cxt = &oops_cxt;

	if (mtdpstore_block_isbad(cxt, off))
		return -EIO;

	mtdpstore_mark_unused(cxt, off);

	/* If the block still has valid data, mtdpstore do erase lazily */
	if (likely(mtdpstore_block_is_used(cxt, off))) {
		mtdpstore_mark_removed(cxt, off);
		return 0;
	}

	/* all zones are unused, erase it */
	return mtdpstore_erase_do(cxt, off);
}

/*
 * What is security for mtdpstore?
 * As there is no erase for panic case, we should ensure at least one zone
 * is writable. Otherwise, panic write will fail.
 * If zone is used, write operation will return -ENOMSG, which means that
 * pstore/blk will try one by one until gets an empty zone. So, it is not
 * needed to ensure the next zone is empty, but at least one.
 */
static int mtdpstore_security(struct mtdpstore_context *cxt, loff_t off)
{
	int ret = 0, i;
	struct mtd_info *mtd = cxt->mtd;
	u32 zonenum = (u32)div_u64(off, cxt->info.kmsg_size);
	u32 zonecnt = (u32)div_u64(cxt->mtd->size, cxt->info.kmsg_size);
	u32 blkcnt = (u32)div_u64(cxt->mtd->size, cxt->mtd->erasesize);
	u32 erasesize = cxt->mtd->erasesize;

	for (i = 0; i < zonecnt; i++) {
		u32 num = (zonenum + i) % zonecnt;

		/* found empty zone */
		if (!test_bit(num, cxt->usedmap))
			return 0;
	}

	/* If there is no any empty zone, we have no way but to do erase */
	while (blkcnt--) {
		div64_u64_rem(off + erasesize, cxt->mtd->size, (u64 *)&off);

		if (mtdpstore_block_isbad(cxt, off))
			continue;

		ret = mtdpstore_erase_do(cxt, off);
		if (!ret) {
			mtdpstore_block_mark_unused(cxt, off);
			break;
		}
	}

	if (ret)
		dev_err(&mtd->dev, "all blocks bad!\n");
	dev_dbg(&mtd->dev, "end security\n");
	return ret;
}

static ssize_t mtdpstore_write(const char *buf, size_t size, loff_t off)
{
	struct mtdpstore_context *cxt = &oops_cxt;
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen;
	int ret;

	if (mtdpstore_block_isbad(cxt, off))
		return -ENOMSG;

	/* zone is used, please try next one */
	if (mtdpstore_is_used(cxt, off))
		return -ENOMSG;

	dev_dbg(&mtd->dev, "try to write off 0x%llx size %zu\n", off, size);
	ret = mtd_write(cxt->mtd, off, size, &retlen, (u_char *)buf);
	if (ret < 0 || retlen != size) {
		dev_err(&mtd->dev, "write failure at %lld (%zu of %zu written), err %d\n",
				off, retlen, size, ret);
		return -EIO;
	}
	mtdpstore_mark_used(cxt, off);

	mtdpstore_security(cxt, off);
	return retlen;
}

static inline bool mtdpstore_is_io_error(int ret)
{
	return ret < 0 && !mtd_is_bitflip(ret) && !mtd_is_eccerr(ret);
}

/*
 * All zones will be read as pstore/blk will read zone one by one when do
 * recover.
 */
static ssize_t mtdpstore_read(char *buf, size_t size, loff_t off)
{
	struct mtdpstore_context *cxt = &oops_cxt;
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen, done;
	int ret;

	if (mtdpstore_block_isbad(cxt, off))
		return -ENOMSG;

	dev_dbg(&mtd->dev, "try to read off 0x%llx size %zu\n", off, size);
	for (done = 0, retlen = 0; done < size; done += retlen) {
		retlen = 0;

		ret = mtd_read(cxt->mtd, off + done, size - done, &retlen,
				(u_char *)buf + done);
		if (mtdpstore_is_io_error(ret)) {
			dev_err(&mtd->dev, "read failure at %lld (%zu of %zu read), err %d\n",
					off + done, retlen, size - done, ret);
			/* the zone may be broken, try next one */
			return -ENOMSG;
		}

		/*
		 * ECC error. The impact on log data is so small. Maybe we can
		 * still read it and try to understand. So mtdpstore just hands
		 * over what it gets and user can judge whether the data is
		 * valid or not.
		 */
		if (mtd_is_eccerr(ret)) {
			dev_err(&mtd->dev, "ecc error at %lld (%zu of %zu read), err %d\n",
					off + done, retlen, size - done, ret);
			/* driver may not set retlen when ecc error */
			retlen = retlen == 0 ? size - done : retlen;
		}
	}

	if (mtdpstore_is_empty(cxt, buf, size))
		mtdpstore_mark_unused(cxt, off);
	else
		mtdpstore_mark_used(cxt, off);

	mtdpstore_security(cxt, off);
	return retlen;
}

static ssize_t mtdpstore_panic_write(const char *buf, size_t size, loff_t off)
{
	struct mtdpstore_context *cxt = &oops_cxt;
	struct mtd_info *mtd = cxt->mtd;
	size_t retlen;
	int ret;

	if (mtdpstore_panic_block_isbad(cxt, off))
		return -ENOMSG;

	/* zone is used, please try next one */
	if (mtdpstore_is_used(cxt, off))
		return -ENOMSG;

	ret = mtd_panic_write(cxt->mtd, off, size, &retlen, (u_char *)buf);
	if (ret < 0 || size != retlen) {
		dev_err(&mtd->dev, "panic write failure at %lld (%zu of %zu read), err %d\n",
				off, retlen, size, ret);
		return -EIO;
	}
	mtdpstore_mark_used(cxt, off);

	return retlen;
}

static void mtdpstore_notify_add(struct mtd_info *mtd)
{
	int ret;
	struct mtdpstore_context *cxt = &oops_cxt;
	struct pstore_blk_config *info = &cxt->info;
	unsigned long longcnt;

	if (!strcmp(mtd->name, info->device))
		cxt->index = mtd->index;

	if (mtd->index != cxt->index || cxt->index < 0)
		return;

	dev_dbg(&mtd->dev, "found matching MTD device %s\n", mtd->name);

	if (mtd->size < info->kmsg_size * 2) {
		dev_err(&mtd->dev, "MTD partition %d not big enough\n",
				mtd->index);
		return;
	}
	/*
	 * kmsg_size must be aligned to 4096 Bytes, which is limited by
	 * psblk. The default value of kmsg_size is 64KB. If kmsg_size
	 * is larger than erasesize, some errors will occur since mtdpsotre
	 * is designed on it.
	 */
	if (mtd->erasesize < info->kmsg_size) {
		dev_err(&mtd->dev, "eraseblock size of MTD partition %d too small\n",
				mtd->index);
		return;
	}
	if (unlikely(info->kmsg_size % mtd->writesize)) {
		dev_err(&mtd->dev, "record size %lu KB must align to write size %d KB\n",
				info->kmsg_size / 1024,
				mtd->writesize / 1024);
		return;
	}

	longcnt = BITS_TO_LONGS(div_u64(mtd->size, info->kmsg_size));
	cxt->rmmap = kcalloc(longcnt, sizeof(long), GFP_KERNEL);
	cxt->usedmap = kcalloc(longcnt, sizeof(long), GFP_KERNEL);

	longcnt = BITS_TO_LONGS(div_u64(mtd->size, mtd->erasesize));
	cxt->badmap = kcalloc(longcnt, sizeof(long), GFP_KERNEL);

	cxt->dev.total_size = mtd->size;
	/* just support dmesg right now */
	cxt->dev.flags = PSTORE_FLAGS_DMESG;
	cxt->dev.read = mtdpstore_read;
	cxt->dev.write = mtdpstore_write;
	cxt->dev.erase = mtdpstore_erase;
	cxt->dev.panic_write = mtdpstore_panic_write;

	ret = register_pstore_device(&cxt->dev);
	if (ret) {
		dev_err(&mtd->dev, "mtd%d register to psblk failed\n",
				mtd->index);
		return;
	}
	cxt->mtd = mtd;
	dev_info(&mtd->dev, "Attached to MTD device %d\n", mtd->index);
}

static int mtdpstore_flush_removed_do(struct mtdpstore_context *cxt,
		loff_t off, size_t size)
{
	struct mtd_info *mtd = cxt->mtd;
	u_char *buf;
	int ret;
	size_t retlen;
	struct erase_info erase;

	buf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* 1st. read to cache */
	ret = mtd_read(mtd, off, mtd->erasesize, &retlen, buf);
	if (mtdpstore_is_io_error(ret))
		goto free;

	/* 2nd. erase block */
	erase.len = mtd->erasesize;
	erase.addr = off;
	ret = mtd_erase(mtd, &erase);
	if (ret)
		goto free;

	/* 3rd. write back */
	while (size) {
		unsigned int zonesize = cxt->info.kmsg_size;

		/* there is valid data on block, write back */
		if (mtdpstore_is_used(cxt, off)) {
			ret = mtd_write(mtd, off, zonesize, &retlen, buf);
			if (ret)
				dev_err(&mtd->dev, "write failure at %lld (%zu of %u written), err %d\n",
						off, retlen, zonesize, ret);
		}

		off += zonesize;
		size -= min_t(unsigned int, zonesize, size);
	}

free:
	kfree(buf);
	return ret;
}

/*
 * What does mtdpstore_flush_removed() do?
 * When user remove any log file on pstore filesystem, mtdpstore should do
 * something to ensure log file removed. If the whole block is no longer used,
 * it's nice to erase the block. However if the block still contains valid log,
 * what mtdpstore can do is to erase and write the valid log back.
 */
static int mtdpstore_flush_removed(struct mtdpstore_context *cxt)
{
	struct mtd_info *mtd = cxt->mtd;
	int ret;
	loff_t off;
	u32 blkcnt = (u32)div_u64(mtd->size, mtd->erasesize);

	for (off = 0; blkcnt > 0; blkcnt--, off += mtd->erasesize) {
		ret = mtdpstore_block_isbad(cxt, off);
		if (ret)
			continue;

		ret = mtdpstore_block_is_removed(cxt, off);
		if (!ret)
			continue;

		ret = mtdpstore_flush_removed_do(cxt, off, mtd->erasesize);
		if (ret)
			return ret;
	}
	return 0;
}

static void mtdpstore_notify_remove(struct mtd_info *mtd)
{
	struct mtdpstore_context *cxt = &oops_cxt;

	if (mtd->index != cxt->index || cxt->index < 0)
		return;

	mtdpstore_flush_removed(cxt);

	unregister_pstore_device(&cxt->dev);
	kfree(cxt->badmap);
	kfree(cxt->usedmap);
	kfree(cxt->rmmap);
	cxt->mtd = NULL;
	cxt->index = -1;
}

static struct mtd_notifier mtdpstore_notifier = {
	.add	= mtdpstore_notify_add,
	.remove	= mtdpstore_notify_remove,
};

static int __init mtdpstore_init(void)
{
	int ret;
	struct mtdpstore_context *cxt = &oops_cxt;
	struct pstore_blk_config *info = &cxt->info;

	ret = pstore_blk_get_config(info);
	if (unlikely(ret))
		return ret;

	if (strlen(info->device) == 0) {
		pr_err("mtd device must be supplied (device name is empty)\n");
		return -EINVAL;
	}
	if (!info->kmsg_size) {
		pr_err("no backend enabled (kmsg_size is 0)\n");
		return -EINVAL;
	}

	/* Setup the MTD device to use */
	ret = kstrtoint((char *)info->device, 0, &cxt->index);
	if (ret)
		cxt->index = -1;

	register_mtd_user(&mtdpstore_notifier);
	return 0;
}
module_init(mtdpstore_init);

static void __exit mtdpstore_exit(void)
{
	unregister_mtd_user(&mtdpstore_notifier);
}
module_exit(mtdpstore_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WeiXiong Liao <liaoweixiong@allwinnertech.com>");
MODULE_DESCRIPTION("MTD backend for pstore/blk");
