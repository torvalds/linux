/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic_sriov.h"
#include "qlcnic.h"
#include <linux/types.h>

#define QLCNIC_SRIOV_VF_MAX_MAC 1
#define QLC_VF_MIN_TX_RATE	100
#define QLC_VF_MAX_TX_RATE	9999

static int qlcnic_sriov_pf_get_vport_handle(struct qlcnic_adapter *, u8);

struct qlcnic_sriov_cmd_handler {
	int (*fn) (struct qlcnic_bc_trans *, struct qlcnic_cmd_args *);
};

struct qlcnic_sriov_fw_cmd_handler {
	u32 cmd;
	int (*fn) (struct qlcnic_bc_trans *, struct qlcnic_cmd_args *);
};

static int qlcnic_sriov_pf_set_vport_info(struct qlcnic_adapter *adapter,
					  struct qlcnic_info *npar_info,
					  u16 vport_id)
{
	struct qlcnic_cmd_args cmd;
	int err;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_NIC_INFO))
		return -ENOMEM;

	cmd.req.arg[1] = (vport_id << 16) | 0x1;
	cmd.req.arg[2] = npar_info->bit_offsets;
	cmd.req.arg[2] |= npar_info->min_tx_bw << 16;
	cmd.req.arg[3] = npar_info->max_tx_bw | (npar_info->max_tx_ques << 16);
	cmd.req.arg[4] = npar_info->max_tx_mac_filters;
	cmd.req.arg[4] |= npar_info->max_rx_mcast_mac_filters << 16;
	cmd.req.arg[5] = npar_info->max_rx_ucast_mac_filters |
			 (npar_info->max_rx_ip_addr << 16);
	cmd.req.arg[6] = npar_info->max_rx_lro_flow |
			 (npar_info->max_rx_status_rings << 16);
	cmd.req.arg[7] = npar_info->max_rx_buf_rings |
			 (npar_info->max_rx_ques << 16);
	cmd.req.arg[8] = npar_info->max_tx_vlan_keys;
	cmd.req.arg[8] |= npar_info->max_local_ipv6_addrs << 16;
	cmd.req.arg[9] = npar_info->max_remote_ipv6_addrs;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to set vport info, err=%d\n", err);

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_pf_cal_res_limit(struct qlcnic_adapter *adapter,
					 struct qlcnic_info *info, u16 func)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_resources *res = &sriov->ff_max;
	u32 temp, num_vf_macs, num_vfs, max;
	int ret = -EIO, vpid, id;
	struct qlcnic_vport *vp;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter, func);
	if (vpid < 0)
		return -EINVAL;

	num_vfs = sriov->num_vfs;
	max = num_vfs + 1;
	info->bit_offsets = 0xffff;
	info->max_tx_ques = res->num_tx_queues / max;
	info->max_rx_mcast_mac_filters = res->num_rx_mcast_mac_filters;
	num_vf_macs = QLCNIC_SRIOV_VF_MAX_MAC;

	if (adapter->ahw->pci_func == func) {
		temp = res->num_rx_mcast_mac_filters - (num_vfs * num_vf_macs);
		info->max_rx_ucast_mac_filters = temp;
		temp = res->num_tx_mac_filters - (num_vfs * num_vf_macs);
		info->max_tx_mac_filters = temp;
		info->min_tx_bw = 0;
		info->max_tx_bw = MAX_BW;
	} else {
		id = qlcnic_sriov_func_to_index(adapter, func);
		if (id < 0)
			return id;
		vp = sriov->vf_info[id].vp;
		info->min_tx_bw = vp->min_tx_bw;
		info->max_tx_bw = vp->max_tx_bw;
		info->max_rx_ucast_mac_filters = num_vf_macs;
		info->max_tx_mac_filters = num_vf_macs;
	}

	info->max_rx_ip_addr = res->num_destip / max;
	info->max_rx_status_rings = res->num_rx_status_rings / max;
	info->max_rx_buf_rings = res->num_rx_buf_rings / max;
	info->max_rx_ques = res->num_rx_queues / max;
	info->max_rx_lro_flow = res->num_lro_flows_supported / max;
	info->max_tx_vlan_keys = res->num_txvlan_keys;
	info->max_local_ipv6_addrs = res->max_local_ipv6_addrs;
	info->max_remote_ipv6_addrs = res->max_remote_ipv6_addrs;

	ret = qlcnic_sriov_pf_set_vport_info(adapter, info, vpid);
	if (ret)
		return ret;

	return 0;
}

static void qlcnic_sriov_pf_set_ff_max_res(struct qlcnic_adapter *adapter,
					   struct qlcnic_info *info)
{
	struct qlcnic_resources *ff_max = &adapter->ahw->sriov->ff_max;

	ff_max->num_tx_mac_filters = info->max_tx_mac_filters;
	ff_max->num_rx_ucast_mac_filters = info->max_rx_ucast_mac_filters;
	ff_max->num_rx_mcast_mac_filters = info->max_rx_mcast_mac_filters;
	ff_max->num_txvlan_keys = info->max_tx_vlan_keys;
	ff_max->num_rx_queues = info->max_rx_ques;
	ff_max->num_tx_queues = info->max_tx_ques;
	ff_max->num_lro_flows_supported = info->max_rx_lro_flow;
	ff_max->num_destip = info->max_rx_ip_addr;
	ff_max->num_rx_buf_rings = info->max_rx_buf_rings;
	ff_max->num_rx_status_rings = info->max_rx_status_rings;
	ff_max->max_remote_ipv6_addrs = info->max_remote_ipv6_addrs;
	ff_max->max_local_ipv6_addrs = info->max_local_ipv6_addrs;
}

