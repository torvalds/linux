// SPDX-License-Identifier: GPL-2.0-only
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/pci.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "hinic_hw_dev.h"
#include "hinic_dev.h"
#include "hinic_hw_mbox.h"
#include "hinic_hw_cmdq.h"
#include "hinic_port.h"
#include "hinic_sriov.h"

static unsigned char set_vf_link_state;
module_param(set_vf_link_state, byte, 0444);
MODULE_PARM_DESC(set_vf_link_state, "Set vf link state, 0 represents link auto, 1 represents link always up, 2 represents link always down. - default is 0.");

#define HINIC_VLAN_PRIORITY_SHIFT 13
#define HINIC_ADD_VLAN_IN_MAC 0x8000
#define HINIC_TX_RATE_TABLE_FULL 12

static int hinic_set_mac(struct hinic_hwdev *hwdev, const u8 *mac_addr,
			 u16 vlan_id, u16 func_id)
{
	struct hinic_port_mac_cmd mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	mac_info.func_idx = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.mac, mac_addr, ETH_ALEN);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_MAC, &mac_info,
				 sizeof(mac_info), &mac_info, &out_size);
	if (err || out_size != sizeof(mac_info) ||
	    (mac_info.status && mac_info.status != HINIC_MGMT_STATUS_EXIST)) {
		dev_err(&hwdev->func_to_io.hwif->pdev->dev, "Failed to set MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EIO;
	}

	return 0;
}

static void hinic_notify_vf_link_status(struct hinic_hwdev *hwdev, u16 vf_id,
					u8 link_status)
{
	struct vf_data_storage *vf_infos = hwdev->func_to_io.vf_infos;
	struct hinic_port_link_status link = {0};
	u16 out_size = sizeof(link);
	int err;

	if (vf_infos[HW_VF_ID_TO_OS(vf_id)].registered) {
		link.link = link_status;
		link.func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;
		err = hinic_mbox_to_vf(hwdev, HINIC_MOD_L2NIC,
				       vf_id, HINIC_PORT_CMD_LINK_STATUS_REPORT,
				       &link, sizeof(link),
				       &link, &out_size, 0);
		if (err || !out_size || link.status)
			dev_err(&hwdev->hwif->pdev->dev,
				"Send link change event to VF %d failed, err: %d, status: 0x%x, out_size: 0x%x\n",
				HW_VF_ID_TO_OS(vf_id), err,
				link.status, out_size);
	}
}

/* send link change event mbox msg to active vfs under the pf */
void hinic_notify_all_vfs_link_changed(struct hinic_hwdev *hwdev,
				       u8 link_status)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	u16 i;

	nic_io->link_status = link_status;
	for (i = 1; i <= nic_io->max_vfs; i++) {
		if (!nic_io->vf_infos[HW_VF_ID_TO_OS(i)].link_forced)
			hinic_notify_vf_link_status(hwdev, i,  link_status);
	}
}

static u16 hinic_vf_info_vlanprio(struct hinic_hwdev *hwdev, int vf_id)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	u16 pf_vlan, vlanprio;
	u8 pf_qos;

	pf_vlan = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan;
	pf_qos = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos;
	vlanprio = pf_vlan | pf_qos << HINIC_VLAN_PRIORITY_SHIFT;

	return vlanprio;
}

static int hinic_set_vf_vlan(struct hinic_hwdev *hwdev, bool add, u16 vid,
			     u8 qos, int vf_id)
{
	struct hinic_vf_vlan_config vf_vlan = {0};
	u16 out_size = sizeof(vf_vlan);
	int err;
	u8 cmd;

	/* VLAN 0 is a special case, don't allow it to be removed */
	if (!vid && !add)
		return 0;

	vf_vlan.func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;
	vf_vlan.vlan_id = vid;
	vf_vlan.qos = qos;

	if (add)
		cmd = HINIC_PORT_CMD_SET_VF_VLAN;
	else
		cmd = HINIC_PORT_CMD_CLR_VF_VLAN;

	err = hinic_port_msg_cmd(hwdev, cmd, &vf_vlan,
				 sizeof(vf_vlan), &vf_vlan, &out_size);
	if (err || !out_size || vf_vlan.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set VF %d vlan, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, vf_vlan.status, out_size);
		return -EFAULT;
	}

	return 0;
}

static int hinic_set_vf_tx_rate_max_min(struct hinic_hwdev *hwdev, u16 vf_id,
					u32 max_rate, u32 min_rate)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	struct hinic_tx_rate_cfg_max_min rate_cfg = {0};
	u16 out_size = sizeof(rate_cfg);
	int err;

	rate_cfg.func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;
	rate_cfg.max_rate = max_rate;
	rate_cfg.min_rate = min_rate;
	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_VF_MAX_MIN_RATE,
				 &rate_cfg, sizeof(rate_cfg), &rate_cfg,
				 &out_size);
	if ((rate_cfg.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     rate_cfg.status) || err || !out_size) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set VF(%d) max rate(%d), min rate(%d), err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), max_rate, min_rate, err,
			rate_cfg.status, out_size);
		return -EIO;
	}

	if (!rate_cfg.status) {
		nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].max_rate = max_rate;
		nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].min_rate = min_rate;
	}

	return rate_cfg.status;
}

