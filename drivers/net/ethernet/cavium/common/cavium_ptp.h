// SPDX-License-Identifier: GPL-2.0
/* cavium_ptp.h - PTP 1588 clock on Cavium hardware
 * Copyright (c) 2003-2015, 2017 Cavium, Inc.
 */

#ifndef CAVIUM_PTP_H
#define CAVIUM_PTP_H

#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>

struct cavium_ptp {
	struct pci_dev *pdev;

	/* Serialize access to cycle_counter, time_counter and hw_registers */
	spinlock_t spin_lock;
	struct cyclecounter cycle_counter;
	struct timecounter time_counter;
	void __iomem *reg_base;

	u32 clock_rate;

	struct ptp_clock_info ptp_info;
	struct ptp_clock *ptp_clock;
};

#if IS_ENABLED(CONFIG_CAVIUM_PTP)

struct cavium_ptp *cavium_ptp_get(void);
void cavium_ptp_put(struct cavium_ptp *ptp);

static inline u64 cavium_ptp_tstamp2time(struct cavium_ptp *ptp, u64 tstamp)
{
	unsigned long flags;
	u64 ret;

	spin_lock_irqsave(&ptp->spin_lock, flags);
	ret = timecounter_cyc2time(&ptp->time_counter, tstamp);
	spin_unlock_irqrestore(&ptp->spin_lock, flags);

	return ret;
}

static inline int cavium_ptp_clock_index(struct cavium_ptp *clock)
{
	return ptp_clock_index(clock->ptp_clock);
}

#else

static inline struct cavium_ptp *cavium_ptp_get(void)
{
	return ERR_PTR(-ENODEV);
}

static inline void cavium_ptp_put(struct cavium_ptp *ptp) {}

static inline u64 cavium_ptp_tstamp2time(struct cavium_ptp *ptp, u64 tstamp)
{
	return 0;
}

static inline int cavium_ptp_clock_index(struct cavium_ptp *clock)
{
	return -1;
}

#endif

#endif
