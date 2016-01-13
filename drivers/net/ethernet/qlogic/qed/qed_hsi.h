/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_HSI_H
#define _QED_HSI_H

#include <linux/types.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/eth_common.h>

struct qed_hwfn;
struct qed_ptt;
/********************************/
/* Add include to common target */
/********************************/

/* opcodes for the event ring */
enum common_event_opcode {
	COMMON_EVENT_PF_START,
	COMMON_EVENT_PF_STOP,
	COMMON_EVENT_RESERVED,
	COMMON_EVENT_RESERVED2,
	COMMON_EVENT_RESERVED3,
	COMMON_EVENT_RESERVED4,
	COMMON_EVENT_RESERVED5,
	MAX_COMMON_EVENT_OPCODE
};

/* Common Ramrod Command IDs */
enum common_ramrod_cmd_id {
	COMMON_RAMROD_UNUSED,
	COMMON_RAMROD_PF_START /* PF Function Start Ramrod */,
	COMMON_RAMROD_PF_STOP /* PF Function Stop Ramrod */,
	COMMON_RAMROD_RESERVED,
	COMMON_RAMROD_RESERVED2,
	COMMON_RAMROD_RESERVED3,
	MAX_COMMON_RAMROD_CMD_ID
};

/* The core storm context for the Ystorm */
struct ystorm_core_conn_st_ctx {
	__le32 reserved[4];
};

/* The core storm context for the Pstorm */
struct pstorm_core_conn_st_ctx {
	__le32 reserved[4];
};

/* Core Slowpath Connection storm context of Xstorm */
struct xstorm_core_conn_st_ctx {
	__le32		spq_base_lo /* SPQ Ring Base Address low dword */;
	__le32		spq_base_hi /* SPQ Ring Base Address high dword */;
	struct regpair	consolid_base_addr;
	__le16		spq_cons /* SPQ Ring Consumer */;
	__le16		consolid_cons /* Consolidation Ring Consumer */;
	__le32		reserved0[55] /* Pad to 15 cycles */;
};

struct xstorm_core_conn_ag_ctx {
	u8	reserved0 /* cdu_validation */;
	u8	core_state /* state */;
	u8	flags0;
#define XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM0_MASK         0x1
#define XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT        0
#define XSTORM_CORE_CONN_AG_CTX_RESERVED1_MASK            0x1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED1_SHIFT           1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED2_MASK            0x1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED2_SHIFT           2
#define XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM3_MASK         0x1
#define XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT        3
#define XSTORM_CORE_CONN_AG_CTX_RESERVED3_MASK            0x1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED3_SHIFT           4
#define XSTORM_CORE_CONN_AG_CTX_RESERVED4_MASK            0x1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED4_SHIFT           5
#define XSTORM_CORE_CONN_AG_CTX_RESERVED5_MASK            0x1   /* bit6 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED5_SHIFT           6
#define XSTORM_CORE_CONN_AG_CTX_RESERVED6_MASK            0x1   /* bit7 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED6_SHIFT           7
	u8 flags1;
#define XSTORM_CORE_CONN_AG_CTX_RESERVED7_MASK            0x1   /* bit8 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED7_SHIFT           0
#define XSTORM_CORE_CONN_AG_CTX_RESERVED8_MASK            0x1   /* bit9 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED8_SHIFT           1
#define XSTORM_CORE_CONN_AG_CTX_RESERVED9_MASK            0x1   /* bit10 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED9_SHIFT           2
#define XSTORM_CORE_CONN_AG_CTX_BIT11_MASK                0x1   /* bit11 */
#define XSTORM_CORE_CONN_AG_CTX_BIT11_SHIFT               3
#define XSTORM_CORE_CONN_AG_CTX_BIT12_MASK                0x1   /* bit12 */
#define XSTORM_CORE_CONN_AG_CTX_BIT12_SHIFT               4
#define XSTORM_CORE_CONN_AG_CTX_BIT13_MASK                0x1   /* bit13 */
#define XSTORM_CORE_CONN_AG_CTX_BIT13_SHIFT               5
#define XSTORM_CORE_CONN_AG_CTX_TX_RULE_ACTIVE_MASK       0x1   /* bit14 */
#define XSTORM_CORE_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT      6
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_ACTIVE_MASK         0x1   /* bit15 */
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT        7
	u8 flags2;
#define XSTORM_CORE_CONN_AG_CTX_CF0_MASK                  0x3   /* timer0cf */
#define XSTORM_CORE_CONN_AG_CTX_CF0_SHIFT                 0
#define XSTORM_CORE_CONN_AG_CTX_CF1_MASK                  0x3   /* timer1cf */
#define XSTORM_CORE_CONN_AG_CTX_CF1_SHIFT                 2
#define XSTORM_CORE_CONN_AG_CTX_CF2_MASK                  0x3   /* timer2cf */
#define XSTORM_CORE_CONN_AG_CTX_CF2_SHIFT                 4
#define XSTORM_CORE_CONN_AG_CTX_CF3_MASK                  0x3
#define XSTORM_CORE_CONN_AG_CTX_CF3_SHIFT                 6
	u8 flags3;
#define XSTORM_CORE_CONN_AG_CTX_CF4_MASK                  0x3   /* cf4 */
#define XSTORM_CORE_CONN_AG_CTX_CF4_SHIFT                 0
#define XSTORM_CORE_CONN_AG_CTX_CF5_MASK                  0x3   /* cf5 */
#define XSTORM_CORE_CONN_AG_CTX_CF5_SHIFT                 2
#define XSTORM_CORE_CONN_AG_CTX_CF6_MASK                  0x3   /* cf6 */
#define XSTORM_CORE_CONN_AG_CTX_CF6_SHIFT                 4
#define XSTORM_CORE_CONN_AG_CTX_CF7_MASK                  0x3   /* cf7 */
#define XSTORM_CORE_CONN_AG_CTX_CF7_SHIFT                 6
	u8 flags4;
#define XSTORM_CORE_CONN_AG_CTX_CF8_MASK                  0x3   /* cf8 */
#define XSTORM_CORE_CONN_AG_CTX_CF8_SHIFT                 0
#define XSTORM_CORE_CONN_AG_CTX_CF9_MASK                  0x3   /* cf9 */
#define XSTORM_CORE_CONN_AG_CTX_CF9_SHIFT                 2
#define XSTORM_CORE_CONN_AG_CTX_CF10_MASK                 0x3   /* cf10 */
#define XSTORM_CORE_CONN_AG_CTX_CF10_SHIFT                4
#define XSTORM_CORE_CONN_AG_CTX_CF11_MASK                 0x3   /* cf11 */
#define XSTORM_CORE_CONN_AG_CTX_CF11_SHIFT                6
	u8 flags5;
#define XSTORM_CORE_CONN_AG_CTX_CF12_MASK                 0x3   /* cf12 */
#define XSTORM_CORE_CONN_AG_CTX_CF12_SHIFT                0
#define XSTORM_CORE_CONN_AG_CTX_CF13_MASK                 0x3   /* cf13 */
#define XSTORM_CORE_CONN_AG_CTX_CF13_SHIFT                2
#define XSTORM_CORE_CONN_AG_CTX_CF14_MASK                 0x3   /* cf14 */
#define XSTORM_CORE_CONN_AG_CTX_CF14_SHIFT                4
#define XSTORM_CORE_CONN_AG_CTX_CF15_MASK                 0x3   /* cf15 */
#define XSTORM_CORE_CONN_AG_CTX_CF15_SHIFT                6
	u8 flags6;
#define XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_MASK     0x3   /* cf16 */
#define XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_SHIFT    0
#define XSTORM_CORE_CONN_AG_CTX_CF17_MASK                 0x3
#define XSTORM_CORE_CONN_AG_CTX_CF17_SHIFT                2
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_MASK                0x3   /* cf18 */
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_SHIFT               4
#define XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_MASK         0x3   /* cf19 */
#define XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_SHIFT        6
	u8 flags7;
#define XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_MASK             0x3   /* cf20 */
#define XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_SHIFT            0
#define XSTORM_CORE_CONN_AG_CTX_RESERVED10_MASK           0x3   /* cf21 */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED10_SHIFT          2
#define XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_MASK            0x3   /* cf22 */
#define XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_SHIFT           4
#define XSTORM_CORE_CONN_AG_CTX_CF0EN_MASK                0x1   /* cf0en */
#define XSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT               6
#define XSTORM_CORE_CONN_AG_CTX_CF1EN_MASK                0x1   /* cf1en */
#define XSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT               7
	u8 flags8;
#define XSTORM_CORE_CONN_AG_CTX_CF2EN_MASK                0x1   /* cf2en */
#define XSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT               0
#define XSTORM_CORE_CONN_AG_CTX_CF3EN_MASK                0x1   /* cf3en */
#define XSTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT               1
#define XSTORM_CORE_CONN_AG_CTX_CF4EN_MASK                0x1   /* cf4en */
#define XSTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT               2
#define XSTORM_CORE_CONN_AG_CTX_CF5EN_MASK                0x1   /* cf5en */
#define XSTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT               3
#define XSTORM_CORE_CONN_AG_CTX_CF6EN_MASK                0x1   /* cf6en */
#define XSTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT               4
#define XSTORM_CORE_CONN_AG_CTX_CF7EN_MASK                0x1   /* cf7en */
#define XSTORM_CORE_CONN_AG_CTX_CF7EN_SHIFT               5
#define XSTORM_CORE_CONN_AG_CTX_CF8EN_MASK                0x1   /* cf8en */
#define XSTORM_CORE_CONN_AG_CTX_CF8EN_SHIFT               6
#define XSTORM_CORE_CONN_AG_CTX_CF9EN_MASK                0x1   /* cf9en */
#define XSTORM_CORE_CONN_AG_CTX_CF9EN_SHIFT               7
	u8 flags9;
#define XSTORM_CORE_CONN_AG_CTX_CF10EN_MASK               0x1   /* cf10en */
#define XSTORM_CORE_CONN_AG_CTX_CF10EN_SHIFT              0
#define XSTORM_CORE_CONN_AG_CTX_CF11EN_MASK               0x1   /* cf11en */
#define XSTORM_CORE_CONN_AG_CTX_CF11EN_SHIFT              1
#define XSTORM_CORE_CONN_AG_CTX_CF12EN_MASK               0x1   /* cf12en */
#define XSTORM_CORE_CONN_AG_CTX_CF12EN_SHIFT              2
#define XSTORM_CORE_CONN_AG_CTX_CF13EN_MASK               0x1   /* cf13en */
#define XSTORM_CORE_CONN_AG_CTX_CF13EN_SHIFT              3
#define XSTORM_CORE_CONN_AG_CTX_CF14EN_MASK               0x1   /* cf14en */
#define XSTORM_CORE_CONN_AG_CTX_CF14EN_SHIFT              4
#define XSTORM_CORE_CONN_AG_CTX_CF15EN_MASK               0x1   /* cf15en */
#define XSTORM_CORE_CONN_AG_CTX_CF15EN_SHIFT              5
#define XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_EN_MASK  0x1   /* cf16en */
#define XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_EN_SHIFT 6
#define XSTORM_CORE_CONN_AG_CTX_CF17EN_MASK               0x1
#define XSTORM_CORE_CONN_AG_CTX_CF17EN_SHIFT              7
	u8 flags10;
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_EN_MASK             0x1   /* cf18en */
#define XSTORM_CORE_CONN_AG_CTX_DQ_CF_EN_SHIFT            0
#define XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_EN_MASK      0x1   /* cf19en */
#define XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT     1
#define XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_EN_MASK          0x1   /* cf20en */
#define XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT         2
#define XSTORM_CORE_CONN_AG_CTX_RESERVED11_MASK           0x1   /* cf21en */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED11_SHIFT          3
#define XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_EN_MASK         0x1   /* cf22en */
#define XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT        4
#define XSTORM_CORE_CONN_AG_CTX_CF23EN_MASK               0x1   /* cf23en */
#define XSTORM_CORE_CONN_AG_CTX_CF23EN_SHIFT              5
#define XSTORM_CORE_CONN_AG_CTX_RESERVED12_MASK           0x1   /* rule0en */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED12_SHIFT          6
#define XSTORM_CORE_CONN_AG_CTX_RESERVED13_MASK           0x1   /* rule1en */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED13_SHIFT          7
	u8 flags11;
#define XSTORM_CORE_CONN_AG_CTX_RESERVED14_MASK           0x1   /* rule2en */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED14_SHIFT          0
#define XSTORM_CORE_CONN_AG_CTX_RESERVED15_MASK           0x1   /* rule3en */
#define XSTORM_CORE_CONN_AG_CTX_RESERVED15_SHIFT          1
#define XSTORM_CORE_CONN_AG_CTX_TX_DEC_RULE_EN_MASK       0x1   /* rule4en */
#define XSTORM_CORE_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT      2
#define XSTORM_CORE_CONN_AG_CTX_RULE5EN_MASK              0x1   /* rule5en */
#define XSTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT             3
#define XSTORM_CORE_CONN_AG_CTX_RULE6EN_MASK              0x1   /* rule6en */
#define XSTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT             4
#define XSTORM_CORE_CONN_AG_CTX_RULE7EN_MASK              0x1   /* rule7en */
#define XSTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT             5
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED1_MASK         0x1   /* rule8en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED1_SHIFT        6
#define XSTORM_CORE_CONN_AG_CTX_RULE9EN_MASK              0x1   /* rule9en */
#define XSTORM_CORE_CONN_AG_CTX_RULE9EN_SHIFT             7
	u8 flags12;
#define XSTORM_CORE_CONN_AG_CTX_RULE10EN_MASK             0x1   /* rule10en */
#define XSTORM_CORE_CONN_AG_CTX_RULE10EN_SHIFT            0
#define XSTORM_CORE_CONN_AG_CTX_RULE11EN_MASK             0x1   /* rule11en */
#define XSTORM_CORE_CONN_AG_CTX_RULE11EN_SHIFT            1
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED2_MASK         0x1   /* rule12en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED2_SHIFT        2
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED3_MASK         0x1   /* rule13en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED3_SHIFT        3
#define XSTORM_CORE_CONN_AG_CTX_RULE14EN_MASK             0x1   /* rule14en */
#define XSTORM_CORE_CONN_AG_CTX_RULE14EN_SHIFT            4
#define XSTORM_CORE_CONN_AG_CTX_RULE15EN_MASK             0x1   /* rule15en */
#define XSTORM_CORE_CONN_AG_CTX_RULE15EN_SHIFT            5
#define XSTORM_CORE_CONN_AG_CTX_RULE16EN_MASK             0x1   /* rule16en */
#define XSTORM_CORE_CONN_AG_CTX_RULE16EN_SHIFT            6
#define XSTORM_CORE_CONN_AG_CTX_RULE17EN_MASK             0x1   /* rule17en */
#define XSTORM_CORE_CONN_AG_CTX_RULE17EN_SHIFT            7
	u8 flags13;
#define XSTORM_CORE_CONN_AG_CTX_RULE18EN_MASK             0x1   /* rule18en */
#define XSTORM_CORE_CONN_AG_CTX_RULE18EN_SHIFT            0
#define XSTORM_CORE_CONN_AG_CTX_RULE19EN_MASK             0x1   /* rule19en */
#define XSTORM_CORE_CONN_AG_CTX_RULE19EN_SHIFT            1
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED4_MASK         0x1   /* rule20en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED4_SHIFT        2
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED5_MASK         0x1   /* rule21en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED5_SHIFT        3
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED6_MASK         0x1   /* rule22en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED6_SHIFT        4
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED7_MASK         0x1   /* rule23en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED7_SHIFT        5
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED8_MASK         0x1   /* rule24en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED8_SHIFT        6
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED9_MASK         0x1   /* rule25en */
#define XSTORM_CORE_CONN_AG_CTX_A0_RESERVED9_SHIFT        7
	u8 flags14;
#define XSTORM_CORE_CONN_AG_CTX_BIT16_MASK                0x1   /* bit16 */
#define XSTORM_CORE_CONN_AG_CTX_BIT16_SHIFT               0
#define XSTORM_CORE_CONN_AG_CTX_BIT17_MASK                0x1   /* bit17 */
#define XSTORM_CORE_CONN_AG_CTX_BIT17_SHIFT               1
#define XSTORM_CORE_CONN_AG_CTX_BIT18_MASK                0x1   /* bit18 */
#define XSTORM_CORE_CONN_AG_CTX_BIT18_SHIFT               2
#define XSTORM_CORE_CONN_AG_CTX_BIT19_MASK                0x1   /* bit19 */
#define XSTORM_CORE_CONN_AG_CTX_BIT19_SHIFT               3
#define XSTORM_CORE_CONN_AG_CTX_BIT20_MASK                0x1   /* bit20 */
#define XSTORM_CORE_CONN_AG_CTX_BIT20_SHIFT               4
#define XSTORM_CORE_CONN_AG_CTX_BIT21_MASK                0x1   /* bit21 */
#define XSTORM_CORE_CONN_AG_CTX_BIT21_SHIFT               5
#define XSTORM_CORE_CONN_AG_CTX_CF23_MASK                 0x3   /* cf23 */
#define XSTORM_CORE_CONN_AG_CTX_CF23_SHIFT                6
	u8	byte2 /* byte2 */;
	__le16	physical_q0 /* physical_q0 */;
	__le16	consolid_prod /* physical_q1 */;
	__le16	reserved16 /* physical_q2 */;
	__le16	tx_bd_cons /* word3 */;
	__le16	tx_bd_or_spq_prod /* word4 */;
	__le16	word5 /* word5 */;
	__le16	conn_dpi /* conn_dpi */;
	u8	byte3 /* byte3 */;
	u8	byte4 /* byte4 */;
	u8	byte5 /* byte5 */;
	u8	byte6 /* byte6 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le32	reg4 /* reg4 */;
	__le32	reg5 /* cf_array0 */;
	__le32	reg6 /* cf_array1 */;
	__le16	word7 /* word7 */;
	__le16	word8 /* word8 */;
	__le16	word9 /* word9 */;
	__le16	word10 /* word10 */;
	__le32	reg7 /* reg7 */;
	__le32	reg8 /* reg8 */;
	__le32	reg9 /* reg9 */;
	u8	byte7 /* byte7 */;
	u8	byte8 /* byte8 */;
	u8	byte9 /* byte9 */;
	u8	byte10 /* byte10 */;
	u8	byte11 /* byte11 */;
	u8	byte12 /* byte12 */;
	u8	byte13 /* byte13 */;
	u8	byte14 /* byte14 */;
	u8	byte15 /* byte15 */;
	u8	byte16 /* byte16 */;
	__le16	word11 /* word11 */;
	__le32	reg10 /* reg10 */;
	__le32	reg11 /* reg11 */;
	__le32	reg12 /* reg12 */;
	__le32	reg13 /* reg13 */;
	__le32	reg14 /* reg14 */;
	__le32	reg15 /* reg15 */;
	__le32	reg16 /* reg16 */;
	__le32	reg17 /* reg17 */;
	__le32	reg18 /* reg18 */;
	__le32	reg19 /* reg19 */;
	__le16	word12 /* word12 */;
	__le16	word13 /* word13 */;
	__le16	word14 /* word14 */;
	__le16	word15 /* word15 */;
};

/* The core storm context for the Mstorm */
struct mstorm_core_conn_st_ctx {
	__le32 reserved[24];
};

/* The core storm context for the Ustorm */
struct ustorm_core_conn_st_ctx {
	__le32 reserved[4];
};

/* core connection context */
struct core_conn_context {
	struct ystorm_core_conn_st_ctx	ystorm_st_context;
	struct regpair			ystorm_st_padding[2] /* padding */;
	struct pstorm_core_conn_st_ctx	pstorm_st_context;
	struct regpair			pstorm_st_padding[2];
	struct xstorm_core_conn_st_ctx	xstorm_st_context;
	struct xstorm_core_conn_ag_ctx	xstorm_ag_context;
	struct mstorm_core_conn_st_ctx	mstorm_st_context;
	struct regpair			mstorm_st_padding[2];
	struct ustorm_core_conn_st_ctx	ustorm_st_context;
	struct regpair			ustorm_st_padding[2] /* padding */;
};

struct eth_mstorm_per_queue_stat {
	struct regpair  ttl0_discard;
	struct regpair  packet_too_big_discard;
	struct regpair  no_buff_discard;
	struct regpair  not_active_discard;
	struct regpair  tpa_coalesced_pkts;
	struct regpair  tpa_coalesced_events;
	struct regpair  tpa_aborts_num;
	struct regpair  tpa_coalesced_bytes;
};

struct eth_pstorm_per_queue_stat {
	struct regpair  sent_ucast_bytes;
	struct regpair  sent_mcast_bytes;
	struct regpair  sent_bcast_bytes;
	struct regpair  sent_ucast_pkts;
	struct regpair  sent_mcast_pkts;
	struct regpair  sent_bcast_pkts;
	struct regpair  error_drop_pkts;
};

struct eth_ustorm_per_queue_stat {
	struct regpair  rcv_ucast_bytes;
	struct regpair  rcv_mcast_bytes;
	struct regpair  rcv_bcast_bytes;
	struct regpair  rcv_ucast_pkts;
	struct regpair  rcv_mcast_pkts;
	struct regpair  rcv_bcast_pkts;
};

/* Event Ring Next Page Address */
struct event_ring_next_addr {
	struct regpair	addr /* Next Page Address */;
	__le32		reserved[2] /* Reserved */;
};

union event_ring_element {
	struct event_ring_entry		entry /* Event Ring Entry */;
	struct event_ring_next_addr	next_addr;
};

enum personality_type {
	PERSONALITY_RESERVED,
	PERSONALITY_RESERVED2,
	PERSONALITY_RDMA_AND_ETH /* Roce or Iwarp */,
	PERSONALITY_RESERVED3,
	PERSONALITY_ETH /* Ethernet */,
	PERSONALITY_RESERVED4,
	MAX_PERSONALITY_TYPE
};

struct pf_start_tunnel_config {
	u8	set_vxlan_udp_port_flg;
	u8	set_geneve_udp_port_flg;
	u8	tx_enable_vxlan /* If set, enable VXLAN tunnel in TX path. */;
	u8	tx_enable_l2geneve;
	u8	tx_enable_ipgeneve;
	u8	tx_enable_l2gre /* If set, enable l2 GRE tunnel in TX path. */;
	u8	tx_enable_ipgre /* If set, enable IP GRE tunnel in TX path. */;
	u8	tunnel_clss_vxlan /* Classification scheme for VXLAN tunnel. */;
	u8	tunnel_clss_l2geneve;
	u8	tunnel_clss_ipgeneve;
	u8	tunnel_clss_l2gre;
	u8	tunnel_clss_ipgre;
	__le16	vxlan_udp_port /* VXLAN tunnel UDP destination port. */;
	__le16	geneve_udp_port /* GENEVE tunnel UDP destination port. */;
};

/* Ramrod data for PF start ramrod */
struct pf_start_ramrod_data {
	struct regpair			event_ring_pbl_addr;
	struct regpair			consolid_q_pbl_addr;
	struct pf_start_tunnel_config	tunnel_config;
	__le16				event_ring_sb_id;
	u8				base_vf_id;
	u8				num_vfs;
	u8				event_ring_num_pages;
	u8				event_ring_sb_index;
	u8				path_id;
	u8				warning_as_error;
	u8				dont_log_ramrods;
	u8				personality;
	__le16				log_type_mask;
	u8				mf_mode /* Multi function mode */;
	u8				integ_phase /* Integration phase */;
	u8				allow_npar_tx_switching;
	u8				inner_to_outer_pri_map[8];
	u8				pri_map_valid;
	u32				outer_tag;
	u8				reserved0[4];
};

enum ports_mode {
	ENGX2_PORTX1 /* 2 engines x 1 port */,
	ENGX2_PORTX2 /* 2 engines x 2 ports */,
	ENGX1_PORTX1 /* 1 engine  x 1 port */,
	ENGX1_PORTX2 /* 1 engine  x 2 ports */,
	ENGX1_PORTX4 /* 1 engine  x 4 ports */,
	MAX_PORTS_MODE
};

/* Ramrod Header of SPQE */
struct ramrod_header {
	__le32	cid /* Slowpath Connection CID */;
	u8	cmd_id /* Ramrod Cmd (Per Protocol Type) */;
	u8	protocol_id /* Ramrod Protocol ID */;
	__le16	echo /* Ramrod echo */;
};

/* Slowpath Element (SPQE) */
struct slow_path_element {
	struct ramrod_header	hdr /* Ramrod Header */;
	struct regpair		data_ptr;
};

struct tstorm_per_port_stat {
	struct regpair	trunc_error_discard;
	struct regpair	mac_error_discard;
	struct regpair	mftag_filter_discard;
	struct regpair	eth_mac_filter_discard;
	struct regpair	ll2_mac_filter_discard;
	struct regpair	ll2_conn_disabled_discard;
	struct regpair	iscsi_irregular_pkt;
	struct regpair	fcoe_irregular_pkt;
	struct regpair	roce_irregular_pkt;
	struct regpair	eth_irregular_pkt;
	struct regpair	toe_irregular_pkt;
	struct regpair	preroce_irregular_pkt;
};

struct atten_status_block {
	__le32	atten_bits;
	__le32	atten_ack;
	__le16	reserved0;
	__le16	sb_index /* status block running index */;
	__le32	reserved1;
};

enum block_addr {
	GRCBASE_GRC		= 0x50000,
	GRCBASE_MISCS		= 0x9000,
	GRCBASE_MISC		= 0x8000,
	GRCBASE_DBU		= 0xa000,
	GRCBASE_PGLUE_B		= 0x2a8000,
	GRCBASE_CNIG		= 0x218000,
	GRCBASE_CPMU		= 0x30000,
	GRCBASE_NCSI		= 0x40000,
	GRCBASE_OPTE		= 0x53000,
	GRCBASE_BMB		= 0x540000,
	GRCBASE_PCIE		= 0x54000,
	GRCBASE_MCP		= 0xe00000,
	GRCBASE_MCP2		= 0x52000,
	GRCBASE_PSWHST		= 0x2a0000,
	GRCBASE_PSWHST2		= 0x29e000,
	GRCBASE_PSWRD		= 0x29c000,
	GRCBASE_PSWRD2		= 0x29d000,
	GRCBASE_PSWWR		= 0x29a000,
	GRCBASE_PSWWR2		= 0x29b000,
	GRCBASE_PSWRQ		= 0x280000,
	GRCBASE_PSWRQ2		= 0x240000,
	GRCBASE_PGLCS		= 0x0,
	GRCBASE_PTU		= 0x560000,
	GRCBASE_DMAE		= 0xc000,
	GRCBASE_TCM		= 0x1180000,
	GRCBASE_MCM		= 0x1200000,
	GRCBASE_UCM		= 0x1280000,
	GRCBASE_XCM		= 0x1000000,
	GRCBASE_YCM		= 0x1080000,
	GRCBASE_PCM		= 0x1100000,
	GRCBASE_QM		= 0x2f0000,
	GRCBASE_TM		= 0x2c0000,
	GRCBASE_DORQ		= 0x100000,
	GRCBASE_BRB		= 0x340000,
	GRCBASE_SRC		= 0x238000,
	GRCBASE_PRS		= 0x1f0000,
	GRCBASE_TSDM		= 0xfb0000,
	GRCBASE_MSDM		= 0xfc0000,
	GRCBASE_USDM		= 0xfd0000,
	GRCBASE_XSDM		= 0xf80000,
	GRCBASE_YSDM		= 0xf90000,
	GRCBASE_PSDM		= 0xfa0000,
	GRCBASE_TSEM		= 0x1700000,
	GRCBASE_MSEM		= 0x1800000,
	GRCBASE_USEM		= 0x1900000,
	GRCBASE_XSEM		= 0x1400000,
	GRCBASE_YSEM		= 0x1500000,
	GRCBASE_PSEM		= 0x1600000,
	GRCBASE_RSS		= 0x238800,
	GRCBASE_TMLD		= 0x4d0000,
	GRCBASE_MULD		= 0x4e0000,
	GRCBASE_YULD		= 0x4c8000,
	GRCBASE_XYLD		= 0x4c0000,
	GRCBASE_PRM		= 0x230000,
	GRCBASE_PBF_PB1		= 0xda0000,
	GRCBASE_PBF_PB2		= 0xda4000,
	GRCBASE_RPB		= 0x23c000,
	GRCBASE_BTB		= 0xdb0000,
	GRCBASE_PBF		= 0xd80000,
	GRCBASE_RDIF		= 0x300000,
	GRCBASE_TDIF		= 0x310000,
	GRCBASE_CDU		= 0x580000,
	GRCBASE_CCFC		= 0x2e0000,
	GRCBASE_TCFC		= 0x2d0000,
	GRCBASE_IGU		= 0x180000,
	GRCBASE_CAU		= 0x1c0000,
	GRCBASE_UMAC		= 0x51000,
	GRCBASE_XMAC		= 0x210000,
	GRCBASE_DBG		= 0x10000,
	GRCBASE_NIG		= 0x500000,
	GRCBASE_WOL		= 0x600000,
	GRCBASE_BMBN		= 0x610000,
	GRCBASE_IPC		= 0x20000,
	GRCBASE_NWM		= 0x800000,
	GRCBASE_NWS		= 0x700000,
	GRCBASE_MS		= 0x6a0000,
	GRCBASE_PHY_PCIE	= 0x618000,
	GRCBASE_MISC_AEU	= 0x8000,
	GRCBASE_BAR0_MAP	= 0x1c00000,
	MAX_BLOCK_ADDR
};

enum block_id {
	BLOCK_GRC,
	BLOCK_MISCS,
	BLOCK_MISC,
	BLOCK_DBU,
	BLOCK_PGLUE_B,
	BLOCK_CNIG,
	BLOCK_CPMU,
	BLOCK_NCSI,
	BLOCK_OPTE,
	BLOCK_BMB,
	BLOCK_PCIE,
	BLOCK_MCP,
	BLOCK_MCP2,
	BLOCK_PSWHST,
	BLOCK_PSWHST2,
	BLOCK_PSWRD,
	BLOCK_PSWRD2,
	BLOCK_PSWWR,
	BLOCK_PSWWR2,
	BLOCK_PSWRQ,
	BLOCK_PSWRQ2,
	BLOCK_PGLCS,
	BLOCK_PTU,
	BLOCK_DMAE,
	BLOCK_TCM,
	BLOCK_MCM,
	BLOCK_UCM,
	BLOCK_XCM,
	BLOCK_YCM,
	BLOCK_PCM,
	BLOCK_QM,
	BLOCK_TM,
	BLOCK_DORQ,
	BLOCK_BRB,
	BLOCK_SRC,
	BLOCK_PRS,
	BLOCK_TSDM,
	BLOCK_MSDM,
	BLOCK_USDM,
	BLOCK_XSDM,
	BLOCK_YSDM,
	BLOCK_PSDM,
	BLOCK_TSEM,
	BLOCK_MSEM,
	BLOCK_USEM,
	BLOCK_XSEM,
	BLOCK_YSEM,
	BLOCK_PSEM,
	BLOCK_RSS,
	BLOCK_TMLD,
	BLOCK_MULD,
	BLOCK_YULD,
	BLOCK_XYLD,
	BLOCK_PRM,
	BLOCK_PBF_PB1,
	BLOCK_PBF_PB2,
	BLOCK_RPB,
	BLOCK_BTB,
	BLOCK_PBF,
	BLOCK_RDIF,
	BLOCK_TDIF,
	BLOCK_CDU,
	BLOCK_CCFC,
	BLOCK_TCFC,
	BLOCK_IGU,
	BLOCK_CAU,
	BLOCK_UMAC,
	BLOCK_XMAC,
	BLOCK_DBG,
	BLOCK_NIG,
	BLOCK_WOL,
	BLOCK_BMBN,
	BLOCK_IPC,
	BLOCK_NWM,
	BLOCK_NWS,
	BLOCK_MS,
	BLOCK_PHY_PCIE,
	BLOCK_MISC_AEU,
	BLOCK_BAR0_MAP,
	MAX_BLOCK_ID
};

enum command_type_bit {
	IGU_COMMAND_TYPE_NOP	= 0,
	IGU_COMMAND_TYPE_SET	= 1,
	MAX_COMMAND_TYPE_BIT
};