static int hinic_set_vf_rate_limit(struct hinic_hwdev *hwdev, u16 vf_id,
				   u32 tx_rate)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	struct hinic_tx_rate_cfg rate_cfg = {0};
	u16 out_size = sizeof(rate_cfg);
	int err;

	rate_cfg.func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;
	rate_cfg.tx_rate = tx_rate;
	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_SET_VF_RATE,
				 &rate_cfg, sizeof(rate_cfg), &rate_cfg,
				 &out_size);
	if (err || !out_size || rate_cfg.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set VF(%d) rate(%d), err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), tx_rate, err, rate_cfg.status,
			out_size);
		if (rate_cfg.status)
			return rate_cfg.status;

		return -EIO;
	}

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].max_rate = tx_rate;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].min_rate = 0;

	return 0;
}

static int hinic_set_vf_tx_rate(struct hinic_hwdev *hwdev, u16 vf_id,
				u32 max_rate, u32 min_rate)
{
	int err;

	err = hinic_set_vf_tx_rate_max_min(hwdev, vf_id, max_rate, min_rate);
	if (err != HINIC_MGMT_CMD_UNSUPPORTED)
		return err;

	if (min_rate) {
		dev_err(&hwdev->hwif->pdev->dev, "Current firmware doesn't support to set min tx rate\n");
		return -EOPNOTSUPP;
	}

	dev_info(&hwdev->hwif->pdev->dev, "Current firmware doesn't support to set min tx rate, force min_tx_rate = max_tx_rate\n");

	return hinic_set_vf_rate_limit(hwdev, vf_id, max_rate);
}

static int hinic_init_vf_config(struct hinic_hwdev *hwdev, u16 vf_id)
{
	struct vf_data_storage *vf_info;
	u16 func_id, vlan_id;
	int err = 0;

	vf_info = hwdev->func_to_io.vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_info->pf_set_mac) {
		func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;

		vlan_id = 0;

		err = hinic_set_mac(hwdev, vf_info->vf_mac_addr, vlan_id,
				    func_id);
		if (err) {
			dev_err(&hwdev->func_to_io.hwif->pdev->dev, "Failed to set VF %d MAC\n",
				HW_VF_ID_TO_OS(vf_id));
			return err;
		}
	}

	if (hinic_vf_info_vlanprio(hwdev, vf_id)) {
		err = hinic_set_vf_vlan(hwdev, true, vf_info->pf_vlan,
					vf_info->pf_qos, vf_id);
		if (err) {
			dev_err(&hwdev->hwif->pdev->dev, "Failed to add VF %d VLAN_QOS\n",
				HW_VF_ID_TO_OS(vf_id));
			return err;
		}
	}

	if (vf_info->max_rate) {
		err = hinic_set_vf_tx_rate(hwdev, vf_id, vf_info->max_rate,
					   vf_info->min_rate);
		if (err) {
			dev_err(&hwdev->hwif->pdev->dev, "Failed to set VF %d max rate: %d, min rate: %d\n",
				HW_VF_ID_TO_OS(vf_id), vf_info->max_rate,
				vf_info->min_rate);
			return err;
		}
	}

	return 0;
}

static int hinic_register_vf_msg_handler(void *hwdev, u16 vf_id,
					 void *buf_in, u16 in_size,
					 void *buf_out, u16 *out_size)
{
	struct hinic_register_vf *register_info = buf_out;
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_func_to_io *nic_io;
	int err;

	nic_io = &hw_dev->func_to_io;
	if (vf_id > nic_io->max_vfs) {
		dev_err(&hw_dev->hwif->pdev->dev, "Register VF id %d exceed limit[0-%d]\n",
			HW_VF_ID_TO_OS(vf_id), HW_VF_ID_TO_OS(nic_io->max_vfs));
		register_info->status = EFAULT;
		return -EFAULT;
	}

	*out_size = sizeof(*register_info);
	err = hinic_init_vf_config(hw_dev, vf_id);
	if (err) {
		register_info->status = EFAULT;
		return err;
	}

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].registered = true;

	return 0;
}

static int hinic_unregister_vf_msg_handler(void *hwdev, u16 vf_id,
					   void *buf_in, u16 in_size,
					   void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_func_to_io *nic_io;

	nic_io = &hw_dev->func_to_io;
	*out_size = 0;
	if (vf_id > nic_io->max_vfs)
		return 0;

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].registered = false;

	return 0;
}

static int hinic_change_vf_mtu_msg_handler(void *hwdev, u16 vf_id,
					   void *buf_in, u16 in_size,
					   void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hw_dev = hwdev;
	int err;

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_CHANGE_MTU, buf_in,
				 in_size, buf_out, out_size);
	if (err) {
		dev_err(&hw_dev->hwif->pdev->dev, "Failed to set VF %u mtu\n",
			vf_id);
		return err;
	}

	return 0;
}

static int hinic_get_vf_mac_msg_handler(void *hwdev, u16 vf_id,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct hinic_port_mac_cmd *mac_info = buf_out;
	struct hinic_hwdev *dev = hwdev;
	struct hinic_func_to_io *nic_io;
	struct vf_data_storage *vf_info;

	nic_io = &dev->func_to_io;
	vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);

	memcpy(mac_info->mac, vf_info->vf_mac_addr, ETH_ALEN);
	mac_info->status = 0;
	*out_size = sizeof(*mac_info);

	return 0;
}

