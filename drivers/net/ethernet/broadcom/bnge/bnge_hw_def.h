/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_HW_DEF_H_
#define _BNGE_HW_DEF_H_

#define TX_BD_FLAGS_TCP_UDP_CHKSUM	BIT(0)
#define TX_BD_FLAGS_IP_CKSUM		BIT(1)
#define TX_BD_FLAGS_NO_CRC		BIT(2)
#define TX_BD_FLAGS_STAMP		BIT(3)
#define TX_BD_FLAGS_T_IP_CHKSUM		BIT(4)
#define TX_BD_FLAGS_LSO			BIT(5)
#define TX_BD_FLAGS_IPID_FMT		BIT(6)
#define TX_BD_FLAGS_T_IPID		BIT(7)
#define TX_BD_HSIZE			GENMASK(23, 16)
#define TX_BD_HSIZE_SHIFT		16

#define TX_BD_CFA_ACTION		GENMASK(31, 16)
#define TX_BD_CFA_ACTION_SHIFT		16

#define TX_BD_CFA_META_MASK		0xfffffff
#define TX_BD_CFA_META_VID_MASK		0xfff
#define TX_BD_CFA_META_PRI_MASK		GENMASK(15, 12)
#define TX_BD_CFA_META_PRI_SHIFT	12
#define TX_BD_CFA_META_TPID_MASK	GENMASK(17, 16)
#define TX_BD_CFA_META_TPID_SHIFT	16
#define TX_BD_CFA_META_KEY		GENMASK(31, 28)
#define TX_BD_CFA_META_KEY_SHIFT	28
#define TX_BD_CFA_META_KEY_VLAN		BIT(28)

struct tx_bd_ext {
	__le32 tx_bd_hsize_lflags;
	__le32 tx_bd_mss;
	__le32 tx_bd_cfa_action;
	__le32 tx_bd_cfa_meta;
};

#define TX_CMP_SQ_CONS_IDX(txcmp)					\
	(le32_to_cpu((txcmp)->sq_cons_idx) & TX_CMP_SQ_CONS_IDX_MASK)

#define RX_CMP_CMP_TYPE				GENMASK(5, 0)
#define RX_CMP_FLAGS_ERROR			BIT(6)
#define RX_CMP_FLAGS_PLACEMENT			GENMASK(9, 7)
#define RX_CMP_FLAGS_RSS_VALID			BIT(10)
#define RX_CMP_FLAGS_PKT_METADATA_PRESENT	BIT(11)
#define RX_CMP_FLAGS_ITYPES_SHIFT		12
#define RX_CMP_FLAGS_ITYPES_MASK		0xf000
#define RX_CMP_FLAGS_ITYPE_UNKNOWN		(0 << 12)
#define RX_CMP_FLAGS_ITYPE_IP			(1 << 12)
#define RX_CMP_FLAGS_ITYPE_TCP			(2 << 12)
#define RX_CMP_FLAGS_ITYPE_UDP			(3 << 12)
#define RX_CMP_FLAGS_ITYPE_FCOE			(4 << 12)
#define RX_CMP_FLAGS_ITYPE_ROCE			(5 << 12)
#define RX_CMP_FLAGS_ITYPE_PTP_WO_TS		(8 << 12)
#define RX_CMP_FLAGS_ITYPE_PTP_W_TS		(9 << 12)
#define RX_CMP_LEN				GENMASK(31, 16)
#define RX_CMP_LEN_SHIFT			16

