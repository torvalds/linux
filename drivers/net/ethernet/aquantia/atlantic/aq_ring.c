/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_ring.c: Definition of functions for Rx/Tx rings. */

#include "aq_ring.h"
#include "aq_nic.h"
#include "aq_hw.h"

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

static struct aq_ring_s *aq_ring_alloc(struct aq_ring_s *self,
				       struct aq_nic_s *aq_nic)
{
	int err = 0;

	self->buff_ring =
		kcalloc(self->size, sizeof(struct aq_ring_buff_s), GFP_KERNEL);

	if (!self->buff_ring) {
		err = -ENOMEM;
		goto err_exit;
	}
	self->dx_ring = dma_alloc_coherent(aq_nic_get_dev(aq_nic),
						self->size * self->dx_size,
						&self->dx_ring_pa, GFP_KERNEL);
	if (!self->dx_ring) {
		err = -ENOMEM;
		goto err_exit;
	}

err_exit:
	if (err < 0) {
		aq_ring_free(self);
		self = NULL;
	}
	return self;
}

struct aq_ring_s *aq_ring_tx_alloc(struct aq_ring_s *self,
				   struct aq_nic_s *aq_nic,
				   unsigned int idx,
				   struct aq_nic_cfg_s *aq_nic_cfg)
{
	int err = 0;

	self->aq_nic = aq_nic;
	self->idx = idx;
	self->size = aq_nic_cfg->txds;
	self->dx_size = aq_nic_cfg->aq_hw_caps->txd_size;

	self = aq_ring_alloc(self, aq_nic);
	if (!self) {
		err = -ENOMEM;
		goto err_exit;
	}

err_exit:
	if (err < 0) {
		aq_ring_free(self);
		self = NULL;
	}
	return self;
}

struct aq_ring_s *aq_ring_rx_alloc(struct aq_ring_s *self,
				   struct aq_nic_s *aq_nic,
				   unsigned int idx,
				   struct aq_nic_cfg_s *aq_nic_cfg)
{
	int err = 0;

	self->aq_nic = aq_nic;
	self->idx = idx;
	self->size = aq_nic_cfg->rxds;
	self->dx_size = aq_nic_cfg->aq_hw_caps->rxd_size;

	self = aq_ring_alloc(self, aq_nic);
	if (!self) {
		err = -ENOMEM;
		goto err_exit;
	}

err_exit:
	if (err < 0) {
		aq_ring_free(self);
		self = NULL;
	}
	return self;
}

int aq_ring_init(struct aq_ring_s *self)
{
	self->hw_head = 0;
	self->sw_head = 0;
	self->sw_tail = 0;
	return 0;
}

void aq_ring_tx_clean(struct aq_ring_s *self)
{
	struct device *dev = aq_nic_get_dev(self->aq_nic);

	for (; self->sw_head != self->hw_head;
		self->sw_head = aq_ring_next_dx(self, self->sw_head)) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];

		if (likely(buff->is_mapped)) {
			if (unlikely(buff->is_sop))
				dma_unmap_single(dev, buff->pa, buff->len,
						 DMA_TO_DEVICE);
			else
				dma_unmap_page(dev, buff->pa, buff->len,
					       DMA_TO_DEVICE);
		}

		if (unlikely(buff->is_eop))
			dev_kfree_skb_any(buff->skb);
	}
}

static inline unsigned int aq_ring_dx_in_range(unsigned int h, unsigned int i,
					       unsigned int t)
{
	return (h < t) ? ((h < i) && (i < t)) : ((h < i) || (i < t));
}

