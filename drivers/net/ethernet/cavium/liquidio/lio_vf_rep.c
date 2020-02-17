/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2017 Cavium, Inc.
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
#include <net/switchdev.h>
#include "lio_vf_rep.h"
#include "octeon_network.h"

static int lio_vf_rep_open(struct net_device *ndev);
static int lio_vf_rep_stop(struct net_device *ndev);
static netdev_tx_t lio_vf_rep_pkt_xmit(struct sk_buff *skb,
				       struct net_device *ndev);
static void lio_vf_rep_tx_timeout(struct net_device *netdev);
static int lio_vf_rep_phys_port_name(struct net_device *dev,
				     char *buf, size_t len);
static void lio_vf_rep_get_stats64(struct net_device *dev,
				   struct rtnl_link_stats64 *stats64);
static int lio_vf_rep_change_mtu(struct net_device *ndev, int new_mtu);

static const struct net_device_ops lio_vf_rep_ndev_ops = {
	.ndo_open = lio_vf_rep_open,
	.ndo_stop = lio_vf_rep_stop,
	.ndo_start_xmit = lio_vf_rep_pkt_xmit,
	.ndo_tx_timeout = lio_vf_rep_tx_timeout,
	.ndo_get_phys_port_name = lio_vf_rep_phys_port_name,
	.ndo_get_stats64 = lio_vf_rep_get_stats64,
	.ndo_change_mtu = lio_vf_rep_change_mtu,
};

static void
lio_vf_rep_send_sc_complete(struct octeon_device *oct,
			    u32 status, void *ptr)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)ptr;
	struct lio_vf_rep_sc_ctx *ctx =
		(struct lio_vf_rep_sc_ctx *)sc->ctxptr;
	struct lio_vf_rep_resp *resp =
		(struct lio_vf_rep_resp *)sc->virtrptr;

	if (status != OCTEON_REQUEST_TIMEOUT && READ_ONCE(resp->status))
		WRITE_ONCE(resp->status, 0);

	complete(&ctx->complete);
}

static int
lio_vf_rep_send_soft_command(struct octeon_device *oct,
			     void *req, int req_size,
			     void *resp, int resp_size)
{
	int tot_resp_size = sizeof(struct lio_vf_rep_resp) + resp_size;
	int ctx_size = sizeof(struct lio_vf_rep_sc_ctx);
	struct octeon_soft_command *sc = NULL;
	struct lio_vf_rep_resp *rep_resp;
	struct lio_vf_rep_sc_ctx *ctx;
	void *sc_req;
	int err;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, req_size,
					  tot_resp_size, ctx_size);
	if (!sc)
		return -ENOMEM;

	ctx = (struct lio_vf_rep_sc_ctx *)sc->ctxptr;
	memset(ctx, 0, ctx_size);
	init_completion(&ctx->complete);

	sc_req = (struct lio_vf_rep_req *)sc->virtdptr;
	memcpy(sc_req, req, req_size);

	rep_resp = (struct lio_vf_rep_resp *)sc->virtrptr;
	memset(rep_resp, 0, tot_resp_size);
	WRITE_ONCE(rep_resp->status, 1);

	sc->iq_no = 0;
	octeon_prepare_soft_command(oct, sc, OPCODE_NIC,
				    OPCODE_NIC_VF_REP_CMD, 0, 0, 0);
	sc->callback = lio_vf_rep_send_sc_complete;
	sc->callback_arg = sc;
	sc->wait_time = LIO_VF_REP_REQ_TMO_MS;

	err = octeon_send_soft_command(oct, sc);
	if (err == IQ_SEND_FAILED)
		goto free_buff;

	wait_for_completion_timeout(&ctx->complete,
				    msecs_to_jiffies
				    (2 * LIO_VF_REP_REQ_TMO_MS));
	err = READ_ONCE(rep_resp->status) ? -EBUSY : 0;
	if (err)
		dev_err(&oct->pci_dev->dev, "VF rep send config failed\n");

	if (resp)
		memcpy(resp, (rep_resp + 1), resp_size);
free_buff:
	octeon_free_soft_command(oct, sc);

	return err;
}

