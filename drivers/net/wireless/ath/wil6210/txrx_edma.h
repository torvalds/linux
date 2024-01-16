/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2012-2016,2018-2019, The Linux Foundation. All rights reserved.
 */

#ifndef WIL6210_TXRX_EDMA_H
#define WIL6210_TXRX_EDMA_H

#include "wil6210.h"

/* limit status ring size in range [ring size..max ring size] */
#define WIL_SRING_SIZE_ORDER_MIN	(WIL_RING_SIZE_ORDER_MIN)
#define WIL_SRING_SIZE_ORDER_MAX	(WIL_RING_SIZE_ORDER_MAX)
/* RX sring order should be bigger than RX ring order */
#define WIL_RX_SRING_SIZE_ORDER_DEFAULT	(12)
#define WIL_TX_SRING_SIZE_ORDER_DEFAULT	(14)
#define WIL_RX_BUFF_ARR_SIZE_DEFAULT (2600)

#define WIL_DEFAULT_RX_STATUS_RING_ID 0
#define WIL_RX_DESC_RING_ID 0
#define WIL_RX_STATUS_IRQ_IDX 0
#define WIL_TX_STATUS_IRQ_IDX 1

#define WIL_EDMA_AGG_WATERMARK (0xffff)
#define WIL_EDMA_AGG_WATERMARK_POS (16)

#define WIL_EDMA_IDLE_TIME_LIMIT_USEC (50)
#define WIL_EDMA_TIME_UNIT_CLK_CYCLES (330) /* fits 1 usec */

/* Error field */
#define WIL_RX_EDMA_ERROR_MIC	(1)
#define WIL_RX_EDMA_ERROR_KEY	(2) /* Key missing */
#define WIL_RX_EDMA_ERROR_REPLAY	(3)
#define WIL_RX_EDMA_ERROR_AMSDU	(4)
#define WIL_RX_EDMA_ERROR_FCS	(7)

#define WIL_RX_EDMA_ERROR_L3_ERR	(BIT(0) | BIT(1))
#define WIL_RX_EDMA_ERROR_L4_ERR	(BIT(0) | BIT(1))

#define WIL_RX_EDMA_DLPF_LU_MISS_BIT		BIT(11)
#define WIL_RX_EDMA_DLPF_LU_MISS_CID_TID_MASK	0x7
#define WIL_RX_EDMA_DLPF_LU_HIT_CID_TID_MASK	0xf

#define WIL_RX_EDMA_DLPF_LU_MISS_CID_POS	2
#define WIL_RX_EDMA_DLPF_LU_HIT_CID_POS		4

#define WIL_RX_EDMA_DLPF_LU_MISS_TID_POS	5

#define WIL_RX_EDMA_MID_VALID_BIT		BIT(20)

#define WIL_EDMA_DESC_TX_MAC_CFG_0_QID_POS 16
#define WIL_EDMA_DESC_TX_MAC_CFG_0_QID_LEN 6

#define WIL_EDMA_DESC_TX_CFG_EOP_POS 0
#define WIL_EDMA_DESC_TX_CFG_EOP_LEN 1

#define WIL_EDMA_DESC_TX_CFG_TSO_DESC_TYPE_POS 3
#define WIL_EDMA_DESC_TX_CFG_TSO_DESC_TYPE_LEN 2

#define WIL_EDMA_DESC_TX_CFG_SEG_EN_POS 5
#define WIL_EDMA_DESC_TX_CFG_SEG_EN_LEN 1

#define WIL_EDMA_DESC_TX_CFG_INSERT_IP_CHKSUM_POS 6
#define WIL_EDMA_DESC_TX_CFG_INSERT_IP_CHKSUM_LEN 1

#define WIL_EDMA_DESC_TX_CFG_INSERT_TCP_CHKSUM_POS 7
#define WIL_EDMA_DESC_TX_CFG_INSERT_TCP_CHKSUM_LEN 1

#define WIL_EDMA_DESC_TX_CFG_L4_TYPE_POS 15
#define WIL_EDMA_DESC_TX_CFG_L4_TYPE_LEN 1

