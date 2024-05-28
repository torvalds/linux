/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * USB roothub wrapper
 *
 * Copyright (C) 2018 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#ifndef __USB_CORE_PHY_H_
#define __USB_CORE_PHY_H_

struct device;
struct usb_phy_roothub;

struct usb_phy_roothub *usb_phy_roothub_alloc(struct device *dev);
struct usb_phy_roothub *usb_phy_roothub_alloc_usb3_phy(struct device *dev);

int usb_phy_roothub_init(struct usb_phy_roothub *phy_roothub);
int usb_phy_roothub_exit(struct usb_phy_roothub *phy_roothub);

int usb_phy_roothub_set_mode(struct usb_phy_roothub *phy_roothub,
			     enum phy_mode mode);
int usb_phy_roothub_calibrate(struct usb_phy_roothub *phy_roothub);
int usb_phy_roothub_notify_connect(struct usb_phy_roothub *phy_roothub, int port);
int usb_phy_roothub_notify_disconnect(struct usb_phy_roothub *phy_roothub, int port);
int usb_phy_roothub_power_on(struct usb_phy_roothub *phy_roothub);
void usb_phy_roothub_power_off(struct usb_phy_roothub *phy_roothub);

int usb_phy_roothub_suspend(struct device *controller_dev,
			    struct usb_phy_roothub *phy_roothub);
int usb_phy_roothub_resume(struct device *controller_dev,
			   struct usb_phy_roothub *phy_roothub);

#endif /* __USB_CORE_PHY_H_ */
