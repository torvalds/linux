/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OTX2_COMMON_H
#define OTX2_COMMON_H

#include <linux/pci.h>
#include <linux/iommu.h>

#include <mbox.h>
#include "otx2_reg.h"
#include "otx2_txrx.h"

/* PCI device IDs */
#define PCI_DEVID_OCTEONTX2_RVU_PF              0xA063

#define PCI_SUBSYS_DEVID_96XX_RVU_PFVF		0xB200

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM                     2
#define PCI_MBOX_BAR_NUM                        4

#define NAME_SIZE                               32

enum arua_mapped_qtypes {
	AURA_NIX_RQ,
	AURA_NIX_SQ,
};

/* NIX LF interrupts range*/
#define NIX_LF_QINT_VEC_START			0x00
#define NIX_LF_CINT_VEC_START			0x40
#define NIX_LF_GINT_VEC				0x80
#define NIX_LF_ERR_VEC				0x81
#define NIX_LF_POISON_VEC			0x82

/* NIX (or NPC) RX errors */
enum otx2_errlvl {
	NPC_ERRLVL_RE,
	NPC_ERRLVL_LID_LA,
	NPC_ERRLVL_LID_LB,
	NPC_ERRLVL_LID_LC,
	NPC_ERRLVL_LID_LD,
	NPC_ERRLVL_LID_LE,
	NPC_ERRLVL_LID_LF,
	NPC_ERRLVL_LID_LG,
	NPC_ERRLVL_LID_LH,
	NPC_ERRLVL_NIX = 0x0F,
};

enum otx2_errcodes_re {
	/* NPC_ERRLVL_RE errcodes */
	ERRCODE_FCS = 0x7,
	ERRCODE_FCS_RCV = 0x8,
	ERRCODE_UNDERSIZE = 0x10,
	ERRCODE_OVERSIZE = 0x11,
	ERRCODE_OL2_LEN_MISMATCH = 0x12,
	/* NPC_ERRLVL_NIX errcodes */
	ERRCODE_OL3_LEN = 0x10,
	ERRCODE_OL4_LEN = 0x11,
	ERRCODE_OL4_CSUM = 0x12,
	ERRCODE_IL3_LEN = 0x20,
	ERRCODE_IL4_LEN = 0x21,
	ERRCODE_IL4_CSUM = 0x22,
};

/* Driver counted stats */
struct otx2_drv_stats {
	atomic_t rx_fcs_errs;
	atomic_t rx_oversize_errs;
	atomic_t rx_undersize_errs;
	atomic_t rx_csum_errs;
	atomic_t rx_len_errs;
	atomic_t rx_other_errs;
};

struct mbox {
	struct otx2_mbox	mbox;
	struct work_struct	mbox_wrk;
	struct otx2_mbox	mbox_up;
	struct work_struct	mbox_up_wrk;
	struct otx2_nic		*pfvf;
	void			*bbuf_base; /* Bounce buffer for mbox memory */
	struct mutex		lock;	/* serialize mailbox access */
	int			num_msgs; /* mbox number of messages */
	int			up_num_msgs; /* mbox_up number of messages */
};

struct otx2_hw {
	struct pci_dev		*pdev;
	u16                     rx_queues;
	u16                     tx_queues;
	u16			max_queues;
	u16			pool_cnt;
	u16			rqpool_cnt;
	u16			sqpool_cnt;

	/* NPA */
	u32			stack_pg_ptrs;  /* No of ptrs per stack page */
	u32			stack_pg_bytes; /* Size of stack page */
	u16			sqb_size;

	/* NIX */
	u16		txschq_list[NIX_TXSCH_LVL_CNT][MAX_TXSCHQ_PER_FUNC];

	/* HW settings, coalescing etc */
	u16			rx_chan_base;
	u16			tx_chan_base;
	u16			cq_qcount_wait;
	u16			cq_ecount_wait;
	u16			rq_skid;
	u8			cq_time_wait;

	/* MSI-X */
	u8			cint_cnt; /* CQ interrupt count */
	u16			npa_msixoff; /* Offset of NPA vectors */
	u16			nix_msixoff; /* Offset of NIX vectors */
	char			*irq_name;
	cpumask_var_t           *affinity_mask;

	/* Stats */
	struct otx2_drv_stats	drv_stats;
};

