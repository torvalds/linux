// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>

#include "hinic_hw_if.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_dev.h"

enum mac_op {
	MAC_DEL,
	MAC_SET,
};

/**
 * change_mac - change(add or delete) mac address
 * @nic_dev: nic device
 * @addr: mac address
 * @vlan_id: vlan number to set with the mac
 * @op: add or delete the mac
 *
 * Return 0 - Success, negative - Failure
 **/
static int change_mac(struct hinic_dev *nic_dev, const u8 *addr,
		      u16 vlan_id, enum mac_op op)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_mac_cmd port_mac_cmd;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(port_mac_cmd);
	struct pci_dev *pdev = hwif->pdev;
	enum hinic_port_cmd cmd;
	int err;

	if (op == MAC_SET)
		cmd = HINIC_PORT_CMD_SET_MAC;
	else
		cmd = HINIC_PORT_CMD_DEL_MAC;

	port_mac_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	port_mac_cmd.vlan_id = vlan_id;
	memcpy(port_mac_cmd.mac, addr, ETH_ALEN);

	err = hinic_port_msg_cmd(hwdev, cmd, &port_mac_cmd,
				 sizeof(port_mac_cmd),
				 &port_mac_cmd, &out_size);
	if (err || out_size != sizeof(port_mac_cmd) ||
	    (port_mac_cmd.status &&
	     (port_mac_cmd.status != HINIC_PF_SET_VF_ALREADY || !HINIC_IS_VF(hwif)) &&
	     port_mac_cmd.status != HINIC_MGMT_STATUS_EXIST)) {
		dev_err(&pdev->dev, "Failed to change MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_mac_cmd.status, out_size);
		return -EFAULT;
	}

	if (port_mac_cmd.status == HINIC_PF_SET_VF_ALREADY) {
		dev_warn(&pdev->dev, "PF has already set VF mac, ignore %s operation\n",
			 (op == MAC_SET) ? "set" : "del");
		return HINIC_PF_SET_VF_ALREADY;
	}

	if (cmd == HINIC_PORT_CMD_SET_MAC && port_mac_cmd.status ==
	    HINIC_MGMT_STATUS_EXIST)
		dev_warn(&pdev->dev, "MAC is repeated, ignore set operation\n");

	return 0;
}