static int hinic_set_vf_mac_msg_handler(void *hwdev, u16 vf_id,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct hinic_port_mac_cmd *mac_out = buf_out;
	struct hinic_port_mac_cmd *mac_in = buf_in;
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_func_to_io *nic_io;
	struct vf_data_storage *vf_info;
	int err;

	nic_io =  &hw_dev->func_to_io;
	vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_info->pf_set_mac && !(vf_info->trust) &&
	    is_valid_ether_addr(mac_in->mac)) {
		dev_warn(&hw_dev->hwif->pdev->dev, "PF has already set VF %d MAC address\n",
			 HW_VF_ID_TO_OS(vf_id));
		mac_out->status = HINIC_PF_SET_VF_ALREADY;
		*out_size = sizeof(*mac_out);
		return 0;
	}

	err = hinic_port_msg_cmd(hw_dev, HINIC_PORT_CMD_SET_MAC, buf_in,
				 in_size, buf_out, out_size);
	if ((err &&  err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) || !(*out_size)) {
		dev_err(&hw_dev->hwif->pdev->dev,
			"Failed to set VF %d MAC address, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, mac_out->status, *out_size);
		return -EFAULT;
	}

	return err;
}

static int hinic_del_vf_mac_msg_handler(void *hwdev, u16 vf_id,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct hinic_port_mac_cmd *mac_out = buf_out;
	struct hinic_port_mac_cmd *mac_in = buf_in;
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_func_to_io *nic_io;
	struct vf_data_storage *vf_info;
	int err;

	nic_io = &hw_dev->func_to_io;
	vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_info->pf_set_mac && is_valid_ether_addr(mac_in->mac) &&
	    !memcmp(vf_info->vf_mac_addr, mac_in->mac, ETH_ALEN)) {
		dev_warn(&hw_dev->hwif->pdev->dev, "PF has already set VF mac.\n");
		mac_out->status = HINIC_PF_SET_VF_ALREADY;
		*out_size = sizeof(*mac_out);
		return 0;
	}

	err = hinic_port_msg_cmd(hw_dev, HINIC_PORT_CMD_DEL_MAC, buf_in,
				 in_size, buf_out, out_size);
	if ((err && err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) || !(*out_size)) {
		dev_err(&hw_dev->hwif->pdev->dev, "Failed to delete VF %d MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, mac_out->status, *out_size);
		return -EFAULT;
	}

	return err;
}

static int hinic_get_vf_link_status_msg_handler(void *hwdev, u16 vf_id,
						void *buf_in, u16 in_size,
						void *buf_out, u16 *out_size)
{
	struct hinic_port_link_cmd *get_link = buf_out;
	struct hinic_hwdev *hw_dev = hwdev;
	struct vf_data_storage *vf_infos;
	struct hinic_func_to_io *nic_io;
	bool link_forced, link_up;

	nic_io = &hw_dev->func_to_io;
	vf_infos = nic_io->vf_infos;
	link_forced = vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced;
	link_up = vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up;

	if (link_forced)
		get_link->state = link_up ?
			HINIC_LINK_STATE_UP : HINIC_LINK_STATE_DOWN;
	else
		get_link->state = nic_io->link_status;

	get_link->status = 0;
	*out_size = sizeof(*get_link);

	return 0;
}

static bool check_func_table(struct hinic_hwdev *hwdev, u16 func_idx,
			     void *buf_in, u16 in_size)
{
	struct hinic_cmd_fw_ctxt *function_table = buf_in;

	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size) ||
	    !function_table->rx_buf_sz)
		return false;

	return true;
}

static struct vf_cmd_msg_handle nic_vf_cmd_msg_handler[] = {
	{HINIC_PORT_CMD_VF_REGISTER, hinic_register_vf_msg_handler},
	{HINIC_PORT_CMD_VF_UNREGISTER, hinic_unregister_vf_msg_handler},
	{HINIC_PORT_CMD_CHANGE_MTU, hinic_change_vf_mtu_msg_handler},
	{HINIC_PORT_CMD_GET_MAC, hinic_get_vf_mac_msg_handler},
	{HINIC_PORT_CMD_SET_MAC, hinic_set_vf_mac_msg_handler},
	{HINIC_PORT_CMD_DEL_MAC, hinic_del_vf_mac_msg_handler},
	{HINIC_PORT_CMD_GET_LINK_STATE, hinic_get_vf_link_status_msg_handler},
};

static struct vf_cmd_check_handle nic_cmd_support_vf[] = {
	{HINIC_PORT_CMD_VF_REGISTER, NULL},
	{HINIC_PORT_CMD_VF_UNREGISTER, NULL},
	{HINIC_PORT_CMD_CHANGE_MTU, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_ADD_VLAN, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_DEL_VLAN, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_DEL_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RX_MODE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_PAUSE_INFO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_LINK_STATE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_LRO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RX_CSUM, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RX_VLAN_OFFLOAD, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_VPORT_STAT, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_CLEAN_VPORT_STAT, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_TEMPLATE_INDIR_TBL,
	 hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_TEMPLATE_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_TEMPLATE_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_HASH_ENGINE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_HASH_ENGINE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_CTX_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_CTX_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_RSS_TEMP_MGR, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_RSS_CFG, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_FWCTXT_INIT, check_func_table},
	{HINIC_PORT_CMD_GET_MGMT_VERSION, NULL},
	{HINIC_PORT_CMD_SET_FUNC_STATE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_GLOBAL_QPN, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_TSO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RQ_IQ_MAP, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_LINK_STATUS_REPORT, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_UPDATE_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_CAP, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_LINK_MODE, hinic_mbox_check_func_id_8B},
};

#define CHECK_IPSU_15BIT	0X8000

static
struct hinic_sriov_info *hinic_get_sriov_info_by_pcidev(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	return &nic_dev->sriov_info;
}

static int hinic_check_mac_info(u8 status, u16 vlan_id)
{
	if ((status && status != HINIC_MGMT_STATUS_EXIST) ||
	    (vlan_id & CHECK_IPSU_15BIT &&
	     status == HINIC_MGMT_STATUS_EXIST))
		return -EINVAL;

	return 0;
}

#define HINIC_VLAN_ID_MASK	0x7FFF

