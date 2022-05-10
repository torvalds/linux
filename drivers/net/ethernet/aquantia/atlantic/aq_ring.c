// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File aq_ring.c: Definition of functions for Rx/Tx rings. */

#include "aq_ring.h"
#include "aq_nic.h"
#include "aq_hw.h"
#include "aq_hw_utils.h"
#include "aq_ptp.h"

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

static inline void aq_free_rxpage(struct aq_rxpage *rxpage, struct device *dev)
{
	unsigned int len = PAGE_SIZE << rxpage->order;

	dma_unmap_page(dev, rxpage->daddr, len, DMA_FROM_DEVICE);

	/* Drop the ref for being in the ring. */
	__free_pages(rxpage->page, rxpage->order);
	rxpage->page = NULL;
}

static int aq_get_rxpage(struct aq_rxpage *rxpage, unsigned int order,
			 struct device *dev)
{
	struct page *page;
	int ret = -ENOMEM;
	dma_addr_t daddr;

	page = dev_alloc_pages(order);
	if (unlikely(!page))
		goto err_exit;

	daddr = dma_map_page(dev, page, 0, PAGE_SIZE << order,
			     DMA_FROM_DEVICE);

	if (unlikely(dma_mapping_error(dev, daddr)))
		goto free_page;

	rxpage->page = page;
	rxpage->daddr = daddr;
	rxpage->order = order;
	rxpage->pg_off = 0;

	return 0;

free_page:
	__free_pages(page, order);

err_exit:
	return ret;
}

static int aq_get_rxpages(struct aq_ring_s *self, struct aq_ring_buff_s *rxbuf,
			  int order)
{
	int ret;

	if (rxbuf->rxdata.page) {
		/* One means ring is the only user and can reuse */
		if (page_ref_count(rxbuf->rxdata.page) > 1) {
			/* Try reuse buffer */
			rxbuf->rxdata.pg_off += AQ_CFG_RX_FRAME_MAX;
			if (rxbuf->rxdata.pg_off + AQ_CFG_RX_FRAME_MAX <=
				(PAGE_SIZE << order)) {
				u64_stats_update_begin(&self->stats.rx.syncp);
				self->stats.rx.pg_flips++;
				u64_stats_update_end(&self->stats.rx.syncp);
			} else {
				/* Buffer exhausted. We have other users and
				 * should release this page and realloc
				 */
				aq_free_rxpage(&rxbuf->rxdata,
					       aq_nic_get_dev(self->aq_nic));
				u64_stats_update_begin(&self->stats.rx.syncp);
				self->stats.rx.pg_losts++;
				u64_stats_update_end(&self->stats.rx.syncp);
			}
		} else {
			rxbuf->rxdata.pg_off = 0;
			u64_stats_update_begin(&self->stats.rx.syncp);
			self->stats.rx.pg_reuses++;
			u64_stats_update_end(&self->stats.rx.syncp);
		}
	}

	if (!rxbuf->rxdata.page) {
		ret = aq_get_rxpage(&rxbuf->rxdata, order,
				    aq_nic_get_dev(self->aq_nic));
		if (ret) {
			u64_stats_update_begin(&self->stats.rx.syncp);
			self->stats.rx.alloc_fails++;
			u64_stats_update_end(&self->stats.rx.syncp);
		}
		return ret;
	}

	return 0;
}

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
	self->page_order = fls(AQ_CFG_RX_FRAME_MAX / PAGE_SIZE +
			       (AQ_CFG_RX_FRAME_MAX % PAGE_SIZE ? 1 : 0)) - 1;

	if (aq_nic_cfg->rxpageorder > self->page_order)
		self->page_order = aq_nic_cfg->rxpageorder;

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

struct aq_ring_s *
aq_ring_hwts_rx_alloc(struct aq_ring_s *self, struct aq_nic_s *aq_nic,
		      unsigned int idx, unsigned int size, unsigned int dx_size)
{
	struct device *dev = aq_nic_get_dev(aq_nic);
	size_t sz = size * dx_size + AQ_CFG_RXDS_DEF;

	memset(self, 0, sizeof(*self));

	self->aq_nic = aq_nic;
	self->idx = idx;
	self->size = size;
	self->dx_size = dx_size;

	self->dx_ring = dma_alloc_coherent(dev, sz, &self->dx_ring_pa,
					   GFP_KERNEL);
	if (!self->dx_ring) {
		aq_ring_free(self);
		return NULL;
	}

	return self;
}

