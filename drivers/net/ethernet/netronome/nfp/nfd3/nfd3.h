/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

#ifndef _NFP_DP_NFD3_H_
#define _NFP_DP_NFD3_H_

struct sk_buff;
struct net_device;

/* TX descriptor format */

#define NFD3_DESC_TX_EOP		BIT(7)
#define NFD3_DESC_TX_OFFSET_MASK	GENMASK(6, 0)
#define NFD3_DESC_TX_MSS_MASK		GENMASK(13, 0)

/* Flags in the host TX descriptor */
#define NFD3_DESC_TX_CSUM		BIT(7)
#define NFD3_DESC_TX_IP4_CSUM		BIT(6)
#define NFD3_DESC_TX_TCP_CSUM		BIT(5)
#define NFD3_DESC_TX_UDP_CSUM		BIT(4)
#define NFD3_DESC_TX_VLAN		BIT(3)
#define NFD3_DESC_TX_LSO		BIT(2)
#define NFD3_DESC_TX_ENCAP		BIT(1)
#define NFD3_DESC_TX_O_IP4_CSUM	BIT(0)

struct nfp_nfd3_tx_desc {
	union {
		struct {
			u8 dma_addr_hi; /* High bits of host buf address */
			__le16 dma_len;	/* Length to DMA for this desc */
			u8 offset_eop;	/* Offset in buf where pkt starts +
					 * highest bit is eop flag.
					 */
			__le32 dma_addr_lo; /* Low 32bit of host buf addr */

			__le16 mss;	/* MSS to be used for LSO */
			u8 lso_hdrlen;	/* LSO, TCP payload offset */
			u8 flags;	/* TX Flags, see @NFD3_DESC_TX_* */
			union {
				struct {
					u8 l3_offset; /* L3 header offset */
					u8 l4_offset; /* L4 header offset */
				};
				__le16 vlan; /* VLAN tag to add if indicated */
			};
			__le16 data_len; /* Length of frame + meta data */
		} __packed;
		__le32 vals[4];
		__le64 vals8[2];
	};
};

/**
 * struct nfp_nfd3_tx_buf - software TX buffer descriptor
 * @skb:	normal ring, sk_buff associated with this buffer
 * @frag:	XDP ring, page frag associated with this buffer
 * @xdp:	XSK buffer pool handle (for AF_XDP)
 * @dma_addr:	DMA mapping address of the buffer
 * @fidx:	Fragment index (-1 for the head and [0..nr_frags-1] for frags)
 * @pkt_cnt:	Number of packets to be produced out of the skb associated
 *		with this buffer (valid only on the head's buffer).
 *		Will be 1 for all non-TSO packets.
 * @is_xsk_tx:	Flag if buffer is a RX buffer after a XDP_TX action and not a
 *		buffer from the TX queue (for AF_XDP).
 * @real_len:	Number of bytes which to be produced out of the skb (valid only
 *		on the head's buffer). Equal to skb->len for non-TSO packets.
 */
struct nfp_nfd3_tx_buf {
	union {
		struct sk_buff *skb;
		void *frag;
		struct xdp_buff *xdp;
	};
	dma_addr_t dma_addr;
	union {
		struct {
			short int fidx;
			u16 pkt_cnt;
		};
		struct {
			bool is_xsk_tx;
		};
	};
	u32 real_len;
};

void
nfp_nfd3_rx_csum(const struct nfp_net_dp *dp, struct nfp_net_r_vector *r_vec,
		 const struct nfp_net_rx_desc *rxd,
		 const struct nfp_meta_parsed *meta, struct sk_buff *skb);
bool
nfp_nfd3_parse_meta(struct net_device *netdev, struct nfp_meta_parsed *meta,
		    void *data, void *pkt, unsigned int pkt_len, int meta_len);
void nfp_nfd3_tx_complete(struct nfp_net_tx_ring *tx_ring, int budget);
int nfp_nfd3_poll(struct napi_struct *napi, int budget);
netdev_tx_t nfp_nfd3_tx(struct sk_buff *skb, struct net_device *netdev);
bool
nfp_nfd3_ctrl_tx_one(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
		     struct sk_buff *skb, bool old);
void nfp_nfd3_ctrl_poll(struct tasklet_struct *t);
void nfp_nfd3_rx_ring_fill_freelist(struct nfp_net_dp *dp,
				    struct nfp_net_rx_ring *rx_ring);
void nfp_nfd3_xsk_tx_free(struct nfp_nfd3_tx_buf *txbuf);
int nfp_nfd3_xsk_poll(struct napi_struct *napi, int budget);

#endif
