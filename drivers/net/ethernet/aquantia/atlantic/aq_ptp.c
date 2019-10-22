// SPDX-License-Identifier: GPL-2.0-only
/* Aquantia Corporation Network Driver
 * Copyright (C) 2014-2019 Aquantia Corporation. All rights reserved
 */

/* File aq_ptp.c:
 * Definition of functions for Linux PTP support.
 */

#include <linux/ptp_clock_kernel.h>
#include <linux/clocksource.h>

#include "aq_nic.h"
#include "aq_ptp.h"
#include "aq_ring.h"

struct ptp_skb_ring {
	struct sk_buff **buff;
	spinlock_t lock;
	unsigned int size;
	unsigned int head;
	unsigned int tail;
};

struct aq_ptp_s {
	struct aq_nic_s *aq_nic;
	spinlock_t ptp_lock;
	spinlock_t ptp_ring_lock;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_info;

	struct aq_ring_param_s ptp_ring_param;

	struct aq_ring_s ptp_tx;
	struct aq_ring_s ptp_rx;
	struct aq_ring_s hwts_rx;

	struct ptp_skb_ring skb_ring;
};

static int aq_ptp_skb_ring_init(struct ptp_skb_ring *ring, unsigned int size)
{
	struct sk_buff **buff = kmalloc(sizeof(*buff) * size, GFP_KERNEL);

	if (!buff)
		return -ENOMEM;

	spin_lock_init(&ring->lock);

	ring->buff = buff;
	ring->size = size;
	ring->head = 0;
	ring->tail = 0;

	return 0;
}

static void aq_ptp_skb_ring_release(struct ptp_skb_ring *ring)
{
	kfree(ring->buff);
	ring->buff = NULL;
}

/* aq_ptp_adjfine
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * adjust the frequency of the ptp cycle counter by the
 * indicated ppb from the base frequency.
 */
static int aq_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;

	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_hw_ops->hw_adj_clock_freq(aq_nic->aq_hw,
					     scaled_ppm_to_ppb(scaled_ppm));
	mutex_unlock(&aq_nic->fwreq_mutex);

	return 0;
}

/* aq_ptp_adjtime
 * @ptp: the ptp clock structure
 * @delta: offset to adjust the cycle counter by
 *
 * adjust the timer by resetting the timecounter structure.
 */
static int aq_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_adj_sys_clock(aq_nic->aq_hw, delta);
	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	return 0;
}

/* aq_ptp_gettime
 * @ptp: the ptp clock structure
 * @ts: timespec structure to hold the current time value
 *
 * read the timecounter and return the correct value on ns,
 * after converting it into a struct timespec.
 */
static int aq_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_get_ptp_ts(aq_nic->aq_hw, &ns);
	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/* aq_ptp_settime
 * @ptp: the ptp clock structure
 * @ts: the timespec containing the new time for the cycle counter
 *
 * reset the timecounter to use a new base value instead of the kernel
 * wall timer value.
 */
static int aq_ptp_settime(struct ptp_clock_info *ptp,
			  const struct timespec64 *ts)
{
	struct aq_ptp_s *aq_ptp = container_of(ptp, struct aq_ptp_s, ptp_info);
	struct aq_nic_s *aq_nic = aq_ptp->aq_nic;
	unsigned long flags;
	u64 ns = timespec64_to_ns(ts);
	u64 now;

	spin_lock_irqsave(&aq_ptp->ptp_lock, flags);
	aq_nic->aq_hw_ops->hw_get_ptp_ts(aq_nic->aq_hw, &now);
	aq_nic->aq_hw_ops->hw_adj_sys_clock(aq_nic->aq_hw, (s64)ns - (s64)now);

	spin_unlock_irqrestore(&aq_ptp->ptp_lock, flags);

	return 0;
}

