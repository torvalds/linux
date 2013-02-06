/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_S5P_USBGADGET_H
#define __PLAT_S5P_USBGADGET_H

struct s5p_usbgadget_platdata {
	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);

#ifdef CONFIG_USB_S3C_OTG_HOST
	irqreturn_t (*udc_irq)(int irq, void *_dev);
#endif
	/* Value of USB PHY tune register */
	unsigned int		phy_tune;
	/* Mask of USB PHY tune register */
	unsigned int		phy_tune_mask;
};

extern void s5p_usbgadget_set_platdata(struct s5p_usbgadget_platdata *pd);

#endif /* __PLAT_S5P_EHCI_H */