struct dmae_cmd {
	__le32 opcode;
#define DMAE_CMD_SRC_MASK              0x1
#define DMAE_CMD_SRC_SHIFT             0
#define DMAE_CMD_DST_MASK              0x3
#define DMAE_CMD_DST_SHIFT             1
#define DMAE_CMD_C_DST_MASK            0x1
#define DMAE_CMD_C_DST_SHIFT           3
#define DMAE_CMD_CRC_RESET_MASK        0x1
#define DMAE_CMD_CRC_RESET_SHIFT       4
#define DMAE_CMD_SRC_ADDR_RESET_MASK   0x1
#define DMAE_CMD_SRC_ADDR_RESET_SHIFT  5
#define DMAE_CMD_DST_ADDR_RESET_MASK   0x1
#define DMAE_CMD_DST_ADDR_RESET_SHIFT  6
#define DMAE_CMD_COMP_FUNC_MASK        0x1
#define DMAE_CMD_COMP_FUNC_SHIFT       7
#define DMAE_CMD_COMP_WORD_EN_MASK     0x1
#define DMAE_CMD_COMP_WORD_EN_SHIFT    8
#define DMAE_CMD_COMP_CRC_EN_MASK      0x1
#define DMAE_CMD_COMP_CRC_EN_SHIFT     9
#define DMAE_CMD_COMP_CRC_OFFSET_MASK  0x7
#define DMAE_CMD_COMP_CRC_OFFSET_SHIFT 10
#define DMAE_CMD_RESERVED1_MASK        0x1
#define DMAE_CMD_RESERVED1_SHIFT       13
#define DMAE_CMD_ENDIANITY_MODE_MASK   0x3
#define DMAE_CMD_ENDIANITY_MODE_SHIFT  14
#define DMAE_CMD_ERR_HANDLING_MASK     0x3
#define DMAE_CMD_ERR_HANDLING_SHIFT    16
#define DMAE_CMD_PORT_ID_MASK          0x3
#define DMAE_CMD_PORT_ID_SHIFT         18
#define DMAE_CMD_SRC_PF_ID_MASK        0xF
#define DMAE_CMD_SRC_PF_ID_SHIFT       20
#define DMAE_CMD_DST_PF_ID_MASK        0xF
#define DMAE_CMD_DST_PF_ID_SHIFT       24
#define DMAE_CMD_SRC_VF_ID_VALID_MASK  0x1
#define DMAE_CMD_SRC_VF_ID_VALID_SHIFT 28
#define DMAE_CMD_DST_VF_ID_VALID_MASK  0x1
#define DMAE_CMD_DST_VF_ID_VALID_SHIFT 29
#define DMAE_CMD_RESERVED2_MASK        0x3
#define DMAE_CMD_RESERVED2_SHIFT       30
	__le32	src_addr_lo;
	__le32	src_addr_hi;
	__le32	dst_addr_lo;
	__le32	dst_addr_hi;
	__le16	length /* Length in DW */;
	__le16	opcode_b;
#define DMAE_CMD_SRC_VF_ID_MASK        0xFF     /* Source VF id */
#define DMAE_CMD_SRC_VF_ID_SHIFT       0
#define DMAE_CMD_DST_VF_ID_MASK        0xFF     /* Destination VF id */
#define DMAE_CMD_DST_VF_ID_SHIFT       8
	__le32	comp_addr_lo /* PCIe completion address low or grc address */;
	__le32	comp_addr_hi;
	__le32	comp_val /* Value to write to copmletion address */;
	__le32	crc32 /* crc16 result */;
	__le32	crc_32_c /* crc32_c result */;
	__le16	crc16 /* crc16 result */;
	__le16	crc16_c /* crc16_c result */;
	__le16	crc10 /* crc_t10 result */;
	__le16	reserved;
	__le16	xsum16 /* checksum16 result  */;
	__le16	xsum8 /* checksum8 result  */;
};

struct igu_cleanup {
	__le32 sb_id_and_flags;
#define IGU_CLEANUP_RESERVED0_MASK     0x7FFFFFF
#define IGU_CLEANUP_RESERVED0_SHIFT    0
#define IGU_CLEANUP_CLEANUP_SET_MASK   0x1 /* cleanup clear - 0, set - 1 */
#define IGU_CLEANUP_CLEANUP_SET_SHIFT  27
#define IGU_CLEANUP_CLEANUP_TYPE_MASK  0x7
#define IGU_CLEANUP_CLEANUP_TYPE_SHIFT 28
#define IGU_CLEANUP_COMMAND_TYPE_MASK  0x1
#define IGU_CLEANUP_COMMAND_TYPE_SHIFT 31
	__le32 reserved1;
};

union igu_command {
	struct igu_prod_cons_update	prod_cons_update;
	struct igu_cleanup		cleanup;
};

struct igu_command_reg_ctrl {
	__le16	opaque_fid;
	__le16	igu_command_reg_ctrl_fields;
#define IGU_COMMAND_REG_CTRL_PXP_BAR_ADDR_MASK  0xFFF
#define IGU_COMMAND_REG_CTRL_PXP_BAR_ADDR_SHIFT 0
#define IGU_COMMAND_REG_CTRL_RESERVED_MASK      0x7
#define IGU_COMMAND_REG_CTRL_RESERVED_SHIFT     12
#define IGU_COMMAND_REG_CTRL_COMMAND_TYPE_MASK  0x1
#define IGU_COMMAND_REG_CTRL_COMMAND_TYPE_SHIFT 15
};

struct igu_mapping_line {
	__le32 igu_mapping_line_fields;
#define IGU_MAPPING_LINE_VALID_MASK            0x1
#define IGU_MAPPING_LINE_VALID_SHIFT           0
#define IGU_MAPPING_LINE_VECTOR_NUMBER_MASK    0xFF
#define IGU_MAPPING_LINE_VECTOR_NUMBER_SHIFT   1
#define IGU_MAPPING_LINE_FUNCTION_NUMBER_MASK  0xFF
#define IGU_MAPPING_LINE_FUNCTION_NUMBER_SHIFT 9
#define IGU_MAPPING_LINE_PF_VALID_MASK         0x1      /* PF-1, VF-0 */
#define IGU_MAPPING_LINE_PF_VALID_SHIFT        17
#define IGU_MAPPING_LINE_IPS_GROUP_MASK        0x3F
#define IGU_MAPPING_LINE_IPS_GROUP_SHIFT       18
#define IGU_MAPPING_LINE_RESERVED_MASK         0xFF
#define IGU_MAPPING_LINE_RESERVED_SHIFT        24
};

struct igu_msix_vector {
	struct regpair	address;
	__le32		data;
	__le32		msix_vector_fields;
#define IGU_MSIX_VECTOR_MASK_BIT_MASK      0x1
#define IGU_MSIX_VECTOR_MASK_BIT_SHIFT     0
#define IGU_MSIX_VECTOR_RESERVED0_MASK     0x7FFF
#define IGU_MSIX_VECTOR_RESERVED0_SHIFT    1
#define IGU_MSIX_VECTOR_STEERING_TAG_MASK  0xFF
#define IGU_MSIX_VECTOR_STEERING_TAG_SHIFT 16
#define IGU_MSIX_VECTOR_RESERVED1_MASK     0xFF
#define IGU_MSIX_VECTOR_RESERVED1_SHIFT    24
};

enum init_modes {
	MODE_BB_A0,
	MODE_RESERVED,
	MODE_RESERVED2,
	MODE_ASIC,
	MODE_RESERVED3,
	MODE_RESERVED4,
	MODE_RESERVED5,
	MODE_SF,
	MODE_MF_SD,
	MODE_MF_SI,
	MODE_PORTS_PER_ENG_1,
	MODE_PORTS_PER_ENG_2,
	MODE_PORTS_PER_ENG_4,
	MODE_40G,
	MODE_100G,
	MODE_EAGLE_ENG1_WORKAROUND,
	MAX_INIT_MODES
};

enum init_phases {
	PHASE_ENGINE,
	PHASE_PORT,
	PHASE_PF,
	PHASE_RESERVED,
	PHASE_QM_PF,
	MAX_INIT_PHASES
};

struct mstorm_core_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define MSTORM_CORE_CONN_AG_CTX_BIT0_MASK     0x1       /* exist_in_qm0 */
#define MSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT    0
#define MSTORM_CORE_CONN_AG_CTX_BIT1_MASK     0x1       /* exist_in_qm1 */
#define MSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT    1
#define MSTORM_CORE_CONN_AG_CTX_CF0_MASK      0x3       /* cf0 */
#define MSTORM_CORE_CONN_AG_CTX_CF0_SHIFT     2
#define MSTORM_CORE_CONN_AG_CTX_CF1_MASK      0x3       /* cf1 */
#define MSTORM_CORE_CONN_AG_CTX_CF1_SHIFT     4
#define MSTORM_CORE_CONN_AG_CTX_CF2_MASK      0x3       /* cf2 */
#define MSTORM_CORE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define MSTORM_CORE_CONN_AG_CTX_CF0EN_MASK    0x1       /* cf0en */
#define MSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT   0
#define MSTORM_CORE_CONN_AG_CTX_CF1EN_MASK    0x1       /* cf1en */
#define MSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT   1
#define MSTORM_CORE_CONN_AG_CTX_CF2EN_MASK    0x1       /* cf2en */
#define MSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT   2
#define MSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK  0x1       /* rule0en */
#define MSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define MSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK  0x1       /* rule1en */
#define MSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define MSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK  0x1       /* rule2en */
#define MSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define MSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK  0x1       /* rule3en */
#define MSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define MSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK  0x1       /* rule4en */
#define MSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16	word0 /* word0 */;
	__le16	word1 /* word1 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
};

/* per encapsulation type enabling flags */
struct prs_reg_encapsulation_type_en {
	u8 flags;
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GRE_ENABLE_MASK     0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GRE_ENABLE_SHIFT    0
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GRE_ENABLE_MASK      0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GRE_ENABLE_SHIFT     1
#define PRS_REG_ENCAPSULATION_TYPE_EN_VXLAN_ENABLE_MASK            0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_VXLAN_ENABLE_SHIFT           2
#define PRS_REG_ENCAPSULATION_TYPE_EN_T_TAG_ENABLE_MASK            0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_T_TAG_ENABLE_SHIFT           3
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GENEVE_ENABLE_MASK  0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GENEVE_ENABLE_SHIFT 4
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GENEVE_ENABLE_MASK   0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GENEVE_ENABLE_SHIFT  5
#define PRS_REG_ENCAPSULATION_TYPE_EN_RESERVED_MASK                0x3
#define PRS_REG_ENCAPSULATION_TYPE_EN_RESERVED_SHIFT               6
};

enum pxp_tph_st_hint {
	TPH_ST_HINT_BIDIR /* Read/Write access by Host and Device */,
	TPH_ST_HINT_REQUESTER /* Read/Write access by Device */,
	TPH_ST_HINT_TARGET,
	TPH_ST_HINT_TARGET_PRIO,
	MAX_PXP_TPH_ST_HINT
};

/* QM hardware structure of enable bypass credit mask */
struct qm_rf_bypass_mask {
	u8 flags;
#define QM_RF_BYPASS_MASK_LINEVOQ_MASK    0x1
#define QM_RF_BYPASS_MASK_LINEVOQ_SHIFT   0
#define QM_RF_BYPASS_MASK_RESERVED0_MASK  0x1
#define QM_RF_BYPASS_MASK_RESERVED0_SHIFT 1
#define QM_RF_BYPASS_MASK_PFWFQ_MASK      0x1
#define QM_RF_BYPASS_MASK_PFWFQ_SHIFT     2
#define QM_RF_BYPASS_MASK_VPWFQ_MASK      0x1
#define QM_RF_BYPASS_MASK_VPWFQ_SHIFT     3
#define QM_RF_BYPASS_MASK_PFRL_MASK       0x1
#define QM_RF_BYPASS_MASK_PFRL_SHIFT      4
#define QM_RF_BYPASS_MASK_VPQCNRL_MASK    0x1
#define QM_RF_BYPASS_MASK_VPQCNRL_SHIFT   5
#define QM_RF_BYPASS_MASK_FWPAUSE_MASK    0x1
#define QM_RF_BYPASS_MASK_FWPAUSE_SHIFT   6
#define QM_RF_BYPASS_MASK_RESERVED1_MASK  0x1
#define QM_RF_BYPASS_MASK_RESERVED1_SHIFT 7
};

/* QM hardware structure of opportunistic credit mask */
struct qm_rf_opportunistic_mask {
	__le16 flags;
#define QM_RF_OPPORTUNISTIC_MASK_LINEVOQ_MASK     0x1
#define QM_RF_OPPORTUNISTIC_MASK_LINEVOQ_SHIFT    0
#define QM_RF_OPPORTUNISTIC_MASK_BYTEVOQ_MASK     0x1
#define QM_RF_OPPORTUNISTIC_MASK_BYTEVOQ_SHIFT    1
#define QM_RF_OPPORTUNISTIC_MASK_PFWFQ_MASK       0x1
#define QM_RF_OPPORTUNISTIC_MASK_PFWFQ_SHIFT      2
#define QM_RF_OPPORTUNISTIC_MASK_VPWFQ_MASK       0x1
#define QM_RF_OPPORTUNISTIC_MASK_VPWFQ_SHIFT      3
#define QM_RF_OPPORTUNISTIC_MASK_PFRL_MASK        0x1
#define QM_RF_OPPORTUNISTIC_MASK_PFRL_SHIFT       4
#define QM_RF_OPPORTUNISTIC_MASK_VPQCNRL_MASK     0x1
#define QM_RF_OPPORTUNISTIC_MASK_VPQCNRL_SHIFT    5
#define QM_RF_OPPORTUNISTIC_MASK_FWPAUSE_MASK     0x1
#define QM_RF_OPPORTUNISTIC_MASK_FWPAUSE_SHIFT    6
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED0_MASK   0x1
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED0_SHIFT  7
#define QM_RF_OPPORTUNISTIC_MASK_QUEUEEMPTY_MASK  0x1
#define QM_RF_OPPORTUNISTIC_MASK_QUEUEEMPTY_SHIFT 8
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED1_MASK   0x7F
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED1_SHIFT  9
};

/* QM hardware structure of QM map memory */
struct qm_rf_pq_map {
	u32 reg;
#define QM_RF_PQ_MAP_PQ_VALID_MASK          0x1         /* PQ active */
#define QM_RF_PQ_MAP_PQ_VALID_SHIFT         0
#define QM_RF_PQ_MAP_RL_ID_MASK             0xFF        /* RL ID */
#define QM_RF_PQ_MAP_RL_ID_SHIFT            1
#define QM_RF_PQ_MAP_VP_PQ_ID_MASK          0x1FF
#define QM_RF_PQ_MAP_VP_PQ_ID_SHIFT         9
#define QM_RF_PQ_MAP_VOQ_MASK               0x1F        /* VOQ */
#define QM_RF_PQ_MAP_VOQ_SHIFT              18
#define QM_RF_PQ_MAP_WRR_WEIGHT_GROUP_MASK  0x3         /* WRR weight */
#define QM_RF_PQ_MAP_WRR_WEIGHT_GROUP_SHIFT 23
#define QM_RF_PQ_MAP_RL_VALID_MASK          0x1         /* RL active */
#define QM_RF_PQ_MAP_RL_VALID_SHIFT         25
#define QM_RF_PQ_MAP_RESERVED_MASK          0x3F
#define QM_RF_PQ_MAP_RESERVED_SHIFT         26
};

/* SDM operation gen command (generate aggregative interrupt) */
struct sdm_op_gen {
	__le32 command;
#define SDM_OP_GEN_COMP_PARAM_MASK  0xFFFF      /* completion parameters 0-15 */
#define SDM_OP_GEN_COMP_PARAM_SHIFT 0
#define SDM_OP_GEN_COMP_TYPE_MASK   0xF         /* completion type 16-19 */
#define SDM_OP_GEN_COMP_TYPE_SHIFT  16
#define SDM_OP_GEN_RESERVED_MASK    0xFFF       /* reserved 20-31 */
#define SDM_OP_GEN_RESERVED_SHIFT   20
};

struct tstorm_core_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define TSTORM_CORE_CONN_AG_CTX_BIT0_MASK     0x1       /* exist_in_qm0 */
#define TSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT    0
#define TSTORM_CORE_CONN_AG_CTX_BIT1_MASK     0x1       /* exist_in_qm1 */
#define TSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT    1
#define TSTORM_CORE_CONN_AG_CTX_BIT2_MASK     0x1       /* bit2 */
#define TSTORM_CORE_CONN_AG_CTX_BIT2_SHIFT    2
#define TSTORM_CORE_CONN_AG_CTX_BIT3_MASK     0x1       /* bit3 */
#define TSTORM_CORE_CONN_AG_CTX_BIT3_SHIFT    3
#define TSTORM_CORE_CONN_AG_CTX_BIT4_MASK     0x1       /* bit4 */
#define TSTORM_CORE_CONN_AG_CTX_BIT4_SHIFT    4
#define TSTORM_CORE_CONN_AG_CTX_BIT5_MASK     0x1       /* bit5 */
#define TSTORM_CORE_CONN_AG_CTX_BIT5_SHIFT    5
#define TSTORM_CORE_CONN_AG_CTX_CF0_MASK      0x3       /* timer0cf */
#define TSTORM_CORE_CONN_AG_CTX_CF0_SHIFT     6
	u8 flags1;
#define TSTORM_CORE_CONN_AG_CTX_CF1_MASK      0x3       /* timer1cf */
#define TSTORM_CORE_CONN_AG_CTX_CF1_SHIFT     0
#define TSTORM_CORE_CONN_AG_CTX_CF2_MASK      0x3       /* timer2cf */
#define TSTORM_CORE_CONN_AG_CTX_CF2_SHIFT     2
#define TSTORM_CORE_CONN_AG_CTX_CF3_MASK      0x3       /* timer_stop_all */
#define TSTORM_CORE_CONN_AG_CTX_CF3_SHIFT     4
#define TSTORM_CORE_CONN_AG_CTX_CF4_MASK      0x3       /* cf4 */
#define TSTORM_CORE_CONN_AG_CTX_CF4_SHIFT     6
	u8 flags2;
#define TSTORM_CORE_CONN_AG_CTX_CF5_MASK      0x3       /* cf5 */
#define TSTORM_CORE_CONN_AG_CTX_CF5_SHIFT     0
#define TSTORM_CORE_CONN_AG_CTX_CF6_MASK      0x3       /* cf6 */
#define TSTORM_CORE_CONN_AG_CTX_CF6_SHIFT     2
#define TSTORM_CORE_CONN_AG_CTX_CF7_MASK      0x3       /* cf7 */
#define TSTORM_CORE_CONN_AG_CTX_CF7_SHIFT     4
#define TSTORM_CORE_CONN_AG_CTX_CF8_MASK      0x3       /* cf8 */
#define TSTORM_CORE_CONN_AG_CTX_CF8_SHIFT     6
	u8 flags3;
#define TSTORM_CORE_CONN_AG_CTX_CF9_MASK      0x3       /* cf9 */
#define TSTORM_CORE_CONN_AG_CTX_CF9_SHIFT     0
#define TSTORM_CORE_CONN_AG_CTX_CF10_MASK     0x3       /* cf10 */
#define TSTORM_CORE_CONN_AG_CTX_CF10_SHIFT    2
#define TSTORM_CORE_CONN_AG_CTX_CF0EN_MASK    0x1       /* cf0en */
#define TSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT   4
#define TSTORM_CORE_CONN_AG_CTX_CF1EN_MASK    0x1       /* cf1en */
#define TSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT   5
#define TSTORM_CORE_CONN_AG_CTX_CF2EN_MASK    0x1       /* cf2en */
#define TSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT   6
#define TSTORM_CORE_CONN_AG_CTX_CF3EN_MASK    0x1       /* cf3en */
#define TSTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT   7
	u8 flags4;
#define TSTORM_CORE_CONN_AG_CTX_CF4EN_MASK    0x1       /* cf4en */
#define TSTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT   0
#define TSTORM_CORE_CONN_AG_CTX_CF5EN_MASK    0x1       /* cf5en */
#define TSTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT   1
#define TSTORM_CORE_CONN_AG_CTX_CF6EN_MASK    0x1       /* cf6en */
#define TSTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT   2
#define TSTORM_CORE_CONN_AG_CTX_CF7EN_MASK    0x1       /* cf7en */
#define TSTORM_CORE_CONN_AG_CTX_CF7EN_SHIFT   3
#define TSTORM_CORE_CONN_AG_CTX_CF8EN_MASK    0x1       /* cf8en */
#define TSTORM_CORE_CONN_AG_CTX_CF8EN_SHIFT   4
#define TSTORM_CORE_CONN_AG_CTX_CF9EN_MASK    0x1       /* cf9en */
#define TSTORM_CORE_CONN_AG_CTX_CF9EN_SHIFT   5
#define TSTORM_CORE_CONN_AG_CTX_CF10EN_MASK   0x1       /* cf10en */
#define TSTORM_CORE_CONN_AG_CTX_CF10EN_SHIFT  6
#define TSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK  0x1       /* rule0en */
#define TSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags5;
#define TSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK  0x1       /* rule1en */
#define TSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT 0
#define TSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK  0x1       /* rule2en */
#define TSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT 1
#define TSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK  0x1       /* rule3en */
#define TSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT 2
#define TSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK  0x1       /* rule4en */
#define TSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT 3
#define TSTORM_CORE_CONN_AG_CTX_RULE5EN_MASK  0x1       /* rule5en */
#define TSTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT 4
#define TSTORM_CORE_CONN_AG_CTX_RULE6EN_MASK  0x1       /* rule6en */
#define TSTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT 5
#define TSTORM_CORE_CONN_AG_CTX_RULE7EN_MASK  0x1       /* rule7en */
#define TSTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT 6
#define TSTORM_CORE_CONN_AG_CTX_RULE8EN_MASK  0x1       /* rule8en */
#define TSTORM_CORE_CONN_AG_CTX_RULE8EN_SHIFT 7
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le32	reg4 /* reg4 */;
	__le32	reg5 /* reg5 */;
	__le32	reg6 /* reg6 */;
	__le32	reg7 /* reg7 */;
	__le32	reg8 /* reg8 */;
	u8	byte2 /* byte2 */;
	u8	byte3 /* byte3 */;
	__le16	word0 /* word0 */;
	u8	byte4 /* byte4 */;
	u8	byte5 /* byte5 */;
	__le16	word1 /* word1 */;
	__le16	word2 /* conn_dpi */;
	__le16	word3 /* word3 */;
	__le32	reg9 /* reg9 */;
	__le32	reg10 /* reg10 */;
};

struct ustorm_core_conn_ag_ctx {
	u8	reserved /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define USTORM_CORE_CONN_AG_CTX_BIT0_MASK     0x1       /* exist_in_qm0 */
#define USTORM_CORE_CONN_AG_CTX_BIT0_SHIFT    0
#define USTORM_CORE_CONN_AG_CTX_BIT1_MASK     0x1       /* exist_in_qm1 */
#define USTORM_CORE_CONN_AG_CTX_BIT1_SHIFT    1
#define USTORM_CORE_CONN_AG_CTX_CF0_MASK      0x3       /* timer0cf */
#define USTORM_CORE_CONN_AG_CTX_CF0_SHIFT     2
#define USTORM_CORE_CONN_AG_CTX_CF1_MASK      0x3       /* timer1cf */
#define USTORM_CORE_CONN_AG_CTX_CF1_SHIFT     4
#define USTORM_CORE_CONN_AG_CTX_CF2_MASK      0x3       /* timer2cf */
#define USTORM_CORE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define USTORM_CORE_CONN_AG_CTX_CF3_MASK      0x3       /* timer_stop_all */
#define USTORM_CORE_CONN_AG_CTX_CF3_SHIFT     0
#define USTORM_CORE_CONN_AG_CTX_CF4_MASK      0x3       /* cf4 */
#define USTORM_CORE_CONN_AG_CTX_CF4_SHIFT     2
#define USTORM_CORE_CONN_AG_CTX_CF5_MASK      0x3       /* cf5 */
#define USTORM_CORE_CONN_AG_CTX_CF5_SHIFT     4
#define USTORM_CORE_CONN_AG_CTX_CF6_MASK      0x3       /* cf6 */
#define USTORM_CORE_CONN_AG_CTX_CF6_SHIFT     6
	u8 flags2;
#define USTORM_CORE_CONN_AG_CTX_CF0EN_MASK    0x1       /* cf0en */
#define USTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT   0
#define USTORM_CORE_CONN_AG_CTX_CF1EN_MASK    0x1       /* cf1en */
#define USTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT   1
#define USTORM_CORE_CONN_AG_CTX_CF2EN_MASK    0x1       /* cf2en */
#define USTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT   2
#define USTORM_CORE_CONN_AG_CTX_CF3EN_MASK    0x1       /* cf3en */
#define USTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT   3
#define USTORM_CORE_CONN_AG_CTX_CF4EN_MASK    0x1       /* cf4en */
#define USTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT   4
#define USTORM_CORE_CONN_AG_CTX_CF5EN_MASK    0x1       /* cf5en */
#define USTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT   5
#define USTORM_CORE_CONN_AG_CTX_CF6EN_MASK    0x1       /* cf6en */
#define USTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT   6
#define USTORM_CORE_CONN_AG_CTX_RULE0EN_MASK  0x1       /* rule0en */
#define USTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags3;
#define USTORM_CORE_CONN_AG_CTX_RULE1EN_MASK  0x1       /* rule1en */
#define USTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT 0
#define USTORM_CORE_CONN_AG_CTX_RULE2EN_MASK  0x1       /* rule2en */
#define USTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT 1
#define USTORM_CORE_CONN_AG_CTX_RULE3EN_MASK  0x1       /* rule3en */
#define USTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT 2
#define USTORM_CORE_CONN_AG_CTX_RULE4EN_MASK  0x1       /* rule4en */
#define USTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT 3
#define USTORM_CORE_CONN_AG_CTX_RULE5EN_MASK  0x1       /* rule5en */
#define USTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT 4
#define USTORM_CORE_CONN_AG_CTX_RULE6EN_MASK  0x1       /* rule6en */
#define USTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT 5
#define USTORM_CORE_CONN_AG_CTX_RULE7EN_MASK  0x1       /* rule7en */
#define USTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT 6
#define USTORM_CORE_CONN_AG_CTX_RULE8EN_MASK  0x1       /* rule8en */
#define USTORM_CORE_CONN_AG_CTX_RULE8EN_SHIFT 7
	u8	byte2 /* byte2 */;
	u8	byte3 /* byte3 */;
	__le16	word0 /* conn_dpi */;
	__le16	word1 /* word1 */;
	__le32	rx_producers /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le16	word2 /* word2 */;
	__le16	word3 /* word3 */;
};

struct ystorm_core_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define YSTORM_CORE_CONN_AG_CTX_BIT0_MASK     0x1       /* exist_in_qm0 */
#define YSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT    0
#define YSTORM_CORE_CONN_AG_CTX_BIT1_MASK     0x1       /* exist_in_qm1 */
#define YSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT    1
#define YSTORM_CORE_CONN_AG_CTX_CF0_MASK      0x3       /* cf0 */
#define YSTORM_CORE_CONN_AG_CTX_CF0_SHIFT     2
#define YSTORM_CORE_CONN_AG_CTX_CF1_MASK      0x3       /* cf1 */
#define YSTORM_CORE_CONN_AG_CTX_CF1_SHIFT     4
#define YSTORM_CORE_CONN_AG_CTX_CF2_MASK      0x3       /* cf2 */
#define YSTORM_CORE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define YSTORM_CORE_CONN_AG_CTX_CF0EN_MASK    0x1       /* cf0en */
#define YSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT   0
#define YSTORM_CORE_CONN_AG_CTX_CF1EN_MASK    0x1       /* cf1en */
#define YSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT   1
#define YSTORM_CORE_CONN_AG_CTX_CF2EN_MASK    0x1       /* cf2en */
#define YSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT   2
#define YSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK  0x1       /* rule0en */
#define YSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define YSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK  0x1       /* rule1en */
#define YSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define YSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK  0x1       /* rule2en */
#define YSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define YSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK  0x1       /* rule3en */
#define YSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define YSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK  0x1       /* rule4en */
#define YSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8	byte2 /* byte2 */;
	u8	byte3 /* byte3 */;
	__le16	word0 /* word0 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le16	word1 /* word1 */;
	__le16	word2 /* word2 */;
	__le16	word3 /* word3 */;
	__le16	word4 /* word4 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
};

/*********************************** Init ************************************/

/* Width of GRC address in bits (addresses are specified in dwords) */
#define GRC_ADDR_BITS                   23
#define MAX_GRC_ADDR                    ((1 << GRC_ADDR_BITS) - 1)

/* indicates an init that should be applied to any phase ID */
#define ANY_PHASE_ID                    0xffff

/* init pattern size in bytes */
#define INIT_PATTERN_SIZE_BITS  4
#define MAX_INIT_PATTERN_SIZE	BIT(INIT_PATTERN_SIZE_BITS)

/* Max size in dwords of a zipped array */
#define MAX_ZIPPED_SIZE                 8192

/* Global PXP window */
#define NUM_OF_PXP_WIN                  19
#define PXP_WIN_DWORD_SIZE_BITS 10
#define PXP_WIN_DWORD_SIZE		BIT(PXP_WIN_DWORD_SIZE_BITS)
#define PXP_WIN_BYTE_SIZE_BITS  (PXP_WIN_DWORD_SIZE_BITS + 2)
#define PXP_WIN_BYTE_SIZE               (PXP_WIN_DWORD_SIZE * 4)

/********************************* GRC Dump **********************************/

/* width of GRC dump register sequence length in bits */
#define DUMP_SEQ_LEN_BITS                       8
#define DUMP_SEQ_LEN_MAX_VAL            ((1 << DUMP_SEQ_LEN_BITS) - 1)

/* width of GRC dump memory length in bits */
#define DUMP_MEM_LEN_BITS                       18
#define DUMP_MEM_LEN_MAX_VAL            ((1 << DUMP_MEM_LEN_BITS) - 1)

/* width of register type ID in bits */
#define REG_TYPE_ID_BITS                        6
#define REG_TYPE_ID_MAX_VAL                     ((1 << REG_TYPE_ID_BITS) - 1)

/* width of block ID in bits */
#define BLOCK_ID_BITS                           8
#define BLOCK_ID_MAX_VAL                        ((1 << BLOCK_ID_BITS) - 1)

/******************************** Idle Check *********************************/

/* max number of idle check predicate immediates */
#define MAX_IDLE_CHK_PRED_IMM           3

/* max number of idle check argument registers */
#define MAX_IDLE_CHK_READ_REGS          3

/* max number of idle check loops */
#define MAX_IDLE_CHK_LOOPS                      0x10000

/* max idle check address increment */
#define MAX_IDLE_CHK_INCREMENT          0x10000

/* inicates an undefined idle check line index */
#define IDLE_CHK_UNDEFINED_LINE_IDX     0xffffff

/* max number of register values following the idle check header */
#define IDLE_CHK_MAX_DUMP_REGS          2

/* arguments for IDLE_CHK_MACRO_TYPE_QM_RD_WR */
#define IDLE_CHK_QM_RD_WR_PTR           0
#define IDLE_CHK_QM_RD_WR_BANK          1

/**************************************/
/* HSI Functions constants and macros */
/**************************************/

/* Number of VLAN priorities */
#define NUM_OF_VLAN_PRIORITIES                  8

/* the MCP Trace meta data signautre is duplicated in the perl script that
 * generats the NVRAM images.
 */
#define MCP_TRACE_META_IMAGE_SIGNATURE  0x669955aa

/* Binary buffer header */
struct bin_buffer_hdr {
	u32	offset;
	u32	length /* buffer length in bytes */;
};

/* binary buffer types */
enum bin_buffer_type {
	BIN_BUF_FW_VER_INFO /* fw_ver_info struct */,
	BIN_BUF_INIT_CMD /* init commands */,
	BIN_BUF_INIT_VAL /* init data */,
	BIN_BUF_INIT_MODE_TREE /* init modes tree */,
	BIN_BUF_IRO /* internal RAM offsets array */,
	MAX_BIN_BUFFER_TYPE
};

/* Chip IDs */
enum chip_ids {
	CHIP_BB_A0 /* BB A0 chip ID */,
	CHIP_BB_B0 /* BB B0 chip ID */,
	CHIP_K2 /* AH chip ID */,
	MAX_CHIP_IDS
};

enum idle_chk_severity_types {
	IDLE_CHK_SEVERITY_ERROR /* idle check failure should cause an error */,
	IDLE_CHK_SEVERITY_ERROR_NO_TRAFFIC,
	IDLE_CHK_SEVERITY_WARNING,
	MAX_IDLE_CHK_SEVERITY_TYPES
};

struct init_array_raw_hdr {
	__le32 data;
#define INIT_ARRAY_RAW_HDR_TYPE_MASK    0xF
#define INIT_ARRAY_RAW_HDR_TYPE_SHIFT   0
#define INIT_ARRAY_RAW_HDR_PARAMS_MASK  0xFFFFFFF       /* init array params */
#define INIT_ARRAY_RAW_HDR_PARAMS_SHIFT 4
};

struct init_array_standard_hdr {
	__le32 data;
#define INIT_ARRAY_STANDARD_HDR_TYPE_MASK  0xF
#define INIT_ARRAY_STANDARD_HDR_TYPE_SHIFT 0
#define INIT_ARRAY_STANDARD_HDR_SIZE_MASK  0xFFFFFFF
#define INIT_ARRAY_STANDARD_HDR_SIZE_SHIFT 4
};

struct init_array_zipped_hdr {
	__le32 data;
#define INIT_ARRAY_ZIPPED_HDR_TYPE_MASK         0xF
#define INIT_ARRAY_ZIPPED_HDR_TYPE_SHIFT        0
#define INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE_MASK  0xFFFFFFF
#define INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE_SHIFT 4
};

struct init_array_pattern_hdr {
	__le32 data;
#define INIT_ARRAY_PATTERN_HDR_TYPE_MASK          0xF
#define INIT_ARRAY_PATTERN_HDR_TYPE_SHIFT         0
#define INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE_MASK  0xF
#define INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE_SHIFT 4
#define INIT_ARRAY_PATTERN_HDR_REPETITIONS_MASK   0xFFFFFF
#define INIT_ARRAY_PATTERN_HDR_REPETITIONS_SHIFT  8
};

union init_array_hdr {
	struct init_array_raw_hdr	raw /* raw init array header */;
	struct init_array_standard_hdr	standard;
	struct init_array_zipped_hdr	zipped /* zipped init array header */;
	struct init_array_pattern_hdr	pattern /* pattern init array header */;
};

enum init_array_types {
	INIT_ARR_STANDARD /* standard init array */,
	INIT_ARR_ZIPPED /* zipped init array */,
	INIT_ARR_PATTERN /* a repeated pattern */,
	MAX_INIT_ARRAY_TYPES
};

/* init operation: callback */
struct init_callback_op {
	__le32	op_data;
#define INIT_CALLBACK_OP_OP_MASK        0xF
#define INIT_CALLBACK_OP_OP_SHIFT       0
#define INIT_CALLBACK_OP_RESERVED_MASK  0xFFFFFFF
#define INIT_CALLBACK_OP_RESERVED_SHIFT 4
	__le16	callback_id /* Callback ID */;
	__le16	block_id /* Blocks ID */;
};

/* init comparison types */
enum init_comparison_types {
	INIT_COMPARISON_EQ /* init value is included in the init command */,
	INIT_COMPARISON_OR /* init value is all zeros */,
	INIT_COMPARISON_AND /* init value is an array of values */,
	MAX_INIT_COMPARISON_TYPES
};

/* init operation: delay */
struct init_delay_op {
	__le32	op_data;
#define INIT_DELAY_OP_OP_MASK        0xF
#define INIT_DELAY_OP_OP_SHIFT       0
#define INIT_DELAY_OP_RESERVED_MASK  0xFFFFFFF
#define INIT_DELAY_OP_RESERVED_SHIFT 4
	__le32	delay /* delay in us */;
};

