/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_mr.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"

/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
struct mthca_mpt_entry {
	u32 flags;
	u32 page_size;
	u32 key;
	u32 pd;
	u64 start;
	u64 length;
	u32 lkey;
	u32 window_count;
	u32 window_count_limit;
	u64 mtt_seg;
	u32 mtt_sz;		/* Arbel only */
	u32 reserved[2];
} __attribute__((packed));

#define MTHCA_MPT_FLAG_SW_OWNS       (0xfUL << 28)
#define MTHCA_MPT_FLAG_MIO           (1 << 17)
#define MTHCA_MPT_FLAG_BIND_ENABLE   (1 << 15)
#define MTHCA_MPT_FLAG_PHYSICAL      (1 <<  9)
#define MTHCA_MPT_FLAG_REGION        (1 <<  8)

#define MTHCA_MTT_FLAG_PRESENT       1

/*
 * Buddy allocator for MTT segments (currently not very efficient
 * since it doesn't keep a free list and just searches linearly
 * through the bitmaps)
 */

static u32 mthca_alloc_mtt(struct mthca_dev *dev, int order)
{
	int o;
	int m;
	u32 seg;

	spin_lock(&dev->mr_table.mpt_alloc.lock);

	for (o = order; o <= dev->mr_table.max_mtt_order; ++o) {
		m = 1 << (dev->mr_table.max_mtt_order - o);
		seg = find_first_bit(dev->mr_table.mtt_buddy[o], m);
		if (seg < m)
			goto found;
	}

	spin_unlock(&dev->mr_table.mpt_alloc.lock);
	return -1;

 found:
	clear_bit(seg, dev->mr_table.mtt_buddy[o]);

	while (o > order) {
		--o;
		seg <<= 1;
		set_bit(seg ^ 1, dev->mr_table.mtt_buddy[o]);
	}

	spin_unlock(&dev->mr_table.mpt_alloc.lock);

	seg <<= order;

	return seg;
}

static void mthca_free_mtt(struct mthca_dev *dev, u32 seg, int order)
{
	seg >>= order;

	spin_lock(&dev->mr_table.mpt_alloc.lock);

	while (test_bit(seg ^ 1, dev->mr_table.mtt_buddy[order])) {
		clear_bit(seg ^ 1, dev->mr_table.mtt_buddy[order]);
		seg >>= 1;
		++order;
	}

	set_bit(seg, dev->mr_table.mtt_buddy[order]);

	spin_unlock(&dev->mr_table.mpt_alloc.lock);
}

static inline u32 hw_index_to_key(struct mthca_dev *dev, u32 ind)
{
	if (dev->hca_type == ARBEL_NATIVE)
		return (ind >> 24) | (ind << 8);
	else
		return ind;
}

static inline u32 key_to_hw_index(struct mthca_dev *dev, u32 key)
{
	if (dev->hca_type == ARBEL_NATIVE)
		return (key << 24) | (key >> 8);
	else
		return key;
}

int mthca_mr_alloc_notrans(struct mthca_dev *dev, u32 pd,
			   u32 access, struct mthca_mr *mr)
{
	void *mailbox;
	struct mthca_mpt_entry *mpt_entry;
	u32 key;
	int err;
	u8 status;

	might_sleep();

	mr->order = -1;
	key = mthca_alloc(&dev->mr_table.mpt_alloc);
	if (key == -1)
		return -ENOMEM;
	mr->ibmr.rkey = mr->ibmr.lkey = hw_index_to_key(dev, key);

	mailbox = kmalloc(sizeof *mpt_entry + MTHCA_CMD_MAILBOX_EXTRA,
			  GFP_KERNEL);
	if (!mailbox) {
		mthca_free(&dev->mr_table.mpt_alloc, mr->ibmr.lkey);
		return -ENOMEM;
	}
	mpt_entry = MAILBOX_ALIGN(mailbox);

	mpt_entry->flags = cpu_to_be32(MTHCA_MPT_FLAG_SW_OWNS     |
				       MTHCA_MPT_FLAG_MIO         |
				       MTHCA_MPT_FLAG_PHYSICAL    |
				       MTHCA_MPT_FLAG_REGION      |
				       access);
	mpt_entry->page_size = 0;
	mpt_entry->key       = cpu_to_be32(key);
	mpt_entry->pd        = cpu_to_be32(pd);
	mpt_entry->start     = 0;
	mpt_entry->length    = ~0ULL;

	memset(&mpt_entry->lkey, 0,
	       sizeof *mpt_entry - offsetof(struct mthca_mpt_entry, lkey));

	err = mthca_SW2HW_MPT(dev, mpt_entry,
			      key & (dev->limits.num_mpts - 1),
			      &status);
	if (err)
		mthca_warn(dev, "SW2HW_MPT failed (%d)\n", err);
	else if (status) {
		mthca_warn(dev, "SW2HW_MPT returned status 0x%02x\n",
			   status);
		err = -EINVAL;
	}

	kfree(mailbox);
	return err;
}

