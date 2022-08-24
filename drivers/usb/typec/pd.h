/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_POWER_DELIVERY__
#define __USB_POWER_DELIVERY__

#include <linux/device.h>
#include <linux/usb/typec.h>

struct usb_power_delivery {
	struct device dev;
	int id;
	u16 revision;
	u16 version;
};

struct usb_power_delivery_capabilities {
	struct device dev;
	struct usb_power_delivery *pd;
	enum typec_role role;
};

#define to_usb_power_delivery_capabilities(o) container_of(o, struct usb_power_delivery_capabilities, dev)
#define to_usb_power_delivery(o) container_of(o, struct usb_power_delivery, dev)

struct usb_power_delivery *usb_power_delivery_find(const char *name);

int usb_power_delivery_init(void);
void usb_power_delivery_exit(void);

#endif /* __USB_POWER_DELIVERY__ */