static int hinic_update_mac(struct hinic_hwdev *hwdev, u8 *old_mac,
			    u8 *new_mac, u16 vlan_id, u16 func_id)
{
	struct hinic_port_mac_update mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !old_mac || !new_mac)
		return -EINVAL;

	if ((vlan_id & HINIC_VLAN_ID_MASK) >= VLAN_N_VID) {
		dev_err(&hwdev->hwif->pdev->dev, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.old_mac, old_mac, ETH_ALEN);
	memcpy(mac_info.new_mac, new_mac, ETH_ALEN);

	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_UPDATE_MAC, &mac_info,
				 sizeof(mac_info), &mac_info, &out_size);

	if (err || !out_size ||
	    hinic_check_mac_info(mac_info.status, mac_info.vlan_id)) {
		dev_err(&hwdev->hwif->pdev->dev,
			"Failed to update MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EINVAL;
	}

	if (mac_info.status == HINIC_MGMT_STATUS_EXIST)
		dev_warn(&hwdev->hwif->pdev->dev, "MAC is repeated. Ignore update operation\n");

	return 0;
}

static void hinic_get_vf_config(struct hinic_hwdev *hwdev, u16 vf_id,
				struct ifla_vf_info *ivi)
{
	struct vf_data_storage *vfinfo;

	vfinfo = hwdev->func_to_io.vf_infos + HW_VF_ID_TO_OS(vf_id);

	ivi->vf = HW_VF_ID_TO_OS(vf_id);
	memcpy(ivi->mac, vfinfo->vf_mac_addr, ETH_ALEN);
	ivi->vlan = vfinfo->pf_vlan;
	ivi->qos = vfinfo->pf_qos;
	ivi->spoofchk = vfinfo->spoofchk;
	ivi->trusted = vfinfo->trust;
	ivi->max_tx_rate = vfinfo->max_rate;
	ivi->min_tx_rate = vfinfo->min_rate;

	if (!vfinfo->link_forced)
		ivi->linkstate = IFLA_VF_LINK_STATE_AUTO;
	else if (vfinfo->link_up)
		ivi->linkstate = IFLA_VF_LINK_STATE_ENABLE;
	else
		ivi->linkstate = IFLA_VF_LINK_STATE_DISABLE;
}

int hinic_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;

	sriov_info = &nic_dev->sriov_info;
	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	hinic_get_vf_config(sriov_info->hwdev, OS_VF_ID_TO_HW(vf), ivi);

	return 0;
}

static int hinic_set_vf_mac(struct hinic_hwdev *hwdev, int vf,
			    unsigned char *mac_addr)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	struct vf_data_storage *vf_info;
	u16 func_id;
	int err;

	vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);

	/* duplicate request, so just return success */
	if (vf_info->pf_set_mac &&
	    !memcmp(vf_info->vf_mac_addr, mac_addr, ETH_ALEN))
		return 0;

	vf_info->pf_set_mac = true;

	func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf;
	err = hinic_update_mac(hwdev, vf_info->vf_mac_addr,
			       mac_addr, 0, func_id);
	if (err) {
		vf_info->pf_set_mac = false;
		return err;
	}

	memcpy(vf_info->vf_mac_addr, mac_addr, ETH_ALEN);

	return 0;
}

int hinic_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	int err;

	sriov_info = &nic_dev->sriov_info;
	if (!is_valid_ether_addr(mac) || vf >= sriov_info->num_vfs)
		return -EINVAL;

	err = hinic_set_vf_mac(sriov_info->hwdev, OS_VF_ID_TO_HW(vf), mac);
	if (err)
		return err;

	netif_info(nic_dev, drv, netdev, "Setting MAC %pM on VF %d\n", mac, vf);
	netif_info(nic_dev, drv, netdev, "Reload the VF driver to make this change effective.");

	return 0;
}

static int hinic_add_vf_vlan(struct hinic_hwdev *hwdev, int vf_id,
			     u16 vlan, u8 qos)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	int err;

	err = hinic_set_vf_vlan(hwdev, true, vlan, qos, vf_id);
	if (err)
		return err;

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan = vlan;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos = qos;

	dev_info(&hwdev->hwif->pdev->dev, "Setting VLAN %d, QOS 0x%x on VF %d\n",
		 vlan, qos, HW_VF_ID_TO_OS(vf_id));
	return 0;
}

static int hinic_kill_vf_vlan(struct hinic_hwdev *hwdev, int vf_id)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	int err;

	err = hinic_set_vf_vlan(hwdev, false,
				nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan,
				nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos,
				vf_id);
	if (err)
		return err;

	dev_info(&hwdev->hwif->pdev->dev, "Remove VLAN %d on VF %d\n",
		 nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan,
		 HW_VF_ID_TO_OS(vf_id));

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan = 0;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos = 0;

	return 0;
}

static int hinic_update_mac_vlan(struct hinic_dev *nic_dev, u16 old_vlan,
				 u16 new_vlan, int vf_id)
{
	struct vf_data_storage *vf_info;
	u16 vlan_id;
	int err;

	if (!nic_dev || old_vlan >= VLAN_N_VID || new_vlan >= VLAN_N_VID)
		return -EINVAL;

	vf_info = nic_dev->hwdev->func_to_io.vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (!vf_info->pf_set_mac)
		return 0;

	vlan_id = old_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;

	err = hinic_port_del_mac(nic_dev, vf_info->vf_mac_addr, vlan_id);
	if (err) {
		dev_err(&nic_dev->hwdev->hwif->pdev->dev, "Failed to delete VF %d MAC %pM vlan %d\n",
			HW_VF_ID_TO_OS(vf_id), vf_info->vf_mac_addr, old_vlan);
		return err;
	}

	vlan_id = new_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;

	err = hinic_port_add_mac(nic_dev, vf_info->vf_mac_addr, vlan_id);
	if (err) {
		dev_err(&nic_dev->hwdev->hwif->pdev->dev, "Failed to add VF %d MAC %pM vlan %d\n",
			HW_VF_ID_TO_OS(vf_id), vf_info->vf_mac_addr, new_vlan);
		goto out;
	}

	return 0;

out:
	vlan_id = old_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;
	hinic_port_add_mac(nic_dev, vf_info->vf_mac_addr, vlan_id);

	return err;
}

