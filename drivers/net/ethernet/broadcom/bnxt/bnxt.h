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

/* DO NOT CHANGE DRV_VER_* defines
 * FIXME: Delete them
 */
#define DRV_VER_MAJ	1
#define DRV_VER_MIN	10
#define DRV_VER_UPD	3

#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/rhashtable.h>
#include <linux/crash_dump.h>
#include <linux/auxiliary_bus.h>
#include <net/devlink.h>
#include <net/dst_metadata.h>
#include <net/xdp.h>
#include <linux/dim.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#ifdef CONFIG_TEE_BNXT_FW
#include <linux/firmware/broadcom/tee_bnxt_fw.h>
#endif

#define BNXT_DEFAULT_RX_COPYBREAK 256
#define BNXT_MAX_RX_COPYBREAK 1024

extern struct list_head bnxt_block_cb_list;

struct page_pool;

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

#define TX_OPAQUE_IDX_MASK	0x0000ffff
#define TX_OPAQUE_BDS_MASK	0x00ff0000
#define TX_OPAQUE_BDS_SHIFT	16
#define TX_OPAQUE_RING_MASK	0xff000000
#define TX_OPAQUE_RING_SHIFT	24

#define SET_TX_OPAQUE(bp, txr, idx, bds)				\
	(((txr)->tx_napi_idx << TX_OPAQUE_RING_SHIFT) |			\
	 ((bds) << TX_OPAQUE_BDS_SHIFT) | ((idx) & (bp)->tx_ring_mask))

#define TX_OPAQUE_IDX(opq)	((opq) & TX_OPAQUE_IDX_MASK)
#define TX_OPAQUE_RING(opq)	(((opq) & TX_OPAQUE_RING_MASK) >>	\
				 TX_OPAQUE_RING_SHIFT)
#define TX_OPAQUE_BDS(opq)	(((opq) & TX_OPAQUE_BDS_MASK) >>	\
				 TX_OPAQUE_BDS_SHIFT)
#define TX_OPAQUE_PROD(bp, opq)	((TX_OPAQUE_IDX(opq) + TX_OPAQUE_BDS(opq)) &\
				 (bp)->tx_ring_mask)

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

#define BNXT_TX_PTP_IS_SET(lflags) ((lflags) & cpu_to_le32(TX_BD_FLAGS_STAMP))

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
	 #define CMP_TYPE_TX_L2_COAL_CMP			 2
	 #define CMP_TYPE_TX_L2_PKT_TS_CMP			 4
	 #define CMP_TYPE_RX_L2_CMP				 17
	 #define CMP_TYPE_RX_AGG_CMP				 18
	 #define CMP_TYPE_RX_L2_TPA_START_CMP			 19
	 #define CMP_TYPE_RX_L2_TPA_END_CMP			 21
	 #define CMP_TYPE_RX_TPA_AGG_CMP			 22
	 #define CMP_TYPE_RX_L2_V3_CMP				 23
	 #define CMP_TYPE_RX_L2_TPA_START_V3_CMP		 25
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

	__le32 sq_cons_idx;
	#define TX_CMP_SQ_CONS_IDX_MASK				0x00ffffff
};

#define TX_CMP_SQ_CONS_IDX(txcmp)					\
	(le32_to_cpu((txcmp)->sq_cons_idx) & TX_CMP_SQ_CONS_IDX_MASK)

struct tx_ts_cmp {
	__le32 tx_ts_cmp_flags_type;
	#define TX_TS_CMP_FLAGS_ERROR				(1 << 6)
	#define TX_TS_CMP_FLAGS_TS_TYPE				(1 << 7)
	 #define TX_TS_CMP_FLAGS_TS_TYPE_PM			 (0 << 7)
	 #define TX_TS_CMP_FLAGS_TS_TYPE_PA			 (1 << 7)
	#define TX_TS_CMP_FLAGS_TS_FALLBACK			(1 << 8)
	#define TX_TS_CMP_TS_SUB_NS				(0xf << 12)
	#define TX_TS_CMP_TS_NS_MID				(0xffff << 16)
	#define TX_TS_CMP_TS_NS_MID_SFT				16
	u32 tx_ts_cmp_opaque;
	__le32 tx_ts_cmp_errors_v;
	#define TX_TS_CMP_V					(1 << 0)
	#define TX_TS_CMP_TS_INVALID_ERR			(1 << 10)
	__le32 tx_ts_cmp_ts_ns_lo;
};

#define BNXT_GET_TX_TS_48B_NS(tscmp)					\
	(le32_to_cpu((tscmp)->tx_ts_cmp_ts_ns_lo) |			\
	 ((u64)(le32_to_cpu((tscmp)->tx_ts_cmp_flags_type) &		\
	  TX_TS_CMP_TS_NS_MID) << TX_TS_CMP_TS_NS_MID_SFT))

#define BNXT_TX_TS_ERR(tscmp)						\
	(((tscmp)->tx_ts_cmp_flags_type & cpu_to_le32(TX_TS_CMP_FLAGS_ERROR)) &&\
	 ((tscmp)->tx_ts_cmp_errors_v & cpu_to_le32(TX_TS_CMP_TS_INVALID_ERR)))

struct rx_cmp {
	__le32 rx_cmp_len_flags_type;
	#define RX_CMP_CMP_TYPE					(0x3f << 0)
	#define RX_CMP_FLAGS_ERROR				(1 << 6)
	#define RX_CMP_FLAGS_PLACEMENT				(7 << 7)
	#define RX_CMP_FLAGS_RSS_VALID				(1 << 10)
	#define RX_CMP_FLAGS_PKT_METADATA_PRESENT		(1 << 11)
	 #define RX_CMP_FLAGS_ITYPES_SHIFT			 12
	 #define RX_CMP_FLAGS_ITYPES_MASK			 0xf000
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
	#define RX_CMP_V3_RSS_EXT_OP_LEGACY			(0xf << 12)
	 #define RX_CMP_V3_RSS_EXT_OP_LEGACY_SHIFT		 12
	#define RX_CMP_V3_RSS_EXT_OP_NEW			(0xf << 8)
	 #define RX_CMP_V3_RSS_EXT_OP_NEW_SHIFT			 8
	#define RX_CMP_PAYLOAD_OFFSET				(0xff << 16)
	 #define RX_CMP_PAYLOAD_OFFSET_SHIFT			 16
	#define RX_CMP_SUB_NS_TS				(0xf << 16)
	 #define RX_CMP_SUB_NS_TS_SHIFT				 16
	#define RX_CMP_METADATA1				(0xf << 28)
	 #define RX_CMP_METADATA1_SHIFT				 28
	#define RX_CMP_METADATA1_TPID_SEL			(0x7 << 28)
	#define RX_CMP_METADATA1_TPID_8021Q			(0x1 << 28)
	#define RX_CMP_METADATA1_TPID_8021AD			(0x0 << 28)
	#define RX_CMP_METADATA1_VALID				(0x8 << 28)

	__le32 rx_cmp_rss_hash;
};

#define BNXT_PTP_RX_TS_VALID(flags)				\
	(((flags) & RX_CMP_FLAGS_ITYPES_MASK) == RX_CMP_FLAGS_ITYPE_PTP_W_TS)

#define BNXT_ALL_RX_TS_VALID(flags)				\
	!((flags) & RX_CMP_FLAGS_PKT_METADATA_PRESENT)

#define RX_CMP_HASH_VALID(rxcmp)				\
	((rxcmp)->rx_cmp_len_flags_type & cpu_to_le32(RX_CMP_FLAGS_RSS_VALID))

#define RSS_PROFILE_ID_MASK	0x1f

