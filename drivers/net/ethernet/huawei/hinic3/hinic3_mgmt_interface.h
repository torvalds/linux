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

struct l2nic_cmd_force_pkt_drop {
	struct mgmt_msg_head msg_head;
	u8                   port;
	u8                   rsvd1[3];
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
