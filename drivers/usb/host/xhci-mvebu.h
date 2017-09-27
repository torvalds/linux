/*
 * Copyright (C) 2014 Marvell
 *
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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
