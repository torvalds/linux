/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include "hnae.h"
#include "hns_dsaf_mac.h"
#include "hns_dsaf_main.h"
#include "hns_dsaf_ppe.h"
#include "hns_dsaf_rcb.h"

#define AE_NAME_PORT_ID_IDX 6

static struct hns_mac_cb *hns_get_mac_cb(struct hnae_handle *handle)
{
	struct  hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	return vf_cb->mac_cb;
}

static struct dsaf_device *hns_ae_get_dsaf_dev(struct hnae_ae_dev *dev)
{
	return container_of(dev, struct dsaf_device, ae_dev);
}

static struct hns_ppe_cb *hns_get_ppe_cb(struct hnae_handle *handle)
{
	int ppe_index;
	struct ppe_common_cb *ppe_comm;
	struct  hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	ppe_comm = vf_cb->dsaf_dev->ppe_common[0];
	ppe_index = vf_cb->port_index;

	return &ppe_comm->ppe_cb[ppe_index];
}

static int hns_ae_get_q_num_per_vf(
	struct dsaf_device *dsaf_dev, int port)
{
	return dsaf_dev->rcb_common[0]->max_q_per_vf;
}

static int hns_ae_get_vf_num_per_port(
	struct dsaf_device *dsaf_dev, int port)
{
	return dsaf_dev->rcb_common[0]->max_vfn;
}

static struct ring_pair_cb *hns_ae_get_base_ring_pair(
	struct dsaf_device *dsaf_dev, int port)
{
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[0];
	int q_num = rcb_comm->max_q_per_vf;
	int vf_num = rcb_comm->max_vfn;

	return &rcb_comm->ring_pair_cb[port * q_num * vf_num];
}

static struct ring_pair_cb *hns_ae_get_ring_pair(struct hnae_queue *q)
{
	return container_of(q, struct ring_pair_cb, q);
}

struct hnae_handle *hns_ae_get_handle(struct hnae_ae_dev *dev,
				      u32 port_id)
{
	int vfnum_per_port;
	int qnum_per_vf;
	int i;
	struct dsaf_device *dsaf_dev;
	struct hnae_handle *ae_handle;
	struct ring_pair_cb *ring_pair_cb;
	struct hnae_vf_cb *vf_cb;

	dsaf_dev = hns_ae_get_dsaf_dev(dev);

	ring_pair_cb = hns_ae_get_base_ring_pair(dsaf_dev, port_id);
	vfnum_per_port = hns_ae_get_vf_num_per_port(dsaf_dev, port_id);
	qnum_per_vf = hns_ae_get_q_num_per_vf(dsaf_dev, port_id);

	vf_cb = kzalloc(sizeof(*vf_cb) +
			qnum_per_vf * sizeof(struct hnae_queue *), GFP_KERNEL);
	if (unlikely(!vf_cb)) {
		dev_err(dsaf_dev->dev, "malloc vf_cb fail!\n");
		ae_handle = ERR_PTR(-ENOMEM);
		goto handle_err;
	}
	ae_handle = &vf_cb->ae_handle;
	/* ae_handle Init  */
	ae_handle->owner_dev = dsaf_dev->dev;
	ae_handle->dev = dev;
	ae_handle->q_num = qnum_per_vf;

	/* find ring pair, and set vf id*/
	for (ae_handle->vf_id = 0;
		ae_handle->vf_id < vfnum_per_port; ae_handle->vf_id++) {
		if (!ring_pair_cb->used_by_vf)
			break;
		ring_pair_cb += qnum_per_vf;
	}
	if (ae_handle->vf_id >= vfnum_per_port) {
		dev_err(dsaf_dev->dev, "malloc queue fail!\n");
		ae_handle = ERR_PTR(-EINVAL);
		goto vf_id_err;
	}

	ae_handle->qs = (struct hnae_queue **)(&ae_handle->qs + 1);
	for (i = 0; i < qnum_per_vf; i++) {
		ae_handle->qs[i] = &ring_pair_cb->q;
		ae_handle->qs[i]->rx_ring.q = ae_handle->qs[i];
		ae_handle->qs[i]->tx_ring.q = ae_handle->qs[i];

		ring_pair_cb->used_by_vf = 1;
		ring_pair_cb++;
	}

	vf_cb->dsaf_dev = dsaf_dev;
	vf_cb->port_index = port_id;
	vf_cb->mac_cb = dsaf_dev->mac_cb[port_id];

	ae_handle->phy_if = vf_cb->mac_cb->phy_if;
	ae_handle->phy_dev = vf_cb->mac_cb->phy_dev;
	ae_handle->if_support = vf_cb->mac_cb->if_support;
	ae_handle->port_type = vf_cb->mac_cb->mac_type;
	ae_handle->media_type = vf_cb->mac_cb->media_type;
	ae_handle->dport_id = port_id;

	return ae_handle;
vf_id_err:
	kfree(vf_cb);
handle_err:
	return ae_handle;
}

