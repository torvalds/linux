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
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/sched.h>

#define PRINT_PREF KERN_INFO "mtd_speedtest: "

static int dev;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static struct mtd_info *mtd;
static unsigned char *iobuf;
static unsigned char *bbt;

static int pgsize;
static int ebcnt;
static int pgcnt;
static int goodebcnt;
static struct timeval start, finish;
static unsigned long next = 1;

static inline unsigned int simple_rand(void)
{
	next = next * 1103515245 + 12345;
	return (unsigned int)((next / 65536) % 32768);
}

static inline void simple_srand(unsigned long seed)
{
	next = seed;
}

static void set_random_data(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; ++i)
		buf[i] = simple_rand();
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

	err = mtd->erase(mtd, &ei);
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
	size_t written = 0;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	err = mtd->write(mtd, addr, mtd->erasesize, &written, iobuf);
	if (err || written != mtd->erasesize) {
		printk(PRINT_PREF "error: write failed at %#llx\n", addr);
		if (!err)
			err = -EINVAL;
	}

	return err;
}

static int write_eraseblock_by_page(int ebnum)
{
	size_t written = 0;
	int i, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtd->write(mtd, addr, pgsize, &written, buf);
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
	size_t written = 0, sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtd->write(mtd, addr, sz, &written, buf);
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
		err = mtd->write(mtd, addr, pgsize, &written, buf);
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
	size_t read = 0;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	err = mtd->read(mtd, addr, mtd->erasesize, &read, iobuf);
	/* Ignore corrected ECC errors */
	if (err == -EUCLEAN)
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
	size_t read = 0;
	int i, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < pgcnt; i++) {
		err = mtd->read(mtd, addr, pgsize, &read, buf);
		/* Ignore corrected ECC errors */
		if (err == -EUCLEAN)
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
	size_t read = 0, sz = pgsize * 2;
	int i, n = pgcnt / 2, err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	void *buf = iobuf;

	for (i = 0; i < n; i++) {
		err = mtd->read(mtd, addr, sz, &read, buf);
		/* Ignore corrected ECC errors */
		if (err == -EUCLEAN)
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
		err = mtd->read(mtd, addr, pgsize, &read, buf);
		/* Ignore corrected ECC errors */
		if (err == -EUCLEAN)
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

	ret = mtd->block_isbad(mtd, addr);
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
	long ms, k, speed;

	ms = (finish.tv_sec - start.tv_sec) * 1000 +
	     (finish.tv_usec - start.tv_usec) / 1000;
	k = goodebcnt * mtd->erasesize / 1024;
	speed = (k * 1000) / ms;
	return speed;
}

static int scan_for_bad_eraseblocks(void)
{
	int i, bad = 0;

	bbt = kmalloc(ebcnt, GFP_KERNEL);
	if (!bbt) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		return -ENOMEM;
	}
	memset(bbt, 0 , ebcnt);

	printk(PRINT_PREF "scanning for bad eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		bbt[i] = is_block_bad(i) ? 1 : 0;
		if (bbt[i])
			bad += 1;
		cond_resched();
	}
	printk(PRINT_PREF "scanned %d eraseblocks, %d are bad\n", i, bad);
	goodebcnt = ebcnt - bad;
	return 0;
}

static int __init mtd_speedtest_init(void)
{
	int err, i;
	long speed;
	uint64_t tmp;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");
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
	pgcnt = mtd->erasesize / mtd->writesize;

	printk(PRINT_PREF "MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       pgsize, ebcnt, pgcnt, mtd->oobsize);

	err = -ENOMEM;
	iobuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!iobuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}

	simple_srand(1);
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
