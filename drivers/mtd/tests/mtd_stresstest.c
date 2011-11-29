/*
 * Copyright (C) 2006-2008 Nokia Corporation
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
 * Test random reads, writes and erases on MTD device.
 *
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#define PRINT_PREF KERN_INFO "mtd_stresstest: "

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

static int rand_eb(void)
{
	int eb;

again:
	if (ebcnt < 32768)
		eb = simple_rand();
	else
		eb = (simple_rand() << 15) | simple_rand();
	/* Read or write up 2 eraseblocks at a time - hence 'ebcnt - 1' */
	eb %= (ebcnt - 1);
	if (bbt[eb])
		goto again;
	return eb;
}

static int rand_offs(void)
{
	int offs;

	if (bufsize < 32768)
		offs = simple_rand();
	else
		offs = (simple_rand() << 15) | simple_rand();
	offs %= bufsize;
	return offs;
}

static int rand_len(int offs)
{
	int len;

	if (bufsize < 32768)
		len = simple_rand();
	else
		len = (simple_rand() << 15) | simple_rand();
	len %= (bufsize - offs);
	return len;
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
	if (unlikely(err)) {
		printk(PRINT_PREF "error %d while erasing EB %d\n", err, ebnum);
		return err;
	}

	if (unlikely(ei.state == MTD_ERASE_FAILED)) {
		printk(PRINT_PREF "some erase error occurred at EB %d\n",
		       ebnum);
		return -EIO;
	}

	return 0;
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

static int do_read(void)
{
	size_t read = 0;
	int eb = rand_eb();
	int offs = rand_offs();
	int len = rand_len(offs), err;
	loff_t addr;

	if (bbt[eb + 1]) {
		if (offs >= mtd->erasesize)
			offs -= mtd->erasesize;
		if (offs + len > mtd->erasesize)
			len = mtd->erasesize - offs;
	}
	addr = eb * mtd->erasesize + offs;
	err = mtd->read(mtd, addr, len, &read, readbuf);
	if (mtd_is_bitflip(err))
		err = 0;
	if (unlikely(err || read != len)) {
		printk(PRINT_PREF "error: read failed at 0x%llx\n",
		       (long long)addr);
		if (!err)
			err = -EINVAL;
		return err;
	}
	return 0;
}

static int do_write(void)
{
	int eb = rand_eb(), offs, err, len;
	size_t written = 0;
	loff_t addr;

	offs = offsets[eb];
	if (offs >= mtd->erasesize) {
		err = erase_eraseblock(eb);
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
			err = erase_eraseblock(eb + 1);
			if (err)
				return err;
			offsets[eb + 1] = 0;
		}
	}
	addr = eb * mtd->erasesize + offs;
	err = mtd->write(mtd, addr, len, &written, writebuf);
	if (unlikely(err || written != len)) {
		printk(PRINT_PREF "error: write failed at 0x%llx\n",
		       (long long)addr);
		if (!err)
			err = -EINVAL;
		return err;
	}
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
	if (simple_rand() & 1)
		return do_read();
	else
		return do_write();
}

static int scan_for_bad_eraseblocks(void)
{
	int i, bad = 0;

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		return -ENOMEM;
	}

	/* NOR flash does not implement block_isbad */
	if (mtd->block_isbad == NULL)
		return 0;

	printk(PRINT_PREF "scanning for bad eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		bbt[i] = is_block_bad(i) ? 1 : 0;
		if (bbt[i])
			bad += 1;
		cond_resched();
	}
	printk(PRINT_PREF "scanned %d eraseblocks, %d are bad\n", i, bad);
	return 0;
}

static int __init mtd_stresstest_init(void)
{
	int err;
	int i, op;
	uint64_t tmp;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");

	if (dev < 0) {
		printk(PRINT_PREF "Please specify a valid mtd-device via module paramter\n");
		printk(KERN_CRIT "CAREFUL: This test wipes all data on the specified MTD device!\n");
		return -EINVAL;
	}

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

	if (ebcnt < 2) {
		printk(PRINT_PREF "error: need at least 2 eraseblocks\n");
		err = -ENOSPC;
		goto out_put_mtd;
	}

	/* Read or write up 2 eraseblocks at a time */
	bufsize = mtd->erasesize * 2;

	err = -ENOMEM;
	readbuf = vmalloc(bufsize);
	writebuf = vmalloc(bufsize);
	offsets = kmalloc(ebcnt * sizeof(int), GFP_KERNEL);
	if (!readbuf || !writebuf || !offsets) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}
	for (i = 0; i < ebcnt; i++)
		offsets[i] = mtd->erasesize;
	simple_srand(current->pid);
	for (i = 0; i < bufsize; i++)
		writebuf[i] = simple_rand();

	err = scan_for_bad_eraseblocks();
	if (err)
		goto out;

	/* Do operations */
	printk(PRINT_PREF "doing operations\n");
	for (op = 0; op < count; op++) {
		if ((op & 1023) == 0)
			printk(PRINT_PREF "%d operations done\n", op);
		err = do_operation();
		if (err)
			goto out;
		cond_resched();
	}
	printk(PRINT_PREF "finished, %d operations done\n", op);

out:
	kfree(offsets);
	kfree(bbt);
	vfree(writebuf);
	vfree(readbuf);
out_put_mtd:
	put_mtd_device(mtd);
	if (err)
		printk(PRINT_PREF "error %d occurred\n", err);
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