static int
lio_vf_rep_open(struct net_device *ndev)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(ndev);
	struct lio_vf_rep_req rep_cfg;
	struct octeon_device *oct;
	int ret;

	oct = vf_rep->oct;

	memset(&rep_cfg, 0, sizeof(rep_cfg));
	rep_cfg.req_type = LIO_VF_REP_REQ_STATE;
	rep_cfg.ifidx = vf_rep->ifidx;
	rep_cfg.rep_state.state = LIO_VF_REP_STATE_UP;

	ret = lio_vf_rep_send_soft_command(oct, &rep_cfg,
					   sizeof(rep_cfg), NULL, 0);

	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"VF_REP open failed with err %d\n", ret);
		return -EIO;
	}

	atomic_set(&vf_rep->ifstate, (atomic_read(&vf_rep->ifstate) |
				      LIO_IFSTATE_RUNNING));

	netif_carrier_on(ndev);
	netif_start_queue(ndev);

	return 0;
}

static int
lio_vf_rep_stop(struct net_device *ndev)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(ndev);
	struct lio_vf_rep_req rep_cfg;
	struct octeon_device *oct;
	int ret;

	oct = vf_rep->oct;

	memset(&rep_cfg, 0, sizeof(rep_cfg));
	rep_cfg.req_type = LIO_VF_REP_REQ_STATE;
	rep_cfg.ifidx = vf_rep->ifidx;
	rep_cfg.rep_state.state = LIO_VF_REP_STATE_DOWN;

	ret = lio_vf_rep_send_soft_command(oct, &rep_cfg,
					   sizeof(rep_cfg), NULL, 0);

	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"VF_REP dev stop failed with err %d\n", ret);
		return -EIO;
	}

	atomic_set(&vf_rep->ifstate, (atomic_read(&vf_rep->ifstate) &
				      ~LIO_IFSTATE_RUNNING));

	netif_tx_disable(ndev);
	netif_carrier_off(ndev);

	return 0;
}

static void
lio_vf_rep_tx_timeout(struct net_device *ndev)
{
	netif_trans_update(ndev);

	netif_wake_queue(ndev);
}

static void
lio_vf_rep_get_stats64(struct net_device *dev,
		       struct rtnl_link_stats64 *stats64)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(dev);

	/* Swap tx and rx stats as VF rep is a switch port */
	stats64->tx_packets = vf_rep->stats.rx_packets;
	stats64->tx_bytes   = vf_rep->stats.rx_bytes;
	stats64->tx_dropped = vf_rep->stats.rx_dropped;

	stats64->rx_packets = vf_rep->stats.tx_packets;
	stats64->rx_bytes   = vf_rep->stats.tx_bytes;
	stats64->rx_dropped = vf_rep->stats.tx_dropped;
}

static int
lio_vf_rep_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(ndev);
	struct lio_vf_rep_req rep_cfg;
	struct octeon_device *oct;
	int ret;

	oct = vf_rep->oct;

	memset(&rep_cfg, 0, sizeof(rep_cfg));
	rep_cfg.req_type = LIO_VF_REP_REQ_MTU;
	rep_cfg.ifidx = vf_rep->ifidx;
	rep_cfg.rep_mtu.mtu = cpu_to_be32(new_mtu);

	ret = lio_vf_rep_send_soft_command(oct, &rep_cfg,
					   sizeof(rep_cfg), NULL, 0);
	if (ret) {
		dev_err(&oct->pci_dev->dev,
			"Change MTU failed with err %d\n", ret);
		return -EIO;
	}

	ndev->mtu = new_mtu;

	return 0;
}

static int
lio_vf_rep_phys_port_name(struct net_device *dev,
			  char *buf, size_t len)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(dev);
	struct octeon_device *oct = vf_rep->oct;
	int ret;

	ret = snprintf(buf, len, "pf%dvf%d", oct->pf_num,
		       vf_rep->ifidx - oct->pf_num * 64 - 1);
	if (ret >= len)
		return -EOPNOTSUPP;

	return 0;
}

static struct net_device *
lio_vf_rep_get_ndev(struct octeon_device *oct, int ifidx)
{
	int vf_id, max_vfs = CN23XX_MAX_VFS_PER_PF + 1;
	int vfid_mask = max_vfs - 1;

	if (ifidx <= oct->pf_num * max_vfs ||
	    ifidx >= oct->pf_num * max_vfs + max_vfs)
		return NULL;

	/* ifidx 1-63 for PF0 VFs
	 * ifidx 65-127 for PF1 VFs
	 */
	vf_id = (ifidx & vfid_mask) - 1;

	return oct->vf_rep_list.ndev[vf_id];
}