#define RX_CMP_V1				BIT(0)
#define RX_CMP_AGG_BUFS				GENMASK(5, 1)
#define RX_CMP_AGG_BUFS_SHIFT			1
#define RX_CMP_RSS_HASH_TYPE			GENMASK(15, 9)
#define RX_CMP_RSS_HASH_TYPE_SHIFT		9
#define RX_CMP_V3_RSS_EXT_OP_LEGACY		GENMASK(15, 12)
#define RX_CMP_V3_RSS_EXT_OP_LEGACY_SHIFT	12
#define RX_CMP_V3_RSS_EXT_OP_NEW		GENMASK(11, 8)
#define RX_CMP_V3_RSS_EXT_OP_NEW_SHIFT		8
#define RX_CMP_PAYLOAD_OFFSET			GENMASK(23, 16)
#define RX_CMP_PAYLOAD_OFFSET_SHIFT		16
#define RX_CMP_SUB_NS_TS			GENMASK(19, 16)
#define RX_CMP_SUB_NS_TS_SHIFT			16
#define RX_CMP_METADATA1			GENMASK(31, 28)
#define RX_CMP_METADATA1_SHIFT			28
#define RX_CMP_METADATA1_TPID_SEL		GENMASK(30, 28)
#define RX_CMP_METADATA1_TPID_8021Q		BIT(28)
#define RX_CMP_METADATA1_TPID_8021AD		(0x0 << 28)
#define RX_CMP_METADATA1_VALID			BIT(31)

struct rx_cmp {
	__le32 rx_cmp_len_flags_type;
	u32 rx_cmp_opaque;
	__le32 rx_cmp_misc_v1;
	__le32 rx_cmp_rss_hash;
};

#define RX_CMP_FLAGS2_IP_CS_CALC			BIT(0)
#define RX_CMP_FLAGS2_L4_CS_CALC			BIT(1)
#define RX_CMP_FLAGS2_T_IP_CS_CALC			BIT(2)
#define RX_CMP_FLAGS2_T_L4_CS_CALC			BIT(3)
#define RX_CMP_FLAGS2_META_FORMAT_VLAN			BIT(4)

#define RX_CMP_FLAGS2_METADATA_TCI_MASK			GENMASK(15, 0)
#define RX_CMP_FLAGS2_METADATA_VID_MASK			GENMASK(11, 0)
#define RX_CMP_FLAGS2_METADATA_TPID_MASK		GENMASK(31, 16)
#define RX_CMP_FLAGS2_METADATA_TPID_SFT			16

#define RX_CMP_V					BIT(0)
#define RX_CMPL_ERRORS_MASK				GENMASK(15, 1)
#define RX_CMPL_ERRORS_SFT				1
#define RX_CMPL_ERRORS_BUFFER_ERROR_MASK		GENMASK(3, 1)
#define RX_CMPL_ERRORS_BUFFER_ERROR_NO_BUFFER		(0x0 << 1)
#define RX_CMPL_ERRORS_BUFFER_ERROR_DID_NOT_FIT		(0x1 << 1)
#define RX_CMPL_ERRORS_BUFFER_ERROR_NOT_ON_CHIP		(0x2 << 1)
#define RX_CMPL_ERRORS_BUFFER_ERROR_BAD_FORMAT		(0x3 << 1)
#define RX_CMPL_ERRORS_IP_CS_ERROR			BIT(4)
#define RX_CMPL_ERRORS_L4_CS_ERROR			BIT(5)
#define RX_CMPL_ERRORS_T_IP_CS_ERROR			BIT(6)
#define RX_CMPL_ERRORS_T_L4_CS_ERROR			BIT(7)
#define RX_CMPL_ERRORS_CRC_ERROR			BIT(8)
#define RX_CMPL_ERRORS_T_PKT_ERROR_MASK			GENMASK(11, 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_NO_ERROR		(0x0 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_VERSION	(0x1 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_HDR_LEN	(0x2 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_TUNNEL_TOTAL_ERROR	(0x3 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_T_IP_TOTAL_ERROR	(0x4 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_T_UDP_TOTAL_ERROR	(0x5 << 9)
#define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_TTL		(0x6 << 9)
#define RX_CMPL_ERRORS_PKT_ERROR_MASK			GENMASK(15, 12)
#define RX_CMPL_ERRORS_PKT_ERROR_NO_ERROR		(0x0 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_VERSION		(0x1 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_HDR_LEN		(0x2 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_TTL		(0x3 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_IP_TOTAL_ERROR		(0x4 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_UDP_TOTAL_ERROR	(0x5 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN		(0x6 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN_TOO_SMALL (0x7 << 12)
#define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_OPT_LEN		(0x8 << 12)