#define WIL_EDMA_DESC_TX_CFG_PSEUDO_HEADER_CALC_EN_POS 5
#define WIL_EDMA_DESC_TX_CFG_PSEUDO_HEADER_CALC_EN_LEN 1

/* Enhanced Rx descriptor - MAC part
 * [dword 0] : Reserved
 * [dword 1] : Reserved
 * [dword 2] : Reserved
 * [dword 3]
 *	bit  0..15 : Buffer ID
 *	bit 16..31 : Reserved
 */
struct wil_ring_rx_enhanced_mac {
	u32 d[3];
	__le16 buff_id;
	u16 reserved;
} __packed;

/* Enhanced Rx descriptor - DMA part
 * [dword 0] - Reserved
 * [dword 1]
 *	bit  0..31 : addr_low:32 The payload buffer address, bits 0-31
 * [dword 2]
 *	bit  0..15 : addr_high_low:16 The payload buffer address, bits 32-47
 *	bit 16..31 : Reserved
 * [dword 3]
 *	bit  0..15 : addr_high_high:16 The payload buffer address, bits 48-63
 *	bit 16..31 : length
 */
struct wil_ring_rx_enhanced_dma {
	u32 d0;
	struct wil_ring_dma_addr addr;
	u16 w5;
	__le16 addr_high_high;
	__le16 length;
} __packed;

struct wil_rx_enhanced_desc {
	struct wil_ring_rx_enhanced_mac mac;
	struct wil_ring_rx_enhanced_dma dma;
} __packed;

/* Enhanced Tx descriptor - DMA part
 * [dword 0]
 *	Same as legacy
 * [dword 1]
 * bit  0..31 : addr_low:32 The payload buffer address, bits 0-31
 * [dword 2]
 * bit  0..15 : addr_high_low:16 The payload buffer address, bits 32-47
 * bit 16..23 : ip_length:8 The IP header length for the TX IP checksum
 *		offload feature
 * bit 24..30 : mac_length:7
 * bit     31 : ip_version:1 1 - IPv4, 0 - IPv6
 * [dword 3]
 * bit  0..15 : addr_high_high:16 The payload buffer address, bits 48-63
 * bit 16..31 : length
 */
struct wil_ring_tx_enhanced_dma {
	u8 l4_hdr_len;
	u8 cmd;
	u16 w1;
	struct wil_ring_dma_addr addr;
	u8  ip_length;
	u8  b11;       /* 0..6: mac_length; 7:ip_version */
	__le16 addr_high_high;
	__le16 length;
} __packed;

/* Enhanced Tx descriptor - MAC part
 * [dword 0]
 * bit  0.. 9 : lifetime_expiry_value:10
 * bit     10 : interrupt_en:1
 * bit     11 : status_en:1
 * bit 12..13 : txss_override:2
 * bit     14 : timestamp_insertion:1
 * bit     15 : duration_preserve:1
 * bit 16..21 : reserved0:6
 * bit 22..26 : mcs_index:5
 * bit     27 : mcs_en:1
 * bit 28..30 : reserved1:3
 * bit     31 : sn_preserved:1
 * [dword 1]
 * bit  0.. 3 : pkt_mode:4
 * bit      4 : pkt_mode_en:1
 * bit  5..14 : reserved0:10
 * bit     15 : ack_policy_en:1
 * bit 16..19 : dst_index:4
 * bit     20 : dst_index_en:1
 * bit 21..22 : ack_policy:2
 * bit     23 : lifetime_en:1
 * bit 24..30 : max_retry:7
 * bit     31 : max_retry_en:1
 * [dword 2]
 * bit  0.. 7 : num_of_descriptors:8
 * bit  8..17 : reserved:10
 * bit 18..19 : l2_translation_type:2 00 - bypass, 01 - 802.3, 10 - 802.11
 * bit     20 : snap_hdr_insertion_en:1
 * bit     21 : vlan_removal_en:1
 * bit 22..23 : reserved0:2
 * bit	   24 : Dest ID extension:1
 * bit 25..31 : reserved0:7
 * [dword 3]
 * bit  0..15 : tso_mss:16
 * bit 16..31 : descriptor_scratchpad:16 - mailbox between driver and ucode
 */