#define RX_CMP_HASH_TYPE(rxcmp)					\
	(((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_RSS_HASH_TYPE) >>\
	  RX_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define RX_CMP_ITYPES(rxcmp)					\
	(le32_to_cpu((rxcmp)->rx_cmp_len_flags_type) & RX_CMP_FLAGS_ITYPES_MASK)

#define RX_CMP_V3_HASH_TYPE_LEGACY(rxcmp)				\
	((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_V3_RSS_EXT_OP_LEGACY) >>\
	 RX_CMP_V3_RSS_EXT_OP_LEGACY_SHIFT)

#define RX_CMP_V3_HASH_TYPE_NEW(rxcmp)				\
	((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_V3_RSS_EXT_OP_NEW) >>\
	 RX_CMP_V3_RSS_EXT_OP_NEW_SHIFT)

#define RX_CMP_V3_HASH_TYPE(bp, rxcmp)				\
	(((bp)->rss_cap & BNXT_RSS_CAP_RSS_TCAM) ?		\
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
	#define RX_CMPL_METADATA0_TCI_MASK			(0xffff << 16)
	#define RX_CMPL_METADATA0_VID_MASK			(0x0fff << 16)
	 #define RX_CMPL_METADATA0_SFT				 16

	__le32 rx_cmp_timestamp;
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

#define RX_CMP_METADATA0_TCI(rxcmp1)					\
	((le32_to_cpu((rxcmp1)->rx_cmp_cfa_code_errors_v2) &		\
	  RX_CMPL_METADATA0_TCI_MASK) >> RX_CMPL_METADATA0_SFT)

struct rx_agg_cmp {
	__le32 rx_agg_cmp_len_flags_type;
	#define RX_AGG_CMP_TYPE					(0x3f << 0)
	#define RX_AGG_CMP_LEN					(0xffff << 16)
	 #define RX_AGG_CMP_LEN_SHIFT				 16
	u32 rx_agg_cmp_opaque;
	__le32 rx_agg_cmp_v;
	#define RX_AGG_CMP_V					(1 << 0)
	#define RX_AGG_CMP_AGG_ID				(0x0fff << 16)
	 #define RX_AGG_CMP_AGG_ID_SHIFT			 16
	__le32 rx_agg_cmp_unused;
};

#define TPA_AGG_AGG_ID(rx_agg)				\
	((le32_to_cpu((rx_agg)->rx_agg_cmp_v) &		\
	 RX_AGG_CMP_AGG_ID) >> RX_AGG_CMP_AGG_ID_SHIFT)

struct rx_tpa_start_cmp {
	__le32 rx_tpa_start_cmp_len_flags_type;
	#define RX_TPA_START_CMP_TYPE				(0x3f << 0)
	#define RX_TPA_START_CMP_FLAGS				(0x3ff << 6)
	 #define RX_TPA_START_CMP_FLAGS_SHIFT			 6
	#define RX_TPA_START_CMP_FLAGS_ERROR			(0x1 << 6)
	#define RX_TPA_START_CMP_FLAGS_PLACEMENT		(0x7 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_SHIFT		 7
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_JUMBO		 (0x1 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_HDS		 (0x2 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_JUMBO	 (0x5 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_HDS	 (0x6 << 7)
	#define RX_TPA_START_CMP_FLAGS_RSS_VALID		(0x1 << 10)
	#define RX_TPA_START_CMP_FLAGS_TIMESTAMP		(0x1 << 11)
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
	#define RX_TPA_START_CMP_V3_RSS_HASH_TYPE		(0x1ff << 7)
	 #define RX_TPA_START_CMP_V3_RSS_HASH_TYPE_SHIFT	 7
	#define RX_TPA_START_CMP_AGG_ID				(0x7f << 25)
	 #define RX_TPA_START_CMP_AGG_ID_SHIFT			 25
	#define RX_TPA_START_CMP_AGG_ID_P5			(0x0fff << 16)
	 #define RX_TPA_START_CMP_AGG_ID_SHIFT_P5		 16
	#define RX_TPA_START_CMP_METADATA1			(0xf << 28)
	 #define RX_TPA_START_CMP_METADATA1_SHIFT		 28
	#define RX_TPA_START_METADATA1_TPID_SEL			(0x7 << 28)
	#define RX_TPA_START_METADATA1_TPID_8021Q		(0x1 << 28)
	#define RX_TPA_START_METADATA1_TPID_8021AD		(0x0 << 28)
	#define RX_TPA_START_METADATA1_VALID			(0x8 << 28)

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

#define TPA_START_AGG_ID(rx_tpa_start)					\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_CMP_AGG_ID) >> RX_TPA_START_CMP_AGG_ID_SHIFT)

#define TPA_START_AGG_ID_P5(rx_tpa_start)				\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_CMP_AGG_ID_P5) >> RX_TPA_START_CMP_AGG_ID_SHIFT_P5)

#define TPA_START_ERROR(rx_tpa_start)					\
	((rx_tpa_start)->rx_tpa_start_cmp_len_flags_type &		\
	 cpu_to_le32(RX_TPA_START_CMP_FLAGS_ERROR))

#define TPA_START_VLAN_VALID(rx_tpa_start)				\
	((rx_tpa_start)->rx_tpa_start_cmp_misc_v1 &			\
	 cpu_to_le32(RX_TPA_START_METADATA1_VALID))

#define TPA_START_VLAN_TPID_SEL(rx_tpa_start)				\
	(le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_METADATA1_TPID_SEL)

struct rx_tpa_start_cmp_ext {
	__le32 rx_tpa_start_cmp_flags2;
	#define RX_TPA_START_CMP_FLAGS2_IP_CS_CALC		(0x1 << 0)
	#define RX_TPA_START_CMP_FLAGS2_L4_CS_CALC		(0x1 << 1)
	#define RX_TPA_START_CMP_FLAGS2_T_IP_CS_CALC		(0x1 << 2)
	#define RX_TPA_START_CMP_FLAGS2_T_L4_CS_CALC		(0x1 << 3)
	#define RX_TPA_START_CMP_FLAGS2_IP_TYPE			(0x1 << 8)
	#define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL_VALID		(0x1 << 9)
	#define RX_TPA_START_CMP_FLAGS2_EXT_META_FORMAT		(0x3 << 10)
	 #define RX_TPA_START_CMP_FLAGS2_EXT_META_FORMAT_SHIFT	 10
	#define RX_TPA_START_CMP_V3_FLAGS2_T_IP_TYPE		(0x1 << 10)
	#define RX_TPA_START_CMP_V3_FLAGS2_AGG_GRO		(0x1 << 11)
	#define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL		(0xffff << 16)
	 #define RX_TPA_START_CMP_FLAGS2_CSUM_CMPL_SHIFT	 16

	__le32 rx_tpa_start_cmp_metadata;
	__le32 rx_tpa_start_cmp_cfa_code_v2;
	#define RX_TPA_START_CMP_V2				(0x1 << 0)
	#define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_MASK	(0x7 << 1)
	 #define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_SHIFT	 1
	 #define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_NO_BUFFER	 (0x0 << 1)
	 #define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT (0x3 << 1)
	 #define RX_TPA_START_CMP_ERRORS_BUFFER_ERROR_FLUSH	 (0x5 << 1)
	#define RX_TPA_START_CMP_CFA_CODE			(0xffff << 16)
	 #define RX_TPA_START_CMPL_CFA_CODE_SHIFT		 16
	#define RX_TPA_START_CMP_METADATA0_TCI_MASK		(0xffff << 16)
	#define RX_TPA_START_CMP_METADATA0_VID_MASK		(0x0fff << 16)
	 #define RX_TPA_START_CMP_METADATA0_SFT			 16
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
	#define RX_TPA_END_CMP_AGG_ID_P5			(0x0fff << 16)
	 #define RX_TPA_END_CMP_AGG_ID_SHIFT_P5			 16

	__le32 rx_tpa_end_cmp_tsdelta;
	#define RX_TPA_END_GRO_TS				(0x1 << 31)
};

#define TPA_END_AGG_ID(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_AGG_ID) >> RX_TPA_END_CMP_AGG_ID_SHIFT)

#define TPA_END_AGG_ID_P5(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_AGG_ID_P5) >> RX_TPA_END_CMP_AGG_ID_SHIFT_P5)

#define TPA_END_PAYLOAD_OFF(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_PAYLOAD_OFFSET) >> RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT)

#define TPA_END_AGG_BUFS(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_AGG_BUFS) >> RX_TPA_END_CMP_AGG_BUFS_SHIFT)

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
	#define RX_TPA_END_CMP_PAYLOAD_OFFSET_P5		(0xff << 16)
	 #define RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT_P5		 16
	#define RX_TPA_END_CMP_AGG_BUFS_P5			(0xff << 24)
	 #define RX_TPA_END_CMP_AGG_BUFS_SHIFT_P5		 24

	__le32 rx_tpa_end_cmp_seg_len;
	#define RX_TPA_END_CMP_TPA_SEG_LEN			(0xffff << 0)

	__le32 rx_tpa_end_cmp_errors_v2;
	#define RX_TPA_END_CMP_V2				(0x1 << 0)
	#define RX_TPA_END_CMP_ERRORS				(0x3 << 1)
	#define RX_TPA_END_CMP_ERRORS_P5			(0x7 << 1)
	#define RX_TPA_END_CMPL_ERRORS_SHIFT			 1
	 #define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_NO_BUFFER	 (0x0 << 1)
	 #define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_NOT_ON_CHIP	 (0x2 << 1)
	 #define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT	 (0x3 << 1)
	 #define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_RSV_ERROR	 (0x4 << 1)
	 #define RX_TPA_END_CMP_ERRORS_BUFFER_ERROR_FLUSH	 (0x5 << 1)

	u32 rx_tpa_end_cmp_start_opaque;
};

#define TPA_END_ERRORS(rx_tpa_end_ext)					\
	((rx_tpa_end_ext)->rx_tpa_end_cmp_errors_v2 &			\
	 cpu_to_le32(RX_TPA_END_CMP_ERRORS))

#define TPA_END_PAYLOAD_OFF_P5(rx_tpa_end_ext)				\
	((le32_to_cpu((rx_tpa_end_ext)->rx_tpa_end_cmp_dup_acks) &	\
	 RX_TPA_END_CMP_PAYLOAD_OFFSET_P5) >>				\
	RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT_P5)

#define TPA_END_AGG_BUFS_P5(rx_tpa_end_ext)				\
	((le32_to_cpu((rx_tpa_end_ext)->rx_tpa_end_cmp_dup_acks) &	\
	 RX_TPA_END_CMP_AGG_BUFS_P5) >> RX_TPA_END_CMP_AGG_BUFS_SHIFT_P5)

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
	!!((data1) &							\
	   ASYNC_EVENT_CMPL_ERROR_RECOVERY_EVENT_DATA1_FLAGS_MASTER_FUNC)

#define EVENT_DATA1_RECOVERY_ENABLED(data1)				\
	!!((data1) &							\
	   ASYNC_EVENT_CMPL_ERROR_RECOVERY_EVENT_DATA1_FLAGS_RECOVERY_ENABLED)

#define BNXT_EVENT_ERROR_REPORT_TYPE(data1)				\
	(((data1) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_MASK) >>\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_SFT)

#define BNXT_EVENT_INVALID_SIGNAL_DATA(data2)				\
	(((data2) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_INVALID_SIGNAL_EVENT_DATA2_PIN_ID_MASK) >>\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_INVALID_SIGNAL_EVENT_DATA2_PIN_ID_SFT)

struct nqe_cn {
	__le16	type;
	#define NQ_CN_TYPE_MASK           0x3fUL
	#define NQ_CN_TYPE_SFT            0
	#define NQ_CN_TYPE_CQ_NOTIFICATION  0x30UL
	#define NQ_CN_TYPE_LAST            NQ_CN_TYPE_CQ_NOTIFICATION
	#define NQ_CN_TOGGLE_MASK         0xc0UL
	#define NQ_CN_TOGGLE_SFT          6
	__le16	reserved16;
	__le32	cq_handle_low;
	__le32	v;
	#define NQ_CN_V     0x1UL
	__le32	cq_handle_high;
};

#define BNXT_NQ_HDL_IDX_MASK	0x00ffffff
#define BNXT_NQ_HDL_TYPE_MASK	0xff000000
#define BNXT_NQ_HDL_TYPE_SHIFT	24
#define BNXT_NQ_HDL_TYPE_RX	0x00
#define BNXT_NQ_HDL_TYPE_TX	0x01

#define BNXT_NQ_HDL_IDX(hdl)	((hdl) & BNXT_NQ_HDL_IDX_MASK)
#define BNXT_NQ_HDL_TYPE(hdl)	(((hdl) & BNXT_NQ_HDL_TYPE_MASK) >>	\
				 BNXT_NQ_HDL_TYPE_SHIFT)

#define BNXT_SET_NQ_HDL(cpr)						\
	(((cpr)->cp_ring_type << BNXT_NQ_HDL_TYPE_SHIFT) | (cpr)->cp_idx)

#define NQE_CN_TYPE(type)	((type) & NQ_CN_TYPE_MASK)
#define NQE_CN_TOGGLE(type)	(((type) & NQ_CN_TOGGLE_MASK) >>	\
				 NQ_CN_TOGGLE_SFT)

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
#define DBR_EPOCH_MASK					0x01000000UL
#define DBR_EPOCH_SFT					24
#define DBR_TOGGLE_MASK					0x06000000UL
#define DBR_TOGGLE_SFT					25
#define DBR_XID_MASK					0x000fffff00000000ULL
#define DBR_XID_SFT					32
#define DBR_PATH_L2					(0x1ULL << 56)
#define DBR_VALID					(0x1ULL << 58)
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
#define DBR_TYPE_NQ_MASK				(0xeULL << 60)
#define DBR_TYPE_NULL					(0xfULL << 60)

#define DB_PF_OFFSET_P5					0x10000
#define DB_VF_OFFSET_P5					0x4000

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

/* First RX buffer page in XDP multi-buf mode
 *
 * +-------------------------------------------------------------------------+
 * | XDP_PACKET_HEADROOM | bp->rx_buf_use_size              | skb_shared_info|
 * | (bp->rx_dma_offset) |                                  |                |
 * +-------------------------------------------------------------------------+
 */
#define BNXT_MAX_PAGE_MODE_MTU_SBUF \
	((unsigned int)PAGE_SIZE - VLAN_ETH_HLEN - NET_IP_ALIGN -	\
	 XDP_PACKET_HEADROOM)
#define BNXT_MAX_PAGE_MODE_MTU	\
	(BNXT_MAX_PAGE_MODE_MTU_SBUF - \
	 SKB_DATA_ALIGN((unsigned int)sizeof(struct skb_shared_info)))

#define BNXT_MIN_PKT_SIZE	52

#define BNXT_DEFAULT_RX_RING_SIZE	511
#define BNXT_DEFAULT_TX_RING_SIZE	511

#define MAX_TPA		64
#define MAX_TPA_P5	256
#define MAX_TPA_P5_MASK	(MAX_TPA_P5 - 1)
#define MAX_TPA_SEGS_P5	0x3f

#if (BNXT_PAGE_SHIFT == 16)
#define MAX_RX_PAGES_AGG_ENA	1
#define MAX_RX_PAGES	4
#define MAX_RX_AGG_PAGES	4
#define MAX_TX_PAGES	1
#define MAX_CP_PAGES	16
#else
#define MAX_RX_PAGES_AGG_ENA	8
#define MAX_RX_PAGES	32
#define MAX_RX_AGG_PAGES	32
#define MAX_TX_PAGES	8
#define MAX_CP_PAGES	128
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
#define BNXT_MAX_RX_DESC_CNT_JUM_ENA	(RX_DESC_CNT * MAX_RX_PAGES_AGG_ENA - 1)
#define BNXT_MAX_RX_JUM_DESC_CNT	(RX_DESC_CNT * MAX_RX_AGG_PAGES - 1)
#define BNXT_MAX_TX_DESC_CNT		(TX_DESC_CNT * MAX_TX_PAGES - 1)

/* Minimum TX BDs for a TX packet with MAX_SKB_FRAGS + 1.  We need one extra
 * BD because the first TX BD is always a long BD.
 */
#define BNXT_MIN_TX_DESC_CNT		(MAX_SKB_FRAGS + 2)

#define RX_RING(bp, x)	(((x) & (bp)->rx_ring_mask) >> (BNXT_PAGE_SHIFT - 4))
#define RX_AGG_RING(bp, x)	(((x) & (bp)->rx_agg_ring_mask) >>	\
				 (BNXT_PAGE_SHIFT - 4))
#define RX_IDX(x)	((x) & (RX_DESC_CNT - 1))

#define TX_RING(bp, x)	(((x) & (bp)->tx_ring_mask) >> (BNXT_PAGE_SHIFT - 4))
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

#define RING_RX(bp, idx)	((idx) & (bp)->rx_ring_mask)
#define NEXT_RX(idx)		((idx) + 1)

#define RING_RX_AGG(bp, idx)	((idx) & (bp)->rx_agg_ring_mask)
#define NEXT_RX_AGG(idx)	((idx) + 1)

#define RING_TX(bp, idx)	((idx) & (bp)->tx_ring_mask)
#define NEXT_TX(idx)		((idx) + 1)

#define ADV_RAW_CMP(idx, n)	((idx) + (n))
#define NEXT_RAW_CMP(idx)	ADV_RAW_CMP(idx, 1)
#define RING_CMP(idx)		((idx) & bp->cp_ring_mask)
#define NEXT_CMP(idx)		RING_CMP(ADV_RAW_CMP(idx, 1))

#define DFLT_HWRM_CMD_TIMEOUT		500

#define BNXT_RX_EVENT		1
#define BNXT_AGG_EVENT		2
#define BNXT_TX_EVENT		4
#define BNXT_REDIRECT_EVENT	8
#define BNXT_TX_CMP_EVENT	0x10

struct bnxt_sw_tx_bd {
	union {
		struct sk_buff		*skb;
		struct xdp_frame	*xdpf;
	};
	DEFINE_DMA_UNMAP_ADDR(mapping);
	DEFINE_DMA_UNMAP_LEN(len);
	struct page		*page;
	u8			is_ts_pkt;
	u8			is_push;
	u8			action;
	unsigned short		nr_frags;
	union {
		u16			rx_prod;
		u16			txts_prod;
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
	struct bnxt_ctx_mem_type	*ctx_mem;

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
	u32			db_ring_mask;
	u32			db_epoch_mask;
	u8			db_epoch_shift;
};

#define DB_EPOCH(db, idx)	(((idx) & (db)->db_epoch_mask) <<	\
				 ((db)->db_epoch_shift))

#define DB_TOGGLE(tgl)		((tgl) << DBR_TOGGLE_SFT)

#define DB_RING_IDX(db, idx)	(((idx) & (db)->db_ring_mask) |		\
				 DB_EPOCH(db, idx))

struct bnxt_tx_ring_info {
	struct bnxt_napi	*bnapi;
	struct bnxt_cp_ring_info	*tx_cpr;
	u16			tx_prod;
	u16			tx_cons;
	u16			tx_hw_cons;
	u16			txq_index;
	u8			tx_napi_idx;
	u8			kick_pending;
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
	/* Synchronize simultaneous xdp_xmit on same ring */
	spinlock_t		xdp_tx_lock;
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
	u16			flags;
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
	u8			agg_count;
	u8			vlan_valid:1;
	u8			cfa_code_valid:1;
	struct rx_agg_cmp	*agg_arr;
};

#define BNXT_AGG_IDX_BMAP_SIZE	(MAX_TPA_P5 / BITS_PER_LONG)

struct bnxt_tpa_idx_map {
	u16		agg_id_tbl[1024];
	unsigned long	agg_idx_bmap[BNXT_AGG_IDX_BMAP_SIZE];
};

struct bnxt_rx_ring_info {
	struct bnxt_napi	*bnapi;
	struct bnxt_cp_ring_info	*rx_cpr;
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

	dma_addr_t		rx_desc_mapping[MAX_RX_PAGES];
	dma_addr_t		rx_agg_desc_mapping[MAX_RX_AGG_PAGES];

	struct bnxt_tpa_info	*rx_tpa;
	struct bnxt_tpa_idx_map *rx_tpa_idx_map;

	struct bnxt_ring_struct	rx_ring_struct;
	struct bnxt_ring_struct	rx_agg_ring_struct;
	struct xdp_rxq_info	xdp_rxq;
	struct page_pool	*page_pool;
	struct page_pool	*head_pool;
};

struct bnxt_rx_sw_stats {
	u64			rx_l4_csum_errors;
	u64			rx_resets;
	u64			rx_buf_errors;
	u64			rx_oom_discards;
	u64			rx_netpoll_discards;
};

struct bnxt_tx_sw_stats {
	u64			tx_resets;
};

struct bnxt_cmn_sw_stats {
	u64			missed_irqs;
};

struct bnxt_sw_stats {
	struct bnxt_rx_sw_stats rx;
	struct bnxt_tx_sw_stats tx;
	struct bnxt_cmn_sw_stats cmn;
};

struct bnxt_total_ring_err_stats {
	u64			rx_total_l4_csum_errors;
	u64			rx_total_resets;
	u64			rx_total_buf_errors;
	u64			rx_total_oom_discards;
	u64			rx_total_netpoll_discards;
	u64			rx_total_ring_discards;
	u64			tx_total_resets;
	u64			tx_total_ring_discards;
	u64			total_missed_irqs;
};

struct bnxt_stats_mem {
	u64		*sw_stats;
	u64		*hw_masks;
	void		*hw_stats;
	dma_addr_t	hw_stats_map;
	int		len;
};

struct bnxt_cp_ring_info {
	struct bnxt_napi	*bnapi;
	u32			cp_raw_cons;
	struct bnxt_db_info	cp_db;

	u8			had_work_done:1;
	u8			has_more_work:1;
	u8			had_nqe_notify:1;
	u8			toggle;

	u8			cp_ring_type;
	u8			cp_idx;

	u32			last_cp_raw_cons;

	struct bnxt_coal	rx_ring_coal;
	u64			rx_packets;
	u64			rx_bytes;
	u64			event_ctr;

	struct dim		dim;

	union {
		struct tx_cmp	**cp_desc_ring;
		struct nqe_cn	**nq_desc_ring;
	};

	dma_addr_t		*cp_desc_mapping;

	struct bnxt_stats_mem	stats;
	u32			hw_stats_ctx_id;

	struct bnxt_sw_stats	*sw_stats;

	struct bnxt_ring_struct	cp_ring_struct;

	int			cp_ring_count;
	struct bnxt_cp_ring_info *cp_ring_arr;
};

#define BNXT_MAX_QUEUE		8
#define BNXT_MAX_TXR_PER_NAPI	BNXT_MAX_QUEUE

#define bnxt_for_each_napi_tx(iter, bnapi, txr)		\
	for (iter = 0, txr = (bnapi)->tx_ring[0]; txr;	\
	     txr = (iter < BNXT_MAX_TXR_PER_NAPI - 1) ?	\
	     (bnapi)->tx_ring[++iter] : NULL)

struct bnxt_napi {
	struct napi_struct	napi;
	struct bnxt		*bp;

	int			index;
	struct bnxt_cp_ring_info	cp_ring;
	struct bnxt_rx_ring_info	*rx_ring;
	struct bnxt_tx_ring_info	*tx_ring[BNXT_MAX_TXR_PER_NAPI];

	void			(*tx_int)(struct bnxt *, struct bnxt_napi *,
					  int budget);
	u8			events;
	u8			tx_fault:1;

	u32			flags;
#define BNXT_NAPI_FLAG_XDP	0x1

	bool			in_reset;
};

/* "TxRx", 2 hypens, plus maximum integer */
#define BNXT_IRQ_NAME_EXTRA	17

struct bnxt_irq {
	irq_handler_t	handler;
	unsigned int	vector;
	u8		requested:1;
	u8		have_cpumask:1;
	char		name[IFNAMSIZ + BNXT_IRQ_NAME_EXTRA];
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

#define BNXT_VNIC_DEFAULT	0
#define BNXT_VNIC_NTUPLE	1

struct bnxt_vnic_info {
	u16		fw_vnic_id; /* returned by Chimp during alloc */
#define BNXT_MAX_CTX_PER_VNIC	8
	u16		fw_rss_cos_lb_ctx[BNXT_MAX_CTX_PER_VNIC];
	u16		fw_l2_ctx_id;
	u16		mru;
#define BNXT_MAX_UC_ADDRS	4
	struct bnxt_l2_filter *l2_filters[BNXT_MAX_UC_ADDRS];
				/* index 0 always dev_addr */
	u16		uc_filter_count;
	u8		*uc_list;

	u16		*fw_grp_ids;
	dma_addr_t	rss_table_dma_addr;
	__le16		*rss_table;
	dma_addr_t	rss_hash_key_dma_addr;
	u64		*rss_hash_key;
	int		rss_table_size;
#define BNXT_RSS_TABLE_ENTRIES_P5	64
#define BNXT_RSS_TABLE_SIZE_P5		(BNXT_RSS_TABLE_ENTRIES_P5 * 4)
#define BNXT_RSS_TABLE_MAX_TBL_P5	8
#define BNXT_MAX_RSS_TABLE_SIZE_P5				\
	(BNXT_RSS_TABLE_SIZE_P5 * BNXT_RSS_TABLE_MAX_TBL_P5)
#define BNXT_MAX_RSS_TABLE_ENTRIES_P5				\
	(BNXT_RSS_TABLE_ENTRIES_P5 * BNXT_RSS_TABLE_MAX_TBL_P5)

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
#define BNXT_VNIC_NTUPLE_FLAG		0x20
#define BNXT_VNIC_RSSCTX_FLAG		0x40
	struct ethtool_rxfh_context *rss_ctx;
	u32		vnic_id;
};

struct bnxt_rss_ctx {
	struct bnxt_vnic_info vnic;
	u8	index;
};

#define BNXT_MAX_ETH_RSS_CTX	32
#define BNXT_VNIC_ID_INVALID	0xffffffff

struct bnxt_hw_rings {
	int tx;
	int rx;
	int grp;
	int cp;
	int cp_p5;
	int stat;
	int vnic;
	int rss_ctx;
};

struct bnxt_hw_resc {
	u16	min_rsscos_ctxs;
	u16	max_rsscos_ctxs;
	u16	resv_rsscos_ctxs;
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
	u32	max_encap_records;
	u32	max_decap_records;
	u32	max_tx_em_flows;
	u32	max_tx_wm_flows;
	u32	max_rx_em_flows;
	u32	max_rx_wm_flows;
};

#if defined(CONFIG_BNXT_SRIOV)
struct bnxt_vf_info {
	u16	fw_fid;
	u8	mac_addr[ETH_ALEN];	/* PF assigned MAC Address */
	u8	vf_mac_addr[ETH_ALEN];	/* VF assigned MAC address, only
					 * stored by PF.
					 */
	u16	vlan;
	u16	func_qcfg_flags;
	u32	flags;
#define BNXT_VF_SPOOFCHK	0x2
#define BNXT_VF_LINK_FORCED	0x4
#define BNXT_VF_LINK_UP		0x8
#define BNXT_VF_TRUST		0x10
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
	u16	registered_vfs;
	u16	max_vfs;
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

struct bnxt_filter_base {
	struct hlist_node	hash;
	struct list_head	list;
	__le64			filter_id;
	u8			type;
#define BNXT_FLTR_TYPE_NTUPLE	1
#define BNXT_FLTR_TYPE_L2	2
	u8			flags;
#define BNXT_ACT_DROP		1
#define BNXT_ACT_RING_DST	2
#define BNXT_ACT_FUNC_DST	4
#define BNXT_ACT_NO_AGING	8
#define BNXT_ACT_RSS_CTX	0x10
	u16			sw_id;
	u16			rxq;
	u16			fw_vnic_id;
	u16			vf_idx;
	unsigned long		state;
#define BNXT_FLTR_VALID		0
#define BNXT_FLTR_INSERTED	1
#define BNXT_FLTR_FW_DELETED	2

	struct rcu_head         rcu;
};

struct bnxt_flow_masks {
	struct flow_dissector_key_ports ports;
	struct flow_dissector_key_addrs addrs;
};

extern const struct bnxt_flow_masks BNXT_FLOW_MASK_NONE;
extern const struct bnxt_flow_masks BNXT_FLOW_IPV6_MASK_ALL;
extern const struct bnxt_flow_masks BNXT_FLOW_IPV4_MASK_ALL;

struct bnxt_ntuple_filter {
	/* base filter must be the first member */
	struct bnxt_filter_base	base;
	struct flow_keys	fkeys;
	struct bnxt_flow_masks	fmasks;
	struct bnxt_l2_filter	*l2_fltr;
	u32			flow_id;
};

struct bnxt_l2_key {
	union {
		struct {
			u8	dst_mac_addr[ETH_ALEN];
			u16	vlan;
		};
		u32	filter_key;
	};
};

struct bnxt_ipv4_tuple {
	struct flow_dissector_key_ipv4_addrs v4addrs;
	struct flow_dissector_key_ports ports;
};

struct bnxt_ipv6_tuple {
	struct flow_dissector_key_ipv6_addrs v6addrs;
	struct flow_dissector_key_ports ports;
};

#define BNXT_L2_KEY_SIZE	(sizeof(struct bnxt_l2_key) / 4)

struct bnxt_l2_filter {
	/* base filter must be the first member */
	struct bnxt_filter_base	base;
	struct bnxt_l2_key	l2_key;
	atomic_t		refcnt;
};

/* Compat version of hwrm_port_phy_qcfg_output capped at 96 bytes.  The
 * first 95 bytes are identical to hwrm_port_phy_qcfg_output in bnxt_hsi.h.
 * The last valid byte in the compat version is different.
 */
struct hwrm_port_phy_qcfg_output_compat {
	__le16	error_code;
	__le16	req_type;
	__le16	seq_id;
	__le16	resp_len;
	u8	link;
	u8	active_fec_signal_mode;
	__le16	link_speed;
	u8	duplex_cfg;
	u8	pause;
	__le16	support_speeds;
	__le16	force_link_speed;
	u8	auto_mode;
	u8	auto_pause;
	__le16	auto_link_speed;
	__le16	auto_link_speed_mask;
	u8	wirespeed;
	u8	lpbk;
	u8	force_pause;
	u8	module_status;
	__le32	preemphasis;
	u8	phy_maj;
	u8	phy_min;
	u8	phy_bld;
	u8	phy_type;
	u8	media_type;
	u8	xcvr_pkg_type;
	u8	eee_config_phy_addr;
	u8	parallel_detect;
	__le16	link_partner_adv_speeds;
	u8	link_partner_adv_auto_mode;
	u8	link_partner_adv_pause;
	__le16	adv_eee_link_speed_mask;
	__le16	link_partner_adv_eee_link_speed_mask;
	__le32	xcvr_identifier_type_tx_lpi_timer;
	__le16	fec_cfg;
	u8	duplex_state;
	u8	option_flags;
	char	phy_vendor_name[16];
	char	phy_vendor_partnumber[16];
	__le16	support_pam4_speeds;
	__le16	force_pam4_link_speed;
	__le16	auto_pam4_link_speed_mask;
	u8	link_partner_pam4_adv_speeds;
	u8	valid;
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
	u8			phy_state;
#define BNXT_PHY_STATE_ENABLED		0
#define BNXT_PHY_STATE_DISABLED		1

	u8			link_state;
#define BNXT_LINK_STATE_UNKNOWN	0
#define BNXT_LINK_STATE_DOWN	1
#define BNXT_LINK_STATE_UP	2
#define BNXT_LINK_IS_UP(bp)	((bp)->link_info.link_state == BNXT_LINK_STATE_UP)
	u8			active_lanes;
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
#define BNXT_LINK_SPEED_200GB	PORT_PHY_QCFG_RESP_LINK_SPEED_200GB
#define BNXT_LINK_SPEED_400GB	PORT_PHY_QCFG_RESP_LINK_SPEED_400GB
	u16			support_speeds;
	u16			support_pam4_speeds;
	u16			support_speeds2;

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
	u16			auto_pam4_link_speeds;
#define BNXT_LINK_PAM4_SPEED_MSK_50GB PORT_PHY_QCFG_RESP_SUPPORT_PAM4_SPEEDS_50G
#define BNXT_LINK_PAM4_SPEED_MSK_100GB PORT_PHY_QCFG_RESP_SUPPORT_PAM4_SPEEDS_100G
#define BNXT_LINK_PAM4_SPEED_MSK_200GB PORT_PHY_QCFG_RESP_SUPPORT_PAM4_SPEEDS_200G
	u16			auto_link_speeds2;
#define BNXT_LINK_SPEEDS2_MSK_1GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_1GB
#define BNXT_LINK_SPEEDS2_MSK_10GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_10GB
#define BNXT_LINK_SPEEDS2_MSK_25GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_25GB
#define BNXT_LINK_SPEEDS2_MSK_40GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_40GB
#define BNXT_LINK_SPEEDS2_MSK_50GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_50GB
#define BNXT_LINK_SPEEDS2_MSK_100GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_100GB
#define BNXT_LINK_SPEEDS2_MSK_50GB_PAM4	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_50GB_PAM4_56
#define BNXT_LINK_SPEEDS2_MSK_100GB_PAM4	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_100GB_PAM4_56
#define BNXT_LINK_SPEEDS2_MSK_200GB_PAM4	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_200GB_PAM4_56
#define BNXT_LINK_SPEEDS2_MSK_400GB_PAM4	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_400GB_PAM4_56
#define BNXT_LINK_SPEEDS2_MSK_100GB_PAM4_112	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_100GB_PAM4_112
#define BNXT_LINK_SPEEDS2_MSK_200GB_PAM4_112	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_200GB_PAM4_112
#define BNXT_LINK_SPEEDS2_MSK_400GB_PAM4_112	\
	PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS2_400GB_PAM4_112

	u16			support_auto_speeds;
	u16			support_pam4_auto_speeds;
	u16			support_auto_speeds2;

	u16			lp_auto_link_speeds;
	u16			lp_auto_pam4_link_speeds;
	u16			force_link_speed;
	u16			force_pam4_link_speed;
	u16			force_link_speed2;
#define BNXT_LINK_SPEED_50GB_PAM4	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_50GB_PAM4_56
#define BNXT_LINK_SPEED_100GB_PAM4	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_100GB_PAM4_56
#define BNXT_LINK_SPEED_200GB_PAM4	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_200GB_PAM4_56
#define BNXT_LINK_SPEED_400GB_PAM4	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_400GB_PAM4_56
#define BNXT_LINK_SPEED_100GB_PAM4_112	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_100GB_PAM4_112
#define BNXT_LINK_SPEED_200GB_PAM4_112	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_200GB_PAM4_112
#define BNXT_LINK_SPEED_400GB_PAM4_112	\
	PORT_PHY_CFG_REQ_FORCE_LINK_SPEEDS2_400GB_PAM4_112

	u32			preemphasis;
	u8			module_status;
	u8			active_fec_sig_mode;
	u16			fec_cfg;
#define BNXT_FEC_NONE		PORT_PHY_QCFG_RESP_FEC_CFG_FEC_NONE_SUPPORTED
#define BNXT_FEC_AUTONEG_CAP	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_AUTONEG_SUPPORTED
#define BNXT_FEC_AUTONEG	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_AUTONEG_ENABLED
#define BNXT_FEC_ENC_BASE_R_CAP	\
	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE74_SUPPORTED
#define BNXT_FEC_ENC_BASE_R	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE74_ENABLED
#define BNXT_FEC_ENC_RS_CAP	\
	PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE91_SUPPORTED
#define BNXT_FEC_ENC_LLRS_CAP	\
	(PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS272_1XN_SUPPORTED |	\
	 PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS272_IEEE_SUPPORTED)
#define BNXT_FEC_ENC_RS		\
	(PORT_PHY_QCFG_RESP_FEC_CFG_FEC_CLAUSE91_ENABLED |	\
	 PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS544_1XN_ENABLED |	\
	 PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS544_IEEE_ENABLED)
#define BNXT_FEC_ENC_LLRS	\
	(PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS272_1XN_ENABLED |	\
	 PORT_PHY_QCFG_RESP_FEC_CFG_FEC_RS272_IEEE_ENABLED)

	/* copy of requested setting from ethtool cmd */
	u8			autoneg;
#define BNXT_AUTONEG_SPEED		1
#define BNXT_AUTONEG_FLOW_CTRL		2
	u8			req_signal_mode;
#define BNXT_SIG_MODE_NRZ	PORT_PHY_QCFG_RESP_SIGNAL_MODE_NRZ
#define BNXT_SIG_MODE_PAM4	PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4
#define BNXT_SIG_MODE_PAM4_112	PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4_112
#define BNXT_SIG_MODE_MAX	(PORT_PHY_QCFG_RESP_SIGNAL_MODE_LAST + 1)
	u8			req_duplex;
	u8			req_flow_ctrl;
	u16			req_link_speed;
	u16			advertising;	/* user adv setting */
	u16			advertising_pam4;
	bool			force_link_chng;

	bool			phy_retry;
	unsigned long		phy_retry_expires;

	/* a copy of phy_qcfg output used to report link
	 * info to VF
	 */
	struct hwrm_port_phy_qcfg_output phy_qcfg_resp;
};

#define BNXT_FEC_RS544_ON					\
	 (PORT_PHY_CFG_REQ_FLAGS_FEC_RS544_1XN_ENABLE |		\
	  PORT_PHY_CFG_REQ_FLAGS_FEC_RS544_IEEE_ENABLE)

#define BNXT_FEC_RS544_OFF					\
	 (PORT_PHY_CFG_REQ_FLAGS_FEC_RS544_1XN_DISABLE |	\
	  PORT_PHY_CFG_REQ_FLAGS_FEC_RS544_IEEE_DISABLE)

#define BNXT_FEC_RS272_ON					\
	 (PORT_PHY_CFG_REQ_FLAGS_FEC_RS272_1XN_ENABLE |		\
	  PORT_PHY_CFG_REQ_FLAGS_FEC_RS272_IEEE_ENABLE)

#define BNXT_FEC_RS272_OFF					\
	 (PORT_PHY_CFG_REQ_FLAGS_FEC_RS272_1XN_DISABLE |	\
	  PORT_PHY_CFG_REQ_FLAGS_FEC_RS272_IEEE_DISABLE)

#define BNXT_PAM4_SUPPORTED(link_info)				\
	((link_info)->support_pam4_speeds)

#define BNXT_FEC_RS_ON(link_info)				\
	(PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE91_ENABLE |		\
	 PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE74_DISABLE |		\
	 (BNXT_PAM4_SUPPORTED(link_info) ?			\
	  (BNXT_FEC_RS544_ON | BNXT_FEC_RS272_OFF) : 0))

#define BNXT_FEC_LLRS_ON					\
	(PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE91_ENABLE |		\
	 PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE74_DISABLE |		\
	 BNXT_FEC_RS272_ON | BNXT_FEC_RS544_OFF)

#define BNXT_FEC_RS_OFF(link_info)				\
	(PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE91_DISABLE |		\
	 (BNXT_PAM4_SUPPORTED(link_info) ?			\
	  (BNXT_FEC_RS544_OFF | BNXT_FEC_RS272_OFF) : 0))

#define BNXT_FEC_BASE_R_ON(link_info)				\
	(PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE74_ENABLE |		\
	 BNXT_FEC_RS_OFF(link_info))

#define BNXT_FEC_ALL_OFF(link_info)				\
	(PORT_PHY_CFG_REQ_FLAGS_FEC_CLAUSE74_DISABLE |		\
	 BNXT_FEC_RS_OFF(link_info))

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
	u16 timeout;
	char string[BNXT_MAX_TEST][ETH_GSTRING_LEN];
};

#define CHIMP_REG_VIEW_ADDR				\
	((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) ? 0x80000000 : 0xb1000000)

#define BNXT_GRCPF_REG_CHIMP_COMM		0x0
#define BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER	0x100
#define BNXT_GRCPF_REG_WINDOW_BASE_OUT		0x400

#define BNXT_GRC_REG_STATUS_P5			0x520

#define BNXT_GRCPF_REG_KONG_COMM		0xA00
#define BNXT_GRCPF_REG_KONG_COMM_TRIGGER	0xB00

#define BNXT_GRC_REG_CHIP_NUM			0x48
#define BNXT_GRC_REG_BASE			0x260000

#define BNXT_TS_REG_TIMESYNC_TS0_LOWER		0x640180c
#define BNXT_TS_REG_TIMESYNC_TS0_UPPER		0x6401810

#define BNXT_GRC_BASE_MASK			0xfffff000
#define BNXT_GRC_OFFSET_MASK			0x00000ffc

struct bnxt_tc_flow_stats {
	u64		packets;
	u64		bytes;
};

#ifdef CONFIG_BNXT_FLOWER_OFFLOAD
struct bnxt_flower_indr_block_cb_priv {
	struct net_device *tunnel_netdev;
	struct bnxt *bp;
	struct list_head list;
};
#endif

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
#define MAX_CTX_BYTES		((size_t)MAX_CTX_TOTAL_PAGES * BNXT_PAGE_SIZE)
#define MAX_CTX_BYTES_MASK	(MAX_CTX_BYTES - 1)

struct bnxt_ctx_pg_info {
	u32		entries;
	u32		nr_pages;
	void		*ctx_pg_arr[MAX_CTX_PAGES];
	dma_addr_t	ctx_dma_arr[MAX_CTX_PAGES];
	struct bnxt_ring_mem_info ring_mem;
	struct bnxt_ctx_pg_info **ctx_pg_tbl;
};

#define BNXT_MAX_TQM_SP_RINGS		1
#define BNXT_MAX_TQM_FP_RINGS		8
#define BNXT_MAX_TQM_RINGS		\
	(BNXT_MAX_TQM_SP_RINGS + BNXT_MAX_TQM_FP_RINGS)

#define BNXT_BACKING_STORE_CFG_LEGACY_LEN	256

#define BNXT_SET_CTX_PAGE_ATTR(attr)					\
do {									\
	if (BNXT_PAGE_SIZE == 0x2000)					\
		attr = FUNC_BACKING_STORE_CFG_REQ_SRQ_PG_SIZE_PG_8K;	\
	else if (BNXT_PAGE_SIZE == 0x10000)				\
		attr = FUNC_BACKING_STORE_CFG_REQ_QPC_PG_SIZE_PG_64K;	\
	else								\
		attr = FUNC_BACKING_STORE_CFG_REQ_QPC_PG_SIZE_PG_4K;	\
} while (0)

struct bnxt_ctx_mem_type {
	u16	type;
	u16	entry_size;
	u32	flags;
#define BNXT_CTX_MEM_TYPE_VALID FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_TYPE_VALID
#define BNXT_CTX_MEM_FW_TRACE		\
	FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_FW_DBG_TRACE
#define BNXT_CTX_MEM_FW_BIN_TRACE	\
	FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_FW_BIN_DBG_TRACE
#define BNXT_CTX_MEM_PERSIST		\
	FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_NEXT_BS_OFFSET

	u32	instance_bmap;
	u8	init_value;
	u8	entry_multiple;
	u16	init_offset;
#define	BNXT_CTX_INIT_INVALID_OFFSET	0xffff
	u32	max_entries;
	u32	min_entries;
	u8	last:1;
	u8	mem_valid:1;
	u8	split_entry_cnt;
#define BNXT_MAX_SPLIT_ENTRY	4
	union {
		struct {
			u32	qp_l2_entries;
			u32	qp_qp1_entries;
			u32	qp_fast_qpmd_entries;
		};
		u32	srq_l2_entries;
		u32	cq_l2_entries;
		u32	vnic_entries;
		struct {
			u32	mrav_av_entries;
			u32	mrav_num_entries_units;
		};
		u32	split[BNXT_MAX_SPLIT_ENTRY];
	};
	struct bnxt_ctx_pg_info	*pg_info;
};

#define BNXT_CTX_MRAV_AV_SPLIT_ENTRY	0

#define BNXT_CTX_QP	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_QP
#define BNXT_CTX_SRQ	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRQ
#define BNXT_CTX_CQ	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CQ
#define BNXT_CTX_VNIC	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_VNIC
#define BNXT_CTX_STAT	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_STAT
#define BNXT_CTX_STQM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SP_TQM_RING
#define BNXT_CTX_FTQM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_FP_TQM_RING
#define BNXT_CTX_MRAV	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_MRAV
#define BNXT_CTX_TIM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_TIM
#define BNXT_CTX_TCK	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_TX_CK
#define BNXT_CTX_RCK	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RX_CK
#define BNXT_CTX_MTQM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_MP_TQM_RING
#define BNXT_CTX_SQDBS	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SQ_DB_SHADOW
#define BNXT_CTX_RQDBS	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RQ_DB_SHADOW
#define BNXT_CTX_SRQDBS	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRQ_DB_SHADOW
#define BNXT_CTX_CQDBS	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CQ_DB_SHADOW
#define BNXT_CTX_TBLSC	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_TBL_SCOPE
#define BNXT_CTX_XPAR	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_XID_PARTITION
#define BNXT_CTX_SRT	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRT_TRACE
#define BNXT_CTX_SRT2	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_SRT2_TRACE
#define BNXT_CTX_CRT	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CRT_TRACE
#define BNXT_CTX_CRT2	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CRT2_TRACE
#define BNXT_CTX_RIGP0	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RIGP0_TRACE
#define BNXT_CTX_L2HWRM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_L2_HWRM_TRACE
#define BNXT_CTX_REHWRM	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_ROCE_HWRM_TRACE
#define BNXT_CTX_CA0	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CA0_TRACE
#define BNXT_CTX_CA1	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CA1_TRACE
#define BNXT_CTX_CA2	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_CA2_TRACE
#define BNXT_CTX_RIGP1	FUNC_BACKING_STORE_QCAPS_V2_REQ_TYPE_RIGP1_TRACE

#define BNXT_CTX_MAX	(BNXT_CTX_TIM + 1)
#define BNXT_CTX_L2_MAX	(BNXT_CTX_FTQM + 1)
#define BNXT_CTX_V2_MAX	(BNXT_CTX_RIGP1 + 1)
#define BNXT_CTX_INV	((u16)-1)

struct bnxt_ctx_mem_info {
	u8	tqm_fp_rings_count;

	u32	flags;
	#define BNXT_CTX_FLAG_INITED	0x01
	struct bnxt_ctx_mem_type	ctx_arr[BNXT_CTX_V2_MAX];
};

enum bnxt_health_severity {
	SEVERITY_NORMAL = 0,
	SEVERITY_WARNING,
	SEVERITY_RECOVERABLE,
	SEVERITY_FATAL,
};

enum bnxt_health_remedy {
	REMEDY_DEVLINK_RECOVER,
	REMEDY_POWER_CYCLE_DEVICE,
	REMEDY_POWER_CYCLE_HOST,
	REMEDY_FW_UPDATE,
	REMEDY_HW_REPLACE,
};

struct bnxt_fw_health {
	u32 flags;
	u32 polling_dsecs;
	u32 master_func_wait_dsecs;
	u32 normal_func_wait_dsecs;
	u32 post_reset_wait_dsecs;
	u32 post_reset_max_wait_dsecs;
	u32 regs[4];
	u32 mapped_regs[4];
#define BNXT_FW_HEALTH_REG		0
#define BNXT_FW_HEARTBEAT_REG		1
#define BNXT_FW_RESET_CNT_REG		2
#define BNXT_FW_RESET_INPROG_REG	3
	u32 fw_reset_inprog_reg_mask;
	u32 last_fw_heartbeat;
	u32 last_fw_reset_cnt;
	u8 enabled:1;
	u8 primary:1;
	u8 status_reliable:1;
	u8 resets_reliable:1;
	u8 tmr_multiplier;
	u8 tmr_counter;
	u8 fw_reset_seq_cnt;
	u32 fw_reset_seq_regs[16];
	u32 fw_reset_seq_vals[16];
	u32 fw_reset_seq_delay_msec[16];
	u32 echo_req_data1;
	u32 echo_req_data2;
	struct devlink_health_reporter	*fw_reporter;
	/* Protects severity and remedy */
	struct mutex lock;
	enum bnxt_health_severity severity;
	enum bnxt_health_remedy remedy;
	u32 arrests;
	u32 discoveries;
	u32 survivals;
	u32 fatalities;
	u32 diagnoses;
};

#define BNXT_FW_HEALTH_REG_TYPE_MASK	3
#define BNXT_FW_HEALTH_REG_TYPE_CFG	0
#define BNXT_FW_HEALTH_REG_TYPE_GRC	1
#define BNXT_FW_HEALTH_REG_TYPE_BAR0	2
#define BNXT_FW_HEALTH_REG_TYPE_BAR1	3

#define BNXT_FW_HEALTH_REG_TYPE(reg)	((reg) & BNXT_FW_HEALTH_REG_TYPE_MASK)
#define BNXT_FW_HEALTH_REG_OFF(reg)	((reg) & ~BNXT_FW_HEALTH_REG_TYPE_MASK)

#define BNXT_FW_HEALTH_WIN_BASE		0x3000
#define BNXT_FW_HEALTH_WIN_MAP_OFF	8

#define BNXT_FW_HEALTH_WIN_OFF(reg)	(BNXT_FW_HEALTH_WIN_BASE +	\
					 ((reg) & BNXT_GRC_OFFSET_MASK))

#define BNXT_FW_STATUS_HEALTH_MSK	0xffff
#define BNXT_FW_STATUS_HEALTHY		0x8000
#define BNXT_FW_STATUS_SHUTDOWN		0x100000
#define BNXT_FW_STATUS_RECOVERING	0x400000

#define BNXT_FW_IS_HEALTHY(sts)		(((sts) & BNXT_FW_STATUS_HEALTH_MSK) ==\
					 BNXT_FW_STATUS_HEALTHY)

#define BNXT_FW_IS_BOOTING(sts)		(((sts) & BNXT_FW_STATUS_HEALTH_MSK) < \
					 BNXT_FW_STATUS_HEALTHY)

