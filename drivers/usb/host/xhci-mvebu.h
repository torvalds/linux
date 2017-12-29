// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Marvell
 *
 * Gregory Clement <gregory.clement@free-electrons.com>
 */

#ifndef __LINUX_XHCI_MVEBU_H
#define __LINUX_XHCI_MVEBU_H

struct usb_hcd;

#if IS_ENABLED(CONFIG_USB_XHCI_MVEBU)
int xhci_mvebu_mbus_init_quirk(struct usb_hcd *hcd);
#else
static inline int xhci_mvebu_mbus_init_quirk(struct usb_hcd *hcd)
{
	return 0;
}
#endif
#endif /* __LINUX_XHCI_MVEBU_H */
