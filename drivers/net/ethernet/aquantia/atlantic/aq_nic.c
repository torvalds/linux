// SPDX-License-Identifier: GPL-2.0-only
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File aq_nic.c: Definition of common code for NIC. */

#include "aq_nic.h"
#include "aq_ring.h"
#include "aq_vec.h"
#include "aq_hw.h"
#include "aq_pci_func.h"
#include "aq_main.h"

#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/timer.h>
#include <linux/cpu.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/ip.h>

static unsigned int aq_itr = AQ_CFG_INTERRUPT_MODERATION_AUTO;
module_param_named(aq_itr, aq_itr, uint, 0644);
MODULE_PARM_DESC(aq_itr, "Interrupt throttling mode");

static unsigned int aq_itr_tx;
module_param_named(aq_itr_tx, aq_itr_tx, uint, 0644);
MODULE_PARM_DESC(aq_itr_tx, "TX interrupt throttle rate");

static unsigned int aq_itr_rx;
module_param_named(aq_itr_rx, aq_itr_rx, uint, 0644);
MODULE_PARM_DESC(aq_itr_rx, "RX interrupt throttle rate");

static void aq_nic_update_ndev_stats(struct aq_nic_s *self);

static void aq_nic_rss_init(struct aq_nic_s *self, unsigned int num_rss_queues)
{
	struct aq_nic_cfg_s *cfg = &self->aq_nic_cfg;
	struct aq_rss_parameters *rss_params = &cfg->aq_rss;
	int i = 0;

	static u8 rss_key[AQ_CFG_RSS_HASHKEY_SIZE] = {
		0x1e, 0xad, 0x71, 0x87, 0x65, 0xfc, 0x26, 0x7d,
		0x0d, 0x45, 0x67, 0x74, 0xcd, 0x06, 0x1a, 0x18,
		0xb6, 0xc1, 0xf0, 0xc7, 0xbb, 0x18, 0xbe, 0xf8,
		0x19, 0x13, 0x4b, 0xa9, 0xd0, 0x3e, 0xfe, 0x70,
		0x25, 0x03, 0xab, 0x50, 0x6a, 0x8b, 0x82, 0x0c
	};

	rss_params->hash_secret_key_size = sizeof(rss_key);
	memcpy(rss_params->hash_secret_key, rss_key, sizeof(rss_key));
	rss_params->indirection_table_size = AQ_CFG_RSS_INDIRECTION_TABLE_MAX;

	for (i = rss_params->indirection_table_size; i--;)
		rss_params->indirection_table[i] = i & (num_rss_queues - 1);
}

/* Checks hw_caps and 'corrects' aq_nic_cfg in runtime */
void aq_nic_cfg_start(struct aq_nic_s *self)
{
	struct aq_nic_cfg_s *cfg = &self->aq_nic_cfg;

	cfg->tcs = AQ_CFG_TCS_DEF;

	cfg->is_polling = AQ_CFG_IS_POLLING_DEF;

	cfg->itr = aq_itr;
	cfg->tx_itr = aq_itr_tx;
	cfg->rx_itr = aq_itr_rx;

	cfg->rxpageorder = AQ_CFG_RX_PAGEORDER;
	cfg->is_rss = AQ_CFG_IS_RSS_DEF;
	cfg->num_rss_queues = AQ_CFG_NUM_RSS_QUEUES_DEF;
	cfg->aq_rss.base_cpu_number = AQ_CFG_RSS_BASE_CPU_NUM_DEF;
	cfg->flow_control = AQ_CFG_FC_MODE;

	cfg->mtu = AQ_CFG_MTU_DEF;
	cfg->link_speed_msk = AQ_CFG_SPEED_MSK;
	cfg->is_autoneg = AQ_CFG_IS_AUTONEG_DEF;

	cfg->is_lro = AQ_CFG_IS_LRO_DEF;

	/*descriptors */
	cfg->rxds = min(cfg->aq_hw_caps->rxds_max, AQ_CFG_RXDS_DEF);
	cfg->txds = min(cfg->aq_hw_caps->txds_max, AQ_CFG_TXDS_DEF);

	/*rss rings */
	cfg->vecs = min(cfg->aq_hw_caps->vecs, AQ_CFG_VECS_DEF);
	cfg->vecs = min(cfg->vecs, num_online_cpus());
	if (self->irqvecs > AQ_HW_SERVICE_IRQS)
		cfg->vecs = min(cfg->vecs, self->irqvecs - AQ_HW_SERVICE_IRQS);
	/* cfg->vecs should be power of 2 for RSS */
	if (cfg->vecs >= 8U)
		cfg->vecs = 8U;
	else if (cfg->vecs >= 4U)
		cfg->vecs = 4U;
	else if (cfg->vecs >= 2U)
		cfg->vecs = 2U;
	else
		cfg->vecs = 1U;

	cfg->num_rss_queues = min(cfg->vecs, AQ_CFG_NUM_RSS_QUEUES_DEF);

	aq_nic_rss_init(self, cfg->num_rss_queues);

	cfg->irq_type = aq_pci_func_get_irq_type(self);

	if ((cfg->irq_type == AQ_HW_IRQ_LEGACY) ||
	    (cfg->aq_hw_caps->vecs == 1U) ||
	    (cfg->vecs == 1U)) {
		cfg->is_rss = 0U;
		cfg->vecs = 1U;
	}

	/* Check if we have enough vectors allocated for
	 * link status IRQ. If no - we'll know link state from
	 * slower service task.
	 */
	if (AQ_HW_SERVICE_IRQS > 0 && cfg->vecs + 1 <= self->irqvecs)
		cfg->link_irq_vec = cfg->vecs;
	else
		cfg->link_irq_vec = 0;

	cfg->link_speed_msk &= cfg->aq_hw_caps->link_speed_msk;
	cfg->features = cfg->aq_hw_caps->hw_features;
	cfg->is_vlan_rx_strip = !!(cfg->features & NETIF_F_HW_VLAN_CTAG_RX);
	cfg->is_vlan_tx_insert = !!(cfg->features & NETIF_F_HW_VLAN_CTAG_TX);
	cfg->is_vlan_force_promisc = true;
}

