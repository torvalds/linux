/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/if_vlan.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"
#include "octeon_network.h"

/* OOM task polling interval */
#define LIO_OOM_POLL_INTERVAL_MS 250

int liquidio_set_feature(struct net_device *netdev, int cmd, u16 param1)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	struct octnic_ctrl_pkt nctrl;
	int ret = 0;

	memset(&nctrl, 0, sizeof(struct octnic_ctrl_pkt));

	nctrl.ncmd.u64 = 0;
	nctrl.ncmd.s.cmd = cmd;
	nctrl.ncmd.s.param1 = param1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.netpndev = (u64)netdev;
	nctrl.cb_fn = liquidio_link_ctrl_cmd_completion;

	ret = octnet_send_nic_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		dev_err(&oct->pci_dev->dev, "Feature change failed in core (ret: 0x%x)\n",
			ret);
	}
	return ret;
}

void octeon_report_tx_completion_to_bql(void *txq, unsigned int pkts_compl,
					unsigned int bytes_compl)
{
	struct netdev_queue *netdev_queue = txq;

	netdev_tx_completed_queue(netdev_queue, pkts_compl, bytes_compl);
}

void octeon_update_tx_completion_counters(void *buf, int reqtype,
					  unsigned int *pkts_compl,
					  unsigned int *bytes_compl)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb = NULL;
	struct octeon_soft_command *sc;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	(*pkts_compl)++;
	*bytes_compl += skb->len;
}

void octeon_report_sent_bytes_to_bql(void *buf, int reqtype)
{
	struct octnet_buf_free_info *finfo;
	struct sk_buff *skb;
	struct octeon_soft_command *sc;
	struct netdev_queue *txq;

	switch (reqtype) {
	case REQTYPE_NORESP_NET:
	case REQTYPE_NORESP_NET_SG:
		finfo = buf;
		skb = finfo->skb;
		break;

	case REQTYPE_RESP_NET_SG:
	case REQTYPE_RESP_NET:
		sc = buf;
		skb = sc->callback_arg;
		break;

	default:
		return;
	}

	txq = netdev_get_tx_queue(skb->dev, skb_get_queue_mapping(skb));
	netdev_tx_sent_queue(txq, skb->len);
}

void liquidio_link_ctrl_cmd_completion(void *nctrl_ptr)
{
	struct octnic_ctrl_pkt *nctrl = (struct octnic_ctrl_pkt *)nctrl_ptr;
	struct net_device *netdev = (struct net_device *)nctrl->netpndev;
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;
	u8 *mac;

	if (nctrl->completion && nctrl->response_code) {
		/* Signal whoever is interested that the response code from the
		 * firmware has arrived.
		 */
		WRITE_ONCE(*nctrl->response_code, nctrl->status);
		complete(nctrl->completion);
	}

	if (nctrl->status)
		return;

	switch (nctrl->ncmd.s.cmd) {
	case OCTNET_CMD_CHANGE_DEVFLAGS:
	case OCTNET_CMD_SET_MULTI_LIST:
		break;

	case OCTNET_CMD_CHANGE_MACADDR:
		mac = ((u8 *)&nctrl->udd[0]) + 2;
		if (nctrl->ncmd.s.param1) {
			/* vfidx is 0 based, but vf_num (param1) is 1 based */
			int vfidx = nctrl->ncmd.s.param1 - 1;
			bool mac_is_admin_assigned = nctrl->ncmd.s.param2;

			if (mac_is_admin_assigned)
				netif_info(lio, probe, lio->netdev,
					   "MAC Address %pM is configured for VF %d\n",
					   mac, vfidx);
		} else {
			netif_info(lio, probe, lio->netdev,
				   " MACAddr changed to %pM\n",
				   mac);
		}
		break;

	case OCTNET_CMD_CHANGE_MTU:
		/* If command is successful, change the MTU. */
		netif_info(lio, probe, lio->netdev, "MTU Changed from %d to %d\n",
			   netdev->mtu, nctrl->ncmd.s.param1);
		dev_info(&oct->pci_dev->dev, "%s MTU Changed from %d to %d\n",
			 netdev->name, netdev->mtu,
			 nctrl->ncmd.s.param1);
		netdev->mtu = nctrl->ncmd.s.param1;
		queue_delayed_work(lio->link_status_wq.wq,
				   &lio->link_status_wq.wk.work, 0);
		break;

	case OCTNET_CMD_GPIO_ACCESS:
		netif_info(lio, probe, lio->netdev, "LED Flashing visual identification\n");

		break;

	case OCTNET_CMD_ID_ACTIVE:
		netif_info(lio, probe, lio->netdev, "LED Flashing visual identification\n");

		break;

	case OCTNET_CMD_LRO_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Enabled\n", netdev->name);
		break;

	case OCTNET_CMD_LRO_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s LRO Disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_ENABLE:
		dev_info(&oct->pci_dev->dev, "%s Firmware debug enabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_VERBOSE_DISABLE:
		dev_info(&oct->pci_dev->dev, "%s Firmware debug disabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_ENABLE_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter enabled\n",
			 netdev->name);
		break;

	case OCTNET_CMD_ADD_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter %d added\n",
			 netdev->name, nctrl->ncmd.s.param1);
		break;

	case OCTNET_CMD_DEL_VLAN_FILTER:
		dev_info(&oct->pci_dev->dev, "%s VLAN filter %d removed\n",
			 netdev->name, nctrl->ncmd.s.param1);
		break;

	case OCTNET_CMD_SET_SETTINGS:
		dev_info(&oct->pci_dev->dev, "%s settings changed\n",
			 netdev->name);

		break;

	/* Case to handle "OCTNET_CMD_TNL_RX_CSUM_CTL"
	 * Command passed by NIC driver
	 */
	case OCTNET_CMD_TNL_RX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == OCTNET_CMD_RXCSUM_ENABLE) {
			netif_info(lio, probe, lio->netdev,
				   "RX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 ==
			   OCTNET_CMD_RXCSUM_DISABLE) {
			netif_info(lio, probe, lio->netdev,
				   "RX Checksum Offload Disabled\n");
		}
		break;

		/* Case to handle "OCTNET_CMD_TNL_TX_CSUM_CTL"
		 * Command passed by NIC driver
		 */
	case OCTNET_CMD_TNL_TX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == OCTNET_CMD_TXCSUM_ENABLE) {
			netif_info(lio, probe, lio->netdev,
				   "TX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 ==
			   OCTNET_CMD_TXCSUM_DISABLE) {
			netif_info(lio, probe, lio->netdev,
				   "TX Checksum Offload Disabled\n");
		}
		break;

		/* Case to handle "OCTNET_CMD_VXLAN_PORT_CONFIG"
		 * Command passed by NIC driver
		 */
	case OCTNET_CMD_VXLAN_PORT_CONFIG:
		if (nctrl->ncmd.s.more == OCTNET_CMD_VXLAN_PORT_ADD) {
			netif_info(lio, probe, lio->netdev,
				   "VxLAN Destination UDP PORT:%d ADDED\n",
				   nctrl->ncmd.s.param1);
		} else if (nctrl->ncmd.s.more ==
			   OCTNET_CMD_VXLAN_PORT_DEL) {
			netif_info(lio, probe, lio->netdev,
				   "VxLAN Destination UDP PORT:%d DELETED\n",
				   nctrl->ncmd.s.param1);
		}
		break;

	case OCTNET_CMD_SET_FLOW_CTL:
		netif_info(lio, probe, lio->netdev, "Set RX/TX flow control parameters\n");
		break;

	default:
		dev_err(&oct->pci_dev->dev, "%s Unknown cmd %d\n", __func__,
			nctrl->ncmd.s.cmd);
	}
}

