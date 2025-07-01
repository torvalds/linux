/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_RMEM_H_
#define _BNGE_RMEM_H_

#define PTU_PTE_VALID             0x1UL
#define PTU_PTE_LAST              0x2UL
#define PTU_PTE_NEXT_TO_LAST      0x4UL

struct bnge_ring_mem_info {
	/* Number of pages to next level */
	int			nr_pages;
	int			page_size;
	u16			flags;
#define BNGE_RMEM_VALID_PTE_FLAG	1
#define BNGE_RMEM_RING_PTE_FLAG		2
#define BNGE_RMEM_USE_FULL_PAGE_FLAG	4

	u16			depth;

	void			**pg_arr;
	dma_addr_t		*dma_arr;

	__le64			*pg_tbl;
	dma_addr_t		dma_pg_tbl;

	int			vmem_size;
	void			**vmem;
};

int bnge_alloc_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem);
void bnge_free_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem);

#endif /* _BNGE_RMEM_H_ */