static void hns_ae_put_handle(struct hnae_handle *handle)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	int i;

	vf_cb->mac_cb	 = NULL;

	kfree(vf_cb);

	for (i = 0; i < handle->q_num; i++)
		hns_ae_get_ring_pair(handle->qs[i])->used_by_vf = 0;
}

static void hns_ae_ring_enable_all(struct hnae_handle *handle, int val)
{
	int q_num = handle->q_num;
	int i;

	for (i = 0; i < q_num; i++)
		hns_rcb_ring_enable_hw(handle->qs[i], val);
}

static void hns_ae_init_queue(struct hnae_queue *q)
{
	struct ring_pair_cb *ring =
		container_of(q, struct ring_pair_cb, q);

	hns_rcb_init_hw(ring);
}

static void hns_ae_fini_queue(struct hnae_queue *q)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(q->handle);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_rcb_reset_ring_hw(q);
}

static int hns_ae_set_mac_address(struct hnae_handle *handle, void *p)
{
	int ret;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	if (!p || !is_valid_ether_addr((const u8 *)p)) {
		dev_err(handle->owner_dev, "is not valid ether addr !\n");
		return -EADDRNOTAVAIL;
	}

	ret = hns_mac_change_vf_addr(mac_cb, handle->vf_id, p);
	if (ret != 0) {
		dev_err(handle->owner_dev,
			"set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

static int hns_ae_add_uc_address(struct hnae_handle *handle,
				 const unsigned char *addr)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return -ENOSPC;

	return hns_mac_add_uc_addr(mac_cb, handle->vf_id, addr);
}

static int hns_ae_rm_uc_address(struct hnae_handle *handle,
				const unsigned char *addr)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return -ENOSPC;

	return hns_mac_rm_uc_addr(mac_cb, handle->vf_id, addr);
}

static int hns_ae_set_multicast_one(struct hnae_handle *handle, void *addr)
{
	int ret;
	char *mac_addr = (char *)addr;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	u8 port_num;

	assert(mac_cb);

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return 0;

	ret = hns_mac_set_multi(mac_cb, mac_cb->mac_id, mac_addr, true);
	if (ret) {
		dev_err(handle->owner_dev,
			"mac add mul_mac:%pM port%d  fail, ret = %#x!\n",
			mac_addr, mac_cb->mac_id, ret);
		return ret;
	}

	ret = hns_mac_get_inner_port_num(mac_cb, handle->vf_id, &port_num);
	if (ret)
		return ret;

	ret = hns_mac_set_multi(mac_cb, port_num, mac_addr, true);
	if (ret)
		dev_err(handle->owner_dev,
			"mac add mul_mac:%pM port%d  fail, ret = %#x!\n",
			mac_addr, DSAF_BASE_INNER_PORT_NUM, ret);

	return ret;
}

static int hns_ae_clr_multicast(struct hnae_handle *handle)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return 0;

	return hns_mac_clr_multicast(mac_cb, handle->vf_id);
}

static int hns_ae_set_mtu(struct hnae_handle *handle, int new_mtu)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	return hns_mac_set_mtu(mac_cb, new_mtu);
}

static void hns_ae_set_tso_stats(struct hnae_handle *handle, int enable)
{
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	hns_ppe_set_tso_enable(ppe_cb, enable);
}

