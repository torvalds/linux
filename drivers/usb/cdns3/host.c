// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS and USBSSP DRD Driver - host side
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
#include "../host/xhci.h"
#include "../host/xhci-plat.h"

#define XECP_PORT_CAP_REG	0x8000
#define XECP_AUX_CTRL_REG1	0x8120

#define CFG_RXDET_P3_EN		BIT(15)
#define LPM_2_STB_SWITCH_EN	BIT(25)

static int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd);

static const struct xhci_plat_priv xhci_plat_cdns3_xhci = {
	.quirks = XHCI_SKIP_PHY_INIT | XHCI_AVOID_BEI,
	.suspend_quirk = xhci_cdns3_suspend_quirk,
};

#ifdef CONFIG_PM_SLEEP
struct cdns_hiber_data {
	struct usb_hcd *hcd;
	struct usb_hcd *shared_hcd;
	struct notifier_block pm_notifier;
	int (*pm_setup)(struct usb_hcd *hcd);
	int (*pm_remove)(struct cdns *cdns);
};
static struct cdns_hiber_data cdns3_hiber_data;

static int cdns_hiber_notifier(struct notifier_block *nb, unsigned long action,
			void *data)
{
	struct usb_hcd *hcd = cdns3_hiber_data.hcd;
	struct usb_hcd *shared_hcd = cdns3_hiber_data.shared_hcd;

	switch (action) {
	case PM_RESTORE_PREPARE:
		if (hcd->state == HC_STATE_SUSPENDED) {
			usb_hcd_resume_root_hub(hcd);
			usb_disable_autosuspend(hcd->self.root_hub);
		}
		if (shared_hcd->state == HC_STATE_SUSPENDED) {
			usb_hcd_resume_root_hub(shared_hcd);
			usb_disable_autosuspend(shared_hcd->self.root_hub);
		}
		break;
	case PM_POST_RESTORE:
		usb_enable_autosuspend(hcd->self.root_hub);
		usb_enable_autosuspend(shared_hcd->self.root_hub);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int cdns_register_pm_notifier(struct usb_hcd *hcd)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	cdns3_hiber_data.hcd = xhci->main_hcd;
	cdns3_hiber_data.shared_hcd = xhci->shared_hcd;
	cdns3_hiber_data.pm_notifier.notifier_call = cdns_hiber_notifier;
	return register_pm_notifier(&cdns3_hiber_data.pm_notifier);
}

static int cdns_unregister_pm_notifier(struct cdns *cdns)
{
	int ret;

	ret = unregister_pm_notifier(&cdns3_hiber_data.pm_notifier);

	cdns3_hiber_data.hcd = NULL;
	cdns3_hiber_data.shared_hcd = NULL;

	return ret;
}
#endif

static int xhci_cdns3_plat_setup(struct usb_hcd *hcd)
{
#ifdef CONFIG_PM_SLEEP
	if (cdns3_hiber_data.pm_setup)
		cdns3_hiber_data.pm_setup(hcd);
#endif
	return 0;
}

static int __cdns_host_init(struct cdns *cdns)
{
	struct platform_device *xhci;
	int ret;
	struct usb_hcd *hcd;

	cdns_drd_host_on(cdns);

	xhci = platform_device_alloc("xhci-hcd", PLATFORM_DEVID_AUTO);
	if (!xhci) {
		dev_err(cdns->dev, "couldn't allocate xHCI device\n");
		return -ENOMEM;
	}

	xhci->dev.parent = cdns->dev;
	cdns->host_dev = xhci;

	ret = platform_device_add_resources(xhci, cdns->xhci_res,
					    CDNS_XHCI_RESOURCES_NUM);
	if (ret) {
		dev_err(cdns->dev, "couldn't add resources to xHCI device\n");
		goto err1;
	}

	cdns->xhci_plat_data = kmemdup(&xhci_plat_cdns3_xhci,
			sizeof(struct xhci_plat_priv), GFP_KERNEL);
	if (!cdns->xhci_plat_data) {
		ret = -ENOMEM;
		goto err1;
	}

	if (cdns->pdata && (cdns->pdata->quirks & CDNS3_DEFAULT_PM_RUNTIME_ALLOW))
		cdns->xhci_plat_data->quirks |= XHCI_DEFAULT_PM_RUNTIME_ALLOW;

	cdns->xhci_plat_data->plat_setup = xhci_cdns3_plat_setup;
#ifdef CONFIG_PM_SLEEP
	if (cdns->pdata && (cdns->pdata->quirks & CDNS3_REGISTER_PM_NOTIFIER)) {
		cdns3_hiber_data.pm_setup = cdns_register_pm_notifier;
		cdns3_hiber_data.pm_remove = cdns_unregister_pm_notifier;
	}
#endif
	ret = platform_device_add_data(xhci, cdns->xhci_plat_data,
			sizeof(struct xhci_plat_priv));
	if (ret)
		goto free_memory;

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(cdns->dev, "failed to register xHCI device\n");
		goto free_memory;
	}

	/* Glue needs to access xHCI region register for Power management */
	hcd = platform_get_drvdata(xhci);
	if (hcd)
		cdns->xhci_regs = hcd->regs;

	return 0;

free_memory:
	kfree(cdns->xhci_plat_data);
err1:
	platform_device_put(xhci);
	return ret;
}


static int xhci_cdns3_suspend_quirk(struct usb_hcd *hcd)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	u32 value;

	if (pm_runtime_status_suspended(hcd->self.controller))
		return 0;

	/* set usbcmd.EU3S */
	value = readl(&xhci->op_regs->command);
	value |= CMD_PM_INDEX;
	writel(value, &xhci->op_regs->command);

	if (hcd->regs) {
		value = readl(hcd->regs + XECP_AUX_CTRL_REG1);
		value |= CFG_RXDET_P3_EN;
		writel(value, hcd->regs + XECP_AUX_CTRL_REG1);

		value = readl(hcd->regs + XECP_PORT_CAP_REG);
		value |= LPM_2_STB_SWITCH_EN;
		writel(value, hcd->regs + XECP_PORT_CAP_REG);
	}

	return 0;
}

static void cdns_host_exit(struct cdns *cdns)
{
#ifdef CONFIG_PM_SLEEP
	if (cdns3_hiber_data.pm_remove) {
		cdns3_hiber_data.pm_remove(cdns);
		cdns3_hiber_data.pm_remove = NULL;
		cdns3_hiber_data.pm_setup = NULL;
	}
#endif

	kfree(cdns->xhci_plat_data);
	platform_device_unregister(cdns->host_dev);
	cdns->host_dev = NULL;
	cdns_drd_host_off(cdns);
}

int cdns_host_init(struct cdns *cdns)
{
	struct cdns_role_driver *rdrv;

	rdrv = devm_kzalloc(cdns->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= __cdns_host_init;
	rdrv->stop	= cdns_host_exit;
	rdrv->state	= CDNS_ROLE_STATE_INACTIVE;
	rdrv->name	= "host";

	cdns->roles[USB_ROLE_HOST] = rdrv;

	return 0;
}
