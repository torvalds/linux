/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_H
#define BNXT_H

#define DRV_MODULE_NAME		"bnxt_en"
#define DRV_MODULE_VERSION	"1.10.0"

#define DRV_VER_MAJ	1
#define DRV_VER_MIN	10
#define DRV_VER_UPD	0

#include <linux/interrupt.h>
#include <linux/rhashtable.h>
#include <net/devlink.h>
#include <net/dst_metadata.h>
#include <net/switchdev.h>
#include <net/xdp.h>
#include <linux/net_dim.h>

struct tx_bd {
	__le32 tx_bd_len_flags_type;
	#define TX_BD_TYPE					(0x3f << 0)
	 #define TX_BD_TYPE_SHORT_TX_BD				 (0x00 << 0)
	 #define TX_BD_TYPE_LONG_TX_BD				 (0x10 << 0)
	#define TX_BD_FLAGS_PACKET_END				(1 << 6)
	#define TX_BD_FLAGS_NO_CMPL				(1 << 7)
	#define TX_BD_FLAGS_BD_CNT				(0x1f << 8)
	 #define TX_BD_FLAGS_BD_CNT_SHIFT			 8
	#define TX_BD_FLAGS_LHINT				(3 << 13)
	 #define TX_BD_FLAGS_LHINT_SHIFT			 13
	 #define TX_BD_FLAGS_LHINT_512_AND_SMALLER		 (0 << 13)
	 #define TX_BD_FLAGS_LHINT_512_TO_1023			 (1 << 13)
	 #define TX_BD_FLAGS_LHINT_1024_TO_2047			 (2 << 13)
	 #define TX_BD_FLAGS_LHINT_2048_AND_LARGER		 (3 << 13)
	#define TX_BD_FLAGS_COAL_NOW				(1 << 15)
	#define TX_BD_LEN					(0xffff << 16)
	 #define TX_BD_LEN_SHIFT				 16

	u32 tx_bd_opaque;
	__le64 tx_bd_haddr;
} __packed;

struct tx_bd_ext {
	__le32 tx_bd_hsize_lflags;
	#define TX_BD_FLAGS_TCP_UDP_CHKSUM			(1 << 0)
	#define TX_BD_FLAGS_IP_CKSUM				(1 << 1)
	#define TX_BD_FLAGS_NO_CRC				(1 << 2)
	#define TX_BD_FLAGS_STAMP				(1 << 3)
	#define TX_BD_FLAGS_T_IP_CHKSUM				(1 << 4)
	#define TX_BD_FLAGS_LSO					(1 << 5)
	#define TX_BD_FLAGS_IPID_FMT				(1 << 6)
	#define TX_BD_FLAGS_T_IPID				(1 << 7)
	#define TX_BD_HSIZE					(0xff << 16)
	 #define TX_BD_HSIZE_SHIFT				 16

	__le32 tx_bd_mss;
	__le32 tx_bd_cfa_action;
	#define TX_BD_CFA_ACTION				(0xffff << 16)
	 #define TX_BD_CFA_ACTION_SHIFT				 16

	__le32 tx_bd_cfa_meta;
	#define TX_BD_CFA_META_MASK                             0xfffffff
	#define TX_BD_CFA_META_VID_MASK                         0xfff
	#define TX_BD_CFA_META_PRI_MASK                         (0xf << 12)
	 #define TX_BD_CFA_META_PRI_SHIFT                        12
	#define TX_BD_CFA_META_TPID_MASK                        (3 << 16)
	 #define TX_BD_CFA_META_TPID_SHIFT                       16
	#define TX_BD_CFA_META_KEY                              (0xf << 28)
	 #define TX_BD_CFA_META_KEY_SHIFT			 28
	#define TX_BD_CFA_META_KEY_VLAN                         (1 << 28)
};

struct rx_bd {
	__le32 rx_bd_len_flags_type;
	#define RX_BD_TYPE					(0x3f << 0)
	 #define RX_BD_TYPE_RX_PACKET_BD			 0x4
	 #define RX_BD_TYPE_RX_BUFFER_BD			 0x5
	 #define RX_BD_TYPE_RX_AGG_BD				 0x6
	 #define RX_BD_TYPE_16B_BD_SIZE				 (0 << 4)
	 #define RX_BD_TYPE_32B_BD_SIZE				 (1 << 4)
	 #define RX_BD_TYPE_48B_BD_SIZE				 (2 << 4)
	 #define RX_BD_TYPE_64B_BD_SIZE				 (3 << 4)
	#define RX_BD_FLAGS_SOP					(1 << 6)
	#define RX_BD_FLAGS_EOP					(1 << 7)
	#define RX_BD_FLAGS_BUFFERS				(3 << 8)
	 #define RX_BD_FLAGS_1_BUFFER_PACKET			 (0 << 8)
	 #define RX_BD_FLAGS_2_BUFFER_PACKET			 (1 << 8)
	 #define RX_BD_FLAGS_3_BUFFER_PACKET			 (2 << 8)
	 #define RX_BD_FLAGS_4_BUFFER_PACKET			 (3 << 8)
	#define RX_BD_LEN					(0xffff << 16)
	 #define RX_BD_LEN_SHIFT				 16

	u32 rx_bd_opaque;
	__le64 rx_bd_haddr;
};

struct tx_cmp {
	__le32 tx_cmp_flags_type;
	#define CMP_TYPE					(0x3f << 0)
	 #define CMP_TYPE_TX_L2_CMP				 0
	 #define CMP_TYPE_RX_L2_CMP				 17
	 #define CMP_TYPE_RX_AGG_CMP				 18
	 #define CMP_TYPE_RX_L2_TPA_START_CMP			 19
	 #define CMP_TYPE_RX_L2_TPA_END_CMP			 21
	 #define CMP_TYPE_STATUS_CMP				 32
	 #define CMP_TYPE_REMOTE_DRIVER_REQ			 34
	 #define CMP_TYPE_REMOTE_DRIVER_RESP			 36
	 #define CMP_TYPE_ERROR_STATUS				 48
	 #define CMPL_BASE_TYPE_STAT_EJECT			 0x1aUL
	 #define CMPL_BASE_TYPE_HWRM_DONE			 0x20UL
	 #define CMPL_BASE_TYPE_HWRM_FWD_REQ			 0x22UL
	 #define CMPL_BASE_TYPE_HWRM_FWD_RESP			 0x24UL
	 #define CMPL_BASE_TYPE_HWRM_ASYNC_EVENT		 0x2eUL

	#define TX_CMP_FLAGS_ERROR				(1 << 6)
	#define TX_CMP_FLAGS_PUSH				(1 << 7)

	u32 tx_cmp_opaque;
	__le32 tx_cmp_errors_v;
	#define TX_CMP_V					(1 << 0)
	#define TX_CMP_ERRORS_BUFFER_ERROR			(7 << 1)
	 #define TX_CMP_ERRORS_BUFFER_ERROR_NO_ERROR		 0
	 #define TX_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT		 2
	 #define TX_CMP_ERRORS_BUFFER_ERROR_INVALID_STAG	 4
	 #define TX_CMP_ERRORS_BUFFER_ERROR_STAG_BOUNDS		 5
	 #define TX_CMP_ERRORS_ZERO_LENGTH_PKT			 (1 << 4)
	 #define TX_CMP_ERRORS_EXCESSIVE_BD_LEN			 (1 << 5)
	 #define TX_CMP_ERRORS_DMA_ERROR			 (1 << 6)
	 #define TX_CMP_ERRORS_HINT_TOO_SHORT			 (1 << 7)

	__le32 tx_cmp_unsed_3;
};

struct rx_cmp {
	__le32 rx_cmp_len_flags_type;
	#define RX_CMP_CMP_TYPE					(0x3f << 0)
	#define RX_CMP_FLAGS_ERROR				(1 << 6)
	#define RX_CMP_FLAGS_PLACEMENT				(7 << 7)
	#define RX_CMP_FLAGS_RSS_VALID				(1 << 10)
	#define RX_CMP_FLAGS_UNUSED				(1 << 11)
	 #define RX_CMP_FLAGS_ITYPES_SHIFT			 12
	 #define RX_CMP_FLAGS_ITYPE_UNKNOWN			 (0 << 12)
	 #define RX_CMP_FLAGS_ITYPE_IP				 (1 << 12)
	 #define RX_CMP_FLAGS_ITYPE_TCP				 (2 << 12)
	 #define RX_CMP_FLAGS_ITYPE_UDP				 (3 << 12)
	 #define RX_CMP_FLAGS_ITYPE_FCOE			 (4 << 12)
	 #define RX_CMP_FLAGS_ITYPE_ROCE			 (5 << 12)
	 #define RX_CMP_FLAGS_ITYPE_PTP_WO_TS			 (8 << 12)
	 #define RX_CMP_FLAGS_ITYPE_PTP_W_TS			 (9 << 12)
	#define RX_CMP_LEN					(0xffff << 16)
	 #define RX_CMP_LEN_SHIFT				 16

	u32 rx_cmp_opaque;
	__le32 rx_cmp_misc_v1;
	#define RX_CMP_V1					(1 << 0)
	#define RX_CMP_AGG_BUFS					(0x1f << 1)
	 #define RX_CMP_AGG_BUFS_SHIFT				 1
	#define RX_CMP_RSS_HASH_TYPE				(0x7f << 9)
	 #define RX_CMP_RSS_HASH_TYPE_SHIFT			 9
	#define RX_CMP_PAYLOAD_OFFSET				(0xff << 16)
	 #define RX_CMP_PAYLOAD_OFFSET_SHIFT			 16

	__le32 rx_cmp_rss_hash;
};

#define RX_CMP_HASH_VALID(rxcmp)				\
	((rxcmp)->rx_cmp_len_flags_type & cpu_to_le32(RX_CMP_FLAGS_RSS_VALID))

#define RSS_PROFILE_ID_MASK	0x1f

