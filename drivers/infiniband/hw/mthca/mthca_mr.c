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
#include "mthca_memfree.h"

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

static u32 mthca_buddy_alloc(struct mthca_buddy *buddy, int order)
{
	int o;
	int m;
	u32 seg;

	spin_lock(&buddy->lock);

	for (o = order; o <= buddy->max_order; ++o) {
		m = 1 << (buddy->max_order - o);
		seg = find_first_bit(buddy->bits[o], m);
		if (seg < m)
			goto found;
	}

	spin_unlock(&buddy->lock);
	return -1;

 found:
	clear_bit(seg, buddy->bits[o]);

	while (o > order) {
		--o;
		seg <<= 1;
		set_bit(seg ^ 1, buddy->bits[o]);
	}

	spin_unlock(&buddy->lock);

	seg <<= order;

	return seg;
}

static void mthca_buddy_free(struct mthca_buddy *buddy, u32 seg, int order)
{
	seg >>= order;

	spin_lock(&buddy->lock);

	while (test_bit(seg ^ 1, buddy->bits[order])) {
		clear_bit(seg ^ 1, buddy->bits[order]);
		seg >>= 1;
		++order;
	}

	set_bit(seg, buddy->bits[order]);

	spin_unlock(&buddy->lock);
}

static int __devinit mthca_buddy_init(struct mthca_buddy *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);

	buddy->bits = kmalloc((buddy->max_order + 1) * sizeof (long *),
			      GFP_KERNEL);
	if (!buddy->bits)
		goto err_out;

	memset(buddy->bits, 0, (buddy->max_order + 1) * sizeof (long *));

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kmalloc(s * sizeof (long), GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free;
		bitmap_zero(buddy->bits[i],
			    1 << (buddy->max_order - i));
	}

	set_bit(0, buddy->bits[buddy->max_order]);

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);

err_out:
	return -ENOMEM;
}

static void __devexit mthca_buddy_cleanup(struct mthca_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

	kfree(buddy->bits);
}

static u32 mthca_alloc_mtt(struct mthca_dev *dev, int order,
			   struct mthca_buddy *buddy)
{
	u32 seg = mthca_buddy_alloc(buddy, order);

	if (seg == -1)
		return -1;

	if (dev->hca_type == ARBEL_NATIVE)
		if (mthca_table_get_range(dev, dev->mr_table.mtt_table, seg,
					  seg + (1 << order) - 1)) {
			mthca_buddy_free(buddy, seg, order);
			seg = -1;
		}

	return seg;
}

static void mthca_free_mtt(struct mthca_dev *dev, u32 seg, int order,
			   struct mthca_buddy* buddy)
{
	mthca_buddy_free(buddy, seg, order);

	if (dev->hca_type == ARBEL_NATIVE)
		mthca_table_put_range(dev, dev->mr_table.mtt_table, seg,
				      seg + (1 << order) - 1);
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
	void *mailbox = NULL;
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

	if (dev->hca_type == ARBEL_NATIVE) {
		err = mthca_table_get(dev, dev->mr_table.mpt_table, key);
		if (err)
			goto err_out_mpt_free;
	}

	mailbox = kmalloc(sizeof *mpt_entry + MTHCA_CMD_MAILBOX_EXTRA,
			  GFP_KERNEL);
	if (!mailbox) {
		err = -ENOMEM;
		goto err_out_table;
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
	if (err) {
		mthca_warn(dev, "SW2HW_MPT failed (%d)\n", err);
		goto err_out_table;
	} else if (status) {
		mthca_warn(dev, "SW2HW_MPT returned status 0x%02x\n",
			   status);
		err = -EINVAL;
		goto err_out_table;
	}

	kfree(mailbox);
	return err;

err_out_table:
	if (dev->hca_type == ARBEL_NATIVE)
		mthca_table_put(dev, dev->mr_table.mpt_table, key);

err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, key);
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

	if (dev->hca_type == ARBEL_NATIVE) {
		err = mthca_table_get(dev, dev->mr_table.mpt_table, key);
		if (err)
			goto err_out_mpt_free;
	}

	for (i = MTHCA_MTT_SEG_SIZE / 8, mr->order = 0;
	     i < list_len;
	     i <<= 1, ++mr->order)
		; /* nothing */

	mr->first_seg = mthca_alloc_mtt(dev, mr->order,
				       	&dev->mr_table.mtt_buddy);
	if (mr->first_seg == -1)
		goto err_out_table;

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
				   mr->first_seg * MTHCA_MTT_SEG_SIZE);
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
					   mr->first_seg * MTHCA_MTT_SEG_SIZE);

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
	mthca_free_mtt(dev, mr->first_seg, mr->order, &dev->mr_table.mtt_buddy);

err_out_table:
	if (dev->hca_type == ARBEL_NATIVE)
		mthca_table_put(dev, dev->mr_table.mpt_table, key);

err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, key);
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
		mthca_free_mtt(dev, mr->first_seg, mr->order, &dev->mr_table.mtt_buddy);

	if (dev->hca_type == ARBEL_NATIVE)
		mthca_table_put(dev, dev->mr_table.mpt_table,
				key_to_hw_index(dev, mr->ibmr.lkey));
	mthca_free(&dev->mr_table.mpt_alloc, key_to_hw_index(dev, mr->ibmr.lkey));
}

int __devinit mthca_init_mr_table(struct mthca_dev *dev)
{
	int err;

	err = mthca_alloc_init(&dev->mr_table.mpt_alloc,
			       dev->limits.num_mpts,
			       ~0, dev->limits.reserved_mrws);
	if (err)
		return err;

	err = mthca_buddy_init(&dev->mr_table.mtt_buddy,
			       fls(dev->limits.num_mtt_segs - 1));
	if (err)
		goto err_mtt_buddy;

	if (dev->limits.reserved_mtts) {
		if (mthca_alloc_mtt(dev, fls(dev->limits.reserved_mtts - 1),
				    &dev->mr_table.mtt_buddy) == -1) {
			mthca_warn(dev, "MTT table of order %d is too small.\n",
				  dev->mr_table.mtt_buddy.max_order);
			err = -ENOMEM;
			goto err_mtt_buddy;
		}
	}

	return 0;

err_mtt_buddy:
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);

	return err;
}

void __devexit mthca_cleanup_mr_table(struct mthca_dev *dev)
{
	/* XXX check if any MRs are still allocated? */
	mthca_buddy_cleanup(&dev->mr_table.mtt_buddy);
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);
}
