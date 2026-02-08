/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RES_H__
#define __BNG_RES_H__

#include "bng_roce_hsi.h"

#define BNG_ROCE_FW_MAX_TIMEOUT	60

#define PTR_CNT_PER_PG		(PAGE_SIZE / sizeof(void *))
#define PTR_MAX_IDX_PER_PG	(PTR_CNT_PER_PG - 1)
#define PTR_PG(x)		(((x) & ~PTR_MAX_IDX_PER_PG) / PTR_CNT_PER_PG)
#define PTR_IDX(x)		((x) & PTR_MAX_IDX_PER_PG)

#define HWQ_CMP(idx, hwq)	((idx) & ((hwq)->max_elements - 1))
#define HWQ_FREE_SLOTS(hwq)	(hwq->max_elements - \
				((HWQ_CMP(hwq->prod, hwq)\
				- HWQ_CMP(hwq->cons, hwq))\
				& (hwq->max_elements - 1)))

#define MAX_PBL_LVL_0_PGS		1
#define MAX_PBL_LVL_1_PGS		512
#define MAX_PBL_LVL_1_PGS_SHIFT		9
#define MAX_PBL_LVL_1_PGS_FOR_LVL_2	256
#define MAX_PBL_LVL_2_PGS		(256 * 512)
#define MAX_PDL_LVL_SHIFT               9

#define BNG_RE_DBR_VALID		(0x1UL << 26)
#define BNG_RE_DBR_EPOCH_SHIFT	24
#define BNG_RE_DBR_TOGGLE_SHIFT	25

#define BNG_MAX_TQM_ALLOC_REQ	48

struct bng_re_reg_desc {
	u8		bar_id;
	resource_size_t	bar_base;
	unsigned long	offset;
	void __iomem	*bar_reg;
	size_t		len;
};

struct bng_re_db_info {
	void __iomem		*db;
	void __iomem		*priv_db;
	struct bng_re_hwq	*hwq;
	u32			xid;
	u32			max_slot;
	u32                     flags;
	u8			toggle;
};

enum bng_re_db_info_flags_mask {
	BNG_RE_FLAG_EPOCH_CONS_SHIFT        = 0x0UL,
	BNG_RE_FLAG_EPOCH_PROD_SHIFT        = 0x1UL,
	BNG_RE_FLAG_EPOCH_CONS_MASK         = 0x1UL,
	BNG_RE_FLAG_EPOCH_PROD_MASK         = 0x2UL,
};

enum bng_re_db_epoch_flag_shift {
	BNG_RE_DB_EPOCH_CONS_SHIFT  = BNG_RE_DBR_EPOCH_SHIFT,
	BNG_RE_DB_EPOCH_PROD_SHIFT  = (BNG_RE_DBR_EPOCH_SHIFT - 1),
};

struct bng_re_chip_ctx {
	u16	chip_num;
	u16	hw_stats_size;
	u64	hwrm_intf_ver;
	u16	hwrm_cmd_max_timeout;
};

struct bng_re_pbl {
	u32		pg_count;
	u32		pg_size;
	void		**pg_arr;
	dma_addr_t	*pg_map_arr;
};

enum bng_re_pbl_lvl {
	BNG_PBL_LVL_0,
	BNG_PBL_LVL_1,
	BNG_PBL_LVL_2,
	BNG_PBL_LVL_MAX
};

enum bng_re_hwq_type {
	BNG_HWQ_TYPE_CTX,
	BNG_HWQ_TYPE_QUEUE
};

struct bng_re_sg_info {
	u32	npages;
	u32	pgshft;
	u32	pgsize;
	bool	nopte;
};

struct bng_re_hwq_attr {
	struct bng_re_res		*res;
	struct bng_re_sg_info		*sginfo;
	enum bng_re_hwq_type		type;
	u32				depth;
	u32				stride;
	u32				aux_stride;
	u32				aux_depth;
};

