/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_LAN_TX_RX_H_
#define _ICE_LAN_TX_RX_H_

union ice_32byte_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
			/* bit 0 of hdr_addr is DD bit */
		__le64 rsvd1;
		__le64 rsvd2;
	} read;
	struct {
		struct {
			struct {
				__le16 mirroring_status;
				__le16 l2tag1;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				__le32 fd_id; /* Flow Director filter ID */
			} hi_dword;
		} qword0;
		struct {
			/* status/error/PTYPE/length */
			__le64 status_error_len;
		} qword1;
		struct {
			__le16 ext_status; /* extended status */
			__le16 rsvd;
			__le16 l2tag2_1;
			__le16 l2tag2_2;
		} qword2;
		struct {
			__le32 reserved;
			__le32 fd_id;
		} qword3;
	} wb; /* writeback */
};

struct ice_fltr_desc {
	__le64 qidx_compq_space_stat;
	__le64 dtype_cmd_vsi_fdid;
};

#define ICE_FXD_FLTR_QW0_QINDEX_S	0
#define ICE_FXD_FLTR_QW0_QINDEX_M	(0x7FFULL << ICE_FXD_FLTR_QW0_QINDEX_S)
#define ICE_FXD_FLTR_QW0_COMP_Q_S	11
#define ICE_FXD_FLTR_QW0_COMP_Q_M	BIT_ULL(ICE_FXD_FLTR_QW0_COMP_Q_S)
#define ICE_FXD_FLTR_QW0_COMP_Q_ZERO	0x0ULL

#define ICE_FXD_FLTR_QW0_COMP_REPORT_S	12
#define ICE_FXD_FLTR_QW0_COMP_REPORT_M	\
				(0x3ULL << ICE_FXD_FLTR_QW0_COMP_REPORT_S)
#define ICE_FXD_FLTR_QW0_COMP_REPORT_SW_FAIL	0x1ULL
#define ICE_FXD_FLTR_QW0_COMP_REPORT_SW		0x2ULL

#define ICE_FXD_FLTR_QW0_FD_SPACE_S	14
#define ICE_FXD_FLTR_QW0_FD_SPACE_M	(0x3ULL << ICE_FXD_FLTR_QW0_FD_SPACE_S)
#define ICE_FXD_FLTR_QW0_FD_SPACE_GUAR_BEST		0x2ULL

#define ICE_FXD_FLTR_QW0_STAT_CNT_S	16
#define ICE_FXD_FLTR_QW0_STAT_CNT_M	\
				(0x1FFFULL << ICE_FXD_FLTR_QW0_STAT_CNT_S)
#define ICE_FXD_FLTR_QW0_STAT_ENA_S	29
#define ICE_FXD_FLTR_QW0_STAT_ENA_M	(0x3ULL << ICE_FXD_FLTR_QW0_STAT_ENA_S)
#define ICE_FXD_FLTR_QW0_STAT_ENA_PKTS		0x1ULL

#define ICE_FXD_FLTR_QW0_EVICT_ENA_S	31
#define ICE_FXD_FLTR_QW0_EVICT_ENA_M	BIT_ULL(ICE_FXD_FLTR_QW0_EVICT_ENA_S)
#define ICE_FXD_FLTR_QW0_EVICT_ENA_FALSE	0x0ULL
#define ICE_FXD_FLTR_QW0_EVICT_ENA_TRUE		0x1ULL

#define ICE_FXD_FLTR_QW0_TO_Q_S		32
#define ICE_FXD_FLTR_QW0_TO_Q_M		(0x7ULL << ICE_FXD_FLTR_QW0_TO_Q_S)
#define ICE_FXD_FLTR_QW0_TO_Q_EQUALS_QINDEX	0x0ULL

#define ICE_FXD_FLTR_QW0_TO_Q_PRI_S	35
#define ICE_FXD_FLTR_QW0_TO_Q_PRI_M	(0x7ULL << ICE_FXD_FLTR_QW0_TO_Q_PRI_S)
#define ICE_FXD_FLTR_QW0_TO_Q_PRIO1	0x1ULL

#define ICE_FXD_FLTR_QW0_DPU_RECIPE_S	38
#define ICE_FXD_FLTR_QW0_DPU_RECIPE_M	\
			(0x3ULL << ICE_FXD_FLTR_QW0_DPU_RECIPE_S)
#define ICE_FXD_FLTR_QW0_DPU_RECIPE_DFLT	0x0ULL

#define ICE_FXD_FLTR_QW0_DROP_S		40
#define ICE_FXD_FLTR_QW0_DROP_M		BIT_ULL(ICE_FXD_FLTR_QW0_DROP_S)
#define ICE_FXD_FLTR_QW0_DROP_NO	0x0ULL
#define ICE_FXD_FLTR_QW0_DROP_YES	0x1ULL

#define ICE_FXD_FLTR_QW0_FLEX_PRI_S	41
#define ICE_FXD_FLTR_QW0_FLEX_PRI_M	(0x7ULL << ICE_FXD_FLTR_QW0_FLEX_PRI_S)
#define ICE_FXD_FLTR_QW0_FLEX_PRI_NONE	0x0ULL