int aq_ptp_ring_init(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int err = 0;

	if (!aq_ptp)
		return 0;

	err = aq_ring_init(&aq_ptp->ptp_tx);
	if (err < 0)
		goto err_exit;
	err = aq_nic->aq_hw_ops->hw_ring_tx_init(aq_nic->aq_hw,
						 &aq_ptp->ptp_tx,
						 &aq_ptp->ptp_ring_param);
	if (err < 0)
		goto err_exit;

	err = aq_ring_init(&aq_ptp->ptp_rx);
	if (err < 0)
		goto err_exit;
	err = aq_nic->aq_hw_ops->hw_ring_rx_init(aq_nic->aq_hw,
						 &aq_ptp->ptp_rx,
						 &aq_ptp->ptp_ring_param);
	if (err < 0)
		goto err_exit;

	err = aq_ring_rx_fill(&aq_ptp->ptp_rx);
	if (err < 0)
		goto err_rx_free;
	err = aq_nic->aq_hw_ops->hw_ring_rx_fill(aq_nic->aq_hw,
						 &aq_ptp->ptp_rx,
						 0U);
	if (err < 0)
		goto err_rx_free;

	err = aq_ring_init(&aq_ptp->hwts_rx);
	if (err < 0)
		goto err_rx_free;
	err = aq_nic->aq_hw_ops->hw_ring_rx_init(aq_nic->aq_hw,
						 &aq_ptp->hwts_rx,
						 &aq_ptp->ptp_ring_param);

	return err;

err_rx_free:
	aq_ring_rx_deinit(&aq_ptp->ptp_rx);
err_exit:
	return err;
}

int aq_ptp_ring_start(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	int err = 0;

	if (!aq_ptp)
		return 0;

	err = aq_nic->aq_hw_ops->hw_ring_tx_start(aq_nic->aq_hw, &aq_ptp->ptp_tx);
	if (err < 0)
		goto err_exit;

	err = aq_nic->aq_hw_ops->hw_ring_rx_start(aq_nic->aq_hw, &aq_ptp->ptp_rx);
	if (err < 0)
		goto err_exit;

	err = aq_nic->aq_hw_ops->hw_ring_rx_start(aq_nic->aq_hw,
						  &aq_ptp->hwts_rx);
	if (err < 0)
		goto err_exit;

err_exit:
	return err;
}

void aq_ptp_ring_stop(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_nic->aq_hw_ops->hw_ring_tx_stop(aq_nic->aq_hw, &aq_ptp->ptp_tx);
	aq_nic->aq_hw_ops->hw_ring_rx_stop(aq_nic->aq_hw, &aq_ptp->ptp_rx);

	aq_nic->aq_hw_ops->hw_ring_rx_stop(aq_nic->aq_hw, &aq_ptp->hwts_rx);
}

void aq_ptp_ring_deinit(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp || !aq_ptp->ptp_tx.aq_nic || !aq_ptp->ptp_rx.aq_nic)
		return;

	aq_ring_tx_clean(&aq_ptp->ptp_tx);
	aq_ring_rx_deinit(&aq_ptp->ptp_rx);
}

#define PTP_8TC_RING_IDX             8
#define PTP_4TC_RING_IDX            16
#define PTP_HWST_RING_IDX           31

int aq_ptp_ring_alloc(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	unsigned int tx_ring_idx, rx_ring_idx;
	struct aq_ring_s *hwts = 0;
	u32 tx_tc_mode, rx_tc_mode;
	struct aq_ring_s *ring;
	int err;

	if (!aq_ptp)
		return 0;

	/* Index must to be 8 (8 TCs) or 16 (4 TCs).
	 * It depends from Traffic Class mode.
	 */
	aq_nic->aq_hw_ops->hw_tx_tc_mode_get(aq_nic->aq_hw, &tx_tc_mode);
	if (tx_tc_mode == 0)
		tx_ring_idx = PTP_8TC_RING_IDX;
	else
		tx_ring_idx = PTP_4TC_RING_IDX;

	ring = aq_ring_tx_alloc(&aq_ptp->ptp_tx, aq_nic,
				tx_ring_idx, &aq_nic->aq_nic_cfg);
	if (!ring) {
		err = -ENOMEM;
		goto err_exit;
	}

	aq_nic->aq_hw_ops->hw_rx_tc_mode_get(aq_nic->aq_hw, &rx_tc_mode);
	if (rx_tc_mode == 0)
		rx_ring_idx = PTP_8TC_RING_IDX;
	else
		rx_ring_idx = PTP_4TC_RING_IDX;

	ring = aq_ring_rx_alloc(&aq_ptp->ptp_rx, aq_nic,
				rx_ring_idx, &aq_nic->aq_nic_cfg);
	if (!ring) {
		err = -ENOMEM;
		goto err_exit_ptp_tx;
	}

	hwts = aq_ring_hwts_rx_alloc(&aq_ptp->hwts_rx, aq_nic, PTP_HWST_RING_IDX,
				     aq_nic->aq_nic_cfg.rxds,
				     aq_nic->aq_nic_cfg.aq_hw_caps->rxd_size);
	if (!hwts) {
		err = -ENOMEM;
		goto err_exit_ptp_rx;
	}

	err = aq_ptp_skb_ring_init(&aq_ptp->skb_ring, aq_nic->aq_nic_cfg.rxds);
	if (err != 0) {
		err = -ENOMEM;
		goto err_exit_hwts_rx;
	}

	return 0;

err_exit_hwts_rx:
	aq_ring_free(&aq_ptp->hwts_rx);
err_exit_ptp_rx:
	aq_ring_free(&aq_ptp->ptp_rx);
err_exit_ptp_tx:
	aq_ring_free(&aq_ptp->ptp_tx);
err_exit:
	return err;
}