#define BNXT_FW_IS_ERR(sts)		(((sts) & BNXT_FW_STATUS_HEALTH_MSK) > \
					 BNXT_FW_STATUS_HEALTHY)

#define BNXT_FW_IS_RECOVERING(sts)	(BNXT_FW_IS_ERR(sts) &&		       \
					 ((sts) & BNXT_FW_STATUS_RECOVERING))

#define BNXT_FW_RETRY			5
#define BNXT_FW_IF_RETRY		10
#define BNXT_FW_SLOT_RESET_RETRY	4

struct bnxt_aux_priv {
	struct auxiliary_device aux_dev;
	struct bnxt_en_dev *edev;
	int id;
};

enum board_idx {
	BCM57301,
	BCM57302,
	BCM57304,
	BCM57417_NPAR,
	BCM58700,
	BCM57311,
	BCM57312,
	BCM57402,
	BCM57404,
	BCM57406,
	BCM57402_NPAR,
	BCM57407,
	BCM57412,
	BCM57414,
	BCM57416,
	BCM57417,
	BCM57412_NPAR,
	BCM57314,
	BCM57417_SFP,
	BCM57416_SFP,
	BCM57404_NPAR,
	BCM57406_NPAR,
	BCM57407_SFP,
	BCM57407_NPAR,
	BCM57414_NPAR,
	BCM57416_NPAR,
	BCM57452,
	BCM57454,
	BCM5745x_NPAR,
	BCM57508,
	BCM57504,
	BCM57502,
	BCM57508_NPAR,
	BCM57504_NPAR,
	BCM57502_NPAR,
	BCM57608,
	BCM57604,
	BCM57602,
	BCM57601,
	BCM58802,
	BCM58804,
	BCM58808,
	NETXTREME_E_VF,
	NETXTREME_C_VF,
	NETXTREME_S_VF,
	NETXTREME_C_VF_HV,
	NETXTREME_E_VF_HV,
	NETXTREME_E_P5_VF,
	NETXTREME_E_P5_VF_HV,
	NETXTREME_E_P7_VF,
};