static void
lio_vf_rep_copy_packet(struct octeon_device *oct,
		       struct sk_buff *skb,
		       int len)
{
	if (likely(len > MIN_SKB_SIZE)) {
		struct octeon_skb_page_info *pg_info;
		unsigned char *va;

		pg_info = ((struct octeon_skb_page_info *)(skb->cb));
		if (pg_info->page) {
			va = page_address(pg_info->page) +
				pg_info->page_offset;
			memcpy(skb->data, va, MIN_SKB_SIZE);
			skb_put(skb, MIN_SKB_SIZE);
		}

		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				pg_info->page,
				pg_info->page_offset + MIN_SKB_SIZE,
				len - MIN_SKB_SIZE,
				LIO_RXBUFFER_SZ);
	} else {
		struct octeon_skb_page_info *pg_info =
			((struct octeon_skb_page_info *)(skb->cb));

		skb_copy_to_linear_data(skb, page_address(pg_info->page) +
					pg_info->page_offset, len);
		skb_put(skb, len);
		put_page(pg_info->page);
	}
}

static int
lio_vf_rep_pkt_recv(struct octeon_recv_info *recv_info, void *buf)
{
	struct octeon_recv_pkt *recv_pkt = recv_info->recv_pkt;
	struct lio_vf_rep_desc *vf_rep;
	struct net_device *vf_ndev;
	struct octeon_device *oct;
	union octeon_rh *rh;
	struct sk_buff *skb;
	int i, ifidx;

	oct = lio_get_device(recv_pkt->octeon_id);
	if (!oct)
		goto free_buffers;

	skb = recv_pkt->buffer_ptr[0];
	rh = &recv_pkt->rh;
	ifidx = rh->r.ossp;

	vf_ndev = lio_vf_rep_get_ndev(oct, ifidx);
	if (!vf_ndev)
		goto free_buffers;

	vf_rep = netdev_priv(vf_ndev);
	if (!(atomic_read(&vf_rep->ifstate) & LIO_IFSTATE_RUNNING) ||
	    recv_pkt->buffer_count > 1)
		goto free_buffers;

	skb->dev = vf_ndev;

	/* Multiple buffers are not used for vf_rep packets.
	 * So just buffer_size[0] is valid.
	 */
	lio_vf_rep_copy_packet(oct, skb, recv_pkt->buffer_size[0]);

	skb_pull(skb, rh->r_dh.len * BYTES_PER_DHLEN_UNIT);
	skb->protocol = eth_type_trans(skb, skb->dev);
	skb->ip_summed = CHECKSUM_NONE;

	netif_rx(skb);

	octeon_free_recv_info(recv_info);

	return 0;

free_buffers:
	for (i = 0; i < recv_pkt->buffer_count; i++)
		recv_buffer_free(recv_pkt->buffer_ptr[i]);

	octeon_free_recv_info(recv_info);

	return 0;
}

static void
lio_vf_rep_packet_sent_callback(struct octeon_device *oct,
				u32 status, void *buf)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)buf;
	struct sk_buff *skb = sc->ctxptr;
	struct net_device *ndev = skb->dev;
	u32 iq_no;

	dma_unmap_single(&oct->pci_dev->dev, sc->dmadptr,
			 sc->datasize, DMA_TO_DEVICE);
	dev_kfree_skb_any(skb);
	iq_no = sc->iq_no;
	octeon_free_soft_command(oct, sc);

	if (octnet_iq_is_full(oct, iq_no))
		return;

	if (netif_queue_stopped(ndev))
		netif_wake_queue(ndev);
}