/* init operation: if_mode */
struct init_if_mode_op {
	__le32 op_data;
#define INIT_IF_MODE_OP_OP_MASK          0xF
#define INIT_IF_MODE_OP_OP_SHIFT         0
#define INIT_IF_MODE_OP_RESERVED1_MASK   0xFFF
#define INIT_IF_MODE_OP_RESERVED1_SHIFT  4
#define INIT_IF_MODE_OP_CMD_OFFSET_MASK  0xFFFF
#define INIT_IF_MODE_OP_CMD_OFFSET_SHIFT 16
	__le16	reserved2;
	__le16	modes_buf_offset;
};

/*  init operation: if_phase */
struct init_if_phase_op {
	__le32 op_data;
#define INIT_IF_PHASE_OP_OP_MASK           0xF
#define INIT_IF_PHASE_OP_OP_SHIFT          0
#define INIT_IF_PHASE_OP_DMAE_ENABLE_MASK  0x1
#define INIT_IF_PHASE_OP_DMAE_ENABLE_SHIFT 4
#define INIT_IF_PHASE_OP_RESERVED1_MASK    0x7FF
#define INIT_IF_PHASE_OP_RESERVED1_SHIFT   5
#define INIT_IF_PHASE_OP_CMD_OFFSET_MASK   0xFFFF
#define INIT_IF_PHASE_OP_CMD_OFFSET_SHIFT  16
	__le32 phase_data;
#define INIT_IF_PHASE_OP_PHASE_MASK        0xFF /* Init phase */
#define INIT_IF_PHASE_OP_PHASE_SHIFT       0
#define INIT_IF_PHASE_OP_RESERVED2_MASK    0xFF
#define INIT_IF_PHASE_OP_RESERVED2_SHIFT   8
#define INIT_IF_PHASE_OP_PHASE_ID_MASK     0xFFFF /* Init phase ID */
#define INIT_IF_PHASE_OP_PHASE_ID_SHIFT    16
};

/* init mode operators */
enum init_mode_ops {
	INIT_MODE_OP_NOT /* init mode not operator */,
	INIT_MODE_OP_OR /* init mode or operator */,
	INIT_MODE_OP_AND /* init mode and operator */,
	MAX_INIT_MODE_OPS
};

/* init operation: raw */
struct init_raw_op {
	__le32	op_data;
#define INIT_RAW_OP_OP_MASK      0xF
#define INIT_RAW_OP_OP_SHIFT     0
#define INIT_RAW_OP_PARAM1_MASK  0xFFFFFFF      /* init param 1 */
#define INIT_RAW_OP_PARAM1_SHIFT 4
	__le32	param2 /* Init param 2 */;
};

/* init array params */
struct init_op_array_params {
	__le16	size /* array size in dwords */;
	__le16	offset /* array start offset in dwords */;
};

/* Write init operation arguments */
union init_write_args {
	__le32				inline_val;
	__le32				zeros_count;
	__le32				array_offset;
	struct init_op_array_params	runtime;
};

/* init operation: write */
struct init_write_op {
	__le32 data;
#define INIT_WRITE_OP_OP_MASK        0xF
#define INIT_WRITE_OP_OP_SHIFT       0
#define INIT_WRITE_OP_SOURCE_MASK    0x7
#define INIT_WRITE_OP_SOURCE_SHIFT   4
#define INIT_WRITE_OP_RESERVED_MASK  0x1
#define INIT_WRITE_OP_RESERVED_SHIFT 7
#define INIT_WRITE_OP_WIDE_BUS_MASK  0x1
#define INIT_WRITE_OP_WIDE_BUS_SHIFT 8
#define INIT_WRITE_OP_ADDRESS_MASK   0x7FFFFF
#define INIT_WRITE_OP_ADDRESS_SHIFT  9
	union init_write_args args /* Write init operation arguments */;
};

/* init operation: read */
struct init_read_op {
	__le32 op_data;
#define INIT_READ_OP_OP_MASK         0xF
#define INIT_READ_OP_OP_SHIFT        0
#define INIT_READ_OP_POLL_COMP_MASK  0x7
#define INIT_READ_OP_POLL_COMP_SHIFT 4
#define INIT_READ_OP_RESERVED_MASK   0x1
#define INIT_READ_OP_RESERVED_SHIFT  7
#define INIT_READ_OP_POLL_MASK       0x1
#define INIT_READ_OP_POLL_SHIFT      8
#define INIT_READ_OP_ADDRESS_MASK    0x7FFFFF
#define INIT_READ_OP_ADDRESS_SHIFT   9
	__le32 expected_val;
};

/* Init operations union */
union init_op {
	struct init_raw_op	raw /* raw init operation */;
	struct init_write_op	write /* write init operation */;
	struct init_read_op	read /* read init operation */;
	struct init_if_mode_op	if_mode /* if_mode init operation */;
	struct init_if_phase_op if_phase /* if_phase init operation */;
	struct init_callback_op callback /* callback init operation */;
	struct init_delay_op	delay /* delay init operation */;
};

/* Init command operation types */
enum init_op_types {
	INIT_OP_READ /* GRC read init command */,
	INIT_OP_WRITE /* GRC write init command */,
	INIT_OP_IF_MODE,
	INIT_OP_IF_PHASE,
	INIT_OP_DELAY /* delay init command */,
	INIT_OP_CALLBACK /* callback init command */,
	MAX_INIT_OP_TYPES
};

/* init source types */
enum init_source_types {
	INIT_SRC_INLINE /* init value is included in the init command */,
	INIT_SRC_ZEROS /* init value is all zeros */,
	INIT_SRC_ARRAY /* init value is an array of values */,
	INIT_SRC_RUNTIME /* init value is provided during runtime */,
	MAX_INIT_SOURCE_TYPES
};

/* Internal RAM Offsets macro data */
struct iro {
	u32	base /* RAM field offset */;
	u16	m1 /* multiplier 1 */;
	u16	m2 /* multiplier 2 */;
	u16	m3 /* multiplier 3 */;
	u16	size /* RAM field size */;
};

/* QM per-port init parameters */
struct init_qm_port_params {
	u8	active /* Indicates if this port is active */;
	u8	num_active_phys_tcs;
	u16	num_pbf_cmd_lines;
	u16	num_btb_blocks;
	__le16	reserved;
};

/* QM per-PQ init parameters */
struct init_qm_pq_params {
	u8	vport_id /* VPORT ID */;
	u8	tc_id /* TC ID */;
	u8	wrr_group /* WRR group */;
	u8	reserved;
};

/* QM per-vport init parameters */
struct init_qm_vport_params {
	u32	vport_rl;
	u16	vport_wfq;
	u16	first_tx_pq_id[NUM_OF_TCS];
};

/* Win 2 */
#define GTT_BAR0_MAP_REG_IGU_CMD \
	0x00f000UL
/* Win 3 */
#define GTT_BAR0_MAP_REG_TSDM_RAM \
	0x010000UL
/* Win 4 */
#define GTT_BAR0_MAP_REG_MSDM_RAM \
	0x011000UL
/* Win 5 */
#define GTT_BAR0_MAP_REG_MSDM_RAM_1024 \
	0x012000UL
/* Win 6 */
#define GTT_BAR0_MAP_REG_USDM_RAM \
	0x013000UL
/* Win 7 */
#define GTT_BAR0_MAP_REG_USDM_RAM_1024 \
	0x014000UL
/* Win 8 */
#define GTT_BAR0_MAP_REG_USDM_RAM_2048 \
	0x015000UL
/* Win 9 */
#define GTT_BAR0_MAP_REG_XSDM_RAM \
	0x016000UL
/* Win 10 */
#define GTT_BAR0_MAP_REG_YSDM_RAM \
	0x017000UL
/* Win 11 */
#define GTT_BAR0_MAP_REG_PSDM_RAM \
	0x018000UL

/**
 * @brief qed_qm_pf_mem_size - prepare QM ILT sizes
 *
 * Returns the required host memory size in 4KB units.
 * Must be called before all QM init HSI functions.
 *
 * @param pf_id			- physical function ID
 * @param num_pf_cids	- number of connections used by this PF
 * @param num_vf_cids	- number of connections used by VFs of this PF
 * @param num_tids		- number of tasks used by this PF
 * @param num_pf_pqs	- number of PQs used by this PF
 * @param num_vf_pqs	- number of PQs used by VFs of this PF
 *
 * @return The required host memory size in 4KB units.
 */
u32 qed_qm_pf_mem_size(u8	pf_id,
		       u32	num_pf_cids,
		       u32	num_vf_cids,
		       u32	num_tids,
		       u16	num_pf_pqs,
		       u16	num_vf_pqs);

struct qed_qm_common_rt_init_params {
	u8				max_ports_per_engine;
	u8				max_phys_tcs_per_port;
	bool				pf_rl_en;
	bool				pf_wfq_en;
	bool				vport_rl_en;
	bool				vport_wfq_en;
	struct init_qm_port_params	*port_params;
};

/**
 * @brief qed_qm_common_rt_init - Prepare QM runtime init values for the
 * engine phase.
 *
 * @param p_hwfn
 * @param max_ports_per_engine	- max number of ports per engine in HW
 * @param max_phys_tcs_per_port	- max number of physical TCs per port in HW
 * @param pf_rl_en				- enable per-PF rate limiters
 * @param pf_wfq_en				- enable per-PF WFQ
 * @param vport_rl_en			- enable per-VPORT rate limiters
 * @param vport_wfq_en			- enable per-VPORT WFQ
 * @param port_params			- array of size MAX_NUM_PORTS with
 *						arameters for each port
 *
 * @return 0 on success, -1 on error.
 */
int qed_qm_common_rt_init(
	struct qed_hwfn				*p_hwfn,
	struct qed_qm_common_rt_init_params	*p_params);

struct qed_qm_pf_rt_init_params {
	u8				port_id;
	u8				pf_id;
	u8				max_phys_tcs_per_port;
	bool				is_first_pf;
	u32				num_pf_cids;
	u32				num_vf_cids;
	u32				num_tids;
	u16				start_pq;
	u16				num_pf_pqs;
	u16				num_vf_pqs;
	u8				start_vport;
	u8				num_vports;
	u8				pf_wfq;
	u32				pf_rl;
	struct init_qm_pq_params	*pq_params;
	struct init_qm_vport_params	*vport_params;
};

int qed_qm_pf_rt_init(struct qed_hwfn			*p_hwfn,
		      struct qed_ptt			*p_ptt,
		      struct qed_qm_pf_rt_init_params	*p_params);

/**
 * @brief qed_init_pf_rl  Initializes the rate limit of the specified PF
 *
 * @param p_hwfn
 * @param p_ptt	- ptt window used for writing the registers
 * @param pf_id	- PF ID
 * @param pf_rl	- rate limit in Mb/sec units
 *
 * @return 0 on success, -1 on error.
 */
int qed_init_pf_rl(struct qed_hwfn	*p_hwfn,
		   struct qed_ptt	*p_ptt,
		   u8			pf_id,
		   u32			pf_rl);

/**
 * @brief qed_init_vport_rl  Initializes the rate limit of the specified VPORT
 *
 * @param p_hwfn
 * @param p_ptt		- ptt window used for writing the registers
 * @param vport_id	- VPORT ID
 * @param vport_rl	- rate limit in Mb/sec units
 *
 * @return 0 on success, -1 on error.
 */

int qed_init_vport_rl(struct qed_hwfn	*p_hwfn,
		      struct qed_ptt	*p_ptt,
		      u8		vport_id,
		      u32		vport_rl);
/**
 * @brief qed_send_qm_stop_cmd  Sends a stop command to the QM
 *
 * @param p_hwfn
 * @param p_ptt	         - ptt window used for writing the registers
 * @param is_release_cmd - true for release, false for stop.
 * @param is_tx_pq       - true for Tx PQs, false for Other PQs.
 * @param start_pq       - first PQ ID to stop
 * @param num_pqs        - Number of PQs to stop, starting from start_pq.
 *
 * @return bool, true if successful, false if timeout occurred while waiting
 *					for QM command done.
 */

bool qed_send_qm_stop_cmd(struct qed_hwfn	*p_hwfn,
			  struct qed_ptt	*p_ptt,
			  bool			is_release_cmd,
			  bool			is_tx_pq,
			  u16			start_pq,
			  u16			num_pqs);

/* Ystorm flow control mode. Use enum fw_flow_ctrl_mode */
#define YSTORM_FLOW_CONTROL_MODE_OFFSET			(IRO[0].base)
#define YSTORM_FLOW_CONTROL_MODE_SIZE			(IRO[0].size)
/* Tstorm port statistics */
#define TSTORM_PORT_STAT_OFFSET(port_id)		(IRO[1].base + \
							 ((port_id) * \
							  IRO[1].m1))
#define TSTORM_PORT_STAT_SIZE				(IRO[1].size)
/* Ustorm VF-PF Channel ready flag */
#define USTORM_VF_PF_CHANNEL_READY_OFFSET(vf_id)	(IRO[2].base +	\
							 ((vf_id) *	\
							  IRO[2].m1))
#define USTORM_VF_PF_CHANNEL_READY_SIZE			(IRO[2].size)
/* Ustorm Final flr cleanup ack */
#define USTORM_FLR_FINAL_ACK_OFFSET			(IRO[3].base)
#define USTORM_FLR_FINAL_ACK_SIZE			(IRO[3].size)
/* Ustorm Event ring consumer */
#define USTORM_EQE_CONS_OFFSET(pf_id)			(IRO[4].base +	\
							 ((pf_id) *	\
							  IRO[4].m1))
#define USTORM_EQE_CONS_SIZE				(IRO[4].size)
/* Ustorm Completion ring consumer */
#define USTORM_CQ_CONS_OFFSET(global_queue_id)		(IRO[5].base +	\
							 ((global_queue_id) * \
							  IRO[5].m1))
#define USTORM_CQ_CONS_SIZE				(IRO[5].size)
/* Xstorm Integration Test Data */
#define XSTORM_INTEG_TEST_DATA_OFFSET			(IRO[6].base)
#define XSTORM_INTEG_TEST_DATA_SIZE			(IRO[6].size)
/* Ystorm Integration Test Data */
#define YSTORM_INTEG_TEST_DATA_OFFSET			(IRO[7].base)
#define YSTORM_INTEG_TEST_DATA_SIZE			(IRO[7].size)
/* Pstorm Integration Test Data */
#define PSTORM_INTEG_TEST_DATA_OFFSET			(IRO[8].base)
#define PSTORM_INTEG_TEST_DATA_SIZE			(IRO[8].size)
/* Tstorm Integration Test Data */
#define TSTORM_INTEG_TEST_DATA_OFFSET			(IRO[9].base)
#define TSTORM_INTEG_TEST_DATA_SIZE			(IRO[9].size)
/* Mstorm Integration Test Data */
#define MSTORM_INTEG_TEST_DATA_OFFSET			(IRO[10].base)
#define MSTORM_INTEG_TEST_DATA_SIZE			(IRO[10].size)
/* Ustorm Integration Test Data */
#define USTORM_INTEG_TEST_DATA_OFFSET			(IRO[11].base)
#define USTORM_INTEG_TEST_DATA_SIZE			(IRO[11].size)
/* Tstorm producers */
#define TSTORM_LL2_RX_PRODS_OFFSET(core_rx_queue_id)	(IRO[12].base +	\
							 ((core_rx_queue_id) * \
							  IRO[12].m1))
#define TSTORM_LL2_RX_PRODS_SIZE			(IRO[12].size)
/* Tstorm LiteL2 queue statistics */
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(core_rx_q_id) (IRO[13].base + \
							     ((core_rx_q_id) * \
							      IRO[13].m1))
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_SIZE		(IRO[13].size)
/* Ustorm LiteL2 queue statistics */
#define CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(core_rx_q_id) (IRO[14].base + \
							     ((core_rx_q_id) * \
							      IRO[14].m1))
#define CORE_LL2_USTORM_PER_QUEUE_STAT_SIZE		(IRO[14].size)
/* Pstorm LiteL2 queue statistics */
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(core_txst_id) (IRO[15].base + \
							     ((core_txst_id) * \
							      IRO[15].m1))
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_SIZE		(IRO[15].size)
/* Mstorm queue statistics */
#define MSTORM_QUEUE_STAT_OFFSET(stat_counter_id) (IRO[16].base + \
						   ((stat_counter_id) *	\
						    IRO[16].m1))
#define MSTORM_QUEUE_STAT_SIZE				(IRO[16].size)
/* Mstorm producers */
#define MSTORM_PRODS_OFFSET(queue_id)			(IRO[17].base +	\
							 ((queue_id) *	\
							  IRO[17].m1))
#define MSTORM_PRODS_SIZE				(IRO[17].size)
/* TPA agregation timeout in us resolution (on ASIC) */
#define MSTORM_TPA_TIMEOUT_US_OFFSET			(IRO[18].base)
#define MSTORM_TPA_TIMEOUT_US_SIZE			(IRO[18].size)
/* Ustorm queue statistics */
#define USTORM_QUEUE_STAT_OFFSET(stat_counter_id)	(IRO[19].base +	\
							((stat_counter_id) * \
							 IRO[19].m1))
#define USTORM_QUEUE_STAT_SIZE				(IRO[19].size)
/* Ustorm queue zone */
#define USTORM_ETH_QUEUE_ZONE_OFFSET(queue_id)		(IRO[20].base +	\
							 ((queue_id) *	\
							  IRO[20].m1))
#define USTORM_ETH_QUEUE_ZONE_SIZE			(IRO[20].size)
/* Pstorm queue statistics */
#define PSTORM_QUEUE_STAT_OFFSET(stat_counter_id)	(IRO[21].base +	\
							 ((stat_counter_id) * \
							  IRO[21].m1))
#define PSTORM_QUEUE_STAT_SIZE				(IRO[21].size)
/* Tstorm last parser message */
#define TSTORM_ETH_PRS_INPUT_OFFSET(pf_id)		(IRO[22].base +	\
							 ((pf_id) *	\
							  IRO[22].m1))
#define TSTORM_ETH_PRS_INPUT_SIZE			(IRO[22].size)
/* Ystorm queue zone */
#define YSTORM_ETH_QUEUE_ZONE_OFFSET(queue_id)		(IRO[23].base +	\
							 ((queue_id) *	\
							  IRO[23].m1))
#define YSTORM_ETH_QUEUE_ZONE_SIZE			(IRO[23].size)
/* Ystorm cqe producer */
#define YSTORM_TOE_CQ_PROD_OFFSET(rss_id)		(IRO[24].base +	\
							 ((rss_id) *	\
							  IRO[24].m1))
#define YSTORM_TOE_CQ_PROD_SIZE				(IRO[24].size)
/* Ustorm cqe producer */
#define USTORM_TOE_CQ_PROD_OFFSET(rss_id)		(IRO[25].base +	\
							 ((rss_id) *	\
							  IRO[25].m1))
#define USTORM_TOE_CQ_PROD_SIZE				(IRO[25].size)
/* Ustorm grq producer */
#define USTORM_TOE_GRQ_PROD_OFFSET(pf_id)		(IRO[26].base +	\
							 ((pf_id) *	\
							  IRO[26].m1))
#define USTORM_TOE_GRQ_PROD_SIZE			(IRO[26].size)
/* Tstorm cmdq-cons of given command queue-id */
#define TSTORM_SCSI_CMDQ_CONS_OFFSET(cmdq_queue_id)	(IRO[27].base +	\
							 ((cmdq_queue_id) * \
							  IRO[27].m1))
#define TSTORM_SCSI_CMDQ_CONS_SIZE			(IRO[27].size)
/* Mstorm rq-cons of given queue-id */
#define MSTORM_SCSI_RQ_CONS_OFFSET(rq_queue_id)		(IRO[28].base +	\
							 ((rq_queue_id) * \
							  IRO[28].m1))
#define MSTORM_SCSI_RQ_CONS_SIZE			(IRO[28].size)
/* Pstorm RoCE statistics */
#define PSTORM_ROCE_STAT_OFFSET(stat_counter_id)	(IRO[29].base +	\
							 ((stat_counter_id) * \
							  IRO[29].m1))
#define PSTORM_ROCE_STAT_SIZE				(IRO[29].size)
/* Tstorm RoCE statistics */
#define TSTORM_ROCE_STAT_OFFSET(stat_counter_id)	(IRO[30].base +	\
							 ((stat_counter_id) * \
							  IRO[30].m1))
#define TSTORM_ROCE_STAT_SIZE				(IRO[30].size)

static const struct iro iro_arr[31] = {
	{ 0x10,	  0x0,	 0x0,	0x0,   0x8     },
	{ 0x4448, 0x60,	 0x0,	0x0,   0x60    },
	{ 0x498,  0x8,	 0x0,	0x0,   0x4     },
	{ 0x494,  0x0,	 0x0,	0x0,   0x4     },
	{ 0x10,	  0x8,	 0x0,	0x0,   0x2     },
	{ 0x90,	  0x8,	 0x0,	0x0,   0x2     },
	{ 0x4540, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x39e0, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x2598, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x4350, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x52d0, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x7a48, 0x0,	 0x0,	0x0,   0xf8    },
	{ 0x100,  0x8,	 0x0,	0x0,   0x8     },
	{ 0x5808, 0x10,	 0x0,	0x0,   0x10    },
	{ 0xb100, 0x30,	 0x0,	0x0,   0x30    },
	{ 0x95c0, 0x30,	 0x0,	0x0,   0x30    },
	{ 0x54f8, 0x40,	 0x0,	0x0,   0x40    },
	{ 0x200,  0x10,	 0x0,	0x0,   0x8     },
	{ 0x9e70, 0x0,	 0x0,	0x0,   0x4     },
	{ 0x7ca0, 0x40,	 0x0,	0x0,   0x30    },
	{ 0xd00,  0x8,	 0x0,	0x0,   0x8     },
	{ 0x2790, 0x80,	 0x0,	0x0,   0x38    },
	{ 0xa520, 0xf0,	 0x0,	0x0,   0xf0    },
	{ 0x80,	  0x8,	 0x0,	0x0,   0x8     },
	{ 0xac0,  0x8,	 0x0,	0x0,   0x8     },
	{ 0x2580, 0x8,	 0x0,	0x0,   0x8     },
	{ 0x2500, 0x8,	 0x0,	0x0,   0x8     },
	{ 0x440,  0x8,	 0x0,	0x0,   0x2     },
	{ 0x1800, 0x8,	 0x0,	0x0,   0x2     },
	{ 0x27c8, 0x80,	 0x0,	0x0,   0x10    },
	{ 0x4710, 0x10,	 0x0,	0x0,   0x10    },
};

