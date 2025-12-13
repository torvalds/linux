/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_RMEM_H_
#define _BNGE_RMEM_H_

struct bnge_ctx_mem_type;
struct bnge_dev;
struct bnge_net;

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

	struct bnge_ctx_mem_type	*ctx_mem;
};

/* The hardware supports certain page sizes.
 * Use the supported page sizes to allocate the rings.
 */
#if (PAGE_SHIFT < 12)
#define BNGE_PAGE_SHIFT	12
#elif (PAGE_SHIFT <= 13)
#define BNGE_PAGE_SHIFT	PAGE_SHIFT
#elif (PAGE_SHIFT < 16)
#define BNGE_PAGE_SHIFT	13
#else
#define BNGE_PAGE_SHIFT	16
#endif
#define BNGE_PAGE_SIZE	(1 << BNGE_PAGE_SHIFT)
/* The RXBD length is 16-bit so we can only support page sizes < 64K */
#if (PAGE_SHIFT > 15)
#define BNGE_RX_PAGE_SHIFT 15
#else
#define BNGE_RX_PAGE_SHIFT PAGE_SHIFT
#endif
#define MAX_CTX_PAGES	(BNGE_PAGE_SIZE / 8)
#define MAX_CTX_TOTAL_PAGES	(MAX_CTX_PAGES * MAX_CTX_PAGES)

struct bnge_ctx_pg_info {
	u32		entries;
	u32		nr_pages;
	void		*ctx_pg_arr[MAX_CTX_PAGES];
	dma_addr_t	ctx_dma_arr[MAX_CTX_PAGES];
	struct bnge_ring_mem_info ring_mem;
	struct bnge_ctx_pg_info **ctx_pg_tbl;
};

#define BNGE_MAX_TQM_SP_RINGS		1
#define BNGE_MAX_TQM_FP_RINGS		8
#define BNGE_MAX_TQM_RINGS		\
	(BNGE_MAX_TQM_SP_RINGS + BNGE_MAX_TQM_FP_RINGS)
#define BNGE_BACKING_STORE_CFG_LEGACY_LEN	256
#define BNGE_SET_CTX_PAGE_ATTR(attr)					\
do {									\
	if (BNGE_PAGE_SIZE == 0x2000)					\
		attr = FUNC_BACKING_STORE_CFG_REQ_SRQ_PG_SIZE_PG_8K;	\
	else if (BNGE_PAGE_SIZE == 0x10000)				\
		attr = FUNC_BACKING_STORE_CFG_REQ_QPC_PG_SIZE_PG_64K;	\
	else								\
		attr = FUNC_BACKING_STORE_CFG_REQ_QPC_PG_SIZE_PG_4K;	\
} while (0)

#define BNGE_CTX_MRAV_AV_SPLIT_ENTRY	0

#define BNGE_CTX_QP	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_QP
#define BNGE_CTX_SRQ	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRQ
#define BNGE_CTX_CQ	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CQ
#define BNGE_CTX_VNIC	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_VNIC
#define BNGE_CTX_STAT	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_STAT
#define BNGE_CTX_STQM	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SP_TQM_RING
#define BNGE_CTX_FTQM	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_FP_TQM_RING
#define BNGE_CTX_MRAV	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_MRAV
#define BNGE_CTX_TIM	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_TIM
#define BNGE_CTX_TCK	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_TX_CK
#define BNGE_CTX_RCK	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RX_CK
#define BNGE_CTX_MTQM	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_MP_TQM_RING
#define BNGE_CTX_SQDBS	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SQ_DB_SHADOW
#define BNGE_CTX_RQDBS	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RQ_DB_SHADOW
#define BNGE_CTX_SRQDBS	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRQ_DB_SHADOW
#define BNGE_CTX_CQDBS	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CQ_DB_SHADOW
#define BNGE_CTX_SRT_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRT_TRACE
#define BNGE_CTX_SRT2_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRT2_TRACE
#define BNGE_CTX_CRT_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CRT_TRACE
#define BNGE_CTX_CRT2_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CRT2_TRACE
#define BNGE_CTX_RIGP0_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RIGP0_TRACE
#define BNGE_CTX_L2_HWRM_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_L2_HWRM_TRACE
#define BNGE_CTX_ROCE_HWRM_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_ROCE_HWRM_TRACE

#define BNGE_CTX_MAX		(BNGE_CTX_TIM + 1)
#define BNGE_CTX_L2_MAX		(BNGE_CTX_FTQM + 1)
#define BNGE_CTX_INV		((u16)-1)

#define BNGE_CTX_V2_MAX	\
	(FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_ROCE_HWRM_TRACE + 1)

#define BNGE_BS_CFG_ALL_DONE	\
	FUNC_BACKING_STORE_CFG_V2_REQ_FLAGS_BS_CFG_ALL_DONE

struct bnge_ctx_mem_type {
	u16	type;
	u16	entry_size;
	u32	flags;
#define BNGE_CTX_MEM_TYPE_VALID	\
	FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_TYPE_VALID
	u32	instance_bmap;
	u8	init_value;
	u8	entry_multiple;
	u16	init_offset;
#define	BNGE_CTX_INIT_INVALID_OFFSET	0xffff
	u32	max_entries;
	u32	min_entries;
	u8	last:1;
	u8	split_entry_cnt;
#define BNGE_MAX_SPLIT_ENTRY	4
	union {
		struct {
			u32	qp_l2_entries;
			u32	qp_qp1_entries;
			u32	qp_fast_qpmd_entries;
		};
		u32	srq_l2_entries;
		u32	cq_l2_entries;
		u32	vnic_entries;
		struct {
			u32	mrav_av_entries;
			u32	mrav_num_entries_units;
		};
		u32	split[BNGE_MAX_SPLIT_ENTRY];
	};
	struct bnge_ctx_pg_info	*pg_info;
};

struct bnge_ctx_mem_info {
	u8	tqm_fp_rings_count;
	u32	flags;
#define BNGE_CTX_FLAG_INITED	0x01
	struct bnge_ctx_mem_type	ctx_arr[BNGE_CTX_V2_MAX];
};

struct bnge_ring_struct {
	struct bnge_ring_mem_info	ring_mem;

	u16			fw_ring_id;
	union {
		u16		grp_idx;
		u16		map_idx; /* Used by NQs */
	};
	u32			handle;
	u8			queue_id;
};

int bnge_alloc_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem);
void bnge_free_ring(struct bnge_dev *bd, struct bnge_ring_mem_info *rmem);
int bnge_alloc_ctx_mem(struct bnge_dev *bd);
void bnge_free_ctx_mem(struct bnge_dev *bd);
void bnge_init_ring_struct(struct bnge_net *bn);

#endif /* _BNGE_RMEM_H_ */