#define ICE_FXD_FLTR_QW0_FLEX_MDID_S	44
#define ICE_FXD_FLTR_QW0_FLEX_MDID_M	(0xFULL << ICE_FXD_FLTR_QW0_FLEX_MDID_S)
#define ICE_FXD_FLTR_QW0_FLEX_MDID0	0x0ULL

#define ICE_FXD_FLTR_QW0_FLEX_VAL_S	48
#define ICE_FXD_FLTR_QW0_FLEX_VAL_M	\
				(0xFFFFULL << ICE_FXD_FLTR_QW0_FLEX_VAL_S)
#define ICE_FXD_FLTR_QW0_FLEX_VAL0	0x0ULL

#define ICE_FXD_FLTR_QW1_DTYPE_S	0
#define ICE_FXD_FLTR_QW1_DTYPE_M	(0xFULL << ICE_FXD_FLTR_QW1_DTYPE_S)
#define ICE_FXD_FLTR_QW1_PCMD_S		4
#define ICE_FXD_FLTR_QW1_PCMD_M		BIT_ULL(ICE_FXD_FLTR_QW1_PCMD_S)
#define ICE_FXD_FLTR_QW1_PCMD_ADD	0x0ULL
#define ICE_FXD_FLTR_QW1_PCMD_REMOVE	0x1ULL

#define ICE_FXD_FLTR_QW1_PROF_PRI_S	5
#define ICE_FXD_FLTR_QW1_PROF_PRI_M	(0x7ULL << ICE_FXD_FLTR_QW1_PROF_PRI_S)
#define ICE_FXD_FLTR_QW1_PROF_PRIO_ZERO	0x0ULL

#define ICE_FXD_FLTR_QW1_PROF_S		8
#define ICE_FXD_FLTR_QW1_PROF_M		(0x3FULL << ICE_FXD_FLTR_QW1_PROF_S)
#define ICE_FXD_FLTR_QW1_PROF_ZERO	0x0ULL

#define ICE_FXD_FLTR_QW1_FD_VSI_S	14
#define ICE_FXD_FLTR_QW1_FD_VSI_M	(0x3FFULL << ICE_FXD_FLTR_QW1_FD_VSI_S)
#define ICE_FXD_FLTR_QW1_SWAP_S		24
#define ICE_FXD_FLTR_QW1_SWAP_M		BIT_ULL(ICE_FXD_FLTR_QW1_SWAP_S)
#define ICE_FXD_FLTR_QW1_SWAP_NOT_SET	0x0ULL
#define ICE_FXD_FLTR_QW1_SWAP_SET	0x1ULL

#define ICE_FXD_FLTR_QW1_FDID_PRI_S	25
#define ICE_FXD_FLTR_QW1_FDID_PRI_M	(0x7ULL << ICE_FXD_FLTR_QW1_FDID_PRI_S)
#define ICE_FXD_FLTR_QW1_FDID_PRI_ONE	0x1ULL
#define ICE_FXD_FLTR_QW1_FDID_PRI_THREE	0x3ULL

#define ICE_FXD_FLTR_QW1_FDID_MDID_S	28
#define ICE_FXD_FLTR_QW1_FDID_MDID_M	(0xFULL << ICE_FXD_FLTR_QW1_FDID_MDID_S)
#define ICE_FXD_FLTR_QW1_FDID_MDID_FD	0x05ULL

#define ICE_FXD_FLTR_QW1_FDID_S		32
#define ICE_FXD_FLTR_QW1_FDID_M		\
			(0xFFFFFFFFULL << ICE_FXD_FLTR_QW1_FDID_S)
#define ICE_FXD_FLTR_QW1_FDID_ZERO	0x0ULL

/* definition for FD filter programming status descriptor WB format */
#define ICE_FXD_FLTR_WB_QW1_DD_S	0
#define ICE_FXD_FLTR_WB_QW1_DD_M	(0x1ULL << ICE_FXD_FLTR_WB_QW1_DD_S)
#define ICE_FXD_FLTR_WB_QW1_DD_YES	0x1ULL

#define ICE_FXD_FLTR_WB_QW1_PROG_ID_S	1
#define ICE_FXD_FLTR_WB_QW1_PROG_ID_M	\
				(0x3ULL << ICE_FXD_FLTR_WB_QW1_PROG_ID_S)
#define ICE_FXD_FLTR_WB_QW1_PROG_ADD	0x0ULL
#define ICE_FXD_FLTR_WB_QW1_PROG_DEL	0x1ULL

#define ICE_FXD_FLTR_WB_QW1_FAIL_S	4
#define ICE_FXD_FLTR_WB_QW1_FAIL_M	(0x1ULL << ICE_FXD_FLTR_WB_QW1_FAIL_S)
#define ICE_FXD_FLTR_WB_QW1_FAIL_YES	0x1ULL

#define ICE_FXD_FLTR_WB_QW1_FAIL_PROF_S	5
#define ICE_FXD_FLTR_WB_QW1_FAIL_PROF_M	\
				(0x1ULL << ICE_FXD_FLTR_WB_QW1_FAIL_PROF_S)
