/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RES_H__
#define __BNG_RES_H__

#define BNG_ROCE_FW_MAX_TIMEOUT	60

#define PTR_CNT_PER_PG		(PAGE_SIZE / sizeof(void *))
#define PTR_MAX_IDX_PER_PG	(PTR_CNT_PER_PG - 1)
#define PTR_PG(x)		(((x) & ~PTR_MAX_IDX_PER_PG) / PTR_CNT_PER_PG)
#define PTR_IDX(x)		((x) & PTR_MAX_IDX_PER_PG)

#define MAX_PBL_LVL_0_PGS		1
#define MAX_PBL_LVL_1_PGS		512
#define MAX_PBL_LVL_1_PGS_SHIFT		9
#define MAX_PBL_LVL_1_PGS_FOR_LVL_2	256
#define MAX_PBL_LVL_2_PGS		(256 * 512)
#define MAX_PDL_LVL_SHIFT               9

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
};

struct bng_re_res {
	struct pci_dev			*pdev;
	struct bng_re_chip_ctx		*cctx;
};

void bng_re_free_hwq(struct bng_re_res *res,
		     struct bng_re_hwq *hwq);

int bng_re_alloc_init_hwq(struct bng_re_hwq *hwq,
			  struct bng_re_hwq_attr *hwq_attr);
#endif
