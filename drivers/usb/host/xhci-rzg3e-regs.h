/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XHCI_RZG3E_H
#define __XHCI_RZG3E_H

#define RZG3E_USB3_HOST_INTEN		0x1044	/* Interrupt Enable */
#define RZG3E_USB3_HOST_U3P0PIPESC(x)	(0x10c0 + (x) * 4) /* PIPE Status and Control Register */

#define RZG3E_USB3_HOST_INTEN_XHC	BIT(0)
#define RZG3E_USB3_HOST_INTEN_HSE	BIT(2)
#define RZG3E_USB3_HOST_INTEN_ENA	(RZG3E_USB3_HOST_INTEN_XHC | RZG3E_USB3_HOST_INTEN_HSE)

#endif /* __XHCI_RZG3E_H */
