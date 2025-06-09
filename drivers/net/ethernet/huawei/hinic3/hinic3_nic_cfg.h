/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_NIC_CFG_H_
#define _HINIC3_NIC_CFG_H_

#include <linux/types.h>

#include "hinic3_hw_intf.h"
#include "hinic3_mgmt_interface.h"

struct hinic3_hwdev;
struct hinic3_nic_dev;

#define HINIC3_MIN_MTU_SIZE          256
#define HINIC3_MAX_JUMBO_FRAME_SIZE  9600

#define HINIC3_VLAN_ID_MASK          0x7FFF

enum hinic3_nic_event_type {
	HINIC3_NIC_EVENT_LINK_DOWN = 0,
	HINIC3_NIC_EVENT_LINK_UP   = 1,
};

int hinic3_set_nic_feature_to_hw(struct hinic3_nic_dev *nic_dev);
bool hinic3_test_support(struct hinic3_nic_dev *nic_dev,
			 enum hinic3_nic_feature_cap feature_bits);
void hinic3_update_nic_feature(struct hinic3_nic_dev *nic_dev, u64 feature_cap);

int hinic3_set_port_mtu(struct net_device *netdev, u16 new_mtu);

int hinic3_set_mac(struct hinic3_hwdev *hwdev, const u8 *mac_addr, u16 vlan_id,
		   u16 func_id);
int hinic3_del_mac(struct hinic3_hwdev *hwdev, const u8 *mac_addr, u16 vlan_id,
		   u16 func_id);
int hinic3_update_mac(struct hinic3_hwdev *hwdev, const u8 *old_mac,
		      u8 *new_mac, u16 vlan_id, u16 func_id);

int hinic3_force_drop_tx_pkt(struct hinic3_hwdev *hwdev);

#endif
