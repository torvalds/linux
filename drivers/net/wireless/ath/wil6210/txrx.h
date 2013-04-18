/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef WIL6210_TXRX_H
#define WIL6210_TXRX_H

#define BUF_SW_OWNED    (1)
#define BUF_HW_OWNED    (0)

/* size of max. Rx packet */
#define RX_BUF_LEN      (2048)
#define TX_BUF_LEN      (2048)
/* how many bytes to reserve for rtap header? */
#define WIL6210_RTAP_SIZE (128)

/* Tx/Rx path */
/*
 * Tx descriptor - MAC part
 * [dword 0]
 * bit  0.. 9 : lifetime_expiry_value:10
 * bit     10 : interrup_en:1
 * bit     11 : status_en:1
 * bit 12..13 : txss_override:2
 * bit     14 : timestamp_insertion:1
 * bit     15 : duration_preserve:1
 * bit 16..21 : reserved0:6
 * bit 22..26 : mcs_index:5
 * bit     27 : mcs_en:1
 * bit 28..29 : reserved1:2
 * bit     30 : reserved2:1
 * bit     31 : sn_preserved:1
 * [dword 1]
 * bit  0.. 3 : pkt_mode:4
 * bit      4 : pkt_mode_en:1
 * bit  5.. 7 : reserved0:3
 * bit  8..13 : reserved1:6
 * bit     14 : reserved2:1
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
 * bit 18..19 : l2_translation_type:2
 * bit     20 : snap_hdr_insertion_en:1
 * bit     21 : vlan_removal_en:1
 * bit 22..31 : reserved0:10
 * [dword 3]
 * bit  0.. 31: ucode_cmd:32
 */
struct vring_tx_mac {
	u32 d[3];
	u32 ucode_cmd;
} __packed;

/* TX MAC Dword 0 */
#define MAC_CFG_DESC_TX_0_LIFETIME_EXPIRY_VALUE_POS 0
#define MAC_CFG_DESC_TX_0_LIFETIME_EXPIRY_VALUE_LEN 10
#define MAC_CFG_DESC_TX_0_LIFETIME_EXPIRY_VALUE_MSK 0x3FF

#define MAC_CFG_DESC_TX_0_INTERRUP_EN_POS 10
#define MAC_CFG_DESC_TX_0_INTERRUP_EN_LEN 1
#define MAC_CFG_DESC_TX_0_INTERRUP_EN_MSK 0x400

#define MAC_CFG_DESC_TX_0_STATUS_EN_POS 11
#define MAC_CFG_DESC_TX_0_STATUS_EN_LEN 1
#define MAC_CFG_DESC_TX_0_STATUS_EN_MSK 0x800

#define MAC_CFG_DESC_TX_0_TXSS_OVERRIDE_POS 12
#define MAC_CFG_DESC_TX_0_TXSS_OVERRIDE_LEN 2
#define MAC_CFG_DESC_TX_0_TXSS_OVERRIDE_MSK 0x3000

#define MAC_CFG_DESC_TX_0_TIMESTAMP_INSERTION_POS 14
#define MAC_CFG_DESC_TX_0_TIMESTAMP_INSERTION_LEN 1
#define MAC_CFG_DESC_TX_0_TIMESTAMP_INSERTION_MSK 0x4000

#define MAC_CFG_DESC_TX_0_DURATION_PRESERVE_POS 15
#define MAC_CFG_DESC_TX_0_DURATION_PRESERVE_LEN 1
#define MAC_CFG_DESC_TX_0_DURATION_PRESERVE_MSK 0x8000

#define MAC_CFG_DESC_TX_0_MCS_INDEX_POS 22
#define MAC_CFG_DESC_TX_0_MCS_INDEX_LEN 5
#define MAC_CFG_DESC_TX_0_MCS_INDEX_MSK 0x7C00000

#define MAC_CFG_DESC_TX_0_MCS_EN_POS 27
#define MAC_CFG_DESC_TX_0_MCS_EN_LEN 1
#define MAC_CFG_DESC_TX_0_MCS_EN_MSK 0x8000000

#define MAC_CFG_DESC_TX_0_SN_PRESERVED_POS 31
#define MAC_CFG_DESC_TX_0_SN_PRESERVED_LEN 1
#define MAC_CFG_DESC_TX_0_SN_PRESERVED_MSK 0x80000000