/**
 * hinic_port_add_mac - add mac address
 * @nic_dev: nic device
 * @addr: mac address
 * @vlan_id: vlan number to set with the mac
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_add_mac(struct hinic_dev *nic_dev,
		       const u8 *addr, u16 vlan_id)
{
	return change_mac(nic_dev, addr, vlan_id, MAC_SET);
}

/**
 * hinic_port_del_mac - remove mac address
 * @nic_dev: nic device
 * @addr: mac address
 * @vlan_id: vlan number that is connected to the mac
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_del_mac(struct hinic_dev *nic_dev, const u8 *addr,
		       u16 vlan_id)
{
	return change_mac(nic_dev, addr, vlan_id, MAC_DEL);
}

/**
 * hinic_port_get_mac - get the mac address of the nic device
 * @nic_dev: nic device
 * @addr: returned mac address
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_get_mac(struct hinic_dev *nic_dev, u8 *addr)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_mac_cmd port_mac_cmd;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(port_mac_cmd);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	port_mac_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_MAC,
				 &port_mac_cmd, sizeof(port_mac_cmd),
				 &port_mac_cmd, &out_size);
	if (err || out_size != sizeof(port_mac_cmd) || port_mac_cmd.status) {
		dev_err(&pdev->dev, "Failed to get mac, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_mac_cmd.status, out_size);
		return -EFAULT;
	}

	memcpy(addr, port_mac_cmd.mac, ETH_ALEN);
	return 0;
}

/**
 * hinic_port_set_mtu - set mtu
 * @nic_dev: nic device
 * @new_mtu: new mtu
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_set_mtu(struct hinic_dev *nic_dev, int new_mtu)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_mtu_cmd port_mtu_cmd;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(port_mtu_cmd);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	port_mtu_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	port_mtu_cmd.mtu = new_mtu;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_CHANGE_MTU,
				 &port_mtu_cmd, sizeof(port_mtu_cmd),
				 &port_mtu_cmd, &out_size);
	if (err || out_size != sizeof(port_mtu_cmd) || port_mtu_cmd.status) {
		dev_err(&pdev->dev, "Failed to set mtu, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_mtu_cmd.status, out_size);
		return -EFAULT;
	}

	return 0;
}

/**
 * hinic_port_add_vlan - add vlan to the nic device
 * @nic_dev: nic device
 * @vlan_id: the vlan number to add
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_add_vlan(struct hinic_dev *nic_dev, u16 vlan_id)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_vlan_cmd port_vlan_cmd;

	port_vlan_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	port_vlan_cmd.vlan_id = vlan_id;

	return hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_ADD_VLAN,
				  &port_vlan_cmd, sizeof(port_vlan_cmd),
				  NULL, NULL);
}

/**
 * hinic_port_del_vlan - delete vlan from the nic device
 * @nic_dev: nic device
 * @vlan_id: the vlan number to delete
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_del_vlan(struct hinic_dev *nic_dev, u16 vlan_id)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_vlan_cmd port_vlan_cmd;

	port_vlan_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	port_vlan_cmd.vlan_id = vlan_id;

	return hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_DEL_VLAN,
				 &port_vlan_cmd, sizeof(port_vlan_cmd),
				 NULL, NULL);
}

/**
 * hinic_port_set_rx_mode - set rx mode in the nic device
 * @nic_dev: nic device
 * @rx_mode: the rx mode to set
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_set_rx_mode(struct hinic_dev *nic_dev, u32 rx_mode)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_rx_mode_cmd rx_mode_cmd;

	rx_mode_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	rx_mode_cmd.rx_mode = rx_mode;

	return hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RX_MODE,
				  &rx_mode_cmd, sizeof(rx_mode_cmd),
				  NULL, NULL);
}

/**
 * hinic_port_link_state - get the link state
 * @nic_dev: nic device
 * @link_state: the returned link state
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_link_state(struct hinic_dev *nic_dev,
			  enum hinic_port_link_state *link_state)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_port_link_cmd link_cmd;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(link_cmd);
	int err;

	link_cmd.func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_LINK_STATE,
				 &link_cmd, sizeof(link_cmd),
				 &link_cmd, &out_size);
	if (err || out_size != sizeof(link_cmd) || link_cmd.status) {
		dev_err(&pdev->dev, "Failed to get link state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, link_cmd.status, out_size);
		return -EINVAL;
	}

	*link_state = link_cmd.state;
	return 0;
}

/**
 * hinic_port_set_state - set port state
 * @nic_dev: nic device
 * @state: the state to set
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_set_state(struct hinic_dev *nic_dev, enum hinic_port_state state)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_port_state_cmd port_state;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(port_state);
	int err;

	if (HINIC_IS_VF(hwdev->hwif))
		return 0;

	port_state.state = state;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_PORT_STATE,
				 &port_state, sizeof(port_state),
				 &port_state, &out_size);
	if (err || out_size != sizeof(port_state) || port_state.status) {
		dev_err(&pdev->dev, "Failed to set port state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_state.status, out_size);
		return -EFAULT;
	}

	return 0;
}

/**
 * hinic_port_set_func_state- set func device state
 * @nic_dev: nic device
 * @state: the state to set
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_set_func_state(struct hinic_dev *nic_dev,
			      enum hinic_func_port_state state)
{
	struct hinic_port_func_state_cmd func_state;
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(func_state);
	int err;

	func_state.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	func_state.state = state;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_FUNC_STATE,
				 &func_state, sizeof(func_state),
				 &func_state, &out_size);
	if (err || out_size != sizeof(func_state) || func_state.status) {
		dev_err(&pdev->dev, "Failed to set port func state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, func_state.status, out_size);
		return -EFAULT;
	}

	return 0;
}

/**
 * hinic_port_get_cap - get port capabilities
 * @nic_dev: nic device
 * @port_cap: returned port capabilities
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_get_cap(struct hinic_dev *nic_dev,
		       struct hinic_port_cap *port_cap)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(*port_cap);
	int err;

	port_cap->func_idx = HINIC_HWIF_FUNC_IDX(hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_CAP,
				 port_cap, sizeof(*port_cap),
				 port_cap, &out_size);
	if (err || out_size != sizeof(*port_cap) || port_cap->status) {
		dev_err(&pdev->dev,
			"Failed to get port capabilities, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_cap->status, out_size);
		return -EIO;
	}

	return 0;
}

/**
 * hinic_port_set_tso - set port tso configuration
 * @nic_dev: nic device
 * @state: the tso state to set
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_port_set_tso(struct hinic_dev *nic_dev, enum hinic_tso_state state)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_tso_config tso_cfg = {0};
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(tso_cfg);
	int err;

	tso_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	tso_cfg.tso_en = state;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_TSO,
				 &tso_cfg, sizeof(tso_cfg),
				 &tso_cfg, &out_size);
	if (err || out_size != sizeof(tso_cfg) || tso_cfg.status) {
		dev_err(&pdev->dev,
			"Failed to set port tso, err: %d, status: 0x%x, out size: 0x%x\n",
			err, tso_cfg.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_rx_csum_offload(struct hinic_dev *nic_dev, u32 en)
{
	struct hinic_checksum_offload rx_csum_cfg = {0};
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	u16 out_size = sizeof(rx_csum_cfg);
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	int err;

	if (!hwdev)
		return -EINVAL;

	hwif = hwdev->hwif;
	pdev = hwif->pdev;
	rx_csum_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rx_csum_cfg.rx_csum_offload = en;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RX_CSUM,
				 &rx_csum_cfg, sizeof(rx_csum_cfg),
				 &rx_csum_cfg, &out_size);
	if (err || !out_size || rx_csum_cfg.status) {
		dev_err(&pdev->dev,
			"Failed to set rx csum offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rx_csum_cfg.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_rx_vlan_offload(struct hinic_dev *nic_dev, u8 en)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_vlan_cfg vlan_cfg;
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	u16 out_size;
	int err;

	if (!hwdev)
		return -EINVAL;

	out_size = sizeof(vlan_cfg);
	hwif = hwdev->hwif;
	pdev = hwif->pdev;
	vlan_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	vlan_cfg.vlan_rx_offload = en;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RX_VLAN_OFFLOAD,
				 &vlan_cfg, sizeof(vlan_cfg),
				 &vlan_cfg, &out_size);
	if (err || !out_size || vlan_cfg.status) {
		dev_err(&pdev->dev,
			"Failed to set rx vlan offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_vlan_fliter(struct hinic_dev *nic_dev, u32 en)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_vlan_filter vlan_filter;
	u16 out_size = sizeof(vlan_filter);
	int err;

	vlan_filter.func_idx = HINIC_HWIF_FUNC_IDX(hwif);
	vlan_filter.enable = en;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_VLAN_FILTER,
				 &vlan_filter, sizeof(vlan_filter),
				 &vlan_filter, &out_size);
	if (vlan_filter.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if ((err == HINIC_MBOX_VF_CMD_ERROR) &&
			   HINIC_IS_VF(hwif)) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if (err || !out_size || vlan_filter.status) {
		dev_err(&pdev->dev,
			"Failed to set vlan filter, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_filter.status, out_size);
		err = -EINVAL;
	}

	return err;
}

int hinic_set_max_qnum(struct hinic_dev *nic_dev, u8 num_rqs)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_rq_num rq_num = { 0 };
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(rq_num);
	int err;

	rq_num.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rq_num.num_rqs = num_rqs;
	rq_num.rq_depth = ilog2(nic_dev->rq_depth);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RQ_IQ_MAP,
				 &rq_num, sizeof(rq_num),
				 &rq_num, &out_size);
	if (err || !out_size || rq_num.status) {
		dev_err(&pdev->dev,
			"Failed to set rxq number, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rq_num.status, out_size);
		return -EIO;
	}

	return 0;
}

static int hinic_set_rx_lro(struct hinic_dev *nic_dev, u8 ipv4_en, u8 ipv6_en,
			    u8 max_wqe_num)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_lro_config lro_cfg = { 0 };
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(lro_cfg);
	int err;

	lro_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	lro_cfg.lro_ipv4_en = ipv4_en;
	lro_cfg.lro_ipv6_en = ipv6_en;
	lro_cfg.lro_max_wqe_num = max_wqe_num;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_LRO,
				 &lro_cfg, sizeof(lro_cfg),
				 &lro_cfg, &out_size);
	if (err || !out_size || lro_cfg.status) {
		dev_err(&pdev->dev,
			"Failed to set lro offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lro_cfg.status, out_size);
		return -EIO;
	}

	return 0;
}

static int hinic_set_rx_lro_timer(struct hinic_dev *nic_dev, u32 timer_value)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_lro_timer lro_timer = { 0 };
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(lro_timer);
	int err;

	lro_timer.status = 0;
	lro_timer.type = 0;
	lro_timer.enable = 1;
	lro_timer.timer = timer_value;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_LRO_TIMER,
				 &lro_timer, sizeof(lro_timer),
				 &lro_timer, &out_size);
	if (lro_timer.status == 0xFF) {
		/* For this case, we think status (0xFF) is OK */
		lro_timer.status = 0;
		dev_dbg(&pdev->dev,
			"Set lro timer not supported by the current FW version, it will be 1ms default\n");
	}

	if (err || !out_size || lro_timer.status) {
		dev_err(&pdev->dev,
			"Failed to set lro timer, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lro_timer.status, out_size);

		return -EIO;
	}

	return 0;
}

