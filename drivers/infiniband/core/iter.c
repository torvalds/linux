// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. */

#include <linux/export.h>
#include <rdma/iter.h>

void __rdma_block_iter_start(struct ib_block_iter *biter,
			     struct scatterlist *sglist, unsigned int nents,
			     unsigned long pgsz)
{
	memset(biter, 0, sizeof(struct ib_block_iter));
	biter->__sg = sglist;
	biter->__sg_nents = nents;

	/* Driver provides best block size to use */
	biter->__pg_bit = __fls(pgsz);
}
EXPORT_SYMBOL(__rdma_block_iter_start);

bool __rdma_block_iter_next(struct ib_block_iter *biter)
{
	dma_addr_t block_offset;
	dma_addr_t delta;

	if (!biter->__sg_nents || !biter->__sg)
		return false;

	biter->__dma_addr = sg_dma_address(biter->__sg) + biter->__sg_advance;
	block_offset = biter->__dma_addr & (BIT_ULL(biter->__pg_bit) - 1);
	delta = BIT_ULL(biter->__pg_bit) - block_offset;

	while (biter->__sg_nents && biter->__sg &&
	       sg_dma_len(biter->__sg) - biter->__sg_advance <= delta) {
		delta -= sg_dma_len(biter->__sg) - biter->__sg_advance;
		biter->__sg_advance = 0;
		biter->__sg = sg_next(biter->__sg);
		biter->__sg_nents--;
	}
	biter->__sg_advance += delta;

	return true;
}
EXPORT_SYMBOL(__rdma_block_iter_next);
