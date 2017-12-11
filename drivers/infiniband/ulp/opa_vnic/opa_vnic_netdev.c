/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains OPA Virtual Network Interface Controller (VNIC) driver
 * netdev functionality.
 */

#include <linux/module.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>

#include "opa_vnic_internal.h"

#define OPA_TX_TIMEOUT_MS 1000

#define OPA_VNIC_SKB_HEADROOM  \
			ALIGN((OPA_VNIC_HDR_LEN + OPA_VNIC_SKB_MDATA_LEN), 8)

/* This function is overloaded for opa_vnic specific implementation */
static void opa_vnic_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	struct opa_vnic_stats vstats;

	memset(&vstats, 0, sizeof(vstats));
	spin_lock(&adapter->stats_lock);
	adapter->rn_ops->ndo_get_stats64(netdev, &vstats.netstats);
	spin_unlock(&adapter->stats_lock);
	memcpy(stats, &vstats.netstats, sizeof(*stats));
}

/* opa_netdev_start_xmit - transmit function */
static netdev_tx_t opa_netdev_start_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);

	v_dbg("xmit: queue %d skb len %d\n", skb->queue_mapping, skb->len);
	/* pad to ensure mininum ethernet packet length */
	if (unlikely(skb->len < ETH_ZLEN)) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;

		skb_put(skb, ETH_ZLEN - skb->len);
	}

	opa_vnic_encap_skb(adapter, skb);
	return adapter->rn_ops->ndo_start_xmit(skb, netdev);
}

static u16 opa_vnic_select_queue(struct net_device *netdev, struct sk_buff *skb,
				 void *accel_priv,
				 select_queue_fallback_t fallback)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	struct opa_vnic_skb_mdata *mdata;
	int rc;

	/* pass entropy and vl as metadata in skb */
	mdata = skb_push(skb, sizeof(*mdata));
	mdata->entropy =  opa_vnic_calc_entropy(adapter, skb);
	mdata->vl = opa_vnic_get_vl(adapter, skb);
	rc = adapter->rn_ops->ndo_select_queue(netdev, skb,
					       accel_priv, fallback);
	skb_pull(skb, sizeof(*mdata));
	return rc;
}

static void opa_vnic_update_state(struct opa_vnic_adapter *adapter, bool up)
{
	struct __opa_veswport_info *info = &adapter->info;

	mutex_lock(&adapter->lock);
	/* Operational state can only be DROP_ALL or FORWARDING */
	if ((info->vport.config_state == OPA_VNIC_STATE_FORWARDING) && up) {
		info->vport.oper_state = OPA_VNIC_STATE_FORWARDING;
		info->vport.eth_link_status = OPA_VNIC_ETH_LINK_UP;
	} else {
		info->vport.oper_state = OPA_VNIC_STATE_DROP_ALL;
		info->vport.eth_link_status = OPA_VNIC_ETH_LINK_DOWN;
	}

	if (info->vport.config_state == OPA_VNIC_STATE_FORWARDING)
		netif_dormant_off(adapter->netdev);
	else
		netif_dormant_on(adapter->netdev);
	mutex_unlock(&adapter->lock);
}

/* opa_vnic_process_vema_config - process vema configuration updates */
void opa_vnic_process_vema_config(struct opa_vnic_adapter *adapter)
{
	struct __opa_veswport_info *info = &adapter->info;
	struct rdma_netdev *rn = netdev_priv(adapter->netdev);
	u8 port_num[OPA_VESW_MAX_NUM_DEF_PORT] = { 0 };
	struct net_device *netdev = adapter->netdev;
	u8 i, port_count = 0;
	u16 port_mask;

	/* If the base_mac_addr is changed, update the interface mac address */
	if (memcmp(info->vport.base_mac_addr, adapter->vema_mac_addr,
		   ARRAY_SIZE(info->vport.base_mac_addr))) {
		struct sockaddr saddr;

		memcpy(saddr.sa_data, info->vport.base_mac_addr,
		       ARRAY_SIZE(info->vport.base_mac_addr));
		mutex_lock(&adapter->lock);
		eth_commit_mac_addr_change(netdev, &saddr);
		memcpy(adapter->vema_mac_addr,
		       info->vport.base_mac_addr, ETH_ALEN);
		mutex_unlock(&adapter->lock);
	}

	rn->set_id(netdev, info->vesw.vesw_id);

	/* Handle MTU limit change */
	rtnl_lock();
	netdev->max_mtu = max_t(unsigned int, info->vesw.eth_mtu,
				netdev->min_mtu);
	if (netdev->mtu > netdev->max_mtu)
		dev_set_mtu(netdev, netdev->max_mtu);
	rtnl_unlock();

	/* Update flow to default port redirection table */
	port_mask = info->vesw.def_port_mask;
	for (i = 0; i < OPA_VESW_MAX_NUM_DEF_PORT; i++) {
		if (port_mask & 1)
			port_num[port_count++] = i;
		port_mask >>= 1;
	}

	/*
	 * Build the flow table. Flow table is required when destination LID
	 * is not available. Up to OPA_VNIC_FLOW_TBL_SIZE flows supported.
	 * Each flow need a default port number to get its dlid from the
	 * u_ucast_dlid array.
	 */
	for (i = 0; i < OPA_VNIC_FLOW_TBL_SIZE; i++)
		adapter->flow_tbl[i] = port_count ? port_num[i % port_count] :
						    OPA_VNIC_INVALID_PORT;

	/* update state */
	opa_vnic_update_state(adapter, !!(netdev->flags & IFF_UP));
}

