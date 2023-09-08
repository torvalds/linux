/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XHCI_RZV2M_H
#define __XHCI_RZV2M_H

#if IS_ENABLED(CONFIG_USB_XHCI_RZV2M)
void xhci_rzv2m_start(struct usb_hcd *hcd);
int xhci_rzv2m_init_quirk(struct usb_hcd *hcd);
#else
static inline void xhci_rzv2m_start(struct usb_hcd *hcd) {}
static inline int xhci_rzv2m_init_quirk(struct usb_hcd *hcd)
{
	return -EINVAL;
}
#endif

#endif /* __XHCI_RZV2M_H */
