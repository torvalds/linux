#define pr_fmt(fmt) "mtd_test: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/printk.h>

#include "mtd_test.h"

int mtdtest_erase_eraseblock(struct mtd_info *mtd, unsigned int ebnum)
{
	int err;
	struct erase_info ei;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	memset(&ei, 0, sizeof(struct erase_info));
	ei.mtd  = mtd;
	ei.addr = addr;
	ei.len  = mtd->erasesize;

	err = mtd_erase(mtd, &ei);
	if (err) {
		pr_info("error %d while erasing EB %d\n", err, ebnum);
		return err;
	}

	if (ei.state == MTD_ERASE_FAILED) {
		pr_info("some erase error occurred at EB %d\n", ebnum);
		return -EIO;
	}
	return 0;
}

static int is_block_bad(struct mtd_info *mtd, unsigned int ebnum)
{
	int ret;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	ret = mtd_block_isbad(mtd, addr);
	if (ret)
		pr_info("block %d is bad\n", ebnum);

	return ret;
}

int mtdtest_scan_for_bad_eraseblocks(struct mtd_info *mtd, unsigned char *bbt,
					unsigned int eb, int ebcnt)
{
	int i, bad = 0;

	if (!mtd_can_have_bb(mtd))
		return 0;

	pr_info("scanning for bad eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		bbt[i] = is_block_bad(mtd, eb + i) ? 1 : 0;
		if (bbt[i])
			bad += 1;
		cond_resched();
	}
	pr_info("scanned %d eraseblocks, %d are bad\n", i, bad);

	return 0;
}

int mtdtest_erase_good_eraseblocks(struct mtd_info *mtd, unsigned char *bbt,
				unsigned int eb, int ebcnt)
{
	int err;
	unsigned int i;

	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = mtdtest_erase_eraseblock(mtd, eb + i);
		if (err)
			return err;
		cond_resched();
	}

	return 0;
}

int mtdtest_read(struct mtd_info *mtd, loff_t addr, size_t size, void *buf)
{
	size_t read;
	int err;

	err = mtd_read(mtd, addr, size, &read, buf);
	/* Ignore corrected ECC errors */
	if (mtd_is_bitflip(err))
		err = 0;
	if (!err && read != size)
		err = -EIO;
	if (err)
		pr_err("error: read failed at %#llx\n", addr);

	return err;
}

int mtdtest_write(struct mtd_info *mtd, loff_t addr, size_t size,
		const void *buf)
{
	size_t written;
	int err;

	err = mtd_write(mtd, addr, size, &written, buf);
	if (!err && written != size)
		err = -EIO;
	if (err)
		pr_err("error: write failed at %#llx\n", addr);

	return err;
}
