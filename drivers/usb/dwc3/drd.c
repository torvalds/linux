// SPDX-License-Identifier: GPL-2.0
/**
 * drd.c - DesignWare USB3 DRD Controller Dual-role support
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Roger Quadros <rogerq@ti.com>
 */

#include <linux/extcon.h>

#include "debug.h"
#include "core.h"
#include "gadget.h"

static void dwc3_drd_update(struct dwc3 *dwc)
{
	int id;

	id = extcon_get_state(dwc->edev, EXTCON_USB_HOST);
	if (id < 0)
		id = 0;

	dwc3_set_mode(dwc, id ?
		      DWC3_GCTL_PRTCAP_HOST :
		      DWC3_GCTL_PRTCAP_DEVICE);
}

static int dwc3_drd_notifier(struct notifier_block *nb,
			     unsigned long event, void *ptr)
{
	struct dwc3 *dwc = container_of(nb, struct dwc3, edev_nb);

	dwc3_set_mode(dwc, event ?
		      DWC3_GCTL_PRTCAP_HOST :
		      DWC3_GCTL_PRTCAP_DEVICE);

	return NOTIFY_DONE;
}

int dwc3_drd_init(struct dwc3 *dwc)
{
	int ret;

	if (dwc->dev->of_node) {
		if (of_property_read_bool(dwc->dev->of_node, "extcon"))
			dwc->edev = extcon_get_edev_by_phandle(dwc->dev, 0);

		if (IS_ERR(dwc->edev))
			return PTR_ERR(dwc->edev);

		dwc->edev_nb.notifier_call = dwc3_drd_notifier;
		ret = extcon_register_notifier(dwc->edev, EXTCON_USB_HOST,
					       &dwc->edev_nb);
		if (ret < 0) {
			dev_err(dwc->dev, "couldn't register cable notifier\n");
			return ret;
		}
	}

	dwc3_drd_update(dwc);

	return 0;
}

void dwc3_drd_exit(struct dwc3 *dwc)
{
	extcon_unregister_notifier(dwc->edev, EXTCON_USB_HOST,
				   &dwc->edev_nb);

	dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);
	flush_work(&dwc->drd_work);
	dwc3_gadget_exit(dwc);
}
