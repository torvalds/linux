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

struct aq_ptp_s {
	struct aq_nic_s *aq_nic;
	spinlock_t ptp_lock;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_info;
};

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
