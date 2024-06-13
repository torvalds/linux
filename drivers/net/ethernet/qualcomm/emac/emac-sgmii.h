/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 */

#ifndef _EMAC_SGMII_H_
#define _EMAC_SGMII_H_

struct emac_adapter;
struct platform_device;

/** emac_sgmii - internal emac phy
 * @init initialization function
 * @open called when the driver is opened
 * @close called when the driver is closed
 * @link_change called when the link state changes
 */
struct sgmii_ops {
	int (*init)(struct emac_adapter *adpt);
	int (*open)(struct emac_adapter *adpt);
	void (*close)(struct emac_adapter *adpt);
	int (*link_change)(struct emac_adapter *adpt, bool link_state);
	void (*reset)(struct emac_adapter *adpt);
};

/** emac_sgmii - internal emac phy
 * @base base address
 * @digital per-lane digital block
 * @irq the interrupt number
 * @decode_error_count reference count of consecutive decode errors
 * @sgmii_ops sgmii ops
 */
struct emac_sgmii {
	void __iomem		*base;
	void __iomem		*digital;
	unsigned int		irq;
	atomic_t		decode_error_count;
	struct	sgmii_ops	*sgmii_ops;
};

int emac_sgmii_config(struct platform_device *pdev, struct emac_adapter *adpt);

int emac_sgmii_init_fsm9900(struct emac_adapter *adpt);
int emac_sgmii_init_qdf2432(struct emac_adapter *adpt);
int emac_sgmii_init_qdf2400(struct emac_adapter *adpt);

int emac_sgmii_init(struct emac_adapter *adpt);
int emac_sgmii_open(struct emac_adapter *adpt);
void emac_sgmii_close(struct emac_adapter *adpt);
int emac_sgmii_link_change(struct emac_adapter *adpt, bool link_state);
void emac_sgmii_reset(struct emac_adapter *adpt);
#endif