#define ICE_FXD_FLTR_WB_QW1_FAIL_PROF_YES	0x1ULL

struct ice_rx_ptype_decoded {
	u32 known:1;
	u32 outer_ip:1;
	u32 outer_ip_ver:2;
	u32 outer_frag:1;
	u32 tunnel_type:3;
	u32 tunnel_end_prot:2;
	u32 tunnel_end_frag:1;
	u32 inner_prot:4;
	u32 payload_layer:3;
};

enum ice_rx_ptype_outer_ip {
	ICE_RX_PTYPE_OUTER_L2	= 0,
	ICE_RX_PTYPE_OUTER_IP	= 1,
};

enum ice_rx_ptype_outer_ip_ver {
	ICE_RX_PTYPE_OUTER_NONE	= 0,
	ICE_RX_PTYPE_OUTER_IPV4	= 1,
	ICE_RX_PTYPE_OUTER_IPV6	= 2,
};

enum ice_rx_ptype_outer_fragmented {
	ICE_RX_PTYPE_NOT_FRAG	= 0,
	ICE_RX_PTYPE_FRAG	= 1,
};

enum ice_rx_ptype_tunnel_type {
	ICE_RX_PTYPE_TUNNEL_NONE		= 0,
	ICE_RX_PTYPE_TUNNEL_IP_IP		= 1,
	ICE_RX_PTYPE_TUNNEL_IP_GRENAT		= 2,
	ICE_RX_PTYPE_TUNNEL_IP_GRENAT_MAC	= 3,
	ICE_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN	= 4,
};

enum ice_rx_ptype_tunnel_end_prot {
	ICE_RX_PTYPE_TUNNEL_END_NONE	= 0,
	ICE_RX_PTYPE_TUNNEL_END_IPV4	= 1,
	ICE_RX_PTYPE_TUNNEL_END_IPV6	= 2,
};

enum ice_rx_ptype_inner_prot {
	ICE_RX_PTYPE_INNER_PROT_NONE		= 0,
	ICE_RX_PTYPE_INNER_PROT_UDP		= 1,
	ICE_RX_PTYPE_INNER_PROT_TCP		= 2,
	ICE_RX_PTYPE_INNER_PROT_SCTP		= 3,
	ICE_RX_PTYPE_INNER_PROT_ICMP		= 4,
	ICE_RX_PTYPE_INNER_PROT_TIMESYNC	= 5,
};

enum ice_rx_ptype_payload_layer {
	ICE_RX_PTYPE_PAYLOAD_LAYER_NONE	= 0,
	ICE_RX_PTYPE_PAYLOAD_LAYER_PAY2	= 1,
	ICE_RX_PTYPE_PAYLOAD_LAYER_PAY3	= 2,
	ICE_RX_PTYPE_PAYLOAD_LAYER_PAY4	= 3,
};

/* Rx Flex Descriptor
 * This descriptor is used instead of the legacy version descriptor when
 * ice_rlan_ctx.adv_desc is set
 */
union ice_32b_rx_flex_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
				 /* bit 0 of hdr_addr is DD bit */
		__le64 rsvd1;
		__le64 rsvd2;
	} read;
	struct {
		/* Qword 0 */
		u8 rxdid; /* descriptor builder profile ID */
		u8 mir_id_umb_cast; /* mirror=[5:0], umb=[7:6] */
		__le16 ptype_flex_flags0; /* ptype=[9:0], ff0=[15:10] */
		__le16 pkt_len; /* [15:14] are reserved */
		__le16 hdr_len_sph_flex_flags1; /* header=[10:0] */
						/* sph=[11:11] */
						/* ff1/ext=[15:12] */

		/* Qword 1 */
		__le16 status_error0;
		__le16 l2tag1;
		__le16 flex_meta0;
		__le16 flex_meta1;

		/* Qword 2 */
		__le16 status_error1;
		u8 flex_flags2;
		u8 time_stamp_low;
		__le16 l2tag2_1st;
		__le16 l2tag2_2nd;

		/* Qword 3 */
		__le16 flex_meta2;
		__le16 flex_meta3;
		union {
			struct {
				__le16 flex_meta4;
				__le16 flex_meta5;
			} flex;
			__le32 ts_high;
		} flex_ts;
	} wb; /* writeback */
};

/* Rx Flex Descriptor NIC Profile
 * This descriptor corresponds to RxDID 2 which contains
 * metadata fields for RSS, flow ID and timestamp info
 */
struct ice_32b_rx_flex_desc_nic {
	/* Qword 0 */
	u8 rxdid;
	u8 mir_id_umb_cast;
	__le16 ptype_flexi_flags0;
	__le16 pkt_len;
	__le16 hdr_len_sph_flex_flags1;

	/* Qword 1 */
	__le16 status_error0;
	__le16 l2tag1;
	__le32 rss_hash;

	/* Qword 2 */
	__le16 status_error1;
	u8 flexi_flags2;
	u8 ts_low;
	__le16 l2tag2_1st;
	__le16 l2tag2_2nd;

