/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_BASE_H_
#define _IGC_BASE_H_

/* forward declaration */
void igc_rx_fifo_flush_base(struct igc_hw *hw);
void igc_power_down_phy_copper_base(struct igc_hw *hw);

/* Transmit Descriptor - Advanced */
union igc_adv_tx_desc {
	struct {
		__le64 buffer_addr;    /* Address of descriptor's data buf */
		__le32 cmd_type_len;
		__le32 olinfo_status;
	} read;
	struct {
		__le64 rsvd;       /* Reserved */
		__le32 nxtseq_seed;
		__le32 status;
	} wb;
};

/* Context descriptors */
struct igc_adv_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 launch_time;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

/* Adv Transmit Descriptor Config Masks */
#define IGC_ADVTXD_MAC_TSTAMP	0x00080000 /* IEEE1588 Timestamp packet */
#define IGC_ADVTXD_DTYP_CTXT	0x00200000 /* Advanced Context Descriptor */
#define IGC_ADVTXD_DTYP_DATA	0x00300000 /* Advanced Data Descriptor */
#define IGC_ADVTXD_DCMD_EOP	0x01000000 /* End of Packet */
#define IGC_ADVTXD_DCMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define IGC_ADVTXD_DCMD_RS	0x08000000 /* Report Status */
#define IGC_ADVTXD_DCMD_DEXT	0x20000000 /* Descriptor extension (1=Adv) */
#define IGC_ADVTXD_DCMD_VLE	0x40000000 /* VLAN pkt enable */
#define IGC_ADVTXD_DCMD_TSE	0x80000000 /* TCP Seg enable */
#define IGC_ADVTXD_PAYLEN_SHIFT	14 /* Adv desc PAYLEN shift */

#define IGC_RAR_ENTRIES		16

/* Receive Descriptor - Advanced */
union igc_adv_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				__le32 data;
				struct {
					__le16 pkt_info; /*RSS type, Pkt type*/
					/* Split Header, header buffer len */
					__le16 hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				struct {
					__le16 ip_id; /* IP id */
					__le16 csum; /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error; /* ext status/error */
			__le16 length; /* Packet length */
			__le16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

/* Additional Transmit Descriptor Control definitions */
#define IGC_TXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Tx Queue */
#define IGC_TXDCTL_SWFLUSH	0x04000000 /* Transmit Software Flush */

/* Additional Receive Descriptor Control definitions */
#define IGC_RXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Rx Queue */
#define IGC_RXDCTL_SWFLUSH		0x04000000 /* Receive Software Flush */

/* SRRCTL bit definitions */
#define IGC_SRRCTL_BSIZEPKT_MASK	GENMASK(6, 0)
#define IGC_SRRCTL_BSIZEPKT(x)		FIELD_PREP(IGC_SRRCTL_BSIZEPKT_MASK, \
					(x) / 1024) /* in 1 KB resolution */
#define IGC_SRRCTL_BSIZEHDR_MASK	GENMASK(13, 8)
#define IGC_SRRCTL_BSIZEHDR(x)		FIELD_PREP(IGC_SRRCTL_BSIZEHDR_MASK, \
					(x) / 64) /* in 64 bytes resolution */
#define IGC_SRRCTL_DESCTYPE_MASK	GENMASK(27, 25)
#define IGC_SRRCTL_DESCTYPE_ADV_ONEBUF	FIELD_PREP(IGC_SRRCTL_DESCTYPE_MASK, 1)

#endif /* _IGC_BASE_H */