#define BNXT_TRACE_BUF_MAGIC_BYTE ((u8)0xbc)
#define BNXT_TRACE_MAX 11

struct bnxt_bs_trace_info {
	u8 *magic_byte;
	u32 last_offset;
	u8 wrapped:1;
	u16 ctx_type;
	u16 trace_type;
};

static inline void bnxt_bs_trace_check_wrap(struct bnxt_bs_trace_info *bs_trace,
					    u32 offset)
{
	if (!bs_trace->wrapped &&
	    *bs_trace->magic_byte != BNXT_TRACE_BUF_MAGIC_BYTE)
		bs_trace->wrapped = 1;
	bs_trace->last_offset = offset;
}

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
#define CHIP_NUM_57452		0xc452
#define CHIP_NUM_57454		0xc454

#define CHIP_NUM_57508		0x1750
#define CHIP_NUM_57504		0x1751
#define CHIP_NUM_57502		0x1752

#define CHIP_NUM_57608		0x1760

#define CHIP_NUM_58802		0xd802
#define CHIP_NUM_58804		0xd804
#define CHIP_NUM_58808		0xd808

	u8			chip_rev;

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
	((chip_num) == CHIP_NUM_5745X ||	\
	 (chip_num) == CHIP_NUM_57452 ||	\
	 (chip_num) == CHIP_NUM_57454)


