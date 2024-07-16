// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Test OOB read and write on MTD device.
 *
 * Author: Adrian Hunter <ext-adrian.hunter@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/div64.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/random.h>

#include "mtd_test.h"

static int dev = -EINVAL;
static int bitflip_limit;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");
module_param(bitflip_limit, int, S_IRUGO);
MODULE_PARM_DESC(bitflip_limit, "Max. allowed bitflips per page");

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
static struct rnd_state rnd_state;

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
	struct mtd_oob_ops ops = { };
	int err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;

	prandom_bytes_state(&rnd_state, writebuf, use_len_max * pgcnt);
	for (i = 0; i < pgcnt; ++i, addr += mtd->writesize) {
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = use_len;
		ops.oobretlen = 0;
		ops.ooboffs   = use_offset;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf + (use_len_max * i) + use_offset;
		err = mtd_write_oob(mtd, addr, &ops);
		if (err || ops.oobretlen != use_len) {
			pr_err("error: writeoob failed at %#llx\n",
			       (long long)addr);
			pr_err("error: use_len %d, use_offset %d\n",
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

	pr_info("writing OOBs of whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			return err;
		if (i % 256 == 0)
			pr_info("written up to eraseblock %u\n", i);

		err = mtdtest_relax();
		if (err)
			return err;
	}
	pr_info("written %u eraseblocks\n", i);
	return 0;
}

/*
 * Display the address, offset and data bytes at comparison failure.
 * Return number of bitflips encountered.
 */
static size_t memcmpshowoffset(loff_t addr, loff_t offset, const void *cs,
			       const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	int res;
	size_t i = 0;
	size_t bitflips = 0;

	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--, i++) {
		res = *su1 ^ *su2;
		if (res) {
			pr_info("error @addr[0x%lx:0x%lx] 0x%x -> 0x%x diff 0x%x\n",
				(unsigned long)addr, (unsigned long)offset + i,
				*su1, *su2, res);
			bitflips += hweight8(res);
		}
	}

	return bitflips;
}

#define memcmpshow(addr, cs, ct, count) memcmpshowoffset((addr), 0, (cs), (ct),\
							 (count))

/*
 * Compare with 0xff and show the address, offset and data bytes at
 * comparison failure. Return number of bitflips encountered.
 */
static size_t memffshow(loff_t addr, loff_t offset, const void *cs,
			size_t count)
{
	const unsigned char *su1;
	int res;
	size_t i = 0;
	size_t bitflips = 0;

	for (su1 = cs; 0 < count; ++su1, count--, i++) {
		res = *su1 ^ 0xff;
		if (res) {
			pr_info("error @addr[0x%lx:0x%lx] 0x%x -> 0xff diff 0x%x\n",
				(unsigned long)addr, (unsigned long)offset + i,
				*su1, res);
			bitflips += hweight8(res);
		}
	}

	return bitflips;
}

