/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/* Used to set SoC specific callbacks */
struct usbmisc_ops {
	/* It's called once when probe a usb device */
	int (*init)(struct device *dev);
};

struct usbmisc_usb_device {
	struct device *dev; /* usb controller device */
	int index;

	unsigned int disable_oc:1; /* over current detect disabled */
};

int usbmisc_set_ops(const struct usbmisc_ops *ops);
void usbmisc_unset_ops(const struct usbmisc_ops *ops);
int
usbmisc_get_init_data(struct device *dev, struct usbmisc_usb_device *usbdev);