#define BNXT_CHIP_NUM_57X0X(chip_num)		\
	(BNXT_CHIP_NUM_5730X(chip_num) || BNXT_CHIP_NUM_5740X(chip_num))

#define BNXT_CHIP_NUM_57X1X(chip_num)		\
	(BNXT_CHIP_NUM_5731X(chip_num) || BNXT_CHIP_NUM_5741X(chip_num))

#define BNXT_CHIP_NUM_588XX(chip_num)		\
	((chip_num) == CHIP_NUM_58802 ||	\
	 (chip_num) == CHIP_NUM_58804 ||        \
	 (chip_num) == CHIP_NUM_58808)

#define BNXT_VPD_FLD_LEN	32
	char			board_partno[BNXT_VPD_FLD_LEN];
	char			board_serialno[BNXT_VPD_FLD_LEN];

	struct net_device	*dev;
	struct pci_dev		*pdev;

	atomic_t		intr_sem;

	u32			flags;
	#define BNXT_FLAG_CHIP_P5_PLUS	0x1
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
	#define BNXT_FLAG_RFS		0x100
	#define BNXT_FLAG_SHARED_RINGS	0x200
	#define BNXT_FLAG_PORT_STATS	0x400
	#define BNXT_FLAG_WOL_CAP	0x4000
	#define BNXT_FLAG_ROCEV1_CAP	0x8000
	#define BNXT_FLAG_ROCEV2_CAP	0x10000
	#define BNXT_FLAG_ROCE_CAP	(BNXT_FLAG_ROCEV1_CAP |	\
					 BNXT_FLAG_ROCEV2_CAP)
	#define BNXT_FLAG_NO_AGG_RINGS	0x20000
	#define BNXT_FLAG_RX_PAGE_MODE	0x40000
	#define BNXT_FLAG_CHIP_P7	0x80000
	#define BNXT_FLAG_MULTI_HOST	0x100000
	#define BNXT_FLAG_DSN_VALID	0x200000
	#define BNXT_FLAG_DOUBLE_DB	0x400000
	#define BNXT_FLAG_UDP_GSO_CAP	0x800000
	#define BNXT_FLAG_CHIP_NITRO_A0	0x1000000
	#define BNXT_FLAG_DIM		0x2000000
	#define BNXT_FLAG_ROCE_MIRROR_CAP	0x4000000
	#define BNXT_FLAG_TX_COAL_CMPL	0x8000000
	#define BNXT_FLAG_PORT_STATS_EXT	0x10000000
	#define BNXT_FLAG_HDS		0x20000000
	#define BNXT_FLAG_AGG_RINGS	(BNXT_FLAG_JUMBO | BNXT_FLAG_GRO | \
					 BNXT_FLAG_LRO | BNXT_FLAG_HDS)

	#define BNXT_FLAG_ALL_CONFIG_FEATS (BNXT_FLAG_TPA |		\
					    BNXT_FLAG_RFS |		\
					    BNXT_FLAG_STRIP_VLAN)