/* Runtime array offsets */
#define DORQ_REG_PF_MAX_ICID_0_RT_OFFSET                                0
#define DORQ_REG_PF_MAX_ICID_1_RT_OFFSET                                1
#define DORQ_REG_PF_MAX_ICID_2_RT_OFFSET                                2
#define DORQ_REG_PF_MAX_ICID_3_RT_OFFSET                                3
#define DORQ_REG_PF_MAX_ICID_4_RT_OFFSET                                4
#define DORQ_REG_PF_MAX_ICID_5_RT_OFFSET                                5
#define DORQ_REG_PF_MAX_ICID_6_RT_OFFSET                                6
#define DORQ_REG_PF_MAX_ICID_7_RT_OFFSET                                7
#define DORQ_REG_VF_MAX_ICID_0_RT_OFFSET                                8
#define DORQ_REG_VF_MAX_ICID_1_RT_OFFSET                                9
#define DORQ_REG_VF_MAX_ICID_2_RT_OFFSET                                10
#define DORQ_REG_VF_MAX_ICID_3_RT_OFFSET                                11
#define DORQ_REG_VF_MAX_ICID_4_RT_OFFSET                                12
#define DORQ_REG_VF_MAX_ICID_5_RT_OFFSET                                13
#define DORQ_REG_VF_MAX_ICID_6_RT_OFFSET                                14
#define DORQ_REG_VF_MAX_ICID_7_RT_OFFSET                                15
#define DORQ_REG_PF_WAKE_ALL_RT_OFFSET                                  16
#define IGU_REG_PF_CONFIGURATION_RT_OFFSET                              17
#define IGU_REG_VF_CONFIGURATION_RT_OFFSET                              18
#define IGU_REG_ATTN_MSG_ADDR_L_RT_OFFSET                               19
#define IGU_REG_ATTN_MSG_ADDR_H_RT_OFFSET                               20
#define IGU_REG_LEADING_EDGE_LATCH_RT_OFFSET                            21
#define IGU_REG_TRAILING_EDGE_LATCH_RT_OFFSET                           22
#define CAU_REG_CQE_AGG_UNIT_SIZE_RT_OFFSET                             23
#define CAU_REG_SB_VAR_MEMORY_RT_OFFSET                                 760
#define CAU_REG_SB_VAR_MEMORY_RT_SIZE                                   736
#define CAU_REG_SB_VAR_MEMORY_RT_OFFSET                                 760
#define CAU_REG_SB_VAR_MEMORY_RT_SIZE                                   736
#define CAU_REG_SB_ADDR_MEMORY_RT_OFFSET                                1496
#define CAU_REG_SB_ADDR_MEMORY_RT_SIZE                                  736
#define CAU_REG_PI_MEMORY_RT_OFFSET                                     2232
#define CAU_REG_PI_MEMORY_RT_SIZE                                       4416
#define PRS_REG_SEARCH_RESP_INITIATOR_TYPE_RT_OFFSET                    6648
#define PRS_REG_TASK_ID_MAX_INITIATOR_PF_RT_OFFSET                      6649
#define PRS_REG_TASK_ID_MAX_INITIATOR_VF_RT_OFFSET                      6650
#define PRS_REG_TASK_ID_MAX_TARGET_PF_RT_OFFSET                         6651
#define PRS_REG_TASK_ID_MAX_TARGET_VF_RT_OFFSET                         6652
#define PRS_REG_SEARCH_TCP_RT_OFFSET                                    6653
#define PRS_REG_SEARCH_FCOE_RT_OFFSET                                   6654
#define PRS_REG_SEARCH_ROCE_RT_OFFSET                                   6655
#define PRS_REG_ROCE_DEST_QP_MAX_VF_RT_OFFSET                           6656
#define PRS_REG_ROCE_DEST_QP_MAX_PF_RT_OFFSET                           6657
#define PRS_REG_SEARCH_OPENFLOW_RT_OFFSET                               6658
#define PRS_REG_SEARCH_NON_IP_AS_OPENFLOW_RT_OFFSET                     6659
#define PRS_REG_OPENFLOW_SUPPORT_ONLY_KNOWN_OVER_IP_RT_OFFSET           6660
#define PRS_REG_OPENFLOW_SEARCH_KEY_MASK_RT_OFFSET                      6661
#define PRS_REG_LIGHT_L2_ETHERTYPE_EN_RT_OFFSET                         6662
#define SRC_REG_FIRSTFREE_RT_OFFSET                                     6663
#define SRC_REG_FIRSTFREE_RT_SIZE                                       2
#define SRC_REG_LASTFREE_RT_OFFSET                                      6665
#define SRC_REG_LASTFREE_RT_SIZE                                        2
#define SRC_REG_COUNTFREE_RT_OFFSET                                     6667
#define SRC_REG_NUMBER_HASH_BITS_RT_OFFSET                              6668
#define PSWRQ2_REG_CDUT_P_SIZE_RT_OFFSET                                6669
#define PSWRQ2_REG_CDUC_P_SIZE_RT_OFFSET                                6670
#define PSWRQ2_REG_TM_P_SIZE_RT_OFFSET                                  6671
#define PSWRQ2_REG_QM_P_SIZE_RT_OFFSET                                  6672
#define PSWRQ2_REG_SRC_P_SIZE_RT_OFFSET                                 6673
#define PSWRQ2_REG_TM_FIRST_ILT_RT_OFFSET                               6674
#define PSWRQ2_REG_TM_LAST_ILT_RT_OFFSET                                6675
#define PSWRQ2_REG_QM_FIRST_ILT_RT_OFFSET                               6676
#define PSWRQ2_REG_QM_LAST_ILT_RT_OFFSET                                6677
#define PSWRQ2_REG_SRC_FIRST_ILT_RT_OFFSET                              6678
#define PSWRQ2_REG_SRC_LAST_ILT_RT_OFFSET                               6679
#define PSWRQ2_REG_CDUC_FIRST_ILT_RT_OFFSET                             6680
#define PSWRQ2_REG_CDUC_LAST_ILT_RT_OFFSET                              6681
#define PSWRQ2_REG_CDUT_FIRST_ILT_RT_OFFSET                             6682
#define PSWRQ2_REG_CDUT_LAST_ILT_RT_OFFSET                              6683
#define PSWRQ2_REG_TSDM_FIRST_ILT_RT_OFFSET                             6684
#define PSWRQ2_REG_TSDM_LAST_ILT_RT_OFFSET                              6685
#define PSWRQ2_REG_TM_NUMBER_OF_PF_BLOCKS_RT_OFFSET                     6686
#define PSWRQ2_REG_CDUT_NUMBER_OF_PF_BLOCKS_RT_OFFSET                   6687
#define PSWRQ2_REG_CDUC_NUMBER_OF_PF_BLOCKS_RT_OFFSET                   6688
#define PSWRQ2_REG_TM_VF_BLOCKS_RT_OFFSET                               6689
#define PSWRQ2_REG_CDUT_VF_BLOCKS_RT_OFFSET                             6690
#define PSWRQ2_REG_CDUC_VF_BLOCKS_RT_OFFSET                             6691
#define PSWRQ2_REG_TM_BLOCKS_FACTOR_RT_OFFSET                           6692
#define PSWRQ2_REG_CDUT_BLOCKS_FACTOR_RT_OFFSET                         6693
#define PSWRQ2_REG_CDUC_BLOCKS_FACTOR_RT_OFFSET                         6694
#define PSWRQ2_REG_VF_BASE_RT_OFFSET                                    6695
#define PSWRQ2_REG_VF_LAST_ILT_RT_OFFSET                                6696
#define PSWRQ2_REG_WR_MBS0_RT_OFFSET                                    6697
#define PSWRQ2_REG_RD_MBS0_RT_OFFSET                                    6698
#define PSWRQ2_REG_DRAM_ALIGN_WR_RT_OFFSET                              6699
#define PSWRQ2_REG_DRAM_ALIGN_RD_RT_OFFSET                              6700
#define PSWRQ2_REG_ILT_MEMORY_RT_OFFSET                                 6701
#define PSWRQ2_REG_ILT_MEMORY_RT_SIZE                                   22000
#define PGLUE_REG_B_VF_BASE_RT_OFFSET                                   28701
#define PGLUE_REG_B_CACHE_LINE_SIZE_RT_OFFSET                           28702
#define PGLUE_REG_B_PF_BAR0_SIZE_RT_OFFSET                              28703
#define PGLUE_REG_B_PF_BAR1_SIZE_RT_OFFSET                              28704
#define PGLUE_REG_B_VF_BAR1_SIZE_RT_OFFSET                              28705
#define TM_REG_VF_ENABLE_CONN_RT_OFFSET                                 28706
#define TM_REG_PF_ENABLE_CONN_RT_OFFSET                                 28707
#define TM_REG_PF_ENABLE_TASK_RT_OFFSET                                 28708
#define TM_REG_GROUP_SIZE_RESOLUTION_CONN_RT_OFFSET                     28709
#define TM_REG_GROUP_SIZE_RESOLUTION_TASK_RT_OFFSET                     28710
#define TM_REG_CONFIG_CONN_MEM_RT_OFFSET                                28711
#define TM_REG_CONFIG_CONN_MEM_RT_SIZE                                  416
#define TM_REG_CONFIG_TASK_MEM_RT_OFFSET                                29127
#define TM_REG_CONFIG_TASK_MEM_RT_SIZE                                  512
#define QM_REG_MAXPQSIZE_0_RT_OFFSET                                    29639
#define QM_REG_MAXPQSIZE_1_RT_OFFSET                                    29640
#define QM_REG_MAXPQSIZE_2_RT_OFFSET                                    29641
#define QM_REG_MAXPQSIZETXSEL_0_RT_OFFSET                               29642
#define QM_REG_MAXPQSIZETXSEL_1_RT_OFFSET                               29643
#define QM_REG_MAXPQSIZETXSEL_2_RT_OFFSET                               29644
#define QM_REG_MAXPQSIZETXSEL_3_RT_OFFSET                               29645
#define QM_REG_MAXPQSIZETXSEL_4_RT_OFFSET                               29646
#define QM_REG_MAXPQSIZETXSEL_5_RT_OFFSET                               29647
#define QM_REG_MAXPQSIZETXSEL_6_RT_OFFSET                               29648
#define QM_REG_MAXPQSIZETXSEL_7_RT_OFFSET                               29649
#define QM_REG_MAXPQSIZETXSEL_8_RT_OFFSET                               29650
#define QM_REG_MAXPQSIZETXSEL_9_RT_OFFSET                               29651
#define QM_REG_MAXPQSIZETXSEL_10_RT_OFFSET                              29652
#define QM_REG_MAXPQSIZETXSEL_11_RT_OFFSET                              29653
#define QM_REG_MAXPQSIZETXSEL_12_RT_OFFSET                              29654
#define QM_REG_MAXPQSIZETXSEL_13_RT_OFFSET                              29655
#define QM_REG_MAXPQSIZETXSEL_14_RT_OFFSET                              29656
#define QM_REG_MAXPQSIZETXSEL_15_RT_OFFSET                              29657
#define QM_REG_MAXPQSIZETXSEL_16_RT_OFFSET                              29658
#define QM_REG_MAXPQSIZETXSEL_17_RT_OFFSET                              29659
#define QM_REG_MAXPQSIZETXSEL_18_RT_OFFSET                              29660
#define QM_REG_MAXPQSIZETXSEL_19_RT_OFFSET                              29661
#define QM_REG_MAXPQSIZETXSEL_20_RT_OFFSET                              29662
#define QM_REG_MAXPQSIZETXSEL_21_RT_OFFSET                              29663
#define QM_REG_MAXPQSIZETXSEL_22_RT_OFFSET                              29664
#define QM_REG_MAXPQSIZETXSEL_23_RT_OFFSET                              29665
#define QM_REG_MAXPQSIZETXSEL_24_RT_OFFSET                              29666
#define QM_REG_MAXPQSIZETXSEL_25_RT_OFFSET                              29667
#define QM_REG_MAXPQSIZETXSEL_26_RT_OFFSET                              29668
#define QM_REG_MAXPQSIZETXSEL_27_RT_OFFSET                              29669
#define QM_REG_MAXPQSIZETXSEL_28_RT_OFFSET                              29670
#define QM_REG_MAXPQSIZETXSEL_29_RT_OFFSET                              29671
#define QM_REG_MAXPQSIZETXSEL_30_RT_OFFSET                              29672
#define QM_REG_MAXPQSIZETXSEL_31_RT_OFFSET                              29673
#define QM_REG_MAXPQSIZETXSEL_32_RT_OFFSET                              29674
#define QM_REG_MAXPQSIZETXSEL_33_RT_OFFSET                              29675
#define QM_REG_MAXPQSIZETXSEL_34_RT_OFFSET                              29676
#define QM_REG_MAXPQSIZETXSEL_35_RT_OFFSET                              29677
#define QM_REG_MAXPQSIZETXSEL_36_RT_OFFSET                              29678
#define QM_REG_MAXPQSIZETXSEL_37_RT_OFFSET                              29679
#define QM_REG_MAXPQSIZETXSEL_38_RT_OFFSET                              29680
#define QM_REG_MAXPQSIZETXSEL_39_RT_OFFSET                              29681
#define QM_REG_MAXPQSIZETXSEL_40_RT_OFFSET                              29682
#define QM_REG_MAXPQSIZETXSEL_41_RT_OFFSET                              29683
#define QM_REG_MAXPQSIZETXSEL_42_RT_OFFSET                              29684
#define QM_REG_MAXPQSIZETXSEL_43_RT_OFFSET                              29685
#define QM_REG_MAXPQSIZETXSEL_44_RT_OFFSET                              29686
#define QM_REG_MAXPQSIZETXSEL_45_RT_OFFSET                              29687
#define QM_REG_MAXPQSIZETXSEL_46_RT_OFFSET                              29688
#define QM_REG_MAXPQSIZETXSEL_47_RT_OFFSET                              29689
#define QM_REG_MAXPQSIZETXSEL_48_RT_OFFSET                              29690
#define QM_REG_MAXPQSIZETXSEL_49_RT_OFFSET                              29691
#define QM_REG_MAXPQSIZETXSEL_50_RT_OFFSET                              29692
#define QM_REG_MAXPQSIZETXSEL_51_RT_OFFSET                              29693
#define QM_REG_MAXPQSIZETXSEL_52_RT_OFFSET                              29694
#define QM_REG_MAXPQSIZETXSEL_53_RT_OFFSET                              29695
#define QM_REG_MAXPQSIZETXSEL_54_RT_OFFSET                              29696
#define QM_REG_MAXPQSIZETXSEL_55_RT_OFFSET                              29697
#define QM_REG_MAXPQSIZETXSEL_56_RT_OFFSET                              29698
#define QM_REG_MAXPQSIZETXSEL_57_RT_OFFSET                              29699
#define QM_REG_MAXPQSIZETXSEL_58_RT_OFFSET                              29700
#define QM_REG_MAXPQSIZETXSEL_59_RT_OFFSET                              29701
#define QM_REG_MAXPQSIZETXSEL_60_RT_OFFSET                              29702
#define QM_REG_MAXPQSIZETXSEL_61_RT_OFFSET                              29703
#define QM_REG_MAXPQSIZETXSEL_62_RT_OFFSET                              29704
#define QM_REG_MAXPQSIZETXSEL_63_RT_OFFSET                              29705
#define QM_REG_BASEADDROTHERPQ_RT_OFFSET                                29706
#define QM_REG_BASEADDROTHERPQ_RT_SIZE                                  128
#define QM_REG_VOQCRDLINE_RT_OFFSET                                     29834
#define QM_REG_VOQCRDLINE_RT_SIZE                                       20
#define QM_REG_VOQINITCRDLINE_RT_OFFSET                                 29854
#define QM_REG_VOQINITCRDLINE_RT_SIZE                                   20
#define QM_REG_AFULLQMBYPTHRPFWFQ_RT_OFFSET                             29874
#define QM_REG_AFULLQMBYPTHRVPWFQ_RT_OFFSET                             29875
#define QM_REG_AFULLQMBYPTHRPFRL_RT_OFFSET                              29876
#define QM_REG_AFULLQMBYPTHRGLBLRL_RT_OFFSET                            29877
#define QM_REG_AFULLOPRTNSTCCRDMASK_RT_OFFSET                           29878
#define QM_REG_WRROTHERPQGRP_0_RT_OFFSET                                29879
#define QM_REG_WRROTHERPQGRP_1_RT_OFFSET                                29880
#define QM_REG_WRROTHERPQGRP_2_RT_OFFSET                                29881
#define QM_REG_WRROTHERPQGRP_3_RT_OFFSET                                29882
#define QM_REG_WRROTHERPQGRP_4_RT_OFFSET                                29883
#define QM_REG_WRROTHERPQGRP_5_RT_OFFSET                                29884
#define QM_REG_WRROTHERPQGRP_6_RT_OFFSET                                29885
#define QM_REG_WRROTHERPQGRP_7_RT_OFFSET                                29886
#define QM_REG_WRROTHERPQGRP_8_RT_OFFSET                                29887
#define QM_REG_WRROTHERPQGRP_9_RT_OFFSET                                29888
#define QM_REG_WRROTHERPQGRP_10_RT_OFFSET                               29889
#define QM_REG_WRROTHERPQGRP_11_RT_OFFSET                               29890
#define QM_REG_WRROTHERPQGRP_12_RT_OFFSET                               29891
#define QM_REG_WRROTHERPQGRP_13_RT_OFFSET                               29892
#define QM_REG_WRROTHERPQGRP_14_RT_OFFSET                               29893
#define QM_REG_WRROTHERPQGRP_15_RT_OFFSET                               29894
#define QM_REG_WRROTHERGRPWEIGHT_0_RT_OFFSET                            29895
#define QM_REG_WRROTHERGRPWEIGHT_1_RT_OFFSET                            29896
#define QM_REG_WRROTHERGRPWEIGHT_2_RT_OFFSET                            29897
#define QM_REG_WRROTHERGRPWEIGHT_3_RT_OFFSET                            29898
#define QM_REG_WRRTXGRPWEIGHT_0_RT_OFFSET                               29899
#define QM_REG_WRRTXGRPWEIGHT_1_RT_OFFSET                               29900
#define QM_REG_PQTX2PF_0_RT_OFFSET                                      29901
#define QM_REG_PQTX2PF_1_RT_OFFSET                                      29902
#define QM_REG_PQTX2PF_2_RT_OFFSET                                      29903
#define QM_REG_PQTX2PF_3_RT_OFFSET                                      29904
#define QM_REG_PQTX2PF_4_RT_OFFSET                                      29905
#define QM_REG_PQTX2PF_5_RT_OFFSET                                      29906
#define QM_REG_PQTX2PF_6_RT_OFFSET                                      29907
#define QM_REG_PQTX2PF_7_RT_OFFSET                                      29908
#define QM_REG_PQTX2PF_8_RT_OFFSET                                      29909
#define QM_REG_PQTX2PF_9_RT_OFFSET                                      29910
#define QM_REG_PQTX2PF_10_RT_OFFSET                                     29911
#define QM_REG_PQTX2PF_11_RT_OFFSET                                     29912
#define QM_REG_PQTX2PF_12_RT_OFFSET                                     29913
#define QM_REG_PQTX2PF_13_RT_OFFSET                                     29914
#define QM_REG_PQTX2PF_14_RT_OFFSET                                     29915
#define QM_REG_PQTX2PF_15_RT_OFFSET                                     29916
#define QM_REG_PQTX2PF_16_RT_OFFSET                                     29917
#define QM_REG_PQTX2PF_17_RT_OFFSET                                     29918
#define QM_REG_PQTX2PF_18_RT_OFFSET                                     29919
#define QM_REG_PQTX2PF_19_RT_OFFSET                                     29920
#define QM_REG_PQTX2PF_20_RT_OFFSET                                     29921
#define QM_REG_PQTX2PF_21_RT_OFFSET                                     29922
#define QM_REG_PQTX2PF_22_RT_OFFSET                                     29923
#define QM_REG_PQTX2PF_23_RT_OFFSET                                     29924
#define QM_REG_PQTX2PF_24_RT_OFFSET                                     29925
#define QM_REG_PQTX2PF_25_RT_OFFSET                                     29926
#define QM_REG_PQTX2PF_26_RT_OFFSET                                     29927
#define QM_REG_PQTX2PF_27_RT_OFFSET                                     29928
#define QM_REG_PQTX2PF_28_RT_OFFSET                                     29929
#define QM_REG_PQTX2PF_29_RT_OFFSET                                     29930
#define QM_REG_PQTX2PF_30_RT_OFFSET                                     29931
#define QM_REG_PQTX2PF_31_RT_OFFSET                                     29932
#define QM_REG_PQTX2PF_32_RT_OFFSET                                     29933
#define QM_REG_PQTX2PF_33_RT_OFFSET                                     29934
#define QM_REG_PQTX2PF_34_RT_OFFSET                                     29935
#define QM_REG_PQTX2PF_35_RT_OFFSET                                     29936
#define QM_REG_PQTX2PF_36_RT_OFFSET                                     29937
#define QM_REG_PQTX2PF_37_RT_OFFSET                                     29938
#define QM_REG_PQTX2PF_38_RT_OFFSET                                     29939
#define QM_REG_PQTX2PF_39_RT_OFFSET                                     29940
#define QM_REG_PQTX2PF_40_RT_OFFSET                                     29941
#define QM_REG_PQTX2PF_41_RT_OFFSET                                     29942
#define QM_REG_PQTX2PF_42_RT_OFFSET                                     29943
#define QM_REG_PQTX2PF_43_RT_OFFSET                                     29944
#define QM_REG_PQTX2PF_44_RT_OFFSET                                     29945
#define QM_REG_PQTX2PF_45_RT_OFFSET                                     29946
#define QM_REG_PQTX2PF_46_RT_OFFSET                                     29947
#define QM_REG_PQTX2PF_47_RT_OFFSET                                     29948
#define QM_REG_PQTX2PF_48_RT_OFFSET                                     29949
#define QM_REG_PQTX2PF_49_RT_OFFSET                                     29950
#define QM_REG_PQTX2PF_50_RT_OFFSET                                     29951
#define QM_REG_PQTX2PF_51_RT_OFFSET                                     29952
#define QM_REG_PQTX2PF_52_RT_OFFSET                                     29953
#define QM_REG_PQTX2PF_53_RT_OFFSET                                     29954
#define QM_REG_PQTX2PF_54_RT_OFFSET                                     29955
#define QM_REG_PQTX2PF_55_RT_OFFSET                                     29956
#define QM_REG_PQTX2PF_56_RT_OFFSET                                     29957
#define QM_REG_PQTX2PF_57_RT_OFFSET                                     29958
#define QM_REG_PQTX2PF_58_RT_OFFSET                                     29959
#define QM_REG_PQTX2PF_59_RT_OFFSET                                     29960
#define QM_REG_PQTX2PF_60_RT_OFFSET                                     29961
#define QM_REG_PQTX2PF_61_RT_OFFSET                                     29962
#define QM_REG_PQTX2PF_62_RT_OFFSET                                     29963
#define QM_REG_PQTX2PF_63_RT_OFFSET                                     29964
#define QM_REG_PQOTHER2PF_0_RT_OFFSET                                   29965
#define QM_REG_PQOTHER2PF_1_RT_OFFSET                                   29966
#define QM_REG_PQOTHER2PF_2_RT_OFFSET                                   29967
#define QM_REG_PQOTHER2PF_3_RT_OFFSET                                   29968
#define QM_REG_PQOTHER2PF_4_RT_OFFSET                                   29969
#define QM_REG_PQOTHER2PF_5_RT_OFFSET                                   29970
#define QM_REG_PQOTHER2PF_6_RT_OFFSET                                   29971
#define QM_REG_PQOTHER2PF_7_RT_OFFSET                                   29972
#define QM_REG_PQOTHER2PF_8_RT_OFFSET                                   29973
#define QM_REG_PQOTHER2PF_9_RT_OFFSET                                   29974
#define QM_REG_PQOTHER2PF_10_RT_OFFSET                                  29975
#define QM_REG_PQOTHER2PF_11_RT_OFFSET                                  29976
#define QM_REG_PQOTHER2PF_12_RT_OFFSET                                  29977
#define QM_REG_PQOTHER2PF_13_RT_OFFSET                                  29978
#define QM_REG_PQOTHER2PF_14_RT_OFFSET                                  29979
#define QM_REG_PQOTHER2PF_15_RT_OFFSET                                  29980
#define QM_REG_RLGLBLPERIOD_0_RT_OFFSET                                 29981
#define QM_REG_RLGLBLPERIOD_1_RT_OFFSET                                 29982
#define QM_REG_RLGLBLPERIODTIMER_0_RT_OFFSET                            29983
#define QM_REG_RLGLBLPERIODTIMER_1_RT_OFFSET                            29984
#define QM_REG_RLGLBLPERIODSEL_0_RT_OFFSET                              29985
#define QM_REG_RLGLBLPERIODSEL_1_RT_OFFSET                              29986
#define QM_REG_RLGLBLPERIODSEL_2_RT_OFFSET                              29987
#define QM_REG_RLGLBLPERIODSEL_3_RT_OFFSET                              29988
#define QM_REG_RLGLBLPERIODSEL_4_RT_OFFSET                              29989
#define QM_REG_RLGLBLPERIODSEL_5_RT_OFFSET                              29990
#define QM_REG_RLGLBLPERIODSEL_6_RT_OFFSET                              29991
#define QM_REG_RLGLBLPERIODSEL_7_RT_OFFSET                              29992
#define QM_REG_RLGLBLINCVAL_RT_OFFSET                                   29993
#define QM_REG_RLGLBLINCVAL_RT_SIZE                                     256
#define QM_REG_RLGLBLUPPERBOUND_RT_OFFSET                               30249
#define QM_REG_RLGLBLUPPERBOUND_RT_SIZE                                 256
#define QM_REG_RLGLBLCRD_RT_OFFSET                                      30505
#define QM_REG_RLGLBLCRD_RT_SIZE                                        256
#define QM_REG_RLGLBLENABLE_RT_OFFSET                                   30761
#define QM_REG_RLPFPERIOD_RT_OFFSET                                     30762
#define QM_REG_RLPFPERIODTIMER_RT_OFFSET                                30763
#define QM_REG_RLPFINCVAL_RT_OFFSET                                     30764
#define QM_REG_RLPFINCVAL_RT_SIZE                                       16
#define QM_REG_RLPFUPPERBOUND_RT_OFFSET                                 30780
#define QM_REG_RLPFUPPERBOUND_RT_SIZE                                   16
#define QM_REG_RLPFCRD_RT_OFFSET                                        30796
#define QM_REG_RLPFCRD_RT_SIZE                                          16
#define QM_REG_RLPFENABLE_RT_OFFSET                                     30812
#define QM_REG_RLPFVOQENABLE_RT_OFFSET                                  30813
#define QM_REG_WFQPFWEIGHT_RT_OFFSET                                    30814
#define QM_REG_WFQPFWEIGHT_RT_SIZE                                      16
#define QM_REG_WFQPFUPPERBOUND_RT_OFFSET                                30830
#define QM_REG_WFQPFUPPERBOUND_RT_SIZE                                  16
#define QM_REG_WFQPFCRD_RT_OFFSET                                       30846
#define QM_REG_WFQPFCRD_RT_SIZE                                         160
#define QM_REG_WFQPFENABLE_RT_OFFSET                                    31006
#define QM_REG_WFQVPENABLE_RT_OFFSET                                    31007
#define QM_REG_BASEADDRTXPQ_RT_OFFSET                                   31008
#define QM_REG_BASEADDRTXPQ_RT_SIZE                                     512
#define QM_REG_TXPQMAP_RT_OFFSET                                        31520
#define QM_REG_TXPQMAP_RT_SIZE                                          512
#define QM_REG_WFQVPWEIGHT_RT_OFFSET                                    32032
#define QM_REG_WFQVPWEIGHT_RT_SIZE                                      512
#define QM_REG_WFQVPUPPERBOUND_RT_OFFSET                                32544
#define QM_REG_WFQVPUPPERBOUND_RT_SIZE                                  512
#define QM_REG_WFQVPCRD_RT_OFFSET                                       33056
#define QM_REG_WFQVPCRD_RT_SIZE                                         512
#define QM_REG_WFQVPMAP_RT_OFFSET                                       33568
#define QM_REG_WFQVPMAP_RT_SIZE                                         512
#define QM_REG_WFQPFCRD_MSB_RT_OFFSET                                   34080
#define QM_REG_WFQPFCRD_MSB_RT_SIZE                                     160
#define NIG_REG_LLH_CLS_TYPE_DUALMODE_RT_OFFSET                         34240
#define NIG_REG_OUTER_TAG_VALUE_LIST0_RT_OFFSET                         34241
#define NIG_REG_OUTER_TAG_VALUE_LIST1_RT_OFFSET                         34242
#define NIG_REG_OUTER_TAG_VALUE_LIST2_RT_OFFSET                         34243
#define NIG_REG_OUTER_TAG_VALUE_LIST3_RT_OFFSET                         34244
#define NIG_REG_OUTER_TAG_VALUE_MASK_RT_OFFSET                          34245
#define NIG_REG_LLH_FUNC_TAGMAC_CLS_TYPE_RT_OFFSET                      34246
#define NIG_REG_LLH_FUNC_TAG_EN_RT_OFFSET                               34247
#define NIG_REG_LLH_FUNC_TAG_EN_RT_SIZE                                 4
#define NIG_REG_LLH_FUNC_TAG_HDR_SEL_RT_OFFSET                          34251
#define NIG_REG_LLH_FUNC_TAG_HDR_SEL_RT_SIZE                            4
#define NIG_REG_LLH_FUNC_TAG_VALUE_RT_OFFSET                            34255
#define NIG_REG_LLH_FUNC_TAG_VALUE_RT_SIZE                              4
#define NIG_REG_LLH_FUNC_NO_TAG_RT_OFFSET                               34259
#define NIG_REG_LLH_FUNC_FILTER_VALUE_RT_OFFSET                         34260
#define NIG_REG_LLH_FUNC_FILTER_VALUE_RT_SIZE                           32
#define NIG_REG_LLH_FUNC_FILTER_EN_RT_OFFSET                            34292
#define NIG_REG_LLH_FUNC_FILTER_EN_RT_SIZE                              16
#define NIG_REG_LLH_FUNC_FILTER_MODE_RT_OFFSET                          34308
#define NIG_REG_LLH_FUNC_FILTER_MODE_RT_SIZE                            16
#define NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE_RT_OFFSET                 34324
#define NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE_RT_SIZE                   16
#define NIG_REG_LLH_FUNC_FILTER_HDR_SEL_RT_OFFSET                       34340
#define NIG_REG_LLH_FUNC_FILTER_HDR_SEL_RT_SIZE                         16
#define NIG_REG_TX_EDPM_CTRL_RT_OFFSET                                  34356
#define CDU_REG_CID_ADDR_PARAMS_RT_OFFSET                               34357
#define CDU_REG_SEGMENT0_PARAMS_RT_OFFSET                               34358
#define CDU_REG_SEGMENT1_PARAMS_RT_OFFSET                               34359
#define CDU_REG_PF_SEG0_TYPE_OFFSET_RT_OFFSET                           34360
#define CDU_REG_PF_SEG1_TYPE_OFFSET_RT_OFFSET                           34361
#define CDU_REG_PF_SEG2_TYPE_OFFSET_RT_OFFSET                           34362
#define CDU_REG_PF_SEG3_TYPE_OFFSET_RT_OFFSET                           34363
#define CDU_REG_PF_FL_SEG0_TYPE_OFFSET_RT_OFFSET                        34364
#define CDU_REG_PF_FL_SEG1_TYPE_OFFSET_RT_OFFSET                        34365
#define CDU_REG_PF_FL_SEG2_TYPE_OFFSET_RT_OFFSET                        34366
#define CDU_REG_PF_FL_SEG3_TYPE_OFFSET_RT_OFFSET                        34367
#define CDU_REG_VF_SEG_TYPE_OFFSET_RT_OFFSET                            34368
#define CDU_REG_VF_FL_SEG_TYPE_OFFSET_RT_OFFSET                         34369
#define PBF_REG_BTB_SHARED_AREA_SIZE_RT_OFFSET                          34370
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET                        34371
#define PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET                           34372
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ0_RT_OFFSET                    34373
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ1_RT_OFFSET                        34374
#define PBF_REG_BTB_GUARANTEED_VOQ1_RT_OFFSET                           34375
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ1_RT_OFFSET                    34376
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ2_RT_OFFSET                        34377
#define PBF_REG_BTB_GUARANTEED_VOQ2_RT_OFFSET                           34378
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ2_RT_OFFSET                    34379
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ3_RT_OFFSET                        34380
#define PBF_REG_BTB_GUARANTEED_VOQ3_RT_OFFSET                           34381
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ3_RT_OFFSET                    34382
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ4_RT_OFFSET                        34383
#define PBF_REG_BTB_GUARANTEED_VOQ4_RT_OFFSET                           34384
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ4_RT_OFFSET                    34385
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ5_RT_OFFSET                        34386
#define PBF_REG_BTB_GUARANTEED_VOQ5_RT_OFFSET                           34387
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ5_RT_OFFSET                    34388
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ6_RT_OFFSET                        34389
#define PBF_REG_BTB_GUARANTEED_VOQ6_RT_OFFSET                           34390
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ6_RT_OFFSET                    34391
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ7_RT_OFFSET                        34392
#define PBF_REG_BTB_GUARANTEED_VOQ7_RT_OFFSET                           34393
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ7_RT_OFFSET                    34394
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ8_RT_OFFSET                        34395
#define PBF_REG_BTB_GUARANTEED_VOQ8_RT_OFFSET                           34396
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ8_RT_OFFSET                    34397
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ9_RT_OFFSET                        34398
#define PBF_REG_BTB_GUARANTEED_VOQ9_RT_OFFSET                           34399
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ9_RT_OFFSET                    34400
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ10_RT_OFFSET                       34401
#define PBF_REG_BTB_GUARANTEED_VOQ10_RT_OFFSET                          34402
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ10_RT_OFFSET                   34403
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ11_RT_OFFSET                       34404
#define PBF_REG_BTB_GUARANTEED_VOQ11_RT_OFFSET                          34405
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ11_RT_OFFSET                   34406
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ12_RT_OFFSET                       34407
#define PBF_REG_BTB_GUARANTEED_VOQ12_RT_OFFSET                          34408
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ12_RT_OFFSET                   34409
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ13_RT_OFFSET                       34410
#define PBF_REG_BTB_GUARANTEED_VOQ13_RT_OFFSET                          34411
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ13_RT_OFFSET                   34412
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ14_RT_OFFSET                       34413
#define PBF_REG_BTB_GUARANTEED_VOQ14_RT_OFFSET                          34414
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ14_RT_OFFSET                   34415
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ15_RT_OFFSET                       34416
#define PBF_REG_BTB_GUARANTEED_VOQ15_RT_OFFSET                          34417
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ15_RT_OFFSET                   34418
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ16_RT_OFFSET                       34419
#define PBF_REG_BTB_GUARANTEED_VOQ16_RT_OFFSET                          34420
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ16_RT_OFFSET                   34421
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ17_RT_OFFSET                       34422
#define PBF_REG_BTB_GUARANTEED_VOQ17_RT_OFFSET                          34423
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ17_RT_OFFSET                   34424
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ18_RT_OFFSET                       34425
#define PBF_REG_BTB_GUARANTEED_VOQ18_RT_OFFSET                          34426
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ18_RT_OFFSET                   34427
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ19_RT_OFFSET                       34428
#define PBF_REG_BTB_GUARANTEED_VOQ19_RT_OFFSET                          34429
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ19_RT_OFFSET                   34430
#define XCM_REG_CON_PHY_Q3_RT_OFFSET                                    34431

#define RUNTIME_ARRAY_SIZE 34432

/* The eth storm context for the Ystorm */
struct ystorm_eth_conn_st_ctx {
	__le32 reserved[4];
};

/* The eth storm context for the Pstorm */
struct pstorm_eth_conn_st_ctx {
	__le32 reserved[8];
};

/* The eth storm context for the Xstorm */
struct xstorm_eth_conn_st_ctx {
	__le32 reserved[60];
};

struct xstorm_eth_conn_ag_ctx {
	u8	reserved0 /* cdu_validation */;
	u8	eth_state /* state */;
	u8	flags0;
#define XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_MASK            0x1
#define XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_SHIFT           0
#define XSTORM_ETH_CONN_AG_CTX_RESERVED1_MASK               0x1
#define XSTORM_ETH_CONN_AG_CTX_RESERVED1_SHIFT              1
#define XSTORM_ETH_CONN_AG_CTX_RESERVED2_MASK               0x1
#define XSTORM_ETH_CONN_AG_CTX_RESERVED2_SHIFT              2
#define XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM3_MASK            0x1
#define XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM3_SHIFT           3
#define XSTORM_ETH_CONN_AG_CTX_RESERVED3_MASK               0x1 /* bit4 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED3_SHIFT              4
#define XSTORM_ETH_CONN_AG_CTX_RESERVED4_MASK               0x1
#define XSTORM_ETH_CONN_AG_CTX_RESERVED4_SHIFT              5
#define XSTORM_ETH_CONN_AG_CTX_RESERVED5_MASK               0x1 /* bit6 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED5_SHIFT              6
#define XSTORM_ETH_CONN_AG_CTX_RESERVED6_MASK               0x1 /* bit7 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED6_SHIFT              7
	u8 flags1;
#define XSTORM_ETH_CONN_AG_CTX_RESERVED7_MASK               0x1 /* bit8 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED7_SHIFT              0
#define XSTORM_ETH_CONN_AG_CTX_RESERVED8_MASK               0x1 /* bit9 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED8_SHIFT              1
#define XSTORM_ETH_CONN_AG_CTX_RESERVED9_MASK               0x1 /* bit10 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED9_SHIFT              2
#define XSTORM_ETH_CONN_AG_CTX_BIT11_MASK                   0x1 /* bit11 */
#define XSTORM_ETH_CONN_AG_CTX_BIT11_SHIFT                  3
#define XSTORM_ETH_CONN_AG_CTX_BIT12_MASK                   0x1 /* bit12 */
#define XSTORM_ETH_CONN_AG_CTX_BIT12_SHIFT                  4
#define XSTORM_ETH_CONN_AG_CTX_BIT13_MASK                   0x1 /* bit13 */
#define XSTORM_ETH_CONN_AG_CTX_BIT13_SHIFT                  5
#define XSTORM_ETH_CONN_AG_CTX_TX_RULE_ACTIVE_MASK          0x1 /* bit14 */
#define XSTORM_ETH_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT         6
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_ACTIVE_MASK            0x1 /* bit15 */
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT           7
	u8 flags2;
#define XSTORM_ETH_CONN_AG_CTX_CF0_MASK                     0x3 /* timer0cf */
#define XSTORM_ETH_CONN_AG_CTX_CF0_SHIFT                    0
#define XSTORM_ETH_CONN_AG_CTX_CF1_MASK                     0x3 /* timer1cf */
#define XSTORM_ETH_CONN_AG_CTX_CF1_SHIFT                    2
#define XSTORM_ETH_CONN_AG_CTX_CF2_MASK                     0x3 /* timer2cf */
#define XSTORM_ETH_CONN_AG_CTX_CF2_SHIFT                    4
#define XSTORM_ETH_CONN_AG_CTX_CF3_MASK                     0x3
#define XSTORM_ETH_CONN_AG_CTX_CF3_SHIFT                    6
	u8 flags3;
#define XSTORM_ETH_CONN_AG_CTX_CF4_MASK                     0x3 /* cf4 */
#define XSTORM_ETH_CONN_AG_CTX_CF4_SHIFT                    0
#define XSTORM_ETH_CONN_AG_CTX_CF5_MASK                     0x3 /* cf5 */
#define XSTORM_ETH_CONN_AG_CTX_CF5_SHIFT                    2
#define XSTORM_ETH_CONN_AG_CTX_CF6_MASK                     0x3 /* cf6 */
#define XSTORM_ETH_CONN_AG_CTX_CF6_SHIFT                    4
#define XSTORM_ETH_CONN_AG_CTX_CF7_MASK                     0x3 /* cf7 */
#define XSTORM_ETH_CONN_AG_CTX_CF7_SHIFT                    6
	u8 flags4;
#define XSTORM_ETH_CONN_AG_CTX_CF8_MASK                     0x3 /* cf8 */
#define XSTORM_ETH_CONN_AG_CTX_CF8_SHIFT                    0
#define XSTORM_ETH_CONN_AG_CTX_CF9_MASK                     0x3 /* cf9 */
#define XSTORM_ETH_CONN_AG_CTX_CF9_SHIFT                    2
#define XSTORM_ETH_CONN_AG_CTX_CF10_MASK                    0x3 /* cf10 */
#define XSTORM_ETH_CONN_AG_CTX_CF10_SHIFT                   4
#define XSTORM_ETH_CONN_AG_CTX_CF11_MASK                    0x3 /* cf11 */
#define XSTORM_ETH_CONN_AG_CTX_CF11_SHIFT                   6
	u8 flags5;
#define XSTORM_ETH_CONN_AG_CTX_CF12_MASK                    0x3 /* cf12 */
#define XSTORM_ETH_CONN_AG_CTX_CF12_SHIFT                   0
#define XSTORM_ETH_CONN_AG_CTX_CF13_MASK                    0x3 /* cf13 */
#define XSTORM_ETH_CONN_AG_CTX_CF13_SHIFT                   2
#define XSTORM_ETH_CONN_AG_CTX_CF14_MASK                    0x3 /* cf14 */
#define XSTORM_ETH_CONN_AG_CTX_CF14_SHIFT                   4
#define XSTORM_ETH_CONN_AG_CTX_CF15_MASK                    0x3 /* cf15 */
#define XSTORM_ETH_CONN_AG_CTX_CF15_SHIFT                   6
	u8 flags6;
#define XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_MASK        0x3 /* cf16 */
#define XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_SHIFT       0
#define XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_MASK        0x3
#define XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_SHIFT       2
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_MASK                   0x3 /* cf18 */
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_SHIFT                  4
#define XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_MASK            0x3 /* cf19 */
#define XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_SHIFT           6
	u8 flags7;
#define XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_MASK                0x3 /* cf20 */
#define XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_SHIFT               0
#define XSTORM_ETH_CONN_AG_CTX_RESERVED10_MASK              0x3 /* cf21 */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED10_SHIFT             2
#define XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_MASK               0x3 /* cf22 */
#define XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_SHIFT              4
#define XSTORM_ETH_CONN_AG_CTX_CF0EN_MASK                   0x1 /* cf0en */
#define XSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT                  6
#define XSTORM_ETH_CONN_AG_CTX_CF1EN_MASK                   0x1 /* cf1en */
#define XSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT                  7
	u8 flags8;
#define XSTORM_ETH_CONN_AG_CTX_CF2EN_MASK                   0x1 /* cf2en */
#define XSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT                  0
#define XSTORM_ETH_CONN_AG_CTX_CF3EN_MASK                   0x1 /* cf3en */
#define XSTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT                  1
#define XSTORM_ETH_CONN_AG_CTX_CF4EN_MASK                   0x1 /* cf4en */
#define XSTORM_ETH_CONN_AG_CTX_CF4EN_SHIFT                  2
#define XSTORM_ETH_CONN_AG_CTX_CF5EN_MASK                   0x1 /* cf5en */
#define XSTORM_ETH_CONN_AG_CTX_CF5EN_SHIFT                  3
#define XSTORM_ETH_CONN_AG_CTX_CF6EN_MASK                   0x1 /* cf6en */
#define XSTORM_ETH_CONN_AG_CTX_CF6EN_SHIFT                  4
#define XSTORM_ETH_CONN_AG_CTX_CF7EN_MASK                   0x1 /* cf7en */
#define XSTORM_ETH_CONN_AG_CTX_CF7EN_SHIFT                  5
#define XSTORM_ETH_CONN_AG_CTX_CF8EN_MASK                   0x1 /* cf8en */
#define XSTORM_ETH_CONN_AG_CTX_CF8EN_SHIFT                  6
#define XSTORM_ETH_CONN_AG_CTX_CF9EN_MASK                   0x1 /* cf9en */
#define XSTORM_ETH_CONN_AG_CTX_CF9EN_SHIFT                  7
	u8 flags9;
#define XSTORM_ETH_CONN_AG_CTX_CF10EN_MASK                  0x1 /* cf10en */
#define XSTORM_ETH_CONN_AG_CTX_CF10EN_SHIFT                 0
#define XSTORM_ETH_CONN_AG_CTX_CF11EN_MASK                  0x1 /* cf11en */
#define XSTORM_ETH_CONN_AG_CTX_CF11EN_SHIFT                 1
#define XSTORM_ETH_CONN_AG_CTX_CF12EN_MASK                  0x1 /* cf12en */
#define XSTORM_ETH_CONN_AG_CTX_CF12EN_SHIFT                 2
#define XSTORM_ETH_CONN_AG_CTX_CF13EN_MASK                  0x1 /* cf13en */
#define XSTORM_ETH_CONN_AG_CTX_CF13EN_SHIFT                 3
#define XSTORM_ETH_CONN_AG_CTX_CF14EN_MASK                  0x1 /* cf14en */
#define XSTORM_ETH_CONN_AG_CTX_CF14EN_SHIFT                 4
#define XSTORM_ETH_CONN_AG_CTX_CF15EN_MASK                  0x1 /* cf15en */
#define XSTORM_ETH_CONN_AG_CTX_CF15EN_SHIFT                 5
#define XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_MASK     0x1 /* cf16en */
#define XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_SHIFT    6
#define XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_EN_MASK     0x1
#define XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_EN_SHIFT    7
	u8 flags10;
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_EN_MASK                0x1 /* cf18en */
#define XSTORM_ETH_CONN_AG_CTX_DQ_CF_EN_SHIFT               0
#define XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_EN_MASK         0x1 /* cf19en */
#define XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT        1
#define XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_EN_MASK             0x1 /* cf20en */
#define XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT            2
#define XSTORM_ETH_CONN_AG_CTX_RESERVED11_MASK              0x1 /* cf21en */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED11_SHIFT             3
#define XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_EN_MASK            0x1 /* cf22en */
#define XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_EN_SHIFT           4
#define XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_MASK  0x1 /* cf23en */
#define XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_SHIFT 5
#define XSTORM_ETH_CONN_AG_CTX_RESERVED12_MASK              0x1 /* rule0en */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED12_SHIFT             6
#define XSTORM_ETH_CONN_AG_CTX_RESERVED13_MASK              0x1 /* rule1en */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED13_SHIFT             7
	u8 flags11;
#define XSTORM_ETH_CONN_AG_CTX_RESERVED14_MASK              0x1 /* rule2en */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED14_SHIFT             0
#define XSTORM_ETH_CONN_AG_CTX_RESERVED15_MASK              0x1 /* rule3en */
#define XSTORM_ETH_CONN_AG_CTX_RESERVED15_SHIFT             1
#define XSTORM_ETH_CONN_AG_CTX_TX_DEC_RULE_EN_MASK          0x1 /* rule4en */
#define XSTORM_ETH_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT         2
#define XSTORM_ETH_CONN_AG_CTX_RULE5EN_MASK                 0x1 /* rule5en */
#define XSTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT                3
#define XSTORM_ETH_CONN_AG_CTX_RULE6EN_MASK                 0x1 /* rule6en */
#define XSTORM_ETH_CONN_AG_CTX_RULE6EN_SHIFT                4
#define XSTORM_ETH_CONN_AG_CTX_RULE7EN_MASK                 0x1 /* rule7en */
#define XSTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT                5
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED1_MASK            0x1 /* rule8en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED1_SHIFT           6
#define XSTORM_ETH_CONN_AG_CTX_RULE9EN_MASK                 0x1 /* rule9en */
#define XSTORM_ETH_CONN_AG_CTX_RULE9EN_SHIFT                7
	u8 flags12;
#define XSTORM_ETH_CONN_AG_CTX_RULE10EN_MASK                0x1 /* rule10en */
#define XSTORM_ETH_CONN_AG_CTX_RULE10EN_SHIFT               0
#define XSTORM_ETH_CONN_AG_CTX_RULE11EN_MASK                0x1 /* rule11en */
#define XSTORM_ETH_CONN_AG_CTX_RULE11EN_SHIFT               1
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED2_MASK            0x1 /* rule12en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED2_SHIFT           2
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED3_MASK            0x1 /* rule13en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED3_SHIFT           3
#define XSTORM_ETH_CONN_AG_CTX_RULE14EN_MASK                0x1 /* rule14en */
#define XSTORM_ETH_CONN_AG_CTX_RULE14EN_SHIFT               4
#define XSTORM_ETH_CONN_AG_CTX_RULE15EN_MASK                0x1 /* rule15en */
#define XSTORM_ETH_CONN_AG_CTX_RULE15EN_SHIFT               5
#define XSTORM_ETH_CONN_AG_CTX_RULE16EN_MASK                0x1 /* rule16en */
#define XSTORM_ETH_CONN_AG_CTX_RULE16EN_SHIFT               6
#define XSTORM_ETH_CONN_AG_CTX_RULE17EN_MASK                0x1 /* rule17en */
#define XSTORM_ETH_CONN_AG_CTX_RULE17EN_SHIFT               7
	u8 flags13;
#define XSTORM_ETH_CONN_AG_CTX_RULE18EN_MASK                0x1 /* rule18en */
#define XSTORM_ETH_CONN_AG_CTX_RULE18EN_SHIFT               0
#define XSTORM_ETH_CONN_AG_CTX_RULE19EN_MASK                0x1 /* rule19en */
#define XSTORM_ETH_CONN_AG_CTX_RULE19EN_SHIFT               1
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED4_MASK            0x1 /* rule20en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED4_SHIFT           2
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED5_MASK            0x1 /* rule21en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED5_SHIFT           3
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED6_MASK            0x1 /* rule22en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED6_SHIFT           4
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED7_MASK            0x1 /* rule23en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED7_SHIFT           5
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED8_MASK            0x1 /* rule24en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED8_SHIFT           6
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED9_MASK            0x1 /* rule25en */
#define XSTORM_ETH_CONN_AG_CTX_A0_RESERVED9_SHIFT           7
	u8 flags14;
#define XSTORM_ETH_CONN_AG_CTX_EDPM_USE_EXT_HDR_MASK        0x1 /* bit16 */
#define XSTORM_ETH_CONN_AG_CTX_EDPM_USE_EXT_HDR_SHIFT       0
#define XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_MASK      0x1 /* bit17 */
#define XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_SHIFT     1
#define XSTORM_ETH_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_MASK    0x1 /* bit18 */
#define XSTORM_ETH_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_SHIFT   2
#define XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_MASK    0x1 /* bit19 */
#define XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_SHIFT   3
#define XSTORM_ETH_CONN_AG_CTX_L2_EDPM_ENABLE_MASK          0x1 /* bit20 */
#define XSTORM_ETH_CONN_AG_CTX_L2_EDPM_ENABLE_SHIFT         4
#define XSTORM_ETH_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK        0x1 /* bit21 */
#define XSTORM_ETH_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT       5
#define XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_MASK              0x3 /* cf23 */
#define XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_SHIFT             6
	u8	edpm_event_id /* byte2 */;
	__le16	physical_q0 /* physical_q0 */;
	__le16	word1 /* physical_q1 */;
	__le16	edpm_num_bds /* physical_q2 */;
	__le16	tx_bd_cons /* word3 */;
	__le16	tx_bd_prod /* word4 */;
	__le16	go_to_bd_cons /* word5 */;
	__le16	conn_dpi /* conn_dpi */;
	u8	byte3 /* byte3 */;
	u8	byte4 /* byte4 */;
	u8	byte5 /* byte5 */;
	u8	byte6 /* byte6 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le32	reg4 /* reg4 */;
	__le32	reg5 /* cf_array0 */;
	__le32	reg6 /* cf_array1 */;
	__le16	word7 /* word7 */;
	__le16	word8 /* word8 */;
	__le16	word9 /* word9 */;
	__le16	word10 /* word10 */;
	__le32	reg7 /* reg7 */;
	__le32	reg8 /* reg8 */;
	__le32	reg9 /* reg9 */;
	u8	byte7 /* byte7 */;
	u8	byte8 /* byte8 */;
	u8	byte9 /* byte9 */;
	u8	byte10 /* byte10 */;
	u8	byte11 /* byte11 */;
	u8	byte12 /* byte12 */;
	u8	byte13 /* byte13 */;
	u8	byte14 /* byte14 */;
	u8	byte15 /* byte15 */;
	u8	byte16 /* byte16 */;
	__le16	word11 /* word11 */;
	__le32	reg10 /* reg10 */;
	__le32	reg11 /* reg11 */;
	__le32	reg12 /* reg12 */;
	__le32	reg13 /* reg13 */;
	__le32	reg14 /* reg14 */;
	__le32	reg15 /* reg15 */;
	__le32	reg16 /* reg16 */;
	__le32	reg17 /* reg17 */;
	__le32	reg18 /* reg18 */;
	__le32	reg19 /* reg19 */;
	__le16	word12 /* word12 */;
	__le16	word13 /* word13 */;
	__le16	word14 /* word14 */;
	__le16	word15 /* word15 */;
};

