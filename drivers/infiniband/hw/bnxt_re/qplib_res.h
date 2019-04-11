/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: QPLib resource manager (header)
 */

#ifndef __BNXT_QPLIB_RES_H__
#define __BNXT_QPLIB_RES_H__

extern const struct bnxt_qplib_gid bnxt_qplib_gid_zero;

#define PTR_CNT_PER_PG		(PAGE_SIZE / sizeof(void *))
#define PTR_MAX_IDX_PER_PG	(PTR_CNT_PER_PG - 1)
#define PTR_PG(x)		(((x) & ~PTR_MAX_IDX_PER_PG) / PTR_CNT_PER_PG)
#define PTR_IDX(x)		((x) & PTR_MAX_IDX_PER_PG)

#define HWQ_CMP(idx, hwq)	((idx) & ((hwq)->max_elements - 1))

#define HWQ_FREE_SLOTS(hwq)	(hwq->max_elements - \
				((HWQ_CMP(hwq->prod, hwq)\
				- HWQ_CMP(hwq->cons, hwq))\
				& (hwq->max_elements - 1)))
enum bnxt_qplib_hwq_type {
	HWQ_TYPE_CTX,
	HWQ_TYPE_QUEUE,
	HWQ_TYPE_L2_CMPL
};

#define MAX_PBL_LVL_0_PGS		1
#define MAX_PBL_LVL_1_PGS		512
#define MAX_PBL_LVL_1_PGS_SHIFT		9
#define MAX_PBL_LVL_1_PGS_FOR_LVL_2	256
#define MAX_PBL_LVL_2_PGS		(256 * 512)

enum bnxt_qplib_pbl_lvl {
	PBL_LVL_0,
	PBL_LVL_1,
	PBL_LVL_2,
	PBL_LVL_MAX
};

#define ROCE_PG_SIZE_4K		(4 * 1024)
#define ROCE_PG_SIZE_8K		(8 * 1024)
#define ROCE_PG_SIZE_64K	(64 * 1024)
#define ROCE_PG_SIZE_2M		(2 * 1024 * 1024)
#define ROCE_PG_SIZE_8M		(8 * 1024 * 1024)
#define ROCE_PG_SIZE_1G		(1024 * 1024 * 1024)

struct bnxt_qplib_pbl {
	u32				pg_count;
	u32				pg_size;
	void				**pg_arr;
	dma_addr_t			*pg_map_arr;
};

struct bnxt_qplib_hwq {
	struct pci_dev			*pdev;
	/* lock to protect qplib_hwq */
	spinlock_t			lock;
	struct bnxt_qplib_pbl		pbl[PBL_LVL_MAX];
	enum bnxt_qplib_pbl_lvl		level;		/* 0, 1, or 2 */
	/* ptr for easy access to the PBL entries */
	void				**pbl_ptr;
	/* ptr for easy access to the dma_addr */
	dma_addr_t			*pbl_dma_ptr;
	u32				max_elements;
	u16				element_size;	/* Size of each entry */

	u32				prod;		/* raw */
	u32				cons;		/* raw */
	u8				cp_bit;
	u8				is_user;
};

/* Tables */
struct bnxt_qplib_pd_tbl {
	unsigned long			*tbl;
	u32				max;
};

struct bnxt_qplib_sgid_tbl {
	struct bnxt_qplib_gid		*tbl;
	u16				*hw_id;
	u16				max;
	u16				active;
	void				*ctx;
	u8				*vlan;
};

struct bnxt_qplib_pkey_tbl {
	u16				*tbl;
	u16				max;
	u16				active;
};

struct bnxt_qplib_dpi {
	u32				dpi;
	void __iomem			*dbr;
	u64				umdbr;
};

struct bnxt_qplib_dpi_tbl {
	void				**app_tbl;
	unsigned long			*tbl;
	u16				max;
	void __iomem			*dbr_bar_reg_iomem;
	u64				unmapped_dbr;
};

struct bnxt_qplib_stats {
	dma_addr_t			dma_map;
	void				*dma;
	u32				size;
	u32				fw_id;
};

struct bnxt_qplib_vf_res {
	u32 max_qp_per_vf;
	u32 max_mrw_per_vf;
	u32 max_srq_per_vf;
	u32 max_cq_per_vf;
	u32 max_gid_per_vf;
};

#define BNXT_QPLIB_MAX_QP_CTX_ENTRY_SIZE	448
#define BNXT_QPLIB_MAX_SRQ_CTX_ENTRY_SIZE	64
#define BNXT_QPLIB_MAX_CQ_CTX_ENTRY_SIZE	64
#define BNXT_QPLIB_MAX_MRW_CTX_ENTRY_SIZE	128