#define BNXT_PF(bp)		(!((bp)->flags & BNXT_FLAG_VF))
#define BNXT_VF(bp)		((bp)->flags & BNXT_FLAG_VF)
#ifdef CONFIG_BNXT_SRIOV
#define	BNXT_VF_IS_TRUSTED(bp)	((bp)->vf.flags & BNXT_VF_TRUST)
#else
#define	BNXT_VF_IS_TRUSTED(bp)	0
#endif
#define BNXT_NPAR(bp)		((bp)->port_partition_type)
#define BNXT_MH(bp)		((bp)->flags & BNXT_FLAG_MULTI_HOST)
#define BNXT_SINGLE_PF(bp)	(BNXT_PF(bp) && !BNXT_NPAR(bp) && !BNXT_MH(bp))
#define BNXT_SH_PORT_CFG_OK(bp)	(BNXT_PF(bp) &&				\
				 ((bp)->phy_flags & BNXT_PHY_FL_SHARED_PORT_CFG))
#define BNXT_PHY_CFG_ABLE(bp)	((BNXT_SINGLE_PF(bp) ||			\
				  BNXT_SH_PORT_CFG_OK(bp)) &&		\
				 (bp)->link_info.phy_state == BNXT_PHY_STATE_ENABLED)
#define BNXT_CHIP_TYPE_NITRO_A0(bp) ((bp)->flags & BNXT_FLAG_CHIP_NITRO_A0)
#define BNXT_RX_PAGE_MODE(bp)	((bp)->flags & BNXT_FLAG_RX_PAGE_MODE)
#define BNXT_SUPPORTS_TPA(bp)	(!BNXT_CHIP_TYPE_NITRO_A0(bp) &&	\
				 (!((bp)->flags & BNXT_FLAG_CHIP_P5_PLUS) ||\
				  (bp)->max_tpa_v2) && !is_kdump_kernel())
#define BNXT_RX_JUMBO_MODE(bp)	((bp)->flags & BNXT_FLAG_JUMBO)

#define BNXT_CHIP_P7(bp)			\
	((bp)->chip_num == CHIP_NUM_57608)

#define BNXT_CHIP_P5(bp)			\
	((bp)->chip_num == CHIP_NUM_57508 ||	\
	 (bp)->chip_num == CHIP_NUM_57504 ||	\
	 (bp)->chip_num == CHIP_NUM_57502)

/* Chip class phase 5 */
#define BNXT_CHIP_P5_PLUS(bp)			\
	(BNXT_CHIP_P5(bp) || BNXT_CHIP_P7(bp))

/* Chip class phase 4.x */
#define BNXT_CHIP_P4(bp)			\
	(BNXT_CHIP_NUM_57X1X((bp)->chip_num) ||	\
	 BNXT_CHIP_NUM_5745X((bp)->chip_num) ||	\
	 BNXT_CHIP_NUM_588XX((bp)->chip_num) ||	\
	 (BNXT_CHIP_NUM_58700((bp)->chip_num) &&	\
	  !BNXT_CHIP_TYPE_NITRO_A0(bp)))

/* Chip class phase 3.x */
#define BNXT_CHIP_P3(bp)			\
	(BNXT_CHIP_NUM_57X0X((bp)->chip_num) ||	\
	 BNXT_CHIP_TYPE_NITRO_A0(bp))

#define BNXT_CHIP_P4_PLUS(bp)			\
	(BNXT_CHIP_P4(bp) || BNXT_CHIP_P5_PLUS(bp))

#define BNXT_CHIP_P5_AND_MINUS(bp)		\
	(BNXT_CHIP_P3(bp) || BNXT_CHIP_P4(bp) || BNXT_CHIP_P5(bp))

	struct bnxt_aux_priv	*aux_priv;
	struct bnxt_en_dev	*edev;

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

	u16			max_tpa_v2;
	u16			max_tpa;
	u32			rx_buf_size;
	u32			rx_buf_use_size;	/* useable size */
	u16			rx_offset;
	u16			rx_dma_offset;
	enum dma_data_direction	rx_dir;
	u32			rx_ring_size;
	u32			rx_agg_ring_size;
	u32			rx_copybreak;
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
	u32			num_rss_ctx;
	int			nr_vnics;
	u32			*rss_indir_tbl;
	u16			rss_indir_tbl_entries;
	u32			rss_hash_cfg;
	u32			rss_hash_delta;
	u32			rss_cap;
#define BNXT_RSS_CAP_RSS_HASH_TYPE_DELTA	BIT(0)
#define BNXT_RSS_CAP_UDP_RSS_CAP		BIT(1)
#define BNXT_RSS_CAP_NEW_RSS_CAP		BIT(2)
#define BNXT_RSS_CAP_RSS_TCAM			BIT(3)
#define BNXT_RSS_CAP_AH_V4_RSS_CAP		BIT(4)
#define BNXT_RSS_CAP_AH_V6_RSS_CAP		BIT(5)
#define BNXT_RSS_CAP_ESP_V4_RSS_CAP		BIT(6)
#define BNXT_RSS_CAP_ESP_V6_RSS_CAP		BIT(7)
#define BNXT_RSS_CAP_MULTI_RSS_CTX		BIT(8)

	u8			rss_hash_key[HW_HASH_KEY_SIZE];
	u8			rss_hash_key_valid:1;
	u8			rss_hash_key_updated:1;

	u16			max_mtu;
	u16			tso_max_segs;
	u8			max_tc;
	u8			max_lltc;	/* lossless TCs */
	struct bnxt_queue_info	q_info[BNXT_MAX_QUEUE];
	u8			tc_to_qidx[BNXT_MAX_QUEUE];
	u8			q_ids[BNXT_MAX_QUEUE];
	u8			max_q;
	u8			num_tc;

	unsigned int		current_interval;
#define BNXT_TIMER_INTERVAL	HZ

	struct timer_list	timer;

	unsigned long		state;
#define BNXT_STATE_OPEN		0
#define BNXT_STATE_IN_SP_TASK	1
#define BNXT_STATE_READ_STATS	2
#define BNXT_STATE_FW_RESET_DET 3
#define BNXT_STATE_IN_FW_RESET	4
#define BNXT_STATE_ABORT_ERR	5
#define BNXT_STATE_FW_FATAL_COND	6
#define BNXT_STATE_DRV_REGISTERED	7
#define BNXT_STATE_PCI_CHANNEL_IO_FROZEN	8
#define BNXT_STATE_NAPI_DISABLED	9
#define BNXT_STATE_L2_FILTER_RETRY	10
#define BNXT_STATE_FW_ACTIVATE		11
#define BNXT_STATE_RECOVER		12
#define BNXT_STATE_FW_NON_FATAL_COND	13
#define BNXT_STATE_FW_ACTIVATE_RESET	14
#define BNXT_STATE_HALF_OPEN		15	/* For offline ethtool tests */

#define BNXT_NO_FW_ACCESS(bp)					\
	(test_bit(BNXT_STATE_FW_FATAL_COND, &(bp)->state) ||	\
	 pci_channel_offline((bp)->pdev))

	struct bnxt_irq	*irq_tbl;
	int			total_irqs;
	int			ulp_num_msix_want;
	u8			mac_addr[ETH_ALEN];

#ifdef CONFIG_BNXT_DCB
	struct ieee_pfc		*ieee_pfc;
	struct ieee_ets		*ieee_ets;
	u8			dcbx_cap;
	u8			default_pri;
	u8			max_dscp_value;
#endif /* CONFIG_BNXT_DCB */

	u32			msg_enable;

	u64			fw_cap;
	#define BNXT_FW_CAP_SHORT_CMD			BIT_ULL(0)
	#define BNXT_FW_CAP_LLDP_AGENT			BIT_ULL(1)
	#define BNXT_FW_CAP_DCBX_AGENT			BIT_ULL(2)
	#define BNXT_FW_CAP_NEW_RM			BIT_ULL(3)
	#define BNXT_FW_CAP_IF_CHANGE			BIT_ULL(4)
	#define BNXT_FW_CAP_ENABLE_RDMA_SRIOV           BIT_ULL(5)
	#define BNXT_FW_CAP_ROCE_VF_RESC_MGMT_SUPPORTED	BIT_ULL(6)
	#define BNXT_FW_CAP_KONG_MB_CHNL		BIT_ULL(7)
	#define BNXT_FW_CAP_OVS_64BIT_HANDLE		BIT_ULL(10)
	#define BNXT_FW_CAP_TRUSTED_VF			BIT_ULL(11)
	#define BNXT_FW_CAP_ERROR_RECOVERY		BIT_ULL(13)
	#define BNXT_FW_CAP_PKG_VER			BIT_ULL(14)
	#define BNXT_FW_CAP_CFA_ADV_FLOW		BIT_ULL(15)
	#define BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2	BIT_ULL(16)
	#define BNXT_FW_CAP_PCIE_STATS_SUPPORTED	BIT_ULL(17)
	#define BNXT_FW_CAP_EXT_STATS_SUPPORTED		BIT_ULL(18)
	#define BNXT_FW_CAP_TX_TS_CMP			BIT_ULL(19)
	#define BNXT_FW_CAP_ERR_RECOVER_RELOAD		BIT_ULL(20)
	#define BNXT_FW_CAP_HOT_RESET			BIT_ULL(21)
	#define BNXT_FW_CAP_PTP_RTC			BIT_ULL(22)
	#define BNXT_FW_CAP_RX_ALL_PKT_TS		BIT_ULL(23)
	#define BNXT_FW_CAP_VLAN_RX_STRIP		BIT_ULL(24)
	#define BNXT_FW_CAP_VLAN_TX_INSERT		BIT_ULL(25)
	#define BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED	BIT_ULL(26)
	#define BNXT_FW_CAP_LIVEPATCH			BIT_ULL(27)
	#define BNXT_FW_CAP_PTP_PPS			BIT_ULL(28)
	#define BNXT_FW_CAP_HOT_RESET_IF		BIT_ULL(29)
	#define BNXT_FW_CAP_RING_MONITOR		BIT_ULL(30)
	#define BNXT_FW_CAP_DBG_QCAPS			BIT_ULL(31)
	#define BNXT_FW_CAP_PTP				BIT_ULL(32)
	#define BNXT_FW_CAP_THRESHOLD_TEMP_SUPPORTED	BIT_ULL(33)
	#define BNXT_FW_CAP_DFLT_VLAN_TPID_PCP		BIT_ULL(34)
	#define BNXT_FW_CAP_PRE_RESV_VNICS		BIT_ULL(35)
	#define BNXT_FW_CAP_BACKING_STORE_V2		BIT_ULL(36)
	#define BNXT_FW_CAP_VNIC_TUNNEL_TPA		BIT_ULL(37)
	#define BNXT_FW_CAP_CFA_NTUPLE_RX_EXT_IP_PROTO	BIT_ULL(38)
	#define BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V3	BIT_ULL(39)
	#define BNXT_FW_CAP_VNIC_RE_FLUSH		BIT_ULL(40)
	#define BNXT_FW_CAP_SW_MAX_RESOURCE_LIMITS	BIT_ULL(41)

	u32			fw_dbg_cap;

