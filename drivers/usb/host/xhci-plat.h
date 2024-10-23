/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#ifndef _XHCI_PLAT_H
#define _XHCI_PLAT_H

struct device;
struct platform_device;
struct usb_hcd;

struct xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	struct xhci_vendor_data *vendor_data;
	int (*plat_setup)(struct usb_hcd *);
	void (*plat_start)(struct usb_hcd *);
	int (*init_quirk)(struct usb_hcd *);
	int (*suspend_quirk)(struct usb_hcd *);
	int (*resume_quirk)(struct usb_hcd *);
};

#define hcd_to_xhci_priv(h) ((struct xhci_plat_priv *)hcd_to_xhci(h)->priv)
#define xhci_to_priv(x) ((struct xhci_plat_priv *)(x)->priv)

struct xhci_plat_priv_overwrite {
	struct xhci_vendor_ops *vendor_ops;
};

int xhci_plat_register_vendor_ops(struct xhci_vendor_ops *vendor_ops);

#endif	/* _XHCI_PLAT_H */