static int set_hw_vf_vlan(struct hinic_dev *nic_dev,
			  u16 cur_vlanprio, int vf, u16 vlan, u8 qos)
{
	u16 old_vlan = cur_vlanprio & VLAN_VID_MASK;
	int err = 0;

	if (vlan || qos) {
		if (cur_vlanprio) {
			err = hinic_kill_vf_vlan(nic_dev->hwdev,
						 OS_VF_ID_TO_HW(vf));
			if (err) {
				dev_err(&nic_dev->sriov_info.pdev->dev, "Failed to delete vf %d old vlan %d\n",
					vf, old_vlan);
				goto out;
			}
		}
		err = hinic_add_vf_vlan(nic_dev->hwdev,
					OS_VF_ID_TO_HW(vf), vlan, qos);
		if (err) {
			dev_err(&nic_dev->sriov_info.pdev->dev, "Failed to add vf %d new vlan %d\n",
				vf, vlan);
			goto out;
		}
	} else {
		err = hinic_kill_vf_vlan(nic_dev->hwdev, OS_VF_ID_TO_HW(vf));
		if (err) {
			dev_err(&nic_dev->sriov_info.pdev->dev, "Failed to delete vf %d vlan %d\n",
				vf, old_vlan);
			goto out;
		}
	}

	err = hinic_update_mac_vlan(nic_dev, old_vlan, vlan,
				    OS_VF_ID_TO_HW(vf));

out:
	return err;
}

int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			  __be16 vlan_proto)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	u16 vlanprio, cur_vlanprio;

	sriov_info = &nic_dev->sriov_info;
	if (vf >= sriov_info->num_vfs || vlan > 4095 || qos > 7)
		return -EINVAL;
	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;
	vlanprio = vlan | qos << HINIC_VLAN_PRIORITY_SHIFT;
	cur_vlanprio = hinic_vf_info_vlanprio(nic_dev->hwdev,
					      OS_VF_ID_TO_HW(vf));
	/* duplicate request, so just return success */
	if (vlanprio == cur_vlanprio)
		return 0;

	return set_hw_vf_vlan(nic_dev, cur_vlanprio, vf, vlan, qos);
}

static int hinic_set_vf_trust(struct hinic_hwdev *hwdev, u16 vf_id,
			      bool trust)
{
	struct vf_data_storage *vf_infos;
	struct hinic_func_to_io *nic_io;

	if (!hwdev)
		return -EINVAL;

	nic_io = &hwdev->func_to_io;
	vf_infos = nic_io->vf_infos;
	vf_infos[vf_id].trust = trust;

	return 0;
}

int hinic_ndo_set_vf_trust(struct net_device *netdev, int vf, bool setting)
{
	struct hinic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	struct hinic_func_to_io *nic_io;
	bool cur_trust;
	int err;

	sriov_info = &adapter->sriov_info;
	nic_io = &adapter->hwdev->func_to_io;

	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	cur_trust = nic_io->vf_infos[vf].trust;
	/* same request, so just return success */
	if ((setting && cur_trust) || (!setting && !cur_trust))
		return 0;

	err = hinic_set_vf_trust(adapter->hwdev, vf, setting);
	if (!err)
		dev_info(&sriov_info->pdev->dev, "Set VF %d trusted %s succeed\n",
			 vf, setting ? "on" : "off");
	else
		dev_err(&sriov_info->pdev->dev, "Failed set VF %d trusted %s\n",
			vf, setting ? "on" : "off");

	return err;
}

int hinic_ndo_set_vf_bw(struct net_device *netdev,
			int vf, int min_tx_rate, int max_tx_rate)
{
	static const u32 speeds[] = {
		SPEED_10, SPEED_100, SPEED_1000, SPEED_10000,
		SPEED_25000, SPEED_40000, SPEED_100000
	};
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_port_cap port_cap = { 0 };
	enum hinic_port_link_state link_state;
	int err;

	if (vf >= nic_dev->sriov_info.num_vfs) {
		netif_err(nic_dev, drv, netdev, "VF number must be less than %d\n",
			  nic_dev->sriov_info.num_vfs);
		return -EINVAL;
	}

	if (max_tx_rate < min_tx_rate) {
		netif_err(nic_dev, drv, netdev, "Max rate %d must be greater than or equal to min rate %d\n",
			  max_tx_rate, min_tx_rate);
		return -EINVAL;
	}

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Get link status failed when setting vf tx rate\n");
		return -EIO;
	}

	if (link_state == HINIC_LINK_STATE_DOWN) {
		netif_err(nic_dev, drv, netdev,
			  "Link status must be up when setting vf tx rate\n");
		return -EPERM;
	}

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err || port_cap.speed > LINK_SPEED_100GB)
		return -EIO;

	/* rate limit cannot be less than 0 and greater than link speed */
	if (max_tx_rate < 0 || max_tx_rate > speeds[port_cap.speed]) {
		netif_err(nic_dev, drv, netdev, "Max tx rate must be in [0 - %d]\n",
			  speeds[port_cap.speed]);
		return -EINVAL;
	}

	err = hinic_set_vf_tx_rate(nic_dev->hwdev, OS_VF_ID_TO_HW(vf),
				   max_tx_rate, min_tx_rate);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Unable to set VF %d max rate %d min rate %d%s\n",
			  vf, max_tx_rate, min_tx_rate,
			  err == HINIC_TX_RATE_TABLE_FULL ?
			  ", tx rate profile is full" : "");
		return -EIO;
	}

	netif_info(nic_dev, drv, netdev,
		   "Set VF %d max tx rate %d min tx rate %d successfully\n",
		   vf, max_tx_rate, min_tx_rate);

	return 0;
}

