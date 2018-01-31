/*
 * Copyright © 2012 NetCommWireless
 * Iwo Mergler <Iwo.Mergler@netcommwireless.com.au>
 *
 * Test for multi-bit error recovery on a NAND page This mostly tests the
 * ECC controller / driver.
 *
 * There are two test modes:
 *
 *	0 - artificially inserting bit errors until the ECC fails
 *	    This is the default method and fairly quick. It should
 *	    be independent of the quality of the FLASH.
 *
 *	1 - re-writing the same pattern repeatedly until the ECC fails.
 *	    This method relies on the physics of NAND FLASH to eventually
 *	    generate '0' bits if '1' has been written sufficient times.
 *	    Depending on the NAND, the first bit errors will appear after
 *	    1000 or more writes and then will usually snowball, reaching the
 *	    limits of the ECC quickly.
 *
 *	    The test stops after 10000 cycles, should your FLASH be
 *	    exceptionally good and not generate bit errors before that. Try
 *	    a different page in that case.
 *
 * Please note that neither of these tests will significantly 'use up' any
 * FLASH endurance. Only a maximum of two erase operations will be performed.
 *
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
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mtd/mtd.h>
#include <linux/err.h>
#include <linux/mtd/rawnand.h>
#include <linux/slab.h>
#include "mtd_test.h"

static int dev;
module_param(dev, int, S_IRUGO);
MODULE_PARM_DESC(dev, "MTD device number to use");

static unsigned page_offset;
module_param(page_offset, uint, S_IRUGO);
MODULE_PARM_DESC(page_offset, "Page number relative to dev start");

static unsigned seed;
module_param(seed, uint, S_IRUGO);
MODULE_PARM_DESC(seed, "Random seed");

static int mode;
module_param(mode, int, S_IRUGO);
MODULE_PARM_DESC(mode, "0=incremental errors, 1=overwrite test");

static unsigned max_overwrite = 10000;

static loff_t   offset;     /* Offset of the page we're using. */
static unsigned eraseblock; /* Eraseblock number for our page. */

/* We assume that the ECC can correct up to a certain number
 * of biterrors per subpage. */
static unsigned subsize;  /* Size of subpages */
static unsigned subcount; /* Number of subpages per page */

static struct mtd_info *mtd;   /* MTD device */

static uint8_t *wbuffer; /* One page write / compare buffer */
static uint8_t *rbuffer; /* One page read buffer */

/* 'random' bytes from known offsets */
static uint8_t hash(unsigned offset)
{
	unsigned v = offset;
	unsigned char c;
	v ^= 0x7f7edfd3;
	v = v ^ (v >> 3);
	v = v ^ (v >> 5);
	v = v ^ (v >> 13);
	c = v & 0xFF;
	/* Reverse bits of result. */
	c = (c & 0x0F) << 4 | (c & 0xF0) >> 4;
	c = (c & 0x33) << 2 | (c & 0xCC) >> 2;
	c = (c & 0x55) << 1 | (c & 0xAA) >> 1;
	return c;
}

/* Writes wbuffer to page */
static int write_page(int log)
{
	if (log)
		pr_info("write_page\n");

	return mtdtest_write(mtd, offset, mtd->writesize, wbuffer);
}

/* Re-writes the data area while leaving the OOB alone. */
static int rewrite_page(int log)
{
	int err = 0;
	struct mtd_oob_ops ops;

	if (log)
		pr_info("rewrite page\n");

	ops.mode      = MTD_OPS_RAW; /* No ECC */
	ops.len       = mtd->writesize;
	ops.retlen    = 0;
	ops.ooblen    = 0;
	ops.oobretlen = 0;
	ops.ooboffs   = 0;
	ops.datbuf    = wbuffer;
	ops.oobbuf    = NULL;

	err = mtd_write_oob(mtd, offset, &ops);
	if (err || ops.retlen != mtd->writesize) {
		pr_err("error: write_oob failed (%d)\n", err);
		if (!err)
			err = -EIO;
	}

	return err;
}

