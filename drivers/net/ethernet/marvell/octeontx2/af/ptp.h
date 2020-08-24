/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell PTP driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef PTP_H
#define PTP_H

#include <linux/timecounter.h>
#include <linux/time64.h>
#include <linux/spinlock.h>

struct ptp {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	u32 clock_rate;
};

struct ptp *ptp_get(void);
void ptp_put(struct ptp *ptp);

extern struct pci_driver ptp_driver;

#endif