struct otx2_nic {
	void __iomem		*reg_base;
	struct net_device	*netdev;
	void			*iommu_domain;
	u16			rbsize; /* Receive buffer size */

	struct otx2_qset	qset;
	struct otx2_hw		hw;
	struct pci_dev		*pdev;
	struct device		*dev;

	/* Mbox */
	struct mbox		mbox;
	struct workqueue_struct *mbox_wq;

	u16			pcifunc; /* RVU PF_FUNC */

	/* Block address of NIX either BLKADDR_NIX0 or BLKADDR_NIX1 */
	int			nix_blkaddr;
};

static inline bool is_96xx_A0(struct pci_dev *pdev)
{
	return (pdev->revision == 0x00) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX_RVU_PFVF);
}

static inline bool is_96xx_B0(struct pci_dev *pdev)
{
	return (pdev->revision == 0x01) &&
		(pdev->subsystem_device == PCI_SUBSYS_DEVID_96XX_RVU_PFVF);
}

static inline void otx2_setup_dev_hw_settings(struct otx2_nic *pfvf)
{
	pfvf->hw.cq_time_wait = CQ_TIMER_THRESH_DEFAULT;
	pfvf->hw.cq_ecount_wait = CQ_CQE_THRESH_DEFAULT;
	pfvf->hw.cq_qcount_wait = CQ_QCOUNT_DEFAULT;

	if (is_96xx_A0(pfvf->pdev)) {
		/* Time based irq coalescing is not supported */
		pfvf->hw.cq_qcount_wait = 0x0;

		/* Due to HW issue previous silicons required minimum
		 * 600 unused CQE to avoid CQ overflow.
		 */
		pfvf->hw.rq_skid = 600;
		pfvf->qset.rqe_cnt = Q_COUNT(Q_SIZE_1K);
	}
}

/* Register read/write APIs */
static inline void __iomem *otx2_get_regaddr(struct otx2_nic *nic, u64 offset)
{
	u64 blkaddr;

	switch ((offset >> RVU_FUNC_BLKADDR_SHIFT) & RVU_FUNC_BLKADDR_MASK) {
	case BLKTYPE_NIX:
		blkaddr = nic->nix_blkaddr;
		break;
	case BLKTYPE_NPA:
		blkaddr = BLKADDR_NPA;
		break;
	default:
		blkaddr = BLKADDR_RVUM;
		break;
	};

	offset &= ~(RVU_FUNC_BLKADDR_MASK << RVU_FUNC_BLKADDR_SHIFT);
	offset |= (blkaddr << RVU_FUNC_BLKADDR_SHIFT);

	return nic->reg_base + offset;
}

static inline void otx2_write64(struct otx2_nic *nic, u64 offset, u64 val)
{
	void __iomem *addr = otx2_get_regaddr(nic, offset);

	writeq(val, addr);
}

static inline u64 otx2_read64(struct otx2_nic *nic, u64 offset)
{
	void __iomem *addr = otx2_get_regaddr(nic, offset);

	return readq(addr);
}

/* Mbox bounce buffer APIs */
static inline int otx2_mbox_bbuf_init(struct mbox *mbox, struct pci_dev *pdev)
{
	struct otx2_mbox *otx2_mbox;
	struct otx2_mbox_dev *mdev;

	mbox->bbuf_base = devm_kmalloc(&pdev->dev, MBOX_SIZE, GFP_KERNEL);
	if (!mbox->bbuf_base)
		return -ENOMEM;

	/* Overwrite mbox mbase to point to bounce buffer, so that PF/VF
	 * prepare all mbox messages in bounce buffer instead of directly
	 * in hw mbox memory.
	 */
	otx2_mbox = &mbox->mbox;
	mdev = &otx2_mbox->dev[0];
	mdev->mbase = mbox->bbuf_base;

	otx2_mbox = &mbox->mbox_up;
	mdev = &otx2_mbox->dev[0];
	mdev->mbase = mbox->bbuf_base;
	return 0;
}

static inline void otx2_sync_mbox_bbuf(struct otx2_mbox *mbox, int devid)
{
	u16 msgs_offset = ALIGN(sizeof(struct mbox_hdr), MBOX_MSG_ALIGN);
	void *hw_mbase = mbox->hwbase + (devid * MBOX_SIZE);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_hdr *hdr;
	u64 msg_size;

	if (mdev->mbase == hw_mbase)
		return;

	hdr = hw_mbase + mbox->rx_start;
	msg_size = hdr->msg_size;

	if (msg_size > mbox->rx_size - msgs_offset)
		msg_size = mbox->rx_size - msgs_offset;

	/* Copy mbox messages from mbox memory to bounce buffer */
	memcpy(mdev->mbase + mbox->rx_start,
	       hw_mbase + mbox->rx_start, msg_size + msgs_offset);
}

