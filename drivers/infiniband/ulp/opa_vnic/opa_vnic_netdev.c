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

#include "opa_vnic_internal.h"

#define OPA_TX_TIMEOUT_MS 1000

#define OPA_VNIC_SKB_HEADROOM  \
			ALIGN((OPA_VNIC_HDR_LEN + OPA_VNIC_SKB_MDATA_LEN), 8)

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
	mdata = (struct opa_vnic_skb_mdata *)skb_push(skb, sizeof(*mdata));
	mdata->entropy =  opa_vnic_calc_entropy(adapter, skb);
	mdata->vl = opa_vnic_get_vl(adapter, skb);
	rc = adapter->rn_ops->ndo_select_queue(netdev, skb,
					       accel_priv, fallback);
	skb_pull(skb, sizeof(*mdata));
	return rc;
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

	return rc;
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

	return 0;
}

/* netdev ops */
static const struct net_device_ops opa_netdev_ops = {
	.ndo_open = opa_netdev_open,
	.ndo_stop = opa_netdev_close,
	.ndo_start_xmit = opa_netdev_start_xmit,
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

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		rc = -ENOMEM;
		goto adapter_err;
	}

	rn = netdev_priv(netdev);
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

	SET_NETDEV_DEV(netdev, ibdev->dev.parent);

	opa_vnic_set_ethtool_ops(netdev);
	rc = register_netdev(netdev);
	if (rc)
		goto netdev_err;

	netif_carrier_off(netdev);
	netif_dormant_on(netdev);
	v_info("initialized\n");

	return adapter;
netdev_err:
	mutex_destroy(&adapter->lock);
	kfree(adapter);
adapter_err:
	ibdev->free_rdma_netdev(netdev);

	return ERR_PTR(rc);
}

/* opa_vnic_rem_netdev - remove vnic netdev interface */
void opa_vnic_rem_netdev(struct opa_vnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ib_device *ibdev = adapter->ibdev;

	v_info("removing\n");
	unregister_netdev(netdev);
	mutex_destroy(&adapter->lock);
	kfree(adapter);
	ibdev->free_rdma_netdev(netdev);
}