static int qlcnic_sriov_get_pf_info(struct qlcnic_adapter *adapter,
				    struct qlcnic_info *npar_info)
{
	int err;
	struct qlcnic_cmd_args cmd;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_NIC_INFO))
		return -ENOMEM;

	cmd.req.arg[1] = 0x2;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to get PF info, err=%d\n", err);
		goto out;
	}

	npar_info->total_pf = cmd.rsp.arg[2] & 0xff;
	npar_info->total_rss_engines = (cmd.rsp.arg[2] >> 8) & 0xff;
	npar_info->max_vports = MSW(cmd.rsp.arg[2]);
	npar_info->max_tx_ques =  LSW(cmd.rsp.arg[3]);
	npar_info->max_tx_mac_filters = MSW(cmd.rsp.arg[3]);
	npar_info->max_rx_mcast_mac_filters = LSW(cmd.rsp.arg[4]);
	npar_info->max_rx_ucast_mac_filters = MSW(cmd.rsp.arg[4]);
	npar_info->max_rx_ip_addr = LSW(cmd.rsp.arg[5]);
	npar_info->max_rx_lro_flow = MSW(cmd.rsp.arg[5]);
	npar_info->max_rx_status_rings = LSW(cmd.rsp.arg[6]);
	npar_info->max_rx_buf_rings = MSW(cmd.rsp.arg[6]);
	npar_info->max_rx_ques = LSW(cmd.rsp.arg[7]);
	npar_info->max_tx_vlan_keys = MSW(cmd.rsp.arg[7]);
	npar_info->max_local_ipv6_addrs = LSW(cmd.rsp.arg[8]);
	npar_info->max_remote_ipv6_addrs = MSW(cmd.rsp.arg[8]);

	qlcnic_sriov_pf_set_ff_max_res(adapter, npar_info);
	dev_info(&adapter->pdev->dev,
		 "\n\ttotal_pf: %d,\n"
		 "\n\ttotal_rss_engines: %d max_vports: %d max_tx_ques %d,\n"
		 "\tmax_tx_mac_filters: %d max_rx_mcast_mac_filters: %d,\n"
		 "\tmax_rx_ucast_mac_filters: 0x%x, max_rx_ip_addr: %d,\n"
		 "\tmax_rx_lro_flow: %d max_rx_status_rings: %d,\n"
		 "\tmax_rx_buf_rings: %d, max_rx_ques: %d, max_tx_vlan_keys %d\n"
		 "\tmax_local_ipv6_addrs: %d, max_remote_ipv6_addrs: %d\n",
		 npar_info->total_pf, npar_info->total_rss_engines,
		 npar_info->max_vports, npar_info->max_tx_ques,
		 npar_info->max_tx_mac_filters,
		 npar_info->max_rx_mcast_mac_filters,
		 npar_info->max_rx_ucast_mac_filters, npar_info->max_rx_ip_addr,
		 npar_info->max_rx_lro_flow, npar_info->max_rx_status_rings,
		 npar_info->max_rx_buf_rings, npar_info->max_rx_ques,
		 npar_info->max_tx_vlan_keys, npar_info->max_local_ipv6_addrs,
		 npar_info->max_remote_ipv6_addrs);

out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

static void qlcnic_sriov_pf_reset_vport_handle(struct qlcnic_adapter *adapter,
					       u8 func)
{
	struct qlcnic_sriov  *sriov = adapter->ahw->sriov;
	struct qlcnic_vport *vp;
	int index;

	if (adapter->ahw->pci_func == func) {
		sriov->vp_handle = 0;
	} else {
		index = qlcnic_sriov_func_to_index(adapter, func);
		if (index < 0)
			return;
		vp = sriov->vf_info[index].vp;
		vp->handle = 0;
	}
}

static void qlcnic_sriov_pf_set_vport_handle(struct qlcnic_adapter *adapter,
					     u16 vport_handle, u8 func)
{
	struct qlcnic_sriov  *sriov = adapter->ahw->sriov;
	struct qlcnic_vport *vp;
	int index;

	if (adapter->ahw->pci_func == func) {
		sriov->vp_handle = vport_handle;
	} else {
		index = qlcnic_sriov_func_to_index(adapter, func);
		if (index < 0)
			return;
		vp = sriov->vf_info[index].vp;
		vp->handle = vport_handle;
	}
}

static int qlcnic_sriov_pf_get_vport_handle(struct qlcnic_adapter *adapter,
					    u8 func)
{
	struct qlcnic_sriov  *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf_info;
	int index;

	if (adapter->ahw->pci_func == func) {
		return sriov->vp_handle;
	} else {
		index = qlcnic_sriov_func_to_index(adapter, func);
		if (index >= 0) {
			vf_info = &sriov->vf_info[index];
			return vf_info->vp->handle;
		}
	}

	return -EINVAL;
}

static int qlcnic_sriov_pf_config_vport(struct qlcnic_adapter *adapter,
					u8 flag, u16 func)
{
	struct qlcnic_cmd_args cmd;
	int ret;
	int vpid;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIG_VPORT))
		return -ENOMEM;

	if (flag) {
		cmd.req.arg[3] = func << 8;
	} else {
		vpid = qlcnic_sriov_pf_get_vport_handle(adapter, func);
		if (vpid < 0) {
			ret = -EINVAL;
			goto out;
		}
		cmd.req.arg[3] = ((vpid & 0xffff) << 8) | 1;
	}

	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed %s vport, err %d for func 0x%x\n",
			(flag ? "enable" : "disable"), ret, func);
		goto out;
	}

	if (flag) {
		vpid = cmd.rsp.arg[2] & 0xffff;
		qlcnic_sriov_pf_set_vport_handle(adapter, vpid, func);
	} else {
		qlcnic_sriov_pf_reset_vport_handle(adapter, func);
	}

out:
	qlcnic_free_mbx_args(&cmd);
	return ret;
}

static int qlcnic_sriov_pf_cfg_vlan_filtering(struct qlcnic_adapter *adapter,
					      u8 enable)
{
	struct qlcnic_cmd_args cmd;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_NIC_INFO);
	if (err)
		return err;

	cmd.req.arg[1] = 0x4;
	if (enable)
		cmd.req.arg[1] |= BIT_16;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to configure VLAN filtering, err=%d\n", err);

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_pf_cfg_eswitch(struct qlcnic_adapter *adapter,
				       u8 func, u8 enable)
{
	struct qlcnic_cmd_args cmd;
	int err = -EIO;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_TOGGLE_ESWITCH))
		return -ENOMEM;

	cmd.req.arg[0] |= (3 << 29);
	cmd.req.arg[1] = ((func & 0xf) << 2) | BIT_6 | BIT_1;
	if (enable)
		cmd.req.arg[1] |= BIT_0;

	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev,
			"Failed to enable sriov eswitch%d\n", err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static void qlcnic_sriov_pf_del_flr_queue(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_back_channel *bc = &sriov->bc;
	int i;

	for (i = 0; i < sriov->num_vfs; i++)
		cancel_work_sync(&sriov->vf_info[i].flr_work);

	destroy_workqueue(bc->bc_flr_wq);
}

static int qlcnic_sriov_pf_create_flr_queue(struct qlcnic_adapter *adapter)
{
	struct qlcnic_back_channel *bc = &adapter->ahw->sriov->bc;
	struct workqueue_struct *wq;

	wq = create_singlethread_workqueue("qlcnic-flr");
	if (wq == NULL) {
		dev_err(&adapter->pdev->dev, "Cannot create FLR workqueue\n");
		return -ENOMEM;
	}

	bc->bc_flr_wq =  wq;
	return 0;
}

void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *adapter)
{
	u8 func = adapter->ahw->pci_func;

	if (!qlcnic_sriov_enable_check(adapter))
		return;

	qlcnic_sriov_pf_del_flr_queue(adapter);
	qlcnic_sriov_cfg_bc_intr(adapter, 0);
	qlcnic_sriov_pf_config_vport(adapter, 0, func);
	qlcnic_sriov_pf_cfg_eswitch(adapter, func, 0);
	qlcnic_sriov_pf_cfg_vlan_filtering(adapter, 0);
	__qlcnic_sriov_cleanup(adapter);
	adapter->ahw->op_mode = QLCNIC_MGMT_FUNC;
	clear_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
}

void qlcnic_sriov_pf_disable(struct qlcnic_adapter *adapter)
{
	if (!qlcnic_sriov_pf_check(adapter))
		return;

	if (!qlcnic_sriov_enable_check(adapter))
		return;

	pci_disable_sriov(adapter->pdev);
	netdev_info(adapter->netdev,
		    "SR-IOV is disabled successfully on port %d\n",
		    adapter->portnum);
}

