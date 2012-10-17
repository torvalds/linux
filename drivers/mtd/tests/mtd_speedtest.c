/*
 * Copyright (C) 2007 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING. If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Test read and write speed of a MTD device.
 *
 * Author: Adrian Hunter <adrian.hunter@nokia.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/random.h>

#define PRINT_PREF KERN_INFO "mtd_speedtest: "

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
static struct timeval start, finish;

static void set_random_data(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i)
		buf[i] = random32();
}

static int erase_eraseblock(int ebnum)
{
	int err;
	struct erase_info ei;
	loff_t addr = ebnum * mtd->erasesize;

	memset(&ei, 0, sizeof(struct erase_info));
	ei.mtd  = mtd;
	ei.addr = addr;
	ei.len  = mtd->erasesize;

	err = mtd_erase(mtd, &ei);
	if (err) {
		printk(PRINT_PREF "error %d while erasing EB %d\n", err, ebnum);
		return err;
	}

	if (ei.state == MTD_ERASE_FAILED) {
		printk(PRINT_PREF "some erase error occurred at EB %d\n",
		       ebnum);
		return -EIO;
	}

	return 0;
}

static int multiblock_erase(int ebnum, int blocks)
{
	int err;
	struct erase_info ei;
	loff_t addr = ebnum * mtd->erasesize;

	memset(&ei, 0, sizeof(struct erase_info));
	ei.mtd  = mtd;
	ei.addr = addr;
	ei.len  = mtd->erasesize * blocks;

	err = mtd_erase(mtd, &ei);
	if (err) {
		printk(PRINT_PREF "error %d while erasing EB %d, blocks %d\n",
		       err, ebnum, blocks);
		return err;
	}

	if (ei.state == MTD_ERASE_FAILED) {
		printk(PRINT_PREF "some erase error occurred at EB %d,"
		       "blocks %d\n", ebnum, blocks);
		return -EIO;
	}

	return 0;
}

static int erase_whole_device(void)
{
	int err;
	unsigned int i;

	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = erase_eraseblock(i);
		if (err)
			return err;
		cond_resched();
	}
	return 0;
}

static int write_eraseblock(int ebnum)
{
	size_t written;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	err = mtd_write(mtd, addr, mtd->erasesize, &written, iobuf);
	if (err || written != mtd->erasesize) {
		printk(PRINT_PREF "error: write failed at %#llx\n", addr);
		if (!err)
			err = -EINVAL;
	}

	return err;
}

static int write_eraseblock_by_page(int ebnum)
{
	size_t written;
	int i, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtd_write(mtd, addr, pgsize, &written, buf);
		if (err || written != pgsize) {
			printk(PRINT_PREF "error: write failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
			break;
		}
		addr += pgsize;
		buf += pgsize;
	}

	return err;
}

static int write_eraseblock_by_2pages(int ebnum)
{
	size_t written, sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtd_write(mtd, addr, sz, &written, buf);
		if (err || written != sz) {
			printk(PRINT_PREF "error: write failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
			return err;
		}
		addr += sz;
		buf += sz;
	}
	if (pgcnt % 2) {
		err = mtd_write(mtd, addr, pgsize, &written, buf);
		if (err || written != pgsize) {
			printk(PRINT_PREF "error: write failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
		}
	}

	return err;
}

static int read_eraseblock(int ebnum)
{
	size_t read;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	err = mtd_read(mtd, addr, mtd->erasesize, &read, iobuf);
	/* Ignore corrected ECC errors */
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != mtd->erasesize) {
		printk(PRINT_PREF "error: read failed at %#llx\n", addr);
		if (!err)
			err = -EINVAL;
	}

	return err;
}

static int read_eraseblock_by_page(int ebnum)
{
	size_t read;
	int i, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtd_read(mtd, addr, pgsize, &read, buf);
		/* Ignore corrected ECC errors */
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != pgsize) {
			printk(PRINT_PREF "error: read failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
			break;
		}
		addr += pgsize;
		buf += pgsize;
	}

	return err;
}

static int read_eraseblock_by_2pages(int ebnum)
{
	size_t read, sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtd_read(mtd, addr, sz, &read, buf);
		/* Ignore corrected ECC errors */
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != sz) {
			printk(PRINT_PREF "error: read failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
			return err;
		}
		addr += sz;
		buf += sz;
	}
	if (pgcnt % 2) {
		err = mtd_read(mtd, addr, pgsize, &read, buf);
		/* Ignore corrected ECC errors */
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != pgsize) {
			printk(PRINT_PREF "error: read failed at %#llx\n",
			       addr);
			if (!err)
				err = -EINVAL;
		}
	}

	return err;
}