int aq_ring_init(struct aq_ring_s *self, const enum atl_ring_type ring_type)
{
	self->hw_head = 0;
	self->sw_head = 0;
	self->sw_tail = 0;
	self->ring_type = ring_type;

	if (self->ring_type == ATL_RING_RX)
		u64_stats_init(&self->stats.rx.syncp);
	else
		u64_stats_init(&self->stats.tx.syncp);

	return 0;
}

static inline bool aq_ring_dx_in_range(unsigned int h, unsigned int i,
				       unsigned int t)
{
	return (h < t) ? ((h < i) && (i < t)) : ((h < i) || (i < t));
}

void aq_ring_update_queue_state(struct aq_ring_s *ring)
{
	if (aq_ring_avail_dx(ring) <= AQ_CFG_SKB_FRAGS_MAX)
		aq_ring_queue_stop(ring);
	else if (aq_ring_avail_dx(ring) > AQ_CFG_RESTART_DESC_THRES)
		aq_ring_queue_wake(ring);
}

void aq_ring_queue_wake(struct aq_ring_s *ring)
{
	struct net_device *ndev = aq_nic_get_ndev(ring->aq_nic);

	if (__netif_subqueue_stopped(ndev,
				     AQ_NIC_RING2QMAP(ring->aq_nic,
						      ring->idx))) {
		netif_wake_subqueue(ndev,
				    AQ_NIC_RING2QMAP(ring->aq_nic, ring->idx));
		u64_stats_update_begin(&ring->stats.tx.syncp);
		ring->stats.tx.queue_restarts++;
		u64_stats_update_end(&ring->stats.tx.syncp);
	}
}

void aq_ring_queue_stop(struct aq_ring_s *ring)
{
	struct net_device *ndev = aq_nic_get_ndev(ring->aq_nic);

	if (!__netif_subqueue_stopped(ndev,
				      AQ_NIC_RING2QMAP(ring->aq_nic,
						       ring->idx)))
		netif_stop_subqueue(ndev,
				    AQ_NIC_RING2QMAP(ring->aq_nic, ring->idx));
}

bool aq_ring_tx_clean(struct aq_ring_s *self)
{
	struct device *dev = aq_nic_get_dev(self->aq_nic);
	unsigned int budget;

	for (budget = AQ_CFG_TX_CLEAN_BUDGET;
	     budget && self->sw_head != self->hw_head; budget--) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];

		if (likely(buff->is_mapped)) {
			if (unlikely(buff->is_sop)) {
				if (!buff->is_eop &&
				    buff->eop_index != 0xffffU &&
				    (!aq_ring_dx_in_range(self->sw_head,
						buff->eop_index,
						self->hw_head)))
					break;

				dma_unmap_single(dev, buff->pa, buff->len,
						 DMA_TO_DEVICE);
			} else {
				dma_unmap_page(dev, buff->pa, buff->len,
					       DMA_TO_DEVICE);
			}
		}

		if (unlikely(buff->is_eop)) {
			u64_stats_update_begin(&self->stats.tx.syncp);
			++self->stats.tx.packets;
			self->stats.tx.bytes += buff->skb->len;
			u64_stats_update_end(&self->stats.tx.syncp);

			dev_kfree_skb_any(buff->skb);
		}
		buff->pa = 0U;
		buff->eop_index = 0xffffU;
		self->sw_head = aq_ring_next_dx(self, self->sw_head);
	}

	return !!budget;
}

static void aq_rx_checksum(struct aq_ring_s *self,
			   struct aq_ring_buff_s *buff,
			   struct sk_buff *skb)
{
	if (!(self->aq_nic->ndev->features & NETIF_F_RXCSUM))
		return;

	if (unlikely(buff->is_cso_err)) {
		u64_stats_update_begin(&self->stats.rx.syncp);
		++self->stats.rx.errors;
		u64_stats_update_end(&self->stats.rx.syncp);
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}
	if (buff->is_ip_cso) {
		__skb_incr_checksum_unnecessary(skb);
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}

	if (buff->is_udp_cso || buff->is_tcp_cso)
		__skb_incr_checksum_unnecessary(skb);
}

