// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Nokia Corporation
 *
 * Test read and write speed of a MTD device.
 *
 * Author: Adrian Hunter <adrian.hunter@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/random.h>

#include "mtd_test.h"

static int dev = -EINVAL;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static int count;
module_param(count, int, S_IRUGO);
MODULE_PARM_DESC(count, "Maximum number of eraseblocks to use "
			"(0 means use all)");

static struct mtd_info *mtd;
static unsigned char *iobuf;
static unsigned char *bbt;

static int pgsize;
static int ebcnt;
static int pgcnt;
static int goodebcnt;
static ktime_t start, finish;

static int multiblock_erase(int ebnum, int blocks)
{
	int err;
	struct erase_info ei;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	memset(&ei, 0, sizeof(struct erase_info));
	ei.addr = addr;
	ei.len  = mtd->erasesize * blocks;

	err = mtd_erase(mtd, &ei);
	if (err) {
		pr_err("error %d while erasing EB %d, blocks %d\n",
		       err, ebnum, blocks);
		return err;
	}

	return 0;
}

static int write_eraseblock(int ebnum)
{
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	return mtdtest_write(mtd, addr, mtd->erasesize, iobuf);
}

static int write_eraseblock_by_page(int ebnum)
{
	int i, err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtdtest_write(mtd, addr, pgsize, buf);
		if (err)
			break;
		addr += pgsize;
		buf += pgsize;
	}

	return err;
}

static int write_eraseblock_by_2pages(int ebnum)
{
	size_t sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtdtest_write(mtd, addr, sz, buf);
		if (err)
			return err;
		addr += sz;
		buf += sz;
	}
	if (pgcnt % 2)
		err = mtdtest_write(mtd, addr, pgsize, buf);

	return err;
}

static int read_eraseblock(int ebnum)
{
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	return mtdtest_read(mtd, addr, mtd->erasesize, iobuf);
}

static int read_eraseblock_by_page(int ebnum)
{
	int i, err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtdtest_read(mtd, addr, pgsize, buf);
		if (err)
			break;
		addr += pgsize;
		buf += pgsize;
	}

	return err;
}

static int read_eraseblock_by_2pages(int ebnum)
{
	size_t sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtdtest_read(mtd, addr, sz, buf);
		if (err)
			return err;
		addr += sz;
		buf += sz;
	}
	if (pgcnt % 2)
		err = mtdtest_read(mtd, addr, pgsize, buf);

	return err;
}

static inline void start_timing(void)
{
	start = ktime_get();
}

static inline void stop_timing(void)
{
	finish = ktime_get();
}

static long calc_speed(void)
{
	uint64_t k, us;

	us = ktime_us_delta(finish, start);
	if (us == 0)
		return 0;
	k = (uint64_t)goodebcnt * (mtd->erasesize / 1024) * 1000000;
	do_div(k, us);
	return k;
}

static int __init mtd_speedtest_init(void)
{
	int err, i, blocks, j, k;
	long speed;
	uint64_t tmp;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");

	if (dev < 0) {
		pr_info("Please specify a valid mtd-device via module parameter\n");
		pr_crit("CAREFUL: This test wipes all data on the specified MTD device!\n");
		return -EINVAL;
	}

	if (count)
		pr_info("MTD device: %d    count: %d\n", dev, count);
	else
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

	if (count > 0 && count < ebcnt)
		ebcnt = count;

	err = -ENOMEM;
	iobuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!iobuf)
		goto out;

	prandom_bytes(iobuf, mtd->erasesize);

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt)
		goto out;
	err = mtdtest_scan_for_bad_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;
	for (i = 0; i < ebcnt; i++) {
		if (!bbt[i])
			goodebcnt++;
	}

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Write all eraseblocks, 1 eraseblock at a time */
	pr_info("testing eraseblock write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("eraseblock write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 1 eraseblock at a time */
	pr_info("testing eraseblock read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("eraseblock read speed is %ld KiB/s\n", speed);

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Write all eraseblocks, 1 page at a time */
	pr_info("testing page write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock_by_page(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("page write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 1 page at a time */
	pr_info("testing page read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock_by_page(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("page read speed is %ld KiB/s\n", speed);

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Write all eraseblocks, 2 pages at a time */
	pr_info("testing 2 page write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock_by_2pages(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("2 page write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 2 pages at a time */
	pr_info("testing 2 page read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock_by_2pages(i);
		if (err)
			goto out;

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	stop_timing();
	speed = calc_speed();
	pr_info("2 page read speed is %ld KiB/s\n", speed);

	/* Erase all eraseblocks */
	pr_info("Testing erase speed\n");
	start_timing();
	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;
	stop_timing();
	speed = calc_speed();
	pr_info("erase speed is %ld KiB/s\n", speed);

	/* Multi-block erase all eraseblocks */
	for (k = 1; k < 7; k++) {
		blocks = 1 << k;
		pr_info("Testing %dx multi-block erase speed\n",
		       blocks);
		start_timing();
		for (i = 0; i < ebcnt; ) {
			for (j = 0; j < blocks && (i + j) < ebcnt; j++)
				if (bbt[i + j])
					break;
			if (j < 1) {
				i++;
				continue;
			}
			err = multiblock_erase(i, j);
			if (err)
				goto out;

			err = mtdtest_relax();
			if (err)
				goto out;

			i += j;
		}
		stop_timing();
		speed = calc_speed();
		pr_info("%dx multi-block erase speed is %ld KiB/s\n",
		       blocks, speed);
	}
	pr_info("finished\n");
out:
	kfree(iobuf);
	kfree(bbt);
	put_mtd_device(mtd);
	if (err)
		pr_info("error %d occurred\n", err);
	printk(KERN_INFO "=================================================\n");
	return err;
}
module_init(mtd_speedtest_init);

static void __exit mtd_speedtest_exit(void)
{
	return;
}
module_exit(mtd_speedtest_exit);

MODULE_DESCRIPTION("Speed test module");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL");
