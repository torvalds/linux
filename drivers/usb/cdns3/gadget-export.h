/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS DRD Driver - Gadget Export APIs.
 *
 * Copyright (C) 2017 NXP
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_GADGET_EXPORT
#define __LINUX_CDNS3_GADGET_EXPORT

#ifdef CONFIG_USB_CDNS3_GADGET

int cdns3_gadget_init(struct cdns3 *cdns);
#else

static inline int cdns3_gadget_init(struct cdns3 *cdns)
{
	return -ENXIO;
}

#endif

#endif /* __LINUX_CDNS3_GADGET_EXPORT */