struct bnxt_qplib_ctx {
	u32				qpc_count;
	struct bnxt_qplib_hwq		qpc_tbl;
	u32				mrw_count;
	struct bnxt_qplib_hwq		mrw_tbl;
	u32				srqc_count;
	struct bnxt_qplib_hwq		srqc_tbl;
	u32				cq_count;
	struct bnxt_qplib_hwq		cq_tbl;
	struct bnxt_qplib_hwq		tim_tbl;
#define MAX_TQM_ALLOC_REQ		48
#define MAX_TQM_ALLOC_BLK_SIZE		8
	u8				tqm_count[MAX_TQM_ALLOC_REQ];
	struct bnxt_qplib_hwq		tqm_pde;
	u32				tqm_pde_level;
	struct bnxt_qplib_hwq		tqm_tbl[MAX_TQM_ALLOC_REQ];
	struct bnxt_qplib_stats		stats;
	struct bnxt_qplib_vf_res	vf_res;
	u64				hwrm_intf_ver;
};

struct bnxt_qplib_chip_ctx {
	u16	chip_num;
	u8	chip_rev;
	u8	chip_metal;
};

#define CHIP_NUM_57500          0x1750

struct bnxt_qplib_res {
	struct pci_dev			*pdev;
	struct bnxt_qplib_chip_ctx	*cctx;
	struct net_device		*netdev;

	struct bnxt_qplib_rcfw		*rcfw;
	struct bnxt_qplib_pd_tbl	pd_tbl;
	struct bnxt_qplib_sgid_tbl	sgid_tbl;
	struct bnxt_qplib_pkey_tbl	pkey_tbl;
	struct bnxt_qplib_dpi_tbl	dpi_tbl;
	bool				prio;
};

static inline bool bnxt_qplib_is_chip_gen_p5(struct bnxt_qplib_chip_ctx *cctx)
{
	return (cctx->chip_num == CHIP_NUM_57500);
}

static inline u8 bnxt_qplib_get_hwq_type(struct bnxt_qplib_res *res)
{
	return bnxt_qplib_is_chip_gen_p5(res->cctx) ?
					HWQ_TYPE_QUEUE : HWQ_TYPE_L2_CMPL;
}

static inline u8 bnxt_qplib_get_ring_type(struct bnxt_qplib_chip_ctx *cctx)
{
	return bnxt_qplib_is_chip_gen_p5(cctx) ?
	       RING_ALLOC_REQ_RING_TYPE_NQ :
	       RING_ALLOC_REQ_RING_TYPE_ROCE_CMPL;
}

#define to_bnxt_qplib(ptr, type, member)	\
	container_of(ptr, type, member)

struct bnxt_qplib_pd;
struct bnxt_qplib_dev_attr;

void bnxt_qplib_free_hwq(struct pci_dev *pdev, struct bnxt_qplib_hwq *hwq);
int bnxt_qplib_alloc_init_hwq(struct pci_dev *pdev, struct bnxt_qplib_hwq *hwq,
			      struct scatterlist *sl, int nmap, u32 *elements,
			      u32 elements_per_page, u32 aux, u32 pg_size,
			      enum bnxt_qplib_hwq_type hwq_type);
void bnxt_qplib_get_guid(u8 *dev_addr, u8 *guid);
int bnxt_qplib_alloc_pd(struct bnxt_qplib_pd_tbl *pd_tbl,
			struct bnxt_qplib_pd *pd);
int bnxt_qplib_dealloc_pd(struct bnxt_qplib_res *res,
			  struct bnxt_qplib_pd_tbl *pd_tbl,
			  struct bnxt_qplib_pd *pd);
int bnxt_qplib_alloc_dpi(struct bnxt_qplib_dpi_tbl *dpit,
			 struct bnxt_qplib_dpi     *dpi,
			 void                      *app);
int bnxt_qplib_dealloc_dpi(struct bnxt_qplib_res *res,
			   struct bnxt_qplib_dpi_tbl *dpi_tbl,
			   struct bnxt_qplib_dpi *dpi);
void bnxt_qplib_cleanup_res(struct bnxt_qplib_res *res);
int bnxt_qplib_init_res(struct bnxt_qplib_res *res);
void bnxt_qplib_free_res(struct bnxt_qplib_res *res);
int bnxt_qplib_alloc_res(struct bnxt_qplib_res *res, struct pci_dev *pdev,
			 struct net_device *netdev,
			 struct bnxt_qplib_dev_attr *dev_attr);
void bnxt_qplib_free_ctx(struct pci_dev *pdev,
			 struct bnxt_qplib_ctx *ctx);
int bnxt_qplib_alloc_ctx(struct pci_dev *pdev,
			 struct bnxt_qplib_ctx *ctx,
			 bool virt_fn, bool is_p5);
#endif /* __BNXT_QPLIB_RES_H__ */
