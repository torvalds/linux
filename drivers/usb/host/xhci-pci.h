/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2020 Linaro Limited */

#ifndef XHCI_PCI_H
#define XHCI_PCI_H

#if IS_ENABLED(CONFIG_USB_XHCI_PCI_RENESAS)
int renesas_xhci_check_request_fw(struct pci_dev *dev,
				  const struct pci_device_id *id);

#else
static int renesas_xhci_check_request_fw(struct pci_dev *dev,
					 const struct pci_device_id *id)
{
	return 0;
}

#endif

struct xhci_driver_data {
	u64 quirks;
	const char *firmware;
};

#endif
