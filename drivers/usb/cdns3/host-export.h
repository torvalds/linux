/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS and USBSSP DRD Driver - Host Export APIs
 *
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 */
#ifndef __LINUX_CDNS3_HOST_EXPORT
#define __LINUX_CDNS3_HOST_EXPORT

#if IS_ENABLED(CONFIG_USB_CDNS_HOST)

int cdns_host_init(struct cdns *cdns);

#else

static inline int cdns_host_init(struct cdns *cdns)
{
	return -ENXIO;
}

static inline void cdns_host_exit(struct cdns *cdns) { }

#endif /* USB_CDNS_HOST */

#endif /* __LINUX_CDNS3_HOST_EXPORT */