static int aq_nic_update_link_status(struct aq_nic_s *self)
{
	int err = self->aq_fw_ops->update_link_status(self->aq_hw);
	u32 fc = 0;

	if (err)
		return err;

	if (self->link_status.mbps != self->aq_hw->aq_link_status.mbps) {
		pr_info("%s: link change old %d new %d\n",
			AQ_CFG_DRV_NAME, self->link_status.mbps,
			self->aq_hw->aq_link_status.mbps);
		aq_nic_update_interrupt_moderation_settings(self);

		/* Driver has to update flow control settings on RX block
		 * on any link event.
		 * We should query FW whether it negotiated FC.
		 */
		if (self->aq_fw_ops->get_flow_control)
			self->aq_fw_ops->get_flow_control(self->aq_hw, &fc);
		if (self->aq_hw_ops->hw_set_fc)
			self->aq_hw_ops->hw_set_fc(self->aq_hw, fc, 0);
	}

	self->link_status = self->aq_hw->aq_link_status;
	if (!netif_carrier_ok(self->ndev) && self->link_status.mbps) {
		aq_utils_obj_set(&self->flags,
				 AQ_NIC_FLAG_STARTED);
		aq_utils_obj_clear(&self->flags,
				   AQ_NIC_LINK_DOWN);
		netif_carrier_on(self->ndev);
		netif_tx_wake_all_queues(self->ndev);
	}
	if (netif_carrier_ok(self->ndev) && !self->link_status.mbps) {
		netif_carrier_off(self->ndev);
		netif_tx_disable(self->ndev);
		aq_utils_obj_set(&self->flags, AQ_NIC_LINK_DOWN);
	}
	return 0;
}

static irqreturn_t aq_linkstate_threaded_isr(int irq, void *private)
{
	struct aq_nic_s *self = private;

	if (!self)
		return IRQ_NONE;

	aq_nic_update_link_status(self);

	self->aq_hw_ops->hw_irq_enable(self->aq_hw,
				       BIT(self->aq_nic_cfg.link_irq_vec));
	return IRQ_HANDLED;
}

static void aq_nic_service_task(struct work_struct *work)
{
	struct aq_nic_s *self = container_of(work, struct aq_nic_s,
					     service_task);
	int err;

	if (aq_utils_obj_test(&self->flags, AQ_NIC_FLAGS_IS_NOT_READY))
		return;

	err = aq_nic_update_link_status(self);
	if (err)
		return;

	mutex_lock(&self->fwreq_mutex);
	if (self->aq_fw_ops->update_stats)
		self->aq_fw_ops->update_stats(self->aq_hw);
	mutex_unlock(&self->fwreq_mutex);

	aq_nic_update_ndev_stats(self);
}

static void aq_nic_service_timer_cb(struct timer_list *t)
{
	struct aq_nic_s *self = from_timer(self, t, service_timer);

	mod_timer(&self->service_timer, jiffies + AQ_CFG_SERVICE_TIMER_INTERVAL);

	aq_ndev_schedule_work(&self->service_task);
}