/* TX MAC Dword 1 */
#define MAC_CFG_DESC_TX_1_PKT_MODE_POS 0
#define MAC_CFG_DESC_TX_1_PKT_MODE_LEN 4
#define MAC_CFG_DESC_TX_1_PKT_MODE_MSK 0xF

#define MAC_CFG_DESC_TX_1_PKT_MODE_EN_POS 4
#define MAC_CFG_DESC_TX_1_PKT_MODE_EN_LEN 1
#define MAC_CFG_DESC_TX_1_PKT_MODE_EN_MSK 0x10

#define MAC_CFG_DESC_TX_1_ACK_POLICY_EN_POS 15
#define MAC_CFG_DESC_TX_1_ACK_POLICY_EN_LEN 1
#define MAC_CFG_DESC_TX_1_ACK_POLICY_EN_MSK 0x8000

#define MAC_CFG_DESC_TX_1_DST_INDEX_POS 16
#define MAC_CFG_DESC_TX_1_DST_INDEX_LEN 4
#define MAC_CFG_DESC_TX_1_DST_INDEX_MSK 0xF0000

#define MAC_CFG_DESC_TX_1_DST_INDEX_EN_POS 20
#define MAC_CFG_DESC_TX_1_DST_INDEX_EN_LEN 1
#define MAC_CFG_DESC_TX_1_DST_INDEX_EN_MSK 0x100000

#define MAC_CFG_DESC_TX_1_ACK_POLICY_POS 21
#define MAC_CFG_DESC_TX_1_ACK_POLICY_LEN 2
#define MAC_CFG_DESC_TX_1_ACK_POLICY_MSK 0x600000

#define MAC_CFG_DESC_TX_1_LIFETIME_EN_POS 23
#define MAC_CFG_DESC_TX_1_LIFETIME_EN_LEN 1
#define MAC_CFG_DESC_TX_1_LIFETIME_EN_MSK 0x800000

#define MAC_CFG_DESC_TX_1_MAX_RETRY_POS 24
#define MAC_CFG_DESC_TX_1_MAX_RETRY_LEN 7
#define MAC_CFG_DESC_TX_1_MAX_RETRY_MSK 0x7F000000

#define MAC_CFG_DESC_TX_1_MAX_RETRY_EN_POS 31
#define MAC_CFG_DESC_TX_1_MAX_RETRY_EN_LEN 1
#define MAC_CFG_DESC_TX_1_MAX_RETRY_EN_MSK 0x80000000

/* TX MAC Dword 2 */
#define MAC_CFG_DESC_TX_2_NUM_OF_DESCRIPTORS_POS 0
#define MAC_CFG_DESC_TX_2_NUM_OF_DESCRIPTORS_LEN 8
#define MAC_CFG_DESC_TX_2_NUM_OF_DESCRIPTORS_MSK 0xFF

#define MAC_CFG_DESC_TX_2_RESERVED_POS 8
#define MAC_CFG_DESC_TX_2_RESERVED_LEN 10
#define MAC_CFG_DESC_TX_2_RESERVED_MSK 0x3FF00

#define MAC_CFG_DESC_TX_2_L2_TRANSLATION_TYPE_POS 18
#define MAC_CFG_DESC_TX_2_L2_TRANSLATION_TYPE_LEN 2
#define MAC_CFG_DESC_TX_2_L2_TRANSLATION_TYPE_MSK 0xC0000

#define MAC_CFG_DESC_TX_2_SNAP_HDR_INSERTION_EN_POS 20
#define MAC_CFG_DESC_TX_2_SNAP_HDR_INSERTION_EN_LEN 1
#define MAC_CFG_DESC_TX_2_SNAP_HDR_INSERTION_EN_MSK 0x100000

#define MAC_CFG_DESC_TX_2_VLAN_REMOVAL_EN_POS 21
#define MAC_CFG_DESC_TX_2_VLAN_REMOVAL_EN_LEN 1
#define MAC_CFG_DESC_TX_2_VLAN_REMOVAL_EN_MSK 0x200000

/* TX MAC Dword 3 */
#define MAC_CFG_DESC_TX_3_UCODE_CMD_POS 0
#define MAC_CFG_DESC_TX_3_UCODE_CMD_LEN 32
#define MAC_CFG_DESC_TX_3_UCODE_CMD_MSK 0xFFFFFFFF