static netdev_tx_t
lio_vf_rep_pkt_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(ndev);
	struct net_device *parent_ndev = vf_rep->parent_ndev;
	struct octeon_device *oct = vf_rep->oct;
	struct octeon_instr_pki_ih3 *pki_ih3;
	struct octeon_soft_command *sc;
	struct lio *parent_lio;
	int status;

	parent_lio = GET_LIO(parent_ndev);

	if (!(atomic_read(&vf_rep->ifstate) & LIO_IFSTATE_RUNNING) ||
	    skb->len <= 0)
		goto xmit_failed;

	if (octnet_iq_is_full(vf_rep->oct, parent_lio->txq)) {
		dev_err(&oct->pci_dev->dev, "VF rep: Device IQ full\n");
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, 0, 0, 0);
	if (!sc) {
		dev_err(&oct->pci_dev->dev, "VF rep: Soft command alloc failed\n");
		goto xmit_failed;
	}

	/* Multiple buffers are not used for vf_rep packets. */
	if (skb_shinfo(skb)->nr_frags != 0) {
		dev_err(&oct->pci_dev->dev, "VF rep: nr_frags != 0. Dropping packet\n");
		goto xmit_failed;
	}

	sc->dmadptr = dma_map_single(&oct->pci_dev->dev,
				     skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(&oct->pci_dev->dev, sc->dmadptr)) {
		dev_err(&oct->pci_dev->dev, "VF rep: DMA mapping failed\n");
		goto xmit_failed;
	}

	sc->virtdptr = skb->data;
	sc->datasize = skb->len;
	sc->ctxptr = skb;
	sc->iq_no = parent_lio->txq;

	octeon_prepare_soft_command(oct, sc, OPCODE_NIC, OPCODE_NIC_VF_REP_PKT,
				    vf_rep->ifidx, 0, 0);
	pki_ih3 = (struct octeon_instr_pki_ih3 *)&sc->cmd.cmd3.pki_ih3;
	pki_ih3->tagtype = ORDERED_TAG;

	sc->callback = lio_vf_rep_packet_sent_callback;
	sc->callback_arg = sc;

	status = octeon_send_soft_command(oct, sc);
	if (status == IQ_SEND_FAILED) {
		dma_unmap_single(&oct->pci_dev->dev, sc->dmadptr,
				 sc->datasize, DMA_TO_DEVICE);
		goto xmit_failed;
	}

	if (status == IQ_SEND_STOP)
		netif_stop_queue(ndev);

	netif_trans_update(ndev);

	return NETDEV_TX_OK;

