/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS DRD Driver - Host Export APIs
 *
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_HOST_EXPORT
#define __LINUX_CDNS3_HOST_EXPORT

struct usb_hcd;
#ifdef CONFIG_USB_CDNS3_HOST

int cdns3_host_init(struct cdns3 *cdns);
int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd);

#else

static inline int cdns3_host_init(struct cdns3 *cdns)
{
	return -ENXIO;
}

static inline void cdns3_host_exit(struct cdns3 *cdns) { }
static inline int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd)
{
	return 0;
}

#endif /* CONFIG_USB_CDNS3_HOST */

#endif /* __LINUX_CDNS3_HOST_EXPORT */