#define RX_CMPL_CFA_CODE_MASK				GENMASK(31, 16)
#define RX_CMPL_CFA_CODE_SFT				16
#define RX_CMPL_METADATA0_TCI_MASK			GENMASK(31, 16)
#define RX_CMPL_METADATA0_VID_MASK			GENMASK(27, 16)
#define RX_CMPL_METADATA0_SFT				16

struct rx_cmp_ext {
	__le32 rx_cmp_flags2;
	__le32 rx_cmp_meta_data;
	__le32 rx_cmp_cfa_code_errors_v2;
	__le32 rx_cmp_timestamp;
};

#define RX_AGG_CMP_TYPE			GENMASK(5, 0)
#define RX_AGG_CMP_LEN			GENMASK(31, 16)
#define RX_AGG_CMP_LEN_SHIFT		16
#define RX_AGG_CMP_V			BIT(0)
#define RX_AGG_CMP_AGG_ID		GENMASK(25, 16)
#define RX_AGG_CMP_AGG_ID_SHIFT		16

struct rx_agg_cmp {
	__le32 rx_agg_cmp_len_flags_type;
	u32 rx_agg_cmp_opaque;
	__le32 rx_agg_cmp_v;
	__le32 rx_agg_cmp_unused;
};

#define RX_CMP_L2_ERRORS						\
	cpu_to_le32(RX_CMPL_ERRORS_BUFFER_ERROR_MASK | RX_CMPL_ERRORS_CRC_ERROR)

#define RX_CMP_L4_CS_BITS						\
	(cpu_to_le32(RX_CMP_FLAGS2_L4_CS_CALC | RX_CMP_FLAGS2_T_L4_CS_CALC))

#define RX_CMP_L4_CS_ERR_BITS						\
	(cpu_to_le32(RX_CMPL_ERRORS_L4_CS_ERROR | RX_CMPL_ERRORS_T_L4_CS_ERROR))

#define RX_CMP_L4_CS_OK(rxcmp1)						\
	    (((rxcmp1)->rx_cmp_flags2 &	RX_CMP_L4_CS_BITS) &&		\
	     !((rxcmp1)->rx_cmp_cfa_code_errors_v2 & RX_CMP_L4_CS_ERR_BITS))

#define RX_CMP_METADATA0_TCI(rxcmp1)					\
	((le32_to_cpu((rxcmp1)->rx_cmp_cfa_code_errors_v2) &		\
	  RX_CMPL_METADATA0_TCI_MASK) >> RX_CMPL_METADATA0_SFT)

#define RX_CMP_ENCAP(rxcmp1)						\
	    ((le32_to_cpu((rxcmp1)->rx_cmp_flags2) &			\
	     RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3)

#define RX_CMP_V3_HASH_TYPE_LEGACY(rxcmp)				\
	((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) &			\
	  RX_CMP_V3_RSS_EXT_OP_LEGACY) >> RX_CMP_V3_RSS_EXT_OP_LEGACY_SHIFT)

#define RX_CMP_V3_HASH_TYPE_NEW(rxcmp)				\
	((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_V3_RSS_EXT_OP_NEW) >>\
	 RX_CMP_V3_RSS_EXT_OP_NEW_SHIFT)

#define RX_CMP_V3_HASH_TYPE(bd, rxcmp)				\
	(((bd)->rss_cap & BNGE_RSS_CAP_RSS_TCAM) ?		\
	  RX_CMP_V3_HASH_TYPE_NEW(rxcmp) :			\
	  RX_CMP_V3_HASH_TYPE_LEGACY(rxcmp))

