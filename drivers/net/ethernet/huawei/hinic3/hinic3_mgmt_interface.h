/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_MGMT_INTERFACE_H_
#define _HINIC3_MGMT_INTERFACE_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/if_ether.h>

#include "hinic3_hw_intf.h"

struct l2nic_cmd_feature_nego {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u8                   opcode;
	u8                   rsvd;
	u64                  s_feature[4];
};

enum l2nic_func_tbl_cfg_bitmap {
	L2NIC_FUNC_TBL_CFG_INIT        = 0,
	L2NIC_FUNC_TBL_CFG_RX_BUF_SIZE = 1,
	L2NIC_FUNC_TBL_CFG_MTU         = 2,
};

struct l2nic_func_tbl_cfg {
	u16 rx_wqe_buf_size;
	u16 mtu;
	u32 rsvd[9];
};

struct l2nic_cmd_set_func_tbl {
	struct mgmt_msg_head      msg_head;
	u16                       func_id;
	u16                       rsvd;
	u32                       cfg_bitmap;
	struct l2nic_func_tbl_cfg tbl_cfg;
};

struct l2nic_cmd_set_mac {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u16                  vlan_id;
	u16                  rsvd1;
	u8                   mac[ETH_ALEN];
};

struct l2nic_cmd_update_mac {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u16                  vlan_id;
	u16                  rsvd1;
	u8                   old_mac[ETH_ALEN];
	u16                  rsvd2;
	u8                   new_mac[ETH_ALEN];
};

struct l2nic_cmd_set_ci_attr {
	struct mgmt_msg_head msg_head;
	u16                  func_idx;
	u8                   dma_attr_off;
	u8                   pending_limit;
	u8                   coalescing_time;
	u8                   intr_en;
	u16                  intr_idx;
	u32                  l2nic_sqn;
	u32                  rsvd;
	u64                  ci_addr;
};

struct l2nic_cmd_clear_qp_resource {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u16                  rsvd1;
};

struct l2nic_cmd_force_pkt_drop {
	struct mgmt_msg_head msg_head;
	u8                   port;
	u8                   rsvd1[3];
};

struct l2nic_cmd_set_vport_state {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u16                  rsvd1;
	/* 0--disable, 1--enable */
	u8                   state;
	u8                   rsvd2[3];
};

struct l2nic_cmd_set_dcb_state {
	struct mgmt_msg_head head;
	u16                  func_id;
	/* 0 - get dcb state, 1 - set dcb state */
	u8                   op_code;
	/* 0 - disable, 1 - enable dcb */
	u8                   state;
	/* 0 - disable, 1 - enable dcb */
	u8                   port_state;
	u8                   rsvd[7];
};