static int hinic_set_vf_spoofchk(struct hinic_hwdev *hwdev, u16 vf_id,
				 bool spoofchk)
{
	struct hinic_spoofchk_set spoofchk_cfg = {0};
	struct vf_data_storage *vf_infos = NULL;
	u16 out_size = sizeof(spoofchk_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	vf_infos = hwdev->func_to_io.vf_infos;

	spoofchk_cfg.func_id = hinic_glb_pf_vf_offset(hwdev->hwif) + vf_id;
	spoofchk_cfg.state = spoofchk ? 1 : 0;
	err = hinic_port_msg_cmd(hwdev, HINIC_PORT_CMD_ENABLE_SPOOFCHK,
				 &spoofchk_cfg, sizeof(spoofchk_cfg),
				 &spoofchk_cfg, &out_size);
	if (spoofchk_cfg.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if (err || !out_size || spoofchk_cfg.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set VF(%d) spoofchk, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, spoofchk_cfg.status,
			out_size);
		err = -EIO;
	}

	vf_infos[HW_VF_ID_TO_OS(vf_id)].spoofchk = spoofchk;

	return err;
}

int hinic_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	bool cur_spoofchk;
	int err;

	sriov_info = &nic_dev->sriov_info;
	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	cur_spoofchk = nic_dev->hwdev->func_to_io.vf_infos[vf].spoofchk;

	/* same request, so just return success */
	if ((setting && cur_spoofchk) || (!setting && !cur_spoofchk))
		return 0;

	err = hinic_set_vf_spoofchk(sriov_info->hwdev,
				    OS_VF_ID_TO_HW(vf), setting);
	if (!err) {
		netif_info(nic_dev, drv, netdev, "Set VF %d spoofchk %s successfully\n",
			   vf, setting ? "on" : "off");
	} else if (err == HINIC_MGMT_CMD_UNSUPPORTED) {
		netif_err(nic_dev, drv, netdev,
			  "Current firmware doesn't support to set vf spoofchk, need to upgrade latest firmware version\n");
		err = -EOPNOTSUPP;
	}

	return err;
}

static int hinic_set_vf_link_state(struct hinic_hwdev *hwdev, u16 vf_id,
				   int link)
{
	struct hinic_func_to_io *nic_io = &hwdev->func_to_io;
	struct vf_data_storage *vf_infos = nic_io->vf_infos;
	u8 link_status = 0;

	switch (link) {
	case HINIC_IFLA_VF_LINK_STATE_AUTO:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = false;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = nic_io->link_status ?
			true : false;
		link_status = nic_io->link_status;
		break;
	case HINIC_IFLA_VF_LINK_STATE_ENABLE:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = true;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = true;
		link_status = HINIC_LINK_UP;
		break;
	case HINIC_IFLA_VF_LINK_STATE_DISABLE:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = true;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = false;
		link_status = HINIC_LINK_DOWN;
		break;
	default:
		return -EINVAL;
	}

	/* Notify the VF of its new link state */
	hinic_notify_vf_link_status(hwdev, vf_id, link_status);

	return 0;
}

int hinic_ndo_set_vf_link_state(struct net_device *netdev, int vf_id, int link)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;

	sriov_info = &nic_dev->sriov_info;

	if (vf_id >= sriov_info->num_vfs) {
		netif_err(nic_dev, drv, netdev,
			  "Invalid VF Identifier %d\n", vf_id);
		return -EINVAL;
	}

	return hinic_set_vf_link_state(sriov_info->hwdev,
				      OS_VF_ID_TO_HW(vf_id), link);
}

/* pf receive message from vf */
static int nic_pf_mbox_handler(void *hwdev, u16 vf_id, u8 cmd, void *buf_in,
			       u16 in_size, void *buf_out, u16 *out_size)
{
	u8 size = ARRAY_SIZE(nic_cmd_support_vf);
	struct vf_cmd_msg_handle *vf_msg_handle;
	struct hinic_hwdev *dev = hwdev;
	struct hinic_func_to_io *nic_io;
	struct hinic_pfhwdev *pfhwdev;
	int err = 0;
	u32 i;

	if (!hwdev)
		return -EINVAL;

	if (!hinic_mbox_check_cmd_valid(hwdev, nic_cmd_support_vf, vf_id, cmd,
					buf_in, in_size, size)) {
		dev_err(&dev->hwif->pdev->dev,
			"PF Receive VF nic cmd: 0x%x, mbox len: 0x%x is invalid\n",
			cmd, in_size);
		return HINIC_MBOX_VF_CMD_ERROR;
	}

	pfhwdev = container_of(dev, struct hinic_pfhwdev, hwdev);
	nic_io = &dev->func_to_io;
	for (i = 0; i < ARRAY_SIZE(nic_vf_cmd_msg_handler); i++) {
		vf_msg_handle = &nic_vf_cmd_msg_handler[i];
		if (cmd == vf_msg_handle->cmd &&
		    vf_msg_handle->cmd_msg_handler) {
			err = vf_msg_handle->cmd_msg_handler(hwdev, vf_id,
							     buf_in, in_size,
							     buf_out,
							     out_size);
			break;
		}
	}
	if (i == ARRAY_SIZE(nic_vf_cmd_msg_handler))
		err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_L2NIC,
					cmd, buf_in, in_size, buf_out,
					out_size, HINIC_MGMT_MSG_SYNC);

	if (err &&  err != HINIC_MBOX_PF_BUSY_ACTIVE_FW)
		dev_err(&nic_io->hwif->pdev->dev, "PF receive VF L2NIC cmd: %d process error, err:%d\n",
			cmd, err);
	return err;
}