#define RX_CMP_HASH_TYPE(rxcmp)					\
	(((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_RSS_HASH_TYPE) >>\
	  RX_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

struct rx_cmp_ext {
	__le32 rx_cmp_flags2;
	#define RX_CMP_FLAGS2_IP_CS_CALC			0x1
	#define RX_CMP_FLAGS2_L4_CS_CALC			(0x1 << 1)
	#define RX_CMP_FLAGS2_T_IP_CS_CALC			(0x1 << 2)
	#define RX_CMP_FLAGS2_T_L4_CS_CALC			(0x1 << 3)
	#define RX_CMP_FLAGS2_META_FORMAT_VLAN			(0x1 << 4)
	__le32 rx_cmp_meta_data;
	#define RX_CMP_FLAGS2_METADATA_TCI_MASK			0xffff
	#define RX_CMP_FLAGS2_METADATA_VID_MASK			0xfff
	#define RX_CMP_FLAGS2_METADATA_TPID_MASK		0xffff0000
	 #define RX_CMP_FLAGS2_METADATA_TPID_SFT		 16
	__le32 rx_cmp_cfa_code_errors_v2;
	#define RX_CMP_V					(1 << 0)
	#define RX_CMPL_ERRORS_MASK				(0x7fff << 1)
	 #define RX_CMPL_ERRORS_SFT				 1
	#define RX_CMPL_ERRORS_BUFFER_ERROR_MASK		(0x7 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_NO_BUFFER		 (0x0 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_DID_NOT_FIT	 (0x1 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_NOT_ON_CHIP	 (0x2 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_BAD_FORMAT		 (0x3 << 1)
	#define RX_CMPL_ERRORS_IP_CS_ERROR			(0x1 << 4)
	#define RX_CMPL_ERRORS_L4_CS_ERROR			(0x1 << 5)
	#define RX_CMPL_ERRORS_T_IP_CS_ERROR			(0x1 << 6)
	#define RX_CMPL_ERRORS_T_L4_CS_ERROR			(0x1 << 7)
	#define RX_CMPL_ERRORS_CRC_ERROR			(0x1 << 8)
	#define RX_CMPL_ERRORS_T_PKT_ERROR_MASK			(0x7 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_NO_ERROR		 (0x0 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_VERSION	 (0x1 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_HDR_LEN	 (0x2 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_TUNNEL_TOTAL_ERROR	 (0x3 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_IP_TOTAL_ERROR	 (0x4 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_UDP_TOTAL_ERROR	 (0x5 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_TTL	 (0x6 << 9)
	#define RX_CMPL_ERRORS_PKT_ERROR_MASK			(0xf << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_NO_ERROR		 (0x0 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_VERSION	 (0x1 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_HDR_LEN	 (0x2 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_TTL		 (0x3 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_IP_TOTAL_ERROR	 (0x4 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_UDP_TOTAL_ERROR	 (0x5 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN	 (0x6 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN_TOO_SMALL (0x7 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_OPT_LEN	 (0x8 << 12)

	#define RX_CMPL_CFA_CODE_MASK				(0xffff << 16)
	 #define RX_CMPL_CFA_CODE_SFT				 16

	__le32 rx_cmp_unused3;
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

#define RX_CMP_ENCAP(rxcmp1)						\
	    ((le32_to_cpu((rxcmp1)->rx_cmp_flags2) &			\
	     RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3)

#define RX_CMP_CFA_CODE(rxcmpl1)					\
	((le32_to_cpu((rxcmpl1)->rx_cmp_cfa_code_errors_v2) &		\
	  RX_CMPL_CFA_CODE_MASK) >> RX_CMPL_CFA_CODE_SFT)

struct rx_agg_cmp {
	__le32 rx_agg_cmp_len_flags_type;
	#define RX_AGG_CMP_TYPE					(0x3f << 0)
	#define RX_AGG_CMP_LEN					(0xffff << 16)
	 #define RX_AGG_CMP_LEN_SHIFT				 16
	u32 rx_agg_cmp_opaque;
	__le32 rx_agg_cmp_v;
	#define RX_AGG_CMP_V					(1 << 0)
	__le32 rx_agg_cmp_unused;
};

struct rx_tpa_start_cmp {
	__le32 rx_tpa_start_cmp_len_flags_type;
	#define RX_TPA_START_CMP_TYPE				(0x3f << 0)
	#define RX_TPA_START_CMP_FLAGS				(0x3ff << 6)
	 #define RX_TPA_START_CMP_FLAGS_SHIFT			 6
	#define RX_TPA_START_CMP_FLAGS_PLACEMENT		(0x7 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_SHIFT		 7
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_JUMBO		 (0x1 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_HDS		 (0x2 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_JUMBO	 (0x5 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_HDS	 (0x6 << 7)
	#define RX_TPA_START_CMP_FLAGS_RSS_VALID		(0x1 << 10)
	#define RX_TPA_START_CMP_FLAGS_ITYPES			(0xf << 12)
	 #define RX_TPA_START_CMP_FLAGS_ITYPES_SHIFT		 12
	 #define RX_TPA_START_CMP_FLAGS_ITYPE_TCP		 (0x2 << 12)
	#define RX_TPA_START_CMP_LEN				(0xffff << 16)
	 #define RX_TPA_START_CMP_LEN_SHIFT			 16

	u32 rx_tpa_start_cmp_opaque;
	__le32 rx_tpa_start_cmp_misc_v1;
	#define RX_TPA_START_CMP_V1				(0x1 << 0)
	#define RX_TPA_START_CMP_RSS_HASH_TYPE			(0x7f << 9)
	 #define RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT		 9
	#define RX_TPA_START_CMP_AGG_ID				(0x7f << 25)
	 #define RX_TPA_START_CMP_AGG_ID_SHIFT			 25

	__le32 rx_tpa_start_cmp_rss_hash;
};

#define TPA_START_HASH_VALID(rx_tpa_start)				\
	((rx_tpa_start)->rx_tpa_start_cmp_len_flags_type &		\
	 cpu_to_le32(RX_TPA_START_CMP_FLAGS_RSS_VALID))

#define TPA_START_HASH_TYPE(rx_tpa_start)				\
	(((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	   RX_TPA_START_CMP_RSS_HASH_TYPE) >>				\
	  RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define TPA_START_AGG_ID(rx_tpa_start)					\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_CMP_AGG_ID) >> RX_TPA_START_CMP_AGG_ID_SHIFT)

struct rx_tpa_start_cmp_ext {
	__le32 rx_tpa_start_cmp_flags2;
	#define RX_TPA_START_CMP_FLAGS2_IP_CS_CALC		(0x1 << 0)
	#define RX_TPA_START_CMP_FLAGS2_L4_CS_CALC		(0x1 << 1)
	#define RX_TPA_START_CMP_FLAGS2_T_IP_CS_CALC		(0x1 << 2)
	#define RX_TPA_START_CMP_FLAGS2_T_L4_CS_CALC		(0x1 << 3)
	#define RX_TPA_START_CMP_FLAGS2_IP_TYPE			(0x1 << 8)

	__le32 rx_tpa_start_cmp_metadata;
	__le32 rx_tpa_start_cmp_cfa_code_v2;
	#define RX_TPA_START_CMP_V2				(0x1 << 0)
	#define RX_TPA_START_CMP_CFA_CODE			(0xffff << 16)
	 #define RX_TPA_START_CMPL_CFA_CODE_SHIFT		 16
	__le32 rx_tpa_start_cmp_hdr_info;
};

#define TPA_START_CFA_CODE(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_cfa_code_v2) &	\
	 RX_TPA_START_CMP_CFA_CODE) >> RX_TPA_START_CMPL_CFA_CODE_SHIFT)

#define TPA_START_IS_IPV6(rx_tpa_start)				\
	(!!((rx_tpa_start)->rx_tpa_start_cmp_flags2 &		\
	    cpu_to_le32(RX_TPA_START_CMP_FLAGS2_IP_TYPE)))

struct rx_tpa_end_cmp {
	__le32 rx_tpa_end_cmp_len_flags_type;
	#define RX_TPA_END_CMP_TYPE				(0x3f << 0)
	#define RX_TPA_END_CMP_FLAGS				(0x3ff << 6)
	 #define RX_TPA_END_CMP_FLAGS_SHIFT			 6
	#define RX_TPA_END_CMP_FLAGS_PLACEMENT			(0x7 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_SHIFT		 7
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_JUMBO		 (0x1 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_HDS		 (0x2 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_JUMBO	 (0x5 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_HDS		 (0x6 << 7)
	#define RX_TPA_END_CMP_FLAGS_RSS_VALID			(0x1 << 10)
	#define RX_TPA_END_CMP_FLAGS_ITYPES			(0xf << 12)
	 #define RX_TPA_END_CMP_FLAGS_ITYPES_SHIFT		 12
	 #define RX_TPA_END_CMP_FLAGS_ITYPE_TCP			 (0x2 << 12)
	#define RX_TPA_END_CMP_LEN				(0xffff << 16)
	 #define RX_TPA_END_CMP_LEN_SHIFT			 16

	u32 rx_tpa_end_cmp_opaque;
	__le32 rx_tpa_end_cmp_misc_v1;
	#define RX_TPA_END_CMP_V1				(0x1 << 0)
	#define RX_TPA_END_CMP_AGG_BUFS				(0x3f << 1)
	 #define RX_TPA_END_CMP_AGG_BUFS_SHIFT			 1
	#define RX_TPA_END_CMP_TPA_SEGS				(0xff << 8)
	 #define RX_TPA_END_CMP_TPA_SEGS_SHIFT			 8
	#define RX_TPA_END_CMP_PAYLOAD_OFFSET			(0xff << 16)
	 #define RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT		 16
	#define RX_TPA_END_CMP_AGG_ID				(0x7f << 25)
	 #define RX_TPA_END_CMP_AGG_ID_SHIFT			 25

	__le32 rx_tpa_end_cmp_tsdelta;
	#define RX_TPA_END_GRO_TS				(0x1 << 31)
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

struct rx_tpa_end_cmp_ext {
	__le32 rx_tpa_end_cmp_dup_acks;
	#define RX_TPA_END_CMP_TPA_DUP_ACKS			(0xf << 0)

	__le32 rx_tpa_end_cmp_seg_len;
	#define RX_TPA_END_CMP_TPA_SEG_LEN			(0xffff << 0)

	__le32 rx_tpa_end_cmp_errors_v2;
	#define RX_TPA_END_CMP_V2				(0x1 << 0)
	#define RX_TPA_END_CMP_ERRORS				(0x3 << 1)
	#define RX_TPA_END_CMPL_ERRORS_SHIFT			 1

	u32 rx_tpa_end_cmp_start_opaque;
};

#define TPA_END_ERRORS(rx_tpa_end_ext)					\
	((rx_tpa_end_ext)->rx_tpa_end_cmp_errors_v2 &			\
	 cpu_to_le32(RX_TPA_END_CMP_ERRORS))

struct nqe_cn {
	__le16	type;
	#define NQ_CN_TYPE_MASK           0x3fUL
	#define NQ_CN_TYPE_SFT            0
	#define NQ_CN_TYPE_CQ_NOTIFICATION  0x30UL
	#define NQ_CN_TYPE_LAST            NQ_CN_TYPE_CQ_NOTIFICATION
	__le16	reserved16;
	__le32	cq_handle_low;
	__le32	v;
	#define NQ_CN_V     0x1UL
	__le32	cq_handle_high;
};

#define DB_IDX_MASK						0xffffff
#define DB_IDX_VALID						(0x1 << 26)
#define DB_IRQ_DIS						(0x1 << 27)
#define DB_KEY_TX						(0x0 << 28)
#define DB_KEY_RX						(0x1 << 28)
#define DB_KEY_CP						(0x2 << 28)
#define DB_KEY_ST						(0x3 << 28)
#define DB_KEY_TX_PUSH						(0x4 << 28)
#define DB_LONG_TX_PUSH						(0x2 << 24)

#define BNXT_MIN_ROCE_CP_RINGS	2
#define BNXT_MIN_ROCE_STAT_CTXS	1

/* 64-bit doorbell */
#define DBR_INDEX_MASK					0x0000000000ffffffULL
#define DBR_XID_MASK					0x000fffff00000000ULL
#define DBR_XID_SFT					32
#define DBR_PATH_L2					(0x1ULL << 56)
#define DBR_TYPE_SQ					(0x0ULL << 60)
#define DBR_TYPE_RQ					(0x1ULL << 60)
#define DBR_TYPE_SRQ					(0x2ULL << 60)
#define DBR_TYPE_SRQ_ARM				(0x3ULL << 60)
#define DBR_TYPE_CQ					(0x4ULL << 60)
#define DBR_TYPE_CQ_ARMSE				(0x5ULL << 60)
#define DBR_TYPE_CQ_ARMALL				(0x6ULL << 60)
#define DBR_TYPE_CQ_ARMENA				(0x7ULL << 60)
#define DBR_TYPE_SRQ_ARMENA				(0x8ULL << 60)
#define DBR_TYPE_CQ_CUTOFF_ACK				(0x9ULL << 60)
#define DBR_TYPE_NQ					(0xaULL << 60)
#define DBR_TYPE_NQ_ARM					(0xbULL << 60)
#define DBR_TYPE_NULL					(0xfULL << 60)

#define INVALID_HW_RING_ID	((u16)-1)

/* The hardware supports certain page sizes.  Use the supported page sizes
 * to allocate the rings.
 */
#if (PAGE_SHIFT < 12)
#define BNXT_PAGE_SHIFT	12
#elif (PAGE_SHIFT <= 13)
#define BNXT_PAGE_SHIFT	PAGE_SHIFT
#elif (PAGE_SHIFT < 16)
#define BNXT_PAGE_SHIFT	13
#else
#define BNXT_PAGE_SHIFT	16
#endif

#define BNXT_PAGE_SIZE	(1 << BNXT_PAGE_SHIFT)

/* The RXBD length is 16-bit so we can only support page sizes < 64K */
#if (PAGE_SHIFT > 15)
#define BNXT_RX_PAGE_SHIFT 15
#else
#define BNXT_RX_PAGE_SHIFT PAGE_SHIFT
#endif

#define BNXT_RX_PAGE_SIZE (1 << BNXT_RX_PAGE_SHIFT)

#define BNXT_MAX_MTU		9500
#define BNXT_MAX_PAGE_MODE_MTU	\
	((unsigned int)PAGE_SIZE - VLAN_ETH_HLEN - NET_IP_ALIGN -	\
	 XDP_PACKET_HEADROOM)

#define BNXT_MIN_PKT_SIZE	52

#define BNXT_DEFAULT_RX_RING_SIZE	511
#define BNXT_DEFAULT_TX_RING_SIZE	511

#define MAX_TPA		64

#if (BNXT_PAGE_SHIFT == 16)
#define MAX_RX_PAGES	1
#define MAX_RX_AGG_PAGES	4
#define MAX_TX_PAGES	1
#define MAX_CP_PAGES	8
#else
#define MAX_RX_PAGES	8
#define MAX_RX_AGG_PAGES	32
#define MAX_TX_PAGES	8
#define MAX_CP_PAGES	64
#endif

#define RX_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct rx_bd))
#define TX_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct tx_bd))
#define CP_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct tx_cmp))

#define SW_RXBD_RING_SIZE (sizeof(struct bnxt_sw_rx_bd) * RX_DESC_CNT)
#define HW_RXBD_RING_SIZE (sizeof(struct rx_bd) * RX_DESC_CNT)

#define SW_RXBD_AGG_RING_SIZE (sizeof(struct bnxt_sw_rx_agg_bd) * RX_DESC_CNT)

#define SW_TXBD_RING_SIZE (sizeof(struct bnxt_sw_tx_bd) * TX_DESC_CNT)
#define HW_TXBD_RING_SIZE (sizeof(struct tx_bd) * TX_DESC_CNT)

#define HW_CMPD_RING_SIZE (sizeof(struct tx_cmp) * CP_DESC_CNT)

#define BNXT_MAX_RX_DESC_CNT		(RX_DESC_CNT * MAX_RX_PAGES - 1)
#define BNXT_MAX_RX_JUM_DESC_CNT	(RX_DESC_CNT * MAX_RX_AGG_PAGES - 1)
#define BNXT_MAX_TX_DESC_CNT		(TX_DESC_CNT * MAX_TX_PAGES - 1)

#define RX_RING(x)	(((x) & ~(RX_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define RX_IDX(x)	((x) & (RX_DESC_CNT - 1))

#define TX_RING(x)	(((x) & ~(TX_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define TX_IDX(x)	((x) & (TX_DESC_CNT - 1))

#define CP_RING(x)	(((x) & ~(CP_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define CP_IDX(x)	((x) & (CP_DESC_CNT - 1))

#define TX_CMP_VALID(txcmp, raw_cons)					\
	(!!((txcmp)->tx_cmp_errors_v & cpu_to_le32(TX_CMP_V)) ==	\
	 !((raw_cons) & bp->cp_bit))

#define RX_CMP_VALID(rxcmp1, raw_cons)					\
	(!!((rxcmp1)->rx_cmp_cfa_code_errors_v2 & cpu_to_le32(RX_CMP_V)) ==\
	 !((raw_cons) & bp->cp_bit))

#define RX_AGG_CMP_VALID(agg, raw_cons)				\
	(!!((agg)->rx_agg_cmp_v & cpu_to_le32(RX_AGG_CMP_V)) ==	\
	 !((raw_cons) & bp->cp_bit))

#define NQ_CMP_VALID(nqcmp, raw_cons)				\
	(!!((nqcmp)->v & cpu_to_le32(NQ_CN_V)) == !((raw_cons) & bp->cp_bit))

#define TX_CMP_TYPE(txcmp)					\
	(le32_to_cpu((txcmp)->tx_cmp_flags_type) & CMP_TYPE)

#define RX_CMP_TYPE(rxcmp)					\
	(le32_to_cpu((rxcmp)->rx_cmp_len_flags_type) & RX_CMP_CMP_TYPE)

#define NEXT_RX(idx)		(((idx) + 1) & bp->rx_ring_mask)

#define NEXT_RX_AGG(idx)	(((idx) + 1) & bp->rx_agg_ring_mask)

#define NEXT_TX(idx)		(((idx) + 1) & bp->tx_ring_mask)

#define ADV_RAW_CMP(idx, n)	((idx) + (n))
#define NEXT_RAW_CMP(idx)	ADV_RAW_CMP(idx, 1)
#define RING_CMP(idx)		((idx) & bp->cp_ring_mask)
#define NEXT_CMP(idx)		RING_CMP(ADV_RAW_CMP(idx, 1))

#define BNXT_HWRM_MAX_REQ_LEN		(bp->hwrm_max_req_len)
#define BNXT_HWRM_SHORT_REQ_LEN		sizeof(struct hwrm_short_input)
#define DFLT_HWRM_CMD_TIMEOUT		500
#define HWRM_CMD_TIMEOUT		(bp->hwrm_cmd_timeout)
#define HWRM_RESET_TIMEOUT		((HWRM_CMD_TIMEOUT) * 4)
#define HWRM_RESP_ERR_CODE_MASK		0xffff
#define HWRM_RESP_LEN_OFFSET		4
#define HWRM_RESP_LEN_MASK		0xffff0000
#define HWRM_RESP_LEN_SFT		16
#define HWRM_RESP_VALID_MASK		0xff000000
#define BNXT_HWRM_REQ_MAX_SIZE		128
#define BNXT_HWRM_REQS_PER_PAGE		(BNXT_PAGE_SIZE /	\
					 BNXT_HWRM_REQ_MAX_SIZE)
#define HWRM_SHORT_MIN_TIMEOUT		3
#define HWRM_SHORT_MAX_TIMEOUT		10
#define HWRM_SHORT_TIMEOUT_COUNTER	5

#define HWRM_MIN_TIMEOUT		25
#define HWRM_MAX_TIMEOUT		40

#define HWRM_TOTAL_TIMEOUT(n)	(((n) <= HWRM_SHORT_TIMEOUT_COUNTER) ?	\
	((n) * HWRM_SHORT_MIN_TIMEOUT) :				\
	(HWRM_SHORT_TIMEOUT_COUNTER * HWRM_SHORT_MIN_TIMEOUT +		\
	 ((n) - HWRM_SHORT_TIMEOUT_COUNTER) * HWRM_MIN_TIMEOUT))

#define HWRM_VALID_BIT_DELAY_USEC	150

#define BNXT_HWRM_CHNL_CHIMP	0
#define BNXT_HWRM_CHNL_KONG	1

#define BNXT_RX_EVENT	1
#define BNXT_AGG_EVENT	2
#define BNXT_TX_EVENT	4

struct bnxt_sw_tx_bd {
	struct sk_buff		*skb;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	u8			is_gso;
	u8			is_push;
	union {
		unsigned short		nr_frags;
		u16			rx_prod;
	};
};

struct bnxt_sw_rx_bd {
	void			*data;
	u8			*data_ptr;
	dma_addr_t		mapping;
};

struct bnxt_sw_rx_agg_bd {
	struct page		*page;
	unsigned int		offset;
	dma_addr_t		mapping;
};

struct bnxt_ring_mem_info {
	int			nr_pages;
	int			page_size;
	u16			flags;
#define BNXT_RMEM_VALID_PTE_FLAG	1
#define BNXT_RMEM_RING_PTE_FLAG		2
#define BNXT_RMEM_USE_FULL_PAGE_FLAG	4

	u16			depth;

	void			**pg_arr;
	dma_addr_t		*dma_arr;

	__le64			*pg_tbl;
	dma_addr_t		pg_tbl_map;

	int			vmem_size;
	void			**vmem;
};

struct bnxt_ring_struct {
	struct bnxt_ring_mem_info	ring_mem;

	u16			fw_ring_id; /* Ring id filled by Chimp FW */
	union {
		u16		grp_idx;
		u16		map_idx; /* Used by cmpl rings */
	};
	u32			handle;
	u8			queue_id;
};

struct tx_push_bd {
	__le32			doorbell;
	__le32			tx_bd_len_flags_type;
	u32			tx_bd_opaque;
	struct tx_bd_ext	txbd2;
};

struct tx_push_buffer {
	struct tx_push_bd	push_bd;
	u32			data[25];
};

struct bnxt_db_info {
	void __iomem		*doorbell;
	union {
		u64		db_key64;
		u32		db_key32;
	};
};

struct bnxt_tx_ring_info {
	struct bnxt_napi	*bnapi;
	u16			tx_prod;
	u16			tx_cons;
	u16			txq_index;
	struct bnxt_db_info	tx_db;

	struct tx_bd		*tx_desc_ring[MAX_TX_PAGES];
	struct bnxt_sw_tx_bd	*tx_buf_ring;

	dma_addr_t		tx_desc_mapping[MAX_TX_PAGES];

	struct tx_push_buffer	*tx_push;
	dma_addr_t		tx_push_mapping;
	__le64			data_mapping;

#define BNXT_DEV_STATE_CLOSING	0x1
	u32			dev_state;

	struct bnxt_ring_struct	tx_ring_struct;
};

#define BNXT_LEGACY_COAL_CMPL_PARAMS					\
	(RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_INT_LAT_TMR_MIN |		\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_INT_LAT_TMR_MAX |		\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_TIMER_RESET |		\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_RING_IDLE |			\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_NUM_CMPL_DMA_AGGR |		\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_NUM_CMPL_DMA_AGGR_DURING_INT | \
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_CMPL_AGGR_DMA_TMR |		\
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_CMPL_AGGR_DMA_TMR_DURING_INT | \
	 RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_NUM_CMPL_AGGR_INT)

#define BNXT_COAL_CMPL_ENABLES						\
	(RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_NUM_CMPL_DMA_AGGR | \
	 RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_CMPL_AGGR_DMA_TMR | \
	 RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_INT_LAT_TMR_MAX | \
	 RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_NUM_CMPL_AGGR_INT)

#define BNXT_COAL_CMPL_MIN_TMR_ENABLE					\
	RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_INT_LAT_TMR_MIN

#define BNXT_COAL_CMPL_AGGR_TMR_DURING_INT_ENABLE			\
	RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_ENABLES_NUM_CMPL_DMA_AGGR_DURING_INT

struct bnxt_coal_cap {
	u32			cmpl_params;
	u32			nq_params;
	u16			num_cmpl_dma_aggr_max;
	u16			num_cmpl_dma_aggr_during_int_max;
	u16			cmpl_aggr_dma_tmr_max;
	u16			cmpl_aggr_dma_tmr_during_int_max;
	u16			int_lat_tmr_min_max;
	u16			int_lat_tmr_max_max;
	u16			num_cmpl_aggr_int_max;
	u16			timer_units;
};

struct bnxt_coal {
	u16			coal_ticks;
	u16			coal_ticks_irq;
	u16			coal_bufs;
	u16			coal_bufs_irq;
			/* RING_IDLE enabled when coal ticks < idle_thresh  */
	u16			idle_thresh;
	u8			bufs_per_record;
	u8			budget;
};

struct bnxt_tpa_info {
	void			*data;
	u8			*data_ptr;
	dma_addr_t		mapping;
	u16			len;
	unsigned short		gso_type;
	u32			flags2;
	u32			metadata;
	enum pkt_hash_types	hash_type;
	u32			rss_hash;
	u32			hdr_info;

#define BNXT_TPA_L4_SIZE(hdr_info)	\
	(((hdr_info) & 0xf8000000) ? ((hdr_info) >> 27) : 32)

#define BNXT_TPA_INNER_L3_OFF(hdr_info)	\
	(((hdr_info) >> 18) & 0x1ff)

#define BNXT_TPA_INNER_L2_OFF(hdr_info)	\
	(((hdr_info) >> 9) & 0x1ff)

#define BNXT_TPA_OUTER_L3_OFF(hdr_info)	\
	((hdr_info) & 0x1ff)

	u16			cfa_code; /* cfa_code in TPA start compl */
};

struct bnxt_rx_ring_info {
	struct bnxt_napi	*bnapi;
	u16			rx_prod;
	u16			rx_agg_prod;
	u16			rx_sw_agg_prod;
	u16			rx_next_cons;
	struct bnxt_db_info	rx_db;
	struct bnxt_db_info	rx_agg_db;

	struct bpf_prog		*xdp_prog;

	struct rx_bd		*rx_desc_ring[MAX_RX_PAGES];
	struct bnxt_sw_rx_bd	*rx_buf_ring;

	struct rx_bd		*rx_agg_desc_ring[MAX_RX_AGG_PAGES];
	struct bnxt_sw_rx_agg_bd	*rx_agg_ring;

	unsigned long		*rx_agg_bmap;
	u16			rx_agg_bmap_size;

	struct page		*rx_page;
	unsigned int		rx_page_offset;

	dma_addr_t		rx_desc_mapping[MAX_RX_PAGES];
	dma_addr_t		rx_agg_desc_mapping[MAX_RX_AGG_PAGES];

	struct bnxt_tpa_info	*rx_tpa;

	struct bnxt_ring_struct	rx_ring_struct;
	struct bnxt_ring_struct	rx_agg_ring_struct;
	struct xdp_rxq_info	xdp_rxq;
};

struct bnxt_cp_ring_info {
	struct bnxt_napi	*bnapi;
	u32			cp_raw_cons;
	struct bnxt_db_info	cp_db;

	u8			had_work_done:1;
	u8			has_more_work:1;

	u32			last_cp_raw_cons;

	struct bnxt_coal	rx_ring_coal;
	u64			rx_packets;
	u64			rx_bytes;
	u64			event_ctr;

	struct net_dim		dim;

	union {
		struct tx_cmp	*cp_desc_ring[MAX_CP_PAGES];
		struct nqe_cn	*nq_desc_ring[MAX_CP_PAGES];
	};

	dma_addr_t		cp_desc_mapping[MAX_CP_PAGES];

	struct ctx_hw_stats	*hw_stats;
	dma_addr_t		hw_stats_map;
	u32			hw_stats_ctx_id;
	u64			rx_l4_csum_errors;
	u64			missed_irqs;

	struct bnxt_ring_struct	cp_ring_struct;

	struct bnxt_cp_ring_info *cp_ring_arr[2];
#define BNXT_RX_HDL	0
#define BNXT_TX_HDL	1
};

struct bnxt_napi {
	struct napi_struct	napi;
	struct bnxt		*bp;

	int			index;
	struct bnxt_cp_ring_info	cp_ring;
	struct bnxt_rx_ring_info	*rx_ring;
	struct bnxt_tx_ring_info	*tx_ring;

	void			(*tx_int)(struct bnxt *, struct bnxt_napi *,
					  int);
	int			tx_pkts;
	u8			events;

	u32			flags;
#define BNXT_NAPI_FLAG_XDP	0x1

	bool			in_reset;
};

struct bnxt_irq {
	irq_handler_t	handler;
	unsigned int	vector;
	u8		requested:1;
	u8		have_cpumask:1;
	char		name[IFNAMSIZ + 2];
	cpumask_var_t	cpu_mask;
};

#define HWRM_RING_ALLOC_TX	0x1
#define HWRM_RING_ALLOC_RX	0x2
#define HWRM_RING_ALLOC_AGG	0x4
#define HWRM_RING_ALLOC_CMPL	0x8
#define HWRM_RING_ALLOC_NQ	0x10

#define INVALID_STATS_CTX_ID	-1

struct bnxt_ring_grp_info {
	u16	fw_stats_ctx;
	u16	fw_grp_id;
	u16	rx_fw_ring_id;
	u16	agg_fw_ring_id;
	u16	cp_fw_ring_id;
};

struct bnxt_vnic_info {
	u16		fw_vnic_id; /* returned by Chimp during alloc */
#define BNXT_MAX_CTX_PER_VNIC	8
	u16		fw_rss_cos_lb_ctx[BNXT_MAX_CTX_PER_VNIC];
	u16		fw_l2_ctx_id;
#define BNXT_MAX_UC_ADDRS	4
	__le64		fw_l2_filter_id[BNXT_MAX_UC_ADDRS];
				/* index 0 always dev_addr */
	u16		uc_filter_count;
	u8		*uc_list;

	u16		*fw_grp_ids;
	dma_addr_t	rss_table_dma_addr;
	__le16		*rss_table;
	dma_addr_t	rss_hash_key_dma_addr;
	u64		*rss_hash_key;
	u32		rx_mask;

	u8		*mc_list;
	int		mc_list_size;
	int		mc_list_count;
	dma_addr_t	mc_list_mapping;
#define BNXT_MAX_MC_ADDRS	16

	u32		flags;
#define BNXT_VNIC_RSS_FLAG	1
#define BNXT_VNIC_RFS_FLAG	2
#define BNXT_VNIC_MCAST_FLAG	4
#define BNXT_VNIC_UCAST_FLAG	8
#define BNXT_VNIC_RFS_NEW_RSS_FLAG	0x10
};

struct bnxt_hw_resc {
	u16	min_rsscos_ctxs;
	u16	max_rsscos_ctxs;
	u16	min_cp_rings;
	u16	max_cp_rings;
	u16	resv_cp_rings;
	u16	min_tx_rings;
	u16	max_tx_rings;
	u16	resv_tx_rings;
	u16	max_tx_sch_inputs;
	u16	min_rx_rings;
	u16	max_rx_rings;
	u16	resv_rx_rings;
	u16	min_hw_ring_grps;
	u16	max_hw_ring_grps;
	u16	resv_hw_ring_grps;
	u16	min_l2_ctxs;
	u16	max_l2_ctxs;
	u16	min_vnics;
	u16	max_vnics;
	u16	resv_vnics;
	u16	min_stat_ctxs;
	u16	max_stat_ctxs;
	u16	resv_stat_ctxs;
	u16	max_nqs;
	u16	max_irqs;
	u16	resv_irqs;
};

#if defined(CONFIG_BNXT_SRIOV)
struct bnxt_vf_info {
	u16	fw_fid;
	u8	mac_addr[ETH_ALEN];	/* PF assigned MAC Address */
	u8	vf_mac_addr[ETH_ALEN];	/* VF assigned MAC address, only
					 * stored by PF.
					 */
	u16	vlan;
	u32	flags;
#define BNXT_VF_QOS		0x1
#define BNXT_VF_SPOOFCHK	0x2
#define BNXT_VF_LINK_FORCED	0x4
#define BNXT_VF_LINK_UP		0x8
#define BNXT_VF_TRUST		0x10
	u32	func_flags; /* func cfg flags */
	u32	min_tx_rate;
	u32	max_tx_rate;
	void	*hwrm_cmd_req_addr;
	dma_addr_t	hwrm_cmd_req_dma_addr;
};
#endif

struct bnxt_pf_info {
#define BNXT_FIRST_PF_FID	1
#define BNXT_FIRST_VF_FID	128
	u16	fw_fid;
	u16	port_id;
	u8	mac_addr[ETH_ALEN];
	u32	first_vf_id;
	u16	active_vfs;
	u16	max_vfs;
	u32	max_encap_records;
	u32	max_decap_records;
	u32	max_tx_em_flows;
	u32	max_tx_wm_flows;
	u32	max_rx_em_flows;
	u32	max_rx_wm_flows;
	unsigned long	*vf_event_bmap;
	u16	hwrm_cmd_req_pages;
	u8	vf_resv_strategy;
#define BNXT_VF_RESV_STRATEGY_MAXIMAL	0
#define BNXT_VF_RESV_STRATEGY_MINIMAL	1
#define BNXT_VF_RESV_STRATEGY_MINIMAL_STATIC	2
	void			*hwrm_cmd_req_addr[4];
	dma_addr_t		hwrm_cmd_req_dma_addr[4];
	struct bnxt_vf_info	*vf;
};

struct bnxt_ntuple_filter {
	struct hlist_node	hash;
	u8			dst_mac_addr[ETH_ALEN];
	u8			src_mac_addr[ETH_ALEN];
	struct flow_keys	fkeys;
	__le64			filter_id;
	u16			sw_id;
	u8			l2_fltr_idx;
	u16			rxq;
	u32			flow_id;
	unsigned long		state;
#define BNXT_FLTR_VALID		0
#define BNXT_FLTR_UPDATE	1
};

struct bnxt_link_info {
	u8			phy_type;
	u8			media_type;
	u8			transceiver;
	u8			phy_addr;
	u8			phy_link_status;
#define BNXT_LINK_NO_LINK	PORT_PHY_QCFG_RESP_LINK_NO_LINK
#define BNXT_LINK_SIGNAL	PORT_PHY_QCFG_RESP_LINK_SIGNAL
#define BNXT_LINK_LINK		PORT_PHY_QCFG_RESP_LINK_LINK
	u8			wire_speed;
	u8			loop_back;
	u8			link_up;
	u8			duplex;
#define BNXT_LINK_DUPLEX_HALF	PORT_PHY_QCFG_RESP_DUPLEX_STATE_HALF
#define BNXT_LINK_DUPLEX_FULL	PORT_PHY_QCFG_RESP_DUPLEX_STATE_FULL
	u8			pause;
#define BNXT_LINK_PAUSE_TX	PORT_PHY_QCFG_RESP_PAUSE_TX
#define BNXT_LINK_PAUSE_RX	PORT_PHY_QCFG_RESP_PAUSE_RX
#define BNXT_LINK_PAUSE_BOTH	(PORT_PHY_QCFG_RESP_PAUSE_RX | \
				 PORT_PHY_QCFG_RESP_PAUSE_TX)
	u8			lp_pause;
	u8			auto_pause_setting;
	u8			force_pause_setting;
	u8			duplex_setting;
	u8			auto_mode;
#define BNXT_AUTO_MODE(mode)	((mode) > BNXT_LINK_AUTO_NONE && \
				 (mode) <= BNXT_LINK_AUTO_MSK)
#define BNXT_LINK_AUTO_NONE     PORT_PHY_QCFG_RESP_AUTO_MODE_NONE
#define BNXT_LINK_AUTO_ALLSPDS	PORT_PHY_QCFG_RESP_AUTO_MODE_ALL_SPEEDS
#define BNXT_LINK_AUTO_ONESPD	PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_SPEED
#define BNXT_LINK_AUTO_ONEORBELOW PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_OR_BELOW
#define BNXT_LINK_AUTO_MSK	PORT_PHY_QCFG_RESP_AUTO_MODE_SPEED_MASK
#define PHY_VER_LEN		3
	u8			phy_ver[PHY_VER_LEN];
	u16			link_speed;
#define BNXT_LINK_SPEED_100MB	PORT_PHY_QCFG_RESP_LINK_SPEED_100MB
#define BNXT_LINK_SPEED_1GB	PORT_PHY_QCFG_RESP_LINK_SPEED_1GB
#define BNXT_LINK_SPEED_2GB	PORT_PHY_QCFG_RESP_LINK_SPEED_2GB
#define BNXT_LINK_SPEED_2_5GB	PORT_PHY_QCFG_RESP_LINK_SPEED_2_5GB
#define BNXT_LINK_SPEED_10GB	PORT_PHY_QCFG_RESP_LINK_SPEED_10GB
#define BNXT_LINK_SPEED_20GB	PORT_PHY_QCFG_RESP_LINK_SPEED_20GB
#define BNXT_LINK_SPEED_25GB	PORT_PHY_QCFG_RESP_LINK_SPEED_25GB
#define BNXT_LINK_SPEED_40GB	PORT_PHY_QCFG_RESP_LINK_SPEED_40GB
#define BNXT_LINK_SPEED_50GB	PORT_PHY_QCFG_RESP_LINK_SPEED_50GB
#define BNXT_LINK_SPEED_100GB	PORT_PHY_QCFG_RESP_LINK_SPEED_100GB
	u16			support_speeds;
	u16			auto_link_speeds;	/* fw adv setting */
#define BNXT_LINK_SPEED_MSK_100MB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100MB
#define BNXT_LINK_SPEED_MSK_1GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_1GB
#define BNXT_LINK_SPEED_MSK_2GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2GB
#define BNXT_LINK_SPEED_MSK_10GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_10GB
#define BNXT_LINK_SPEED_MSK_2_5GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2_5GB
#define BNXT_LINK_SPEED_MSK_20GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_20GB
#define BNXT_LINK_SPEED_MSK_25GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_25GB
#define BNXT_LINK_SPEED_MSK_40GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_40GB
#define BNXT_LINK_SPEED_MSK_50GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_50GB
#define BNXT_LINK_SPEED_MSK_100GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100GB
	u16			support_auto_speeds;
	u16			lp_auto_link_speeds;
	u16			force_link_speed;
	u32			preemphasis;
	u8			module_status;
	u16			fec_cfg;
#define BNXT_FEC_AUTONEG	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_AUTONEG_ENABLED
#define BNXT_FEC_ENC_BASE_R	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE74_ENABLED
#define BNXT_FEC_ENC_RS		PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE91_ENABLED

	/* copy of requested setting from ethtool cmd */
	u8			autoneg;
#define BNXT_AUTONEG_SPEED		1
#define BNXT_AUTONEG_FLOW_CTRL		2
	u8			req_duplex;
	u8			req_flow_ctrl;
	u16			req_link_speed;
	u16			advertising;	/* user adv setting */
	bool			force_link_chng;

	bool			phy_retry;
	unsigned long		phy_retry_expires;

	/* a copy of phy_qcfg output used to report link
	 * info to VF
	 */
	struct hwrm_port_phy_qcfg_output phy_qcfg_resp;
};

#define BNXT_MAX_QUEUE	8

struct bnxt_queue_info {
	u8	queue_id;
	u8	queue_profile;
};

#define BNXT_MAX_LED			4

struct bnxt_led_info {
	u8	led_id;
	u8	led_type;
	u8	led_group_id;
	u8	unused;
	__le16	led_state_caps;
#define BNXT_LED_ALT_BLINK_CAP(x)	((x) &	\
	cpu_to_le16(PORT_LED_QCAPS_RESP_LED0_STATE_CAPS_BLINK_ALT_SUPPORTED))

	__le16	led_color_caps;
};

#define BNXT_MAX_TEST	8

struct bnxt_test_info {
	u8 offline_mask;
	u8 flags;
#define BNXT_TEST_FL_EXT_LPBK	0x1
	u16 timeout;
	char string[BNXT_MAX_TEST][ETH_GSTRING_LEN];
};

#define BNXT_GRCPF_REG_CHIMP_COMM		0x0
#define BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER	0x100
#define BNXT_GRCPF_REG_WINDOW_BASE_OUT		0x400
#define BNXT_CAG_REG_LEGACY_INT_STATUS		0x4014
#define BNXT_CAG_REG_BASE			0x300000

#define BNXT_GRCPF_REG_KONG_COMM		0xA00
#define BNXT_GRCPF_REG_KONG_COMM_TRIGGER	0xB00

struct bnxt_tc_flow_stats {
	u64		packets;
	u64		bytes;
};

struct bnxt_tc_info {
	bool				enabled;

	/* hash table to store TC offloaded flows */
	struct rhashtable		flow_table;
	struct rhashtable_params	flow_ht_params;

	/* hash table to store L2 keys of TC flows */
	struct rhashtable		l2_table;
	struct rhashtable_params	l2_ht_params;
	/* hash table to store L2 keys for TC tunnel decap */
	struct rhashtable		decap_l2_table;
	struct rhashtable_params	decap_l2_ht_params;
	/* hash table to store tunnel decap entries */
	struct rhashtable		decap_table;
	struct rhashtable_params	decap_ht_params;
	/* hash table to store tunnel encap entries */
	struct rhashtable		encap_table;
	struct rhashtable_params	encap_ht_params;

	/* lock to atomically add/del an l2 node when a flow is
	 * added or deleted.
	 */
	struct mutex			lock;

	/* Fields used for batching stats query */
	struct rhashtable_iter		iter;
#define BNXT_FLOW_STATS_BATCH_MAX	10
	struct bnxt_tc_stats_batch {
		void			  *flow_node;
		struct bnxt_tc_flow_stats hw_stats;
	} stats_batch[BNXT_FLOW_STATS_BATCH_MAX];

	/* Stat counter mask (width) */
	u64				bytes_mask;
	u64				packets_mask;
};

struct bnxt_vf_rep_stats {
	u64			packets;
	u64			bytes;
	u64			dropped;
};

struct bnxt_vf_rep {
	struct bnxt			*bp;
	struct net_device		*dev;
	struct metadata_dst		*dst;
	u16				vf_idx;
	u16				tx_cfa_action;
	u16				rx_cfa_code;

	struct bnxt_vf_rep_stats	rx_stats;
	struct bnxt_vf_rep_stats	tx_stats;
};

#define PTU_PTE_VALID             0x1UL
#define PTU_PTE_LAST              0x2UL
#define PTU_PTE_NEXT_TO_LAST      0x4UL

#define MAX_CTX_PAGES	(BNXT_PAGE_SIZE / 8)
#define MAX_CTX_TOTAL_PAGES	(MAX_CTX_PAGES * MAX_CTX_PAGES)

struct bnxt_ctx_pg_info {
	u32		entries;
	u32		nr_pages;
	void		*ctx_pg_arr[MAX_CTX_PAGES];
	dma_addr_t	ctx_dma_arr[MAX_CTX_PAGES];
	struct bnxt_ring_mem_info ring_mem;
	struct bnxt_ctx_pg_info **ctx_pg_tbl;
};

struct bnxt_ctx_mem_info {
	u32	qp_max_entries;
	u16	qp_min_qp1_entries;
	u16	qp_max_l2_entries;
	u16	qp_entry_size;
	u16	srq_max_l2_entries;
	u32	srq_max_entries;
	u16	srq_entry_size;
	u16	cq_max_l2_entries;
	u32	cq_max_entries;
	u16	cq_entry_size;
	u16	vnic_max_vnic_entries;
	u16	vnic_max_ring_table_entries;
	u16	vnic_entry_size;
	u32	stat_max_entries;
	u16	stat_entry_size;
	u16	tqm_entry_size;
	u32	tqm_min_entries_per_ring;
	u32	tqm_max_entries_per_ring;
	u32	mrav_max_entries;
	u16	mrav_entry_size;
	u16	tim_entry_size;
	u32	tim_max_entries;
	u8	tqm_entries_multiple;

	u32	flags;
	#define BNXT_CTX_FLAG_INITED	0x01

	struct bnxt_ctx_pg_info qp_mem;
	struct bnxt_ctx_pg_info srq_mem;
	struct bnxt_ctx_pg_info cq_mem;
	struct bnxt_ctx_pg_info vnic_mem;
	struct bnxt_ctx_pg_info stat_mem;
	struct bnxt_ctx_pg_info mrav_mem;
	struct bnxt_ctx_pg_info tim_mem;
	struct bnxt_ctx_pg_info *tqm_mem[9];
};

struct bnxt {
	void __iomem		*bar0;
	void __iomem		*bar1;
	void __iomem		*bar2;

	u32			reg_base;
	u16			chip_num;
#define CHIP_NUM_57301		0x16c8
#define CHIP_NUM_57302		0x16c9
#define CHIP_NUM_57304		0x16ca
#define CHIP_NUM_58700		0x16cd
#define CHIP_NUM_57402		0x16d0
#define CHIP_NUM_57404		0x16d1
#define CHIP_NUM_57406		0x16d2
#define CHIP_NUM_57407		0x16d5

#define CHIP_NUM_57311		0x16ce
#define CHIP_NUM_57312		0x16cf
#define CHIP_NUM_57314		0x16df
#define CHIP_NUM_57317		0x16e0
#define CHIP_NUM_57412		0x16d6
#define CHIP_NUM_57414		0x16d7
#define CHIP_NUM_57416		0x16d8
#define CHIP_NUM_57417		0x16d9
#define CHIP_NUM_57412L		0x16da
#define CHIP_NUM_57414L		0x16db

#define CHIP_NUM_5745X		0xd730

#define CHIP_NUM_57500		0x1750

#define CHIP_NUM_58802		0xd802
#define CHIP_NUM_58804		0xd804
#define CHIP_NUM_58808		0xd808

#define BNXT_CHIP_NUM_5730X(chip_num)		\
	((chip_num) >= CHIP_NUM_57301 &&	\
	 (chip_num) <= CHIP_NUM_57304)

#define BNXT_CHIP_NUM_5740X(chip_num)		\
	(((chip_num) >= CHIP_NUM_57402 &&	\
	  (chip_num) <= CHIP_NUM_57406) ||	\
	 (chip_num) == CHIP_NUM_57407)

#define BNXT_CHIP_NUM_5731X(chip_num)		\
	((chip_num) == CHIP_NUM_57311 ||	\
	 (chip_num) == CHIP_NUM_57312 ||	\
	 (chip_num) == CHIP_NUM_57314 ||	\
	 (chip_num) == CHIP_NUM_57317)

#define BNXT_CHIP_NUM_5741X(chip_num)		\
	((chip_num) >= CHIP_NUM_57412 &&	\
	 (chip_num) <= CHIP_NUM_57414L)

#define BNXT_CHIP_NUM_58700(chip_num)		\
	 ((chip_num) == CHIP_NUM_58700)

#define BNXT_CHIP_NUM_5745X(chip_num)		\
	 ((chip_num) == CHIP_NUM_5745X)

#define BNXT_CHIP_NUM_57X0X(chip_num)		\
	(BNXT_CHIP_NUM_5730X(chip_num) || BNXT_CHIP_NUM_5740X(chip_num))

#define BNXT_CHIP_NUM_57X1X(chip_num)		\
	(BNXT_CHIP_NUM_5731X(chip_num) || BNXT_CHIP_NUM_5741X(chip_num))

#define BNXT_CHIP_NUM_588XX(chip_num)		\
	((chip_num) == CHIP_NUM_58802 ||	\
	 (chip_num) == CHIP_NUM_58804 ||        \
	 (chip_num) == CHIP_NUM_58808)

	struct net_device	*dev;
	struct pci_dev		*pdev;

	atomic_t		intr_sem;

	u32			flags;
	#define BNXT_FLAG_CHIP_P5	0x1
	#define BNXT_FLAG_VF		0x2
	#define BNXT_FLAG_LRO		0x4
#ifdef CONFIG_INET
	#define BNXT_FLAG_GRO		0x8
#else
	/* Cannot support hardware GRO if CONFIG_INET is not set */
	#define BNXT_FLAG_GRO		0x0
#endif
	#define BNXT_FLAG_TPA		(BNXT_FLAG_LRO | BNXT_FLAG_GRO)
	#define BNXT_FLAG_JUMBO		0x10
	#define BNXT_FLAG_STRIP_VLAN	0x20
	#define BNXT_FLAG_AGG_RINGS	(BNXT_FLAG_JUMBO | BNXT_FLAG_GRO | \
					 BNXT_FLAG_LRO)
	#define BNXT_FLAG_USING_MSIX	0x40
	#define BNXT_FLAG_MSIX_CAP	0x80
	#define BNXT_FLAG_RFS		0x100
	#define BNXT_FLAG_SHARED_RINGS	0x200
	#define BNXT_FLAG_PORT_STATS	0x400
	#define BNXT_FLAG_UDP_RSS_CAP	0x800
	#define BNXT_FLAG_EEE_CAP	0x1000
	#define BNXT_FLAG_NEW_RSS_CAP	0x2000
	#define BNXT_FLAG_WOL_CAP	0x4000
	#define BNXT_FLAG_ROCEV1_CAP	0x8000
	#define BNXT_FLAG_ROCEV2_CAP	0x10000
	#define BNXT_FLAG_ROCE_CAP	(BNXT_FLAG_ROCEV1_CAP |	\
					 BNXT_FLAG_ROCEV2_CAP)
	#define BNXT_FLAG_NO_AGG_RINGS	0x20000
	#define BNXT_FLAG_RX_PAGE_MODE	0x40000
	#define BNXT_FLAG_MULTI_HOST	0x100000
	#define BNXT_FLAG_DOUBLE_DB	0x400000
	#define BNXT_FLAG_CHIP_NITRO_A0	0x1000000
	#define BNXT_FLAG_DIM		0x2000000
	#define BNXT_FLAG_ROCE_MIRROR_CAP	0x4000000
	#define BNXT_FLAG_PORT_STATS_EXT	0x10000000

	#define BNXT_FLAG_ALL_CONFIG_FEATS (BNXT_FLAG_TPA |		\
					    BNXT_FLAG_RFS |		\
					    BNXT_FLAG_STRIP_VLAN)

#define BNXT_PF(bp)		(!((bp)->flags & BNXT_FLAG_VF))
#define BNXT_VF(bp)		((bp)->flags & BNXT_FLAG_VF)
#define BNXT_NPAR(bp)		((bp)->port_partition_type)
#define BNXT_MH(bp)		((bp)->flags & BNXT_FLAG_MULTI_HOST)
#define BNXT_SINGLE_PF(bp)	(BNXT_PF(bp) && !BNXT_NPAR(bp) && !BNXT_MH(bp))
#define BNXT_CHIP_TYPE_NITRO_A0(bp) ((bp)->flags & BNXT_FLAG_CHIP_NITRO_A0)
#define BNXT_RX_PAGE_MODE(bp)	((bp)->flags & BNXT_FLAG_RX_PAGE_MODE)
#define BNXT_SUPPORTS_TPA(bp)	(!BNXT_CHIP_TYPE_NITRO_A0(bp) &&	\
				 !(bp->flags & BNXT_FLAG_CHIP_P5))

/* Chip class phase 5 */
#define BNXT_CHIP_P5(bp)			\
	((bp)->chip_num == CHIP_NUM_57500)

/* Chip class phase 4.x */
#define BNXT_CHIP_P4(bp)			\
	(BNXT_CHIP_NUM_57X1X((bp)->chip_num) ||	\
	 BNXT_CHIP_NUM_5745X((bp)->chip_num) ||	\
	 BNXT_CHIP_NUM_588XX((bp)->chip_num) ||	\
	 (BNXT_CHIP_NUM_58700((bp)->chip_num) &&	\
	  !BNXT_CHIP_TYPE_NITRO_A0(bp)))

#define BNXT_CHIP_P4_PLUS(bp)			\
	(BNXT_CHIP_P4(bp) || BNXT_CHIP_P5(bp))

	struct bnxt_en_dev	*edev;
	struct bnxt_en_dev *	(*ulp_probe)(struct net_device *);

	struct bnxt_napi	**bnapi;

	struct bnxt_rx_ring_info	*rx_ring;
	struct bnxt_tx_ring_info	*tx_ring;
	u16			*tx_ring_map;

	struct sk_buff *	(*gro_func)(struct bnxt_tpa_info *, int, int,
					    struct sk_buff *);

	struct sk_buff *	(*rx_skb_func)(struct bnxt *,
					       struct bnxt_rx_ring_info *,
					       u16, void *, u8 *, dma_addr_t,
					       unsigned int);

	u32			rx_buf_size;
	u32			rx_buf_use_size;	/* useable size */
	u16			rx_offset;
	u16			rx_dma_offset;
	enum dma_data_direction	rx_dir;
	u32			rx_ring_size;
	u32			rx_agg_ring_size;
	u32			rx_copy_thresh;
	u32			rx_ring_mask;
	u32			rx_agg_ring_mask;
	int			rx_nr_pages;
	int			rx_agg_nr_pages;
	int			rx_nr_rings;
	int			rsscos_nr_ctxs;

	u32			tx_ring_size;
	u32			tx_ring_mask;
	int			tx_nr_pages;
	int			tx_nr_rings;
	int			tx_nr_rings_per_tc;
	int			tx_nr_rings_xdp;

	int			tx_wake_thresh;
	int			tx_push_thresh;
	int			tx_push_size;

	u32			cp_ring_size;
	u32			cp_ring_mask;
	u32			cp_bit;
	int			cp_nr_pages;
	int			cp_nr_rings;

	/* grp_info indexed by completion ring index */
	struct bnxt_ring_grp_info	*grp_info;
	struct bnxt_vnic_info	*vnic_info;
	int			nr_vnics;
	u32			rss_hash_cfg;

	u16			max_mtu;
	u8			max_tc;
	u8			max_lltc;	/* lossless TCs */
	struct bnxt_queue_info	q_info[BNXT_MAX_QUEUE];
	u8			tc_to_qidx[BNXT_MAX_QUEUE];
	u8			q_ids[BNXT_MAX_QUEUE];
	u8			max_q;

	unsigned int		current_interval;
#define BNXT_TIMER_INTERVAL	HZ

	struct timer_list	timer;

	unsigned long		state;
#define BNXT_STATE_OPEN		0
#define BNXT_STATE_IN_SP_TASK	1
#define BNXT_STATE_READ_STATS	2

	struct bnxt_irq	*irq_tbl;
	int			total_irqs;
	u8			mac_addr[ETH_ALEN];

#ifdef CONFIG_BNXT_DCB
	struct ieee_pfc		*ieee_pfc;
	struct ieee_ets		*ieee_ets;
	u8			dcbx_cap;
	u8			default_pri;
	u8			max_dscp_value;
#endif /* CONFIG_BNXT_DCB */

	u32			msg_enable;

	u32			fw_cap;
	#define BNXT_FW_CAP_SHORT_CMD			0x00000001
	#define BNXT_FW_CAP_LLDP_AGENT			0x00000002
	#define BNXT_FW_CAP_DCBX_AGENT			0x00000004
	#define BNXT_FW_CAP_NEW_RM			0x00000008
	#define BNXT_FW_CAP_IF_CHANGE			0x00000010
	#define BNXT_FW_CAP_KONG_MB_CHNL		0x00000080
	#define BNXT_FW_CAP_OVS_64BIT_HANDLE		0x00000400

#define BNXT_NEW_RM(bp)		((bp)->fw_cap & BNXT_FW_CAP_NEW_RM)
	u32			hwrm_spec_code;
	u16			hwrm_cmd_seq;
	u16                     hwrm_cmd_kong_seq;
	u16			hwrm_intr_seq_id;
	void			*hwrm_short_cmd_req_addr;
	dma_addr_t		hwrm_short_cmd_req_dma_addr;
	void			*hwrm_cmd_resp_addr;
	dma_addr_t		hwrm_cmd_resp_dma_addr;
	void			*hwrm_cmd_kong_resp_addr;
	dma_addr_t		hwrm_cmd_kong_resp_dma_addr;

	struct rtnl_link_stats64	net_stats_prev;
	struct rx_port_stats	*hw_rx_port_stats;
	struct tx_port_stats	*hw_tx_port_stats;
	struct rx_port_stats_ext	*hw_rx_port_stats_ext;
	struct tx_port_stats_ext	*hw_tx_port_stats_ext;
	dma_addr_t		hw_rx_port_stats_map;
	dma_addr_t		hw_tx_port_stats_map;
	dma_addr_t		hw_rx_port_stats_ext_map;
	dma_addr_t		hw_tx_port_stats_ext_map;
	int			hw_port_stats_size;
	u16			fw_rx_stats_ext_size;
	u16			fw_tx_stats_ext_size;
	u8			pri2cos[8];
	u8			pri2cos_valid;

	u16			hwrm_max_req_len;
	u16			hwrm_max_ext_req_len;
	int			hwrm_cmd_timeout;
	struct mutex		hwrm_cmd_lock;	/* serialize hwrm messages */
	struct hwrm_ver_get_output	ver_resp;
#define FW_VER_STR_LEN		32
#define BC_HWRM_STR_LEN		21
#define PHY_VER_STR_LEN         (FW_VER_STR_LEN - BC_HWRM_STR_LEN)
	char			fw_ver_str[FW_VER_STR_LEN];
	__be16			vxlan_port;
	u8			vxlan_port_cnt;
	__le16			vxlan_fw_dst_port_id;
	__be16			nge_port;
	u8			nge_port_cnt;
	__le16			nge_fw_dst_port_id;
	u8			port_partition_type;
	u8			port_count;
	u16			br_mode;

	struct bnxt_coal_cap	coal_cap;
	struct bnxt_coal	rx_coal;
	struct bnxt_coal	tx_coal;

	u32			stats_coal_ticks;
#define BNXT_DEF_STATS_COAL_TICKS	 1000000
#define BNXT_MIN_STATS_COAL_TICKS	  250000
#define BNXT_MAX_STATS_COAL_TICKS	 1000000

	struct work_struct	sp_task;
	unsigned long		sp_event;
#define BNXT_RX_MASK_SP_EVENT		0
#define BNXT_RX_NTP_FLTR_SP_EVENT	1
#define BNXT_LINK_CHNG_SP_EVENT		2
#define BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT	3
#define BNXT_VXLAN_ADD_PORT_SP_EVENT	4
#define BNXT_VXLAN_DEL_PORT_SP_EVENT	5
#define BNXT_RESET_TASK_SP_EVENT	6
#define BNXT_RST_RING_SP_EVENT		7
#define BNXT_HWRM_PF_UNLOAD_SP_EVENT	8
#define BNXT_PERIODIC_STATS_SP_EVENT	9
#define BNXT_HWRM_PORT_MODULE_SP_EVENT	10
#define BNXT_RESET_TASK_SILENT_SP_EVENT	11
#define BNXT_GENEVE_ADD_PORT_SP_EVENT	12
#define BNXT_GENEVE_DEL_PORT_SP_EVENT	13
#define BNXT_LINK_SPEED_CHNG_SP_EVENT	14
#define BNXT_FLOW_STATS_SP_EVENT	15
#define BNXT_UPDATE_PHY_SP_EVENT	16
#define BNXT_RING_COAL_NOW_SP_EVENT	17

	struct bnxt_hw_resc	hw_resc;
	struct bnxt_pf_info	pf;
	struct bnxt_ctx_mem_info	*ctx;
#ifdef CONFIG_BNXT_SRIOV
	int			nr_vfs;
	struct bnxt_vf_info	vf;
	wait_queue_head_t	sriov_cfg_wait;
	bool			sriov_cfg;
#define BNXT_SRIOV_CFG_WAIT_TMO	msecs_to_jiffies(10000)

	/* lock to protect VF-rep creation/cleanup via
	 * multiple paths such as ->sriov_configure() and
	 * devlink ->eswitch_mode_set()
	 */
	struct mutex		sriov_lock;
#endif

#if BITS_PER_LONG == 32
	/* ensure atomic 64-bit doorbell writes on 32-bit systems. */
	spinlock_t		db_lock;
#endif

#define BNXT_NTP_FLTR_MAX_FLTR	4096
#define BNXT_NTP_FLTR_HASH_SIZE	512
#define BNXT_NTP_FLTR_HASH_MASK	(BNXT_NTP_FLTR_HASH_SIZE - 1)
	struct hlist_head	ntp_fltr_hash_tbl[BNXT_NTP_FLTR_HASH_SIZE];
	spinlock_t		ntp_fltr_lock;	/* for hash table add, del */

	unsigned long		*ntp_fltr_bmap;
	int			ntp_fltr_count;

	/* To protect link related settings during link changes and
	 * ethtool settings changes.
	 */
	struct mutex		link_lock;
	struct bnxt_link_info	link_info;
	struct ethtool_eee	eee;
	u32			lpi_tmr_lo;
	u32			lpi_tmr_hi;

	u8			num_tests;
	struct bnxt_test_info	*test_info;

	u8			wol_filter_id;
	u8			wol;

	u8			num_leds;
	struct bnxt_led_info	leds[BNXT_MAX_LED];

	struct bpf_prog		*xdp_prog;

	/* devlink interface and vf-rep structs */
	struct devlink		*dl;
	enum devlink_eswitch_mode eswitch_mode;
	struct bnxt_vf_rep	**vf_reps; /* array of vf-rep ptrs */
	u16			*cfa_code_map; /* cfa_code -> vf_idx map */
	u8			switch_id[8];
	struct bnxt_tc_info	*tc_info;
	struct dentry		*debugfs_pdev;
	struct dentry		*debugfs_dim;
	struct device		*hwmon_dev;
};

