/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef _NFP_DP_NFDK_H_
#define _NFP_DP_NFDK_H_

#include <linux/bitops.h>
#include <linux/types.h>

#define NFDK_TX_DESC_PER_SIMPLE_PKT	2

#define NFDK_TX_MAX_DATA_PER_HEAD	SZ_4K
#define NFDK_TX_MAX_DATA_PER_DESC	SZ_16K
#define NFDK_TX_DESC_BLOCK_SZ		256
#define NFDK_TX_DESC_BLOCK_CNT		(NFDK_TX_DESC_BLOCK_SZ /	\
					 sizeof(struct nfp_nfdk_tx_desc))
#define NFDK_TX_DESC_STOP_CNT		(NFDK_TX_DESC_BLOCK_CNT *	\
					 NFDK_TX_DESC_PER_SIMPLE_PKT)
#define NFDK_TX_MAX_DATA_PER_BLOCK	SZ_64K
#define NFDK_TX_DESC_GATHER_MAX		17

/* TX descriptor format */

#define NFDK_DESC_TX_MSS_MASK		GENMASK(13, 0)

#define NFDK_DESC_TX_CHAIN_META		BIT(3)
#define NFDK_DESC_TX_ENCAP		BIT(2)
#define NFDK_DESC_TX_L4_CSUM		BIT(1)
#define NFDK_DESC_TX_L3_CSUM		BIT(0)

#define NFDK_DESC_TX_DMA_LEN_HEAD	GENMASK(11, 0)
#define NFDK_DESC_TX_TYPE_HEAD		GENMASK(15, 12)
#define NFDK_DESC_TX_DMA_LEN		GENMASK(13, 0)
#define NFDK_DESC_TX_TYPE_NOP		0
#define NFDK_DESC_TX_TYPE_GATHER	1
#define NFDK_DESC_TX_TYPE_TSO		2
#define NFDK_DESC_TX_TYPE_SIMPLE	8
#define NFDK_DESC_TX_EOP		BIT(14)

#define NFDK_META_LEN			GENMASK(7, 0)
#define NFDK_META_FIELDS		GENMASK(31, 8)

#define D_BLOCK_CPL(idx)		(NFDK_TX_DESC_BLOCK_CNT -	\
					 (idx) % NFDK_TX_DESC_BLOCK_CNT)

struct nfp_nfdk_tx_desc {
	union {
		struct {
			u8 dma_addr_hi;  /* High bits of host buf address */
			u8 padding;  /* Must be zero */
			__le16 dma_len_type; /* Length to DMA for this desc */
			__le32 dma_addr_lo;  /* Low 32bit of host buf addr */
		};

		struct {
			__le16 mss;	/* MSS to be used for LSO */
			u8 lso_hdrlen;  /* LSO, TCP payload offset */
			u8 lso_totsegs; /* LSO, total segments */
			u8 l3_offset;   /* L3 header offset */
			u8 l4_offset;   /* L4 header offset */
			__le16 lso_meta_res; /* Rsvd bits in TSO metadata */
		};

		struct {
			u8 flags;	/* TX Flags, see @NFDK_DESC_TX_* */
			u8 reserved[7];	/* meta byte placeholder */
		};

		__le32 vals[2];
		__le64 raw;
	};
};

/* The device don't make use of the 2 or 3 least significant bits of the address
 * due to alignment constraints. The driver can make use of those bits to carry
 * information about the buffer before giving it to the device.
 *
 * NOTE: The driver must clear the lower bits before handing the buffer to the
 * device.
 *
 * - NFDK_TX_BUF_INFO_SOP - Start of a packet
 *   Mark the buffer as a start of a packet. This is used in the XDP TX process
 *   to stash virtual and DMA address so that they can be recycled when the TX
 *   operation is completed.
 */
#define NFDK_TX_BUF_PTR(val) ((val) & ~(sizeof(void *) - 1))
#define NFDK_TX_BUF_INFO(val) ((val) & (sizeof(void *) - 1))
#define NFDK_TX_BUF_INFO_SOP BIT(0)

struct nfp_nfdk_tx_buf {
	union {
		/* First slot */
		union {
			struct sk_buff *skb;
			void *frag;
			unsigned long val;
		};

		/* 1 + nr_frags next slots */
		dma_addr_t dma_addr;

		/* TSO (optional) */
		struct {
			u32 pkt_cnt;
			u32 real_len;
		};

		u64 raw;
	};
};

static inline int nfp_nfdk_headlen_to_segs(unsigned int headlen)
{
	/* First descriptor fits less data, so adjust for that */
	return DIV_ROUND_UP(headlen +
			    NFDK_TX_MAX_DATA_PER_DESC -
			    NFDK_TX_MAX_DATA_PER_HEAD,
			    NFDK_TX_MAX_DATA_PER_DESC);
}

int nfp_nfdk_poll(struct napi_struct *napi, int budget);
netdev_tx_t nfp_nfdk_tx(struct sk_buff *skb, struct net_device *netdev);
bool
nfp_nfdk_ctrl_tx_one(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		     struct sk_buff *skb, bool old);
void nfp_nfdk_ctrl_poll(struct tasklet_struct *t);
void nfp_nfdk_rx_ring_fill_freelist(struct nfp_net_dp *dp,
				    struct nfp_net_rx_ring *rx_ring);
#endif
