/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright Altera Corporation (C) 2016. All rights reserved.
 *
 * Author: Tien Hock Loh <thloh@altera.com>
 */

#ifndef __TSE_PCS_H__
#define __TSE_PCS_H__

#include <linux/phy.h>
#include <linux/timer.h>

struct tse_pcs {
	struct device *dev;
	void __iomem *tse_pcs_base;
	void __iomem *sgmii_adapter_base;
	struct timer_list aneg_link_timer;
	int autoneg;
};

int tse_pcs_init(void __iomem *base, struct tse_pcs *pcs);
void tse_pcs_fix_mac_speed(struct tse_pcs *pcs, struct phy_device *phy_dev,
			   unsigned int speed);

#endif /* __TSE_PCS_H__ */