#define BNXT_RX_STATS_OFFSET(counter)			\
	(offsetof(struct rx_port_stats, counter) / 8)

#define BNXT_TX_STATS_OFFSET(counter)			\
	((offsetof(struct tx_port_stats, counter) +	\
	  sizeof(struct rx_port_stats) + 512) / 8)

#define BNXT_RX_STATS_EXT_OFFSET(counter)		\
	(offsetof(struct rx_port_stats_ext, counter) / 8)

#define BNXT_TX_STATS_EXT_OFFSET(counter)		\
	(offsetof(struct tx_port_stats_ext, counter) / 8)

#define I2C_DEV_ADDR_A0				0xa0
#define I2C_DEV_ADDR_A2				0xa2
#define SFF_DIAG_SUPPORT_OFFSET			0x5c
#define SFF_MODULE_ID_SFP			0x3
#define SFF_MODULE_ID_QSFP			0xc
#define SFF_MODULE_ID_QSFP_PLUS			0xd
#define SFF_MODULE_ID_QSFP28			0x11
#define BNXT_MAX_PHY_I2C_RESP_SIZE		64

static inline u32 bnxt_tx_avail(struct bnxt *bp, struct bnxt_tx_ring_info *txr)
{
	/* Tell compiler to fetch tx indices from memory. */
	barrier();

	return bp->tx_ring_size -
		((txr->tx_prod - txr->tx_cons) & bp->tx_ring_mask);
}

