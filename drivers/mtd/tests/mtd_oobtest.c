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
 * Test OOB read and write on MTD device.
 *
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 */

#include <asm/div64.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/sched.h>

#define PRINT_PREF KERN_INFO "mtd_oobtest: "

static int dev;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static struct mtd_info *mtd;
static unsigned char *readbuf;
static unsigned char *writebuf;
static unsigned char *bbt;

static int ebcnt;
static int pgcnt;
static int errcnt;
static int use_offset;
static int use_len;
static int use_len_max;
static int vary_offset;
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

static void do_vary_offset(void)
{
	use_len -= 1;
	if (use_len < 1) {
		use_offset += 1;
		if (use_offset >= use_len_max)
			use_offset = 0;
		use_len = use_len_max - use_offset;
	}
}

static int write_eraseblock(int ebnum)
{
	int i;
	struct mtd_oob_ops ops;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	for (i = 0; i < pgcnt; ++i, addr += mtd->writesize) {
		set_random_data(writebuf, use_len);
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = use_len;
		ops.oobretlen = 0;
		ops.ooboffs   = use_offset;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf;
		err = mtd->write_oob(mtd, addr, &ops);
		if (err || ops.oobretlen != use_len) {
			printk(PRINT_PREF "error: writeoob failed at %#llx\n",
			       (long long)addr);
			printk(PRINT_PREF "error: use_len %d, use_offset %d\n",
			       use_len, use_offset);
			errcnt += 1;
			return err ? err : -1;
		}
		if (vary_offset)
			do_vary_offset();
	}

	return err;
}

