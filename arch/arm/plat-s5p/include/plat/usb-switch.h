/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_S5P_USB_SWITCH_H
#define __PLAT_S5P_USB_SWITCH_H

struct s5p_usbswitch_platdata {
	unsigned gpio_host_detect;
	unsigned gpio_device_detect;
	unsigned gpio_host_vbus;
	unsigned gpio_drd_host_detect;
	unsigned gpio_drd_device_detect;

	struct device *ehci_dev;
	struct device *ohci_dev;
	struct device *xhci_dev;

	struct device *s3c_udc_dev;
	struct device *exynos_udc_dev;
};

extern void s5p_usbswitch_set_platdata(struct s5p_usbswitch_platdata *pd);
#endif /* __PLAT_S5P_USB_SWITCH_H */