/* Reads page into rbuffer. Returns number of corrected bit errors (>=0)
 * or error (<0) */
static int read_page(int log)
{
	int err = 0;
	size_t read;
	struct mtd_ecc_stats oldstats;

	if (log)
		pr_info("read_page\n");

	/* Saving last mtd stats */
	memcpy(&oldstats, &mtd->ecc_stats, sizeof(oldstats));

	err = mtd_read(mtd, offset, mtd->writesize, &read, rbuffer);
	if (!err || err == -EUCLEAN)
		err = mtd->ecc_stats.corrected - oldstats.corrected;

	if (err < 0 || read != mtd->writesize) {
		pr_err("error: read failed at %#llx\n", (long long)offset);
		if (err >= 0)
			err = -EIO;
	}

	return err;
}

/* Verifies rbuffer against random sequence */
static int verify_page(int log)
{
	unsigned i, errs = 0;

	if (log)
		pr_info("verify_page\n");

	for (i = 0; i < mtd->writesize; i++) {
		if (rbuffer[i] != hash(i+seed)) {
			pr_err("Error: page offset %u, expected %02x, got %02x\n",
				i, hash(i+seed), rbuffer[i]);
			errs++;
		}
	}

	if (errs)
		return -EIO;
	else
		return 0;
}

#define CBIT(v, n) ((v) & (1 << (n)))
#define BCLR(v, n) ((v) = (v) & ~(1 << (n)))

/* Finds the first '1' bit in wbuffer starting at offset 'byte'
 * and sets it to '0'. */
static int insert_biterror(unsigned byte)
{
	int bit;

	while (byte < mtd->writesize) {
		for (bit = 7; bit >= 0; bit--) {
			if (CBIT(wbuffer[byte], bit)) {
				BCLR(wbuffer[byte], bit);
				pr_info("Inserted biterror @ %u/%u\n", byte, bit);
				return 0;
			}
		}
		byte++;
	}
	pr_err("biterror: Failed to find a '1' bit\n");
	return -EIO;
}

/* Writes 'random' data to page and then introduces deliberate bit
 * errors into the page, while verifying each step. */
static int incremental_errors_test(void)
{
	int err = 0;
	unsigned i;
	unsigned errs_per_subpage = 0;

	pr_info("incremental biterrors test\n");

	for (i = 0; i < mtd->writesize; i++)
		wbuffer[i] = hash(i+seed);

	err = write_page(1);
	if (err)
		goto exit;

	while (1) {

		err = rewrite_page(1);
		if (err)
			goto exit;

		err = read_page(1);
		if (err > 0)
			pr_info("Read reported %d corrected bit errors\n", err);
		if (err < 0) {
			pr_err("After %d biterrors per subpage, read reported error %d\n",
				errs_per_subpage, err);
			err = 0;
			goto exit;
		}

		err = verify_page(1);
		if (err) {
			pr_err("ECC failure, read data is incorrect despite read success\n");
			goto exit;
		}

		pr_info("Successfully corrected %d bit errors per subpage\n",
			errs_per_subpage);

		for (i = 0; i < subcount; i++) {
			err = insert_biterror(i * subsize);
			if (err < 0)
				goto exit;
		}
		errs_per_subpage++;
	}

exit:
	return err;
}


/* Writes 'random' data to page and then re-writes that same data repeatedly.
   This eventually develops bit errors (bits written as '1' will slowly become
   '0'), which are corrected as far as the ECC is capable of. */
