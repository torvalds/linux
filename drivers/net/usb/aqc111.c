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

static int aqc111_write32_cmd_nopm(struct usbnet *dev, u8 cmd, u16 value,
				   u16 index, u32 *data)
{
	u32 tmp = *data;

	cpu_to_le32s(&tmp);

	return aqc111_write_cmd_nopm(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static int aqc111_write32_cmd(struct usbnet *dev, u8 cmd, u16 value,
			      u16 index, u32 *data)
{
	u32 tmp = *data;

	cpu_to_le32s(&tmp);

	return aqc111_write_cmd(dev, cmd, value, index, sizeof(tmp), &tmp);
}

static void aqc111_set_phy_speed(struct usbnet *dev, u8 autoneg, u16 speed)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;

	aqc111_data->phy_cfg &= ~AQ_ADV_MASK;
	aqc111_data->phy_cfg |= AQ_PAUSE;
	aqc111_data->phy_cfg |= AQ_ASYM_PAUSE;
	aqc111_data->phy_cfg |= AQ_DOWNSHIFT;
	aqc111_data->phy_cfg &= ~AQ_DSH_RETRIES_MASK;
	aqc111_data->phy_cfg |= (3 << AQ_DSH_RETRIES_SHIFT) &
				AQ_DSH_RETRIES_MASK;

	if (autoneg == AUTONEG_ENABLE) {
		switch (speed) {
		case SPEED_5000:
			aqc111_data->phy_cfg |= AQ_ADV_5G;
			/* fall-through */
		case SPEED_2500:
			aqc111_data->phy_cfg |= AQ_ADV_2G5;
			/* fall-through */
		case SPEED_1000:
			aqc111_data->phy_cfg |= AQ_ADV_1G;
			/* fall-through */
		case SPEED_100:
			aqc111_data->phy_cfg |= AQ_ADV_100M;
			/* fall-through */
		}
	} else {
		switch (speed) {
		case SPEED_5000:
			aqc111_data->phy_cfg |= AQ_ADV_5G;
			break;
		case SPEED_2500:
			aqc111_data->phy_cfg |= AQ_ADV_2G5;
			break;
		case SPEED_1000:
			aqc111_data->phy_cfg |= AQ_ADV_1G;
			break;
		case SPEED_100:
			aqc111_data->phy_cfg |= AQ_ADV_100M;
			break;
		}
	}

	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0, &aqc111_data->phy_cfg);
}

static const struct net_device_ops aqc111_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
};

static void aqc111_read_fw_version(struct usbnet *dev,
				   struct aqc111_data *aqc111_data)
{
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_MAJOR,
			1, 1, &aqc111_data->fw_ver.major);
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_MINOR,
			1, 1, &aqc111_data->fw_ver.minor);
	aqc111_read_cmd(dev, AQ_ACCESS_MAC, AQ_FW_VER_REV,
			1, 1, &aqc111_data->fw_ver.rev);

	if (aqc111_data->fw_ver.major & 0x80)
		aqc111_data->fw_ver.major &= ~0x80;
}

static int aqc111_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	enum usb_device_speed usb_speed = udev->speed;
	struct aqc111_data *aqc111_data;
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

	aqc111_data = kzalloc(sizeof(*aqc111_data), GFP_KERNEL);
	if (!aqc111_data)
		return -ENOMEM;

	/* store aqc111_data pointer in device data field */
	dev->driver_priv = aqc111_data;

	dev->net->netdev_ops = &aqc111_netdev_ops;

	aqc111_read_fw_version(dev, aqc111_data);
	aqc111_data->autoneg = AUTONEG_ENABLE;
	aqc111_data->advertised_speed = (usb_speed == USB_SPEED_SUPER) ?
					 SPEED_5000 : SPEED_1000;

	return 0;
}

static void aqc111_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16;

	/* Force bz */
	reg16 = SFR_PHYPWR_RSTCTL_BZ;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd_nopm(dev, AQ_ACCESS_MAC, SFR_PHYPWR_RSTCTL,
				2, &reg16);

	/* Power down ethernet PHY */
	aqc111_data->phy_cfg &= ~AQ_ADV_MASK;
	aqc111_data->phy_cfg |= AQ_LOW_POWER;
	aqc111_data->phy_cfg &= ~AQ_PHY_POWER_EN;
	aqc111_write32_cmd_nopm(dev, AQ_PHY_OPS, 0, 0,
				&aqc111_data->phy_cfg);

	kfree(aqc111_data);
}

static void aqc111_status(struct usbnet *dev, struct urb *urb)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u64 *event_data = NULL;
	int link = 0;

	if (urb->actual_length < sizeof(*event_data))
		return;

	event_data = urb->transfer_buffer;
	le64_to_cpus(event_data);

	if (*event_data & AQ_LS_MASK)
		link = 1;
	else
		link = 0;

	aqc111_data->link_speed = (*event_data & AQ_SPEED_MASK) >>
				  AQ_SPEED_SHIFT;
	aqc111_data->link = link;

	if (netif_carrier_ok(dev->net) != link)
		usbnet_defer_kevent(dev, EVENT_LINK_RESET);
}

static void aqc111_configure_rx(struct usbnet *dev,
				struct aqc111_data *aqc111_data)
{
	enum usb_device_speed usb_speed = dev->udev->speed;
	u16 link_speed = 0, usb_host = 0;
	u8 buf[5] = { 0 };
	u8 queue_num = 0;
	u16 reg16 = 0;
	u8 reg8 = 0;

