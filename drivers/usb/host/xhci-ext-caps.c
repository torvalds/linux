// SPDX-License-Identifier: GPL-2.0
/*
 * XHCI extended capability handling
 *
 * Copyright (c) 2017 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pci.h>
#include "xhci.h"

#define USB_SW_DRV_NAME		"intel_xhci_usb_sw"
#define USB_SW_RESOURCE_SIZE	0x400

#define PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI	0x22b5

static const struct property_entry role_switch_props[] = {
	PROPERTY_ENTRY_BOOL("sw_switch_disable"),
	{},
};

static void xhci_intel_unregister_pdev(void *arg)
{
	platform_device_unregister(arg);
}

static int xhci_create_intel_xhci_sw_pdev(struct xhci_hcd *xhci, u32 cap_offset)
{
	struct usb_hcd *hcd = xhci_to_hcd(xhci);
	struct device *dev = hcd->self.controller;
	struct platform_device *pdev;
	struct pci_dev *pci = to_pci_dev(dev);
	struct resource	res = { 0, };
	int ret;

	pdev = platform_device_alloc(USB_SW_DRV_NAME, PLATFORM_DEVID_NONE);
	if (!pdev) {
		xhci_err(xhci, "couldn't allocate %s platform device\n",
			 USB_SW_DRV_NAME);
		return -ENOMEM;
	}

	res.start = hcd->rsrc_start + cap_offset;
	res.end	  = res.start + USB_SW_RESOURCE_SIZE - 1;
	res.name  = USB_SW_DRV_NAME;
	res.flags = IORESOURCE_MEM;

	ret = platform_device_add_resources(pdev, &res, 1);
	if (ret) {
		dev_err(dev, "couldn't add resources to intel_xhci_usb_sw pdev\n");
		platform_device_put(pdev);
		return ret;
	}

	if (pci->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI) {
		ret = platform_device_add_properties(pdev, role_switch_props);
		if (ret) {
			dev_err(dev, "failed to register device properties\n");
			platform_device_put(pdev);
			return ret;
		}
	}

	pdev->dev.parent = dev;

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(dev, "couldn't register intel_xhci_usb_sw pdev\n");
		platform_device_put(pdev);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, xhci_intel_unregister_pdev, pdev);
	if (ret) {
		dev_err(dev, "couldn't add unregister action for intel_xhci_usb_sw pdev\n");
		return ret;
	}

	return 0;
}

int xhci_ext_cap_init(struct xhci_hcd *xhci)
{
	void __iomem *base = &xhci->cap_regs->hc_capbase;
	u32 offset, val;
	int ret;

	offset = xhci_find_next_ext_cap(base, 0, 0);

	while (offset) {
		val = readl(base + offset);

		switch (XHCI_EXT_CAPS_ID(val)) {
		case XHCI_EXT_CAPS_VENDOR_INTEL:
			if (xhci->quirks & XHCI_INTEL_USB_ROLE_SW) {
				ret = xhci_create_intel_xhci_sw_pdev(xhci,
								     offset);
				if (ret)
					return ret;
			}
			break;
		}
		offset = xhci_find_next_ext_cap(base, offset, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xhci_ext_cap_init);