/*
 * Set the power on default values in adapter's vema interface structure.
 */
static inline void opa_vnic_set_pod_values(struct opa_vnic_adapter *adapter)
{
	adapter->info.vport.max_mac_tbl_ent = OPA_VNIC_MAC_TBL_MAX_ENTRIES;
	adapter->info.vport.max_smac_ent = OPA_VNIC_MAX_SMAC_LIMIT;
	adapter->info.vport.config_state = OPA_VNIC_STATE_DROP_ALL;
	adapter->info.vport.eth_link_status = OPA_VNIC_ETH_LINK_DOWN;
	adapter->info.vesw.eth_mtu = ETH_DATA_LEN;
}

/* opa_vnic_set_mac_addr - change mac address */
static int opa_vnic_set_mac_addr(struct net_device *netdev, void *addr)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	struct sockaddr *sa = addr;
	int rc;

	if (!memcmp(netdev->dev_addr, sa->sa_data, ETH_ALEN))
		return 0;

	mutex_lock(&adapter->lock);
	rc = eth_mac_addr(netdev, addr);
	mutex_unlock(&adapter->lock);
	if (rc)
		return rc;

	adapter->info.vport.uc_macs_gen_count++;
	opa_vnic_vema_report_event(adapter,
				   OPA_VESWPORT_TRAP_IFACE_UCAST_MAC_CHANGE);
	return 0;
}

/*
 * opa_vnic_mac_send_event - post event on possible mac list exchange
 *  Send trap when digest from uc/mc mac list differs from previous run.
 *  Digest is evaluated similar to how cksum does.
 */
static void opa_vnic_mac_send_event(struct net_device *netdev, u8 event)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	struct netdev_hw_addr *ha;
	struct netdev_hw_addr_list *hw_list;
	u32 *ref_crc;
	u32 l, crc = 0;

	switch (event) {
	case OPA_VESWPORT_TRAP_IFACE_UCAST_MAC_CHANGE:
		hw_list = &netdev->uc;
		adapter->info.vport.uc_macs_gen_count++;
		ref_crc = &adapter->umac_hash;
		break;
	case OPA_VESWPORT_TRAP_IFACE_MCAST_MAC_CHANGE:
		hw_list = &netdev->mc;
		adapter->info.vport.mc_macs_gen_count++;
		ref_crc = &adapter->mmac_hash;
		break;
	default:
		return;
	}
	netdev_hw_addr_list_for_each(ha, hw_list) {
		crc = crc32_le(crc, ha->addr, ETH_ALEN);
	}
	l = netdev_hw_addr_list_count(hw_list) * ETH_ALEN;
	crc = ~crc32_le(crc, (void *)&l, sizeof(l));

	if (crc != *ref_crc) {
		*ref_crc = crc;
		opa_vnic_vema_report_event(adapter, event);
	}
}

/* opa_vnic_set_rx_mode - handle uc/mc mac list change */
static void opa_vnic_set_rx_mode(struct net_device *netdev)
{
	opa_vnic_mac_send_event(netdev,
				OPA_VESWPORT_TRAP_IFACE_UCAST_MAC_CHANGE);

	opa_vnic_mac_send_event(netdev,
				OPA_VESWPORT_TRAP_IFACE_MCAST_MAC_CHANGE);
}