struct bng_re_hwq {
	struct pci_dev			*pdev;
	/* lock to protect hwq */
	spinlock_t			lock;
	struct bng_re_pbl		pbl[BNG_PBL_LVL_MAX + 1];
	/* Valid values: 0, 1, 2 */
	enum bng_re_pbl_lvl		level;
	/* PBL entries */
	void				**pbl_ptr;
	/* PBL  dma_addr */
	dma_addr_t			*pbl_dma_ptr;
	u32				max_elements;
	u32				depth;
	u16				element_size;
	u32				prod;
	u32				cons;
	/* queue entry per page */
	u16				qe_ppg;
};

struct bng_re_stats {
	dma_addr_t			dma_map;
	void				*dma;
	u32				size;
	u32				fw_id;
};

struct bng_re_res {
	struct pci_dev			*pdev;
	struct bng_re_chip_ctx		*cctx;
	struct bng_re_dev_attr		*dattr;
};

static inline void *bng_re_get_qe(struct bng_re_hwq *hwq,
				  u32 indx, u64 *pg)
{
	u32 pg_num, pg_idx;

	pg_num = (indx / hwq->qe_ppg);
	pg_idx = (indx % hwq->qe_ppg);
	if (pg)
		*pg = (u64)&hwq->pbl_ptr[pg_num];
	return (void *)(hwq->pbl_ptr[pg_num] + hwq->element_size * pg_idx);
}

#define BNG_RE_INIT_DBHDR(xid, type, indx, toggle) \
	(((u64)(((xid) & DBC_DBC_XID_MASK) | DBC_DBC_PATH_ROCE |  \
		(type) | BNG_RE_DBR_VALID) << 32) | (indx) |  \
	 (((u32)(toggle)) << (BNG_RE_DBR_TOGGLE_SHIFT)))

static inline void bng_re_ring_db(struct bng_re_db_info *info,
				  u32 type)
{
	u64 key = 0;
	u32 indx;
	u8 toggle = 0;

	if (type == DBC_DBC_TYPE_CQ_ARMALL ||
	    type == DBC_DBC_TYPE_CQ_ARMSE)
		toggle = info->toggle;

	indx = (info->hwq->cons & DBC_DBC_INDEX_MASK) |
	       ((info->flags & BNG_RE_FLAG_EPOCH_CONS_MASK) <<
		 BNG_RE_DB_EPOCH_CONS_SHIFT);

	key =  BNG_RE_INIT_DBHDR(info->xid, type, indx, toggle);
	writeq(key, info->db);
}

static inline void bng_re_ring_nq_db(struct bng_re_db_info *info,
				     struct bng_re_chip_ctx *cctx,
				     bool arm)
{
	u32 type;

	type = arm ? DBC_DBC_TYPE_NQ_ARM : DBC_DBC_TYPE_NQ;
	bng_re_ring_db(info, type);
}

static inline void bng_re_hwq_incr_cons(u32 max_elements, u32 *cons, u32 cnt,
					u32 *dbinfo_flags)
{
	/* move cons and update toggle/epoch if wrap around */
	*cons += cnt;
	if (*cons >= max_elements) {
		*cons %= max_elements;
		*dbinfo_flags ^= 1UL << BNG_RE_FLAG_EPOCH_CONS_SHIFT;
	}
}

static inline bool _is_max_srq_ext_supported(u16 dev_cap_ext_flags_2)
{
	return !!(dev_cap_ext_flags_2 & CREQ_QUERY_FUNC_RESP_SB_MAX_SRQ_EXTENDED);
}

void bng_re_free_hwq(struct bng_re_res *res,
		     struct bng_re_hwq *hwq);

int bng_re_alloc_init_hwq(struct bng_re_hwq *hwq,
			  struct bng_re_hwq_attr *hwq_attr);

void bng_re_free_stats_ctx_mem(struct pci_dev *pdev,
			       struct bng_re_stats *stats);

int bng_re_alloc_stats_ctx_mem(struct pci_dev *pdev,
			       struct bng_re_chip_ctx *cctx,
			       struct bng_re_stats *stats);
#endif