static int cfg_mbx_pf_proc_vf_msg(void *hwdev, u16 vf_id, u8 cmd, void *buf_in,
				  u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_dev_cap *dev_cap = buf_out;
	struct hinic_hwdev *dev = hwdev;
	struct hinic_cap *cap;

	cap = &dev->nic_cap;
	memset(dev_cap, 0, sizeof(*dev_cap));

	dev_cap->max_vf = cap->max_vf;
	dev_cap->max_sqs = cap->max_vf_qps;
	dev_cap->max_rqs = cap->max_vf_qps;
	dev_cap->port_id = dev->port_id;

	*out_size = sizeof(*dev_cap);

	return 0;
}

static int hinic_init_vf_infos(struct hinic_func_to_io *nic_io, u16 vf_id)
{
	struct vf_data_storage *vf_infos = nic_io->vf_infos;

	if (set_vf_link_state > HINIC_IFLA_VF_LINK_STATE_DISABLE) {
		dev_warn(&nic_io->hwif->pdev->dev, "Module Parameter set_vf_link_state value %d is out of range, resetting to %d\n",
			 set_vf_link_state, HINIC_IFLA_VF_LINK_STATE_AUTO);
		set_vf_link_state = HINIC_IFLA_VF_LINK_STATE_AUTO;
	}

	switch (set_vf_link_state) {
	case HINIC_IFLA_VF_LINK_STATE_AUTO:
		vf_infos[vf_id].link_forced = false;
		break;
	case HINIC_IFLA_VF_LINK_STATE_ENABLE:
		vf_infos[vf_id].link_forced = true;
		vf_infos[vf_id].link_up = true;
		break;
	case HINIC_IFLA_VF_LINK_STATE_DISABLE:
		vf_infos[vf_id].link_forced = true;
		vf_infos[vf_id].link_up = false;
		break;
	default:
		dev_err(&nic_io->hwif->pdev->dev, "Invalid input parameter set_vf_link_state: %d\n",
			set_vf_link_state);
		return -EINVAL;
	}

	return 0;
}

static void hinic_clear_vf_infos(struct hinic_dev *nic_dev, u16 vf_id)
{
	struct vf_data_storage *vf_infos;

	vf_infos = nic_dev->hwdev->func_to_io.vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_infos->pf_set_mac)
		hinic_port_del_mac(nic_dev, vf_infos->vf_mac_addr, 0);

	if (hinic_vf_info_vlanprio(nic_dev->hwdev, vf_id))
		hinic_kill_vf_vlan(nic_dev->hwdev, vf_id);

	if (vf_infos->max_rate)
		hinic_set_vf_tx_rate(nic_dev->hwdev, vf_id, 0, 0);

	if (vf_infos->spoofchk)
		hinic_set_vf_spoofchk(nic_dev->hwdev, vf_id, false);

	if (vf_infos->trust)
		hinic_set_vf_trust(nic_dev->hwdev, vf_id, false);

	memset(vf_infos, 0, sizeof(*vf_infos));
	/* set vf_infos to default */
	hinic_init_vf_infos(&nic_dev->hwdev->func_to_io, HW_VF_ID_TO_OS(vf_id));
}

static int hinic_deinit_vf_hw(struct hinic_sriov_info *sriov_info,
			      u16 start_vf_id, u16 end_vf_id)
{
	struct hinic_dev *nic_dev;
	u16 func_idx, idx;

	nic_dev = container_of(sriov_info, struct hinic_dev, sriov_info);

	for (idx = start_vf_id; idx <= end_vf_id; idx++) {
		func_idx = hinic_glb_pf_vf_offset(nic_dev->hwdev->hwif) + idx;
		hinic_set_wq_page_size(nic_dev->hwdev, func_idx,
				       HINIC_HW_WQ_PAGE_SIZE);
		hinic_clear_vf_infos(nic_dev, idx);
	}

	return 0;
}