int hinic_set_rx_lro_state(struct hinic_dev *nic_dev, u8 lro_en,
			   u32 lro_timer, u32 wqe_num)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	u8 ipv4_en;
	u8 ipv6_en;
	int err;

	if (!hwdev)
		return -EINVAL;

	ipv4_en = lro_en ? 1 : 0;
	ipv6_en = lro_en ? 1 : 0;

	err = hinic_set_rx_lro(nic_dev, ipv4_en, ipv6_en, (u8)wqe_num);
	if (err)
		return err;

	if (HINIC_IS_VF(nic_dev->hwdev->hwif))
		return 0;

	err = hinic_set_rx_lro_timer(nic_dev, lro_timer);
	if (err)
		return err;

	return 0;
}

int hinic_rss_set_indir_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			    const u32 *indir_table)
{
	struct hinic_rss_indirect_tbl *indir_tbl;
	struct hinic_func_to_io *func_to_io;
	struct hinic_cmdq_buf cmd_buf;
	struct hinic_hwdev *hwdev;
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	u32 indir_size;
	u64 out_param;
	int err, i;
	u32 *temp;

	hwdev = nic_dev->hwdev;
	func_to_io = &hwdev->func_to_io;
	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	err = hinic_alloc_cmdq_buf(&func_to_io->cmdqs, &cmd_buf);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate cmdq buf\n");
		return err;
	}

	cmd_buf.size = sizeof(*indir_tbl);

	indir_tbl = cmd_buf.buf;
	indir_tbl->group_index = cpu_to_be32(tmpl_idx);

	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++) {
		indir_tbl->entry[i] = indir_table[i];

		if (0x3 == (i & 0x3)) {
			temp = (u32 *)&indir_tbl->entry[i - 3];
			*temp = cpu_to_be32(*temp);
		}
	}

	/* cfg the rss indirect table by command queue */
	indir_size = HINIC_RSS_INDIR_SIZE / 2;
	indir_tbl->offset = 0;
	indir_tbl->size = cpu_to_be32(indir_size);

	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_INDIR_TABLE,
				     &cmd_buf, &out_param);
	if (err || out_param != 0) {
		dev_err(&pdev->dev, "Failed to set rss indir table\n");
		err = -EFAULT;
		goto free_buf;
	}

	indir_tbl->offset = cpu_to_be32(indir_size);
	indir_tbl->size = cpu_to_be32(indir_size);
	memcpy(&indir_tbl->entry[0], &indir_tbl->entry[indir_size], indir_size);

	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_INDIR_TABLE,
				     &cmd_buf, &out_param);
	if (err || out_param != 0) {
		dev_err(&pdev->dev, "Failed to set rss indir table\n");
		err = -EFAULT;
	}