#define EXT_OP_INNER_4		0x0
#define EXT_OP_OUTER_4		0x2
#define EXT_OP_INNFL_3		0x8
#define EXT_OP_OUTFL_3		0xa

#define RX_CMP_VLAN_VALID(rxcmp)				\
	((rxcmp)->rx_cmp_misc_v1 & cpu_to_le32(RX_CMP_METADATA1_VALID))

#define RX_CMP_VLAN_TPID_SEL(rxcmp)				\
	(le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_METADATA1_TPID_SEL)

#define RSS_PROFILE_ID_MASK	GENMASK(4, 0)

#define RX_CMP_HASH_TYPE(rxcmp)					\
	(((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_RSS_HASH_TYPE) >>\
	  RX_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define RX_CMP_HASH_VALID(rxcmp)				\
	((rxcmp)->rx_cmp_len_flags_type & cpu_to_le32(RX_CMP_FLAGS_RSS_VALID))

#define TPA_AGG_AGG_ID(rx_agg)				\
	((le32_to_cpu((rx_agg)->rx_agg_cmp_v) &		\
	 RX_AGG_CMP_AGG_ID) >> RX_AGG_CMP_AGG_ID_SHIFT)

#define RX_TPA_START_CMP_TYPE				GENMASK(5, 0)
#define RX_TPA_START_CMP_FLAGS				GENMASK(15, 6)
#define RX_TPA_START_CMP_FLAGS_SHIFT			6
#define RX_TPA_START_CMP_FLAGS_ERROR			BIT(6)
#define RX_TPA_START_CMP_FLAGS_PLACEMENT		GENMASK(9, 7)
#define RX_TPA_START_CMP_FLAGS_PLACEMENT_SHIFT		7
#define RX_TPA_START_CMP_FLAGS_PLACEMENT_JUMBO		BIT(7)
#define RX_TPA_START_CMP_FLAGS_PLACEMENT_HDS		(0x2 << 7)
#define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_JUMBO	(0x5 << 7)
#define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_HDS	(0x6 << 7)
#define RX_TPA_START_CMP_FLAGS_RSS_VALID		BIT(10)
#define RX_TPA_START_CMP_FLAGS_TIMESTAMP		BIT(11)
#define RX_TPA_START_CMP_FLAGS_ITYPES			GENMASK(15, 12)
#define RX_TPA_START_CMP_FLAGS_ITYPES_SHIFT		12
#define RX_TPA_START_CMP_FLAGS_ITYPE_TCP		(0x2 << 12)
#define RX_TPA_START_CMP_LEN				GENMASK(31, 16)
#define RX_TPA_START_CMP_LEN_SHIFT			16
#define RX_TPA_START_CMP_V1				BIT(0)
#define RX_TPA_START_CMP_RSS_HASH_TYPE			GENMASK(15, 9)
#define RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT		9
#define RX_TPA_START_CMP_V3_RSS_HASH_TYPE		GENMASK(15, 7)
#define RX_TPA_START_CMP_V3_RSS_HASH_TYPE_SHIFT		7
#define RX_TPA_START_CMP_AGG_ID				GENMASK(25, 16)
#define RX_TPA_START_CMP_AGG_ID_SHIFT			16
#define RX_TPA_START_CMP_METADATA1			GENMASK(31, 28)
#define RX_TPA_START_CMP_METADATA1_SHIFT		28
#define RX_TPA_START_METADATA1_TPID_SEL			GENMASK(30, 28)
#define RX_TPA_START_METADATA1_TPID_8021Q		BIT(28)
#define RX_TPA_START_METADATA1_TPID_8021AD		(0x0 << 28)
#define RX_TPA_START_METADATA1_VALID			BIT(31)

struct rx_tpa_start_cmp {
	__le32 rx_tpa_start_cmp_len_flags_type;
	u32 rx_tpa_start_cmp_opaque;
	__le32 rx_tpa_start_cmp_misc_v1;
	__le32 rx_tpa_start_cmp_rss_hash;
};

#define TPA_START_HASH_VALID(rx_tpa_start)				\
	((rx_tpa_start)->rx_tpa_start_cmp_len_flags_type &		\
	 cpu_to_le32(RX_TPA_START_CMP_FLAGS_RSS_VALID))

#define TPA_START_HASH_TYPE(rx_tpa_start)				\
	(((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	   RX_TPA_START_CMP_RSS_HASH_TYPE) >>				\
	  RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define TPA_START_V3_HASH_TYPE(rx_tpa_start)				\
	(((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	   RX_TPA_START_CMP_V3_RSS_HASH_TYPE) >>			\
	  RX_TPA_START_CMP_V3_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define TPA_START_AGG_ID(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_CMP_AGG_ID) >> RX_TPA_START_CMP_AGG_ID_SHIFT)

#define TPA_START_ERROR(rx_tpa_start)					\
	((rx_tpa_start)->rx_tpa_start_cmp_len_flags_type &		\
	 cpu_to_le32(RX_TPA_START_CMP_FLAGS_ERROR))

#define TPA_START_VLAN_VALID(rx_tpa_start)				\
	((rx_tpa_start)->rx_tpa_start_cmp_misc_v1 &			\
	 cpu_to_le32(RX_TPA_START_METADATA1_VALID))

#define TPA_START_VLAN_TPID_SEL(rx_tpa_start)				\
	(le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_METADATA1_TPID_SEL)

#define RX_TPA_START_CMP_FLAGS2_IP_CS_CALC		BIT(0)
#define RX_TPA_START_CMP_FLAGS2_L4_CS_CALC		BIT(1)
#define RX_TPA_START_CMP_FLAGS2_T_IP_CS_CALC		BIT(2)
#define RX_TPA_START_CMP_FLAGS2_T_L4_CS_CALC		BIT(3)
#define RX_TPA_START_CMP_FLAGS2_IP_TYPE			BIT(8)
#define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL_VALID		BIT(9)
#define RX_TPA_START_CMP_FLAGS2_EXT_META_FORMAT		GENMASK(11, 10)
#define RX_TPA_START_CMP_FLAGS2_EXT_META_FORMAT_SHIFT	10
#define RX_TPA_START_CMP_V3_FLAGS2_T_IP_TYPE		BIT(10)
#define RX_TPA_START_CMP_V3_FLAGS2_AGG_GRO		BIT(11)
#define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL		GENMASK(31, 16)
#define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL_SHIFT		16
#define RX_TPA_START_CMP_V2				BIT(0)
#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_MASK	GENMASK(3, 1)
#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_SHIFT	1
#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_NO_BUFFER	(0x0 << 1)
#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT	(0x3 << 1)
#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_FLUSH	(0x5 << 1)
#define RX_TPA_START_CMP_CFA_CODE			GENMASK(31, 16)
#define RX_TPA_START_CMPL_CFA_CODE_SHIFT		16
#define RX_TPA_START_CMP_METADATA0_TCI_MASK		GENMASK(31, 16)
#define RX_TPA_START_CMP_METADATA0_VID_MASK		GENMASK(27, 16)
#define RX_TPA_START_CMP_METADATA0_SFT			16

struct rx_tpa_start_cmp_ext {
	__le32 rx_tpa_start_cmp_flags2;
	__le32 rx_tpa_start_cmp_metadata;
	__le32 rx_tpa_start_cmp_cfa_code_v2;
	__le32 rx_tpa_start_cmp_hdr_info;
};

#define TPA_START_CFA_CODE(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_cfa_code_v2) &	\
	 RX_TPA_START_CMP_CFA_CODE) >> RX_TPA_START_CMPL_CFA_CODE_SHIFT)

#define TPA_START_IS_IPV6(rx_tpa_start)				\
	(!!((rx_tpa_start)->rx_tpa_start_cmp_flags2 &		\
	    cpu_to_le32(RX_TPA_START_CMP_FLAGS2_IP_TYPE)))

#define TPA_START_ERROR_CODE(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_cfa_code_v2) &	\
	  RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_MASK) >>			\
	 RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_SHIFT)

#define TPA_START_METADATA0_TCI(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_cfa_code_v2) &	\
	  RX_TPA_START_CMP_METADATA0_TCI_MASK) >>			\
	 RX_TPA_START_CMP_METADATA0_SFT)