#if BITS_PER_LONG == 32
#define writeq(val64, db)			\
do {						\
	spin_lock(&bp->db_lock);		\
	writel((val64) & 0xffffffff, db);	\
	writel((val64) >> 32, (db) + 4);	\
	spin_unlock(&bp->db_lock);		\
} while (0)

#define writeq_relaxed writeq
#endif

/* For TX and RX ring doorbells with no ordering guarantee*/
static inline void bnxt_db_write_relaxed(struct bnxt *bp,
					 struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		writeq_relaxed(db->db_key64 | idx, db->doorbell);
	} else {
		u32 db_val = db->db_key32 | idx;

		writel_relaxed(db_val, db->doorbell);
		if (bp->flags & BNXT_FLAG_DOUBLE_DB)
			writel_relaxed(db_val, db->doorbell);
	}
}

/* For TX and RX ring doorbells */
static inline void bnxt_db_write(struct bnxt *bp, struct bnxt_db_info *db,
				 u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		writeq(db->db_key64 | idx, db->doorbell);
	} else {
		u32 db_val = db->db_key32 | idx;

		writel(db_val, db->doorbell);
		if (bp->flags & BNXT_FLAG_DOUBLE_DB)
			writel(db_val, db->doorbell);
	}
}

static inline bool bnxt_cfa_hwrm_message(u16 req_type)
{
	switch (req_type) {
	case HWRM_CFA_ENCAP_RECORD_ALLOC:
	case HWRM_CFA_ENCAP_RECORD_FREE:
	case HWRM_CFA_DECAP_FILTER_ALLOC:
	case HWRM_CFA_DECAP_FILTER_FREE:
	case HWRM_CFA_NTUPLE_FILTER_ALLOC:
	case HWRM_CFA_NTUPLE_FILTER_FREE:
	case HWRM_CFA_NTUPLE_FILTER_CFG:
	case HWRM_CFA_EM_FLOW_ALLOC:
	case HWRM_CFA_EM_FLOW_FREE:
	case HWRM_CFA_EM_FLOW_CFG:
	case HWRM_CFA_FLOW_ALLOC:
	case HWRM_CFA_FLOW_FREE:
	case HWRM_CFA_FLOW_INFO:
	case HWRM_CFA_FLOW_FLUSH:
	case HWRM_CFA_FLOW_STATS:
	case HWRM_CFA_METER_PROFILE_ALLOC:
	case HWRM_CFA_METER_PROFILE_FREE:
	case HWRM_CFA_METER_PROFILE_CFG:
	case HWRM_CFA_METER_INSTANCE_ALLOC:
	case HWRM_CFA_METER_INSTANCE_FREE:
		return true;
	default:
		return false;
	}
}

