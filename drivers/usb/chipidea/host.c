/*
 * host.c - ChipIdea USB host controller driver
 *
 * Copyright (c) 2012 Intel Corporation
 *
 * Author: Alexander Shishkin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/chipidea.h>

#define CHIPIDEA_EHCI
#include "../host/ehci-hcd.c"

#include "ci.h"
#include "bits.h"
#include "host.h"

static int ci_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	hcd->has_tt = 1;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	ehci_port_power(ehci, 0);

	return ret;
}

static const struct hc_driver ci_ehci_hc_driver = {
	.description	= "ehci_hcd",
	.product_desc	= "ChipIdea HDRC EHCI",
	.hcd_priv_size	= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq	= ehci_irq,
	.flags	= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset		= ci_ehci_setup,
	.start		= ehci_run,
	.stop		= ehci_stop,
	.shutdown	= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static irqreturn_t host_irq(struct ci13xxx *ci)
{
	return usb_hcd_irq(ci->irq, ci->hcd);
}

static int host_start(struct ci13xxx *ci)
{
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&ci_ehci_hc_driver, ci->dev, dev_name(ci->dev));
	if (!hcd)
		return -ENOMEM;

	dev_set_drvdata(ci->dev, ci);
	hcd->rsrc_start = ci->hw_bank.phys;
	hcd->rsrc_len = ci->hw_bank.size;
	hcd->regs = ci->hw_bank.abs;
	hcd->has_tt = 1;

	hcd->power_budget = ci->platdata->power_budget;
	hcd->phy = ci->transceiver;

	ehci = hcd_to_ehci(hcd);
	ehci->caps = ci->hw_bank.cap;
	ehci->has_hostpc = ci->hw_bank.lpm;

	ret = usb_add_hcd(hcd, 0, 0);
	if (ret)
		usb_put_hcd(hcd);
	else
		ci->hcd = hcd;

	return ret;
}

static void host_stop(struct ci13xxx *ci)
{
	struct usb_hcd *hcd = ci->hcd;

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
}

int ci_hdrc_host_init(struct ci13xxx *ci)
{
	struct ci_role_driver *rdrv;

	if (!hw_read(ci, CAP_DCCPARAMS, DCCPARAMS_HC))
		return -ENXIO;

	rdrv = devm_kzalloc(ci->dev, sizeof(struct ci_role_driver), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= host_start;
	rdrv->stop	= host_stop;
	rdrv->irq	= host_irq;
	rdrv->name	= "host";
	ci->roles[CI_ROLE_HOST] = rdrv;

	return 0;
}