struct wil_ring_tx_enhanced_mac {
	u32 d[3];
	__le16 tso_mss;
	u16 scratchpad;
} __packed;

struct wil_tx_enhanced_desc {
	struct wil_ring_tx_enhanced_mac mac;
	struct wil_ring_tx_enhanced_dma dma;
} __packed;

#define TX_STATUS_DESC_READY_POS 7

/* Enhanced TX status message
 * [dword 0]
 *	bit  0.. 7 : Number of Descriptor:8 - The number of descriptors that
 *		     are used to form the packets. It  is needed for WB when
 *		     releasing the packet
 *	bit  8..15 : tx_ring_id:8 The transmission ring ID that is related to
 *		     the message
 *	bit 16..23 : Status:8 - The TX status Code
 *		0x0 - A successful transmission
 *		0x1 - Retry expired
 *		0x2 - Lifetime Expired
 *		0x3 - Released
 *		0x4-0xFF - Reserved
 *	bit 24..30 : Reserved:7
 *	bit     31 : Descriptor Ready bit:1 - It is initiated to
 *		zero by the driver when the ring is created. It is set by the HW
 *		to one for each completed status message. Each wrap around,
 *		the DR bit value is flipped.
 * [dword 1]
 *	bit 0..31  : timestamp:32 - Set when MPDU is transmitted.
 * [dword 2]
 *	bit  0.. 4 : MCS:5 - The transmitted MCS value
 *	bit      5 : Reserved:1
 *	bit  6.. 7 : CB mode:2 - 0-DMG 1-EDMG 2-Wide
 *	bit  8..12 : QID:5 - The QID that was used for the transmission
 *	bit 13..15 : Reserved:3
 *	bit 16..20 : Num of MSDUs:5 - Number of MSDUs in the aggregation
 *	bit 21..22 : Reserved:2
 *	bit     23 : Retry:1 - An indication that the transmission was retried
 *	bit 24..31 : TX-Sector:8 - the antenna sector that was used for
 *		     transmission
 * [dword 3]
 *	bit  0..11 : Sequence number:12 - The Sequence Number that was used
 *		     for the MPDU transmission
 *	bit 12..31 : Reserved:20
 */
struct wil_ring_tx_status {
	u8 num_descriptors;
	u8 ring_id;
	u8 status;
	u8 desc_ready; /* Only the last bit should be set */
	u32 timestamp;
	u32 d2;
	u16 seq_number; /* Only the first 12 bits */
	u16 w7;
} __packed;