static void aq_nic_polling_timer_cb(struct timer_list *t)
{
	struct aq_nic_s *self = from_timer(self, t, polling_timer);
	struct aq_vec_s *aq_vec = NULL;
	unsigned int i = 0U;

	for (i = 0U, aq_vec = self->aq_vec[0];
		self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i])
		aq_vec_isr(i, (void *)aq_vec);

	mod_timer(&self->polling_timer, jiffies +
		  AQ_CFG_POLLING_TIMER_INTERVAL);
}

int aq_nic_ndev_register(struct aq_nic_s *self)
{
	int err = 0;

	if (!self->ndev) {
		err = -EINVAL;
		goto err_exit;
	}

	err = hw_atl_utils_initfw(self->aq_hw, &self->aq_fw_ops);
	if (err)
		goto err_exit;

	mutex_lock(&self->fwreq_mutex);
	err = self->aq_fw_ops->get_mac_permanent(self->aq_hw,
			    self->ndev->dev_addr);
	mutex_unlock(&self->fwreq_mutex);
	if (err)
		goto err_exit;

#if defined(AQ_CFG_MAC_ADDR_PERMANENT)
	{
		static u8 mac_addr_permanent[] = AQ_CFG_MAC_ADDR_PERMANENT;

		ether_addr_copy(self->ndev->dev_addr, mac_addr_permanent);
	}
#endif

	for (self->aq_vecs = 0; self->aq_vecs < aq_nic_get_cfg(self)->vecs;
	     self->aq_vecs++) {
		self->aq_vec[self->aq_vecs] =
		    aq_vec_alloc(self, self->aq_vecs, aq_nic_get_cfg(self));
		if (!self->aq_vec[self->aq_vecs]) {
			err = -ENOMEM;
			goto err_exit;
		}
	}

	netif_carrier_off(self->ndev);

	netif_tx_disable(self->ndev);

	err = register_netdev(self->ndev);
	if (err)
		goto err_exit;

err_exit:
	return err;
}

void aq_nic_ndev_init(struct aq_nic_s *self)
{
	const struct aq_hw_caps_s *aq_hw_caps = self->aq_nic_cfg.aq_hw_caps;
	struct aq_nic_cfg_s *aq_nic_cfg = &self->aq_nic_cfg;

	self->ndev->hw_features |= aq_hw_caps->hw_features;
	self->ndev->features = aq_hw_caps->hw_features;
	self->ndev->vlan_features |= NETIF_F_HW_CSUM | NETIF_F_RXCSUM |
				     NETIF_F_RXHASH | NETIF_F_SG |
				     NETIF_F_LRO | NETIF_F_TSO;
	self->ndev->priv_flags = aq_hw_caps->hw_priv_flags;
	self->ndev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	self->ndev->mtu = aq_nic_cfg->mtu - ETH_HLEN;
	self->ndev->max_mtu = aq_hw_caps->mtu - ETH_FCS_LEN - ETH_HLEN;

}

void aq_nic_set_tx_ring(struct aq_nic_s *self, unsigned int idx,
			struct aq_ring_s *ring)
{
	self->aq_ring_tx[idx] = ring;
}

struct net_device *aq_nic_get_ndev(struct aq_nic_s *self)
{
	return self->ndev;
}

int aq_nic_init(struct aq_nic_s *self)
{
	struct aq_vec_s *aq_vec = NULL;
	int err = 0;
	unsigned int i = 0U;

	self->power_state = AQ_HW_POWER_STATE_D0;
	mutex_lock(&self->fwreq_mutex);
	err = self->aq_hw_ops->hw_reset(self->aq_hw);
	mutex_unlock(&self->fwreq_mutex);
	if (err < 0)
		goto err_exit;

	err = self->aq_hw_ops->hw_init(self->aq_hw,
				       aq_nic_get_ndev(self)->dev_addr);
	if (err < 0)
		goto err_exit;

	for (i = 0U, aq_vec = self->aq_vec[0];
		self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i])
		aq_vec_init(aq_vec, self->aq_hw_ops, self->aq_hw);

	netif_carrier_off(self->ndev);

err_exit:
	return err;
}

