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
 * Test page read and write on MTD device.
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

static int dev = -EINVAL;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static struct mtd_info *mtd;
static unsigned char *twopages;
static unsigned char *writebuf;
static unsigned char *boundary;
static unsigned char *bbt;

static int pgsize;
static int bufsize;
static int ebcnt;
static int pgcnt;
static int errcnt;
static struct rnd_state rnd_state;

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
		pr_err("error %d while erasing EB %d\n", err, ebnum);
		return err;
	}

	if (ei.state == MTD_ERASE_FAILED) {
		pr_err("some erase error occurred at EB %d\n",
		       ebnum);
		return -EIO;
	}

	return 0;
}

static int write_eraseblock(int ebnum)
{
	int err = 0;
	size_t written;
	loff_t addr = ebnum * mtd->erasesize;

	prandom_bytes_state(&rnd_state, writebuf, mtd->erasesize);
	cond_resched();
	err = mtd_write(mtd, addr, mtd->erasesize, &written, writebuf);
	if (err || written != mtd->erasesize)
		pr_err("error: write failed at %#llx\n",
		       (long long)addr);

	return err;
}

static int verify_eraseblock(int ebnum)
{
	uint32_t j;
	size_t read;
	int err = 0, i;
	loff_t addr0, addrn;
	loff_t addr = ebnum * mtd->erasesize;

	addr0 = 0;
	for (i = 0; i < ebcnt && bbt[i]; ++i)
		addr0 += mtd->erasesize;

	addrn = mtd->size;
	for (i = 0; i < ebcnt && bbt[ebcnt - i - 1]; ++i)
		addrn -= mtd->erasesize;

	prandom_bytes_state(&rnd_state, writebuf, mtd->erasesize);
	for (j = 0; j < pgcnt - 1; ++j, addr += pgsize) {
		/* Do a read to set the internal dataRAMs to different data */
		err = mtd_read(mtd, addr0, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)addr0);
			return err;
		}
		err = mtd_read(mtd, addrn - bufsize, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)(addrn - bufsize));
			return err;
		}
		memset(twopages, 0, bufsize);
		err = mtd_read(mtd, addr, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)addr);
			break;
		}
		if (memcmp(twopages, writebuf + (j * pgsize), bufsize)) {
			pr_err("error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
		}
	}
	/* Check boundary between eraseblocks */
	if (addr <= addrn - pgsize - pgsize && !bbt[ebnum + 1]) {
		struct rnd_state old_state = rnd_state;

		/* Do a read to set the internal dataRAMs to different data */
		err = mtd_read(mtd, addr0, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)addr0);
			return err;
		}
		err = mtd_read(mtd, addrn - bufsize, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)(addrn - bufsize));
			return err;
		}
		memset(twopages, 0, bufsize);
		err = mtd_read(mtd, addr, bufsize, &read, twopages);
		if (mtd_is_bitflip(err))
			err = 0;
		if (err || read != bufsize) {
			pr_err("error: read failed at %#llx\n",
			       (long long)addr);
			return err;
		}
		memcpy(boundary, writebuf + mtd->erasesize - pgsize, pgsize);
		prandom_bytes_state(&rnd_state, boundary + pgsize, pgsize);
		if (memcmp(twopages, boundary, bufsize)) {
			pr_err("error: verify failed at %#llx\n",
			       (long long)addr);
			errcnt += 1;
		}
		rnd_state = old_state;
	}
	return err;
}

