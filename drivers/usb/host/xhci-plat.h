/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _XHCI_PLAT_H
#define _XHCI_PLAT_H

#include "xhci.h"	/* for hcd_to_xhci() */

enum xhci_plat_type {
	XHCI_PLAT_TYPE_MARVELL_ARMADA = 1,
	XHCI_PLAT_TYPE_RENESAS_RCAR_GEN2,
	XHCI_PLAT_TYPE_RENESAS_RCAR_GEN3,
};

struct xhci_plat_priv {
	enum xhci_plat_type type;
	const char *firmware_name;
};

#define hcd_to_xhci_priv(h) ((struct xhci_plat_priv *)hcd_to_xhci(h)->priv)

static inline bool xhci_plat_type_is(struct usb_hcd *hcd,
				     enum xhci_plat_type type)
{
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);

	if (priv && priv->type == type)
		return true;
	else
		return false;
}
#endif	/* _XHCI_PLAT_H */