static int qlcnic_pci_sriov_disable(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);

	qlcnic_sriov_pf_disable(adapter);

	qlcnic_sriov_pf_cleanup(adapter);

	/* After disabling SRIOV re-init the driver in default mode
	   configure opmode based on op_mode of function
	 */
	if (qlcnic_83xx_configure_opmode(adapter))
		return -EIO;

	if (netif_running(netdev))
		__qlcnic_up(adapter, netdev);

	return 0;
}

static int qlcnic_sriov_pf_init(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_info nic_info, pf_info, vp_info;
	int err;
	u8 func = ahw->pci_func;

	if (!qlcnic_sriov_enable_check(adapter))
		return 0;

	err = qlcnic_sriov_pf_cfg_vlan_filtering(adapter, 1);
	if (err)
		return err;

	err = qlcnic_sriov_pf_cfg_eswitch(adapter, func, 1);
	if (err)
		goto disable_vlan_filtering;

	err = qlcnic_sriov_pf_config_vport(adapter, 1, func);
	if (err)
		goto disable_eswitch;

	err = qlcnic_sriov_get_pf_info(adapter, &pf_info);
	if (err)
		goto delete_vport;

	err = qlcnic_get_nic_info(adapter, &nic_info, func);
	if (err)
		goto delete_vport;

	err = qlcnic_sriov_pf_cal_res_limit(adapter, &vp_info, func);
	if (err)
		goto delete_vport;

	err = qlcnic_sriov_cfg_bc_intr(adapter, 1);
	if (err)
		goto delete_vport;

	ahw->physical_port = (u8) nic_info.phys_port;
	ahw->switch_mode = nic_info.switch_mode;
	ahw->max_mtu = nic_info.max_mtu;
	ahw->capabilities = nic_info.capabilities;
	ahw->nic_mode = QLC_83XX_SRIOV_MODE;
	return err;

delete_vport:
	qlcnic_sriov_pf_config_vport(adapter, 0, func);

disable_eswitch:
	qlcnic_sriov_pf_cfg_eswitch(adapter, func, 0);

disable_vlan_filtering:
	qlcnic_sriov_pf_cfg_vlan_filtering(adapter, 0);

	return err;
}

static int qlcnic_sriov_pf_enable(struct qlcnic_adapter *adapter, int num_vfs)
{
	int err;

	if (!qlcnic_sriov_enable_check(adapter))
		return 0;

	err = pci_enable_sriov(adapter->pdev, num_vfs);
	if (err)
		qlcnic_sriov_pf_cleanup(adapter);

	return err;
}

static int __qlcnic_pci_sriov_enable(struct qlcnic_adapter *adapter,
				     int num_vfs)
{
	int err = 0;

	set_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
	adapter->ahw->op_mode = QLCNIC_SRIOV_PF_FUNC;

	err = qlcnic_sriov_init(adapter, num_vfs);
	if (err)
		goto clear_op_mode;

	err = qlcnic_sriov_pf_create_flr_queue(adapter);
	if (err)
		goto sriov_cleanup;

	err = qlcnic_sriov_pf_init(adapter);
	if (err)
		goto del_flr_queue;

	err = qlcnic_sriov_pf_enable(adapter, num_vfs);
	return err;

del_flr_queue:
	qlcnic_sriov_pf_del_flr_queue(adapter);

sriov_cleanup:
	__qlcnic_sriov_cleanup(adapter);

clear_op_mode:
	clear_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
	adapter->ahw->op_mode = QLCNIC_MGMT_FUNC;
	return err;
}

static int qlcnic_pci_sriov_enable(struct qlcnic_adapter *adapter, int num_vfs)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	if (!(adapter->flags & QLCNIC_MSIX_ENABLED)) {
		netdev_err(netdev,
			   "SR-IOV cannot be enabled, when legacy interrupts are enabled\n");
		return -EIO;
	}

	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);

	err = __qlcnic_pci_sriov_enable(adapter, num_vfs);
	if (err) {
		netdev_info(netdev, "Failed to enable SR-IOV on port %d\n",
			    adapter->portnum);

		err = -EIO;
		if (qlcnic_83xx_configure_opmode(adapter))
			goto error;
	} else {
		netdev_info(netdev,
			    "SR-IOV is enabled successfully on port %d\n",
			    adapter->portnum);
		/* Return number of vfs enabled */
		err = num_vfs;
	}
	if (netif_running(netdev))
		__qlcnic_up(adapter, netdev);

error:
	return err;
}

int qlcnic_pci_sriov_configure(struct pci_dev *dev, int num_vfs)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(dev);
	int err;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EBUSY;

	if (num_vfs == 0)
		err = qlcnic_pci_sriov_disable(adapter);
	else
		err = qlcnic_pci_sriov_enable(adapter, num_vfs);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return err;
}

static int qlcnic_sriov_set_vf_acl(struct qlcnic_adapter *adapter, u8 func)
{
	struct qlcnic_cmd_args cmd;
	struct qlcnic_vport *vp;
	int err, id;
	u8 *mac;

	id = qlcnic_sriov_func_to_index(adapter, func);
	if (id < 0)
		return id;

	vp = adapter->ahw->sriov->vf_info[id].vp;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_NIC_INFO);
	if (err)
		return err;

	cmd.req.arg[1] = 0x3 | func << 16;
	if (vp->spoofchk == true) {
		mac = vp->mac;
		cmd.req.arg[2] |= BIT_1 | BIT_3 | BIT_8;
		cmd.req.arg[4] = mac[5] | mac[4] << 8 | mac[3] << 16 |
				 mac[2] << 24;
		cmd.req.arg[5] = mac[1] | mac[0] << 8;
	}

	if (vp->vlan_mode == QLC_PVID_MODE) {
		cmd.req.arg[2] |= BIT_6;
		cmd.req.arg[3] |= vp->vlan << 8;
	}

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev, "Failed to set ACL, err=%d\n",
			err);

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_set_vf_vport_info(struct qlcnic_adapter *adapter,
					  u16 func)
{
	struct qlcnic_info defvp_info;
	int err;

	err = qlcnic_sriov_pf_cal_res_limit(adapter, &defvp_info, func);
	if (err)
		return -EIO;

	err = qlcnic_sriov_set_vf_acl(adapter, func);
	if (err)
		return err;

	return 0;
}

static int qlcnic_sriov_pf_channel_cfg_cmd(struct qlcnic_bc_trans *trans,
					   struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_vport *vp = vf->vp;
	struct qlcnic_adapter *adapter;
	u16 func = vf->pci_func;
	int err;

	adapter = vf->adapter;

	if (trans->req_hdr->cmd_op == QLCNIC_BC_CMD_CHANNEL_INIT) {
		err = qlcnic_sriov_pf_config_vport(adapter, 1, func);
		if (!err) {
			err = qlcnic_sriov_set_vf_vport_info(adapter, func);
			if (err)
				qlcnic_sriov_pf_config_vport(adapter, 0, func);
		}
	} else {
		if (vp->vlan_mode == QLC_GUEST_VLAN_MODE)
			vp->vlan = 0;
		err = qlcnic_sriov_pf_config_vport(adapter, 0, func);
	}

	if (err)
		goto err_out;

	cmd->rsp.arg[0] |= (1 << 25);

	if (trans->req_hdr->cmd_op == QLCNIC_BC_CMD_CHANNEL_INIT)
		set_bit(QLC_BC_VF_STATE, &vf->state);
	else
		clear_bit(QLC_BC_VF_STATE, &vf->state);

	return err;

err_out:
	cmd->rsp.arg[0] |= (2 << 25);
	return err;
}