int aq_nic_start(struct aq_nic_s *self)
{
	struct aq_vec_s *aq_vec = NULL;
	int err = 0;
	unsigned int i = 0U;

	err = self->aq_hw_ops->hw_multicast_list_set(self->aq_hw,
						     self->mc_list.ar,
						     self->mc_list.count);
	if (err < 0)
		goto err_exit;

	err = self->aq_hw_ops->hw_packet_filter_set(self->aq_hw,
						    self->packet_filter);
	if (err < 0)
		goto err_exit;

	for (i = 0U, aq_vec = self->aq_vec[0];
		self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i]) {
		err = aq_vec_start(aq_vec);
		if (err < 0)
			goto err_exit;
	}

	err = self->aq_hw_ops->hw_start(self->aq_hw);
	if (err < 0)
		goto err_exit;

	err = aq_nic_update_interrupt_moderation_settings(self);
	if (err)
		goto err_exit;

	INIT_WORK(&self->service_task, aq_nic_service_task);

	timer_setup(&self->service_timer, aq_nic_service_timer_cb, 0);
	aq_nic_service_timer_cb(&self->service_timer);

	if (self->aq_nic_cfg.is_polling) {
		timer_setup(&self->polling_timer, aq_nic_polling_timer_cb, 0);
		mod_timer(&self->polling_timer, jiffies +
			  AQ_CFG_POLLING_TIMER_INTERVAL);
	} else {
		for (i = 0U, aq_vec = self->aq_vec[0];
			self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i]) {
			err = aq_pci_func_alloc_irq(self, i, self->ndev->name,
						    aq_vec_isr, aq_vec,
						    aq_vec_get_affinity_mask(aq_vec));
			if (err < 0)
				goto err_exit;
		}

		if (self->aq_nic_cfg.link_irq_vec) {
			int irqvec = pci_irq_vector(self->pdev,
						   self->aq_nic_cfg.link_irq_vec);
			err = request_threaded_irq(irqvec, NULL,
						   aq_linkstate_threaded_isr,
						   IRQF_SHARED | IRQF_ONESHOT,
						   self->ndev->name, self);
			if (err < 0)
				goto err_exit;
			self->msix_entry_mask |= (1 << self->aq_nic_cfg.link_irq_vec);
		}

		err = self->aq_hw_ops->hw_irq_enable(self->aq_hw,
						     AQ_CFG_IRQ_MASK);
		if (err < 0)
			goto err_exit;
	}

	err = netif_set_real_num_tx_queues(self->ndev, self->aq_vecs);
	if (err < 0)
		goto err_exit;

	err = netif_set_real_num_rx_queues(self->ndev, self->aq_vecs);
	if (err < 0)
		goto err_exit;

	netif_tx_start_all_queues(self->ndev);

err_exit:
	return err;
}

static unsigned int aq_nic_map_skb(struct aq_nic_s *self,
				   struct sk_buff *skb,
				   struct aq_ring_s *ring)
{
	unsigned int ret = 0U;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int frag_count = 0U;
	unsigned int dx = ring->sw_tail;
	struct aq_ring_buff_s *first = NULL;
	struct aq_ring_buff_s *dx_buff = &ring->buff_ring[dx];
	bool need_context_tag = false;

	dx_buff->flags = 0U;

	if (unlikely(skb_is_gso(skb))) {
		dx_buff->mss = skb_shinfo(skb)->gso_size;
		dx_buff->is_gso = 1U;
		dx_buff->len_pkt = skb->len;
		dx_buff->len_l2 = ETH_HLEN;
		dx_buff->len_l3 = ip_hdrlen(skb);
		dx_buff->len_l4 = tcp_hdrlen(skb);
		dx_buff->eop_index = 0xffffU;
		dx_buff->is_ipv6 =
			(ip_hdr(skb)->version == 6) ? 1U : 0U;
		need_context_tag = true;
	}

	if (self->aq_nic_cfg.is_vlan_tx_insert && skb_vlan_tag_present(skb)) {
		dx_buff->vlan_tx_tag = skb_vlan_tag_get(skb);
		dx_buff->len_pkt = skb->len;
		dx_buff->is_vlan = 1U;
		need_context_tag = true;
	}

	if (need_context_tag) {
		dx = aq_ring_next_dx(ring, dx);
		dx_buff = &ring->buff_ring[dx];
		dx_buff->flags = 0U;
		++ret;
	}