static int hns_ae_start(struct hnae_handle *handle)
{
	int ret;
	int k;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	ret = hns_mac_vm_config_bc_en(mac_cb, 0, true);
	if (ret)
		return ret;

	for (k = 0; k < handle->q_num; k++) {
		if (AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver))
			hns_rcb_int_clr_hw(handle->qs[k],
					   RCB_INT_FLAG_TX | RCB_INT_FLAG_RX);
		else
			hns_rcbv2_int_clr_hw(handle->qs[k],
					     RCB_INT_FLAG_TX | RCB_INT_FLAG_RX);
	}
	hns_ae_ring_enable_all(handle, 1);
	msleep(100);

	hns_mac_start(mac_cb);

	return 0;
}

void hns_ae_stop(struct hnae_handle *handle)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	/* just clean tx fbd, neednot rx fbd*/
	hns_rcb_wait_fbd_clean(handle->qs, handle->q_num, RCB_INT_FLAG_TX);

	msleep(20);

	hns_mac_stop(mac_cb);

	usleep_range(10000, 20000);

	hns_ae_ring_enable_all(handle, 0);

	(void)hns_mac_vm_config_bc_en(mac_cb, 0, false);
}

static void hns_ae_reset(struct hnae_handle *handle)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_DEBUG) {
		hns_mac_reset(vf_cb->mac_cb);
		hns_ppe_reset_common(vf_cb->dsaf_dev, 0);
	}
}

void hns_ae_toggle_ring_irq(struct hnae_ring *ring, u32 mask)
{
	u32 flag;

	if (is_tx_ring(ring))
		flag = RCB_INT_FLAG_TX;
	else
		flag = RCB_INT_FLAG_RX;

	hns_rcb_int_ctrl_hw(ring->q, flag, mask);
}

static void hns_aev2_toggle_ring_irq(struct hnae_ring *ring, u32 mask)
{
	u32 flag;

	if (is_tx_ring(ring))
		flag = RCB_INT_FLAG_TX;
	else
		flag = RCB_INT_FLAG_RX;

	hns_rcbv2_int_ctrl_hw(ring->q, flag, mask);
}

static int hns_ae_get_link_status(struct hnae_handle *handle)
{
	u32 link_status;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_mac_get_link_status(mac_cb, &link_status);

	return !!link_status;
}

static int hns_ae_get_mac_info(struct hnae_handle *handle,
			       u8 *auto_neg, u16 *speed, u8 *duplex)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	return hns_mac_get_port_info(mac_cb, auto_neg, speed, duplex);
}

static void hns_ae_adjust_link(struct hnae_handle *handle, int speed,
			       int duplex)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_mac_adjust_link(mac_cb, speed, duplex);
}

static void hns_ae_get_ring_bdnum_limit(struct hnae_queue *queue,
					u32 *uplimit)
{
	*uplimit = HNS_RCB_RING_MAX_PENDING_BD;
}

static void hns_ae_get_pauseparam(struct hnae_handle *handle,
				  u32 *auto_neg, u32 *rx_en, u32 *tx_en)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;

	hns_mac_get_autoneg(mac_cb, auto_neg);

	hns_mac_get_pauseparam(mac_cb, rx_en, tx_en);

	/* Service port's pause feature is provided by DSAF, not mac */
	if (handle->port_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_rx_mac_pause_en(dsaf_dev, mac_cb->mac_id, rx_en);
}

static int hns_ae_set_autoneg(struct hnae_handle *handle, u8 enable)
{
	assert(handle);

	return hns_mac_set_autoneg(hns_get_mac_cb(handle), enable);
}

static void hns_ae_set_promisc_mode(struct hnae_handle *handle, u32 en)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_set_promisc_mode(hns_ae_get_dsaf_dev(handle->dev), en);
	hns_mac_set_promisc(mac_cb, (u8)!!en);
}

static int hns_ae_get_autoneg(struct hnae_handle *handle)
{
	u32     auto_neg;

	assert(handle);

	hns_mac_get_autoneg(hns_get_mac_cb(handle), &auto_neg);

	return auto_neg;
}

static int hns_ae_set_pauseparam(struct hnae_handle *handle,
				 u32 autoneg, u32 rx_en, u32 tx_en)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	int ret;

	ret = hns_mac_set_autoneg(mac_cb, autoneg);
	if (ret)
		return ret;

	/* Service port's pause feature is provided by DSAF, not mac */
	if (handle->port_type == HNAE_PORT_SERVICE) {
		ret = hns_dsaf_set_rx_mac_pause_en(dsaf_dev,
						   mac_cb->mac_id, rx_en);
		if (ret)
			return ret;
		rx_en = 0;
	}
	return hns_mac_set_pauseparam(mac_cb, rx_en, tx_en);
}