#define BNXT_NEW_RM(bp)		((bp)->fw_cap & BNXT_FW_CAP_NEW_RM)
#define BNXT_PTP_USE_RTC(bp)	(!BNXT_MH(bp) && \
				 ((bp)->fw_cap & BNXT_FW_CAP_PTP_RTC))
#define BNXT_SUPPORTS_NTUPLE_VNIC(bp)	\
	(BNXT_PF(bp) && ((bp)->fw_cap & BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V3))

#define BNXT_SUPPORTS_MULTI_RSS_CTX(bp)				\
	(BNXT_PF(bp) && BNXT_SUPPORTS_NTUPLE_VNIC(bp) &&	\
	 ((bp)->rss_cap & BNXT_RSS_CAP_MULTI_RSS_CTX))
#define BNXT_SUPPORTS_QUEUE_API(bp)				\
	(BNXT_PF(bp) && BNXT_SUPPORTS_NTUPLE_VNIC(bp) &&	\
	 ((bp)->fw_cap & BNXT_FW_CAP_VNIC_RE_FLUSH))
#define BNXT_RDMA_SRIOV_EN(bp)		\
	((bp)->fw_cap & BNXT_FW_CAP_ENABLE_RDMA_SRIOV)
#define BNXT_ROCE_VF_RESC_CAP(bp)	\
	((bp)->fw_cap & BNXT_FW_CAP_ROCE_VF_RESC_MGMT_SUPPORTED)
#define BNXT_SW_RES_LMT(bp)		\
	((bp)->fw_cap & BNXT_FW_CAP_SW_MAX_RESOURCE_LIMITS)

	u32			hwrm_spec_code;
	u16			hwrm_cmd_seq;
	u16                     hwrm_cmd_kong_seq;
	struct dma_pool		*hwrm_dma_pool;
	struct hlist_head	hwrm_pending_list;

	struct rtnl_link_stats64	net_stats_prev;
	struct bnxt_stats_mem	port_stats;
	struct bnxt_stats_mem	rx_port_stats_ext;
	struct bnxt_stats_mem	tx_port_stats_ext;
	u16			fw_rx_stats_ext_size;
	u16			fw_tx_stats_ext_size;
	u16			hw_ring_stats_size;
	u8			pri2cos_idx[8];
	u8			pri2cos_valid;

	struct bnxt_total_ring_err_stats ring_err_stats_prev;

	u16			hwrm_max_req_len;
	u16			hwrm_max_ext_req_len;
	unsigned int		hwrm_cmd_timeout;
	unsigned int		hwrm_cmd_max_timeout;
	struct mutex		hwrm_cmd_lock;	/* serialize hwrm messages */
	struct hwrm_ver_get_output	ver_resp;
#define FW_VER_STR_LEN		32
#define BC_HWRM_STR_LEN		21
#define PHY_VER_STR_LEN         (FW_VER_STR_LEN - BC_HWRM_STR_LEN)
	char			fw_ver_str[FW_VER_STR_LEN];
	char			hwrm_ver_supp[FW_VER_STR_LEN];
	char			nvm_cfg_ver[FW_VER_STR_LEN];
	u64			fw_ver_code;
#define BNXT_FW_VER_CODE(maj, min, bld, rsv)			\
	((u64)(maj) << 48 | (u64)(min) << 32 | (u64)(bld) << 16 | (rsv))
#define BNXT_FW_MAJ(bp)		((bp)->fw_ver_code >> 48)
#define BNXT_FW_BLD(bp)		(((bp)->fw_ver_code >> 16) & 0xffff)

	u16			vxlan_fw_dst_port_id;
	u16			nge_fw_dst_port_id;
	u16			vxlan_gpe_fw_dst_port_id;
	__be16			vxlan_port;
	__be16			nge_port;
	__be16			vxlan_gpe_port;
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
#define BNXT_RESET_TASK_SP_EVENT	6
#define BNXT_RST_RING_SP_EVENT		7
#define BNXT_HWRM_PF_UNLOAD_SP_EVENT	8
#define BNXT_PERIODIC_STATS_SP_EVENT	9
#define BNXT_HWRM_PORT_MODULE_SP_EVENT	10
#define BNXT_RESET_TASK_SILENT_SP_EVENT	11
#define BNXT_LINK_SPEED_CHNG_SP_EVENT	14
#define BNXT_FLOW_STATS_SP_EVENT	15
#define BNXT_UPDATE_PHY_SP_EVENT	16
#define BNXT_RING_COAL_NOW_SP_EVENT	17
#define BNXT_FW_RESET_NOTIFY_SP_EVENT	18
#define BNXT_FW_EXCEPTION_SP_EVENT	19
#define BNXT_LINK_CFG_CHANGE_SP_EVENT	21
#define BNXT_THERMAL_THRESHOLD_SP_EVENT	22
#define BNXT_FW_ECHO_REQUEST_SP_EVENT	23
#define BNXT_RESTART_ULP_SP_EVENT	24

	struct delayed_work	fw_reset_task;
	int			fw_reset_state;
#define BNXT_FW_RESET_STATE_POLL_VF	1
#define BNXT_FW_RESET_STATE_RESET_FW	2
#define BNXT_FW_RESET_STATE_ENABLE_DEV	3
#define BNXT_FW_RESET_STATE_POLL_FW	4
#define BNXT_FW_RESET_STATE_OPENING	5
#define BNXT_FW_RESET_STATE_POLL_FW_DOWN	6

	u16			fw_reset_min_dsecs;
#define BNXT_DFLT_FW_RST_MIN_DSECS	20
	u16			fw_reset_max_dsecs;
#define BNXT_DFLT_FW_RST_MAX_DSECS	60
	unsigned long		fw_reset_timestamp;

	struct bnxt_fw_health	*fw_health;

	struct bnxt_hw_resc	hw_resc;
	struct bnxt_pf_info	pf;
	struct bnxt_ctx_mem_info	*ctx;
#ifdef CONFIG_BNXT_SRIOV
	int			nr_vfs;
	struct bnxt_vf_info	vf;
	wait_queue_head_t	sriov_cfg_wait;
	bool			sriov_cfg;
#define BNXT_SRIOV_CFG_WAIT_TMO	msecs_to_jiffies(10000)
#endif

#if BITS_PER_LONG == 32
	/* ensure atomic 64-bit doorbell writes on 32-bit systems. */
	spinlock_t		db_lock;
#endif
	int			db_offset;	/* db_offset within db_size */
	int			db_size;

#define BNXT_NTP_FLTR_MAX_FLTR	4096
#define BNXT_MAX_FLTR		(BNXT_NTP_FLTR_MAX_FLTR + BNXT_L2_FLTR_MAX_FLTR)
#define BNXT_NTP_FLTR_HASH_SIZE	512
#define BNXT_NTP_FLTR_HASH_MASK	(BNXT_NTP_FLTR_HASH_SIZE - 1)
	struct hlist_head	ntp_fltr_hash_tbl[BNXT_NTP_FLTR_HASH_SIZE];
	spinlock_t		ntp_fltr_lock;	/* for hash table add, del */

	unsigned long		*ntp_fltr_bmap;
	int			ntp_fltr_count;
	int			max_fltr;

#define BNXT_L2_FLTR_MAX_FLTR	1024
#define BNXT_L2_FLTR_HASH_SIZE	32
#define BNXT_L2_FLTR_HASH_MASK	(BNXT_L2_FLTR_HASH_SIZE - 1)
	struct hlist_head	l2_fltr_hash_tbl[BNXT_L2_FLTR_HASH_SIZE];

	u32			hash_seed;
	u64			toeplitz_prefix;

	struct list_head	usr_fltr_list;

	/* To protect link related settings during link changes and
	 * ethtool settings changes.
	 */
	struct mutex		link_lock;
	struct bnxt_link_info	link_info;
	struct ethtool_keee	eee;
	u32			lpi_tmr_lo;
	u32			lpi_tmr_hi;

	/* copied from flags and flags2 in hwrm_port_phy_qcaps_output */
	u32			phy_flags;
#define BNXT_PHY_FL_EEE_CAP		PORT_PHY_QCAPS_RESP_FLAGS_EEE_SUPPORTED
#define BNXT_PHY_FL_EXT_LPBK		PORT_PHY_QCAPS_RESP_FLAGS_EXTERNAL_LPBK_SUPPORTED
#define BNXT_PHY_FL_AN_PHY_LPBK		PORT_PHY_QCAPS_RESP_FLAGS_AUTONEG_LPBK_SUPPORTED
#define BNXT_PHY_FL_SHARED_PORT_CFG	PORT_PHY_QCAPS_RESP_FLAGS_SHARED_PHY_CFG_SUPPORTED
#define BNXT_PHY_FL_PORT_STATS_NO_RESET	PORT_PHY_QCAPS_RESP_FLAGS_CUMULATIVE_COUNTERS_ON_RESET
#define BNXT_PHY_FL_NO_PHY_LPBK		PORT_PHY_QCAPS_RESP_FLAGS_LOCAL_LPBK_NOT_SUPPORTED
#define BNXT_PHY_FL_FW_MANAGED_LKDN	PORT_PHY_QCAPS_RESP_FLAGS_FW_MANAGED_LINK_DOWN
#define BNXT_PHY_FL_NO_FCS		PORT_PHY_QCAPS_RESP_FLAGS_NO_FCS
#define BNXT_PHY_FL_NO_PAUSE		(PORT_PHY_QCAPS_RESP_FLAGS2_PAUSE_UNSUPPORTED << 8)
#define BNXT_PHY_FL_NO_PFC		(PORT_PHY_QCAPS_RESP_FLAGS2_PFC_UNSUPPORTED << 8)
#define BNXT_PHY_FL_BANK_SEL		(PORT_PHY_QCAPS_RESP_FLAGS2_BANK_ADDR_SUPPORTED << 8)
#define BNXT_PHY_FL_SPEEDS2		(PORT_PHY_QCAPS_RESP_FLAGS2_SPEEDS2_SUPPORTED << 8)

	/* copied from flags in hwrm_port_mac_qcaps_output */
	u8			mac_flags;
#define BNXT_MAC_FL_NO_MAC_LPBK		\
	PORT_MAC_QCAPS_RESP_FLAGS_LOCAL_LPBK_NOT_SUPPORTED

	u8			num_tests;
	struct bnxt_test_info	*test_info;

	u8			wol_filter_id;
	u8			wol;

	u8			num_leds;
	struct bnxt_led_info	leds[BNXT_MAX_LED];
	u16			dump_flag;
#define BNXT_DUMP_LIVE		0
#define BNXT_DUMP_CRASH		1
#define BNXT_DUMP_DRIVER	2

	struct bpf_prog		*xdp_prog;

	struct bnxt_ptp_cfg	*ptp_cfg;
	u8			ptp_all_rx_tstamp;

	/* devlink interface and vf-rep structs */
	struct devlink		*dl;
	struct devlink_port	dl_port;
	enum devlink_eswitch_mode eswitch_mode;
	struct bnxt_vf_rep	**vf_reps; /* array of vf-rep ptrs */
	u16			*cfa_code_map; /* cfa_code -> vf_idx map */
	u8			dsn[8];
	struct bnxt_tc_info	*tc_info;
	struct list_head	tc_indr_block_list;
	struct dentry		*debugfs_pdev;
#ifdef CONFIG_BNXT_HWMON
	struct device		*hwmon_dev;
	u8			warn_thresh_temp;
	u8			crit_thresh_temp;
	u8			fatal_thresh_temp;
	u8			shutdown_thresh_temp;
#endif
	u32			thermal_threshold_type;
	enum board_idx		board_idx;

	struct bnxt_ctx_pg_info	*fw_crash_mem;
	u32			fw_crash_len;
	struct bnxt_bs_trace_info bs_trace[BNXT_TRACE_MAX];
};

