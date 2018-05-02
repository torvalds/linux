/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_vec.c: Definition of common structure for vector of Rx and Tx rings.
 * Definition of functions for Rx and Tx rings. Friendly module for aq_nic.
 */

#include "aq_vec.h"
#include "aq_nic.h"
#include "aq_ring.h"
#include "aq_hw.h"

#include <linux/netdevice.h>

struct aq_vec_s {
	const struct aq_hw_ops *aq_hw_ops;
	struct aq_hw_s *aq_hw;
	struct aq_nic_s *aq_nic;
	unsigned int tx_rings;
	unsigned int rx_rings;
	struct aq_ring_param_s aq_ring_param;
	struct napi_struct napi;
	struct aq_ring_s ring[AQ_CFG_TCS_MAX][2];
};

#define AQ_VEC_TX_ID 0
#define AQ_VEC_RX_ID 1

static int aq_vec_poll(struct napi_struct *napi, int budget)
{
	struct aq_vec_s *self = container_of(napi, struct aq_vec_s, napi);
	unsigned int sw_tail_old = 0U;
	struct aq_ring_s *ring = NULL;
	bool was_tx_cleaned = true;
	unsigned int i = 0U;
	int work_done = 0;
	int err = 0;

	if (!self) {
		err = -EINVAL;
	} else {
		for (i = 0U, ring = self->ring[0];
			self->tx_rings > i; ++i, ring = self->ring[i]) {
			if (self->aq_hw_ops->hw_ring_tx_head_update) {
				err = self->aq_hw_ops->hw_ring_tx_head_update(
							self->aq_hw,
							&ring[AQ_VEC_TX_ID]);
				if (err < 0)
					goto err_exit;
			}

			if (ring[AQ_VEC_TX_ID].sw_head !=
			    ring[AQ_VEC_TX_ID].hw_head) {
				was_tx_cleaned = aq_ring_tx_clean(&ring[AQ_VEC_TX_ID]);
				aq_ring_update_queue_state(&ring[AQ_VEC_TX_ID]);
			}

			err = self->aq_hw_ops->hw_ring_rx_receive(self->aq_hw,
					    &ring[AQ_VEC_RX_ID]);
			if (err < 0)
				goto err_exit;

			if (ring[AQ_VEC_RX_ID].sw_head !=
				ring[AQ_VEC_RX_ID].hw_head) {
				err = aq_ring_rx_clean(&ring[AQ_VEC_RX_ID],
						       napi,
						       &work_done,
						       budget - work_done);
				if (err < 0)
					goto err_exit;

				sw_tail_old = ring[AQ_VEC_RX_ID].sw_tail;

				err = aq_ring_rx_fill(&ring[AQ_VEC_RX_ID]);
				if (err < 0)
					goto err_exit;

				err = self->aq_hw_ops->hw_ring_rx_fill(
					self->aq_hw,
					&ring[AQ_VEC_RX_ID], sw_tail_old);
				if (err < 0)
					goto err_exit;
			}
		}

		if (!was_tx_cleaned)
			work_done = budget;

		if (work_done < budget) {
			napi_complete_done(napi, work_done);
			self->aq_hw_ops->hw_irq_enable(self->aq_hw,
					1U << self->aq_ring_param.vec_idx);
		}
	}
err_exit:
	return work_done;
}

struct aq_vec_s *aq_vec_alloc(struct aq_nic_s *aq_nic, unsigned int idx,
			      struct aq_nic_cfg_s *aq_nic_cfg)
{
	struct aq_vec_s *self = NULL;
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;
	int err = 0;

	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		err = -ENOMEM;
		goto err_exit;
	}

	self->aq_nic = aq_nic;
	self->aq_ring_param.vec_idx = idx;
	self->aq_ring_param.cpu =
		idx + aq_nic_cfg->aq_rss.base_cpu_number;

	cpumask_set_cpu(self->aq_ring_param.cpu,
			&self->aq_ring_param.affinity_mask);

	self->tx_rings = 0;
	self->rx_rings = 0;

	netif_napi_add(aq_nic_get_ndev(aq_nic), &self->napi,
		       aq_vec_poll, AQ_CFG_NAPI_WEIGHT);

	for (i = 0; i < aq_nic_cfg->tcs; ++i) {
		unsigned int idx_ring = AQ_NIC_TCVEC2RING(self->nic,
						self->tx_rings,
						self->aq_ring_param.vec_idx);

		ring = aq_ring_tx_alloc(&self->ring[i][AQ_VEC_TX_ID], aq_nic,
					idx_ring, aq_nic_cfg);
		if (!ring) {
			err = -ENOMEM;
			goto err_exit;
		}

		++self->tx_rings;

		aq_nic_set_tx_ring(aq_nic, idx_ring, ring);

		ring = aq_ring_rx_alloc(&self->ring[i][AQ_VEC_RX_ID], aq_nic,
					idx_ring, aq_nic_cfg);
		if (!ring) {
			err = -ENOMEM;
			goto err_exit;
		}

		++self->rx_rings;
	}

err_exit:
	if (err < 0) {
		aq_vec_free(self);
		self = NULL;
	}
	return self;
}