int mthca_mr_alloc_phys(struct mthca_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			u32 access, struct mthca_mr *mr)
{
	void *mailbox;
	u64 *mtt_entry;
	struct mthca_mpt_entry *mpt_entry;
	u32 key;
	int err = -ENOMEM;
	u8 status;
	int i;

	might_sleep();
	WARN_ON(buffer_size_shift >= 32);

	key = mthca_alloc(&dev->mr_table.mpt_alloc);
	if (key == -1)
		return -ENOMEM;
	mr->ibmr.rkey = mr->ibmr.lkey = hw_index_to_key(dev, key);

	for (i = dev->limits.mtt_seg_size / 8, mr->order = 0;
	     i < list_len;
	     i <<= 1, ++mr->order)
		; /* nothing */

	mr->first_seg = mthca_alloc_mtt(dev, mr->order);
	if (mr->first_seg == -1)
		goto err_out_mpt_free;

	/*
	 * If list_len is odd, we add one more dummy entry for
	 * firmware efficiency.
	 */
	mailbox = kmalloc(max(sizeof *mpt_entry,
			      (size_t) 8 * (list_len + (list_len & 1) + 2)) +
			  MTHCA_CMD_MAILBOX_EXTRA,
			  GFP_KERNEL);
	if (!mailbox)
		goto err_out_free_mtt;

	mtt_entry = MAILBOX_ALIGN(mailbox);

	mtt_entry[0] = cpu_to_be64(dev->mr_table.mtt_base +
				   mr->first_seg * dev->limits.mtt_seg_size);
	mtt_entry[1] = 0;
	for (i = 0; i < list_len; ++i)
		mtt_entry[i + 2] = cpu_to_be64(buffer_list[i] |
					       MTHCA_MTT_FLAG_PRESENT);
	if (list_len & 1) {
		mtt_entry[i + 2] = 0;
		++list_len;
	}

	if (0) {
		mthca_dbg(dev, "Dumping MPT entry\n");
		for (i = 0; i < list_len + 2; ++i)
			printk(KERN_ERR "[%2d] %016llx\n",
			       i, (unsigned long long) be64_to_cpu(mtt_entry[i]));
	}

	err = mthca_WRITE_MTT(dev, mtt_entry, list_len, &status);
	if (err) {
		mthca_warn(dev, "WRITE_MTT failed (%d)\n", err);
		goto err_out_mailbox_free;
	}
	if (status) {
		mthca_warn(dev, "WRITE_MTT returned status 0x%02x\n",
			   status);
		err = -EINVAL;
		goto err_out_mailbox_free;
	}

	mpt_entry = MAILBOX_ALIGN(mailbox);

	mpt_entry->flags = cpu_to_be32(MTHCA_MPT_FLAG_SW_OWNS     |
				       MTHCA_MPT_FLAG_MIO         |
				       MTHCA_MPT_FLAG_REGION      |
				       access);

	mpt_entry->page_size = cpu_to_be32(buffer_size_shift - 12);
	mpt_entry->key       = cpu_to_be32(key);
	mpt_entry->pd        = cpu_to_be32(pd);
	mpt_entry->start     = cpu_to_be64(iova);
	mpt_entry->length    = cpu_to_be64(total_size);
	memset(&mpt_entry->lkey, 0,
	       sizeof *mpt_entry - offsetof(struct mthca_mpt_entry, lkey));
	mpt_entry->mtt_seg   = cpu_to_be64(dev->mr_table.mtt_base +
					   mr->first_seg * dev->limits.mtt_seg_size);

	if (0) {
		mthca_dbg(dev, "Dumping MPT entry %08x:\n", mr->ibmr.lkey);
		for (i = 0; i < sizeof (struct mthca_mpt_entry) / 4; ++i) {
			if (i % 4 == 0)
				printk("[%02x] ", i * 4);
			printk(" %08x", be32_to_cpu(((u32 *) mpt_entry)[i]));
			if ((i + 1) % 4 == 0)
				printk("\n");
		}
	}

	err = mthca_SW2HW_MPT(dev, mpt_entry,
			      key & (dev->limits.num_mpts - 1),
			      &status);
	if (err)
		mthca_warn(dev, "SW2HW_MPT failed (%d)\n", err);
	else if (status) {
		mthca_warn(dev, "SW2HW_MPT returned status 0x%02x\n",
			   status);
		err = -EINVAL;
	}

	kfree(mailbox);
	return err;

 err_out_mailbox_free:
	kfree(mailbox);

 err_out_free_mtt:
	mthca_free_mtt(dev, mr->first_seg, mr->order);

 err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, mr->ibmr.lkey);
	return err;
}