/* Enhanced Rx status message - compressed part
 * [dword 0]
 *	bit  0.. 2 : L2 Rx Status:3 - The L2 packet reception Status
 *		     0-Success, 1-MIC Error, 2-Key Error, 3-Replay Error,
 *		     4-A-MSDU Error, 5-Reserved, 6-Reserved, 7-FCS Error
 *	bit  3.. 4 : L3 Rx Status:2 - Bit0 - L3I - L3 identified and checksum
 *		     calculated, Bit1- L3Err - IPv4 Checksum Error
 *	bit  5.. 6 : L4 Rx Status:2 - Bit0 - L4I - L4 identified and checksum
 *		     calculated, Bit1- L4Err - TCP/UDP Checksum Error
 *	bit      7 : Reserved:1
 *	bit  8..19 : Flow ID:12 - MSDU flow ID
 *	bit     20 : MID_V:1 - The MAC ID field is valid
 *	bit 21..22 : MID:2 - The MAC ID
 *	bit     23 : L3T:1 - IP types: 0-IPv6, 1-IPv4
 *	bit     24 : L4T:1 - Layer 4 Type: 0-UDP, 1-TCP
 *	bit     25 : BC:1 - The received MPDU is broadcast
 *	bit     26 : MC:1 - The received MPDU is multicast
 *	bit     27 : Raw:1 - The MPDU received with no translation
 *	bit     28 : Sec:1 - The FC control (b14) - Frame Protected
 *	bit     29 : Error:1 - An error is set when (L2 status != 0) ||
 *		(L3 status == 3) || (L4 status == 3)
 *	bit     30 : EOP:1 - End of MSDU signaling. It is set to mark the end
 *		     of the transfer, otherwise the status indicates buffer
 *		     only completion.
 *	bit     31 : Descriptor Ready bit:1 - It is initiated to
 *		     zero by the driver when the ring is created. It is set
 *		     by the HW to one for each completed status message.
 *		     Each wrap around, the DR bit value is flipped.
 * [dword 1]
 *	bit  0.. 5 : MAC Len:6 - The number of bytes that are used for L2 header
 *	bit  6..11 : IPLEN:6 - The number of DW that are used for L3 header
 *	bit 12..15 : I4Len:4 - The number of DW that are used for L4 header
 *	bit 16..21 : MCS:6 - The received MCS field from the PLCP Header
 *	bit 22..23 : CB mode:2 - The CB Mode: 0-DMG, 1-EDMG, 2-Wide
 *	bit 24..27 : Data Offset:4 - The data offset, a code that describe the
 *		     payload shift from the beginning of the buffer:
 *		     0 - 0 Bytes, 3 - 2 Bytes
 *	bit     28 : A-MSDU Present:1 - The QoS (b7) A-MSDU present field
 *	bit     29 : A-MSDU Type:1 The QoS (b8) A-MSDU Type field
 *	bit     30 : A-MPDU:1 - Packet is part of aggregated MPDU
 *	bit     31 : Key ID:1 - The extracted Key ID from the encryption header
 * [dword 2]
 *	bit  0..15 : Buffer ID:16 - The Buffer Identifier
 *	bit 16..31 : Length:16 - It indicates the valid bytes that are stored
 *		     in the current descriptor buffer. For multiple buffer
 *		     descriptor, SW need to sum the total descriptor length
 *		     in all buffers to produce the packet length
 * [dword 3]
 *	bit  0..31  : timestamp:32 - The MPDU Timestamp.
 */
struct wil_rx_status_compressed {
	u32 d0;
	u32 d1;
	__le16 buff_id;
	__le16 length;
	u32 timestamp;
} __packed;

/* Enhanced Rx status message - extension part
 * [dword 0]
 *	bit  0.. 4 : QID:5 - The Queue Identifier that the packet is received
 *		     from
 *	bit  5.. 7 : Reserved:3
 *	bit  8..11 : TID:4 - The QoS (b3-0) TID Field
 *	bit 12..15   Source index:4 - The Source index that was found
		     during Parsing the TA. This field is used to define the
		     source of the packet
 *	bit 16..18 : Destination index:3 - The Destination index that
		     was found during Parsing the RA.
 *	bit 19..20 : DS Type:2 - The FC Control (b9-8) - From / To DS
 *	bit 21..22 : MIC ICR:2 - this signal tells the DMA to assert an
		     interrupt after it writes the packet
 *	bit     23 : ESOP:1 - The QoS (b4) ESOP field
 *	bit     24 : RDG:1
 *	bit 25..31 : Reserved:7
 * [dword 1]
 *	bit  0.. 1 : Frame Type:2 - The FC Control (b3-2) - MPDU Type
		     (management, data, control and extension)
 *	bit  2.. 5 : Syb type:4 - The FC Control (b7-4) - Frame Subtype
 *	bit  6..11 : Ext sub type:6 - The FC Control (b11-8) - Frame Extended
 *                   Subtype
 *	bit 12..13 : ACK Policy:2 - The QoS (b6-5) ACK Policy fields
 *	bit 14     : DECRYPT_BYP:1 - The MPDU is bypass by the decryption unit
 *	bit 15..23 : Reserved:9
 *	bit 24..31 : RSSI/SNR:8 - The RSSI / SNR measurement for the received
 *                   MPDU
 * [dword 2]
 *	bit  0..11 : SN:12 - The received Sequence number field
 *	bit 12..15 : Reserved:4
 *	bit 16..31 : PN bits [15:0]:16
 * [dword 3]
 *	bit  0..31 : PN bits [47:16]:32
 */
struct wil_rx_status_extension {
	u32 d0;
	u32 d1;
	__le16 seq_num; /* only lower 12 bits */
	struct_group_attr(pn, __packed,
		u16 pn_15_0;
		u32 pn_47_16;
	);
} __packed;