void octeon_pf_changed_vf_macaddr(struct octeon_device *oct, u8 *mac)
{
	bool macaddr_changed = false;
	struct net_device *netdev;
	struct lio *lio;

	rtnl_lock();

	netdev = oct->props[0].netdev;
	lio = GET_LIO(netdev);

	lio->linfo.macaddr_is_admin_asgnd = true;

	if (!ether_addr_equal(netdev->dev_addr, mac)) {
		macaddr_changed = true;
		ether_addr_copy(netdev->dev_addr, mac);
		ether_addr_copy(((u8 *)&lio->linfo.hw_addr) + 2, mac);
		call_netdevice_notifiers(NETDEV_CHANGEADDR, netdev);
	}

	rtnl_unlock();

	if (macaddr_changed)
		dev_info(&oct->pci_dev->dev,
			 "PF changed VF's MAC address to %pM\n", mac);

	/* no need to notify the firmware of the macaddr change because
	 * the PF did that already
	 */
}

static void octnet_poll_check_rxq_oom_status(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct lio *lio = (struct lio *)wk->ctxptr;
	struct octeon_device *oct = lio->oct_dev;
	struct octeon_droq *droq;
	int q, q_no = 0;

	if (ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
		for (q = 0; q < lio->linfo.num_rxpciq; q++) {
			q_no = lio->linfo.rxpciq[q].s.q_no;
			droq = oct->droq[q_no];
			if (!droq)
				continue;
			octeon_droq_check_oom(droq);
		}
	}
	queue_delayed_work(lio->rxq_status_wq.wq,
			   &lio->rxq_status_wq.wk.work,
			   msecs_to_jiffies(LIO_OOM_POLL_INTERVAL_MS));
}

int setup_rx_oom_poll_fn(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);
	struct octeon_device *oct = lio->oct_dev;

	lio->rxq_status_wq.wq = alloc_workqueue("rxq-oom-status",
						WQ_MEM_RECLAIM, 0);
	if (!lio->rxq_status_wq.wq) {
		dev_err(&oct->pci_dev->dev, "unable to create cavium rxq oom status wq\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&lio->rxq_status_wq.wk.work,
			  octnet_poll_check_rxq_oom_status);
	lio->rxq_status_wq.wk.ctxptr = lio;
	queue_delayed_work(lio->rxq_status_wq.wq,
			   &lio->rxq_status_wq.wk.work,
			   msecs_to_jiffies(LIO_OOM_POLL_INTERVAL_MS));
	return 0;
}

void cleanup_rx_oom_poll_fn(struct net_device *netdev)
{
	struct lio *lio = GET_LIO(netdev);

	if (lio->rxq_status_wq.wq) {
		cancel_delayed_work_sync(&lio->rxq_status_wq.wk.work);
		flush_workqueue(lio->rxq_status_wq.wq);
		destroy_workqueue(lio->rxq_status_wq.wq);
	}
}
