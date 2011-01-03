/*
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/mxc_ehci.h>

int mxc_initialize_usb_hw(int port, unsigned int flags)
{
#if defined(CONFIG_SOC_IMX25)
	if (cpu_is_mx25())
		return mx25_initialize_usb_hw(port, flags);
#endif /* if defined(CONFIG_SOC_IMX25) */
#if defined(CONFIG_ARCH_MX3)
	if (cpu_is_mx31())
		return mx31_initialize_usb_hw(port, flags);
	if (cpu_is_mx35())
		return mx35_initialize_usb_hw(port, flags);
#endif /* CONFIG_ARCH_MX3 */
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27())
		return mx27_initialize_usb_hw(port, flags);
#endif /* CONFIG_MACH_MX27 */
#ifdef CONFIG_SOC_IMX51
	if (cpu_is_mx51())
		return mx51_initialize_usb_hw(port, flags);
#endif
	printk(KERN_WARNING
		"%s() unable to setup USBCONTROL for this CPU\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(mxc_initialize_usb_hw);

