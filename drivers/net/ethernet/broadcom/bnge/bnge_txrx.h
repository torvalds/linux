/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_TXRX_H_
#define _BNGE_TXRX_H_

#include <linux/bnge/hsi.h>
#include "bnge_netdev.h"

static inline u32 bnge_tx_avail(struct bnge_net *bn,
				const struct bnge_tx_ring_info *txr)
{
	u32 used = READ_ONCE(txr->tx_prod) - READ_ONCE(txr->tx_cons);

	return bn->tx_ring_size - (used & bn->tx_ring_mask);
}

static inline void bnge_writeq_relaxed(struct bnge_dev *bd, u64 val,
				       void __iomem *addr)
{
#if BITS_PER_LONG == 32
	spin_lock(&bd->db_lock);
	lo_hi_writeq_relaxed(val, addr);
	spin_unlock(&bd->db_lock);
#else
	writeq_relaxed(val, addr);
#endif
}

/* For TX and RX ring doorbells with no ordering guarantee*/
static inline void bnge_db_write_relaxed(struct bnge_net *bn,
					 struct bnge_db_info *db, u32 idx)
{
	bnge_writeq_relaxed(bn->bd, db->db_key64 | DB_RING_IDX(db, idx),
			    db->doorbell);
}

#define TX_OPAQUE_IDX_MASK	0x0000ffff
#define TX_OPAQUE_BDS_MASK	0x00ff0000
#define TX_OPAQUE_BDS_SHIFT	16
#define TX_OPAQUE_RING_MASK	0xff000000
#define TX_OPAQUE_RING_SHIFT	24

#define SET_TX_OPAQUE(bn, txr, idx, bds)				\
	(((txr)->tx_napi_idx << TX_OPAQUE_RING_SHIFT) |			\
	 ((bds) << TX_OPAQUE_BDS_SHIFT) | ((idx) & (bn)->tx_ring_mask))

#define TX_OPAQUE_IDX(opq)	((opq) & TX_OPAQUE_IDX_MASK)
#define TX_OPAQUE_RING(opq)	(((opq) & TX_OPAQUE_RING_MASK) >>	\
				 TX_OPAQUE_RING_SHIFT)
#define TX_OPAQUE_BDS(opq)	(((opq) & TX_OPAQUE_BDS_MASK) >>	\
				 TX_OPAQUE_BDS_SHIFT)
#define TX_OPAQUE_PROD(bn, opq)	((TX_OPAQUE_IDX(opq) + TX_OPAQUE_BDS(opq)) &\
				 (bn)->tx_ring_mask)
#define TX_BD_CNT(n)	(((n) << TX_BD_FLAGS_BD_CNT_SHIFT) & TX_BD_FLAGS_BD_CNT)

#define TX_MAX_BD_CNT	32

#define TX_MAX_FRAGS		(TX_MAX_BD_CNT - 2)

/* Minimum TX BDs for a TX packet with MAX_SKB_FRAGS + 1.  We need one extra
 * BD because the first TX BD is always a long BD.
 */
#define BNGE_MIN_TX_DESC_CNT		(MAX_SKB_FRAGS + 2)

#define RX_RING(bn, x)	(((x) & (bn)->rx_ring_mask) >> (BNGE_PAGE_SHIFT - 4))
#define RX_AGG_RING(bn, x)	(((x) & (bn)->rx_agg_ring_mask) >>	\
				 (BNGE_PAGE_SHIFT - 4))
#define RX_IDX(x)	((x) & (RX_DESC_CNT - 1))

#define TX_RING(bn, x)	(((x) & (bn)->tx_ring_mask) >> (BNGE_PAGE_SHIFT - 4))
#define TX_IDX(x)	((x) & (TX_DESC_CNT - 1))

#define CP_RING(x)	(((x) & ~(CP_DESC_CNT - 1)) >> (BNGE_PAGE_SHIFT - 4))
#define CP_IDX(x)	((x) & (CP_DESC_CNT - 1))

#define TX_CMP_VALID(bn, txcmp, raw_cons)				\
	(!!((txcmp)->tx_cmp_errors_v & cpu_to_le32(TX_CMP_V)) ==	\
	 !((raw_cons) & (bn)->cp_bit))

#define RX_CMP_VALID(bn, rxcmp1, raw_cons)				\
	(!!((rxcmp1)->rx_cmp_cfa_code_errors_v2 & cpu_to_le32(RX_CMP_V)) ==\
	 !((raw_cons) & (bn)->cp_bit))

#define RX_AGG_CMP_VALID(bn, agg, raw_cons)			\
	(!!((agg)->rx_agg_cmp_v & cpu_to_le32(RX_AGG_CMP_V)) ==	\
	 !((raw_cons) & (bn)->cp_bit))

#define NQ_CMP_VALID(bn, nqcmp, raw_cons)				\
	(!!((nqcmp)->v & cpu_to_le32(NQ_CN_V)) == !((raw_cons) & (bn)->cp_bit))

#define TX_CMP_TYPE(txcmp)					\
	(le32_to_cpu((txcmp)->tx_cmp_flags_type) & CMP_TYPE)

#define RX_CMP_TYPE(rxcmp)					\
	(le32_to_cpu((rxcmp)->rx_cmp_len_flags_type) & RX_CMP_CMP_TYPE)

#define RING_RX(bn, idx)	((idx) & (bn)->rx_ring_mask)
#define NEXT_RX(idx)		((idx) + 1)

#define RING_RX_AGG(bn, idx)	((idx) & (bn)->rx_agg_ring_mask)
#define NEXT_RX_AGG(idx)	((idx) + 1)

#define SW_TX_RING(bn, idx)	((idx) & (bn)->tx_ring_mask)
#define NEXT_TX(idx)		((idx) + 1)

#define ADV_RAW_CMP(idx, n)	((idx) + (n))
#define NEXT_RAW_CMP(idx)	ADV_RAW_CMP(idx, 1)
#define RING_CMP(bn, idx)	((idx) & (bn)->cp_ring_mask)
#define NEXT_CMP(bn, idx)	RING_CMP(bn, ADV_RAW_CMP(idx, 1))

#define RX_CMP_ITYPES(rxcmp)					\
	(le32_to_cpu((rxcmp)->rx_cmp_len_flags_type) & RX_CMP_FLAGS_ITYPES_MASK)

#define RX_CMP_CFA_CODE(rxcmpl1)					\
	((le32_to_cpu((rxcmpl1)->rx_cmp_cfa_code_errors_v2) &		\
	  RX_CMPL_CFA_CODE_MASK) >> RX_CMPL_CFA_CODE_SFT)

irqreturn_t bnge_msix(int irq, void *dev_instance);
netdev_tx_t bnge_start_xmit(struct sk_buff *skb, struct net_device *dev);
void bnge_reuse_rx_data(struct bnge_rx_ring_info *rxr, u16 cons, void *data);
int bnge_napi_poll(struct napi_struct *napi, int budget);
netdev_features_t bnge_features_check(struct sk_buff *skb,
				      struct net_device *dev,
				      netdev_features_t features);
#endif /* _BNGE_TXRX_H_ */
