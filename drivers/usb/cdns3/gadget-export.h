/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS and USBSSP DRD Driver - Gadget Export APIs.
 *
 * Copyright (C) 2017 NXP
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_GADGET_EXPORT
#define __LINUX_CDNS3_GADGET_EXPORT

#if IS_ENABLED(CONFIG_USB_CDNSP_GADGET)

int cdnsp_gadget_init(struct cdns *cdns);
#else

static inline int cdnsp_gadget_init(struct cdns *cdns)
{
	return -ENXIO;
}

#endif /* CONFIG_USB_CDNSP_GADGET */

#if IS_ENABLED(CONFIG_USB_CDNS3_GADGET)

int cdns3_gadget_init(struct cdns *cdns);
#else

static inline int cdns3_gadget_init(struct cdns *cdns)
{
	return -ENXIO;
}

#endif /* CONFIG_USB_CDNS3_GADGET */

#endif /* __LINUX_CDNS3_GADGET_EXPORT */
