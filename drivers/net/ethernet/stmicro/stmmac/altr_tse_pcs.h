/* Copyright Altera Corporation (C) 2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
