/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 */

/* EMAC DMA HW engine uses three rings:
 * Tx:
 *   TPD: Transmit Packet Descriptor ring.
 * Rx:
 *   RFD: Receive Free Descriptor ring.
 *     Ring of descriptors with empty buffers to be filled by Rx HW.
 *   RRD: Receive Return Descriptor ring.
 *     Ring of descriptors with buffers filled with received data.
 */

#ifndef _EMAC_HW_H_
#define _EMAC_HW_H_

/* EMAC_CSR register offsets */
#define EMAC_EMAC_WRAPPER_CSR1                                0x000000
#define EMAC_EMAC_WRAPPER_CSR2                                0x000004
#define EMAC_EMAC_WRAPPER_TX_TS_LO                            0x000104
#define EMAC_EMAC_WRAPPER_TX_TS_HI                            0x000108
#define EMAC_EMAC_WRAPPER_TX_TS_INX                           0x00010c

/* DMA Order Settings */
enum emac_dma_order {
	emac_dma_ord_in = 1,
	emac_dma_ord_enh = 2,
	emac_dma_ord_out = 4
};

enum emac_dma_req_block {
	emac_dma_req_128 = 0,
	emac_dma_req_256 = 1,
	emac_dma_req_512 = 2,
	emac_dma_req_1024 = 3,
	emac_dma_req_2048 = 4,
	emac_dma_req_4096 = 5
};

/* Returns the value of bits idx...idx+n_bits */
#define BITS_GET(val, lo, hi) ((le32_to_cpu(val) & GENMASK((hi), (lo))) >> lo)
#define BITS_SET(val, lo, hi, new_val) \
	val = cpu_to_le32((le32_to_cpu(val) & (~GENMASK((hi), (lo)))) |	\
		(((new_val) << (lo)) & GENMASK((hi), (lo))))

/* RRD (Receive Return Descriptor) */
struct emac_rrd {
	u32	word[6];

/* number of RFD */
#define RRD_NOR(rrd)			BITS_GET((rrd)->word[0], 16, 19)
/* start consumer index of rfd-ring */
#define RRD_SI(rrd)			BITS_GET((rrd)->word[0], 20, 31)
/* vlan-tag (CVID, CFI and PRI) */
#define RRD_CVALN_TAG(rrd)		BITS_GET((rrd)->word[2], 0, 15)
/* length of the packet */
#define RRD_PKT_SIZE(rrd)		BITS_GET((rrd)->word[3], 0, 13)
/* L4(TCP/UDP) checksum failed */
#define RRD_L4F(rrd)			BITS_GET((rrd)->word[3], 14, 14)
/* vlan tagged */
#define RRD_CVTAG(rrd)			BITS_GET((rrd)->word[3], 16, 16)
/* When set, indicates that the descriptor is updated by the IP core.
 * When cleared, indicates that the descriptor is invalid.
 */
#define RRD_UPDT(rrd)			BITS_GET((rrd)->word[3], 31, 31)
#define RRD_UPDT_SET(rrd, val)		BITS_SET((rrd)->word[3], 31, 31, val)
/* timestamp low */
#define RRD_TS_LOW(rrd)			BITS_GET((rrd)->word[4], 0, 29)
/* timestamp high */
#define RRD_TS_HI(rrd)			le32_to_cpu((rrd)->word[5])
};

/* TPD (Transmit Packet Descriptor) */
struct emac_tpd {
	u32				word[4];

/* Number of bytes of the transmit packet. (include 4-byte CRC) */
#define TPD_BUF_LEN_SET(tpd, val)	BITS_SET((tpd)->word[0], 0, 15, val)
/* Custom Checksum Offload: When set, ask IP core to offload custom checksum */
#define TPD_CSX_SET(tpd, val)		BITS_SET((tpd)->word[1], 8, 8, val)
/* TCP Large Send Offload: When set, ask IP core to do offload TCP Large Send */
#define TPD_LSO(tpd)			BITS_GET((tpd)->word[1], 12, 12)
#define TPD_LSO_SET(tpd, val)		BITS_SET((tpd)->word[1], 12, 12, val)
/*  Large Send Offload Version: When set, indicates this is an LSOv2
 * (for both IPv4 and IPv6). When cleared, indicates this is an LSOv1
 * (only for IPv4).
 */
#define TPD_LSOV_SET(tpd, val)		BITS_SET((tpd)->word[1], 13, 13, val)
/* IPv4 packet: When set, indicates this is an  IPv4 packet, this bit is only
 * for LSOV2 format.
 */
#define TPD_IPV4_SET(tpd, val)		BITS_SET((tpd)->word[1], 16, 16, val)
/* 0: Ethernet   frame (DA+SA+TYPE+DATA+CRC)
 * 1: IEEE 802.3 frame (DA+SA+LEN+DSAP+SSAP+CTL+ORG+TYPE+DATA+CRC)
 */
#define TPD_TYP_SET(tpd, val)		BITS_SET((tpd)->word[1], 17, 17, val)
/* Low-32bit Buffer Address */
#define TPD_BUFFER_ADDR_L_SET(tpd, val)	((tpd)->word[2] = cpu_to_le32(val))
/* CVLAN Tag to be inserted if INS_VLAN_TAG is set, CVLAN TPID based on global
 * register configuration.
 */
#define TPD_CVLAN_TAG_SET(tpd, val)	BITS_SET((tpd)->word[3], 0, 15, val)
/*  Insert CVlan Tag: When set, ask MAC to insert CVLAN TAG to outgoing packet
 */
#define TPD_INSTC_SET(tpd, val)		BITS_SET((tpd)->word[3], 17, 17, val)
/* High-14bit Buffer Address, So, the 64b-bit address is
 * {DESC_CTRL_11_TX_DATA_HIADDR[17:0],(register) BUFFER_ADDR_H, BUFFER_ADDR_L}
 * Extend TPD_BUFFER_ADDR_H to [31, 18], because we never enable timestamping.
 */
#define TPD_BUFFER_ADDR_H_SET(tpd, val)	BITS_SET((tpd)->word[3], 18, 31, val)
/* Format D. Word offset from the 1st byte of this packet to start to calculate
 * the custom checksum.
 */
#define TPD_PAYLOAD_OFFSET_SET(tpd, val) BITS_SET((tpd)->word[1], 0, 7, val)
/*  Format D. Word offset from the 1st byte of this packet to fill the custom
 * checksum to
 */
#define TPD_CXSUM_OFFSET_SET(tpd, val)	BITS_SET((tpd)->word[1], 18, 25, val)

/* Format C. TCP Header offset from the 1st byte of this packet. (byte unit) */
#define TPD_TCPHDR_OFFSET_SET(tpd, val)	BITS_SET((tpd)->word[1], 0, 7, val)
/* Format C. MSS (Maximum Segment Size) got from the protocol layer. (byte unit)
 */
#define TPD_MSS_SET(tpd, val)		BITS_SET((tpd)->word[1], 18, 30, val)
/* packet length in ext tpd */
#define TPD_PKT_LEN_SET(tpd, val)	((tpd)->word[2] = cpu_to_le32(val))
};