static int crosstest(void)
{
	size_t read;
	int err = 0, i;
	loff_t addr, addr0, addrn;
	unsigned char *pp1, *pp2, *pp3, *pp4;

	pr_info("crosstest\n");
	pp1 = kmalloc(pgsize * 4, GFP_KERNEL);
	if (!pp1)
		return -ENOMEM;
	pp2 = pp1 + pgsize;
	pp3 = pp2 + pgsize;
	pp4 = pp3 + pgsize;
	memset(pp1, 0, pgsize * 4);

	addr0 = 0;
	for (i = 0; i < ebcnt && bbt[i]; ++i)
		addr0 += mtd->erasesize;

	addrn = mtd->size;
	for (i = 0; i < ebcnt && bbt[ebcnt - i - 1]; ++i)
		addrn -= mtd->erasesize;

	/* Read 2nd-to-last page to pp1 */
	addr = addrn - pgsize - pgsize;
	err = mtd_read(mtd, addr, pgsize, &read, pp1);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr);
		kfree(pp1);
		return err;
	}

	/* Read 3rd-to-last page to pp1 */
	addr = addrn - pgsize - pgsize - pgsize;
	err = mtd_read(mtd, addr, pgsize, &read, pp1);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr);
		kfree(pp1);
		return err;
	}

	/* Read first page to pp2 */
	addr = addr0;
	pr_info("reading page at %#llx\n", (long long)addr);
	err = mtd_read(mtd, addr, pgsize, &read, pp2);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr);
		kfree(pp1);
		return err;
	}

	/* Read last page to pp3 */
	addr = addrn - pgsize;
	pr_info("reading page at %#llx\n", (long long)addr);
	err = mtd_read(mtd, addr, pgsize, &read, pp3);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr);
		kfree(pp1);
		return err;
	}

	/* Read first page again to pp4 */
	addr = addr0;
	pr_info("reading page at %#llx\n", (long long)addr);
	err = mtd_read(mtd, addr, pgsize, &read, pp4);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr);
		kfree(pp1);
		return err;
	}

	/* pp2 and pp4 should be the same */
	pr_info("verifying pages read at %#llx match\n",
	       (long long)addr0);
	if (memcmp(pp2, pp4, pgsize)) {
		pr_err("verify failed!\n");
		errcnt += 1;
	} else if (!err)
		pr_info("crosstest ok\n");
	kfree(pp1);
	return err;
}