#define AQ_SKB_ALIGN SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
int aq_ring_rx_clean(struct aq_ring_s *self, int *work_done, int budget)
{
	struct net_device *ndev = aq_nic_get_ndev(self->aq_nic);
	int err = 0;
	bool is_rsc_completed = true;

	for (; (self->sw_head != self->hw_head) && budget;
		self->sw_head = aq_ring_next_dx(self, self->sw_head),
		--budget, ++(*work_done)) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];
		struct sk_buff *skb = NULL;
		unsigned int next_ = 0U;
		unsigned int i = 0U;
		struct aq_ring_buff_s *buff_ = NULL;

		if (buff->is_error) {
			__free_pages(buff->page, 0);
			continue;
		}

		if (buff->is_cleaned)
			continue;

		if (!buff->is_eop) {
			for (next_ = buff->next,
			     buff_ = &self->buff_ring[next_]; true;
			     next_ = buff_->next,
			     buff_ = &self->buff_ring[next_]) {
				is_rsc_completed =
					aq_ring_dx_in_range(self->sw_head,
							    next_,
							    self->hw_head);

				if (unlikely(!is_rsc_completed)) {
					is_rsc_completed = false;
					break;
				}

				if (buff_->is_eop)
					break;
			}

			if (!is_rsc_completed) {
				err = 0;
				goto err_exit;
			}
		}

		/* for single fragment packets use build_skb() */
		if (buff->is_eop) {
			skb = build_skb(page_address(buff->page),
					buff->len + AQ_SKB_ALIGN);
			if (unlikely(!skb)) {
				err = -ENOMEM;
				goto err_exit;
			}

			skb_put(skb, buff->len);
		} else {
			skb = netdev_alloc_skb(ndev, ETH_HLEN);
			if (unlikely(!skb)) {
				err = -ENOMEM;
				goto err_exit;
			}
			skb_put(skb, ETH_HLEN);
			memcpy(skb->data, page_address(buff->page), ETH_HLEN);

			skb_add_rx_frag(skb, 0, buff->page, ETH_HLEN,
					buff->len - ETH_HLEN,
					SKB_TRUESIZE(buff->len - ETH_HLEN));

			for (i = 1U, next_ = buff->next,
			     buff_ = &self->buff_ring[next_]; true;
			     next_ = buff_->next,
			     buff_ = &self->buff_ring[next_], ++i) {
				skb_add_rx_frag(skb, i, buff_->page, 0,
						buff_->len,
						SKB_TRUESIZE(buff->len -
						ETH_HLEN));
				buff_->is_cleaned = 1;

				if (buff_->is_eop)
					break;
			}
		}

		skb->protocol = eth_type_trans(skb, ndev);
		if (unlikely(buff->is_cso_err)) {
			++self->stats.rx.errors;
			__skb_mark_checksum_bad(skb);
		} else {
			if (buff->is_ip_cso) {
				__skb_incr_checksum_unnecessary(skb);
				if (buff->is_udp_cso || buff->is_tcp_cso)
					__skb_incr_checksum_unnecessary(skb);
			} else {
				skb->ip_summed = CHECKSUM_NONE;
			}
		}

		skb_set_hash(skb, buff->rss_hash,
			     buff->is_hash_l4 ? PKT_HASH_TYPE_L4 :
			     PKT_HASH_TYPE_NONE);

		skb_record_rx_queue(skb, self->idx);

		netif_receive_skb(skb);

		++self->stats.rx.packets;
		self->stats.rx.bytes += skb->len;
	}

err_exit:
	return err;
}

int aq_ring_rx_fill(struct aq_ring_s *self)
{
	unsigned int pages_order = fls(AQ_CFG_RX_FRAME_MAX / PAGE_SIZE +
		(AQ_CFG_RX_FRAME_MAX % PAGE_SIZE ? 1 : 0)) - 1;
	struct aq_ring_buff_s *buff = NULL;
	int err = 0;
	int i = 0;

	for (i = aq_ring_avail_dx(self); i--;
		self->sw_tail = aq_ring_next_dx(self, self->sw_tail)) {
		buff = &self->buff_ring[self->sw_tail];

		buff->flags = 0U;
		buff->len = AQ_CFG_RX_FRAME_MAX;

		buff->page = alloc_pages(GFP_ATOMIC | __GFP_COLD |
					 __GFP_COMP, pages_order);
		if (!buff->page) {
			err = -ENOMEM;
			goto err_exit;
		}

		buff->pa = dma_map_page(aq_nic_get_dev(self->aq_nic),
					buff->page, 0,
					AQ_CFG_RX_FRAME_MAX, DMA_FROM_DEVICE);

		if (dma_mapping_error(aq_nic_get_dev(self->aq_nic), buff->pa)) {
			err = -ENOMEM;
			goto err_exit;
		}

		buff = NULL;
	}

err_exit:
	if (err < 0) {
		if (buff && buff->page)
			__free_pages(buff->page, 0);
	}

	return err;
}

void aq_ring_rx_deinit(struct aq_ring_s *self)
{
	if (!self)
		goto err_exit;

	for (; self->sw_head != self->sw_tail;
		self->sw_head = aq_ring_next_dx(self, self->sw_head)) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];

		dma_unmap_page(aq_nic_get_dev(self->aq_nic), buff->pa,
			       AQ_CFG_RX_FRAME_MAX, DMA_FROM_DEVICE);

		__free_pages(buff->page, 0);
	}

err_exit:;
}

void aq_ring_free(struct aq_ring_s *self)
{
	if (!self)
		goto err_exit;

	kfree(self->buff_ring);

	if (self->dx_ring)
		dma_free_coherent(aq_nic_get_dev(self->aq_nic),
				  self->size * self->dx_size, self->dx_ring,
				  self->dx_ring_pa);

err_exit:;
}