static inline void otx2_mbox_lock_init(struct mbox *mbox)
{
	mutex_init(&mbox->lock);
}

static inline void otx2_mbox_lock(struct mbox *mbox)
{
	mutex_lock(&mbox->lock);
}

static inline void otx2_mbox_unlock(struct mbox *mbox)
{
	mutex_unlock(&mbox->lock);
}

/* With the absence of API for 128-bit IO memory access for arm64,
 * implement required operations at place.
 */
#if defined(CONFIG_ARM64)
static inline void otx2_write128(u64 lo, u64 hi, void __iomem *addr)
{
	__asm__ volatile("stp %x[x0], %x[x1], [%x[p1],#0]!"
			 ::[x0]"r"(lo), [x1]"r"(hi), [p1]"r"(addr));
}

static inline u64 otx2_atomic64_add(u64 incr, u64 *ptr)
{
	u64 result;

	__asm__ volatile(".cpu   generic+lse\n"
			 "ldadd %x[i], %x[r], [%[b]]"
			 : [r]"=r"(result), "+m"(*ptr)
			 : [i]"r"(incr), [b]"r"(ptr)
			 : "memory");
	return result;
}

static inline u64 otx2_lmt_flush(uint64_t addr)
{
	u64 result = 0;

	__asm__ volatile(".cpu  generic+lse\n"
			 "ldeor xzr,%x[rf],[%[rs]]"
			 : [rf]"=r"(result)
			 : [rs]"r"(addr));
	return result;
}

#else
#define otx2_write128(lo, hi, addr)
#define otx2_atomic64_add(incr, ptr)		({ *ptr += incr; })
#define otx2_lmt_flush(addr)			({ 0; })
#endif

/* Alloc pointer from pool/aura */
static inline u64 otx2_aura_allocptr(struct otx2_nic *pfvf, int aura)
{
	u64 *ptr = (u64 *)otx2_get_regaddr(pfvf,
			   NPA_LF_AURA_OP_ALLOCX(0));
	u64 incr = (u64)aura | BIT_ULL(63);

	return otx2_atomic64_add(incr, ptr);
}

/* Free pointer to a pool/aura */
static inline void otx2_aura_freeptr(struct otx2_nic *pfvf,
				     int aura, s64 buf)
{
	otx2_write128((u64)buf, (u64)aura | BIT_ULL(63),
		      otx2_get_regaddr(pfvf, NPA_LF_AURA_OP_FREE0));
}

/* Update page ref count */
static inline void otx2_get_page(struct otx2_pool *pool)
{
	if (!pool->page)
		return;

	if (pool->pageref)
		page_ref_add(pool->page, pool->pageref);
	pool->pageref = 0;
	pool->page = NULL;
}

static inline int otx2_get_pool_idx(struct otx2_nic *pfvf, int type, int idx)
{
	if (type == AURA_NIX_SQ)
		return pfvf->hw.rqpool_cnt + idx;

	 /* AURA_NIX_RQ */
	return idx;
}

/* Mbox APIs */
static inline int otx2_sync_mbox_msg(struct mbox *mbox)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox, 0))
		return 0;
	otx2_mbox_msg_send(&mbox->mbox, 0);
	err = otx2_mbox_wait_for_rsp(&mbox->mbox, 0);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox, 0);
}

static inline int otx2_sync_mbox_up_msg(struct mbox *mbox, int devid)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox_up, devid))
		return 0;
	otx2_mbox_msg_send(&mbox->mbox_up, devid);
	err = otx2_mbox_wait_for_rsp(&mbox->mbox_up, devid);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox_up, devid);
}

/* Use this API to send mbox msgs in atomic context
 * where sleeping is not allowed
 */
static inline int otx2_sync_mbox_msg_busy_poll(struct mbox *mbox)
{
	int err;

	if (!otx2_mbox_nonempty(&mbox->mbox, 0))
		return 0;
	otx2_mbox_msg_send(&mbox->mbox, 0);
	err = otx2_mbox_busy_poll_for_rsp(&mbox->mbox, 0);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(&mbox->mbox, 0);
}