free_buf:
	hinic_free_cmdq_buf(&func_to_io->cmdqs, &cmd_buf);

	return err;
}

int hinic_rss_get_indir_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			    u32 *indir_table)
{
	struct hinic_rss_indir_table rss_cfg = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(rss_cfg);
	int err = 0, i;

	rss_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rss_cfg.template_id = tmpl_idx;

	err = hinic_port_msg_cmd(hwdev,
				 HINIC_PORT_CMD_GET_RSS_TEMPLATE_INDIR_TBL,
				 &rss_cfg, sizeof(rss_cfg), &rss_cfg,
				 &out_size);
	if (err || !out_size || rss_cfg.status) {
		dev_err(&pdev->dev, "Failed to get indir table, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_cfg.status, out_size);
		return -EINVAL;
	}

	hinic_be32_to_cpu(rss_cfg.indir, HINIC_RSS_INDIR_SIZE);
	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++)
		indir_table[i] = rss_cfg.indir[i];

	return 0;
}

int hinic_set_rss_type(struct hinic_dev *nic_dev, u32 tmpl_idx,
		       struct hinic_rss_type rss_type)
{
	struct hinic_rss_context_tbl *ctx_tbl;
	struct hinic_func_to_io *func_to_io;
	struct hinic_cmdq_buf cmd_buf;
	struct hinic_hwdev *hwdev;
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	u64 out_param;
	u32 ctx = 0;
	int err;