	dx_buff->len = skb_headlen(skb);
	dx_buff->pa = dma_map_single(aq_nic_get_dev(self),
				     skb->data,
				     dx_buff->len,
				     DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(aq_nic_get_dev(self), dx_buff->pa)))
		goto exit;

	first = dx_buff;
	dx_buff->len_pkt = skb->len;
	dx_buff->is_sop = 1U;
	dx_buff->is_mapped = 1U;
	++ret;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		dx_buff->is_ip_cso = (htons(ETH_P_IP) == skb->protocol) ?
			1U : 0U;

		if (ip_hdr(skb)->version == 4) {
			dx_buff->is_tcp_cso =
				(ip_hdr(skb)->protocol == IPPROTO_TCP) ?
					1U : 0U;
			dx_buff->is_udp_cso =
				(ip_hdr(skb)->protocol == IPPROTO_UDP) ?
					1U : 0U;
		} else if (ip_hdr(skb)->version == 6) {
			dx_buff->is_tcp_cso =
				(ipv6_hdr(skb)->nexthdr == NEXTHDR_TCP) ?
					1U : 0U;
			dx_buff->is_udp_cso =
				(ipv6_hdr(skb)->nexthdr == NEXTHDR_UDP) ?
					1U : 0U;
		}
	}

	for (; nr_frags--; ++frag_count) {
		unsigned int frag_len = 0U;
		unsigned int buff_offset = 0U;
		unsigned int buff_size = 0U;
		dma_addr_t frag_pa;
		skb_frag_t *frag = &skb_shinfo(skb)->frags[frag_count];

		frag_len = skb_frag_size(frag);

		while (frag_len) {
			if (frag_len > AQ_CFG_TX_FRAME_MAX)
				buff_size = AQ_CFG_TX_FRAME_MAX;
			else
				buff_size = frag_len;

			frag_pa = skb_frag_dma_map(aq_nic_get_dev(self),
						   frag,
						   buff_offset,
						   buff_size,
						   DMA_TO_DEVICE);

			if (unlikely(dma_mapping_error(aq_nic_get_dev(self),
						       frag_pa)))
				goto mapping_error;

			dx = aq_ring_next_dx(ring, dx);
			dx_buff = &ring->buff_ring[dx];

			dx_buff->flags = 0U;
			dx_buff->len = buff_size;
			dx_buff->pa = frag_pa;
			dx_buff->is_mapped = 1U;
			dx_buff->eop_index = 0xffffU;

			frag_len -= buff_size;
			buff_offset += buff_size;

			++ret;
		}
	}

	first->eop_index = dx;
	dx_buff->is_eop = 1U;
	dx_buff->skb = skb;
	goto exit;

mapping_error:
	for (dx = ring->sw_tail;
	     ret > 0;
	     --ret, dx = aq_ring_next_dx(ring, dx)) {
		dx_buff = &ring->buff_ring[dx];

		if (!dx_buff->is_gso && !dx_buff->is_vlan && dx_buff->pa) {
			if (unlikely(dx_buff->is_sop)) {
				dma_unmap_single(aq_nic_get_dev(self),
						 dx_buff->pa,
						 dx_buff->len,
						 DMA_TO_DEVICE);
			} else {
				dma_unmap_page(aq_nic_get_dev(self),
					       dx_buff->pa,
					       dx_buff->len,
					       DMA_TO_DEVICE);
			}
		}
	}

exit:
	return ret;
}

int aq_nic_xmit(struct aq_nic_s *self, struct sk_buff *skb)
{
	struct aq_ring_s *ring = NULL;
	unsigned int frags = 0U;
	unsigned int vec = skb->queue_mapping % self->aq_nic_cfg.vecs;
	unsigned int tc = 0U;
	int err = NETDEV_TX_OK;

	frags = skb_shinfo(skb)->nr_frags + 1;

	ring = self->aq_ring_tx[AQ_NIC_TCVEC2RING(self, tc, vec)];

	if (frags > AQ_CFG_SKB_FRAGS_MAX) {
		dev_kfree_skb_any(skb);
		goto err_exit;
	}

	aq_ring_update_queue_state(ring);

	/* Above status update may stop the queue. Check this. */
	if (__netif_subqueue_stopped(self->ndev, ring->idx)) {
		err = NETDEV_TX_BUSY;
		goto err_exit;
	}

	frags = aq_nic_map_skb(self, skb, ring);

	if (likely(frags)) {
		err = self->aq_hw_ops->hw_ring_tx_xmit(self->aq_hw,
						       ring, frags);
		if (err >= 0) {
			++ring->stats.tx.packets;
			ring->stats.tx.bytes += skb->len;
		}
	} else {
		err = NETDEV_TX_BUSY;
	}

err_exit:
	return err;
}

int aq_nic_update_interrupt_moderation_settings(struct aq_nic_s *self)
{
	return self->aq_hw_ops->hw_interrupt_moderation_set(self->aq_hw);
}

int aq_nic_set_packet_filter(struct aq_nic_s *self, unsigned int flags)
{
	int err = 0;

	err = self->aq_hw_ops->hw_packet_filter_set(self->aq_hw, flags);
	if (err < 0)
		goto err_exit;

	self->packet_filter = flags;

err_exit:
	return err;
}