	/* Qword 3 */
	__le32 flow_id;
	union {
		struct {
			__le16 vlan_id;
			__le16 flow_id_ipv6;
		} flex;
		__le32 ts_high;
	} flex_ts;
};

/* Receive Flex Descriptor profile IDs: There are a total
 * of 64 profiles where profile IDs 0/1 are for legacy; and
 * profiles 2-63 are flex profiles that can be programmed
 * with a specific metadata (profile 7 reserved for HW)
 */
enum ice_rxdid {
	ICE_RXDID_LEGACY_0		= 0,
	ICE_RXDID_LEGACY_1		= 1,
	ICE_RXDID_FLEX_NIC		= 2,
	ICE_RXDID_FLEX_NIC_2		= 6,
	ICE_RXDID_HW			= 7,
	ICE_RXDID_LAST			= 63,
};

/* Receive Flex Descriptor Rx opcode values */
#define ICE_RX_OPC_MDID		0x01

/* Receive Descriptor MDID values that access packet flags */
enum ice_flex_mdid_pkt_flags {
	ICE_RX_MDID_PKT_FLAGS_15_0	= 20,
	ICE_RX_MDID_PKT_FLAGS_31_16,
	ICE_RX_MDID_PKT_FLAGS_47_32,
	ICE_RX_MDID_PKT_FLAGS_63_48,
};

/* Receive Descriptor MDID values */
enum ice_flex_rx_mdid {
	ICE_RX_MDID_FLOW_ID_LOWER	= 5,
	ICE_RX_MDID_FLOW_ID_HIGH,
	ICE_RX_MDID_SRC_VSI		= 19,
	ICE_RX_MDID_HASH_LOW		= 56,
	ICE_RX_MDID_HASH_HIGH,
};

/* Rx/Tx Flag64 packet flag bits */
enum ice_flg64_bits {
	ICE_FLG_PKT_DSI		= 0,
	ICE_FLG_EVLAN_x8100	= 14,
	ICE_FLG_EVLAN_x9100,
	ICE_FLG_VLAN_x8100,
	ICE_FLG_TNL_MAC		= 22,
	ICE_FLG_TNL_VLAN,
	ICE_FLG_PKT_FRG,
	ICE_FLG_FIN		= 32,
	ICE_FLG_SYN,
	ICE_FLG_RST,
	ICE_FLG_TNL0		= 38,
	ICE_FLG_TNL1,
	ICE_FLG_TNL2,
	ICE_FLG_UDP_GRE,
	ICE_FLG_RSVD		= 63
};

/* for ice_32byte_rx_flex_desc.ptype_flexi_flags0 member */
#define ICE_RX_FLEX_DESC_PTYPE_M	(0x3FF) /* 10-bits */

/* for ice_32byte_rx_flex_desc.pkt_length member */
#define ICE_RX_FLX_DESC_PKT_LEN_M	(0x3FFF) /* 14-bits */

enum ice_rx_flex_desc_status_error_0_bits {
	/* Note: These are predefined bit offsets */
	ICE_RX_FLEX_DESC_STATUS0_DD_S = 0,
	ICE_RX_FLEX_DESC_STATUS0_EOF_S,
	ICE_RX_FLEX_DESC_STATUS0_HBO_S,
	ICE_RX_FLEX_DESC_STATUS0_L3L4P_S,
	ICE_RX_FLEX_DESC_STATUS0_XSUM_IPE_S,
	ICE_RX_FLEX_DESC_STATUS0_XSUM_L4E_S,
	ICE_RX_FLEX_DESC_STATUS0_XSUM_EIPE_S,
	ICE_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_S,
	ICE_RX_FLEX_DESC_STATUS0_LPBK_S,
	ICE_RX_FLEX_DESC_STATUS0_IPV6EXADD_S,
	ICE_RX_FLEX_DESC_STATUS0_RXE_S,
	ICE_RX_FLEX_DESC_STATUS0_CRCP_S,
	ICE_RX_FLEX_DESC_STATUS0_RSS_VALID_S,
	ICE_RX_FLEX_DESC_STATUS0_L2TAG1P_S,
	ICE_RX_FLEX_DESC_STATUS0_XTRMD0_VALID_S,
	ICE_RX_FLEX_DESC_STATUS0_XTRMD1_VALID_S,
	ICE_RX_FLEX_DESC_STATUS0_LAST /* this entry must be last!!! */
};

enum ice_rx_flex_desc_status_error_1_bits {
	/* Note: These are predefined bit offsets */
	ICE_RX_FLEX_DESC_STATUS1_NAT_S = 4,
	ICE_RX_FLEX_DESC_STATUS1_LAST /* this entry must be last!!! */
};

#define ICE_RXQ_CTX_SIZE_DWORDS		8
#define ICE_RXQ_CTX_SZ			(ICE_RXQ_CTX_SIZE_DWORDS * sizeof(u32))
#define ICE_TX_CMPLTNQ_CTX_SIZE_DWORDS	22
#define ICE_TX_DRBELL_Q_CTX_SIZE_DWORDS	5
#define GLTCLAN_CQ_CNTX(i, CQ)		(GLTCLAN_CQ_CNTX0(CQ) + ((i) * 0x0800))