struct wil_rx_status_extended {
	struct wil_rx_status_compressed comp;
	struct wil_rx_status_extension ext;
} __packed;

static inline void *wil_skb_rxstatus(struct sk_buff *skb)
{
	return (void *)skb->cb;
}

static inline __le16 wil_rx_status_get_length(void *msg)
{
	return ((struct wil_rx_status_compressed *)msg)->length;
}

static inline u8 wil_rx_status_get_mcs(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d1,
			    16, 21);
}

static inline u8 wil_rx_status_get_cb_mode(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d1,
			    22, 23);
}

static inline u16 wil_rx_status_get_flow_id(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    8, 19);
}

static inline u8 wil_rx_status_get_mcast(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    26, 26);
}

/**
 * In case of DLPF miss the parsing of flow Id should be as follows:
 * dest_id:2
 * src_id :3 - cid
 * tid:3
 * Otherwise:
 * tid:4
 * cid:4
 */

static inline u8 wil_rx_status_get_cid(void *msg)
{
	u16 val = wil_rx_status_get_flow_id(msg);

	if (val & WIL_RX_EDMA_DLPF_LU_MISS_BIT)
		/* CID is in bits 2..4 */
		return (val >> WIL_RX_EDMA_DLPF_LU_MISS_CID_POS) &
			WIL_RX_EDMA_DLPF_LU_MISS_CID_TID_MASK;
	else
		/* CID is in bits 4..7 */
		return (val >> WIL_RX_EDMA_DLPF_LU_HIT_CID_POS) &
			WIL_RX_EDMA_DLPF_LU_HIT_CID_TID_MASK;
}

static inline u8 wil_rx_status_get_tid(void *msg)
{
	u16 val = wil_rx_status_get_flow_id(msg);

	if (val & WIL_RX_EDMA_DLPF_LU_MISS_BIT)
		/* TID is in bits 5..7 */
		return (val >> WIL_RX_EDMA_DLPF_LU_MISS_TID_POS) &
			WIL_RX_EDMA_DLPF_LU_MISS_CID_TID_MASK;
	else
		/* TID is in bits 0..3 */
		return val & WIL_RX_EDMA_DLPF_LU_MISS_CID_TID_MASK;
}

static inline int wil_rx_status_get_eop(void *msg) /* EoP = End of Packet */
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    30, 30);
}

static inline void wil_rx_status_reset_buff_id(struct wil_status_ring *s)
{
	((struct wil_rx_status_compressed *)
		(s->va + (s->elem_size * s->swhead)))->buff_id = 0;
}

static inline __le16 wil_rx_status_get_buff_id(void *msg)
{
	return ((struct wil_rx_status_compressed *)msg)->buff_id;
}

static inline u8 wil_rx_status_get_data_offset(void *msg)
{
	u8 val = WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d1,
			      24, 27);

	switch (val) {
	case 0: return 0;
	case 3: return 2;
	default: return 0xFF;
	}
}

static inline int wil_rx_status_get_frame_type(struct wil6210_priv *wil,
					       void *msg)
{
	if (wil->use_compressed_rx_status)
		return IEEE80211_FTYPE_DATA;

	return WIL_GET_BITS(((struct wil_rx_status_extended *)msg)->ext.d1,
			    0, 1) << 2;
}

static inline int wil_rx_status_get_fc1(struct wil6210_priv *wil, void *msg)
{
	if (wil->use_compressed_rx_status)
		return 0;

	return WIL_GET_BITS(((struct wil_rx_status_extended *)msg)->ext.d1,
			    0, 5) << 2;
}

static inline __le16 wil_rx_status_get_seq(struct wil6210_priv *wil, void *msg)
{
	if (wil->use_compressed_rx_status)
		return 0;

	return ((struct wil_rx_status_extended *)msg)->ext.seq_num;
}

static inline u8 wil_rx_status_get_retry(void *msg)
{
	/* retry bit is missing in EDMA HW. return 1 to be on the safe side */
	return 1;
}

static inline int wil_rx_status_get_mid(void *msg)
{
	if (!(((struct wil_rx_status_compressed *)msg)->d0 &
	    WIL_RX_EDMA_MID_VALID_BIT))
		return 0; /* use the default MID */

	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    21, 22);
}