#define M(_name, _id, _fn_name, _req_type, _rsp_type)                   \
static struct _req_type __maybe_unused					\
*otx2_mbox_alloc_msg_ ## _fn_name(struct mbox *mbox)                    \
{									\
	struct _req_type *req;						\
									\
	req = (struct _req_type *)otx2_mbox_alloc_msg_rsp(		\
		&mbox->mbox, 0, sizeof(struct _req_type),		\
		sizeof(struct _rsp_type));				\
	if (!req)							\
		return NULL;						\
	req->hdr.sig = OTX2_MBOX_REQ_SIG;				\
	req->hdr.id = _id;						\
	return req;							\
}

MBOX_MESSAGES
#undef M

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
int									\
otx2_mbox_up_handler_ ## _fn_name(struct otx2_nic *pfvf,		\
				struct _req_type *req,			\
				struct _rsp_type *rsp);			\

MBOX_UP_CGX_MESSAGES
#undef M

#define	RVU_PFVF_PF_SHIFT	10
#define	RVU_PFVF_PF_MASK	0x3F
#define	RVU_PFVF_FUNC_SHIFT	0
#define	RVU_PFVF_FUNC_MASK	0x3FF

static inline int rvu_get_pf(u16 pcifunc)
{
	return (pcifunc >> RVU_PFVF_PF_SHIFT) & RVU_PFVF_PF_MASK;
}

static inline dma_addr_t otx2_dma_map_page(struct otx2_nic *pfvf,
					   struct page *page,
					   size_t offset, size_t size,
					   enum dma_data_direction dir)
{
	dma_addr_t iova;

	iova = dma_map_page_attrs(pfvf->dev, page,
				  offset, size, dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (unlikely(dma_mapping_error(pfvf->dev, iova)))
		return (dma_addr_t)NULL;
	return iova;
}

static inline void otx2_dma_unmap_page(struct otx2_nic *pfvf,
				       dma_addr_t addr, size_t size,
				       enum dma_data_direction dir)
{
	dma_unmap_page_attrs(pfvf->dev, addr, size,
			     dir, DMA_ATTR_SKIP_CPU_SYNC);
}

/* MSI-X APIs */
void otx2_free_cints(struct otx2_nic *pfvf, int n);
void otx2_set_cints_affinity(struct otx2_nic *pfvf);

void otx2_config_irq_coalescing(struct otx2_nic *pfvf, int qidx);

/* RVU block related APIs */
int otx2_attach_npa_nix(struct otx2_nic *pfvf);
int otx2_detach_resources(struct mbox *mbox);
int otx2_config_npa(struct otx2_nic *pfvf);
int otx2_sq_aura_pool_init(struct otx2_nic *pfvf);
int otx2_rq_aura_pool_init(struct otx2_nic *pfvf);
void otx2_aura_pool_free(struct otx2_nic *pfvf);
void otx2_free_aura_ptr(struct otx2_nic *pfvf, int type);
void otx2_sq_free_sqbs(struct otx2_nic *pfvf);
int otx2_config_nix(struct otx2_nic *pfvf);
int otx2_config_nix_queues(struct otx2_nic *pfvf);
int otx2_txschq_config(struct otx2_nic *pfvf, int lvl);
int otx2_txsch_alloc(struct otx2_nic *pfvf);
int otx2_txschq_stop(struct otx2_nic *pfvf);
void otx2_sqb_flush(struct otx2_nic *pfvf);
dma_addr_t otx2_alloc_rbuf(struct otx2_nic *pfvf, struct otx2_pool *pool,
			   gfp_t gfp);
void otx2_ctx_disable(struct mbox *mbox, int type, bool npa);
void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);
void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq);

/* Mbox handlers */
void mbox_handler_msix_offset(struct otx2_nic *pfvf,
			      struct msix_offset_rsp *rsp);
void mbox_handler_npa_lf_alloc(struct otx2_nic *pfvf,
			       struct npa_lf_alloc_rsp *rsp);
void mbox_handler_nix_lf_alloc(struct otx2_nic *pfvf,
			       struct nix_lf_alloc_rsp *rsp);
void mbox_handler_nix_txsch_alloc(struct otx2_nic *pf,
				  struct nix_txsch_alloc_rsp *rsp);
#endif /* OTX2_COMMON_H */
