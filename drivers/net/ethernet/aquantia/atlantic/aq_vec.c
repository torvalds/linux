// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
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
			u64_stats_update_begin(&ring[AQ_VEC_RX_ID].stats.rx.syncp);
			ring[AQ_VEC_RX_ID].stats.rx.polls++;
			u64_stats_update_end(&ring[AQ_VEC_RX_ID].stats.rx.syncp);
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

err_exit:
		if (!was_tx_cleaned)
			work_done = budget;

		if (work_done < budget) {
			napi_complete_done(napi, work_done);
			self->aq_hw_ops->hw_irq_enable(self->aq_hw,
					1U << self->aq_ring_param.vec_idx);
		}
	}

	return work_done;
}

struct aq_vec_s *aq_vec_alloc(struct aq_nic_s *aq_nic, unsigned int idx,
			      struct aq_nic_cfg_s *aq_nic_cfg)
{
	struct aq_vec_s *self = NULL;

	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self)
		goto err_exit;

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

err_exit:
	return self;
}

int aq_vec_ring_alloc(struct aq_vec_s *self, struct aq_nic_s *aq_nic,
		      unsigned int idx, struct aq_nic_cfg_s *aq_nic_cfg)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;
	int err = 0;

	for (i = 0; i < aq_nic_cfg->tcs; ++i) {
		const unsigned int idx_ring = AQ_NIC_CFG_TCVEC2RING(aq_nic_cfg,
								    i, idx);

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
		aq_vec_ring_free(self);
		self = NULL;
	}

	return err;
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
		err = aq_ring_init(&ring[AQ_VEC_TX_ID], ATL_RING_TX);
		if (err < 0)
			goto err_exit;

		err = self->aq_hw_ops->hw_ring_tx_init(self->aq_hw,
						       &ring[AQ_VEC_TX_ID],
						       &self->aq_ring_param);
		if (err < 0)
			goto err_exit;

		err = aq_ring_init(&ring[AQ_VEC_RX_ID], ATL_RING_RX);
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
	if (!self)
		goto err_exit;

	netif_napi_del(&self->napi);

	kfree(self);

err_exit:;
}

void aq_vec_ring_free(struct aq_vec_s *self)
{
	struct aq_ring_s *ring = NULL;
	unsigned int i = 0U;

	if (!self)
		goto err_exit;

	for (i = 0U, ring = self->ring[0];
		self->tx_rings > i; ++i, ring = self->ring[i]) {
		aq_ring_free(&ring[AQ_VEC_TX_ID]);
		if (i < self->rx_rings)
			aq_ring_free(&ring[AQ_VEC_RX_ID]);
	}

	self->tx_rings = 0;
	self->rx_rings = 0;
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
	int err;

	if (!self)
		return IRQ_NONE;
	err = self->aq_hw_ops->hw_irq_read(self->aq_hw, &irq_mask);
	if (err < 0)
		return IRQ_NONE;

	if (irq_mask) {
		self->aq_hw_ops->hw_irq_disable(self->aq_hw,
			      1U << self->aq_ring_param.vec_idx);
		napi_schedule(&self->napi);
	} else {
		self->aq_hw_ops->hw_irq_enable(self->aq_hw, 1U);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

cpumask_t *aq_vec_get_affinity_mask(struct aq_vec_s *self)
{
	return &self->aq_ring_param.affinity_mask;
}

bool aq_vec_is_valid_tc(struct aq_vec_s *self, const unsigned int tc)
{
	return tc < self->rx_rings && tc < self->tx_rings;
}

unsigned int aq_vec_get_sw_stats(struct aq_vec_s *self, const unsigned int tc, u64 *data)
{
	unsigned int count;

	if (!aq_vec_is_valid_tc(self, tc))
		return 0;

	count = aq_ring_fill_stats_data(&self->ring[tc][AQ_VEC_RX_ID], data);
	count += aq_ring_fill_stats_data(&self->ring[tc][AQ_VEC_TX_ID], data + count);

	return count;
}