int aq_vec_init(struct aq_vec_s *self, const struct aq_hw_ops *aq_hw_ops,
		struct aq_hw_s *aq_hw)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;
	int err = 0;

	self->aq_hw_ops = aq_hw_ops;
	self->aq_hw = aq_hw;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		err = aq_ring_init(&ring[AQ_VEC_TX_ID]);
		if (err < 0)
			goto err_exit;

		err = self->aq_hw_ops->hw_ring_tx_init(self->aq_hw,
						       &ring[AQ_VEC_TX_ID],
						       &self->aq_ring_param);
		if (err < 0)
			goto err_exit;

		err = aq_ring_init(&ring[AQ_VEC_RX_ID]);
		if (err < 0)
			goto err_exit;

		err = self->aq_hw_ops->hw_ring_rx_init(self->aq_hw,
						       &ring[AQ_VEC_RX_ID],
						       &self->aq_ring_param);
		if (err < 0)
			goto err_exit;

		err = aq_ring_rx_fill(&ring[AQ_VEC_RX_ID]);
		if (err < 0)
			goto err_exit;

		err = self->aq_hw_ops->hw_ring_rx_fill(self->aq_hw,
						       &ring[AQ_VEC_RX_ID], 0U);
		if (err < 0)
			goto err_exit;
	}

err_exit:
	return err;
}

int aq_vec_start(struct aq_vec_s *self)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;
	int err = 0;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		err = self->aq_hw_ops->hw_ring_tx_start(self->aq_hw,
							&ring[AQ_VEC_TX_ID]);
		if (err < 0)
			goto err_exit;

		err = self->aq_hw_ops->hw_ring_rx_start(self->aq_hw,
							&ring[AQ_VEC_RX_ID]);
		if (err < 0)
			goto err_exit;
	}

	napi_enable(&self->napi);

err_exit:
	return err;
}

void aq_vec_stop(struct aq_vec_s *self)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		self->aq_hw_ops->hw_ring_tx_stop(self->aq_hw,
						 &ring[AQ_VEC_TX_ID]);

		self->aq_hw_ops->hw_ring_rx_stop(self->aq_hw,
						 &ring[AQ_VEC_RX_ID]);
	}

	napi_disable(&self->napi);
}

void aq_vec_deinit(struct aq_vec_s *self)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;

	if (!self)
		goto err_exit;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		aq_ring_tx_clean(&ring[AQ_VEC_TX_ID]);
		aq_ring_rx_deinit(&ring[AQ_VEC_RX_ID]);
	}
err_exit:;
}

void aq_vec_free(struct aq_vec_s *self)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;

	if (!self)
		goto err_exit;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		aq_ring_free(&ring[AQ_VEC_TX_ID]);
		aq_ring_free(&ring[AQ_VEC_RX_ID]);
	}

	netif_napi_del(&self->napi);

	kfree(self);

err_exit:;
}

irqreturn_t aq_vec_isr(int irq, void *private)
{
	struct aq_vec_s *self = private;
	int err = 0;

	if (!self) {
		err = -EINVAL;
		goto err_exit;
	}
	napi_schedule(&self->napi);

err_exit:
	return err >= 0 ? IRQ_HANDLED : IRQ_NONE;
}

irqreturn_t aq_vec_isr_legacy(int irq, void *private)
{
	struct aq_vec_s *self = private;
	u64 irq_mask = 0U;
	irqreturn_t err = 0;

	if (!self) {
		err = -EINVAL;
		goto err_exit;
	}
	err = self->aq_hw_ops->hw_irq_read(self->aq_hw, &irq_mask);
	if (err < 0)
		goto err_exit;

	if (irq_mask) {
		self->aq_hw_ops->hw_irq_disable(self->aq_hw,
			      1U << self->aq_ring_param.vec_idx);
		napi_schedule(&self->napi);
	} else {
		self->aq_hw_ops->hw_irq_enable(self->aq_hw, 1U);
		err = IRQ_NONE;
	}

err_exit:
	return err >= 0 ? IRQ_HANDLED : IRQ_NONE;
}

cpumask_t *aq_vec_get_affinity_mask(struct aq_vec_s *self)
{
	return &self->aq_ring_param.affinity_mask;
}

void aq_vec_add_stats(struct aq_vec_s *self,
		      struct aq_ring_stats_rx_s *stats_rx,
		      struct aq_ring_stats_tx_s *stats_tx)
{
	struct aq_ring_s *ring = NULL;
	unsigned int r = 0U;

	for (r = 0U, ring = self->ring[0];
		self->tx_rings > r; ++r, ring = self->ring[r]) {
		struct aq_ring_stats_tx_s *tx = &ring[AQ_VEC_TX_ID].stats.tx;
		struct aq_ring_stats_rx_s *rx = &ring[AQ_VEC_RX_ID].stats.rx;

		stats_rx->packets += rx->packets;
		stats_rx->bytes += rx->bytes;
		stats_rx->errors += rx->errors;
		stats_rx->jumbo_packets += rx->jumbo_packets;
		stats_rx->lro_packets += rx->lro_packets;

		stats_tx->packets += tx->packets;
		stats_tx->bytes += tx->bytes;
		stats_tx->errors += tx->errors;
		stats_tx->queue_restarts += tx->queue_restarts;
	}
}

int aq_vec_get_sw_stats(struct aq_vec_s *self, u64 *data, unsigned int *p_count)
{
	unsigned int count = 0U;
	struct aq_ring_stats_rx_s stats_rx;
	struct aq_ring_stats_tx_s stats_tx;

	memset(&stats_rx, 0U, sizeof(struct aq_ring_stats_rx_s));
	memset(&stats_tx, 0U, sizeof(struct aq_ring_stats_tx_s));
	aq_vec_add_stats(self, &stats_rx, &stats_tx);

	/* This data should mimic aq_ethtool_queue_stat_names structure
	 */
	data[count] += stats_rx.packets;
	data[++count] += stats_tx.packets;
	data[++count] += stats_tx.queue_restarts;
	data[++count] += stats_rx.jumbo_packets;
	data[++count] += stats_rx.lro_packets;
	data[++count] += stats_rx.errors;

	if (p_count)
		*p_count = ++count;

	return 0;
}