static inline bool bnxt_kong_hwrm_message(struct bnxt *bp, struct input *req)
{
	return (bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL &&
		bnxt_cfa_hwrm_message(le16_to_cpu(req->req_type)));
}

static inline bool bnxt_hwrm_kong_chnl(struct bnxt *bp, struct input *req)
{
	return (bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL &&
		req->resp_addr == cpu_to_le64(bp->hwrm_cmd_kong_resp_dma_addr));
}

static inline void *bnxt_get_hwrm_resp_addr(struct bnxt *bp, void *req)
{
	if (bnxt_hwrm_kong_chnl(bp, (struct input *)req))
		return bp->hwrm_cmd_kong_resp_addr;
	else
		return bp->hwrm_cmd_resp_addr;
}

static inline u16 bnxt_get_hwrm_seq_id(struct bnxt *bp, u16 dst)
{
	u16 seq_id;

	if (dst == BNXT_HWRM_CHNL_CHIMP)
		seq_id = bp->hwrm_cmd_seq++;
	else
		seq_id = bp->hwrm_cmd_kong_seq++;
	return seq_id;
}

extern const u16 bnxt_lhint_arr[];

int bnxt_alloc_rx_data(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
		       u16 prod, gfp_t gfp);
void bnxt_reuse_rx_data(struct bnxt_rx_ring_info *rxr, u16 cons, void *data);
void bnxt_set_tpa_flags(struct bnxt *bp);
void bnxt_set_ring_params(struct bnxt *);
int bnxt_set_rx_skb_mode(struct bnxt *bp, bool page_mode);
void bnxt_hwrm_cmd_hdr_init(struct bnxt *, void *, u16, u16, u16);
int _hwrm_send_message(struct bnxt *, void *, u32, int);
int _hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 len, int timeout);
int hwrm_send_message(struct bnxt *, void *, u32, int);
int hwrm_send_message_silent(struct bnxt *, void *, u32, int);
int bnxt_hwrm_func_rgtr_async_events(struct bnxt *bp, unsigned long *bmap,
				     int bmap_size);