xmit_failed:
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static int
lio_vf_rep_attr_get(struct net_device *dev, struct switchdev_attr *attr)
{
	struct lio_vf_rep_desc *vf_rep = netdev_priv(dev);
	struct net_device *parent_ndev = vf_rep->parent_ndev;
	struct lio *lio = GET_LIO(parent_ndev);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = ETH_ALEN;
		ether_addr_copy(attr->u.ppid.id,
				(void *)&lio->linfo.hw_addr + 2);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct switchdev_ops lio_vf_rep_switchdev_ops = {
	.switchdev_port_attr_get        = lio_vf_rep_attr_get,
};

static void
lio_vf_rep_fetch_stats(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct lio_vf_rep_desc *vf_rep = wk->ctxptr;
	struct lio_vf_rep_stats stats;
	struct lio_vf_rep_req rep_cfg;
	struct octeon_device *oct;
	int ret;

	oct = vf_rep->oct;

	memset(&rep_cfg, 0, sizeof(rep_cfg));
	rep_cfg.req_type = LIO_VF_REP_REQ_STATS;
	rep_cfg.ifidx = vf_rep->ifidx;

	ret = lio_vf_rep_send_soft_command(oct, &rep_cfg, sizeof(rep_cfg),
					   &stats, sizeof(stats));

	if (!ret) {
		octeon_swap_8B_data((u64 *)&stats, (sizeof(stats) >> 3));
		memcpy(&vf_rep->stats, &stats, sizeof(stats));
	}

	schedule_delayed_work(&vf_rep->stats_wk.work,
			      msecs_to_jiffies(LIO_VF_REP_STATS_POLL_TIME_MS));
}

int
lio_vf_rep_create(struct octeon_device *oct)
{
	struct lio_vf_rep_desc *vf_rep;
	struct net_device *ndev;
	int i, num_vfs;

	if (oct->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return 0;

	if (!oct->sriov_info.sriov_enabled)
		return 0;

	num_vfs = oct->sriov_info.num_vfs_alloced;

	oct->vf_rep_list.num_vfs = 0;
	for (i = 0; i < num_vfs; i++) {
		ndev = alloc_etherdev(sizeof(struct lio_vf_rep_desc));

		if (!ndev) {
			dev_err(&oct->pci_dev->dev,
				"VF rep device %d creation failed\n", i);
			goto cleanup;
		}

		ndev->min_mtu = LIO_MIN_MTU_SIZE;
		ndev->max_mtu = LIO_MAX_MTU_SIZE;
		ndev->netdev_ops = &lio_vf_rep_ndev_ops;
		SWITCHDEV_SET_OPS(ndev, &lio_vf_rep_switchdev_ops);

		vf_rep = netdev_priv(ndev);
		memset(vf_rep, 0, sizeof(*vf_rep));

		vf_rep->ndev = ndev;
		vf_rep->oct = oct;
		vf_rep->parent_ndev = oct->props[0].netdev;
		vf_rep->ifidx = (oct->pf_num * 64) + i + 1;

		eth_hw_addr_random(ndev);

		if (register_netdev(ndev)) {
			dev_err(&oct->pci_dev->dev, "VF rep nerdev registration failed\n");

			free_netdev(ndev);
			goto cleanup;
		}

		netif_carrier_off(ndev);

		INIT_DELAYED_WORK(&vf_rep->stats_wk.work,
				  lio_vf_rep_fetch_stats);
		vf_rep->stats_wk.ctxptr = (void *)vf_rep;
		schedule_delayed_work(&vf_rep->stats_wk.work,
				      msecs_to_jiffies
				      (LIO_VF_REP_STATS_POLL_TIME_MS));
		oct->vf_rep_list.num_vfs++;
		oct->vf_rep_list.ndev[i] = ndev;
	}

	if (octeon_register_dispatch_fn(oct, OPCODE_NIC,
					OPCODE_NIC_VF_REP_PKT,
					lio_vf_rep_pkt_recv, oct)) {
		dev_err(&oct->pci_dev->dev, "VF rep Dispatch func registration failed\n");

		goto cleanup;
	}

	return 0;

cleanup:
	for (i = 0; i < oct->vf_rep_list.num_vfs; i++) {
		ndev = oct->vf_rep_list.ndev[i];
		oct->vf_rep_list.ndev[i] = NULL;
		if (ndev) {
			vf_rep = netdev_priv(ndev);
			cancel_delayed_work_sync
				(&vf_rep->stats_wk.work);
			unregister_netdev(ndev);
			free_netdev(ndev);
		}
	}

	oct->vf_rep_list.num_vfs = 0;

	return -1;
}

void
lio_vf_rep_destroy(struct octeon_device *oct)
{
	struct lio_vf_rep_desc *vf_rep;
	struct net_device *ndev;
	int i;

	if (oct->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return;

	if (!oct->sriov_info.sriov_enabled)
		return;

	for (i = 0; i < oct->vf_rep_list.num_vfs; i++) {
		ndev = oct->vf_rep_list.ndev[i];
		oct->vf_rep_list.ndev[i] = NULL;
		if (ndev) {
			vf_rep = netdev_priv(ndev);
			cancel_delayed_work_sync
				(&vf_rep->stats_wk.work);
			netif_tx_disable(ndev);
			netif_carrier_off(ndev);

			unregister_netdev(ndev);
			free_netdev(ndev);
		}
	}

	oct->vf_rep_list.num_vfs = 0;
}

static int
lio_vf_rep_netdev_event(struct notifier_block *nb,
			unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct lio_vf_rep_desc *vf_rep;
	struct lio_vf_rep_req rep_cfg;
	struct octeon_device *oct;
	int ret;

	switch (event) {
	case NETDEV_REGISTER:
	case NETDEV_CHANGENAME:
		break;

	default:
		return NOTIFY_DONE;
	}

	if (ndev->netdev_ops != &lio_vf_rep_ndev_ops)
		return NOTIFY_DONE;

	vf_rep = netdev_priv(ndev);
	oct = vf_rep->oct;

	if (strlen(ndev->name) > LIO_IF_NAME_SIZE) {
		dev_err(&oct->pci_dev->dev,
			"Device name change sync failed as the size is > %d\n",
			LIO_IF_NAME_SIZE);
		return NOTIFY_DONE;
	}

	memset(&rep_cfg, 0, sizeof(rep_cfg));
	rep_cfg.req_type = LIO_VF_REP_REQ_DEVNAME;
	rep_cfg.ifidx = vf_rep->ifidx;
	strncpy(rep_cfg.rep_name.name, ndev->name, LIO_IF_NAME_SIZE);

	ret = lio_vf_rep_send_soft_command(oct, &rep_cfg,
					   sizeof(rep_cfg), NULL, 0);
	if (ret)
		dev_err(&oct->pci_dev->dev,
			"vf_rep netdev name change failed with err %d\n", ret);

	return NOTIFY_DONE;
}

static struct notifier_block lio_vf_rep_netdev_notifier = {
	.notifier_call = lio_vf_rep_netdev_event,
};

int
lio_vf_rep_modinit(void)
{
	if (register_netdevice_notifier(&lio_vf_rep_netdev_notifier)) {
		pr_err("netdev notifier registration failed\n");
		return -EFAULT;
	}

	return 0;
}

void
lio_vf_rep_modexit(void)
{
	if (unregister_netdevice_notifier(&lio_vf_rep_netdev_notifier))
		pr_err("netdev notifier unregister failed\n");
}