#define L2NIC_RSS_TYPE_VALID_MASK         BIT(23)
#define L2NIC_RSS_TYPE_TCP_IPV6_EXT_MASK  BIT(24)
#define L2NIC_RSS_TYPE_IPV6_EXT_MASK      BIT(25)
#define L2NIC_RSS_TYPE_TCP_IPV6_MASK      BIT(26)
#define L2NIC_RSS_TYPE_IPV6_MASK          BIT(27)
#define L2NIC_RSS_TYPE_TCP_IPV4_MASK      BIT(28)
#define L2NIC_RSS_TYPE_IPV4_MASK          BIT(29)
#define L2NIC_RSS_TYPE_UDP_IPV6_MASK      BIT(30)
#define L2NIC_RSS_TYPE_UDP_IPV4_MASK      BIT(31)
#define L2NIC_RSS_TYPE_SET(val, member)  \
	FIELD_PREP(L2NIC_RSS_TYPE_##member##_MASK, val)
#define L2NIC_RSS_TYPE_GET(val, member)  \
	FIELD_GET(L2NIC_RSS_TYPE_##member##_MASK, val)

#define L2NIC_RSS_INDIR_SIZE  256
#define L2NIC_RSS_KEY_SIZE    40

/* IEEE 802.1Qaz std */
#define L2NIC_DCB_COS_MAX     0x8

struct l2nic_cmd_set_rss_ctx_tbl {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u16                  rsvd1;
	u32                  context;
};

struct l2nic_cmd_cfg_rss_engine {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u8                   opcode;
	u8                   hash_engine;
	u8                   rsvd1[4];
};

struct l2nic_cmd_cfg_rss_hash_key {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u8                   opcode;
	u8                   rsvd1;
	u8                   key[L2NIC_RSS_KEY_SIZE];
};

struct l2nic_cmd_cfg_rss {
	struct mgmt_msg_head msg_head;
	u16                  func_id;
	u8                   rss_en;
	u8                   rq_priority_number;
	u8                   prio_tc[L2NIC_DCB_COS_MAX];
	u16                  num_qps;
	u16                  rsvd1;
};

/* Commands between NIC to fw */
enum l2nic_cmd {
	/* FUNC CFG */
	L2NIC_CMD_SET_FUNC_TBL        = 5,
	L2NIC_CMD_SET_VPORT_ENABLE    = 6,
	L2NIC_CMD_SET_SQ_CI_ATTR      = 8,
	L2NIC_CMD_CLEAR_QP_RESOURCE   = 11,
	L2NIC_CMD_FEATURE_NEGO        = 15,
	L2NIC_CMD_SET_MAC             = 21,
	L2NIC_CMD_DEL_MAC             = 22,
	L2NIC_CMD_UPDATE_MAC          = 23,
	L2NIC_CMD_CFG_RSS             = 60,
	L2NIC_CMD_CFG_RSS_HASH_KEY    = 63,
	L2NIC_CMD_CFG_RSS_HASH_ENGINE = 64,
	L2NIC_CMD_SET_RSS_CTX_TBL     = 65,
	L2NIC_CMD_QOS_DCB_STATE       = 110,
	L2NIC_CMD_FORCE_PKT_DROP      = 113,
	L2NIC_CMD_MAX                 = 256,
};

struct l2nic_cmd_rss_set_indir_tbl {
	__le32 rsvd[4];
	__le16 entry[L2NIC_RSS_INDIR_SIZE];
};

/* NIC CMDQ MODE */
enum l2nic_ucode_cmd {
	L2NIC_UCODE_CMD_MODIFY_QUEUE_CTX  = 0,
	L2NIC_UCODE_CMD_CLEAN_QUEUE_CTX   = 1,
	L2NIC_UCODE_CMD_SET_RSS_INDIR_TBL = 4,
};

/* hilink mac group command */
enum mag_cmd {
	MAG_CMD_GET_LINK_STATUS = 7,
};

/* firmware also use this cmd report link event to driver */
struct mag_cmd_get_link_status {
	struct mgmt_msg_head head;
	u8                   port_id;
	/* 0:link down  1:link up */
	u8                   status;
	u8                   rsvd0[2];
};

enum hinic3_nic_feature_cap {
	HINIC3_NIC_F_CSUM           = BIT(0),
	HINIC3_NIC_F_SCTP_CRC       = BIT(1),
	HINIC3_NIC_F_TSO            = BIT(2),
	HINIC3_NIC_F_LRO            = BIT(3),
	HINIC3_NIC_F_UFO            = BIT(4),
	HINIC3_NIC_F_RSS            = BIT(5),
	HINIC3_NIC_F_RX_VLAN_FILTER = BIT(6),
	HINIC3_NIC_F_RX_VLAN_STRIP  = BIT(7),
	HINIC3_NIC_F_TX_VLAN_INSERT = BIT(8),
	HINIC3_NIC_F_VXLAN_OFFLOAD  = BIT(9),
	HINIC3_NIC_F_FDIR           = BIT(11),
	HINIC3_NIC_F_PROMISC        = BIT(12),
	HINIC3_NIC_F_ALLMULTI       = BIT(13),
	HINIC3_NIC_F_RATE_LIMIT     = BIT(16),
};

#define HINIC3_NIC_F_ALL_MASK           0x33bff
#define HINIC3_NIC_DRV_DEFAULT_FEATURE  0x3f03f

#endif