void aq_ptp_ring_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	aq_ring_free(&aq_ptp->ptp_tx);
	aq_ring_free(&aq_ptp->ptp_rx);
	aq_ring_free(&aq_ptp->hwts_rx);

	aq_ptp_skb_ring_release(&aq_ptp->skb_ring);
}

static struct ptp_clock_info aq_ptp_clock = {
	.owner		= THIS_MODULE,
	.name		= "atlantic ptp",
	.max_adj	= 999999999,
	.n_ext_ts	= 0,
	.pps		= 0,
	.adjfine	= aq_ptp_adjfine,
	.adjtime	= aq_ptp_adjtime,
	.gettime64	= aq_ptp_gettime,
	.settime64	= aq_ptp_settime,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pin_config	= NULL,
};

void aq_ptp_clock_init(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	aq_ptp_settime(&aq_ptp->ptp_info, &ts);
}

int aq_ptp_init(struct aq_nic_s *aq_nic, unsigned int idx_vec)
{
	struct hw_atl_utils_mbox mbox;
	struct ptp_clock *clock;
	struct aq_ptp_s *aq_ptp;
	int err = 0;

	if (!aq_nic->aq_hw_ops->hw_get_ptp_ts) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	if (!aq_nic->aq_fw_ops->enable_ptp) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	hw_atl_utils_mpi_read_stats(aq_nic->aq_hw, &mbox);

	if (!(mbox.info.caps_ex & BIT(CAPS_EX_PHY_PTP_EN))) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	aq_ptp = kzalloc(sizeof(*aq_ptp), GFP_KERNEL);
	if (!aq_ptp) {
		err = -ENOMEM;
		goto err_exit;
	}

	aq_ptp->aq_nic = aq_nic;

	spin_lock_init(&aq_ptp->ptp_lock);
	spin_lock_init(&aq_ptp->ptp_ring_lock);

	aq_ptp->ptp_info = aq_ptp_clock;
	clock = ptp_clock_register(&aq_ptp->ptp_info, &aq_nic->ndev->dev);
	if (!clock || IS_ERR(clock)) {
		netdev_err(aq_nic->ndev, "ptp_clock_register failed\n");
		err = PTR_ERR(clock);
		goto err_exit;
	}
	aq_ptp->ptp_clock = clock;

	aq_nic->aq_ptp = aq_ptp;

	/* enable ptp counter */
	aq_utils_obj_set(&aq_nic->aq_hw->flags, AQ_HW_PTP_AVAILABLE);
	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_fw_ops->enable_ptp(aq_nic->aq_hw, 1);
	aq_ptp_clock_init(aq_nic);
	mutex_unlock(&aq_nic->fwreq_mutex);

	return 0;

err_exit:
	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
	return err;
}

void aq_ptp_unregister(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	ptp_clock_unregister(aq_ptp->ptp_clock);
}

void aq_ptp_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	/* disable ptp */
	mutex_lock(&aq_nic->fwreq_mutex);
	aq_nic->aq_fw_ops->enable_ptp(aq_nic->aq_hw, 0);
	mutex_unlock(&aq_nic->fwreq_mutex);

	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
}