/* TX DMA Dword 0 */
#define DMA_CFG_DESC_TX_0_L4_LENGTH_POS 0
#define DMA_CFG_DESC_TX_0_L4_LENGTH_LEN 8
#define DMA_CFG_DESC_TX_0_L4_LENGTH_MSK 0xFF

#define DMA_CFG_DESC_TX_0_CMD_EOP_POS 8
#define DMA_CFG_DESC_TX_0_CMD_EOP_LEN 1
#define DMA_CFG_DESC_TX_0_CMD_EOP_MSK 0x100

#define DMA_CFG_DESC_TX_0_CMD_DMA_IT_POS 10
#define DMA_CFG_DESC_TX_0_CMD_DMA_IT_LEN 1
#define DMA_CFG_DESC_TX_0_CMD_DMA_IT_MSK 0x400

#define DMA_CFG_DESC_TX_0_SEGMENT_BUF_DETAILS_POS 11
#define DMA_CFG_DESC_TX_0_SEGMENT_BUF_DETAILS_LEN 2
#define DMA_CFG_DESC_TX_0_SEGMENT_BUF_DETAILS_MSK 0x1800

#define DMA_CFG_DESC_TX_0_TCP_SEG_EN_POS 13
#define DMA_CFG_DESC_TX_0_TCP_SEG_EN_LEN 1
#define DMA_CFG_DESC_TX_0_TCP_SEG_EN_MSK 0x2000

#define DMA_CFG_DESC_TX_0_IPV4_CHECKSUM_EN_POS 14
#define DMA_CFG_DESC_TX_0_IPV4_CHECKSUM_EN_LEN 1
#define DMA_CFG_DESC_TX_0_IPV4_CHECKSUM_EN_MSK 0x4000

#define DMA_CFG_DESC_TX_0_TCP_UDP_CHECKSUM_EN_POS 15
#define DMA_CFG_DESC_TX_0_TCP_UDP_CHECKSUM_EN_LEN 1
#define DMA_CFG_DESC_TX_0_TCP_UDP_CHECKSUM_EN_MSK 0x8000

#define DMA_CFG_DESC_TX_0_QID_POS 16
#define DMA_CFG_DESC_TX_0_QID_LEN 5
#define DMA_CFG_DESC_TX_0_QID_MSK 0x1F0000

#define DMA_CFG_DESC_TX_0_PSEUDO_HEADER_CALC_EN_POS 21
#define DMA_CFG_DESC_TX_0_PSEUDO_HEADER_CALC_EN_LEN 1
#define DMA_CFG_DESC_TX_0_PSEUDO_HEADER_CALC_EN_MSK 0x200000

#define DMA_CFG_DESC_TX_0_L4_TYPE_POS 30
#define DMA_CFG_DESC_TX_0_L4_TYPE_LEN 2
#define DMA_CFG_DESC_TX_0_L4_TYPE_MSK 0xC0000000


#define TX_DMA_STATUS_DU         BIT(0)

struct vring_tx_dma {
	u32 d0;
	u32 addr_low;
	u16 addr_high;
	u8  ip_length;
	u8  b11;       /* 0..6: mac_length; 7:ip_version */
	u8  error;     /* 0..2: err; 3..7: reserved; */
	u8  status;    /* 0: used; 1..7; reserved */
	u16 length;
} __packed;

