/*
 * Copyright 2012-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H
#define __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H
#include <linux/usb/otg.h>
#include <linux/power_supply.h>

enum battery_charging_spec {
	BATTERY_CHARGING_SPEC_NONE = 0,
	BATTERY_CHARGING_SPEC_UNKNOWN,
	BATTERY_CHARGING_SPEC_1_0,
	BATTERY_CHARGING_SPEC_1_1,
	BATTERY_CHARGING_SPEC_1_2,
};

struct usb_charger {
	/* USB controller */
	struct device		*dev;
	struct power_supply	*psy;
	struct power_supply_desc	psy_desc;
	struct mutex		lock;

	/* Compliant with Battery Charging Specification version (if any) */
	enum battery_charging_spec	bc;

	/* properties */
	unsigned		present:1;
	unsigned		online:1;
	unsigned		max_current;
};

struct imx_usbmisc_data {
	struct device *dev;
	int index;
	struct regmap *anatop;
	struct usb_charger *charger;

	unsigned int disable_oc:1; /* over current detect disabled */
	unsigned int evdo:1; /* set external vbus divider option */
	/*
	 * Specifies the delay between powering up the xtal 24MHz clock
	 * and release the clock to the digital logic inside the analog block
	 */
	unsigned int osc_clkgate_delay;
	enum usb_dr_mode available_role;
};

int imx_usbmisc_init(struct imx_usbmisc_data *);
int imx_usbmisc_init_post(struct imx_usbmisc_data *);
int imx_usbmisc_set_wakeup(struct imx_usbmisc_data *, bool);
int imx_usbmisc_charger_detection(struct imx_usbmisc_data *data, bool connect);
int imx_usbmisc_charger_secondary_detection(struct imx_usbmisc_data *data);
int imx_usbmisc_power_lost_check(struct imx_usbmisc_data *);
int imx_usbmisc_hsic_set_connect(struct imx_usbmisc_data *);
int imx_usbmisc_hsic_set_clk(struct imx_usbmisc_data *, bool);
int imx_usbmisc_term_select_override(struct imx_usbmisc_data *data,
						bool enable, int val);

#endif /* __DRIVER_USB_CHIPIDEA_CI_HDRC_IMX_H */
