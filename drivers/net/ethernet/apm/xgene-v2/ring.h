/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#ifndef __XGENE_ENET_V2_RING_H__
#define __XGENE_ENET_V2_RING_H__

#define XGENE_ENET_DESC_SIZE	64
#define XGENE_ENET_NUM_DESC	256
#define NUM_BUFS		8
#define SLOT_EMPTY		0xfff

#define DMATXCTRL		0xa180
#define DMATXDESCL		0xa184
#define DMATXDESCH		0xa1a0
#define DMATXSTATUS		0xa188
#define DMARXCTRL		0xa18c
#define DMARXDESCL		0xa190
#define DMARXDESCH		0xa1a4
#define DMARXSTATUS		0xa194
#define DMAINTRMASK		0xa198
#define DMAINTERRUPT		0xa19c

#define D_POS			62
#define D_LEN			2
#define E_POS			63
#define E_LEN			1
#define PKT_ADDRL_POS		0
#define PKT_ADDRL_LEN		32
#define PKT_ADDRH_POS		32
#define PKT_ADDRH_LEN		10
#define PKT_SIZE_POS		32
#define PKT_SIZE_LEN		12
#define NEXT_DESC_ADDRL_POS	0
#define NEXT_DESC_ADDRL_LEN	32
#define NEXT_DESC_ADDRH_POS	48
#define NEXT_DESC_ADDRH_LEN	10

#define TXPKTCOUNT_POS		16
#define TXPKTCOUNT_LEN		8
#define RXPKTCOUNT_POS		16
#define RXPKTCOUNT_LEN		8

#define TX_PKT_SENT		BIT(0)
#define TX_BUS_ERROR		BIT(3)
#define RX_PKT_RCVD		BIT(4)
#define RX_BUS_ERROR		BIT(7)
#define RXSTATUS_RXPKTRCVD	BIT(0)

struct xge_raw_desc {
	__le64 m0;
	__le64 m1;
	__le64 m2;
	__le64 m3;
	__le64 m4;
	__le64 m5;
	__le64 m6;
	__le64 m7;
};

struct pkt_info {
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	void *pkt_buf;
};

/* software context of a descriptor ring */
struct xge_desc_ring {
	struct net_device *ndev;
	dma_addr_t dma_addr;
	u8 head;
	u8 tail;
	union {
		void *desc_addr;
		struct xge_raw_desc *raw_desc;
	};
	struct pkt_info (*pkt_info);
};

static inline u64 xge_set_desc_bits(int pos, int len, u64 val)
{
	return (val & ((1ULL << len) - 1)) << pos;
}

static inline u64 xge_get_desc_bits(int pos, int len, u64 src)
{
	return (src >> pos) & ((1ULL << len) - 1);
}

#define SET_BITS(field, val) \
		xge_set_desc_bits(field ## _POS, field ## _LEN, val)

#define GET_BITS(field, src) \
		xge_get_desc_bits(field ## _POS, field ## _LEN, src)

void xge_setup_desc(struct xge_desc_ring *ring);
void xge_update_tx_desc_addr(struct xge_pdata *pdata);
void xge_update_rx_desc_addr(struct xge_pdata *pdata);
void xge_intr_enable(struct xge_pdata *pdata);
void xge_intr_disable(struct xge_pdata *pdata);

#endif  /* __XGENE_ENET_V2_RING_H__ */