/*
 * Rx descriptor - MAC part
 * [dword 0]
 * bit  0.. 3 : tid:4 The QoS (b3-0) TID Field
 * bit  4.. 6 : connection_id:3 :The Source index that  was found during
 *  Parsing the TA.  This field is used to  define the source of the packet
 * bit      7 : reserved:1
 * bit  8.. 9 : mac_id:2 : The MAC virtual  Ring number (always zero)
 * bit 10..11 : frame_type:2 : The FC Control  (b3-2) -  MPDU Type
 *              (management, data, control  and extension)
 * bit 12..15 : frame_subtype:4 : The FC Control  (b7-4) -  Frame Subtype
 * bit 16..27 : seq_number:12 The received Sequence number field
 * bit 28..31 : extended:4 extended subtype
 * [dword 1]
 * bit  0.. 3 : reserved
 * bit  4.. 5 : key_id:2
 * bit      6 : decrypt_bypass:1
 * bit      7 : security:1
 * bit  8.. 9 : ds_bits:2
 * bit     10 : a_msdu_present:1  from qos header
 * bit     11 : a_msdu_type:1  from qos header
 * bit     12 : a_mpdu:1  part of AMPDU aggregation
 * bit     13 : broadcast:1
 * bit     14 : mutlicast:1
 * bit     15 : reserved:1
 * bit 16..20 : rx_mac_qid:5   The Queue Identifier that the packet
 *                             is received from
 * bit 21..24 : mcs:4
 * bit 25..28 : mic_icr:4
 * bit 29..31 : reserved:3
 * [dword 2]
 * bit  0.. 2 : time_slot:3 The timeslot that the MPDU is received
 * bit      3 : fc_protocol_ver:1 The FC Control  (b0) - Protocol  Version
 * bit      4 : fc_order:1 The FC Control (b15) -Order
 * bit  5.. 7 : qos_ack_policy:3  The QoS (b6-5) ack policy Field
 * bit      8 : esop:1 The QoS (b4) ESOP field
 * bit      9 : qos_rdg_more_ppdu:1 The QoS (b9) RDG  field
 * bit 10..14 : qos_reserved:5 The QoS (b14-10) Reserved  field
 * bit     15 : qos_ac_constraint:1
 * bit 16..31 : pn_15_0:16 low 2 bytes of PN
 * [dword 3]
 * bit  0..31 : pn_47_16:32 high 4 bytes of PN
 */
struct vring_rx_mac {
	u32 d0;
	u32 d1;
	u16 w4;
	u16 pn_15_0;
	u32 pn_47_16;
} __packed;

/*
 * Rx descriptor - DMA part
 * [dword 0]
 * bit  0.. 7 : l4_length:8 layer 4 length
 * bit  8.. 9 : reserved:2
 * bit     10 : cmd_dma_it:1
 * bit 11..15 : reserved:5
 * bit 16..29 : phy_info_length:14
 * bit 30..31 : l4_type:2 valid if the L4I bit is set in the status field
 * [dword 1]
 * bit  0..31 : addr_low:32 The payload buffer low address
 * [dword 2]
 * bit  0..15 : addr_high:16 The payload buffer high address
 * bit 16..23 : ip_length:8
 * bit 24..30 : mac_length:7
 * bit     31 : ip_version:1
 * [dword 3]
 *  [byte 12] error
 *  [byte 13] status
 * bit      0 : du:1
 * bit      1 : eop:1
 * bit      2 : error:1
 * bit      3 : mi:1
 * bit      4 : l3_identified:1
 * bit      5 : l4_identified:1
 * bit      6 : phy_info_included:1
 * bit      7 : reserved:1
 *  [word 7] length
 *
 */

#define RX_DMA_D0_CMD_DMA_IT     BIT(10)

#define RX_DMA_STATUS_DU         BIT(0)
#define RX_DMA_STATUS_ERROR      BIT(2)
#define RX_DMA_STATUS_PHY_INFO   BIT(6)

struct vring_rx_dma {
	u32 d0;
	u32 addr_low;
	u16 addr_high;
	u8  ip_length;
	u8  b11;
	u8  error;
	u8  status;
	u16 length;
} __packed;

struct vring_tx_desc {
	struct vring_tx_mac mac;
	struct vring_tx_dma dma;
} __packed;

struct vring_rx_desc {
	struct vring_rx_mac mac;
	struct vring_rx_dma dma;
} __packed;

union vring_desc {
	struct vring_tx_desc tx;
	struct vring_rx_desc rx;
} __packed;

static inline int wil_rxdesc_phy_length(struct vring_rx_desc *d)
{
	return WIL_GET_BITS(d->dma.d0, 16, 29);
}

static inline int wil_rxdesc_mcs(struct vring_rx_desc *d)
{
	return WIL_GET_BITS(d->mac.d1, 21, 24);
}

static inline int wil_rxdesc_ds_bits(struct vring_rx_desc *d)
{
	return WIL_GET_BITS(d->mac.d1, 8, 9);
}

static inline int wil_rxdesc_ftype(struct vring_rx_desc *d)
{
	return WIL_GET_BITS(d->mac.d0, 10, 11);
}

static inline struct vring_rx_desc *wil_skb_rxdesc(struct sk_buff *skb)
{
	return (void *)skb->cb;
}

#endif /* WIL6210_TXRX_H */