/* opa_netdev_open - activate network interface */
static int opa_netdev_open(struct net_device *netdev)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	int rc;

	rc = adapter->rn_ops->ndo_open(adapter->netdev);
	if (rc) {
		v_dbg("open failed %d\n", rc);
		return rc;
	}

	/* Update status and send trap */
	opa_vnic_update_state(adapter, true);
	opa_vnic_vema_report_event(adapter,
				   OPA_VESWPORT_TRAP_ETH_LINK_STATUS_CHANGE);
	return 0;
}

/* opa_netdev_close - disable network interface */
static int opa_netdev_close(struct net_device *netdev)
{
	struct opa_vnic_adapter *adapter = opa_vnic_priv(netdev);
	int rc;

	rc = adapter->rn_ops->ndo_stop(adapter->netdev);
	if (rc) {
		v_dbg("close failed %d\n", rc);
		return rc;
	}

	/* Update status and send trap */
	opa_vnic_update_state(adapter, false);
	opa_vnic_vema_report_event(adapter,
				   OPA_VESWPORT_TRAP_ETH_LINK_STATUS_CHANGE);
	return 0;
}

/* netdev ops */
static const struct net_device_ops opa_netdev_ops = {
	.ndo_open = opa_netdev_open,
	.ndo_stop = opa_netdev_close,
	.ndo_start_xmit = opa_netdev_start_xmit,
	.ndo_get_stats64 = opa_vnic_get_stats64,
	.ndo_set_rx_mode = opa_vnic_set_rx_mode,
	.ndo_select_queue = opa_vnic_select_queue,
	.ndo_set_mac_address = opa_vnic_set_mac_addr,
};

/* opa_vnic_add_netdev - create vnic netdev interface */
struct opa_vnic_adapter *opa_vnic_add_netdev(struct ib_device *ibdev,
					     u8 port_num, u8 vport_num)
{
	struct opa_vnic_adapter *adapter;
	struct net_device *netdev;
	struct rdma_netdev *rn;
	int rc;

	netdev = ibdev->alloc_rdma_netdev(ibdev, port_num,
					  RDMA_NETDEV_OPA_VNIC,
					  "veth%d", NET_NAME_UNKNOWN,
					  ether_setup);
	if (!netdev)
		return ERR_PTR(-ENOMEM);
	else if (IS_ERR(netdev))
		return ERR_CAST(netdev);

	rn = netdev_priv(netdev);
	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		rc = -ENOMEM;
		goto adapter_err;
	}

	rn->clnt_priv = adapter;
	rn->hca = ibdev;
	rn->port_num = port_num;
	adapter->netdev = netdev;
	adapter->ibdev = ibdev;
	adapter->port_num = port_num;
	adapter->vport_num = vport_num;
	adapter->rn_ops = netdev->netdev_ops;

	netdev->netdev_ops = &opa_netdev_ops;
	netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	netdev->hard_header_len += OPA_VNIC_SKB_HEADROOM;
	mutex_init(&adapter->lock);
	mutex_init(&adapter->mactbl_lock);
	spin_lock_init(&adapter->stats_lock);

	SET_NETDEV_DEV(netdev, ibdev->dev.parent);

	opa_vnic_set_ethtool_ops(netdev);

	opa_vnic_set_pod_values(adapter);

	rc = register_netdev(netdev);
	if (rc)
		goto netdev_err;

	netif_carrier_off(netdev);
	netif_dormant_on(netdev);
	v_info("initialized\n");

	return adapter;
netdev_err:
	mutex_destroy(&adapter->lock);
	mutex_destroy(&adapter->mactbl_lock);
	kfree(adapter);
adapter_err:
	rn->free_rdma_netdev(netdev);

	return ERR_PTR(rc);
}

/* opa_vnic_rem_netdev - remove vnic netdev interface */
void opa_vnic_rem_netdev(struct opa_vnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct rdma_netdev *rn = netdev_priv(netdev);

	v_info("removing\n");
	unregister_netdev(netdev);
	opa_vnic_release_mac_tbl(adapter);
	mutex_destroy(&adapter->lock);
	mutex_destroy(&adapter->mactbl_lock);
	kfree(adapter);
	rn->free_rdma_netdev(netdev);
}
