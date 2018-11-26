// SPDX-License-Identifier: GPL-2.0-or-later
/* Aquantia Corp. Aquantia AQtion USB to 5GbE Controller
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 * Copyright (C) 2005 Phil Chang <pchang23@sbcglobal.net>
 * Copyright (C) 2002-2003 TiVo Inc.
 * Copyright (C) 2017-2018 ASIX
 * Copyright (C) 2018 Aquantia Corp.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>

#include "aqc111.h"

static int aqc111_read_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				u16 index, u16 size, void *data)
{
	int ret;

	ret = usbnet_read_cmd_nopm(dev, cmd, USB_DIR_IN | USB_TYPE_VENDOR |
				   USB_RECIP_DEVICE, value, index, data, size);

	if (unlikely(ret < 0))
		netdev_warn(dev->net,
			    "Failed to read(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, ret);

	return ret;
}

static int aqc111_read_cmd(struct usbnet *dev, u8 cmd, u16 value,
			   u16 index, u16 size, void *data)
{
	int ret;

	ret = usbnet_read_cmd(dev, cmd, USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, index, data, size);

	if (unlikely(ret < 0))
		netdev_warn(dev->net,
			    "Failed to read(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, ret);

	return ret;
}

static int aqc111_read16_cmd(struct usbnet *dev, u8 cmd, u16 value,
			     u16 index, u16 *data)
{
	int ret = 0;

	ret = aqc111_read_cmd(dev, cmd, value, index, sizeof(*data), data);
	le16_to_cpus(data);

	return ret;
}

static int __aqc111_write_cmd(struct usbnet *dev, u8 cmd, u8 reqtype,
			      u16 value, u16 index, u16 size, const void *data)
{
	int err = -ENOMEM;
	void *buf = NULL;

	netdev_dbg(dev->net,
		   "%s cmd=%#x reqtype=%#x value=%#x index=%#x size=%d\n",
		   __func__, cmd, reqtype, value, index, size);

	if (data) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			goto out;
	}

	err = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      cmd, reqtype, value, index, buf, size,
			      (cmd == AQ_PHY_POWER) ? AQ_USB_PHY_SET_TIMEOUT :
			      AQ_USB_SET_TIMEOUT);

	if (unlikely(err < 0))
		netdev_warn(dev->net,
			    "Failed to write(0x%x) reg index 0x%04x: %d\n",
			    cmd, index, err);
	kfree(buf);

out:
	return err;
}

static int aqc111_write_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				 u16 index, u16 size, void *data)
{
	int ret;

	ret = __aqc111_write_cmd(dev, cmd, USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index, size, data);

	return ret;
}

static int aqc111_write_cmd(struct usbnet *dev, u8 cmd, u16 value,
			    u16 index, u16 size, void *data)
{
	int ret;

	if (usb_autopm_get_interface(dev->intf) < 0)
		return -ENODEV;

	ret = __aqc111_write_cmd(dev, cmd, USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_DEVICE, value, index, size, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int aqc111_write16_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				   u16 index, u16 *data)
{
	u16 tmp = *data;

	cpu_to_le16s(&tmp);

	return aqc111_write_cmd_nopm(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write16_cmd(struct usbnet *dev, u8 cmd, u16 value,
			      u16 index, u16 *data)
{
	u16 tmp = *data;

	cpu_to_le16s(&tmp);

	return aqc111_write_cmd(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static const struct net_device_ops aqc111_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
};

static int aqc111_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int ret;

	/* Check if vendor configuration */
	if (udev->actconfig->desc.bConfigurationValue != 1) {
		usb_driver_set_configuration(udev, 1);
		return -ENODEV;
	}

	usb_reset_configuration(dev->udev);

	ret = usbnet_get_endpoints(dev, intf);
	if (ret < 0) {
		netdev_dbg(dev->net, "usbnet_get_endpoints failed");
		return ret;
	}

	dev->net->netdev_ops = &aqc111_netdev_ops;

	return 0;
}

static void aqc111_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	u16 reg16;

	/* Force bz */
	reg16 = SFR_PHYPWR_RSTCTL_BZ;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);
}

static int aqc111_reset(struct usbnet *dev)
{
	u8 reg8 = 0;

	reg8 = 0xFF;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BM_INT_MASK, 1, 1, &reg8);

	reg8 = 0x0;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_SWP_CTRL, 1, 1, &reg8);

	aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);
	reg8 &= ~(SFR_MONITOR_MODE_EPHYRW | SFR_MONITOR_MODE_RWLC |
		  SFR_MONITOR_MODE_RWMP | SFR_MONITOR_MODE_RWWF |
		  SFR_MONITOR_MODE_RW_FLAG);
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);

	return 0;
}

static int aqc111_stop(struct usbnet *dev)
{
	u16 reg16 = 0;

	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			  2, &reg16);
	reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			   2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

	return 0;
}

static const struct driver_info aqc111_info = {
	.description	= "Aquantia AQtion USB to 5GbE Controller",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.reset		= aqc111_reset,
	.stop		= aqc111_stop,
};

#define AQC111_USB_ETH_DEV(vid, pid, table) \
	USB_DEVICE_INTERFACE_CLASS((vid), (pid), USB_CLASS_VENDOR_SPEC), \
	.driver_info = (unsigned long)&(table) \
}, \
{ \
	USB_DEVICE_AND_INTERFACE_INFO((vid), (pid), \
				      USB_CLASS_COMM, \
				      USB_CDC_SUBCLASS_ETHERNET, \
				      USB_CDC_PROTO_NONE), \
	.driver_info = (unsigned long)&(table),

static const struct usb_device_id products[] = {
	{AQC111_USB_ETH_DEV(0x2eca, 0xc101, aqc111_info)},
	{ },/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver aq_driver = {
	.name		= "aqc111",
	.id_table	= products,
	.probe		= usbnet_probe,
	.disconnect	= usbnet_disconnect,
};

module_usb_driver(aq_driver);

MODULE_DESCRIPTION("Aquantia AQtion USB to 5/2.5GbE Controllers");
MODULE_LICENSE("GPL");