/* RLAN Rx queue context data
 *
 * The sizes of the variables may be larger than needed due to crossing byte
 * boundaries. If we do not have the width of the variable set to the correct
 * size then we could end up shifting bits off the top of the variable when the
 * variable is at the top of a byte and crosses over into the next byte.
 */
struct ice_rlan_ctx {
	u16 head;
	u16 cpuid; /* bigger than needed, see above for reason */
#define ICE_RLAN_BASE_S 7
	u64 base;
	u16 qlen;
#define ICE_RLAN_CTX_DBUF_S 7
	u16 dbuf; /* bigger than needed, see above for reason */
#define ICE_RLAN_CTX_HBUF_S 6
	u16 hbuf; /* bigger than needed, see above for reason */
	u8 dtype;
	u8 dsize;
	u8 crcstrip;
	u8 l2tsel;
	u8 hsplit_0;
	u8 hsplit_1;
	u8 showiv;
	u32 rxmax; /* bigger than needed, see above for reason */
	u8 tphrdesc_ena;
	u8 tphwdesc_ena;
	u8 tphdata_ena;
	u8 tphhead_ena;
	u16 lrxqthresh; /* bigger than needed, see above for reason */
	u8 prefena;	/* NOTE: normally must be set to 1 at init */
};

struct ice_ctx_ele {
	u16 offset;
	u16 size_of;
	u16 width;
	u16 lsb;
};

#define ICE_CTX_STORE(_struct, _ele, _width, _lsb) {	\
	.offset = offsetof(struct _struct, _ele),	\
	.size_of = sizeof_field(struct _struct, _ele),	\
	.width = _width,				\
	.lsb = _lsb,					\
}

/* for hsplit_0 field of Rx RLAN context */
enum ice_rlan_ctx_rx_hsplit_0 {
	ICE_RLAN_RX_HSPLIT_0_NO_SPLIT		= 0,
	ICE_RLAN_RX_HSPLIT_0_SPLIT_L2		= 1,
	ICE_RLAN_RX_HSPLIT_0_SPLIT_IP		= 2,
	ICE_RLAN_RX_HSPLIT_0_SPLIT_TCP_UDP	= 4,
	ICE_RLAN_RX_HSPLIT_0_SPLIT_SCTP		= 8,
};

/* for hsplit_1 field of Rx RLAN context */
enum ice_rlan_ctx_rx_hsplit_1 {
	ICE_RLAN_RX_HSPLIT_1_NO_SPLIT		= 0,
	ICE_RLAN_RX_HSPLIT_1_SPLIT_L2		= 1,
	ICE_RLAN_RX_HSPLIT_1_SPLIT_ALWAYS	= 2,
};

/* Tx Descriptor */
struct ice_tx_desc {
	__le64 buf_addr; /* Address of descriptor's data buf */
	__le64 cmd_type_offset_bsz;
};

enum ice_tx_desc_dtype_value {
	ICE_TX_DESC_DTYPE_DATA		= 0x0,
	ICE_TX_DESC_DTYPE_CTX		= 0x1,
	ICE_TX_DESC_DTYPE_FLTR_PROG	= 0x8,
	/* DESC_DONE - HW has completed write-back of descriptor */
	ICE_TX_DESC_DTYPE_DESC_DONE	= 0xF,
};

#define ICE_TXD_QW1_CMD_S	4
#define ICE_TXD_QW1_CMD_M	(0xFFFUL << ICE_TXD_QW1_CMD_S)

enum ice_tx_desc_cmd_bits {
	ICE_TX_DESC_CMD_EOP			= 0x0001,
	ICE_TX_DESC_CMD_RS			= 0x0002,
	ICE_TX_DESC_CMD_IL2TAG1			= 0x0008,
	ICE_TX_DESC_CMD_DUMMY			= 0x0010,
	ICE_TX_DESC_CMD_IIPT_IPV6		= 0x0020,
	ICE_TX_DESC_CMD_IIPT_IPV4		= 0x0040,
	ICE_TX_DESC_CMD_IIPT_IPV4_CSUM		= 0x0060,
	ICE_TX_DESC_CMD_L4T_EOFT_TCP		= 0x0100,
	ICE_TX_DESC_CMD_L4T_EOFT_SCTP		= 0x0200,
	ICE_TX_DESC_CMD_L4T_EOFT_UDP		= 0x0300,
	ICE_TX_DESC_CMD_RE			= 0x0400,
};

#define ICE_TXD_QW1_OFFSET_S	16
#define ICE_TXD_QW1_OFFSET_M	(0x3FFFFULL << ICE_TXD_QW1_OFFSET_S)

enum ice_tx_desc_len_fields {
	/* Note: These are predefined bit offsets */
	ICE_TX_DESC_LEN_MACLEN_S	= 0, /* 7 BITS */
	ICE_TX_DESC_LEN_IPLEN_S	= 7, /* 7 BITS */
	ICE_TX_DESC_LEN_L4_LEN_S	= 14 /* 4 BITS */
};