#define BNXT_NUM_RX_RING_STATS			8
#define BNXT_NUM_TX_RING_STATS			8
#define BNXT_NUM_TPA_RING_STATS			4
#define BNXT_NUM_TPA_RING_STATS_P5		5
#define BNXT_NUM_TPA_RING_STATS_P7		6

#define BNXT_RING_STATS_SIZE_P5					\
	((BNXT_NUM_RX_RING_STATS + BNXT_NUM_TX_RING_STATS +	\
	  BNXT_NUM_TPA_RING_STATS_P5) * 8)

#define BNXT_RING_STATS_SIZE_P7					\
	((BNXT_NUM_RX_RING_STATS + BNXT_NUM_TX_RING_STATS +	\
	  BNXT_NUM_TPA_RING_STATS_P7) * 8)

#define BNXT_GET_RING_STATS64(sw, counter)		\
	(*((sw) + offsetof(struct ctx_hw_stats, counter) / 8))

#define BNXT_GET_RX_PORT_STATS64(sw, counter)		\
	(*((sw) + offsetof(struct rx_port_stats, counter) / 8))

#define BNXT_GET_TX_PORT_STATS64(sw, counter)		\
	(*((sw) + offsetof(struct tx_port_stats, counter) / 8))

#define BNXT_PORT_STATS_SIZE				\
	(sizeof(struct rx_port_stats) + sizeof(struct tx_port_stats) + 1024)

#define BNXT_TX_PORT_STATS_BYTE_OFFSET			\
	(sizeof(struct rx_port_stats) + 512)

#define BNXT_RX_STATS_OFFSET(counter)			\
	(offsetof(struct rx_port_stats, counter) / 8)

#define BNXT_TX_STATS_OFFSET(counter)			\
	((offsetof(struct tx_port_stats, counter) +	\
	  BNXT_TX_PORT_STATS_BYTE_OFFSET) / 8)

#define BNXT_RX_STATS_EXT_OFFSET(counter)		\
	(offsetof(struct rx_port_stats_ext, counter) / 8)

#define BNXT_RX_STATS_EXT_NUM_LEGACY                   \
	BNXT_RX_STATS_EXT_OFFSET(rx_fec_corrected_blocks)

#define BNXT_TX_STATS_EXT_OFFSET(counter)		\
	(offsetof(struct tx_port_stats_ext, counter) / 8)

#define BNXT_HW_FEATURE_VLAN_ALL_RX				\
	(NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_STAG_RX)
#define BNXT_HW_FEATURE_VLAN_ALL_TX				\
	(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX)

#define I2C_DEV_ADDR_A0				0xa0
#define I2C_DEV_ADDR_A2				0xa2
#define SFF_DIAG_SUPPORT_OFFSET			0x5c
#define SFF_MODULE_ID_SFP			0x3
#define SFF_MODULE_ID_QSFP			0xc
#define SFF_MODULE_ID_QSFP_PLUS			0xd
#define SFF_MODULE_ID_QSFP28			0x11
#define BNXT_MAX_PHY_I2C_RESP_SIZE		64

#define BNXT_HDS_THRESHOLD_MAX			1023

static inline u32 bnxt_tx_avail(struct bnxt *bp,
				const struct bnxt_tx_ring_info *txr)
{
	u32 used = READ_ONCE(txr->tx_prod) - READ_ONCE(txr->tx_cons);

	return bp->tx_ring_size - (used & bp->tx_ring_mask);
}

static inline void bnxt_writeq(struct bnxt *bp, u64 val,
			       volatile void __iomem *addr)
{
#if BITS_PER_LONG == 32
	spin_lock(&bp->db_lock);
	lo_hi_writeq(val, addr);
	spin_unlock(&bp->db_lock);
#else
	writeq(val, addr);
#endif
}

static inline void bnxt_writeq_relaxed(struct bnxt *bp, u64 val,
				       volatile void __iomem *addr)
{
#if BITS_PER_LONG == 32
	spin_lock(&bp->db_lock);
	lo_hi_writeq_relaxed(val, addr);
	spin_unlock(&bp->db_lock);
#else
	writeq_relaxed(val, addr);
#endif
}

/* For TX and RX ring doorbells with no ordering guarantee*/
static inline void bnxt_db_write_relaxed(struct bnxt *bp,
					 struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		bnxt_writeq_relaxed(bp, db->db_key64 | DB_RING_IDX(db, idx),
				    db->doorbell);
	} else {
		u32 db_val = db->db_key32 | DB_RING_IDX(db, idx);

		writel_relaxed(db_val, db->doorbell);
		if (bp->flags & BNXT_FLAG_DOUBLE_DB)
			writel_relaxed(db_val, db->doorbell);
	}
}

/* For TX and RX ring doorbells */
static inline void bnxt_db_write(struct bnxt *bp, struct bnxt_db_info *db,
				 u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		bnxt_writeq(bp, db->db_key64 | DB_RING_IDX(db, idx),
			    db->doorbell);
	} else {
		u32 db_val = db->db_key32 | DB_RING_IDX(db, idx);

		writel(db_val, db->doorbell);
		if (bp->flags & BNXT_FLAG_DOUBLE_DB)
			writel(db_val, db->doorbell);
	}
}

/* Must hold rtnl_lock */
static inline bool bnxt_sriov_cfg(struct bnxt *bp)
{
#if defined(CONFIG_BNXT_SRIOV)
	return BNXT_PF(bp) && (bp->pf.active_vfs || bp->sriov_cfg);
#else
	return false;
#endif
}

extern const u16 bnxt_bstore_to_trace[];
extern const u16 bnxt_lhint_arr[];

int bnxt_alloc_rx_data(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
		       u16 prod, gfp_t gfp);
void bnxt_reuse_rx_data(struct bnxt_rx_ring_info *rxr, u16 cons, void *data);
u32 bnxt_fw_health_readl(struct bnxt *bp, int reg_idx);
bool bnxt_bs_trace_avail(struct bnxt *bp, u16 type);
void bnxt_set_tpa_flags(struct bnxt *bp);
void bnxt_set_ring_params(struct bnxt *);
void bnxt_set_rx_skb_mode(struct bnxt *bp, bool page_mode);
void bnxt_insert_usr_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr);
void bnxt_del_one_usr_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr);
int bnxt_hwrm_func_drv_rgtr(struct bnxt *bp, unsigned long *bmap,
			    int bmap_size, bool async_only);
int bnxt_hwrm_func_drv_unrgtr(struct bnxt *bp);
void bnxt_del_l2_filter(struct bnxt *bp, struct bnxt_l2_filter *fltr);
struct bnxt_l2_filter *bnxt_alloc_new_l2_filter(struct bnxt *bp,
						struct bnxt_l2_key *key,
						u16 flags);
int bnxt_hwrm_l2_filter_free(struct bnxt *bp, struct bnxt_l2_filter *fltr);
int bnxt_hwrm_l2_filter_alloc(struct bnxt *bp, struct bnxt_l2_filter *fltr);
int bnxt_hwrm_cfa_ntuple_filter_free(struct bnxt *bp,
				     struct bnxt_ntuple_filter *fltr);
int bnxt_hwrm_cfa_ntuple_filter_alloc(struct bnxt *bp,
				      struct bnxt_ntuple_filter *fltr);
int bnxt_hwrm_vnic_set_tpa(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			   u32 tpa_flags);
void bnxt_fill_ipv6_mask(__be32 mask[4]);
void bnxt_set_dflt_rss_indir_tbl(struct bnxt *bp,
				 struct ethtool_rxfh_context *rss_ctx);
int bnxt_get_nr_rss_ctxs(struct bnxt *bp, int rx_rings);
int bnxt_hwrm_vnic_cfg(struct bnxt *bp, struct bnxt_vnic_info *vnic);
int bnxt_hwrm_vnic_alloc(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			 unsigned int start_rx_ring_idx,
			 unsigned int nr_rings);
int __bnxt_hwrm_get_tx_rings(struct bnxt *bp, u16 fid, int *tx_rings);
int bnxt_nq_rings_in_use(struct bnxt *bp);
int bnxt_hwrm_set_coal(struct bnxt *);
size_t bnxt_copy_ctx_mem(struct bnxt *bp, struct bnxt_ctx_mem_type *ctxm,
			 void *buf, size_t offset);
void bnxt_free_ctx_mem(struct bnxt *bp, bool force);
int bnxt_num_tx_to_cp(struct bnxt *bp, int tx);
unsigned int bnxt_get_max_func_stat_ctxs(struct bnxt *bp);
unsigned int bnxt_get_avail_stat_ctxs_for_en(struct bnxt *bp);
unsigned int bnxt_get_max_func_cp_rings(struct bnxt *bp);
unsigned int bnxt_get_avail_cp_rings_for_en(struct bnxt *bp);
int bnxt_reserve_rings(struct bnxt *bp, bool irq_re_init);
void bnxt_tx_disable(struct bnxt *bp);
void bnxt_tx_enable(struct bnxt *bp);
void bnxt_sched_reset_txr(struct bnxt *bp, struct bnxt_tx_ring_info *txr,
			  u16 curr);
void bnxt_report_link(struct bnxt *bp);
int bnxt_update_link(struct bnxt *bp, bool chng_link_state);
int bnxt_hwrm_set_pause(struct bnxt *);
int bnxt_hwrm_set_link_setting(struct bnxt *, bool, bool);
int bnxt_cancel_reservations(struct bnxt *bp, bool fw_reset);
int bnxt_hwrm_alloc_wol_fltr(struct bnxt *bp);
int bnxt_hwrm_free_wol_fltr(struct bnxt *bp);
int bnxt_hwrm_func_resc_qcaps(struct bnxt *bp, bool all);
int bnxt_hwrm_func_qcaps(struct bnxt *bp);
int bnxt_hwrm_fw_set_time(struct bnxt *);
int bnxt_hwrm_vnic_update(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			  u8 valid);
int bnxt_hwrm_vnic_rss_cfg_p5(struct bnxt *bp, struct bnxt_vnic_info *vnic);
int __bnxt_setup_vnic_p5(struct bnxt *bp, struct bnxt_vnic_info *vnic);
void bnxt_del_one_rss_ctx(struct bnxt *bp, struct bnxt_rss_ctx *rss_ctx,
			  bool all);
int bnxt_open_nic(struct bnxt *, bool, bool);
int bnxt_half_open_nic(struct bnxt *bp);
void bnxt_half_close_nic(struct bnxt *bp);
void bnxt_reenable_sriov(struct bnxt *bp);
void bnxt_close_nic(struct bnxt *, bool, bool);
void bnxt_get_ring_err_stats(struct bnxt *bp,
			     struct bnxt_total_ring_err_stats *stats);
bool bnxt_rfs_capable(struct bnxt *bp, bool new_rss_ctx);
int bnxt_dbg_hwrm_rd_reg(struct bnxt *bp, u32 reg_off, u16 num_words,
			 u32 *reg_buf);
void bnxt_fw_exception(struct bnxt *bp);
void bnxt_fw_reset(struct bnxt *bp);
int bnxt_check_rings(struct bnxt *bp, int tx, int rx, bool sh, int tcs,
		     int tx_xdp);
int bnxt_fw_init_one(struct bnxt *bp);
bool bnxt_hwrm_reset_permitted(struct bnxt *bp);
int bnxt_setup_mq_tc(struct net_device *dev, u8 tc);
struct bnxt_ntuple_filter *bnxt_lookup_ntp_filter_from_idx(struct bnxt *bp,
				struct bnxt_ntuple_filter *fltr, u32 idx);
u32 bnxt_get_ntp_filter_idx(struct bnxt *bp, struct flow_keys *fkeys,
			    const struct sk_buff *skb);
int bnxt_insert_ntp_filter(struct bnxt *bp, struct bnxt_ntuple_filter *fltr,
			   u32 idx);
void bnxt_del_ntp_filter(struct bnxt *bp, struct bnxt_ntuple_filter *fltr);
int bnxt_get_max_rings(struct bnxt *, int *, int *, bool);
int bnxt_restore_pf_fw_resources(struct bnxt *bp);
int bnxt_get_port_parent_id(struct net_device *dev,
			    struct netdev_phys_item_id *ppid);
void bnxt_dim_work(struct work_struct *work);
int bnxt_hwrm_set_ring_coal(struct bnxt *bp, struct bnxt_napi *bnapi);
void bnxt_print_device_info(struct bnxt *bp);
#endif
