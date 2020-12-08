/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS and USBSSP DRD Driver - Host Export APIs
 *
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_HOST_EXPORT
#define __LINUX_CDNS3_HOST_EXPORT

struct usb_hcd;

#if IS_ENABLED(CONFIG_USB_CDNS_HOST)

int cdns_host_init(struct cdns *cdns);
int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd);

#else

static inline int cdns_host_init(struct cdns *cdns)
{
	return -ENXIO;
}

static inline void cdns_host_exit(struct cdns *cdns) { }
static inline int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd)
{
	return 0;
}

#endif /* USB_CDNS_HOST */

#endif /* __LINUX_CDNS3_HOST_EXPORT */