static int qlcnic_sriov_cfg_vf_def_mac(struct qlcnic_adapter *adapter,
				       struct qlcnic_vport *vp,
				       u16 func, u16 vlan, u8 op)
{
	struct qlcnic_cmd_args cmd;
	struct qlcnic_macvlan_mbx mv;
	u8 *addr;
	int err;
	u32 *buf;
	int vpid;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIG_MAC_VLAN))
		return -ENOMEM;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter, func);
	if (vpid < 0) {
		err = -EINVAL;
		goto out;
	}

	if (vlan)
		op = ((op == QLCNIC_MAC_ADD || op == QLCNIC_MAC_VLAN_ADD) ?
		      QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_VLAN_DEL);

	cmd.req.arg[1] = op | (1 << 8) | (3 << 6);
	cmd.req.arg[1] |= ((vpid & 0xffff) << 16) | BIT_31;

	addr = vp->mac;
	mv.vlan = vlan;
	mv.mac_addr0 = addr[0];
	mv.mac_addr1 = addr[1];
	mv.mac_addr2 = addr[2];
	mv.mac_addr3 = addr[3];
	mv.mac_addr4 = addr[4];
	mv.mac_addr5 = addr[5];
	buf = &cmd.req.arg[2];
	memcpy(buf, &mv, sizeof(struct qlcnic_macvlan_mbx));

	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err)
		dev_err(&adapter->pdev->dev,
			"MAC-VLAN %s to CAM failed, err=%d.\n",
			((op == 1) ? "add " : "delete "), err);