/* The eth storm context for the Tstorm */
struct tstorm_eth_conn_st_ctx {
	__le32 reserved[4];
};

/* The eth storm context for the Mstorm */
struct mstorm_eth_conn_st_ctx {
	__le32 reserved[8];
};

/* The eth storm context for the Ustorm */
struct ustorm_eth_conn_st_ctx {
	__le32 reserved[40];
};

/* eth connection context */
struct eth_conn_context {
	struct ystorm_eth_conn_st_ctx	ystorm_st_context;
	struct regpair			ystorm_st_padding[2] /* padding */;
	struct pstorm_eth_conn_st_ctx	pstorm_st_context;
	struct regpair			pstorm_st_padding[2] /* padding */;
	struct xstorm_eth_conn_st_ctx	xstorm_st_context;
	struct xstorm_eth_conn_ag_ctx	xstorm_ag_context;
	struct tstorm_eth_conn_st_ctx	tstorm_st_context;
	struct regpair			tstorm_st_padding[2] /* padding */;
	struct mstorm_eth_conn_st_ctx	mstorm_st_context;
	struct ustorm_eth_conn_st_ctx	ustorm_st_context;
};

enum eth_filter_action {
	ETH_FILTER_ACTION_REMOVE,
	ETH_FILTER_ACTION_ADD,
	ETH_FILTER_ACTION_REPLACE,
	MAX_ETH_FILTER_ACTION
};

struct eth_filter_cmd {
	u8      type /* Filter Type (MAC/VLAN/Pair/VNI) */;
	u8      vport_id /* the vport id */;
	u8      action /* filter command action: add/remove/replace */;
	u8      reserved0;
	__le32  vni;
	__le16  mac_lsb;
	__le16  mac_mid;
	__le16  mac_msb;
	__le16  vlan_id;
};

struct eth_filter_cmd_header {
	u8      rx;
	u8      tx;
	u8      cmd_cnt;
	u8      assert_on_error;
	u8      reserved1[4];
};

enum eth_filter_type {
	ETH_FILTER_TYPE_MAC,
	ETH_FILTER_TYPE_VLAN,
	ETH_FILTER_TYPE_PAIR,
	ETH_FILTER_TYPE_INNER_MAC,
	ETH_FILTER_TYPE_INNER_VLAN,
	ETH_FILTER_TYPE_INNER_PAIR,
	ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR,
	ETH_FILTER_TYPE_MAC_VNI_PAIR,
	ETH_FILTER_TYPE_VNI,
	MAX_ETH_FILTER_TYPE
};

enum eth_ramrod_cmd_id {
	ETH_RAMROD_UNUSED,
	ETH_RAMROD_VPORT_START /* VPort Start Ramrod */,
	ETH_RAMROD_VPORT_UPDATE /* VPort Update Ramrod */,
	ETH_RAMROD_VPORT_STOP /* VPort Stop Ramrod */,
	ETH_RAMROD_RX_QUEUE_START /* RX Queue Start Ramrod */,
	ETH_RAMROD_RX_QUEUE_STOP /* RX Queue Stop Ramrod */,
	ETH_RAMROD_TX_QUEUE_START /* TX Queue Start Ramrod */,
	ETH_RAMROD_TX_QUEUE_STOP /* TX Queue Stop Ramrod */,
	ETH_RAMROD_FILTERS_UPDATE /* Add or Remove Mac/Vlan/Pair filters */,
	ETH_RAMROD_RX_QUEUE_UPDATE /* RX Queue Update Ramrod */,
	ETH_RAMROD_RESERVED,
	ETH_RAMROD_RESERVED2,
	ETH_RAMROD_RESERVED3,
	ETH_RAMROD_RESERVED4,
	ETH_RAMROD_RESERVED5,
	ETH_RAMROD_RESERVED6,
	ETH_RAMROD_RESERVED7,
	ETH_RAMROD_RESERVED8,
	MAX_ETH_RAMROD_CMD_ID
};

struct eth_vport_rss_config {
	__le16 capabilities;
#define ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY_MASK	0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY_SHIFT       0
#define ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY_MASK	0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY_SHIFT       1
#define ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY_MASK    0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY_SHIFT   2
#define ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY_MASK    0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY_SHIFT   3
#define ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY_MASK    0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY_SHIFT   4
#define ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY_MASK    0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY_SHIFT   5
#define ETH_VPORT_RSS_CONFIG_EN_5_TUPLE_CAPABILITY_MASK  0x1
#define ETH_VPORT_RSS_CONFIG_EN_5_TUPLE_CAPABILITY_SHIFT 6
#define ETH_VPORT_RSS_CONFIG_CALC_4TUP_TCP_FRAG_MASK     0x1
#define ETH_VPORT_RSS_CONFIG_CALC_4TUP_TCP_FRAG_SHIFT    7
#define ETH_VPORT_RSS_CONFIG_CALC_4TUP_UDP_FRAG_MASK     0x1
#define ETH_VPORT_RSS_CONFIG_CALC_4TUP_UDP_FRAG_SHIFT    8
#define ETH_VPORT_RSS_CONFIG_RESERVED0_MASK	      0x7F
#define ETH_VPORT_RSS_CONFIG_RESERVED0_SHIFT	     9
	u8      rss_id;
	u8      rss_mode;
	u8      update_rss_key;
	u8      update_rss_ind_table;
	u8      update_rss_capabilities;
	u8      tbl_size;
	__le32  reserved2[2];
	__le16  indirection_table[ETH_RSS_IND_TABLE_ENTRIES_NUM];
	__le32  rss_key[ETH_RSS_KEY_SIZE_REGS];
	__le32  reserved3[2];
};

enum eth_vport_rss_mode {
	ETH_VPORT_RSS_MODE_DISABLED,
	ETH_VPORT_RSS_MODE_REGULAR,
	MAX_ETH_VPORT_RSS_MODE
};

struct eth_vport_rx_mode {
	__le16 state;
#define ETH_VPORT_RX_MODE_UCAST_DROP_ALL_MASK	  0x1
#define ETH_VPORT_RX_MODE_UCAST_DROP_ALL_SHIFT	 0
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_ALL_MASK	0x1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_ALL_SHIFT       1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED_MASK  0x1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED_SHIFT 2
#define ETH_VPORT_RX_MODE_MCAST_DROP_ALL_MASK	  0x1
#define ETH_VPORT_RX_MODE_MCAST_DROP_ALL_SHIFT	 3
#define ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL_MASK	0x1
#define ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL_SHIFT       4
#define ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL_MASK	0x1
#define ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL_SHIFT       5
#define ETH_VPORT_RX_MODE_RESERVED1_MASK	       0x3FF
#define ETH_VPORT_RX_MODE_RESERVED1_SHIFT	      6
	__le16 reserved2[3];
};

struct eth_vport_tpa_param {
	u64     reserved[2];
};

struct eth_vport_tx_mode {
	__le16 state;
#define ETH_VPORT_TX_MODE_UCAST_DROP_ALL_MASK    0x1
#define ETH_VPORT_TX_MODE_UCAST_DROP_ALL_SHIFT   0
#define ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL_MASK  0x1
#define ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL_SHIFT 1
#define ETH_VPORT_TX_MODE_MCAST_DROP_ALL_MASK    0x1
#define ETH_VPORT_TX_MODE_MCAST_DROP_ALL_SHIFT   2
#define ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL_MASK  0x1
#define ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL_SHIFT 3
#define ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL_MASK  0x1
#define ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL_SHIFT 4
#define ETH_VPORT_TX_MODE_RESERVED1_MASK	 0x7FF
#define ETH_VPORT_TX_MODE_RESERVED1_SHIFT	5
	__le16 reserved2[3];
};

struct rx_queue_start_ramrod_data {
	__le16	  rx_queue_id;
	__le16	  num_of_pbl_pages;
	__le16	  bd_max_bytes;
	__le16	  sb_id;
	u8	      sb_index;
	u8	      vport_id;
	u8	      default_rss_queue_flg;
	u8	      complete_cqe_flg;
	u8	      complete_event_flg;
	u8	      stats_counter_id;
	u8	      pin_context;
	u8	      pxp_tph_valid_bd;
	u8	      pxp_tph_valid_pkt;
	u8	      pxp_st_hint;
	__le16	  pxp_st_index;
	u8	      reserved[4];
	struct regpair  cqe_pbl_addr;
	struct regpair  bd_base;
	struct regpair  sge_base;
};

struct rx_queue_stop_ramrod_data {
	__le16  rx_queue_id;
	u8      complete_cqe_flg;
	u8      complete_event_flg;
	u8      vport_id;
	u8      reserved[3];
};

struct rx_queue_update_ramrod_data {
	__le16	  rx_queue_id;
	u8	      complete_cqe_flg;
	u8	      complete_event_flg;
	u8	      init_sge_ring_flg;
	u8	      vport_id;
	u8	      pxp_tph_valid_sge;
	u8	      pxp_st_hint;
	__le16	  pxp_st_index;
	u8	      reserved[6];
	struct regpair  sge_base;
};

struct tx_queue_start_ramrod_data {
	__le16  sb_id;
	u8      sb_index;
	u8      vport_id;
	u8      tc;
	u8      stats_counter_id;
	__le16  qm_pq_id;
	u8      flags;
#define TX_QUEUE_START_RAMROD_DATA_DISABLE_OPPORTUNISTIC_MASK  0x1
#define TX_QUEUE_START_RAMROD_DATA_DISABLE_OPPORTUNISTIC_SHIFT 0
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_PKT_DUP_MASK      0x1
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_PKT_DUP_SHIFT     1
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_TX_DEST_MASK      0x1
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_TX_DEST_SHIFT     2
#define TX_QUEUE_START_RAMROD_DATA_RESERVED0_MASK	      0x1F
#define TX_QUEUE_START_RAMROD_DATA_RESERVED0_SHIFT	     3
	u8	      pin_context;
	u8	      pxp_tph_valid_bd;
	u8	      pxp_tph_valid_pkt;
	__le16	  pxp_st_index;
	u8	      pxp_st_hint;
	u8	      reserved1[3];
	__le16	  queue_zone_id;
	__le16	  test_dup_count;
	__le16	  pbl_size;
	struct regpair  pbl_base_addr;
};

struct tx_queue_stop_ramrod_data {
	__le16 reserved[4];
};

struct vport_filter_update_ramrod_data {
	struct eth_filter_cmd_header    filter_cmd_hdr;
	struct eth_filter_cmd	   filter_cmds[ETH_FILTER_RULES_COUNT];
};

struct vport_start_ramrod_data {
	u8			      vport_id;
	u8			      sw_fid;
	__le16			  mtu;
	u8			      drop_ttl0_en;
	u8			      inner_vlan_removal_en;
	struct eth_vport_rx_mode	rx_mode;
	struct eth_vport_tx_mode	tx_mode;
	struct eth_vport_tpa_param      tpa_param;
	__le16			  sge_buff_size;
	u8			      max_sges_num;
	u8			      tx_switching_en;
	u8			      anti_spoofing_en;
	u8			      default_vlan_en;
	u8			      handle_ptp_pkts;
	u8			      silent_vlan_removal_en;
	__le16			  default_vlan;
	u8			      untagged;
	u8			      reserved[7];
};

struct vport_stop_ramrod_data {
	u8      vport_id;
	u8      reserved[7];
};

struct vport_update_ramrod_data_cmn {
	u8      vport_id;
	u8      update_rx_active_flg;
	u8      rx_active_flg;
	u8      update_tx_active_flg;
	u8      tx_active_flg;
	u8      update_rx_mode_flg;
	u8      update_tx_mode_flg;
	u8      update_approx_mcast_flg;
	u8      update_rss_flg;
	u8      update_inner_vlan_removal_en_flg;
	u8      inner_vlan_removal_en;
	u8      update_tpa_param_flg;
	u8      update_tpa_en_flg;
	u8      update_sge_param_flg;
	__le16  sge_buff_size;
	u8      max_sges_num;
	u8      update_tx_switching_en_flg;
	u8      tx_switching_en;
	u8      update_anti_spoofing_en_flg;
	u8      anti_spoofing_en;
	u8      update_handle_ptp_pkts;
	u8      handle_ptp_pkts;
	u8      update_default_vlan_en_flg;
	u8      default_vlan_en;
	u8      update_default_vlan_flg;
	__le16  default_vlan;
	u8      update_accept_any_vlan_flg;
	u8      accept_any_vlan;
	u8      silent_vlan_removal_en;
	u8      reserved;
};

struct vport_update_ramrod_mcast {
	__le32 bins[ETH_MULTICAST_MAC_BINS_IN_REGS];
};

struct vport_update_ramrod_data {
	struct vport_update_ramrod_data_cmn     common;
	struct eth_vport_rx_mode		rx_mode;
	struct eth_vport_tx_mode		tx_mode;
	struct eth_vport_tpa_param	      tpa_param;
	struct vport_update_ramrod_mcast	approx_mcast;
	struct eth_vport_rss_config	     rss_config;
};

struct mstorm_eth_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define MSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_MASK  0x1   /* exist_in_qm0 */
#define MSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_SHIFT 0
#define MSTORM_ETH_CONN_AG_CTX_BIT1_MASK          0x1   /* exist_in_qm1 */
#define MSTORM_ETH_CONN_AG_CTX_BIT1_SHIFT         1
#define MSTORM_ETH_CONN_AG_CTX_CF0_MASK           0x3   /* cf0 */
#define MSTORM_ETH_CONN_AG_CTX_CF0_SHIFT          2
#define MSTORM_ETH_CONN_AG_CTX_CF1_MASK           0x3   /* cf1 */
#define MSTORM_ETH_CONN_AG_CTX_CF1_SHIFT          4
#define MSTORM_ETH_CONN_AG_CTX_CF2_MASK           0x3   /* cf2 */
#define MSTORM_ETH_CONN_AG_CTX_CF2_SHIFT          6
	u8 flags1;
#define MSTORM_ETH_CONN_AG_CTX_CF0EN_MASK         0x1   /* cf0en */
#define MSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT        0
#define MSTORM_ETH_CONN_AG_CTX_CF1EN_MASK         0x1   /* cf1en */
#define MSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT        1
#define MSTORM_ETH_CONN_AG_CTX_CF2EN_MASK         0x1   /* cf2en */
#define MSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT        2
#define MSTORM_ETH_CONN_AG_CTX_RULE0EN_MASK       0x1   /* rule0en */
#define MSTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT      3
#define MSTORM_ETH_CONN_AG_CTX_RULE1EN_MASK       0x1   /* rule1en */
#define MSTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT      4
#define MSTORM_ETH_CONN_AG_CTX_RULE2EN_MASK       0x1   /* rule2en */
#define MSTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT      5
#define MSTORM_ETH_CONN_AG_CTX_RULE3EN_MASK       0x1   /* rule3en */
#define MSTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT      6
#define MSTORM_ETH_CONN_AG_CTX_RULE4EN_MASK       0x1   /* rule4en */
#define MSTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT      7
	__le16	word0 /* word0 */;
	__le16	word1 /* word1 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
};

struct tstorm_eth_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define TSTORM_ETH_CONN_AG_CTX_BIT0_MASK      0x1       /* exist_in_qm0 */
#define TSTORM_ETH_CONN_AG_CTX_BIT0_SHIFT     0
#define TSTORM_ETH_CONN_AG_CTX_BIT1_MASK      0x1       /* exist_in_qm1 */
#define TSTORM_ETH_CONN_AG_CTX_BIT1_SHIFT     1
#define TSTORM_ETH_CONN_AG_CTX_BIT2_MASK      0x1       /* bit2 */
#define TSTORM_ETH_CONN_AG_CTX_BIT2_SHIFT     2
#define TSTORM_ETH_CONN_AG_CTX_BIT3_MASK      0x1       /* bit3 */
#define TSTORM_ETH_CONN_AG_CTX_BIT3_SHIFT     3
#define TSTORM_ETH_CONN_AG_CTX_BIT4_MASK      0x1       /* bit4 */
#define TSTORM_ETH_CONN_AG_CTX_BIT4_SHIFT     4
#define TSTORM_ETH_CONN_AG_CTX_BIT5_MASK      0x1       /* bit5 */
#define TSTORM_ETH_CONN_AG_CTX_BIT5_SHIFT     5
#define TSTORM_ETH_CONN_AG_CTX_CF0_MASK       0x3       /* timer0cf */
#define TSTORM_ETH_CONN_AG_CTX_CF0_SHIFT      6
	u8 flags1;
#define TSTORM_ETH_CONN_AG_CTX_CF1_MASK       0x3       /* timer1cf */
#define TSTORM_ETH_CONN_AG_CTX_CF1_SHIFT      0
#define TSTORM_ETH_CONN_AG_CTX_CF2_MASK       0x3       /* timer2cf */
#define TSTORM_ETH_CONN_AG_CTX_CF2_SHIFT      2
#define TSTORM_ETH_CONN_AG_CTX_CF3_MASK       0x3       /* timer_stop_all */
#define TSTORM_ETH_CONN_AG_CTX_CF3_SHIFT      4
#define TSTORM_ETH_CONN_AG_CTX_CF4_MASK       0x3       /* cf4 */
#define TSTORM_ETH_CONN_AG_CTX_CF4_SHIFT      6
	u8 flags2;
#define TSTORM_ETH_CONN_AG_CTX_CF5_MASK       0x3       /* cf5 */
#define TSTORM_ETH_CONN_AG_CTX_CF5_SHIFT      0
#define TSTORM_ETH_CONN_AG_CTX_CF6_MASK       0x3       /* cf6 */
#define TSTORM_ETH_CONN_AG_CTX_CF6_SHIFT      2
#define TSTORM_ETH_CONN_AG_CTX_CF7_MASK       0x3       /* cf7 */
#define TSTORM_ETH_CONN_AG_CTX_CF7_SHIFT      4
#define TSTORM_ETH_CONN_AG_CTX_CF8_MASK       0x3       /* cf8 */
#define TSTORM_ETH_CONN_AG_CTX_CF8_SHIFT      6
	u8 flags3;
#define TSTORM_ETH_CONN_AG_CTX_CF9_MASK       0x3       /* cf9 */
#define TSTORM_ETH_CONN_AG_CTX_CF9_SHIFT      0
#define TSTORM_ETH_CONN_AG_CTX_CF10_MASK      0x3       /* cf10 */
#define TSTORM_ETH_CONN_AG_CTX_CF10_SHIFT     2
#define TSTORM_ETH_CONN_AG_CTX_CF0EN_MASK     0x1       /* cf0en */
#define TSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT    4
#define TSTORM_ETH_CONN_AG_CTX_CF1EN_MASK     0x1       /* cf1en */
#define TSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT    5
#define TSTORM_ETH_CONN_AG_CTX_CF2EN_MASK     0x1       /* cf2en */
#define TSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT    6
#define TSTORM_ETH_CONN_AG_CTX_CF3EN_MASK     0x1       /* cf3en */
#define TSTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT    7
	u8 flags4;
#define TSTORM_ETH_CONN_AG_CTX_CF4EN_MASK     0x1       /* cf4en */
#define TSTORM_ETH_CONN_AG_CTX_CF4EN_SHIFT    0
#define TSTORM_ETH_CONN_AG_CTX_CF5EN_MASK     0x1       /* cf5en */
#define TSTORM_ETH_CONN_AG_CTX_CF5EN_SHIFT    1
#define TSTORM_ETH_CONN_AG_CTX_CF6EN_MASK     0x1       /* cf6en */
#define TSTORM_ETH_CONN_AG_CTX_CF6EN_SHIFT    2
#define TSTORM_ETH_CONN_AG_CTX_CF7EN_MASK     0x1       /* cf7en */
#define TSTORM_ETH_CONN_AG_CTX_CF7EN_SHIFT    3
#define TSTORM_ETH_CONN_AG_CTX_CF8EN_MASK     0x1       /* cf8en */
#define TSTORM_ETH_CONN_AG_CTX_CF8EN_SHIFT    4
#define TSTORM_ETH_CONN_AG_CTX_CF9EN_MASK     0x1       /* cf9en */
#define TSTORM_ETH_CONN_AG_CTX_CF9EN_SHIFT    5
#define TSTORM_ETH_CONN_AG_CTX_CF10EN_MASK    0x1       /* cf10en */
#define TSTORM_ETH_CONN_AG_CTX_CF10EN_SHIFT   6
#define TSTORM_ETH_CONN_AG_CTX_RULE0EN_MASK   0x1       /* rule0en */
#define TSTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT  7
	u8 flags5;
#define TSTORM_ETH_CONN_AG_CTX_RULE1EN_MASK   0x1       /* rule1en */
#define TSTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT  0
#define TSTORM_ETH_CONN_AG_CTX_RULE2EN_MASK   0x1       /* rule2en */
#define TSTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT  1
#define TSTORM_ETH_CONN_AG_CTX_RULE3EN_MASK   0x1       /* rule3en */
#define TSTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT  2
#define TSTORM_ETH_CONN_AG_CTX_RULE4EN_MASK   0x1       /* rule4en */
#define TSTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT  3
#define TSTORM_ETH_CONN_AG_CTX_RULE5EN_MASK   0x1       /* rule5en */
#define TSTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT  4
#define TSTORM_ETH_CONN_AG_CTX_RX_BD_EN_MASK  0x1       /* rule6en */
#define TSTORM_ETH_CONN_AG_CTX_RX_BD_EN_SHIFT 5
#define TSTORM_ETH_CONN_AG_CTX_RULE7EN_MASK   0x1       /* rule7en */
#define TSTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT  6
#define TSTORM_ETH_CONN_AG_CTX_RULE8EN_MASK   0x1       /* rule8en */
#define TSTORM_ETH_CONN_AG_CTX_RULE8EN_SHIFT  7
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le32	reg4 /* reg4 */;
	__le32	reg5 /* reg5 */;
	__le32	reg6 /* reg6 */;
	__le32	reg7 /* reg7 */;
	__le32	reg8 /* reg8 */;
	u8	byte2 /* byte2 */;
	u8	byte3 /* byte3 */;
	__le16	rx_bd_cons /* word0 */;
	u8	byte4 /* byte4 */;
	u8	byte5 /* byte5 */;
	__le16	rx_bd_prod /* word1 */;
	__le16	word2 /* conn_dpi */;
	__le16	word3 /* word3 */;
	__le32	reg9 /* reg9 */;
	__le32	reg10 /* reg10 */;
};

struct ustorm_eth_conn_ag_ctx {
	u8	byte0 /* cdu_validation */;
	u8	byte1 /* state */;
	u8	flags0;
#define USTORM_ETH_CONN_AG_CTX_BIT0_MASK                  0x1
#define USTORM_ETH_CONN_AG_CTX_BIT0_SHIFT                 0
#define USTORM_ETH_CONN_AG_CTX_BIT1_MASK                  0x1
#define USTORM_ETH_CONN_AG_CTX_BIT1_SHIFT                 1
#define USTORM_ETH_CONN_AG_CTX_CF0_MASK                   0x3   /* timer0cf */
#define USTORM_ETH_CONN_AG_CTX_CF0_SHIFT                  2
#define USTORM_ETH_CONN_AG_CTX_CF1_MASK                   0x3   /* timer1cf */
#define USTORM_ETH_CONN_AG_CTX_CF1_SHIFT                  4
#define USTORM_ETH_CONN_AG_CTX_CF2_MASK                   0x3   /* timer2cf */
#define USTORM_ETH_CONN_AG_CTX_CF2_SHIFT                  6
	u8 flags1;
#define USTORM_ETH_CONN_AG_CTX_CF3_MASK                   0x3
#define USTORM_ETH_CONN_AG_CTX_CF3_SHIFT                  0
#define USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_MASK             0x3   /* cf4 */
#define USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_SHIFT            2
#define USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_MASK             0x3   /* cf5 */
#define USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_SHIFT            4
#define USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_MASK     0x3   /* cf6 */
#define USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_SHIFT    6
	u8 flags2;
#define USTORM_ETH_CONN_AG_CTX_CF0EN_MASK                 0x1   /* cf0en */
#define USTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT                0
#define USTORM_ETH_CONN_AG_CTX_CF1EN_MASK                 0x1   /* cf1en */
#define USTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT                1
#define USTORM_ETH_CONN_AG_CTX_CF2EN_MASK                 0x1   /* cf2en */
#define USTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT                2
#define USTORM_ETH_CONN_AG_CTX_CF3EN_MASK                 0x1   /* cf3en */
#define USTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT                3
#define USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_EN_MASK          0x1   /* cf4en */
#define USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_EN_SHIFT         4
#define USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_EN_MASK          0x1   /* cf5en */
#define USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_EN_SHIFT         5
#define USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_MASK  0x1   /* cf6en */
#define USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_SHIFT 6
#define USTORM_ETH_CONN_AG_CTX_RULE0EN_MASK               0x1   /* rule0en */
#define USTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags3;
#define USTORM_ETH_CONN_AG_CTX_RULE1EN_MASK               0x1   /* rule1en */
#define USTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT              0
#define USTORM_ETH_CONN_AG_CTX_RULE2EN_MASK               0x1   /* rule2en */
#define USTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT              1
#define USTORM_ETH_CONN_AG_CTX_RULE3EN_MASK               0x1   /* rule3en */
#define USTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT              2
#define USTORM_ETH_CONN_AG_CTX_RULE4EN_MASK               0x1   /* rule4en */
#define USTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT              3
#define USTORM_ETH_CONN_AG_CTX_RULE5EN_MASK               0x1   /* rule5en */
#define USTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT              4
#define USTORM_ETH_CONN_AG_CTX_RULE6EN_MASK               0x1   /* rule6en */
#define USTORM_ETH_CONN_AG_CTX_RULE6EN_SHIFT              5
#define USTORM_ETH_CONN_AG_CTX_RULE7EN_MASK               0x1   /* rule7en */
#define USTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT              6
#define USTORM_ETH_CONN_AG_CTX_RULE8EN_MASK               0x1   /* rule8en */
#define USTORM_ETH_CONN_AG_CTX_RULE8EN_SHIFT              7
	u8	byte2 /* byte2 */;
	u8	byte3 /* byte3 */;
	__le16	word0 /* conn_dpi */;
	__le16	tx_bd_cons /* word1 */;
	__le32	reg0 /* reg0 */;
	__le32	reg1 /* reg1 */;
	__le32	reg2 /* reg2 */;
	__le32	reg3 /* reg3 */;
	__le16	tx_drv_bd_cons /* word2 */;
	__le16	rx_drv_cqe_cons /* word3 */;
};

