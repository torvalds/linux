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

	case OCTNET_CMD_VLAN_FILTER_CTL:
		if (nctrl->ncmd.s.param1)
			dev_info(&oct->pci_dev->dev,
				 "%s VLAN filter enabled\n", netdev->name);
		else
			dev_info(&oct->pci_dev->dev,
				 "%s VLAN filter disabled\n", netdev->name);
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

/* Runs in interrupt context. */
void lio_update_txq_status(struct octeon_device *oct, int iq_num)
{
	struct octeon_instr_queue *iq = oct->instr_queue[iq_num];
	struct net_device *netdev;
	struct lio *lio;

	netdev = oct->props[iq->ifidx].netdev;

	/* This is needed because the first IQ does not have
	 * a netdev associated with it.
	 */
	if (!netdev)
		return;

	lio = GET_LIO(netdev);
	if (netif_is_multiqueue(netdev)) {
		if (__netif_subqueue_stopped(netdev, iq->q_index) &&
		    lio->linfo.link.s.link_up &&
		    (!octnet_iq_is_full(oct, iq_num))) {
			netif_wake_subqueue(netdev, iq->q_index);
			INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, iq_num,
						  tx_restart, 1);
		}
	} else if (netif_queue_stopped(netdev) &&
		   lio->linfo.link.s.link_up &&
		   (!octnet_iq_is_full(oct, lio->txq))) {
		INCR_INSTRQUEUE_PKT_COUNT(lio->oct_dev, lio->txq,
					  tx_restart, 1);
		netif_wake_queue(netdev);
	}
}

/**
 * \brief Setup output queue
 * @param oct octeon device
 * @param q_no which queue
 * @param num_descs how many descriptors
 * @param desc_size size of each descriptor
 * @param app_ctx application context
 */
int octeon_setup_droq(struct octeon_device *oct, int q_no, int num_descs,
		      int desc_size, void *app_ctx)
{
	int ret_val;

	dev_dbg(&oct->pci_dev->dev, "Creating Droq: %d\n", q_no);
	/* droq creation and local register settings. */
	ret_val = octeon_create_droq(oct, q_no, num_descs, desc_size, app_ctx);
	if (ret_val < 0)
		return ret_val;

	if (ret_val == 1) {
		dev_dbg(&oct->pci_dev->dev, "Using default droq %d\n", q_no);
		return 0;
	}

	/* Enable the droq queues */
	octeon_set_droq_pkt_op(oct, q_no, 1);

	/* Send Credit for Octeon Output queues. Credits are always
	 * sent after the output queue is enabled.
	 */
	writel(oct->droq[q_no]->max_count, oct->droq[q_no]->pkts_credit_reg);

	return ret_val;
}

/** Routine to push packets arriving on Octeon interface upto network layer.
 * @param oct_id   - octeon device id.
 * @param skbuff   - skbuff struct to be passed to network layer.
 * @param len      - size of total data received.
 * @param rh       - Control header associated with the packet
 * @param param    - additional control data with the packet
 * @param arg      - farg registered in droq_ops
 */
