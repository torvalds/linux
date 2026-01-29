// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/if_vlan.h>

#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"

#define MGMT_MSG_CMD_OP_ADD  1
#define MGMT_MSG_CMD_OP_DEL  0

static int hinic3_feature_nego(struct hinic3_hwdev *hwdev, u8 opcode,
			       u64 *s_feature, u16 size)
{
	struct l2nic_cmd_feature_nego feature_nego = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	feature_nego.func_id = hinic3_global_func_id(hwdev);
	feature_nego.opcode = opcode;
	if (opcode == MGMT_MSG_CMD_OP_SET)
		memcpy(feature_nego.s_feature, s_feature,
		       array_size(size, sizeof(u64)));

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
		memcpy(s_feature, feature_nego.s_feature,
		       array_size(size, sizeof(u64)));

	return 0;
}

int hinic3_get_nic_feature_from_hw(struct hinic3_nic_dev *nic_dev)
{
	return hinic3_feature_nego(nic_dev->hwdev, MGMT_MSG_CMD_OP_GET,
				   &nic_dev->nic_io->feature_cap, 1);
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

static int hinic3_set_rx_lro(struct hinic3_hwdev *hwdev, u8 ipv4_en, u8 ipv6_en,
			     u8 lro_max_pkt_len)
{
	struct l2nic_cmd_lro_config lro_cfg = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	lro_cfg.func_id = hinic3_global_func_id(hwdev);
	lro_cfg.opcode = MGMT_MSG_CMD_OP_SET;
	lro_cfg.lro_ipv4_en = ipv4_en;
	lro_cfg.lro_ipv6_en = ipv6_en;
	lro_cfg.lro_max_pkt_len = lro_max_pkt_len;

	mgmt_msg_params_init_default(&msg_params, &lro_cfg,
				     sizeof(lro_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_RX_LRO,
				       &msg_params);

	if (err || lro_cfg.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set lro offload, err: %d, status: 0x%x\n",
			err, lro_cfg.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_set_rx_lro_timer(struct hinic3_hwdev *hwdev, u32 timer_value)
{
	struct l2nic_cmd_lro_timer lro_timer = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	lro_timer.opcode = MGMT_MSG_CMD_OP_SET;
	lro_timer.timer = timer_value;

	mgmt_msg_params_init_default(&msg_params, &lro_timer,
				     sizeof(lro_timer));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_LRO_TIMER,
				       &msg_params);

	if (err || lro_timer.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set lro timer, err: %d, status: 0x%x\n",
			err, lro_timer.msg_head.status);

		return -EFAULT;
	}

	return 0;
}

int hinic3_set_rx_lro_state(struct hinic3_hwdev *hwdev, u8 lro_en,
			    u32 lro_timer, u8 lro_max_pkt_len)
{
	u8 ipv4_en, ipv6_en;
	int err;

	ipv4_en = lro_en ? 1 : 0;
	ipv6_en = lro_en ? 1 : 0;

	dev_dbg(hwdev->dev, "Set LRO max coalesce packet size to %uK\n",
		lro_max_pkt_len);

	err = hinic3_set_rx_lro(hwdev, ipv4_en, ipv6_en, lro_max_pkt_len);
	if (err)
		return err;

	/* we don't set LRO timer for VF */
	if (HINIC3_IS_VF(hwdev))
		return 0;

	dev_dbg(hwdev->dev, "Set LRO timer to %u\n", lro_timer);

	return hinic3_set_rx_lro_timer(hwdev, lro_timer);
}

int hinic3_set_rx_vlan_offload(struct hinic3_hwdev *hwdev, u8 en)
{
	struct l2nic_cmd_vlan_offload vlan_cfg = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	vlan_cfg.func_id = hinic3_global_func_id(hwdev);
	vlan_cfg.vlan_offload = en;

	mgmt_msg_params_init_default(&msg_params, &vlan_cfg,
				     sizeof(vlan_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_RX_VLAN_OFFLOAD,
				       &msg_params);

	if (err || vlan_cfg.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set rx vlan offload, err: %d, status: 0x%x\n",
			err, vlan_cfg.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_set_vlan_filter(struct hinic3_hwdev *hwdev, u32 vlan_filter_ctrl)
{
	struct l2nic_cmd_set_vlan_filter vlan_filter = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	vlan_filter.func_id = hinic3_global_func_id(hwdev);
	vlan_filter.vlan_filter_ctrl = vlan_filter_ctrl;

	mgmt_msg_params_init_default(&msg_params, &vlan_filter,
				     sizeof(vlan_filter));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_VLAN_FILTER_EN,
				       &msg_params);

	if (err || vlan_filter.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set vlan filter, err: %d, status: 0x%x\n",
			err, vlan_filter.msg_head.status);
		return -EFAULT;
	}

	return 0;
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

int hinic3_init_function_table(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct l2nic_func_tbl_cfg func_tbl_cfg = {};
	u32 cfg_bitmap;

	func_tbl_cfg.mtu = 0x3FFF; /* default, max mtu */
	func_tbl_cfg.rx_wqe_buf_size = nic_io->rx_buf_len;

	cfg_bitmap = BIT(L2NIC_FUNC_TBL_CFG_INIT) |
		     BIT(L2NIC_FUNC_TBL_CFG_MTU) |
		     BIT(L2NIC_FUNC_TBL_CFG_RX_BUF_SIZE);

	return hinic3_set_function_table(nic_dev->hwdev, cfg_bitmap,
					 &func_tbl_cfg);
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

static bool hinic3_check_vf_set_by_pf(struct hinic3_hwdev *hwdev,
				      u8 status)
{
	return HINIC3_IS_VF(hwdev) && status == HINIC3_PF_SET_VF_ALREADY;
}

static int hinic3_check_mac_info(struct hinic3_hwdev *hwdev, u8 status,
				 u16 vlan_id)
{
	if ((status && status != MGMT_STATUS_EXIST) ||
	    ((vlan_id & BIT(15)) && status == MGMT_STATUS_EXIST)) {
		if (hinic3_check_vf_set_by_pf(hwdev, status))
			return 0;

		return -EINVAL;
	}

	return 0;
}

int hinic3_get_default_mac(struct hinic3_hwdev *hwdev, u8 *mac_addr)
{
	struct l2nic_cmd_set_mac mac_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	mac_info.func_id = hinic3_global_func_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &mac_info, sizeof(mac_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_GET_MAC,
				       &msg_params);

	if (err || mac_info.msg_head.status) {
		dev_err(hwdev->dev,
			"Failed to get mac, err: %d, status: 0x%x\n",
			err, mac_info.msg_head.status);
		return -EFAULT;
	}

	ether_addr_copy(mac_addr, mac_info.mac);

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

	if (hinic3_check_vf_set_by_pf(hwdev, mac_info.msg_head.status)) {
		dev_warn(hwdev->dev, "PF has already set VF mac, Ignore set operation\n");
		return -EADDRINUSE;
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
	if (err ||
	    (mac_info.msg_head.status &&
	     !hinic3_check_vf_set_by_pf(hwdev, mac_info.msg_head.status))) {
		dev_err(hwdev->dev,
			"Failed to delete MAC, err: %d, status: 0x%x\n",
			err, mac_info.msg_head.status);
		return -EFAULT;
	}

	if (hinic3_check_vf_set_by_pf(hwdev, mac_info.msg_head.status)) {
		dev_warn(hwdev->dev, "PF has already set VF mac, Ignore delete operation.\n");
		return -EADDRINUSE;
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

	if (hinic3_check_vf_set_by_pf(hwdev, mac_info.msg_head.status)) {
		dev_warn(hwdev->dev, "PF has already set VF MAC. Ignore update operation\n");
		return -EADDRINUSE;
	}

	if (mac_info.msg_head.status == HINIC3_MGMT_STATUS_EXIST) {
		dev_warn(hwdev->dev,
			 "MAC is repeated. Ignore update operation\n");
		return 0;
	}

	return 0;
}

int hinic3_set_ci_table(struct hinic3_hwdev *hwdev, struct hinic3_sq_attr *attr)
{
	struct l2nic_cmd_set_ci_attr cons_idx_attr = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	cons_idx_attr.func_idx = hinic3_global_func_id(hwdev);
	cons_idx_attr.dma_attr_off  = attr->dma_attr_off;
	cons_idx_attr.pending_limit = attr->pending_limit;
	cons_idx_attr.coalescing_time  = attr->coalescing_time;

	if (attr->intr_en) {
		cons_idx_attr.intr_en = attr->intr_en;
		cons_idx_attr.intr_idx = attr->intr_idx;
	}

	cons_idx_attr.l2nic_sqn = attr->l2nic_sqn;
	cons_idx_attr.ci_addr = attr->ci_dma_base;

	mgmt_msg_params_init_default(&msg_params, &cons_idx_attr,
				     sizeof(cons_idx_attr));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_SQ_CI_ATTR, &msg_params);
	if (err || cons_idx_attr.msg_head.status) {
		dev_err(hwdev->dev,
			"Failed to set ci attribute table, err: %d, status: 0x%x\n",
			err, cons_idx_attr.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_flush_qps_res(struct hinic3_hwdev *hwdev)
{
	struct l2nic_cmd_clear_qp_resource sq_res = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	sq_res.func_id = hinic3_global_func_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &sq_res, sizeof(sq_res));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CLEAR_QP_RESOURCE,
				       &msg_params);
	if (err || sq_res.msg_head.status) {
		dev_err(hwdev->dev, "Failed to clear sq resources, err: %d, status: 0x%x\n",
			err, sq_res.msg_head.status);
		return -EINVAL;
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

int hinic3_set_rx_mode(struct hinic3_hwdev *hwdev, u32 rx_mode)
{
	struct l2nic_rx_mode_config rx_mode_cfg = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	rx_mode_cfg.func_id = hinic3_global_func_id(hwdev);
	rx_mode_cfg.rx_mode = rx_mode;

	mgmt_msg_params_init_default(&msg_params, &rx_mode_cfg,
				     sizeof(rx_mode_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_RX_MODE, &msg_params);

	if (err || rx_mode_cfg.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set rx mode, err: %d, status: 0x%x\n",
			err, rx_mode_cfg.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_config_vlan(struct hinic3_hwdev *hwdev,
			      u8 opcode, u16 vlan_id, u16 func_id)
{
	struct l2nic_cmd_vlan_config vlan_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	vlan_info.opcode = opcode;
	vlan_info.func_id = func_id;
	vlan_info.vlan_id = vlan_id;

	mgmt_msg_params_init_default(&msg_params, &vlan_info,
				     sizeof(vlan_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_FUNC_VLAN, &msg_params);

	if (err || vlan_info.msg_head.status) {
		dev_err(hwdev->dev,
			"Failed to %s vlan, err: %d, status: 0x%x\n",
			opcode == MGMT_MSG_CMD_OP_ADD ? "add" : "delete",
			err, vlan_info.msg_head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_add_vlan(struct hinic3_hwdev *hwdev, u16 vlan_id, u16 func_id)
{
	return hinic3_config_vlan(hwdev, MGMT_MSG_CMD_OP_ADD, vlan_id, func_id);
}

int hinic3_del_vlan(struct hinic3_hwdev *hwdev, u16 vlan_id, u16 func_id)
{
	return hinic3_config_vlan(hwdev, MGMT_MSG_CMD_OP_DEL, vlan_id, func_id);
}

int hinic3_set_port_enable(struct hinic3_hwdev *hwdev, bool enable)
{
	struct mag_cmd_set_port_enable en_state = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	if (HINIC3_IS_VF(hwdev))
		return 0;

	en_state.function_id = hinic3_global_func_id(hwdev);
	en_state.state = enable ? MAG_CMD_TX_ENABLE | MAG_CMD_RX_ENABLE :
				MAG_CMD_PORT_DISABLE;

	mgmt_msg_params_init_default(&msg_params, &en_state,
				     sizeof(en_state));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_HILINK,
				       MAG_CMD_SET_PORT_ENABLE, &msg_params);

	if (err || en_state.head.status) {
		dev_err(hwdev->dev, "Failed to set port state, err: %d, status: 0x%x\n",
			err, en_state.head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_sync_dcb_state(struct hinic3_hwdev *hwdev, u8 op_code, u8 state)
{
	struct l2nic_cmd_set_dcb_state dcb_state = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	dcb_state.op_code = op_code;
	dcb_state.state = state;
	dcb_state.func_id = hinic3_global_func_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &dcb_state,
				     sizeof(dcb_state));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_QOS_DCB_STATE, &msg_params);
	if (err || dcb_state.head.status) {
		dev_err(hwdev->dev,
			"Failed to set dcb state, err: %d, status: 0x%x\n",
			err, dcb_state.head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_get_link_status(struct hinic3_hwdev *hwdev, bool *link_status_up)
{
	struct mag_cmd_get_link_status get_link = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	get_link.port_id = hinic3_physical_port_id(hwdev);

	mgmt_msg_params_init_default(&msg_params, &get_link, sizeof(get_link));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_HILINK,
				       MAG_CMD_GET_LINK_STATUS, &msg_params);
	if (err || get_link.head.status) {
		dev_err(hwdev->dev, "Failed to get link state, err: %d, status: 0x%x\n",
			err, get_link.head.status);
		return -EIO;
	}

	*link_status_up = !!get_link.status;

	return 0;
}

int hinic3_set_vport_enable(struct hinic3_hwdev *hwdev, u16 func_id,
			    bool enable)
{
	struct l2nic_cmd_set_vport_state en_state = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	en_state.func_id = func_id;
	en_state.state = enable ? 1 : 0;

	mgmt_msg_params_init_default(&msg_params, &en_state, sizeof(en_state));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_VPORT_ENABLE, &msg_params);
	if (err || en_state.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set vport state, err: %d, status: 0x%x\n",
			err, en_state.msg_head.status);
		return -EINVAL;
	}

	return 0;
}
