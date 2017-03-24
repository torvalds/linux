/*
 * drivers/usb/host/xhci-rcar.h
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _XHCI_RCAR_H
#define _XHCI_RCAR_H

#define XHCI_RCAR_FIRMWARE_NAME_V1	"r8a779x_usb3_v1.dlmem"
#define XHCI_RCAR_FIRMWARE_NAME_V2	"r8a779x_usb3_v2.dlmem"
#define XHCI_RCAR_FIRMWARE_NAME_V3	"r8a779x_usb3_v3.dlmem"

#if IS_ENABLED(CONFIG_USB_XHCI_RCAR)
void xhci_rcar_start(struct usb_hcd *hcd);
int xhci_rcar_init_quirk(struct usb_hcd *hcd);
#else
static inline void xhci_rcar_start(struct usb_hcd *hcd)
{
}

static inline int xhci_rcar_init_quirk(struct usb_hcd *hcd)
{
	return 0;
}
#endif
#endif /* _XHCI_RCAR_H */