static void hns_ae_get_coalesce_usecs(struct hnae_handle *handle,
				      u32 *tx_usecs, u32 *rx_usecs)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	*tx_usecs = hns_rcb_get_coalesce_usecs(ring_pair->rcb_common,
					       ring_pair->port_id_in_comm);
	*rx_usecs = hns_rcb_get_coalesce_usecs(ring_pair->rcb_common,
					       ring_pair->port_id_in_comm);
}

static void hns_ae_get_rx_max_coalesced_frames(struct hnae_handle *handle,
					       u32 *tx_frames, u32 *rx_frames)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	*tx_frames = hns_rcb_get_coalesced_frames(ring_pair->rcb_common,
						  ring_pair->port_id_in_comm);
	*rx_frames = hns_rcb_get_coalesced_frames(ring_pair->rcb_common,
						  ring_pair->port_id_in_comm);
}

static int hns_ae_set_coalesce_usecs(struct hnae_handle *handle,
				     u32 timeout)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	return hns_rcb_set_coalesce_usecs(
		ring_pair->rcb_common, ring_pair->port_id_in_comm, timeout);
}

static int  hns_ae_set_coalesce_frames(struct hnae_handle *handle,
				       u32 coalesce_frames)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	return hns_rcb_set_coalesced_frames(
		ring_pair->rcb_common,
		ring_pair->port_id_in_comm, coalesce_frames);
}

static void hns_ae_get_coalesce_range(struct hnae_handle *handle,
				      u32 *tx_frames_low, u32 *rx_frames_low,
				      u32 *tx_frames_high, u32 *rx_frames_high,
				      u32 *tx_usecs_low, u32 *rx_usecs_low,
				      u32 *tx_usecs_high, u32 *rx_usecs_high)
{
	struct dsaf_device *dsaf_dev;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	*tx_frames_low  = HNS_RCB_MIN_COALESCED_FRAMES;
	*rx_frames_low  = HNS_RCB_MIN_COALESCED_FRAMES;
	*tx_frames_high =
		(dsaf_dev->desc_num - 1 > HNS_RCB_MAX_COALESCED_FRAMES) ?
		HNS_RCB_MAX_COALESCED_FRAMES : dsaf_dev->desc_num - 1;
	*rx_frames_high =
		(dsaf_dev->desc_num - 1 > HNS_RCB_MAX_COALESCED_FRAMES) ?
		 HNS_RCB_MAX_COALESCED_FRAMES : dsaf_dev->desc_num - 1;
	*tx_usecs_low   = 0;
	*rx_usecs_low   = 0;
	*tx_usecs_high  = HNS_RCB_MAX_COALESCED_USECS;
	*rx_usecs_high  = HNS_RCB_MAX_COALESCED_USECS;
}

void hns_ae_update_stats(struct hnae_handle *handle,
			 struct net_device_stats *net_stats)
{
	int port;
	int idx;
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	struct hnae_queue *queue;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	u64 tx_bytes = 0, rx_bytes = 0, tx_packets = 0, rx_packets = 0;
	u64 rx_errors = 0, tx_errors = 0, tx_dropped = 0;
	u64 rx_missed_errors = 0;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	if (!dsaf_dev)
		return;
	port = vf_cb->port_index;
	ppe_cb = hns_get_ppe_cb(handle);
	mac_cb = hns_get_mac_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		queue = handle->qs[idx];
		hns_rcb_update_stats(queue);

		tx_bytes += queue->tx_ring.stats.tx_bytes;
		tx_packets += queue->tx_ring.stats.tx_pkts;
		rx_bytes += queue->rx_ring.stats.rx_bytes;
		rx_packets += queue->rx_ring.stats.rx_pkts;