	hwdev = nic_dev->hwdev;
	func_to_io = &hwdev->func_to_io;
	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	err = hinic_alloc_cmdq_buf(&func_to_io->cmdqs, &cmd_buf);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	ctx |=  HINIC_RSS_TYPE_SET(1, VALID) |
		HINIC_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
		HINIC_RSS_TYPE_SET(rss_type.ipv6_ext, IPV6_EXT) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv6_ext, TCP_IPV6_EXT) |
		HINIC_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);

	cmd_buf.size = sizeof(struct hinic_rss_context_tbl);

	ctx_tbl = (struct hinic_rss_context_tbl *)cmd_buf.buf;
	ctx_tbl->group_index = cpu_to_be32(tmpl_idx);
	ctx_tbl->offset = 0;
	ctx_tbl->size = sizeof(u32);
	ctx_tbl->size = cpu_to_be32(ctx_tbl->size);
	ctx_tbl->rsvd = 0;
	ctx_tbl->ctx = cpu_to_be32(ctx);

	/* cfg the rss context table by command queue */
	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_CONTEXT_TABLE,
				     &cmd_buf, &out_param);

	hinic_free_cmdq_buf(&func_to_io->cmdqs, &cmd_buf);

	if (err || out_param != 0) {
		dev_err(&pdev->dev, "Failed to set rss context table, err: %d\n",
			err);
		return -EFAULT;
	}

	return 0;
}

int hinic_get_rss_type(struct hinic_dev *nic_dev, u32 tmpl_idx,
		       struct hinic_rss_type *rss_type)
{
	struct hinic_rss_context_table ctx_tbl = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	u16 out_size = sizeof(ctx_tbl);
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	int err;

	if (!hwdev || !rss_type)
		return -EINVAL;

	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	ctx_tbl.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	ctx_tbl.template_id = tmpl_idx;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_RSS_CTX_TBL,
				 &ctx_tbl, sizeof(ctx_tbl),
				 &ctx_tbl, &out_size);
	if (err || !out_size || ctx_tbl.status) {
		dev_err(&pdev->dev, "Failed to get hash type, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ctx_tbl.status, out_size);
		return -EINVAL;
	}

	rss_type->ipv4          = HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV4);
	rss_type->ipv6          = HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV6);
	rss_type->ipv6_ext      = HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV6_EXT);
	rss_type->tcp_ipv4      = HINIC_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV4);
	rss_type->tcp_ipv6      = HINIC_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV6);
	rss_type->tcp_ipv6_ext  = HINIC_RSS_TYPE_GET(ctx_tbl.context,
						     TCP_IPV6_EXT);
	rss_type->udp_ipv4      = HINIC_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV4);
	rss_type->udp_ipv6      = HINIC_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV6);

	return 0;
}

