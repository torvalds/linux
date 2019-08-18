// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 */

#ifndef __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H
#define __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H

struct imx_usbmisc_data {
	struct device *dev;
	int index;

	unsigned int disable_oc:1; /* over current detect disabled */

	/* true if over-current polarity is active low */
	unsigned int oc_pol_active_low:1;

	/* true if dt specifies polarity */
	unsigned int oc_pol_configured:1;

	unsigned int pwr_pol:1; /* power polarity */
	unsigned int evdo:1; /* set external vbus divider option */
	unsigned int ulpi:1; /* connected to an ULPI phy */
	unsigned int hsic:1; /* HSIC controlller */
};

int imx_usbmisc_init(struct imx_usbmisc_data *data);
int imx_usbmisc_init_post(struct imx_usbmisc_data *data);
int imx_usbmisc_set_wakeup(struct imx_usbmisc_data *data, bool enabled);
int imx_usbmisc_hsic_set_connect(struct imx_usbmisc_data *data);
int imx_usbmisc_hsic_set_clk(struct imx_usbmisc_data *data, bool on);

#endif /* __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H */
