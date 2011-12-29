/*
 * Copyright (C) 2006-2007 Nokia Corporation
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
 * Test sub-page read and write on MTD device.
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>

#define PRINT_PREF KERN_INFO "mtd_subpagetest: "

static int dev = -EINVAL;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static struct mtd_info *mtd;
static unsigned char *writebuf;
static unsigned char *readbuf;
static unsigned char *bbt;

static int subpgsize;
static int bufsize;
static int ebcnt;
static int pgcnt;
static int errcnt;
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

static inline void clear_data(unsigned char *buf, size_t len)
{
	memset(buf, 0, len);
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

static int erase_whole_device(void)
{
	int err;
	unsigned int i;

	printk(PRINT_PREF "erasing whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = erase_eraseblock(i);
		if (err)
			return err;
		cond_resched();
	}
	printk(PRINT_PREF "erased %u eraseblocks\n", i);
	return 0;
}

static int write_eraseblock(int ebnum)
{
	size_t written;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	set_random_data(writebuf, subpgsize);
	err = mtd_write(mtd, addr, subpgsize, &written, writebuf);
	if (unlikely(err || written != subpgsize)) {
		printk(PRINT_PREF "error: write failed at %#llx\n",
		       (long long)addr);
		if (written != subpgsize) {
			printk(PRINT_PREF "  write size: %#x\n", subpgsize);
			printk(PRINT_PREF "  written: %#zx\n", written);
		}
		return err ? err : -1;
	}

	addr += subpgsize;

	set_random_data(writebuf, subpgsize);
	err = mtd_write(mtd, addr, subpgsize, &written, writebuf);
	if (unlikely(err || written != subpgsize)) {
		printk(PRINT_PREF "error: write failed at %#llx\n",
		       (long long)addr);
		if (written != subpgsize) {
			printk(PRINT_PREF "  write size: %#x\n", subpgsize);
			printk(PRINT_PREF "  written: %#zx\n", written);
		}
		return err ? err : -1;
	}

	return err;
}

static int write_eraseblock2(int ebnum)
{
	size_t written;
	int err = 0, k;
	loff_t addr = ebnum * mtd->erasesize;

	for (k = 1; k < 33; ++k) {
		if (addr + (subpgsize * k) > (ebnum + 1) * mtd->erasesize)
			break;
		set_random_data(writebuf, subpgsize * k);
		err = mtd_write(mtd, addr, subpgsize * k, &written, writebuf);
		if (unlikely(err || written != subpgsize * k)) {
			printk(PRINT_PREF "error: write failed at %#llx\n",
			       (long long)addr);
			if (written != subpgsize) {
				printk(PRINT_PREF "  write size: %#x\n",
				       subpgsize * k);
				printk(PRINT_PREF "  written: %#08zx\n",
				       written);
			}
			return err ? err : -1;
		}
		addr += subpgsize * k;
	}

	return err;
}

static void print_subpage(unsigned char *p)
{
	int i, j;

	for (i = 0; i < subpgsize; ) {
		for (j = 0; i < subpgsize && j < 32; ++i, ++j)
			printk("%02x", *p++);
		printk("\n");
	}
}

static int verify_eraseblock(int ebnum)
{
	size_t read;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	set_random_data(writebuf, subpgsize);
	clear_data(readbuf, subpgsize);
	err = mtd_read(mtd, addr, subpgsize, &read, readbuf);
	if (unlikely(err || read != subpgsize)) {
		if (mtd_is_bitflip(err) && read == subpgsize) {
			printk(PRINT_PREF "ECC correction at %#llx\n",
			       (long long)addr);
			err = 0;
		} else {
			printk(PRINT_PREF "error: read failed at %#llx\n",
			       (long long)addr);
			return err ? err : -1;
		}
	}
	if (unlikely(memcmp(readbuf, writebuf, subpgsize))) {
		printk(PRINT_PREF "error: verify failed at %#llx\n",
		       (long long)addr);
		printk(PRINT_PREF "------------- written----------------\n");
		print_subpage(writebuf);
		printk(PRINT_PREF "------------- read ------------------\n");
		print_subpage(readbuf);
		printk(PRINT_PREF "-------------------------------------\n");
		errcnt += 1;
	}

	addr += subpgsize;

	set_random_data(writebuf, subpgsize);
	clear_data(readbuf, subpgsize);
	err = mtd_read(mtd, addr, subpgsize, &read, readbuf);
	if (unlikely(err || read != subpgsize)) {
		if (mtd_is_bitflip(err) && read == subpgsize) {
			printk(PRINT_PREF "ECC correction at %#llx\n",
			       (long long)addr);
			err = 0;
		} else {
			printk(PRINT_PREF "error: read failed at %#llx\n",
			       (long long)addr);
			return err ? err : -1;
		}
	}
	if (unlikely(memcmp(readbuf, writebuf, subpgsize))) {
		printk(PRINT_PREF "error: verify failed at %#llx\n",
		       (long long)addr);
		printk(PRINT_PREF "------------- written----------------\n");
		print_subpage(writebuf);
		printk(PRINT_PREF "------------- read ------------------\n");
		print_subpage(readbuf);
		printk(PRINT_PREF "-------------------------------------\n");
		errcnt += 1;
	}

	return err;
}

static int verify_eraseblock2(int ebnum)
{
	size_t read;
	int err = 0, k;
	loff_t addr = ebnum * mtd->erasesize;

	for (k = 1; k < 33; ++k) {
		if (addr + (subpgsize * k) > (ebnum + 1) * mtd->erasesize)
			break;
		set_random_data(writebuf, subpgsize * k);
		clear_data(readbuf, subpgsize * k);
		err = mtd_read(mtd, addr, subpgsize * k, &read, readbuf);
		if (unlikely(err || read != subpgsize * k)) {
			if (mtd_is_bitflip(err) && read == subpgsize * k) {
				printk(PRINT_PREF "ECC correction at %#llx\n",
				       (long long)addr);
				err = 0;
			} else {
				printk(PRINT_PREF "error: read failed at "
				       "%#llx\n", (long long)addr);
				return err ? err : -1;
			}
		}
		if (unlikely(memcmp(readbuf, writebuf, subpgsize * k))) {
			printk(PRINT_PREF "error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
		}
		addr += subpgsize * k;
	}

	return err;
}

static int verify_eraseblock_ff(int ebnum)
{
	uint32_t j;
	size_t read;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	memset(writebuf, 0xff, subpgsize);
	for (j = 0; j < mtd->erasesize / subpgsize; ++j) {
		clear_data(readbuf, subpgsize);
		err = mtd_read(mtd, addr, subpgsize, &read, readbuf);
		if (unlikely(err || read != subpgsize)) {
			if (mtd_is_bitflip(err) && read == subpgsize) {
				printk(PRINT_PREF "ECC correction at %#llx\n",
				       (long long)addr);
				err = 0;
			} else {
				printk(PRINT_PREF "error: read failed at "
				       "%#llx\n", (long long)addr);
				return err ? err : -1;
			}
		}
		if (unlikely(memcmp(readbuf, writebuf, subpgsize))) {
			printk(PRINT_PREF "error: verify 0xff failed at "
			       "%#llx\n", (long long)addr);
			errcnt += 1;
		}
		addr += subpgsize;
	}

	return err;
}

static int verify_all_eraseblocks_ff(void)
{
	int err;
	unsigned int i;

	printk(PRINT_PREF "verifying all eraseblocks for 0xff\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock_ff(i);
		if (err)
			return err;
		if (i % 256 == 0)
			printk(PRINT_PREF "verified up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "verified %u eraseblocks\n", i);
	return 0;
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

static int scan_for_bad_eraseblocks(void)
{
	int i, bad = 0;

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		return -ENOMEM;
	}

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

static int __init mtd_subpagetest_init(void)
{
	int err = 0;
	uint32_t i;
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

	if (mtd->type != MTD_NANDFLASH) {
		printk(PRINT_PREF "this test requires NAND flash\n");
		goto out;
	}

	subpgsize = mtd->writesize >> mtd->subpage_sft;
	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / mtd->writesize;

	printk(PRINT_PREF "MTD device size %llu, eraseblock size %u, "
	       "page size %u, subpage size %u, count of eraseblocks %u, "
	       "pages per eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       mtd->writesize, subpgsize, ebcnt, pgcnt, mtd->oobsize);

	err = -ENOMEM;
	bufsize = subpgsize * 32;
	writebuf = kmalloc(bufsize, GFP_KERNEL);
	if (!writebuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}
	readbuf = kmalloc(bufsize, GFP_KERNEL);
	if (!readbuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}

	err = scan_for_bad_eraseblocks();
	if (err)
		goto out;

	err = erase_whole_device();
	if (err)
		goto out;

	printk(PRINT_PREF "writing whole device\n");
	simple_srand(1);
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (unlikely(err))
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "written up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "written %u eraseblocks\n", i);

	simple_srand(1);
	printk(PRINT_PREF "verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock(i);
		if (unlikely(err))
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "verified up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "verified %u eraseblocks\n", i);

	err = erase_whole_device();
	if (err)
		goto out;

	err = verify_all_eraseblocks_ff();
	if (err)
		goto out;

	/* Write all eraseblocks */
	simple_srand(3);
	printk(PRINT_PREF "writing whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock2(i);
		if (unlikely(err))
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "written up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "written %u eraseblocks\n", i);

	/* Check all eraseblocks */
	simple_srand(3);
	printk(PRINT_PREF "verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock2(i);
		if (unlikely(err))
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "verified up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "verified %u eraseblocks\n", i);

	err = erase_whole_device();
	if (err)
		goto out;

	err = verify_all_eraseblocks_ff();
	if (err)
		goto out;

	printk(PRINT_PREF "finished with %d errors\n", errcnt);

out:
	kfree(bbt);
	kfree(readbuf);
	kfree(writebuf);
	put_mtd_device(mtd);
	if (err)
		printk(PRINT_PREF "error %d occurred\n", err);
	printk(KERN_INFO "=================================================\n");
	return err;
}
module_init(mtd_subpagetest_init);

static void __exit mtd_subpagetest_exit(void)
{
	return;
}
module_exit(mtd_subpagetest_exit);

MODULE_DESCRIPTION("Subpage test module");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL");