int hinic_rss_set_template_tbl(struct hinic_dev *nic_dev, u32 template_id,
			       const u8 *temp)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_rss_key rss_key = { 0 };
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(rss_key);
	int err;

	rss_key.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rss_key.template_id = template_id;
	memcpy(rss_key.key, temp, HINIC_RSS_KEY_SIZE);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RSS_TEMPLATE_TBL,
				 &rss_key, sizeof(rss_key),
				 &rss_key, &out_size);
	if (err || !out_size || rss_key.status) {
		dev_err(&pdev->dev,
			"Failed to set rss hash key, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_key.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_rss_get_template_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			       u8 *temp)
{
	struct hinic_rss_template_key temp_key = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	u16 out_size = sizeof(temp_key);
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	int err;

	if (!hwdev || !temp)
		return -EINVAL;

	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	temp_key.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	temp_key.template_id = tmpl_idx;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_RSS_TEMPLATE_TBL,
				 &temp_key, sizeof(temp_key),
				 &temp_key, &out_size);
	if (err || !out_size || temp_key.status) {
		dev_err(&pdev->dev, "Failed to set hash key, err: %d, status: 0x%x, out size: 0x%x\n",
			err, temp_key.status, out_size);
		return -EINVAL;
	}

	memcpy(temp, temp_key.key, HINIC_RSS_KEY_SIZE);

	return 0;
}

int hinic_rss_set_hash_engine(struct hinic_dev *nic_dev, u8 template_id,
			      u8 type)
{
	struct hinic_rss_engine_type rss_engine = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(rss_engine);
	int err;

	rss_engine.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rss_engine.hash_engine = type;
	rss_engine.template_id = template_id;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_RSS_HASH_ENGINE,
				 &rss_engine, sizeof(rss_engine),
				 &rss_engine, &out_size);
	if (err || !out_size || rss_engine.status) {
		dev_err(&pdev->dev,
			"Failed to set hash engine, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_engine.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_rss_get_hash_engine(struct hinic_dev *nic_dev, u8 tmpl_idx, u8 *type)
{
	struct hinic_rss_engine_type hash_type = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	u16 out_size = sizeof(hash_type);
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	int err;

	if (!hwdev || !type)
		return -EINVAL;

	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	hash_type.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	hash_type.template_id = tmpl_idx;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_RSS_HASH_ENGINE,
				 &hash_type, sizeof(hash_type),
				 &hash_type, &out_size);
	if (err || !out_size || hash_type.status) {
		dev_err(&pdev->dev, "Failed to get hash engine, err: %d, status: 0x%x, out size: 0x%x\n",
			err, hash_type.status, out_size);
		return -EINVAL;
	}

	*type = hash_type.hash_engine;
	return 0;
}

int hinic_rss_cfg(struct hinic_dev *nic_dev, u8 rss_en, u8 template_id)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_rss_config rss_cfg = { 0 };
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u16 out_size = sizeof(rss_cfg);
	int err;

	rss_cfg.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	rss_cfg.rss_en = rss_en;
	rss_cfg.template_id = template_id;
	rss_cfg.rq_priority_number = 0;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_RSS_CFG,
				 &rss_cfg, sizeof(rss_cfg),
				 &rss_cfg, &out_size);
	if (err || !out_size || rss_cfg.status) {
		dev_err(&pdev->dev,
			"Failed to set rss cfg, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_rss_template_alloc(struct hinic_dev *nic_dev, u8 *tmpl_idx)
{
	struct hinic_rss_template_mgmt template_mgmt = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(template_mgmt);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	template_mgmt.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	template_mgmt.cmd = NIC_RSS_CMD_TEMP_ALLOC;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_RSS_TEMP_MGR,
				 &template_mgmt, sizeof(template_mgmt),
				 &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.status) {
		dev_err(&pdev->dev, "Failed to alloc rss template, err: %d, status: 0x%x, out size: 0x%x\n",
			err, template_mgmt.status, out_size);
		return -EINVAL;
	}

	*tmpl_idx = template_mgmt.template_id;

	return 0;
}

int hinic_rss_template_free(struct hinic_dev *nic_dev, u8 tmpl_idx)
{
	struct hinic_rss_template_mgmt template_mgmt = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(template_mgmt);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	template_mgmt.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	template_mgmt.template_id = tmpl_idx;
	template_mgmt.cmd = NIC_RSS_CMD_TEMP_FREE;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_RSS_TEMP_MGR,
				 &template_mgmt, sizeof(template_mgmt),
				 &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.status) {
		dev_err(&pdev->dev, "Failed to free rss template, err: %d, status: 0x%x, out size: 0x%x\n",
			err, template_mgmt.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_get_vport_stats(struct hinic_dev *nic_dev,
			  struct hinic_vport_stats *stats)
{
	struct hinic_cmd_vport_stats vport_stats = { 0 };
	struct hinic_port_stats_info stats_info = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	u16 out_size = sizeof(vport_stats);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	stats_info.stats_version = HINIC_PORT_STATS_VERSION;
	stats_info.func_id = HINIC_HWIF_FUNC_IDX(hwif);
	stats_info.stats_size = sizeof(vport_stats);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_VPORT_STAT,
				 &stats_info, sizeof(stats_info),
				 &vport_stats, &out_size);
	if (err || !out_size || vport_stats.status) {
		dev_err(&pdev->dev,
			"Failed to get function statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vport_stats.status, out_size);
		return -EFAULT;
	}

	memcpy(stats, &vport_stats.stats, sizeof(*stats));
	return 0;
}

int hinic_get_phy_port_stats(struct hinic_dev *nic_dev,
			     struct hinic_phy_port_stats *stats)
{
	struct hinic_port_stats_info stats_info = { 0 };
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_port_stats *port_stats;
	u16 out_size = sizeof(*port_stats);
	struct pci_dev *pdev = hwif->pdev;
	int err;

	port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
	if (!port_stats)
		return -ENOMEM;

	stats_info.stats_version = HINIC_PORT_STATS_VERSION;
	stats_info.stats_size = sizeof(*port_stats);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_PORT_STATISTICS,
				 &stats_info, sizeof(stats_info),
				 port_stats, &out_size);
	if (err || !out_size || port_stats->status) {
		dev_err(&pdev->dev,
			"Failed to get port statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_stats->status, out_size);
		err = -EINVAL;
		goto out;
	}

	memcpy(stats, &port_stats->stats, sizeof(*stats));

out:
	kfree(port_stats);

	return err;
}

int hinic_get_mgmt_version(struct hinic_dev *nic_dev, u8 *mgmt_ver)
{
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_version_info up_ver = {0};
	u16 out_size = sizeof(up_ver);
	struct hinic_hwif *hwif;
	struct pci_dev *pdev;
	int err;

	if (!hwdev)
		return -EINVAL;

	hwif = hwdev->hwif;
	pdev = hwif->pdev;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_MGMT_VERSION,
				 &up_ver, sizeof(up_ver), &up_ver,
				 &out_size);
	if (err || !out_size || up_ver.status) {
		dev_err(&pdev->dev,
			"Failed to get mgmt version, err: %d, status: 0x%x, out size: 0x%x\n",
			err, up_ver.status, out_size);
		return -EINVAL;
	}

	snprintf(mgmt_ver, HINIC_MGMT_VERSION_MAX_LEN, "%s", up_ver.ver);

	return 0;
}

int hinic_get_link_mode(struct hinic_hwdev *hwdev,
			struct hinic_link_mode_cmd *link_mode)
{
	u16 out_size;
	int err;

	if (!hwdev || !link_mode)
		return -EINVAL;

	link_mode->func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	out_size = sizeof(*link_mode);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_LINK_MODE,
				 link_mode, sizeof(*link_mode),
				 link_mode, &out_size);
	if (err || !out_size || link_mode->status) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to get link mode, err: %d, status: 0x%x, out size: 0x%x\n",
			err, link_mode->status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_autoneg(struct hinic_hwdev *hwdev, bool enable)
{
	struct hinic_set_autoneg_cmd autoneg = {0};
	u16 out_size = sizeof(autoneg);
	int err;

	if (!hwdev)
		return -EINVAL;

	autoneg.func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	autoneg.enable = enable;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_AUTONEG,
				 &autoneg, sizeof(autoneg),
				 &autoneg, &out_size);
	if (err || !out_size || autoneg.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to %s autoneg, err: %d, status: 0x%x, out size: 0x%x\n",
			enable ? "enable" : "disable", err, autoneg.status,
			out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_speed(struct hinic_hwdev *hwdev, enum nic_speed_level speed)
{
	struct hinic_speed_cmd speed_info = {0};
	u16 out_size = sizeof(speed_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	speed_info.func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	speed_info.speed = speed;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_SPEED,
				 &speed_info, sizeof(speed_info),
				 &speed_info, &out_size);
	if (err || !out_size || speed_info.status) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to set speed, err: %d, status: 0x%x, out size: 0x%x\n",
			err, speed_info.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_link_settings(struct hinic_hwdev *hwdev,
			    struct hinic_link_ksettings_info *info)
{
	u16 out_size = sizeof(*info);
	int err;

	err = hinic_hilink_msg_cmd(hwdev, HINIC_HILINK_CMD_SET_LINK_SETTINGS,
				   info, sizeof(*info), info, &out_size);
	if ((info->status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     info->status) || err || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to set link settings, err: %d, status: 0x%x, out size: 0x%x\n",
			err, info->status, out_size);
		return -EFAULT;
	}

	return info->status;
}

int hinic_get_hw_pause_info(struct hinic_hwdev *hwdev,
			    struct hinic_pause_config *pause_info)
{
	u16 out_size = sizeof(*pause_info);
	int err;

	pause_info->func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_PAUSE_INFO,
				 pause_info, sizeof(*pause_info),
				 pause_info, &out_size);
	if (err || !out_size || pause_info->status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to get pause info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pause_info->status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_hw_pause_info(struct hinic_hwdev *hwdev,
			    struct hinic_pause_config *pause_info)
{
	u16 out_size = sizeof(*pause_info);
	int err;

	pause_info->func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_PAUSE_INFO,
				 pause_info, sizeof(*pause_info),
				 pause_info, &out_size);
	if (err || !out_size || pause_info->status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set pause info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pause_info->status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_dcb_set_pfc(struct hinic_hwdev *hwdev, u8 pfc_en, u8 pfc_bitmap)
{
	struct hinic_nic_cfg *nic_cfg = &hwdev->func_to_io.nic_cfg;
	struct hinic_set_pfc pfc = {0};
	u16 out_size = sizeof(pfc);
	int err;

	if (HINIC_IS_VF(hwdev->hwif))
		return 0;

	mutex_lock(&nic_cfg->cfg_mutex);

	pfc.func_id = HINIC_HWIF_FUNC_IDX(hwdev->hwif);
	pfc.pfc_bitmap = pfc_bitmap;
	pfc.pfc_en = pfc_en;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_PFC,
				 &pfc, sizeof(pfc), &pfc, &out_size);
	if (err || pfc.status || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to %s pfc, err: %d, status: 0x%x, out size: 0x%x\n",
			pfc_en ? "enable" : "disable", err, pfc.status,
			out_size);
		mutex_unlock(&nic_cfg->cfg_mutex);
		return -EIO;
	}

	/* pause settings is opposite from pfc */
	nic_cfg->rx_pause = pfc_en ? 0 : 1;
	nic_cfg->tx_pause = pfc_en ? 0 : 1;

	mutex_unlock(&nic_cfg->cfg_mutex);

	return 0;
}

int hinic_set_loopback_mode(struct hinic_hwdev *hwdev, u32 mode, u32 enable)
{
	struct hinic_port_loopback lb = {0};
	u16 out_size = sizeof(lb);
	int err;

	lb.mode = mode;
	lb.en = enable;

	if (mode < LOOP_MODE_MIN || mode > LOOP_MODE_MAX) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Invalid loopback mode %d to set\n", mode);
		return -EINVAL;
	}

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_LOOPBACK_MODE,
				 &lb, sizeof(lb), &lb, &out_size);
	if (err || !out_size || lb.status) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to set loopback mode %d en %d, err: %d, status: 0x%x, out size: 0x%x\n",
			mode, enable, err, lb.status, out_size);
		return -EIO;
	}

	return 0;
}

