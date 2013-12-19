#ifndef __MACH_ULPI_H
#define __MACH_ULPI_H

#include <linux/usb/ulpi.h>

#ifdef CONFIG_USB_ULPI_VIEWPORT
static inline struct usb_phy *imx_otg_ulpi_create(unsigned int flags)
{
	return otg_ulpi_create(&ulpi_viewport_access_ops, flags);
}
#else
static inline struct usb_phy *imx_otg_ulpi_create(unsigned int flags)
{
	return NULL;
}
#endif

#endif /* __MACH_ULPI_H */

