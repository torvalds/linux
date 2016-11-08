/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 and
* only version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef _EMAC_PHY_H_
#define _EMAC_PHY_H_

typedef int (*emac_sgmii_initialize)(struct emac_adapter *adpt);

/** emac_phy - internal emac phy
 * @base base address
 * @digital per-lane digital block
 * @initialize initialization function
 */
struct emac_phy {
	void __iomem		*base;
	void __iomem		*digital;
	emac_sgmii_initialize	initialize;
};

struct emac_adapter;

int emac_phy_config(struct platform_device *pdev, struct emac_adapter *adpt);

#endif /* _EMAC_PHY_H_ */