int aq_nic_set_multicast_list(struct aq_nic_s *self, struct net_device *ndev)
{
	const struct aq_hw_ops *hw_ops = self->aq_hw_ops;
	struct aq_nic_cfg_s *cfg = &self->aq_nic_cfg;
	unsigned int packet_filter = ndev->flags;
	struct netdev_hw_addr *ha = NULL;
	unsigned int i = 0U;
	int err = 0;

	self->mc_list.count = 0;
	if (netdev_uc_count(ndev) > AQ_HW_MULTICAST_ADDRESS_MAX) {
		packet_filter |= IFF_PROMISC;
	} else {
		netdev_for_each_uc_addr(ha, ndev) {
			ether_addr_copy(self->mc_list.ar[i++], ha->addr);
		}
	}

	cfg->is_mc_list_enabled = !!(packet_filter & IFF_MULTICAST);
	if (cfg->is_mc_list_enabled) {
		if (i + netdev_mc_count(ndev) > AQ_HW_MULTICAST_ADDRESS_MAX) {
			packet_filter |= IFF_ALLMULTI;
		} else {
			netdev_for_each_mc_addr(ha, ndev) {
				ether_addr_copy(self->mc_list.ar[i++],
						ha->addr);
			}
		}
	}

	if (i > 0 && i <= AQ_HW_MULTICAST_ADDRESS_MAX) {
		self->mc_list.count = i;
		err = hw_ops->hw_multicast_list_set(self->aq_hw,
						    self->mc_list.ar,
						    self->mc_list.count);
		if (err < 0)
			return err;
	}
	return aq_nic_set_packet_filter(self, packet_filter);
}

int aq_nic_set_mtu(struct aq_nic_s *self, int new_mtu)
{
	self->aq_nic_cfg.mtu = new_mtu;

	return 0;
}

int aq_nic_set_mac(struct aq_nic_s *self, struct net_device *ndev)
{
	return self->aq_hw_ops->hw_set_mac_address(self->aq_hw, ndev->dev_addr);
}

unsigned int aq_nic_get_link_speed(struct aq_nic_s *self)
{
	return self->link_status.mbps;
}

int aq_nic_get_regs(struct aq_nic_s *self, struct ethtool_regs *regs, void *p)
{
	u32 *regs_buff = p;
	int err = 0;

	regs->version = 1;

	err = self->aq_hw_ops->hw_get_regs(self->aq_hw,
					   self->aq_nic_cfg.aq_hw_caps,
					   regs_buff);
	if (err < 0)
		goto err_exit;

err_exit:
	return err;
}

int aq_nic_get_regs_count(struct aq_nic_s *self)
{
	return self->aq_nic_cfg.aq_hw_caps->mac_regs_count;
}

void aq_nic_get_stats(struct aq_nic_s *self, u64 *data)
{
	unsigned int i = 0U;
	unsigned int count = 0U;
	struct aq_vec_s *aq_vec = NULL;
	struct aq_stats_s *stats;

	if (self->aq_fw_ops->update_stats) {
		mutex_lock(&self->fwreq_mutex);
		self->aq_fw_ops->update_stats(self->aq_hw);
		mutex_unlock(&self->fwreq_mutex);
	}
	stats = self->aq_hw_ops->hw_get_hw_stats(self->aq_hw);

	if (!stats)
		goto err_exit;

	data[i] = stats->uprc + stats->mprc + stats->bprc;
	data[++i] = stats->uprc;
	data[++i] = stats->mprc;
	data[++i] = stats->bprc;
	data[++i] = stats->erpt;
	data[++i] = stats->uptc + stats->mptc + stats->bptc;
	data[++i] = stats->uptc;
	data[++i] = stats->mptc;
	data[++i] = stats->bptc;
	data[++i] = stats->ubrc;
	data[++i] = stats->ubtc;
	data[++i] = stats->mbrc;
	data[++i] = stats->mbtc;
	data[++i] = stats->bbrc;
	data[++i] = stats->bbtc;
	data[++i] = stats->ubrc + stats->mbrc + stats->bbrc;
	data[++i] = stats->ubtc + stats->mbtc + stats->bbtc;
	data[++i] = stats->dma_pkt_rc;
	data[++i] = stats->dma_pkt_tc;
	data[++i] = stats->dma_oct_rc;
	data[++i] = stats->dma_oct_tc;
	data[++i] = stats->dpc;

	i++;

	data += i;

	for (i = 0U, aq_vec = self->aq_vec[0];
		aq_vec && self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i]) {
		data += count;
		aq_vec_get_sw_stats(aq_vec, data, &count);
	}

err_exit:;
}

