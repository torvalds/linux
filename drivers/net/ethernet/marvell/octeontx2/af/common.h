/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell.
 */

#ifndef COMMON_H
#define COMMON_H

#include "rvu_struct.h"

#define OTX2_ALIGN			128  /* Align to cacheline */

#define Q_SIZE_16		0ULL /* 16 entries */
#define Q_SIZE_64		1ULL /* 64 entries */
#define Q_SIZE_256		2ULL
#define Q_SIZE_1K		3ULL
#define Q_SIZE_4K		4ULL
#define Q_SIZE_16K		5ULL
#define Q_SIZE_64K		6ULL
#define Q_SIZE_256K		7ULL
#define Q_SIZE_1M		8ULL /* Million entries */
#define Q_SIZE_MIN		Q_SIZE_16
#define Q_SIZE_MAX		Q_SIZE_1M

#define Q_COUNT(x)		(16ULL << (2 * x))
#define Q_SIZE(x, n)		((ilog2(x) - (n)) / 2)

/* Admin queue info */

/* Since we intend to add only one instruction at a time,
 * keep queue size to it's minimum.
 */
#define AQ_SIZE			Q_SIZE_16
/* HW head & tail pointer mask */
#define AQ_PTR_MASK		0xFFFFF

struct qmem {
	void            *base;
	dma_addr_t	iova;
	int		alloc_sz;
	u32		entry_sz;
	u8		align;
	u32		qsize;
};

static inline int qmem_alloc(struct device *dev, struct qmem **q,
			     int qsize, int entry_sz)
{
	struct qmem *qmem;
	int aligned_addr;

	if (!qsize)
		return -EINVAL;

	*q = devm_kzalloc(dev, sizeof(*qmem), GFP_KERNEL);
	if (!*q)
		return -ENOMEM;
	qmem = *q;

	qmem->entry_sz = entry_sz;
	qmem->alloc_sz = (qsize * entry_sz) + OTX2_ALIGN;
	qmem->base = dma_alloc_attrs(dev, qmem->alloc_sz, &qmem->iova,
				     GFP_KERNEL, DMA_ATTR_FORCE_CONTIGUOUS);
	if (!qmem->base)
		return -ENOMEM;

	qmem->qsize = qsize;

	aligned_addr = ALIGN((u64)qmem->iova, OTX2_ALIGN);
	qmem->align = (aligned_addr - qmem->iova);
	qmem->base += qmem->align;
	qmem->iova += qmem->align;
	return 0;
}

static inline void qmem_free(struct device *dev, struct qmem *qmem)
{
	if (!qmem)
		return;

	if (qmem->base)
		dma_free_attrs(dev, qmem->alloc_sz,
			       qmem->base - qmem->align,
			       qmem->iova - qmem->align,
			       DMA_ATTR_FORCE_CONTIGUOUS);
	devm_kfree(dev, qmem);
}

struct admin_queue {
	struct qmem	*inst;
	struct qmem	*res;
	spinlock_t	lock; /* Serialize inst enqueue from PFs */
};

/* NPA aura count */
enum npa_aura_sz {
	NPA_AURA_SZ_0,
	NPA_AURA_SZ_128,
	NPA_AURA_SZ_256,
	NPA_AURA_SZ_512,
	NPA_AURA_SZ_1K,
	NPA_AURA_SZ_2K,
	NPA_AURA_SZ_4K,
	NPA_AURA_SZ_8K,
	NPA_AURA_SZ_16K,
	NPA_AURA_SZ_32K,
	NPA_AURA_SZ_64K,
	NPA_AURA_SZ_128K,
	NPA_AURA_SZ_256K,
	NPA_AURA_SZ_512K,
	NPA_AURA_SZ_1M,
	NPA_AURA_SZ_MAX,
};

#define NPA_AURA_COUNT(x)	(1ULL << ((x) + 6))

/* NPA AQ result structure for init/read/write of aura HW contexts */
struct npa_aq_aura_res {
	struct	npa_aq_res_s	res;
	struct	npa_aura_s	aura_ctx;
	struct	npa_aura_s	ctx_mask;
};

/* NPA AQ result structure for init/read/write of pool HW contexts */
struct npa_aq_pool_res {
	struct	npa_aq_res_s	res;
	struct	npa_pool_s	pool_ctx;
	struct	npa_pool_s	ctx_mask;
};

/* NIX Transmit schedulers */
enum nix_scheduler {
	NIX_TXSCH_LVL_SMQ = 0x0,
	NIX_TXSCH_LVL_MDQ = 0x0,
	NIX_TXSCH_LVL_TL4 = 0x1,
	NIX_TXSCH_LVL_TL3 = 0x2,
	NIX_TXSCH_LVL_TL2 = 0x3,
	NIX_TXSCH_LVL_TL1 = 0x4,
	NIX_TXSCH_LVL_CNT = 0x5,
};