void
liquidio_push_packet(u32 octeon_id __attribute__((unused)),
		     void *skbuff,
		     u32 len,
		     union octeon_rh *rh,
		     void *param,
		     void *arg)
{
	struct net_device *netdev = (struct net_device *)arg;
	struct octeon_droq *droq =
	    container_of(param, struct octeon_droq, napi);
	struct sk_buff *skb = (struct sk_buff *)skbuff;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct napi_struct *napi = param;
	u16 vtag = 0;
	u32 r_dh_off;
	u64 ns;

	if (netdev) {
		struct lio *lio = GET_LIO(netdev);
		struct octeon_device *oct = lio->oct_dev;
		int packet_was_received;

		/* Do not proceed if the interface is not in RUNNING state. */
		if (!ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
			recv_buffer_free(skb);
			droq->stats.rx_dropped++;
			return;
		}

		skb->dev = netdev;

		skb_record_rx_queue(skb, droq->q_no);
		if (likely(len > MIN_SKB_SIZE)) {
			struct octeon_skb_page_info *pg_info;
			unsigned char *va;

			pg_info = ((struct octeon_skb_page_info *)(skb->cb));
			if (pg_info->page) {
				/* For Paged allocation use the frags */
				va = page_address(pg_info->page) +
					pg_info->page_offset;
				memcpy(skb->data, va, MIN_SKB_SIZE);
				skb_put(skb, MIN_SKB_SIZE);
				skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
						pg_info->page,
						pg_info->page_offset +
						MIN_SKB_SIZE,
						len - MIN_SKB_SIZE,
						LIO_RXBUFFER_SZ);
			}
		} else {
			struct octeon_skb_page_info *pg_info =
				((struct octeon_skb_page_info *)(skb->cb));
			skb_copy_to_linear_data(skb, page_address(pg_info->page)
						+ pg_info->page_offset, len);
			skb_put(skb, len);
			put_page(pg_info->page);
		}

		r_dh_off = (rh->r_dh.len - 1) * BYTES_PER_DHLEN_UNIT;

		if (oct->ptp_enable) {
			if (rh->r_dh.has_hwtstamp) {
				/* timestamp is included from the hardware at
				 * the beginning of the packet.
				 */
				if (ifstate_check
					(lio,
					 LIO_IFSTATE_RX_TIMESTAMP_ENABLED)) {
					/* Nanoseconds are in the first 64-bits
					 * of the packet.
					 */
					memcpy(&ns, (skb->data + r_dh_off),
					       sizeof(ns));
					r_dh_off -= BYTES_PER_DHLEN_UNIT;
					shhwtstamps = skb_hwtstamps(skb);
					shhwtstamps->hwtstamp =
						ns_to_ktime(ns +
							    lio->ptp_adjust);
				}
			}
		}

		if (rh->r_dh.has_hash) {
			__be32 *hash_be = (__be32 *)(skb->data + r_dh_off);
			u32 hash = be32_to_cpu(*hash_be);

			skb_set_hash(skb, hash, PKT_HASH_TYPE_L4);
			r_dh_off -= BYTES_PER_DHLEN_UNIT;
		}

		skb_pull(skb, rh->r_dh.len * BYTES_PER_DHLEN_UNIT);
		skb->protocol = eth_type_trans(skb, skb->dev);

		if ((netdev->features & NETIF_F_RXCSUM) &&
		    (((rh->r_dh.encap_on) &&
		      (rh->r_dh.csum_verified & CNNIC_TUN_CSUM_VERIFIED)) ||
		     (!(rh->r_dh.encap_on) &&
		      (rh->r_dh.csum_verified & CNNIC_CSUM_VERIFIED))))
			/* checksum has already been verified */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		/* Setting Encapsulation field on basis of status received
		 * from the firmware
		 */
		if (rh->r_dh.encap_on) {
			skb->encapsulation = 1;
			skb->csum_level = 1;
			droq->stats.rx_vxlan++;
		}

		/* inbound VLAN tag */
		if ((netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
		    rh->r_dh.vlan) {
			u16 priority = rh->r_dh.priority;
			u16 vid = rh->r_dh.vlan;

			vtag = (priority << VLAN_PRIO_SHIFT) | vid;
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vtag);
		}

		packet_was_received = (napi_gro_receive(napi, skb) != GRO_DROP);

		if (packet_was_received) {
			droq->stats.rx_bytes_received += len;
			droq->stats.rx_pkts_received++;
		} else {
			droq->stats.rx_dropped++;
			netif_info(lio, rx_err, lio->netdev,
				   "droq:%d  error rx_dropped:%llu\n",
				   droq->q_no, droq->stats.rx_dropped);
		}

	} else {
		recv_buffer_free(skb);
	}
}

/**
 * \brief wrapper for calling napi_schedule
 * @param param parameters to pass to napi_schedule
 *
 * Used when scheduling on different CPUs
 */
static void napi_schedule_wrapper(void *param)
{
	struct napi_struct *napi = param;

	napi_schedule(napi);
}

/**
 * \brief callback when receive interrupt occurs and we are in NAPI mode
 * @param arg pointer to octeon output queue
 */
void liquidio_napi_drv_callback(void *arg)
{
	struct octeon_device *oct;
	struct octeon_droq *droq = arg;
	int this_cpu = smp_processor_id();

	oct = droq->oct_dev;

	if (OCTEON_CN23XX_PF(oct) || OCTEON_CN23XX_VF(oct) ||
	    droq->cpu_id == this_cpu) {
		napi_schedule_irqoff(&droq->napi);
	} else {
		struct call_single_data *csd = &droq->csd;

		csd->func = napi_schedule_wrapper;
		csd->info = &droq->napi;
		csd->flags = 0;

		smp_call_function_single_async(droq->cpu_id, csd);
	}
}