static void aq_nic_update_ndev_stats(struct aq_nic_s *self)
{
	struct net_device *ndev = self->ndev;
	struct aq_stats_s *stats = self->aq_hw_ops->hw_get_hw_stats(self->aq_hw);

	ndev->stats.rx_packets = stats->dma_pkt_rc;
	ndev->stats.rx_bytes = stats->dma_oct_rc;
	ndev->stats.rx_errors = stats->erpr;
	ndev->stats.rx_dropped = stats->dpc;
	ndev->stats.tx_packets = stats->dma_pkt_tc;
	ndev->stats.tx_bytes = stats->dma_oct_tc;
	ndev->stats.tx_errors = stats->erpt;
	ndev->stats.multicast = stats->mprc;
}

void aq_nic_get_link_ksettings(struct aq_nic_s *self,
			       struct ethtool_link_ksettings *cmd)
{
	if (self->aq_nic_cfg.aq_hw_caps->media_type == AQ_HW_MEDIA_TYPE_FIBRE)
		cmd->base.port = PORT_FIBRE;
	else
		cmd->base.port = PORT_TP;
	/* This driver supports only 10G capable adapters, so DUPLEX_FULL */
	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.autoneg = self->aq_nic_cfg.is_autoneg;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);

	if (self->aq_nic_cfg.aq_hw_caps->link_speed_msk & AQ_NIC_RATE_10G)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     10000baseT_Full);

	if (self->aq_nic_cfg.aq_hw_caps->link_speed_msk & AQ_NIC_RATE_5G)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     5000baseT_Full);

	if (self->aq_nic_cfg.aq_hw_caps->link_speed_msk & AQ_NIC_RATE_2GS)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     2500baseT_Full);

	if (self->aq_nic_cfg.aq_hw_caps->link_speed_msk & AQ_NIC_RATE_1G)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     1000baseT_Full);

	if (self->aq_nic_cfg.aq_hw_caps->link_speed_msk & AQ_NIC_RATE_100M)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     100baseT_Full);

	if (self->aq_nic_cfg.aq_hw_caps->flow_control)
		ethtool_link_ksettings_add_link_mode(cmd, supported,
						     Pause);

	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);

	if (self->aq_nic_cfg.aq_hw_caps->media_type == AQ_HW_MEDIA_TYPE_FIBRE)
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	else
		ethtool_link_ksettings_add_link_mode(cmd, supported, TP);

	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	if (self->aq_nic_cfg.is_autoneg)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);

	if (self->aq_nic_cfg.link_speed_msk  & AQ_NIC_RATE_10G)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     10000baseT_Full);

	if (self->aq_nic_cfg.link_speed_msk  & AQ_NIC_RATE_5G)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     5000baseT_Full);

	if (self->aq_nic_cfg.link_speed_msk  & AQ_NIC_RATE_2GS)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     2500baseT_Full);

	if (self->aq_nic_cfg.link_speed_msk  & AQ_NIC_RATE_1G)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     1000baseT_Full);

	if (self->aq_nic_cfg.link_speed_msk  & AQ_NIC_RATE_100M)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     100baseT_Full);

	if (self->aq_nic_cfg.flow_control & AQ_NIC_FC_RX)
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Pause);

	/* Asym is when either RX or TX, but not both */
	if (!!(self->aq_nic_cfg.flow_control & AQ_NIC_FC_TX) ^
	    !!(self->aq_nic_cfg.flow_control & AQ_NIC_FC_RX))
		ethtool_link_ksettings_add_link_mode(cmd, advertising,
						     Asym_Pause);

	if (self->aq_nic_cfg.aq_hw_caps->media_type == AQ_HW_MEDIA_TYPE_FIBRE)
		ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);
	else
		ethtool_link_ksettings_add_link_mode(cmd, advertising, TP);
}

int aq_nic_set_link_ksettings(struct aq_nic_s *self,
			      const struct ethtool_link_ksettings *cmd)
{
	u32 speed = 0U;
	u32 rate = 0U;
	int err = 0;

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		rate = self->aq_nic_cfg.aq_hw_caps->link_speed_msk;
		self->aq_nic_cfg.is_autoneg = true;
	} else {
		speed = cmd->base.speed;

		switch (speed) {
		case SPEED_100:
			rate = AQ_NIC_RATE_100M;
			break;

		case SPEED_1000:
			rate = AQ_NIC_RATE_1G;
			break;

		case SPEED_2500:
			rate = AQ_NIC_RATE_2GS;
			break;

		case SPEED_5000:
			rate = AQ_NIC_RATE_5G;
			break;

		case SPEED_10000:
			rate = AQ_NIC_RATE_10G;
			break;

		default:
			err = -1;
			goto err_exit;
		break;
		}
		if (!(self->aq_nic_cfg.aq_hw_caps->link_speed_msk & rate)) {
			err = -1;
			goto err_exit;
		}

		self->aq_nic_cfg.is_autoneg = false;
	}

	mutex_lock(&self->fwreq_mutex);
	err = self->aq_fw_ops->set_link_speed(self->aq_hw, rate);
	mutex_unlock(&self->fwreq_mutex);
	if (err < 0)
		goto err_exit;

	self->aq_nic_cfg.link_speed_msk = rate;

