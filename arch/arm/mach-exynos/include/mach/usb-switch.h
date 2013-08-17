/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MACH_EXYNOS_USB_SWITCH_H
#define __MACH_EXYNOS_USB_SWITCH_H

struct s5p_usbswitch_platdata {
	unsigned gpio_host_detect;
	unsigned gpio_device_detect;
	unsigned gpio_host_vbus;

	struct device *ehci_dev;
	struct device *ohci_dev;

	struct device *s3c_udc_dev;
};

extern void s5p_usbswitch_set_platdata(struct s5p_usbswitch_platdata *pd);
#endif /* __MACH_EXYNOS_USB_SWITCH_H */