static int is_block_bad(int ebnum)
{
	loff_t addr = ebnum * mtd->erasesize;
	int ret;

	ret = mtd_block_isbad(mtd, addr);
	if (ret)
		printk(PRINT_PREF "block %d is bad\n", ebnum);
	return ret;
}

static inline void start_timing(void)
{
	do_gettimeofday(&start);
}

static inline void stop_timing(void)
{
	do_gettimeofday(&finish);
}

static long calc_speed(void)
{
	uint64_t k;
	long ms;

	ms = (finish.tv_sec - start.tv_sec) * 1000 +
	     (finish.tv_usec - start.tv_usec) / 1000;
	if (ms == 0)
		return 0;
	k = goodebcnt * (mtd->erasesize / 1024) * 1000;
	do_div(k, ms);
	return k;
}

static int scan_for_bad_eraseblocks(void)
{
	int i, bad = 0;

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		return -ENOMEM;
	}

	if (!mtd_can_have_bb(mtd))
		goto out;

	printk(PRINT_PREF "scanning for bad eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		bbt[i] = is_block_bad(i) ? 1 : 0;
		if (bbt[i])
			bad += 1;
		cond_resched();
	}
	printk(PRINT_PREF "scanned %d eraseblocks, %d are bad\n", i, bad);
out:
	goodebcnt = ebcnt - bad;
	return 0;
}

static int __init mtd_speedtest_init(void)
{
	int err, i, blocks, j, k;
	long speed;
	uint64_t tmp;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");

	if (dev < 0) {
		printk(PRINT_PREF "Please specify a valid mtd-device via module paramter\n");
		printk(KERN_CRIT "CAREFUL: This test wipes all data on the specified MTD device!\n");
		return -EINVAL;
	}

	if (count)
		printk(PRINT_PREF "MTD device: %d    count: %d\n", dev, count);
	else
		printk(PRINT_PREF "MTD device: %d\n", dev);

	mtd = get_mtd_device(NULL, dev);
	if (IS_ERR(mtd)) {
		err = PTR_ERR(mtd);
		printk(PRINT_PREF "error: cannot get MTD device\n");
		return err;
	}

	if (mtd->writesize == 1) {
		printk(PRINT_PREF "not NAND flash, assume page size is 512 "
		       "bytes.\n");
		pgsize = 512;
	} else
		pgsize = mtd->writesize;

	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / pgsize;

	printk(PRINT_PREF "MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       pgsize, ebcnt, pgcnt, mtd->oobsize);

	if (count > 0 && count < ebcnt)
		ebcnt = count;

	err = -ENOMEM;
	iobuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!iobuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}

	set_random_data(iobuf, mtd->erasesize);

	err = scan_for_bad_eraseblocks();
	if (err)
		goto out;

	err = erase_whole_device();
	if (err)
		goto out;

	/* Write all eraseblocks, 1 eraseblock at a time */
	printk(PRINT_PREF "testing eraseblock write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "eraseblock write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 1 eraseblock at a time */
	printk(PRINT_PREF "testing eraseblock read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "eraseblock read speed is %ld KiB/s\n", speed);

	err = erase_whole_device();
	if (err)
		goto out;

	/* Write all eraseblocks, 1 page at a time */
	printk(PRINT_PREF "testing page write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock_by_page(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "page write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 1 page at a time */
	printk(PRINT_PREF "testing page read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock_by_page(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "page read speed is %ld KiB/s\n", speed);

	err = erase_whole_device();
	if (err)
		goto out;

	/* Write all eraseblocks, 2 pages at a time */
	printk(PRINT_PREF "testing 2 page write speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock_by_2pages(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "2 page write speed is %ld KiB/s\n", speed);

	/* Read all eraseblocks, 2 pages at a time */
	printk(PRINT_PREF "testing 2 page read speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = read_eraseblock_by_2pages(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "2 page read speed is %ld KiB/s\n", speed);

	/* Erase all eraseblocks */
	printk(PRINT_PREF "Testing erase speed\n");
	start_timing();
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = erase_eraseblock(i);
		if (err)
			goto out;
		cond_resched();
	}
	stop_timing();
	speed = calc_speed();
	printk(PRINT_PREF "erase speed is %ld KiB/s\n", speed);

	/* Multi-block erase all eraseblocks */
	for (k = 1; k < 7; k++) {
		blocks = 1 << k;
		printk(PRINT_PREF "Testing %dx multi-block erase speed\n",
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
			cond_resched();
			i += j;
		}
		stop_timing();
		speed = calc_speed();
		printk(PRINT_PREF "%dx multi-block erase speed is %ld KiB/s\n",
		       blocks, speed);
	}
	printk(PRINT_PREF "finished\n");
out:
	kfree(iobuf);
	kfree(bbt);
	put_mtd_device(mtd);
	if (err)
		printk(PRINT_PREF "error %d occurred\n", err);
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
