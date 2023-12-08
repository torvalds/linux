// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/mhi_ep.h>
#include "internal.h"

size_t mhi_ep_ring_addr2offset(struct mhi_ep_ring *ring, u64 ptr)
{
	return (ptr - ring->rbase) / sizeof(struct mhi_ring_element);
}

static u32 mhi_ep_ring_num_elems(struct mhi_ep_ring *ring)
{
	__le64 rlen;

	memcpy_fromio(&rlen, (void __iomem *) &ring->ring_ctx->generic.rlen, sizeof(u64));

	return le64_to_cpu(rlen) / sizeof(struct mhi_ring_element);
}

void mhi_ep_ring_inc_index(struct mhi_ep_ring *ring)
{
	ring->rd_offset = (ring->rd_offset + 1) % ring->ring_size;
}

static int __mhi_ep_cache_ring(struct mhi_ep_ring *ring, size_t end)
{
	struct mhi_ep_cntrl *mhi_cntrl = ring->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	size_t start, copy_size;
	int ret;

	/* Don't proceed in the case of event ring. This happens during mhi_ep_ring_start(). */
	if (ring->type == RING_TYPE_ER)
		return 0;

	/* No need to cache the ring if write pointer is unmodified */
	if (ring->wr_offset == end)
		return 0;

	start = ring->wr_offset;
	if (start < end) {
		copy_size = (end - start) * sizeof(struct mhi_ring_element);
		ret = mhi_cntrl->read_from_host(mhi_cntrl, ring->rbase +
						(start * sizeof(struct mhi_ring_element)),
						&ring->ring_cache[start], copy_size);
		if (ret < 0)
			return ret;
	} else {
		copy_size = (ring->ring_size - start) * sizeof(struct mhi_ring_element);
		ret = mhi_cntrl->read_from_host(mhi_cntrl, ring->rbase +
						(start * sizeof(struct mhi_ring_element)),
						&ring->ring_cache[start], copy_size);
		if (ret < 0)
			return ret;

		if (end) {
			ret = mhi_cntrl->read_from_host(mhi_cntrl, ring->rbase,
							&ring->ring_cache[0],
							end * sizeof(struct mhi_ring_element));
			if (ret < 0)
				return ret;
		}
	}

	dev_dbg(dev, "Cached ring: start %zu end %zu size %zu\n", start, end, copy_size);

	return 0;
}

static int mhi_ep_cache_ring(struct mhi_ep_ring *ring, u64 wr_ptr)
{
	size_t wr_offset;
	int ret;

	wr_offset = mhi_ep_ring_addr2offset(ring, wr_ptr);

	/* Cache the host ring till write offset */
	ret = __mhi_ep_cache_ring(ring, wr_offset);
	if (ret)
		return ret;

	ring->wr_offset = wr_offset;

	return 0;
}

int mhi_ep_update_wr_offset(struct mhi_ep_ring *ring)
{
	u64 wr_ptr;

	wr_ptr = mhi_ep_mmio_get_db(ring);

	return mhi_ep_cache_ring(ring, wr_ptr);
}

/* TODO: Support for adding multiple ring elements to the ring */
int mhi_ep_ring_add_element(struct mhi_ep_ring *ring, struct mhi_ring_element *el)
{
	struct mhi_ep_cntrl *mhi_cntrl = ring->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	size_t old_offset = 0;
	u32 num_free_elem;
	__le64 rp;
	int ret;

	ret = mhi_ep_update_wr_offset(ring);
	if (ret) {
		dev_err(dev, "Error updating write pointer\n");
		return ret;
	}

	if (ring->rd_offset < ring->wr_offset)
		num_free_elem = (ring->wr_offset - ring->rd_offset) - 1;
	else
		num_free_elem = ((ring->ring_size - ring->rd_offset) + ring->wr_offset) - 1;

	/* Check if there is space in ring for adding at least an element */
	if (!num_free_elem) {
		dev_err(dev, "No space left in the ring\n");
		return -ENOSPC;
	}

	old_offset = ring->rd_offset;
	mhi_ep_ring_inc_index(ring);

	dev_dbg(dev, "Adding an element to ring at offset (%zu)\n", ring->rd_offset);

	/* Update rp in ring context */
	rp = cpu_to_le64(ring->rd_offset * sizeof(*el) + ring->rbase);
	memcpy_toio((void __iomem *) &ring->ring_ctx->generic.rp, &rp, sizeof(u64));

	ret = mhi_cntrl->write_to_host(mhi_cntrl, el, ring->rbase + (old_offset * sizeof(*el)),
				       sizeof(*el));
	if (ret < 0)
		return ret;

	return 0;
}

void mhi_ep_ring_init(struct mhi_ep_ring *ring, enum mhi_ep_ring_type type, u32 id)
{
	ring->type = type;
	if (ring->type == RING_TYPE_CMD) {
		ring->db_offset_h = EP_CRDB_HIGHER;
		ring->db_offset_l = EP_CRDB_LOWER;
	} else if (ring->type == RING_TYPE_CH) {
		ring->db_offset_h = CHDB_HIGHER_n(id);
		ring->db_offset_l = CHDB_LOWER_n(id);
		ring->ch_id = id;
	} else {
		ring->db_offset_h = ERDB_HIGHER_n(id);
		ring->db_offset_l = ERDB_LOWER_n(id);
	}
}

int mhi_ep_ring_start(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_ring *ring,
			union mhi_ep_ring_ctx *ctx)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	__le64 val;
	int ret;

	ring->mhi_cntrl = mhi_cntrl;
	ring->ring_ctx = ctx;
	ring->ring_size = mhi_ep_ring_num_elems(ring);
	memcpy_fromio(&val, (void __iomem *) &ring->ring_ctx->generic.rbase, sizeof(u64));
	ring->rbase = le64_to_cpu(val);

	if (ring->type == RING_TYPE_CH)
		ring->er_index = le32_to_cpu(ring->ring_ctx->ch.erindex);

	if (ring->type == RING_TYPE_ER)
		ring->irq_vector = le32_to_cpu(ring->ring_ctx->ev.msivec);

	/* During ring init, both rp and wp are equal */
	memcpy_fromio(&val, (void __iomem *) &ring->ring_ctx->generic.rp, sizeof(u64));
	ring->rd_offset = mhi_ep_ring_addr2offset(ring, le64_to_cpu(val));
	ring->wr_offset = mhi_ep_ring_addr2offset(ring, le64_to_cpu(val));

	/* Allocate ring cache memory for holding the copy of host ring */
	ring->ring_cache = kcalloc(ring->ring_size, sizeof(struct mhi_ring_element), GFP_KERNEL);
	if (!ring->ring_cache)
		return -ENOMEM;

	memcpy_fromio(&val, (void __iomem *) &ring->ring_ctx->generic.wp, sizeof(u64));
	ret = mhi_ep_cache_ring(ring, le64_to_cpu(val));
	if (ret) {
		dev_err(dev, "Failed to cache ring\n");
		kfree(ring->ring_cache);
		return ret;
	}

	ring->started = true;

	return 0;
}

void mhi_ep_ring_reset(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_ring *ring)
{
	ring->started = false;
	kfree(ring->ring_cache);
	ring->ring_cache = NULL;
}