#define RX_TPA_END_CMP_TYPE				GENMASK(5, 0)
#define RX_TPA_END_CMP_FLAGS				GENMASK(15, 6)
#define RX_TPA_END_CMP_FLAGS_SHIFT			6
#define RX_TPA_END_CMP_FLAGS_PLACEMENT			GENMASK(9, 7)
#define RX_TPA_END_CMP_FLAGS_PLACEMENT_SHIFT		7
#define RX_TPA_END_CMP_FLAGS_PLACEMENT_JUMBO		BIT(7)
#define RX_TPA_END_CMP_FLAGS_PLACEMENT_HDS		(0x2 << 7)
#define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_JUMBO	(0x5 << 7)
#define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_HDS		(0x6 << 7)
#define RX_TPA_END_CMP_FLAGS_RSS_VALID			BIT(10)
#define RX_TPA_END_CMP_FLAGS_ITYPES			GENMASK(15, 12)
#define RX_TPA_END_CMP_FLAGS_ITYPES_SHIFT		12
#define RX_TPA_END_CMP_FLAGS_ITYPE_TCP			(0x2 << 12)
#define RX_TPA_END_CMP_LEN				GENMASK(31, 16)
#define RX_TPA_END_CMP_LEN_SHIFT			16
#define RX_TPA_END_CMP_V1				BIT(0)
#define RX_TPA_END_CMP_TPA_SEGS				GENMASK(15, 8)
#define RX_TPA_END_CMP_TPA_SEGS_SHIFT			8
#define RX_TPA_END_CMP_AGG_ID				GENMASK(25, 16)
#define RX_TPA_END_CMP_AGG_ID_SHIFT			16
#define RX_TPA_END_GRO_TS				BIT(31)

