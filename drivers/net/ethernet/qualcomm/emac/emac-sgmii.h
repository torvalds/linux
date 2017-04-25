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

typedef int (*emac_sgmii_function)(struct emac_adapter *adpt);

/** emac_sgmii - internal emac phy
 * @base base address
 * @digital per-lane digital block
 * @irq the interrupt number
 * @decode_error_count reference count of consecutive decode errors
 * @initialize initialization function
 * @open called when the driver is opened
 * @close called when the driver is closed
 * @link_up called when the link comes up
 * @link_down called when the link comes down
 */
struct emac_sgmii {
	void __iomem		*base;
	void __iomem		*digital;
	unsigned int		irq;
	atomic_t		decode_error_count;
	emac_sgmii_function	initialize;
	emac_sgmii_function	open;
	emac_sgmii_function	close;
	emac_sgmii_function	link_up;
	emac_sgmii_function	link_down;
};

int emac_sgmii_config(struct platform_device *pdev, struct emac_adapter *adpt);
void emac_sgmii_reset(struct emac_adapter *adpt);

int emac_sgmii_init_fsm9900(struct emac_adapter *adpt);
int emac_sgmii_init_qdf2432(struct emac_adapter *adpt);
int emac_sgmii_init_qdf2400(struct emac_adapter *adpt);

#endif
