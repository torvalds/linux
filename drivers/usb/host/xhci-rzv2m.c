// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver for RZ/V2M
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/usb/rzv2m_usb3drd.h>
#include "xhci.h"
#include "xhci-plat.h"
#include "xhci-rzv2m.h"

#define RZV2M_USB3_INTEN	0x1044	/* Interrupt Enable */

#define RZV2M_USB3_INT_XHC_ENA	BIT(0)
#define RZV2M_USB3_INT_HSE_ENA	BIT(2)
#define RZV2M_USB3_INT_ENA_VAL	(RZV2M_USB3_INT_XHC_ENA \
				 | RZV2M_USB3_INT_HSE_ENA)

int xhci_rzv2m_init_quirk(struct usb_hcd *hcd)
{
	struct device *dev = hcd->self.controller;

	rzv2m_usb3drd_reset(dev->parent, true);

	return 0;
}

void xhci_rzv2m_start(struct usb_hcd *hcd)
{
	u32 int_en;

	if (hcd->regs) {
		/* Interrupt Enable */
		int_en = readl(hcd->regs + RZV2M_USB3_INTEN);
		int_en |= RZV2M_USB3_INT_ENA_VAL;
		writel(int_en, hcd->regs + RZV2M_USB3_INTEN);
	}
}
