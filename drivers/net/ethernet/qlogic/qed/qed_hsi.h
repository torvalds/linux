/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
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
#include <linux/qed/storage_common.h>
#include <linux/qed/tcp_common.h>
#include <linux/qed/fcoe_common.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/iscsi_common.h>
#include <linux/qed/nvmetcp_common.h>
#include <linux/qed/iwarp_common.h>
#include <linux/qed/rdma_common.h>
#include <linux/qed/roce_common.h>
#include <linux/qed/qed_fcoe_if.h>

struct qed_hwfn;
struct qed_ptt;

/* Opcodes for the event ring */
enum common_event_opcode {
	COMMON_EVENT_PF_START,
	COMMON_EVENT_PF_STOP,
	COMMON_EVENT_VF_START,
	COMMON_EVENT_VF_STOP,
	COMMON_EVENT_VF_PF_CHANNEL,
	COMMON_EVENT_VF_FLR,
	COMMON_EVENT_PF_UPDATE,
	COMMON_EVENT_MALICIOUS_VF,
	COMMON_EVENT_RL_UPDATE,
	COMMON_EVENT_EMPTY,
	MAX_COMMON_EVENT_OPCODE
};

/* Common Ramrod Command IDs */
enum common_ramrod_cmd_id {
	COMMON_RAMROD_UNUSED,
	COMMON_RAMROD_PF_START,
	COMMON_RAMROD_PF_STOP,
	COMMON_RAMROD_VF_START,
	COMMON_RAMROD_VF_STOP,
	COMMON_RAMROD_PF_UPDATE,
	COMMON_RAMROD_RL_UPDATE,
	COMMON_RAMROD_EMPTY,
	MAX_COMMON_RAMROD_CMD_ID
};

/* How ll2 should deal with packet upon errors */
enum core_error_handle {
	LL2_DROP_PACKET,
	LL2_DO_NOTHING,
	LL2_ASSERT,
	MAX_CORE_ERROR_HANDLE
};

/* Opcodes for the event ring */
enum core_event_opcode {
	CORE_EVENT_TX_QUEUE_START,
	CORE_EVENT_TX_QUEUE_STOP,
	CORE_EVENT_RX_QUEUE_START,
	CORE_EVENT_RX_QUEUE_STOP,
	CORE_EVENT_RX_QUEUE_FLUSH,
	CORE_EVENT_TX_QUEUE_UPDATE,
	CORE_EVENT_QUEUE_STATS_QUERY,
	MAX_CORE_EVENT_OPCODE
};

/* The L4 pseudo checksum mode for Core */
enum core_l4_pseudo_checksum_mode {
	CORE_L4_PSEUDO_CSUM_CORRECT_LENGTH,
	CORE_L4_PSEUDO_CSUM_ZERO_LENGTH,
	MAX_CORE_L4_PSEUDO_CHECKSUM_MODE
};

/* Light-L2 RX Producers in Tstorm RAM */
struct core_ll2_port_stats {
	struct regpair gsi_invalid_hdr;
	struct regpair gsi_invalid_pkt_length;
	struct regpair gsi_unsupported_pkt_typ;
	struct regpair gsi_crcchksm_error;
};

/* LL2 TX Per Queue Stats */
struct core_ll2_pstorm_per_queue_stat {
	struct regpair sent_ucast_bytes;
	struct regpair sent_mcast_bytes;
	struct regpair sent_bcast_bytes;
	struct regpair sent_ucast_pkts;
	struct regpair sent_mcast_pkts;
	struct regpair sent_bcast_pkts;
	struct regpair error_drop_pkts;
};

/* Light-L2 RX Producers in Tstorm RAM */
struct core_ll2_rx_prod {
	__le16 bd_prod;
	__le16 cqe_prod;
};

struct core_ll2_tstorm_per_queue_stat {
	struct regpair packet_too_big_discard;
	struct regpair no_buff_discard;
};

struct core_ll2_ustorm_per_queue_stat {
	struct regpair rcv_ucast_bytes;
	struct regpair rcv_mcast_bytes;
	struct regpair rcv_bcast_bytes;
	struct regpair rcv_ucast_pkts;
	struct regpair rcv_mcast_pkts;
	struct regpair rcv_bcast_pkts;
};

/* Structure for doorbell data, in PWM mode, for RX producers update. */
struct core_pwm_prod_update_data {
	__le16 icid; /* internal CID */
	u8 reserved0;
	u8 params;
#define CORE_PWM_PROD_UPDATE_DATA_AGG_CMD_MASK	  0x3
#define CORE_PWM_PROD_UPDATE_DATA_AGG_CMD_SHIFT   0
#define CORE_PWM_PROD_UPDATE_DATA_RESERVED1_MASK  0x3F	/* Set 0 */
#define CORE_PWM_PROD_UPDATE_DATA_RESERVED1_SHIFT 2
	struct core_ll2_rx_prod prod; /* Producers */
};

/* Core Ramrod Command IDs (light L2) */
enum core_ramrod_cmd_id {
	CORE_RAMROD_UNUSED,
	CORE_RAMROD_RX_QUEUE_START,
	CORE_RAMROD_TX_QUEUE_START,
	CORE_RAMROD_RX_QUEUE_STOP,
	CORE_RAMROD_TX_QUEUE_STOP,
	CORE_RAMROD_RX_QUEUE_FLUSH,
	CORE_RAMROD_TX_QUEUE_UPDATE,
	CORE_RAMROD_QUEUE_STATS_QUERY,
	MAX_CORE_RAMROD_CMD_ID
};

/* Core RX CQE Type for Light L2 */
enum core_roce_flavor_type {
	CORE_ROCE,
	CORE_RROCE,
	MAX_CORE_ROCE_FLAVOR_TYPE
};

/* Specifies how ll2 should deal with packets errors: packet_too_big and
 * no_buff.
 */
struct core_rx_action_on_error {
	u8 error_type;
#define CORE_RX_ACTION_ON_ERROR_PACKET_TOO_BIG_MASK	0x3
#define CORE_RX_ACTION_ON_ERROR_PACKET_TOO_BIG_SHIFT	0
#define CORE_RX_ACTION_ON_ERROR_NO_BUFF_MASK		0x3
#define CORE_RX_ACTION_ON_ERROR_NO_BUFF_SHIFT		2
#define CORE_RX_ACTION_ON_ERROR_RESERVED_MASK		0xF
#define CORE_RX_ACTION_ON_ERROR_RESERVED_SHIFT		4
};

/* Core RX BD for Light L2 */
struct core_rx_bd {
	struct regpair addr;
	__le16 reserved[4];
};

/* Core RX CM offload BD for Light L2 */
struct core_rx_bd_with_buff_len {
	struct regpair addr;
	__le16 buff_length;
	__le16 reserved[3];
};

/* Core RX CM offload BD for Light L2 */
union core_rx_bd_union {
	struct core_rx_bd rx_bd;
	struct core_rx_bd_with_buff_len rx_bd_with_len;
};

/* Opaque Data for Light L2 RX CQE */
struct core_rx_cqe_opaque_data {
	__le32 data[2];
};

/* Core RX CQE Type for Light L2 */
enum core_rx_cqe_type {
	CORE_RX_CQE_ILLEGAL_TYPE,
	CORE_RX_CQE_TYPE_REGULAR,
	CORE_RX_CQE_TYPE_GSI_OFFLOAD,
	CORE_RX_CQE_TYPE_SLOW_PATH,
	MAX_CORE_RX_CQE_TYPE
};

/* Core RX CQE for Light L2 */
struct core_rx_fast_path_cqe {
	u8 type;
	u8 placement_offset;
	struct parsing_and_err_flags parse_flags;
	__le16 packet_length;
	__le16 vlan;
	struct core_rx_cqe_opaque_data opaque_data;
	struct parsing_err_flags err_flags;
	__le16 reserved0;
	__le32 reserved1[3];
};

/* Core Rx CM offload CQE */
struct core_rx_gsi_offload_cqe {
	u8 type;
	u8 data_length_error;
	struct parsing_and_err_flags parse_flags;
	__le16 data_length;
	__le16 vlan;
	__le32 src_mac_addrhi;
	__le16 src_mac_addrlo;
	__le16 qp_id;
	__le32 src_qp;
	struct core_rx_cqe_opaque_data opaque_data;
	__le32 reserved;
};

/* Core RX CQE for Light L2 */
struct core_rx_slow_path_cqe {
	u8 type;
	u8 ramrod_cmd_id;
	__le16 echo;
	struct core_rx_cqe_opaque_data opaque_data;
	__le32 reserved1[5];
};

/* Core RX CM offload BD for Light L2 */
union core_rx_cqe_union {
	struct core_rx_fast_path_cqe rx_cqe_fp;
	struct core_rx_gsi_offload_cqe rx_cqe_gsi;
	struct core_rx_slow_path_cqe rx_cqe_sp;
};

/* Ramrod data for rx queue start ramrod */
struct core_rx_start_ramrod_data {
	struct regpair bd_base;
	struct regpair cqe_pbl_addr;
	__le16 mtu;
	__le16 sb_id;
	u8 sb_index;
	u8 complete_cqe_flg;
	u8 complete_event_flg;
	u8 drop_ttl0_flg;
	__le16 num_of_pbl_pages;
	u8 inner_vlan_stripping_en;
	u8 report_outer_vlan;
	u8 queue_id;
	u8 main_func_queue;
	u8 mf_si_bcast_accept_all;
	u8 mf_si_mcast_accept_all;
	struct core_rx_action_on_error action_on_error;
	u8 gsi_offload_flag;
	u8 vport_id_valid;
	u8 vport_id;
	u8 zero_prod_flg;
	u8 wipe_inner_vlan_pri_en;
	u8 reserved[2];
};

/* Ramrod data for rx queue stop ramrod */
struct core_rx_stop_ramrod_data {
	u8 complete_cqe_flg;
	u8 complete_event_flg;
	u8 queue_id;
	u8 reserved1;
	__le16 reserved2[2];
};

/* Flags for Core TX BD */
struct core_tx_bd_data {
	__le16 as_bitfield;
#define CORE_TX_BD_DATA_FORCE_VLAN_MODE_MASK		0x1
#define CORE_TX_BD_DATA_FORCE_VLAN_MODE_SHIFT		0
#define CORE_TX_BD_DATA_VLAN_INSERTION_MASK		0x1
#define CORE_TX_BD_DATA_VLAN_INSERTION_SHIFT		1
#define CORE_TX_BD_DATA_START_BD_MASK			0x1
#define CORE_TX_BD_DATA_START_BD_SHIFT			2
#define CORE_TX_BD_DATA_IP_CSUM_MASK			0x1
#define CORE_TX_BD_DATA_IP_CSUM_SHIFT			3
#define CORE_TX_BD_DATA_L4_CSUM_MASK			0x1
#define CORE_TX_BD_DATA_L4_CSUM_SHIFT			4
#define CORE_TX_BD_DATA_IPV6_EXT_MASK			0x1
#define CORE_TX_BD_DATA_IPV6_EXT_SHIFT			5
#define CORE_TX_BD_DATA_L4_PROTOCOL_MASK		0x1
#define CORE_TX_BD_DATA_L4_PROTOCOL_SHIFT		6
#define CORE_TX_BD_DATA_L4_PSEUDO_CSUM_MODE_MASK	0x1
#define CORE_TX_BD_DATA_L4_PSEUDO_CSUM_MODE_SHIFT	7
#define CORE_TX_BD_DATA_NBDS_MASK			0xF
#define CORE_TX_BD_DATA_NBDS_SHIFT			8
#define CORE_TX_BD_DATA_ROCE_FLAV_MASK			0x1
#define CORE_TX_BD_DATA_ROCE_FLAV_SHIFT			12
#define CORE_TX_BD_DATA_IP_LEN_MASK			0x1
#define CORE_TX_BD_DATA_IP_LEN_SHIFT			13
#define CORE_TX_BD_DATA_DISABLE_STAG_INSERTION_MASK	0x1
#define CORE_TX_BD_DATA_DISABLE_STAG_INSERTION_SHIFT	14
#define CORE_TX_BD_DATA_RESERVED0_MASK			0x1
#define CORE_TX_BD_DATA_RESERVED0_SHIFT			15
};

/* Core TX BD for Light L2 */
struct core_tx_bd {
	struct regpair addr;
	__le16 nbytes;
	__le16 nw_vlan_or_lb_echo;
	struct core_tx_bd_data bd_data;
	__le16 bitfield1;
#define CORE_TX_BD_L4_HDR_OFFSET_W_MASK		0x3FFF
#define CORE_TX_BD_L4_HDR_OFFSET_W_SHIFT	0
#define CORE_TX_BD_TX_DST_MASK			0x3
#define CORE_TX_BD_TX_DST_SHIFT			14
};

/* Light L2 TX Destination */
enum core_tx_dest {
	CORE_TX_DEST_NW,
	CORE_TX_DEST_LB,
	CORE_TX_DEST_RESERVED,
	CORE_TX_DEST_DROP,
	MAX_CORE_TX_DEST
};

/* Ramrod data for tx queue start ramrod */
struct core_tx_start_ramrod_data {
	struct regpair pbl_base_addr;
	__le16 mtu;
	__le16 sb_id;
	u8 sb_index;
	u8 stats_en;
	u8 stats_id;
	u8 conn_type;
	__le16 pbl_size;
	__le16 qm_pq_id;
	u8 gsi_offload_flag;
	u8 ctx_stats_en;
	u8 vport_id_valid;
	u8 vport_id;
	u8 enforce_security_flag;
	u8 reserved[7];
};

/* Ramrod data for tx queue stop ramrod */
struct core_tx_stop_ramrod_data {
	__le32 reserved0[2];
};

/* Ramrod data for tx queue update ramrod */
struct core_tx_update_ramrod_data {
	u8 update_qm_pq_id_flg;
	u8 reserved0;
	__le16 qm_pq_id;
	__le32 reserved1;
};

/* Enum flag for what type of dcb data to update */
enum dcb_dscp_update_mode {
	DONT_UPDATE_DCB_DSCP,
	UPDATE_DCB,
	UPDATE_DSCP,
	UPDATE_DCB_DSCP,
	MAX_DCB_DSCP_UPDATE_MODE
};

/* The core storm context for the Ystorm */
struct ystorm_core_conn_st_ctx {
	__le32 reserved[4];
};

/* The core storm context for the Pstorm */
struct pstorm_core_conn_st_ctx {
	__le32 reserved[20];
};

/* Core Slowpath Connection storm context of Xstorm */
struct xstorm_core_conn_st_ctx {
	__le32 spq_base_lo;
	__le32 spq_base_hi;
	struct regpair consolid_base_addr;
	__le16 spq_cons;
	__le16 consolid_cons;
	__le32 reserved0[55];
};

struct e4_xstorm_core_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED1_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED1_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED2_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED3_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED4_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED4_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED5_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED5_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED6_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED6_SHIFT	7
	u8 flags1;
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED7_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED7_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED8_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED8_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED9_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED9_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT12_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT12_SHIFT		4
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT13_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT13_SHIFT		5
#define E4_XSTORM_CORE_CONN_AG_CTX_TX_RULE_ACTIVE_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_ACTIVE_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT	7
	u8 flags2;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF3_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF7_SHIFT	6
	u8 flags4;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF12_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF12_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF13_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF13_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF14_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF14_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF15_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF15_SHIFT	6
	u8 flags6;
#define E4_XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF17_MASK			0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF17_SHIFT			2
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_MASK			0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_SHIFT			4
#define E4_XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_MASK		0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_SHIFT		6
	u8 flags7;
#define E4_XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED10_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED10_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT		6
#define E4_XSTORM_CORE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT		7
	u8 flags8;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF7EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF7EN_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_CF8EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF8EN_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_CF9EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF9EN_SHIFT	7
	u8 flags9;
#define E4_XSTORM_CORE_CONN_AG_CTX_CF10EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF10EN_SHIFT			0
#define E4_XSTORM_CORE_CONN_AG_CTX_CF11EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF11EN_SHIFT			1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF12EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF12EN_SHIFT			2
#define E4_XSTORM_CORE_CONN_AG_CTX_CF13EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF13EN_SHIFT			3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF14EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF14EN_SHIFT			4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF15EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF15EN_SHIFT			5
#define E4_XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CONSOLID_PROD_CF_EN_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_CF17EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF17EN_SHIFT			7
	u8 flags10;
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_DQ_CF_EN_SHIFT		0
#define E4_XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT		2
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED11_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED11_SHIFT		3
#define E4_XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT		4
#define E4_XSTORM_CORE_CONN_AG_CTX_CF23EN_MASK			0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_CF23EN_SHIFT			5
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED12_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED12_SHIFT		6
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED13_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED13_SHIFT		7
	u8 flags11;
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED14_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED14_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED15_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RESERVED15_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_TX_DEC_RULE_EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE9EN_MASK		0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE9EN_SHIFT	7
	u8 flags12;
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE10EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE10EN_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE11EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE11EN_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE14EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE14EN_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE15EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE15EN_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE16EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE16EN_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE17EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE17EN_SHIFT	7
	u8 flags13;
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE18EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE18EN_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE19EN_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_RULE19EN_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED4_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED4_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED5_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED5_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED6_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED6_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED7_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED7_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED8_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED8_SHIFT	6
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED9_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT16_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT16_SHIFT	0
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT17_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT17_SHIFT	1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT18_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT18_SHIFT	2
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT19_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT19_SHIFT	3
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT20_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT20_SHIFT	4
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT21_MASK	0x1
#define E4_XSTORM_CORE_CONN_AG_CTX_BIT21_SHIFT	5
#define E4_XSTORM_CORE_CONN_AG_CTX_CF23_MASK	0x3
#define E4_XSTORM_CORE_CONN_AG_CTX_CF23_SHIFT	6
	u8 byte2;
	__le16 physical_q0;
	__le16 consolid_prod;
	__le16 reserved16;
	__le16 tx_bd_cons;
	__le16 tx_bd_or_spq_prod;
	__le16 updated_qm_pq_id;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le16 word7;
	__le16 word8;
	__le16 word9;
	__le16 word10;
	__le32 reg7;
	__le32 reg8;
	__le32 reg9;
	u8 byte7;
	u8 byte8;
	u8 byte9;
	u8 byte10;
	u8 byte11;
	u8 byte12;
	u8 byte13;
	u8 byte14;
	u8 byte15;
	u8 e5_reserved;
	__le16 word11;
	__le32 reg10;
	__le32 reg11;
	__le32 reg12;
	__le32 reg13;
	__le32 reg14;
	__le32 reg15;
	__le32 reg16;
	__le32 reg17;
	__le32 reg18;
	__le32 reg19;
	__le16 word12;
	__le16 word13;
	__le16 word14;
	__le16 word15;
};

struct e4_tstorm_core_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT2_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT2_SHIFT	2
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT3_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT3_SHIFT	3
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT4_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT4_SHIFT	4
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT5_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_BIT5_SHIFT	5
#define E4_TSTORM_CORE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF0_SHIFT	6
	u8 flags1;
#define E4_TSTORM_CORE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF1_SHIFT	0
#define E4_TSTORM_CORE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF2_SHIFT	2
#define E4_TSTORM_CORE_CONN_AG_CTX_CF3_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF3_SHIFT	4
#define E4_TSTORM_CORE_CONN_AG_CTX_CF4_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF4_SHIFT	6
	u8 flags2;
#define E4_TSTORM_CORE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF5_SHIFT	0
#define E4_TSTORM_CORE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF6_SHIFT	2
#define E4_TSTORM_CORE_CONN_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF7_SHIFT	4
#define E4_TSTORM_CORE_CONN_AG_CTX_CF8_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF8_SHIFT	6
	u8 flags3;
#define E4_TSTORM_CORE_CONN_AG_CTX_CF9_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF9_SHIFT	0
#define E4_TSTORM_CORE_CONN_AG_CTX_CF10_MASK	0x3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF10_SHIFT	2
#define E4_TSTORM_CORE_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT	4
#define E4_TSTORM_CORE_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT	5
#define E4_TSTORM_CORE_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT	6
#define E4_TSTORM_CORE_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_CORE_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT		0
#define E4_TSTORM_CORE_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT		1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT		2
#define E4_TSTORM_CORE_CONN_AG_CTX_CF7EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF7EN_SHIFT		3
#define E4_TSTORM_CORE_CONN_AG_CTX_CF8EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF8EN_SHIFT		4
#define E4_TSTORM_CORE_CONN_AG_CTX_CF9EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF9EN_SHIFT		5
#define E4_TSTORM_CORE_CONN_AG_CTX_CF10EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_CF10EN_SHIFT		6
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags5;
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_CORE_CONN_AG_CTX_RULE8EN_SHIFT	7
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 byte2;
	u8 byte3;
	__le16 word0;
	u8 byte4;
	u8 byte5;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le32 ll2_rx_prod;
	__le32 reg10;
};

struct e4_ustorm_core_conn_ag_ctx {
	u8 reserved;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_CORE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_USTORM_CORE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_USTORM_CORE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_USTORM_CORE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_USTORM_CORE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_USTORM_CORE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_USTORM_CORE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_USTORM_CORE_CONN_AG_CTX_CF3_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF3_SHIFT	0
#define E4_USTORM_CORE_CONN_AG_CTX_CF4_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF4_SHIFT	2
#define E4_USTORM_CORE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF5_SHIFT	4
#define E4_USTORM_CORE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_USTORM_CORE_CONN_AG_CTX_CF6_SHIFT	6
	u8 flags2;
#define E4_USTORM_CORE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_USTORM_CORE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_USTORM_CORE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_USTORM_CORE_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF3EN_SHIFT		3
#define E4_USTORM_CORE_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF4EN_SHIFT		4
#define E4_USTORM_CORE_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF5EN_SHIFT		5
#define E4_USTORM_CORE_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_CF6EN_SHIFT		6
#define E4_USTORM_CORE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags3;
#define E4_USTORM_CORE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_CORE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_CORE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_CORE_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_CORE_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_CORE_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_CORE_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_USTORM_CORE_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 rx_producers;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

/* The core storm context for the Mstorm */
struct mstorm_core_conn_st_ctx {
	__le32 reserved[40];
};

/* The core storm context for the Ustorm */
struct ustorm_core_conn_st_ctx {
	__le32 reserved[20];
};

/* The core storm context for the Tstorm */
struct tstorm_core_conn_st_ctx {
	__le32 reserved[4];
};

/* core connection context */
struct e4_core_conn_context {
	struct ystorm_core_conn_st_ctx ystorm_st_context;
	struct regpair ystorm_st_padding[2];
	struct pstorm_core_conn_st_ctx pstorm_st_context;
	struct regpair pstorm_st_padding[2];
	struct xstorm_core_conn_st_ctx xstorm_st_context;
	struct e4_xstorm_core_conn_ag_ctx xstorm_ag_context;
	struct e4_tstorm_core_conn_ag_ctx tstorm_ag_context;
	struct e4_ustorm_core_conn_ag_ctx ustorm_ag_context;
	struct mstorm_core_conn_st_ctx mstorm_st_context;
	struct ustorm_core_conn_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
	struct tstorm_core_conn_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
};

struct eth_mstorm_per_pf_stat {
	struct regpair gre_discard_pkts;
	struct regpair vxlan_discard_pkts;
	struct regpair geneve_discard_pkts;
	struct regpair lb_discard_pkts;
};

struct eth_mstorm_per_queue_stat {
	struct regpair ttl0_discard;
	struct regpair packet_too_big_discard;
	struct regpair no_buff_discard;
	struct regpair not_active_discard;
	struct regpair tpa_coalesced_pkts;
	struct regpair tpa_coalesced_events;
	struct regpair tpa_aborts_num;
	struct regpair tpa_coalesced_bytes;
};

/* Ethernet TX Per PF */
struct eth_pstorm_per_pf_stat {
	struct regpair sent_lb_ucast_bytes;
	struct regpair sent_lb_mcast_bytes;
	struct regpair sent_lb_bcast_bytes;
	struct regpair sent_lb_ucast_pkts;
	struct regpair sent_lb_mcast_pkts;
	struct regpair sent_lb_bcast_pkts;
	struct regpair sent_gre_bytes;
	struct regpair sent_vxlan_bytes;
	struct regpair sent_geneve_bytes;
	struct regpair sent_mpls_bytes;
	struct regpair sent_gre_mpls_bytes;
	struct regpair sent_udp_mpls_bytes;
	struct regpair sent_gre_pkts;
	struct regpair sent_vxlan_pkts;
	struct regpair sent_geneve_pkts;
	struct regpair sent_mpls_pkts;
	struct regpair sent_gre_mpls_pkts;
	struct regpair sent_udp_mpls_pkts;
	struct regpair gre_drop_pkts;
	struct regpair vxlan_drop_pkts;
	struct regpair geneve_drop_pkts;
	struct regpair mpls_drop_pkts;
	struct regpair gre_mpls_drop_pkts;
	struct regpair udp_mpls_drop_pkts;
};

/* Ethernet TX Per Queue Stats */
struct eth_pstorm_per_queue_stat {
	struct regpair sent_ucast_bytes;
	struct regpair sent_mcast_bytes;
	struct regpair sent_bcast_bytes;
	struct regpair sent_ucast_pkts;
	struct regpair sent_mcast_pkts;
	struct regpair sent_bcast_pkts;
	struct regpair error_drop_pkts;
};

/* ETH Rx producers data */
struct eth_rx_rate_limit {
	__le16 mult;
	__le16 cnst;
	u8 add_sub_cnst;
	u8 reserved0;
	__le16 reserved1;
};

/* Update RSS indirection table entry command */
struct eth_tstorm_rss_update_data {
	u8 valid;
	u8 vport_id;
	u8 ind_table_index;
	u8 reserved;
	__le16 ind_table_value;
	__le16 reserved1;
};

struct eth_ustorm_per_pf_stat {
	struct regpair rcv_lb_ucast_bytes;
	struct regpair rcv_lb_mcast_bytes;
	struct regpair rcv_lb_bcast_bytes;
	struct regpair rcv_lb_ucast_pkts;
	struct regpair rcv_lb_mcast_pkts;
	struct regpair rcv_lb_bcast_pkts;
	struct regpair rcv_gre_bytes;
	struct regpair rcv_vxlan_bytes;
	struct regpair rcv_geneve_bytes;
	struct regpair rcv_gre_pkts;
	struct regpair rcv_vxlan_pkts;
	struct regpair rcv_geneve_pkts;
};

struct eth_ustorm_per_queue_stat {
	struct regpair rcv_ucast_bytes;
	struct regpair rcv_mcast_bytes;
	struct regpair rcv_bcast_bytes;
	struct regpair rcv_ucast_pkts;
	struct regpair rcv_mcast_pkts;
	struct regpair rcv_bcast_pkts;
};

/* Event Ring VF-PF Channel data */
struct vf_pf_channel_eqe_data {
	struct regpair msg_addr;
};

/* Event Ring malicious VF data */
struct malicious_vf_eqe_data {
	u8 vf_id;
	u8 err_id;
	__le16 reserved[3];
};

/* Event Ring initial cleanup data */
struct initial_cleanup_eqe_data {
	u8 vf_id;
	u8 reserved[7];
};

/* Event Data Union */
union event_ring_data {
	u8 bytes[8];
	struct vf_pf_channel_eqe_data vf_pf_channel;
	struct iscsi_eqe_data iscsi_info;
	struct iscsi_connect_done_results iscsi_conn_done_info;
	union rdma_eqe_data rdma_data;
	struct malicious_vf_eqe_data malicious_vf;
	struct initial_cleanup_eqe_data vf_init_cleanup;
};

/* Event Ring Entry */
struct event_ring_entry {
	u8 protocol_id;
	u8 opcode;
	u8 reserved0;
	u8 vf_id;
	__le16 echo;
	u8 fw_return_code;
	u8 flags;
#define EVENT_RING_ENTRY_ASYNC_MASK		0x1
#define EVENT_RING_ENTRY_ASYNC_SHIFT		0
#define EVENT_RING_ENTRY_RESERVED1_MASK		0x7F
#define EVENT_RING_ENTRY_RESERVED1_SHIFT	1
	union event_ring_data data;
};

/* Event Ring Next Page Address */
struct event_ring_next_addr {
	struct regpair addr;
	__le32 reserved[2];
};

/* Event Ring Element */
union event_ring_element {
	struct event_ring_entry entry;
	struct event_ring_next_addr next_addr;
};

/* Ports mode */
enum fw_flow_ctrl_mode {
	flow_ctrl_pause,
	flow_ctrl_pfc,
	MAX_FW_FLOW_CTRL_MODE
};

/* GFT profile type */
enum gft_profile_type {
	GFT_PROFILE_TYPE_4_TUPLE,
	GFT_PROFILE_TYPE_L4_DST_PORT,
	GFT_PROFILE_TYPE_IP_DST_ADDR,
	GFT_PROFILE_TYPE_IP_SRC_ADDR,
	GFT_PROFILE_TYPE_TUNNEL_TYPE,
	MAX_GFT_PROFILE_TYPE
};

/* Major and Minor hsi Versions */
struct hsi_fp_ver_struct {
	u8 minor_ver_arr[2];
	u8 major_ver_arr[2];
};

enum iwarp_ll2_tx_queues {
	IWARP_LL2_IN_ORDER_TX_QUEUE = 1,
	IWARP_LL2_ALIGNED_TX_QUEUE,
	IWARP_LL2_ALIGNED_RIGHT_TRIMMED_TX_QUEUE,
	IWARP_LL2_ERROR,
	MAX_IWARP_LL2_TX_QUEUES
};

/* Malicious VF error ID */
enum malicious_vf_error_id {
	MALICIOUS_VF_NO_ERROR,
	VF_PF_CHANNEL_NOT_READY,
	VF_ZONE_MSG_NOT_VALID,
	VF_ZONE_FUNC_NOT_ENABLED,
	ETH_PACKET_TOO_SMALL,
	ETH_ILLEGAL_VLAN_MODE,
	ETH_MTU_VIOLATION,
	ETH_ILLEGAL_INBAND_TAGS,
	ETH_VLAN_INSERT_AND_INBAND_VLAN,
	ETH_ILLEGAL_NBDS,
	ETH_FIRST_BD_WO_SOP,
	ETH_INSUFFICIENT_BDS,
	ETH_ILLEGAL_LSO_HDR_NBDS,
	ETH_ILLEGAL_LSO_MSS,
	ETH_ZERO_SIZE_BD,
	ETH_ILLEGAL_LSO_HDR_LEN,
	ETH_INSUFFICIENT_PAYLOAD,
	ETH_EDPM_OUT_OF_SYNC,
	ETH_TUNN_IPV6_EXT_NBD_ERR,
	ETH_CONTROL_PACKET_VIOLATION,
	ETH_ANTI_SPOOFING_ERR,
	ETH_PACKET_SIZE_TOO_LARGE,
	CORE_ILLEGAL_VLAN_MODE,
	CORE_ILLEGAL_NBDS,
	CORE_FIRST_BD_WO_SOP,
	CORE_INSUFFICIENT_BDS,
	CORE_PACKET_TOO_SMALL,
	CORE_ILLEGAL_INBAND_TAGS,
	CORE_VLAN_INSERT_AND_INBAND_VLAN,
	CORE_MTU_VIOLATION,
	CORE_CONTROL_PACKET_VIOLATION,
	CORE_ANTI_SPOOFING_ERR,
	CORE_PACKET_SIZE_TOO_LARGE,
	CORE_ILLEGAL_BD_FLAGS,
	CORE_GSI_PACKET_VIOLATION,
	MAX_MALICIOUS_VF_ERROR_ID,
};

/* Mstorm non-triggering VF zone */
struct mstorm_non_trigger_vf_zone {
	struct eth_mstorm_per_queue_stat eth_queue_stat;
	struct eth_rx_prod_data eth_rx_queue_producers[ETH_MAX_NUM_RX_QUEUES_PER_VF_QUAD];
};

/* Mstorm VF zone */
struct mstorm_vf_zone {
	struct mstorm_non_trigger_vf_zone non_trigger;
};

/* vlan header including TPID and TCI fields */
struct vlan_header {
	__le16 tpid;
	__le16 tci;
};

/* outer tag configurations */
struct outer_tag_config_struct {
	u8 enable_stag_pri_change;
	u8 pri_map_valid;
	u8 reserved[2];
	struct vlan_header outer_tag;
	u8 inner_to_outer_pri_map[8];
};

/* personality per PF */
enum personality_type {
	BAD_PERSONALITY_TYP,
	PERSONALITY_TCP_ULP,
	PERSONALITY_FCOE,
	PERSONALITY_RDMA_AND_ETH,
	PERSONALITY_RDMA,
	PERSONALITY_CORE,
	PERSONALITY_ETH,
	PERSONALITY_RESERVED,
	MAX_PERSONALITY_TYPE
};

/* tunnel configuration */
struct pf_start_tunnel_config {
	u8 set_vxlan_udp_port_flg;
	u8 set_geneve_udp_port_flg;
	u8 set_no_inner_l2_vxlan_udp_port_flg;
	u8 tunnel_clss_vxlan;
	u8 tunnel_clss_l2geneve;
	u8 tunnel_clss_ipgeneve;
	u8 tunnel_clss_l2gre;
	u8 tunnel_clss_ipgre;
	__le16 vxlan_udp_port;
	__le16 geneve_udp_port;
	__le16 no_inner_l2_vxlan_udp_port;
	__le16 reserved[3];
};

/* Ramrod data for PF start ramrod */
struct pf_start_ramrod_data {
	struct regpair event_ring_pbl_addr;
	struct regpair consolid_q_pbl_addr;
	struct pf_start_tunnel_config tunnel_config;
	__le16 event_ring_sb_id;
	u8 base_vf_id;
	u8 num_vfs;
	u8 event_ring_num_pages;
	u8 event_ring_sb_index;
	u8 path_id;
	u8 warning_as_error;
	u8 dont_log_ramrods;
	u8 personality;
	__le16 log_type_mask;
	u8 mf_mode;
	u8 integ_phase;
	u8 allow_npar_tx_switching;
	u8 reserved0;
	struct hsi_fp_ver_struct hsi_fp_ver;
	struct outer_tag_config_struct outer_tag_config;
};

/* Data for port update ramrod */
struct protocol_dcb_data {
	u8 dcb_enable_flag;
	u8 dscp_enable_flag;
	u8 dcb_priority;
	u8 dcb_tc;
	u8 dscp_val;
	u8 dcb_dont_add_vlan0;
};

/* Update tunnel configuration */
struct pf_update_tunnel_config {
	u8 update_rx_pf_clss;
	u8 update_rx_def_ucast_clss;
	u8 update_rx_def_non_ucast_clss;
	u8 set_vxlan_udp_port_flg;
	u8 set_geneve_udp_port_flg;
	u8 set_no_inner_l2_vxlan_udp_port_flg;
	u8 tunnel_clss_vxlan;
	u8 tunnel_clss_l2geneve;
	u8 tunnel_clss_ipgeneve;
	u8 tunnel_clss_l2gre;
	u8 tunnel_clss_ipgre;
	u8 reserved;
	__le16 vxlan_udp_port;
	__le16 geneve_udp_port;
	__le16 no_inner_l2_vxlan_udp_port;
	__le16 reserved1[3];
};

/* Data for port update ramrod */
struct pf_update_ramrod_data {
	u8 update_eth_dcb_data_mode;
	u8 update_fcoe_dcb_data_mode;
	u8 update_iscsi_dcb_data_mode;
	u8 update_roce_dcb_data_mode;
	u8 update_rroce_dcb_data_mode;
	u8 update_iwarp_dcb_data_mode;
	u8 update_mf_vlan_flag;
	u8 update_enable_stag_pri_change;
	struct protocol_dcb_data eth_dcb_data;
	struct protocol_dcb_data fcoe_dcb_data;
	struct protocol_dcb_data iscsi_dcb_data;
	struct protocol_dcb_data roce_dcb_data;
	struct protocol_dcb_data rroce_dcb_data;
	struct protocol_dcb_data iwarp_dcb_data;
	__le16 mf_vlan;
	u8 enable_stag_pri_change;
	u8 reserved;
	struct pf_update_tunnel_config tunnel_config;
};

/* Ports mode */
enum ports_mode {
	ENGX2_PORTX1,
	ENGX2_PORTX2,
	ENGX1_PORTX1,
	ENGX1_PORTX2,
	ENGX1_PORTX4,
	MAX_PORTS_MODE
};

/* use to index in hsi_fp_[major|minor]_ver_arr per protocol */
enum protocol_version_array_key {
	ETH_VER_KEY = 0,
	ROCE_VER_KEY,
	MAX_PROTOCOL_VERSION_ARRAY_KEY
};

/* RDMA TX Stats */
struct rdma_sent_stats {
	struct regpair sent_bytes;
	struct regpair sent_pkts;
};

/* Pstorm non-triggering VF zone */
struct pstorm_non_trigger_vf_zone {
	struct eth_pstorm_per_queue_stat eth_queue_stat;
	struct rdma_sent_stats rdma_stats;
};

/* Pstorm VF zone */
struct pstorm_vf_zone {
	struct pstorm_non_trigger_vf_zone non_trigger;
	struct regpair reserved[7];
};

/* Ramrod Header of SPQE */
struct ramrod_header {
	__le32 cid;
	u8 cmd_id;
	u8 protocol_id;
	__le16 echo;
};

/* RDMA RX Stats */
struct rdma_rcv_stats {
	struct regpair rcv_bytes;
	struct regpair rcv_pkts;
};

/* Data for update QCN/DCQCN RL ramrod */
struct rl_update_ramrod_data {
	u8 qcn_update_param_flg;
	u8 dcqcn_update_param_flg;
	u8 rl_init_flg;
	u8 rl_start_flg;
	u8 rl_stop_flg;
	u8 rl_id_first;
	u8 rl_id_last;
	u8 rl_dc_qcn_flg;
	u8 dcqcn_reset_alpha_on_idle;
	u8 rl_bc_stage_th;
	u8 rl_timer_stage_th;
	u8 reserved1;
	__le32 rl_bc_rate;
	__le16 rl_max_rate;
	__le16 rl_r_ai;
	__le16 rl_r_hai;
	__le16 dcqcn_g;
	__le32 dcqcn_k_us;
	__le32 dcqcn_timeuot_us;
	__le32 qcn_timeuot_us;
	__le32 reserved2;
};

/* Slowpath Element (SPQE) */
struct slow_path_element {
	struct ramrod_header hdr;
	struct regpair data_ptr;
};

/* Tstorm non-triggering VF zone */
struct tstorm_non_trigger_vf_zone {
	struct rdma_rcv_stats rdma_stats;
};

struct tstorm_per_port_stat {
	struct regpair trunc_error_discard;
	struct regpair mac_error_discard;
	struct regpair mftag_filter_discard;
	struct regpair eth_mac_filter_discard;
	struct regpair ll2_mac_filter_discard;
	struct regpair ll2_conn_disabled_discard;
	struct regpair iscsi_irregular_pkt;
	struct regpair fcoe_irregular_pkt;
	struct regpair roce_irregular_pkt;
	struct regpair iwarp_irregular_pkt;
	struct regpair eth_irregular_pkt;
	struct regpair toe_irregular_pkt;
	struct regpair preroce_irregular_pkt;
	struct regpair eth_gre_tunn_filter_discard;
	struct regpair eth_vxlan_tunn_filter_discard;
	struct regpair eth_geneve_tunn_filter_discard;
	struct regpair eth_gft_drop_pkt;
};

/* Tstorm VF zone */
struct tstorm_vf_zone {
	struct tstorm_non_trigger_vf_zone non_trigger;
};

/* Tunnel classification scheme */
enum tunnel_clss {
	TUNNEL_CLSS_MAC_VLAN = 0,
	TUNNEL_CLSS_MAC_VNI,
	TUNNEL_CLSS_INNER_MAC_VLAN,
	TUNNEL_CLSS_INNER_MAC_VNI,
	TUNNEL_CLSS_MAC_VLAN_DUAL_STAGE,
	MAX_TUNNEL_CLSS
};

/* Ustorm non-triggering VF zone */
struct ustorm_non_trigger_vf_zone {
	struct eth_ustorm_per_queue_stat eth_queue_stat;
	struct regpair vf_pf_msg_addr;
};

/* Ustorm triggering VF zone */
struct ustorm_trigger_vf_zone {
	u8 vf_pf_msg_valid;
	u8 reserved[7];
};

/* Ustorm VF zone */
struct ustorm_vf_zone {
	struct ustorm_non_trigger_vf_zone non_trigger;
	struct ustorm_trigger_vf_zone trigger;
};

/* VF-PF channel data */
struct vf_pf_channel_data {
	__le32 ready;
	u8 valid;
	u8 reserved0;
	__le16 reserved1;
};

/* Ramrod data for VF start ramrod */
struct vf_start_ramrod_data {
	u8 vf_id;
	u8 enable_flr_ack;
	__le16 opaque_fid;
	u8 personality;
	u8 reserved[7];
	struct hsi_fp_ver_struct hsi_fp_ver;

};

/* Ramrod data for VF start ramrod */
struct vf_stop_ramrod_data {
	u8 vf_id;
	u8 reserved0;
	__le16 reserved1;
	__le32 reserved2;
};

/* VF zone size mode */
enum vf_zone_size_mode {
	VF_ZONE_SIZE_MODE_DEFAULT,
	VF_ZONE_SIZE_MODE_DOUBLE,
	VF_ZONE_SIZE_MODE_QUAD,
	MAX_VF_ZONE_SIZE_MODE
};

/* Xstorm non-triggering VF zone */
struct xstorm_non_trigger_vf_zone {
	struct regpair non_edpm_ack_pkts;
};

/* Tstorm VF zone */
struct xstorm_vf_zone {
	struct xstorm_non_trigger_vf_zone non_trigger;
};

/* Attentions status block */
struct atten_status_block {
	__le32 atten_bits;
	__le32 atten_ack;
	__le16 reserved0;
	__le16 sb_index;
	__le32 reserved1;
};

/* DMAE command */
struct dmae_cmd {
	__le32 opcode;
#define DMAE_CMD_SRC_MASK		0x1
#define DMAE_CMD_SRC_SHIFT		0
#define DMAE_CMD_DST_MASK		0x3
#define DMAE_CMD_DST_SHIFT		1
#define DMAE_CMD_C_DST_MASK		0x1
#define DMAE_CMD_C_DST_SHIFT		3
#define DMAE_CMD_CRC_RESET_MASK		0x1
#define DMAE_CMD_CRC_RESET_SHIFT	4
#define DMAE_CMD_SRC_ADDR_RESET_MASK	0x1
#define DMAE_CMD_SRC_ADDR_RESET_SHIFT	5
#define DMAE_CMD_DST_ADDR_RESET_MASK	0x1
#define DMAE_CMD_DST_ADDR_RESET_SHIFT	6
#define DMAE_CMD_COMP_FUNC_MASK		0x1
#define DMAE_CMD_COMP_FUNC_SHIFT	7
#define DMAE_CMD_COMP_WORD_EN_MASK	0x1
#define DMAE_CMD_COMP_WORD_EN_SHIFT	8
#define DMAE_CMD_COMP_CRC_EN_MASK	0x1
#define DMAE_CMD_COMP_CRC_EN_SHIFT	9
#define DMAE_CMD_COMP_CRC_OFFSET_MASK	0x7
#define DMAE_CMD_COMP_CRC_OFFSET_SHIFT 10
#define DMAE_CMD_RESERVED1_MASK		0x1
#define DMAE_CMD_RESERVED1_SHIFT	13
#define DMAE_CMD_ENDIANITY_MODE_MASK	0x3
#define DMAE_CMD_ENDIANITY_MODE_SHIFT	14
#define DMAE_CMD_ERR_HANDLING_MASK	0x3
#define DMAE_CMD_ERR_HANDLING_SHIFT	16
#define DMAE_CMD_PORT_ID_MASK		0x3
#define DMAE_CMD_PORT_ID_SHIFT		18
#define DMAE_CMD_SRC_PF_ID_MASK		0xF
#define DMAE_CMD_SRC_PF_ID_SHIFT	20
#define DMAE_CMD_DST_PF_ID_MASK		0xF
#define DMAE_CMD_DST_PF_ID_SHIFT	24
#define DMAE_CMD_SRC_VF_ID_VALID_MASK	0x1
#define DMAE_CMD_SRC_VF_ID_VALID_SHIFT 28
#define DMAE_CMD_DST_VF_ID_VALID_MASK	0x1
#define DMAE_CMD_DST_VF_ID_VALID_SHIFT 29
#define DMAE_CMD_RESERVED2_MASK		0x3
#define DMAE_CMD_RESERVED2_SHIFT	30
	__le32 src_addr_lo;
	__le32 src_addr_hi;
	__le32 dst_addr_lo;
	__le32 dst_addr_hi;
	__le16 length_dw;
	__le16 opcode_b;
#define DMAE_CMD_SRC_VF_ID_MASK		0xFF
#define DMAE_CMD_SRC_VF_ID_SHIFT	0
#define DMAE_CMD_DST_VF_ID_MASK		0xFF
#define DMAE_CMD_DST_VF_ID_SHIFT	8
	__le32 comp_addr_lo;
	__le32 comp_addr_hi;
	__le32 comp_val;
	__le32 crc32;
	__le32 crc_32_c;
	__le16 crc16;
	__le16 crc16_c;
	__le16 crc10;
	__le16 error_bit_reserved;
#define DMAE_CMD_ERROR_BIT_MASK        0x1
#define DMAE_CMD_ERROR_BIT_SHIFT       0
#define DMAE_CMD_RESERVED_MASK	       0x7FFF
#define DMAE_CMD_RESERVED_SHIFT        1
	__le16 xsum16;
	__le16 xsum8;
};

enum dmae_cmd_comp_crc_en_enum {
	dmae_cmd_comp_crc_disabled,
	dmae_cmd_comp_crc_enabled,
	MAX_DMAE_CMD_COMP_CRC_EN_ENUM
};

enum dmae_cmd_comp_func_enum {
	dmae_cmd_comp_func_to_src,
	dmae_cmd_comp_func_to_dst,
	MAX_DMAE_CMD_COMP_FUNC_ENUM
};

enum dmae_cmd_comp_word_en_enum {
	dmae_cmd_comp_word_disabled,
	dmae_cmd_comp_word_enabled,
	MAX_DMAE_CMD_COMP_WORD_EN_ENUM
};

enum dmae_cmd_c_dst_enum {
	dmae_cmd_c_dst_pcie,
	dmae_cmd_c_dst_grc,
	MAX_DMAE_CMD_C_DST_ENUM
};

enum dmae_cmd_dst_enum {
	dmae_cmd_dst_none_0,
	dmae_cmd_dst_pcie,
	dmae_cmd_dst_grc,
	dmae_cmd_dst_none_3,
	MAX_DMAE_CMD_DST_ENUM
};

enum dmae_cmd_error_handling_enum {
	dmae_cmd_error_handling_send_regular_comp,
	dmae_cmd_error_handling_send_comp_with_err,
	dmae_cmd_error_handling_dont_send_comp,
	MAX_DMAE_CMD_ERROR_HANDLING_ENUM
};

enum dmae_cmd_src_enum {
	dmae_cmd_src_pcie,
	dmae_cmd_src_grc,
	MAX_DMAE_CMD_SRC_ENUM
};

struct e4_mstorm_core_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_CORE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_MSTORM_CORE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_MSTORM_CORE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_MSTORM_CORE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_MSTORM_CORE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_MSTORM_CORE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_MSTORM_CORE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_MSTORM_CORE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_MSTORM_CORE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_MSTORM_CORE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_MSTORM_CORE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_MSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

struct e4_ystorm_core_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_CORE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_CORE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_CORE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_YSTORM_CORE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_CORE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_YSTORM_CORE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_CORE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_YSTORM_CORE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_CORE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_YSTORM_CORE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_YSTORM_CORE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_YSTORM_CORE_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

/* DMAE parameters */
struct qed_dmae_params {
	u32 flags;
/* If QED_DMAE_PARAMS_RW_REPL_SRC flag is set and the
 * source is a block of length DMAE_MAX_RW_SIZE and the
 * destination is larger, the source block will be duplicated as
 * many times as required to fill the destination block. This is
 * used mostly to write a zeroed buffer to destination address
 * using DMA
 */
#define QED_DMAE_PARAMS_RW_REPL_SRC_MASK	0x1
#define QED_DMAE_PARAMS_RW_REPL_SRC_SHIFT	0
#define QED_DMAE_PARAMS_SRC_VF_VALID_MASK	0x1
#define QED_DMAE_PARAMS_SRC_VF_VALID_SHIFT	1
#define QED_DMAE_PARAMS_DST_VF_VALID_MASK	0x1
#define QED_DMAE_PARAMS_DST_VF_VALID_SHIFT	2
#define QED_DMAE_PARAMS_COMPLETION_DST_MASK	0x1
#define QED_DMAE_PARAMS_COMPLETION_DST_SHIFT	3
#define QED_DMAE_PARAMS_PORT_VALID_MASK		0x1
#define QED_DMAE_PARAMS_PORT_VALID_SHIFT	4
#define QED_DMAE_PARAMS_SRC_PF_VALID_MASK	0x1
#define QED_DMAE_PARAMS_SRC_PF_VALID_SHIFT	5
#define QED_DMAE_PARAMS_DST_PF_VALID_MASK	0x1
#define QED_DMAE_PARAMS_DST_PF_VALID_SHIFT	6
#define QED_DMAE_PARAMS_RESERVED_MASK		0x1FFFFFF
#define QED_DMAE_PARAMS_RESERVED_SHIFT		7
	u8 src_vfid;
	u8 dst_vfid;
	u8 port_id;
	u8 src_pfid;
	u8 dst_pfid;
	u8 reserved1;
	__le16 reserved2;
};

/* IGU cleanup command */
struct igu_cleanup {
	__le32 sb_id_and_flags;
#define IGU_CLEANUP_RESERVED0_MASK	0x7FFFFFF
#define IGU_CLEANUP_RESERVED0_SHIFT	0
#define IGU_CLEANUP_CLEANUP_SET_MASK	0x1
#define IGU_CLEANUP_CLEANUP_SET_SHIFT	27
#define IGU_CLEANUP_CLEANUP_TYPE_MASK	0x7
#define IGU_CLEANUP_CLEANUP_TYPE_SHIFT	28
#define IGU_CLEANUP_COMMAND_TYPE_MASK	0x1
#define IGU_CLEANUP_COMMAND_TYPE_SHIFT	31
	__le32 reserved1;
};

/* IGU firmware driver command */
union igu_command {
	struct igu_prod_cons_update prod_cons_update;
	struct igu_cleanup cleanup;
};

/* IGU firmware driver command */
struct igu_command_reg_ctrl {
	__le16 opaque_fid;
	__le16 igu_command_reg_ctrl_fields;
#define IGU_COMMAND_REG_CTRL_PXP_BAR_ADDR_MASK	0xFFF
#define IGU_COMMAND_REG_CTRL_PXP_BAR_ADDR_SHIFT	0
#define IGU_COMMAND_REG_CTRL_RESERVED_MASK	0x7
#define IGU_COMMAND_REG_CTRL_RESERVED_SHIFT	12
#define IGU_COMMAND_REG_CTRL_COMMAND_TYPE_MASK	0x1
#define IGU_COMMAND_REG_CTRL_COMMAND_TYPE_SHIFT	15
};

/* IGU mapping line structure */
struct igu_mapping_line {
	__le32 igu_mapping_line_fields;
#define IGU_MAPPING_LINE_VALID_MASK		0x1
#define IGU_MAPPING_LINE_VALID_SHIFT		0
#define IGU_MAPPING_LINE_VECTOR_NUMBER_MASK	0xFF
#define IGU_MAPPING_LINE_VECTOR_NUMBER_SHIFT	1
#define IGU_MAPPING_LINE_FUNCTION_NUMBER_MASK	0xFF
#define IGU_MAPPING_LINE_FUNCTION_NUMBER_SHIFT	9
#define IGU_MAPPING_LINE_PF_VALID_MASK		0x1
#define IGU_MAPPING_LINE_PF_VALID_SHIFT		17
#define IGU_MAPPING_LINE_IPS_GROUP_MASK		0x3F
#define IGU_MAPPING_LINE_IPS_GROUP_SHIFT	18
#define IGU_MAPPING_LINE_RESERVED_MASK		0xFF
#define IGU_MAPPING_LINE_RESERVED_SHIFT		24
};

/* IGU MSIX line structure */
struct igu_msix_vector {
	struct regpair address;
	__le32 data;
	__le32 msix_vector_fields;
#define IGU_MSIX_VECTOR_MASK_BIT_MASK		0x1
#define IGU_MSIX_VECTOR_MASK_BIT_SHIFT		0
#define IGU_MSIX_VECTOR_RESERVED0_MASK		0x7FFF
#define IGU_MSIX_VECTOR_RESERVED0_SHIFT		1
#define IGU_MSIX_VECTOR_STEERING_TAG_MASK	0xFF
#define IGU_MSIX_VECTOR_STEERING_TAG_SHIFT	16
#define IGU_MSIX_VECTOR_RESERVED1_MASK		0xFF
#define IGU_MSIX_VECTOR_RESERVED1_SHIFT		24
};
/* per encapsulation type enabling flags */
struct prs_reg_encapsulation_type_en {
	u8 flags;
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GRE_ENABLE_MASK		0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GRE_ENABLE_SHIFT		0
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GRE_ENABLE_MASK		0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GRE_ENABLE_SHIFT		1
#define PRS_REG_ENCAPSULATION_TYPE_EN_VXLAN_ENABLE_MASK			0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_VXLAN_ENABLE_SHIFT		2
#define PRS_REG_ENCAPSULATION_TYPE_EN_T_TAG_ENABLE_MASK			0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_T_TAG_ENABLE_SHIFT		3
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GENEVE_ENABLE_MASK	0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GENEVE_ENABLE_SHIFT	4
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GENEVE_ENABLE_MASK	0x1
#define PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GENEVE_ENABLE_SHIFT	5
#define PRS_REG_ENCAPSULATION_TYPE_EN_RESERVED_MASK			0x3
#define PRS_REG_ENCAPSULATION_TYPE_EN_RESERVED_SHIFT			6
};

enum pxp_tph_st_hint {
	TPH_ST_HINT_BIDIR,
	TPH_ST_HINT_REQUESTER,
	TPH_ST_HINT_TARGET,
	TPH_ST_HINT_TARGET_PRIO,
	MAX_PXP_TPH_ST_HINT
};

/* QM hardware structure of enable bypass credit mask */
struct qm_rf_bypass_mask {
	u8 flags;
#define QM_RF_BYPASS_MASK_LINEVOQ_MASK		0x1
#define QM_RF_BYPASS_MASK_LINEVOQ_SHIFT		0
#define QM_RF_BYPASS_MASK_RESERVED0_MASK	0x1
#define QM_RF_BYPASS_MASK_RESERVED0_SHIFT	1
#define QM_RF_BYPASS_MASK_PFWFQ_MASK		0x1
#define QM_RF_BYPASS_MASK_PFWFQ_SHIFT		2
#define QM_RF_BYPASS_MASK_VPWFQ_MASK		0x1
#define QM_RF_BYPASS_MASK_VPWFQ_SHIFT		3
#define QM_RF_BYPASS_MASK_PFRL_MASK		0x1
#define QM_RF_BYPASS_MASK_PFRL_SHIFT		4
#define QM_RF_BYPASS_MASK_VPQCNRL_MASK		0x1
#define QM_RF_BYPASS_MASK_VPQCNRL_SHIFT		5
#define QM_RF_BYPASS_MASK_FWPAUSE_MASK		0x1
#define QM_RF_BYPASS_MASK_FWPAUSE_SHIFT		6
#define QM_RF_BYPASS_MASK_RESERVED1_MASK	0x1
#define QM_RF_BYPASS_MASK_RESERVED1_SHIFT	7
};

/* QM hardware structure of opportunistic credit mask */
struct qm_rf_opportunistic_mask {
	__le16 flags;
#define QM_RF_OPPORTUNISTIC_MASK_LINEVOQ_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_LINEVOQ_SHIFT		0
#define QM_RF_OPPORTUNISTIC_MASK_BYTEVOQ_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_BYTEVOQ_SHIFT		1
#define QM_RF_OPPORTUNISTIC_MASK_PFWFQ_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_PFWFQ_SHIFT		2
#define QM_RF_OPPORTUNISTIC_MASK_VPWFQ_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_VPWFQ_SHIFT		3
#define QM_RF_OPPORTUNISTIC_MASK_PFRL_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_PFRL_SHIFT		4
#define QM_RF_OPPORTUNISTIC_MASK_VPQCNRL_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_VPQCNRL_SHIFT		5
#define QM_RF_OPPORTUNISTIC_MASK_FWPAUSE_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_FWPAUSE_SHIFT		6
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED0_MASK		0x1
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED0_SHIFT	7
#define QM_RF_OPPORTUNISTIC_MASK_QUEUEEMPTY_MASK	0x1
#define QM_RF_OPPORTUNISTIC_MASK_QUEUEEMPTY_SHIFT	8
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED1_MASK		0x7F
#define QM_RF_OPPORTUNISTIC_MASK_RESERVED1_SHIFT	9
};

/* QM hardware structure of QM map memory */
struct qm_rf_pq_map_e4 {
	__le32 reg;
#define QM_RF_PQ_MAP_E4_PQ_VALID_MASK		0x1
#define QM_RF_PQ_MAP_E4_PQ_VALID_SHIFT		0
#define QM_RF_PQ_MAP_E4_RL_ID_MASK		0xFF
#define QM_RF_PQ_MAP_E4_RL_ID_SHIFT		1
#define QM_RF_PQ_MAP_E4_VP_PQ_ID_MASK		0x1FF
#define QM_RF_PQ_MAP_E4_VP_PQ_ID_SHIFT		9
#define QM_RF_PQ_MAP_E4_VOQ_MASK		0x1F
#define QM_RF_PQ_MAP_E4_VOQ_SHIFT		18
#define QM_RF_PQ_MAP_E4_WRR_WEIGHT_GROUP_MASK	0x3
#define QM_RF_PQ_MAP_E4_WRR_WEIGHT_GROUP_SHIFT	23
#define QM_RF_PQ_MAP_E4_RL_VALID_MASK		0x1
#define QM_RF_PQ_MAP_E4_RL_VALID_SHIFT		25
#define QM_RF_PQ_MAP_E4_RESERVED_MASK		0x3F
#define QM_RF_PQ_MAP_E4_RESERVED_SHIFT		26
};

/* Completion params for aggregated interrupt completion */
struct sdm_agg_int_comp_params {
	__le16 params;
#define SDM_AGG_INT_COMP_PARAMS_AGG_INT_INDEX_MASK	0x3F
#define SDM_AGG_INT_COMP_PARAMS_AGG_INT_INDEX_SHIFT	0
#define SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_ENABLE_MASK	0x1
#define SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_ENABLE_SHIFT	6
#define SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_BIT_MASK	0x1FF
#define SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_BIT_SHIFT	7
};

/* SDM operation gen command (generate aggregative interrupt) */
struct sdm_op_gen {
	__le32 command;
#define SDM_OP_GEN_COMP_PARAM_MASK	0xFFFF
#define SDM_OP_GEN_COMP_PARAM_SHIFT	0
#define SDM_OP_GEN_COMP_TYPE_MASK	0xF
#define SDM_OP_GEN_COMP_TYPE_SHIFT	16
#define SDM_OP_GEN_RESERVED_MASK	0xFFF
#define SDM_OP_GEN_RESERVED_SHIFT	20
};

/* Physical memory descriptor */
struct phys_mem_desc {
	dma_addr_t phys_addr;
	void *virt_addr;
	u32 size;		/* In bytes */
};

/* Virtual memory descriptor */
struct virt_mem_desc {
	void *ptr;
	u32 size;		/* In bytes */
};

/****************************************/
/* Debug Tools HSI constants and macros */
/****************************************/

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
	BLOCK_DMAE,
	BLOCK_PTU,
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
	BLOCK_MSTAT,
	BLOCK_DBG,
	BLOCK_NIG,
	BLOCK_WOL,
	BLOCK_BMBN,
	BLOCK_IPC,
	BLOCK_NWM,
	BLOCK_NWS,
	BLOCK_MS,
	BLOCK_PHY_PCIE,
	BLOCK_LED,
	BLOCK_AVS_WRAP,
	BLOCK_PXPREQBUS,
	BLOCK_BAR0_MAP,
	BLOCK_MCP_FIO,
	BLOCK_LAST_INIT,
	BLOCK_PRS_FC,
	BLOCK_PBF_FC,
	BLOCK_NIG_LB_FC,
	BLOCK_NIG_LB_FC_PLLH,
	BLOCK_NIG_TX_FC_PLLH,
	BLOCK_NIG_TX_FC,
	BLOCK_NIG_RX_FC_PLLH,
	BLOCK_NIG_RX_FC,
	MAX_BLOCK_ID
};

/* binary debug buffer types */
enum bin_dbg_buffer_type {
	BIN_BUF_DBG_MODE_TREE,
	BIN_BUF_DBG_DUMP_REG,
	BIN_BUF_DBG_DUMP_MEM,
	BIN_BUF_DBG_IDLE_CHK_REGS,
	BIN_BUF_DBG_IDLE_CHK_IMMS,
	BIN_BUF_DBG_IDLE_CHK_RULES,
	BIN_BUF_DBG_IDLE_CHK_PARSING_DATA,
	BIN_BUF_DBG_ATTN_BLOCKS,
	BIN_BUF_DBG_ATTN_REGS,
	BIN_BUF_DBG_ATTN_INDEXES,
	BIN_BUF_DBG_ATTN_NAME_OFFSETS,
	BIN_BUF_DBG_BLOCKS,
	BIN_BUF_DBG_BLOCKS_CHIP_DATA,
	BIN_BUF_DBG_BUS_LINES,
	BIN_BUF_DBG_BLOCKS_USER_DATA,
	BIN_BUF_DBG_BLOCKS_CHIP_USER_DATA,
	BIN_BUF_DBG_BUS_LINE_NAME_OFFSETS,
	BIN_BUF_DBG_RESET_REGS,
	BIN_BUF_DBG_PARSING_STRINGS,
	MAX_BIN_DBG_BUFFER_TYPE
};


/* Attention bit mapping */
struct dbg_attn_bit_mapping {
	u16 data;
#define DBG_ATTN_BIT_MAPPING_VAL_MASK			0x7FFF
#define DBG_ATTN_BIT_MAPPING_VAL_SHIFT			0
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_MASK	0x1
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_SHIFT	15
};

/* Attention block per-type data */
struct dbg_attn_block_type_data {
	u16 names_offset;
	u16 reserved1;
	u8 num_regs;
	u8 reserved2;
	u16 regs_offset;

};

/* Block attentions */
struct dbg_attn_block {
	struct dbg_attn_block_type_data per_type_data[2];
};

/* Attention register result */
struct dbg_attn_reg_result {
	u32 data;
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_MASK	0xFFFFFF
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_SHIFT	0
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_MASK	0xFF
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_SHIFT	24
	u16 block_attn_offset;
	u16 reserved;
	u32 sts_val;
	u32 mask_val;
};

/* Attention block result */
struct dbg_attn_block_result {
	u8 block_id;
	u8 data;
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_MASK	0x3
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_SHIFT	0
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_MASK	0x3F
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_SHIFT	2
	u16 names_offset;
	struct dbg_attn_reg_result reg_results[15];
};

/* Mode header */
struct dbg_mode_hdr {
	u16 data;
#define DBG_MODE_HDR_EVAL_MODE_MASK		0x1
#define DBG_MODE_HDR_EVAL_MODE_SHIFT		0
#define DBG_MODE_HDR_MODES_BUF_OFFSET_MASK	0x7FFF
#define DBG_MODE_HDR_MODES_BUF_OFFSET_SHIFT	1
};

/* Attention register */
struct dbg_attn_reg {
	struct dbg_mode_hdr mode;
	u16 block_attn_offset;
	u32 data;
#define DBG_ATTN_REG_STS_ADDRESS_MASK	0xFFFFFF
#define DBG_ATTN_REG_STS_ADDRESS_SHIFT	0
#define DBG_ATTN_REG_NUM_REG_ATTN_MASK	0xFF
#define DBG_ATTN_REG_NUM_REG_ATTN_SHIFT 24
	u32 sts_clr_address;
	u32 mask_address;
};

/* Attention types */
enum dbg_attn_type {
	ATTN_TYPE_INTERRUPT,
	ATTN_TYPE_PARITY,
	MAX_DBG_ATTN_TYPE
};

/* Block debug data */
struct dbg_block {
	u8 name[15];
	u8 associated_storm_letter;
};

/* Chip-specific block debug data */
struct dbg_block_chip {
	u8 flags;
#define DBG_BLOCK_CHIP_IS_REMOVED_MASK		 0x1
#define DBG_BLOCK_CHIP_IS_REMOVED_SHIFT		 0
#define DBG_BLOCK_CHIP_HAS_RESET_REG_MASK	 0x1
#define DBG_BLOCK_CHIP_HAS_RESET_REG_SHIFT	 1
#define DBG_BLOCK_CHIP_UNRESET_BEFORE_DUMP_MASK  0x1
#define DBG_BLOCK_CHIP_UNRESET_BEFORE_DUMP_SHIFT 2
#define DBG_BLOCK_CHIP_HAS_DBG_BUS_MASK		 0x1
#define DBG_BLOCK_CHIP_HAS_DBG_BUS_SHIFT	 3
#define DBG_BLOCK_CHIP_HAS_LATENCY_EVENTS_MASK	 0x1
#define DBG_BLOCK_CHIP_HAS_LATENCY_EVENTS_SHIFT  4
#define DBG_BLOCK_CHIP_RESERVED0_MASK		 0x7
#define DBG_BLOCK_CHIP_RESERVED0_SHIFT		 5
	u8 dbg_client_id;
	u8 reset_reg_id;
	u8 reset_reg_bit_offset;
	struct dbg_mode_hdr dbg_bus_mode;
	u16 reserved1;
	u8 reserved2;
	u8 num_of_dbg_bus_lines;
	u16 dbg_bus_lines_offset;
	u32 dbg_select_reg_addr;
	u32 dbg_dword_enable_reg_addr;
	u32 dbg_shift_reg_addr;
	u32 dbg_force_valid_reg_addr;
	u32 dbg_force_frame_reg_addr;
};

/* Chip-specific block user debug data */
struct dbg_block_chip_user {
	u8 num_of_dbg_bus_lines;
	u8 has_latency_events;
	u16 names_offset;
};

/* Block user debug data */
struct dbg_block_user {
	u8 name[16];
};

/* Block Debug line data */
struct dbg_bus_line {
	u8 data;
#define DBG_BUS_LINE_NUM_OF_GROUPS_MASK		0xF
#define DBG_BUS_LINE_NUM_OF_GROUPS_SHIFT	0
#define DBG_BUS_LINE_IS_256B_MASK		0x1
#define DBG_BUS_LINE_IS_256B_SHIFT		4
#define DBG_BUS_LINE_RESERVED_MASK		0x7
#define DBG_BUS_LINE_RESERVED_SHIFT		5
	u8 group_sizes;
};

/* Condition header for registers dump */
struct dbg_dump_cond_hdr {
	struct dbg_mode_hdr mode; /* Mode header */
	u8 block_id; /* block ID */
	u8 data_size; /* size in dwords of the data following this header */
};

/* Memory data for registers dump */
struct dbg_dump_mem {
	u32 dword0;
#define DBG_DUMP_MEM_ADDRESS_MASK	0xFFFFFF
#define DBG_DUMP_MEM_ADDRESS_SHIFT	0
#define DBG_DUMP_MEM_MEM_GROUP_ID_MASK	0xFF
#define DBG_DUMP_MEM_MEM_GROUP_ID_SHIFT	24
	u32 dword1;
#define DBG_DUMP_MEM_LENGTH_MASK	0xFFFFFF
#define DBG_DUMP_MEM_LENGTH_SHIFT	0
#define DBG_DUMP_MEM_WIDE_BUS_MASK	0x1
#define DBG_DUMP_MEM_WIDE_BUS_SHIFT	24
#define DBG_DUMP_MEM_RESERVED_MASK	0x7F
#define DBG_DUMP_MEM_RESERVED_SHIFT	25
};

/* Register data for registers dump */
struct dbg_dump_reg {
	u32 data;
#define DBG_DUMP_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_DUMP_REG_ADDRESS_SHIFT	0
#define DBG_DUMP_REG_WIDE_BUS_MASK	0x1
#define DBG_DUMP_REG_WIDE_BUS_SHIFT	23
#define DBG_DUMP_REG_LENGTH_MASK	0xFF
#define DBG_DUMP_REG_LENGTH_SHIFT	24
};

/* Split header for registers dump */
struct dbg_dump_split_hdr {
	u32 hdr;
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_MASK	0xFFFFFF
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_SHIFT	0
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_MASK	0xFF
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_SHIFT	24
};

/* Condition header for idle check */
struct dbg_idle_chk_cond_hdr {
	struct dbg_mode_hdr mode; /* Mode header */
	u16 data_size; /* size in dwords of the data following this header */
};

/* Idle Check condition register */
struct dbg_idle_chk_cond_reg {
	u32 data;
#define DBG_IDLE_CHK_COND_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_IDLE_CHK_COND_REG_ADDRESS_SHIFT	0
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_MASK	0x1
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_SHIFT	23
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_MASK	0xFF
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_SHIFT	24
	u16 num_entries;
	u8 entry_size;
	u8 start_entry;
};

/* Idle Check info register */
struct dbg_idle_chk_info_reg {
	u32 data;
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_SHIFT	0
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_MASK	0x1
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_SHIFT	23
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_MASK	0xFF
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_SHIFT	24
	u16 size; /* register size in dwords */
	struct dbg_mode_hdr mode; /* Mode header */
};

/* Idle Check register */
union dbg_idle_chk_reg {
	struct dbg_idle_chk_cond_reg cond_reg; /* condition register */
	struct dbg_idle_chk_info_reg info_reg; /* info register */
};

/* Idle Check result header */
struct dbg_idle_chk_result_hdr {
	u16 rule_id; /* Failing rule index */
	u16 mem_entry_id; /* Failing memory entry index */
	u8 num_dumped_cond_regs; /* number of dumped condition registers */
	u8 num_dumped_info_regs; /* number of dumped condition registers */
	u8 severity; /* from dbg_idle_chk_severity_types enum */
	u8 reserved;
};

/* Idle Check result register header */
struct dbg_idle_chk_result_reg_hdr {
	u8 data;
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_MASK  0x1
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_SHIFT 0
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_MASK  0x7F
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_SHIFT 1
	u8 start_entry; /* index of the first checked entry */
	u16 size; /* register size in dwords */
};

/* Idle Check rule */
struct dbg_idle_chk_rule {
	u16 rule_id; /* Idle Check rule ID */
	u8 severity; /* value from dbg_idle_chk_severity_types enum */
	u8 cond_id; /* Condition ID */
	u8 num_cond_regs; /* number of condition registers */
	u8 num_info_regs; /* number of info registers */
	u8 num_imms; /* number of immediates in the condition */
	u8 reserved1;
	u16 reg_offset; /* offset of this rules registers in the idle check
			 * register array (in dbg_idle_chk_reg units).
			 */
	u16 imm_offset; /* offset of this rules immediate values in the
			 * immediate values array (in dwords).
			 */
};

/* Idle Check rule parsing data */
struct dbg_idle_chk_rule_parsing_data {
	u32 data;
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_MASK	0x1
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_SHIFT	0
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_MASK	0x7FFFFFFF
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_SHIFT	1
};

/* Idle check severity types */
enum dbg_idle_chk_severity_types {
	/* idle check failure should cause an error */
	IDLE_CHK_SEVERITY_ERROR,
	/* idle check failure should cause an error only if theres no traffic */
	IDLE_CHK_SEVERITY_ERROR_NO_TRAFFIC,
	/* idle check failure should cause a warning */
	IDLE_CHK_SEVERITY_WARNING,
	MAX_DBG_IDLE_CHK_SEVERITY_TYPES
};

/* Reset register */
struct dbg_reset_reg {
	u32 data;
#define DBG_RESET_REG_ADDR_MASK        0xFFFFFF
#define DBG_RESET_REG_ADDR_SHIFT       0
#define DBG_RESET_REG_IS_REMOVED_MASK  0x1
#define DBG_RESET_REG_IS_REMOVED_SHIFT 24
#define DBG_RESET_REG_RESERVED_MASK    0x7F
#define DBG_RESET_REG_RESERVED_SHIFT   25
};

/* Debug Bus block data */
struct dbg_bus_block_data {
	u8 enable_mask;
	u8 right_shift;
	u8 force_valid_mask;
	u8 force_frame_mask;
	u8 dword_mask;
	u8 line_num;
	u8 hw_id;
	u8 flags;
#define DBG_BUS_BLOCK_DATA_IS_256B_LINE_MASK  0x1
#define DBG_BUS_BLOCK_DATA_IS_256B_LINE_SHIFT 0
#define DBG_BUS_BLOCK_DATA_RESERVED_MASK      0x7F
#define DBG_BUS_BLOCK_DATA_RESERVED_SHIFT     1
};

enum dbg_bus_clients {
	DBG_BUS_CLIENT_RBCN,
	DBG_BUS_CLIENT_RBCP,
	DBG_BUS_CLIENT_RBCR,
	DBG_BUS_CLIENT_RBCT,
	DBG_BUS_CLIENT_RBCU,
	DBG_BUS_CLIENT_RBCF,
	DBG_BUS_CLIENT_RBCX,
	DBG_BUS_CLIENT_RBCS,
	DBG_BUS_CLIENT_RBCH,
	DBG_BUS_CLIENT_RBCZ,
	DBG_BUS_CLIENT_OTHER_ENGINE,
	DBG_BUS_CLIENT_TIMESTAMP,
	DBG_BUS_CLIENT_CPU,
	DBG_BUS_CLIENT_RBCY,
	DBG_BUS_CLIENT_RBCQ,
	DBG_BUS_CLIENT_RBCM,
	DBG_BUS_CLIENT_RBCB,
	DBG_BUS_CLIENT_RBCW,
	DBG_BUS_CLIENT_RBCV,
	MAX_DBG_BUS_CLIENTS
};

/* Debug Bus constraint operation types */
enum dbg_bus_constraint_ops {
	DBG_BUS_CONSTRAINT_OP_EQ,
	DBG_BUS_CONSTRAINT_OP_NE,
	DBG_BUS_CONSTRAINT_OP_LT,
	DBG_BUS_CONSTRAINT_OP_LTC,
	DBG_BUS_CONSTRAINT_OP_LE,
	DBG_BUS_CONSTRAINT_OP_LEC,
	DBG_BUS_CONSTRAINT_OP_GT,
	DBG_BUS_CONSTRAINT_OP_GTC,
	DBG_BUS_CONSTRAINT_OP_GE,
	DBG_BUS_CONSTRAINT_OP_GEC,
	MAX_DBG_BUS_CONSTRAINT_OPS
};

/* Debug Bus trigger state data */
struct dbg_bus_trigger_state_data {
	u8 msg_len;
	u8 constraint_dword_mask;
	u8 storm_id;
	u8 reserved;
};

/* Debug Bus memory address */
struct dbg_bus_mem_addr {
	u32 lo;
	u32 hi;
};

/* Debug Bus PCI buffer data */
struct dbg_bus_pci_buf_data {
	struct dbg_bus_mem_addr phys_addr; /* PCI buffer physical address */
	struct dbg_bus_mem_addr virt_addr; /* PCI buffer virtual address */
	u32 size; /* PCI buffer size in bytes */
};

/* Debug Bus Storm EID range filter params */
struct dbg_bus_storm_eid_range_params {
	u8 min; /* Minimal event ID to filter on */
	u8 max; /* Maximal event ID to filter on */
};

/* Debug Bus Storm EID mask filter params */
struct dbg_bus_storm_eid_mask_params {
	u8 val; /* Event ID value */
	u8 mask; /* Event ID mask. 1s in the mask = dont care bits. */
};

/* Debug Bus Storm EID filter params */
union dbg_bus_storm_eid_params {
	struct dbg_bus_storm_eid_range_params range;
	struct dbg_bus_storm_eid_mask_params mask;
};

/* Debug Bus Storm data */
struct dbg_bus_storm_data {
	u8 enabled;
	u8 mode;
	u8 hw_id;
	u8 eid_filter_en;
	u8 eid_range_not_mask;
	u8 cid_filter_en;
	union dbg_bus_storm_eid_params eid_filter_params;
	u32 cid;
};

/* Debug Bus data */
struct dbg_bus_data {
	u32 app_version;
	u8 state;
	u8 mode_256b_en;
	u8 num_enabled_blocks;
	u8 num_enabled_storms;
	u8 target;
	u8 one_shot_en;
	u8 grc_input_en;
	u8 timestamp_input_en;
	u8 filter_en;
	u8 adding_filter;
	u8 filter_pre_trigger;
	u8 filter_post_trigger;
	u8 trigger_en;
	u8 filter_constraint_dword_mask;
	u8 next_trigger_state;
	u8 next_constraint_id;
	struct dbg_bus_trigger_state_data trigger_states[3];
	u8 filter_msg_len;
	u8 rcv_from_other_engine;
	u8 blocks_dword_mask;
	u8 blocks_dword_overlap;
	u32 hw_id_mask;
	struct dbg_bus_pci_buf_data pci_buf;
	struct dbg_bus_block_data blocks[132];
	struct dbg_bus_storm_data storms[6];
};

/* Debug bus states */
enum dbg_bus_states {
	DBG_BUS_STATE_IDLE,
	DBG_BUS_STATE_READY,
	DBG_BUS_STATE_RECORDING,
	DBG_BUS_STATE_STOPPED,
	MAX_DBG_BUS_STATES
};

/* Debug Bus Storm modes */
enum dbg_bus_storm_modes {
	DBG_BUS_STORM_MODE_PRINTF,
	DBG_BUS_STORM_MODE_PRAM_ADDR,
	DBG_BUS_STORM_MODE_DRA_RW,
	DBG_BUS_STORM_MODE_DRA_W,
	DBG_BUS_STORM_MODE_LD_ST_ADDR,
	DBG_BUS_STORM_MODE_DRA_FSM,
	DBG_BUS_STORM_MODE_FAST_DBGMUX,
	DBG_BUS_STORM_MODE_RH,
	DBG_BUS_STORM_MODE_RH_WITH_STORE,
	DBG_BUS_STORM_MODE_FOC,
	DBG_BUS_STORM_MODE_EXT_STORE,
	MAX_DBG_BUS_STORM_MODES
};

/* Debug bus target IDs */
enum dbg_bus_targets {
	DBG_BUS_TARGET_ID_INT_BUF,
	DBG_BUS_TARGET_ID_NIG,
	DBG_BUS_TARGET_ID_PCI,
	MAX_DBG_BUS_TARGETS
};

/* GRC Dump data */
struct dbg_grc_data {
	u8 params_initialized;
	u8 reserved1;
	u16 reserved2;
	u32 param_val[48];
};

/* Debug GRC params */
enum dbg_grc_params {
	DBG_GRC_PARAM_DUMP_TSTORM,
	DBG_GRC_PARAM_DUMP_MSTORM,
	DBG_GRC_PARAM_DUMP_USTORM,
	DBG_GRC_PARAM_DUMP_XSTORM,
	DBG_GRC_PARAM_DUMP_YSTORM,
	DBG_GRC_PARAM_DUMP_PSTORM,
	DBG_GRC_PARAM_DUMP_REGS,
	DBG_GRC_PARAM_DUMP_RAM,
	DBG_GRC_PARAM_DUMP_PBUF,
	DBG_GRC_PARAM_DUMP_IOR,
	DBG_GRC_PARAM_DUMP_VFC,
	DBG_GRC_PARAM_DUMP_CM_CTX,
	DBG_GRC_PARAM_DUMP_PXP,
	DBG_GRC_PARAM_DUMP_RSS,
	DBG_GRC_PARAM_DUMP_CAU,
	DBG_GRC_PARAM_DUMP_QM,
	DBG_GRC_PARAM_DUMP_MCP,
	DBG_GRC_PARAM_DUMP_DORQ,
	DBG_GRC_PARAM_DUMP_CFC,
	DBG_GRC_PARAM_DUMP_IGU,
	DBG_GRC_PARAM_DUMP_BRB,
	DBG_GRC_PARAM_DUMP_BTB,
	DBG_GRC_PARAM_DUMP_BMB,
	DBG_GRC_PARAM_RESERVD1,
	DBG_GRC_PARAM_DUMP_MULD,
	DBG_GRC_PARAM_DUMP_PRS,
	DBG_GRC_PARAM_DUMP_DMAE,
	DBG_GRC_PARAM_DUMP_TM,
	DBG_GRC_PARAM_DUMP_SDM,
	DBG_GRC_PARAM_DUMP_DIF,
	DBG_GRC_PARAM_DUMP_STATIC,
	DBG_GRC_PARAM_UNSTALL,
	DBG_GRC_PARAM_RESERVED2,
	DBG_GRC_PARAM_MCP_TRACE_META_SIZE,
	DBG_GRC_PARAM_EXCLUDE_ALL,
	DBG_GRC_PARAM_CRASH,
	DBG_GRC_PARAM_PARITY_SAFE,
	DBG_GRC_PARAM_DUMP_CM,
	DBG_GRC_PARAM_DUMP_PHY,
	DBG_GRC_PARAM_NO_MCP,
	DBG_GRC_PARAM_NO_FW_VER,
	DBG_GRC_PARAM_RESERVED3,
	DBG_GRC_PARAM_DUMP_MCP_HW_DUMP,
	DBG_GRC_PARAM_DUMP_ILT_CDUC,
	DBG_GRC_PARAM_DUMP_ILT_CDUT,
	DBG_GRC_PARAM_DUMP_CAU_EXT,
	MAX_DBG_GRC_PARAMS
};

/* Debug status codes */
enum dbg_status {
	DBG_STATUS_OK,
	DBG_STATUS_APP_VERSION_NOT_SET,
	DBG_STATUS_UNSUPPORTED_APP_VERSION,
	DBG_STATUS_DBG_BLOCK_NOT_RESET,
	DBG_STATUS_INVALID_ARGS,
	DBG_STATUS_OUTPUT_ALREADY_SET,
	DBG_STATUS_INVALID_PCI_BUF_SIZE,
	DBG_STATUS_PCI_BUF_ALLOC_FAILED,
	DBG_STATUS_PCI_BUF_NOT_ALLOCATED,
	DBG_STATUS_INVALID_FILTER_TRIGGER_DWORDS,
	DBG_STATUS_NO_MATCHING_FRAMING_MODE,
	DBG_STATUS_VFC_READ_ERROR,
	DBG_STATUS_STORM_ALREADY_ENABLED,
	DBG_STATUS_STORM_NOT_ENABLED,
	DBG_STATUS_BLOCK_ALREADY_ENABLED,
	DBG_STATUS_BLOCK_NOT_ENABLED,
	DBG_STATUS_NO_INPUT_ENABLED,
	DBG_STATUS_NO_FILTER_TRIGGER_256B,
	DBG_STATUS_FILTER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_NOT_ENABLED,
	DBG_STATUS_CANT_ADD_CONSTRAINT,
	DBG_STATUS_TOO_MANY_TRIGGER_STATES,
	DBG_STATUS_TOO_MANY_CONSTRAINTS,
	DBG_STATUS_RECORDING_NOT_STARTED,
	DBG_STATUS_DATA_DIDNT_TRIGGER,
	DBG_STATUS_NO_DATA_RECORDED,
	DBG_STATUS_DUMP_BUF_TOO_SMALL,
	DBG_STATUS_DUMP_NOT_CHUNK_ALIGNED,
	DBG_STATUS_UNKNOWN_CHIP,
	DBG_STATUS_VIRT_MEM_ALLOC_FAILED,
	DBG_STATUS_BLOCK_IN_RESET,
	DBG_STATUS_INVALID_TRACE_SIGNATURE,
	DBG_STATUS_INVALID_NVRAM_BUNDLE,
	DBG_STATUS_NVRAM_GET_IMAGE_FAILED,
	DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE,
	DBG_STATUS_NVRAM_READ_FAILED,
	DBG_STATUS_IDLE_CHK_PARSE_FAILED,
	DBG_STATUS_MCP_TRACE_BAD_DATA,
	DBG_STATUS_MCP_TRACE_NO_META,
	DBG_STATUS_MCP_COULD_NOT_HALT,
	DBG_STATUS_MCP_COULD_NOT_RESUME,
	DBG_STATUS_RESERVED0,
	DBG_STATUS_SEMI_FIFO_NOT_EMPTY,
	DBG_STATUS_IGU_FIFO_BAD_DATA,
	DBG_STATUS_MCP_COULD_NOT_MASK_PRTY,
	DBG_STATUS_FW_ASSERTS_PARSE_FAILED,
	DBG_STATUS_REG_FIFO_BAD_DATA,
	DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA,
	DBG_STATUS_DBG_ARRAY_NOT_SET,
	DBG_STATUS_RESERVED1,
	DBG_STATUS_NON_MATCHING_LINES,
	DBG_STATUS_INSUFFICIENT_HW_IDS,
	DBG_STATUS_DBG_BUS_IN_USE,
	DBG_STATUS_INVALID_STORM_DBG_MODE,
	DBG_STATUS_OTHER_ENGINE_BB_ONLY,
	DBG_STATUS_FILTER_SINGLE_HW_ID,
	DBG_STATUS_TRIGGER_SINGLE_HW_ID,
	DBG_STATUS_MISSING_TRIGGER_STATE_STORM,
	MAX_DBG_STATUS
};

/* Debug Storms IDs */
enum dbg_storms {
	DBG_TSTORM_ID,
	DBG_MSTORM_ID,
	DBG_USTORM_ID,
	DBG_XSTORM_ID,
	DBG_YSTORM_ID,
	DBG_PSTORM_ID,
	MAX_DBG_STORMS
};

/* Idle Check data */
struct idle_chk_data {
	u32 buf_size;
	u8 buf_size_set;
	u8 reserved1;
	u16 reserved2;
};

struct pretend_params {
	u8 split_type;
	u8 reserved;
	u16 split_id;
};

/* Debug Tools data (per HW function)
 */
struct dbg_tools_data {
	struct dbg_grc_data grc;
	struct dbg_bus_data bus;
	struct idle_chk_data idle_chk;
	u8 mode_enable[40];
	u8 block_in_reset[132];
	u8 chip_id;
	u8 hw_type;
	u8 num_ports;
	u8 num_pfs_per_port;
	u8 num_vfs;
	u8 initialized;
	u8 use_dmae;
	u8 reserved;
	struct pretend_params pretend;
	u32 num_regs_read;
};

/* ILT Clients */
enum ilt_clients {
	ILT_CLI_CDUC,
	ILT_CLI_CDUT,
	ILT_CLI_QM,
	ILT_CLI_TM,
	ILT_CLI_SRC,
	ILT_CLI_TSDM,
	ILT_CLI_RGFS,
	ILT_CLI_TGFS,
	MAX_ILT_CLIENTS
};

/********************************/
/* HSI Init Functions constants */
/********************************/

/* Number of VLAN priorities */
#define NUM_OF_VLAN_PRIORITIES	8

/* BRB RAM init requirements */
struct init_brb_ram_req {
	u32 guranteed_per_tc;
	u32 headroom_per_tc;
	u32 min_pkt_size;
	u32 max_ports_per_engine;
	u8 num_active_tcs[MAX_NUM_PORTS];
};

/* ETS per-TC init requirements */
struct init_ets_tc_req {
	u8 use_sp;
	u8 use_wfq;
	u16 weight;
};

/* ETS init requirements */
struct init_ets_req {
	u32 mtu;
	struct init_ets_tc_req tc_req[NUM_OF_TCS];
};

/* NIG LB RL init requirements */
struct init_nig_lb_rl_req {
	u16 lb_mac_rate;
	u16 lb_rate;
	u32 mtu;
	u16 tc_rate[NUM_OF_PHYS_TCS];
};

/* NIG TC mapping for each priority */
struct init_nig_pri_tc_map_entry {
	u8 tc_id;
	u8 valid;
};

/* NIG priority to TC map init requirements */
struct init_nig_pri_tc_map_req {
	struct init_nig_pri_tc_map_entry pri[NUM_OF_VLAN_PRIORITIES];
};

/* QM per global RL init parameters */
struct init_qm_global_rl_params {
	u32 rate_limit;
};

/* QM per-port init parameters */
struct init_qm_port_params {
	u16 active_phys_tcs;
	u16 num_pbf_cmd_lines;
	u16 num_btb_blocks;
	u8 active;
	u8 reserved;
};

/* QM per-PQ init parameters */
struct init_qm_pq_params {
	u8 vport_id;
	u8 tc_id;
	u8 wrr_group;
	u8 rl_valid;
	u16 rl_id;
	u8 port_id;
	u8 reserved;
};

/* QM per-vport init parameters */
struct init_qm_vport_params {
	u16 wfq;
	u16 first_tx_pq_id[NUM_OF_TCS];
};

/**************************************/
/* Init Tool HSI constants and macros */
/**************************************/

/* Width of GRC address in bits (addresses are specified in dwords) */
#define GRC_ADDR_BITS	23
#define MAX_GRC_ADDR	(BIT(GRC_ADDR_BITS) - 1)

/* indicates an init that should be applied to any phase ID */
#define ANY_PHASE_ID	0xffff

/* Max size in dwords of a zipped array */
#define MAX_ZIPPED_SIZE	8192
enum chip_ids {
	CHIP_BB,
	CHIP_K2,
	MAX_CHIP_IDS
};

struct fw_asserts_ram_section {
	__le16 section_ram_line_offset;
	__le16 section_ram_line_size;
	u8 list_dword_offset;
	u8 list_element_dword_size;
	u8 list_num_elements;
	u8 list_next_index_dword_offset;
};

struct fw_ver_num {
	u8 major;
	u8 minor;
	u8 rev;
	u8 eng;
};

struct fw_ver_info {
	__le16 tools_ver;
	u8 image_id;
	u8 reserved1;
	struct fw_ver_num num;
	__le32 timestamp;
	__le32 reserved2;
};

struct fw_info {
	struct fw_ver_info ver;
	struct fw_asserts_ram_section fw_asserts_section;
};

struct fw_info_location {
	__le32 grc_addr;
	__le32 size;
};

enum init_modes {
	MODE_RESERVED,
	MODE_BB,
	MODE_K2,
	MODE_ASIC,
	MODE_RESERVED2,
	MODE_RESERVED3,
	MODE_RESERVED4,
	MODE_RESERVED5,
	MODE_SF,
	MODE_MF_SD,
	MODE_MF_SI,
	MODE_PORTS_PER_ENG_1,
	MODE_PORTS_PER_ENG_2,
	MODE_PORTS_PER_ENG_4,
	MODE_100G,
	MODE_RESERVED6,
	MODE_RESERVED7,
	MAX_INIT_MODES
};

enum init_phases {
	PHASE_ENGINE,
	PHASE_PORT,
	PHASE_PF,
	PHASE_VF,
	PHASE_QM_PF,
	MAX_INIT_PHASES
};

enum init_split_types {
	SPLIT_TYPE_NONE,
	SPLIT_TYPE_PORT,
	SPLIT_TYPE_PF,
	SPLIT_TYPE_PORT_PF,
	SPLIT_TYPE_VF,
	MAX_INIT_SPLIT_TYPES
};

/* Binary buffer header */
struct bin_buffer_hdr {
	u32 offset;
	u32 length;
};

/* Binary init buffer types */
enum bin_init_buffer_type {
	BIN_BUF_INIT_FW_VER_INFO,
	BIN_BUF_INIT_CMD,
	BIN_BUF_INIT_VAL,
	BIN_BUF_INIT_MODE_TREE,
	BIN_BUF_INIT_IRO,
	BIN_BUF_INIT_OVERLAYS,
	MAX_BIN_INIT_BUFFER_TYPE
};

/* FW overlay buffer header */
struct fw_overlay_buf_hdr {
	u32 data;
#define FW_OVERLAY_BUF_HDR_STORM_ID_MASK  0xFF
#define FW_OVERLAY_BUF_HDR_STORM_ID_SHIFT 0
#define FW_OVERLAY_BUF_HDR_BUF_SIZE_MASK  0xFFFFFF
#define FW_OVERLAY_BUF_HDR_BUF_SIZE_SHIFT 8
};

/* init array header: raw */
struct init_array_raw_hdr {
	__le32						data;
#define INIT_ARRAY_RAW_HDR_TYPE_MASK			0xF
#define INIT_ARRAY_RAW_HDR_TYPE_SHIFT			0
#define INIT_ARRAY_RAW_HDR_PARAMS_MASK			0xFFFFFFF
#define INIT_ARRAY_RAW_HDR_PARAMS_SHIFT			4
};

/* init array header: standard */
struct init_array_standard_hdr {
	__le32						data;
#define INIT_ARRAY_STANDARD_HDR_TYPE_MASK		0xF
#define INIT_ARRAY_STANDARD_HDR_TYPE_SHIFT		0
#define INIT_ARRAY_STANDARD_HDR_SIZE_MASK		0xFFFFFFF
#define INIT_ARRAY_STANDARD_HDR_SIZE_SHIFT		4
};

/* init array header: zipped */
struct init_array_zipped_hdr {
	__le32						data;
#define INIT_ARRAY_ZIPPED_HDR_TYPE_MASK			0xF
#define INIT_ARRAY_ZIPPED_HDR_TYPE_SHIFT		0
#define INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE_MASK		0xFFFFFFF
#define INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE_SHIFT		4
};

/* init array header: pattern */
struct init_array_pattern_hdr {
	__le32						data;
#define INIT_ARRAY_PATTERN_HDR_TYPE_MASK		0xF
#define INIT_ARRAY_PATTERN_HDR_TYPE_SHIFT		0
#define INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE_MASK	0xF
#define INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE_SHIFT	4
#define INIT_ARRAY_PATTERN_HDR_REPETITIONS_MASK		0xFFFFFF
#define INIT_ARRAY_PATTERN_HDR_REPETITIONS_SHIFT	8
};

/* init array header union */
union init_array_hdr {
	struct init_array_raw_hdr			raw;
	struct init_array_standard_hdr			standard;
	struct init_array_zipped_hdr			zipped;
	struct init_array_pattern_hdr			pattern;
};

/* init array types */
enum init_array_types {
	INIT_ARR_STANDARD,
	INIT_ARR_ZIPPED,
	INIT_ARR_PATTERN,
	MAX_INIT_ARRAY_TYPES
};

/* init operation: callback */
struct init_callback_op {
	__le32						op_data;
#define INIT_CALLBACK_OP_OP_MASK			0xF
#define INIT_CALLBACK_OP_OP_SHIFT			0
#define INIT_CALLBACK_OP_RESERVED_MASK			0xFFFFFFF
#define INIT_CALLBACK_OP_RESERVED_SHIFT			4
	__le16						callback_id;
	__le16						block_id;
};

/* init operation: delay */
struct init_delay_op {
	__le32						op_data;
#define INIT_DELAY_OP_OP_MASK				0xF
#define INIT_DELAY_OP_OP_SHIFT				0
#define INIT_DELAY_OP_RESERVED_MASK			0xFFFFFFF
#define INIT_DELAY_OP_RESERVED_SHIFT			4
	__le32						delay;
};

/* init operation: if_mode */
struct init_if_mode_op {
	__le32						op_data;
#define INIT_IF_MODE_OP_OP_MASK				0xF
#define INIT_IF_MODE_OP_OP_SHIFT			0
#define INIT_IF_MODE_OP_RESERVED1_MASK			0xFFF
#define INIT_IF_MODE_OP_RESERVED1_SHIFT			4
#define INIT_IF_MODE_OP_CMD_OFFSET_MASK			0xFFFF
#define INIT_IF_MODE_OP_CMD_OFFSET_SHIFT		16
	__le16						reserved2;
	__le16						modes_buf_offset;
};

/* init operation: if_phase */
struct init_if_phase_op {
	__le32						op_data;
#define INIT_IF_PHASE_OP_OP_MASK			0xF
#define INIT_IF_PHASE_OP_OP_SHIFT			0
#define INIT_IF_PHASE_OP_RESERVED1_MASK			0xFFF
#define INIT_IF_PHASE_OP_RESERVED1_SHIFT		4
#define INIT_IF_PHASE_OP_CMD_OFFSET_MASK		0xFFFF
#define INIT_IF_PHASE_OP_CMD_OFFSET_SHIFT		16
	__le32						phase_data;
#define INIT_IF_PHASE_OP_PHASE_MASK			0xFF
#define INIT_IF_PHASE_OP_PHASE_SHIFT			0
#define INIT_IF_PHASE_OP_RESERVED2_MASK			0xFF
#define INIT_IF_PHASE_OP_RESERVED2_SHIFT		8
#define INIT_IF_PHASE_OP_PHASE_ID_MASK			0xFFFF
#define INIT_IF_PHASE_OP_PHASE_ID_SHIFT			16
};

/* init mode operators */
enum init_mode_ops {
	INIT_MODE_OP_NOT,
	INIT_MODE_OP_OR,
	INIT_MODE_OP_AND,
	MAX_INIT_MODE_OPS
};

/* init operation: raw */
struct init_raw_op {
	__le32						op_data;
#define INIT_RAW_OP_OP_MASK				0xF
#define INIT_RAW_OP_OP_SHIFT				0
#define INIT_RAW_OP_PARAM1_MASK				0xFFFFFFF
#define INIT_RAW_OP_PARAM1_SHIFT			4
	__le32						param2;
};

/* init array params */
struct init_op_array_params {
	__le16						size;
	__le16						offset;
};

/* Write init operation arguments */
union init_write_args {
	__le32						inline_val;
	__le32						zeros_count;
	__le32						array_offset;
	struct init_op_array_params			runtime;
};

/* init operation: write */
struct init_write_op {
	__le32						data;
#define INIT_WRITE_OP_OP_MASK				0xF
#define INIT_WRITE_OP_OP_SHIFT				0
#define INIT_WRITE_OP_SOURCE_MASK			0x7
#define INIT_WRITE_OP_SOURCE_SHIFT			4
#define INIT_WRITE_OP_RESERVED_MASK			0x1
#define INIT_WRITE_OP_RESERVED_SHIFT			7
#define INIT_WRITE_OP_WIDE_BUS_MASK			0x1
#define INIT_WRITE_OP_WIDE_BUS_SHIFT			8
#define INIT_WRITE_OP_ADDRESS_MASK			0x7FFFFF
#define INIT_WRITE_OP_ADDRESS_SHIFT			9
	union init_write_args				args;
};

/* init operation: read */
struct init_read_op {
	__le32						op_data;
#define INIT_READ_OP_OP_MASK				0xF
#define INIT_READ_OP_OP_SHIFT				0
#define INIT_READ_OP_POLL_TYPE_MASK			0xF
#define INIT_READ_OP_POLL_TYPE_SHIFT			4
#define INIT_READ_OP_RESERVED_MASK			0x1
#define INIT_READ_OP_RESERVED_SHIFT			8
#define INIT_READ_OP_ADDRESS_MASK			0x7FFFFF
#define INIT_READ_OP_ADDRESS_SHIFT			9
	__le32						expected_val;
};

/* Init operations union */
union init_op {
	struct init_raw_op				raw;
	struct init_write_op				write;
	struct init_read_op				read;
	struct init_if_mode_op				if_mode;
	struct init_if_phase_op				if_phase;
	struct init_callback_op				callback;
	struct init_delay_op				delay;
};

/* Init command operation types */
enum init_op_types {
	INIT_OP_READ,
	INIT_OP_WRITE,
	INIT_OP_IF_MODE,
	INIT_OP_IF_PHASE,
	INIT_OP_DELAY,
	INIT_OP_CALLBACK,
	MAX_INIT_OP_TYPES
};

/* init polling types */
enum init_poll_types {
	INIT_POLL_NONE,
	INIT_POLL_EQ,
	INIT_POLL_OR,
	INIT_POLL_AND,
	MAX_INIT_POLL_TYPES
};

/* init source types */
enum init_source_types {
	INIT_SRC_INLINE,
	INIT_SRC_ZEROS,
	INIT_SRC_ARRAY,
	INIT_SRC_RUNTIME,
	MAX_INIT_SOURCE_TYPES
};

/* Internal RAM Offsets macro data */
struct iro {
	u32 base;
	u16 m1;
	u16 m2;
	u16 m3;
	u16 size;
};

/***************************** Public Functions *******************************/

/**
 * qed_dbg_set_bin_ptr(): Sets a pointer to the binary data with debug
 *                        arrays.
 *
 * @p_hwfn: HW device data.
 * @bin_ptr: A pointer to the binary data with debug arrays.
 *
 * Return: enum dbg status.
 */
enum dbg_status qed_dbg_set_bin_ptr(struct qed_hwfn *p_hwfn,
				    const u8 * const bin_ptr);

/**
 * qed_read_regs(): Reads registers into a buffer (using GRC).
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf: Destination buffer.
 * @addr: Source GRC address in dwords.
 * @len: Number of registers to read.
 *
 * Return: Void.
 */
void qed_read_regs(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u32 *buf, u32 addr, u32 len);

/**
 * qed_read_fw_info(): Reads FW info from the chip.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @fw_info: (Out) a pointer to write the FW info into.
 *
 * Return: True if the FW info was read successfully from one of the Storms,
 * or false if all Storms are in reset.
 *
 * The FW info contains FW-related information, such as the FW version,
 * FW image (main/L2B/kuku), FW timestamp, etc.
 * The FW info is read from the internal RAM of the first Storm that is not in
 * reset.
 */
bool qed_read_fw_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, struct fw_info *fw_info);
/**
 * qed_dbg_grc_config(): Sets the value of a GRC parameter.
 *
 * @p_hwfn: HW device data.
 * @grc_param: GRC parameter.
 * @val: Value to set.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - Grc_param is invalid.
 *         - Val is outside the allowed boundaries.
 */
enum dbg_status qed_dbg_grc_config(struct qed_hwfn *p_hwfn,
				   enum dbg_grc_params grc_param, u32 val);

/**
 * qed_dbg_grc_set_params_default(): Reverts all GRC parameters to their
 *                                   default value.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_dbg_grc_set_params_default(struct qed_hwfn *p_hwfn);
/**
 * qed_dbg_grc_get_dump_buf_size(): Returns the required buffer size for
 *                                  GRC Dump.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) required buffer size (in dwords) for the GRC Dump
 *             data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_grc_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size);

/**
 * qed_dbg_grc_dump(): Dumps GRC data into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the collected GRC data into.
 * @buf_size_in_dwords:Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified dump buffer is too small.
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_grc_dump(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf,
				 u32 buf_size_in_dwords,
				 u32 *num_dumped_dwords);

/**
 * qed_dbg_idle_chk_get_dump_buf_size(): Returns the required buffer size
 *                                       for idle check results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) required buffer size (in dwords) for the idle check
 *             data.
 *
 * return: Error if one of the following holds:
 *        - The version wasn't set.
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_idle_chk_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_idle_chk_dump: Performs idle check and writes the results
 *                        into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the idle check data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The specified buffer is too small.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_idle_chk_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_mcp_trace_get_dump_buf_size(): Returns the required buffer size
 *                                        for mcp trace results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for mcp trace data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The trace data in MCP scratchpad contain an invalid signature.
 *         - The bundle ID in NVRAM is invalid.
 *         - The trace meta data cannot be found (in NVRAM or image file).
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_mcp_trace_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						    struct qed_ptt *p_ptt,
						    u32 *buf_size);

/**
 * qed_dbg_mcp_trace_dump(): Performs mcp trace and writes the results
 *                           into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the mcp trace data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified buffer is too small.
 *        - The trace data in MCP scratchpad contain an invalid signature.
 *        - The bundle ID in NVRAM is invalid.
 *        - The trace meta data cannot be found (in NVRAM or image file).
 *        - The trace meta data cannot be read (from NVRAM or image file).
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_mcp_trace_dump(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       u32 *dump_buf,
				       u32 buf_size_in_dwords,
				       u32 *num_dumped_dwords);

/**
 * qed_dbg_reg_fifo_get_dump_buf_size(): Returns the required buffer size
 *                                       for grc trace fifo results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for reg fifo data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_reg_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_reg_fifo_dump(): Reads the reg fifo and writes the results into
 *                          the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the reg fifo data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified buffer is too small.
 *        - DMAE transaction failed.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_reg_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_igu_fifo_get_dump_buf_size(): Returns the required buffer size
 *                                       for the IGU fifo results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for the IGU fifo
 *            data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_igu_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_igu_fifo_dump(): Reads the IGU fifo and writes the results into
 *                          the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the IGU fifo data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *         - The specified buffer is too small
 *         - DMAE transaction failed
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_igu_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_protection_override_get_dump_buf_size(): Returns the required
 *        buffer size for protection override window results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for protection
 *             override data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status
qed_dbg_protection_override_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size);
/**
 * qed_dbg_protection_override_dump(): Reads protection override window
 *       entries and writes the results into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the protection override data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * @return: Error if one of the following holds:
 *          - The version wasn't set.
 *          - The specified buffer is too small.
 *          - DMAE transaction failed.
 *             Otherwise, returns ok.
 */
enum dbg_status qed_dbg_protection_override_dump(struct qed_hwfn *p_hwfn,
						 struct qed_ptt *p_ptt,
						 u32 *dump_buf,
						 u32 buf_size_in_dwords,
						 u32 *num_dumped_dwords);
/**
 * qed_dbg_fw_asserts_get_dump_buf_size(): Returns the required buffer
 *                                         size for FW Asserts results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for FW Asserts data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_fw_asserts_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						     struct qed_ptt *p_ptt,
						     u32 *buf_size);
/**
 * qed_dbg_fw_asserts_dump(): Reads the FW Asserts and writes the results
 *                            into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the FW Asserts data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The specified buffer is too small.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_fw_asserts_dump(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					u32 *dump_buf,
					u32 buf_size_in_dwords,
					u32 *num_dumped_dwords);

/**
 * qed_dbg_read_attn(): Reads the attention registers of the specified
 * block and type, and writes the results into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @block: Block ID.
 * @attn_type: Attention type.
 * @clear_status: Indicates if the attention status should be cleared.
 * @results:  (OUT) Pointer to write the read results into.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_read_attn(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  enum block_id block,
				  enum dbg_attn_type attn_type,
				  bool clear_status,
				  struct dbg_attn_block_result *results);

/**
 * qed_dbg_print_attn(): Prints attention registers values in the
 *                       specified results struct.
 *
 * @p_hwfn: HW device data.
 * @results: Pointer to the attention read results
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_print_attn(struct qed_hwfn *p_hwfn,
				   struct dbg_attn_block_result *results);

/******************************* Data Types **********************************/

struct mcp_trace_format {
	u32 data;
#define MCP_TRACE_FORMAT_MODULE_MASK	0x0000ffff
#define MCP_TRACE_FORMAT_MODULE_OFFSET	0
#define MCP_TRACE_FORMAT_LEVEL_MASK	0x00030000
#define MCP_TRACE_FORMAT_LEVEL_OFFSET	16
#define MCP_TRACE_FORMAT_P1_SIZE_MASK	0x000c0000
#define MCP_TRACE_FORMAT_P1_SIZE_OFFSET 18
#define MCP_TRACE_FORMAT_P2_SIZE_MASK	0x00300000
#define MCP_TRACE_FORMAT_P2_SIZE_OFFSET 20
#define MCP_TRACE_FORMAT_P3_SIZE_MASK	0x00c00000
#define MCP_TRACE_FORMAT_P3_SIZE_OFFSET 22
#define MCP_TRACE_FORMAT_LEN_MASK	0xff000000
#define MCP_TRACE_FORMAT_LEN_OFFSET	24

	char *format_str;
};

/* MCP Trace Meta data structure */
struct mcp_trace_meta {
	u32 modules_num;
	char **modules;
	u32 formats_num;
	struct mcp_trace_format *formats;
	bool is_allocated;
};

/* Debug Tools user data */
struct dbg_tools_user_data {
	struct mcp_trace_meta mcp_trace_meta;
	const u32 *mcp_trace_user_meta_buf;
};

/******************************** Constants **********************************/

#define MAX_NAME_LEN	16

/***************************** Public Functions *******************************/

/**
 * qed_dbg_user_set_bin_ptr(): Sets a pointer to the binary data with
 *                             debug arrays.
 *
 * @p_hwfn: HW device data.
 * @bin_ptr: a pointer to the binary data with debug arrays.
 *
 * Return: dbg_status.
 */
enum dbg_status qed_dbg_user_set_bin_ptr(struct qed_hwfn *p_hwfn,
					 const u8 * const bin_ptr);

/**
 * qed_dbg_alloc_user_data(): Allocates user debug data.
 *
 * @p_hwfn: HW device data.
 * @user_data_ptr: (OUT) a pointer to the allocated memory.
 *
 * Return: dbg_status.
 */
enum dbg_status qed_dbg_alloc_user_data(struct qed_hwfn *p_hwfn,
					void **user_data_ptr);

/**
 * qed_dbg_get_status_str(): Returns a string for the specified status.
 *
 * @status: A debug status code.
 *
 * Return: A string for the specified status.
 */
const char *qed_dbg_get_status_str(enum dbg_status status);

/**
 * qed_get_idle_chk_results_buf_size(): Returns the required buffer size
 *                                      for idle check results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: idle check dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_idle_chk_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32  num_dumped_dwords,
						  u32 *results_buf_size);
/**
 * qed_print_idle_chk_results(): Prints idle check results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: idle check dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf: buffer for printing the idle check results.
 * @num_errors: (OUT) number of errors found in idle check.
 * @num_warnings: (OUT) number of warnings found in idle check.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_idle_chk_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf,
					   u32 *num_errors,
					   u32 *num_warnings);

/**
 * qed_dbg_mcp_trace_set_meta_data(): Sets the MCP Trace meta data.
 *
 * @p_hwfn: HW device data.
 * @meta_buf: Meta buffer.
 *
 * Return: Void.
 *
 * Needed in case the MCP Trace dump doesn't contain the meta data (e.g. due to
 * no NVRAM access).
 */
void qed_dbg_mcp_trace_set_meta_data(struct qed_hwfn *p_hwfn,
				     const u32 *meta_buf);

/**
 * qed_get_mcp_trace_results_buf_size(): Returns the required buffer size
 *                                       for MCP Trace results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP Trace dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Rrror if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_mcp_trace_results_buf_size(struct qed_hwfn *p_hwfn,
						   u32 *dump_buf,
						   u32 num_dumped_dwords,
						   u32 *results_buf_size);

/**
 * qed_print_mcp_trace_results(): Prints MCP Trace results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP trace dump buffer, starting from the header.
 * @num_dumped_dwords: Member of dwords that were dumped.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_results(struct qed_hwfn *p_hwfn,
					    u32 *dump_buf,
					    u32 num_dumped_dwords,
					    char *results_buf);

/**
 * qed_print_mcp_trace_results_cont(): Prints MCP Trace results, and
 * keeps the MCP trace meta data allocated, to support continuous MCP Trace
 * parsing. After the continuous parsing ends, mcp_trace_free_meta_data should
 * be called to free the meta data.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MVP trace dump buffer, starting from the header.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_results_cont(struct qed_hwfn *p_hwfn,
						 u32 *dump_buf,
						 char *results_buf);

/**
 * qed_print_mcp_trace_line(): Prints MCP Trace results for a single line
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP trace dump buffer, starting from the header.
 * @num_dumped_bytes: Number of bytes that were dumped.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_line(struct qed_hwfn *p_hwfn,
					 u8 *dump_buf,
					 u32 num_dumped_bytes,
					 char *results_buf);

/**
 * qed_mcp_trace_free_meta_data(): Frees the MCP Trace meta data.
 * Should be called after continuous MCP Trace parsing.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_mcp_trace_free_meta_data(struct qed_hwfn *p_hwfn);

/**
 * qed_get_reg_fifo_results_buf_size(): Returns the required buffer size
 *                                      for reg_fifo results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Reg fifo dump buffer.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                     results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_reg_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size);

/**
 * qed_print_reg_fifo_results(): Prints reg fifo results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Reg fifo dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the reg fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_reg_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf);

/**
 * qed_get_igu_fifo_results_buf_size(): Returns the required buffer size
 *                                      for igu_fifo results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: IGU fifo dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_igu_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size);

/**
 * qed_print_igu_fifo_results(): Prints IGU fifo results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: IGU fifo dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the IGU fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_igu_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf);

/**
 * qed_get_protection_override_results_buf_size(): Returns the required
 *         buffer size for protection override results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Protection override dump buffer.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status
qed_get_protection_override_results_buf_size(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     u32 *results_buf_size);

/**
 * qed_print_protection_override_results(): Prints protection override
 *                                          results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Protection override dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the reg fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_protection_override_results(struct qed_hwfn *p_hwfn,
						      u32 *dump_buf,
						      u32 num_dumped_dwords,
						      char *results_buf);

/**
 * qed_get_fw_asserts_results_buf_size(): Returns the required buffer size
 *                                        for FW Asserts results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: FW Asserts dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_fw_asserts_results_buf_size(struct qed_hwfn *p_hwfn,
						    u32 *dump_buf,
						    u32 num_dumped_dwords,
						    u32 *results_buf_size);

/**
 * qed_print_fw_asserts_results(): Prints FW Asserts results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: FW Asserts dump buffer, starting from the header.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf: buffer for printing the FW Asserts results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_fw_asserts_results(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     char *results_buf);

/**
 * qed_dbg_parse_attn(): Parses and prints attention registers values in
 *                      the specified results struct.
 *
 * @p_hwfn: HW device data.
 * @results: Pointer to the attention read results
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_parse_attn(struct qed_hwfn *p_hwfn,
				   struct dbg_attn_block_result *results);

/* Win 2 */
#define GTT_BAR0_MAP_REG_IGU_CMD	0x00f000UL

/* Win 3 */
#define GTT_BAR0_MAP_REG_TSDM_RAM	0x010000UL

/* Win 4 */
#define GTT_BAR0_MAP_REG_MSDM_RAM	0x011000UL

/* Win 5 */
#define GTT_BAR0_MAP_REG_MSDM_RAM_1024	0x012000UL

/* Win 6 */
#define GTT_BAR0_MAP_REG_MSDM_RAM_2048	0x013000UL

/* Win 7 */
#define GTT_BAR0_MAP_REG_USDM_RAM	0x014000UL

/* Win 8 */
#define GTT_BAR0_MAP_REG_USDM_RAM_1024	0x015000UL

/* Win 9 */
#define GTT_BAR0_MAP_REG_USDM_RAM_2048	0x016000UL

/* Win 10 */
#define GTT_BAR0_MAP_REG_XSDM_RAM	0x017000UL

/* Win 11 */
#define GTT_BAR0_MAP_REG_XSDM_RAM_1024	0x018000UL

/* Win 12 */
#define GTT_BAR0_MAP_REG_YSDM_RAM	0x019000UL

/* Win 13 */
#define GTT_BAR0_MAP_REG_PSDM_RAM	0x01a000UL

/**
 * qed_qm_pf_mem_size(): Prepare QM ILT sizes.
 *
 * @num_pf_cids: Number of connections used by this PF.
 * @num_vf_cids: Number of connections used by VFs of this PF.
 * @num_tids: Number of tasks used by this PF.
 * @num_pf_pqs: Number of PQs used by this PF.
 * @num_vf_pqs: Number of PQs used by VFs of this PF.
 *
 * Return: The required host memory size in 4KB units.
 *
 * Returns the required host memory size in 4KB units.
 * Must be called before all QM init HSI functions.
 */
u32 qed_qm_pf_mem_size(u32 num_pf_cids,
		       u32 num_vf_cids,
		       u32 num_tids, u16 num_pf_pqs, u16 num_vf_pqs);

struct qed_qm_common_rt_init_params {
	u8 max_ports_per_engine;
	u8 max_phys_tcs_per_port;
	bool pf_rl_en;
	bool pf_wfq_en;
	bool global_rl_en;
	bool vport_wfq_en;
	struct init_qm_port_params *port_params;
};

int qed_qm_common_rt_init(struct qed_hwfn *p_hwfn,
			  struct qed_qm_common_rt_init_params *p_params);

struct qed_qm_pf_rt_init_params {
	u8 port_id;
	u8 pf_id;
	u8 max_phys_tcs_per_port;
	bool is_pf_loading;
	u32 num_pf_cids;
	u32 num_vf_cids;
	u32 num_tids;
	u16 start_pq;
	u16 num_pf_pqs;
	u16 num_vf_pqs;
	u16 start_vport;
	u16 num_vports;
	u16 pf_wfq;
	u32 pf_rl;
	struct init_qm_pq_params *pq_params;
	struct init_qm_vport_params *vport_params;
};

int qed_qm_pf_rt_init(struct qed_hwfn *p_hwfn,
	struct qed_ptt *p_ptt,
	struct qed_qm_pf_rt_init_params *p_params);

/**
 * qed_init_pf_wfq(): Initializes the WFQ weight of the specified PF.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers
 * @pf_id: PF ID
 * @pf_wfq: WFQ weight. Must be non-zero.
 *
 * Return: 0 on success, -1 on error.
 */
int qed_init_pf_wfq(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, u8 pf_id, u16 pf_wfq);

/**
 * qed_init_pf_rl(): Initializes the rate limit of the specified PF
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @pf_id: PF ID.
 * @pf_rl: rate limit in Mb/sec units
 *
 * Return: 0 on success, -1 on error.
 */
int qed_init_pf_rl(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u8 pf_id, u32 pf_rl);

/**
 * qed_init_vport_wfq(): Initializes the WFQ weight of the specified VPORT
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers
 * @first_tx_pq_id: An array containing the first Tx PQ ID associated
 *                  with the VPORT for each TC. This array is filled by
 *                  qed_qm_pf_rt_init
 * @wfq: WFQ weight. Must be non-zero.
 *
 * Return: 0 on success, -1 on error.
 */
int qed_init_vport_wfq(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u16 first_tx_pq_id[NUM_OF_TCS], u16 wfq);

/**
 * qed_init_global_rl():  Initializes the rate limit of the specified
 * rate limiter.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @rl_id: RL ID.
 * @rate_limit: Rate limit in Mb/sec units
 *
 * Return: 0 on success, -1 on error.
 */
int qed_init_global_rl(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u16 rl_id, u32 rate_limit);

/**
 * qed_send_qm_stop_cmd(): Sends a stop command to the QM.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @is_release_cmd: true for release, false for stop.
 * @is_tx_pq: true for Tx PQs, false for Other PQs.
 * @start_pq: first PQ ID to stop
 * @num_pqs: Number of PQs to stop, starting from start_pq.
 *
 * Return: Bool, true if successful, false if timeout occurred while waiting
 *         for QM command done.
 */
bool qed_send_qm_stop_cmd(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  bool is_release_cmd,
			  bool is_tx_pq, u16 start_pq, u16 num_pqs);

/**
 * qed_set_vxlan_dest_port(): Initializes vxlan tunnel destination udp port.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dest_port: vxlan destination udp port.
 *
 * Return: Void.
 */
void qed_set_vxlan_dest_port(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u16 dest_port);

/**
 * qed_set_vxlan_enable(): Enable or disable VXLAN tunnel in HW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @vxlan_enable: vxlan enable flag.
 *
 * Return: Void.
 */
void qed_set_vxlan_enable(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, bool vxlan_enable);

/**
 * qed_set_gre_enable(): Enable or disable GRE tunnel in HW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @eth_gre_enable: Eth GRE enable flag.
 * @ip_gre_enable: IP GRE enable flag.
 *
 * Return: Void.
 */
void qed_set_gre_enable(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			bool eth_gre_enable, bool ip_gre_enable);

/**
 * qed_set_geneve_dest_port(): Initializes geneve tunnel destination udp port
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dest_port: Geneve destination udp port.
 *
 * Retur: Void.
 */
void qed_set_geneve_dest_port(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt, u16 dest_port);

/**
 * qed_set_geneve_enable(): Enable or disable GRE tunnel in HW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @eth_geneve_enable: Eth GENEVE enable flag.
 * @ip_geneve_enable: IP GENEVE enable flag.
 *
 * Return: Void.
 */
void qed_set_geneve_enable(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt,
			   bool eth_geneve_enable, bool ip_geneve_enable);

void qed_set_vxlan_no_l2_enable(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt, bool enable);

/**
 * qed_gft_disable(): Disable GFT.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @pf_id: PF on which to disable GFT.
 *
 * Return: Void.
 */
void qed_gft_disable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, u16 pf_id);

/**
 * qed_gft_config(): Enable and configure HW for GFT.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @pf_id: PF on which to enable GFT.
 * @tcp: Set profile tcp packets.
 * @udp: Set profile udp  packet.
 * @ipv4: Set profile ipv4 packet.
 * @ipv6: Set profile ipv6 packet.
 * @profile_type: Define packet same fields. Use enum gft_profile_type.
 *
 * Return: Void.
 */
void qed_gft_config(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    u16 pf_id,
		    bool tcp,
		    bool udp,
		    bool ipv4, bool ipv6, enum gft_profile_type profile_type);

/**
 * qed_enable_context_validation(): Enable and configure context
 *                                  validation.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 *
 * Return: Void.
 */
void qed_enable_context_validation(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt);

/**
 * qed_calc_session_ctx_validation(): Calcualte validation byte for
 *                                    session context.
 *
 * @p_ctx_mem: Pointer to context memory.
 * @ctx_size: Context size.
 * @ctx_type: Context type.
 * @cid: Context cid.
 *
 * Return: Void.
 */
void qed_calc_session_ctx_validation(void *p_ctx_mem,
				     u16 ctx_size, u8 ctx_type, u32 cid);

/**
 * qed_calc_task_ctx_validation(): Calcualte validation byte for task
 *                                 context.
 *
 * @p_ctx_mem: Pointer to context memory.
 * @ctx_size: Context size.
 * @ctx_type: Context type.
 * @tid: Context tid.
 *
 * Return: Void.
 */
void qed_calc_task_ctx_validation(void *p_ctx_mem,
				  u16 ctx_size, u8 ctx_type, u32 tid);

/**
 * qed_memset_session_ctx(): Memset session context to 0 while
 *                            preserving validation bytes.
 *
 * @p_ctx_mem: Pointer to context memory.
 * @ctx_size: Size to initialzie.
 * @ctx_type: Context type.
 *
 * Return: Void.
 */
void qed_memset_session_ctx(void *p_ctx_mem, u32 ctx_size, u8 ctx_type);

/**
 * qed_memset_task_ctx(): Memset task context to 0 while preserving
 *                        validation bytes.
 *
 * @p_ctx_mem: Pointer to context memory.
 * @ctx_size: size to initialzie.
 * @ctx_type: context type.
 *
 * Return: Void.
 */
void qed_memset_task_ctx(void *p_ctx_mem, u32 ctx_size, u8 ctx_type);

#define NUM_STORMS 6

/**
 * qed_set_rdma_error_level(): Sets the RDMA assert level.
 *                             If the severity of the error will be
 *                             above the level, the FW will assert.
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @assert_level: An array of assert levels for each storm.
 *
 * Return: Void.
 */
void qed_set_rdma_error_level(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      u8 assert_level[NUM_STORMS]);
/**
 * qed_fw_overlay_mem_alloc(): Allocates and fills the FW overlay memory.
 *
 * @p_hwfn: HW device data.
 * @fw_overlay_in_buf: The input FW overlay buffer.
 * @buf_size_in_bytes: The size of the input FW overlay buffer in bytes.
 *		        must be aligned to dwords.
 *
 * Return: A pointer to the allocated overlays memory,
 * or NULL in case of failures.
 */
struct phys_mem_desc *
qed_fw_overlay_mem_alloc(struct qed_hwfn *p_hwfn,
			 const u32 *const fw_overlay_in_buf,
			 u32 buf_size_in_bytes);

/**
 * qed_fw_overlay_init_ram(): Initializes the FW overlay RAM.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @fw_overlay_mem: the allocated FW overlay memory.
 *
 * Return: Void.
 */
void qed_fw_overlay_init_ram(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt,
			     struct phys_mem_desc *fw_overlay_mem);

/**
 * qed_fw_overlay_mem_free(): Frees the FW overlay memory.
 *
 * @p_hwfn: HW device data.
 * @fw_overlay_mem: The allocated FW overlay memory to free.
 *
 * Return: Void.
 */
void qed_fw_overlay_mem_free(struct qed_hwfn *p_hwfn,
			     struct phys_mem_desc *fw_overlay_mem);

/* Ystorm flow control mode. Use enum fw_flow_ctrl_mode */
#define YSTORM_FLOW_CONTROL_MODE_OFFSET			(IRO[0].base)
#define YSTORM_FLOW_CONTROL_MODE_SIZE			(IRO[0].size)

/* Tstorm port statistics */
#define TSTORM_PORT_STAT_OFFSET(port_id) \
	(IRO[1].base + ((port_id) * IRO[1].m1))
#define TSTORM_PORT_STAT_SIZE				(IRO[1].size)

/* Tstorm ll2 port statistics */
#define TSTORM_LL2_PORT_STAT_OFFSET(port_id) \
	(IRO[2].base + ((port_id) * IRO[2].m1))
#define TSTORM_LL2_PORT_STAT_SIZE			(IRO[2].size)

/* Ustorm VF-PF Channel ready flag */
#define USTORM_VF_PF_CHANNEL_READY_OFFSET(vf_id) \
	(IRO[3].base + ((vf_id) * IRO[3].m1))
#define USTORM_VF_PF_CHANNEL_READY_SIZE			(IRO[3].size)

/* Ustorm Final flr cleanup ack */
#define USTORM_FLR_FINAL_ACK_OFFSET(pf_id) \
	(IRO[4].base + ((pf_id) * IRO[4].m1))
#define USTORM_FLR_FINAL_ACK_SIZE			(IRO[4].size)

/* Ustorm Event ring consumer */
#define USTORM_EQE_CONS_OFFSET(pf_id) \
	(IRO[5].base + ((pf_id) * IRO[5].m1))
#define USTORM_EQE_CONS_SIZE				(IRO[5].size)

/* Ustorm eth queue zone */
#define USTORM_ETH_QUEUE_ZONE_OFFSET(queue_zone_id) \
	(IRO[6].base + ((queue_zone_id) * IRO[6].m1))
#define USTORM_ETH_QUEUE_ZONE_SIZE			(IRO[6].size)

/* Ustorm Common Queue ring consumer */
#define USTORM_COMMON_QUEUE_CONS_OFFSET(queue_zone_id) \
	(IRO[7].base + ((queue_zone_id) * IRO[7].m1))
#define USTORM_COMMON_QUEUE_CONS_SIZE			(IRO[7].size)

/* Xstorm common PQ info */
#define XSTORM_PQ_INFO_OFFSET(pq_id) \
	(IRO[8].base + ((pq_id) * IRO[8].m1))
#define XSTORM_PQ_INFO_SIZE				(IRO[8].size)

/* Xstorm Integration Test Data */
#define XSTORM_INTEG_TEST_DATA_OFFSET			(IRO[9].base)
#define XSTORM_INTEG_TEST_DATA_SIZE			(IRO[9].size)

/* Ystorm Integration Test Data */
#define YSTORM_INTEG_TEST_DATA_OFFSET			(IRO[10].base)
#define YSTORM_INTEG_TEST_DATA_SIZE			(IRO[10].size)

/* Pstorm Integration Test Data */
#define PSTORM_INTEG_TEST_DATA_OFFSET			(IRO[11].base)
#define PSTORM_INTEG_TEST_DATA_SIZE			(IRO[11].size)

/* Tstorm Integration Test Data */
#define TSTORM_INTEG_TEST_DATA_OFFSET			(IRO[12].base)
#define TSTORM_INTEG_TEST_DATA_SIZE			(IRO[12].size)

/* Mstorm Integration Test Data */
#define MSTORM_INTEG_TEST_DATA_OFFSET			(IRO[13].base)
#define MSTORM_INTEG_TEST_DATA_SIZE			(IRO[13].size)

/* Ustorm Integration Test Data */
#define USTORM_INTEG_TEST_DATA_OFFSET			(IRO[14].base)
#define USTORM_INTEG_TEST_DATA_SIZE			(IRO[14].size)

/* Xstorm overlay buffer host address */
#define XSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[15].base)
#define XSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[15].size)

/* Ystorm overlay buffer host address */
#define YSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[16].base)
#define YSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[16].size)

/* Pstorm overlay buffer host address */
#define PSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[17].base)
#define PSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[17].size)

/* Tstorm overlay buffer host address */
#define TSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[18].base)
#define TSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[18].size)

/* Mstorm overlay buffer host address */
#define MSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[19].base)
#define MSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[19].size)

/* Ustorm overlay buffer host address */
#define USTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[20].base)
#define USTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[20].size)

/* Tstorm producers */
#define TSTORM_LL2_RX_PRODS_OFFSET(core_rx_queue_id) \
	(IRO[21].base + ((core_rx_queue_id) * IRO[21].m1))
#define TSTORM_LL2_RX_PRODS_SIZE			(IRO[21].size)

/* Tstorm LightL2 queue statistics */
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id) \
	(IRO[22].base + ((core_rx_queue_id) * IRO[22].m1))
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_SIZE		(IRO[22].size)

/* Ustorm LiteL2 queue statistics */
#define CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id) \
	(IRO[23].base + ((core_rx_queue_id) * IRO[23].m1))
#define CORE_LL2_USTORM_PER_QUEUE_STAT_SIZE		(IRO[23].size)

/* Pstorm LiteL2 queue statistics */
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(core_tx_stats_id) \
	(IRO[24].base + ((core_tx_stats_id) * IRO[24].m1))
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_SIZE		(IRO[24].size)

/* Mstorm queue statistics */
#define MSTORM_QUEUE_STAT_OFFSET(stat_counter_id) \
	(IRO[25].base + ((stat_counter_id) * IRO[25].m1))
#define MSTORM_QUEUE_STAT_SIZE				(IRO[25].size)

/* TPA agregation timeout in us resolution (on ASIC) */
#define MSTORM_TPA_TIMEOUT_US_OFFSET			(IRO[26].base)
#define MSTORM_TPA_TIMEOUT_US_SIZE			(IRO[26].size)

/* Mstorm ETH VF queues producers offset in RAM. Used in default VF zone size
 * mode
 */
#define MSTORM_ETH_VF_PRODS_OFFSET(vf_id, vf_queue_id) \
	(IRO[27].base + ((vf_id) * IRO[27].m1) + ((vf_queue_id) * IRO[27].m2))
#define MSTORM_ETH_VF_PRODS_SIZE			(IRO[27].size)

/* Mstorm ETH PF queues producers */
#define MSTORM_ETH_PF_PRODS_OFFSET(queue_id) \
	(IRO[28].base + ((queue_id) * IRO[28].m1))
#define MSTORM_ETH_PF_PRODS_SIZE			(IRO[28].size)

/* Mstorm pf statistics */
#define MSTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[29].base + ((pf_id) * IRO[29].m1))
#define MSTORM_ETH_PF_STAT_SIZE				(IRO[29].size)

/* Ustorm queue statistics */
#define USTORM_QUEUE_STAT_OFFSET(stat_counter_id) \
	(IRO[30].base + ((stat_counter_id) * IRO[30].m1))
#define USTORM_QUEUE_STAT_SIZE				(IRO[30].size)

/* Ustorm pf statistics */
#define USTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[31].base + ((pf_id) * IRO[31].m1))
#define USTORM_ETH_PF_STAT_SIZE				(IRO[31].size)

/* Pstorm queue statistics */
#define PSTORM_QUEUE_STAT_OFFSET(stat_counter_id)	\
	(IRO[32].base + ((stat_counter_id) * IRO[32].m1))
#define PSTORM_QUEUE_STAT_SIZE				(IRO[32].size)

/* Pstorm pf statistics */
#define PSTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[33].base + ((pf_id) * IRO[33].m1))
#define PSTORM_ETH_PF_STAT_SIZE				(IRO[33].size)

/* Control frame's EthType configuration for TX control frame security */
#define PSTORM_CTL_FRAME_ETHTYPE_OFFSET(eth_type_id)	\
	(IRO[34].base + ((eth_type_id) * IRO[34].m1))
#define PSTORM_CTL_FRAME_ETHTYPE_SIZE			(IRO[34].size)

/* Tstorm last parser message */
#define TSTORM_ETH_PRS_INPUT_OFFSET			(IRO[35].base)
#define TSTORM_ETH_PRS_INPUT_SIZE			(IRO[35].size)

/* Tstorm Eth limit Rx rate */
#define ETH_RX_RATE_LIMIT_OFFSET(pf_id)	\
	(IRO[36].base + ((pf_id) * IRO[36].m1))
#define ETH_RX_RATE_LIMIT_SIZE				(IRO[36].size)

/* RSS indirection table entry update command per PF offset in TSTORM PF BAR0.
 * Use eth_tstorm_rss_update_data for update
 */
#define TSTORM_ETH_RSS_UPDATE_OFFSET(pf_id) \
	(IRO[37].base + ((pf_id) * IRO[37].m1))
#define TSTORM_ETH_RSS_UPDATE_SIZE			(IRO[37].size)

/* Xstorm queue zone */
#define XSTORM_ETH_QUEUE_ZONE_OFFSET(queue_id) \
	(IRO[38].base + ((queue_id) * IRO[38].m1))
#define XSTORM_ETH_QUEUE_ZONE_SIZE			(IRO[38].size)

/* Ystorm cqe producer */
#define YSTORM_TOE_CQ_PROD_OFFSET(rss_id) \
	(IRO[39].base + ((rss_id) * IRO[39].m1))
#define YSTORM_TOE_CQ_PROD_SIZE				(IRO[39].size)

/* Ustorm cqe producer */
#define USTORM_TOE_CQ_PROD_OFFSET(rss_id) \
	(IRO[40].base + ((rss_id) * IRO[40].m1))
#define USTORM_TOE_CQ_PROD_SIZE				(IRO[40].size)

/* Ustorm grq producer */
#define USTORM_TOE_GRQ_PROD_OFFSET(pf_id) \
	(IRO[41].base + ((pf_id) * IRO[41].m1))
#define USTORM_TOE_GRQ_PROD_SIZE			(IRO[41].size)

/* Tstorm cmdq-cons of given command queue-id */
#define TSTORM_SCSI_CMDQ_CONS_OFFSET(cmdq_queue_id) \
	(IRO[42].base + ((cmdq_queue_id) * IRO[42].m1))
#define TSTORM_SCSI_CMDQ_CONS_SIZE			(IRO[42].size)

/* Tstorm (reflects M-Storm) bdq-external-producer of given function ID,
 * BDqueue-id
 */
#define TSTORM_SCSI_BDQ_EXT_PROD_OFFSET(storage_func_id, bdq_id) \
	(IRO[43].base + ((storage_func_id) * IRO[43].m1) + \
	 ((bdq_id) * IRO[43].m2))
#define TSTORM_SCSI_BDQ_EXT_PROD_SIZE			(IRO[43].size)

/* Mstorm bdq-external-producer of given BDQ resource ID, BDqueue-id */
#define MSTORM_SCSI_BDQ_EXT_PROD_OFFSET(storage_func_id, bdq_id) \
	(IRO[44].base + ((storage_func_id) * IRO[44].m1) + \
	 ((bdq_id) * IRO[44].m2))
#define MSTORM_SCSI_BDQ_EXT_PROD_SIZE			(IRO[44].size)

/* Tstorm iSCSI RX stats */
#define TSTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[45].base + ((storage_func_id) * IRO[45].m1))
#define TSTORM_ISCSI_RX_STATS_SIZE			(IRO[45].size)

/* Mstorm iSCSI RX stats */
#define MSTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[46].base + ((storage_func_id) * IRO[46].m1))
#define MSTORM_ISCSI_RX_STATS_SIZE			(IRO[46].size)

/* Ustorm iSCSI RX stats */
#define USTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[47].base + ((storage_func_id) * IRO[47].m1))
#define USTORM_ISCSI_RX_STATS_SIZE			(IRO[47].size)

/* Xstorm iSCSI TX stats */
#define XSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[48].base + ((storage_func_id) * IRO[48].m1))
#define XSTORM_ISCSI_TX_STATS_SIZE			(IRO[48].size)

/* Ystorm iSCSI TX stats */
#define YSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[49].base + ((storage_func_id) * IRO[49].m1))
#define YSTORM_ISCSI_TX_STATS_SIZE			(IRO[49].size)

/* Pstorm iSCSI TX stats */
#define PSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[50].base + ((storage_func_id) * IRO[50].m1))
#define PSTORM_ISCSI_TX_STATS_SIZE			(IRO[50].size)

/* Tstorm FCoE RX stats */
#define TSTORM_FCOE_RX_STATS_OFFSET(pf_id) \
	(IRO[51].base + ((pf_id) * IRO[51].m1))
#define TSTORM_FCOE_RX_STATS_SIZE			(IRO[51].size)

/* Pstorm FCoE TX stats */
#define PSTORM_FCOE_TX_STATS_OFFSET(pf_id) \
	(IRO[52].base + ((pf_id) * IRO[52].m1))
#define PSTORM_FCOE_TX_STATS_SIZE			(IRO[52].size)

/* Pstorm RDMA queue statistics */
#define PSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id) \
	(IRO[53].base + ((rdma_stat_counter_id) * IRO[53].m1))
#define PSTORM_RDMA_QUEUE_STAT_SIZE			(IRO[53].size)

/* Tstorm RDMA queue statistics */
#define TSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id) \
	(IRO[54].base + ((rdma_stat_counter_id) * IRO[54].m1))
#define TSTORM_RDMA_QUEUE_STAT_SIZE			(IRO[54].size)

/* Xstorm error level for assert */
#define XSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[55].base + ((pf_id) * IRO[55].m1))
#define XSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[55].size)

/* Ystorm error level for assert */
#define YSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[56].base + ((pf_id) * IRO[56].m1))
#define YSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[56].size)

/* Pstorm error level for assert */
#define PSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[57].base + ((pf_id) * IRO[57].m1))
#define PSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[57].size)

/* Tstorm error level for assert */
#define TSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[58].base + ((pf_id) * IRO[58].m1))
#define TSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[58].size)

/* Mstorm error level for assert */
#define MSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[59].base + ((pf_id) * IRO[59].m1))
#define MSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[59].size)

/* Ustorm error level for assert */
#define USTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[60].base + ((pf_id) * IRO[60].m1))
#define USTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[60].size)

/* Xstorm iWARP rxmit stats */
#define XSTORM_IWARP_RXMIT_STATS_OFFSET(pf_id) \
	(IRO[61].base + ((pf_id) * IRO[61].m1))
#define XSTORM_IWARP_RXMIT_STATS_SIZE			(IRO[61].size)

/* Tstorm RoCE Event Statistics */
#define TSTORM_ROCE_EVENTS_STAT_OFFSET(roce_pf_id)	\
	(IRO[62].base + ((roce_pf_id) * IRO[62].m1))
#define TSTORM_ROCE_EVENTS_STAT_SIZE			(IRO[62].size)

/* DCQCN Received Statistics */
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_OFFSET(roce_pf_id)\
	(IRO[63].base + ((roce_pf_id) * IRO[63].m1))
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_SIZE		(IRO[63].size)

/* RoCE Error Statistics */
#define YSTORM_ROCE_ERROR_STATS_OFFSET(roce_pf_id)	\
	(IRO[64].base + ((roce_pf_id) * IRO[64].m1))
#define YSTORM_ROCE_ERROR_STATS_SIZE			(IRO[64].size)

/* DCQCN Sent Statistics */
#define PSTORM_ROCE_DCQCN_SENT_STATS_OFFSET(roce_pf_id)	\
	(IRO[65].base + ((roce_pf_id) * IRO[65].m1))
#define PSTORM_ROCE_DCQCN_SENT_STATS_SIZE		(IRO[65].size)

/* RoCE CQEs Statistics */
#define USTORM_ROCE_CQE_STATS_OFFSET(roce_pf_id)	\
	(IRO[66].base + ((roce_pf_id) * IRO[66].m1))
#define USTORM_ROCE_CQE_STATS_SIZE			(IRO[66].size)

/* Runtime array offsets */
#define DORQ_REG_PF_MAX_ICID_0_RT_OFFSET				0
#define DORQ_REG_PF_MAX_ICID_1_RT_OFFSET				1
#define DORQ_REG_PF_MAX_ICID_2_RT_OFFSET				2
#define DORQ_REG_PF_MAX_ICID_3_RT_OFFSET				3
#define DORQ_REG_PF_MAX_ICID_4_RT_OFFSET				4
#define DORQ_REG_PF_MAX_ICID_5_RT_OFFSET				5
#define DORQ_REG_PF_MAX_ICID_6_RT_OFFSET				6
#define DORQ_REG_PF_MAX_ICID_7_RT_OFFSET				7
#define DORQ_REG_VF_MAX_ICID_0_RT_OFFSET				8
#define DORQ_REG_VF_MAX_ICID_1_RT_OFFSET				9
#define DORQ_REG_VF_MAX_ICID_2_RT_OFFSET				10
#define DORQ_REG_VF_MAX_ICID_3_RT_OFFSET				11
#define DORQ_REG_VF_MAX_ICID_4_RT_OFFSET				12
#define DORQ_REG_VF_MAX_ICID_5_RT_OFFSET				13
#define DORQ_REG_VF_MAX_ICID_6_RT_OFFSET				14
#define DORQ_REG_VF_MAX_ICID_7_RT_OFFSET				15
#define DORQ_REG_VF_ICID_BIT_SHIFT_NORM_RT_OFFSET			16
#define DORQ_REG_PF_WAKE_ALL_RT_OFFSET					17
#define DORQ_REG_TAG1_ETHERTYPE_RT_OFFSET				18
#define IGU_REG_PF_CONFIGURATION_RT_OFFSET				19
#define IGU_REG_VF_CONFIGURATION_RT_OFFSET				20
#define IGU_REG_ATTN_MSG_ADDR_L_RT_OFFSET				21
#define IGU_REG_ATTN_MSG_ADDR_H_RT_OFFSET				22
#define IGU_REG_LEADING_EDGE_LATCH_RT_OFFSET				23
#define IGU_REG_TRAILING_EDGE_LATCH_RT_OFFSET				24
#define CAU_REG_CQE_AGG_UNIT_SIZE_RT_OFFSET				25
#define CAU_REG_SB_VAR_MEMORY_RT_OFFSET					26
#define CAU_REG_SB_VAR_MEMORY_RT_SIZE					736
#define CAU_REG_SB_ADDR_MEMORY_RT_OFFSET				762
#define CAU_REG_SB_ADDR_MEMORY_RT_SIZE					736
#define CAU_REG_PI_MEMORY_RT_OFFSET					1498
#define CAU_REG_PI_MEMORY_RT_SIZE					4416
#define PRS_REG_SEARCH_RESP_INITIATOR_TYPE_RT_OFFSET			5914
#define PRS_REG_TASK_ID_MAX_INITIATOR_PF_RT_OFFSET			5915
#define PRS_REG_TASK_ID_MAX_INITIATOR_VF_RT_OFFSET			5916
#define PRS_REG_TASK_ID_MAX_TARGET_PF_RT_OFFSET				5917
#define PRS_REG_TASK_ID_MAX_TARGET_VF_RT_OFFSET				5918
#define PRS_REG_SEARCH_TCP_RT_OFFSET					5919
#define PRS_REG_SEARCH_FCOE_RT_OFFSET					5920
#define PRS_REG_SEARCH_ROCE_RT_OFFSET					5921
#define PRS_REG_ROCE_DEST_QP_MAX_VF_RT_OFFSET				5922
#define PRS_REG_ROCE_DEST_QP_MAX_PF_RT_OFFSET				5923
#define PRS_REG_SEARCH_OPENFLOW_RT_OFFSET				5924
#define PRS_REG_SEARCH_NON_IP_AS_OPENFLOW_RT_OFFSET			5925
#define PRS_REG_OPENFLOW_SUPPORT_ONLY_KNOWN_OVER_IP_RT_OFFSET		5926
#define PRS_REG_OPENFLOW_SEARCH_KEY_MASK_RT_OFFSET			5927
#define PRS_REG_TAG_ETHERTYPE_0_RT_OFFSET				5928
#define PRS_REG_LIGHT_L2_ETHERTYPE_EN_RT_OFFSET				5929
#define SRC_REG_FIRSTFREE_RT_OFFSET					5930
#define SRC_REG_FIRSTFREE_RT_SIZE					2
#define SRC_REG_LASTFREE_RT_OFFSET					5932
#define SRC_REG_LASTFREE_RT_SIZE					2
#define SRC_REG_COUNTFREE_RT_OFFSET					5934
#define SRC_REG_NUMBER_HASH_BITS_RT_OFFSET				5935
#define PSWRQ2_REG_CDUT_P_SIZE_RT_OFFSET				5936
#define PSWRQ2_REG_CDUC_P_SIZE_RT_OFFSET				5937
#define PSWRQ2_REG_TM_P_SIZE_RT_OFFSET					5938
#define PSWRQ2_REG_QM_P_SIZE_RT_OFFSET					5939
#define PSWRQ2_REG_SRC_P_SIZE_RT_OFFSET					5940
#define PSWRQ2_REG_TSDM_P_SIZE_RT_OFFSET				5941
#define PSWRQ2_REG_TM_FIRST_ILT_RT_OFFSET				5942
#define PSWRQ2_REG_TM_LAST_ILT_RT_OFFSET				5943
#define PSWRQ2_REG_QM_FIRST_ILT_RT_OFFSET				5944
#define PSWRQ2_REG_QM_LAST_ILT_RT_OFFSET				5945
#define PSWRQ2_REG_SRC_FIRST_ILT_RT_OFFSET				5946
#define PSWRQ2_REG_SRC_LAST_ILT_RT_OFFSET				5947
#define PSWRQ2_REG_CDUC_FIRST_ILT_RT_OFFSET				5948
#define PSWRQ2_REG_CDUC_LAST_ILT_RT_OFFSET				5949
#define PSWRQ2_REG_CDUT_FIRST_ILT_RT_OFFSET				5950
#define PSWRQ2_REG_CDUT_LAST_ILT_RT_OFFSET				5951
#define PSWRQ2_REG_TSDM_FIRST_ILT_RT_OFFSET				5952
#define PSWRQ2_REG_TSDM_LAST_ILT_RT_OFFSET				5953
#define PSWRQ2_REG_TM_NUMBER_OF_PF_BLOCKS_RT_OFFSET			5954
#define PSWRQ2_REG_CDUT_NUMBER_OF_PF_BLOCKS_RT_OFFSET			5955
#define PSWRQ2_REG_CDUC_NUMBER_OF_PF_BLOCKS_RT_OFFSET			5956
#define PSWRQ2_REG_TM_VF_BLOCKS_RT_OFFSET				5957
#define PSWRQ2_REG_CDUT_VF_BLOCKS_RT_OFFSET				5958
#define PSWRQ2_REG_CDUC_VF_BLOCKS_RT_OFFSET				5959
#define PSWRQ2_REG_TM_BLOCKS_FACTOR_RT_OFFSET				5960
#define PSWRQ2_REG_CDUT_BLOCKS_FACTOR_RT_OFFSET				5961
#define PSWRQ2_REG_CDUC_BLOCKS_FACTOR_RT_OFFSET				5962
#define PSWRQ2_REG_VF_BASE_RT_OFFSET					5963
#define PSWRQ2_REG_VF_LAST_ILT_RT_OFFSET				5964
#define PSWRQ2_REG_DRAM_ALIGN_WR_RT_OFFSET				5965
#define PSWRQ2_REG_DRAM_ALIGN_RD_RT_OFFSET				5966
#define PSWRQ2_REG_ILT_MEMORY_RT_OFFSET					5967
#define PSWRQ2_REG_ILT_MEMORY_RT_SIZE					22000
#define PGLUE_REG_B_VF_BASE_RT_OFFSET					27967
#define PGLUE_REG_B_MSDM_OFFSET_MASK_B_RT_OFFSET			27968
#define PGLUE_REG_B_MSDM_VF_SHIFT_B_RT_OFFSET				27969
#define PGLUE_REG_B_CACHE_LINE_SIZE_RT_OFFSET				27970
#define PGLUE_REG_B_PF_BAR0_SIZE_RT_OFFSET				27971
#define PGLUE_REG_B_PF_BAR1_SIZE_RT_OFFSET				27972
#define PGLUE_REG_B_VF_BAR1_SIZE_RT_OFFSET				27973
#define TM_REG_VF_ENABLE_CONN_RT_OFFSET					27974
#define TM_REG_PF_ENABLE_CONN_RT_OFFSET					27975
#define TM_REG_PF_ENABLE_TASK_RT_OFFSET					27976
#define TM_REG_GROUP_SIZE_RESOLUTION_CONN_RT_OFFSET			27977
#define TM_REG_GROUP_SIZE_RESOLUTION_TASK_RT_OFFSET			27978
#define TM_REG_CONFIG_CONN_MEM_RT_OFFSET				27979
#define TM_REG_CONFIG_CONN_MEM_RT_SIZE					416
#define TM_REG_CONFIG_TASK_MEM_RT_OFFSET				28395
#define TM_REG_CONFIG_TASK_MEM_RT_SIZE					512
#define QM_REG_MAXPQSIZE_0_RT_OFFSET					28907
#define QM_REG_MAXPQSIZE_1_RT_OFFSET					28908
#define QM_REG_MAXPQSIZE_2_RT_OFFSET					28909
#define QM_REG_MAXPQSIZETXSEL_0_RT_OFFSET				28910
#define QM_REG_MAXPQSIZETXSEL_1_RT_OFFSET				28911
#define QM_REG_MAXPQSIZETXSEL_2_RT_OFFSET				28912
#define QM_REG_MAXPQSIZETXSEL_3_RT_OFFSET				28913
#define QM_REG_MAXPQSIZETXSEL_4_RT_OFFSET				28914
#define QM_REG_MAXPQSIZETXSEL_5_RT_OFFSET				28915
#define QM_REG_MAXPQSIZETXSEL_6_RT_OFFSET				28916
#define QM_REG_MAXPQSIZETXSEL_7_RT_OFFSET				28917
#define QM_REG_MAXPQSIZETXSEL_8_RT_OFFSET				28918
#define QM_REG_MAXPQSIZETXSEL_9_RT_OFFSET				28919
#define QM_REG_MAXPQSIZETXSEL_10_RT_OFFSET				28920
#define QM_REG_MAXPQSIZETXSEL_11_RT_OFFSET				28921
#define QM_REG_MAXPQSIZETXSEL_12_RT_OFFSET				28922
#define QM_REG_MAXPQSIZETXSEL_13_RT_OFFSET				28923
#define QM_REG_MAXPQSIZETXSEL_14_RT_OFFSET				28924
#define QM_REG_MAXPQSIZETXSEL_15_RT_OFFSET				28925
#define QM_REG_MAXPQSIZETXSEL_16_RT_OFFSET				28926
#define QM_REG_MAXPQSIZETXSEL_17_RT_OFFSET				28927
#define QM_REG_MAXPQSIZETXSEL_18_RT_OFFSET				28928
#define QM_REG_MAXPQSIZETXSEL_19_RT_OFFSET				28929
#define QM_REG_MAXPQSIZETXSEL_20_RT_OFFSET				28930
#define QM_REG_MAXPQSIZETXSEL_21_RT_OFFSET				28931
#define QM_REG_MAXPQSIZETXSEL_22_RT_OFFSET				28932
#define QM_REG_MAXPQSIZETXSEL_23_RT_OFFSET				28933
#define QM_REG_MAXPQSIZETXSEL_24_RT_OFFSET				28934
#define QM_REG_MAXPQSIZETXSEL_25_RT_OFFSET				28935
#define QM_REG_MAXPQSIZETXSEL_26_RT_OFFSET				28936
#define QM_REG_MAXPQSIZETXSEL_27_RT_OFFSET				28937
#define QM_REG_MAXPQSIZETXSEL_28_RT_OFFSET				28938
#define QM_REG_MAXPQSIZETXSEL_29_RT_OFFSET				28939
#define QM_REG_MAXPQSIZETXSEL_30_RT_OFFSET				28940
#define QM_REG_MAXPQSIZETXSEL_31_RT_OFFSET				28941
#define QM_REG_MAXPQSIZETXSEL_32_RT_OFFSET				28942
#define QM_REG_MAXPQSIZETXSEL_33_RT_OFFSET				28943
#define QM_REG_MAXPQSIZETXSEL_34_RT_OFFSET				28944
#define QM_REG_MAXPQSIZETXSEL_35_RT_OFFSET				28945
#define QM_REG_MAXPQSIZETXSEL_36_RT_OFFSET				28946
#define QM_REG_MAXPQSIZETXSEL_37_RT_OFFSET				28947
#define QM_REG_MAXPQSIZETXSEL_38_RT_OFFSET				28948
#define QM_REG_MAXPQSIZETXSEL_39_RT_OFFSET				28949
#define QM_REG_MAXPQSIZETXSEL_40_RT_OFFSET				28950
#define QM_REG_MAXPQSIZETXSEL_41_RT_OFFSET				28951
#define QM_REG_MAXPQSIZETXSEL_42_RT_OFFSET				28952
#define QM_REG_MAXPQSIZETXSEL_43_RT_OFFSET				28953
#define QM_REG_MAXPQSIZETXSEL_44_RT_OFFSET				28954
#define QM_REG_MAXPQSIZETXSEL_45_RT_OFFSET				28955
#define QM_REG_MAXPQSIZETXSEL_46_RT_OFFSET				28956
#define QM_REG_MAXPQSIZETXSEL_47_RT_OFFSET				28957
#define QM_REG_MAXPQSIZETXSEL_48_RT_OFFSET				28958
#define QM_REG_MAXPQSIZETXSEL_49_RT_OFFSET				28959
#define QM_REG_MAXPQSIZETXSEL_50_RT_OFFSET				28960
#define QM_REG_MAXPQSIZETXSEL_51_RT_OFFSET				28961
#define QM_REG_MAXPQSIZETXSEL_52_RT_OFFSET				28962
#define QM_REG_MAXPQSIZETXSEL_53_RT_OFFSET				28963
#define QM_REG_MAXPQSIZETXSEL_54_RT_OFFSET				28964
#define QM_REG_MAXPQSIZETXSEL_55_RT_OFFSET				28965
#define QM_REG_MAXPQSIZETXSEL_56_RT_OFFSET				28966
#define QM_REG_MAXPQSIZETXSEL_57_RT_OFFSET				28967
#define QM_REG_MAXPQSIZETXSEL_58_RT_OFFSET				28968
#define QM_REG_MAXPQSIZETXSEL_59_RT_OFFSET				28969
#define QM_REG_MAXPQSIZETXSEL_60_RT_OFFSET				28970
#define QM_REG_MAXPQSIZETXSEL_61_RT_OFFSET				28971
#define QM_REG_MAXPQSIZETXSEL_62_RT_OFFSET				28972
#define QM_REG_MAXPQSIZETXSEL_63_RT_OFFSET				28973
#define QM_REG_BASEADDROTHERPQ_RT_OFFSET				28974
#define QM_REG_BASEADDROTHERPQ_RT_SIZE					128
#define QM_REG_PTRTBLOTHER_RT_OFFSET					29102
#define QM_REG_PTRTBLOTHER_RT_SIZE					256
#define QM_REG_VOQCRDLINE_RT_OFFSET					29358
#define QM_REG_VOQCRDLINE_RT_SIZE					20
#define QM_REG_VOQINITCRDLINE_RT_OFFSET					29378
#define QM_REG_VOQINITCRDLINE_RT_SIZE					20
#define QM_REG_AFULLQMBYPTHRPFWFQ_RT_OFFSET				29398
#define QM_REG_AFULLQMBYPTHRVPWFQ_RT_OFFSET				29399
#define QM_REG_AFULLQMBYPTHRPFRL_RT_OFFSET				29400
#define QM_REG_AFULLQMBYPTHRGLBLRL_RT_OFFSET				29401
#define QM_REG_AFULLOPRTNSTCCRDMASK_RT_OFFSET				29402
#define QM_REG_WRROTHERPQGRP_0_RT_OFFSET				29403
#define QM_REG_WRROTHERPQGRP_1_RT_OFFSET				29404
#define QM_REG_WRROTHERPQGRP_2_RT_OFFSET				29405
#define QM_REG_WRROTHERPQGRP_3_RT_OFFSET				29406
#define QM_REG_WRROTHERPQGRP_4_RT_OFFSET				29407
#define QM_REG_WRROTHERPQGRP_5_RT_OFFSET				29408
#define QM_REG_WRROTHERPQGRP_6_RT_OFFSET				29409
#define QM_REG_WRROTHERPQGRP_7_RT_OFFSET				29410
#define QM_REG_WRROTHERPQGRP_8_RT_OFFSET				29411
#define QM_REG_WRROTHERPQGRP_9_RT_OFFSET				29412
#define QM_REG_WRROTHERPQGRP_10_RT_OFFSET				29413
#define QM_REG_WRROTHERPQGRP_11_RT_OFFSET				29414
#define QM_REG_WRROTHERPQGRP_12_RT_OFFSET				29415
#define QM_REG_WRROTHERPQGRP_13_RT_OFFSET				29416
#define QM_REG_WRROTHERPQGRP_14_RT_OFFSET				29417
#define QM_REG_WRROTHERPQGRP_15_RT_OFFSET				29418
#define QM_REG_WRROTHERGRPWEIGHT_0_RT_OFFSET				29419
#define QM_REG_WRROTHERGRPWEIGHT_1_RT_OFFSET				29420
#define QM_REG_WRROTHERGRPWEIGHT_2_RT_OFFSET				29421
#define QM_REG_WRROTHERGRPWEIGHT_3_RT_OFFSET				29422
#define QM_REG_WRRTXGRPWEIGHT_0_RT_OFFSET				29423
#define QM_REG_WRRTXGRPWEIGHT_1_RT_OFFSET				29424
#define QM_REG_PQTX2PF_0_RT_OFFSET					29425
#define QM_REG_PQTX2PF_1_RT_OFFSET					29426
#define QM_REG_PQTX2PF_2_RT_OFFSET					29427
#define QM_REG_PQTX2PF_3_RT_OFFSET					29428
#define QM_REG_PQTX2PF_4_RT_OFFSET					29429
#define QM_REG_PQTX2PF_5_RT_OFFSET					29430
#define QM_REG_PQTX2PF_6_RT_OFFSET					29431
#define QM_REG_PQTX2PF_7_RT_OFFSET					29432
#define QM_REG_PQTX2PF_8_RT_OFFSET					29433
#define QM_REG_PQTX2PF_9_RT_OFFSET					29434
#define QM_REG_PQTX2PF_10_RT_OFFSET					29435
#define QM_REG_PQTX2PF_11_RT_OFFSET					29436
#define QM_REG_PQTX2PF_12_RT_OFFSET					29437
#define QM_REG_PQTX2PF_13_RT_OFFSET					29438
#define QM_REG_PQTX2PF_14_RT_OFFSET					29439
#define QM_REG_PQTX2PF_15_RT_OFFSET					29440
#define QM_REG_PQTX2PF_16_RT_OFFSET					29441
#define QM_REG_PQTX2PF_17_RT_OFFSET					29442
#define QM_REG_PQTX2PF_18_RT_OFFSET					29443
#define QM_REG_PQTX2PF_19_RT_OFFSET					29444
#define QM_REG_PQTX2PF_20_RT_OFFSET					29445
#define QM_REG_PQTX2PF_21_RT_OFFSET					29446
#define QM_REG_PQTX2PF_22_RT_OFFSET					29447
#define QM_REG_PQTX2PF_23_RT_OFFSET					29448
#define QM_REG_PQTX2PF_24_RT_OFFSET					29449
#define QM_REG_PQTX2PF_25_RT_OFFSET					29450
#define QM_REG_PQTX2PF_26_RT_OFFSET					29451
#define QM_REG_PQTX2PF_27_RT_OFFSET					29452
#define QM_REG_PQTX2PF_28_RT_OFFSET					29453
#define QM_REG_PQTX2PF_29_RT_OFFSET					29454
#define QM_REG_PQTX2PF_30_RT_OFFSET					29455
#define QM_REG_PQTX2PF_31_RT_OFFSET					29456
#define QM_REG_PQTX2PF_32_RT_OFFSET					29457
#define QM_REG_PQTX2PF_33_RT_OFFSET					29458
#define QM_REG_PQTX2PF_34_RT_OFFSET					29459
#define QM_REG_PQTX2PF_35_RT_OFFSET					29460
#define QM_REG_PQTX2PF_36_RT_OFFSET					29461
#define QM_REG_PQTX2PF_37_RT_OFFSET					29462
#define QM_REG_PQTX2PF_38_RT_OFFSET					29463
#define QM_REG_PQTX2PF_39_RT_OFFSET					29464
#define QM_REG_PQTX2PF_40_RT_OFFSET					29465
#define QM_REG_PQTX2PF_41_RT_OFFSET					29466
#define QM_REG_PQTX2PF_42_RT_OFFSET					29467
#define QM_REG_PQTX2PF_43_RT_OFFSET					29468
#define QM_REG_PQTX2PF_44_RT_OFFSET					29469
#define QM_REG_PQTX2PF_45_RT_OFFSET					29470
#define QM_REG_PQTX2PF_46_RT_OFFSET					29471
#define QM_REG_PQTX2PF_47_RT_OFFSET					29472
#define QM_REG_PQTX2PF_48_RT_OFFSET					29473
#define QM_REG_PQTX2PF_49_RT_OFFSET					29474
#define QM_REG_PQTX2PF_50_RT_OFFSET					29475
#define QM_REG_PQTX2PF_51_RT_OFFSET					29476
#define QM_REG_PQTX2PF_52_RT_OFFSET					29477
#define QM_REG_PQTX2PF_53_RT_OFFSET					29478
#define QM_REG_PQTX2PF_54_RT_OFFSET					29479
#define QM_REG_PQTX2PF_55_RT_OFFSET					29480
#define QM_REG_PQTX2PF_56_RT_OFFSET					29481
#define QM_REG_PQTX2PF_57_RT_OFFSET					29482
#define QM_REG_PQTX2PF_58_RT_OFFSET					29483
#define QM_REG_PQTX2PF_59_RT_OFFSET					29484
#define QM_REG_PQTX2PF_60_RT_OFFSET					29485
#define QM_REG_PQTX2PF_61_RT_OFFSET					29486
#define QM_REG_PQTX2PF_62_RT_OFFSET					29487
#define QM_REG_PQTX2PF_63_RT_OFFSET					29488
#define QM_REG_PQOTHER2PF_0_RT_OFFSET					29489
#define QM_REG_PQOTHER2PF_1_RT_OFFSET					29490
#define QM_REG_PQOTHER2PF_2_RT_OFFSET					29491
#define QM_REG_PQOTHER2PF_3_RT_OFFSET					29492
#define QM_REG_PQOTHER2PF_4_RT_OFFSET					29493
#define QM_REG_PQOTHER2PF_5_RT_OFFSET					29494
#define QM_REG_PQOTHER2PF_6_RT_OFFSET					29495
#define QM_REG_PQOTHER2PF_7_RT_OFFSET					29496
#define QM_REG_PQOTHER2PF_8_RT_OFFSET					29497
#define QM_REG_PQOTHER2PF_9_RT_OFFSET					29498
#define QM_REG_PQOTHER2PF_10_RT_OFFSET					29499
#define QM_REG_PQOTHER2PF_11_RT_OFFSET					29500
#define QM_REG_PQOTHER2PF_12_RT_OFFSET					29501
#define QM_REG_PQOTHER2PF_13_RT_OFFSET					29502
#define QM_REG_PQOTHER2PF_14_RT_OFFSET					29503
#define QM_REG_PQOTHER2PF_15_RT_OFFSET					29504
#define QM_REG_RLGLBLPERIOD_0_RT_OFFSET					29505
#define QM_REG_RLGLBLPERIOD_1_RT_OFFSET					29506
#define QM_REG_RLGLBLPERIODTIMER_0_RT_OFFSET				29507
#define QM_REG_RLGLBLPERIODTIMER_1_RT_OFFSET				29508
#define QM_REG_RLGLBLPERIODSEL_0_RT_OFFSET				29509
#define QM_REG_RLGLBLPERIODSEL_1_RT_OFFSET				29510
#define QM_REG_RLGLBLPERIODSEL_2_RT_OFFSET				29511
#define QM_REG_RLGLBLPERIODSEL_3_RT_OFFSET				29512
#define QM_REG_RLGLBLPERIODSEL_4_RT_OFFSET				29513
#define QM_REG_RLGLBLPERIODSEL_5_RT_OFFSET				29514
#define QM_REG_RLGLBLPERIODSEL_6_RT_OFFSET				29515
#define QM_REG_RLGLBLPERIODSEL_7_RT_OFFSET				29516
#define QM_REG_RLGLBLINCVAL_RT_OFFSET					29517
#define QM_REG_RLGLBLINCVAL_RT_SIZE					256
#define QM_REG_RLGLBLUPPERBOUND_RT_OFFSET				29773
#define QM_REG_RLGLBLUPPERBOUND_RT_SIZE					256
#define QM_REG_RLGLBLCRD_RT_OFFSET					30029
#define QM_REG_RLGLBLCRD_RT_SIZE					256
#define QM_REG_RLGLBLENABLE_RT_OFFSET					30285
#define QM_REG_RLPFPERIOD_RT_OFFSET					30286
#define QM_REG_RLPFPERIODTIMER_RT_OFFSET				30287
#define QM_REG_RLPFINCVAL_RT_OFFSET					30288
#define QM_REG_RLPFINCVAL_RT_SIZE					16
#define QM_REG_RLPFUPPERBOUND_RT_OFFSET					30304
#define QM_REG_RLPFUPPERBOUND_RT_SIZE					16
#define QM_REG_RLPFCRD_RT_OFFSET					30320
#define QM_REG_RLPFCRD_RT_SIZE						16
#define QM_REG_RLPFENABLE_RT_OFFSET					30336
#define QM_REG_RLPFVOQENABLE_RT_OFFSET					30337
#define QM_REG_WFQPFWEIGHT_RT_OFFSET					30338
#define QM_REG_WFQPFWEIGHT_RT_SIZE					16
#define QM_REG_WFQPFUPPERBOUND_RT_OFFSET				30354
#define QM_REG_WFQPFUPPERBOUND_RT_SIZE					16
#define QM_REG_WFQPFCRD_RT_OFFSET					30370
#define QM_REG_WFQPFCRD_RT_SIZE						160
#define QM_REG_WFQPFENABLE_RT_OFFSET					30530
#define QM_REG_WFQVPENABLE_RT_OFFSET					30531
#define QM_REG_BASEADDRTXPQ_RT_OFFSET					30532
#define QM_REG_BASEADDRTXPQ_RT_SIZE					512
#define QM_REG_TXPQMAP_RT_OFFSET					31044
#define QM_REG_TXPQMAP_RT_SIZE						512
#define QM_REG_WFQVPWEIGHT_RT_OFFSET					31556
#define QM_REG_WFQVPWEIGHT_RT_SIZE					512
#define QM_REG_WFQVPCRD_RT_OFFSET					32068
#define QM_REG_WFQVPCRD_RT_SIZE						512
#define QM_REG_WFQVPMAP_RT_OFFSET					32580
#define QM_REG_WFQVPMAP_RT_SIZE						512
#define QM_REG_PTRTBLTX_RT_OFFSET					33092
#define QM_REG_PTRTBLTX_RT_SIZE						1024
#define QM_REG_WFQPFCRD_MSB_RT_OFFSET					34116
#define QM_REG_WFQPFCRD_MSB_RT_SIZE					160
#define NIG_REG_TAG_ETHERTYPE_0_RT_OFFSET				34276
#define NIG_REG_BRB_GATE_DNTFWD_PORT_RT_OFFSET				34277
#define NIG_REG_OUTER_TAG_VALUE_LIST0_RT_OFFSET				34278
#define NIG_REG_OUTER_TAG_VALUE_LIST1_RT_OFFSET				34279
#define NIG_REG_OUTER_TAG_VALUE_LIST2_RT_OFFSET				34280
#define NIG_REG_OUTER_TAG_VALUE_LIST3_RT_OFFSET				34281
#define NIG_REG_LLH_FUNC_TAGMAC_CLS_TYPE_RT_OFFSET			34282
#define NIG_REG_LLH_FUNC_TAG_EN_RT_OFFSET				34283
#define NIG_REG_LLH_FUNC_TAG_EN_RT_SIZE					4
#define NIG_REG_LLH_FUNC_TAG_VALUE_RT_OFFSET				34287
#define NIG_REG_LLH_FUNC_TAG_VALUE_RT_SIZE				4
#define NIG_REG_LLH_FUNC_FILTER_VALUE_RT_OFFSET				34291
#define NIG_REG_LLH_FUNC_FILTER_VALUE_RT_SIZE				32
#define NIG_REG_LLH_FUNC_FILTER_EN_RT_OFFSET				34323
#define NIG_REG_LLH_FUNC_FILTER_EN_RT_SIZE				16
#define NIG_REG_LLH_FUNC_FILTER_MODE_RT_OFFSET				34339
#define NIG_REG_LLH_FUNC_FILTER_MODE_RT_SIZE				16
#define NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE_RT_OFFSET			34355
#define NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE_RT_SIZE			16
#define NIG_REG_LLH_FUNC_FILTER_HDR_SEL_RT_OFFSET			34371
#define NIG_REG_LLH_FUNC_FILTER_HDR_SEL_RT_SIZE				16
#define NIG_REG_TX_EDPM_CTRL_RT_OFFSET					34387
#define NIG_REG_PPF_TO_ENGINE_SEL_RT_OFFSET				34388
#define NIG_REG_PPF_TO_ENGINE_SEL_RT_SIZE				8
#define CDU_REG_CID_ADDR_PARAMS_RT_OFFSET				34396
#define CDU_REG_SEGMENT0_PARAMS_RT_OFFSET				34397
#define CDU_REG_SEGMENT1_PARAMS_RT_OFFSET				34398
#define CDU_REG_PF_SEG0_TYPE_OFFSET_RT_OFFSET				34399
#define CDU_REG_PF_SEG1_TYPE_OFFSET_RT_OFFSET				34400
#define CDU_REG_PF_SEG2_TYPE_OFFSET_RT_OFFSET				34401
#define CDU_REG_PF_SEG3_TYPE_OFFSET_RT_OFFSET				34402
#define CDU_REG_PF_FL_SEG0_TYPE_OFFSET_RT_OFFSET			34403
#define CDU_REG_PF_FL_SEG1_TYPE_OFFSET_RT_OFFSET			34404
#define CDU_REG_PF_FL_SEG2_TYPE_OFFSET_RT_OFFSET			34405
#define CDU_REG_PF_FL_SEG3_TYPE_OFFSET_RT_OFFSET			34406
#define CDU_REG_VF_SEG_TYPE_OFFSET_RT_OFFSET				34407
#define CDU_REG_VF_FL_SEG_TYPE_OFFSET_RT_OFFSET				34408
#define PBF_REG_TAG_ETHERTYPE_0_RT_OFFSET				34409
#define PBF_REG_BTB_SHARED_AREA_SIZE_RT_OFFSET				34410
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET			34411
#define PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET				34412
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ0_RT_OFFSET			34413
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ1_RT_OFFSET			34414
#define PBF_REG_BTB_GUARANTEED_VOQ1_RT_OFFSET				34415
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ1_RT_OFFSET			34416
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ2_RT_OFFSET			34417
#define PBF_REG_BTB_GUARANTEED_VOQ2_RT_OFFSET				34418
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ2_RT_OFFSET			34419
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ3_RT_OFFSET			34420
#define PBF_REG_BTB_GUARANTEED_VOQ3_RT_OFFSET				34421
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ3_RT_OFFSET			34422
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ4_RT_OFFSET			34423
#define PBF_REG_BTB_GUARANTEED_VOQ4_RT_OFFSET				34424
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ4_RT_OFFSET			34425
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ5_RT_OFFSET			34426
#define PBF_REG_BTB_GUARANTEED_VOQ5_RT_OFFSET				34427
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ5_RT_OFFSET			34428
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ6_RT_OFFSET			34429
#define PBF_REG_BTB_GUARANTEED_VOQ6_RT_OFFSET				34430
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ6_RT_OFFSET			34431
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ7_RT_OFFSET			34432
#define PBF_REG_BTB_GUARANTEED_VOQ7_RT_OFFSET				34433
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ7_RT_OFFSET			34434
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ8_RT_OFFSET			34435
#define PBF_REG_BTB_GUARANTEED_VOQ8_RT_OFFSET				34436
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ8_RT_OFFSET			34437
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ9_RT_OFFSET			34438
#define PBF_REG_BTB_GUARANTEED_VOQ9_RT_OFFSET				34439
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ9_RT_OFFSET			34440
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ10_RT_OFFSET			34441
#define PBF_REG_BTB_GUARANTEED_VOQ10_RT_OFFSET				34442
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ10_RT_OFFSET			34443
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ11_RT_OFFSET			34444
#define PBF_REG_BTB_GUARANTEED_VOQ11_RT_OFFSET				34445
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ11_RT_OFFSET			34446
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ12_RT_OFFSET			34447
#define PBF_REG_BTB_GUARANTEED_VOQ12_RT_OFFSET				34448
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ12_RT_OFFSET			34449
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ13_RT_OFFSET			34450
#define PBF_REG_BTB_GUARANTEED_VOQ13_RT_OFFSET				34451
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ13_RT_OFFSET			34452
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ14_RT_OFFSET			34453
#define PBF_REG_BTB_GUARANTEED_VOQ14_RT_OFFSET				34454
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ14_RT_OFFSET			34455
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ15_RT_OFFSET			34456
#define PBF_REG_BTB_GUARANTEED_VOQ15_RT_OFFSET				34457
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ15_RT_OFFSET			34458
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ16_RT_OFFSET			34459
#define PBF_REG_BTB_GUARANTEED_VOQ16_RT_OFFSET				34460
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ16_RT_OFFSET			34461
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ17_RT_OFFSET			34462
#define PBF_REG_BTB_GUARANTEED_VOQ17_RT_OFFSET				34463
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ17_RT_OFFSET			34464
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ18_RT_OFFSET			34465
#define PBF_REG_BTB_GUARANTEED_VOQ18_RT_OFFSET				34466
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ18_RT_OFFSET			34467
#define PBF_REG_YCMD_QS_NUM_LINES_VOQ19_RT_OFFSET			34468
#define PBF_REG_BTB_GUARANTEED_VOQ19_RT_OFFSET				34469
#define PBF_REG_BTB_SHARED_AREA_SETUP_VOQ19_RT_OFFSET			34470
#define XCM_REG_CON_PHY_Q3_RT_OFFSET					34471

#define RUNTIME_ARRAY_SIZE 34472

/* Init Callbacks */
#define DMAE_READY_CB	0

/* The eth storm context for the Tstorm */
struct tstorm_eth_conn_st_ctx {
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

struct e4_xstorm_eth_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED1_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED1_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED2_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED3_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED4_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED4_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED5_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED5_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED6_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED6_SHIFT	7
		u8 flags1;
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED7_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED7_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED8_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED8_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED9_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED9_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_ETH_CONN_AG_CTX_E5_RESERVED2_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_E5_RESERVED2_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_E5_RESERVED3_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_E5_RESERVED3_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_TX_RULE_ACTIVE_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_ACTIVE_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT	7
	u8 flags2;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF0_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF1_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF2_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF3_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF7_SHIFT	6
		u8 flags4;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF12_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF12_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF13_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF13_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF14_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF14_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF15_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF15_SHIFT	6
	u8 flags6;
#define E4_XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_MASK		0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_MASK		0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_MASK			0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_SHIFT			4
#define E4_XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_MASK		0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_SHIFT		6
	u8 flags7;
#define E4_XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_MASK		0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED10_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED10_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT		6
#define E4_XSTORM_ETH_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT		7
	u8 flags8;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF4EN_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF5EN_SHIFT	3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF6EN_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF7EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF7EN_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_CF8EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF8EN_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_CF9EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF9EN_SHIFT	7
	u8 flags9;
#define E4_XSTORM_ETH_CONN_AG_CTX_CF10EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF10EN_SHIFT			0
#define E4_XSTORM_ETH_CONN_AG_CTX_CF11EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF11EN_SHIFT			1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF12EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF12EN_SHIFT			2
#define E4_XSTORM_ETH_CONN_AG_CTX_CF13EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF13EN_SHIFT			3
#define E4_XSTORM_ETH_CONN_AG_CTX_CF14EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF14EN_SHIFT			4
#define E4_XSTORM_ETH_CONN_AG_CTX_CF15EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_CF15EN_SHIFT			5
#define E4_XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_MULTI_UNICAST_CF_EN_SHIFT	7
	u8 flags10;
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_EN_MASK			0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_DQ_CF_EN_SHIFT		0
#define E4_XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT		1
#define E4_XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT		2
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED11_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED11_SHIFT		3
#define E4_XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_SLOW_PATH_EN_SHIFT		4
#define E4_XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED12_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED12_SHIFT		6
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED13_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED13_SHIFT		7
	u8 flags11;
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED14_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED14_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED15_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RESERVED15_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_TX_DEC_RULE_EN_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT		3
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE6EN_SHIFT		4
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT		5
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE9EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE9EN_SHIFT		7
	u8 flags12;
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE10EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE10EN_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE11EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE11EN_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE14EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE14EN_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE15EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE15EN_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE16EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE16EN_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE17EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE17EN_SHIFT	7
	u8 flags13;
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE18EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE18EN_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE19EN_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_RULE19EN_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED4_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED4_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED5_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED5_SHIFT	3
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED6_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED6_SHIFT	4
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED7_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED7_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED8_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED8_SHIFT	6
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED9_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_USE_EXT_HDR_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_USE_EXT_HDR_SHIFT	0
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_SHIFT	1
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_SHIFT	2
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_MASK	0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_SHIFT	3
#define E4_XSTORM_ETH_CONN_AG_CTX_L2_EDPM_ENABLE_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_L2_EDPM_ENABLE_SHIFT		4
#define E4_XSTORM_ETH_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK		0x1
#define E4_XSTORM_ETH_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT	5
#define E4_XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_MASK		0x3
#define E4_XSTORM_ETH_CONN_AG_CTX_TPH_ENABLE_SHIFT		6
	u8 edpm_event_id;
	__le16 physical_q0;
	__le16 e5_reserved1;
	__le16 edpm_num_bds;
	__le16 tx_bd_cons;
	__le16 tx_bd_prod;
	__le16 updated_qm_pq_id;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le16 word7;
	__le16 word8;
	__le16 word9;
	__le16 word10;
	__le32 reg7;
	__le32 reg8;
	__le32 reg9;
	u8 byte7;
	u8 byte8;
	u8 byte9;
	u8 byte10;
	u8 byte11;
	u8 byte12;
	u8 byte13;
	u8 byte14;
	u8 byte15;
	u8 e5_reserved;
	__le16 word11;
	__le32 reg10;
	__le32 reg11;
	__le32 reg12;
	__le32 reg13;
	__le32 reg14;
	__le32 reg15;
	__le32 reg16;
	__le32 reg17;
	__le32 reg18;
	__le32 reg19;
	__le16 word12;
	__le16 word13;
	__le16 word14;
	__le16 word15;
};

/* The eth storm context for the Ystorm */
struct ystorm_eth_conn_st_ctx {
	__le32 reserved[8];
};

struct e4_ystorm_eth_conn_ag_ctx {
	u8 byte0;
	u8 state;
	u8 flags0;
#define E4_YSTORM_ETH_CONN_AG_CTX_BIT0_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_BIT0_SHIFT			0
#define E4_YSTORM_ETH_CONN_AG_CTX_BIT1_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_BIT1_SHIFT			1
#define E4_YSTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_MASK	0x3
#define E4_YSTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_SHIFT	2
#define E4_YSTORM_ETH_CONN_AG_CTX_PMD_TERMINATE_CF_MASK		0x3
#define E4_YSTORM_ETH_CONN_AG_CTX_PMD_TERMINATE_CF_SHIFT	4
#define E4_YSTORM_ETH_CONN_AG_CTX_CF2_MASK			0x3
#define E4_YSTORM_ETH_CONN_AG_CTX_CF2_SHIFT			6
	u8 flags1;
#define E4_YSTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_MASK	0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_SHIFT	0
#define E4_YSTORM_ETH_CONN_AG_CTX_PMD_TERMINATE_CF_EN_MASK	0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_PMD_TERMINATE_CF_EN_SHIFT	1
#define E4_YSTORM_ETH_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT			2
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT			3
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE1EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT			4
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE2EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT			5
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE3EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT			6
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE4EN_MASK			0x1
#define E4_YSTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT			7
	u8 tx_q0_int_coallecing_timeset;
	u8 byte3;
	__le16 word0;
	__le32 terminate_spqe;
	__le32 reg1;
	__le16 tx_bd_cons_upd;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

struct e4_tstorm_eth_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT2_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT2_SHIFT	2
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT3_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT3_SHIFT	3
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT4_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT4_SHIFT	4
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT5_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_BIT5_SHIFT	5
#define E4_TSTORM_ETH_CONN_AG_CTX_CF0_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF0_SHIFT	6
	u8 flags1;
#define E4_TSTORM_ETH_CONN_AG_CTX_CF1_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF1_SHIFT	0
#define E4_TSTORM_ETH_CONN_AG_CTX_CF2_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF2_SHIFT	2
#define E4_TSTORM_ETH_CONN_AG_CTX_CF3_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF3_SHIFT	4
#define E4_TSTORM_ETH_CONN_AG_CTX_CF4_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF4_SHIFT	6
	u8 flags2;
#define E4_TSTORM_ETH_CONN_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF5_SHIFT	0
#define E4_TSTORM_ETH_CONN_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF6_SHIFT	2
#define E4_TSTORM_ETH_CONN_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF7_SHIFT	4
#define E4_TSTORM_ETH_CONN_AG_CTX_CF8_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF8_SHIFT	6
	u8 flags3;
#define E4_TSTORM_ETH_CONN_AG_CTX_CF9_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF9_SHIFT	0
#define E4_TSTORM_ETH_CONN_AG_CTX_CF10_MASK	0x3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF10_SHIFT	2
#define E4_TSTORM_ETH_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT	4
#define E4_TSTORM_ETH_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT	5
#define E4_TSTORM_ETH_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT	6
#define E4_TSTORM_ETH_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_ETH_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF4EN_SHIFT	0
#define E4_TSTORM_ETH_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF5EN_SHIFT	1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF6EN_SHIFT	2
#define E4_TSTORM_ETH_CONN_AG_CTX_CF7EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF7EN_SHIFT	3
#define E4_TSTORM_ETH_CONN_AG_CTX_CF8EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF8EN_SHIFT	4
#define E4_TSTORM_ETH_CONN_AG_CTX_CF9EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF9EN_SHIFT	5
#define E4_TSTORM_ETH_CONN_AG_CTX_CF10EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_CF10EN_SHIFT	6
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags5;
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT		0
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT		1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT		2
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT		3
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT		4
#define E4_TSTORM_ETH_CONN_AG_CTX_RX_BD_EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RX_BD_EN_SHIFT	5
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT		6
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_ETH_CONN_AG_CTX_RULE8EN_SHIFT		7
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 byte2;
	u8 byte3;
	__le16 rx_bd_cons;
	u8 byte4;
	u8 byte5;
	__le16 rx_bd_prod;
	__le16 word2;
	__le16 word3;
	__le32 reg9;
	__le32 reg10;
};

struct e4_ustorm_eth_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_ETH_CONN_AG_CTX_BIT0_MASK			0x1
#define E4_USTORM_ETH_CONN_AG_CTX_BIT0_SHIFT			0
#define E4_USTORM_ETH_CONN_AG_CTX_BIT1_MASK			0x1
#define E4_USTORM_ETH_CONN_AG_CTX_BIT1_SHIFT			1
#define E4_USTORM_ETH_CONN_AG_CTX_TX_PMD_TERMINATE_CF_MASK	0x3
#define E4_USTORM_ETH_CONN_AG_CTX_TX_PMD_TERMINATE_CF_SHIFT	2
#define E4_USTORM_ETH_CONN_AG_CTX_RX_PMD_TERMINATE_CF_MASK	0x3
#define E4_USTORM_ETH_CONN_AG_CTX_RX_PMD_TERMINATE_CF_SHIFT	4
#define E4_USTORM_ETH_CONN_AG_CTX_CF2_MASK			0x3
#define E4_USTORM_ETH_CONN_AG_CTX_CF2_SHIFT			6
	u8 flags1;
#define E4_USTORM_ETH_CONN_AG_CTX_CF3_MASK			0x3
#define E4_USTORM_ETH_CONN_AG_CTX_CF3_SHIFT			0
#define E4_USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_MASK		0x3
#define E4_USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_SHIFT		2
#define E4_USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_MASK		0x3
#define E4_USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_SHIFT		4
#define E4_USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_MASK	0x3
#define E4_USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_SHIFT	6
	u8 flags2;
#define E4_USTORM_ETH_CONN_AG_CTX_TX_PMD_TERMINATE_CF_EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_TX_PMD_TERMINATE_CF_EN_SHIFT	0
#define E4_USTORM_ETH_CONN_AG_CTX_RX_PMD_TERMINATE_CF_EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RX_PMD_TERMINATE_CF_EN_SHIFT	1
#define E4_USTORM_ETH_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_USTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT			2
#define E4_USTORM_ETH_CONN_AG_CTX_CF3EN_MASK			0x1
#define E4_USTORM_ETH_CONN_AG_CTX_CF3EN_SHIFT			3
#define E4_USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_EN_MASK		0x1
#define E4_USTORM_ETH_CONN_AG_CTX_TX_ARM_CF_EN_SHIFT		4
#define E4_USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_EN_MASK		0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RX_ARM_CF_EN_SHIFT		5
#define E4_USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_TX_BD_CONS_UPD_CF_EN_SHIFT	6
#define E4_USTORM_ETH_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT			7
	u8 flags3;
#define E4_USTORM_ETH_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_ETH_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_ETH_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_ETH_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_ETH_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_ETH_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_ETH_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_USTORM_ETH_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 tx_bd_cons;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 tx_int_coallecing_timeset;
	__le16 tx_drv_bd_cons;
	__le16 rx_drv_cqe_cons;
};

/* The eth storm context for the Ustorm */
struct ustorm_eth_conn_st_ctx {
	__le32 reserved[40];
};

/* The eth storm context for the Mstorm */
struct mstorm_eth_conn_st_ctx {
	__le32 reserved[8];
};

/* eth connection context */
struct e4_eth_conn_context {
	struct tstorm_eth_conn_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
	struct pstorm_eth_conn_st_ctx pstorm_st_context;
	struct xstorm_eth_conn_st_ctx xstorm_st_context;
	struct e4_xstorm_eth_conn_ag_ctx xstorm_ag_context;
	struct e4_tstorm_eth_conn_ag_ctx tstorm_ag_context;
	struct ystorm_eth_conn_st_ctx ystorm_st_context;
	struct e4_ystorm_eth_conn_ag_ctx ystorm_ag_context;
	struct e4_ustorm_eth_conn_ag_ctx ustorm_ag_context;
	struct ustorm_eth_conn_st_ctx ustorm_st_context;
	struct mstorm_eth_conn_st_ctx mstorm_st_context;
};

/* Ethernet filter types: mac/vlan/pair */
enum eth_error_code {
	ETH_OK = 0x00,
	ETH_FILTERS_MAC_ADD_FAIL_FULL,
	ETH_FILTERS_MAC_ADD_FAIL_FULL_MTT2,
	ETH_FILTERS_MAC_ADD_FAIL_DUP_MTT2,
	ETH_FILTERS_MAC_ADD_FAIL_DUP_STT2,
	ETH_FILTERS_MAC_DEL_FAIL_NOF,
	ETH_FILTERS_MAC_DEL_FAIL_NOF_MTT2,
	ETH_FILTERS_MAC_DEL_FAIL_NOF_STT2,
	ETH_FILTERS_MAC_ADD_FAIL_ZERO_MAC,
	ETH_FILTERS_VLAN_ADD_FAIL_FULL,
	ETH_FILTERS_VLAN_ADD_FAIL_DUP,
	ETH_FILTERS_VLAN_DEL_FAIL_NOF,
	ETH_FILTERS_VLAN_DEL_FAIL_NOF_TT1,
	ETH_FILTERS_PAIR_ADD_FAIL_DUP,
	ETH_FILTERS_PAIR_ADD_FAIL_FULL,
	ETH_FILTERS_PAIR_ADD_FAIL_FULL_MAC,
	ETH_FILTERS_PAIR_DEL_FAIL_NOF,
	ETH_FILTERS_PAIR_DEL_FAIL_NOF_TT1,
	ETH_FILTERS_PAIR_ADD_FAIL_ZERO_MAC,
	ETH_FILTERS_VNI_ADD_FAIL_FULL,
	ETH_FILTERS_VNI_ADD_FAIL_DUP,
	ETH_FILTERS_GFT_UPDATE_FAIL,
	ETH_RX_QUEUE_FAIL_LOAD_VF_DATA,
	ETH_FILTERS_GFS_ADD_FILTER_FAIL_MAX_HOPS,
	ETH_FILTERS_GFS_ADD_FILTER_FAIL_NO_FREE_ENRTY,
	ETH_FILTERS_GFS_ADD_FILTER_FAIL_ALREADY_EXISTS,
	ETH_FILTERS_GFS_ADD_FILTER_FAIL_PCI_ERROR,
	ETH_FILTERS_GFS_ADD_FINLER_FAIL_MAGIC_NUM_ERROR,
	ETH_FILTERS_GFS_DEL_FILTER_FAIL_MAX_HOPS,
	ETH_FILTERS_GFS_DEL_FILTER_FAIL_NO_MATCH_ENRTY,
	ETH_FILTERS_GFS_DEL_FILTER_FAIL_PCI_ERROR,
	ETH_FILTERS_GFS_DEL_FILTER_FAIL_MAGIC_NUM_ERROR,
	MAX_ETH_ERROR_CODE
};

/* Opcodes for the event ring */
enum eth_event_opcode {
	ETH_EVENT_UNUSED,
	ETH_EVENT_VPORT_START,
	ETH_EVENT_VPORT_UPDATE,
	ETH_EVENT_VPORT_STOP,
	ETH_EVENT_TX_QUEUE_START,
	ETH_EVENT_TX_QUEUE_STOP,
	ETH_EVENT_RX_QUEUE_START,
	ETH_EVENT_RX_QUEUE_UPDATE,
	ETH_EVENT_RX_QUEUE_STOP,
	ETH_EVENT_FILTERS_UPDATE,
	ETH_EVENT_RX_ADD_OPENFLOW_FILTER,
	ETH_EVENT_RX_DELETE_OPENFLOW_FILTER,
	ETH_EVENT_RX_CREATE_OPENFLOW_ACTION,
	ETH_EVENT_RX_ADD_UDP_FILTER,
	ETH_EVENT_RX_DELETE_UDP_FILTER,
	ETH_EVENT_RX_CREATE_GFT_ACTION,
	ETH_EVENT_RX_GFT_UPDATE_FILTER,
	ETH_EVENT_TX_QUEUE_UPDATE,
	ETH_EVENT_RGFS_ADD_FILTER,
	ETH_EVENT_RGFS_DEL_FILTER,
	ETH_EVENT_TGFS_ADD_FILTER,
	ETH_EVENT_TGFS_DEL_FILTER,
	ETH_EVENT_GFS_COUNTERS_REPORT_REQUEST,
	MAX_ETH_EVENT_OPCODE
};

/* Classify rule types in E2/E3 */
enum eth_filter_action {
	ETH_FILTER_ACTION_UNUSED,
	ETH_FILTER_ACTION_REMOVE,
	ETH_FILTER_ACTION_ADD,
	ETH_FILTER_ACTION_REMOVE_ALL,
	MAX_ETH_FILTER_ACTION
};

/* Command for adding/removing a classification rule $$KEEP_ENDIANNESS$$ */
struct eth_filter_cmd {
	u8 type;
	u8 vport_id;
	u8 action;
	u8 reserved0;
	__le32 vni;
	__le16 mac_lsb;
	__le16 mac_mid;
	__le16 mac_msb;
	__le16 vlan_id;
};

/*	$$KEEP_ENDIANNESS$$ */
struct eth_filter_cmd_header {
	u8 rx;
	u8 tx;
	u8 cmd_cnt;
	u8 assert_on_error;
	u8 reserved1[4];
};

/* Ethernet filter types: mac/vlan/pair */
enum eth_filter_type {
	ETH_FILTER_TYPE_UNUSED,
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

/* inner to inner vlan priority translation configurations */
struct eth_in_to_in_pri_map_cfg {
	u8 inner_vlan_pri_remap_en;
	u8 reserved[7];
	u8 non_rdma_in_to_in_pri_map[8];
	u8 rdma_in_to_in_pri_map[8];
};

/* Eth IPv4 Fragment Type */
enum eth_ipv4_frag_type {
	ETH_IPV4_NOT_FRAG,
	ETH_IPV4_FIRST_FRAG,
	ETH_IPV4_NON_FIRST_FRAG,
	MAX_ETH_IPV4_FRAG_TYPE
};

/* eth IPv4 Fragment Type */
enum eth_ip_type {
	ETH_IPV4,
	ETH_IPV6,
	MAX_ETH_IP_TYPE
};

/* Ethernet Ramrod Command IDs */
enum eth_ramrod_cmd_id {
	ETH_RAMROD_UNUSED,
	ETH_RAMROD_VPORT_START,
	ETH_RAMROD_VPORT_UPDATE,
	ETH_RAMROD_VPORT_STOP,
	ETH_RAMROD_RX_QUEUE_START,
	ETH_RAMROD_RX_QUEUE_STOP,
	ETH_RAMROD_TX_QUEUE_START,
	ETH_RAMROD_TX_QUEUE_STOP,
	ETH_RAMROD_FILTERS_UPDATE,
	ETH_RAMROD_RX_QUEUE_UPDATE,
	ETH_RAMROD_RX_CREATE_OPENFLOW_ACTION,
	ETH_RAMROD_RX_ADD_OPENFLOW_FILTER,
	ETH_RAMROD_RX_DELETE_OPENFLOW_FILTER,
	ETH_RAMROD_RX_ADD_UDP_FILTER,
	ETH_RAMROD_RX_DELETE_UDP_FILTER,
	ETH_RAMROD_RX_CREATE_GFT_ACTION,
	ETH_RAMROD_GFT_UPDATE_FILTER,
	ETH_RAMROD_TX_QUEUE_UPDATE,
	ETH_RAMROD_RGFS_FILTER_ADD,
	ETH_RAMROD_RGFS_FILTER_DEL,
	ETH_RAMROD_TGFS_FILTER_ADD,
	ETH_RAMROD_TGFS_FILTER_DEL,
	ETH_RAMROD_GFS_COUNTERS_REPORT_REQUEST,
	MAX_ETH_RAMROD_CMD_ID
};

/* Return code from eth sp ramrods */
struct eth_return_code {
	u8 value;
#define ETH_RETURN_CODE_ERR_CODE_MASK  0x3F
#define ETH_RETURN_CODE_ERR_CODE_SHIFT 0
#define ETH_RETURN_CODE_RESERVED_MASK  0x1
#define ETH_RETURN_CODE_RESERVED_SHIFT 6
#define ETH_RETURN_CODE_RX_TX_MASK     0x1
#define ETH_RETURN_CODE_RX_TX_SHIFT    7
};

/* tx destination enum */
enum eth_tx_dst_mode_config_enum {
	ETH_TX_DST_MODE_CONFIG_DISABLE,
	ETH_TX_DST_MODE_CONFIG_FORWARD_DATA_IN_BD,
	ETH_TX_DST_MODE_CONFIG_FORWARD_DATA_IN_VPORT,
	MAX_ETH_TX_DST_MODE_CONFIG_ENUM
};

/* What to do in case an error occurs */
enum eth_tx_err {
	ETH_TX_ERR_DROP,
	ETH_TX_ERR_ASSERT_MALICIOUS,
	MAX_ETH_TX_ERR
};

/* Array of the different error type behaviors */
struct eth_tx_err_vals {
	__le16 values;
#define ETH_TX_ERR_VALS_ILLEGAL_VLAN_MODE_MASK			0x1
#define ETH_TX_ERR_VALS_ILLEGAL_VLAN_MODE_SHIFT			0
#define ETH_TX_ERR_VALS_PACKET_TOO_SMALL_MASK			0x1
#define ETH_TX_ERR_VALS_PACKET_TOO_SMALL_SHIFT			1
#define ETH_TX_ERR_VALS_ANTI_SPOOFING_ERR_MASK			0x1
#define ETH_TX_ERR_VALS_ANTI_SPOOFING_ERR_SHIFT			2
#define ETH_TX_ERR_VALS_ILLEGAL_INBAND_TAGS_MASK		0x1
#define ETH_TX_ERR_VALS_ILLEGAL_INBAND_TAGS_SHIFT		3
#define ETH_TX_ERR_VALS_VLAN_INSERTION_W_INBAND_TAG_MASK	0x1
#define ETH_TX_ERR_VALS_VLAN_INSERTION_W_INBAND_TAG_SHIFT	4
#define ETH_TX_ERR_VALS_MTU_VIOLATION_MASK			0x1
#define ETH_TX_ERR_VALS_MTU_VIOLATION_SHIFT			5
#define ETH_TX_ERR_VALS_ILLEGAL_CONTROL_FRAME_MASK		0x1
#define ETH_TX_ERR_VALS_ILLEGAL_CONTROL_FRAME_SHIFT		6
#define ETH_TX_ERR_VALS_ILLEGAL_BD_FLAGS_MASK			0x1
#define ETH_TX_ERR_VALS_ILLEGAL_BD_FLAGS_SHIFT			7
#define ETH_TX_ERR_VALS_RESERVED_MASK				0xFF
#define ETH_TX_ERR_VALS_RESERVED_SHIFT				8
};

/* vport rss configuration data */
struct eth_vport_rss_config {
	__le16 capabilities;
#define ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY_SHIFT		0
#define ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY_SHIFT		1
#define ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY_SHIFT		2
#define ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY_SHIFT		3
#define ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY_SHIFT		4
#define ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY_SHIFT		5
#define ETH_VPORT_RSS_CONFIG_EN_5_TUPLE_CAPABILITY_MASK		0x1
#define ETH_VPORT_RSS_CONFIG_EN_5_TUPLE_CAPABILITY_SHIFT	6
#define ETH_VPORT_RSS_CONFIG_RESERVED0_MASK			0x1FF
#define ETH_VPORT_RSS_CONFIG_RESERVED0_SHIFT			7
	u8 rss_id;
	u8 rss_mode;
	u8 update_rss_key;
	u8 update_rss_ind_table;
	u8 update_rss_capabilities;
	u8 tbl_size;
	__le32 reserved2[2];
	__le16 indirection_table[ETH_RSS_IND_TABLE_ENTRIES_NUM];
	__le32 rss_key[ETH_RSS_KEY_SIZE_REGS];
	__le32 reserved3[2];
};

/* eth vport RSS mode */
enum eth_vport_rss_mode {
	ETH_VPORT_RSS_MODE_DISABLED,
	ETH_VPORT_RSS_MODE_REGULAR,
	MAX_ETH_VPORT_RSS_MODE
};

/* Command for setting classification flags for a vport $$KEEP_ENDIANNESS$$ */
struct eth_vport_rx_mode {
	__le16 state;
#define ETH_VPORT_RX_MODE_UCAST_DROP_ALL_MASK		0x1
#define ETH_VPORT_RX_MODE_UCAST_DROP_ALL_SHIFT		0
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_ALL_SHIFT	1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED_MASK	0x1
#define ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED_SHIFT	2
#define ETH_VPORT_RX_MODE_MCAST_DROP_ALL_MASK		0x1
#define ETH_VPORT_RX_MODE_MCAST_DROP_ALL_SHIFT		3
#define ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL_SHIFT	4
#define ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL_SHIFT	5
#define ETH_VPORT_RX_MODE_ACCEPT_ANY_VNI_MASK		0x1
#define ETH_VPORT_RX_MODE_ACCEPT_ANY_VNI_SHIFT		6
#define ETH_VPORT_RX_MODE_RESERVED1_MASK		0x1FF
#define ETH_VPORT_RX_MODE_RESERVED1_SHIFT		7
};

/* Command for setting tpa parameters */
struct eth_vport_tpa_param {
	u8 tpa_ipv4_en_flg;
	u8 tpa_ipv6_en_flg;
	u8 tpa_ipv4_tunn_en_flg;
	u8 tpa_ipv6_tunn_en_flg;
	u8 tpa_pkt_split_flg;
	u8 tpa_hdr_data_split_flg;
	u8 tpa_gro_consistent_flg;

	u8 tpa_max_aggs_num;

	__le16 tpa_max_size;
	__le16 tpa_min_size_to_start;

	__le16 tpa_min_size_to_cont;
	u8 max_buff_num;
	u8 reserved;
};

/* Command for setting classification flags for a vport $$KEEP_ENDIANNESS$$ */
struct eth_vport_tx_mode {
	__le16 state;
#define ETH_VPORT_TX_MODE_UCAST_DROP_ALL_MASK		0x1
#define ETH_VPORT_TX_MODE_UCAST_DROP_ALL_SHIFT		0
#define ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_TX_MODE_UCAST_ACCEPT_ALL_SHIFT	1
#define ETH_VPORT_TX_MODE_MCAST_DROP_ALL_MASK		0x1
#define ETH_VPORT_TX_MODE_MCAST_DROP_ALL_SHIFT		2
#define ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL_SHIFT	3
#define ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL_MASK		0x1
#define ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL_SHIFT	4
#define ETH_VPORT_TX_MODE_RESERVED1_MASK		0x7FF
#define ETH_VPORT_TX_MODE_RESERVED1_SHIFT		5
};

/* GFT filter update action type */
enum gft_filter_update_action {
	GFT_ADD_FILTER,
	GFT_DELETE_FILTER,
	MAX_GFT_FILTER_UPDATE_ACTION
};

/* Ramrod data for rx add openflow filter */
struct rx_add_openflow_filter_data {
	__le16 action_icid;
	u8 priority;
	u8 reserved0;
	__le32 tenant_id;
	__le16 dst_mac_hi;
	__le16 dst_mac_mid;
	__le16 dst_mac_lo;
	__le16 src_mac_hi;
	__le16 src_mac_mid;
	__le16 src_mac_lo;
	__le16 vlan_id;
	__le16 l2_eth_type;
	u8 ipv4_dscp;
	u8 ipv4_frag_type;
	u8 ipv4_over_ip;
	u8 tenant_id_exists;
	__le32 ipv4_dst_addr;
	__le32 ipv4_src_addr;
	__le16 l4_dst_port;
	__le16 l4_src_port;
};

/* Ramrod data for rx create gft action */
struct rx_create_gft_action_data {
	u8 vport_id;
	u8 reserved[7];
};

/* Ramrod data for rx create openflow action */
struct rx_create_openflow_action_data {
	u8 vport_id;
	u8 reserved[7];
};

/* Ramrod data for rx queue start ramrod */
struct rx_queue_start_ramrod_data {
	__le16 rx_queue_id;
	__le16 num_of_pbl_pages;
	__le16 bd_max_bytes;
	__le16 sb_id;
	u8 sb_index;
	u8 vport_id;
	u8 default_rss_queue_flg;
	u8 complete_cqe_flg;
	u8 complete_event_flg;
	u8 stats_counter_id;
	u8 pin_context;
	u8 pxp_tph_valid_bd;
	u8 pxp_tph_valid_pkt;
	u8 pxp_st_hint;

	__le16 pxp_st_index;
	u8 pmd_mode;

	u8 notify_en;
	u8 toggle_val;

	u8 vf_rx_prod_index;
	u8 vf_rx_prod_use_zone_a;
	u8 reserved[5];
	__le16 reserved1;
	struct regpair cqe_pbl_addr;
	struct regpair bd_base;
	struct regpair reserved2;
};

/* Ramrod data for rx queue stop ramrod */
struct rx_queue_stop_ramrod_data {
	__le16 rx_queue_id;
	u8 complete_cqe_flg;
	u8 complete_event_flg;
	u8 vport_id;
	u8 reserved[3];
};

/* Ramrod data for rx queue update ramrod */
struct rx_queue_update_ramrod_data {
	__le16 rx_queue_id;
	u8 complete_cqe_flg;
	u8 complete_event_flg;
	u8 vport_id;
	u8 set_default_rss_queue;
	u8 reserved[3];
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	__le16 reserved4;
	__le16 reserved5;
	struct regpair reserved6;
};

/* Ramrod data for rx Add UDP Filter */
struct rx_udp_filter_data {
	__le16 action_icid;
	__le16 vlan_id;
	u8 ip_type;
	u8 tenant_id_exists;
	__le16 reserved1;
	__le32 ip_dst_addr[4];
	__le32 ip_src_addr[4];
	__le16 udp_dst_port;
	__le16 udp_src_port;
	__le32 tenant_id;
};

/* Add or delete GFT filter - filter is packet header of type of packet wished
 * to pass certain FW flow.
 */
struct rx_update_gft_filter_data {
	struct regpair pkt_hdr_addr;
	__le16 pkt_hdr_length;
	__le16 action_icid;
	__le16 rx_qid;
	__le16 flow_id;
	__le16 vport_id;
	u8 action_icid_valid;
	u8 rx_qid_valid;
	u8 flow_id_valid;
	u8 filter_action;
	u8 assert_on_error;
	u8 inner_vlan_removal_en;
};

/* Ramrod data for tx queue start ramrod */
struct tx_queue_start_ramrod_data {
	__le16 sb_id;
	u8 sb_index;
	u8 vport_id;
	u8 reserved0;
	u8 stats_counter_id;
	__le16 qm_pq_id;
	u8 flags;
#define TX_QUEUE_START_RAMROD_DATA_DISABLE_OPPORTUNISTIC_MASK	0x1
#define TX_QUEUE_START_RAMROD_DATA_DISABLE_OPPORTUNISTIC_SHIFT	0
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_PKT_DUP_MASK	0x1
#define TX_QUEUE_START_RAMROD_DATA_TEST_MODE_PKT_DUP_SHIFT	1
#define TX_QUEUE_START_RAMROD_DATA_PMD_MODE_MASK		0x1
#define TX_QUEUE_START_RAMROD_DATA_PMD_MODE_SHIFT		2
#define TX_QUEUE_START_RAMROD_DATA_NOTIFY_EN_MASK		0x1
#define TX_QUEUE_START_RAMROD_DATA_NOTIFY_EN_SHIFT		3
#define TX_QUEUE_START_RAMROD_DATA_PIN_CONTEXT_MASK		0x1
#define TX_QUEUE_START_RAMROD_DATA_PIN_CONTEXT_SHIFT		4
#define TX_QUEUE_START_RAMROD_DATA_RESERVED1_MASK		0x7
#define TX_QUEUE_START_RAMROD_DATA_RESERVED1_SHIFT		5
	u8 pxp_st_hint;
	u8 pxp_tph_valid_bd;
	u8 pxp_tph_valid_pkt;
	__le16 pxp_st_index;
	__le16 comp_agg_size;
	__le16 queue_zone_id;
	__le16 reserved2;
	__le16 pbl_size;
	__le16 tx_queue_id;
	__le16 same_as_last_id;
	__le16 reserved[3];
	struct regpair pbl_base_addr;
	struct regpair bd_cons_address;
};

/* Ramrod data for tx queue stop ramrod */
struct tx_queue_stop_ramrod_data {
	__le16 reserved[4];
};

/* Ramrod data for tx queue update ramrod */
struct tx_queue_update_ramrod_data {
	__le16 update_qm_pq_id_flg;
	__le16 qm_pq_id;
	__le32 reserved0;
	struct regpair reserved1[5];
};

/* Inner to Inner VLAN priority map update mode */
enum update_in_to_in_pri_map_mode_enum {
	ETH_IN_TO_IN_PRI_MAP_UPDATE_DISABLED,
	ETH_IN_TO_IN_PRI_MAP_UPDATE_NON_RDMA_TBL,
	ETH_IN_TO_IN_PRI_MAP_UPDATE_RDMA_TBL,
	MAX_UPDATE_IN_TO_IN_PRI_MAP_MODE_ENUM
};

/* Ramrod data for vport update ramrod */
struct vport_filter_update_ramrod_data {
	struct eth_filter_cmd_header filter_cmd_hdr;
	struct eth_filter_cmd filter_cmds[ETH_FILTER_RULES_COUNT];
};

/* Ramrod data for vport start ramrod */
struct vport_start_ramrod_data {
	u8 vport_id;
	u8 sw_fid;
	__le16 mtu;
	u8 drop_ttl0_en;
	u8 inner_vlan_removal_en;
	struct eth_vport_rx_mode rx_mode;
	struct eth_vport_tx_mode tx_mode;
	struct eth_vport_tpa_param tpa_param;
	__le16 default_vlan;
	u8 tx_switching_en;
	u8 anti_spoofing_en;
	u8 default_vlan_en;
	u8 handle_ptp_pkts;
	u8 silent_vlan_removal_en;
	u8 untagged;
	struct eth_tx_err_vals tx_err_behav;
	u8 zero_placement_offset;
	u8 ctl_frame_mac_check_en;
	u8 ctl_frame_ethtype_check_en;
	u8 reserved0;
	u8 reserved1;
	u8 tx_dst_port_mode_config;
	u8 dst_vport_id;
	u8 tx_dst_port_mode;
	u8 dst_vport_id_valid;
	u8 wipe_inner_vlan_pri_en;
	u8 reserved2[2];
	struct eth_in_to_in_pri_map_cfg in_to_in_vlan_pri_map_cfg;
};

/* Ramrod data for vport stop ramrod */
struct vport_stop_ramrod_data {
	u8 vport_id;
	u8 reserved[7];
};

/* Ramrod data for vport update ramrod */
struct vport_update_ramrod_data_cmn {
	u8 vport_id;
	u8 update_rx_active_flg;
	u8 rx_active_flg;
	u8 update_tx_active_flg;
	u8 tx_active_flg;
	u8 update_rx_mode_flg;
	u8 update_tx_mode_flg;
	u8 update_approx_mcast_flg;

	u8 update_rss_flg;
	u8 update_inner_vlan_removal_en_flg;

	u8 inner_vlan_removal_en;
	u8 update_tpa_param_flg;
	u8 update_tpa_en_flg;
	u8 update_tx_switching_en_flg;

	u8 tx_switching_en;
	u8 update_anti_spoofing_en_flg;

	u8 anti_spoofing_en;
	u8 update_handle_ptp_pkts;

	u8 handle_ptp_pkts;
	u8 update_default_vlan_en_flg;

	u8 default_vlan_en;

	u8 update_default_vlan_flg;

	__le16 default_vlan;
	u8 update_accept_any_vlan_flg;

	u8 accept_any_vlan;
	u8 silent_vlan_removal_en;
	u8 update_mtu_flg;

	__le16 mtu;
	u8 update_ctl_frame_checks_en_flg;
	u8 ctl_frame_mac_check_en;
	u8 ctl_frame_ethtype_check_en;
	u8 update_in_to_in_pri_map_mode;
	u8 in_to_in_pri_map[8];
	u8 reserved[6];
};

struct vport_update_ramrod_mcast {
	__le32 bins[ETH_MULTICAST_MAC_BINS_IN_REGS];
};

/* Ramrod data for vport update ramrod */
struct vport_update_ramrod_data {
	struct vport_update_ramrod_data_cmn common;

	struct eth_vport_rx_mode rx_mode;
	struct eth_vport_tx_mode tx_mode;
	__le32 reserved[3];
	struct eth_vport_tpa_param tpa_param;
	struct vport_update_ramrod_mcast approx_mcast;
	struct eth_vport_rss_config rss_config;
};

struct e4_xstorm_eth_conn_ag_ctx_dq_ext_ldpart {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EXIST_IN_QM0_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EXIST_IN_QM0_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED1_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED1_SHIFT		1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED2_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED2_SHIFT		2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EXIST_IN_QM3_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EXIST_IN_QM3_SHIFT	3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED3_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED3_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED4_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED4_SHIFT		5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED5_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED5_SHIFT		6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED6_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED6_SHIFT		7
	u8 flags1;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED7_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED7_SHIFT		0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED8_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED8_SHIFT		1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED9_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED9_SHIFT		2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_BIT11_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_BIT11_SHIFT		3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_E5_RESERVED2_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_E5_RESERVED2_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_E5_RESERVED3_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_E5_RESERVED3_SHIFT	5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TX_RULE_ACTIVE_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TX_RULE_ACTIVE_SHIFT	6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_ACTIVE_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_ACTIVE_SHIFT	7
	u8 flags2;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF0_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF0_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF1_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF1_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF2_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF2_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF3_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF3_SHIFT	6
	u8 flags3;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF4_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF4_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF5_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF5_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF6_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF6_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF7_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF7_SHIFT	6
	u8 flags4;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF8_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF8_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF9_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF9_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF10_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF10_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF11_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF11_SHIFT	6
	u8 flags5;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF12_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF12_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF13_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF13_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF14_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF14_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF15_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF15_SHIFT	6
	u8 flags6;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_GO_TO_BD_CONS_CF_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_GO_TO_BD_CONS_CF_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_MULTI_UNICAST_CF_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_MULTI_UNICAST_CF_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_MASK		0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TERMINATE_CF_MASK	0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TERMINATE_CF_SHIFT	6
	u8 flags7;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_FLUSH_Q0_MASK		0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_FLUSH_Q0_SHIFT		0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED10_MASK		0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED10_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_SLOW_PATH_MASK		0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_SLOW_PATH_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF0EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF0EN_SHIFT		6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF1EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF1EN_SHIFT		7
	u8 flags8;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF2EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF2EN_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF3EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF3EN_SHIFT	1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF4EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF4EN_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF5EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF5EN_SHIFT	3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF6EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF6EN_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF7EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF7EN_SHIFT	5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF8EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF8EN_SHIFT	6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF9EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF9EN_SHIFT	7
	u8 flags9;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF10EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF10EN_SHIFT			0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF11EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF11EN_SHIFT			1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF12EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF12EN_SHIFT			2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF13EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF13EN_SHIFT			3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF14EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF14EN_SHIFT			4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF15EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_CF15EN_SHIFT			5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_GO_TO_BD_CONS_CF_EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_GO_TO_BD_CONS_CF_EN_SHIFT	6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_MULTI_UNICAST_CF_EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_MULTI_UNICAST_CF_EN_SHIFT	7
	u8 flags10;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_EN_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_DQ_CF_EN_SHIFT			0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TERMINATE_CF_EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TERMINATE_CF_EN_SHIFT		1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_FLUSH_Q0_EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_FLUSH_Q0_EN_SHIFT		2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED11_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED11_SHIFT		3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_SLOW_PATH_EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_SLOW_PATH_EN_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TPH_ENABLE_EN_RESERVED_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TPH_ENABLE_EN_RESERVED_SHIFT	5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED12_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED12_SHIFT		6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED13_MASK			0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED13_SHIFT		7
	u8 flags11;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED14_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED14_SHIFT	0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED15_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RESERVED15_SHIFT	1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TX_DEC_RULE_EN_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TX_DEC_RULE_EN_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE5EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE5EN_SHIFT		3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE6EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE6EN_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE7EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE7EN_SHIFT		5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED1_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED1_SHIFT	6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE9EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE9EN_SHIFT		7
	u8 flags12;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE10EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE10EN_SHIFT		0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE11EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE11EN_SHIFT		1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED2_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED2_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED3_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED3_SHIFT	3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE14EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE14EN_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE15EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE15EN_SHIFT		5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE16EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE16EN_SHIFT		6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE17EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE17EN_SHIFT		7
	u8 flags13;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE18EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE18EN_SHIFT		0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE19EN_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_RULE19EN_SHIFT		1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED4_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED4_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED5_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED5_SHIFT	3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED6_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED6_SHIFT	4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED7_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED7_SHIFT	5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED8_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED8_SHIFT	6
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED9_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_USE_EXT_HDR_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_USE_EXT_HDR_SHIFT		0
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_SEND_RAW_L3L4_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_SEND_RAW_L3L4_SHIFT	1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_INBAND_PROP_HDR_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_INBAND_PROP_HDR_SHIFT	2
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_SEND_EXT_TUNNEL_MASK	0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_EDPM_SEND_EXT_TUNNEL_SHIFT	3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_L2_EDPM_ENABLE_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_L2_EDPM_ENABLE_SHIFT		4
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_MASK		0x1
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_SHIFT		5
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TPH_ENABLE_MASK			0x3
#define E4XSTORMETHCONNAGCTXDQEXTLDPART_TPH_ENABLE_SHIFT		6
	u8 edpm_event_id;
	__le16 physical_q0;
	__le16 e5_reserved1;
	__le16 edpm_num_bds;
	__le16 tx_bd_cons;
	__le16 tx_bd_prod;
	__le16 updated_qm_pq_id;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
};

struct e4_mstorm_eth_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	 0
#define E4_MSTORM_ETH_CONN_AG_CTX_BIT1_MASK		0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_BIT1_SHIFT		1
#define E4_MSTORM_ETH_CONN_AG_CTX_CF0_MASK		0x3
#define E4_MSTORM_ETH_CONN_AG_CTX_CF0_SHIFT		2
#define E4_MSTORM_ETH_CONN_AG_CTX_CF1_MASK		0x3
#define E4_MSTORM_ETH_CONN_AG_CTX_CF1_SHIFT		4
#define E4_MSTORM_ETH_CONN_AG_CTX_CF2_MASK		0x3
#define E4_MSTORM_ETH_CONN_AG_CTX_CF2_SHIFT		6
	u8 flags1;
#define E4_MSTORM_ETH_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_MSTORM_ETH_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_MSTORM_ETH_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_MSTORM_ETH_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

struct e4_xstorm_eth_hw_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED1_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED1_SHIFT	1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED2_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED3_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED4_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED4_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED5_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED5_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED6_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED6_SHIFT	7
	u8 flags1;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED7_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED7_SHIFT		0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED8_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED8_SHIFT		1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED9_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED9_SHIFT		2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_BIT11_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_E5_RESERVED2_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_E5_RESERVED2_SHIFT		4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_E5_RESERVED3_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_E5_RESERVED3_SHIFT		5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TX_RULE_ACTIVE_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TX_RULE_ACTIVE_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_ACTIVE_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_ACTIVE_SHIFT		7
	u8 flags2;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF0_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF1_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF2_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF3_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF7_SHIFT	6
	u8 flags4;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF12_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF12_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF13_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF13_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF14_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF14_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF15_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF15_SHIFT	6
	u8 flags6;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_MASK			0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_SHIFT		4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_MASK		0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_SHIFT		6
	u8 flags7;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED10_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED10_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF0EN_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF1EN_SHIFT	7
	u8 flags8;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF2EN_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF3EN_SHIFT	1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF4EN_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF5EN_SHIFT	3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF6EN_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF7EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF7EN_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF8EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF8EN_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF9EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF9EN_SHIFT	7
	u8 flags9;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF10EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF10EN_SHIFT		0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF11EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF11EN_SHIFT		1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF12EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF12EN_SHIFT		2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF13EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF13EN_SHIFT		3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF14EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF14EN_SHIFT		4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF15EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_CF15EN_SHIFT		5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_GO_TO_BD_CONS_CF_EN_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_MULTI_UNICAST_CF_EN_SHIFT	7
	u8 flags10;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_EN_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_DQ_CF_EN_SHIFT			0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TERMINATE_CF_EN_SHIFT		1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_EN_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT			2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED11_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED11_SHIFT			3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_EN_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_SLOW_PATH_EN_SHIFT			4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_EN_RESERVED_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED12_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED12_SHIFT			6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED13_MASK			0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED13_SHIFT			7
	u8 flags11;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED14_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED14_SHIFT		0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED15_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RESERVED15_SHIFT		1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TX_DEC_RULE_EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TX_DEC_RULE_EN_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE5EN_SHIFT		3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE6EN_SHIFT		4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE7EN_SHIFT		5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED1_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED1_SHIFT		6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE9EN_MASK		0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE9EN_SHIFT		7
	u8 flags12;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE10EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE10EN_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE11EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE11EN_SHIFT	1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE14EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE14EN_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE15EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE15EN_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE16EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE16EN_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE17EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE17EN_SHIFT	7
	u8 flags13;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE18EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE18EN_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE19EN_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_RULE19EN_SHIFT	1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED4_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED4_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED5_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED5_SHIFT	3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED6_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED6_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED7_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED7_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED8_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED8_SHIFT	6
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED9_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_USE_EXT_HDR_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_USE_EXT_HDR_SHIFT	0
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_RAW_L3L4_SHIFT	1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_INBAND_PROP_HDR_SHIFT	2
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_EDPM_SEND_EXT_TUNNEL_SHIFT	3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_L2_EDPM_ENABLE_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_L2_EDPM_ENABLE_SHIFT	4
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK	0x1
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT	5
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_MASK		0x3
#define E4_XSTORM_ETH_HW_CONN_AG_CTX_TPH_ENABLE_SHIFT		6
	u8 edpm_event_id;
	__le16 physical_q0;
	__le16 e5_reserved1;
	__le16 edpm_num_bds;
	__le16 tx_bd_cons;
	__le16 tx_bd_prod;
	__le16 updated_qm_pq_id;
	__le16 conn_dpi;
};

/* GFT CAM line struct with fields breakout */
struct gft_cam_line_mapped {
	__le32 camline;
#define GFT_CAM_LINE_MAPPED_VALID_MASK				0x1
#define GFT_CAM_LINE_MAPPED_VALID_SHIFT				0
#define GFT_CAM_LINE_MAPPED_IP_VERSION_MASK			0x1
#define GFT_CAM_LINE_MAPPED_IP_VERSION_SHIFT			1
#define GFT_CAM_LINE_MAPPED_TUNNEL_IP_VERSION_MASK		0x1
#define GFT_CAM_LINE_MAPPED_TUNNEL_IP_VERSION_SHIFT		2
#define GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_MASK		0xF
#define GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_SHIFT		3
#define GFT_CAM_LINE_MAPPED_TUNNEL_TYPE_MASK			0xF
#define GFT_CAM_LINE_MAPPED_TUNNEL_TYPE_SHIFT			7
#define GFT_CAM_LINE_MAPPED_PF_ID_MASK				0xF
#define GFT_CAM_LINE_MAPPED_PF_ID_SHIFT				11
#define GFT_CAM_LINE_MAPPED_IP_VERSION_MASK_MASK		0x1
#define GFT_CAM_LINE_MAPPED_IP_VERSION_MASK_SHIFT		15
#define GFT_CAM_LINE_MAPPED_TUNNEL_IP_VERSION_MASK_MASK		0x1
#define GFT_CAM_LINE_MAPPED_TUNNEL_IP_VERSION_MASK_SHIFT	16
#define GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_MASK_MASK	0xF
#define GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_MASK_SHIFT	17
#define GFT_CAM_LINE_MAPPED_TUNNEL_TYPE_MASK_MASK		0xF
#define GFT_CAM_LINE_MAPPED_TUNNEL_TYPE_MASK_SHIFT		21
#define GFT_CAM_LINE_MAPPED_PF_ID_MASK_MASK			0xF
#define GFT_CAM_LINE_MAPPED_PF_ID_MASK_SHIFT			25
#define GFT_CAM_LINE_MAPPED_RESERVED1_MASK			0x7
#define GFT_CAM_LINE_MAPPED_RESERVED1_SHIFT			29
};


/* Used in gft_profile_key: Indication for ip version */
enum gft_profile_ip_version {
	GFT_PROFILE_IPV4 = 0,
	GFT_PROFILE_IPV6 = 1,
	MAX_GFT_PROFILE_IP_VERSION
};

/* Profile key stucr fot GFT logic in Prs */
struct gft_profile_key {
	__le16 profile_key;
#define GFT_PROFILE_KEY_IP_VERSION_MASK			0x1
#define GFT_PROFILE_KEY_IP_VERSION_SHIFT		0
#define GFT_PROFILE_KEY_TUNNEL_IP_VERSION_MASK		0x1
#define GFT_PROFILE_KEY_TUNNEL_IP_VERSION_SHIFT		1
#define GFT_PROFILE_KEY_UPPER_PROTOCOL_TYPE_MASK	0xF
#define GFT_PROFILE_KEY_UPPER_PROTOCOL_TYPE_SHIFT	2
#define GFT_PROFILE_KEY_TUNNEL_TYPE_MASK		0xF
#define GFT_PROFILE_KEY_TUNNEL_TYPE_SHIFT		6
#define GFT_PROFILE_KEY_PF_ID_MASK			0xF
#define GFT_PROFILE_KEY_PF_ID_SHIFT			10
#define GFT_PROFILE_KEY_RESERVED0_MASK			0x3
#define GFT_PROFILE_KEY_RESERVED0_SHIFT			14
};

/* Used in gft_profile_key: Indication for tunnel type */
enum gft_profile_tunnel_type {
	GFT_PROFILE_NO_TUNNEL = 0,
	GFT_PROFILE_VXLAN_TUNNEL = 1,
	GFT_PROFILE_GRE_MAC_OR_NVGRE_TUNNEL = 2,
	GFT_PROFILE_GRE_IP_TUNNEL = 3,
	GFT_PROFILE_GENEVE_MAC_TUNNEL = 4,
	GFT_PROFILE_GENEVE_IP_TUNNEL = 5,
	MAX_GFT_PROFILE_TUNNEL_TYPE
};

/* Used in gft_profile_key: Indication for protocol type */
enum gft_profile_upper_protocol_type {
	GFT_PROFILE_ROCE_PROTOCOL = 0,
	GFT_PROFILE_RROCE_PROTOCOL = 1,
	GFT_PROFILE_FCOE_PROTOCOL = 2,
	GFT_PROFILE_ICMP_PROTOCOL = 3,
	GFT_PROFILE_ARP_PROTOCOL = 4,
	GFT_PROFILE_USER_TCP_SRC_PORT_1_INNER = 5,
	GFT_PROFILE_USER_TCP_DST_PORT_1_INNER = 6,
	GFT_PROFILE_TCP_PROTOCOL = 7,
	GFT_PROFILE_USER_UDP_DST_PORT_1_INNER = 8,
	GFT_PROFILE_USER_UDP_DST_PORT_2_OUTER = 9,
	GFT_PROFILE_UDP_PROTOCOL = 10,
	GFT_PROFILE_USER_IP_1_INNER = 11,
	GFT_PROFILE_USER_IP_2_OUTER = 12,
	GFT_PROFILE_USER_ETH_1_INNER = 13,
	GFT_PROFILE_USER_ETH_2_OUTER = 14,
	GFT_PROFILE_RAW = 15,
	MAX_GFT_PROFILE_UPPER_PROTOCOL_TYPE
};

/* GFT RAM line struct */
struct gft_ram_line {
	__le32 lo;
#define GFT_RAM_LINE_VLAN_SELECT_MASK			0x3
#define GFT_RAM_LINE_VLAN_SELECT_SHIFT			0
#define GFT_RAM_LINE_TUNNEL_ENTROPHY_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_ENTROPHY_SHIFT		2
#define GFT_RAM_LINE_TUNNEL_TTL_EQUAL_ONE_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_TTL_EQUAL_ONE_SHIFT		3
#define GFT_RAM_LINE_TUNNEL_TTL_MASK			0x1
#define GFT_RAM_LINE_TUNNEL_TTL_SHIFT			4
#define GFT_RAM_LINE_TUNNEL_ETHERTYPE_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_ETHERTYPE_SHIFT		5
#define GFT_RAM_LINE_TUNNEL_DST_PORT_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_DST_PORT_SHIFT		6
#define GFT_RAM_LINE_TUNNEL_SRC_PORT_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_SRC_PORT_SHIFT		7
#define GFT_RAM_LINE_TUNNEL_DSCP_MASK			0x1
#define GFT_RAM_LINE_TUNNEL_DSCP_SHIFT			8
#define GFT_RAM_LINE_TUNNEL_OVER_IP_PROTOCOL_MASK	0x1
#define GFT_RAM_LINE_TUNNEL_OVER_IP_PROTOCOL_SHIFT	9
#define GFT_RAM_LINE_TUNNEL_DST_IP_MASK			0x1
#define GFT_RAM_LINE_TUNNEL_DST_IP_SHIFT		10
#define GFT_RAM_LINE_TUNNEL_SRC_IP_MASK			0x1
#define GFT_RAM_LINE_TUNNEL_SRC_IP_SHIFT		11
#define GFT_RAM_LINE_TUNNEL_PRIORITY_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_PRIORITY_SHIFT		12
#define GFT_RAM_LINE_TUNNEL_PROVIDER_VLAN_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_PROVIDER_VLAN_SHIFT		13
#define GFT_RAM_LINE_TUNNEL_VLAN_MASK			0x1
#define GFT_RAM_LINE_TUNNEL_VLAN_SHIFT			14
#define GFT_RAM_LINE_TUNNEL_DST_MAC_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_DST_MAC_SHIFT		15
#define GFT_RAM_LINE_TUNNEL_SRC_MAC_MASK		0x1
#define GFT_RAM_LINE_TUNNEL_SRC_MAC_SHIFT		16
#define GFT_RAM_LINE_TTL_EQUAL_ONE_MASK			0x1
#define GFT_RAM_LINE_TTL_EQUAL_ONE_SHIFT		17
#define GFT_RAM_LINE_TTL_MASK				0x1
#define GFT_RAM_LINE_TTL_SHIFT				18
#define GFT_RAM_LINE_ETHERTYPE_MASK			0x1
#define GFT_RAM_LINE_ETHERTYPE_SHIFT			19
#define GFT_RAM_LINE_RESERVED0_MASK			0x1
#define GFT_RAM_LINE_RESERVED0_SHIFT			20
#define GFT_RAM_LINE_TCP_FLAG_FIN_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_FIN_SHIFT			21
#define GFT_RAM_LINE_TCP_FLAG_SYN_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_SYN_SHIFT			22
#define GFT_RAM_LINE_TCP_FLAG_RST_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_RST_SHIFT			23
#define GFT_RAM_LINE_TCP_FLAG_PSH_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_PSH_SHIFT			24
#define GFT_RAM_LINE_TCP_FLAG_ACK_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_ACK_SHIFT			25
#define GFT_RAM_LINE_TCP_FLAG_URG_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_URG_SHIFT			26
#define GFT_RAM_LINE_TCP_FLAG_ECE_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_ECE_SHIFT			27
#define GFT_RAM_LINE_TCP_FLAG_CWR_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_CWR_SHIFT			28
#define GFT_RAM_LINE_TCP_FLAG_NS_MASK			0x1
#define GFT_RAM_LINE_TCP_FLAG_NS_SHIFT			29
#define GFT_RAM_LINE_DST_PORT_MASK			0x1
#define GFT_RAM_LINE_DST_PORT_SHIFT			30
#define GFT_RAM_LINE_SRC_PORT_MASK			0x1
#define GFT_RAM_LINE_SRC_PORT_SHIFT			31
	__le32 hi;
#define GFT_RAM_LINE_DSCP_MASK				0x1
#define GFT_RAM_LINE_DSCP_SHIFT				0
#define GFT_RAM_LINE_OVER_IP_PROTOCOL_MASK		0x1
#define GFT_RAM_LINE_OVER_IP_PROTOCOL_SHIFT		1
#define GFT_RAM_LINE_DST_IP_MASK			0x1
#define GFT_RAM_LINE_DST_IP_SHIFT			2
#define GFT_RAM_LINE_SRC_IP_MASK			0x1
#define GFT_RAM_LINE_SRC_IP_SHIFT			3
#define GFT_RAM_LINE_PRIORITY_MASK			0x1
#define GFT_RAM_LINE_PRIORITY_SHIFT			4
#define GFT_RAM_LINE_PROVIDER_VLAN_MASK			0x1
#define GFT_RAM_LINE_PROVIDER_VLAN_SHIFT		5
#define GFT_RAM_LINE_VLAN_MASK				0x1
#define GFT_RAM_LINE_VLAN_SHIFT				6
#define GFT_RAM_LINE_DST_MAC_MASK			0x1
#define GFT_RAM_LINE_DST_MAC_SHIFT			7
#define GFT_RAM_LINE_SRC_MAC_MASK			0x1
#define GFT_RAM_LINE_SRC_MAC_SHIFT			8
#define GFT_RAM_LINE_TENANT_ID_MASK			0x1
#define GFT_RAM_LINE_TENANT_ID_SHIFT			9
#define GFT_RAM_LINE_RESERVED1_MASK			0x3FFFFF
#define GFT_RAM_LINE_RESERVED1_SHIFT			10
};

/* Used in the first 2 bits for gft_ram_line: Indication for vlan mask */
enum gft_vlan_select {
	INNER_PROVIDER_VLAN = 0,
	INNER_VLAN = 1,
	OUTER_PROVIDER_VLAN = 2,
	OUTER_VLAN = 3,
	MAX_GFT_VLAN_SELECT
};

/* The rdma task context of Mstorm */
struct ystorm_rdma_task_st_ctx {
	struct regpair temp[4];
};

struct e4_ystorm_rdma_task_ag_ctx {
	u8 reserved;
	u8 byte1;
	__le16 msem_ctx_upd_seq;
	u8 flags0;
#define E4_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK		0xF
#define E4_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT	0
#define E4_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT		4
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT1_MASK			0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT			5
#define E4_YSTORM_RDMA_TASK_AG_CTX_VALID_MASK			0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_VALID_SHIFT			6
#define E4_YSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_SHIFT		7
	u8 flags1;
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0_MASK		0x3
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT		0
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1_MASK		0x3
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT		2
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_MASK	0x3
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_SHIFT	4
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT		6
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT		7
	u8 flags2;
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT4_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT		0
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT	1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT	2
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT	3
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT	4
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT	5
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT	6
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK		0x1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT	7
	u8 key;
	__le32 mw_cnt_or_qp_id;
	u8 ref_cnt_seq;
	u8 ctx_upd_seq;
	__le16 dif_flags;
	__le16 tx_ref_count;
	__le16 last_used_ltid;
	__le16 parent_mr_lo;
	__le16 parent_mr_hi;
	__le32 fbo_lo;
	__le32 fbo_hi;
};

struct e4_mstorm_rdma_task_ag_ctx {
	u8 reserved;
	u8 byte1;
	__le16 icid;
	u8 flags0;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK		0xF
#define E4_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT	0
#define E4_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT		4
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT1_MASK			0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT			5
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT2_MASK			0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT			6
#define E4_MSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_SHIFT		7
	u8 flags1;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0_MASK	0x3
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT	0
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1_MASK	0x3
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT	2
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2_MASK	0x3
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT	4
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK	0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT	6
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK	0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT	7
	u8 flags2;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT		0
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT	1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT	2
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT	3
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT	4
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT	5
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT	6
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK		0x1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT	7
	u8 key;
	__le32 mw_cnt_or_qp_id;
	u8 ref_cnt_seq;
	u8 ctx_upd_seq;
	__le16 dif_flags;
	__le16 tx_ref_count;
	__le16 last_used_ltid;
	__le16 parent_mr_lo;
	__le16 parent_mr_hi;
	__le32 fbo_lo;
	__le32 fbo_hi;
};

/* The roce task context of Mstorm */
struct mstorm_rdma_task_st_ctx {
	struct regpair temp[4];
};

/* The roce task context of Ustorm */
struct ustorm_rdma_task_st_ctx {
	struct regpair temp[6];
};

struct e4_ustorm_rdma_task_ag_ctx {
	u8 reserved;
	u8 state;
	__le16 icid;
	u8 flags0;
#define E4_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK		0xF
#define E4_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT	0
#define E4_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT		4
#define E4_USTORM_RDMA_TASK_AG_CTX_BIT1_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT			5
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_MASK	0x3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_SHIFT	6
	u8 flags1;
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_MASK	0x3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_SHIFT	0
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_MASK		0x3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_SHIFT		2
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_BLOCK_SIZE_MASK          0x3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_BLOCK_SIZE_SHIFT         4
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_MASK		0x3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_SHIFT		6
	u8 flags2;
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_MASK	0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_SHIFT	0
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED2_MASK		0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED2_SHIFT		1
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED3_MASK		0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED3_SHIFT		2
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED4_MASK               0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED4_SHIFT              3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK		0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT	4
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT		5
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT		6
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT		7
	u8 flags3;
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RXMIT_PROD_CONS_EN_MASK	0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RXMIT_PROD_CONS_EN_SHIFT	0
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT		1
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_PROD_CONS_EN_MASK	0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_PROD_CONS_EN_SHIFT	2
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK			0x1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT		3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_MASK		0xF
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT		4
	__le32 dif_err_intervals;
	__le32 dif_error_1st_interval;
	__le32 dif_rxmit_cons;
	__le32 dif_rxmit_prod;
	__le32 sge_index;
	__le32 sq_cons;
	u8 byte2;
	u8 byte3;
	__le16 dif_write_cons;
	__le16 dif_write_prod;
	__le16 word3;
	__le32 dif_error_buffer_address_lo;
	__le32 dif_error_buffer_address_hi;
};

/* RDMA task context */
struct e4_rdma_task_context {
	struct ystorm_rdma_task_st_ctx ystorm_st_context;
	struct e4_ystorm_rdma_task_ag_ctx ystorm_ag_context;
	struct tdif_task_context tdif_context;
	struct e4_mstorm_rdma_task_ag_ctx mstorm_ag_context;
	struct mstorm_rdma_task_st_ctx mstorm_st_context;
	struct rdif_task_context rdif_context;
	struct ustorm_rdma_task_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
	struct e4_ustorm_rdma_task_ag_ctx ustorm_ag_context;
};

/* rdma function init ramrod data */
struct rdma_close_func_ramrod_data {
	u8 cnq_start_offset;
	u8 num_cnqs;
	u8 vf_id;
	u8 vf_valid;
	u8 reserved[4];
};

/* rdma function init CNQ parameters */
struct rdma_cnq_params {
	__le16 sb_num;
	u8 sb_index;
	u8 num_pbl_pages;
	__le32 reserved;
	struct regpair pbl_base_addr;
	__le16 queue_zone_num;
	u8 reserved1[6];
};

/* rdma create cq ramrod data */
struct rdma_create_cq_ramrod_data {
	struct regpair cq_handle;
	struct regpair pbl_addr;
	__le32 max_cqes;
	__le16 pbl_num_pages;
	__le16 dpi;
	u8 is_two_level_pbl;
	u8 cnq_id;
	u8 pbl_log_page_size;
	u8 toggle_bit;
	__le16 int_timeout;
	u8 vf_id;
	u8 flags;
#define RDMA_CREATE_CQ_RAMROD_DATA_VF_ID_VALID_MASK  0x1
#define RDMA_CREATE_CQ_RAMROD_DATA_VF_ID_VALID_SHIFT 0
#define RDMA_CREATE_CQ_RAMROD_DATA_RESERVED1_MASK    0x7F
#define RDMA_CREATE_CQ_RAMROD_DATA_RESERVED1_SHIFT   1
};

/* rdma deregister tid ramrod data */
struct rdma_deregister_tid_ramrod_data {
	__le32 itid;
	__le32 reserved;
};

/* rdma destroy cq output params */
struct rdma_destroy_cq_output_params {
	__le16 cnq_num;
	__le16 reserved0;
	__le32 reserved1;
};

/* rdma destroy cq ramrod data */
struct rdma_destroy_cq_ramrod_data {
	struct regpair output_params_addr;
};

/* RDMA slow path EQ cmd IDs */
enum rdma_event_opcode {
	RDMA_EVENT_UNUSED,
	RDMA_EVENT_FUNC_INIT,
	RDMA_EVENT_FUNC_CLOSE,
	RDMA_EVENT_REGISTER_MR,
	RDMA_EVENT_DEREGISTER_MR,
	RDMA_EVENT_CREATE_CQ,
	RDMA_EVENT_RESIZE_CQ,
	RDMA_EVENT_DESTROY_CQ,
	RDMA_EVENT_CREATE_SRQ,
	RDMA_EVENT_MODIFY_SRQ,
	RDMA_EVENT_DESTROY_SRQ,
	MAX_RDMA_EVENT_OPCODE
};

/* RDMA FW return code for slow path ramrods */
enum rdma_fw_return_code {
	RDMA_RETURN_OK = 0,
	RDMA_RETURN_REGISTER_MR_BAD_STATE_ERR,
	RDMA_RETURN_DEREGISTER_MR_BAD_STATE_ERR,
	RDMA_RETURN_RESIZE_CQ_ERR,
	RDMA_RETURN_NIG_DRAIN_REQ,
	RDMA_RETURN_GENERAL_ERR,
	MAX_RDMA_FW_RETURN_CODE
};

/* rdma function init header */
struct rdma_init_func_hdr {
	u8 cnq_start_offset;
	u8 num_cnqs;
	u8 cq_ring_mode;
	u8 vf_id;
	u8 vf_valid;
	u8 relaxed_ordering;
	__le16 first_reg_srq_id;
	__le32 reg_srq_base_addr;
	u8 searcher_mode;
	u8 pvrdma_mode;
	u8 max_num_ns_log;
	u8 reserved;
};

/* rdma function init ramrod data */
struct rdma_init_func_ramrod_data {
	struct rdma_init_func_hdr params_header;
	struct rdma_cnq_params cnq_params[NUM_OF_GLOBAL_QUEUES];
};

/* RDMA ramrod command IDs */
enum rdma_ramrod_cmd_id {
	RDMA_RAMROD_UNUSED,
	RDMA_RAMROD_FUNC_INIT,
	RDMA_RAMROD_FUNC_CLOSE,
	RDMA_RAMROD_REGISTER_MR,
	RDMA_RAMROD_DEREGISTER_MR,
	RDMA_RAMROD_CREATE_CQ,
	RDMA_RAMROD_RESIZE_CQ,
	RDMA_RAMROD_DESTROY_CQ,
	RDMA_RAMROD_CREATE_SRQ,
	RDMA_RAMROD_MODIFY_SRQ,
	RDMA_RAMROD_DESTROY_SRQ,
	MAX_RDMA_RAMROD_CMD_ID
};

/* rdma register tid ramrod data */
struct rdma_register_tid_ramrod_data {
	__le16 flags;
#define RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG_MASK	0x1F
#define RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG_SHIFT	0
#define RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL_MASK	0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL_SHIFT	5
#define RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED_SHIFT		6
#define RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR_SHIFT		7
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ_SHIFT		8
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE_SHIFT	9
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC_MASK	0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC_SHIFT	10
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE_SHIFT		11
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ_SHIFT		12
#define RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND_MASK	0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND_SHIFT	13
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED_MASK		0x3
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED_SHIFT		14
	u8 flags1;
#define RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG_MASK	0x1F
#define RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG_SHIFT	0
#define RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE_MASK		0x7
#define RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE_SHIFT		5
	u8 flags2;
#define RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR_MASK		0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR_SHIFT		0
#define RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG_MASK	0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG_SHIFT	1
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED1_MASK		0x3F
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED1_SHIFT		2
	u8 key;
	u8 length_hi;
	u8 vf_id;
	u8 vf_valid;
	__le16 pd;
	__le16 reserved2;
	__le32 length_lo;
	__le32 itid;
	__le32 reserved3;
	struct regpair va;
	struct regpair pbl_base;
	struct regpair dif_error_addr;
	__le32 reserved4[4];
};

/* rdma resize cq output params */
struct rdma_resize_cq_output_params {
	__le32 old_cq_cons;
	__le32 old_cq_prod;
};

/* rdma resize cq ramrod data */
struct rdma_resize_cq_ramrod_data {
	u8 flags;
#define RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_MASK		0x1
#define RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_SHIFT		0
#define RDMA_RESIZE_CQ_RAMROD_DATA_IS_TWO_LEVEL_PBL_MASK	0x1
#define RDMA_RESIZE_CQ_RAMROD_DATA_IS_TWO_LEVEL_PBL_SHIFT	1
#define RDMA_RESIZE_CQ_RAMROD_DATA_VF_ID_VALID_MASK		0x1
#define RDMA_RESIZE_CQ_RAMROD_DATA_VF_ID_VALID_SHIFT		2
#define RDMA_RESIZE_CQ_RAMROD_DATA_RESERVED_MASK		0x1F
#define RDMA_RESIZE_CQ_RAMROD_DATA_RESERVED_SHIFT		3
	u8 pbl_log_page_size;
	__le16 pbl_num_pages;
	__le32 max_cqes;
	struct regpair pbl_addr;
	struct regpair output_params_addr;
	u8 vf_id;
	u8 reserved1[7];
};

/* The rdma SRQ context */
struct rdma_srq_context {
	struct regpair temp[8];
};

/* rdma create qp requester ramrod data */
struct rdma_srq_create_ramrod_data {
	u8 flags;
#define RDMA_SRQ_CREATE_RAMROD_DATA_XRC_FLAG_MASK         0x1
#define RDMA_SRQ_CREATE_RAMROD_DATA_XRC_FLAG_SHIFT        0
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED_KEY_EN_MASK  0x1
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED_KEY_EN_SHIFT 1
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED1_MASK        0x3F
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED1_SHIFT       2
	u8 reserved2;
	__le16 xrc_domain;
	__le32 xrc_srq_cq_cid;
	struct regpair pbl_base_addr;
	__le16 pages_in_srq_pbl;
	__le16 pd_id;
	struct rdma_srq_id srq_id;
	__le16 page_size;
	__le16 reserved3;
	__le32 reserved4;
	struct regpair producers_addr;
};

/* rdma create qp requester ramrod data */
struct rdma_srq_destroy_ramrod_data {
	struct rdma_srq_id srq_id;
	__le32 reserved;
};

/* rdma create qp requester ramrod data */
struct rdma_srq_modify_ramrod_data {
	struct rdma_srq_id srq_id;
	__le32 wqe_limit;
};

/* RDMA Tid type enumeration (for register_tid ramrod) */
enum rdma_tid_type {
	RDMA_TID_REGISTERED_MR,
	RDMA_TID_FMR,
	RDMA_TID_MW,
	MAX_RDMA_TID_TYPE
};

/* The rdma XRC SRQ context */
struct rdma_xrc_srq_context {
	struct regpair temp[9];
};

struct e4_tstorm_rdma_task_ag_ctx {
	u8 byte0;
	u8 byte1;
	__le16 word0;
	u8 flags0;
#define E4_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_MASK		0xF
#define E4_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_SHIFT	0
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT0_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT0_SHIFT		4
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT1_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT		5
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT2_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT		6
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT3_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT3_SHIFT		7
	u8 flags1;
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT4_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT	0
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT5_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT5_SHIFT	1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT	2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT	4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT	6
	u8 flags2;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3_SHIFT	0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4_SHIFT	2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5_SHIFT	4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6_SHIFT	6
	u8 flags3;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7_SHIFT	0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT	2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT	3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT	4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3EN_SHIFT	5
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4EN_SHIFT	6
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5EN_MASK	0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6EN_SHIFT		0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7EN_SHIFT		1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT	2
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT	3
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT	4
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT	5
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT	6
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT	7
	u8 byte2;
	__le16 word1;
	__le32 reg0;
	u8 byte3;
	u8 byte4;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg1;
	__le32 reg2;
};

struct e4_ustorm_rdma_conn_ag_ctx {
	u8 reserved;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_USTORM_RDMA_CONN_AG_CTX_DIF_ERROR_REPORTED_MASK  0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_DIF_ERROR_REPORTED_SHIFT 1
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK	0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT	2
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1_MASK		0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1_SHIFT		4
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2_MASK		0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2_SHIFT		6
	u8 flags1;
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3_MASK		0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3_SHIFT		0
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_MASK	0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT	2
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_MASK	0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_SHIFT	4
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6_MASK		0x3
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6_SHIFT		6
	u8 flags2;
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT		0
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1EN_MASK			0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT			1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT			2
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3EN_MASK			0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3EN_SHIFT			3
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT	4
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT		5
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6EN_MASK			0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT			6
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_SHIFT		7
	u8 flags3;
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_EN_SHIFT		0
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 nvmf_only;
	__le16 conn_dpi;
	__le16 word1;
	__le32 cq_cons;
	__le32 cq_se_prod;
	__le32 cq_prod;
	__le32 reg3;
	__le16 int_timeout;
	__le16 word3;
};

struct e4_xstorm_roce_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM0_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT     0
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT1_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT1_SHIFT             1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT2_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT2_SHIFT             2
#define E4_XSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM3_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT     3
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT4_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT4_SHIFT             4
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT5_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT5_SHIFT             5
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT6_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT6_SHIFT             6
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT7_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT7_SHIFT             7
	u8 flags1;
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT8_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT8_SHIFT             0
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT9_MASK              0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT9_SHIFT             1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT10_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT10_SHIFT            2
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT11_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT11_SHIFT            3
#define E4_XSTORM_ROCE_CONN_AG_CTX_MSDM_FLUSH_MASK        0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_MSDM_FLUSH_SHIFT       4
#define E4_XSTORM_ROCE_CONN_AG_CTX_MSEM_FLUSH_MASK        0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_MSEM_FLUSH_SHIFT       5
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT14_MASK	       0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT14_SHIFT	       6
#define E4_XSTORM_ROCE_CONN_AG_CTX_YSTORM_FLUSH_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF0_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF0_SHIFT              0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF1_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF1_SHIFT              2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF2_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF2_SHIFT              4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF3_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF3_SHIFT              6
	u8 flags3;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF4_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF4_SHIFT              0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF5_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF5_SHIFT              2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF6_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF6_SHIFT              4
#define E4_XSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_MASK       0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF8_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF8_SHIFT              0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF9_MASK               0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF9_SHIFT              2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF10_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF10_SHIFT             4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF11_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF11_SHIFT             6
	u8 flags5;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF12_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF12_SHIFT             0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF13_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF13_SHIFT             2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF14_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF14_SHIFT             4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF15_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF15_SHIFT             6
	u8 flags6;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF16_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF16_SHIFT             0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF17_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF17_SHIFT             2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF18_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF18_SHIFT             4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF19_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF19_SHIFT             6
	u8 flags7;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF20_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF20_SHIFT             0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF21_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF21_SHIFT             2
#define E4_XSTORM_ROCE_CONN_AG_CTX_SLOW_PATH_MASK         0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_SLOW_PATH_SHIFT        4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF0EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF0EN_SHIFT            6
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF1EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF1EN_SHIFT            7
	u8 flags8;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF2EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF2EN_SHIFT            0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF3EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF3EN_SHIFT            1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF4EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF4EN_SHIFT            2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF5EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF5EN_SHIFT            3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF6EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF6EN_SHIFT            4
#define E4_XSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK    0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT   5
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF8EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF8EN_SHIFT            6
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF9EN_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF9EN_SHIFT            7
	u8 flags9;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF10EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF10EN_SHIFT           0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF11EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF11EN_SHIFT           1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF12EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF12EN_SHIFT           2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF13EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF13EN_SHIFT           3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF14EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF14EN_SHIFT           4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF15EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF15EN_SHIFT           5
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF16EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF16EN_SHIFT           6
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF17EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF17EN_SHIFT           7
	u8 flags10;
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF18EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF18EN_SHIFT           0
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF19EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF19EN_SHIFT           1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF20EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF20EN_SHIFT           2
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF21EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF21EN_SHIFT           3
#define E4_XSTORM_ROCE_CONN_AG_CTX_SLOW_PATH_EN_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT     4
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF23EN_MASK            0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF23EN_SHIFT           5
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE0EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE0EN_SHIFT          6
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE1EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE1EN_SHIFT          7
	u8 flags11;
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE2EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE2EN_SHIFT          0
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE3EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE3EN_SHIFT          1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE4EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE4EN_SHIFT          2
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE5EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE5EN_SHIFT          3
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE6EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE6EN_SHIFT          4
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE7EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE7EN_SHIFT          5
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED1_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED1_SHIFT     6
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE9EN_MASK           0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE9EN_SHIFT          7
	u8 flags12;
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE10EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE10EN_SHIFT         0
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE11EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE11EN_SHIFT         1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED2_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED2_SHIFT     2
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED3_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED3_SHIFT     3
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE14EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE14EN_SHIFT         4
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE15EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE15EN_SHIFT         5
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE16EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE16EN_SHIFT         6
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE17EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE17EN_SHIFT         7
	u8 flags13;
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE18EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE18EN_SHIFT         0
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE19EN_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RULE19EN_SHIFT         1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED4_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED4_SHIFT     2
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED5_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED5_SHIFT     3
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED6_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED6_SHIFT     4
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED7_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED7_SHIFT     5
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED8_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED8_SHIFT     6
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED9_MASK      0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E4_XSTORM_ROCE_CONN_AG_CTX_MIGRATION_MASK         0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_MIGRATION_SHIFT        0
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT17_MASK             0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_BIT17_SHIFT            1
#define E4_XSTORM_ROCE_CONN_AG_CTX_DPM_PORT_NUM_MASK      0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_DPM_PORT_NUM_SHIFT     2
#define E4_XSTORM_ROCE_CONN_AG_CTX_RESERVED_MASK          0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_RESERVED_SHIFT         4
#define E4_XSTORM_ROCE_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK  0x1
#define E4_XSTORM_ROCE_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT 5
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF23_MASK              0x3
#define E4_XSTORM_ROCE_CONN_AG_CTX_CF23_SHIFT             6
	u8 byte2;
	__le16 physical_q0;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le16 word5;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 snd_nxt_psn;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
};

struct e4_tstorm_roce_conn_ag_ctx {
	u8 reserved0;
	u8 byte1;
	u8 flags0;
#define E4_TSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM0_MASK          0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT         0
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT1_MASK                  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT1_SHIFT                 1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT2_MASK                  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT2_SHIFT                 2
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT3_MASK                  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT3_SHIFT                 3
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT4_MASK                  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT4_SHIFT                 4
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT5_MASK                  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_BIT5_SHIFT                 5
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF0_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF0_SHIFT                  6
	u8 flags1;
#define E4_TSTORM_ROCE_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK       0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT      0
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF2_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF2_SHIFT                  2
#define E4_TSTORM_ROCE_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK     0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT    4
#define E4_TSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_MASK           0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT          6
	u8 flags2;
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF5_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF5_SHIFT                  0
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF6_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF6_SHIFT                  2
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF7_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF7_SHIFT                  4
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF8_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF8_SHIFT                  6
	u8 flags3;
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF9_MASK                   0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF9_SHIFT                  0
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF10_MASK                  0x3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF10_SHIFT                 2
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF0EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF0EN_SHIFT                4
#define E4_TSTORM_ROCE_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK    0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT   5
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF2EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF2EN_SHIFT                6
#define E4_TSTORM_ROCE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK  0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT 7
	u8 flags4;
#define E4_TSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK        0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT       0
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF5EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF5EN_SHIFT                1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF6EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF6EN_SHIFT                2
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF7EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF7EN_SHIFT                3
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF8EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF8EN_SHIFT                4
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF9EN_MASK                 0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF9EN_SHIFT                5
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF10EN_MASK                0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_CF10EN_SHIFT               6
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE0EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags5;
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE1EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE1EN_SHIFT              0
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE2EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE2EN_SHIFT              1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE3EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE3EN_SHIFT              2
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE4EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE4EN_SHIFT              3
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE5EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE5EN_SHIFT              4
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE6EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE6EN_SHIFT              5
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE7EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE7EN_SHIFT              6
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE8EN_MASK               0x1
#define E4_TSTORM_ROCE_CONN_AG_CTX_RULE8EN_SHIFT              7
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 byte2;
	u8 byte3;
	__le16 word0;
	u8 byte4;
	u8 byte5;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le32 reg9;
	__le32 reg10;
};

/* The roce storm context of Ystorm */
struct ystorm_roce_conn_st_ctx {
	struct regpair temp[2];
};

/* The roce storm context of Mstorm */
struct pstorm_roce_conn_st_ctx {
	struct regpair temp[16];
};

/* The roce storm context of Xstorm */
struct xstorm_roce_conn_st_ctx {
	struct regpair temp[24];
};

/* The roce storm context of Tstorm */
struct tstorm_roce_conn_st_ctx {
	struct regpair temp[30];
};

/* The roce storm context of Mstorm */
struct mstorm_roce_conn_st_ctx {
	struct regpair temp[6];
};

/* The roce storm context of Ustorm */
struct ustorm_roce_conn_st_ctx {
	struct regpair temp[14];
};

/* roce connection context */
struct e4_roce_conn_context {
	struct ystorm_roce_conn_st_ctx ystorm_st_context;
	struct regpair ystorm_st_padding[2];
	struct pstorm_roce_conn_st_ctx pstorm_st_context;
	struct xstorm_roce_conn_st_ctx xstorm_st_context;
	struct e4_xstorm_roce_conn_ag_ctx xstorm_ag_context;
	struct e4_tstorm_roce_conn_ag_ctx tstorm_ag_context;
	struct timers_context timer_context;
	struct e4_ustorm_rdma_conn_ag_ctx ustorm_ag_context;
	struct tstorm_roce_conn_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
	struct mstorm_roce_conn_st_ctx mstorm_st_context;
	struct regpair mstorm_st_padding[2];
	struct ustorm_roce_conn_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
};

/* roce cqes statistics */
struct roce_cqe_stats {
	__le32 req_cqe_error;
	__le32 req_remote_access_errors;
	__le32 req_remote_invalid_request;
	__le32 resp_cqe_error;
	__le32 resp_local_length_error;
	__le32 reserved;
};

/* roce create qp requester ramrod data */
struct roce_create_qp_req_ramrod_data {
	__le16 flags;
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ROCE_FLAVOR_MASK			0x3
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ROCE_FLAVOR_SHIFT		0
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_FMR_AND_RESERVED_EN_MASK		0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_FMR_AND_RESERVED_EN_SHIFT	2
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_SIGNALED_COMP_MASK		0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_SIGNALED_COMP_SHIFT		3
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_MASK				0x7
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_SHIFT			4
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_XRC_FLAG_MASK			0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_XRC_FLAG_SHIFT			7
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_MASK		0xF
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_SHIFT		8
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_MASK			0xF
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_SHIFT		12
	u8 max_ord;
	u8 traffic_class;
	u8 hop_limit;
	u8 orq_num_pages;
	__le16 p_key;
	__le32 flow_label;
	__le32 dst_qp_id;
	__le32 ack_timeout_val;
	__le32 initial_psn;
	__le16 mtu;
	__le16 pd;
	__le16 sq_num_pages;
	__le16 low_latency_phy_queue;
	struct regpair sq_pbl_addr;
	struct regpair orq_pbl_addr;
	__le16 local_mac_addr[3];
	__le16 remote_mac_addr[3];
	__le16 vlan_id;
	__le16 udp_src_port;
	__le32 src_gid[4];
	__le32 dst_gid[4];
	__le32 cq_cid;
	struct regpair qp_handle_for_cqe;
	struct regpair qp_handle_for_async;
	u8 stats_counter_id;
	u8 vf_id;
	u8 vport_id;
	u8 flags2;
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_EDPM_MODE_MASK			0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_EDPM_MODE_SHIFT			0
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_VF_ID_VALID_MASK			0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_VF_ID_VALID_SHIFT		1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RESERVED_MASK			0x3F
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RESERVED_SHIFT			2
	u8 name_space;
	u8 reserved3[3];
	__le16 regular_latency_phy_queue;
	__le16 dpi;
};

/* roce create qp responder ramrod data */
struct roce_create_qp_resp_ramrod_data {
	__le32 flags;
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ROCE_FLAVOR_MASK		0x3
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ROCE_FLAVOR_SHIFT		0
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_RD_EN_MASK			0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_RD_EN_SHIFT		2
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_WR_EN_MASK			0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_WR_EN_SHIFT		3
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ATOMIC_EN_MASK			0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ATOMIC_EN_SHIFT			4
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_SRQ_FLG_MASK			0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_SRQ_FLG_SHIFT			5
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN_MASK	0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN_SHIFT	6
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_KEY_EN_MASK		0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_KEY_EN_SHIFT		7
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_MASK			0x7
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_SHIFT			8
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_MASK		0x1F
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_SHIFT		11
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_XRC_FLAG_MASK             0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_XRC_FLAG_SHIFT            16
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_VF_ID_VALID_MASK	0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_VF_ID_VALID_SHIFT	17
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_MASK		0x3FFF
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_SHIFT		18
	__le16 xrc_domain;
	u8 max_ird;
	u8 traffic_class;
	u8 hop_limit;
	u8 irq_num_pages;
	__le16 p_key;
	__le32 flow_label;
	__le32 dst_qp_id;
	u8 stats_counter_id;
	u8 reserved1;
	__le16 mtu;
	__le32 initial_psn;
	__le16 pd;
	__le16 rq_num_pages;
	struct rdma_srq_id srq_id;
	struct regpair rq_pbl_addr;
	struct regpair irq_pbl_addr;
	__le16 local_mac_addr[3];
	__le16 remote_mac_addr[3];
	__le16 vlan_id;
	__le16 udp_src_port;
	__le32 src_gid[4];
	__le32 dst_gid[4];
	struct regpair qp_handle_for_cqe;
	struct regpair qp_handle_for_async;
	__le16 low_latency_phy_queue;
	u8 vf_id;
	u8 vport_id;
	__le32 cq_cid;
	__le16 regular_latency_phy_queue;
	__le16 dpi;
	__le32 src_qp_id;
	u8 name_space;
	u8 reserved3[3];
};

/* roce DCQCN received statistics */
struct roce_dcqcn_received_stats {
	struct regpair ecn_pkt_rcv;
	struct regpair cnp_pkt_rcv;
};

/* roce DCQCN sent statistics */
struct roce_dcqcn_sent_stats {
	struct regpair cnp_pkt_sent;
};

/* RoCE destroy qp requester output params */
struct roce_destroy_qp_req_output_params {
	__le32 cq_prod;
	__le32 reserved;
};

/* RoCE destroy qp requester ramrod data */
struct roce_destroy_qp_req_ramrod_data {
	struct regpair output_params_addr;
};

/* RoCE destroy qp responder output params */
struct roce_destroy_qp_resp_output_params {
	__le32 cq_prod;
	__le32 reserved;
};

/* RoCE destroy qp responder ramrod data */
struct roce_destroy_qp_resp_ramrod_data {
	struct regpair output_params_addr;
	__le32 src_qp_id;
	__le32 reserved;
};

/* roce error statistics */
struct roce_error_stats {
	__le32 resp_remote_access_errors;
	__le32 reserved;
};

/* roce special events statistics */
struct roce_events_stats {
	__le32 silent_drops;
	__le32 rnr_naks_sent;
	__le32 retransmit_count;
	__le32 icrc_error_count;
	__le32 implied_nak_seq_err;
	__le32 duplicate_request;
	__le32 local_ack_timeout_err;
	__le32 out_of_sequence;
	__le32 packet_seq_err;
	__le32 rnr_nak_retry_err;
};

/* roce slow path EQ cmd IDs */
enum roce_event_opcode {
	ROCE_EVENT_CREATE_QP = 11,
	ROCE_EVENT_MODIFY_QP,
	ROCE_EVENT_QUERY_QP,
	ROCE_EVENT_DESTROY_QP,
	ROCE_EVENT_CREATE_UD_QP,
	ROCE_EVENT_DESTROY_UD_QP,
	ROCE_EVENT_FUNC_UPDATE,
	MAX_ROCE_EVENT_OPCODE
};

/* roce func init ramrod data */
struct roce_init_func_params {
	u8 ll2_queue_id;
	u8 cnp_vlan_priority;
	u8 cnp_dscp;
	u8 flags;
#define ROCE_INIT_FUNC_PARAMS_DCQCN_NP_EN_MASK		0x1
#define ROCE_INIT_FUNC_PARAMS_DCQCN_NP_EN_SHIFT		0
#define ROCE_INIT_FUNC_PARAMS_DCQCN_RP_EN_MASK		0x1
#define ROCE_INIT_FUNC_PARAMS_DCQCN_RP_EN_SHIFT		1
#define ROCE_INIT_FUNC_PARAMS_RESERVED0_MASK		0x3F
#define ROCE_INIT_FUNC_PARAMS_RESERVED0_SHIFT		2
	__le32 cnp_send_timeout;
	__le16 rl_offset;
	u8 rl_count_log;
	u8 reserved1[5];
};

/* roce func init ramrod data */
struct roce_init_func_ramrod_data {
	struct rdma_init_func_ramrod_data rdma;
	struct roce_init_func_params roce;
};

/* roce modify qp requester ramrod data */
struct roce_modify_qp_req_ramrod_data {
	__le16 flags;
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_ERR_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_ERR_FLG_SHIFT		0
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_SQD_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_SQD_FLG_SHIFT		1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_EN_SQD_ASYNC_NOTIFY_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_EN_SQD_ASYNC_NOTIFY_SHIFT	2
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_P_KEY_FLG_MASK			0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_P_KEY_FLG_SHIFT			3
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ADDRESS_VECTOR_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ADDRESS_VECTOR_FLG_SHIFT		4
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MAX_ORD_FLG_MASK			0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MAX_ORD_FLG_SHIFT		5
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_FLG_SHIFT		6
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_FLG_SHIFT		7
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ACK_TIMEOUT_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ACK_TIMEOUT_FLG_SHIFT		8
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_MASK			0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_SHIFT			9
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_MASK				0x7
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_SHIFT			10
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PHYSICAL_QUEUE_FLG_MASK		0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PHYSICAL_QUEUE_FLG_SHIFT		13
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RESERVED1_MASK			0x3
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RESERVED1_SHIFT			14
	u8 fields;
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_MASK	0xF
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_SHIFT	0
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_MASK		0xF
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_SHIFT	4
	u8 max_ord;
	u8 traffic_class;
	u8 hop_limit;
	__le16 p_key;
	__le32 flow_label;
	__le32 ack_timeout_val;
	__le16 mtu;
	__le16 reserved2;
	__le32 reserved3[2];
	__le16 low_latency_phy_queue;
	__le16 regular_latency_phy_queue;
	__le32 src_gid[4];
	__le32 dst_gid[4];
};

/* roce modify qp responder ramrod data */
struct roce_modify_qp_resp_ramrod_data {
	__le16 flags;
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MOVE_TO_ERR_FLG_MASK		0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MOVE_TO_ERR_FLG_SHIFT		0
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_RD_EN_MASK			0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_RD_EN_SHIFT		1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_WR_EN_MASK			0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_WR_EN_SHIFT		2
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ATOMIC_EN_MASK			0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ATOMIC_EN_SHIFT			3
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_P_KEY_FLG_MASK			0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_P_KEY_FLG_SHIFT			4
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ADDRESS_VECTOR_FLG_MASK		0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ADDRESS_VECTOR_FLG_SHIFT	5
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MAX_IRD_FLG_MASK		0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MAX_IRD_FLG_SHIFT		6
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_MASK			0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_SHIFT			7
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_FLG_MASK	0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_FLG_SHIFT	8
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_OPS_EN_FLG_MASK		0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_OPS_EN_FLG_SHIFT		9
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_MASK		0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_SHIFT	10
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RESERVED1_MASK			0x1F
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RESERVED1_SHIFT			11
	u8 fields;
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_MASK		0x7
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_SHIFT		0
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_MASK	0x1F
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_SHIFT	3
	u8 max_ird;
	u8 traffic_class;
	u8 hop_limit;
	__le16 p_key;
	__le32 flow_label;
	__le16 mtu;
	__le16 low_latency_phy_queue;
	__le16 regular_latency_phy_queue;
	u8 reserved2[6];
	__le32 src_gid[4];
	__le32 dst_gid[4];
};

/* RoCE query qp requester output params */
struct roce_query_qp_req_output_params {
	__le32 psn;
	__le32 flags;
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_ERR_FLG_MASK		0x1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_ERR_FLG_SHIFT		0
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_SQ_DRAINING_FLG_MASK	0x1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_SQ_DRAINING_FLG_SHIFT	1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_RESERVED0_MASK		0x3FFFFFFF
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_RESERVED0_SHIFT		2
};

/* RoCE query qp requester ramrod data */
struct roce_query_qp_req_ramrod_data {
	struct regpair output_params_addr;
};

/* RoCE query qp responder output params */
struct roce_query_qp_resp_output_params {
	__le32 psn;
	__le32 flags;
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_ERROR_FLG_MASK  0x1
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_ERROR_FLG_SHIFT 0
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_RESERVED0_MASK  0x7FFFFFFF
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_RESERVED0_SHIFT 1
};

/* RoCE query qp responder ramrod data */
struct roce_query_qp_resp_ramrod_data {
	struct regpair output_params_addr;
};

/* ROCE ramrod command IDs */
enum roce_ramrod_cmd_id {
	ROCE_RAMROD_CREATE_QP = 11,
	ROCE_RAMROD_MODIFY_QP,
	ROCE_RAMROD_QUERY_QP,
	ROCE_RAMROD_DESTROY_QP,
	ROCE_RAMROD_CREATE_UD_QP,
	ROCE_RAMROD_DESTROY_UD_QP,
	ROCE_RAMROD_FUNC_UPDATE,
	MAX_ROCE_RAMROD_CMD_ID
};

/* RoCE func init ramrod data */
struct roce_update_func_params {
	u8 cnp_vlan_priority;
	u8 cnp_dscp;
	__le16 flags;
#define ROCE_UPDATE_FUNC_PARAMS_DCQCN_NP_EN_MASK	0x1
#define ROCE_UPDATE_FUNC_PARAMS_DCQCN_NP_EN_SHIFT	0
#define ROCE_UPDATE_FUNC_PARAMS_DCQCN_RP_EN_MASK	0x1
#define ROCE_UPDATE_FUNC_PARAMS_DCQCN_RP_EN_SHIFT	1
#define ROCE_UPDATE_FUNC_PARAMS_RESERVED0_MASK		0x3FFF
#define ROCE_UPDATE_FUNC_PARAMS_RESERVED0_SHIFT		2
	__le32 cnp_send_timeout;
};

struct e4_xstorm_roce_conn_ag_ctx_dq_ext_ld_part {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT1_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT1_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT2_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT2_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_SHIFT	3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT4_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT4_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT5_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT5_SHIFT		5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT6_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT6_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT7_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT7_SHIFT		7
	u8 flags1;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT8_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT8_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT9_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT9_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_SHIFT		3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSDM_FLUSH_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSDM_FLUSH_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSEM_FLUSH_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSEM_FLUSH_SHIFT	5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT14_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT14_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_SHIFT	7
	u8 flags2;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3_SHIFT	6
	u8 flags3;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_SHIFT	6
	u8 flags4;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11_SHIFT	6
	u8 flags5;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15_SHIFT	6
	u8 flags6;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19_SHIFT	6
	u8 flags7;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_SHIFT		7
	u8 flags8;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4EN_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5EN_SHIFT		3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6EN_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_SHIFT	5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_SHIFT		7
	u8 flags9;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_SHIFT	1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_SHIFT	3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14EN_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_SHIFT	5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_SHIFT	6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_SHIFT	7
	u8 flags10;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_SHIFT		3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_SHIFT		5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_SHIFT		7
	u8 flags11;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_SHIFT		2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_SHIFT		3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE7EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE7EN_SHIFT		5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_SHIFT	6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_SHIFT		7
	u8 flags12;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE10EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE10EN_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_SHIFT	3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE14EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE14EN_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_SHIFT		5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE16EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE16EN_SHIFT		6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE17EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE17EN_SHIFT		7
	u8 flags13;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_SHIFT		0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_SHIFT	3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_SHIFT	4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_SHIFT	5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_SHIFT	6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_SHIFT	0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_SHIFT		1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_MASK	0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_SHIFT	2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_MASK		0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_SHIFT		4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_MASK	0x1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_SHIFT	5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23_MASK		0x3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23_SHIFT		6
	u8 byte2;
	__le16 physical_q0;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le16 word5;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 snd_nxt_psn;
	__le32 reg4;
};

struct e4_mstorm_roce_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_ROCE_CONN_AG_CTX_BIT0_MASK     0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_ROCE_CONN_AG_CTX_BIT1_MASK     0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF0_MASK      0x3
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF1_MASK      0x3
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF2_MASK      0x3
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF0EN_MASK    0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF1EN_MASK    0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF2EN_MASK    0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE0EN_MASK  0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE1EN_MASK  0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE2EN_MASK  0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE3EN_MASK  0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE4EN_MASK  0x1
#define E4_MSTORM_ROCE_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

struct e4_mstorm_roce_req_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK		0x3
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT	2
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK		0x3
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT	4
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK		0x3
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

struct e4_mstorm_roce_resp_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK	0x3
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT	2
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK	0x3
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT	4
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK	0x3
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

struct e4_tstorm_roce_req_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT		0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURRED_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURRED_SHIFT		1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURRED_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURRED_SHIFT	2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_MASK			0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_SHIFT			3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_SHIFT		4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_MASK			0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_SHIFT			5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_MASK			0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_SHIFT			6
	u8 flags1;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK             0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT            0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_MASK			0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_SHIFT		2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK		0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT		4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK			0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT		6
	u8 flags2;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FORCE_COMP_CF_MASK               0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FORCE_COMP_CF_SHIFT              0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_MASK	0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_SHIFT	2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_MASK	0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_SHIFT	4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_MASK	0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_SHIFT	6
	u8 flags3;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_MASK	0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_SHIFT	0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_MASK	0x3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_SHIFT	2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_MASK			0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_SHIFT		4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK          0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT         5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_SHIFT		6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT		0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FORCE_COMP_CF_EN_MASK            0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FORCE_COMP_CF_EN_SHIFT           1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_SHIFT		2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_SHIFT	3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_SHIFT		4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_SHIFT	5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_SHIFT	6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT			7
	u8 flags5;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT		0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_DIF_CNT_EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_DIF_CNT_EN_SHIFT		1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT		2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT		3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT		4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_MASK	0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_SHIFT	5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT		6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT		7
	__le32 dif_rxmit_cnt;
	__le32 snd_nxt_psn;
	__le32 snd_max_psn;
	__le32 orq_prod;
	__le32 reg4;
	__le32 dif_acked_cnt;
	__le32 dif_cnt;
	__le32 reg7;
	__le32 reg8;
	u8 tx_cqe_error_type;
	u8 orq_cache_idx;
	__le16 snd_sq_cons_th;
	u8 byte4;
	u8 byte5;
	__le16 snd_sq_cons;
	__le16 conn_dpi;
	__le16 force_comp_cons;
	__le32 dif_rxmit_acked_cnt;
	__le32 reg10;
};

struct e4_tstorm_roce_resp_conn_ag_ctx {
	u8 byte0;
	u8 state;
	u8 flags0;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT		0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_MASK	0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_SHIFT	1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_SHIFT			2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_SHIFT			3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_SHIFT		4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_SHIFT			5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK			0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT			6
	u8 flags1;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK            0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT           0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_MASK	0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_SHIFT	2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT		4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK	0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT	6
	u8 flags2;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK                0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT               0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT		2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_SHIFT		4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT		6
	u8 flags3;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT		0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK		0x3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT		2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT		4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK         0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT        5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_MASK	0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_SHIFT	6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT		7
	u8 flags4;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT		0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK             0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT            1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT			2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_SHIFT			3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT			4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT			5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT			6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT			7
	u8 flags5;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT		0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT		1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT		2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT		3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT		4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_SHIFT	5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT		6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT		7
	__le32 psn_and_rxmit_id_echo;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 tx_async_error_type;
	u8 byte3;
	__le16 rq_cons;
	u8 byte4;
	u8 byte5;
	__le16 rq_prod;
	__le16 conn_dpi;
	__le16 irq_cons;
	__le32 reg9;
	__le32 reg10;
};

struct e4_ustorm_roce_req_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT	2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT	4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT	0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_SHIFT	2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_SHIFT	4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_MASK		0x3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_SHIFT	6
	u8 flags2;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT	3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_SHIFT	4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_SHIFT	5
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_SHIFT	6
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags3;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

struct e4_ustorm_roce_resp_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT	2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT	4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT	0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_SHIFT	2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_SHIFT	4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK	0x3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT	6
	u8 flags2;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT	3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_SHIFT	4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_SHIFT	5
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT	6
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags3;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

struct e4_xstorm_roce_req_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_SHIFT		4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_SHIFT		5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_SHIFT		6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_SHIFT		7
	u8 flags1;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MSDM_FLUSH_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MSDM_FLUSH_SHIFT		4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MSEM_FLUSH_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MSEM_FLUSH_SHIFT		5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_SHIFT	6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_SHIFT	7
	u8 flags2;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT	6
	u8 flags4;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DIF_ERROR_CF_MASK        0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DIF_ERROR_CF_SHIFT       0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SCAN_SQ_FOR_COMP_CF_MASK     0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SCAN_SQ_FOR_COMP_CF_SHIFT    2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_SHIFT		6
	u8 flags6;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_SHIFT	6
	u8 flags7;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT	6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT	7
	u8 flags8;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT	3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT	5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DIF_ERROR_CF_EN_MASK     0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DIF_ERROR_CF_EN_SHIFT    6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SCAN_SQ_FOR_COMP_CF_EN_MASK  0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SCAN_SQ_FOR_COMP_CF_EN_SHIFT 7
	u8 flags9;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_SHIFT		3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_SHIFT		5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_SHIFT		6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_SHIFT		7
	u8 flags10;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_SHIFT		3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_SHIFT		5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT		6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT		7
	u8 flags11;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT		2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT		3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT		4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_SHIFT	5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_SHIFT		7
	u8 flags12;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_SHIFT		5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_SHIFT	6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_SHIFT	7
	u8 flags13;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_SHIFT		0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_SHIFT	3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_SHIFT	4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_SHIFT	5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_SHIFT	6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_SHIFT	0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_SHIFT		1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_MASK	0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_SHIFT	2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_MASK		0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_SHIFT		4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK	0x1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT	5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_MASK		0x3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_SHIFT		6
	u8 byte2;
	__le16 physical_q0;
	__le16 word1;
	__le16 sq_cmp_cons;
	__le16 sq_cons;
	__le16 sq_prod;
	__le16 dif_error_first_sq_cons;
	__le16 conn_dpi;
	u8 dif_error_sge_index;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 lsn;
	__le32 ssn;
	__le32 snd_una_psn;
	__le32 snd_nxt_psn;
	__le32 dif_error_offset;
	__le32 orq_cons_th;
	__le32 orq_cons;
};

struct e4_xstorm_roce_resp_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_SHIFT		2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_SHIFT		4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_SHIFT		5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_SHIFT		6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_SHIFT		7
	u8 flags1;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_SHIFT		2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_MSDM_FLUSH_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_MSDM_FLUSH_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_MSEM_FLUSH_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_MSEM_FLUSH_SHIFT	5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_SHIFT	6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_SHIFT	7
	u8 flags2;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_MASK		0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT	6
	u8 flags4;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_SHIFT	6
	u8 flags6;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_SHIFT	6
	u8 flags7;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT	6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT	7
	u8 flags8;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT	5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT		6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT		7
	u8 flags9;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_SHIFT	1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_SHIFT	5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_SHIFT	6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_SHIFT	7
	u8 flags10;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_SHIFT		2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_SHIFT		3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_SHIFT		5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT		6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT		7
	u8 flags11;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT		2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT		3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT		4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT		5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_SHIFT		7
	u8 flags12;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE11EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE11EN_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_SHIFT		4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_SHIFT		5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_SHIFT		6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_SHIFT		7
	u8 flags13;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_SHIFT		0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_MASK		0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_SHIFT		1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_SHIFT	5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_SHIFT	6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_SHIFT	7
	u8 flags14;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_SHIFT	0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_SHIFT	1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_SHIFT	2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_SHIFT	3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_SHIFT	4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_MASK	0x1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_SHIFT	5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_MASK	0x3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_SHIFT	6
	u8 byte2;
	__le16 physical_q0;
	__le16 irq_prod_shadow;
	__le16 word2;
	__le16 irq_cons;
	__le16 irq_prod;
	__le16 e5_reserved1;
	__le16 conn_dpi;
	u8 rxmit_opcode;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 rxmit_psn_and_id;
	__le32 rxmit_bytes_length;
	__le32 psn;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 msn_and_syndrome;
};

struct e4_ystorm_roce_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_ROCE_CONN_AG_CTX_BIT0_MASK     0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_ROCE_CONN_AG_CTX_BIT1_MASK     0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF0_MASK      0x3
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF1_MASK      0x3
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF2_MASK      0x3
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF0EN_MASK    0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF1EN_MASK    0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF2EN_MASK    0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE0EN_MASK  0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE1EN_MASK  0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE2EN_MASK  0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE3EN_MASK  0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE4EN_MASK  0x1
#define E4_YSTORM_ROCE_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

struct e4_ystorm_roce_req_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK		0x3
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK		0x3
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK		0x3
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

struct e4_ystorm_roce_resp_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK	0x3
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK	0x3
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK	0x3
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT	0
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT	1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT	2
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

/* Roce doorbell data */
enum roce_flavor {
	PLAIN_ROCE,
	RROCE_IPV4,
	RROCE_IPV6,
	MAX_ROCE_FLAVOR
};

/* The iwarp storm context of Ystorm */
struct ystorm_iwarp_conn_st_ctx {
	__le32 reserved[4];
};

/* The iwarp storm context of Pstorm */
struct pstorm_iwarp_conn_st_ctx {
	__le32 reserved[36];
};

/* The iwarp storm context of Xstorm */
struct xstorm_iwarp_conn_st_ctx {
	__le32 reserved[48];
};

struct e4_xstorm_iwarp_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_SHIFT	1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM2_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM2_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT4_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT		4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_SHIFT	5
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT6_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT6_SHIFT		6
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT7_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT7_SHIFT		7
	u8 flags1;
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT8_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT8_SHIFT				0
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT9_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT9_SHIFT				1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT10_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT10_SHIFT				2
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT11_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT11_SHIFT				3
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT12_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT12_SHIFT				4
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT13_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT13_SHIFT				5
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT14_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT14_SHIFT				6
#define E4_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_SHIFT 7
	u8 flags2;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0_MASK			0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT			0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1_MASK			0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT			2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2_MASK			0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT			4
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK		0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT	6
	u8 flags3;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT	6
	u8 flags4;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12_MASK		0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12_SHIFT		0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13_MASK		0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13_SHIFT		2
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15_MASK		0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15_SHIFT		6
	u8 flags6;
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_SHIFT 0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17_MASK				0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17_SHIFT				2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18_MASK				0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18_SHIFT				4
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_MASK			0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_SHIFT			6
	u8 flags7;
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT		6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT		7
	u8 flags8;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT			0
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT	1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT			2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT			3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT			4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT			5
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT			6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9EN_SHIFT			7
	u8 flags9;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10EN_SHIFT			0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11EN_SHIFT			1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12EN_SHIFT			2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13EN_SHIFT			3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT		4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15EN_SHIFT			5
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_MASK 0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_SHIFT 6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17EN_MASK				0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17EN_SHIFT			7
	u8 flags10;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18EN_MASK			0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18EN_SHIFT		0
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT		1
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT		2
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_SHIFT		3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT		4
#define E4_XSTORM_IWARP_CONN_AG_CTX_SEND_TERMINATE_CF_EN_MASK               0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_SEND_TERMINATE_CF_EN_SHIFT              5
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT		6
#define E4_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_SHIFT	7
	u8 flags11;
#define E4_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT	1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT	3
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT	5
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_SHIFT	7
	u8 flags12;
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_SHIFT		1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_SHIFT		2
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_SHIFT		3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_SHIFT		5
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_SHIFT		6
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_SHIFT		7
	u8 flags13;
#define E4_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_SHIFT	0
#define E4_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_SHIFT	1
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_SHIFT	2
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_SHIFT		3
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_SHIFT		4
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_SHIFT	5
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_SHIFT		6
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_SHIFT		7
	u8 flags14;
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT16_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT16_SHIFT		0
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT17_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT17_SHIFT		1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT18_MASK		0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT18_SHIFT		2
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED1_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED1_SHIFT	3
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED2_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED2_SHIFT	4
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED3_MASK	0x1
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED3_SHIFT	5
#define E4_XSTORM_IWARP_CONN_AG_CTX_SEND_TERMINATE_CF_MASK	0x3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SEND_TERMINATE_CF_SHIFT	6
	u8 byte2;
	__le16 physical_q0;
	__le16 physical_q1;
	__le16 sq_comp_cons;
	__le16 sq_tx_cons;
	__le16 sq_prod;
	__le16 word5;
	__le16 conn_dpi;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 more_to_send_seq;
	__le32 reg4;
	__le32 rewinded_snd_max_or_term_opcode;
	__le32 rd_msn;
	__le16 irq_prod_via_msdm;
	__le16 irq_cons;
	__le16 hq_cons_th_or_mpa_data;
	__le16 hq_cons;
	__le32 atom_msn;
	__le32 orq_cons;
	__le32 orq_cons_th;
	u8 byte7;
	u8 wqe_data_pad_bytes;
	u8 max_ord;
	u8 former_hq_prod;
	u8 irq_prod_via_msem;
	u8 byte12;
	u8 max_pkt_pdu_size_lo;
	u8 max_pkt_pdu_size_hi;
	u8 byte15;
	u8 e5_reserved;
	__le16 e5_reserved4;
	__le32 reg10;
	__le32 reg11;
	__le32 shared_queue_page_addr_lo;
	__le32 shared_queue_page_addr_hi;
	__le32 reg14;
	__le32 reg15;
	__le32 reg16;
	__le32 reg17;
};

struct e4_tstorm_iwarp_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT1_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT		1
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT2_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT2_SHIFT		2
#define E4_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_OR_TERMINATE_SENT_MASK  0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_OR_TERMINATE_SENT_SHIFT 3
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT4_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT		4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_MASK	0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_SHIFT	5
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0_MASK		0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT		6
	u8 flags1;
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_MASK		0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_SHIFT		0
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_MASK		0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_SHIFT	2
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK		0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT	4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4_MASK			0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT			6
	u8 flags2;
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT	0
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT	2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT	4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8_MASK	0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT	6
	u8 flags3;
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_MASK 0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_SHIFT 0
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_MASK	0x3
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_SHIFT	2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT				4
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_MASK			0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_SHIFT			5
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_EN_SHIFT		6
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT		7
	u8 flags4;
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT				0
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT				1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT				2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT				3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK				0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT				4
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPL_EN_MASK 0x1
#define	E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPL_EN_SHIFT 5
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_MASK	0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_SHIFT	6
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT			7
	u8 flags5;
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT		0
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT		1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT		2
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT		3
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT		4
#define E4_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_MASK	0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_SHIFT	5
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT		6
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT		7
	__le32 reg0;
	__le32 reg1;
	__le32 unaligned_nxt_seq;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 orq_cache_idx;
	u8 hq_prod;
	__le16 sq_tx_cons_th;
	u8 orq_prod;
	u8 irq_cons;
	__le16 sq_tx_cons;
	__le16 conn_dpi;
	__le16 rq_prod;
	__le32 snd_seq;
	__le32 last_hq_sequence;
};

/* The iwarp storm context of Tstorm */
struct tstorm_iwarp_conn_st_ctx {
	__le32 reserved[60];
};

/* The iwarp storm context of Mstorm */
struct mstorm_iwarp_conn_st_ctx {
	__le32 reserved[32];
};

/* The iwarp storm context of Ustorm */
struct ustorm_iwarp_conn_st_ctx {
	struct regpair reserved[14];
};

/* iwarp connection context */
struct e4_iwarp_conn_context {
	struct ystorm_iwarp_conn_st_ctx ystorm_st_context;
	struct regpair ystorm_st_padding[2];
	struct pstorm_iwarp_conn_st_ctx pstorm_st_context;
	struct regpair pstorm_st_padding[2];
	struct xstorm_iwarp_conn_st_ctx xstorm_st_context;
	struct e4_xstorm_iwarp_conn_ag_ctx xstorm_ag_context;
	struct e4_tstorm_iwarp_conn_ag_ctx tstorm_ag_context;
	struct timers_context timer_context;
	struct e4_ustorm_rdma_conn_ag_ctx ustorm_ag_context;
	struct tstorm_iwarp_conn_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
	struct mstorm_iwarp_conn_st_ctx mstorm_st_context;
	struct ustorm_iwarp_conn_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
};

/* iWARP create QP params passed by driver to FW in CreateQP Request Ramrod */
struct iwarp_create_qp_ramrod_data {
	u8 flags;
#define IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN_MASK	0x1
#define IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN_SHIFT	0
#define IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP_SHIFT		1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN_SHIFT		2
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN_SHIFT		3
#define IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN_SHIFT		4
#define IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG_SHIFT		5
#define IWARP_CREATE_QP_RAMROD_DATA_LOW_LATENCY_QUEUE_EN_MASK	0x1
#define IWARP_CREATE_QP_RAMROD_DATA_LOW_LATENCY_QUEUE_EN_SHIFT	6
#define IWARP_CREATE_QP_RAMROD_DATA_RESERVED0_MASK		0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RESERVED0_SHIFT		7
	u8 reserved1;
	__le16 pd;
	__le16 sq_num_pages;
	__le16 rq_num_pages;
	__le32 reserved3[2];
	struct regpair qp_handle_for_cqe;
	struct rdma_srq_id srq_id;
	__le32 cq_cid_for_sq;
	__le32 cq_cid_for_rq;
	__le16 dpi;
	__le16 physical_q0;
	__le16 physical_q1;
	u8 reserved2[6];
};

/* iWARP completion queue types */
enum iwarp_eqe_async_opcode {
	IWARP_EVENT_TYPE_ASYNC_CONNECT_COMPLETE,
	IWARP_EVENT_TYPE_ASYNC_ENHANCED_MPA_REPLY_ARRIVED,
	IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_COMPLETE,
	IWARP_EVENT_TYPE_ASYNC_CID_CLEANED,
	IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED,
	IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE,
	IWARP_EVENT_TYPE_ASYNC_CQ_OVERFLOW,
	IWARP_EVENT_TYPE_ASYNC_SRQ_EMPTY,
	IWARP_EVENT_TYPE_ASYNC_SRQ_LIMIT,
	MAX_IWARP_EQE_ASYNC_OPCODE
};

struct iwarp_eqe_data_mpa_async_completion {
	__le16 ulp_data_len;
	u8 rtr_type_sent;
	u8 reserved[5];
};

struct iwarp_eqe_data_tcp_async_completion {
	__le16 ulp_data_len;
	u8 mpa_handshake_mode;
	u8 reserved[5];
};

/* iWARP completion queue types */
enum iwarp_eqe_sync_opcode {
	IWARP_EVENT_TYPE_TCP_OFFLOAD =
	11,
	IWARP_EVENT_TYPE_MPA_OFFLOAD,
	IWARP_EVENT_TYPE_MPA_OFFLOAD_SEND_RTR,
	IWARP_EVENT_TYPE_CREATE_QP,
	IWARP_EVENT_TYPE_QUERY_QP,
	IWARP_EVENT_TYPE_MODIFY_QP,
	IWARP_EVENT_TYPE_DESTROY_QP,
	IWARP_EVENT_TYPE_ABORT_TCP_OFFLOAD,
	MAX_IWARP_EQE_SYNC_OPCODE
};

/* iWARP EQE completion status */
enum iwarp_fw_return_code {
	IWARP_CONN_ERROR_TCP_CONNECT_INVALID_PACKET = 6,
	IWARP_CONN_ERROR_TCP_CONNECTION_RST,
	IWARP_CONN_ERROR_TCP_CONNECT_TIMEOUT,
	IWARP_CONN_ERROR_MPA_ERROR_REJECT,
	IWARP_CONN_ERROR_MPA_NOT_SUPPORTED_VER,
	IWARP_CONN_ERROR_MPA_RST,
	IWARP_CONN_ERROR_MPA_FIN,
	IWARP_CONN_ERROR_MPA_RTR_MISMATCH,
	IWARP_CONN_ERROR_MPA_INSUF_IRD,
	IWARP_CONN_ERROR_MPA_INVALID_PACKET,
	IWARP_CONN_ERROR_MPA_LOCAL_ERROR,
	IWARP_CONN_ERROR_MPA_TIMEOUT,
	IWARP_CONN_ERROR_MPA_TERMINATE,
	IWARP_QP_IN_ERROR_GOOD_CLOSE,
	IWARP_QP_IN_ERROR_BAD_CLOSE,
	IWARP_EXCEPTION_DETECTED_LLP_CLOSED,
	IWARP_EXCEPTION_DETECTED_LLP_RESET,
	IWARP_EXCEPTION_DETECTED_IRQ_FULL,
	IWARP_EXCEPTION_DETECTED_RQ_EMPTY,
	IWARP_EXCEPTION_DETECTED_SRQ_EMPTY,
	IWARP_EXCEPTION_DETECTED_SRQ_LIMIT,
	IWARP_EXCEPTION_DETECTED_LLP_TIMEOUT,
	IWARP_EXCEPTION_DETECTED_REMOTE_PROTECTION_ERROR,
	IWARP_EXCEPTION_DETECTED_CQ_OVERFLOW,
	IWARP_EXCEPTION_DETECTED_LOCAL_CATASTROPHIC,
	IWARP_EXCEPTION_DETECTED_LOCAL_ACCESS_ERROR,
	IWARP_EXCEPTION_DETECTED_REMOTE_OPERATION_ERROR,
	IWARP_EXCEPTION_DETECTED_TERMINATE_RECEIVED,
	MAX_IWARP_FW_RETURN_CODE
};

/* unaligned opaque data received from LL2 */
struct iwarp_init_func_params {
	u8 ll2_ooo_q_index;
	u8 reserved1[7];
};

/* iwarp func init ramrod data */
struct iwarp_init_func_ramrod_data {
	struct rdma_init_func_ramrod_data rdma;
	struct tcp_init_params tcp;
	struct iwarp_init_func_params iwarp;
};

/* iWARP QP - possible states to transition to */
enum iwarp_modify_qp_new_state_type {
	IWARP_MODIFY_QP_STATE_CLOSING = 1,
	IWARP_MODIFY_QP_STATE_ERROR = 2,
	MAX_IWARP_MODIFY_QP_NEW_STATE_TYPE
};

/* iwarp modify qp responder ramrod data */
struct iwarp_modify_qp_ramrod_data {
	__le16 transition_to_state;
	__le16 flags;
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_RD_EN_MASK		0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_RD_EN_SHIFT		0
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_WR_EN_MASK		0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_WR_EN_SHIFT		1
#define IWARP_MODIFY_QP_RAMROD_DATA_ATOMIC_EN_MASK		0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_ATOMIC_EN_SHIFT		2
#define IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN_MASK		0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN_SHIFT	3
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_OPS_EN_FLG_MASK	0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_OPS_EN_FLG_SHIFT	4
#define IWARP_MODIFY_QP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_MASK	0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_SHIFT	5
#define IWARP_MODIFY_QP_RAMROD_DATA_RESERVED_MASK		0x3FF
#define IWARP_MODIFY_QP_RAMROD_DATA_RESERVED_SHIFT		6
	__le16 physical_q0;
	__le16 physical_q1;
	__le32 reserved1[10];
};

/* MPA params for Enhanced mode */
struct mpa_rq_params {
	__le32 ird;
	__le32 ord;
};

/* MPA host Address-Len for private data */
struct mpa_ulp_buffer {
	struct regpair addr;
	__le16 len;
	__le16 reserved[3];
};

/* iWARP MPA offload params common to Basic and Enhanced modes */
struct mpa_outgoing_params {
	u8 crc_needed;
	u8 reject;
	u8 reserved[6];
	struct mpa_rq_params out_rq;
	struct mpa_ulp_buffer outgoing_ulp_buffer;
};

/* iWARP MPA offload params passed by driver to FW in MPA Offload Request
 * Ramrod.
 */
struct iwarp_mpa_offload_ramrod_data {
	struct mpa_outgoing_params common;
	__le32 tcp_cid;
	u8 mode;
	u8 tcp_connect_side;
	u8 rtr_pref;
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RTR_SUPPORTED_MASK	0x7
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RTR_SUPPORTED_SHIFT	0
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RESERVED1_MASK		0x1F
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RESERVED1_SHIFT		3
	u8 reserved2;
	struct mpa_ulp_buffer incoming_ulp_buffer;
	struct regpair async_eqe_output_buf;
	struct regpair handle_for_async;
	struct regpair shared_queue_addr;
	__le16 rcv_wnd;
	u8 stats_counter_id;
	u8 reserved3[13];
};

/* iWARP TCP connection offload params passed by driver to FW */
struct iwarp_offload_params {
	struct mpa_ulp_buffer incoming_ulp_buffer;
	struct regpair async_eqe_output_buf;
	struct regpair handle_for_async;
	__le16 physical_q0;
	__le16 physical_q1;
	u8 stats_counter_id;
	u8 mpa_mode;
	u8 reserved[10];
};

/* iWARP query QP output params */
struct iwarp_query_qp_output_params {
	__le32 flags;
#define IWARP_QUERY_QP_OUTPUT_PARAMS_ERROR_FLG_MASK	0x1
#define IWARP_QUERY_QP_OUTPUT_PARAMS_ERROR_FLG_SHIFT	0
#define IWARP_QUERY_QP_OUTPUT_PARAMS_RESERVED0_MASK	0x7FFFFFFF
#define IWARP_QUERY_QP_OUTPUT_PARAMS_RESERVED0_SHIFT	1
	u8 reserved1[4];
};

/* iWARP query QP ramrod data */
struct iwarp_query_qp_ramrod_data {
	struct regpair output_params_addr;
};

/* iWARP Ramrod Command IDs */
enum iwarp_ramrod_cmd_id {
	IWARP_RAMROD_CMD_ID_TCP_OFFLOAD = 11,
	IWARP_RAMROD_CMD_ID_MPA_OFFLOAD,
	IWARP_RAMROD_CMD_ID_MPA_OFFLOAD_SEND_RTR,
	IWARP_RAMROD_CMD_ID_CREATE_QP,
	IWARP_RAMROD_CMD_ID_QUERY_QP,
	IWARP_RAMROD_CMD_ID_MODIFY_QP,
	IWARP_RAMROD_CMD_ID_DESTROY_QP,
	IWARP_RAMROD_CMD_ID_ABORT_TCP_OFFLOAD,
	MAX_IWARP_RAMROD_CMD_ID
};

/* Per PF iWARP retransmit path statistics */
struct iwarp_rxmit_stats_drv {
	struct regpair tx_go_to_slow_start_event_cnt;
	struct regpair tx_fast_retransmit_event_cnt;
};

/* iWARP and TCP connection offload params passed by driver to FW in iWARP
 * offload ramrod.
 */
struct iwarp_tcp_offload_ramrod_data {
	struct tcp_offload_params_opt2 tcp;
	struct iwarp_offload_params iwarp;
};

/* iWARP MPA negotiation types */
enum mpa_negotiation_mode {
	MPA_NEGOTIATION_TYPE_BASIC = 1,
	MPA_NEGOTIATION_TYPE_ENHANCED = 2,
	MAX_MPA_NEGOTIATION_MODE
};

/* iWARP MPA Enhanced mode RTR types */
enum mpa_rtr_type {
	MPA_RTR_TYPE_NONE = 0,
	MPA_RTR_TYPE_ZERO_SEND = 1,
	MPA_RTR_TYPE_ZERO_WRITE = 2,
	MPA_RTR_TYPE_ZERO_SEND_AND_WRITE = 3,
	MPA_RTR_TYPE_ZERO_READ = 4,
	MPA_RTR_TYPE_ZERO_SEND_AND_READ = 5,
	MPA_RTR_TYPE_ZERO_WRITE_AND_READ = 6,
	MPA_RTR_TYPE_ZERO_SEND_AND_WRITE_AND_READ = 7,
	MAX_MPA_RTR_TYPE
};

/* unaligned opaque data received from LL2 */
struct unaligned_opaque_data {
	__le16 first_mpa_offset;
	u8 tcp_payload_offset;
	u8 flags;
#define UNALIGNED_OPAQUE_DATA_PKT_REACHED_WIN_RIGHT_EDGE_MASK	0x1
#define UNALIGNED_OPAQUE_DATA_PKT_REACHED_WIN_RIGHT_EDGE_SHIFT	0
#define UNALIGNED_OPAQUE_DATA_CONNECTION_CLOSED_MASK		0x1
#define UNALIGNED_OPAQUE_DATA_CONNECTION_CLOSED_SHIFT		1
#define UNALIGNED_OPAQUE_DATA_RESERVED_MASK			0x3F
#define UNALIGNED_OPAQUE_DATA_RESERVED_SHIFT			2
	__le32 cid;
};

struct e4_mstorm_iwarp_conn_ag_ctx {
	u8 reserved;
	u8 state;
	u8 flags0;
#define E4_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT		0
#define E4_MSTORM_IWARP_CONN_AG_CTX_BIT1_MASK			0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT			1
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_MASK	0x3
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_SHIFT	2
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1_MASK			0x3
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT			4
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2_MASK			0x3
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT			6
	u8 flags1;
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_MASK	0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_SHIFT	0
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK			0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT			1
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT			2
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT		3
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT		4
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT		5
#define E4_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_SHIFT		6
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT		7
	__le16 rcq_cons;
	__le16 rcq_cons_th;
	__le32 reg0;
	__le32 reg1;
};

struct e4_ustorm_iwarp_conn_ag_ctx {
	u8 reserved;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_USTORM_IWARP_CONN_AG_CTX_BIT1_MASK		0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT		1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0_MASK		0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0_SHIFT		2
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1_MASK		0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1_SHIFT		4
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2_MASK		0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2_SHIFT		6
	u8 flags1;
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3_MASK		0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3_SHIFT		0
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_MASK	0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT	2
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_MASK	0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_SHIFT	4
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6_MASK		0x3
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6_SHIFT		6
	u8 flags2;
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0EN_MASK			0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT			0
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1EN_MASK			0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT			1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT			2
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3EN_MASK			0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3EN_SHIFT			3
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT	4
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_MASK		0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT		5
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6EN_MASK			0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT			6
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_MASK		0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_SHIFT		7
	u8 flags3;
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_EN_MASK		0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_EN_SHIFT		0
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 cq_cons;
	__le32 cq_se_prod;
	__le32 cq_prod;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

struct e4_ystorm_iwarp_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0_MASK	0x3
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1_MASK	0x3
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2_MASK	0x3
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

/* The fcoe storm context of Ystorm */
struct ystorm_fcoe_conn_st_ctx {
	u8 func_mode;
	u8 cos;
	u8 conf_version;
	u8 eth_hdr_size;
	__le16 stat_ram_addr;
	__le16 mtu;
	__le16 max_fc_payload_len;
	__le16 tx_max_fc_pay_len;
	u8 fcp_cmd_size;
	u8 fcp_rsp_size;
	__le16 mss;
	struct regpair reserved;
	__le16 min_frame_size;
	u8 protection_info_flags;
#define YSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_MASK		0x1
#define YSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_SHIFT	0
#define YSTORM_FCOE_CONN_ST_CTX_VALID_MASK			0x1
#define YSTORM_FCOE_CONN_ST_CTX_VALID_SHIFT			1
#define YSTORM_FCOE_CONN_ST_CTX_RESERVED1_MASK			0x3F
#define YSTORM_FCOE_CONN_ST_CTX_RESERVED1_SHIFT			2
	u8 dst_protection_per_mss;
	u8 src_protection_per_mss;
	u8 ptu_log_page_size;
	u8 flags;
#define YSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK	0x1
#define YSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT	0
#define YSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_MASK	0x1
#define YSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_SHIFT	1
#define YSTORM_FCOE_CONN_ST_CTX_RSRV_MASK		0x3F
#define YSTORM_FCOE_CONN_ST_CTX_RSRV_SHIFT		2
	u8 fcp_xfer_size;
};

/* FCoE 16-bits vlan structure */
struct fcoe_vlan_fields {
	__le16 fields;
#define FCOE_VLAN_FIELDS_VID_MASK	0xFFF
#define FCOE_VLAN_FIELDS_VID_SHIFT	0
#define FCOE_VLAN_FIELDS_CLI_MASK	0x1
#define FCOE_VLAN_FIELDS_CLI_SHIFT	12
#define FCOE_VLAN_FIELDS_PRI_MASK	0x7
#define FCOE_VLAN_FIELDS_PRI_SHIFT	13
};

/* FCoE 16-bits vlan union */
union fcoe_vlan_field_union {
	struct fcoe_vlan_fields fields;
	__le16 val;
};

/* FCoE 16-bits vlan, vif union */
union fcoe_vlan_vif_field_union {
	union fcoe_vlan_field_union vlan;
	__le16 vif;
};

/* Ethernet context section */
struct pstorm_fcoe_eth_context_section {
	u8 remote_addr_3;
	u8 remote_addr_2;
	u8 remote_addr_1;
	u8 remote_addr_0;
	u8 local_addr_1;
	u8 local_addr_0;
	u8 remote_addr_5;
	u8 remote_addr_4;
	u8 local_addr_5;
	u8 local_addr_4;
	u8 local_addr_3;
	u8 local_addr_2;
	union fcoe_vlan_vif_field_union vif_outer_vlan;
	__le16 vif_outer_eth_type;
	union fcoe_vlan_vif_field_union inner_vlan;
	__le16 inner_eth_type;
};

/* The fcoe storm context of Pstorm */
struct pstorm_fcoe_conn_st_ctx {
	u8 func_mode;
	u8 cos;
	u8 conf_version;
	u8 rsrv;
	__le16 stat_ram_addr;
	__le16 mss;
	struct regpair abts_cleanup_addr;
	struct pstorm_fcoe_eth_context_section eth;
	u8 sid_2;
	u8 sid_1;
	u8 sid_0;
	u8 flags;
#define PSTORM_FCOE_CONN_ST_CTX_VNTAG_VLAN_MASK			0x1
#define PSTORM_FCOE_CONN_ST_CTX_VNTAG_VLAN_SHIFT		0
#define PSTORM_FCOE_CONN_ST_CTX_SUPPORT_REC_RR_TOV_MASK		0x1
#define PSTORM_FCOE_CONN_ST_CTX_SUPPORT_REC_RR_TOV_SHIFT	1
#define PSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK		0x1
#define PSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT		2
#define PSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_MASK		0x1
#define PSTORM_FCOE_CONN_ST_CTX_OUTER_VLAN_FLAG_SHIFT		3
#define PSTORM_FCOE_CONN_ST_CTX_SINGLE_VLAN_FLAG_MASK		0x1
#define PSTORM_FCOE_CONN_ST_CTX_SINGLE_VLAN_FLAG_SHIFT		4
#define PSTORM_FCOE_CONN_ST_CTX_RESERVED_MASK			0x7
#define PSTORM_FCOE_CONN_ST_CTX_RESERVED_SHIFT			5
	u8 did_2;
	u8 did_1;
	u8 did_0;
	u8 src_mac_index;
	__le16 rec_rr_tov_val;
	u8 q_relative_offset;
	u8 reserved1;
};

/* The fcoe storm context of Xstorm */
struct xstorm_fcoe_conn_st_ctx {
	u8 func_mode;
	u8 src_mac_index;
	u8 conf_version;
	u8 cached_wqes_avail;
	__le16 stat_ram_addr;
	u8 flags;
#define XSTORM_FCOE_CONN_ST_CTX_SQ_DEFERRED_MASK		0x1
#define XSTORM_FCOE_CONN_ST_CTX_SQ_DEFERRED_SHIFT		0
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_MASK		0x1
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_SHIFT		1
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_ORIG_MASK	0x1
#define XSTORM_FCOE_CONN_ST_CTX_INNER_VLAN_FLAG_ORIG_SHIFT	2
#define XSTORM_FCOE_CONN_ST_CTX_LAST_QUEUE_HANDLED_MASK		0x3
#define XSTORM_FCOE_CONN_ST_CTX_LAST_QUEUE_HANDLED_SHIFT	3
#define XSTORM_FCOE_CONN_ST_CTX_RSRV_MASK			0x7
#define XSTORM_FCOE_CONN_ST_CTX_RSRV_SHIFT			5
	u8 cached_wqes_offset;
	u8 reserved2;
	u8 eth_hdr_size;
	u8 seq_id;
	u8 max_conc_seqs;
	__le16 num_pages_in_pbl;
	__le16 reserved;
	struct regpair sq_pbl_addr;
	struct regpair sq_curr_page_addr;
	struct regpair sq_next_page_addr;
	struct regpair xferq_pbl_addr;
	struct regpair xferq_curr_page_addr;
	struct regpair xferq_next_page_addr;
	struct regpair respq_pbl_addr;
	struct regpair respq_curr_page_addr;
	struct regpair respq_next_page_addr;
	__le16 mtu;
	__le16 tx_max_fc_pay_len;
	__le16 max_fc_payload_len;
	__le16 min_frame_size;
	__le16 sq_pbl_next_index;
	__le16 respq_pbl_next_index;
	u8 fcp_cmd_byte_credit;
	u8 fcp_rsp_byte_credit;
	__le16 protection_info;
#define XSTORM_FCOE_CONN_ST_CTX_PROTECTION_PERF_MASK		0x1
#define XSTORM_FCOE_CONN_ST_CTX_PROTECTION_PERF_SHIFT		0
#define XSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_MASK		0x1
#define XSTORM_FCOE_CONN_ST_CTX_SUPPORT_PROTECTION_SHIFT	1
#define XSTORM_FCOE_CONN_ST_CTX_VALID_MASK			0x1
#define XSTORM_FCOE_CONN_ST_CTX_VALID_SHIFT			2
#define XSTORM_FCOE_CONN_ST_CTX_FRAME_PROT_ALIGNED_MASK		0x1
#define XSTORM_FCOE_CONN_ST_CTX_FRAME_PROT_ALIGNED_SHIFT	3
#define XSTORM_FCOE_CONN_ST_CTX_RESERVED3_MASK			0xF
#define XSTORM_FCOE_CONN_ST_CTX_RESERVED3_SHIFT			4
#define XSTORM_FCOE_CONN_ST_CTX_DST_PROTECTION_PER_MSS_MASK	0xFF
#define XSTORM_FCOE_CONN_ST_CTX_DST_PROTECTION_PER_MSS_SHIFT	8
	__le16 xferq_pbl_next_index;
	__le16 page_size;
	u8 mid_seq;
	u8 fcp_xfer_byte_credit;
	u8 reserved1[2];
	struct fcoe_wqe cached_wqes[16];
};

struct e4_xstorm_fcoe_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED1_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED2_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED3_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED4_SHIFT	5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED5_SHIFT	6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED6_SHIFT	7
	u8 flags1;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED7_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED8_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED9_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT12_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT12_SHIFT		4
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT13_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT13_SHIFT		5
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT14_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT14_SHIFT		6
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT15_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT15_SHIFT		7
	u8 flags2;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3_SHIFT	6
	u8 flags3;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT	6
	u8 flags4;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15_SHIFT	6
	u8 flags6;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_SHIFT	6
	u8 flags7;
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED10_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT		6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT		7
	u8 flags8;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT	5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT	6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT	7
	u8 flags9;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF11EN_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF12EN_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF13EN_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF14EN_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF15EN_SHIFT	5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF16EN_SHIFT	6
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF17EN_SHIFT	7
	u8 flags10;
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18EN_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF18EN_SHIFT		0
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_DQ_CF_EN_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED11_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_SLOW_PATH_EN_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23EN_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23EN_SHIFT		5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED12_SHIFT	6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED13_SHIFT	7
	u8 flags11;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED14_SHIFT		0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED15_SHIFT		1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESERVED16_SHIFT		2
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK			0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT		3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK			0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT		4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK			0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT		5
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED1_SHIFT		6
#define E4_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_XFERQ_DECISION_EN_SHIFT	7
	u8 flags12;
#define E4_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_SQ_DECISION_EN_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE11EN_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED2_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED3_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE14EN_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE15EN_SHIFT	5
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE16EN_SHIFT	6
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE17EN_SHIFT	7
	u8 flags13;
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RESPQ_DECISION_EN_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_RULE19EN_SHIFT		1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED4_SHIFT		2
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED5_SHIFT		3
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED6_SHIFT		4
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED7_SHIFT		5
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED8_SHIFT		6
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_MASK		0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_A0_RESERVED9_SHIFT		7
	u8 flags14;
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT16_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT16_SHIFT	0
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT17_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT17_SHIFT	1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT18_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT18_SHIFT	2
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT19_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT19_SHIFT	3
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT20_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT20_SHIFT	4
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT21_MASK	0x1
#define E4_XSTORM_FCOE_CONN_AG_CTX_BIT21_SHIFT	5
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23_MASK	0x3
#define E4_XSTORM_FCOE_CONN_AG_CTX_CF23_SHIFT	6
	u8 byte2;
	__le16 physical_q0;
	__le16 word1;
	__le16 word2;
	__le16 sq_cons;
	__le16 sq_prod;
	__le16 xferq_prod;
	__le16 xferq_cons;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 remain_io;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le16 respq_prod;
	__le16 respq_cons;
	__le16 word9;
	__le16 word10;
	__le32 reg7;
	__le32 reg8;
};

/* The fcoe storm context of Ustorm */
struct ustorm_fcoe_conn_st_ctx {
	struct regpair respq_pbl_addr;
	__le16 num_pages_in_pbl;
	u8 ptu_log_page_size;
	u8 log_page_size;
	__le16 respq_prod;
	u8 reserved[2];
};

struct e4_tstorm_fcoe_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT1_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT		1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT2_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT2_SHIFT		2
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT3_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT3_SHIFT		3
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT4_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT4_SHIFT		4
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT5_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_BIT5_SHIFT		5
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_SHIFT	6
	u8 flags1;
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_MASK		0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT		0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2_MASK			0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT			2
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT	4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4_MASK			0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4_SHIFT			6
	u8 flags2;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5_SHIFT	0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6_SHIFT	2
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7_SHIFT	4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8_MASK	0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8_SHIFT	6
	u8 flags3;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9_MASK			0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9_SHIFT			0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10_MASK			0x3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10_SHIFT			2
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_MASK	0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN_SHIFT	4
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT		5
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT			6
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK	0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT		0
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT		1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT		2
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF7EN_SHIFT		3
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF8EN_SHIFT		4
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF9EN_SHIFT		5
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_CF10EN_SHIFT		6
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags5;
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_TSTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT	7
	__le32 reg0;
	__le32 reg1;
};

struct e4_ustorm_fcoe_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3_SHIFT	0
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4_SHIFT	2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5_SHIFT	4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6_MASK	0x3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6_SHIFT	6
	u8 flags2;
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF3EN_SHIFT		3
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF4EN_SHIFT		4
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF5EN_SHIFT		5
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_CF6EN_SHIFT		6
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags3;
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE5EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE6EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE7EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE8EN_MASK		0x1
#define E4_USTORM_FCOE_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

/* The fcoe storm context of Tstorm */
struct tstorm_fcoe_conn_st_ctx {
	__le16 stat_ram_addr;
	__le16 rx_max_fc_payload_len;
	__le16 e_d_tov_val;
	u8 flags;
#define TSTORM_FCOE_CONN_ST_CTX_INC_SEQ_CNT_MASK	0x1
#define TSTORM_FCOE_CONN_ST_CTX_INC_SEQ_CNT_SHIFT	0
#define TSTORM_FCOE_CONN_ST_CTX_SUPPORT_CONF_MASK	0x1
#define TSTORM_FCOE_CONN_ST_CTX_SUPPORT_CONF_SHIFT	1
#define TSTORM_FCOE_CONN_ST_CTX_DEF_Q_IDX_MASK		0x3F
#define TSTORM_FCOE_CONN_ST_CTX_DEF_Q_IDX_SHIFT		2
	u8 timers_cleanup_invocation_cnt;
	__le32 reserved1[2];
	__le32 dst_mac_address_bytes_0_to_3;
	__le16 dst_mac_address_bytes_4_to_5;
	__le16 ramrod_echo;
	u8 flags1;
#define TSTORM_FCOE_CONN_ST_CTX_MODE_MASK	0x3
#define TSTORM_FCOE_CONN_ST_CTX_MODE_SHIFT	0
#define TSTORM_FCOE_CONN_ST_CTX_RESERVED_MASK	0x3F
#define TSTORM_FCOE_CONN_ST_CTX_RESERVED_SHIFT	2
	u8 cq_relative_offset;
	u8 cmdq_relative_offset;
	u8 bdq_resource_id;
	u8 reserved0[4];
};

struct e4_mstorm_fcoe_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_MSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

/* Fast path part of the fcoe storm context of Mstorm */
struct fcoe_mstorm_fcoe_conn_st_ctx_fp {
	__le16 xfer_prod;
	u8 num_cqs;
	u8 reserved1;
	u8 protection_info;
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_SUPPORT_PROTECTION_MASK  0x1
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_SUPPORT_PROTECTION_SHIFT 0
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_VALID_MASK               0x1
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_VALID_SHIFT              1
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_RESERVED0_MASK           0x3F
#define FCOE_MSTORM_FCOE_CONN_ST_CTX_FP_RESERVED0_SHIFT          2
	u8 q_relative_offset;
	u8 reserved2[2];
};

/* Non fast path part of the fcoe storm context of Mstorm */
struct fcoe_mstorm_fcoe_conn_st_ctx_non_fp {
	__le16 conn_id;
	__le16 stat_ram_addr;
	__le16 num_pages_in_pbl;
	u8 ptu_log_page_size;
	u8 log_page_size;
	__le16 unsolicited_cq_count;
	__le16 cmdq_count;
	u8 bdq_resource_id;
	u8 reserved0[3];
	struct regpair xferq_pbl_addr;
	struct regpair reserved1;
	struct regpair reserved2[3];
};

/* The fcoe storm context of Mstorm */
struct mstorm_fcoe_conn_st_ctx {
	struct fcoe_mstorm_fcoe_conn_st_ctx_fp fp;
	struct fcoe_mstorm_fcoe_conn_st_ctx_non_fp non_fp;
};

/* fcoe connection context */
struct e4_fcoe_conn_context {
	struct ystorm_fcoe_conn_st_ctx ystorm_st_context;
	struct pstorm_fcoe_conn_st_ctx pstorm_st_context;
	struct regpair pstorm_st_padding[2];
	struct xstorm_fcoe_conn_st_ctx xstorm_st_context;
	struct e4_xstorm_fcoe_conn_ag_ctx xstorm_ag_context;
	struct regpair xstorm_ag_padding[6];
	struct ustorm_fcoe_conn_st_ctx ustorm_st_context;
	struct regpair ustorm_st_padding[2];
	struct e4_tstorm_fcoe_conn_ag_ctx tstorm_ag_context;
	struct regpair tstorm_ag_padding[2];
	struct timers_context timer_context;
	struct e4_ustorm_fcoe_conn_ag_ctx ustorm_ag_context;
	struct tstorm_fcoe_conn_st_ctx tstorm_st_context;
	struct e4_mstorm_fcoe_conn_ag_ctx mstorm_ag_context;
	struct mstorm_fcoe_conn_st_ctx mstorm_st_context;
};

/* FCoE connection offload params passed by driver to FW in FCoE offload
 * ramrod.
 */
struct fcoe_conn_offload_ramrod_params {
	struct fcoe_conn_offload_ramrod_data offload_ramrod_data;
};

/* FCoE connection terminate params passed by driver to FW in FCoE terminate
 * conn ramrod.
 */
struct fcoe_conn_terminate_ramrod_params {
	struct fcoe_conn_terminate_ramrod_data terminate_ramrod_data;
};

/* FCoE event type */
enum fcoe_event_type {
	FCOE_EVENT_INIT_FUNC,
	FCOE_EVENT_DESTROY_FUNC,
	FCOE_EVENT_STAT_FUNC,
	FCOE_EVENT_OFFLOAD_CONN,
	FCOE_EVENT_TERMINATE_CONN,
	FCOE_EVENT_ERROR,
	MAX_FCOE_EVENT_TYPE
};

/* FCoE init params passed by driver to FW in FCoE init ramrod */
struct fcoe_init_ramrod_params {
	struct fcoe_init_func_ramrod_data init_ramrod_data;
};

/* FCoE ramrod Command IDs */
enum fcoe_ramrod_cmd_id {
	FCOE_RAMROD_CMD_ID_INIT_FUNC,
	FCOE_RAMROD_CMD_ID_DESTROY_FUNC,
	FCOE_RAMROD_CMD_ID_STAT_FUNC,
	FCOE_RAMROD_CMD_ID_OFFLOAD_CONN,
	FCOE_RAMROD_CMD_ID_TERMINATE_CONN,
	MAX_FCOE_RAMROD_CMD_ID
};

/* FCoE statistics params buffer passed by driver to FW in FCoE statistics
 * ramrod.
 */
struct fcoe_stat_ramrod_params {
	struct fcoe_stat_ramrod_data stat_ramrod_data;
};

struct e4_ystorm_fcoe_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0_MASK	0x3
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1_MASK	0x3
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2_MASK	0x3
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_MASK		0x1
#define E4_YSTORM_FCOE_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

/* The iscsi storm connection context of Ystorm */
struct ystorm_iscsi_conn_st_ctx {
	__le32 reserved[8];
};

/* Combined iSCSI and TCP storm connection of Pstorm */
struct pstorm_iscsi_tcp_conn_st_ctx {
	__le32 tcp[32];
	__le32 iscsi[4];
};

/* The combined tcp and iscsi storm context of Xstorm */
struct xstorm_iscsi_tcp_conn_st_ctx {
	__le32 reserved_tcp[4];
	__le32 reserved_iscsi[44];
};

struct e4_xstorm_iscsi_conn_ag_ctx {
	u8 cdu_validation;
	u8 state;
	u8 flags0;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_SHIFT	1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_SHIFT	3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_SHIFT	5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT6_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT6_SHIFT		6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT7_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT7_SHIFT		7
	u8 flags1;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT8_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT8_SHIFT		0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT9_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT9_SHIFT		1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT10_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT10_SHIFT		2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT11_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT11_SHIFT		3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT12_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT12_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT13_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT13_SHIFT		5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT14_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT14_SHIFT		6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_SHIFT	7
	u8 flags2;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0_MASK			0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT			0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1_MASK			0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT			2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2_MASK			0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT			4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK		0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT	6
	u8 flags3;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT	4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT	6
	u8 flags4;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10_SHIFT	4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11_SHIFT	6
	u8 flags5;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12_MASK				0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12_SHIFT				0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13_MASK				0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13_SHIFT				2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14_MASK				0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14_SHIFT				4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_SHIFT	6
	u8 flags6;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16_MASK		0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16_SHIFT		0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17_MASK		0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17_SHIFT		2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18_MASK		0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_SHIFT	6
	u8 flags7;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_MASK		0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT			6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT			7
	u8 flags8;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT			0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT	1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT			2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT			3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT			4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT			5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT			6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_SHIFT			7
	u8 flags9;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_SHIFT			0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_SHIFT			1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_SHIFT			2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_SHIFT			3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_SHIFT			4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_SHIFT	5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_SHIFT			6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_SHIFT			7
	u8 flags10;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_MASK				0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_SHIFT			0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT			1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_SHIFT	3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_SHIFT			4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_SHIFT		5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT			6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_SHIFT	7
	u8 flags11;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT	1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_SHIFT	2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT	3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT	4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT	5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_SHIFT	6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_SHIFT	7
	u8 flags12;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_SHIFT		1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_SHIFT		2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_SHIFT		3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_SHIFT		5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_SHIFT		6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_SHIFT		7
	u8 flags13;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_SHIFT	0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_SHIFT	1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_SHIFT		2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_SHIFT		3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_SHIFT		4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_SHIFT		5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_SHIFT		6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_MASK		0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_SHIFT		7
	u8 flags14;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT16_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT16_SHIFT			0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT17_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT17_SHIFT			1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT18_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT18_SHIFT			2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT19_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT19_SHIFT			3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT20_MASK			0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT20_SHIFT			4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_MASK	0x1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_SHIFT	5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_MASK	0x3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_SHIFT	6
	u8 byte2;
	__le16 physical_q0;
	__le16 physical_q1;
	__le16 dummy_dorq_var;
	__le16 sq_cons;
	__le16 sq_prod;
	__le16 word5;
	__le16 slow_io_total_data_tx_update;
	u8 byte3;
	u8 byte4;
	u8 byte5;
	u8 byte6;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 more_to_send_seq;
	__le32 reg4;
	__le32 reg5;
	__le32 hq_scan_next_relevant_ack;
	__le16 r2tq_prod;
	__le16 r2tq_cons;
	__le16 hq_prod;
	__le16 hq_cons;
	__le32 remain_seq;
	__le32 bytes_to_next_pdu;
	__le32 hq_tcp_seq;
	u8 byte7;
	u8 byte8;
	u8 byte9;
	u8 byte10;
	u8 byte11;
	u8 byte12;
	u8 byte13;
	u8 byte14;
	u8 byte15;
	u8 e5_reserved;
	__le16 word11;
	__le32 reg10;
	__le32 reg11;
	__le32 exp_stat_sn;
	__le32 ongoing_fast_rxmit_seq;
	__le32 reg14;
	__le32 reg15;
	__le32 reg16;
	__le32 reg17;
};

struct e4_tstorm_iscsi_conn_ag_ctx {
	u8 reserved0;
	u8 state;
	u8 flags0;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT	0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT		1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT2_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT2_SHIFT		2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT3_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT3_SHIFT		3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT		4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT5_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT5_SHIFT		5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0_MASK		0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT		6
	u8 flags1;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_MASK		0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_SHIFT		0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_MASK		0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_SHIFT		2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK		0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT	4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4_MASK			0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT			6
	u8 flags2;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5_MASK	0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT	0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6_MASK	0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT	2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7_MASK	0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT	4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8_MASK	0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT	6
	u8 flags3;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_MASK		0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_SHIFT		0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_OOO_ISLES_CF_MASK	0x3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_OOO_ISLES_CF_SHIFT	2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK			0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT			4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_SHIFT	5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_SHIFT	6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT	7
	u8 flags4;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT		0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT		1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT		2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT		3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK		0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT		4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT	5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_OOO_ISLES_CF_EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_OOO_ISLES_CF_EN_SHIFT	6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags5;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT	7
	__le32 reg0;
	__le32 reg1;
	__le32 rx_tcp_checksum_err_cnt;
	__le32 reg3;
	__le32 reg4;
	__le32 reg5;
	__le32 reg6;
	__le32 reg7;
	__le32 reg8;
	u8 cid_offload_cnt;
	u8 byte3;
	__le16 word0;
};

struct e4_ustorm_iscsi_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT	2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT	4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3_SHIFT	0
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT	2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT	4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6_MASK	0x3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT	6
	u8 flags2;
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3EN_SHIFT		3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT		4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT		5
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK		0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT		6
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT	7
	u8 flags3;
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT	0
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT	1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT	2
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT	3
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT	4
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT	5
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT	6
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK	0x1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
	__le32 reg2;
	__le32 reg3;
	__le16 word2;
	__le16 word3;
};

/* The iscsi storm connection context of Tstorm */
struct tstorm_iscsi_conn_st_ctx {
	__le32 reserved[44];
};

struct e4_mstorm_iscsi_conn_ag_ctx {
	u8 reserved;
	u8 state;
	u8 flags0;
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0_MASK	0x3
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT	2
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1_MASK	0x3
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT	4
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2_MASK	0x3
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT	7
	__le16 word0;
	__le16 word1;
	__le32 reg0;
	__le32 reg1;
};

/* Combined iSCSI and TCP storm connection of Mstorm */
struct mstorm_iscsi_tcp_conn_st_ctx {
	__le32 reserved_tcp[20];
	__le32 reserved_iscsi[12];
};

/* The iscsi storm context of Ustorm */
struct ustorm_iscsi_conn_st_ctx {
	__le32 reserved[52];
};

/* iscsi connection context */
struct e4_iscsi_conn_context {
	struct ystorm_iscsi_conn_st_ctx ystorm_st_context;
	struct pstorm_iscsi_tcp_conn_st_ctx pstorm_st_context;
	struct regpair pstorm_st_padding[2];
	struct pb_context xpb2_context;
	struct xstorm_iscsi_tcp_conn_st_ctx xstorm_st_context;
	struct regpair xstorm_st_padding[2];
	struct e4_xstorm_iscsi_conn_ag_ctx xstorm_ag_context;
	struct e4_tstorm_iscsi_conn_ag_ctx tstorm_ag_context;
	struct regpair tstorm_ag_padding[2];
	struct timers_context timer_context;
	struct e4_ustorm_iscsi_conn_ag_ctx ustorm_ag_context;
	struct pb_context upb_context;
	struct tstorm_iscsi_conn_st_ctx tstorm_st_context;
	struct regpair tstorm_st_padding[2];
	struct e4_mstorm_iscsi_conn_ag_ctx mstorm_ag_context;
	struct mstorm_iscsi_tcp_conn_st_ctx mstorm_st_context;
	struct ustorm_iscsi_conn_st_ctx ustorm_st_context;
};

/* iSCSI init params passed by driver to FW in iSCSI init ramrod */
struct iscsi_init_ramrod_params {
	struct iscsi_spe_func_init iscsi_init_spe;
	struct tcp_init_params tcp_init;
};

struct e4_ystorm_iscsi_conn_ag_ctx {
	u8 byte0;
	u8 byte1;
	u8 flags0;
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT	0
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT	1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0_MASK	0x3
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT	2
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1_MASK	0x3
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT	4
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2_MASK	0x3
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT	6
	u8 flags1;
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK		0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT		0
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK		0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT		1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK		0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT		2
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT	3
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT	4
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT	5
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT	6
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK	0x1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT	7
	u8 byte2;
	u8 byte3;
	__le16 word0;
	__le32 reg0;
	__le32 reg1;
	__le16 word1;
	__le16 word2;
	__le16 word3;
	__le16 word4;
	__le32 reg2;
	__le32 reg3;
};

#define MFW_TRACE_SIGNATURE     0x25071946

/* The trace in the buffer */
#define MFW_TRACE_EVENTID_MASK          0x00ffff
#define MFW_TRACE_PRM_SIZE_MASK         0x0f0000
#define MFW_TRACE_PRM_SIZE_OFFSET	16
#define MFW_TRACE_ENTRY_SIZE            3

struct mcp_trace {
	u32 signature;		/* Help to identify that the trace is valid */
	u32 size;		/* the size of the trace buffer in bytes */
	u32 curr_level;		/* 2 - all will be written to the buffer
				 * 1 - debug trace will not be written
				 * 0 - just errors will be written to the buffer
				 */
	u32 modules_mask[2];	/* a bit per module, 1 means write it, 0 means
				 * mask it.
				 */

	/* Warning: the following pointers are assumed to be 32bits as they are
	 * used only in the MFW.
	 */
	u32 trace_prod; /* The next trace will be written to this offset */
	u32 trace_oldest; /* The oldest valid trace starts at this offset
			   * (usually very close after the current producer).
			   */
};

#define VF_MAX_STATIC 192

#define MCP_GLOB_PATH_MAX	2
#define MCP_PORT_MAX		2
#define MCP_GLOB_PORT_MAX	4
#define MCP_GLOB_FUNC_MAX	16

typedef u32 offsize_t;		/* In DWORDS !!! */
/* Offset from the beginning of the MCP scratchpad */
#define OFFSIZE_OFFSET_SHIFT	0
#define OFFSIZE_OFFSET_MASK	0x0000ffff
/* Size of specific element (not the whole array if any) */
#define OFFSIZE_SIZE_SHIFT	16
#define OFFSIZE_SIZE_MASK	0xffff0000

#define SECTION_OFFSET(_offsize) ((((_offsize &			\
				     OFFSIZE_OFFSET_MASK) >>	\
				    OFFSIZE_OFFSET_SHIFT) << 2))

#define QED_SECTION_SIZE(_offsize) (((_offsize &		\
				      OFFSIZE_SIZE_MASK) >>	\
				     OFFSIZE_SIZE_SHIFT) << 2)

#define SECTION_ADDR(_offsize, idx) (MCP_REG_SCRATCH +			\
				     SECTION_OFFSET(_offsize) +		\
				     (QED_SECTION_SIZE(_offsize) * idx))

#define SECTION_OFFSIZE_ADDR(_pub_base, _section)	\
	(_pub_base + offsetof(struct mcp_public_data, sections[_section]))

/* PHY configuration */
struct eth_phy_cfg {
	u32					speed;
#define ETH_SPEED_AUTONEG			0x0
#define ETH_SPEED_SMARTLINQ			0x8

	u32					pause;
#define ETH_PAUSE_NONE				0x0
#define ETH_PAUSE_AUTONEG			0x1
#define ETH_PAUSE_RX				0x2
#define ETH_PAUSE_TX				0x4

	u32					adv_speed;

	u32					loopback_mode;
#define ETH_LOOPBACK_NONE			0x0
#define ETH_LOOPBACK_INT_PHY			0x1
#define ETH_LOOPBACK_EXT_PHY			0x2
#define ETH_LOOPBACK_EXT			0x3
#define ETH_LOOPBACK_MAC			0x4
#define ETH_LOOPBACK_CNIG_AH_ONLY_0123		0x5
#define ETH_LOOPBACK_CNIG_AH_ONLY_2301		0x6
#define ETH_LOOPBACK_PCS_AH_ONLY		0x7
#define ETH_LOOPBACK_REVERSE_MAC_AH_ONLY	0x8
#define ETH_LOOPBACK_INT_PHY_FEA_AH_ONLY	0x9

	u32					eee_cfg;
#define EEE_CFG_EEE_ENABLED			BIT(0)
#define EEE_CFG_TX_LPI				BIT(1)
#define EEE_CFG_ADV_SPEED_1G			BIT(2)
#define EEE_CFG_ADV_SPEED_10G			BIT(3)
#define EEE_TX_TIMER_USEC_MASK			0xfffffff0
#define EEE_TX_TIMER_USEC_OFFSET		4
#define EEE_TX_TIMER_USEC_BALANCED_TIME		0xa00
#define EEE_TX_TIMER_USEC_AGGRESSIVE_TIME	0x100
#define EEE_TX_TIMER_USEC_LATENCY_TIME		0x6000

	u32					deprecated;

	u32					fec_mode;
#define FEC_FORCE_MODE_MASK			0x000000ff
#define FEC_FORCE_MODE_OFFSET			0
#define FEC_FORCE_MODE_NONE			0x00
#define FEC_FORCE_MODE_FIRECODE			0x01
#define FEC_FORCE_MODE_RS			0x02
#define FEC_FORCE_MODE_AUTO			0x07
#define FEC_EXTENDED_MODE_MASK			0xffffff00
#define FEC_EXTENDED_MODE_OFFSET		8
#define ETH_EXT_FEC_NONE			0x00000100
#define ETH_EXT_FEC_10G_NONE			0x00000200
#define ETH_EXT_FEC_10G_BASE_R			0x00000400
#define ETH_EXT_FEC_20G_NONE			0x00000800
#define ETH_EXT_FEC_20G_BASE_R			0x00001000
#define ETH_EXT_FEC_25G_NONE			0x00002000
#define ETH_EXT_FEC_25G_BASE_R			0x00004000
#define ETH_EXT_FEC_25G_RS528			0x00008000
#define ETH_EXT_FEC_40G_NONE			0x00010000
#define ETH_EXT_FEC_40G_BASE_R			0x00020000
#define ETH_EXT_FEC_50G_NONE			0x00040000
#define ETH_EXT_FEC_50G_BASE_R			0x00080000
#define ETH_EXT_FEC_50G_RS528			0x00100000
#define ETH_EXT_FEC_50G_RS544			0x00200000
#define ETH_EXT_FEC_100G_NONE			0x00400000
#define ETH_EXT_FEC_100G_BASE_R			0x00800000
#define ETH_EXT_FEC_100G_RS528			0x01000000
#define ETH_EXT_FEC_100G_RS544			0x02000000

	u32					extended_speed;
#define ETH_EXT_SPEED_MASK			0x0000ffff
#define ETH_EXT_SPEED_OFFSET			0
#define ETH_EXT_SPEED_AN			0x00000001
#define ETH_EXT_SPEED_1G			0x00000002
#define ETH_EXT_SPEED_10G			0x00000004
#define ETH_EXT_SPEED_20G			0x00000008
#define ETH_EXT_SPEED_25G			0x00000010
#define ETH_EXT_SPEED_40G			0x00000020
#define ETH_EXT_SPEED_50G_BASE_R		0x00000040
#define ETH_EXT_SPEED_50G_BASE_R2		0x00000080
#define ETH_EXT_SPEED_100G_BASE_R2		0x00000100
#define ETH_EXT_SPEED_100G_BASE_R4		0x00000200
#define ETH_EXT_SPEED_100G_BASE_P4		0x00000400
#define ETH_EXT_ADV_SPEED_MASK			0xffff0000
#define ETH_EXT_ADV_SPEED_OFFSET		16
#define ETH_EXT_ADV_SPEED_RESERVED		0x00010000
#define ETH_EXT_ADV_SPEED_1G			0x00020000
#define ETH_EXT_ADV_SPEED_10G			0x00040000
#define ETH_EXT_ADV_SPEED_20G			0x00080000
#define ETH_EXT_ADV_SPEED_25G			0x00100000
#define ETH_EXT_ADV_SPEED_40G			0x00200000
#define ETH_EXT_ADV_SPEED_50G_BASE_R		0x00400000
#define ETH_EXT_ADV_SPEED_50G_BASE_R2		0x00800000
#define ETH_EXT_ADV_SPEED_100G_BASE_R2		0x01000000
#define ETH_EXT_ADV_SPEED_100G_BASE_R4		0x02000000
#define ETH_EXT_ADV_SPEED_100G_BASE_P4		0x04000000
};

struct port_mf_cfg {
	u32 dynamic_cfg;
#define PORT_MF_CFG_OV_TAG_MASK		0x0000ffff
#define PORT_MF_CFG_OV_TAG_SHIFT	0
#define PORT_MF_CFG_OV_TAG_DEFAULT	PORT_MF_CFG_OV_TAG_MASK

	u32 reserved[1];
};

struct eth_stats {
	u64 r64;
	u64 r127;
	u64 r255;
	u64 r511;
	u64 r1023;
	u64 r1518;

	union {
		struct {
			u64 r1522;
			u64 r2047;
			u64 r4095;
			u64 r9216;
			u64 r16383;
		} bb0;
		struct {
			u64 unused1;
			u64 r1519_to_max;
			u64 unused2;
			u64 unused3;
			u64 unused4;
		} ah0;
	} u0;

	u64 rfcs;
	u64 rxcf;
	u64 rxpf;
	u64 rxpp;
	u64 raln;
	u64 rfcr;
	u64 rovr;
	u64 rjbr;
	u64 rund;
	u64 rfrg;
	u64 t64;
	u64 t127;
	u64 t255;
	u64 t511;
	u64 t1023;
	u64 t1518;

	union {
		struct {
			u64 t2047;
			u64 t4095;
			u64 t9216;
			u64 t16383;
		} bb1;
		struct {
			u64 t1519_to_max;
			u64 unused6;
			u64 unused7;
			u64 unused8;
		} ah1;
	} u1;

	u64 txpf;
	u64 txpp;

	union {
		struct {
			u64 tlpiec;
			u64 tncl;
		} bb2;
		struct {
			u64 unused9;
			u64 unused10;
		} ah2;
	} u2;

	u64 rbyte;
	u64 rxuca;
	u64 rxmca;
	u64 rxbca;
	u64 rxpok;
	u64 tbyte;
	u64 txuca;
	u64 txmca;
	u64 txbca;
	u64 txcf;
};

struct brb_stats {
	u64 brb_truncate[8];
	u64 brb_discard[8];
};

struct port_stats {
	struct brb_stats brb;
	struct eth_stats eth;
};

struct couple_mode_teaming {
	u8 port_cmt[MCP_GLOB_PORT_MAX];
#define PORT_CMT_IN_TEAM	(1 << 0)

#define PORT_CMT_PORT_ROLE	(1 << 1)
#define PORT_CMT_PORT_INACTIVE	(0 << 1)
#define PORT_CMT_PORT_ACTIVE	(1 << 1)

#define PORT_CMT_TEAM_MASK	(1 << 2)
#define PORT_CMT_TEAM0		(0 << 2)
#define PORT_CMT_TEAM1		(1 << 2)
};

#define LLDP_CHASSIS_ID_STAT_LEN	4
#define LLDP_PORT_ID_STAT_LEN		4
#define DCBX_MAX_APP_PROTOCOL		32
#define MAX_SYSTEM_LLDP_TLV_DATA	32

enum _lldp_agent {
	LLDP_NEAREST_BRIDGE = 0,
	LLDP_NEAREST_NON_TPMR_BRIDGE,
	LLDP_NEAREST_CUSTOMER_BRIDGE,
	LLDP_MAX_LLDP_AGENTS
};

struct lldp_config_params_s {
	u32 config;
#define LLDP_CONFIG_TX_INTERVAL_MASK	0x000000ff
#define LLDP_CONFIG_TX_INTERVAL_SHIFT	0
#define LLDP_CONFIG_HOLD_MASK		0x00000f00
#define LLDP_CONFIG_HOLD_SHIFT		8
#define LLDP_CONFIG_MAX_CREDIT_MASK	0x0000f000
#define LLDP_CONFIG_MAX_CREDIT_SHIFT	12
#define LLDP_CONFIG_ENABLE_RX_MASK	0x40000000
#define LLDP_CONFIG_ENABLE_RX_SHIFT	30
#define LLDP_CONFIG_ENABLE_TX_MASK	0x80000000
#define LLDP_CONFIG_ENABLE_TX_SHIFT	31
	u32 local_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	u32 local_port_id[LLDP_PORT_ID_STAT_LEN];
};

struct lldp_status_params_s {
	u32 prefix_seq_num;
	u32 status;
	u32 peer_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	u32 peer_port_id[LLDP_PORT_ID_STAT_LEN];
	u32 suffix_seq_num;
};

struct dcbx_ets_feature {
	u32 flags;
#define DCBX_ETS_ENABLED_MASK	0x00000001
#define DCBX_ETS_ENABLED_SHIFT	0
#define DCBX_ETS_WILLING_MASK	0x00000002
#define DCBX_ETS_WILLING_SHIFT	1
#define DCBX_ETS_ERROR_MASK	0x00000004
#define DCBX_ETS_ERROR_SHIFT	2
#define DCBX_ETS_CBS_MASK	0x00000008
#define DCBX_ETS_CBS_SHIFT	3
#define DCBX_ETS_MAX_TCS_MASK	0x000000f0
#define DCBX_ETS_MAX_TCS_SHIFT	4
#define DCBX_OOO_TC_MASK	0x00000f00
#define DCBX_OOO_TC_SHIFT	8
	u32 pri_tc_tbl[1];
#define DCBX_TCP_OOO_TC		(4)

#define NIG_ETS_ISCSI_OOO_CLIENT_OFFSET	(DCBX_TCP_OOO_TC + 1)
#define DCBX_CEE_STRICT_PRIORITY	0xf
	u32 tc_bw_tbl[2];
	u32 tc_tsa_tbl[2];
#define DCBX_ETS_TSA_STRICT	0
#define DCBX_ETS_TSA_CBS	1
#define DCBX_ETS_TSA_ETS	2
};

#define DCBX_TCP_OOO_TC			(4)
#define DCBX_TCP_OOO_K2_4PORT_TC	(3)

struct dcbx_app_priority_entry {
	u32 entry;
#define DCBX_APP_PRI_MAP_MASK		0x000000ff
#define DCBX_APP_PRI_MAP_SHIFT		0
#define DCBX_APP_PRI_0			0x01
#define DCBX_APP_PRI_1			0x02
#define DCBX_APP_PRI_2			0x04
#define DCBX_APP_PRI_3			0x08
#define DCBX_APP_PRI_4			0x10
#define DCBX_APP_PRI_5			0x20
#define DCBX_APP_PRI_6			0x40
#define DCBX_APP_PRI_7			0x80
#define DCBX_APP_SF_MASK		0x00000300
#define DCBX_APP_SF_SHIFT		8
#define DCBX_APP_SF_ETHTYPE		0
#define DCBX_APP_SF_PORT		1
#define DCBX_APP_SF_IEEE_MASK		0x0000f000
#define DCBX_APP_SF_IEEE_SHIFT		12
#define DCBX_APP_SF_IEEE_RESERVED	0
#define DCBX_APP_SF_IEEE_ETHTYPE	1
#define DCBX_APP_SF_IEEE_TCP_PORT	2
#define DCBX_APP_SF_IEEE_UDP_PORT	3
#define DCBX_APP_SF_IEEE_TCP_UDP_PORT	4

#define DCBX_APP_PROTOCOL_ID_MASK	0xffff0000
#define DCBX_APP_PROTOCOL_ID_SHIFT	16
};

struct dcbx_app_priority_feature {
	u32 flags;
#define DCBX_APP_ENABLED_MASK		0x00000001
#define DCBX_APP_ENABLED_SHIFT		0
#define DCBX_APP_WILLING_MASK		0x00000002
#define DCBX_APP_WILLING_SHIFT		1
#define DCBX_APP_ERROR_MASK		0x00000004
#define DCBX_APP_ERROR_SHIFT		2
#define DCBX_APP_MAX_TCS_MASK		0x0000f000
#define DCBX_APP_MAX_TCS_SHIFT		12
#define DCBX_APP_NUM_ENTRIES_MASK	0x00ff0000
#define DCBX_APP_NUM_ENTRIES_SHIFT	16
	struct dcbx_app_priority_entry app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

struct dcbx_features {
	struct dcbx_ets_feature ets;
	u32 pfc;
#define DCBX_PFC_PRI_EN_BITMAP_MASK	0x000000ff
#define DCBX_PFC_PRI_EN_BITMAP_SHIFT	0
#define DCBX_PFC_PRI_EN_BITMAP_PRI_0	0x01
#define DCBX_PFC_PRI_EN_BITMAP_PRI_1	0x02
#define DCBX_PFC_PRI_EN_BITMAP_PRI_2	0x04
#define DCBX_PFC_PRI_EN_BITMAP_PRI_3	0x08
#define DCBX_PFC_PRI_EN_BITMAP_PRI_4	0x10
#define DCBX_PFC_PRI_EN_BITMAP_PRI_5	0x20
#define DCBX_PFC_PRI_EN_BITMAP_PRI_6	0x40
#define DCBX_PFC_PRI_EN_BITMAP_PRI_7	0x80

#define DCBX_PFC_FLAGS_MASK		0x0000ff00
#define DCBX_PFC_FLAGS_SHIFT		8
#define DCBX_PFC_CAPS_MASK		0x00000f00
#define DCBX_PFC_CAPS_SHIFT		8
#define DCBX_PFC_MBC_MASK		0x00004000
#define DCBX_PFC_MBC_SHIFT		14
#define DCBX_PFC_WILLING_MASK		0x00008000
#define DCBX_PFC_WILLING_SHIFT		15
#define DCBX_PFC_ENABLED_MASK		0x00010000
#define DCBX_PFC_ENABLED_SHIFT		16
#define DCBX_PFC_ERROR_MASK		0x00020000
#define DCBX_PFC_ERROR_SHIFT		17

	struct dcbx_app_priority_feature app;
};

struct dcbx_local_params {
	u32 config;
#define DCBX_CONFIG_VERSION_MASK	0x00000007
#define DCBX_CONFIG_VERSION_SHIFT	0
#define DCBX_CONFIG_VERSION_DISABLED	0
#define DCBX_CONFIG_VERSION_IEEE	1
#define DCBX_CONFIG_VERSION_CEE		2
#define DCBX_CONFIG_VERSION_STATIC	4

	u32 flags;
	struct dcbx_features features;
};

struct dcbx_mib {
	u32 prefix_seq_num;
	u32 flags;
	struct dcbx_features features;
	u32 suffix_seq_num;
};

struct lldp_system_tlvs_buffer_s {
	u16 valid;
	u16 length;
	u32 data[MAX_SYSTEM_LLDP_TLV_DATA];
};

struct dcb_dscp_map {
	u32 flags;
#define DCB_DSCP_ENABLE_MASK	0x1
#define DCB_DSCP_ENABLE_SHIFT	0
#define DCB_DSCP_ENABLE	1
	u32 dscp_pri_map[8];
};

struct public_global {
	u32 max_path;
	u32 max_ports;
#define MODE_1P 1
#define MODE_2P 2
#define MODE_3P 3
#define MODE_4P 4
	u32 debug_mb_offset;
	u32 phymod_dbg_mb_offset;
	struct couple_mode_teaming cmt;
	s32 internal_temperature;
	u32 mfw_ver;
	u32 running_bundle_id;
	s32 external_temperature;
	u32 mdump_reason;
	u64 reserved;
	u32 data_ptr;
	u32 data_size;
};

struct fw_flr_mb {
	u32 aggint;
	u32 opgen_addr;
	u32 accum_ack;
};

struct public_path {
	struct fw_flr_mb flr_mb;
	u32 mcp_vf_disabled[VF_MAX_STATIC / 32];

	u32 process_kill;
#define PROCESS_KILL_COUNTER_MASK	0x0000ffff
#define PROCESS_KILL_COUNTER_SHIFT	0
#define PROCESS_KILL_GLOB_AEU_BIT_MASK	0xffff0000
#define PROCESS_KILL_GLOB_AEU_BIT_SHIFT	16
#define GLOBAL_AEU_BIT(aeu_reg_id, aeu_bit) (aeu_reg_id * 32 + aeu_bit)
};

struct public_port {
	u32						validity_map;

	u32						link_status;
#define LINK_STATUS_LINK_UP				0x00000001
#define LINK_STATUS_SPEED_AND_DUPLEX_MASK		0x0000001e
#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		(1 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(2 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10G		(3 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_20G		(4 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_40G		(5 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_50G		(6 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100G		(7 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_25G		(8 << 1)
#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED		0x00000020
#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE		0x00000040
#define LINK_STATUS_PARALLEL_DETECTION_USED		0x00000080
#define LINK_STATUS_PFC_ENABLED				0x00000100
#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE	0x00000200
#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE	0x00000400
#define LINK_STATUS_LINK_PARTNER_10G_CAPABLE		0x00000800
#define LINK_STATUS_LINK_PARTNER_20G_CAPABLE		0x00001000
#define LINK_STATUS_LINK_PARTNER_40G_CAPABLE		0x00002000
#define LINK_STATUS_LINK_PARTNER_50G_CAPABLE		0x00004000
#define LINK_STATUS_LINK_PARTNER_100G_CAPABLE		0x00008000
#define LINK_STATUS_LINK_PARTNER_25G_CAPABLE		0x00010000
#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK	0x000c0000
#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE	(0 << 18)
#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	(1 << 18)
#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE	(2 << 18)
#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE		(3 << 18)
#define LINK_STATUS_SFP_TX_FAULT			0x00100000
#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED		0x00200000
#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED		0x00400000
#define LINK_STATUS_RX_SIGNAL_PRESENT			0x00800000
#define LINK_STATUS_MAC_LOCAL_FAULT			0x01000000
#define LINK_STATUS_MAC_REMOTE_FAULT			0x02000000
#define LINK_STATUS_UNSUPPORTED_SPD_REQ			0x04000000

#define LINK_STATUS_FEC_MODE_MASK			0x38000000
#define LINK_STATUS_FEC_MODE_NONE			(0 << 27)
#define LINK_STATUS_FEC_MODE_FIRECODE_CL74		(1 << 27)
#define LINK_STATUS_FEC_MODE_RS_CL91			(2 << 27)

	u32 link_status1;
	u32 ext_phy_fw_version;
	u32 drv_phy_cfg_addr;

	u32 port_stx;

	u32 stat_nig_timer;

	struct port_mf_cfg port_mf_config;
	struct port_stats stats;

	u32 media_type;
#define MEDIA_UNSPECIFIED	0x0
#define MEDIA_SFPP_10G_FIBER	0x1
#define MEDIA_XFP_FIBER		0x2
#define MEDIA_DA_TWINAX		0x3
#define MEDIA_BASE_T		0x4
#define MEDIA_SFP_1G_FIBER	0x5
#define MEDIA_MODULE_FIBER	0x6
#define MEDIA_KR		0xf0
#define MEDIA_NOT_PRESENT	0xff

	u32 lfa_status;
	u32 link_change_count;

	struct lldp_config_params_s lldp_config_params[LLDP_MAX_LLDP_AGENTS];
	struct lldp_status_params_s lldp_status_params[LLDP_MAX_LLDP_AGENTS];
	struct lldp_system_tlvs_buffer_s system_lldp_tlvs_buf;

	/* DCBX related MIB */
	struct dcbx_local_params local_admin_dcbx_mib;
	struct dcbx_mib remote_dcbx_mib;
	struct dcbx_mib operational_dcbx_mib;

	u32 reserved[2];

	u32						transceiver_data;
#define ETH_TRANSCEIVER_STATE_MASK			0x000000ff
#define ETH_TRANSCEIVER_STATE_SHIFT			0x00000000
#define ETH_TRANSCEIVER_STATE_OFFSET			0x00000000
#define ETH_TRANSCEIVER_STATE_UNPLUGGED			0x00000000
#define ETH_TRANSCEIVER_STATE_PRESENT			0x00000001
#define ETH_TRANSCEIVER_STATE_VALID			0x00000003
#define ETH_TRANSCEIVER_STATE_UPDATING			0x00000008
#define ETH_TRANSCEIVER_TYPE_MASK			0x0000ff00
#define ETH_TRANSCEIVER_TYPE_OFFSET			0x8
#define ETH_TRANSCEIVER_TYPE_NONE			0x00
#define ETH_TRANSCEIVER_TYPE_UNKNOWN			0xff
#define ETH_TRANSCEIVER_TYPE_1G_PCC			0x01
#define ETH_TRANSCEIVER_TYPE_1G_ACC			0x02
#define ETH_TRANSCEIVER_TYPE_1G_LX			0x03
#define ETH_TRANSCEIVER_TYPE_1G_SX			0x04
#define ETH_TRANSCEIVER_TYPE_10G_SR			0x05
#define ETH_TRANSCEIVER_TYPE_10G_LR			0x06
#define ETH_TRANSCEIVER_TYPE_10G_LRM			0x07
#define ETH_TRANSCEIVER_TYPE_10G_ER			0x08
#define ETH_TRANSCEIVER_TYPE_10G_PCC			0x09
#define ETH_TRANSCEIVER_TYPE_10G_ACC			0x0a
#define ETH_TRANSCEIVER_TYPE_XLPPI			0x0b
#define ETH_TRANSCEIVER_TYPE_40G_LR4			0x0c
#define ETH_TRANSCEIVER_TYPE_40G_SR4			0x0d
#define ETH_TRANSCEIVER_TYPE_40G_CR4			0x0e
#define ETH_TRANSCEIVER_TYPE_100G_AOC			0x0f
#define ETH_TRANSCEIVER_TYPE_100G_SR4			0x10
#define ETH_TRANSCEIVER_TYPE_100G_LR4			0x11
#define ETH_TRANSCEIVER_TYPE_100G_ER4			0x12
#define ETH_TRANSCEIVER_TYPE_100G_ACC			0x13
#define ETH_TRANSCEIVER_TYPE_100G_CR4			0x14
#define ETH_TRANSCEIVER_TYPE_4x10G_SR			0x15
#define ETH_TRANSCEIVER_TYPE_25G_CA_N			0x16
#define ETH_TRANSCEIVER_TYPE_25G_ACC_S			0x17
#define ETH_TRANSCEIVER_TYPE_25G_CA_S			0x18
#define ETH_TRANSCEIVER_TYPE_25G_ACC_M			0x19
#define ETH_TRANSCEIVER_TYPE_25G_CA_L			0x1a
#define ETH_TRANSCEIVER_TYPE_25G_ACC_L			0x1b
#define ETH_TRANSCEIVER_TYPE_25G_SR			0x1c
#define ETH_TRANSCEIVER_TYPE_25G_LR			0x1d
#define ETH_TRANSCEIVER_TYPE_25G_AOC			0x1e
#define ETH_TRANSCEIVER_TYPE_4x10G			0x1f
#define ETH_TRANSCEIVER_TYPE_4x25G_CR			0x20
#define ETH_TRANSCEIVER_TYPE_1000BASET			0x21
#define ETH_TRANSCEIVER_TYPE_10G_BASET			0x22
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR	0x30
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_CR	0x31
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR	0x32
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR	0x33
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR	0x34
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR	0x35
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_AOC	0x36
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_SR	0x37
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_LR	0x38
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_SR	0x39
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_LR	0x3a

	u32 wol_info;
	u32 wol_pkt_len;
	u32 wol_pkt_details;
	struct dcb_dscp_map dcb_dscp_map;

	u32 eee_status;
#define EEE_ACTIVE_BIT			BIT(0)
#define EEE_LD_ADV_STATUS_MASK		0x000000f0
#define EEE_LD_ADV_STATUS_OFFSET	4
#define EEE_1G_ADV			BIT(1)
#define EEE_10G_ADV			BIT(2)
#define EEE_LP_ADV_STATUS_MASK		0x00000f00
#define EEE_LP_ADV_STATUS_OFFSET	8
#define EEE_SUPPORTED_SPEED_MASK	0x0000f000
#define EEE_SUPPORTED_SPEED_OFFSET	12
#define EEE_1G_SUPPORTED		BIT(1)
#define EEE_10G_SUPPORTED		BIT(2)

	u32 eee_remote;
#define EEE_REMOTE_TW_TX_MASK   0x0000ffff
#define EEE_REMOTE_TW_TX_OFFSET 0
#define EEE_REMOTE_TW_RX_MASK   0xffff0000
#define EEE_REMOTE_TW_RX_OFFSET 16

	u32 reserved1;
	u32 oem_cfg_port;
#define OEM_CFG_CHANNEL_TYPE_MASK                       0x00000003
#define OEM_CFG_CHANNEL_TYPE_OFFSET                     0
#define OEM_CFG_CHANNEL_TYPE_VLAN_PARTITION             0x1
#define OEM_CFG_CHANNEL_TYPE_STAGGED                    0x2
#define OEM_CFG_SCHED_TYPE_MASK                         0x0000000C
#define OEM_CFG_SCHED_TYPE_OFFSET                       2
#define OEM_CFG_SCHED_TYPE_ETS                          0x1
#define OEM_CFG_SCHED_TYPE_VNIC_BW                      0x2
};

struct public_func {
	u32 reserved0[2];

	u32 mtu_size;

	u32 reserved[7];

	u32 config;
#define FUNC_MF_CFG_FUNC_HIDE			0x00000001
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING		0x00000002
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING_SHIFT	0x00000001

#define FUNC_MF_CFG_PROTOCOL_MASK	0x000000f0
#define FUNC_MF_CFG_PROTOCOL_SHIFT	4
#define FUNC_MF_CFG_PROTOCOL_ETHERNET	0x00000000
#define FUNC_MF_CFG_PROTOCOL_ISCSI              0x00000010
#define FUNC_MF_CFG_PROTOCOL_FCOE               0x00000020
#define FUNC_MF_CFG_PROTOCOL_ROCE               0x00000030
#define FUNC_MF_CFG_PROTOCOL_NVMETCP    0x00000040
#define FUNC_MF_CFG_PROTOCOL_MAX	0x00000040

#define FUNC_MF_CFG_MIN_BW_MASK		0x0000ff00
#define FUNC_MF_CFG_MIN_BW_SHIFT	8
#define FUNC_MF_CFG_MIN_BW_DEFAULT	0x00000000
#define FUNC_MF_CFG_MAX_BW_MASK		0x00ff0000
#define FUNC_MF_CFG_MAX_BW_SHIFT	16
#define FUNC_MF_CFG_MAX_BW_DEFAULT	0x00640000

	u32 status;
#define FUNC_STATUS_VIRTUAL_LINK_UP	0x00000001

	u32 mac_upper;
#define FUNC_MF_CFG_UPPERMAC_MASK	0x0000ffff
#define FUNC_MF_CFG_UPPERMAC_SHIFT	0
#define FUNC_MF_CFG_UPPERMAC_DEFAULT	FUNC_MF_CFG_UPPERMAC_MASK
	u32 mac_lower;
#define FUNC_MF_CFG_LOWERMAC_DEFAULT	0xffffffff

	u32 fcoe_wwn_port_name_upper;
	u32 fcoe_wwn_port_name_lower;

	u32 fcoe_wwn_node_name_upper;
	u32 fcoe_wwn_node_name_lower;

	u32 ovlan_stag;
#define FUNC_MF_CFG_OV_STAG_MASK	0x0000ffff
#define FUNC_MF_CFG_OV_STAG_SHIFT	0
#define FUNC_MF_CFG_OV_STAG_DEFAULT	FUNC_MF_CFG_OV_STAG_MASK

	u32 pf_allocation;

	u32 preserve_data;

	u32 driver_last_activity_ts;

	u32 drv_ack_vf_disabled[VF_MAX_STATIC / 32];

	u32 drv_id;
#define DRV_ID_PDA_COMP_VER_MASK	0x0000ffff
#define DRV_ID_PDA_COMP_VER_SHIFT	0

#define LOAD_REQ_HSI_VERSION		2
#define DRV_ID_MCP_HSI_VER_MASK		0x00ff0000
#define DRV_ID_MCP_HSI_VER_SHIFT	16
#define DRV_ID_MCP_HSI_VER_CURRENT	(LOAD_REQ_HSI_VERSION << \
					 DRV_ID_MCP_HSI_VER_SHIFT)

#define DRV_ID_DRV_TYPE_MASK		0x7f000000
#define DRV_ID_DRV_TYPE_SHIFT		24
#define DRV_ID_DRV_TYPE_UNKNOWN		(0 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_LINUX		(1 << DRV_ID_DRV_TYPE_SHIFT)

#define DRV_ID_DRV_INIT_HW_MASK		0x80000000
#define DRV_ID_DRV_INIT_HW_SHIFT	31
#define DRV_ID_DRV_INIT_HW_FLAG		(1 << DRV_ID_DRV_INIT_HW_SHIFT)

	u32 oem_cfg_func;
#define OEM_CFG_FUNC_TC_MASK                    0x0000000F
#define OEM_CFG_FUNC_TC_OFFSET                  0
#define OEM_CFG_FUNC_TC_0                       0x0
#define OEM_CFG_FUNC_TC_1                       0x1
#define OEM_CFG_FUNC_TC_2                       0x2
#define OEM_CFG_FUNC_TC_3                       0x3
#define OEM_CFG_FUNC_TC_4                       0x4
#define OEM_CFG_FUNC_TC_5                       0x5
#define OEM_CFG_FUNC_TC_6                       0x6
#define OEM_CFG_FUNC_TC_7                       0x7

#define OEM_CFG_FUNC_HOST_PRI_CTRL_MASK         0x00000030
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OFFSET       4
#define OEM_CFG_FUNC_HOST_PRI_CTRL_VNIC         0x1
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OS           0x2
};

struct mcp_mac {
	u32 mac_upper;
	u32 mac_lower;
};

struct mcp_val64 {
	u32 lo;
	u32 hi;
};

struct mcp_file_att {
	u32 nvm_start_addr;
	u32 len;
};

struct bist_nvm_image_att {
	u32 return_code;
	u32 image_type;
	u32 nvm_start_addr;
	u32 len;
};

#define MCP_DRV_VER_STR_SIZE 16
#define MCP_DRV_VER_STR_SIZE_DWORD (MCP_DRV_VER_STR_SIZE / sizeof(u32))
#define MCP_DRV_NVM_BUF_LEN 32
struct drv_version_stc {
	u32 version;
	u8 name[MCP_DRV_VER_STR_SIZE - 4];
};

struct lan_stats_stc {
	u64 ucast_rx_pkts;
	u64 ucast_tx_pkts;
	u32 fcs_err;
	u32 rserved;
};

struct fcoe_stats_stc {
	u64 rx_pkts;
	u64 tx_pkts;
	u32 fcs_err;
	u32 login_failure;
};

struct ocbb_data_stc {
	u32 ocbb_host_addr;
	u32 ocsd_host_addr;
	u32 ocsd_req_update_interval;
};

#define MAX_NUM_OF_SENSORS 7
struct temperature_status_stc {
	u32 num_of_sensors;
	u32 sensor[MAX_NUM_OF_SENSORS];
};

/* crash dump configuration header */
struct mdump_config_stc {
	u32 version;
	u32 config;
	u32 epoc;
	u32 num_of_logs;
	u32 valid_logs;
};

enum resource_id_enum {
	RESOURCE_NUM_SB_E = 0,
	RESOURCE_NUM_L2_QUEUE_E = 1,
	RESOURCE_NUM_VPORT_E = 2,
	RESOURCE_NUM_VMQ_E = 3,
	RESOURCE_FACTOR_NUM_RSS_PF_E = 4,
	RESOURCE_FACTOR_RSS_PER_VF_E = 5,
	RESOURCE_NUM_RL_E = 6,
	RESOURCE_NUM_PQ_E = 7,
	RESOURCE_NUM_VF_E = 8,
	RESOURCE_VFC_FILTER_E = 9,
	RESOURCE_ILT_E = 10,
	RESOURCE_CQS_E = 11,
	RESOURCE_GFT_PROFILES_E = 12,
	RESOURCE_NUM_TC_E = 13,
	RESOURCE_NUM_RSS_ENGINES_E = 14,
	RESOURCE_LL2_QUEUE_E = 15,
	RESOURCE_RDMA_STATS_QUEUE_E = 16,
	RESOURCE_BDQ_E = 17,
	RESOURCE_QCN_E = 18,
	RESOURCE_LLH_FILTER_E = 19,
	RESOURCE_VF_MAC_ADDR = 20,
	RESOURCE_LL2_CQS_E = 21,
	RESOURCE_VF_CNQS = 22,
	RESOURCE_MAX_NUM,
	RESOURCE_NUM_INVALID = 0xFFFFFFFF
};

/* Resource ID is to be filled by the driver in the MB request
 * Size, offset & flags to be filled by the MFW in the MB response
 */
struct resource_info {
	enum resource_id_enum res_id;
	u32 size;		/* number of allocated resources */
	u32 offset;		/* Offset of the 1st resource */
	u32 vf_size;
	u32 vf_offset;
	u32 flags;
#define RESOURCE_ELEMENT_STRICT (1 << 0)
};

#define DRV_ROLE_NONE           0
#define DRV_ROLE_PREBOOT        1
#define DRV_ROLE_OS             2
#define DRV_ROLE_KDUMP          3

struct load_req_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_REQ_ROLE_MASK              0x000000FF
#define LOAD_REQ_ROLE_SHIFT             0
#define LOAD_REQ_LOCK_TO_MASK           0x0000FF00
#define LOAD_REQ_LOCK_TO_SHIFT          8
#define LOAD_REQ_LOCK_TO_DEFAULT        0
#define LOAD_REQ_LOCK_TO_NONE           255
#define LOAD_REQ_FORCE_MASK             0x000F0000
#define LOAD_REQ_FORCE_SHIFT            16
#define LOAD_REQ_FORCE_NONE             0
#define LOAD_REQ_FORCE_PF               1
#define LOAD_REQ_FORCE_ALL              2
#define LOAD_REQ_FLAGS0_MASK            0x00F00000
#define LOAD_REQ_FLAGS0_SHIFT           20
#define LOAD_REQ_FLAGS0_AVOID_RESET     (0x1 << 0)
};

struct load_rsp_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_RSP_ROLE_MASK              0x000000FF
#define LOAD_RSP_ROLE_SHIFT             0
#define LOAD_RSP_HSI_MASK               0x0000FF00
#define LOAD_RSP_HSI_SHIFT              8
#define LOAD_RSP_FLAGS0_MASK            0x000F0000
#define LOAD_RSP_FLAGS0_SHIFT           16
#define LOAD_RSP_FLAGS0_DRV_EXISTS      (0x1 << 0)
};

struct mdump_retain_data_stc {
	u32 valid;
	u32 epoch;
	u32 pf;
	u32 status;
};

union drv_union_data {
	u32 ver_str[MCP_DRV_VER_STR_SIZE_DWORD];
	struct mcp_mac wol_mac;

	struct eth_phy_cfg drv_phy_cfg;

	struct mcp_val64 val64;

	u8 raw_data[MCP_DRV_NVM_BUF_LEN];

	struct mcp_file_att file_att;

	u32 ack_vf_disabled[VF_MAX_STATIC / 32];

	struct drv_version_stc drv_version;

	struct lan_stats_stc lan_stats;
	struct fcoe_stats_stc fcoe_stats;
	struct ocbb_data_stc ocbb_info;
	struct temperature_status_stc temp_info;
	struct resource_info resource;
	struct bist_nvm_image_att nvm_image_att;
	struct mdump_config_stc mdump_config;
};

struct public_drv_mb {
	u32 drv_mb_header;
#define DRV_MSG_CODE_MASK			0xffff0000
#define DRV_MSG_CODE_LOAD_REQ			0x10000000
#define DRV_MSG_CODE_LOAD_DONE			0x11000000
#define DRV_MSG_CODE_INIT_HW			0x12000000
#define DRV_MSG_CODE_CANCEL_LOAD_REQ            0x13000000
#define DRV_MSG_CODE_UNLOAD_REQ			0x20000000
#define DRV_MSG_CODE_UNLOAD_DONE		0x21000000
#define DRV_MSG_CODE_INIT_PHY			0x22000000
#define DRV_MSG_CODE_LINK_RESET			0x23000000
#define DRV_MSG_CODE_SET_DCBX			0x25000000
#define DRV_MSG_CODE_OV_UPDATE_CURR_CFG         0x26000000
#define DRV_MSG_CODE_OV_UPDATE_BUS_NUM          0x27000000
#define DRV_MSG_CODE_OV_UPDATE_BOOT_PROGRESS    0x28000000
#define DRV_MSG_CODE_OV_UPDATE_STORM_FW_VER     0x29000000
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE     0x31000000
#define DRV_MSG_CODE_BW_UPDATE_ACK              0x32000000
#define DRV_MSG_CODE_OV_UPDATE_MTU              0x33000000
#define DRV_MSG_GET_RESOURCE_ALLOC_MSG		0x34000000
#define DRV_MSG_SET_RESOURCE_VALUE_MSG		0x35000000
#define DRV_MSG_CODE_OV_UPDATE_WOL              0x38000000
#define DRV_MSG_CODE_OV_UPDATE_ESWITCH_MODE     0x39000000
#define DRV_MSG_CODE_GET_OEM_UPDATES            0x41000000

#define DRV_MSG_CODE_BW_UPDATE_ACK		0x32000000
#define DRV_MSG_CODE_NIG_DRAIN			0x30000000
#define DRV_MSG_CODE_S_TAG_UPDATE_ACK		0x3b000000
#define DRV_MSG_CODE_GET_NVM_CFG_OPTION		0x003e0000
#define DRV_MSG_CODE_SET_NVM_CFG_OPTION		0x003f0000
#define DRV_MSG_CODE_INITIATE_PF_FLR            0x02010000
#define DRV_MSG_CODE_VF_DISABLED_DONE		0xc0000000
#define DRV_MSG_CODE_CFG_VF_MSIX		0xc0010000
#define DRV_MSG_CODE_CFG_PF_VFS_MSIX		0xc0020000
#define DRV_MSG_CODE_NVM_PUT_FILE_BEGIN		0x00010000
#define DRV_MSG_CODE_NVM_PUT_FILE_DATA		0x00020000
#define DRV_MSG_CODE_NVM_GET_FILE_ATT		0x00030000
#define DRV_MSG_CODE_NVM_READ_NVRAM		0x00050000
#define DRV_MSG_CODE_NVM_WRITE_NVRAM		0x00060000
#define DRV_MSG_CODE_MCP_RESET			0x00090000
#define DRV_MSG_CODE_SET_VERSION		0x000f0000
#define DRV_MSG_CODE_MCP_HALT                   0x00100000
#define DRV_MSG_CODE_SET_VMAC                   0x00110000
#define DRV_MSG_CODE_GET_VMAC                   0x00120000
#define DRV_MSG_CODE_VMAC_TYPE_SHIFT            4
#define DRV_MSG_CODE_VMAC_TYPE_MASK             0x30
#define DRV_MSG_CODE_VMAC_TYPE_MAC              1
#define DRV_MSG_CODE_VMAC_TYPE_WWNN             2
#define DRV_MSG_CODE_VMAC_TYPE_WWPN             3

#define DRV_MSG_CODE_GET_STATS                  0x00130000
#define DRV_MSG_CODE_STATS_TYPE_LAN             1
#define DRV_MSG_CODE_STATS_TYPE_FCOE            2
#define DRV_MSG_CODE_STATS_TYPE_ISCSI           3
#define DRV_MSG_CODE_STATS_TYPE_RDMA            4

#define DRV_MSG_CODE_TRANSCEIVER_READ           0x00160000

#define DRV_MSG_CODE_MASK_PARITIES              0x001a0000

#define DRV_MSG_CODE_BIST_TEST			0x001e0000
#define DRV_MSG_CODE_SET_LED_MODE		0x00200000
#define DRV_MSG_CODE_RESOURCE_CMD		0x00230000
/* Send crash dump commands with param[3:0] - opcode */
#define DRV_MSG_CODE_MDUMP_CMD			0x00250000
#define DRV_MSG_CODE_GET_TLV_DONE		0x002f0000
#define DRV_MSG_CODE_GET_ENGINE_CONFIG		0x00370000
#define DRV_MSG_CODE_GET_PPFID_BITMAP		0x43000000

#define DRV_MSG_CODE_DEBUG_DATA_SEND		0xc0040000

#define RESOURCE_CMD_REQ_RESC_MASK		0x0000001F
#define RESOURCE_CMD_REQ_RESC_SHIFT		0
#define RESOURCE_CMD_REQ_OPCODE_MASK		0x000000E0
#define RESOURCE_CMD_REQ_OPCODE_SHIFT		5
#define RESOURCE_OPCODE_REQ			1
#define RESOURCE_OPCODE_REQ_WO_AGING		2
#define RESOURCE_OPCODE_REQ_W_AGING		3
#define RESOURCE_OPCODE_RELEASE			4
#define RESOURCE_OPCODE_FORCE_RELEASE		5
#define RESOURCE_CMD_REQ_AGE_MASK		0x0000FF00
#define RESOURCE_CMD_REQ_AGE_SHIFT		8

#define RESOURCE_CMD_RSP_OWNER_MASK		0x000000FF
#define RESOURCE_CMD_RSP_OWNER_SHIFT		0
#define RESOURCE_CMD_RSP_OPCODE_MASK		0x00000700
#define RESOURCE_CMD_RSP_OPCODE_SHIFT		8
#define RESOURCE_OPCODE_GNT			1
#define RESOURCE_OPCODE_BUSY			2
#define RESOURCE_OPCODE_RELEASED		3
#define RESOURCE_OPCODE_RELEASED_PREVIOUS	4
#define RESOURCE_OPCODE_WRONG_OWNER		5
#define RESOURCE_OPCODE_UNKNOWN_CMD		255

#define RESOURCE_DUMP				0

/* DRV_MSG_CODE_MDUMP_CMD parameters */
#define MDUMP_DRV_PARAM_OPCODE_MASK             0x0000000f
#define DRV_MSG_CODE_MDUMP_ACK                  0x01
#define DRV_MSG_CODE_MDUMP_SET_VALUES           0x02
#define DRV_MSG_CODE_MDUMP_TRIGGER              0x03
#define DRV_MSG_CODE_MDUMP_GET_CONFIG           0x04
#define DRV_MSG_CODE_MDUMP_SET_ENABLE           0x05
#define DRV_MSG_CODE_MDUMP_CLEAR_LOGS           0x06
#define DRV_MSG_CODE_MDUMP_GET_RETAIN           0x07
#define DRV_MSG_CODE_MDUMP_CLR_RETAIN           0x08

#define DRV_MSG_CODE_HW_DUMP_TRIGGER            0x0a
#define DRV_MSG_CODE_MDUMP_GEN_MDUMP2           0x0b
#define DRV_MSG_CODE_MDUMP_FREE_MDUMP2          0x0c

#define DRV_MSG_CODE_GET_PF_RDMA_PROTOCOL	0x002b0000
#define DRV_MSG_CODE_OS_WOL			0x002e0000

#define DRV_MSG_CODE_FEATURE_SUPPORT		0x00300000
#define DRV_MSG_CODE_GET_MFW_FEATURE_SUPPORT	0x00310000
#define DRV_MSG_SEQ_NUMBER_MASK			0x0000ffff

	u32 drv_mb_param;
#define DRV_MB_PARAM_UNLOAD_WOL_UNKNOWN         0x00000000
#define DRV_MB_PARAM_UNLOAD_WOL_MCP             0x00000001
#define DRV_MB_PARAM_UNLOAD_WOL_DISABLED        0x00000002
#define DRV_MB_PARAM_UNLOAD_WOL_ENABLED         0x00000003
#define DRV_MB_PARAM_DCBX_NOTIFY_MASK		0x000000FF
#define DRV_MB_PARAM_DCBX_NOTIFY_SHIFT		3

#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MBI     0x3
#define DRV_MB_PARAM_NVM_OFFSET_OFFSET          0
#define DRV_MB_PARAM_NVM_OFFSET_MASK            0x00FFFFFF
#define DRV_MB_PARAM_NVM_LEN_OFFSET		24
#define DRV_MB_PARAM_NVM_LEN_MASK               0xFF000000

#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_SHIFT	0
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_MASK	0x000000FF
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_SHIFT	8
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_MASK	0x0000FF00
#define DRV_MB_PARAM_LLDP_SEND_MASK		0x00000001
#define DRV_MB_PARAM_LLDP_SEND_SHIFT		0

#define DRV_MB_PARAM_OV_CURR_CFG_SHIFT		0
#define DRV_MB_PARAM_OV_CURR_CFG_MASK		0x0000000F
#define DRV_MB_PARAM_OV_CURR_CFG_NONE		0
#define DRV_MB_PARAM_OV_CURR_CFG_OS		1
#define DRV_MB_PARAM_OV_CURR_CFG_VENDOR_SPEC	2
#define DRV_MB_PARAM_OV_CURR_CFG_OTHER		3

#define DRV_MB_PARAM_OV_STORM_FW_VER_SHIFT	0
#define DRV_MB_PARAM_OV_STORM_FW_VER_MASK	0xFFFFFFFF
#define DRV_MB_PARAM_OV_STORM_FW_VER_MAJOR_MASK	0xFF000000
#define DRV_MB_PARAM_OV_STORM_FW_VER_MINOR_MASK	0x00FF0000
#define DRV_MB_PARAM_OV_STORM_FW_VER_BUILD_MASK	0x0000FF00
#define DRV_MB_PARAM_OV_STORM_FW_VER_DROP_MASK	0x000000FF

#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_SHIFT	0
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_MASK	0xF
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_UNKNOWN	0x1
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_NOT_LOADED	0x2
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_LOADING	0x3
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_DISABLED	0x4
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_ACTIVE	0x5

#define DRV_MB_PARAM_OV_MTU_SIZE_SHIFT	0
#define DRV_MB_PARAM_OV_MTU_SIZE_MASK	0xFFFFFFFF

#define DRV_MB_PARAM_WOL_MASK	(DRV_MB_PARAM_WOL_DEFAULT | \
				 DRV_MB_PARAM_WOL_DISABLED | \
				 DRV_MB_PARAM_WOL_ENABLED)
#define DRV_MB_PARAM_WOL_DEFAULT	DRV_MB_PARAM_UNLOAD_WOL_MCP
#define DRV_MB_PARAM_WOL_DISABLED	DRV_MB_PARAM_UNLOAD_WOL_DISABLED
#define DRV_MB_PARAM_WOL_ENABLED	DRV_MB_PARAM_UNLOAD_WOL_ENABLED

#define DRV_MB_PARAM_ESWITCH_MODE_MASK	(DRV_MB_PARAM_ESWITCH_MODE_NONE | \
					 DRV_MB_PARAM_ESWITCH_MODE_VEB | \
					 DRV_MB_PARAM_ESWITCH_MODE_VEPA)
#define DRV_MB_PARAM_ESWITCH_MODE_NONE	0x0
#define DRV_MB_PARAM_ESWITCH_MODE_VEB	0x1
#define DRV_MB_PARAM_ESWITCH_MODE_VEPA	0x2

#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_MASK	0x1
#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_OFFSET	0

#define DRV_MB_PARAM_SET_LED_MODE_OPER		0x0
#define DRV_MB_PARAM_SET_LED_MODE_ON		0x1
#define DRV_MB_PARAM_SET_LED_MODE_OFF		0x2

#define DRV_MB_PARAM_TRANSCEIVER_PORT_OFFSET			0
#define DRV_MB_PARAM_TRANSCEIVER_PORT_MASK			0x00000003
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_OFFSET			2
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_MASK			0x000000fc
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_OFFSET		8
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_MASK		0x0000ff00
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_OFFSET			16
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_MASK			0xffff0000

	/* Resource Allocation params - Driver version support */
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK		0xffff0000
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT		16
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK		0x0000ffff
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT		0

#define DRV_MB_PARAM_BIST_REGISTER_TEST				1
#define DRV_MB_PARAM_BIST_CLOCK_TEST				2
#define DRV_MB_PARAM_BIST_NVM_TEST_NUM_IMAGES			3
#define DRV_MB_PARAM_BIST_NVM_TEST_IMAGE_BY_INDEX		4

#define DRV_MB_PARAM_BIST_RC_UNKNOWN				0
#define DRV_MB_PARAM_BIST_RC_PASSED				1
#define DRV_MB_PARAM_BIST_RC_FAILED				2
#define DRV_MB_PARAM_BIST_RC_INVALID_PARAMETER			3

#define DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT			0
#define DRV_MB_PARAM_BIST_TEST_INDEX_MASK			0x000000ff
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_SHIFT		8
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_MASK			0x0000ff00

#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_MASK			0x0000ffff
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_OFFSET		0
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EEE			0x00000002
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_FEC_CONTROL		0x00000004
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EXT_SPEED_FEC_CONTROL	0x00000008
#define DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_VLINK			0x00010000

/* DRV_MSG_CODE_DEBUG_DATA_SEND parameters */
#define DRV_MSG_CODE_DEBUG_DATA_SEND_SIZE_OFFSET		0
#define DRV_MSG_CODE_DEBUG_DATA_SEND_SIZE_MASK			0xff

/* Driver attributes params */
#define DRV_MB_PARAM_ATTRIBUTE_KEY_OFFSET			0
#define DRV_MB_PARAM_ATTRIBUTE_KEY_MASK				0x00ffffff
#define DRV_MB_PARAM_ATTRIBUTE_CMD_OFFSET			24
#define DRV_MB_PARAM_ATTRIBUTE_CMD_MASK				0xff000000

#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_OFFSET			0
#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_SHIFT			0
#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_MASK			0x0000ffff
#define DRV_MB_PARAM_NVM_CFG_OPTION_ALL_SHIFT			16
#define DRV_MB_PARAM_NVM_CFG_OPTION_ALL_MASK			0x00010000
#define DRV_MB_PARAM_NVM_CFG_OPTION_INIT_SHIFT			17
#define DRV_MB_PARAM_NVM_CFG_OPTION_INIT_MASK			0x00020000
#define DRV_MB_PARAM_NVM_CFG_OPTION_COMMIT_SHIFT		18
#define DRV_MB_PARAM_NVM_CFG_OPTION_COMMIT_MASK			0x00040000
#define DRV_MB_PARAM_NVM_CFG_OPTION_FREE_SHIFT			19
#define DRV_MB_PARAM_NVM_CFG_OPTION_FREE_MASK			0x00080000
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL_SHIFT		20
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL_MASK		0x00100000
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID_SHIFT		24
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID_MASK		0x0f000000

	u32 fw_mb_header;
#define FW_MSG_CODE_MASK			0xffff0000
#define FW_MSG_CODE_UNSUPPORTED                 0x00000000
#define FW_MSG_CODE_DRV_LOAD_ENGINE		0x10100000
#define FW_MSG_CODE_DRV_LOAD_PORT		0x10110000
#define FW_MSG_CODE_DRV_LOAD_FUNCTION		0x10120000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_PDA	0x10200000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_HSI_1	0x10210000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_DIAG	0x10220000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_HSI        0x10230000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_REQUIRES_FORCE 0x10300000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_REJECT     0x10310000
#define FW_MSG_CODE_DRV_LOAD_DONE		0x11100000
#define FW_MSG_CODE_DRV_UNLOAD_ENGINE		0x20110000
#define FW_MSG_CODE_DRV_UNLOAD_PORT		0x20120000
#define FW_MSG_CODE_DRV_UNLOAD_FUNCTION		0x20130000
#define FW_MSG_CODE_DRV_UNLOAD_DONE		0x21100000
#define FW_MSG_CODE_RESOURCE_ALLOC_OK           0x34000000
#define FW_MSG_CODE_RESOURCE_ALLOC_UNKNOWN      0x35000000
#define FW_MSG_CODE_RESOURCE_ALLOC_DEPRECATED   0x36000000
#define FW_MSG_CODE_S_TAG_UPDATE_ACK_DONE	0x3b000000
#define FW_MSG_CODE_DRV_CFG_VF_MSIX_DONE	0xb0010000

#define FW_MSG_CODE_NVM_OK			0x00010000
#define FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK	0x00400000
#define FW_MSG_CODE_PHY_OK			0x00110000
#define FW_MSG_CODE_OK				0x00160000
#define FW_MSG_CODE_ERROR			0x00170000
#define FW_MSG_CODE_TRANSCEIVER_DIAG_OK		0x00160000
#define FW_MSG_CODE_TRANSCEIVER_DIAG_ERROR	0x00170000
#define FW_MSG_CODE_TRANSCEIVER_NOT_PRESENT	0x00020000

#define FW_MSG_CODE_OS_WOL_SUPPORTED            0x00800000
#define FW_MSG_CODE_OS_WOL_NOT_SUPPORTED        0x00810000
#define FW_MSG_CODE_DRV_CFG_PF_VFS_MSIX_DONE	0x00870000
#define FW_MSG_SEQ_NUMBER_MASK			0x0000ffff

#define FW_MSG_CODE_DEBUG_DATA_SEND_INV_ARG	0xb0070000
#define FW_MSG_CODE_DEBUG_DATA_SEND_BUF_FULL	0xb0080000
#define FW_MSG_CODE_DEBUG_DATA_SEND_NO_BUF	0xb0090000
#define FW_MSG_CODE_DEBUG_NOT_ENABLED		0xb00a0000
#define FW_MSG_CODE_DEBUG_DATA_SEND_OK		0xb00b0000

#define FW_MSG_CODE_MDUMP_INVALID_CMD		0x00030000

	u32							fw_mb_param;
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK		0xffff0000
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT		16
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK		0x0000ffff
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT		0

	/* Get PF RDMA protocol command response */
#define FW_MB_PARAM_GET_PF_RDMA_NONE				0x0
#define FW_MB_PARAM_GET_PF_RDMA_ROCE				0x1
#define FW_MB_PARAM_GET_PF_RDMA_IWARP				0x2
#define FW_MB_PARAM_GET_PF_RDMA_BOTH				0x3

	/* Get MFW feature support response */
#define FW_MB_PARAM_FEATURE_SUPPORT_SMARTLINQ			BIT(0)
#define FW_MB_PARAM_FEATURE_SUPPORT_EEE				BIT(1)
#define FW_MB_PARAM_FEATURE_SUPPORT_FEC_CONTROL			BIT(5)
#define FW_MB_PARAM_FEATURE_SUPPORT_EXT_SPEED_FEC_CONTROL	BIT(6)
#define FW_MB_PARAM_FEATURE_SUPPORT_VLINK			BIT(16)

#define FW_MB_PARAM_LOAD_DONE_DID_EFUSE_ERROR			BIT(0)

#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_MASK		0x00000001
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_SHIFT		0
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_MASK		0x00000002
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_SHIFT		1
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_MASK			0x00000004
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_SHIFT		2
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_MASK			0x00000008
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_SHIFT		3

#define FW_MB_PARAM_PPFID_BITMAP_MASK				0xff
#define FW_MB_PARAM_PPFID_BITMAP_SHIFT				0

	u32							drv_pulse_mb;
#define DRV_PULSE_SEQ_MASK					0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK				0xffff0000
#define DRV_PULSE_ALWAYS_ALIVE					0x00008000

	u32							mcp_pulse_mb;
#define MCP_PULSE_SEQ_MASK					0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE					0x00008000
#define MCP_EVENT_MASK						0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ			0x00010000

	union drv_union_data					union_data;
};

#define FW_MB_PARAM_NVM_PUT_FILE_REQ_OFFSET_MASK		0x00ffffff
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_OFFSET_SHIFT		0
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_SIZE_MASK			0xff000000
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_SIZE_SHIFT			24

enum MFW_DRV_MSG_TYPE {
	MFW_DRV_MSG_LINK_CHANGE,
	MFW_DRV_MSG_FLR_FW_ACK_FAILED,
	MFW_DRV_MSG_VF_DISABLED,
	MFW_DRV_MSG_LLDP_DATA_UPDATED,
	MFW_DRV_MSG_DCBX_REMOTE_MIB_UPDATED,
	MFW_DRV_MSG_DCBX_OPERATIONAL_MIB_UPDATED,
	MFW_DRV_MSG_ERROR_RECOVERY,
	MFW_DRV_MSG_BW_UPDATE,
	MFW_DRV_MSG_S_TAG_UPDATE,
	MFW_DRV_MSG_GET_LAN_STATS,
	MFW_DRV_MSG_GET_FCOE_STATS,
	MFW_DRV_MSG_GET_ISCSI_STATS,
	MFW_DRV_MSG_GET_RDMA_STATS,
	MFW_DRV_MSG_FAILURE_DETECTED,
	MFW_DRV_MSG_TRANSCEIVER_STATE_CHANGE,
	MFW_DRV_MSG_CRITICAL_ERROR_OCCURRED,
	MFW_DRV_MSG_RESERVED,
	MFW_DRV_MSG_GET_TLV_REQ,
	MFW_DRV_MSG_OEM_CFG_UPDATE,
	MFW_DRV_MSG_MAX
};

#define MFW_DRV_MSG_MAX_DWORDS(msgs)	(((msgs - 1) >> 2) + 1)
#define MFW_DRV_MSG_DWORD(msg_id)	(msg_id >> 2)
#define MFW_DRV_MSG_OFFSET(msg_id)	((msg_id & 0x3) << 3)
#define MFW_DRV_MSG_MASK(msg_id)	(0xff << MFW_DRV_MSG_OFFSET(msg_id))

struct public_mfw_mb {
	u32 sup_msgs;
	u32 msg[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
	u32 ack[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
};

enum public_sections {
	PUBLIC_DRV_MB,
	PUBLIC_MFW_MB,
	PUBLIC_GLOBAL,
	PUBLIC_PATH,
	PUBLIC_PORT,
	PUBLIC_FUNC,
	PUBLIC_MAX_SECTIONS
};

struct mcp_public_data {
	u32 num_sections;
	u32 sections[PUBLIC_MAX_SECTIONS];
	struct public_drv_mb drv_mb[MCP_GLOB_FUNC_MAX];
	struct public_mfw_mb mfw_mb[MCP_GLOB_FUNC_MAX];
	struct public_global global;
	struct public_path path[MCP_GLOB_PATH_MAX];
	struct public_port port[MCP_GLOB_PORT_MAX];
	struct public_func func[MCP_GLOB_FUNC_MAX];
};

#define MAX_I2C_TRANSACTION_SIZE	16

/* OCBB definitions */
enum tlvs {
	/* Category 1: Device Properties */
	DRV_TLV_CLP_STR,
	DRV_TLV_CLP_STR_CTD,
	/* Category 6: Device Configuration */
	DRV_TLV_SCSI_TO,
	DRV_TLV_R_T_TOV,
	DRV_TLV_R_A_TOV,
	DRV_TLV_E_D_TOV,
	DRV_TLV_CR_TOV,
	DRV_TLV_BOOT_TYPE,
	/* Category 8: Port Configuration */
	DRV_TLV_NPIV_ENABLED,
	/* Category 10: Function Configuration */
	DRV_TLV_FEATURE_FLAGS,
	DRV_TLV_LOCAL_ADMIN_ADDR,
	DRV_TLV_ADDITIONAL_MAC_ADDR_1,
	DRV_TLV_ADDITIONAL_MAC_ADDR_2,
	DRV_TLV_LSO_MAX_OFFLOAD_SIZE,
	DRV_TLV_LSO_MIN_SEGMENT_COUNT,
	DRV_TLV_PROMISCUOUS_MODE,
	DRV_TLV_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_NUM_OF_NET_QUEUE_VMQ_CFG,
	DRV_TLV_FLEX_NIC_OUTER_VLAN_ID,
	DRV_TLV_OS_DRIVER_STATES,
	DRV_TLV_PXE_BOOT_PROGRESS,
	/* Category 12: FC/FCoE Configuration */
	DRV_TLV_NPIV_STATE,
	DRV_TLV_NUM_OF_NPIV_IDS,
	DRV_TLV_SWITCH_NAME,
	DRV_TLV_SWITCH_PORT_NUM,
	DRV_TLV_SWITCH_PORT_ID,
	DRV_TLV_VENDOR_NAME,
	DRV_TLV_SWITCH_MODEL,
	DRV_TLV_SWITCH_FW_VER,
	DRV_TLV_QOS_PRIORITY_PER_802_1P,
	DRV_TLV_PORT_ALIAS,
	DRV_TLV_PORT_STATE,
	DRV_TLV_FIP_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_LINK_FAILURE_COUNT,
	DRV_TLV_FCOE_BOOT_PROGRESS,
	/* Category 13: iSCSI Configuration */
	DRV_TLV_TARGET_LLMNR_ENABLED,
	DRV_TLV_HEADER_DIGEST_FLAG_ENABLED,
	DRV_TLV_DATA_DIGEST_FLAG_ENABLED,
	DRV_TLV_AUTHENTICATION_METHOD,
	DRV_TLV_ISCSI_BOOT_TARGET_PORTAL,
	DRV_TLV_MAX_FRAME_SIZE,
	DRV_TLV_PDU_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_ISCSI_BOOT_PROGRESS,
	/* Category 20: Device Data */
	DRV_TLV_PCIE_BUS_RX_UTILIZATION,
	DRV_TLV_PCIE_BUS_TX_UTILIZATION,
	DRV_TLV_DEVICE_CPU_CORES_UTILIZATION,
	DRV_TLV_LAST_VALID_DCC_TLV_RECEIVED,
	DRV_TLV_NCSI_RX_BYTES_RECEIVED,
	DRV_TLV_NCSI_TX_BYTES_SENT,
	/* Category 22: Base Port Data */
	DRV_TLV_RX_DISCARDS,
	DRV_TLV_RX_ERRORS,
	DRV_TLV_TX_ERRORS,
	DRV_TLV_TX_DISCARDS,
	DRV_TLV_RX_FRAMES_RECEIVED,
	DRV_TLV_TX_FRAMES_SENT,
	/* Category 23: FC/FCoE Port Data */
	DRV_TLV_RX_BROADCAST_PACKETS,
	DRV_TLV_TX_BROADCAST_PACKETS,
	/* Category 28: Base Function Data */
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV4,
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV6,
	DRV_TLV_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_PF_RX_FRAMES_RECEIVED,
	DRV_TLV_RX_BYTES_RECEIVED,
	DRV_TLV_PF_TX_FRAMES_SENT,
	DRV_TLV_TX_BYTES_SENT,
	DRV_TLV_IOV_OFFLOAD,
	DRV_TLV_PCI_ERRORS_CAP_ID,
	DRV_TLV_UNCORRECTABLE_ERROR_STATUS,
	DRV_TLV_UNCORRECTABLE_ERROR_MASK,
	DRV_TLV_CORRECTABLE_ERROR_STATUS,
	DRV_TLV_CORRECTABLE_ERROR_MASK,
	DRV_TLV_PCI_ERRORS_AECC_REGISTER,
	DRV_TLV_TX_QUEUES_EMPTY,
	DRV_TLV_RX_QUEUES_EMPTY,
	DRV_TLV_TX_QUEUES_FULL,
	DRV_TLV_RX_QUEUES_FULL,
	/* Category 29: FC/FCoE Function Data */
	DRV_TLV_FCOE_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_FRAMES_RECEIVED,
	DRV_TLV_FCOE_RX_BYTES_RECEIVED,
	DRV_TLV_FCOE_TX_FRAMES_SENT,
	DRV_TLV_FCOE_TX_BYTES_SENT,
	DRV_TLV_CRC_ERROR_COUNT,
	DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_1_TIMESTAMP,
	DRV_TLV_CRC_ERROR_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_2_TIMESTAMP,
	DRV_TLV_CRC_ERROR_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_3_TIMESTAMP,
	DRV_TLV_CRC_ERROR_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_4_TIMESTAMP,
	DRV_TLV_CRC_ERROR_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_5_TIMESTAMP,
	DRV_TLV_LOSS_OF_SYNC_ERROR_COUNT,
	DRV_TLV_LOSS_OF_SIGNAL_ERRORS,
	DRV_TLV_PRIMITIVE_SEQUENCE_PROTOCOL_ERROR_COUNT,
	DRV_TLV_DISPARITY_ERROR_COUNT,
	DRV_TLV_CODE_VIOLATION_ERROR_COUNT,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_ACC_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_RJT,
	DRV_TLV_LAST_FLOGI_RJT_TIMESTAMP,
	DRV_TLV_FDISCS_SENT_COUNT,
	DRV_TLV_FDISC_ACCS_RECEIVED,
	DRV_TLV_FDISC_RJTS_RECEIVED,
	DRV_TLV_PLOGI_SENT_COUNT,
	DRV_TLV_PLOGI_ACCS_RECEIVED,
	DRV_TLV_PLOGI_RJTS_RECEIVED,
	DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_1_TIMESTAMP,
	DRV_TLV_PLOGI_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_2_TIMESTAMP,
	DRV_TLV_PLOGI_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_3_TIMESTAMP,
	DRV_TLV_PLOGI_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_4_TIMESTAMP,
	DRV_TLV_PLOGI_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_5_TIMESTAMP,
	DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_1_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_2_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_2_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_3_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_3_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_4_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_4_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_5_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_5_ACC_TIMESTAMP,
	DRV_TLV_LOGOS_ISSUED,
	DRV_TLV_LOGO_ACCS_RECEIVED,
	DRV_TLV_LOGO_RJTS_RECEIVED,
	DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_1_TIMESTAMP,
	DRV_TLV_LOGO_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_2_TIMESTAMP,
	DRV_TLV_LOGO_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_3_TIMESTAMP,
	DRV_TLV_LOGO_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_4_TIMESTAMP,
	DRV_TLV_LOGO_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_5_TIMESTAMP,
	DRV_TLV_LOGOS_RECEIVED,
	DRV_TLV_ACCS_ISSUED,
	DRV_TLV_PRLIS_ISSUED,
	DRV_TLV_ACCS_RECEIVED,
	DRV_TLV_ABTS_SENT_COUNT,
	DRV_TLV_ABTS_ACCS_RECEIVED,
	DRV_TLV_ABTS_RJTS_RECEIVED,
	DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_1_TIMESTAMP,
	DRV_TLV_ABTS_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_2_TIMESTAMP,
	DRV_TLV_ABTS_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_3_TIMESTAMP,
	DRV_TLV_ABTS_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_4_TIMESTAMP,
	DRV_TLV_ABTS_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_5_TIMESTAMP,
	DRV_TLV_RSCNS_RECEIVED,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_2,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_3,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_4,
	DRV_TLV_LUN_RESETS_ISSUED,
	DRV_TLV_ABORT_TASK_SETS_ISSUED,
	DRV_TLV_TPRLOS_SENT,
	DRV_TLV_NOS_SENT_COUNT,
	DRV_TLV_NOS_RECEIVED_COUNT,
	DRV_TLV_OLS_COUNT,
	DRV_TLV_LR_COUNT,
	DRV_TLV_LRR_COUNT,
	DRV_TLV_LIP_SENT_COUNT,
	DRV_TLV_LIP_RECEIVED_COUNT,
	DRV_TLV_EOFA_COUNT,
	DRV_TLV_EOFNI_COUNT,
	DRV_TLV_SCSI_STATUS_CHECK_CONDITION_COUNT,
	DRV_TLV_SCSI_STATUS_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_BUSY_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_RESERVATION_CONFLICT_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_SET_FULL_COUNT,
	DRV_TLV_SCSI_STATUS_ACA_ACTIVE_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_ABORTED_COUNT,
	DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_1_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_2_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_2_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_3_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_3_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_4_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_4_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_5_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_5_TIMESTAMP,
	/* Category 30: iSCSI Function Data */
	DRV_TLV_PDU_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_ISCSI_PDU_RX_FRAMES_RECEIVED,
	DRV_TLV_ISCSI_PDU_RX_BYTES_RECEIVED,
	DRV_TLV_ISCSI_PDU_TX_FRAMES_SENT,
	DRV_TLV_ISCSI_PDU_TX_BYTES_SENT
};

struct nvm_cfg_mac_address {
	u32							mac_addr_hi;
#define NVM_CFG_MAC_ADDRESS_HI_MASK				0x0000ffff
#define NVM_CFG_MAC_ADDRESS_HI_OFFSET				0

	u32							mac_addr_lo;
};

struct nvm_cfg1_glob {
	u32							generic_cont0;
#define NVM_CFG1_GLOB_MF_MODE_MASK				0x00000ff0
#define NVM_CFG1_GLOB_MF_MODE_OFFSET				4
#define NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED			0x0
#define NVM_CFG1_GLOB_MF_MODE_DEFAULT				0x1
#define NVM_CFG1_GLOB_MF_MODE_SPIO4				0x2
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_0				0x3
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_5				0x4
#define NVM_CFG1_GLOB_MF_MODE_NPAR2_0				0x5
#define NVM_CFG1_GLOB_MF_MODE_BD				0x6
#define NVM_CFG1_GLOB_MF_MODE_UFP				0x7

	u32							engineering_change[3];
	u32							manufacturing_id;
	u32							serial_number[4];
	u32							pcie_cfg;
	u32							mgmt_traffic;

	u32							core_cfg;
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK			0x000000ff
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET			0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G		0x0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G			0x1
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G		0x2
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F			0x3
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E		0x4
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G		0x5
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G			0xb
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G			0xc
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G			0xd
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X25G			0xe
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X10G			0xf
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_2X50G_R1		0x11
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_4X50G_R1		0x12
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_1X100G_R2		0x13
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_2X100G_R2		0x14
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_1X100G_R4		0x15

	u32							e_lane_cfg1;
	u32							e_lane_cfg2;
	u32							f_lane_cfg1;
	u32							f_lane_cfg2;
	u32							mps10_preemphasis;
	u32							mps10_driver_current;
	u32							mps25_preemphasis;
	u32							mps25_driver_current;
	u32							pci_id;
	u32							pci_subsys_id;
	u32							bar;
	u32							mps10_txfir_main;
	u32							mps10_txfir_post;
	u32							mps25_txfir_main;
	u32							mps25_txfir_post;
	u32							manufacture_ver;
	u32							manufacture_time;
	u32							led_global_settings;
	u32							generic_cont1;

	u32							mbi_version;
#define NVM_CFG1_GLOB_MBI_VERSION_0_MASK			0x000000ff
#define NVM_CFG1_GLOB_MBI_VERSION_0_OFFSET			0
#define NVM_CFG1_GLOB_MBI_VERSION_1_MASK			0x0000ff00
#define NVM_CFG1_GLOB_MBI_VERSION_1_OFFSET			8
#define NVM_CFG1_GLOB_MBI_VERSION_2_MASK			0x00ff0000
#define NVM_CFG1_GLOB_MBI_VERSION_2_OFFSET			16

	u32							mbi_date;
	u32							misc_sig;

	u32							device_capabilities;
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET		0x1
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_FCOE			0x2
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI			0x4
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE			0x8

	u32							power_dissipated;
	u32							power_consumed;
	u32							efi_version;
	u32							multi_net_modes_cap;
	u32							reserved[41];
};

struct nvm_cfg1_path {
	u32							reserved[30];
};

struct nvm_cfg1_port {
	u32							rel_to_opt123;
	u32							rel_to_opt124;

	u32							generic_cont0;
#define NVM_CFG1_PORT_DCBX_MODE_MASK				0x000f0000
#define NVM_CFG1_PORT_DCBX_MODE_OFFSET				16
#define NVM_CFG1_PORT_DCBX_MODE_DISABLED			0x0
#define NVM_CFG1_PORT_DCBX_MODE_IEEE				0x1
#define NVM_CFG1_PORT_DCBX_MODE_CEE				0x2
#define NVM_CFG1_PORT_DCBX_MODE_DYNAMIC				0x3
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_MASK		0x00f00000
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_OFFSET		20
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ETHERNET	0x1
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_FCOE		0x2
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ISCSI		0x4

	u32							pcie_cfg;
	u32							features;

	u32							speed_cap_mask;
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK		0x0000ffff
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_OFFSET		0
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G		0x1
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G		0x2
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G		0x4
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G		0x8
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G		0x10
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G		0x20
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G		0x40

	u32							link_settings;
#define NVM_CFG1_PORT_DRV_LINK_SPEED_MASK			0x0000000f
#define NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET			0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG			0x0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_1G				0x1
#define NVM_CFG1_PORT_DRV_LINK_SPEED_10G			0x2
#define NVM_CFG1_PORT_DRV_LINK_SPEED_20G			0x3
#define NVM_CFG1_PORT_DRV_LINK_SPEED_25G			0x4
#define NVM_CFG1_PORT_DRV_LINK_SPEED_40G			0x5
#define NVM_CFG1_PORT_DRV_LINK_SPEED_50G			0x6
#define NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G			0x7
#define NVM_CFG1_PORT_DRV_LINK_SPEED_SMARTLINQ			0x8
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK			0x00000070
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET			4
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG			0x1
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX			0x2
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX			0x4
#define NVM_CFG1_PORT_FEC_FORCE_MODE_MASK			0x000e0000
#define NVM_CFG1_PORT_FEC_FORCE_MODE_OFFSET			17
#define NVM_CFG1_PORT_FEC_FORCE_MODE_NONE			0x0
#define NVM_CFG1_PORT_FEC_FORCE_MODE_FIRECODE			0x1
#define NVM_CFG1_PORT_FEC_FORCE_MODE_RS				0x2
#define NVM_CFG1_PORT_FEC_FORCE_MODE_AUTO			0x7

	u32							phy_cfg;
	u32							mgmt_traffic;

	u32							ext_phy;
	/* EEE power saving mode */
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_MASK		0x00ff0000
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_OFFSET		16
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_DISABLED		0x0
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_BALANCED		0x1
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_AGGRESSIVE		0x2
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_LOW_LATENCY		0x3

	u32							mba_cfg1;
	u32							mba_cfg2;
	u32							vf_cfg;
	struct nvm_cfg_mac_address				lldp_mac_address;
	u32							led_port_settings;
	u32							transceiver_00;
	u32							device_ids;

	u32							board_cfg;
#define NVM_CFG1_PORT_PORT_TYPE_MASK				0x000000ff
#define NVM_CFG1_PORT_PORT_TYPE_OFFSET				0
#define NVM_CFG1_PORT_PORT_TYPE_UNDEFINED			0x0
#define NVM_CFG1_PORT_PORT_TYPE_MODULE				0x1
#define NVM_CFG1_PORT_PORT_TYPE_BACKPLANE			0x2
#define NVM_CFG1_PORT_PORT_TYPE_EXT_PHY				0x3
#define NVM_CFG1_PORT_PORT_TYPE_MODULE_SLAVE			0x4

	u32							mnm_10g_cap;
	u32							mnm_10g_ctrl;
	u32							mnm_10g_misc;
	u32							mnm_25g_cap;
	u32							mnm_25g_ctrl;
	u32							mnm_25g_misc;
	u32							mnm_40g_cap;
	u32							mnm_40g_ctrl;
	u32							mnm_40g_misc;
	u32							mnm_50g_cap;
	u32							mnm_50g_ctrl;
	u32							mnm_50g_misc;
	u32							mnm_100g_cap;
	u32							mnm_100g_ctrl;
	u32							mnm_100g_misc;

	u32							temperature;
	u32							ext_phy_cfg1;

	u32							extended_speed;
#define NVM_CFG1_PORT_EXTENDED_SPEED_MASK			0x0000ffff
#define NVM_CFG1_PORT_EXTENDED_SPEED_OFFSET			0
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_AN		0x1
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_1G		0x2
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_10G		0x4
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_20G		0x8
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_25G		0x10
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_40G		0x20
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_50G_R		0x40
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_50G_R2		0x80
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_R2		0x100
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_R4		0x200
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_P4		0x400
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_MASK			0xffff0000
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_OFFSET			16
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_RESERVED	0x1
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_1G		0x2
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_10G		0x4
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_20G		0x8
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_25G		0x10
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_40G		0x20
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_50G_R	0x40
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_50G_R2	0x80
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_R2	0x100
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_R4	0x200
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_P4	0x400

	u32							extended_fec_mode;

	u32							reserved[112];
};

struct nvm_cfg1_func {
	struct nvm_cfg_mac_address mac_address;
	u32 rsrv1;
	u32 rsrv2;
	u32 device_id;
	u32 cmn_cfg;
	u32 pci_cfg;
	struct nvm_cfg_mac_address fcoe_node_wwn_mac_addr;
	struct nvm_cfg_mac_address fcoe_port_wwn_mac_addr;
	u32 preboot_generic_cfg;
	u32 reserved[8];
};

struct nvm_cfg1 {
	struct nvm_cfg1_glob glob;
	struct nvm_cfg1_path path[MCP_GLOB_PATH_MAX];
	struct nvm_cfg1_port port[MCP_GLOB_PORT_MAX];
	struct nvm_cfg1_func func[MCP_GLOB_FUNC_MAX];
};

enum spad_sections {
	SPAD_SECTION_TRACE,
	SPAD_SECTION_NVM_CFG,
	SPAD_SECTION_PUBLIC,
	SPAD_SECTION_PRIVATE,
	SPAD_SECTION_MAX
};

#define MCP_TRACE_SIZE          2048	/* 2kb */

/* This section is located at a fixed location in the beginning of the
 * scratchpad, to ensure that the MCP trace is not run over during MFW upgrade.
 * All the rest of data has a floating location which differs from version to
 * version, and is pointed by the mcp_meta_data below.
 * Moreover, the spad_layout section is part of the MFW firmware, and is loaded
 * with it from nvram in order to clear this portion.
 */
struct static_init {
	u32 num_sections;
	offsize_t sections[SPAD_SECTION_MAX];
#define SECTION(_sec_) (*((offsize_t *)(STRUCT_OFFSET(sections[_sec_]))))

	struct mcp_trace trace;
#define MCP_TRACE_P ((struct mcp_trace *)(STRUCT_OFFSET(trace)))
	u8 trace_buffer[MCP_TRACE_SIZE];
#define MCP_TRACE_BUF ((u8 *)(STRUCT_OFFSET(trace_buffer)))
	/* running_mfw has the same definition as in nvm_map.h.
	 * This bit indicate both the running dir, and the running bundle.
	 * It is set once when the LIM is loaded.
	 */
	u32 running_mfw;
#define RUNNING_MFW (*((u32 *)(STRUCT_OFFSET(running_mfw))))
	u32 build_time;
#define MFW_BUILD_TIME (*((u32 *)(STRUCT_OFFSET(build_time))))
	u32 reset_type;
#define RESET_TYPE (*((u32 *)(STRUCT_OFFSET(reset_type))))
	u32 mfw_secure_mode;
#define MFW_SECURE_MODE (*((u32 *)(STRUCT_OFFSET(mfw_secure_mode))))
	u16 pme_status_pf_bitmap;
#define PME_STATUS_PF_BITMAP (*((u16 *)(STRUCT_OFFSET(pme_status_pf_bitmap))))
	u16 pme_enable_pf_bitmap;
#define PME_ENABLE_PF_BITMAP (*((u16 *)(STRUCT_OFFSET(pme_enable_pf_bitmap))))
	u32 mim_nvm_addr;
	u32 mim_start_addr;
	u32 ah_pcie_link_params;
#define AH_PCIE_LINK_PARAMS_LINK_SPEED_MASK     (0x000000ff)
#define AH_PCIE_LINK_PARAMS_LINK_SPEED_SHIFT    (0)
#define AH_PCIE_LINK_PARAMS_LINK_WIDTH_MASK     (0x0000ff00)
#define AH_PCIE_LINK_PARAMS_LINK_WIDTH_SHIFT    (8)
#define AH_PCIE_LINK_PARAMS_ASPM_MODE_MASK      (0x00ff0000)
#define AH_PCIE_LINK_PARAMS_ASPM_MODE_SHIFT     (16)
#define AH_PCIE_LINK_PARAMS_ASPM_CAP_MASK       (0xff000000)
#define AH_PCIE_LINK_PARAMS_ASPM_CAP_SHIFT      (24)
#define AH_PCIE_LINK_PARAMS (*((u32 *)(STRUCT_OFFSET(ah_pcie_link_params))))

	u32 rsrv_persist[5];	/* Persist reserved for MFW upgrades */
};

#define NVM_MAGIC_VALUE		0x669955aa

enum nvm_image_type {
	NVM_TYPE_TIM1 = 0x01,
	NVM_TYPE_TIM2 = 0x02,
	NVM_TYPE_MIM1 = 0x03,
	NVM_TYPE_MIM2 = 0x04,
	NVM_TYPE_MBA = 0x05,
	NVM_TYPE_MODULES_PN = 0x06,
	NVM_TYPE_VPD = 0x07,
	NVM_TYPE_MFW_TRACE1 = 0x08,
	NVM_TYPE_MFW_TRACE2 = 0x09,
	NVM_TYPE_NVM_CFG1 = 0x0a,
	NVM_TYPE_L2B = 0x0b,
	NVM_TYPE_DIR1 = 0x0c,
	NVM_TYPE_EAGLE_FW1 = 0x0d,
	NVM_TYPE_FALCON_FW1 = 0x0e,
	NVM_TYPE_PCIE_FW1 = 0x0f,
	NVM_TYPE_HW_SET = 0x10,
	NVM_TYPE_LIM = 0x11,
	NVM_TYPE_AVS_FW1 = 0x12,
	NVM_TYPE_DIR2 = 0x13,
	NVM_TYPE_CCM = 0x14,
	NVM_TYPE_EAGLE_FW2 = 0x15,
	NVM_TYPE_FALCON_FW2 = 0x16,
	NVM_TYPE_PCIE_FW2 = 0x17,
	NVM_TYPE_AVS_FW2 = 0x18,
	NVM_TYPE_INIT_HW = 0x19,
	NVM_TYPE_DEFAULT_CFG = 0x1a,
	NVM_TYPE_MDUMP = 0x1b,
	NVM_TYPE_META = 0x1c,
	NVM_TYPE_ISCSI_CFG = 0x1d,
	NVM_TYPE_FCOE_CFG = 0x1f,
	NVM_TYPE_ETH_PHY_FW1 = 0x20,
	NVM_TYPE_ETH_PHY_FW2 = 0x21,
	NVM_TYPE_BDN = 0x22,
	NVM_TYPE_8485X_PHY_FW = 0x23,
	NVM_TYPE_PUB_KEY = 0x24,
	NVM_TYPE_RECOVERY = 0x25,
	NVM_TYPE_PLDM = 0x26,
	NVM_TYPE_UPK1 = 0x27,
	NVM_TYPE_UPK2 = 0x28,
	NVM_TYPE_MASTER_KC = 0x29,
	NVM_TYPE_BACKUP_KC = 0x2a,
	NVM_TYPE_HW_DUMP = 0x2b,
	NVM_TYPE_HW_DUMP_OUT = 0x2c,
	NVM_TYPE_BIN_NVM_META = 0x30,
	NVM_TYPE_ROM_TEST = 0xf0,
	NVM_TYPE_88X33X0_PHY_FW = 0x31,
	NVM_TYPE_88X33X0_PHY_SLAVE_FW = 0x32,
	NVM_TYPE_MAX,
};

#define DIR_ID_1    (0)

#endif
