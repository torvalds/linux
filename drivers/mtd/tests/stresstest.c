// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Test random reads, writes and erases on MTD device.
 *
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/random.h>

#include "mtd_test.h"

static int dev = -EINVAL;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static int count = 10000;
module_param(count, int, S_IRUGO);
MODULE_PARM_DESC(count, "Number of operations to do (default is 10000)");

static struct mtd_info *mtd;
static unsigned char *writebuf;
static unsigned char *readbuf;
static unsigned char *bbt;
static int *offsets;

static int pgsize;
static int bufsize;
static int ebcnt;
static int pgcnt;

static int rand_eb(void)
{
	unsigned int eb;

again:
	/* Read or write up 2 eraseblocks at a time - hence 'ebcnt - 1' */
	eb = prandom_u32_max(ebcnt - 1);
	if (bbt[eb])
		goto again;
	return eb;
}

static int rand_offs(void)
{
	return prandom_u32_max(bufsize);
}

static int rand_len(int offs)
{
	return prandom_u32_max(bufsize - offs);
}

static int do_read(void)
{
	int eb = rand_eb();
	int offs = rand_offs();
	int len = rand_len(offs);
	loff_t addr;

	if (bbt[eb + 1]) {
		if (offs >= mtd->erasesize)
			offs -= mtd->erasesize;
		if (offs + len > mtd->erasesize)
			len = mtd->erasesize - offs;
	}
	addr = (loff_t)eb * mtd->erasesize + offs;
	return mtdtest_read(mtd, addr, len, readbuf);
}

static int do_write(void)
{
	int eb = rand_eb(), offs, err, len;
	loff_t addr;

	offs = offsets[eb];
	if (offs >= mtd->erasesize) {
		err = mtdtest_erase_eraseblock(mtd, eb);
		if (err)
			return err;
		offs = offsets[eb] = 0;
	}
	len = rand_len(offs);
	len = ((len + pgsize - 1) / pgsize) * pgsize;
	if (offs + len > mtd->erasesize) {
		if (bbt[eb + 1])
			len = mtd->erasesize - offs;
		else {
			err = mtdtest_erase_eraseblock(mtd, eb + 1);
			if (err)
				return err;
			offsets[eb + 1] = 0;
		}
	}
	addr = (loff_t)eb * mtd->erasesize + offs;
	err = mtdtest_write(mtd, addr, len, writebuf);
	if (unlikely(err))
		return err;
	offs += len;
	while (offs > mtd->erasesize) {
		offsets[eb++] = mtd->erasesize;
		offs -= mtd->erasesize;
	}
	offsets[eb] = offs;
	return 0;
}

static int do_operation(void)
{
	if (prandom_u32_max(2))
		return do_read();
	else
		return do_write();
}

static int __init mtd_stresstest_init(void)
{
	int err;
	int i, op;
	uint64_t tmp;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");

	if (dev < 0) {
		pr_info("Please specify a valid mtd-device via module parameter\n");
		pr_crit("CAREFUL: This test wipes all data on the specified MTD device!\n");
		return -EINVAL;
	}

	pr_info("MTD device: %d\n", dev);

	mtd = get_mtd_device(NULL, dev);
	if (IS_ERR(mtd)) {
		err = PTR_ERR(mtd);
		pr_err("error: cannot get MTD device\n");
		return err;
	}

	if (mtd->writesize == 1) {
		pr_info("not NAND flash, assume page size is 512 "
		       "bytes.\n");
		pgsize = 512;
	} else
		pgsize = mtd->writesize;

	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / pgsize;

	pr_info("MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       pgsize, ebcnt, pgcnt, mtd->oobsize);

	if (ebcnt < 2) {
		pr_err("error: need at least 2 eraseblocks\n");
		err = -ENOSPC;
		goto out_put_mtd;
	}

	/* Read or write up 2 eraseblocks at a time */
	bufsize = mtd->erasesize * 2;

	err = -ENOMEM;
	readbuf = vmalloc(bufsize);
	writebuf = vmalloc(bufsize);
	offsets = kmalloc_array(ebcnt, sizeof(int), GFP_KERNEL);
	if (!readbuf || !writebuf || !offsets)
		goto out;
	for (i = 0; i < ebcnt; i++)
		offsets[i] = mtd->erasesize;
	prandom_bytes(writebuf, bufsize);

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt)
		goto out;
	err = mtdtest_scan_for_bad_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Do operations */
	pr_info("doing operations\n");
	for (op = 0; op < count; op++) {
		if ((op & 1023) == 0)
			pr_info("%d operations done\n", op);
		err = do_operation();
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	pr_info("finished, %d operations done\n", op);

out:
	kfree(offsets);
	kfree(bbt);
	vfree(writebuf);
	vfree(readbuf);
out_put_mtd:
	put_mtd_device(mtd);
	if (err)
		pr_info("error %d occurred\n", err);
	printk(KERN_INFO "=================================================\n");
	return err;
}
module_init(mtd_stresstest_init);

static void __exit mtd_stresstest_exit(void)
{
	return;
}
module_exit(mtd_stresstest_exit);

MODULE_DESCRIPTION("Stress test module");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL");
