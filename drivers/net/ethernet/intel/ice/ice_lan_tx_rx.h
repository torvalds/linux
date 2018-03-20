/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_LAN_TX_RX_H_
#define _ICE_LAN_TX_RX_H_

union ice_32byte_rx_desc {
	struct {
		__le64  pkt_addr; /* Packet buffer address */
		__le64  hdr_addr; /* Header buffer address */
			/* bit 0 of hdr_addr is DD bit */
		__le64  rsvd1;
		__le64  rsvd2;
	} read;
	struct {
		struct {
			struct {
				__le16 mirroring_status;
				__le16 l2tag1;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				__le32 fd_id; /* Flow Director filter id */
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

/* RX Flex Descriptor
 * This descriptor is used instead of the legacy version descriptor when
 * ice_rlan_ctx.adv_desc is set
 */
union ice_32b_rx_flex_desc {
	struct {
		__le64  pkt_addr; /* Packet buffer address */
		__le64  hdr_addr; /* Header buffer address */
				  /* bit 0 of hdr_addr is DD bit */
		__le64  rsvd1;
		__le64  rsvd2;
	} read;
	struct {
		/* Qword 0 */
		u8 rxdid; /* descriptor builder profile id */
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

/* Receive Flex Descriptor profile IDs: There are a total
 * of 64 profiles where profile IDs 0/1 are for legacy; and
 * profiles 2-63 are flex profiles that can be programmed
 * with a specific metadata (profile 7 reserved for HW)
 */
enum ice_rxdid {
	ICE_RXDID_START			= 0,
	ICE_RXDID_LEGACY_0		= ICE_RXDID_START,
	ICE_RXDID_LEGACY_1,
	ICE_RXDID_FLX_START,
	ICE_RXDID_FLEX_NIC		= ICE_RXDID_FLX_START,
	ICE_RXDID_FLX_LAST		= 63,
	ICE_RXDID_LAST			= ICE_RXDID_FLX_LAST
};

/* Receive Flex Descriptor Rx opcode values */
#define ICE_RX_OPC_MDID		0x01

/* Receive Descriptor MDID values */
#define ICE_RX_MDID_FLOW_ID_LOWER	5
#define ICE_RX_MDID_FLOW_ID_HIGH	6
#define ICE_RX_MDID_HASH_LOW		56
#define ICE_RX_MDID_HASH_HIGH		57

/* Rx Flag64 packet flag bits */
enum ice_rx_flg64_bits {
	ICE_RXFLG_PKT_DSI	= 0,
	ICE_RXFLG_EVLAN_x8100	= 15,
	ICE_RXFLG_EVLAN_x9100,
	ICE_RXFLG_VLAN_x8100,
	ICE_RXFLG_TNL_MAC	= 22,
	ICE_RXFLG_TNL_VLAN,
	ICE_RXFLG_PKT_FRG,
	ICE_RXFLG_FIN		= 32,
	ICE_RXFLG_SYN,
	ICE_RXFLG_RST,
	ICE_RXFLG_TNL0		= 38,
	ICE_RXFLG_TNL1,
	ICE_RXFLG_TNL2,
	ICE_RXFLG_UDP_GRE,
	ICE_RXFLG_RSVD		= 63
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

#define ICE_RXQ_CTX_SIZE_DWORDS		8
#define ICE_RXQ_CTX_SZ			(ICE_RXQ_CTX_SIZE_DWORDS * sizeof(u32))

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
	u64 base;
	u16 qlen;
#define ICE_RLAN_CTX_DBUF_S 7
	u16 dbuf; /* bigger than needed, see above for reason */
#define ICE_RLAN_CTX_HBUF_S 6
	u16 hbuf; /* bigger than needed, see above for reason */
	u8  dtype;
	u8  dsize;
	u8  crcstrip;
	u8  l2tsel;
	u8  hsplit_0;
	u8  hsplit_1;
	u8  showiv;
	u32 rxmax; /* bigger than needed, see above for reason */
	u8  tphrdesc_ena;
	u8  tphwdesc_ena;
	u8  tphdata_ena;
	u8  tphhead_ena;
	u16 lrxqthresh; /* bigger than needed, see above for reason */
};

struct ice_ctx_ele {
	u16 offset;
	u16 size_of;
	u16 width;
	u16 lsb;
};

#define ICE_CTX_STORE(_struct, _ele, _width, _lsb) {	\
	.offset = offsetof(struct _struct, _ele),	\
	.size_of = FIELD_SIZEOF(struct _struct, _ele),	\
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

/* TX Descriptor */
struct ice_tx_desc {
	__le64 buf_addr; /* Address of descriptor's data buf */
	__le64 cmd_type_offset_bsz;
};

enum ice_tx_desc_dtype_value {
	ICE_TX_DESC_DTYPE_DATA		= 0x0,
	ICE_TX_DESC_DTYPE_CTX		= 0x1,
	/* DESC_DONE - HW has completed write-back of descriptor */
	ICE_TX_DESC_DTYPE_DESC_DONE	= 0xF,
};

#define ICE_TXD_QW1_CMD_S	4
#define ICE_TXD_QW1_CMD_M	(0xFFFUL << ICE_TXD_QW1_CMD_S)

enum ice_tx_desc_cmd_bits {
	ICE_TX_DESC_CMD_EOP			= 0x0001,
	ICE_TX_DESC_CMD_RS			= 0x0002,
};

#define ICE_TXD_QW1_OFFSET_S	16
#define ICE_TXD_QW1_TX_BUF_SZ_S	34
#define ICE_TXD_QW1_L2TAG1_S	48

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
	u8  port_num;
	u16 cgd_num;		/* bigger than needed, see above for reason */
	u8  pf_num;
	u16 vmvf_num;
	u8  vmvf_type;
#define ICE_TLAN_CTX_VMVF_TYPE_VMQ	1
#define ICE_TLAN_CTX_VMVF_TYPE_PF	2
	u16 src_vsi;
	u8  tsyn_ena;
	u8  alt_vlan;
	u16 cpuid;		/* bigger than needed, see above for reason */
	u8  wb_mode;
	u8  tphrd_desc;
	u8  tphrd;
	u8  tphwr_desc;
	u16 cmpq_id;
	u16 qnum_in_func;
	u8  itr_notification_mode;
	u8  adjust_prof_id;
	u32 qlen;		/* bigger than needed, see above for reason */
	u8  quanta_prof_idx;
	u8  tso_ena;
	u16 tso_qnum;
	u8  legacy_int;
	u8  drop_ena;
	u8  cache_prof_idx;
	u8  pkt_shaper_prof_idx;
	u8  int_q_state;	/* width not needed - internal do not write */
};
#endif /* _ICE_LAN_TX_RX_H_ */
