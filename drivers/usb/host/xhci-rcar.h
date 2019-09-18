// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/usb/host/xhci-rcar.h
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 */

#ifndef _XHCI_RCAR_H
#define _XHCI_RCAR_H

#define XHCI_RCAR_FIRMWARE_NAME_V1	"r8a779x_usb3_v1.dlmem"
#define XHCI_RCAR_FIRMWARE_NAME_V2	"r8a779x_usb3_v2.dlmem"
#define XHCI_RCAR_FIRMWARE_NAME_V3	"r8a779x_usb3_v3.dlmem"

#if IS_ENABLED(CONFIG_USB_XHCI_RCAR)
void xhci_rcar_start(struct usb_hcd *hcd);
int xhci_rcar_init_quirk(struct usb_hcd *hcd);
int xhci_rcar_resume_quirk(struct usb_hcd *hcd);
#else
static inline void xhci_rcar_start(struct usb_hcd *hcd)
{
}

static inline int xhci_rcar_init_quirk(struct usb_hcd *hcd)
{
	return 0;
}

static inline int xhci_rcar_resume_quirk(struct usb_hcd *hcd)
{
	return 0;
}
#endif

/*
 * On R-Car Gen2 and Gen3, the AC64 bit (bit 0) of HCCPARAMS1 is set
 * to 1. However, these SoCs don't support 64-bit address memory
 * pointers. So, this driver clears the AC64 bit of xhci->hcc_params
 * to call dma_set_coherent_mask(dev, DMA_BIT_MASK(32)) in
 * xhci_gen_setup() by using the XHCI_NO_64BIT_SUPPORT quirk.
 *
 * And, since the firmware/internal CPU control the USBSTS.STS_HALT
 * and the process speed is down when the roothub port enters U3,
 * long delay for the handshake of STS_HALT is neeed in xhci_suspend()
 * by using the XHCI_SLOW_SUSPEND quirk.
 */
#define SET_XHCI_PLAT_PRIV_FOR_RCAR(firmware)				\
	.firmware_name = firmware,					\
	.quirks = XHCI_NO_64BIT_SUPPORT | XHCI_TRUST_TX_LENGTH |	\
		  XHCI_SLOW_SUSPEND,					\
	.init_quirk = xhci_rcar_init_quirk,				\
	.plat_start = xhci_rcar_start,					\
	.resume_quirk = xhci_rcar_resume_quirk,

#endif /* _XHCI_RCAR_H */
