/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell PTP driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef PTP_H
#define PTP_H

#include <linux/timecounter.h>
#include <linux/time64.h>
#include <linux/spinlock.h>

struct ptp {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	u64 (*read_ptp_tstmp)(struct ptp *ptp);
	spinlock_t ptp_lock; /* lock */
	u32 clock_rate;
};

struct ptp *ptp_get(void);
void ptp_put(struct ptp *ptp);
void ptp_start(struct ptp *ptp, u64 sclk, u32 ext_clk_freq, u32 extts);

extern struct pci_driver ptp_driver;

#endif