#define ICE_TXD_QW1_MACLEN_M (0x7FUL << ICE_TX_DESC_LEN_MACLEN_S)
#define ICE_TXD_QW1_IPLEN_M  (0x7FUL << ICE_TX_DESC_LEN_IPLEN_S)
#define ICE_TXD_QW1_L4LEN_M  (0xFUL << ICE_TX_DESC_LEN_L4_LEN_S)

/* Tx descriptor field limits in bytes */
#define ICE_TXD_MACLEN_MAX ((ICE_TXD_QW1_MACLEN_M >> \
			     ICE_TX_DESC_LEN_MACLEN_S) * ICE_BYTES_PER_WORD)
#define ICE_TXD_IPLEN_MAX ((ICE_TXD_QW1_IPLEN_M >> \
			    ICE_TX_DESC_LEN_IPLEN_S) * ICE_BYTES_PER_DWORD)
#define ICE_TXD_L4LEN_MAX ((ICE_TXD_QW1_L4LEN_M >> \
			    ICE_TX_DESC_LEN_L4_LEN_S) * ICE_BYTES_PER_DWORD)

#define ICE_TXD_QW1_TX_BUF_SZ_S	34
#define ICE_TXD_QW1_L2TAG1_S	48

/* Context descriptors */
struct ice_tx_ctx_desc {
	__le32 tunneling_params;
	__le16 l2tag2;
	__le16 rsvd;
	__le64 qw1;
};

#define ICE_TXD_CTX_QW1_CMD_S	4
#define ICE_TXD_CTX_QW1_CMD_M	(0x7FUL << ICE_TXD_CTX_QW1_CMD_S)

#define ICE_TXD_CTX_QW1_TSO_LEN_S	30
#define ICE_TXD_CTX_QW1_TSO_LEN_M	\
			(0x3FFFFULL << ICE_TXD_CTX_QW1_TSO_LEN_S)

#define ICE_TXD_CTX_QW1_MSS_S	50
#define ICE_TXD_CTX_MIN_MSS	64

enum ice_tx_ctx_desc_cmd_bits {
	ICE_TX_CTX_DESC_TSO		= 0x01,
	ICE_TX_CTX_DESC_TSYN		= 0x02,
	ICE_TX_CTX_DESC_IL2TAG2		= 0x04,
	ICE_TX_CTX_DESC_IL2TAG2_IL2H	= 0x08,
	ICE_TX_CTX_DESC_SWTCH_NOTAG	= 0x00,
	ICE_TX_CTX_DESC_SWTCH_UPLINK	= 0x10,
	ICE_TX_CTX_DESC_SWTCH_LOCAL	= 0x20,
	ICE_TX_CTX_DESC_SWTCH_VSI	= 0x30,
	ICE_TX_CTX_DESC_RESERVED	= 0x40
};

enum ice_tx_ctx_desc_eipt_offload {
	ICE_TX_CTX_EIPT_NONE		= 0x0,
	ICE_TX_CTX_EIPT_IPV6		= 0x1,
	ICE_TX_CTX_EIPT_IPV4_NO_CSUM	= 0x2,
	ICE_TX_CTX_EIPT_IPV4		= 0x3
};

#define ICE_TXD_CTX_QW0_EIPLEN_S	2

#define ICE_TXD_CTX_QW0_L4TUNT_S	9

#define ICE_TXD_CTX_UDP_TUNNELING	BIT_ULL(ICE_TXD_CTX_QW0_L4TUNT_S)
#define ICE_TXD_CTX_GRE_TUNNELING	(0x2ULL << ICE_TXD_CTX_QW0_L4TUNT_S)

#define ICE_TXD_CTX_QW0_NATLEN_S	12

#define ICE_TXD_CTX_QW0_L4T_CS_S	23
#define ICE_TXD_CTX_QW0_L4T_CS_M	BIT_ULL(ICE_TXD_CTX_QW0_L4T_CS_S)

#define ICE_LAN_TXQ_MAX_QGRPS	127
#define ICE_LAN_TXQ_MAX_QDIS	1023

/* Tx queue context data
 *
 * The sizes of the variables may be larger than needed due to crossing byte
 * boundaries. If we do not have the width of the variable set to the correct
 * size then we could end up shifting bits off the top of the variable when the
 * variable is at the top of a byte and crosses over into the next byte.
 */
struct ice_tlan_ctx {
#define ICE_TLAN_CTX_BASE_S	7
	u64 base;		/* base is defined in 128-byte units */
	u8 port_num;
	u16 cgd_num;		/* bigger than needed, see above for reason */
	u8 pf_num;
	u16 vmvf_num;
	u8 vmvf_type;
#define ICE_TLAN_CTX_VMVF_TYPE_VF	0
#define ICE_TLAN_CTX_VMVF_TYPE_VMQ	1
#define ICE_TLAN_CTX_VMVF_TYPE_PF	2
	u16 src_vsi;
	u8 tsyn_ena;
	u8 internal_usage_flag;
	u8 alt_vlan;
	u16 cpuid;		/* bigger than needed, see above for reason */
	u8 wb_mode;
	u8 tphrd_desc;
	u8 tphrd;
	u8 tphwr_desc;
	u16 cmpq_id;
	u16 qnum_in_func;
	u8 itr_notification_mode;
	u8 adjust_prof_id;
	u32 qlen;		/* bigger than needed, see above for reason */
	u8 quanta_prof_idx;
	u8 tso_ena;
	u16 tso_qnum;
	u8 legacy_int;
	u8 drop_ena;
	u8 cache_prof_idx;
	u8 pkt_shaper_prof_idx;
	u8 int_q_state;	/* width not needed - internal - DO NOT WRITE!!! */
};

