/*
 * asynchronous raid6 recovery self test
 * Copyright (c) 2009, Intel Corporation.
 *
 * based on drivers/md/raid6test/test.c:
 * 	Copyright 2002-2007 H. Peter Anvin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/async_tx.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/random.h>

#undef pr
#define pr(fmt, args...) pr_info("raid6test: " fmt, ##args)

#define NDISKS 16 /* Including P and Q */

static struct page *dataptrs[NDISKS];
static addr_conv_t addr_conv[NDISKS];
static struct page *data[NDISKS+3];
static struct page *spare;
static struct page *recovi;
static struct page *recovj;

static void callback(void *param)
{
	struct completion *cmp = param;

	complete(cmp);
}

static void makedata(int disks)
{
	int i, j;

	for (i = 0; i < disks; i++) {
		for (j = 0; j < PAGE_SIZE/sizeof(u32); j += sizeof(u32)) {
			u32 *p = page_address(data[i]) + j;

			*p = random32();
		}

		dataptrs[i] = data[i];
	}
}

static char disk_type(int d, int disks)
{
	if (d == disks - 2)
		return 'P';
	else if (d == disks - 1)
		return 'Q';
	else
		return 'D';
}

/* Recover two failed blocks. */
static void raid6_dual_recov(int disks, size_t bytes, int faila, int failb, struct page **ptrs)
{
	struct async_submit_ctl submit;
	struct completion cmp;
	struct dma_async_tx_descriptor *tx = NULL;
	enum sum_check_flags result = ~0;

	if (faila > failb)
		swap(faila, failb);

	if (failb == disks-1) {
		if (faila == disks-2) {
			/* P+Q failure.  Just rebuild the syndrome. */
			init_async_submit(&submit, 0, NULL, NULL, NULL, addr_conv);
			tx = async_gen_syndrome(ptrs, 0, disks, bytes, &submit);
		} else {
			struct page *blocks[disks];
			struct page *dest;
			int count = 0;
			int i;

			/* data+Q failure.  Reconstruct data from P,
			 * then rebuild syndrome
			 */
			for (i = disks; i-- ; ) {
				if (i == faila || i == failb)
					continue;
				blocks[count++] = ptrs[i];
			}
			dest = ptrs[faila];
			init_async_submit(&submit, ASYNC_TX_XOR_ZERO_DST, NULL,
					  NULL, NULL, addr_conv);
			tx = async_xor(dest, blocks, 0, count, bytes, &submit);

			init_async_submit(&submit, 0, tx, NULL, NULL, addr_conv);
			tx = async_gen_syndrome(ptrs, 0, disks, bytes, &submit);
		}
	} else {
		if (failb == disks-2) {
			/* data+P failure. */
			init_async_submit(&submit, 0, NULL, NULL, NULL, addr_conv);
			tx = async_raid6_datap_recov(disks, bytes, faila, ptrs, &submit);
		} else {
			/* data+data failure. */
			init_async_submit(&submit, 0, NULL, NULL, NULL, addr_conv);
			tx = async_raid6_2data_recov(disks, bytes, faila, failb, ptrs, &submit);
		}
	}
	init_completion(&cmp);
	init_async_submit(&submit, ASYNC_TX_ACK, tx, callback, &cmp, addr_conv);
	tx = async_syndrome_val(ptrs, 0, disks, bytes, &result, spare, &submit);
	async_tx_issue_pending(tx);

	if (wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000)) == 0)
		pr("%s: timeout! (faila: %d failb: %d disks: %d)\n",
		   __func__, faila, failb, disks);

	if (result != 0)
		pr("%s: validation failure! faila: %d failb: %d sum_check_flags: %x\n",
		   __func__, faila, failb, result);
}

static int test_disks(int i, int j, int disks)
{
	int erra, errb;

	memset(page_address(recovi), 0xf0, PAGE_SIZE);
	memset(page_address(recovj), 0xba, PAGE_SIZE);

	dataptrs[i] = recovi;
	dataptrs[j] = recovj;

	raid6_dual_recov(disks, PAGE_SIZE, i, j, dataptrs);

	erra = memcmp(page_address(data[i]), page_address(recovi), PAGE_SIZE);
	errb = memcmp(page_address(data[j]), page_address(recovj), PAGE_SIZE);

	pr("%s(%d, %d): faila=%3d(%c)  failb=%3d(%c)  %s\n",
	   __func__, i, j, i, disk_type(i, disks), j, disk_type(j, disks),
	   (!erra && !errb) ? "OK" : !erra ? "ERRB" : !errb ? "ERRA" : "ERRAB");

	dataptrs[i] = data[i];
	dataptrs[j] = data[j];

	return erra || errb;
}

static int test(int disks, int *tests)
{
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	struct completion cmp;
	int err = 0;
	int i, j;

	recovi = data[disks];
	recovj = data[disks+1];
	spare  = data[disks+2];

	makedata(disks);

	/* Nuke syndromes */
	memset(page_address(data[disks-2]), 0xee, PAGE_SIZE);
	memset(page_address(data[disks-1]), 0xee, PAGE_SIZE);

	/* Generate assumed good syndrome */
	init_completion(&cmp);
	init_async_submit(&submit, ASYNC_TX_ACK, NULL, callback, &cmp, addr_conv);
	tx = async_gen_syndrome(dataptrs, 0, disks, PAGE_SIZE, &submit);
	async_tx_issue_pending(tx);

	if (wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000)) == 0) {
		pr("error: initial gen_syndrome(%d) timed out\n", disks);
		return 1;
	}

	pr("testing the %d-disk case...\n", disks);
	for (i = 0; i < disks-1; i++)
		for (j = i+1; j < disks; j++) {
			(*tests)++;
			err += test_disks(i, j, disks);
		}

	return err;
}


static int raid6_test(void)
{
	int err = 0;
	int tests = 0;
	int i;

	for (i = 0; i < NDISKS+3; i++) {
		data[i] = alloc_page(GFP_KERNEL);
		if (!data[i]) {
			while (i--)
				put_page(data[i]);
			return -ENOMEM;
		}
	}

	/* the 4-disk and 5-disk cases are special for the recovery code */
	if (NDISKS > 4)
		err += test(4, &tests);
	if (NDISKS > 5)
		err += test(5, &tests);
	/* the 11 and 12 disk cases are special for ioatdma (p-disabled
	 * q-continuation without extended descriptor)
	 */
	if (NDISKS > 12) {
		err += test(11, &tests);
		err += test(12, &tests);
	}
	err += test(NDISKS, &tests);

	pr("\n");
	pr("complete (%d tests, %d failure%s)\n",
	   tests, err, err == 1 ? "" : "s");

	for (i = 0; i < NDISKS+3; i++)
		put_page(data[i]);

	return 0;
}

static void raid6_test_exit(void)
{
}

/* when compiled-in wait for drivers to load first (assumes dma drivers
 * are also compliled-in)
 */
late_initcall(raid6_test);
module_exit(raid6_test_exit);
MODULE_AUTHOR("Dan Williams <dan.j.williams@intel.com>");
MODULE_DESCRIPTION("asynchronous RAID-6 recovery self tests");
MODULE_LICENSE("GPL");
