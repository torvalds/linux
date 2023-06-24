// SPDX-License-Identifier: GPL-2.0
/*
 * host.c - DesignWare USB3 DRD Controller Host Glue
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 */

#include <linux/irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "core.h"

static void dwc3_host_fill_xhci_irq_res(struct dwc3 *dwc,
					int irq, char *name)
{
	struct platform_device *pdev = to_platform_device(dwc->dev);
	struct device_node *np = dev_of_node(&pdev->dev);

	dwc->xhci_resources[1].start = irq;
	dwc->xhci_resources[1].end = irq;
	dwc->xhci_resources[1].flags = IORESOURCE_IRQ | irq_get_trigger_type(irq);
	if (!name && np)
		dwc->xhci_resources[1].name = of_node_full_name(pdev->dev.of_node);
	else
		dwc->xhci_resources[1].name = name;
}

static int dwc3_host_get_irq(struct dwc3 *dwc)
{
	struct platform_device	*dwc3_pdev = to_platform_device(dwc->dev);
	int irq;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "host");
	if (irq > 0) {
		dwc3_host_fill_xhci_irq_res(dwc, irq, "host");
		goto out;
	}

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq_byname_optional(dwc3_pdev, "dwc_usb3");
	if (irq > 0) {
		dwc3_host_fill_xhci_irq_res(dwc, irq, "dwc_usb3");
		goto out;
	}

	if (irq == -EPROBE_DEFER)
		goto out;

	irq = platform_get_irq(dwc3_pdev, 0);
	if (irq > 0) {
		dwc3_host_fill_xhci_irq_res(dwc, irq, NULL);
		goto out;
	}

	if (!irq)
		irq = -EINVAL;

out:
	return irq;
}

int dwc3_host_init(struct dwc3 *dwc)
{
	struct property_entry	props[4];
	struct platform_device	*xhci;
	int			ret, irq;
	int			prop_idx = 0;

	irq = dwc3_host_get_irq(dwc);
	if (irq < 0)
		return irq;

	xhci = platform_device_alloc("xhci-hcd", PLATFORM_DEVID_AUTO);
	if (!xhci) {
		dev_err(dwc->dev, "couldn't allocate xHCI device\n");
		return -ENOMEM;
	}

	xhci->dev.parent	= dwc->dev;

	dwc->xhci = xhci;

	ret = platform_device_add_resources(xhci, dwc->xhci_resources,
						DWC3_XHCI_RESOURCES_NUM);
	if (ret) {
		dev_err(dwc->dev, "couldn't add resources to xHCI device\n");
		goto err;
	}

	memset(props, 0, sizeof(struct property_entry) * ARRAY_SIZE(props));

	if (dwc->usb3_lpm_capable)
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("usb3-lpm-capable");

	if (dwc->usb2_lpm_disable)
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("usb2-lpm-disable");

	/**
	 * WORKAROUND: dwc3 revisions <=3.00a have a limitation
	 * where Port Disable command doesn't work.
	 *
	 * The suggested workaround is that we avoid Port Disable
	 * completely.
	 *
	 * This following flag tells XHCI to do just that.
	 */
	if (DWC3_VER_IS_WITHIN(DWC3, ANY, 300A))
		props[prop_idx++] = PROPERTY_ENTRY_BOOL("quirk-broken-port-ped");

	if (prop_idx) {
		ret = device_create_managed_software_node(&xhci->dev, props, NULL);
		if (ret) {
			dev_err(dwc->dev, "failed to add properties to xHCI\n");
			goto err;
		}
	}

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(dwc->dev, "failed to register xHCI device\n");
		goto err;
	}

	return 0;
err:
	platform_device_put(xhci);
	return ret;
}

void dwc3_host_exit(struct dwc3 *dwc)
{
	platform_device_unregister(dwc->xhci);
	dwc->xhci = NULL;
}
