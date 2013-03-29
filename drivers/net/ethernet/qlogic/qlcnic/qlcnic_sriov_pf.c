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

static int qlcnic_sriov_pf_get_vport_handle(struct qlcnic_adapter *, u8);

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
	int ret = -EIO, vpid;
	u32 temp, num_vf_macs, num_vfs, max;

	vpid = qlcnic_sriov_pf_get_vport_handle(adapter, func);
	if (vpid < 0)
		return -EINVAL;

	num_vfs = sriov->num_vfs;
	max = num_vfs + 1;
	info->bit_offsets = 0xffff;
	info->min_tx_bw = 0;
	info->max_tx_bw = MAX_BW;
	info->max_tx_ques = res->num_tx_queues / max;
	info->max_rx_mcast_mac_filters = res->num_rx_mcast_mac_filters;
	num_vf_macs = QLCNIC_SRIOV_VF_MAX_MAC;

	if (adapter->ahw->pci_func == func) {
		temp = res->num_rx_mcast_mac_filters - (num_vfs * num_vf_macs);
		info->max_rx_ucast_mac_filters = temp;
		temp = res->num_tx_mac_filters - (num_vfs * num_vf_macs);
		info->max_tx_mac_filters = temp;
	} else {
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

	if (adapter->ahw->pci_func == func)
		sriov->vp_handle = 0;
}

static void qlcnic_sriov_pf_set_vport_handle(struct qlcnic_adapter *adapter,
					     u16 vport_handle, u8 func)
{
	struct qlcnic_sriov  *sriov = adapter->ahw->sriov;

	if (adapter->ahw->pci_func == func)
		sriov->vp_handle = vport_handle;
}

static int qlcnic_sriov_pf_get_vport_handle(struct qlcnic_adapter *adapter,
					    u8 func)
{
	struct qlcnic_sriov  *sriov = adapter->ahw->sriov;

	if (adapter->ahw->pci_func == func)
		return sriov->vp_handle;

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

void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *adapter)
{
	u8 func = adapter->ahw->pci_func;

	if (!qlcnic_sriov_enable_check(adapter))
		return;

	qlcnic_sriov_pf_config_vport(adapter, 0, func);
	qlcnic_sriov_pf_cfg_eswitch(adapter, func, 0);
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

	err = qlcnic_sriov_pf_cfg_eswitch(adapter, func, 1);
	if (err)
		goto clear_sriov_enable;

	err = qlcnic_sriov_pf_config_vport(adapter, 1, func);
	if (err)
		goto disable_eswitch;

	err = qlcnic_sriov_get_pf_info(adapter, &pf_info);
	if (err)
		goto delete_vport;

	qlcnic_sriov_pf_set_ff_max_res(adapter, &pf_info);

	err = qlcnic_get_nic_info(adapter, &nic_info, func);
	if (err)
		goto delete_vport;

	err = qlcnic_sriov_pf_cal_res_limit(adapter, &vp_info, func);
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

clear_sriov_enable:
	__qlcnic_sriov_cleanup(adapter);
	adapter->ahw->op_mode = QLCNIC_MGMT_FUNC;
	clear_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
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

	if (qlcnic_sriov_init(adapter, num_vfs)) {
		clear_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
		adapter->ahw->op_mode = QLCNIC_MGMT_FUNC;
		return -EIO;
	}

	if (qlcnic_sriov_pf_init(adapter))
		return -EIO;

	err = qlcnic_sriov_pf_enable(adapter, num_vfs);
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

		if (qlcnic_83xx_configure_opmode(adapter))
			goto error;
	} else {
		netdev_info(adapter->netdev,
			    "SR-IOV is enabled successfully on port %d\n",
			    adapter->portnum);
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