static int _set_led_status(struct hinic_hwdev *hwdev, u8 port,
			   enum hinic_led_type type,
			   enum hinic_led_mode mode, u8 reset)
{
	struct hinic_led_info led_info = {0};
	u16 out_size = sizeof(led_info);
	struct hinic_pfhwdev *pfhwdev;
	int err;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	led_info.port = port;
	led_info.reset = reset;

	led_info.type = type;
	led_info.mode = mode;

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_SET_LED_STATUS,
				&led_info, sizeof(led_info),
				&led_info, &out_size, HINIC_MGMT_MSG_SYNC);
	if (err || led_info.status || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set led status, err: %d, status: 0x%x, out size: 0x%x\n",
			err, led_info.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic_set_led_status(struct hinic_hwdev *hwdev, u8 port,
			 enum hinic_led_type type, enum hinic_led_mode mode)
{
	if (!hwdev)
		return -EINVAL;

	return _set_led_status(hwdev, port, type, mode, 0);
}

int hinic_reset_led_status(struct hinic_hwdev *hwdev, u8 port)
{
	int err;

	if (!hwdev)
		return -EINVAL;

	err = _set_led_status(hwdev, port, HINIC_LED_TYPE_INVALID,
			      HINIC_LED_MODE_INVALID, 1);
	if (err)
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to reset led status\n");

	return err;
}

static bool hinic_if_sfp_absent(struct hinic_hwdev *hwdev)
{
	struct hinic_cmd_get_light_module_abs sfp_abs = {0};
	u16 out_size = sizeof(sfp_abs);
	u8 port_id = hwdev->port_id;
	int err;

	sfp_abs.port_id = port_id;
	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_SFP_ABS,
				 &sfp_abs, sizeof(sfp_abs), &sfp_abs,
				 &out_size);
	if (sfp_abs.status || err || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to get port%d sfp absent status, err: %d, status: 0x%x, out size: 0x%x\n",
			port_id, err, sfp_abs.status, out_size);
		return true;
	}

	return ((sfp_abs.abs_status == 0) ? false : true);
}