static int overwrite_test(void)
{
	int err = 0;
	unsigned i;
	unsigned max_corrected = 0;
	unsigned opno = 0;
	/* We don't expect more than this many correctable bit errors per
	 * page. */
	#define MAXBITS 512
	static unsigned bitstats[MAXBITS]; /* bit error histogram. */

	memset(bitstats, 0, sizeof(bitstats));

	pr_info("overwrite biterrors test\n");

	for (i = 0; i < mtd->writesize; i++)
		wbuffer[i] = hash(i+seed);

	err = write_page(1);
	if (err)
		goto exit;

	while (opno < max_overwrite) {

		err = write_page(0);
		if (err)
			break;

		err = read_page(0);
		if (err >= 0) {
			if (err >= MAXBITS) {
				pr_info("Implausible number of bit errors corrected\n");
				err = -EIO;
				break;
			}
			bitstats[err]++;
			if (err > max_corrected) {
				max_corrected = err;
				pr_info("Read reported %d corrected bit errors\n",
					err);
			}
		} else { /* err < 0 */
			pr_info("Read reported error %d\n", err);
			err = 0;
			break;
		}

		err = verify_page(0);
		if (err) {
			bitstats[max_corrected] = opno;
			pr_info("ECC failure, read data is incorrect despite read success\n");
			break;
		}

		err = mtdtest_relax();
		if (err)
			break;

		opno++;
	}

	/* At this point bitstats[0] contains the number of ops with no bit
	 * errors, bitstats[1] the number of ops with 1 bit error, etc. */
	pr_info("Bit error histogram (%d operations total):\n", opno);
	for (i = 0; i < max_corrected; i++)
		pr_info("Page reads with %3d corrected bit errors: %d\n",
			i, bitstats[i]);

exit:
	return err;
}

static int __init mtd_nandbiterrs_init(void)
{
	int err = 0;

	printk("\n");
	printk(KERN_INFO "==================================================\n");
	pr_info("MTD device: %d\n", dev);

	mtd = get_mtd_device(NULL, dev);
	if (IS_ERR(mtd)) {
		err = PTR_ERR(mtd);
		pr_err("error: cannot get MTD device\n");
		goto exit_mtddev;
	}

	if (!mtd_type_is_nand(mtd)) {
		pr_info("this test requires NAND flash\n");
		err = -ENODEV;
		goto exit_nand;
	}

	pr_info("MTD device size %llu, eraseblock=%u, page=%u, oob=%u\n",
		(unsigned long long)mtd->size, mtd->erasesize,
		mtd->writesize, mtd->oobsize);

	subsize  = mtd->writesize >> mtd->subpage_sft;
	subcount = mtd->writesize / subsize;

	pr_info("Device uses %d subpages of %d bytes\n", subcount, subsize);

	offset     = (loff_t)page_offset * mtd->writesize;
	eraseblock = mtd_div_by_eb(offset, mtd);

	pr_info("Using page=%u, offset=%llu, eraseblock=%u\n",
		page_offset, offset, eraseblock);

	wbuffer = kmalloc(mtd->writesize, GFP_KERNEL);
	if (!wbuffer) {
		err = -ENOMEM;
		goto exit_wbuffer;
	}

	rbuffer = kmalloc(mtd->writesize, GFP_KERNEL);
	if (!rbuffer) {
		err = -ENOMEM;
		goto exit_rbuffer;
	}

	err = mtdtest_erase_eraseblock(mtd, eraseblock);
	if (err)
		goto exit_error;

	if (mode == 0)
		err = incremental_errors_test();
	else
		err = overwrite_test();

	if (err)
		goto exit_error;

	/* We leave the block un-erased in case of test failure. */
	err = mtdtest_erase_eraseblock(mtd, eraseblock);
	if (err)
		goto exit_error;

	err = -EIO;
	pr_info("finished successfully.\n");
	printk(KERN_INFO "==================================================\n");

exit_error:
	kfree(rbuffer);
exit_rbuffer:
	kfree(wbuffer);
exit_wbuffer:
	/* Nothing */
exit_nand:
	put_mtd_device(mtd);
exit_mtddev:
	return err;
}

static void __exit mtd_nandbiterrs_exit(void)
{
	return;
}

module_init(mtd_nandbiterrs_init);
module_exit(mtd_nandbiterrs_exit);

MODULE_DESCRIPTION("NAND bit error recovery test");
MODULE_AUTHOR("Iwo Mergler");
MODULE_LICENSE("GPL");