struct rx_tpa_end_cmp {
	__le32 rx_tpa_end_cmp_len_flags_type;
	u32 rx_tpa_end_cmp_opaque;
	__le32 rx_tpa_end_cmp_misc_v1;
	__le32 rx_tpa_end_cmp_tsdelta;
};

#define TPA_END_AGG_ID(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_AGG_ID) >> RX_TPA_END_CMP_AGG_ID_SHIFT)

#define TPA_END_TPA_SEGS(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_TPA_SEGS) >> RX_TPA_END_CMP_TPA_SEGS_SHIFT)

#define RX_TPA_END_CMP_FLAGS_PLACEMENT_ANY_GRO				\
	cpu_to_le32(RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_JUMBO &		\
		    RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_HDS)

#define TPA_END_GRO(rx_tpa_end)						\
	((rx_tpa_end)->rx_tpa_end_cmp_len_flags_type &			\
	 RX_TPA_END_CMP_FLAGS_PLACEMENT_ANY_GRO)

#define TPA_END_GRO_TS(rx_tpa_end)					\
	(!!((rx_tpa_end)->rx_tpa_end_cmp_tsdelta &			\
	    cpu_to_le32(RX_TPA_END_GRO_TS)))

#define RX_TPA_END_CMP_TPA_DUP_ACKS			GENMASK(3, 0)
#define RX_TPA_END_CMP_PAYLOAD_OFFSET			GENMASK(23, 16)
#define RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT		16
#define RX_TPA_END_CMP_AGG_BUFS				GENMASK(31, 24)
#define RX_TPA_END_CMP_AGG_BUFS_SHIFT			24
#define RX_TPA_END_CMP_TPA_SEG_LEN			GENMASK(15, 0)
#define RX_TPA_END_CMP_V2				BIT(0)
#define RX_TPA_END_CMP_ERRORS				GENMASK(2, 1)
#define RX_TPA_END_CMPL_ERRORS_SHIFT			1
#define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_NO_BUFFER	(0x0 << 1)
#define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_NOT_ON_CHIP	(0x2 << 1)
#define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT	(0x3 << 1)
#define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_RSV_ERROR	(0x4 << 1)
#define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_FLUSH	(0x5 << 1)

