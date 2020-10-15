// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Driver - host side
 *
 * Copyright (C) 2018-2019 Cadence Design Systems.
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 *          Pawel Laszczak <pawell@cadence.com>
 */

#include <linux/platform_device.h>
#include "core.h"
#include "drd.h"
#include "host-export.h"
#include <linux/usb/hcd.h>

static int __cdns3_host_init(struct cdns3 *cdns)
{
	struct platform_device *xhci;
	int ret;
	struct usb_hcd *hcd;

	cdns3_drd_host_on(cdns);

	xhci = platform_device_alloc("xhci-hcd", PLATFORM_DEVID_AUTO);
	if (!xhci) {
		dev_err(cdns->dev, "couldn't allocate xHCI device\n");
		return -ENOMEM;
	}

	xhci->dev.parent = cdns->dev;
	cdns->host_dev = xhci;

	ret = platform_device_add_resources(xhci, cdns->xhci_res,
					    CDNS3_XHCI_RESOURCES_NUM);
	if (ret) {
		dev_err(cdns->dev, "couldn't add resources to xHCI device\n");
		goto err1;
	}

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(cdns->dev, "failed to register xHCI device\n");
		goto err1;
	}

	/* Glue needs to access xHCI region register for Power management */
	hcd = platform_get_drvdata(xhci);
	if (hcd)
		cdns->xhci_regs = hcd->regs;

	return 0;
err1:
	platform_device_put(xhci);
	return ret;
}

static void cdns3_host_exit(struct cdns3 *cdns)
{
	platform_device_unregister(cdns->host_dev);
	cdns->host_dev = NULL;
	cdns3_drd_host_off(cdns);
}

int cdns3_host_init(struct cdns3 *cdns)
{
	struct cdns3_role_driver *rdrv;

	rdrv = devm_kzalloc(cdns->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= __cdns3_host_init;
	rdrv->stop	= cdns3_host_exit;
	rdrv->state	= CDNS3_ROLE_STATE_INACTIVE;
	rdrv->name	= "host";

	cdns->roles[USB_ROLE_HOST] = rdrv;

	return 0;
}