struct xstorm_eth_hw_conn_ag_ctx {
	u8	reserved0 /* cdu_validation */;
	u8	eth_state /* state */;
	u8	flags0;
#define XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM0_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM0_SHIFT           0
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED1_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED1_SHIFT              1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED2_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED2_SHIFT              2
#define XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM3_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM3_SHIFT           3
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED3_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED3_SHIFT              4
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED4_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED4_SHIFT              5
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED5_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED5_SHIFT              6
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED6_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED6_SHIFT              7
	u8 flags1;
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED7_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED7_SHIFT              0
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED8_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED8_SHIFT              1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED9_MASK               0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED9_SHIFT              2
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT11_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT11_SHIFT                  3
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT12_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT12_SHIFT                  4
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT13_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_BIT13_SHIFT                  5
#define XSTORM_ETH_HW_CONN_AG_CTX_TX_RULE_ACTIVE_MASK          0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT         6
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_ACTIVE_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT           7
	u8 flags2;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF0_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF0_SHIFT                    0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF1_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF1_SHIFT                    2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF2_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF2_SHIFT                    4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF3_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF3_SHIFT                    6
	u8 flags3;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF4_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF4_SHIFT                    0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF5_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF5_SHIFT                    2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF6_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF6_SHIFT                    4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF7_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF7_SHIFT                    6
	u8 flags4;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF8_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF8_SHIFT                    0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF9_MASK                     0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF9_SHIFT                    2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF10_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF10_SHIFT                   4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF11_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF11_SHIFT                   6
	u8 flags5;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF12_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF12_SHIFT                   0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF13_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF13_SHIFT                   2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF14_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF14_SHIFT                   4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF15_MASK                    0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF15_SHIFT                   6
	u8 flags6;
#define XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_MASK        0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_SHIFT       0
#define XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_MASK        0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_SHIFT       2
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_MASK                   0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_SHIFT                  4
#define XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_MASK            0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_SHIFT           6
	u8 flags7;
#define XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_MASK                0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_SHIFT               0
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED10_MASK              0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED10_SHIFT             2
#define XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_MASK               0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_SHIFT              4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF0EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF0EN_SHIFT                  6
#define XSTORM_ETH_HW_CONN_AG_CTX_CF1EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF1EN_SHIFT                  7
	u8 flags8;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF2EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF2EN_SHIFT                  0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF3EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF3EN_SHIFT                  1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF4EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF4EN_SHIFT                  2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF5EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF5EN_SHIFT                  3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF6EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF6EN_SHIFT                  4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF7EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF7EN_SHIFT                  5
#define XSTORM_ETH_HW_CONN_AG_CTX_CF8EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF8EN_SHIFT                  6
#define XSTORM_ETH_HW_CONN_AG_CTX_CF9EN_MASK                   0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF9EN_SHIFT                  7
	u8 flags9;
#define XSTORM_ETH_HW_CONN_AG_CTX_CF10EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF10EN_SHIFT                 0
#define XSTORM_ETH_HW_CONN_AG_CTX_CF11EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF11EN_SHIFT                 1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF12EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF12EN_SHIFT                 2
#define XSTORM_ETH_HW_CONN_AG_CTX_CF13EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF13EN_SHIFT                 3
#define XSTORM_ETH_HW_CONN_AG_CTX_CF14EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF14EN_SHIFT                 4
#define XSTORM_ETH_HW_CONN_AG_CTX_CF15EN_MASK                  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_CF15EN_SHIFT                 5
#define XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_MASK     0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_SHIFT    6
#define XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_EN_MASK     0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_EN_SHIFT    7
	u8 flags10;
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_EN_SHIFT               0
#define XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_EN_MASK         0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT        1
#define XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_EN_MASK             0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT            2
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED11_MASK              0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED11_SHIFT             3
#define XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_EN_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_EN_SHIFT           4
#define XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_MASK  0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_SHIFT 5
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED12_MASK              0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED12_SHIFT             6
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED13_MASK              0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED13_SHIFT             7
	u8 flags11;
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED14_MASK              0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED14_SHIFT             0
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED15_MASK              0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RESERVED15_SHIFT             1
#define XSTORM_ETH_HW_CONN_AG_CTX_TX_DEC_RULE_EN_MASK          0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT         2
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE5EN_MASK                 0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE5EN_SHIFT                3
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE6EN_MASK                 0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE6EN_SHIFT                4
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE7EN_MASK                 0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE7EN_SHIFT                5
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED1_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED1_SHIFT           6
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE9EN_MASK                 0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE9EN_SHIFT                7
	u8 flags12;
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE10EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE10EN_SHIFT               0
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE11EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE11EN_SHIFT               1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED2_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED2_SHIFT           2
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED3_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED3_SHIFT           3
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE14EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE14EN_SHIFT               4
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE15EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE15EN_SHIFT               5
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE16EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE16EN_SHIFT               6
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE17EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE17EN_SHIFT               7
	u8 flags13;
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE18EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE18EN_SHIFT               0
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE19EN_MASK                0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_RULE19EN_SHIFT               1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED4_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED4_SHIFT           2
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED5_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED5_SHIFT           3
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED6_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED6_SHIFT           4
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED7_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED7_SHIFT           5
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED8_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED8_SHIFT           6
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED9_MASK            0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED9_SHIFT           7
	u8 flags14;
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_USE_EXT_HDR_MASK        0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_USE_EXT_HDR_SHIFT       0
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_MASK      0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_SHIFT     1
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_MASK    0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_SHIFT   2
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_MASK    0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_SHIFT   3
#define XSTORM_ETH_HW_CONN_AG_CTX_L2_EDPM_ENABLE_MASK          0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_L2_EDPM_ENABLE_SHIFT         4
#define XSTORM_ETH_HW_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK        0x1
#define XSTORM_ETH_HW_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT       5
#define XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_MASK              0x3
#define XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_SHIFT             6
	u8	edpm_event_id /* byte2 */;
	__le16	physical_q0 /* physical_q0 */;
	__le16	word1 /* physical_q1 */;
	__le16	edpm_num_bds /* physical_q2 */;
	__le16	tx_bd_cons /* word3 */;
	__le16	tx_bd_prod /* word4 */;
	__le16	go_to_bd_cons /* word5 */;
	__le16	conn_dpi /* conn_dpi */;
};

#define VF_MAX_STATIC 192       /* In case of K2 */

#define MCP_GLOB_PATH_MAX       2
#define MCP_PORT_MAX            2       /* Global */
#define MCP_GLOB_PORT_MAX       4       /* Global */
#define MCP_GLOB_FUNC_MAX       16      /* Global */

typedef u32 offsize_t;                  /* In DWORDS !!! */
/* Offset from the beginning of the MCP scratchpad */
#define OFFSIZE_OFFSET_SHIFT    0
#define OFFSIZE_OFFSET_MASK     0x0000ffff
/* Size of specific element (not the whole array if any) */
#define OFFSIZE_SIZE_SHIFT      16
#define OFFSIZE_SIZE_MASK       0xffff0000

/* SECTION_OFFSET is calculating the offset in bytes out of offsize */
#define SECTION_OFFSET(_offsize)        ((((_offsize &		    \
					    OFFSIZE_OFFSET_MASK) >> \
					   OFFSIZE_OFFSET_SHIFT) << 2))

/* QED_SECTION_SIZE is calculating the size in bytes out of offsize */
#define QED_SECTION_SIZE(_offsize)              (((_offsize &		 \
						   OFFSIZE_SIZE_MASK) >> \
						  OFFSIZE_SIZE_SHIFT) << 2)

/* SECTION_ADDR returns the GRC addr of a section, given offsize and index
 * within section.
 */
#define SECTION_ADDR(_offsize, idx)     (MCP_REG_SCRATCH +	    \
					 SECTION_OFFSET(_offsize) + \
					 (QED_SECTION_SIZE(_offsize) * idx))

/* SECTION_OFFSIZE_ADDR returns the GRC addr to the offsize address.
 * Use offsetof, since the OFFSETUP collide with the firmware definition
 */
#define SECTION_OFFSIZE_ADDR(_pub_base, _section) (_pub_base +		     \
						   offsetof(struct	     \
							    mcp_public_data, \
							    sections[_section]))
/* PHY configuration */
struct pmm_phy_cfg {
	u32	speed;
#define PMM_SPEED_AUTONEG   0

	u32	pause;  /* bitmask */
#define PMM_PAUSE_NONE          0x0
#define PMM_PAUSE_AUTONEG       0x1
#define PMM_PAUSE_RX            0x2
#define PMM_PAUSE_TX            0x4

	u32	adv_speed;  /* Default should be the speed_cap_mask */
	u32	loopback_mode;
#define PMM_LOOPBACK_NONE               0
#define PMM_LOOPBACK_INT_PHY    1
#define PMM_LOOPBACK_EXT_PHY    2
#define PMM_LOOPBACK_EXT                3
#define PMM_LOOPBACK_MAC                4

	/* features */
	u32 feature_config_flags;
};

struct port_mf_cfg {
	u32	dynamic_cfg; /* device control channel */
#define PORT_MF_CFG_OV_TAG_MASK              0x0000ffff
#define PORT_MF_CFG_OV_TAG_SHIFT             0
#define PORT_MF_CFG_OV_TAG_DEFAULT         PORT_MF_CFG_OV_TAG_MASK

	u32	reserved[1];
};

/* DO NOT add new fields in the middle
 * MUST be synced with struct pmm_stats_map
 */
struct pmm_stats {
	u64	r64;    /* 0x00 (Offset 0x00 ) RX 64-byte frame counter*/
	u64	r127;   /* 0x01 (Offset 0x08 ) RX 65 to 127 byte frame counter*/
	u64	r255;
	u64	r511;
	u64	r1023;
	u64	r1518;
	u64	r1522;
	u64	r2047;
	u64	r4095;
	u64	r9216;
	u64	r16383;
	u64	rfcs;   /* 0x0F (Offset 0x58 ) RX FCS error frame counter*/
	u64	rxcf;   /* 0x10 (Offset 0x60 ) RX control frame counter*/
	u64	rxpf;   /* 0x11 (Offset 0x68 ) RX pause frame counter*/
	u64	rxpp;   /* 0x12 (Offset 0x70 ) RX PFC frame counter*/
	u64	raln;   /* 0x16 (Offset 0x78 ) RX alignment error counter*/
	u64	rfcr;   /* 0x19 (Offset 0x80 ) RX false carrier counter */
	u64	rovr;   /* 0x1A (Offset 0x88 ) RX oversized frame counter*/
	u64	rjbr;   /* 0x1B (Offset 0x90 ) RX jabber frame counter */
	u64	rund;   /* 0x34 (Offset 0x98 ) RX undersized frame counter */
	u64	rfrg;   /* 0x35 (Offset 0xa0 ) RX fragment counter */
	u64	t64;    /* 0x40 (Offset 0xa8 ) TX 64-byte frame counter */
	u64	t127;
	u64	t255;
	u64	t511;
	u64	t1023;
	u64	t1518;
	u64	t2047;
	u64	t4095;
	u64	t9216;
	u64	t16383;
	u64	txpf;   /* 0x50 (Offset 0xf8 ) TX pause frame counter */
	u64	txpp;   /* 0x51 (Offset 0x100) TX PFC frame counter */
	u64	tlpiec;
	u64	tncl;
	u64	rbyte;  /* 0x3d (Offset 0x118) RX byte counter */
	u64	rxuca;  /* 0x0c (Offset 0x120) RX UC frame counter */
	u64	rxmca;  /* 0x0d (Offset 0x128) RX MC frame counter */
	u64	rxbca;  /* 0x0e (Offset 0x130) RX BC frame counter */
	u64	rxpok;
	u64	tbyte;  /* 0x6f (Offset 0x140) TX byte counter */
	u64	txuca;  /* 0x4d (Offset 0x148) TX UC frame counter */
	u64	txmca;  /* 0x4e (Offset 0x150) TX MC frame counter */
	u64	txbca;  /* 0x4f (Offset 0x158) TX BC frame counter */
	u64	txcf;   /* 0x54 (Offset 0x160) TX control frame counter */
};

struct brb_stats {
	u64	brb_truncate[8];
	u64	brb_discard[8];
};

struct port_stats {
	struct brb_stats	brb;
	struct pmm_stats	pmm;
};

#define CMT_TEAM0 0
#define CMT_TEAM1 1
#define CMT_TEAM_MAX 2

struct couple_mode_teaming {
	u8 port_cmt[MCP_GLOB_PORT_MAX];
#define PORT_CMT_IN_TEAM		BIT(0)

#define PORT_CMT_PORT_ROLE		BIT(1)
#define PORT_CMT_PORT_INACTIVE      (0 << 1)
#define PORT_CMT_PORT_ACTIVE		BIT(1)

#define PORT_CMT_TEAM_MASK		BIT(2)
#define PORT_CMT_TEAM0              (0 << 2)
#define PORT_CMT_TEAM1			BIT(2)
};

/**************************************
*     LLDP and DCBX HSI structures
**************************************/
#define LLDP_CHASSIS_ID_STAT_LEN 4
#define LLDP_PORT_ID_STAT_LEN 4
#define DCBX_MAX_APP_PROTOCOL           32
#define MAX_SYSTEM_LLDP_TLV_DATA    32

enum lldp_agent_e {
	LLDP_NEAREST_BRIDGE = 0,
	LLDP_NEAREST_NON_TPMR_BRIDGE,
	LLDP_NEAREST_CUSTOMER_BRIDGE,
	LLDP_MAX_LLDP_AGENTS
};

struct lldp_config_params_s {
	u32 config;
#define LLDP_CONFIG_TX_INTERVAL_MASK        0x000000ff
#define LLDP_CONFIG_TX_INTERVAL_SHIFT       0
#define LLDP_CONFIG_HOLD_MASK               0x00000f00
#define LLDP_CONFIG_HOLD_SHIFT              8
#define LLDP_CONFIG_MAX_CREDIT_MASK         0x0000f000
#define LLDP_CONFIG_MAX_CREDIT_SHIFT        12
#define LLDP_CONFIG_ENABLE_RX_MASK          0x40000000
#define LLDP_CONFIG_ENABLE_RX_SHIFT         30
#define LLDP_CONFIG_ENABLE_TX_MASK          0x80000000
#define LLDP_CONFIG_ENABLE_TX_SHIFT         31
	u32	local_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	u32	local_port_id[LLDP_PORT_ID_STAT_LEN];
};

struct lldp_status_params_s {
	u32	prefix_seq_num;
	u32	status; /* TBD */

	/* Holds remote Chassis ID TLV header, subtype and 9B of payload. */
	u32	peer_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];

	/* Holds remote Port ID TLV header, subtype and 9B of payload. */
	u32	peer_port_id[LLDP_PORT_ID_STAT_LEN];
	u32	suffix_seq_num;
};

struct dcbx_ets_feature {
	u32 flags;
#define DCBX_ETS_ENABLED_MASK                   0x00000001
#define DCBX_ETS_ENABLED_SHIFT                  0
#define DCBX_ETS_WILLING_MASK                   0x00000002
#define DCBX_ETS_WILLING_SHIFT                  1
#define DCBX_ETS_ERROR_MASK                     0x00000004
#define DCBX_ETS_ERROR_SHIFT                    2
#define DCBX_ETS_CBS_MASK                       0x00000008
#define DCBX_ETS_CBS_SHIFT                      3
#define DCBX_ETS_MAX_TCS_MASK                   0x000000f0
#define DCBX_ETS_MAX_TCS_SHIFT                  4
	u32	pri_tc_tbl[1];
#define DCBX_ISCSI_OOO_TC                       4
#define NIG_ETS_ISCSI_OOO_CLIENT_OFFSET         (DCBX_ISCSI_OOO_TC + 1)
	u32	tc_bw_tbl[2];
	u32	tc_tsa_tbl[2];
#define DCBX_ETS_TSA_STRICT                     0
#define DCBX_ETS_TSA_CBS                        1
#define DCBX_ETS_TSA_ETS                        2
};

struct dcbx_app_priority_entry {
	u32 entry;
#define DCBX_APP_PRI_MAP_MASK       0x000000ff
#define DCBX_APP_PRI_MAP_SHIFT      0
#define DCBX_APP_PRI_0              0x01
#define DCBX_APP_PRI_1              0x02
#define DCBX_APP_PRI_2              0x04
#define DCBX_APP_PRI_3              0x08
#define DCBX_APP_PRI_4              0x10
#define DCBX_APP_PRI_5              0x20
#define DCBX_APP_PRI_6              0x40
#define DCBX_APP_PRI_7              0x80
#define DCBX_APP_SF_MASK            0x00000300
#define DCBX_APP_SF_SHIFT           8
#define DCBX_APP_SF_ETHTYPE         0
#define DCBX_APP_SF_PORT            1
#define DCBX_APP_PROTOCOL_ID_MASK   0xffff0000
#define DCBX_APP_PROTOCOL_ID_SHIFT  16
};

/* FW structure in BE */
struct dcbx_app_priority_feature {
	u32 flags;
#define DCBX_APP_ENABLED_MASK           0x00000001
#define DCBX_APP_ENABLED_SHIFT          0
#define DCBX_APP_WILLING_MASK           0x00000002
#define DCBX_APP_WILLING_SHIFT          1
#define DCBX_APP_ERROR_MASK             0x00000004
#define DCBX_APP_ERROR_SHIFT            2
/* Not in use
 * #define DCBX_APP_DEFAULT_PRI_MASK       0x00000f00
 * #define DCBX_APP_DEFAULT_PRI_SHIFT      8
 */
#define DCBX_APP_MAX_TCS_MASK           0x0000f000
#define DCBX_APP_MAX_TCS_SHIFT          12
#define DCBX_APP_NUM_ENTRIES_MASK       0x00ff0000
#define DCBX_APP_NUM_ENTRIES_SHIFT      16
	struct dcbx_app_priority_entry app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

/* FW structure in BE */
struct dcbx_features {
	/* PG feature */
	struct dcbx_ets_feature ets;

	/* PFC feature */
	u32			pfc;
#define DCBX_PFC_PRI_EN_BITMAP_MASK             0x000000ff
#define DCBX_PFC_PRI_EN_BITMAP_SHIFT            0
#define DCBX_PFC_PRI_EN_BITMAP_PRI_0            0x01
#define DCBX_PFC_PRI_EN_BITMAP_PRI_1            0x02
#define DCBX_PFC_PRI_EN_BITMAP_PRI_2            0x04
#define DCBX_PFC_PRI_EN_BITMAP_PRI_3            0x08
#define DCBX_PFC_PRI_EN_BITMAP_PRI_4            0x10
#define DCBX_PFC_PRI_EN_BITMAP_PRI_5            0x20
#define DCBX_PFC_PRI_EN_BITMAP_PRI_6            0x40
#define DCBX_PFC_PRI_EN_BITMAP_PRI_7            0x80

#define DCBX_PFC_FLAGS_MASK                     0x0000ff00
#define DCBX_PFC_FLAGS_SHIFT                    8
#define DCBX_PFC_CAPS_MASK                      0x00000f00
#define DCBX_PFC_CAPS_SHIFT                     8
#define DCBX_PFC_MBC_MASK                       0x00004000
#define DCBX_PFC_MBC_SHIFT                      14
#define DCBX_PFC_WILLING_MASK                   0x00008000
#define DCBX_PFC_WILLING_SHIFT                  15
#define DCBX_PFC_ENABLED_MASK                   0x00010000
#define DCBX_PFC_ENABLED_SHIFT                  16
#define DCBX_PFC_ERROR_MASK                     0x00020000
#define DCBX_PFC_ERROR_SHIFT                    17

	/* APP feature */
	struct dcbx_app_priority_feature app;
};

struct dcbx_local_params {
	u32 config;
#define DCBX_CONFIG_VERSION_MASK            0x00000003
#define DCBX_CONFIG_VERSION_SHIFT           0
#define DCBX_CONFIG_VERSION_DISABLED        0
#define DCBX_CONFIG_VERSION_IEEE            1
#define DCBX_CONFIG_VERSION_CEE             2

	u32			flags;
	struct dcbx_features	features;
};

struct dcbx_mib {
	u32	prefix_seq_num;
	u32	flags;
	struct dcbx_features	features;
	u32			suffix_seq_num;
};

struct lldp_system_tlvs_buffer_s {
	u16	valid;
	u16	length;
	u32	data[MAX_SYSTEM_LLDP_TLV_DATA];
};

/**************************************/
/*                                    */
/*     P U B L I C      G L O B A L   */
/*                                    */
/**************************************/
struct public_global {
	u32				max_path;
#define MAX_PATH_BIG_BEAR       2
#define MAX_PATH_K2             1
	u32				max_ports;
#define MODE_1P 1
#define MODE_2P 2
#define MODE_3P 3
#define MODE_4P 4
	u32				debug_mb_offset;
	u32				phymod_dbg_mb_offset;
	struct couple_mode_teaming	cmt;
	s32				internal_temperature;
	u32				mfw_ver;
	u32				running_bundle_id;
};

/**************************************/
/*                                    */
/*     P U B L I C      P A T H       */
/*                                    */
/**************************************/

/****************************************************************************
* Shared Memory 2 Region                                                   *
****************************************************************************/
/* The fw_flr_ack is actually built in the following way:                   */
/* 8 bit:  PF ack                                                           */
/* 128 bit: VF ack                                                           */
/* 8 bit:  ios_dis_ack                                                      */
/* In order to maintain endianity in the mailbox hsi, we want to keep using */
/* u32. The fw must have the VF right after the PF since this is how it     */
/* access arrays(it expects always the VF to reside after the PF, and that  */
/* makes the calculation much easier for it. )                              */
/* In order to answer both limitations, and keep the struct small, the code */
/* will abuse the structure defined here to achieve the actual partition    */
/* above                                                                    */
/****************************************************************************/
struct fw_flr_mb {
	u32	aggint;
	u32	opgen_addr;
	u32	accum_ack;  /* 0..15:PF, 16..207:VF, 256..271:IOV_DIS */
#define ACCUM_ACK_PF_BASE       0
#define ACCUM_ACK_PF_SHIFT      0

#define ACCUM_ACK_VF_BASE       8
#define ACCUM_ACK_VF_SHIFT      3

#define ACCUM_ACK_IOV_DIS_BASE  256
#define ACCUM_ACK_IOV_DIS_SHIFT 8
};

struct public_path {
	struct fw_flr_mb	flr_mb;
	u32			mcp_vf_disabled[VF_MAX_STATIC / 32];

	u32			process_kill;
#define PROCESS_KILL_COUNTER_MASK               0x0000ffff
#define PROCESS_KILL_COUNTER_SHIFT              0
#define PROCESS_KILL_GLOB_AEU_BIT_MASK          0xffff0000
#define PROCESS_KILL_GLOB_AEU_BIT_SHIFT         16
#define GLOBAL_AEU_BIT(aeu_reg_id, aeu_bit) (aeu_reg_id * 32 + aeu_bit)
};

/**************************************/
/*                                    */
/*     P U B L I C      P O R T       */
/*                                    */
/**************************************/

/****************************************************************************
* Driver <-> FW Mailbox                                                    *
****************************************************************************/

struct public_port {
	u32 validity_map;   /* 0x0 (4*2 = 0x8) */

	/* validity bits */
#define MCP_VALIDITY_PCI_CFG                    0x00100000
#define MCP_VALIDITY_MB                         0x00200000
#define MCP_VALIDITY_DEV_INFO                   0x00400000
#define MCP_VALIDITY_RESERVED                   0x00000007

	/* One licensing bit should be set */
#define MCP_VALIDITY_LIC_KEY_IN_EFFECT_MASK     0x00000038
#define MCP_VALIDITY_LIC_MANUF_KEY_IN_EFFECT    0x00000008
#define MCP_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT  0x00000010
#define MCP_VALIDITY_LIC_NO_KEY_IN_EFFECT       0x00000020

	/* Active MFW */
#define MCP_VALIDITY_ACTIVE_MFW_UNKNOWN         0x00000000
#define MCP_VALIDITY_ACTIVE_MFW_MASK            0x000001c0
#define MCP_VALIDITY_ACTIVE_MFW_NCSI            0x00000040
#define MCP_VALIDITY_ACTIVE_MFW_NONE            0x000001c0

	u32 link_status;
#define LINK_STATUS_LINK_UP \
	0x00000001
#define LINK_STATUS_SPEED_AND_DUPLEX_MASK                       0x0000001e
#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		BIT(1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD            (2 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10G                        (3 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_20G                        (4 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_40G                        (5 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_50G                        (6 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100G                       (7 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_25G                        (8 << 1)

#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED                      0x00000020

#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE                     0x00000040
#define LINK_STATUS_PARALLEL_DETECTION_USED                     0x00000080

#define LINK_STATUS_PFC_ENABLED	\
	0x00000100
#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE        0x00000200
#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE        0x00000400
#define LINK_STATUS_LINK_PARTNER_10G_CAPABLE            0x00000800
#define LINK_STATUS_LINK_PARTNER_20G_CAPABLE            0x00001000
#define LINK_STATUS_LINK_PARTNER_40G_CAPABLE            0x00002000
#define LINK_STATUS_LINK_PARTNER_50G_CAPABLE            0x00004000
#define LINK_STATUS_LINK_PARTNER_100G_CAPABLE           0x00008000
#define LINK_STATUS_LINK_PARTNER_25G_CAPABLE            0x00010000

#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK      0x000C0000
#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE      (0 << 18)
#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	BIT(18)
#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE       (2 << 18)
#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE                     (3 << 18)

#define LINK_STATUS_SFP_TX_FAULT \
	0x00100000
#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED                     0x00200000
#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED                     0x00400000

	u32			link_status1;
	u32			ext_phy_fw_version;
	u32			drv_phy_cfg_addr;

	u32			port_stx;

	u32			stat_nig_timer;

	struct port_mf_cfg	port_mf_config;
	struct port_stats	stats;

	u32			media_type;
#define MEDIA_UNSPECIFIED       0x0
#define MEDIA_SFPP_10G_FIBER    0x1
#define MEDIA_XFP_FIBER         0x2
#define MEDIA_DA_TWINAX         0x3
#define MEDIA_BASE_T            0x4
#define MEDIA_SFP_1G_FIBER      0x5
#define MEDIA_KR                0xf0
#define MEDIA_NOT_PRESENT       0xff

	u32 lfa_status;
#define LFA_LINK_FLAP_REASON_OFFSET             0
#define LFA_LINK_FLAP_REASON_MASK               0x000000ff
#define LFA_NO_REASON                                   (0 << 0)
#define LFA_LINK_DOWN					BIT(0)
#define LFA_FORCE_INIT                                  BIT(1)
#define LFA_LOOPBACK_MISMATCH                           BIT(2)
#define LFA_SPEED_MISMATCH                              BIT(3)
#define LFA_FLOW_CTRL_MISMATCH                          BIT(4)
#define LFA_ADV_SPEED_MISMATCH                          BIT(5)
#define LINK_FLAP_AVOIDANCE_COUNT_OFFSET        8
#define LINK_FLAP_AVOIDANCE_COUNT_MASK          0x0000ff00
#define LINK_FLAP_COUNT_OFFSET                  16
#define LINK_FLAP_COUNT_MASK                    0x00ff0000

	u32					link_change_count;

	/* LLDP params */
	struct lldp_config_params_s		lldp_config_params[
		LLDP_MAX_LLDP_AGENTS];
	struct lldp_status_params_s		lldp_status_params[
		LLDP_MAX_LLDP_AGENTS];
	struct lldp_system_tlvs_buffer_s	system_lldp_tlvs_buf;

	/* DCBX related MIB */
	struct dcbx_local_params		local_admin_dcbx_mib;
	struct dcbx_mib				remote_dcbx_mib;
	struct dcbx_mib				operational_dcbx_mib;
};

/**************************************/
/*                                    */
/*     P U B L I C      F U N C       */
/*                                    */
/**************************************/

struct public_func {
	u32	iscsi_boot_signature;
	u32	iscsi_boot_block_offset;

	u32	reserved[8];

	u32	config;

	/* E/R/I/D */
	/* function 0 of each port cannot be hidden */
#define FUNC_MF_CFG_FUNC_HIDE                   0x00000001
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING          0x00000002
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING_SHIFT    0x00000001

#define FUNC_MF_CFG_PROTOCOL_MASK               0x000000f0
#define FUNC_MF_CFG_PROTOCOL_SHIFT              4
#define FUNC_MF_CFG_PROTOCOL_ETHERNET           0x00000000
#define FUNC_MF_CFG_PROTOCOL_ISCSI              0x00000010
#define FUNC_MF_CFG_PROTOCOL_FCOE               0x00000020
#define FUNC_MF_CFG_PROTOCOL_ROCE               0x00000030
#define FUNC_MF_CFG_PROTOCOL_MAX                0x00000030

	/* MINBW, MAXBW */
	/* value range - 0..100, increments in 1 %  */
#define FUNC_MF_CFG_MIN_BW_MASK                 0x0000ff00
#define FUNC_MF_CFG_MIN_BW_SHIFT                8
#define FUNC_MF_CFG_MIN_BW_DEFAULT              0x00000000
#define FUNC_MF_CFG_MAX_BW_MASK                 0x00ff0000
#define FUNC_MF_CFG_MAX_BW_SHIFT                16
#define FUNC_MF_CFG_MAX_BW_DEFAULT              0x00640000

	u32	status;
#define FUNC_STATUS_VLINK_DOWN                  0x00000001

	u32	mac_upper;  /* MAC */
#define FUNC_MF_CFG_UPPERMAC_MASK               0x0000ffff
#define FUNC_MF_CFG_UPPERMAC_SHIFT              0
#define FUNC_MF_CFG_UPPERMAC_DEFAULT            FUNC_MF_CFG_UPPERMAC_MASK
	u32	mac_lower;
#define FUNC_MF_CFG_LOWERMAC_DEFAULT            0xffffffff

	u32	fcoe_wwn_port_name_upper;
	u32	fcoe_wwn_port_name_lower;

	u32	fcoe_wwn_node_name_upper;
	u32	fcoe_wwn_node_name_lower;

	u32	ovlan_stag; /* tags */
#define FUNC_MF_CFG_OV_STAG_MASK              0x0000ffff
#define FUNC_MF_CFG_OV_STAG_SHIFT             0
#define FUNC_MF_CFG_OV_STAG_DEFAULT           FUNC_MF_CFG_OV_STAG_MASK

	u32	pf_allocation;  /* vf per pf */

	u32	preserve_data;  /* Will be used bt CCM */

	u32	driver_last_activity_ts;

	u32	drv_ack_vf_disabled[VF_MAX_STATIC / 32]; /* 0x0044 */

	u32	drv_id;
#define DRV_ID_PDA_COMP_VER_MASK        0x0000ffff
#define DRV_ID_PDA_COMP_VER_SHIFT       0

#define DRV_ID_MCP_HSI_VER_MASK         0x00ff0000
#define DRV_ID_MCP_HSI_VER_SHIFT        16
#define DRV_ID_MCP_HSI_VER_CURRENT	BIT(DRV_ID_MCP_HSI_VER_SHIFT)

#define DRV_ID_DRV_TYPE_MASK            0xff000000
#define DRV_ID_DRV_TYPE_SHIFT           24
#define DRV_ID_DRV_TYPE_UNKNOWN         (0 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_LINUX		BIT(DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_WINDOWS         (2 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_DIAG            (3 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_PREBOOT         (4 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_SOLARIS         (5 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_VMWARE          (6 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_FREEBSD         (7 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_AIX             (8 << DRV_ID_DRV_TYPE_SHIFT)
};

/**************************************/
/*                                    */
/*     P U B L I C       M B          */
/*                                    */
/**************************************/
/* This is the only section that the driver can write to, and each */
/* Basically each driver request to set feature parameters,
 * will be done using a different command, which will be linked
 * to a specific data structure from the union below.
 * For huge strucuture, the common blank structure should be used.
 */

struct mcp_mac {
	u32	mac_upper;  /* Upper 16 bits are always zeroes */
	u32	mac_lower;
};

struct mcp_val64 {
	u32	lo;
	u32	hi;
};

struct mcp_file_att {
	u32	nvm_start_addr;
	u32	len;
};

#define MCP_DRV_VER_STR_SIZE 16
#define MCP_DRV_VER_STR_SIZE_DWORD (MCP_DRV_VER_STR_SIZE / sizeof(u32))
#define MCP_DRV_NVM_BUF_LEN 32
struct drv_version_stc {
	u32	version;
	u8	name[MCP_DRV_VER_STR_SIZE - 4];
};

union drv_union_data {
	u32			ver_str[MCP_DRV_VER_STR_SIZE_DWORD];
	struct mcp_mac		wol_mac;

	struct pmm_phy_cfg	drv_phy_cfg;

	struct mcp_val64	val64; /* For PHY / AVS commands */

	u8			raw_data[MCP_DRV_NVM_BUF_LEN];

	struct mcp_file_att	file_att;

	u32			ack_vf_disabled[VF_MAX_STATIC / 32];

	struct drv_version_stc	drv_version;
};

struct public_drv_mb {
	u32 drv_mb_header;
#define DRV_MSG_CODE_MASK                       0xffff0000
#define DRV_MSG_CODE_LOAD_REQ                   0x10000000
#define DRV_MSG_CODE_LOAD_DONE                  0x11000000
#define DRV_MSG_CODE_UNLOAD_REQ                 0x20000000
#define DRV_MSG_CODE_UNLOAD_DONE                0x21000000
#define DRV_MSG_CODE_INIT_PHY                   0x22000000
	/* Params - FORCE - Reinitialize the link regardless of LFA */
	/*        - DONT_CARE - Don't flap the link if up */
#define DRV_MSG_CODE_LINK_RESET                 0x23000000

#define DRV_MSG_CODE_SET_LLDP                   0x24000000
#define DRV_MSG_CODE_SET_DCBX                   0x25000000

#define DRV_MSG_CODE_NIG_DRAIN                  0x30000000

#define DRV_MSG_CODE_INITIATE_FLR               0x02000000
#define DRV_MSG_CODE_VF_DISABLED_DONE           0xc0000000
#define DRV_MSG_CODE_CFG_VF_MSIX                0xc0010000
#define DRV_MSG_CODE_NVM_PUT_FILE_BEGIN         0x00010000
#define DRV_MSG_CODE_NVM_PUT_FILE_DATA          0x00020000
#define DRV_MSG_CODE_NVM_GET_FILE_ATT           0x00030000
#define DRV_MSG_CODE_NVM_READ_NVRAM             0x00050000
#define DRV_MSG_CODE_NVM_WRITE_NVRAM            0x00060000
#define DRV_MSG_CODE_NVM_DEL_FILE               0x00080000
#define DRV_MSG_CODE_MCP_RESET                  0x00090000
#define DRV_MSG_CODE_SET_SECURE_MODE            0x000a0000
#define DRV_MSG_CODE_PHY_RAW_READ               0x000b0000
#define DRV_MSG_CODE_PHY_RAW_WRITE              0x000c0000
#define DRV_MSG_CODE_PHY_CORE_READ              0x000d0000
#define DRV_MSG_CODE_PHY_CORE_WRITE             0x000e0000
#define DRV_MSG_CODE_SET_VERSION                0x000f0000

#define DRV_MSG_CODE_SET_LED_MODE               0x00200000

#define DRV_MSG_SEQ_NUMBER_MASK                 0x0000ffff