out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_validate_create_rx_ctx(struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[0] >> 29) != 0x3)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_create_rx_ctx_cmd(struct qlcnic_bc_trans *tran,
					     struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = tran->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	struct qlcnic_rcv_mbx_out *mbx_out;
	int err;
	u16 vlan;

	err = qlcnic_sriov_validate_create_rx_ctx(cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	cmd->req.arg[6] = vf->vp->handle;
	err = qlcnic_issue_cmd(adapter, cmd);

	vlan = vf->vp->vlan;
	if (!err) {
		mbx_out = (struct qlcnic_rcv_mbx_out *)&cmd->rsp.arg[1];
		vf->rx_ctx_id = mbx_out->ctx_id;
		qlcnic_sriov_cfg_vf_def_mac(adapter, vf->vp, vf->pci_func,
					    vlan, QLCNIC_MAC_ADD);
	} else {
		vf->rx_ctx_id = 0;
	}

	return err;
}

static int qlcnic_sriov_pf_mac_address_cmd(struct qlcnic_bc_trans *trans,
					   struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	u8 type, *mac;

	type = cmd->req.arg[1];
	switch (type) {
	case QLCNIC_SET_STATION_MAC:
	case QLCNIC_SET_FAC_DEF_MAC:
		cmd->rsp.arg[0] = (2 << 25);
		break;
	case QLCNIC_GET_CURRENT_MAC:
		cmd->rsp.arg[0] = (1 << 25);
		mac = vf->vp->mac;
		cmd->rsp.arg[2] = mac[1] | ((mac[0] << 8) & 0xff00);
		cmd->rsp.arg[1] = mac[5] | ((mac[4] << 8) & 0xff00) |
				  ((mac[3]) << 16 & 0xff0000) |
				  ((mac[2]) << 24 & 0xff000000);
	}

	return 0;
}

static int qlcnic_sriov_validate_create_tx_ctx(struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[0] >> 29) != 0x3)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_create_tx_ctx_cmd(struct qlcnic_bc_trans *trans,
					     struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	struct qlcnic_tx_mbx_out *mbx_out;
	int err;

	err = qlcnic_sriov_validate_create_tx_ctx(cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	cmd->req.arg[5] |= vf->vp->handle << 16;
	err = qlcnic_issue_cmd(adapter, cmd);
	if (!err) {
		mbx_out = (struct qlcnic_tx_mbx_out *)&cmd->rsp.arg[2];
		vf->tx_ctx_id = mbx_out->ctx_id;
	} else {
		vf->tx_ctx_id = 0;
	}

	return err;
}

static int qlcnic_sriov_validate_del_rx_ctx(struct qlcnic_vf_info *vf,
					    struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[0] >> 29) != 0x3)
		return -EINVAL;

	if ((cmd->req.arg[1] & 0xffff) != vf->rx_ctx_id)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_del_rx_ctx_cmd(struct qlcnic_bc_trans *trans,
					  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;
	u16 vlan;

	err = qlcnic_sriov_validate_del_rx_ctx(vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	vlan = vf->vp->vlan;
	qlcnic_sriov_cfg_vf_def_mac(adapter, vf->vp, vf->pci_func,
				    vlan, QLCNIC_MAC_DEL);
	cmd->req.arg[1] |= vf->vp->handle << 16;
	err = qlcnic_issue_cmd(adapter, cmd);

	if (!err)
		vf->rx_ctx_id = 0;

	return err;
}

static int qlcnic_sriov_validate_del_tx_ctx(struct qlcnic_vf_info *vf,
					    struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[0] >> 29) != 0x3)
		return -EINVAL;

	if ((cmd->req.arg[1] & 0xffff) != vf->tx_ctx_id)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_del_tx_ctx_cmd(struct qlcnic_bc_trans *trans,
					  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_del_tx_ctx(vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	cmd->req.arg[1] |= vf->vp->handle << 16;
	err = qlcnic_issue_cmd(adapter, cmd);

	if (!err)
		vf->tx_ctx_id = 0;

	return err;
}

static int qlcnic_sriov_validate_cfg_lro(struct qlcnic_vf_info *vf,
					 struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[1] >> 16) != vf->rx_ctx_id)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_cfg_lro_cmd(struct qlcnic_bc_trans *trans,
				       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_cfg_lro(vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_pf_cfg_ip_cmd(struct qlcnic_bc_trans *trans,
				      struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err = -EIO;
	u8 op;

	op =  cmd->req.arg[1] & 0xff;

	cmd->req.arg[1] |= vf->vp->handle << 16;
	cmd->req.arg[1] |= BIT_31;

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_validate_cfg_intrpt(struct qlcnic_vf_info *vf,
					    struct qlcnic_cmd_args *cmd)
{
	if (((cmd->req.arg[1] >> 8) & 0xff) != vf->pci_func)
		return -EINVAL;

	if (!(cmd->req.arg[1] & BIT_16))
		return -EINVAL;

	if ((cmd->req.arg[1] & 0xff) != 0x1)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_cfg_intrpt_cmd(struct qlcnic_bc_trans *trans,
					  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_cfg_intrpt(vf, cmd);
	if (err)
		cmd->rsp.arg[0] |= (0x6 << 25);
	else
		err = qlcnic_issue_cmd(adapter, cmd);

	return err;
}

static int qlcnic_sriov_validate_mtu(struct qlcnic_adapter *adapter,
				     struct qlcnic_vf_info *vf,
				     struct qlcnic_cmd_args *cmd)
{
	if (cmd->req.arg[1] != vf->rx_ctx_id)
		return -EINVAL;

	if (cmd->req.arg[2] > adapter->ahw->max_mtu)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_set_mtu_cmd(struct qlcnic_bc_trans *trans,
				       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_mtu(adapter, vf, cmd);
	if (err)
		cmd->rsp.arg[0] |= (0x6 << 25);
	else
		err = qlcnic_issue_cmd(adapter, cmd);

	return err;
}

static int qlcnic_sriov_validate_get_nic_info(struct qlcnic_vf_info *vf,
					      struct qlcnic_cmd_args *cmd)
{
	if (cmd->req.arg[1] & BIT_31) {
		if (((cmd->req.arg[1] >> 16) & 0x7fff) != vf->pci_func)
			return -EINVAL;
	} else {
		cmd->req.arg[1] |= vf->vp->handle << 16;
	}

	return 0;
}

static int qlcnic_sriov_pf_get_nic_info_cmd(struct qlcnic_bc_trans *trans,
					    struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_get_nic_info(vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_validate_cfg_rss(struct qlcnic_vf_info *vf,
					 struct qlcnic_cmd_args *cmd)
{
	if (cmd->req.arg[1] != vf->rx_ctx_id)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_cfg_rss_cmd(struct qlcnic_bc_trans *trans,
				       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_cfg_rss(vf, cmd);
	if (err)
		cmd->rsp.arg[0] |= (0x6 << 25);
	else
		err = qlcnic_issue_cmd(adapter, cmd);

	return err;
}

static int qlcnic_sriov_validate_cfg_intrcoal(struct qlcnic_adapter *adapter,
					      struct qlcnic_vf_info *vf,
					      struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_nic_intr_coalesce *coal = &adapter->ahw->coal;
	u16 ctx_id, pkts, time;

	ctx_id = cmd->req.arg[1] >> 16;
	pkts = cmd->req.arg[2] & 0xffff;
	time = cmd->req.arg[2] >> 16;

	if (ctx_id != vf->rx_ctx_id)
		return -EINVAL;
	if (pkts > coal->rx_packets)
		return -EINVAL;
	if (time < coal->rx_time_us)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_cfg_intrcoal_cmd(struct qlcnic_bc_trans *tran,
					    struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = tran->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_cfg_intrcoal(adapter, vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_validate_cfg_macvlan(struct qlcnic_adapter *adapter,
					     struct qlcnic_vf_info *vf,
					     struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_macvlan_mbx *macvlan;
	struct qlcnic_vport *vp = vf->vp;
	u8 op, new_op;

	if (!(cmd->req.arg[1] & BIT_8))
		return -EINVAL;

	cmd->req.arg[1] |= (vf->vp->handle << 16);
	cmd->req.arg[1] |= BIT_31;

	macvlan = (struct qlcnic_macvlan_mbx *)&cmd->req.arg[2];
	if (!(macvlan->mac_addr0 & BIT_0)) {
		dev_err(&adapter->pdev->dev,
			"MAC address change is not allowed from VF %d",
			vf->pci_func);
		return -EINVAL;
	}

	if (vp->vlan_mode == QLC_PVID_MODE) {
		op = cmd->req.arg[1] & 0x7;
		cmd->req.arg[1] &= ~0x7;
		new_op = (op == QLCNIC_MAC_ADD || op == QLCNIC_MAC_VLAN_ADD) ?
			 QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_VLAN_DEL;
		cmd->req.arg[3] |= vp->vlan << 16;
		cmd->req.arg[1] |= new_op;
	}

	return 0;
}

static int qlcnic_sriov_pf_cfg_macvlan_cmd(struct qlcnic_bc_trans *trans,
					   struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_cfg_macvlan(adapter, vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_validate_linkevent(struct qlcnic_vf_info *vf,
					   struct qlcnic_cmd_args *cmd)
{
	if ((cmd->req.arg[1] >> 16) != vf->rx_ctx_id)
		return -EINVAL;

	return 0;
}

static int qlcnic_sriov_pf_linkevent_cmd(struct qlcnic_bc_trans *trans,
					 struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	err = qlcnic_sriov_validate_linkevent(vf, cmd);
	if (err) {
		cmd->rsp.arg[0] |= (0x6 << 25);
		return err;
	}

	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_pf_cfg_promisc_cmd(struct qlcnic_bc_trans *trans,
					   struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_adapter *adapter = vf->adapter;
	int err;

	cmd->req.arg[1] |= vf->vp->handle << 16;
	cmd->req.arg[1] |= BIT_31;
	err = qlcnic_issue_cmd(adapter, cmd);
	return err;
}

static int qlcnic_sriov_pf_get_acl_cmd(struct qlcnic_bc_trans *trans,
				       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct qlcnic_vport *vp = vf->vp;
	u8 cmd_op, mode = vp->vlan_mode;
	struct qlcnic_adapter *adapter;

	adapter = vf->adapter;

	cmd_op = trans->req_hdr->cmd_op;
	cmd->rsp.arg[0] |= 1 << 25;

	/* For 84xx adapter in case of PVID , PFD should send vlan mode as
	 * QLC_NO_VLAN_MODE to VFD which is zero in mailbox response
	 */
	if (qlcnic_84xx_check(adapter) && mode == QLC_PVID_MODE)
		return 0;

	switch (mode) {
	case QLC_GUEST_VLAN_MODE:
		cmd->rsp.arg[1] = mode | 1 << 8;
		cmd->rsp.arg[2] = 1 << 16;
		break;
	case QLC_PVID_MODE:
		cmd->rsp.arg[1] = mode | 1 << 8 | vp->vlan << 16;
		break;
	}

	return 0;
}

static int qlcnic_sriov_pf_del_guest_vlan(struct qlcnic_adapter *adapter,
					  struct qlcnic_vf_info *vf)

{
	struct qlcnic_vport *vp = vf->vp;

	if (!vp->vlan)
		return -EINVAL;

	if (!vf->rx_ctx_id) {
		vp->vlan = 0;
		return 0;
	}

	qlcnic_sriov_cfg_vf_def_mac(adapter, vp, vf->pci_func,
				    vp->vlan, QLCNIC_MAC_DEL);
	vp->vlan = 0;
	qlcnic_sriov_cfg_vf_def_mac(adapter, vp, vf->pci_func,
				    0, QLCNIC_MAC_ADD);
	return 0;
}

static int qlcnic_sriov_pf_add_guest_vlan(struct qlcnic_adapter *adapter,
					  struct qlcnic_vf_info *vf,
					  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vport *vp = vf->vp;
	int err = -EIO;

	if (vp->vlan)
		return err;

	if (!vf->rx_ctx_id) {
		vp->vlan = cmd->req.arg[1] >> 16;
		return 0;
	}

	err = qlcnic_sriov_cfg_vf_def_mac(adapter, vp, vf->pci_func,
					  0, QLCNIC_MAC_DEL);
	if (err)
		return err;

	vp->vlan = cmd->req.arg[1] >> 16;
	err = qlcnic_sriov_cfg_vf_def_mac(adapter, vp, vf->pci_func,
					  vp->vlan, QLCNIC_MAC_ADD);

	if (err) {
		qlcnic_sriov_cfg_vf_def_mac(adapter, vp, vf->pci_func,
					    0, QLCNIC_MAC_ADD);
		vp->vlan = 0;
	}

	return err;
}

static int qlcnic_sriov_pf_cfg_guest_vlan_cmd(struct qlcnic_bc_trans *tran,
					      struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_vf_info  *vf = tran->vf;
	struct qlcnic_adapter *adapter =  vf->adapter;
	struct qlcnic_vport *vp = vf->vp;
	int err = -EIO;
	u8 op;

	if (vp->vlan_mode != QLC_GUEST_VLAN_MODE) {
		cmd->rsp.arg[0] |= 2 << 25;
		return err;
	}

	op = cmd->req.arg[1] & 0xf;

	if (op)
		err = qlcnic_sriov_pf_add_guest_vlan(adapter, vf, cmd);
	else
		err = qlcnic_sriov_pf_del_guest_vlan(adapter, vf);

	cmd->rsp.arg[0] |= err ? 2 << 25 : 1 << 25;
	return err;
}

static const int qlcnic_pf_passthru_supp_cmds[] = {
	QLCNIC_CMD_GET_STATISTICS,
	QLCNIC_CMD_GET_PORT_CONFIG,
	QLCNIC_CMD_GET_LINK_STATUS,
	QLCNIC_CMD_DCB_QUERY_CAP,
	QLCNIC_CMD_DCB_QUERY_PARAM,
	QLCNIC_CMD_INIT_NIC_FUNC,
	QLCNIC_CMD_STOP_NIC_FUNC,
};

static const struct qlcnic_sriov_cmd_handler qlcnic_pf_bc_cmd_hdlr[] = {
	[QLCNIC_BC_CMD_CHANNEL_INIT] = {&qlcnic_sriov_pf_channel_cfg_cmd},
	[QLCNIC_BC_CMD_CHANNEL_TERM] = {&qlcnic_sriov_pf_channel_cfg_cmd},
	[QLCNIC_BC_CMD_GET_ACL]	= {&qlcnic_sriov_pf_get_acl_cmd},
	[QLCNIC_BC_CMD_CFG_GUEST_VLAN]	= {&qlcnic_sriov_pf_cfg_guest_vlan_cmd},
};

static const struct qlcnic_sriov_fw_cmd_handler qlcnic_pf_fw_cmd_hdlr[] = {
	{QLCNIC_CMD_CREATE_RX_CTX, qlcnic_sriov_pf_create_rx_ctx_cmd},
	{QLCNIC_CMD_CREATE_TX_CTX, qlcnic_sriov_pf_create_tx_ctx_cmd},
	{QLCNIC_CMD_MAC_ADDRESS, qlcnic_sriov_pf_mac_address_cmd},
	{QLCNIC_CMD_DESTROY_RX_CTX, qlcnic_sriov_pf_del_rx_ctx_cmd},
	{QLCNIC_CMD_DESTROY_TX_CTX, qlcnic_sriov_pf_del_tx_ctx_cmd},
	{QLCNIC_CMD_CONFIGURE_HW_LRO, qlcnic_sriov_pf_cfg_lro_cmd},
	{QLCNIC_CMD_CONFIGURE_IP_ADDR, qlcnic_sriov_pf_cfg_ip_cmd},
	{QLCNIC_CMD_CONFIG_INTRPT, qlcnic_sriov_pf_cfg_intrpt_cmd},
	{QLCNIC_CMD_SET_MTU, qlcnic_sriov_pf_set_mtu_cmd},
	{QLCNIC_CMD_GET_NIC_INFO, qlcnic_sriov_pf_get_nic_info_cmd},
	{QLCNIC_CMD_CONFIGURE_RSS, qlcnic_sriov_pf_cfg_rss_cmd},
	{QLCNIC_CMD_CONFIG_INTR_COAL, qlcnic_sriov_pf_cfg_intrcoal_cmd},
	{QLCNIC_CMD_CONFIG_MAC_VLAN, qlcnic_sriov_pf_cfg_macvlan_cmd},
	{QLCNIC_CMD_GET_LINK_EVENT, qlcnic_sriov_pf_linkevent_cmd},
	{QLCNIC_CMD_CONFIGURE_MAC_RX_MODE, qlcnic_sriov_pf_cfg_promisc_cmd},
};

void qlcnic_sriov_pf_process_bc_cmd(struct qlcnic_adapter *adapter,
				    struct qlcnic_bc_trans *trans,
				    struct qlcnic_cmd_args *cmd)
{
	u8 size, cmd_op;

	cmd_op = trans->req_hdr->cmd_op;

	if (trans->req_hdr->op_type == QLC_BC_CMD) {
		size = ARRAY_SIZE(qlcnic_pf_bc_cmd_hdlr);
		if (cmd_op < size) {
			qlcnic_pf_bc_cmd_hdlr[cmd_op].fn(trans, cmd);
			return;
		}
	} else {
		int i;
		size = ARRAY_SIZE(qlcnic_pf_fw_cmd_hdlr);
		for (i = 0; i < size; i++) {
			if (cmd_op == qlcnic_pf_fw_cmd_hdlr[i].cmd) {
				qlcnic_pf_fw_cmd_hdlr[i].fn(trans, cmd);
				return;
			}
		}

		size = ARRAY_SIZE(qlcnic_pf_passthru_supp_cmds);
		for (i = 0; i < size; i++) {
			if (cmd_op == qlcnic_pf_passthru_supp_cmds[i]) {
				qlcnic_issue_cmd(adapter, cmd);
				return;
			}
		}
	}

	cmd->rsp.arg[0] |= (0x9 << 25);
}

void qlcnic_pf_set_interface_id_create_rx_ctx(struct qlcnic_adapter *adapter,
					     u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= vpid;
}

void qlcnic_pf_set_interface_id_del_rx_ctx(struct qlcnic_adapter *adapter,
					   u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= vpid << 16;
}

void qlcnic_pf_set_interface_id_create_tx_ctx(struct qlcnic_adapter *adapter,
					      u32 *int_id)
{
	int vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= vpid << 16;
}

void qlcnic_pf_set_interface_id_del_tx_ctx(struct qlcnic_adapter *adapter,
					   u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= vpid << 16;
}

void qlcnic_pf_set_interface_id_promisc(struct qlcnic_adapter *adapter,
					u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= (vpid << 16) | BIT_31;
}

void qlcnic_pf_set_interface_id_ipaddr(struct qlcnic_adapter *adapter,
				       u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= (vpid << 16) | BIT_31;
}

void qlcnic_pf_set_interface_id_macaddr(struct qlcnic_adapter *adapter,
					u32 *int_id)
{
	u16 vpid;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter,
						adapter->ahw->pci_func);
	*int_id |= (vpid << 16) | BIT_31;
}

static void qlcnic_sriov_del_rx_ctx(struct qlcnic_adapter *adapter,
				    struct qlcnic_vf_info *vf)
{
	struct qlcnic_cmd_args cmd;
	int vpid;

	if (!vf->rx_ctx_id)
		return;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_RX_CTX))
		return;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter, vf->pci_func);
	if (vpid >= 0) {
		cmd.req.arg[1] = vf->rx_ctx_id | (vpid & 0xffff) << 16;
		if (qlcnic_issue_cmd(adapter, &cmd))
			dev_err(&adapter->pdev->dev,
				"Failed to delete Tx ctx in firmware for func 0x%x\n",
				vf->pci_func);
		else
			vf->rx_ctx_id = 0;
	}

	qlcnic_free_mbx_args(&cmd);
}

static void qlcnic_sriov_del_tx_ctx(struct qlcnic_adapter *adapter,
				    struct qlcnic_vf_info *vf)
{
	struct qlcnic_cmd_args cmd;
	int vpid;

	if (!vf->tx_ctx_id)
		return;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_TX_CTX))
		return;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter, vf->pci_func);
	if (vpid >= 0) {
		cmd.req.arg[1] |= vf->tx_ctx_id | (vpid & 0xffff) << 16;
		if (qlcnic_issue_cmd(adapter, &cmd))
			dev_err(&adapter->pdev->dev,
				"Failed to delete Tx ctx in firmware for func 0x%x\n",
				vf->pci_func);
		else
			vf->tx_ctx_id = 0;
	}

	qlcnic_free_mbx_args(&cmd);
}

static int qlcnic_sriov_add_act_list_irqsave(struct qlcnic_sriov *sriov,
					     struct qlcnic_vf_info *vf,
					     struct qlcnic_bc_trans *trans)
{
	struct qlcnic_trans_list *t_list = &vf->rcv_act;
	unsigned long flag;

	spin_lock_irqsave(&t_list->lock, flag);

	__qlcnic_sriov_add_act_list(sriov, vf, trans);

	spin_unlock_irqrestore(&t_list->lock, flag);
	return 0;
}

static void __qlcnic_sriov_process_flr(struct qlcnic_vf_info *vf)
{
	struct qlcnic_adapter *adapter = vf->adapter;

	qlcnic_sriov_cleanup_list(&vf->rcv_pend);
	cancel_work_sync(&vf->trans_work);
	qlcnic_sriov_cleanup_list(&vf->rcv_act);

	if (test_bit(QLC_BC_VF_SOFT_FLR, &vf->state)) {
		qlcnic_sriov_del_tx_ctx(adapter, vf);
		qlcnic_sriov_del_rx_ctx(adapter, vf);
	}

	qlcnic_sriov_pf_config_vport(adapter, 0, vf->pci_func);

	clear_bit(QLC_BC_VF_FLR, &vf->state);
	if (test_bit(QLC_BC_VF_SOFT_FLR, &vf->state)) {
		qlcnic_sriov_add_act_list_irqsave(adapter->ahw->sriov, vf,
						  vf->flr_trans);
		clear_bit(QLC_BC_VF_SOFT_FLR, &vf->state);
		vf->flr_trans = NULL;
	}
}

static void qlcnic_sriov_pf_process_flr(struct work_struct *work)
{
	struct qlcnic_vf_info *vf;

	vf = container_of(work, struct qlcnic_vf_info, flr_work);
	__qlcnic_sriov_process_flr(vf);
	return;
}

static void qlcnic_sriov_schedule_flr(struct qlcnic_sriov *sriov,
				      struct qlcnic_vf_info *vf,
				      work_func_t func)
{
	if (test_bit(__QLCNIC_RESETTING, &vf->adapter->state))
		return;

	INIT_WORK(&vf->flr_work, func);
	queue_work(sriov->bc.bc_flr_wq, &vf->flr_work);
}

static void qlcnic_sriov_handle_soft_flr(struct qlcnic_adapter *adapter,
					 struct qlcnic_bc_trans *trans,
					 struct qlcnic_vf_info *vf)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;

	set_bit(QLC_BC_VF_FLR, &vf->state);
	clear_bit(QLC_BC_VF_STATE, &vf->state);
	set_bit(QLC_BC_VF_SOFT_FLR, &vf->state);
	vf->flr_trans = trans;
	qlcnic_sriov_schedule_flr(sriov, vf, qlcnic_sriov_pf_process_flr);
	netdev_info(adapter->netdev, "Software FLR for PCI func %d\n",
		    vf->pci_func);
}

bool qlcnic_sriov_soft_flr_check(struct qlcnic_adapter *adapter,
				 struct qlcnic_bc_trans *trans,
				 struct qlcnic_vf_info *vf)
{
	struct qlcnic_bc_hdr *hdr = trans->req_hdr;

	if ((hdr->cmd_op == QLCNIC_BC_CMD_CHANNEL_INIT) &&
	    (hdr->op_type == QLC_BC_CMD) &&
	     test_bit(QLC_BC_VF_STATE, &vf->state)) {
		qlcnic_sriov_handle_soft_flr(adapter, trans, vf);
		return true;
	}

	return false;
}

void qlcnic_sriov_pf_handle_flr(struct qlcnic_sriov *sriov,
				struct qlcnic_vf_info *vf)
{
	struct net_device *dev = vf->adapter->netdev;
	struct qlcnic_vport *vp = vf->vp;

	if (!test_and_clear_bit(QLC_BC_VF_STATE, &vf->state)) {
		clear_bit(QLC_BC_VF_FLR, &vf->state);
		return;
	}

	if (test_and_set_bit(QLC_BC_VF_FLR, &vf->state)) {
		netdev_info(dev, "FLR for PCI func %d in progress\n",
			    vf->pci_func);
		return;
	}

	if (vp->vlan_mode == QLC_GUEST_VLAN_MODE)
		vp->vlan = 0;

	qlcnic_sriov_schedule_flr(sriov, vf, qlcnic_sriov_pf_process_flr);
	netdev_info(dev, "FLR received for PCI func %d\n", vf->pci_func);
}

void qlcnic_sriov_pf_reset(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_sriov *sriov = ahw->sriov;
	struct qlcnic_vf_info *vf;
	u16 num_vfs = sriov->num_vfs;
	int i;

	for (i = 0; i < num_vfs; i++) {
		vf = &sriov->vf_info[i];
		vf->rx_ctx_id = 0;
		vf->tx_ctx_id = 0;
		cancel_work_sync(&vf->flr_work);
		__qlcnic_sriov_process_flr(vf);
		clear_bit(QLC_BC_VF_STATE, &vf->state);
	}

	qlcnic_sriov_pf_reset_vport_handle(adapter, ahw->pci_func);
	QLCWRX(ahw, QLCNIC_MBX_INTR_ENBL, (ahw->num_msix - 1) << 8);
}

int qlcnic_sriov_pf_reinit(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int err;

	if (!qlcnic_sriov_enable_check(adapter))
		return 0;

	ahw->op_mode = QLCNIC_SRIOV_PF_FUNC;

	err = qlcnic_sriov_pf_init(adapter);
	if (err)
		return err;

	dev_info(&adapter->pdev->dev, "%s: op_mode %d\n",
		 __func__, ahw->op_mode);
	return err;
}

int qlcnic_sriov_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	int i, num_vfs;
	struct qlcnic_vf_info *vf_info;
	u8 *curr_mac;

	if (!qlcnic_sriov_pf_check(adapter))
		return -EOPNOTSUPP;

	num_vfs = sriov->num_vfs;

	if (!is_valid_ether_addr(mac) || vf >= num_vfs)
		return -EINVAL;

	if (!compare_ether_addr(adapter->mac_addr, mac)) {
		netdev_err(netdev, "MAC address is already in use by the PF\n");
		return -EINVAL;
	}

	for (i = 0; i < num_vfs; i++) {
		vf_info = &sriov->vf_info[i];
		if (!compare_ether_addr(vf_info->vp->mac, mac)) {
			netdev_err(netdev,
				   "MAC address is already in use by VF %d\n",
				   i);
			return -EINVAL;
		}
	}

	vf_info = &sriov->vf_info[vf];
	curr_mac = vf_info->vp->mac;

	if (test_bit(QLC_BC_VF_STATE, &vf_info->state)) {
		netdev_err(netdev,
			   "MAC address change failed for VF %d, as VF driver is loaded. Please unload VF driver and retry the operation\n",
			   vf);
		return -EOPNOTSUPP;
	}

	memcpy(curr_mac, mac, netdev->addr_len);
	netdev_info(netdev, "MAC Address %pM  is configured for VF %d\n",
		    mac, vf);
	return 0;
}

int qlcnic_sriov_set_vf_tx_rate(struct net_device *netdev, int vf, int tx_rate)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf_info;
	struct qlcnic_info nic_info;
	struct qlcnic_vport *vp;
	u16 vpid;

	if (!qlcnic_sriov_pf_check(adapter))
		return -EOPNOTSUPP;

	if (vf >= sriov->num_vfs)
		return -EINVAL;

	if (tx_rate >= 10000 || tx_rate < 100) {
		netdev_err(netdev,
			   "Invalid Tx rate, allowed range is [%d - %d]",
			   QLC_VF_MIN_TX_RATE, QLC_VF_MAX_TX_RATE);
		return -EINVAL;
	}

	if (tx_rate == 0)
		tx_rate = 10000;

	vf_info = &sriov->vf_info[vf];
	vp = vf_info->vp;
	vpid = vp->handle;

	if (test_bit(QLC_BC_VF_STATE, &vf_info->state)) {
		if (qlcnic_sriov_get_vf_vport_info(adapter, &nic_info, vpid))
			return -EIO;

		nic_info.max_tx_bw = tx_rate / 100;
		nic_info.bit_offsets = BIT_0;

		if (qlcnic_sriov_pf_set_vport_info(adapter, &nic_info, vpid))
			return -EIO;
	}

	vp->max_tx_bw = tx_rate / 100;
	netdev_info(netdev,
		    "Setting Tx rate %d (Mbps), %d %% of PF bandwidth, for VF %d\n",
		    tx_rate, vp->max_tx_bw, vf);
	return 0;
}

int qlcnic_sriov_set_vf_vlan(struct net_device *netdev, int vf,
			     u16 vlan, u8 qos)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf_info;
	struct qlcnic_vport *vp;

	if (!qlcnic_sriov_pf_check(adapter))
		return -EOPNOTSUPP;

	if (vf >= sriov->num_vfs || qos > 7)
		return -EINVAL;

	if (vlan > MAX_VLAN_ID) {
		netdev_err(netdev,
			   "Invalid VLAN ID, allowed range is [0 - %d]\n",
			   MAX_VLAN_ID);
		return -EINVAL;
	}

	vf_info = &sriov->vf_info[vf];
	vp = vf_info->vp;
	if (test_bit(QLC_BC_VF_STATE, &vf_info->state)) {
		netdev_err(netdev,
			   "VLAN change failed for VF %d, as VF driver is loaded. Please unload VF driver and retry the operation\n",
			   vf);
		return -EOPNOTSUPP;
	}

	switch (vlan) {
	case 4095:
		vp->vlan = 0;
		vp->vlan_mode = QLC_GUEST_VLAN_MODE;
		break;
	case 0:
		vp->vlan_mode = QLC_NO_VLAN_MODE;
		vp->vlan = 0;
		vp->qos = 0;
		break;
	default:
		vp->vlan_mode = QLC_PVID_MODE;
		vp->vlan = vlan;
		vp->qos = qos;
	}

	netdev_info(netdev, "Setting VLAN %d, QoS %d, for VF %d\n",
		    vlan, qos, vf);
	return 0;
}

static inline __u32 qlcnic_sriov_get_vf_vlan(struct qlcnic_adapter *adapter,
					     struct qlcnic_vport *vp, int vf)
{
	__u32 vlan = 0;

	switch (vp->vlan_mode) {
	case QLC_PVID_MODE:
		vlan = vp->vlan;
		break;
	case QLC_GUEST_VLAN_MODE:
		vlan = MAX_VLAN_ID;
		break;
	case QLC_NO_VLAN_MODE:
		vlan = 0;
		break;
	default:
		netdev_info(adapter->netdev, "Invalid VLAN mode = %d for VF %d\n",
			    vp->vlan_mode, vf);
	}

	return vlan;
}

int qlcnic_sriov_get_vf_config(struct net_device *netdev,
			       int vf, struct ifla_vf_info *ivi)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vport *vp;

	if (!qlcnic_sriov_pf_check(adapter))
		return -EOPNOTSUPP;

	if (vf >= sriov->num_vfs)
		return -EINVAL;

	vp = sriov->vf_info[vf].vp;
	memcpy(&ivi->mac, vp->mac, ETH_ALEN);
	ivi->vlan = qlcnic_sriov_get_vf_vlan(adapter, vp, vf);
	ivi->qos = vp->qos;
	ivi->spoofchk = vp->spoofchk;
	if (vp->max_tx_bw == MAX_BW)
		ivi->tx_rate = 0;
	else
		ivi->tx_rate = vp->max_tx_bw * 100;

	ivi->vf = vf;
	return 0;
}

int qlcnic_sriov_set_vf_spoofchk(struct net_device *netdev, int vf, bool chk)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf_info;
	struct qlcnic_vport *vp;

	if (!qlcnic_sriov_pf_check(adapter))
		return -EOPNOTSUPP;

	if (vf >= sriov->num_vfs)
		return -EINVAL;

	vf_info = &sriov->vf_info[vf];
	vp = vf_info->vp;
	if (test_bit(QLC_BC_VF_STATE, &vf_info->state)) {
		netdev_err(netdev,
			   "Spoof check change failed for VF %d, as VF driver is loaded. Please unload VF driver and retry the operation\n",
			   vf);
		return -EOPNOTSUPP;
	}

	vp->spoofchk = chk;
	return 0;
}