void mthca_free_mr(struct mthca_dev *dev, struct mthca_mr *mr)
{
	int err;
	u8 status;

	might_sleep();

	err = mthca_HW2SW_MPT(dev, NULL,
			      key_to_hw_index(dev, mr->ibmr.lkey) &
			      (dev->limits.num_mpts - 1),
			      &status);
	if (err)
		mthca_warn(dev, "HW2SW_MPT failed (%d)\n", err);
	else if (status)
		mthca_warn(dev, "HW2SW_MPT returned status 0x%02x\n",
			   status);

	if (mr->order >= 0)
		mthca_free_mtt(dev, mr->first_seg, mr->order);

	mthca_free(&dev->mr_table.mpt_alloc, key_to_hw_index(dev, mr->ibmr.lkey));
}

int __devinit mthca_init_mr_table(struct mthca_dev *dev)
{
	int err;
	int i, s;

	err = mthca_alloc_init(&dev->mr_table.mpt_alloc,
			       dev->limits.num_mpts,
			       ~0, dev->limits.reserved_mrws);
	if (err)
		return err;

	err = -ENOMEM;

	for (i = 1, dev->mr_table.max_mtt_order = 0;
	     i < dev->limits.num_mtt_segs;
	     i <<= 1, ++dev->mr_table.max_mtt_order)
		; /* nothing */

	dev->mr_table.mtt_buddy = kmalloc((dev->mr_table.max_mtt_order + 1) *
					  sizeof (long *),
					  GFP_KERNEL);
	if (!dev->mr_table.mtt_buddy)
		goto err_out;

	for (i = 0; i <= dev->mr_table.max_mtt_order; ++i)
		dev->mr_table.mtt_buddy[i] = NULL;

	for (i = 0; i <= dev->mr_table.max_mtt_order; ++i) {
		s = BITS_TO_LONGS(1 << (dev->mr_table.max_mtt_order - i));
		dev->mr_table.mtt_buddy[i] = kmalloc(s * sizeof (long),
						     GFP_KERNEL);
		if (!dev->mr_table.mtt_buddy[i])
			goto err_out_free;
		bitmap_zero(dev->mr_table.mtt_buddy[i],
			    1 << (dev->mr_table.max_mtt_order - i));
	}

	set_bit(0, dev->mr_table.mtt_buddy[dev->mr_table.max_mtt_order]);

	for (i = 0; i < dev->mr_table.max_mtt_order; ++i)
		if (1 << i >= dev->limits.reserved_mtts)
			break;

	if (i == dev->mr_table.max_mtt_order) {
		mthca_err(dev, "MTT table of order %d is "
			  "too small.\n", i);
		goto err_out_free;
	}

	(void) mthca_alloc_mtt(dev, i);

	return 0;

 err_out_free:
	for (i = 0; i <= dev->mr_table.max_mtt_order; ++i)
		kfree(dev->mr_table.mtt_buddy[i]);

 err_out:
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);

	return err;
}

void __devexit mthca_cleanup_mr_table(struct mthca_dev *dev)
{
	int i;

	/* XXX check if any MRs are still allocated? */
	for (i = 0; i <= dev->mr_table.max_mtt_order; ++i)
		kfree(dev->mr_table.mtt_buddy[i]);
	kfree(dev->mr_table.mtt_buddy);
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);
}