	u32 drv_mb_param;

	/* UNLOAD_REQ params */
#define DRV_MB_PARAM_UNLOAD_WOL_UNKNOWN         0x00000000
#define DRV_MB_PARAM_UNLOAD_WOL_MCP             0x00000001
#define DRV_MB_PARAM_UNLOAD_WOL_DISABLED        0x00000002
#define DRV_MB_PARAM_UNLOAD_WOL_ENABLED         0x00000003

	/* UNLOAD_DONE_params */
#define DRV_MB_PARAM_UNLOAD_NON_D3_POWER        0x00000001

	/* INIT_PHY params */
#define DRV_MB_PARAM_INIT_PHY_FORCE             0x00000001
#define DRV_MB_PARAM_INIT_PHY_DONT_CARE         0x00000002

	/* LLDP / DCBX params*/
#define DRV_MB_PARAM_LLDP_SEND_MASK             0x00000001
#define DRV_MB_PARAM_LLDP_SEND_SHIFT            0
#define DRV_MB_PARAM_LLDP_AGENT_MASK            0x00000006
#define DRV_MB_PARAM_LLDP_AGENT_SHIFT           1
#define DRV_MB_PARAM_DCBX_NOTIFY_MASK           0x00000008
#define DRV_MB_PARAM_DCBX_NOTIFY_SHIFT          3

#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_MASK   0x000000FF
#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_SHIFT  0

#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MFW     0x1
#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_IMAGE   0x2

#define DRV_MB_PARAM_NVM_OFFSET_SHIFT           0
#define DRV_MB_PARAM_NVM_OFFSET_MASK            0x00FFFFFF
#define DRV_MB_PARAM_NVM_LEN_SHIFT              24
#define DRV_MB_PARAM_NVM_LEN_MASK               0xFF000000

#define DRV_MB_PARAM_PHY_ADDR_SHIFT             0
#define DRV_MB_PARAM_PHY_ADDR_MASK              0x1FF0FFFF
#define DRV_MB_PARAM_PHY_LANE_SHIFT             16
#define DRV_MB_PARAM_PHY_LANE_MASK              0x000F0000
#define DRV_MB_PARAM_PHY_SELECT_PORT_SHIFT      29
#define DRV_MB_PARAM_PHY_SELECT_PORT_MASK       0x20000000
#define DRV_MB_PARAM_PHY_PORT_SHIFT             30
#define DRV_MB_PARAM_PHY_PORT_MASK              0xc0000000

/* configure vf MSIX params*/
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_SHIFT    0
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_MASK     0x000000FF
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_SHIFT   8
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_MASK    0x0000FF00

#define DRV_MB_PARAM_SET_LED_MODE_OPER          0x0
#define DRV_MB_PARAM_SET_LED_MODE_ON            0x1
#define DRV_MB_PARAM_SET_LED_MODE_OFF           0x2

	u32 fw_mb_header;
#define FW_MSG_CODE_MASK                        0xffff0000
#define FW_MSG_CODE_DRV_LOAD_ENGINE             0x10100000
#define FW_MSG_CODE_DRV_LOAD_PORT               0x10110000
#define FW_MSG_CODE_DRV_LOAD_FUNCTION           0x10120000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_PDA        0x10200000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_HSI        0x10210000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_DIAG       0x10220000
#define FW_MSG_CODE_DRV_LOAD_DONE               0x11100000
#define FW_MSG_CODE_DRV_UNLOAD_ENGINE           0x20110000
#define FW_MSG_CODE_DRV_UNLOAD_PORT             0x20120000
#define FW_MSG_CODE_DRV_UNLOAD_FUNCTION         0x20130000
#define FW_MSG_CODE_DRV_UNLOAD_DONE             0x21100000
#define FW_MSG_CODE_INIT_PHY_DONE               0x21200000
#define FW_MSG_CODE_INIT_PHY_ERR_INVALID_ARGS   0x21300000
#define FW_MSG_CODE_LINK_RESET_DONE             0x23000000
#define FW_MSG_CODE_SET_LLDP_DONE               0x24000000
#define FW_MSG_CODE_SET_LLDP_UNSUPPORTED_AGENT  0x24010000
#define FW_MSG_CODE_SET_DCBX_DONE               0x25000000
#define FW_MSG_CODE_NIG_DRAIN_DONE              0x30000000
#define FW_MSG_CODE_VF_DISABLED_DONE            0xb0000000
#define FW_MSG_CODE_DRV_CFG_VF_MSIX_DONE        0xb0010000
#define FW_MSG_CODE_FLR_ACK                     0x02000000
#define FW_MSG_CODE_FLR_NACK                    0x02100000

#define FW_MSG_CODE_NVM_OK                      0x00010000
#define FW_MSG_CODE_NVM_INVALID_MODE            0x00020000
#define FW_MSG_CODE_NVM_PREV_CMD_WAS_NOT_FINISHED       0x00030000
#define FW_MSG_CODE_NVM_FAILED_TO_ALLOCATE_PAGE 0x00040000
#define FW_MSG_CODE_NVM_INVALID_DIR_FOUND       0x00050000
#define FW_MSG_CODE_NVM_PAGE_NOT_FOUND          0x00060000
#define FW_MSG_CODE_NVM_FAILED_PARSING_BNDLE_HEADER 0x00070000
#define FW_MSG_CODE_NVM_FAILED_PARSING_IMAGE_HEADER 0x00080000
#define FW_MSG_CODE_NVM_PARSING_OUT_OF_SYNC     0x00090000
#define FW_MSG_CODE_NVM_FAILED_UPDATING_DIR     0x000a0000
#define FW_MSG_CODE_NVM_FAILED_TO_FREE_PAGE     0x000b0000
#define FW_MSG_CODE_NVM_FILE_NOT_FOUND          0x000c0000
#define FW_MSG_CODE_NVM_OPERATION_FAILED        0x000d0000
#define FW_MSG_CODE_NVM_FAILED_UNALIGNED        0x000e0000
#define FW_MSG_CODE_NVM_BAD_OFFSET              0x000f0000
#define FW_MSG_CODE_NVM_BAD_SIGNATURE           0x00100000
#define FW_MSG_CODE_NVM_FILE_READ_ONLY          0x00200000
#define FW_MSG_CODE_NVM_UNKNOWN_FILE            0x00300000
#define FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK      0x00400000
#define FW_MSG_CODE_MCP_RESET_REJECT            0x00600000
#define FW_MSG_CODE_PHY_OK                      0x00110000
#define FW_MSG_CODE_PHY_ERROR                   0x00120000
#define FW_MSG_CODE_SET_SECURE_MODE_ERROR       0x00130000
#define FW_MSG_CODE_SET_SECURE_MODE_OK          0x00140000
#define FW_MSG_MODE_PHY_PRIVILEGE_ERROR         0x00150000

#define FW_MSG_SEQ_NUMBER_MASK                  0x0000ffff

	u32	fw_mb_param;

	u32	drv_pulse_mb;
#define DRV_PULSE_SEQ_MASK                      0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK              0xffff0000
#define DRV_PULSE_ALWAYS_ALIVE                  0x00008000
	u32 mcp_pulse_mb;
#define MCP_PULSE_SEQ_MASK                      0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE                  0x00008000
#define MCP_EVENT_MASK                          0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ        0x00010000