int bnxt_hwrm_vnic_cfg(struct bnxt *bp, u16 vnic_id);
int __bnxt_hwrm_get_tx_rings(struct bnxt *bp, u16 fid, int *tx_rings);
int bnxt_nq_rings_in_use(struct bnxt *bp);
int bnxt_hwrm_set_coal(struct bnxt *);
unsigned int bnxt_get_max_func_stat_ctxs(struct bnxt *bp);
unsigned int bnxt_get_avail_stat_ctxs_for_en(struct bnxt *bp);
unsigned int bnxt_get_max_func_cp_rings(struct bnxt *bp);
unsigned int bnxt_get_avail_cp_rings_for_en(struct bnxt *bp);
int bnxt_get_avail_msix(struct bnxt *bp, int num);
int bnxt_reserve_rings(struct bnxt *bp);
void bnxt_tx_disable(struct bnxt *bp);
void bnxt_tx_enable(struct bnxt *bp);
int bnxt_hwrm_set_pause(struct bnxt *);
int bnxt_hwrm_set_link_setting(struct bnxt *, bool, bool);
int bnxt_hwrm_alloc_wol_fltr(struct bnxt *bp);
int bnxt_hwrm_free_wol_fltr(struct bnxt *bp);
int bnxt_hwrm_func_resc_qcaps(struct bnxt *bp, bool all);
int bnxt_hwrm_fw_set_time(struct bnxt *);
int bnxt_open_nic(struct bnxt *, bool, bool);
int bnxt_half_open_nic(struct bnxt *bp);
void bnxt_half_close_nic(struct bnxt *bp);
int bnxt_close_nic(struct bnxt *, bool, bool);
int bnxt_check_rings(struct bnxt *bp, int tx, int rx, bool sh, int tcs,
		     int tx_xdp);
int bnxt_setup_mq_tc(struct net_device *dev, u8 tc);
int bnxt_get_max_rings(struct bnxt *, int *, int *, bool);
int bnxt_restore_pf_fw_resources(struct bnxt *bp);
int bnxt_port_attr_get(struct bnxt *bp, struct switchdev_attr *attr);
void bnxt_dim_work(struct work_struct *work);
int bnxt_hwrm_set_ring_coal(struct bnxt *bp, struct bnxt_napi *bnapi);

#endif