static int erasecrosstest(void)
{
	size_t read, written;
	int err = 0, i, ebnum, ebnum2;
	loff_t addr0;
	char *readbuf = twopages;

	pr_info("erasecrosstest\n");

	ebnum = 0;
	addr0 = 0;
	for (i = 0; i < ebcnt && bbt[i]; ++i) {
		addr0 += mtd->erasesize;
		ebnum += 1;
	}

	ebnum2 = ebcnt - 1;
	while (ebnum2 && bbt[ebnum2])
		ebnum2 -= 1;

	pr_info("erasing block %d\n", ebnum);
	err = erase_eraseblock(ebnum);
	if (err)
		return err;

	pr_info("writing 1st page of block %d\n", ebnum);
	prandom_bytes_state(&rnd_state, writebuf, pgsize);
	strcpy(writebuf, "There is no data like this!");
	err = mtd_write(mtd, addr0, pgsize, &written, writebuf);
	if (err || written != pgsize) {
		pr_info("error: write failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("reading 1st page of block %d\n", ebnum);
	memset(readbuf, 0, pgsize);
	err = mtd_read(mtd, addr0, pgsize, &read, readbuf);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("verifying 1st page of block %d\n", ebnum);
	if (memcmp(writebuf, readbuf, pgsize)) {
		pr_err("verify failed!\n");
		errcnt += 1;
		return -1;
	}

	pr_info("erasing block %d\n", ebnum);
	err = erase_eraseblock(ebnum);
	if (err)
		return err;

	pr_info("writing 1st page of block %d\n", ebnum);
	prandom_bytes_state(&rnd_state, writebuf, pgsize);
	strcpy(writebuf, "There is no data like this!");
	err = mtd_write(mtd, addr0, pgsize, &written, writebuf);
	if (err || written != pgsize) {
		pr_err("error: write failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("erasing block %d\n", ebnum2);
	err = erase_eraseblock(ebnum2);
	if (err)
		return err;

	pr_info("reading 1st page of block %d\n", ebnum);
	memset(readbuf, 0, pgsize);
	err = mtd_read(mtd, addr0, pgsize, &read, readbuf);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("verifying 1st page of block %d\n", ebnum);
	if (memcmp(writebuf, readbuf, pgsize)) {
		pr_err("verify failed!\n");
		errcnt += 1;
		return -1;
	}

	if (!err)
		pr_info("erasecrosstest ok\n");
	return err;
}

static int erasetest(void)
{
	size_t read, written;
	int err = 0, i, ebnum, ok = 1;
	loff_t addr0;

	pr_info("erasetest\n");

	ebnum = 0;
	addr0 = 0;
	for (i = 0; i < ebcnt && bbt[i]; ++i) {
		addr0 += mtd->erasesize;
		ebnum += 1;
	}

	pr_info("erasing block %d\n", ebnum);
	err = erase_eraseblock(ebnum);
	if (err)
		return err;

	pr_info("writing 1st page of block %d\n", ebnum);
	prandom_bytes_state(&rnd_state, writebuf, pgsize);
	err = mtd_write(mtd, addr0, pgsize, &written, writebuf);
	if (err || written != pgsize) {
		pr_err("error: write failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("erasing block %d\n", ebnum);
	err = erase_eraseblock(ebnum);
	if (err)
		return err;

	pr_info("reading 1st page of block %d\n", ebnum);
	err = mtd_read(mtd, addr0, pgsize, &read, twopages);
	if (mtd_is_bitflip(err))
		err = 0;
	if (err || read != pgsize) {
		pr_err("error: read failed at %#llx\n",
		       (long long)addr0);
		return err ? err : -1;
	}

	pr_info("verifying 1st page of block %d is all 0xff\n",
	       ebnum);
	for (i = 0; i < pgsize; ++i)
		if (twopages[i] != 0xff) {
			pr_err("verifying all 0xff failed at %d\n",
			       i);
			errcnt += 1;
			ok = 0;
			break;
		}

	if (ok && !err)
		pr_info("erasetest ok\n");

	return err;
}

static int is_block_bad(int ebnum)
{
	loff_t addr = ebnum * mtd->erasesize;
	int ret;

	ret = mtd_block_isbad(mtd, addr);
	if (ret)
		pr_info("block %d is bad\n", ebnum);
	return ret;
}

static int scan_for_bad_eraseblocks(void)
{
	int i, bad = 0;

	bbt = kzalloc(ebcnt, GFP_KERNEL);
	if (!bbt)
		return -ENOMEM;

	pr_info("scanning for bad eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		bbt[i] = is_block_bad(i) ? 1 : 0;
		if (bbt[i])
			bad += 1;
		cond_resched();
	}
	pr_info("scanned %d eraseblocks, %d are bad\n", i, bad);
	return 0;
}

static int __init mtd_pagetest_init(void)
{
	int err = 0;
	uint64_t tmp;
	uint32_t i;

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

	if (mtd->type != MTD_NANDFLASH) {
		pr_info("this test requires NAND flash\n");
		goto out;
	}

	tmp = mtd->size;
	do_div(tmp, mtd->erasesize);
	ebcnt = tmp;
	pgcnt = mtd->erasesize / mtd->writesize;
	pgsize = mtd->writesize;

	pr_info("MTD device size %llu, eraseblock size %u, "
	       "page size %u, count of eraseblocks %u, pages per "
	       "eraseblock %u, OOB size %u\n",
	       (unsigned long long)mtd->size, mtd->erasesize,
	       pgsize, ebcnt, pgcnt, mtd->oobsize);

	err = -ENOMEM;
	bufsize = pgsize * 2;
	writebuf = kmalloc(mtd->erasesize, GFP_KERNEL);
	if (!writebuf)
		goto out;
	twopages = kmalloc(bufsize, GFP_KERNEL);
	if (!twopages)
		goto out;
	boundary = kmalloc(bufsize, GFP_KERNEL);
	if (!boundary)
		goto out;

	err = scan_for_bad_eraseblocks();
	if (err)
		goto out;

	/* Erase all eraseblocks */
	pr_info("erasing whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = erase_eraseblock(i);
		if (err)
			goto out;
		cond_resched();
	}
	pr_info("erased %u eraseblocks\n", i);

	/* Write all eraseblocks */
	prandom_seed_state(&rnd_state, 1);
	pr_info("writing whole device\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = write_eraseblock(i);
		if (err)
			goto out;
		if (i % 256 == 0)
			pr_info("written up to eraseblock %u\n", i);
		cond_resched();
	}
	pr_info("written %u eraseblocks\n", i);

	/* Check all eraseblocks */
	prandom_seed_state(&rnd_state, 1);
	pr_info("verifying all eraseblocks\n");
	for (i = 0; i < ebcnt; ++i) {
		if (bbt[i])
			continue;
		err = verify_eraseblock(i);
		if (err)
			goto out;
		if (i % 256 == 0)
			pr_info("verified up to eraseblock %u\n", i);
		cond_resched();
	}
	pr_info("verified %u eraseblocks\n", i);

	err = crosstest();
	if (err)
		goto out;

	err = erasecrosstest();
	if (err)
		goto out;

	err = erasetest();
	if (err)
		goto out;

	pr_info("finished with %d errors\n", errcnt);
out:

	kfree(bbt);
	kfree(boundary);
	kfree(twopages);
	kfree(writebuf);
	put_mtd_device(mtd);
	if (err)
		pr_info("error %d occurred\n", err);
	printk(KERN_INFO "=================================================\n");
	return err;
}
module_init(mtd_pagetest_init);

static void __exit mtd_pagetest_exit(void)
{
	return;
}
module_exit(mtd_pagetest_exit);

MODULE_DESCRIPTION("NAND page test");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL");