	union drv_union_data union_data;
};

/* MFW - DRV MB */
/**********************************************************************
* Description
*   Incremental Aggregative
*   8-bit MFW counter per message
*   8-bit ack-counter per message
* Capabilities
*   Provides up to 256 aggregative message per type
*   Provides 4 message types in dword
*   Message type pointers to byte offset
*   Backward Compatibility by using sizeof for the counters.
*   No lock requires for 32bit messages
* Limitations:
* In case of messages greater than 32bit, a dedicated mechanism(e.g lock)
* is required to prevent data corruption.
**********************************************************************/
enum MFW_DRV_MSG_TYPE {
	MFW_DRV_MSG_LINK_CHANGE,
	MFW_DRV_MSG_FLR_FW_ACK_FAILED,
	MFW_DRV_MSG_VF_DISABLED,
	MFW_DRV_MSG_LLDP_DATA_UPDATED,
	MFW_DRV_MSG_DCBX_REMOTE_MIB_UPDATED,
	MFW_DRV_MSG_DCBX_OPERATIONAL_MIB_UPDATED,
	MFW_DRV_MSG_ERROR_RECOVERY,
	MFW_DRV_MSG_MAX
};

#define MFW_DRV_MSG_MAX_DWORDS(msgs)    (((msgs - 1) >> 2) + 1)
#define MFW_DRV_MSG_DWORD(msg_id)       (msg_id >> 2)
#define MFW_DRV_MSG_OFFSET(msg_id)      ((msg_id & 0x3) << 3)
#define MFW_DRV_MSG_MASK(msg_id)        (0xff << MFW_DRV_MSG_OFFSET(msg_id))

struct public_mfw_mb {
	u32	sup_msgs;
	u32	msg[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
	u32	ack[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
};

/**************************************/
/*                                    */
/*     P U B L I C       D A T A      */
/*                                    */
/**************************************/
enum public_sections {
	PUBLIC_DRV_MB,          /* Points to the first drv_mb of path0 */
	PUBLIC_MFW_MB,          /* Points to the first mfw_mb of path0 */
	PUBLIC_GLOBAL,
	PUBLIC_PATH,
	PUBLIC_PORT,
	PUBLIC_FUNC,
	PUBLIC_MAX_SECTIONS
};

struct drv_ver_info_stc {
	u32	ver;
	u8	name[32];
};

struct mcp_public_data {
	/* The sections fields is an array */
	u32			num_sections;
	offsize_t		sections[PUBLIC_MAX_SECTIONS];
	struct public_drv_mb	drv_mb[MCP_GLOB_FUNC_MAX];
	struct public_mfw_mb	mfw_mb[MCP_GLOB_FUNC_MAX];
	struct public_global	global;
	struct public_path	path[MCP_GLOB_PATH_MAX];
	struct public_port	port[MCP_GLOB_PORT_MAX];
	struct public_func	func[MCP_GLOB_FUNC_MAX];
	struct drv_ver_info_stc drv_info;
};

struct nvm_cfg_mac_address {
	u32	mac_addr_hi;
#define NVM_CFG_MAC_ADDRESS_HI_MASK                             0x0000FFFF
#define NVM_CFG_MAC_ADDRESS_HI_OFFSET                           0

	u32	mac_addr_lo;
};

/******************************************
* nvm_cfg1 structs
******************************************/

struct nvm_cfg1_glob {
	u32 generic_cont0;					/* 0x0 */
#define NVM_CFG1_GLOB_BOARD_SWAP_MASK                           0x0000000F
#define NVM_CFG1_GLOB_BOARD_SWAP_OFFSET                         0
#define NVM_CFG1_GLOB_BOARD_SWAP_NONE                           0x0
#define NVM_CFG1_GLOB_BOARD_SWAP_PATH                           0x1
#define NVM_CFG1_GLOB_BOARD_SWAP_PORT                           0x2
#define NVM_CFG1_GLOB_BOARD_SWAP_BOTH                           0x3
#define NVM_CFG1_GLOB_MF_MODE_MASK                              0x00000FF0
#define NVM_CFG1_GLOB_MF_MODE_OFFSET                            4
#define NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED                        0x0
#define NVM_CFG1_GLOB_MF_MODE_FORCED_SF                         0x1
#define NVM_CFG1_GLOB_MF_MODE_SPIO4                             0x2
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_0                           0x3
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_5                           0x4
#define NVM_CFG1_GLOB_MF_MODE_NPAR2_0                           0x5
#define NVM_CFG1_GLOB_MF_MODE_BD                                0x6
#define NVM_CFG1_GLOB_MF_MODE_UFP                               0x7
#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_MASK              0x00001000
#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_OFFSET            12
#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_DISABLED          0x0
#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_ENABLED           0x1
#define NVM_CFG1_GLOB_AVS_MARGIN_LOW_MASK                       0x001FE000
#define NVM_CFG1_GLOB_AVS_MARGIN_LOW_OFFSET                     13
#define NVM_CFG1_GLOB_AVS_MARGIN_HIGH_MASK                      0x1FE00000
#define NVM_CFG1_GLOB_AVS_MARGIN_HIGH_OFFSET                    21
#define NVM_CFG1_GLOB_ENABLE_SRIOV_MASK                         0x20000000
#define NVM_CFG1_GLOB_ENABLE_SRIOV_OFFSET                       29
#define NVM_CFG1_GLOB_ENABLE_SRIOV_DISABLED                     0x0
#define NVM_CFG1_GLOB_ENABLE_SRIOV_ENABLED                      0x1
#define NVM_CFG1_GLOB_ENABLE_ATC_MASK                           0x40000000
#define NVM_CFG1_GLOB_ENABLE_ATC_OFFSET                         30
#define NVM_CFG1_GLOB_ENABLE_ATC_DISABLED                       0x0
#define NVM_CFG1_GLOB_ENABLE_ATC_ENABLED                        0x1
#define NVM_CFG1_GLOB_CLOCK_SLOWDOWN_MASK                       0x80000000
#define NVM_CFG1_GLOB_CLOCK_SLOWDOWN_OFFSET                     31
#define NVM_CFG1_GLOB_CLOCK_SLOWDOWN_DISABLED                   0x0
#define NVM_CFG1_GLOB_CLOCK_SLOWDOWN_ENABLED                    0x1

	u32	engineering_change[3];				/* 0x4 */

	u32	manufacturing_id;				/* 0x10 */

	u32	serial_number[4];				/* 0x14 */

	u32	pcie_cfg;					/* 0x24 */
#define NVM_CFG1_GLOB_PCI_GEN_MASK                              0x00000003
#define NVM_CFG1_GLOB_PCI_GEN_OFFSET                            0
#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN1                          0x0
#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN2                          0x1
#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN3                          0x2
#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_MASK                   0x00000004
#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_OFFSET                 2
#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_DISABLED               0x0
#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_ENABLED                0x1
#define NVM_CFG1_GLOB_ASPM_SUPPORT_MASK                         0x00000018
#define NVM_CFG1_GLOB_ASPM_SUPPORT_OFFSET                       3
#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_L1_ENABLED               0x0
#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_DISABLED                 0x1
#define NVM_CFG1_GLOB_ASPM_SUPPORT_L1_DISABLED                  0x2
#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_L1_DISABLED              0x3
#define NVM_CFG1_GLOB_PREVENT_PCIE_L1_MENTRY_MASK               0x00000020
#define NVM_CFG1_GLOB_PREVENT_PCIE_L1_MENTRY_OFFSET             5
#define NVM_CFG1_GLOB_PREVENT_PCIE_L1_MENTRY_DISABLED           0x0
#define NVM_CFG1_GLOB_PREVENT_PCIE_L1_MENTRY_ENABLED            0x1
#define NVM_CFG1_GLOB_PCIE_G2_TX_AMPLITUDE_MASK                 0x000003C0
#define NVM_CFG1_GLOB_PCIE_G2_TX_AMPLITUDE_OFFSET               6
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_MASK                     0x00001C00
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_OFFSET                   10
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_HW                       0x0
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_0DB                      0x1
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_3_5DB                    0x2
#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_6_0DB                    0x3
#define NVM_CFG1_GLOB_WWN_NODE_PREFIX0_MASK                     0x001FE000
#define NVM_CFG1_GLOB_WWN_NODE_PREFIX0_OFFSET                   13
#define NVM_CFG1_GLOB_WWN_NODE_PREFIX1_MASK                     0x1FE00000
#define NVM_CFG1_GLOB_WWN_NODE_PREFIX1_OFFSET                   21
#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_MASK                      0x60000000
#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_OFFSET                    29

	u32 mgmt_traffic;                                       /* 0x28 */
#define NVM_CFG1_GLOB_RESERVED60_MASK                           0x00000001
#define NVM_CFG1_GLOB_RESERVED60_OFFSET                         0
#define NVM_CFG1_GLOB_RESERVED60_100KHZ                         0x0
#define NVM_CFG1_GLOB_RESERVED60_400KHZ                         0x1
#define NVM_CFG1_GLOB_WWN_PORT_PREFIX0_MASK                     0x000001FE
#define NVM_CFG1_GLOB_WWN_PORT_PREFIX0_OFFSET                   1
#define NVM_CFG1_GLOB_WWN_PORT_PREFIX1_MASK                     0x0001FE00
#define NVM_CFG1_GLOB_WWN_PORT_PREFIX1_OFFSET                   9
#define NVM_CFG1_GLOB_SMBUS_ADDRESS_MASK                        0x01FE0000
#define NVM_CFG1_GLOB_SMBUS_ADDRESS_OFFSET                      17
#define NVM_CFG1_GLOB_SIDEBAND_MODE_MASK                        0x06000000
#define NVM_CFG1_GLOB_SIDEBAND_MODE_OFFSET                      25
#define NVM_CFG1_GLOB_SIDEBAND_MODE_DISABLED                    0x0
#define NVM_CFG1_GLOB_SIDEBAND_MODE_RMII                        0x1
#define NVM_CFG1_GLOB_SIDEBAND_MODE_SGMII                       0x2

	u32 core_cfg;                                           /* 0x2C */
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK                    0x000000FF
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET                  0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X40G                0x0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X50G                0x1
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X100G               0x2
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X10G_F              0x3
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X10G_E              0x4
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X20G                0x5
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X40G                0xB
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X25G                0xC
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X25G                0xD
#define NVM_CFG1_GLOB_EAGLE_ENFORCE_TX_FIR_CFG_MASK             0x00000100
#define NVM_CFG1_GLOB_EAGLE_ENFORCE_TX_FIR_CFG_OFFSET           8
#define NVM_CFG1_GLOB_EAGLE_ENFORCE_TX_FIR_CFG_DISABLED         0x0
#define NVM_CFG1_GLOB_EAGLE_ENFORCE_TX_FIR_CFG_ENABLED          0x1
#define NVM_CFG1_GLOB_FALCON_ENFORCE_TX_FIR_CFG_MASK            0x00000200
#define NVM_CFG1_GLOB_FALCON_ENFORCE_TX_FIR_CFG_OFFSET          9
#define NVM_CFG1_GLOB_FALCON_ENFORCE_TX_FIR_CFG_DISABLED        0x0
#define NVM_CFG1_GLOB_FALCON_ENFORCE_TX_FIR_CFG_ENABLED         0x1
#define NVM_CFG1_GLOB_EAGLE_CORE_ADDR_MASK                      0x0003FC00
#define NVM_CFG1_GLOB_EAGLE_CORE_ADDR_OFFSET                    10
#define NVM_CFG1_GLOB_FALCON_CORE_ADDR_MASK                     0x03FC0000
#define NVM_CFG1_GLOB_FALCON_CORE_ADDR_OFFSET                   18
#define NVM_CFG1_GLOB_AVS_MODE_MASK                             0x1C000000
#define NVM_CFG1_GLOB_AVS_MODE_OFFSET                           26
#define NVM_CFG1_GLOB_AVS_MODE_CLOSE_LOOP                       0x0
#define NVM_CFG1_GLOB_AVS_MODE_OPEN_LOOP                        0x1
#define NVM_CFG1_GLOB_AVS_MODE_DISABLED                         0x3
#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_MASK                 0x60000000
#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_OFFSET               29
#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_DISABLED             0x0
#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_ENABLED              0x1

	u32 e_lane_cfg1;					/* 0x30 */
#define NVM_CFG1_GLOB_RX_LANE0_SWAP_MASK                        0x0000000F
#define NVM_CFG1_GLOB_RX_LANE0_SWAP_OFFSET                      0
#define NVM_CFG1_GLOB_RX_LANE1_SWAP_MASK                        0x000000F0
#define NVM_CFG1_GLOB_RX_LANE1_SWAP_OFFSET                      4
#define NVM_CFG1_GLOB_RX_LANE2_SWAP_MASK                        0x00000F00
#define NVM_CFG1_GLOB_RX_LANE2_SWAP_OFFSET                      8
#define NVM_CFG1_GLOB_RX_LANE3_SWAP_MASK                        0x0000F000
#define NVM_CFG1_GLOB_RX_LANE3_SWAP_OFFSET                      12
#define NVM_CFG1_GLOB_TX_LANE0_SWAP_MASK                        0x000F0000
#define NVM_CFG1_GLOB_TX_LANE0_SWAP_OFFSET                      16
#define NVM_CFG1_GLOB_TX_LANE1_SWAP_MASK                        0x00F00000
#define NVM_CFG1_GLOB_TX_LANE1_SWAP_OFFSET                      20
#define NVM_CFG1_GLOB_TX_LANE2_SWAP_MASK                        0x0F000000
#define NVM_CFG1_GLOB_TX_LANE2_SWAP_OFFSET                      24
#define NVM_CFG1_GLOB_TX_LANE3_SWAP_MASK                        0xF0000000
#define NVM_CFG1_GLOB_TX_LANE3_SWAP_OFFSET                      28

	u32 e_lane_cfg2;					/* 0x34 */
#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_MASK                    0x00000001
#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_OFFSET                  0
#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_MASK                    0x00000002
#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_OFFSET                  1
#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_MASK                    0x00000004
#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_OFFSET                  2
#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_MASK                    0x00000008
#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_OFFSET                  3
#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_MASK                    0x00000010
#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_OFFSET                  4
#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_MASK                    0x00000020
#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_OFFSET                  5
#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_MASK                    0x00000040
#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_OFFSET                  6
#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_MASK                    0x00000080
#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_OFFSET                  7
#define NVM_CFG1_GLOB_SMBUS_MODE_MASK                           0x00000F00
#define NVM_CFG1_GLOB_SMBUS_MODE_OFFSET                         8
#define NVM_CFG1_GLOB_SMBUS_MODE_DISABLED                       0x0
#define NVM_CFG1_GLOB_SMBUS_MODE_100KHZ                         0x1
#define NVM_CFG1_GLOB_SMBUS_MODE_400KHZ                         0x2
#define NVM_CFG1_GLOB_NCSI_MASK                                 0x0000F000
#define NVM_CFG1_GLOB_NCSI_OFFSET                               12
#define NVM_CFG1_GLOB_NCSI_DISABLED                             0x0
#define NVM_CFG1_GLOB_NCSI_ENABLED                              0x1

	u32 f_lane_cfg1;					/* 0x38 */
#define NVM_CFG1_GLOB_RX_LANE0_SWAP_MASK                        0x0000000F
#define NVM_CFG1_GLOB_RX_LANE0_SWAP_OFFSET                      0
#define NVM_CFG1_GLOB_RX_LANE1_SWAP_MASK                        0x000000F0
#define NVM_CFG1_GLOB_RX_LANE1_SWAP_OFFSET                      4
#define NVM_CFG1_GLOB_RX_LANE2_SWAP_MASK                        0x00000F00
#define NVM_CFG1_GLOB_RX_LANE2_SWAP_OFFSET                      8
#define NVM_CFG1_GLOB_RX_LANE3_SWAP_MASK                        0x0000F000
#define NVM_CFG1_GLOB_RX_LANE3_SWAP_OFFSET                      12
#define NVM_CFG1_GLOB_TX_LANE0_SWAP_MASK                        0x000F0000
#define NVM_CFG1_GLOB_TX_LANE0_SWAP_OFFSET                      16
#define NVM_CFG1_GLOB_TX_LANE1_SWAP_MASK                        0x00F00000
#define NVM_CFG1_GLOB_TX_LANE1_SWAP_OFFSET                      20
#define NVM_CFG1_GLOB_TX_LANE2_SWAP_MASK                        0x0F000000
#define NVM_CFG1_GLOB_TX_LANE2_SWAP_OFFSET                      24
#define NVM_CFG1_GLOB_TX_LANE3_SWAP_MASK                        0xF0000000
#define NVM_CFG1_GLOB_TX_LANE3_SWAP_OFFSET                      28

	u32 f_lane_cfg2;					/* 0x3C */
#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_MASK                    0x00000001
#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_OFFSET                  0
#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_MASK                    0x00000002
#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_OFFSET                  1
#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_MASK                    0x00000004
#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_OFFSET                  2
#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_MASK                    0x00000008
#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_OFFSET                  3
#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_MASK                    0x00000010
#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_OFFSET                  4
#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_MASK                    0x00000020
#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_OFFSET                  5
#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_MASK                    0x00000040
#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_OFFSET                  6
#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_MASK                    0x00000080
#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_OFFSET                  7

	u32 eagle_preemphasis;					/* 0x40 */
#define NVM_CFG1_GLOB_LANE0_PREEMP_MASK                         0x000000FF
#define NVM_CFG1_GLOB_LANE0_PREEMP_OFFSET                       0
#define NVM_CFG1_GLOB_LANE1_PREEMP_MASK                         0x0000FF00
#define NVM_CFG1_GLOB_LANE1_PREEMP_OFFSET                       8
#define NVM_CFG1_GLOB_LANE2_PREEMP_MASK                         0x00FF0000
#define NVM_CFG1_GLOB_LANE2_PREEMP_OFFSET                       16
#define NVM_CFG1_GLOB_LANE3_PREEMP_MASK                         0xFF000000
#define NVM_CFG1_GLOB_LANE3_PREEMP_OFFSET                       24

	u32 eagle_driver_current;				/* 0x44 */
#define NVM_CFG1_GLOB_LANE0_AMP_MASK                            0x000000FF
#define NVM_CFG1_GLOB_LANE0_AMP_OFFSET                          0
#define NVM_CFG1_GLOB_LANE1_AMP_MASK                            0x0000FF00
#define NVM_CFG1_GLOB_LANE1_AMP_OFFSET                          8
#define NVM_CFG1_GLOB_LANE2_AMP_MASK                            0x00FF0000
#define NVM_CFG1_GLOB_LANE2_AMP_OFFSET                          16
#define NVM_CFG1_GLOB_LANE3_AMP_MASK                            0xFF000000
#define NVM_CFG1_GLOB_LANE3_AMP_OFFSET                          24

	u32 falcon_preemphasis;					/* 0x48 */
#define NVM_CFG1_GLOB_LANE0_PREEMP_MASK                         0x000000FF
#define NVM_CFG1_GLOB_LANE0_PREEMP_OFFSET                       0
#define NVM_CFG1_GLOB_LANE1_PREEMP_MASK                         0x0000FF00
#define NVM_CFG1_GLOB_LANE1_PREEMP_OFFSET                       8
#define NVM_CFG1_GLOB_LANE2_PREEMP_MASK                         0x00FF0000
#define NVM_CFG1_GLOB_LANE2_PREEMP_OFFSET                       16
#define NVM_CFG1_GLOB_LANE3_PREEMP_MASK                         0xFF000000
#define NVM_CFG1_GLOB_LANE3_PREEMP_OFFSET                       24

	u32 falcon_driver_current;				/* 0x4C */
#define NVM_CFG1_GLOB_LANE0_AMP_MASK                            0x000000FF
#define NVM_CFG1_GLOB_LANE0_AMP_OFFSET                          0
#define NVM_CFG1_GLOB_LANE1_AMP_MASK                            0x0000FF00
#define NVM_CFG1_GLOB_LANE1_AMP_OFFSET                          8
#define NVM_CFG1_GLOB_LANE2_AMP_MASK                            0x00FF0000
#define NVM_CFG1_GLOB_LANE2_AMP_OFFSET                          16
#define NVM_CFG1_GLOB_LANE3_AMP_MASK                            0xFF000000
#define NVM_CFG1_GLOB_LANE3_AMP_OFFSET                          24

	u32	pci_id;						/* 0x50 */
#define NVM_CFG1_GLOB_VENDOR_ID_MASK                            0x0000FFFF
#define NVM_CFG1_GLOB_VENDOR_ID_OFFSET                          0

	u32	pci_subsys_id;					/* 0x54 */
#define NVM_CFG1_GLOB_SUBSYSTEM_VENDOR_ID_MASK                  0x0000FFFF
#define NVM_CFG1_GLOB_SUBSYSTEM_VENDOR_ID_OFFSET                0
#define NVM_CFG1_GLOB_SUBSYSTEM_DEVICE_ID_MASK                  0xFFFF0000
#define NVM_CFG1_GLOB_SUBSYSTEM_DEVICE_ID_OFFSET                16

	u32	bar;						/* 0x58 */
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_MASK                   0x0000000F
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_OFFSET                 0
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_DISABLED               0x0
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_2K                     0x1
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_4K                     0x2
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_8K                     0x3
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_16K                    0x4
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_32K                    0x5
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_64K                    0x6
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_128K                   0x7
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_256K                   0x8
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_512K                   0x9
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_1M                     0xA
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_2M                     0xB
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_4M                     0xC
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_8M                     0xD
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_16M                    0xE
#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_32M                    0xF
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_MASK                     0x000000F0
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_OFFSET                   4
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_DISABLED                 0x0
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_4K                       0x1
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_8K                       0x2
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_16K                      0x3
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_32K                      0x4
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_64K                      0x5
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_128K                     0x6
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_256K                     0x7
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_512K                     0x8
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_1M                       0x9
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_2M                       0xA
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_4M                       0xB
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_8M                       0xC
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_16M                      0xD
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_32M                      0xE
#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_64M                      0xF
#define NVM_CFG1_GLOB_BAR2_SIZE_MASK                            0x00000F00
#define NVM_CFG1_GLOB_BAR2_SIZE_OFFSET                          8
#define NVM_CFG1_GLOB_BAR2_SIZE_DISABLED                        0x0
#define NVM_CFG1_GLOB_BAR2_SIZE_64K                             0x1
#define NVM_CFG1_GLOB_BAR2_SIZE_128K                            0x2
#define NVM_CFG1_GLOB_BAR2_SIZE_256K                            0x3
#define NVM_CFG1_GLOB_BAR2_SIZE_512K                            0x4
#define NVM_CFG1_GLOB_BAR2_SIZE_1M                              0x5
#define NVM_CFG1_GLOB_BAR2_SIZE_2M                              0x6
#define NVM_CFG1_GLOB_BAR2_SIZE_4M                              0x7
#define NVM_CFG1_GLOB_BAR2_SIZE_8M                              0x8
#define NVM_CFG1_GLOB_BAR2_SIZE_16M                             0x9
#define NVM_CFG1_GLOB_BAR2_SIZE_32M                             0xA
#define NVM_CFG1_GLOB_BAR2_SIZE_64M                             0xB
#define NVM_CFG1_GLOB_BAR2_SIZE_128M                            0xC
#define NVM_CFG1_GLOB_BAR2_SIZE_256M                            0xD
#define NVM_CFG1_GLOB_BAR2_SIZE_512M                            0xE
#define NVM_CFG1_GLOB_BAR2_SIZE_1G                              0xF

	u32 eagle_txfir_main;					/* 0x5C */
#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_MASK                     0x000000FF
#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_OFFSET                   0
#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_MASK                     0x0000FF00
#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_OFFSET                   8
#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_MASK                     0x00FF0000
#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_OFFSET                   16
#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_MASK                     0xFF000000
#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_OFFSET                   24

	u32 eagle_txfir_post;					/* 0x60 */
#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_MASK                     0x000000FF
#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_OFFSET                   0
#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_MASK                     0x0000FF00
#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_OFFSET                   8
#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_MASK                     0x00FF0000
#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_OFFSET                   16
#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_MASK                     0xFF000000
#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_OFFSET                   24

	u32 falcon_txfir_main;					/* 0x64 */
#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_MASK                     0x000000FF
#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_OFFSET                   0
#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_MASK                     0x0000FF00
#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_OFFSET                   8
#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_MASK                     0x00FF0000
#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_OFFSET                   16
#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_MASK                     0xFF000000
#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_OFFSET                   24

	u32 falcon_txfir_post;					/* 0x68 */
#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_MASK                     0x000000FF
#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_OFFSET                   0
#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_MASK                     0x0000FF00
#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_OFFSET                   8
#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_MASK                     0x00FF0000
#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_OFFSET                   16
#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_MASK                     0xFF000000
#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_OFFSET                   24

	u32 manufacture_ver;					/* 0x6C */
#define NVM_CFG1_GLOB_MANUF0_VER_MASK                           0x0000003F
#define NVM_CFG1_GLOB_MANUF0_VER_OFFSET                         0
#define NVM_CFG1_GLOB_MANUF1_VER_MASK                           0x00000FC0
#define NVM_CFG1_GLOB_MANUF1_VER_OFFSET                         6
#define NVM_CFG1_GLOB_MANUF2_VER_MASK                           0x0003F000
#define NVM_CFG1_GLOB_MANUF2_VER_OFFSET                         12
#define NVM_CFG1_GLOB_MANUF3_VER_MASK                           0x00FC0000
#define NVM_CFG1_GLOB_MANUF3_VER_OFFSET                         18
#define NVM_CFG1_GLOB_MANUF4_VER_MASK                           0x3F000000
#define NVM_CFG1_GLOB_MANUF4_VER_OFFSET                         24

	u32 manufacture_time;					/* 0x70 */
#define NVM_CFG1_GLOB_MANUF0_TIME_MASK                          0x0000003F
#define NVM_CFG1_GLOB_MANUF0_TIME_OFFSET                        0
#define NVM_CFG1_GLOB_MANUF1_TIME_MASK                          0x00000FC0
#define NVM_CFG1_GLOB_MANUF1_TIME_OFFSET                        6
#define NVM_CFG1_GLOB_MANUF2_TIME_MASK                          0x0003F000
#define NVM_CFG1_GLOB_MANUF2_TIME_OFFSET                        12

	u32 led_global_settings;				/* 0x74 */
#define NVM_CFG1_GLOB_LED_SWAP_0_MASK                           0x0000000F
#define NVM_CFG1_GLOB_LED_SWAP_0_OFFSET                         0
#define NVM_CFG1_GLOB_LED_SWAP_1_MASK                           0x000000F0
#define NVM_CFG1_GLOB_LED_SWAP_1_OFFSET                         4
#define NVM_CFG1_GLOB_LED_SWAP_2_MASK                           0x00000F00
#define NVM_CFG1_GLOB_LED_SWAP_2_OFFSET                         8
#define NVM_CFG1_GLOB_LED_SWAP_3_MASK                           0x0000F000
#define NVM_CFG1_GLOB_LED_SWAP_3_OFFSET                         12

	u32	generic_cont1;					/* 0x78 */
#define NVM_CFG1_GLOB_AVS_DAC_CODE_MASK                         0x000003FF
#define NVM_CFG1_GLOB_AVS_DAC_CODE_OFFSET                       0

	u32	mbi_version;					/* 0x7C */
#define NVM_CFG1_GLOB_MBI_VERSION_0_MASK                        0x000000FF
#define NVM_CFG1_GLOB_MBI_VERSION_0_OFFSET                      0
#define NVM_CFG1_GLOB_MBI_VERSION_1_MASK                        0x0000FF00
#define NVM_CFG1_GLOB_MBI_VERSION_1_OFFSET                      8
#define NVM_CFG1_GLOB_MBI_VERSION_2_MASK                        0x00FF0000
#define NVM_CFG1_GLOB_MBI_VERSION_2_OFFSET                      16

	u32	mbi_date;					/* 0x80 */

	u32	misc_sig;					/* 0x84 */

	/*  Define the GPIO mapping to switch i2c mux */
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_0_MASK                   0x000000FF
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_0_OFFSET                 0
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_1_MASK                   0x0000FF00
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_1_OFFSET                 8
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__NA                      0x0
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO0                   0x1
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO1                   0x2
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO2                   0x3
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO3                   0x4
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO4                   0x5
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO5                   0x6
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO6                   0x7
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO7                   0x8
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO8                   0x9
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO9                   0xA
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO10                  0xB
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO11                  0xC
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO12                  0xD
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO13                  0xE
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO14                  0xF
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO15                  0x10
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO16                  0x11
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO17                  0x12
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO18                  0x13
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO19                  0x14
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO20                  0x15
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO21                  0x16
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO22                  0x17
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO23                  0x18
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO24                  0x19
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO25                  0x1A
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO26                  0x1B
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO27                  0x1C
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO28                  0x1D
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO29                  0x1E
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO30                  0x1F
#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO31                  0x20

	u32 reserved[46];					/* 0x88 */
};

struct nvm_cfg1_path {
	u32 reserved[30];					/* 0x0 */
};

struct nvm_cfg1_port {
	u32 power_dissipated;					/* 0x0 */
#define NVM_CFG1_PORT_POWER_DIS_D0_MASK                         0x000000FF
#define NVM_CFG1_PORT_POWER_DIS_D0_OFFSET                       0
#define NVM_CFG1_PORT_POWER_DIS_D1_MASK                         0x0000FF00
#define NVM_CFG1_PORT_POWER_DIS_D1_OFFSET                       8
#define NVM_CFG1_PORT_POWER_DIS_D2_MASK                         0x00FF0000
#define NVM_CFG1_PORT_POWER_DIS_D2_OFFSET                       16
#define NVM_CFG1_PORT_POWER_DIS_D3_MASK                         0xFF000000
#define NVM_CFG1_PORT_POWER_DIS_D3_OFFSET                       24

	u32 power_consumed;					/* 0x4 */
#define NVM_CFG1_PORT_POWER_CONS_D0_MASK                        0x000000FF
#define NVM_CFG1_PORT_POWER_CONS_D0_OFFSET                      0
#define NVM_CFG1_PORT_POWER_CONS_D1_MASK                        0x0000FF00
#define NVM_CFG1_PORT_POWER_CONS_D1_OFFSET                      8
#define NVM_CFG1_PORT_POWER_CONS_D2_MASK                        0x00FF0000
#define NVM_CFG1_PORT_POWER_CONS_D2_OFFSET                      16
#define NVM_CFG1_PORT_POWER_CONS_D3_MASK                        0xFF000000
#define NVM_CFG1_PORT_POWER_CONS_D3_OFFSET                      24

	u32 generic_cont0;					/* 0x8 */
#define NVM_CFG1_PORT_LED_MODE_MASK                             0x000000FF
#define NVM_CFG1_PORT_LED_MODE_OFFSET                           0
#define NVM_CFG1_PORT_LED_MODE_MAC1                             0x0
#define NVM_CFG1_PORT_LED_MODE_PHY1                             0x1
#define NVM_CFG1_PORT_LED_MODE_PHY2                             0x2
#define NVM_CFG1_PORT_LED_MODE_PHY3                             0x3
#define NVM_CFG1_PORT_LED_MODE_MAC2                             0x4
#define NVM_CFG1_PORT_LED_MODE_PHY4                             0x5
#define NVM_CFG1_PORT_LED_MODE_PHY5                             0x6
#define NVM_CFG1_PORT_LED_MODE_PHY6                             0x7
#define NVM_CFG1_PORT_LED_MODE_MAC3                             0x8
#define NVM_CFG1_PORT_LED_MODE_PHY7                             0x9
#define NVM_CFG1_PORT_LED_MODE_PHY8                             0xA
#define NVM_CFG1_PORT_LED_MODE_PHY9                             0xB
#define NVM_CFG1_PORT_LED_MODE_MAC4                             0xC
#define NVM_CFG1_PORT_LED_MODE_PHY10                            0xD
#define NVM_CFG1_PORT_LED_MODE_PHY11                            0xE
#define NVM_CFG1_PORT_LED_MODE_PHY12                            0xF
#define NVM_CFG1_PORT_ROCE_PRIORITY_MASK                        0x0000FF00
#define NVM_CFG1_PORT_ROCE_PRIORITY_OFFSET                      8
#define NVM_CFG1_PORT_DCBX_MODE_MASK                            0x000F0000
#define NVM_CFG1_PORT_DCBX_MODE_OFFSET                          16
#define NVM_CFG1_PORT_DCBX_MODE_DISABLED                        0x0
#define NVM_CFG1_PORT_DCBX_MODE_IEEE                            0x1
#define NVM_CFG1_PORT_DCBX_MODE_CEE                             0x2
#define NVM_CFG1_PORT_DCBX_MODE_DYNAMIC                         0x3

	u32	pcie_cfg;					/* 0xC */
#define NVM_CFG1_PORT_RESERVED15_MASK                           0x00000007
#define NVM_CFG1_PORT_RESERVED15_OFFSET                         0

	u32	features;					/* 0x10 */
#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_MASK           0x00000001
#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_OFFSET         0
#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_DISABLED       0x0
#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_ENABLED        0x1
#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_MASK                     0x00000002
#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_OFFSET                   1
#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_DISABLED                 0x0
#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_ENABLED                  0x1

	u32 speed_cap_mask;					/* 0x14 */
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK            0x0000FFFF
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_OFFSET          0
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G              0x1
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G             0x2
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G             0x8
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G             0x10
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G             0x20
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_100G            0x40
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_MASK            0xFFFF0000
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_OFFSET          16
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_1G              0x1
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_10G             0x2
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_25G             0x8
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_40G             0x10
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_50G             0x20
#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_100G            0x40

	u32 link_settings;					/* 0x18 */
#define NVM_CFG1_PORT_DRV_LINK_SPEED_MASK                       0x0000000F
#define NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET                     0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG                    0x0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_1G                         0x1
#define NVM_CFG1_PORT_DRV_LINK_SPEED_10G                        0x2
#define NVM_CFG1_PORT_DRV_LINK_SPEED_25G                        0x4
#define NVM_CFG1_PORT_DRV_LINK_SPEED_40G                        0x5
#define NVM_CFG1_PORT_DRV_LINK_SPEED_50G                        0x6
#define NVM_CFG1_PORT_DRV_LINK_SPEED_100G                       0x7
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK                     0x00000070
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET                   4
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG                  0x1
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX                       0x2
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX                       0x4
#define NVM_CFG1_PORT_MFW_LINK_SPEED_MASK                       0x00000780
#define NVM_CFG1_PORT_MFW_LINK_SPEED_OFFSET                     7
#define NVM_CFG1_PORT_MFW_LINK_SPEED_AUTONEG                    0x0
#define NVM_CFG1_PORT_MFW_LINK_SPEED_1G                         0x1
#define NVM_CFG1_PORT_MFW_LINK_SPEED_10G                        0x2
#define NVM_CFG1_PORT_MFW_LINK_SPEED_25G                        0x4
#define NVM_CFG1_PORT_MFW_LINK_SPEED_40G                        0x5
#define NVM_CFG1_PORT_MFW_LINK_SPEED_50G                        0x6
#define NVM_CFG1_PORT_MFW_LINK_SPEED_100G                       0x7
#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_MASK                     0x00003800
#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_OFFSET                   11
#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_AUTONEG                  0x1
#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_RX                       0x2
#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_TX                       0x4
#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_MASK      0x00004000
#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_OFFSET    14
#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_DISABLED  0x0
#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_ENABLED   0x1

	u32 phy_cfg;						/* 0x1C */
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_MASK                  0x0000FFFF
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_OFFSET                0
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_HIGIG                 0x1
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_SCRAMBLER             0x2
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_FIBER                 0x4
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_DISABLE_CL72_AN       0x8
#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_DISABLE_FEC_AN        0x10
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_MASK                 0x00FF0000
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_OFFSET               16
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_BYPASS               0x0
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR                   0x2
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR2                  0x3
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR4                  0x4
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XFI                  0x8
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_SFI                  0x9
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_1000X                0xB
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_SGMII                0xC
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XLAUI                0xD
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_CAUI                 0xE
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XLPPI                0xF
#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_CPPI                 0x10
#define NVM_CFG1_PORT_AN_MODE_MASK                              0xFF000000
#define NVM_CFG1_PORT_AN_MODE_OFFSET                            24
#define NVM_CFG1_PORT_AN_MODE_NONE                              0x0
#define NVM_CFG1_PORT_AN_MODE_CL73                              0x1
#define NVM_CFG1_PORT_AN_MODE_CL37                              0x2
#define NVM_CFG1_PORT_AN_MODE_CL73_BAM                          0x3
#define NVM_CFG1_PORT_AN_MODE_CL37_BAM                          0x4
#define NVM_CFG1_PORT_AN_MODE_HPAM                              0x5
#define NVM_CFG1_PORT_AN_MODE_SGMII                             0x6

	u32 mgmt_traffic;					/* 0x20 */
#define NVM_CFG1_PORT_RESERVED61_MASK                           0x0000000F
#define NVM_CFG1_PORT_RESERVED61_OFFSET                         0
#define NVM_CFG1_PORT_RESERVED61_DISABLED                       0x0
#define NVM_CFG1_PORT_RESERVED61_NCSI_OVER_RMII                 0x1
#define NVM_CFG1_PORT_RESERVED61_NCSI_OVER_SMBUS                0x2

	u32 ext_phy;						/* 0x24 */
#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_MASK                    0x000000FF
#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_OFFSET                  0
#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_NONE                    0x0
#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_BCM84844                0x1
#define NVM_CFG1_PORT_EXTERNAL_PHY_ADDRESS_MASK                 0x0000FF00
#define NVM_CFG1_PORT_EXTERNAL_PHY_ADDRESS_OFFSET               8

	u32 mba_cfg1;						/* 0x28 */
#define NVM_CFG1_PORT_MBA_MASK                                  0x00000001
#define NVM_CFG1_PORT_MBA_OFFSET                                0
#define NVM_CFG1_PORT_MBA_DISABLED                              0x0
#define NVM_CFG1_PORT_MBA_ENABLED                               0x1
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_MASK                        0x00000006
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_OFFSET                      1
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_AUTO                        0x0
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_BBS                         0x1
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_INT18H                      0x2
#define NVM_CFG1_PORT_MBA_BOOT_TYPE_INT19H                      0x3
#define NVM_CFG1_PORT_MBA_DELAY_TIME_MASK                       0x00000078
#define NVM_CFG1_PORT_MBA_DELAY_TIME_OFFSET                     3
#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_MASK                    0x00000080
#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_OFFSET                  7
#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_CTRL_S                  0x0
#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_CTRL_B                  0x1
#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_MASK                0x00000100
#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_OFFSET              8
#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_DISABLED            0x0
#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_ENABLED             0x1
#define NVM_CFG1_PORT_RESERVED5_MASK                            0x0001FE00
#define NVM_CFG1_PORT_RESERVED5_OFFSET                          9
#define NVM_CFG1_PORT_RESERVED5_DISABLED                        0x0
#define NVM_CFG1_PORT_RESERVED5_2K                              0x1
#define NVM_CFG1_PORT_RESERVED5_4K                              0x2
#define NVM_CFG1_PORT_RESERVED5_8K                              0x3
#define NVM_CFG1_PORT_RESERVED5_16K                             0x4
#define NVM_CFG1_PORT_RESERVED5_32K                             0x5
#define NVM_CFG1_PORT_RESERVED5_64K                             0x6
#define NVM_CFG1_PORT_RESERVED5_128K                            0x7
#define NVM_CFG1_PORT_RESERVED5_256K                            0x8
#define NVM_CFG1_PORT_RESERVED5_512K                            0x9
#define NVM_CFG1_PORT_RESERVED5_1M                              0xA
#define NVM_CFG1_PORT_RESERVED5_2M                              0xB
#define NVM_CFG1_PORT_RESERVED5_4M                              0xC
#define NVM_CFG1_PORT_RESERVED5_8M                              0xD
#define NVM_CFG1_PORT_RESERVED5_16M                             0xE
#define NVM_CFG1_PORT_RESERVED5_32M                             0xF
#define NVM_CFG1_PORT_MBA_LINK_SPEED_MASK                       0x001E0000
#define NVM_CFG1_PORT_MBA_LINK_SPEED_OFFSET                     17
#define NVM_CFG1_PORT_MBA_LINK_SPEED_AUTONEG                    0x0
#define NVM_CFG1_PORT_MBA_LINK_SPEED_1G                         0x1
#define NVM_CFG1_PORT_MBA_LINK_SPEED_10G                        0x2
#define NVM_CFG1_PORT_MBA_LINK_SPEED_25G                        0x4
#define NVM_CFG1_PORT_MBA_LINK_SPEED_40G                        0x5
#define NVM_CFG1_PORT_MBA_LINK_SPEED_50G                        0x6
#define NVM_CFG1_PORT_MBA_LINK_SPEED_100G                       0x7
#define NVM_CFG1_PORT_MBA_BOOT_RETRY_COUNT_MASK                 0x00E00000
#define NVM_CFG1_PORT_MBA_BOOT_RETRY_COUNT_OFFSET               21

	u32	mba_cfg2;					/* 0x2C */
#define NVM_CFG1_PORT_MBA_VLAN_VALUE_MASK                       0x0000FFFF
#define NVM_CFG1_PORT_MBA_VLAN_VALUE_OFFSET                     0
#define NVM_CFG1_PORT_MBA_VLAN_MASK                             0x00010000
#define NVM_CFG1_PORT_MBA_VLAN_OFFSET                           16

	u32	vf_cfg;						/* 0x30 */
#define NVM_CFG1_PORT_RESERVED8_MASK                            0x0000FFFF
#define NVM_CFG1_PORT_RESERVED8_OFFSET                          0
#define NVM_CFG1_PORT_RESERVED6_MASK                            0x000F0000
#define NVM_CFG1_PORT_RESERVED6_OFFSET                          16
#define NVM_CFG1_PORT_RESERVED6_DISABLED                        0x0
#define NVM_CFG1_PORT_RESERVED6_4K                              0x1
#define NVM_CFG1_PORT_RESERVED6_8K                              0x2
#define NVM_CFG1_PORT_RESERVED6_16K                             0x3
#define NVM_CFG1_PORT_RESERVED6_32K                             0x4
#define NVM_CFG1_PORT_RESERVED6_64K                             0x5
#define NVM_CFG1_PORT_RESERVED6_128K                            0x6
#define NVM_CFG1_PORT_RESERVED6_256K                            0x7
#define NVM_CFG1_PORT_RESERVED6_512K                            0x8
#define NVM_CFG1_PORT_RESERVED6_1M                              0x9
#define NVM_CFG1_PORT_RESERVED6_2M                              0xA
#define NVM_CFG1_PORT_RESERVED6_4M                              0xB
#define NVM_CFG1_PORT_RESERVED6_8M                              0xC
#define NVM_CFG1_PORT_RESERVED6_16M                             0xD
#define NVM_CFG1_PORT_RESERVED6_32M                             0xE
#define NVM_CFG1_PORT_RESERVED6_64M                             0xF

	struct nvm_cfg_mac_address	lldp_mac_address;	/* 0x34 */

	u32				led_port_settings;	/* 0x3C */
#define NVM_CFG1_PORT_LANE_LED_SPD_0_SEL_MASK                   0x000000FF
#define NVM_CFG1_PORT_LANE_LED_SPD_0_SEL_OFFSET                 0
#define NVM_CFG1_PORT_LANE_LED_SPD_1_SEL_MASK                   0x0000FF00
#define NVM_CFG1_PORT_LANE_LED_SPD_1_SEL_OFFSET                 8
#define NVM_CFG1_PORT_LANE_LED_SPD_2_SEL_MASK                   0x00FF0000
#define NVM_CFG1_PORT_LANE_LED_SPD_2_SEL_OFFSET                 16
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_1G                      0x1
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_10G                     0x2
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_25G                     0x8
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_40G                     0x10
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_50G                     0x20
#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_100G                    0x40

	u32 transceiver_00;					/* 0x40 */

	/*  Define for mapping of transceiver signal module absent */
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_MASK                     0x000000FF
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_OFFSET                   0
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_NA                       0x0
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO0                    0x1
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO1                    0x2
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO2                    0x3
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO3                    0x4
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO4                    0x5
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO5                    0x6
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO6                    0x7
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO7                    0x8
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO8                    0x9
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO9                    0xA
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO10                   0xB
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO11                   0xC
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO12                   0xD
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO13                   0xE
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO14                   0xF
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO15                   0x10
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO16                   0x11
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO17                   0x12
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO18                   0x13
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO19                   0x14
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO20                   0x15
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO21                   0x16
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO22                   0x17
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO23                   0x18
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO24                   0x19
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO25                   0x1A
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO26                   0x1B
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO27                   0x1C
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO28                   0x1D
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO29                   0x1E
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO30                   0x1F
#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO31                   0x20
	/*  Define the GPIO mux settings  to switch i2c mux to this port */
#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_0_MASK                  0x00000F00
#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_0_OFFSET                8
#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_1_MASK                  0x0000F000
#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_1_OFFSET                12

	u32 reserved[133];					/* 0x44 */
};

struct nvm_cfg1_func {
	struct nvm_cfg_mac_address	mac_address;		/* 0x0 */

	u32				rsrv1;			/* 0x8 */
#define NVM_CFG1_FUNC_RESERVED1_MASK                            0x0000FFFF
#define NVM_CFG1_FUNC_RESERVED1_OFFSET                          0
#define NVM_CFG1_FUNC_RESERVED2_MASK                            0xFFFF0000
#define NVM_CFG1_FUNC_RESERVED2_OFFSET                          16

	u32				rsrv2;			/* 0xC */
#define NVM_CFG1_FUNC_RESERVED3_MASK                            0x0000FFFF
#define NVM_CFG1_FUNC_RESERVED3_OFFSET                          0
#define NVM_CFG1_FUNC_RESERVED4_MASK                            0xFFFF0000
#define NVM_CFG1_FUNC_RESERVED4_OFFSET                          16

	u32				device_id;		/* 0x10 */
#define NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_MASK                  0x0000FFFF
#define NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_OFFSET                0
#define NVM_CFG1_FUNC_VENDOR_DEVICE_ID_MASK                     0xFFFF0000
#define NVM_CFG1_FUNC_VENDOR_DEVICE_ID_OFFSET                   16

	u32				cmn_cfg;		/* 0x14 */
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_MASK                    0x00000007
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_OFFSET                  0
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_PXE                     0x0
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_RPL                     0x1
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_BOOTP                   0x2
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_ISCSI_BOOT              0x3
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_FCOE_BOOT               0x4
#define NVM_CFG1_FUNC_MBA_BOOT_PROTOCOL_NONE                    0x7
#define NVM_CFG1_FUNC_VF_PCI_DEVICE_ID_MASK                     0x0007FFF8
#define NVM_CFG1_FUNC_VF_PCI_DEVICE_ID_OFFSET                   3
#define NVM_CFG1_FUNC_PERSONALITY_MASK                          0x00780000
#define NVM_CFG1_FUNC_PERSONALITY_OFFSET                        19
#define NVM_CFG1_FUNC_PERSONALITY_ETHERNET                      0x0
#define NVM_CFG1_FUNC_PERSONALITY_ISCSI                         0x1
#define NVM_CFG1_FUNC_PERSONALITY_FCOE                          0x2
#define NVM_CFG1_FUNC_PERSONALITY_ROCE                          0x3
#define NVM_CFG1_FUNC_BANDWIDTH_WEIGHT_MASK                     0x7F800000
#define NVM_CFG1_FUNC_BANDWIDTH_WEIGHT_OFFSET                   23
#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_MASK                   0x80000000
#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_OFFSET                 31
#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_DISABLED               0x0
#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_ENABLED                0x1

	u32 pci_cfg;						/* 0x18 */
#define NVM_CFG1_FUNC_NUMBER_OF_VFS_PER_PF_MASK                 0x0000007F
#define NVM_CFG1_FUNC_NUMBER_OF_VFS_PER_PF_OFFSET               0
#define NVM_CFG1_FUNC_RESERVESD12_MASK                          0x00003F80
#define NVM_CFG1_FUNC_RESERVESD12_OFFSET                        7
#define NVM_CFG1_FUNC_BAR1_SIZE_MASK                            0x0003C000
#define NVM_CFG1_FUNC_BAR1_SIZE_OFFSET                          14
#define NVM_CFG1_FUNC_BAR1_SIZE_DISABLED                        0x0
#define NVM_CFG1_FUNC_BAR1_SIZE_64K                             0x1
#define NVM_CFG1_FUNC_BAR1_SIZE_128K                            0x2
#define NVM_CFG1_FUNC_BAR1_SIZE_256K                            0x3
#define NVM_CFG1_FUNC_BAR1_SIZE_512K                            0x4
#define NVM_CFG1_FUNC_BAR1_SIZE_1M                              0x5
#define NVM_CFG1_FUNC_BAR1_SIZE_2M                              0x6
#define NVM_CFG1_FUNC_BAR1_SIZE_4M                              0x7
#define NVM_CFG1_FUNC_BAR1_SIZE_8M                              0x8
#define NVM_CFG1_FUNC_BAR1_SIZE_16M                             0x9
#define NVM_CFG1_FUNC_BAR1_SIZE_32M                             0xA
#define NVM_CFG1_FUNC_BAR1_SIZE_64M                             0xB
#define NVM_CFG1_FUNC_BAR1_SIZE_128M                            0xC
#define NVM_CFG1_FUNC_BAR1_SIZE_256M                            0xD
#define NVM_CFG1_FUNC_BAR1_SIZE_512M                            0xE
#define NVM_CFG1_FUNC_BAR1_SIZE_1G                              0xF
#define NVM_CFG1_FUNC_MAX_BANDWIDTH_MASK                        0x03FC0000
#define NVM_CFG1_FUNC_MAX_BANDWIDTH_OFFSET                      18

	struct nvm_cfg_mac_address	fcoe_node_wwn_mac_addr;	/* 0x1C */

	struct nvm_cfg_mac_address	fcoe_port_wwn_mac_addr;	/* 0x24 */

	u32				reserved[9];		/* 0x2C */
};

struct nvm_cfg1 {
	struct nvm_cfg1_glob	glob;				/* 0x0 */

	struct nvm_cfg1_path	path[MCP_GLOB_PATH_MAX];	/* 0x140 */

	struct nvm_cfg1_port	port[MCP_GLOB_PORT_MAX];	/* 0x230 */

	struct nvm_cfg1_func	func[MCP_GLOB_FUNC_MAX];	/* 0xB90 */
};

/******************************************
* nvm_cfg structs
******************************************/

enum nvm_cfg_sections {
	NVM_CFG_SECTION_NVM_CFG1,
	NVM_CFG_SECTION_MAX
};

struct nvm_cfg {
	u32		num_sections;
	u32		sections_offset[NVM_CFG_SECTION_MAX];
	struct nvm_cfg1 cfg1;
};

#define PORT_0          0
#define PORT_1          1
#define PORT_2          2
#define PORT_3          3

extern struct spad_layout g_spad;

#define MCP_SPAD_SIZE                       0x00028000  /* 160 KB */

#define SPAD_OFFSET(addr) (((u32)addr - (u32)CPU_SPAD_BASE))

#define TO_OFFSIZE(_offset, _size)				\
	(u32)((((u32)(_offset) >> 2) << OFFSIZE_OFFSET_SHIFT) |	\
	      (((u32)(_size) >> 2) << OFFSIZE_SIZE_SHIFT))

enum spad_sections {
	SPAD_SECTION_TRACE,
	SPAD_SECTION_NVM_CFG,
	SPAD_SECTION_PUBLIC,
	SPAD_SECTION_PRIVATE,
	SPAD_SECTION_MAX
};

struct spad_layout {
	struct nvm_cfg		nvm_cfg;
	struct mcp_public_data	public_data;
};

#define CRC_MAGIC_VALUE                     0xDEBB20E3
#define CRC32_POLYNOMIAL                    0xEDB88320
#define NVM_CRC_SIZE                            (sizeof(u32))

enum nvm_sw_arbitrator {
	NVM_SW_ARB_HOST,
	NVM_SW_ARB_MCP,
	NVM_SW_ARB_UART,
	NVM_SW_ARB_RESERVED
};

/****************************************************************************
* Boot Strap Region                                                        *
****************************************************************************/
struct legacy_bootstrap_region {
	u32	magic_value;
#define NVM_MAGIC_VALUE          0x669955aa
	u32	sram_start_addr;
	u32	code_len;               /* boot code length (in dwords) */
	u32	code_start_addr;
	u32	crc;                    /* 32-bit CRC */
};

/****************************************************************************
* Directories Region                                                       *
****************************************************************************/
struct nvm_code_entry {
	u32	image_type;             /* Image type */
	u32	nvm_start_addr;         /* NVM address of the image */
	u32	len;                    /* Include CRC */
	u32	sram_start_addr;
	u32	sram_run_addr;          /* Relevant in case of MIM only */
};

enum nvm_image_type {
	NVM_TYPE_TIM1		= 0x01,
	NVM_TYPE_TIM2		= 0x02,
	NVM_TYPE_MIM1		= 0x03,
	NVM_TYPE_MIM2		= 0x04,
	NVM_TYPE_MBA		= 0x05,
	NVM_TYPE_MODULES_PN	= 0x06,
	NVM_TYPE_VPD		= 0x07,
	NVM_TYPE_MFW_TRACE1	= 0x08,
	NVM_TYPE_MFW_TRACE2	= 0x09,
	NVM_TYPE_NVM_CFG1	= 0x0a,
	NVM_TYPE_L2B		= 0x0b,
	NVM_TYPE_DIR1		= 0x0c,
	NVM_TYPE_EAGLE_FW1	= 0x0d,
	NVM_TYPE_FALCON_FW1	= 0x0e,
	NVM_TYPE_PCIE_FW1	= 0x0f,
	NVM_TYPE_HW_SET		= 0x10,
	NVM_TYPE_LIM		= 0x11,
	NVM_TYPE_AVS_FW1	= 0x12,
	NVM_TYPE_DIR2		= 0x13,
	NVM_TYPE_CCM		= 0x14,
	NVM_TYPE_EAGLE_FW2	= 0x15,
	NVM_TYPE_FALCON_FW2	= 0x16,
	NVM_TYPE_PCIE_FW2	= 0x17,
	NVM_TYPE_AVS_FW2	= 0x18,

	NVM_TYPE_MAX,
};

#define MAX_NVM_DIR_ENTRIES 200

struct nvm_dir {
	s32 seq;
#define NVM_DIR_NEXT_MFW_MASK   0x00000001
#define NVM_DIR_SEQ_MASK        0xfffffffe
#define NVM_DIR_NEXT_MFW(seq) ((seq) & NVM_DIR_NEXT_MFW_MASK)

#define IS_DIR_SEQ_VALID(seq) ((seq & NVM_DIR_SEQ_MASK) != NVM_DIR_SEQ_MASK)

	u32			num_images;
	u32			rsrv;
	struct nvm_code_entry	code[1]; /* Up to MAX_NVM_DIR_ENTRIES */
};

#define NVM_DIR_SIZE(_num_images) (sizeof(struct nvm_dir) +		 \
				   (_num_images -			 \
				    1) * sizeof(struct nvm_code_entry) + \
				   NVM_CRC_SIZE)

struct nvm_vpd_image {
	u32	format_revision;
#define VPD_IMAGE_VERSION        1

	/* This array length depends on the number of VPD fields */
	u8	vpd_data[1];
};

/****************************************************************************
* NVRAM FULL MAP                                                           *
****************************************************************************/
#define DIR_ID_1    (0)
#define DIR_ID_2    (1)
#define MAX_DIR_IDS (2)

#define MFW_BUNDLE_1    (0)
#define MFW_BUNDLE_2    (1)
#define MAX_MFW_BUNDLES (2)

#define FLASH_PAGE_SIZE 0x1000
#define NVM_DIR_MAX_SIZE    (FLASH_PAGE_SIZE)           /* 4Kb */
#define ASIC_MIM_MAX_SIZE   (300 * FLASH_PAGE_SIZE)     /* 1.2Mb */
#define FPGA_MIM_MAX_SIZE   (25 * FLASH_PAGE_SIZE)      /* 60Kb */

#define LIM_MAX_SIZE        ((2 *				      \
			      FLASH_PAGE_SIZE) -		      \
			     sizeof(struct legacy_bootstrap_region) - \
			     NVM_RSV_SIZE)
#define LIM_OFFSET          (NVM_OFFSET(lim_image))
#define NVM_RSV_SIZE            (44)
#define MIM_MAX_SIZE(is_asic) ((is_asic) ? ASIC_MIM_MAX_SIZE : \
			       FPGA_MIM_MAX_SIZE)
#define MIM_OFFSET(idx, is_asic) (NVM_OFFSET(dir[MAX_MFW_BUNDLES]) + \
				  ((idx ==			     \
				    NVM_TYPE_MIM2) ? MIM_MAX_SIZE(is_asic) : 0))
#define NVM_FIXED_AREA_SIZE(is_asic) (sizeof(struct nvm_image) + \
				      MIM_MAX_SIZE(is_asic) * 2)

union nvm_dir_union {
	struct nvm_dir	dir;
	u8		page[FLASH_PAGE_SIZE];
};

/*                        Address
 *  +-------------------+ 0x000000
 *  |    Bootstrap:     |
 *  | magic_number      |
 *  | sram_start_addr   |
 *  | code_len          |
 *  | code_start_addr   |
 *  | crc               |
 *  +-------------------+ 0x000014
 *  | rsrv              |
 *  +-------------------+ 0x000040
 *  | LIM               |
 *  +-------------------+ 0x002000
 *  | Dir1              |
 *  +-------------------+ 0x003000
 *  | Dir2              |
 *  +-------------------+ 0x004000
 *  | MIM1              |
 *  +-------------------+ 0x130000
 *  | MIM2              |
 *  +-------------------+ 0x25C000
 *  | Rest Images:      |
 *  | TIM1/2            |
 *  | MFW_TRACE1/2      |
 *  | Eagle/Falcon FW   |
 *  | PCIE/AVS FW       |
 *  | MBA/CCM/L2B       |
 *  | VPD               |
 *  | optic_modules     |
 *  |  ...              |
 *  +-------------------+ 0x400000
 */
struct nvm_image {
/*********** !!!  FIXED SECTIONS  !!! DO NOT MODIFY !!! **********************/
	/* NVM Offset  (size) */
	struct legacy_bootstrap_region	bootstrap;
	u8				rsrv[NVM_RSV_SIZE];
	u8				lim_image[LIM_MAX_SIZE];
	union nvm_dir_union		dir[MAX_MFW_BUNDLES];

	/* MIM1_IMAGE                              0x004000 (0x12c000) */
	/* MIM2_IMAGE                              0x130000 (0x12c000) */
/*********** !!!  FIXED SECTIONS  !!! DO NOT MODIFY !!! **********************/
};                              /* 0x134 */

#define NVM_OFFSET(f)	((u32_t)((int_ptr_t)(&(((struct nvm_image *)0)->f))))

struct hw_set_info {
	u32	reg_type;
#define GRC_REG_TYPE 1
#define PHY_REG_TYPE 2
#define PCI_REG_TYPE 4

	u32	bank_num;
	u32	pf_num;
	u32	operation;
#define READ_OP     1
#define WRITE_OP    2
#define RMW_SET_OP  3
#define RMW_CLR_OP  4

	u32	reg_addr;
	u32	reg_data;

	u32	reset_type;
#define POR_RESET_TYPE	BIT(0)
#define HARD_RESET_TYPE	BIT(1)
#define CORE_RESET_TYPE	BIT(2)
#define MCP_RESET_TYPE	BIT(3)
#define PERSET_ASSERT	BIT(4)
#define PERSET_DEASSERT	BIT(5)
};

struct hw_set_image {
	u32			format_version;
#define HW_SET_IMAGE_VERSION        1
	u32			no_hw_sets;

	/* This array length depends on the no_hw_sets */
	struct hw_set_info	hw_sets[1];
};

#endif