int hinic_vf_func_init(struct hinic_hwdev *hwdev)
{
	struct hinic_register_vf register_info = {0};
	u16 out_size = sizeof(register_info);
	struct hinic_func_to_io *nic_io;
	int err = 0;
	u32 size, i;

	err = hinic_vf_mbox_random_id_init(hwdev);
	if (err) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to init vf mbox random id, err: %d\n",
			err);
		return err;
	}

	nic_io = &hwdev->func_to_io;

	if (HINIC_IS_VF(hwdev->hwif)) {
		err = hinic_mbox_to_pf(hwdev, HINIC_MOD_L2NIC,
				       HINIC_PORT_CMD_VF_REGISTER,
				       &register_info, sizeof(register_info),
				       &register_info, &out_size, 0);
		if (err || register_info.status || !out_size) {
			dev_err(&hwdev->hwif->pdev->dev,
				"Failed to register VF, err: %d, status: 0x%x, out size: 0x%x\n",
				err, register_info.status, out_size);
			return -EIO;
		}
	} else {
		err = hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_CFGM,
						cfg_mbx_pf_proc_vf_msg);
		if (err) {
			dev_err(&hwdev->hwif->pdev->dev,
				"Register PF mailbox callback failed\n");
			return err;
		}
		nic_io->max_vfs = hwdev->nic_cap.max_vf;
		size = sizeof(*nic_io->vf_infos) * nic_io->max_vfs;
		if (size != 0) {
			nic_io->vf_infos = kzalloc(size, GFP_KERNEL);
			if (!nic_io->vf_infos) {
				err = -ENOMEM;
				goto out_free_nic_io;
			}

			for (i = 0; i < nic_io->max_vfs; i++) {
				err = hinic_init_vf_infos(nic_io, i);
				if (err)
					goto err_init_vf_infos;
			}

			err = hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_L2NIC,
							nic_pf_mbox_handler);
			if (err)
				goto err_register_pf_mbox_cb;
		}
	}

	return 0;

err_register_pf_mbox_cb:
err_init_vf_infos:
	kfree(nic_io->vf_infos);
out_free_nic_io:
	return err;
}

void hinic_vf_func_free(struct hinic_hwdev *hwdev)
{
	struct hinic_register_vf unregister = {0};
	u16 out_size = sizeof(unregister);
	int err;

	if (HINIC_IS_VF(hwdev->hwif)) {
		err = hinic_mbox_to_pf(hwdev, HINIC_MOD_L2NIC,
				       HINIC_PORT_CMD_VF_UNREGISTER,
				       &unregister, sizeof(unregister),
				       &unregister, &out_size, 0);
		if (err || !out_size || unregister.status)
			dev_err(&hwdev->hwif->pdev->dev, "Failed to unregister VF, err: %d, status: 0x%x, out_size: 0x%x\n",
				err, unregister.status, out_size);
	} else {
		if (hwdev->func_to_io.vf_infos) {
			hinic_unregister_pf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
			kfree(hwdev->func_to_io.vf_infos);
		}
	}
}

static int hinic_init_vf_hw(struct hinic_hwdev *hwdev, u16 start_vf_id,
			    u16 end_vf_id)
{
	u16 i, func_idx;
	int err;

	/* vf use 256K as default wq page size, and can't change it */
	for (i = start_vf_id; i <= end_vf_id; i++) {
		func_idx = hinic_glb_pf_vf_offset(hwdev->hwif) + i;
		err = hinic_set_wq_page_size(hwdev, func_idx,
					     HINIC_DEFAULT_WQ_PAGE_SIZE);
		if (err)
			return err;
	}

	return 0;
}

int hinic_pci_sriov_disable(struct pci_dev *pdev)
{
	struct hinic_sriov_info *sriov_info;
	u16 tmp_vfs;

	sriov_info = hinic_get_sriov_info_by_pcidev(pdev);
	/* if SR-IOV is already disabled then nothing will be done */
	if (!sriov_info->sriov_enabled)
		return 0;

	set_bit(HINIC_SRIOV_DISABLE, &sriov_info->state);

	/* If our VFs are assigned we cannot shut down SR-IOV
	 * without causing issues, so just leave the hardware
	 * available but disabled
	 */
	if (pci_vfs_assigned(sriov_info->pdev)) {
		clear_bit(HINIC_SRIOV_DISABLE, &sriov_info->state);
		dev_warn(&pdev->dev, "Unloading driver while VFs are assigned - VFs will not be deallocated\n");
		return -EPERM;
	}
	sriov_info->sriov_enabled = false;

	/* disable iov and allow time for transactions to clear */
	pci_disable_sriov(sriov_info->pdev);

	tmp_vfs = (u16)sriov_info->num_vfs;
	sriov_info->num_vfs = 0;
	hinic_deinit_vf_hw(sriov_info, OS_VF_ID_TO_HW(0),
			   OS_VF_ID_TO_HW(tmp_vfs - 1));

	clear_bit(HINIC_SRIOV_DISABLE, &sriov_info->state);

	return 0;
}

int hinic_pci_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct hinic_sriov_info *sriov_info;
	int err;

	sriov_info = hinic_get_sriov_info_by_pcidev(pdev);

	if (test_and_set_bit(HINIC_SRIOV_ENABLE, &sriov_info->state)) {
		dev_err(&pdev->dev,
			"SR-IOV enable in process, please wait, num_vfs %d\n",
			num_vfs);
		return -EPERM;
	}

	err = hinic_init_vf_hw(sriov_info->hwdev, OS_VF_ID_TO_HW(0),
			       OS_VF_ID_TO_HW((u16)num_vfs - 1));
	if (err) {
		dev_err(&sriov_info->pdev->dev,
			"Failed to init vf in hardware before enable sriov, error %d\n",
			err);
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return err;
	}

	err = pci_enable_sriov(sriov_info->pdev, num_vfs);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to enable SR-IOV, error %d\n", err);
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return err;
	}

	sriov_info->sriov_enabled = true;
	sriov_info->num_vfs = num_vfs;
	clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);

	return num_vfs;
}

int hinic_pci_sriov_configure(struct pci_dev *dev, int num_vfs)
{
	struct hinic_sriov_info *sriov_info;

	sriov_info = hinic_get_sriov_info_by_pcidev(dev);

	if (test_bit(HINIC_FUNC_REMOVE, &sriov_info->state))
		return -EBUSY;

	if (!num_vfs)
		return hinic_pci_sriov_disable(dev);
	else
		return hinic_pci_sriov_enable(dev, num_vfs);
}