struct rx_tpa_end_cmp_ext {
	__le32 rx_tpa_end_cmp_dup_acks;
	__le32 rx_tpa_end_cmp_seg_len;
	__le32 rx_tpa_end_cmp_errors_v2;
	u32 rx_tpa_end_cmp_start_opaque;
};

#define TPA_END_ERRORS(rx_tpa_end_ext)					\
	((rx_tpa_end_ext)->rx_tpa_end_cmp_errors_v2 &			\
	 cpu_to_le32(RX_TPA_END_CMP_ERRORS))

#define TPA_END_PAYLOAD_OFF(rx_tpa_end_ext)				\
	((le32_to_cpu((rx_tpa_end_ext)->rx_tpa_end_cmp_dup_acks) &	\
	 RX_TPA_END_CMP_PAYLOAD_OFFSET) >>				\
	RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT)

#define TPA_END_AGG_BUFS(rx_tpa_end_ext)				\
	((le32_to_cpu((rx_tpa_end_ext)->rx_tpa_end_cmp_dup_acks) &	\
	 RX_TPA_END_CMP_AGG_BUFS) >> RX_TPA_END_CMP_AGG_BUFS_SHIFT)

#define EVENT_DATA1_RESET_NOTIFY_FATAL(data1)				\
	(((data1) &							\
	  ASYNC_EVENT_CMPL_RESET_NOTIFY_EVENT_DATA1_REASON_CODE_MASK) ==\
	 ASYNC_EVENT_CMPL_RESET_NOTIFY_EVENT_DATA1_REASON_CODE_FW_EXCEPTION_FATAL)

#define EVENT_DATA1_RESET_NOTIFY_FW_ACTIVATION(data1)			\
	(((data1) &							\
	  ASYNC_EVENT_CMPL_RESET_NOTIFY_EVENT_DATA1_REASON_CODE_MASK) ==\
	ASYNC_EVENT_CMPL_RESET_NOTIFY_EVENT_DATA1_REASON_CODE_FW_ACTIVATION)

#define EVENT_DATA2_RESET_NOTIFY_FW_STATUS_CODE(data2)			\
	((data2) &							\
	ASYNC_EVENT_CMPL_RESET_NOTIFY_EVENT_DATA2_FW_STATUS_CODE_MASK)

#define EVENT_DATA1_RECOVERY_MASTER_FUNC(data1)				\
	(!!((data1) &							\
	   ASYNC_EVENT_CMPL_ERROR_RECOVERY_EVENT_DATA1_FLAGS_MASTER_FUNC))

#define EVENT_DATA1_RECOVERY_ENABLED(data1)				\
	(!!((data1) &							\
	   ASYNC_EVENT_CMPL_ERROR_RECOVERY_EVENT_DATA1_FLAGS_RECOVERY_ENABLED))

#define BNGE_EVENT_ERROR_REPORT_TYPE(data1)				\
	(((data1) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_MASK) >>\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_SFT)

#define BNGE_EVENT_INVALID_SIGNAL_DATA(data2)				\
	(((data2) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_INVALID_SIGNAL_EVENT_DATA2_PIN_ID_MASK) >>\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_INVALID_SIGNAL_EVENT_DATA2_PIN_ID_SFT)
#endif /* _BNGE_HW_DEF_H_ */