	buf[0] = 0x00;
	buf[1] = 0xF8;
	buf[2] = 0x07;
	switch (aqc111_data->link_speed) {
	case AQ_INT_SPEED_5G:
		link_speed = 5000;
		reg8 = 0x05;
		reg16 = 0x001F;
		break;
	case AQ_INT_SPEED_2_5G:
		link_speed = 2500;
		reg16 = 0x003F;
		break;
	case AQ_INT_SPEED_1G:
		link_speed = 1000;
		reg16 = 0x009F;
		break;
	case AQ_INT_SPEED_100M:
		link_speed = 100;
		queue_num = 1;
		reg16 = 0x063F;
		buf[1] = 0xFB;
		buf[2] = 0x4;
		break;
	}

	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_INTER_PACKET_GAP_0,
			 1, 1, &reg8);

	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_TX_PAUSE_RESEND_T, 3, 3, buf);

	switch (usb_speed) {
	case USB_SPEED_SUPER:
		usb_host = 3;
		break;
	case USB_SPEED_HIGH:
		usb_host = 2;
		break;
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		usb_host = 1;
		queue_num = 0;
		break;
	default:
		usb_host = 0;
		break;
	}

	memcpy(buf, &AQC111_BULKIN_SIZE[queue_num], 5);
	/* RX bulk configuration */
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_RX_BULKIN_QCTRL, 5, 5, buf);

	/* Set high low water level */
	reg16 = 0x0810;

	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_PAUSE_WATERLVL_LOW,
			   2, &reg16);
	netdev_info(dev->net, "Link Speed %d, USB %d", link_speed, usb_host);
}

static int aqc111_link_reset(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16 = 0;
	u8 reg8 = 0;

	if (aqc111_data->link == 1) { /* Link up */
		aqc111_configure_rx(dev, aqc111_data);

		/* Vlan Tag Filter */
		reg8 = SFR_VLAN_CONTROL_VSO;

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_VLAN_ID_CONTROL,
				 1, 1, &reg8);

		reg8 = 0x0;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BMRX_DMA_CONTROL,
				 1, 1, &reg8);

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BMTX_DMA_CONTROL,
				 1, 1, &reg8);

		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_ARC_CTRL, 1, 1, &reg8);

		reg16 = SFR_RX_CTL_IPE | SFR_RX_CTL_AB;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

		reg8 = SFR_RX_PATH_READY;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_ETH_MAC_PATH,
				 1, 1, &reg8);

		reg8 = SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);

		reg16 = 0;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		reg16 = SFR_MEDIUM_XGMIIMODE | SFR_MEDIUM_FULL_DUPLEX;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				  2, &reg16);

		reg16 |= SFR_MEDIUM_RECEIVE_EN | SFR_MEDIUM_RXFLOW_CTRLEN |
			 SFR_MEDIUM_TXFLOW_CTRLEN;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		reg16 = SFR_RX_CTL_IPE | SFR_RX_CTL_AB | SFR_RX_CTL_START;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

		netif_carrier_on(dev->net);
	} else {
		aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				  2, &reg16);
		reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
				   2, &reg16);

		aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);
		reg16 &= ~SFR_RX_CTL_START;
		aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

		reg8 = SFR_BULK_OUT_FLUSH_EN | SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);
		reg8 = SFR_BULK_OUT_EFF_EN;
		aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BULK_OUT_CTRL,
				 1, 1, &reg8);

		netif_carrier_off(dev->net);
	}
	return 0;
}

static int aqc111_reset(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u8 reg8 = 0;

	/* Power up ethernet PHY */
	aqc111_data->phy_cfg = AQ_PHY_POWER_EN;
	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
			   &aqc111_data->phy_cfg);

	reg8 = 0xFF;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_BM_INT_MASK, 1, 1, &reg8);

	reg8 = 0x0;
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_SWP_CTRL, 1, 1, &reg8);

	aqc111_read_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);
	reg8 &= ~(SFR_MONITOR_MODE_EPHYRW | SFR_MONITOR_MODE_RWLC |
		  SFR_MONITOR_MODE_RWMP | SFR_MONITOR_MODE_RWWF |
		  SFR_MONITOR_MODE_RW_FLAG);
	aqc111_write_cmd(dev, AQ_ACCESS_MAC, SFR_MONITOR_MODE, 1, 1, &reg8);

	netif_carrier_off(dev->net);

	/* Phy advertise */
	aqc111_set_phy_speed(dev, aqc111_data->autoneg,
			     aqc111_data->advertised_speed);

	return 0;
}

static int aqc111_stop(struct usbnet *dev)
{
	struct aqc111_data *aqc111_data = dev->driver_priv;
	u16 reg16 = 0;

	aqc111_read16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			  2, &reg16);
	reg16 &= ~SFR_MEDIUM_RECEIVE_EN;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_MEDIUM_STATUS_MODE,
			   2, &reg16);
	reg16 = 0;
	aqc111_write16_cmd(dev, AQ_ACCESS_MAC, SFR_RX_CTL, 2, &reg16);

	/* Put PHY to low power*/
	aqc111_data->phy_cfg |= AQ_LOW_POWER;
	aqc111_write32_cmd(dev, AQ_PHY_OPS, 0, 0,
			   &aqc111_data->phy_cfg);

	netif_carrier_off(dev->net);

	return 0;
}

static const struct driver_info aqc111_info = {
	.description	= "Aquantia AQtion USB to 5GbE Controller",
	.bind		= aqc111_bind,
	.unbind		= aqc111_unbind,
	.status		= aqc111_status,
	.link_reset	= aqc111_link_reset,
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
