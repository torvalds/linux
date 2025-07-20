/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 */

#include <linux/slab.h>
#include <linux/errno.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"
#include "mthca_memfree.h"

struct mthca_mtt {
	struct mthca_buddy *buddy;
	int                 order;
	u32                 first_seg;
};

/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
struct mthca_mpt_entry {
	__be32 flags;
	__be32 page_size;
	__be32 key;
	__be32 pd;
	__be64 start;
	__be64 length;
	__be32 lkey;
	__be32 window_count;
	__be32 window_count_limit;
	__be64 mtt_seg;
	__be32 mtt_sz;		/* Arbel only */
	u32    reserved[2];
} __packed;

#define MTHCA_MPT_FLAG_SW_OWNS       (0xfUL << 28)
#define MTHCA_MPT_FLAG_MIO           (1 << 17)
#define MTHCA_MPT_FLAG_BIND_ENABLE   (1 << 15)
#define MTHCA_MPT_FLAG_PHYSICAL      (1 <<  9)
#define MTHCA_MPT_FLAG_REGION        (1 <<  8)

#define MTHCA_MTT_FLAG_PRESENT       1

#define MTHCA_MPT_STATUS_SW 0xF0
#define MTHCA_MPT_STATUS_HW 0x00

#define SINAI_FMR_KEY_INC 0x1000000

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

	for (o = order; o <= buddy->max_order; ++o)
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			seg = find_first_bit(buddy->bits[o], m);
			if (seg < m)
				goto found;
		}

	spin_unlock(&buddy->lock);
	return -1;

 found:
	__clear_bit(seg, buddy->bits[o]);
	--buddy->num_free[o];

	while (o > order) {
		--o;
		seg <<= 1;
		__set_bit(seg ^ 1, buddy->bits[o]);
		++buddy->num_free[o];
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
		__clear_bit(seg ^ 1, buddy->bits[order]);
		--buddy->num_free[order];
		seg >>= 1;
		++order;
	}

	__set_bit(seg, buddy->bits[order]);
	++buddy->num_free[order];

	spin_unlock(&buddy->lock);
}