static int verify_eraseblock(int ebnum)
{
	int i;
	struct mtd_oob_ops ops = { };
	int err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	size_t bitflips;

	prandom_bytes_state(&rnd_state, writebuf, use_len_max * pgcnt);
	for (i = 0; i < pgcnt; ++i, addr += mtd->writesize) {
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = use_len;
		ops.oobretlen = 0;
		ops.ooboffs   = use_offset;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		err = mtd_read_oob(mtd, addr, &ops);
		if (mtd_is_bitflip(err))
			err = 0;

		if (err || ops.oobretlen != use_len) {
			pr_err("error: readoob failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			return err ? err : -1;
		}

		bitflips = memcmpshow(addr, readbuf,
				      writebuf + (use_len_max * i) + use_offset,
				      use_len);
		if (bitflips > bitflip_limit) {
			pr_err("error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			if (errcnt > 1000) {
				pr_err("error: too many errors\n");
				return -1;
			}
		} else if (bitflips) {
			pr_info("ignoring error as within bitflip_limit\n");
		}

		if (use_offset != 0 || use_len < mtd->oobavail) {
			int k;

			ops.mode      = MTD_OPS_AUTO_OOB;
			ops.len       = 0;
			ops.retlen    = 0;
			ops.ooblen    = mtd->oobavail;
			ops.oobretlen = 0;
			ops.ooboffs   = 0;
			ops.datbuf    = NULL;
			ops.oobbuf    = readbuf;
			err = mtd_read_oob(mtd, addr, &ops);
			if (mtd_is_bitflip(err))
				err = 0;

			if (err || ops.oobretlen != mtd->oobavail) {
				pr_err("error: readoob failed at %#llx\n",
						(long long)addr);
				errcnt += 1;
				return err ? err : -1;
			}
			bitflips = memcmpshowoffset(addr, use_offset,
						    readbuf + use_offset,
						    writebuf + (use_len_max * i) + use_offset,
						    use_len);

			/* verify pre-offset area for 0xff */
			bitflips += memffshow(addr, 0, readbuf, use_offset);

			/* verify post-(use_offset + use_len) area for 0xff */
			k = use_offset + use_len;
			bitflips += memffshow(addr, k, readbuf + k,
					      mtd->oobavail - k);

			if (bitflips > bitflip_limit) {
				pr_err("error: verify failed at %#llx\n",
						(long long)addr);
				errcnt += 1;
				if (errcnt > 1000) {
					pr_err("error: too many errors\n");
					return -1;
				}
			} else if (bitflips) {
				pr_info("ignoring errors as within bitflip limit\n");
			}
		}
		if (vary_offset)
			do_vary_offset();
	}
	return err;
}

static int verify_eraseblock_in_one_go(int ebnum)
{
	struct mtd_oob_ops ops = { };
	int err = 0;
	loff_t addr = (loff_t)ebnum * mtd->erasesize;
	size_t len = mtd->oobavail * pgcnt;
	size_t oobavail = mtd->oobavail;
	size_t bitflips;
	int i;

	prandom_bytes_state(&rnd_state, writebuf, len);
	ops.mode      = MTD_OPS_AUTO_OOB;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = len;
	ops.oobretlen = 0;
	ops.ooboffs   = 0;
	ops.datbuf    = NULL;
	ops.oobbuf    = readbuf;

	/* read entire block's OOB at one go */
	err = mtd_read_oob(mtd, addr, &ops);
	if (mtd_is_bitflip(err))
		err = 0;

	if (err || ops.oobretlen != len) {
		pr_err("error: readoob failed at %#llx\n",
		       (long long)addr);
		errcnt += 1;
		return err ? err : -1;
	}

	/* verify one page OOB at a time for bitflip per page limit check */
	for (i = 0; i < pgcnt; ++i, addr += mtd->writesize) {
		bitflips = memcmpshow(addr, readbuf + (i * oobavail),
				      writebuf + (i * oobavail), oobavail);
		if (bitflips > bitflip_limit) {
			pr_err("error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			if (errcnt > 1000) {
				pr_err("error: too many errors\n");
				return -1;
			}
		} else if (bitflips) {
			pr_info("ignoring error as within bitflip_limit\n");
		}
	}

	return err;
}

static int verify_all_eraseblocks(void)
{
	int err;
	unsigned int i;

	pr_info("verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock(i);
		if (err)
			return err;
		if (i % 256 == 0)
			pr_info("verified up to eraseblock %u\n", i);

		err = mtdtest_relax();
		if (err)
			return err;
	}
	pr_info("verified %u eraseblocks\n", i);
	return 0;
}

static int __init mtd_oobtest_init(void)
{
	int err = 0;
	unsigned int i;
	uint64_t tmp;
	struct mtd_oob_ops ops = { };
	loff_t addr = 0, addr0;

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

	if (!mtd_type_is_nand(mtd)) {
		pr_info("this test requires NAND flash\n");
		goto out;
	}

	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / mtd->writesize;

	pr_info("MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       mtd->writesize, ebcnt, pgcnt, mtd->oobsize);

	err = -ENOMEM;
	readbuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!readbuf)
		goto out;
	writebuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!writebuf)
		goto out;
	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt)
		goto out;

	err = mtdtest_scan_for_bad_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	use_offset = 0;
	use_len = mtd->oobavail;
	use_len_max = mtd->oobavail;
	vary_offset = 0;

	/* First test: write all OOB, read it back and verify */
	pr_info("test 1 of 5\n");

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	prandom_seed_state(&rnd_state, 1);
	err = write_whole_device();
	if (err)
		goto out;

	prandom_seed_state(&rnd_state, 1);
	err = verify_all_eraseblocks();
	if (err)
		goto out;

	/*
	 * Second test: write all OOB, a block at a time, read it back and
	 * verify.
	 */
	pr_info("test 2 of 5\n");

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	prandom_seed_state(&rnd_state, 3);
	err = write_whole_device();
	if (err)
		goto out;

	/* Check all eraseblocks */
	prandom_seed_state(&rnd_state, 3);
	pr_info("verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock_in_one_go(i);
		if (err)
			goto out;
		if (i % 256 == 0)
			pr_info("verified up to eraseblock %u\n", i);

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	pr_info("verified %u eraseblocks\n", i);

	/*
	 * Third test: write OOB at varying offsets and lengths, read it back
	 * and verify.
	 */
	pr_info("test 3 of 5\n");

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Write all eraseblocks */
	use_offset = 0;
	use_len = mtd->oobavail;
	use_len_max = mtd->oobavail;
	vary_offset = 1;
	prandom_seed_state(&rnd_state, 5);

	err = write_whole_device();
	if (err)
		goto out;

	/* Check all eraseblocks */
	use_offset = 0;
	use_len = mtd->oobavail;
	use_len_max = mtd->oobavail;
	vary_offset = 1;
	prandom_seed_state(&rnd_state, 5);
	err = verify_all_eraseblocks();
	if (err)
		goto out;

	use_offset = 0;
	use_len = mtd->oobavail;
	use_len_max = mtd->oobavail;
	vary_offset = 0;

	/* Fourth test: try to write off end of device */
	pr_info("test 4 of 5\n");

	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	addr0 = 0;
	for (i = 0; i < ebcnt && bbt[i]; ++i)
		addr0 += mtd->erasesize;

	/* Attempt to write off end of OOB */
	ops.mode      = MTD_OPS_AUTO_OOB;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = 1;
	ops.oobretlen = 0;
	ops.ooboffs   = mtd->oobavail;
	ops.datbuf    = NULL;
	ops.oobbuf    = writebuf;
	pr_info("attempting to start write past end of OOB\n");
	pr_info("an error is expected...\n");
	err = mtd_write_oob(mtd, addr0, &ops);
	if (err) {
		pr_info("error occurred as expected\n");
	} else {
		pr_err("error: can write past end of OOB\n");
		errcnt += 1;
	}

	/* Attempt to read off end of OOB */
	ops.mode      = MTD_OPS_AUTO_OOB;
	ops.len       = 0;
	ops.retlen    = 0;
	ops.ooblen    = 1;
	ops.oobretlen = 0;
	ops.ooboffs   = mtd->oobavail;
	ops.datbuf    = NULL;
	ops.oobbuf    = readbuf;
	pr_info("attempting to start read past end of OOB\n");
	pr_info("an error is expected...\n");
	err = mtd_read_oob(mtd, addr0, &ops);
	if (mtd_is_bitflip(err))
		err = 0;

	if (err) {
		pr_info("error occurred as expected\n");
	} else {
		pr_err("error: can read past end of OOB\n");
		errcnt += 1;
	}

	if (bbt[ebcnt - 1])
		pr_info("skipping end of device tests because last "
		       "block is bad\n");
	else {
		/* Attempt to write off end of device */
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->oobavail + 1;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf;
		pr_info("attempting to write past end of device\n");
		pr_info("an error is expected...\n");
		err = mtd_write_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			pr_info("error occurred as expected\n");
		} else {
			pr_err("error: wrote past end of device\n");
			errcnt += 1;
		}

		/* Attempt to read off end of device */
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->oobavail + 1;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		pr_info("attempting to read past end of device\n");
		pr_info("an error is expected...\n");
		err = mtd_read_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (mtd_is_bitflip(err))
			err = 0;

		if (err) {
			pr_info("error occurred as expected\n");
		} else {
			pr_err("error: read past end of device\n");
			errcnt += 1;
		}

		err = mtdtest_erase_eraseblock(mtd, ebcnt - 1);
		if (err)
			goto out;

		/* Attempt to write off end of device */
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->oobavail;
		ops.oobretlen = 0;
		ops.ooboffs   = 1;
		ops.datbuf    = NULL;
		ops.oobbuf    = writebuf;
		pr_info("attempting to write past end of device\n");
		pr_info("an error is expected...\n");
		err = mtd_write_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (err) {
			pr_info("error occurred as expected\n");
		} else {
			pr_err("error: wrote past end of device\n");
			errcnt += 1;
		}

		/* Attempt to read off end of device */
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->oobavail;
		ops.oobretlen = 0;
		ops.ooboffs   = 1;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		pr_info("attempting to read past end of device\n");
		pr_info("an error is expected...\n");
		err = mtd_read_oob(mtd, mtd->size - mtd->writesize, &ops);
		if (mtd_is_bitflip(err))
			err = 0;

		if (err) {
			pr_info("error occurred as expected\n");
		} else {
			pr_err("error: read past end of device\n");
			errcnt += 1;
		}
	}

	/* Fifth test: write / read across block boundaries */
	pr_info("test 5 of 5\n");

	/* Erase all eraseblocks */
	err = mtdtest_erase_good_eraseblocks(mtd, bbt, 0, ebcnt);
	if (err)
		goto out;

	/* Write all eraseblocks */
	prandom_seed_state(&rnd_state, 11);
	pr_info("writing OOBs of whole device\n");
	for (i = 0; i < ebcnt - 1; ++i) {
		int cnt = 2;
		int pg;
		size_t sz = mtd->oobavail;
		if (bbt[i] || bbt[i + 1])
			continue;
		addr = (loff_t)(i + 1) * mtd->erasesize - mtd->writesize;
		prandom_bytes_state(&rnd_state, writebuf, sz * cnt);
		for (pg = 0; pg < cnt; ++pg) {
			ops.mode      = MTD_OPS_AUTO_OOB;
			ops.len       = 0;
			ops.retlen    = 0;
			ops.ooblen    = sz;
			ops.oobretlen = 0;
			ops.ooboffs   = 0;
			ops.datbuf    = NULL;
			ops.oobbuf    = writebuf + pg * sz;
			err = mtd_write_oob(mtd, addr, &ops);
			if (err)
				goto out;
			if (i % 256 == 0)
				pr_info("written up to eraseblock %u\n", i);

			err = mtdtest_relax();
			if (err)
				goto out;

			addr += mtd->writesize;
		}
	}
	pr_info("written %u eraseblocks\n", i);

	/* Check all eraseblocks */
	prandom_seed_state(&rnd_state, 11);
	pr_info("verifying all eraseblocks\n");
	for (i = 0; i < ebcnt - 1; ++i) {
		if (bbt[i] || bbt[i + 1])
			continue;
		prandom_bytes_state(&rnd_state, writebuf, mtd->oobavail * 2);
		addr = (loff_t)(i + 1) * mtd->erasesize - mtd->writesize;
		ops.mode      = MTD_OPS_AUTO_OOB;
		ops.len       = 0;
		ops.retlen    = 0;
		ops.ooblen    = mtd->oobavail * 2;
		ops.oobretlen = 0;
		ops.ooboffs   = 0;
		ops.datbuf    = NULL;
		ops.oobbuf    = readbuf;
		err = mtd_read_oob(mtd, addr, &ops);
		if (mtd_is_bitflip(err))
			err = 0;

		if (err)
			goto out;
		if (memcmpshow(addr, readbuf, writebuf,
			       mtd->oobavail * 2)) {
			pr_err("error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
			if (errcnt > 1000) {
				err = -EINVAL;
				pr_err("error: too many errors\n");
				goto out;
			}
		}
		if (i % 256 == 0)
			pr_info("verified up to eraseblock %u\n", i);

		err = mtdtest_relax();
		if (err)
			goto out;
	}
	pr_info("verified %u eraseblocks\n", i);

	pr_info("finished with %d errors\n", errcnt);
out:
	kfree(bbt);
	kfree(writebuf);
	kfree(readbuf);
	put_mtd_device(mtd);
	if (err)
		pr_info("error %d occurred\n", err);
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