		rx_errors += queue->rx_ring.stats.err_pkt_len
				+ queue->rx_ring.stats.l2_err
				+ queue->rx_ring.stats.l3l4_csum_err;
	}

	hns_ppe_update_stats(ppe_cb);
	rx_missed_errors = ppe_cb->hw_stats.rx_drop_no_buf;
	tx_errors += ppe_cb->hw_stats.tx_err_checksum
		+ ppe_cb->hw_stats.tx_err_fifo_empty;

	if (mac_cb->mac_type == HNAE_PORT_SERVICE) {
		hns_dsaf_update_stats(dsaf_dev, port);
		/* for port upline direction, i.e., rx. */
		rx_missed_errors += dsaf_dev->hw_stats[port].bp_drop;
		rx_missed_errors += dsaf_dev->hw_stats[port].pad_drop;
		rx_missed_errors += dsaf_dev->hw_stats[port].crc_false;

		/* for port downline direction, i.e., tx. */
		port = port + DSAF_PPE_INODE_BASE;
		hns_dsaf_update_stats(dsaf_dev, port);
		tx_dropped += dsaf_dev->hw_stats[port].bp_drop;
		tx_dropped += dsaf_dev->hw_stats[port].pad_drop;
		tx_dropped += dsaf_dev->hw_stats[port].crc_false;
		tx_dropped += dsaf_dev->hw_stats[port].rslt_drop;
		tx_dropped += dsaf_dev->hw_stats[port].vlan_drop;
		tx_dropped += dsaf_dev->hw_stats[port].stp_drop;
	}

	hns_mac_update_stats(mac_cb);
	rx_errors += mac_cb->hw_stats.rx_fifo_overrun_err;

	tx_errors += mac_cb->hw_stats.tx_bad_pkts
		+ mac_cb->hw_stats.tx_fragment_err
		+ mac_cb->hw_stats.tx_jabber_err
		+ mac_cb->hw_stats.tx_underrun_err
		+ mac_cb->hw_stats.tx_crc_err;

	net_stats->tx_bytes = tx_bytes;
	net_stats->tx_packets = tx_packets;
	net_stats->rx_bytes = rx_bytes;
	net_stats->rx_dropped = 0;
	net_stats->rx_packets = rx_packets;
	net_stats->rx_errors = rx_errors;
	net_stats->tx_errors = tx_errors;
	net_stats->tx_dropped = tx_dropped;
	net_stats->rx_missed_errors = rx_missed_errors;
	net_stats->rx_crc_errors = mac_cb->hw_stats.rx_fcs_err;
	net_stats->rx_frame_errors = mac_cb->hw_stats.rx_align_err;
	net_stats->rx_fifo_errors = mac_cb->hw_stats.rx_fifo_overrun_err;
	net_stats->rx_length_errors = mac_cb->hw_stats.rx_len_err;
	net_stats->multicast = mac_cb->hw_stats.rx_mc_pkts;
}

void hns_ae_get_stats(struct hnae_handle *handle, u64 *data)
{
	int idx;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	u64 *p = data;
	struct  hnae_vf_cb *vf_cb;

	if (!handle || !data) {
		pr_err("hns_ae_get_stats NULL handle or data pointer!\n");
		return;
	}

	vf_cb = hns_ae_get_vf_cb(handle);
	mac_cb = hns_get_mac_cb(handle);
	ppe_cb = hns_get_ppe_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		hns_rcb_get_stats(handle->qs[idx], p);
		p += hns_rcb_get_ring_sset_count((int)ETH_SS_STATS);
	}

	hns_ppe_get_stats(ppe_cb, p);
	p += hns_ppe_get_sset_count((int)ETH_SS_STATS);

	hns_mac_get_stats(mac_cb, p);
	p += hns_mac_get_sset_count(mac_cb, (int)ETH_SS_STATS);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_stats(vf_cb->dsaf_dev, p, vf_cb->port_index);
}

void hns_ae_get_strings(struct hnae_handle *handle,
			u32 stringset, u8 *data)
{
	int port;
	int idx;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	u8 *p = data;
	struct	hnae_vf_cb *vf_cb;

	assert(handle);

	vf_cb = hns_ae_get_vf_cb(handle);
	port = vf_cb->port_index;
	mac_cb = hns_get_mac_cb(handle);
	ppe_cb = hns_get_ppe_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		hns_rcb_get_strings(stringset, p, idx);
		p += ETH_GSTRING_LEN * hns_rcb_get_ring_sset_count(stringset);
	}

	hns_ppe_get_strings(ppe_cb, stringset, p);
	p += ETH_GSTRING_LEN * hns_ppe_get_sset_count(stringset);

	hns_mac_get_strings(mac_cb, stringset, p);
	p += ETH_GSTRING_LEN * hns_mac_get_sset_count(mac_cb, stringset);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_strings(stringset, p, port, dsaf_dev);
}