#define TXSCH_RR_QTM_MAX		((1 << 24) - 1)
#define TXSCH_TL1_DFLT_RR_QTM		TXSCH_RR_QTM_MAX
#define TXSCH_TL1_DFLT_RR_PRIO		(0x7ull)
#define CN10K_MAX_DWRR_WEIGHT          16384 /* Weight is 14bit on CN10K */

/* Don't change the order as on CN10K (except CN10KB)
 * SMQX_CFG[SDP] value should be 1 for SDP flows.
 */
#define SMQ_LINK_TYPE_RPM		0
#define SMQ_LINK_TYPE_SDP		1
#define SMQ_LINK_TYPE_LBK		2

/* Min/Max packet sizes, excluding FCS */
#define	NIC_HW_MIN_FRS			40
#define	NIC_HW_MAX_FRS			9212
#define	SDP_HW_MAX_FRS			65535
#define	SDP_HW_MIN_FRS			16
#define CN10K_LMAC_LINK_MAX_FRS		16380 /* 16k - FCS */
#define CN10K_LBK_LINK_MAX_FRS		65535 /* 64k */
#define SDP_LINK_CREDIT			0x320202

/* NIX RX action operation*/
#define NIX_RX_ACTIONOP_DROP		(0x0ull)
#define NIX_RX_ACTIONOP_UCAST		(0x1ull)
#define NIX_RX_ACTIONOP_UCAST_IPSEC	(0x2ull)
#define NIX_RX_ACTIONOP_MCAST		(0x3ull)
#define NIX_RX_ACTIONOP_RSS		(0x4ull)
/* Use the RX action set in the default unicast entry */
#define NIX_RX_ACTION_DEFAULT		(0xfull)

/* NIX TX action operation*/
#define NIX_TX_ACTIONOP_DROP		(0x0ull)
#define NIX_TX_ACTIONOP_UCAST_DEFAULT	(0x1ull)
#define NIX_TX_ACTIONOP_UCAST_CHAN	(0x2ull)
#define NIX_TX_ACTIONOP_MCAST		(0x3ull)
#define NIX_TX_ACTIONOP_DROP_VIOL	(0x5ull)

#define NPC_MCAM_KEY_X1			0
#define NPC_MCAM_KEY_X2			1
#define NPC_MCAM_KEY_X4			2

#define NIX_INTFX_RX(a)			(0x0ull | (a) << 1)
#define NIX_INTFX_TX(a)			(0x1ull | (a) << 1)

/* Default interfaces are NIX0_RX and NIX0_TX */
#define NIX_INTF_RX			NIX_INTFX_RX(0)
#define NIX_INTF_TX			NIX_INTFX_TX(0)

#define NIX_INTF_TYPE_CGX		0
#define NIX_INTF_TYPE_LBK		1
#define NIX_INTF_TYPE_SDP		2

#define MAX_LMAC_PKIND			12
#define NIX_LINK_CGX_LMAC(a, b)		(0 + 4 * (a) + (b))
#define NIX_LINK_LBK(a)			(12 + (a))
#define NIX_CHAN_CGX_LMAC_CHX(a, b, c)	(0x800 + 0x100 * (a) + 0x10 * (b) + (c))
#define NIX_CHAN_LBK_CHX(a, b)		(0 + 0x100 * (a) + (b))
#define NIX_CHAN_SDP_CH_START          (0x700ull)
#define NIX_CHAN_SDP_CHX(a)            (NIX_CHAN_SDP_CH_START + (a))
#define NIX_CHAN_SDP_NUM_CHANS		256
#define NIX_CHAN_CPT_CH_START          (0x800ull)

/* The mask is to extract lower 10-bits of channel number
 * which CPT will pass to X2P.
 */
#define NIX_CHAN_CPT_X2P_MASK          (0x3ffull)

/* NIX LSO format indices.
 * As of now TSO is the only one using, so statically assigning indices.
 */
#define NIX_LSO_FORMAT_IDX_TSOV4	0
#define NIX_LSO_FORMAT_IDX_TSOV6	1

/* RSS info */
#define MAX_RSS_GROUPS			8
/* Group 0 has to be used in default pkt forwarding MCAM entries
 * reserved for NIXLFs. Groups 1-7 can be used for RSS for ntuple
 * filters.
 */
#define DEFAULT_RSS_CONTEXT_GROUP	0
#define MAX_RSS_INDIR_TBL_SIZE		256 /* 1 << Max adder bits */

/* NDC info */
enum ndc_idx_e {
	NIX0_RX = 0x0,
	NIX0_TX = 0x1,
	NPA0_U  = 0x2,
	NIX1_RX = 0x4,
	NIX1_TX = 0x5,
};

enum ndc_ctype_e {
	CACHING = 0x0,
	BYPASS = 0x1,
};

#define NDC_MAX_PORT 6
#define NDC_READ_TRANS 0
#define NDC_WRITE_TRANS 1

#endif /* COMMON_H */