#define AQ_SKB_ALIGN SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
int aq_ring_rx_clean(struct aq_ring_s *self,
		     struct napi_struct *napi,
		     int *work_done,
		     int budget)
{
	struct net_device *ndev = aq_nic_get_ndev(self->aq_nic);
	bool is_rsc_completed = true;
	int err = 0;

	for (; (self->sw_head != self->hw_head) && budget;
		self->sw_head = aq_ring_next_dx(self, self->sw_head),
		--budget, ++(*work_done)) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];
		bool is_ptp_ring = aq_ptp_ring(self->aq_nic, self);
		struct aq_ring_buff_s *buff_ = NULL;
		struct sk_buff *skb = NULL;
		unsigned int next_ = 0U;
		unsigned int i = 0U;
		u16 hdr_len;

		if (buff->is_cleaned)
			continue;

		if (!buff->is_eop) {
			buff_ = buff;
			do {
				if (buff_->next >= self->size) {
					err = -EIO;
					goto err_exit;
				}
				next_ = buff_->next,
				buff_ = &self->buff_ring[next_];
				is_rsc_completed =
					aq_ring_dx_in_range(self->sw_head,
							    next_,
							    self->hw_head);

				if (unlikely(!is_rsc_completed))
					break;

				buff->is_error |= buff_->is_error;
				buff->is_cso_err |= buff_->is_cso_err;

			} while (!buff_->is_eop);

			if (!is_rsc_completed) {
				err = 0;
				goto err_exit;
			}
			if (buff->is_error ||
			    (buff->is_lro && buff->is_cso_err)) {
				buff_ = buff;
				do {
					if (buff_->next >= self->size) {
						err = -EIO;
						goto err_exit;
					}
					next_ = buff_->next,
					buff_ = &self->buff_ring[next_];

					buff_->is_cleaned = true;
				} while (!buff_->is_eop);

				u64_stats_update_begin(&self->stats.rx.syncp);
				++self->stats.rx.errors;
				u64_stats_update_end(&self->stats.rx.syncp);
				continue;
			}
		}

		if (buff->is_error) {
			u64_stats_update_begin(&self->stats.rx.syncp);
			++self->stats.rx.errors;
			u64_stats_update_end(&self->stats.rx.syncp);
			continue;
		}

		dma_sync_single_range_for_cpu(aq_nic_get_dev(self->aq_nic),
					      buff->rxdata.daddr,
					      buff->rxdata.pg_off,
					      buff->len, DMA_FROM_DEVICE);

		skb = napi_alloc_skb(napi, AQ_CFG_RX_HDR_SIZE);
		if (unlikely(!skb)) {
			u64_stats_update_begin(&self->stats.rx.syncp);
			self->stats.rx.skb_alloc_fails++;
			u64_stats_update_end(&self->stats.rx.syncp);
			err = -ENOMEM;
			goto err_exit;
		}
		if (is_ptp_ring)
			buff->len -=
				aq_ptp_extract_ts(self->aq_nic, skb,
						  aq_buf_vaddr(&buff->rxdata),
						  buff->len);

		hdr_len = buff->len;
		if (hdr_len > AQ_CFG_RX_HDR_SIZE)
			hdr_len = eth_get_headlen(skb->dev,
						  aq_buf_vaddr(&buff->rxdata),
						  AQ_CFG_RX_HDR_SIZE);

		memcpy(__skb_put(skb, hdr_len), aq_buf_vaddr(&buff->rxdata),
		       ALIGN(hdr_len, sizeof(long)));

		if (buff->len - hdr_len > 0) {
			skb_add_rx_frag(skb, i++, buff->rxdata.page,
					buff->rxdata.pg_off + hdr_len,
					buff->len - hdr_len,
					AQ_CFG_RX_FRAME_MAX);
			page_ref_inc(buff->rxdata.page);
		}

		if (!buff->is_eop) {
			buff_ = buff;
			do {
				next_ = buff_->next;
				buff_ = &self->buff_ring[next_];

				dma_sync_single_range_for_cpu(aq_nic_get_dev(self->aq_nic),
							      buff_->rxdata.daddr,
							      buff_->rxdata.pg_off,
							      buff_->len,
							      DMA_FROM_DEVICE);
				skb_add_rx_frag(skb, i++,
						buff_->rxdata.page,
						buff_->rxdata.pg_off,
						buff_->len,
						AQ_CFG_RX_FRAME_MAX);
				page_ref_inc(buff_->rxdata.page);
				buff_->is_cleaned = 1;

				buff->is_ip_cso &= buff_->is_ip_cso;
				buff->is_udp_cso &= buff_->is_udp_cso;
				buff->is_tcp_cso &= buff_->is_tcp_cso;
				buff->is_cso_err |= buff_->is_cso_err;

			} while (!buff_->is_eop);
		}

		if (buff->is_vlan)
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       buff->vlan_rx_tag);

		skb->protocol = eth_type_trans(skb, ndev);

		aq_rx_checksum(self, buff, skb);

		skb_set_hash(skb, buff->rss_hash,
			     buff->is_hash_l4 ? PKT_HASH_TYPE_L4 :
			     PKT_HASH_TYPE_NONE);
		/* Send all PTP traffic to 0 queue */
		skb_record_rx_queue(skb,
				    is_ptp_ring ? 0
						: AQ_NIC_RING2QMAP(self->aq_nic,
								   self->idx));

		u64_stats_update_begin(&self->stats.rx.syncp);
		++self->stats.rx.packets;
		self->stats.rx.bytes += skb->len;
		u64_stats_update_end(&self->stats.rx.syncp);

		napi_gro_receive(napi, skb);
	}

