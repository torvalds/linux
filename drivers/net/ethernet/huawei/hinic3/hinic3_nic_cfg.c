// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/if_vlan.h>

#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"

static int hinic3_feature_nego(struct hinic3_hwdev *hwdev, u8 opcode,
			       u64 *s_feature, u16 size)
{
	struct l2nic_cmd_feature_nego feature_nego = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	feature_nego.func_id = hinic3_global_func_id(hwdev);
	feature_nego.opcode = opcode;
	if (opcode == MGMT_MSG_CMD_OP_SET)
		memcpy(feature_nego.s_feature, s_feature, size * sizeof(u64));

	mgmt_msg_params_init_default(&msg_params, &feature_nego,
				     sizeof(feature_nego));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_FEATURE_NEGO, &msg_params);
	if (err || feature_nego.msg_head.status) {
		dev_err(hwdev->dev, "Failed to negotiate nic feature, err:%d, status: 0x%x\n",
			err, feature_nego.msg_head.status);
		return -EIO;
	}

	if (opcode == MGMT_MSG_CMD_OP_GET)
		memcpy(s_feature, feature_nego.s_feature, size * sizeof(u64));

	return 0;
}

int hinic3_set_nic_feature_to_hw(struct hinic3_nic_dev *nic_dev)
{
	return hinic3_feature_nego(nic_dev->hwdev, MGMT_MSG_CMD_OP_SET,
				   &nic_dev->nic_io->feature_cap, 1);
}

bool hinic3_test_support(struct hinic3_nic_dev *nic_dev,
			 enum hinic3_nic_feature_cap feature_bits)
{
	return (nic_dev->nic_io->feature_cap & feature_bits) == feature_bits;
}

void hinic3_update_nic_feature(struct hinic3_nic_dev *nic_dev, u64 feature_cap)
{
	nic_dev->nic_io->feature_cap = feature_cap;
}

static int hinic3_set_function_table(struct hinic3_hwdev *hwdev, u32 cfg_bitmap,
				     const struct l2nic_func_tbl_cfg *cfg)
{
	struct l2nic_cmd_set_func_tbl cmd_func_tbl = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	cmd_func_tbl.func_id = hinic3_global_func_id(hwdev);
	cmd_func_tbl.cfg_bitmap = cfg_bitmap;
	cmd_func_tbl.tbl_cfg = *cfg;

	mgmt_msg_params_init_default(&msg_params, &cmd_func_tbl,
				     sizeof(cmd_func_tbl));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_FUNC_TBL, &msg_params);
	if (err || cmd_func_tbl.msg_head.status) {
		dev_err(hwdev->dev,
			"Failed to set func table, bitmap: 0x%x, err: %d, status: 0x%x\n",
			cfg_bitmap, err, cmd_func_tbl.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_set_port_mtu(struct net_device *netdev, u16 new_mtu)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct l2nic_func_tbl_cfg func_tbl_cfg = {};
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;

	func_tbl_cfg.mtu = new_mtu;
	return hinic3_set_function_table(hwdev, BIT(L2NIC_FUNC_TBL_CFG_MTU),
					 &func_tbl_cfg);
}

static int hinic3_check_mac_info(struct hinic3_hwdev *hwdev, u8 status,
				 u16 vlan_id)
{
	if ((status && status != MGMT_STATUS_EXIST) ||
	    ((vlan_id & BIT(15)) && status == MGMT_STATUS_EXIST)) {
		return -EINVAL;
	}

	return 0;
}

int hinic3_set_mac(struct hinic3_hwdev *hwdev, const u8 *mac_addr, u16 vlan_id,
		   u16 func_id)
{
	struct l2nic_cmd_set_mac mac_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	if ((vlan_id & HINIC3_VLAN_ID_MASK) >= VLAN_N_VID) {
		dev_err(hwdev->dev, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC3_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	ether_addr_copy(mac_info.mac, mac_addr);

	mgmt_msg_params_init_default(&msg_params, &mac_info, sizeof(mac_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_MAC, &msg_params);
	if (err || hinic3_check_mac_info(hwdev, mac_info.msg_head.status,
					 mac_info.vlan_id)) {
		dev_err(hwdev->dev,
			"Failed to update MAC, err: %d, status: 0x%x\n",
			err, mac_info.msg_head.status);
		return -EIO;
	}

	if (mac_info.msg_head.status == MGMT_STATUS_PF_SET_VF_ALREADY) {
		dev_warn(hwdev->dev, "PF has already set VF mac, Ignore set operation\n");
		return 0;
	}

	if (mac_info.msg_head.status == MGMT_STATUS_EXIST) {
		dev_warn(hwdev->dev, "MAC is repeated. Ignore update operation\n");
		return 0;
	}

	return 0;
}

int hinic3_del_mac(struct hinic3_hwdev *hwdev, const u8 *mac_addr, u16 vlan_id,
		   u16 func_id)
{
	struct l2nic_cmd_set_mac mac_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	if ((vlan_id & HINIC3_VLAN_ID_MASK) >= VLAN_N_VID) {
		dev_err(hwdev->dev, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC3_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	ether_addr_copy(mac_info.mac, mac_addr);

	mgmt_msg_params_init_default(&msg_params, &mac_info, sizeof(mac_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_DEL_MAC, &msg_params);
	if (err) {
		dev_err(hwdev->dev,
			"Failed to delete MAC, err: %d, status: 0x%x\n",
			err, mac_info.msg_head.status);
		return err;
	}

	return 0;
}

int hinic3_update_mac(struct hinic3_hwdev *hwdev, const u8 *old_mac,
		      u8 *new_mac, u16 vlan_id, u16 func_id)
{
	struct l2nic_cmd_update_mac mac_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	if ((vlan_id & HINIC3_VLAN_ID_MASK) >= VLAN_N_VID) {
		dev_err(hwdev->dev, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC3_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	ether_addr_copy(mac_info.old_mac, old_mac);
	ether_addr_copy(mac_info.new_mac, new_mac);

	mgmt_msg_params_init_default(&msg_params, &mac_info, sizeof(mac_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_UPDATE_MAC, &msg_params);
	if (err || hinic3_check_mac_info(hwdev, mac_info.msg_head.status,
					 mac_info.vlan_id)) {
		dev_err(hwdev->dev,
			"Failed to update MAC, err: %d, status: 0x%x\n",
			err, mac_info.msg_head.status);
		return -EIO;
	}
	return 0;
}

int hinic3_force_drop_tx_pkt(struct hinic3_hwdev *hwdev)
{
	struct l2nic_cmd_force_pkt_drop pkt_drop = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	pkt_drop.port = hinic3_physical_port_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &pkt_drop, sizeof(pkt_drop));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_FORCE_PKT_DROP, &msg_params);
	if ((pkt_drop.msg_head.status != MGMT_STATUS_CMD_UNSUPPORTED &&
	     pkt_drop.msg_head.status) || err) {
		dev_err(hwdev->dev,
			"Failed to set force tx packets drop, err: %d, status: 0x%x\n",
			err, pkt_drop.msg_head.status);
		return -EFAULT;
	}

	return pkt_drop.msg_head.status;
}
