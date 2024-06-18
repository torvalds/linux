/* SPDX-License-Identifier: GPL-2.0+ */
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
	unsigned int hsic:1; /* HSIC controller */
	unsigned int ext_id:1; /* ID from exteranl event */
	unsigned int ext_vbus:1; /* Vbus from exteranl event */
	struct usb_phy *usb_phy;
	enum usb_dr_mode available_role; /* runtime usb dr mode */
	int emp_curr_control;
	int dc_vol_level_adjust;
	int rise_fall_time_adjust;
};

int imx_usbmisc_init(struct imx_usbmisc_data *data);
int imx_usbmisc_init_post(struct imx_usbmisc_data *data);
int imx_usbmisc_hsic_set_connect(struct imx_usbmisc_data *data);
int imx_usbmisc_charger_detection(struct imx_usbmisc_data *data, bool connect);
int imx_usbmisc_suspend(struct imx_usbmisc_data *data, bool wakeup);
int imx_usbmisc_resume(struct imx_usbmisc_data *data, bool wakeup);

#endif /* __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H */