static int mthca_buddy_init(struct mthca_buddy *buddy, int max_order)
{
	int i;

	buddy->max_order = max_order;
	spin_lock_init(&buddy->lock);

	buddy->bits = kcalloc(buddy->max_order + 1, sizeof(*buddy->bits),
			      GFP_KERNEL);
	buddy->num_free = kcalloc((buddy->max_order + 1), sizeof *buddy->num_free,
				  GFP_KERNEL);
	if (!buddy->bits || !buddy->num_free)
		goto err_out;

	for (i = 0; i <= buddy->max_order; ++i) {
		buddy->bits[i] = bitmap_zalloc(1 << (buddy->max_order - i),
					       GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free;
	}

	__set_bit(0, buddy->bits[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free:
	for (i = 0; i <= buddy->max_order; ++i)
		bitmap_free(buddy->bits[i]);

err_out:
	kfree(buddy->bits);
	kfree(buddy->num_free);

	return -ENOMEM;
}

static void mthca_buddy_cleanup(struct mthca_buddy *buddy)
{
	int i;

	for (i = 0; i <= buddy->max_order; ++i)
		bitmap_free(buddy->bits[i]);

	kfree(buddy->bits);
	kfree(buddy->num_free);
}

static u32 mthca_alloc_mtt_range(struct mthca_dev *dev, int order,
				 struct mthca_buddy *buddy)
{
	u32 seg = mthca_buddy_alloc(buddy, order);

	if (seg == -1)
		return -1;

	if (mthca_is_memfree(dev))
		if (mthca_table_get_range(dev, dev->mr_table.mtt_table, seg,
					  seg + (1 << order) - 1)) {
			mthca_buddy_free(buddy, seg, order);
			seg = -1;
		}

	return seg;
}

static struct mthca_mtt *__mthca_alloc_mtt(struct mthca_dev *dev, int size,
					   struct mthca_buddy *buddy)
{
	struct mthca_mtt *mtt;
	int i;

	if (size <= 0)
		return ERR_PTR(-EINVAL);

	mtt = kmalloc(sizeof *mtt, GFP_KERNEL);
	if (!mtt)
		return ERR_PTR(-ENOMEM);

	mtt->buddy = buddy;
	mtt->order = 0;
	for (i = dev->limits.mtt_seg_size / 8; i < size; i <<= 1)
		++mtt->order;

	mtt->first_seg = mthca_alloc_mtt_range(dev, mtt->order, buddy);
	if (mtt->first_seg == -1) {
		kfree(mtt);
		return ERR_PTR(-ENOMEM);
	}

	return mtt;
}

struct mthca_mtt *mthca_alloc_mtt(struct mthca_dev *dev, int size)
{
	return __mthca_alloc_mtt(dev, size, &dev->mr_table.mtt_buddy);
}

void mthca_free_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt)
{
	if (!mtt)
		return;

	mthca_buddy_free(mtt->buddy, mtt->first_seg, mtt->order);

	mthca_table_put_range(dev, dev->mr_table.mtt_table,
			      mtt->first_seg,
			      mtt->first_seg + (1 << mtt->order) - 1);

	kfree(mtt);
}

static int __mthca_write_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt,
			     int start_index, u64 *buffer_list, int list_len)
{
	struct mthca_mailbox *mailbox;
	__be64 *mtt_entry;
	int err = 0;
	int i;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mtt_entry = mailbox->buf;

	while (list_len > 0) {
		mtt_entry[0] = cpu_to_be64(dev->mr_table.mtt_base +
					   mtt->first_seg * dev->limits.mtt_seg_size +
					   start_index * 8);
		mtt_entry[1] = 0;
		for (i = 0; i < list_len && i < MTHCA_MAILBOX_SIZE / 8 - 2; ++i)
			mtt_entry[i + 2] = cpu_to_be64(buffer_list[i] |
						       MTHCA_MTT_FLAG_PRESENT);

		/*
		 * If we have an odd number of entries to write, add
		 * one more dummy entry for firmware efficiency.
		 */
		if (i & 1)
			mtt_entry[i + 2] = 0;

		err = mthca_WRITE_MTT(dev, mailbox, (i + 1) & ~1);
		if (err) {
			mthca_warn(dev, "WRITE_MTT failed (%d)\n", err);
			goto out;
		}

		list_len    -= i;
		start_index += i;
		buffer_list += i;
	}

out:
	mthca_free_mailbox(dev, mailbox);
	return err;
}

int mthca_write_mtt_size(struct mthca_dev *dev)
{
	if (dev->mr_table.fmr_mtt_buddy != &dev->mr_table.mtt_buddy ||
	    !(dev->mthca_flags & MTHCA_FLAG_FMR))
		/*
		 * Be friendly to WRITE_MTT command
		 * and leave two empty slots for the
		 * index and reserved fields of the
		 * mailbox.
		 */
		return PAGE_SIZE / sizeof (u64) - 2;

	/* For Arbel, all MTTs must fit in the same page. */
	return mthca_is_memfree(dev) ? (PAGE_SIZE / sizeof (u64)) : 0x7ffffff;
}

static void mthca_tavor_write_mtt_seg(struct mthca_dev *dev,
				      struct mthca_mtt *mtt, int start_index,
				      u64 *buffer_list, int list_len)
{
	u64 __iomem *mtts;
	int i;

	mtts = dev->mr_table.tavor_fmr.mtt_base + mtt->first_seg * dev->limits.mtt_seg_size +
		start_index * sizeof (u64);
	for (i = 0; i < list_len; ++i)
		mthca_write64_raw(cpu_to_be64(buffer_list[i] | MTHCA_MTT_FLAG_PRESENT),
				  mtts + i);
}

static void mthca_arbel_write_mtt_seg(struct mthca_dev *dev,
				      struct mthca_mtt *mtt, int start_index,
				      u64 *buffer_list, int list_len)
{
	__be64 *mtts;
	dma_addr_t dma_handle;
	int i;
	int s = start_index * sizeof (u64);

	/* For Arbel, all MTTs must fit in the same page. */
	BUG_ON(s / PAGE_SIZE != (s + list_len * sizeof(u64) - 1) / PAGE_SIZE);
	/* Require full segments */
	BUG_ON(s % dev->limits.mtt_seg_size);

	mtts = mthca_table_find(dev->mr_table.mtt_table, mtt->first_seg +
				s / dev->limits.mtt_seg_size, &dma_handle);

	BUG_ON(!mtts);

	dma_sync_single_for_cpu(&dev->pdev->dev, dma_handle,
				list_len * sizeof (u64), DMA_TO_DEVICE);

	for (i = 0; i < list_len; ++i)
		mtts[i] = cpu_to_be64(buffer_list[i] | MTHCA_MTT_FLAG_PRESENT);

	dma_sync_single_for_device(&dev->pdev->dev, dma_handle,
				   list_len * sizeof (u64), DMA_TO_DEVICE);
}

int mthca_write_mtt(struct mthca_dev *dev, struct mthca_mtt *mtt,
		    int start_index, u64 *buffer_list, int list_len)
{
	int size = mthca_write_mtt_size(dev);
	int chunk;

	if (dev->mr_table.fmr_mtt_buddy != &dev->mr_table.mtt_buddy ||
	    !(dev->mthca_flags & MTHCA_FLAG_FMR))
		return __mthca_write_mtt(dev, mtt, start_index, buffer_list, list_len);

	while (list_len > 0) {
		chunk = min(size, list_len);
		if (mthca_is_memfree(dev))
			mthca_arbel_write_mtt_seg(dev, mtt, start_index,
						  buffer_list, chunk);
		else
			mthca_tavor_write_mtt_seg(dev, mtt, start_index,
						  buffer_list, chunk);

		list_len    -= chunk;
		start_index += chunk;
		buffer_list += chunk;
	}

	return 0;
}

static inline u32 tavor_hw_index_to_key(u32 ind)
{
	return ind;
}

static inline u32 tavor_key_to_hw_index(u32 key)
{
	return key;
}

static inline u32 arbel_hw_index_to_key(u32 ind)
{
	return (ind >> 24) | (ind << 8);
}

static inline u32 arbel_key_to_hw_index(u32 key)
{
	return (key << 24) | (key >> 8);
}

static inline u32 hw_index_to_key(struct mthca_dev *dev, u32 ind)
{
	if (mthca_is_memfree(dev))
		return arbel_hw_index_to_key(ind);
	else
		return tavor_hw_index_to_key(ind);
}

static inline u32 key_to_hw_index(struct mthca_dev *dev, u32 key)
{
	if (mthca_is_memfree(dev))
		return arbel_key_to_hw_index(key);
	else
		return tavor_key_to_hw_index(key);
}

static inline u32 adjust_key(struct mthca_dev *dev, u32 key)
{
	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		return ((key << 20) & 0x800000) | (key & 0x7fffff);
	else
		return key;
}

int mthca_mr_alloc(struct mthca_dev *dev, u32 pd, int buffer_size_shift,
		   u64 iova, u64 total_size, u32 access, struct mthca_mr *mr)
{
	struct mthca_mailbox *mailbox;
	struct mthca_mpt_entry *mpt_entry;
	u32 key;
	int i;
	int err;

	WARN_ON(buffer_size_shift >= 32);

	key = mthca_alloc(&dev->mr_table.mpt_alloc);
	if (key == -1)
		return -ENOMEM;
	key = adjust_key(dev, key);
	mr->ibmr.rkey = mr->ibmr.lkey = hw_index_to_key(dev, key);

	if (mthca_is_memfree(dev)) {
		err = mthca_table_get(dev, dev->mr_table.mpt_table, key);
		if (err)
			goto err_out_mpt_free;
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_out_table;
	}
	mpt_entry = mailbox->buf;

	mpt_entry->flags = cpu_to_be32(MTHCA_MPT_FLAG_SW_OWNS     |
				       MTHCA_MPT_FLAG_MIO         |
				       MTHCA_MPT_FLAG_REGION      |
				       access);
	if (!mr->mtt)
		mpt_entry->flags |= cpu_to_be32(MTHCA_MPT_FLAG_PHYSICAL);

	mpt_entry->page_size = cpu_to_be32(buffer_size_shift - 12);
	mpt_entry->key       = cpu_to_be32(key);
	mpt_entry->pd        = cpu_to_be32(pd);
	mpt_entry->start     = cpu_to_be64(iova);
	mpt_entry->length    = cpu_to_be64(total_size);

	memset_startat(mpt_entry, 0, lkey);

	if (mr->mtt)
		mpt_entry->mtt_seg =
			cpu_to_be64(dev->mr_table.mtt_base +
				    mr->mtt->first_seg * dev->limits.mtt_seg_size);

	if (0) {
		mthca_dbg(dev, "Dumping MPT entry %08x:\n", mr->ibmr.lkey);
		for (i = 0; i < sizeof (struct mthca_mpt_entry) / 4; ++i) {
			if (i % 4 == 0)
				printk("[%02x] ", i * 4);
			printk(" %08x", be32_to_cpu(((__be32 *) mpt_entry)[i]));
			if ((i + 1) % 4 == 0)
				printk("\n");
		}
	}

	err = mthca_SW2HW_MPT(dev, mailbox,
			      key & (dev->limits.num_mpts - 1));
	if (err) {
		mthca_warn(dev, "SW2HW_MPT failed (%d)\n", err);
		goto err_out_mailbox;
	}

	mthca_free_mailbox(dev, mailbox);
	return err;

err_out_mailbox:
	mthca_free_mailbox(dev, mailbox);

err_out_table:
	mthca_table_put(dev, dev->mr_table.mpt_table, key);

err_out_mpt_free:
	mthca_free(&dev->mr_table.mpt_alloc, key);
	return err;
}

int mthca_mr_alloc_notrans(struct mthca_dev *dev, u32 pd,
			   u32 access, struct mthca_mr *mr)
{
	mr->mtt = NULL;
	return mthca_mr_alloc(dev, pd, 12, 0, ~0ULL, access, mr);
}

int mthca_mr_alloc_phys(struct mthca_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			u32 access, struct mthca_mr *mr)
{
	int err;

	mr->mtt = mthca_alloc_mtt(dev, list_len);
	if (IS_ERR(mr->mtt))
		return PTR_ERR(mr->mtt);

	err = mthca_write_mtt(dev, mr->mtt, 0, buffer_list, list_len);
	if (err) {
		mthca_free_mtt(dev, mr->mtt);
		return err;
	}

	err = mthca_mr_alloc(dev, pd, buffer_size_shift, iova,
			     total_size, access, mr);
	if (err)
		mthca_free_mtt(dev, mr->mtt);

	return err;
}

/* Free mr */
static void mthca_free_region(struct mthca_dev *dev, u32 lkey)
{
	mthca_table_put(dev, dev->mr_table.mpt_table,
			key_to_hw_index(dev, lkey));

	mthca_free(&dev->mr_table.mpt_alloc, key_to_hw_index(dev, lkey));
}

void mthca_free_mr(struct mthca_dev *dev, struct mthca_mr *mr)
{
	int err;

	err = mthca_HW2SW_MPT(dev, NULL,
			      key_to_hw_index(dev, mr->ibmr.lkey) &
			      (dev->limits.num_mpts - 1));
	if (err)
		mthca_warn(dev, "HW2SW_MPT failed (%d)\n", err);

	mthca_free_region(dev, mr->ibmr.lkey);
	mthca_free_mtt(dev, mr->mtt);
}

int mthca_init_mr_table(struct mthca_dev *dev)
{
	phys_addr_t addr;
	int mpts, mtts, err, i;

	err = mthca_alloc_init(&dev->mr_table.mpt_alloc,
			       dev->limits.num_mpts,
			       ~0, dev->limits.reserved_mrws);
	if (err)
		return err;

	if (!mthca_is_memfree(dev) &&
	    (dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN))
		dev->limits.fmr_reserved_mtts = 0;
	else
		dev->mthca_flags |= MTHCA_FLAG_FMR;

	if (dev->mthca_flags & MTHCA_FLAG_SINAI_OPT)
		mthca_dbg(dev, "Memory key throughput optimization activated.\n");

	err = mthca_buddy_init(&dev->mr_table.mtt_buddy,
			       fls(dev->limits.num_mtt_segs - 1));

	if (err)
		goto err_mtt_buddy;

	dev->mr_table.tavor_fmr.mpt_base = NULL;
	dev->mr_table.tavor_fmr.mtt_base = NULL;

	if (dev->limits.fmr_reserved_mtts) {
		i = fls(dev->limits.fmr_reserved_mtts - 1);

		if (i >= 31) {
			mthca_warn(dev, "Unable to reserve 2^31 FMR MTTs.\n");
			err = -EINVAL;
			goto err_fmr_mpt;
		}
		mpts = mtts = 1 << i;
	} else {
		mtts = dev->limits.num_mtt_segs;
		mpts = dev->limits.num_mpts;
	}

	if (!mthca_is_memfree(dev) &&
	    (dev->mthca_flags & MTHCA_FLAG_FMR)) {

		addr = pci_resource_start(dev->pdev, 4) +
			((pci_resource_len(dev->pdev, 4) - 1) &
			 dev->mr_table.mpt_base);

		dev->mr_table.tavor_fmr.mpt_base =
			ioremap(addr, mpts * sizeof(struct mthca_mpt_entry));

		if (!dev->mr_table.tavor_fmr.mpt_base) {
			mthca_warn(dev, "MPT ioremap for FMR failed.\n");
			err = -ENOMEM;
			goto err_fmr_mpt;
		}

		addr = pci_resource_start(dev->pdev, 4) +
			((pci_resource_len(dev->pdev, 4) - 1) &
			 dev->mr_table.mtt_base);

		dev->mr_table.tavor_fmr.mtt_base =
			ioremap(addr, mtts * dev->limits.mtt_seg_size);
		if (!dev->mr_table.tavor_fmr.mtt_base) {
			mthca_warn(dev, "MTT ioremap for FMR failed.\n");
			err = -ENOMEM;
			goto err_fmr_mtt;
		}
	}

	if (dev->limits.fmr_reserved_mtts) {
		err = mthca_buddy_init(&dev->mr_table.tavor_fmr.mtt_buddy, fls(mtts - 1));
		if (err)
			goto err_fmr_mtt_buddy;

		/* Prevent regular MRs from using FMR keys */
		err = mthca_buddy_alloc(&dev->mr_table.mtt_buddy, fls(mtts - 1));
		if (err)
			goto err_reserve_fmr;

		dev->mr_table.fmr_mtt_buddy =
			&dev->mr_table.tavor_fmr.mtt_buddy;
	} else
		dev->mr_table.fmr_mtt_buddy = &dev->mr_table.mtt_buddy;

	/* FMR table is always the first, take reserved MTTs out of there */
	if (dev->limits.reserved_mtts) {
		i = fls(dev->limits.reserved_mtts - 1);

		if (mthca_alloc_mtt_range(dev, i,
					  dev->mr_table.fmr_mtt_buddy) == -1) {
			mthca_warn(dev, "MTT table of order %d is too small.\n",
				  dev->mr_table.fmr_mtt_buddy->max_order);
			err = -ENOMEM;
			goto err_reserve_mtts;
		}
	}

	return 0;

err_reserve_mtts:
err_reserve_fmr:
	if (dev->limits.fmr_reserved_mtts)
		mthca_buddy_cleanup(&dev->mr_table.tavor_fmr.mtt_buddy);

err_fmr_mtt_buddy:
	if (dev->mr_table.tavor_fmr.mtt_base)
		iounmap(dev->mr_table.tavor_fmr.mtt_base);

err_fmr_mtt:
	if (dev->mr_table.tavor_fmr.mpt_base)
		iounmap(dev->mr_table.tavor_fmr.mpt_base);

err_fmr_mpt:
	mthca_buddy_cleanup(&dev->mr_table.mtt_buddy);

err_mtt_buddy:
	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);

	return err;
}

void mthca_cleanup_mr_table(struct mthca_dev *dev)
{
	/* XXX check if any MRs are still allocated? */
	if (dev->limits.fmr_reserved_mtts)
		mthca_buddy_cleanup(&dev->mr_table.tavor_fmr.mtt_buddy);

	mthca_buddy_cleanup(&dev->mr_table.mtt_buddy);

	if (dev->mr_table.tavor_fmr.mtt_base)
		iounmap(dev->mr_table.tavor_fmr.mtt_base);
	if (dev->mr_table.tavor_fmr.mpt_base)
		iounmap(dev->mr_table.tavor_fmr.mpt_base);

	mthca_alloc_cleanup(&dev->mr_table.mpt_alloc);
}