int hinic_get_sfp_eeprom(struct hinic_hwdev *hwdev, u8 *data, u16 *len)
{
	struct hinic_cmd_get_std_sfp_info sfp_info = {0};
	u16 out_size = sizeof(sfp_info);
	u8 port_id;
	int err;

	if (!hwdev || !data || !len)
		return -EINVAL;

	port_id = hwdev->port_id;

	if (hinic_if_sfp_absent(hwdev))
		return -ENXIO;

	sfp_info.port_id = port_id;
	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_GET_STD_SFP_INFO,
				 &sfp_info, sizeof(sfp_info), &sfp_info,
				 &out_size);
	if (sfp_info.status || err || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to get port%d sfp eeprom information, err: %d, status: 0x%x, out size: 0x%x\n",
			port_id, err, sfp_info.status, out_size);
		return -EIO;
	}

	*len = min_t(u16, sfp_info.eeprom_len, STD_SFP_INFO_MAX_SIZE);
	memcpy(data, sfp_info.sfp_info, STD_SFP_INFO_MAX_SIZE);

	return 0;
}

int hinic_get_sfp_type(struct hinic_hwdev *hwdev, u8 *data0, u8 *data1)
{
	u8 sfp_data[STD_SFP_INFO_MAX_SIZE];
	u16 len;
	int err;

	if (hinic_if_sfp_absent(hwdev))
		return -ENXIO;

	err = hinic_get_sfp_eeprom(hwdev, sfp_data, &len);
	if (err)
		return err;

	*data0 = sfp_data[0];
	*data1 = sfp_data[1];

	return 0;
}