int hns_ae_get_sset_count(struct hnae_handle *handle, int stringset)
{
	u32 sset_count = 0;
	struct hns_mac_cb *mac_cb;
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	assert(handle);

	mac_cb = hns_get_mac_cb(handle);

	sset_count += hns_rcb_get_ring_sset_count(stringset) * handle->q_num;
	sset_count += hns_ppe_get_sset_count(stringset);
	sset_count += hns_mac_get_sset_count(mac_cb, stringset);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		sset_count += hns_dsaf_get_sset_count(dsaf_dev, stringset);

	return sset_count;
}

static int hns_ae_config_loopback(struct hnae_handle *handle,
				  enum hnae_loop loop, int en)
{
	int ret;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;

	switch (loop) {
	case MAC_INTERNALLOOP_PHY:
		ret = 0;
		break;
	case MAC_INTERNALLOOP_SERDES:
		ret = dsaf_dev->misc_op->cfg_serdes_loopback(vf_cb->mac_cb,
							     !!en);
		break;
	case MAC_INTERNALLOOP_MAC:
		ret = hns_mac_config_mac_loopback(vf_cb->mac_cb, loop, en);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

void hns_ae_update_led_status(struct hnae_handle *handle)
{
	struct hns_mac_cb *mac_cb;

	assert(handle);
	mac_cb = hns_get_mac_cb(handle);
	if (!mac_cb->cpld_ctrl)
		return;
	hns_set_led_opt(mac_cb);
}

int hns_ae_cpld_set_led_id(struct hnae_handle *handle,
			   enum hnae_led_state status)
{
	struct hns_mac_cb *mac_cb;

	assert(handle);

	mac_cb = hns_get_mac_cb(handle);

	return hns_cpld_led_set_id(mac_cb, status);
}

void hns_ae_get_regs(struct hnae_handle *handle, void *data)
{
	u32 *p = data;
	int i;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	hns_ppe_get_regs(ppe_cb, p);
	p += hns_ppe_get_regs_count();

	hns_rcb_get_common_regs(vf_cb->dsaf_dev->rcb_common[0], p);
	p += hns_rcb_get_common_regs_count();

	for (i = 0; i < handle->q_num; i++) {
		hns_rcb_get_ring_regs(handle->qs[i], p);
		p += hns_rcb_get_ring_regs_count();
	}

	hns_mac_get_regs(vf_cb->mac_cb, p);
	p += hns_mac_get_regs_count(vf_cb->mac_cb);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_regs(vf_cb->dsaf_dev, vf_cb->port_index, p);
}

int hns_ae_get_regs_len(struct hnae_handle *handle)
{
	u32 total_num;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	total_num = hns_ppe_get_regs_count();
	total_num += hns_rcb_get_common_regs_count();
	total_num += hns_rcb_get_ring_regs_count() * handle->q_num;
	total_num += hns_mac_get_regs_count(vf_cb->mac_cb);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		total_num += hns_dsaf_get_regs_count();

	return total_num;
}

static u32 hns_ae_get_rss_key_size(struct hnae_handle *handle)
{
	return HNS_PPEV2_RSS_KEY_SIZE;
}

static u32 hns_ae_get_rss_indir_size(struct hnae_handle *handle)
{
	return HNS_PPEV2_RSS_IND_TBL_SIZE;
}

static int hns_ae_get_rss(struct hnae_handle *handle, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	/* currently we support only one type of hash function i.e. Toep hash */
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	/* get the RSS Key required by the user */
	if (key)
		memcpy(key, ppe_cb->rss_key, HNS_PPEV2_RSS_KEY_SIZE);

	/* update the current hash->queue mappings from the shadow RSS table */
	memcpy(indir, ppe_cb->rss_indir_table,
	       HNS_PPEV2_RSS_IND_TBL_SIZE * sizeof(*indir));

	return 0;
}

static int hns_ae_set_rss(struct hnae_handle *handle, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	/* set the RSS Hash Key if specififed by the user */
	if (key)
		hns_ppe_set_rss_key(ppe_cb, (u32 *)key);

	/* update the shadow RSS table with user specified qids */
	memcpy(ppe_cb->rss_indir_table, indir,
	       HNS_PPEV2_RSS_IND_TBL_SIZE * sizeof(*indir));

	/* now update the hardware */
	hns_ppe_set_indir_table(ppe_cb, ppe_cb->rss_indir_table);

	return 0;
}

static struct hnae_ae_ops hns_dsaf_ops = {
	.get_handle = hns_ae_get_handle,
	.put_handle = hns_ae_put_handle,
	.init_queue = hns_ae_init_queue,
	.fini_queue = hns_ae_fini_queue,
	.start = hns_ae_start,
	.stop = hns_ae_stop,
	.reset = hns_ae_reset,
	.toggle_ring_irq = hns_ae_toggle_ring_irq,
	.get_status = hns_ae_get_link_status,
	.get_info = hns_ae_get_mac_info,
	.adjust_link = hns_ae_adjust_link,
	.set_loopback = hns_ae_config_loopback,
	.get_ring_bdnum_limit = hns_ae_get_ring_bdnum_limit,
	.get_pauseparam = hns_ae_get_pauseparam,
	.set_autoneg = hns_ae_set_autoneg,
	.get_autoneg = hns_ae_get_autoneg,
	.set_pauseparam = hns_ae_set_pauseparam,
	.get_coalesce_usecs = hns_ae_get_coalesce_usecs,
	.get_rx_max_coalesced_frames = hns_ae_get_rx_max_coalesced_frames,
	.set_coalesce_usecs = hns_ae_set_coalesce_usecs,
	.set_coalesce_frames = hns_ae_set_coalesce_frames,
	.get_coalesce_range = hns_ae_get_coalesce_range,
	.set_promisc_mode = hns_ae_set_promisc_mode,
	.set_mac_addr = hns_ae_set_mac_address,
	.add_uc_addr = hns_ae_add_uc_address,
	.rm_uc_addr = hns_ae_rm_uc_address,
	.set_mc_addr = hns_ae_set_multicast_one,
	.clr_mc_addr = hns_ae_clr_multicast,
	.set_mtu = hns_ae_set_mtu,
	.update_stats = hns_ae_update_stats,
	.set_tso_stats = hns_ae_set_tso_stats,
	.get_stats = hns_ae_get_stats,
	.get_strings = hns_ae_get_strings,
	.get_sset_count = hns_ae_get_sset_count,
	.update_led_status = hns_ae_update_led_status,
	.set_led_id = hns_ae_cpld_set_led_id,
	.get_regs = hns_ae_get_regs,
	.get_regs_len = hns_ae_get_regs_len,
	.get_rss_key_size = hns_ae_get_rss_key_size,
	.get_rss_indir_size = hns_ae_get_rss_indir_size,
	.get_rss = hns_ae_get_rss,
	.set_rss = hns_ae_set_rss
};

int hns_dsaf_ae_init(struct dsaf_device *dsaf_dev)
{
	struct hnae_ae_dev *ae_dev = &dsaf_dev->ae_dev;
	static atomic_t id = ATOMIC_INIT(-1);

	switch (dsaf_dev->dsaf_ver) {
	case AE_VERSION_1:
		hns_dsaf_ops.toggle_ring_irq = hns_ae_toggle_ring_irq;
		break;
	case AE_VERSION_2:
		hns_dsaf_ops.toggle_ring_irq = hns_aev2_toggle_ring_irq;
		break;
	default:
		break;
	}

	snprintf(ae_dev->name, AE_NAME_SIZE, "%s%d", DSAF_DEVICE_NAME,
		 (int)atomic_inc_return(&id));
	ae_dev->ops = &hns_dsaf_ops;
	ae_dev->dev = dsaf_dev->dev;

	return hnae_ae_register(ae_dev, THIS_MODULE);
}

void hns_dsaf_ae_uninit(struct dsaf_device *dsaf_dev)
{
	hnae_ae_unregister(&dsaf_dev->ae_dev);
}