err_exit:
	return err;
}

void aq_ring_hwts_rx_clean(struct aq_ring_s *self, struct aq_nic_s *aq_nic)
{
#if IS_REACHABLE(CONFIG_PTP_1588_CLOCK)
	while (self->sw_head != self->hw_head) {
		u64 ns;

		aq_nic->aq_hw_ops->extract_hwts(aq_nic->aq_hw,
						self->dx_ring +
						(self->sw_head * self->dx_size),
						self->dx_size, &ns);
		aq_ptp_tx_hwtstamp(aq_nic, ns);

		self->sw_head = aq_ring_next_dx(self, self->sw_head);
	}
#endif
}

int aq_ring_rx_fill(struct aq_ring_s *self)
{
	unsigned int page_order = self->page_order;
	struct aq_ring_buff_s *buff = NULL;
	int err = 0;
	int i = 0;

	if (aq_ring_avail_dx(self) < min_t(unsigned int, AQ_CFG_RX_REFILL_THRES,
					   self->size / 2))
		return err;

	for (i = aq_ring_avail_dx(self); i--;
		self->sw_tail = aq_ring_next_dx(self, self->sw_tail)) {
		buff = &self->buff_ring[self->sw_tail];

		buff->flags = 0U;
		buff->len = AQ_CFG_RX_FRAME_MAX;

		err = aq_get_rxpages(self, buff, page_order);
		if (err)
			goto err_exit;

		buff->pa = aq_buf_daddr(&buff->rxdata);
		buff = NULL;
	}

err_exit:
	return err;
}

void aq_ring_rx_deinit(struct aq_ring_s *self)
{
	if (!self)
		return;

	for (; self->sw_head != self->sw_tail;
		self->sw_head = aq_ring_next_dx(self, self->sw_head)) {
		struct aq_ring_buff_s *buff = &self->buff_ring[self->sw_head];

		aq_free_rxpage(&buff->rxdata, aq_nic_get_dev(self->aq_nic));
	}
}

void aq_ring_free(struct aq_ring_s *self)
{
	if (!self)
		return;

	kfree(self->buff_ring);

	if (self->dx_ring)
		dma_free_coherent(aq_nic_get_dev(self->aq_nic),
				  self->size * self->dx_size, self->dx_ring,
				  self->dx_ring_pa);
}

unsigned int aq_ring_fill_stats_data(struct aq_ring_s *self, u64 *data)
{
	unsigned int count;
	unsigned int start;

	if (self->ring_type == ATL_RING_RX) {
		/* This data should mimic aq_ethtool_queue_rx_stat_names structure */
		do {
			count = 0;
			start = u64_stats_fetch_begin_irq(&self->stats.rx.syncp);
			data[count] = self->stats.rx.packets;
			data[++count] = self->stats.rx.jumbo_packets;
			data[++count] = self->stats.rx.lro_packets;
			data[++count] = self->stats.rx.errors;
			data[++count] = self->stats.rx.alloc_fails;
			data[++count] = self->stats.rx.skb_alloc_fails;
			data[++count] = self->stats.rx.polls;
		} while (u64_stats_fetch_retry_irq(&self->stats.rx.syncp, start));
	} else {
		/* This data should mimic aq_ethtool_queue_tx_stat_names structure */
		do {
			count = 0;
			start = u64_stats_fetch_begin_irq(&self->stats.tx.syncp);
			data[count] = self->stats.tx.packets;
			data[++count] = self->stats.tx.queue_restarts;
		} while (u64_stats_fetch_retry_irq(&self->stats.tx.syncp, start));
	}

	return ++count;
}