/* emac_ring_header represents a single, contiguous block of DMA space
 * mapped for the three descriptor rings (tpd, rfd, rrd)
 */
struct emac_ring_header {
	void			*v_addr;	/* virtual address */
	dma_addr_t		dma_addr;	/* dma address */
	size_t			size;		/* length in bytes */
	size_t			used;
};

/* emac_buffer is wrapper around a pointer to a socket buffer
 * so a DMA handle can be stored along with the skb
 */
struct emac_buffer {
	struct sk_buff		*skb;		/* socket buffer */
	u16			length;		/* rx buffer length */
	dma_addr_t		dma_addr;	/* dma address */
};

/* receive free descriptor (rfd) ring */
struct emac_rfd_ring {
	struct emac_buffer	*rfbuff;
	u32			*v_addr;	/* virtual address */
	dma_addr_t		dma_addr;	/* dma address */
	size_t			size;		/* length in bytes */
	unsigned int		count;		/* number of desc in the ring */
	unsigned int		produce_idx;
	unsigned int		process_idx;
	unsigned int		consume_idx;	/* unused */
};

/* Receive Return Desciptor (RRD) ring */
struct emac_rrd_ring {
	u32			*v_addr;	/* virtual address */
	dma_addr_t		dma_addr;	/* physical address */
	size_t			size;		/* length in bytes */
	unsigned int		count;		/* number of desc in the ring */
	unsigned int		produce_idx;	/* unused */
	unsigned int		consume_idx;
};

/* Rx queue */
struct emac_rx_queue {
	struct net_device	*netdev;	/* netdev ring belongs to */
	struct emac_rrd_ring	rrd;
	struct emac_rfd_ring	rfd;
	struct napi_struct	napi;
	struct emac_irq		*irq;

	u32			intr;
	u32			produce_mask;
	u32			process_mask;
	u32			consume_mask;

	u16			produce_reg;
	u16			process_reg;
	u16			consume_reg;

	u8			produce_shift;
	u8			process_shft;
	u8			consume_shift;
};

/* Transimit Packet Descriptor (tpd) ring */
struct emac_tpd_ring {
	struct emac_buffer	*tpbuff;
	u32			*v_addr;	/* virtual address */
	dma_addr_t		dma_addr;	/* dma address */

	size_t			size;		/* length in bytes */
	unsigned int		count;		/* number of desc in the ring */
	unsigned int		produce_idx;
	unsigned int		consume_idx;
	unsigned int		last_produce_idx;
};

/* Tx queue */
struct emac_tx_queue {
	struct emac_tpd_ring	tpd;

	u32			produce_mask;
	u32			consume_mask;

	u16			max_packets;	/* max packets per interrupt */
	u16			produce_reg;
	u16			consume_reg;

	u8			produce_shift;
	u8			consume_shift;
};

struct emac_adapter;

int  emac_mac_up(struct emac_adapter *adpt);
void emac_mac_down(struct emac_adapter *adpt);
void emac_mac_reset(struct emac_adapter *adpt);
void emac_mac_stop(struct emac_adapter *adpt);
void emac_mac_mode_config(struct emac_adapter *adpt);
void emac_mac_rx_process(struct emac_adapter *adpt, struct emac_rx_queue *rx_q,
			 int *num_pkts, int max_pkts);
int emac_mac_tx_buf_send(struct emac_adapter *adpt, struct emac_tx_queue *tx_q,
			 struct sk_buff *skb);
void emac_mac_tx_process(struct emac_adapter *adpt, struct emac_tx_queue *tx_q);
void emac_mac_rx_tx_ring_init_all(struct platform_device *pdev,
				  struct emac_adapter *adpt);
int  emac_mac_rx_tx_rings_alloc_all(struct emac_adapter *adpt);
void emac_mac_rx_tx_rings_free_all(struct emac_adapter *adpt);
void emac_mac_multicast_addr_clear(struct emac_adapter *adpt);
void emac_mac_multicast_addr_set(struct emac_adapter *adpt, u8 *addr);

#endif /*_EMAC_HW_H_*/
