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

#ifndef _EMAC_SGMII_H_
#define _EMAC_SGMII_H_

struct emac_adapter;
struct platform_device;

int emac_sgmii_init_v1(struct emac_adapter *adpt);
int emac_sgmii_init_v2(struct emac_adapter *adpt);
int emac_sgmii_config(struct platform_device *pdev, struct emac_adapter *adpt);
void emac_sgmii_reset(struct emac_adapter *adpt);

#endif