err_exit:
	return err;
}

struct aq_nic_cfg_s *aq_nic_get_cfg(struct aq_nic_s *self)
{
	return &self->aq_nic_cfg;
}

u32 aq_nic_get_fw_version(struct aq_nic_s *self)
{
	u32 fw_version = 0U;

	self->aq_hw_ops->hw_get_fw_version(self->aq_hw, &fw_version);

	return fw_version;
}

int aq_nic_stop(struct aq_nic_s *self)
{
	struct aq_vec_s *aq_vec = NULL;
	unsigned int i = 0U;

	netif_tx_disable(self->ndev);
	netif_carrier_off(self->ndev);

	del_timer_sync(&self->service_timer);
	cancel_work_sync(&self->service_task);

	self->aq_hw_ops->hw_irq_disable(self->aq_hw, AQ_CFG_IRQ_MASK);

	if (self->aq_nic_cfg.is_polling)
		del_timer_sync(&self->polling_timer);
	else
		aq_pci_func_free_irqs(self);

	for (i = 0U, aq_vec = self->aq_vec[0];
		self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i])
		aq_vec_stop(aq_vec);

	return self->aq_hw_ops->hw_stop(self->aq_hw);
}

void aq_nic_deinit(struct aq_nic_s *self)
{
	struct aq_vec_s *aq_vec = NULL;
	unsigned int i = 0U;

	if (!self)
		goto err_exit;

	for (i = 0U, aq_vec = self->aq_vec[0];
		self->aq_vecs > i; ++i, aq_vec = self->aq_vec[i])
		aq_vec_deinit(aq_vec);

	if (likely(self->aq_fw_ops->deinit)) {
		mutex_lock(&self->fwreq_mutex);
		self->aq_fw_ops->deinit(self->aq_hw);
		mutex_unlock(&self->fwreq_mutex);
	}

	if (self->power_state != AQ_HW_POWER_STATE_D0 ||
	    self->aq_hw->aq_nic_cfg->wol)
		if (likely(self->aq_fw_ops->set_power)) {
			mutex_lock(&self->fwreq_mutex);
			self->aq_fw_ops->set_power(self->aq_hw,
						   self->power_state,
						   self->ndev->dev_addr);
			mutex_unlock(&self->fwreq_mutex);
		}


err_exit:;
}

void aq_nic_free_vectors(struct aq_nic_s *self)
{
	unsigned int i = 0U;

	if (!self)
		goto err_exit;

	for (i = ARRAY_SIZE(self->aq_vec); i--;) {
		if (self->aq_vec[i]) {
			aq_vec_free(self->aq_vec[i]);
			self->aq_vec[i] = NULL;
		}
	}

err_exit:;
}

int aq_nic_change_pm_state(struct aq_nic_s *self, pm_message_t *pm_msg)
{
	int err = 0;

	if (!netif_running(self->ndev)) {
		err = 0;
		goto out;
	}
	rtnl_lock();
	if (pm_msg->event & PM_EVENT_SLEEP || pm_msg->event & PM_EVENT_FREEZE) {
		self->power_state = AQ_HW_POWER_STATE_D3;
		netif_device_detach(self->ndev);
		netif_tx_stop_all_queues(self->ndev);

		err = aq_nic_stop(self);
		if (err < 0)
			goto err_exit;

		aq_nic_deinit(self);
	} else {
		err = aq_nic_init(self);
		if (err < 0)
			goto err_exit;

		err = aq_nic_start(self);
		if (err < 0)
			goto err_exit;

		netif_device_attach(self->ndev);
		netif_tx_start_all_queues(self->ndev);
	}

err_exit:
	rtnl_unlock();
out:
	return err;
}

void aq_nic_shutdown(struct aq_nic_s *self)
{
	int err = 0;

	if (!self->ndev)
		return;

	rtnl_lock();

	netif_device_detach(self->ndev);

	if (netif_running(self->ndev)) {
		err = aq_nic_stop(self);
		if (err < 0)
			goto err_exit;
	}
	aq_nic_deinit(self);

err_exit:
	rtnl_unlock();
}