static int write_whole_device(void)
{
	int err;
	unsigned int i;

	printk(PRINT_PREF "writing OOBs of whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			return err;
		if (i % 256 == 0)
			printk(PRINT_PREF "written up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "written %u eraseblocks\n", i);
	return 0;
}

static int verify_eraseblock(int ebnum)
{
	int i;
	struct mtd_oob_ops ops;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;

	for (i = 0; i < pgcnt; ++i, addr += mtd->writesize) {
		set_random_data(writebuf, use_len);
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = use_len;
		ops.oobretlen = 0;
		ops.ooboffs   = use_offset;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		err = mtd->read_oob(mtd, addr, &ops);
		if (err || ops.oobretlen != use_len) {
			printk(PRINT_PREF "error: readoob failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			return err ? err : -1;
		}
		if (memcmp(readbuf, writebuf, use_len)) {
			printk(PRINT_PREF "error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			if (errcnt > 1000) {
				printk(PRINT_PREF "error: too many errors\n");
				return -1;
			}
		}
		if (use_offset != 0 || use_len < mtd->ecclayout->oobavail) {
			int k;

			ops.mode      = MTD_OOB_AUTO;
			ops.len       = 0;
			ops.retlen    = 0;
			ops.ooblen    = mtd->ecclayout->oobavail;
			ops.oobretlen = 0;
			ops.ooboffs   = 0;
			ops.datbuf    = NULL;
			ops.oobbuf    = readbuf;
			err = mtd->read_oob(mtd, addr, &ops);
			if (err || ops.oobretlen != mtd->ecclayout->oobavail) {
				printk(PRINT_PREF "error: readoob failed at "
				       "%#llx\n", (long long)addr);
				errcnt += 1;
				return err ? err : -1;
			}
			if (memcmp(readbuf + use_offset, writebuf, use_len)) {
				printk(PRINT_PREF "error: verify failed at "
				       "%#llx\n", (long long)addr);
				errcnt += 1;
				if (errcnt > 1000) {
					printk(PRINT_PREF "error: too many "
					       "errors\n");
					return -1;
				}
			}
			for (k = 0; k < use_offset; ++k)
				if (readbuf[k] != 0xff) {
					printk(PRINT_PREF "error: verify 0xff "
					       "failed at %#llx\n",
					       (long long)addr);
					errcnt += 1;
					if (errcnt > 1000) {
						printk(PRINT_PREF "error: too "
						       "many errors\n");
						return -1;
					}
				}
			for (k = use_offset + use_len;
			     k < mtd->ecclayout->oobavail; ++k)
				if (readbuf[k] != 0xff) {
					printk(PRINT_PREF "error: verify 0xff "
					       "failed at %#llx\n",
					       (long long)addr);
					errcnt += 1;
					if (errcnt > 1000) {
						printk(PRINT_PREF "error: too "
						       "many errors\n");
						return -1;
					}
				}
		}
		if (vary_offset)
			do_vary_offset();
	}
	return err;
}

static int verify_eraseblock_in_one_go(int ebnum)
{
	struct mtd_oob_ops ops;
	int err = 0;
	loff_t addr = ebnum * mtd->erasesize;
	size_t len = mtd->ecclayout->oobavail * pgcnt;

	set_random_data(writebuf, len);
	ops.mode      = MTD_OOB_AUTO;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = len;
	ops.oobretlen = 0;
	ops.ooboffs   = 0;
	ops.datbuf    = NULL;
	ops.oobbuf    = readbuf;
	err = mtd->read_oob(mtd, addr, &ops);
	if (err || ops.oobretlen != len) {
		printk(PRINT_PREF "error: readoob failed at %#llx\n",
		       (long long)addr);
		errcnt += 1;
		return err ? err : -1;
	}
	if (memcmp(readbuf, writebuf, len)) {
		printk(PRINT_PREF "error: verify failed at %#llx\n",
		       (long long)addr);
		errcnt += 1;
		if (errcnt > 1000) {
			printk(PRINT_PREF "error: too many errors\n");
			return -1;
		}
	}

	return err;
}

static int verify_all_eraseblocks(void)
{
	int err;
	unsigned int i;

	printk(PRINT_PREF "verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock(i);
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
	int ret;
	loff_t addr = ebnum * mtd->erasesize;

	ret = mtd->block_isbad(mtd, addr);
	if (ret)
		printk(PRINT_PREF "block %d is bad\n", ebnum);
	return ret;
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
	return 0;
}

static int __init mtd_oobtest_init(void)
{
	int err = 0;
	unsigned int i;
	uint64_t tmp;
	struct mtd_oob_ops ops;
	loff_t addr = 0, addr0;

	printk(KERN_INFO "\n");
	printk(KERN_INFO "=================================================\n");
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

	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / mtd->writesize;

	printk(PRINT_PREF "MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       mtd->writesize, ebcnt, pgcnt, mtd->oobsize);

	err = -ENOMEM;
	mtd->erasesize = mtd->erasesize;
	readbuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!readbuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}
	writebuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!writebuf) {
		printk(PRINT_PREF "error: cannot allocate memory\n");
		goto out;
	}

	err = scan_for_bad_eraseblocks();
	if (err)
		goto out;

	use_offset = 0;
	use_len = mtd->ecclayout->oobavail;
	use_len_max = mtd->ecclayout->oobavail;
	vary_offset = 0;

	/* First test: write all OOB, read it back and verify */
	printk(PRINT_PREF "test 1 of 5\n");

	err = erase_whole_device();
	if (err)
		goto out;

	simple_srand(1);
	err = write_whole_device();
	if (err)
		goto out;

	simple_srand(1);
	err = verify_all_eraseblocks();
	if (err)
		goto out;

	/*
	 * Second test: write all OOB, a block at a time, read it back and
	 * verify.
	 */
	printk(PRINT_PREF "test 2 of 5\n");

	err = erase_whole_device();
	if (err)
		goto out;

	simple_srand(3);
	err = write_whole_device();
	if (err)
		goto out;

	/* Check all eraseblocks */
	simple_srand(3);
	printk(PRINT_PREF "verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock_in_one_go(i);
		if (err)
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "verified up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "verified %u eraseblocks\n", i);

	/*
	 * Third test: write OOB at varying offsets and lengths, read it back
	 * and verify.
	 */
	printk(PRINT_PREF "test 3 of 5\n");

	err = erase_whole_device();
	if (err)
		goto out;

	/* Write all eraseblocks */
	use_offset = 0;
	use_len = mtd->ecclayout->oobavail;
	use_len_max = mtd->ecclayout->oobavail;
	vary_offset = 1;
	simple_srand(5);
	printk(PRINT_PREF "writing OOBs of whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			goto out;
		if (i % 256 == 0)
			printk(PRINT_PREF "written up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "written %u eraseblocks\n", i);

	/* Check all eraseblocks */
	use_offset = 0;
	use_len = mtd->ecclayout->oobavail;
	use_len_max = mtd->ecclayout->oobavail;
	vary_offset = 1;
	simple_srand(5);
	err = verify_all_eraseblocks();
	if (err)
		goto out;

	use_offset = 0;
	use_len = mtd->ecclayout->oobavail;
	use_len_max = mtd->ecclayout->oobavail;
	vary_offset = 0;

	/* Fourth test: try to write off end of device */
	printk(PRINT_PREF "test 4 of 5\n");

	err = erase_whole_device();
	if (err)
		goto out;

	addr0 = 0;
	for (i = 0; bbt[i] && i < ebcnt; ++i)
		addr0 += mtd->erasesize;

	/* Attempt to write off end of OOB */
	ops.mode      = MTD_OOB_AUTO;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = 1;
	ops.oobretlen = 0;
	ops.ooboffs   = mtd->ecclayout->oobavail;
	ops.datbuf    = NULL;
	ops.oobbuf    = writebuf;
	printk(PRINT_PREF "attempting to start write past end of OOB\n");
	printk(PRINT_PREF "an error is expected...\n");
	err = mtd->write_oob(mtd, addr0, &ops);
	if (err) {
		printk(PRINT_PREF "error occurred as expected\n");
		err = 0;
	} else {
		printk(PRINT_PREF "error: can write past end of OOB\n");
		errcnt += 1;
	}

	/* Attempt to read off end of OOB */
	ops.mode      = MTD_OOB_AUTO;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = 1;
	ops.oobretlen = 0;
	ops.ooboffs   = mtd->ecclayout->oobavail;
	ops.datbuf    = NULL;
	ops.oobbuf    = readbuf;
	printk(PRINT_PREF "attempting to start read past end of OOB\n");
	printk(PRINT_PREF "an error is expected...\n");
	err = mtd->read_oob(mtd, addr0, &ops);
	if (err) {
		printk(PRINT_PREF "error occurred as expected\n");
		err = 0;
	} else {
		printk(PRINT_PREF "error: can read past end of OOB\n");
		errcnt += 1;
	}

	if (bbt[ebcnt - 1])
		printk(PRINT_PREF "skipping end of device tests because last "
		       "block is bad\n");
	else {
		/* Attempt to write off end of device */
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->ecclayout->oobavail + 1;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf;
		printk(PRINT_PREF "attempting to write past end of device\n");
		printk(PRINT_PREF "an error is expected...\n");
		err = mtd->write_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			printk(PRINT_PREF "error occurred as expected\n");
			err = 0;
		} else {
			printk(PRINT_PREF "error: wrote past end of device\n");
			errcnt += 1;
		}

		/* Attempt to read off end of device */
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->ecclayout->oobavail + 1;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		printk(PRINT_PREF "attempting to read past end of device\n");
		printk(PRINT_PREF "an error is expected...\n");
		err = mtd->read_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			printk(PRINT_PREF "error occurred as expected\n");
			err = 0;
		} else {
			printk(PRINT_PREF "error: read past end of device\n");
			errcnt += 1;
		}

		err = erase_eraseblock(ebcnt - 1);
		if (err)
			goto out;

		/* Attempt to write off end of device */
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->ecclayout->oobavail;
		ops.oobretlen = 0;
		ops.ooboffs   = 1;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf;
		printk(PRINT_PREF "attempting to write past end of device\n");
		printk(PRINT_PREF "an error is expected...\n");
		err = mtd->write_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			printk(PRINT_PREF "error occurred as expected\n");
			err = 0;
		} else {
			printk(PRINT_PREF "error: wrote past end of device\n");
			errcnt += 1;
		}

		/* Attempt to read off end of device */
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->ecclayout->oobavail;
		ops.oobretlen = 0;
		ops.ooboffs   = 1;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		printk(PRINT_PREF "attempting to read past end of device\n");
		printk(PRINT_PREF "an error is expected...\n");
		err = mtd->read_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			printk(PRINT_PREF "error occurred as expected\n");
			err = 0;
		} else {
			printk(PRINT_PREF "error: read past end of device\n");
			errcnt += 1;
		}
	}

	/* Fifth test: write / read across block boundaries */
	printk(PRINT_PREF "test 5 of 5\n");

	/* Erase all eraseblocks */
	err = erase_whole_device();
	if (err)
		goto out;

	/* Write all eraseblocks */
	simple_srand(11);
	printk(PRINT_PREF "writing OOBs of whole device\n");
	for (i = 0; i < ebcnt - 1; ++i) {
		int cnt = 2;
		int pg;
		size_t sz = mtd->ecclayout->oobavail;
		if (bbt[i] || bbt[i + 1])
			continue;
		addr = (i + 1) * mtd->erasesize - mtd->writesize;
		for (pg = 0; pg < cnt; ++pg) {
			set_random_data(writebuf, sz);
			ops.mode      = MTD_OOB_AUTO;
			ops.len       = 0;
			ops.retlen    = 0;
			ops.ooblen    = sz;
			ops.oobretlen = 0;
			ops.ooboffs   = 0;
			ops.datbuf    = NULL;
			ops.oobbuf    = writebuf;
			err = mtd->write_oob(mtd, addr, &ops);
			if (err)
				goto out;
			if (i % 256 == 0)
				printk(PRINT_PREF "written up to eraseblock "
				       "%u\n", i);
			cond_resched();
			addr += mtd->writesize;
		}
	}
	printk(PRINT_PREF "written %u eraseblocks\n", i);

	/* Check all eraseblocks */
	simple_srand(11);
	printk(PRINT_PREF "verifying all eraseblocks\n");
	for (i = 0; i < ebcnt - 1; ++i) {
		if (bbt[i] || bbt[i + 1])
			continue;
		set_random_data(writebuf, mtd->ecclayout->oobavail * 2);
		addr = (i + 1) * mtd->erasesize - mtd->writesize;
		ops.mode      = MTD_OOB_AUTO;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->ecclayout->oobavail * 2;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		err = mtd->read_oob(mtd, addr, &ops);
		if (err)
			goto out;
		if (memcmp(readbuf, writebuf, mtd->ecclayout->oobavail * 2)) {
			printk(PRINT_PREF "error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			if (errcnt > 1000) {
				printk(PRINT_PREF "error: too many errors\n");
				goto out;
			}
		}
		if (i % 256 == 0)
			printk(PRINT_PREF "verified up to eraseblock %u\n", i);
		cond_resched();
	}
	printk(PRINT_PREF "verified %u eraseblocks\n", i);

	printk(PRINT_PREF "finished with %d errors\n", errcnt);
out:
	kfree(bbt);
	kfree(writebuf);
	kfree(readbuf);
	put_mtd_device(mtd);
	if (err)
		printk(PRINT_PREF "error %d occurred\n", err);
	printk(KERN_INFO "=================================================\n");
	return err;
}
module_init(mtd_oobtest_init);

static void __exit mtd_oobtest_exit(void)
{
	return;
}
module_exit(mtd_oobtest_exit);

MODULE_DESCRIPTION("Out-of-band test module");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL");