static inline int wil_rx_status_get_error(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    29, 29);
}

static inline int wil_rx_status_get_l2_rx_status(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    0, 2);
}

static inline int wil_rx_status_get_l3_rx_status(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    3, 4);
}

static inline int wil_rx_status_get_l4_rx_status(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    5, 6);
}

/* L4	L3	Expected result
 * 0	0	Ok. No L3 and no L4 known protocols found.
 *		Treated as L2 packet. (no offloads on this packet)
 * 0	1	Ok. It means that L3 was found, and checksum check passed.
 *		No known L4 protocol was found.
 * 0	2	It means that L3 protocol was found, and checksum check failed.
 *		No L4 known protocol was found.
 * 1	any	Ok. It means that L4 was found, and checksum check passed.
 * 3	0	Not a possible scenario.
 * 3	1	Recalculate. It means that L3 protocol was found, and checksum
 *		passed. But L4 checksum failed. Need to see if really failed,
 *		or due to fragmentation.
 * 3	2	Both L3 and L4 checksum check failed.
 */
static inline int wil_rx_status_get_checksum(void *msg,
					     struct wil_net_stats *stats)
{
	int l3_rx_status = wil_rx_status_get_l3_rx_status(msg);
	int l4_rx_status = wil_rx_status_get_l4_rx_status(msg);

	if (l4_rx_status == 1)
		return CHECKSUM_UNNECESSARY;

	if (l4_rx_status == 0 && l3_rx_status == 1)
		return CHECKSUM_UNNECESSARY;

	if (l3_rx_status == 0 && l4_rx_status == 0)
		/* L2 packet */
		return CHECKSUM_NONE;

	/* If HW reports bad checksum, let IP stack re-check it
	 * For example, HW doesn't understand Microsoft IP stack that
	 * mis-calculates TCP checksum - if it should be 0x0,
	 * it writes 0xffff in violation of RFC 1624
	 */
	stats->rx_csum_err++;
	return CHECKSUM_NONE;
}

static inline int wil_rx_status_get_security(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d0,
			    28, 28);
}

static inline u8 wil_rx_status_get_key_id(void *msg)
{
	return WIL_GET_BITS(((struct wil_rx_status_compressed *)msg)->d1,
			    31, 31);
}

static inline u8 wil_tx_status_get_mcs(struct wil_ring_tx_status *msg)
{
	return WIL_GET_BITS(msg->d2, 0, 4);
}

static inline u32 wil_ring_next_head(struct wil_ring *ring)
{
	return (ring->swhead + 1) % ring->size;
}

static inline void wil_desc_set_addr_edma(struct wil_ring_dma_addr *addr,
					  __le16 *addr_high_high,
					  dma_addr_t pa)
{
	addr->addr_low = cpu_to_le32(lower_32_bits(pa));
	addr->addr_high = cpu_to_le16((u16)upper_32_bits(pa));
	*addr_high_high = cpu_to_le16((u16)(upper_32_bits(pa) >> 16));
}

static inline
dma_addr_t wil_tx_desc_get_addr_edma(struct wil_ring_tx_enhanced_dma *dma)
{
	return le32_to_cpu(dma->addr.addr_low) |
			   ((u64)le16_to_cpu(dma->addr.addr_high) << 32) |
			   ((u64)le16_to_cpu(dma->addr_high_high) << 48);
}

static inline
dma_addr_t wil_rx_desc_get_addr_edma(struct wil_ring_rx_enhanced_dma *dma)
{
	return le32_to_cpu(dma->addr.addr_low) |
			   ((u64)le16_to_cpu(dma->addr.addr_high) << 32) |
			   ((u64)le16_to_cpu(dma->addr_high_high) << 48);
}

void wil_configure_interrupt_moderation_edma(struct wil6210_priv *wil);
int wil_tx_sring_handler(struct wil6210_priv *wil,
			 struct wil_status_ring *sring);
void wil_rx_handle_edma(struct wil6210_priv *wil, int *quota);
void wil_init_txrx_ops_edma(struct wil6210_priv *wil);

#endif /* WIL6210_TXRX_EDMA_H */