/* The ice_ptype_lkup table is used to convert from the 10-bit ptype in the
 * hardware to a bit-field that can be used by SW to more easily determine the
 * packet type.
 *
 * Macros are used to shorten the table lines and make this table human
 * readable.
 *
 * We store the PTYPE in the top byte of the bit field - this is just so that
 * we can check that the table doesn't have a row missing, as the index into
 * the table should be the PTYPE.
 *
 * Typical work flow:
 *
 * IF NOT ice_ptype_lkup[ptype].known
 * THEN
 *      Packet is unknown
 * ELSE IF ice_ptype_lkup[ptype].outer_ip == ICE_RX_PTYPE_OUTER_IP
 *      Use the rest of the fields to look at the tunnels, inner protocols, etc
 * ELSE
 *      Use the enum ice_rx_l2_ptype to decode the packet type
 * ENDIF
 */

/* macro to make the table lines short, use explicit indexing with [PTYPE] */
#define ICE_PTT(PTYPE, OUTER_IP, OUTER_IP_VER, OUTER_FRAG, T, TE, TEF, I, PL)\
	[PTYPE] = { \
		1, \
		ICE_RX_PTYPE_OUTER_##OUTER_IP, \
		ICE_RX_PTYPE_OUTER_##OUTER_IP_VER, \
		ICE_RX_PTYPE_##OUTER_FRAG, \
		ICE_RX_PTYPE_TUNNEL_##T, \
		ICE_RX_PTYPE_TUNNEL_END_##TE, \
		ICE_RX_PTYPE_##TEF, \
		ICE_RX_PTYPE_INNER_PROT_##I, \
		ICE_RX_PTYPE_PAYLOAD_LAYER_##PL }

#define ICE_PTT_UNUSED_ENTRY(PTYPE) [PTYPE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* shorter macros makes the table fit but are terse */
#define ICE_RX_PTYPE_NOF		ICE_RX_PTYPE_NOT_FRAG
#define ICE_RX_PTYPE_FRG		ICE_RX_PTYPE_FRAG

/* Lookup table mapping in the 10-bit HW PTYPE to the bit field for decoding */
static const struct ice_rx_ptype_decoded ice_ptype_lkup[BIT(10)] = {
	/* L2 Packet types */
	ICE_PTT_UNUSED_ENTRY(0),
	ICE_PTT(1, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	ICE_PTT_UNUSED_ENTRY(2),
	ICE_PTT_UNUSED_ENTRY(3),
	ICE_PTT_UNUSED_ENTRY(4),
	ICE_PTT_UNUSED_ENTRY(5),
	ICE_PTT(6, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	ICE_PTT(7, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	ICE_PTT_UNUSED_ENTRY(8),
	ICE_PTT_UNUSED_ENTRY(9),
	ICE_PTT(10, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	ICE_PTT(11, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	ICE_PTT_UNUSED_ENTRY(12),
	ICE_PTT_UNUSED_ENTRY(13),
	ICE_PTT_UNUSED_ENTRY(14),
	ICE_PTT_UNUSED_ENTRY(15),
	ICE_PTT_UNUSED_ENTRY(16),
	ICE_PTT_UNUSED_ENTRY(17),
	ICE_PTT_UNUSED_ENTRY(18),
	ICE_PTT_UNUSED_ENTRY(19),
	ICE_PTT_UNUSED_ENTRY(20),
	ICE_PTT_UNUSED_ENTRY(21),

	/* Non Tunneled IPv4 */
	ICE_PTT(22, IP, IPV4, FRG, NONE, NONE, NOF, NONE, PAY3),
	ICE_PTT(23, IP, IPV4, NOF, NONE, NONE, NOF, NONE, PAY3),
	ICE_PTT(24, IP, IPV4, NOF, NONE, NONE, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(25),
	ICE_PTT(26, IP, IPV4, NOF, NONE, NONE, NOF, TCP,  PAY4),
	ICE_PTT(27, IP, IPV4, NOF, NONE, NONE, NOF, SCTP, PAY4),
	ICE_PTT(28, IP, IPV4, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv4 --> IPv4 */
	ICE_PTT(29, IP, IPV4, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	ICE_PTT(30, IP, IPV4, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	ICE_PTT(31, IP, IPV4, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(32),
	ICE_PTT(33, IP, IPV4, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(34, IP, IPV4, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(35, IP, IPV4, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> IPv6 */
	ICE_PTT(36, IP, IPV4, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	ICE_PTT(37, IP, IPV4, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	ICE_PTT(38, IP, IPV4, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(39),
	ICE_PTT(40, IP, IPV4, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(41, IP, IPV4, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(42, IP, IPV4, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT */
	ICE_PTT(43, IP, IPV4, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> IPv4 */
	ICE_PTT(44, IP, IPV4, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	ICE_PTT(45, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	ICE_PTT(46, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(47),
	ICE_PTT(48, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(49, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(50, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> IPv6 */
	ICE_PTT(51, IP, IPV4, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	ICE_PTT(52, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	ICE_PTT(53, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(54),
	ICE_PTT(55, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(56, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(57, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC */
	ICE_PTT(58, IP, IPV4, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> MAC --> IPv4 */
	ICE_PTT(59, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	ICE_PTT(60, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	ICE_PTT(61, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(62),
	ICE_PTT(63, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(64, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(65, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT -> MAC --> IPv6 */
	ICE_PTT(66, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	ICE_PTT(67, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	ICE_PTT(68, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(69),
	ICE_PTT(70, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(71, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(72, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC/VLAN */
	ICE_PTT(73, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv4 ---> GRE/NAT -> MAC/VLAN --> IPv4 */
	ICE_PTT(74, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	ICE_PTT(75, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	ICE_PTT(76, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(77),
	ICE_PTT(78, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(79, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(80, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv4 -> GRE/NAT -> MAC/VLAN --> IPv6 */
	ICE_PTT(81, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	ICE_PTT(82, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	ICE_PTT(83, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(84),
	ICE_PTT(85, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(86, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(87, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* Non Tunneled IPv6 */
	ICE_PTT(88, IP, IPV6, FRG, NONE, NONE, NOF, NONE, PAY3),
	ICE_PTT(89, IP, IPV6, NOF, NONE, NONE, NOF, NONE, PAY3),
	ICE_PTT(90, IP, IPV6, NOF, NONE, NONE, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(91),
	ICE_PTT(92, IP, IPV6, NOF, NONE, NONE, NOF, TCP,  PAY4),
	ICE_PTT(93, IP, IPV6, NOF, NONE, NONE, NOF, SCTP, PAY4),
	ICE_PTT(94, IP, IPV6, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv6 --> IPv4 */
	ICE_PTT(95, IP, IPV6, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	ICE_PTT(96, IP, IPV6, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	ICE_PTT(97, IP, IPV6, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(98),
	ICE_PTT(99, IP, IPV6, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(100, IP, IPV6, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(101, IP, IPV6, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> IPv6 */
	ICE_PTT(102, IP, IPV6, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	ICE_PTT(103, IP, IPV6, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	ICE_PTT(104, IP, IPV6, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(105),
	ICE_PTT(106, IP, IPV6, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(107, IP, IPV6, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(108, IP, IPV6, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT */
	ICE_PTT(109, IP, IPV6, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> IPv4 */
	ICE_PTT(110, IP, IPV6, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	ICE_PTT(111, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	ICE_PTT(112, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(113),
	ICE_PTT(114, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(115, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(116, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> IPv6 */
	ICE_PTT(117, IP, IPV6, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	ICE_PTT(118, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	ICE_PTT(119, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(120),
	ICE_PTT(121, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(122, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(123, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC */
	ICE_PTT(124, IP, IPV6, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC -> IPv4 */
	ICE_PTT(125, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	ICE_PTT(126, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	ICE_PTT(127, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(128),
	ICE_PTT(129, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(130, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(131, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC -> IPv6 */
	ICE_PTT(132, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	ICE_PTT(133, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	ICE_PTT(134, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(135),
	ICE_PTT(136, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(137, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(138, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN */
	ICE_PTT(139, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv4 */
	ICE_PTT(140, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	ICE_PTT(141, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	ICE_PTT(142, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(143),
	ICE_PTT(144, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	ICE_PTT(145, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	ICE_PTT(146, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv6 */
	ICE_PTT(147, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	ICE_PTT(148, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	ICE_PTT(149, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	ICE_PTT_UNUSED_ENTRY(150),
	ICE_PTT(151, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	ICE_PTT(152, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	ICE_PTT(153, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* unused entries */
	[154 ... 1023] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static inline struct ice_rx_ptype_decoded ice_decode_rx_desc_ptype(u16 ptype)
{
	return ice_ptype_lkup[ptype];
}

#define ICE_LINK_SPEED_UNKNOWN		0
#define ICE_LINK_SPEED_10MBPS		10
#define ICE_LINK_SPEED_100MBPS		100
#define ICE_LINK_SPEED_1000MBPS		1000
#define ICE_LINK_SPEED_2500MBPS		2500
#define ICE_LINK_SPEED_5000MBPS		5000
#define ICE_LINK_SPEED_10000MBPS	10000
#define ICE_LINK_SPEED_20000MBPS	20000
#define ICE_LINK_SPEED_25000MBPS	25000
#define ICE_LINK_SPEED_40000MBPS	40000
#define ICE_LINK_SPEED_50000MBPS	50000
#define ICE_LINK_SPEED_100000MBPS	100000

#endif /* _ICE_LAN_TX_RX_H_ */
