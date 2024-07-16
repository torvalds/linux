// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/r8152.h>

#define OCP_BASE		0xe86c

static int pla_read_word(struct usbnet *dev, u16 index)
{
	u16 byen = BYTE_EN_WORD;
	u8 shift = index & 2;
	__le32 tmp;
	int ret;

	if (shift)
		byen <<= shift;

	index &= ~3;

	ret = usbnet_read_cmd(dev, RTL8152_REQ_GET_REGS, RTL8152_REQT_READ, index,
			      MCU_TYPE_PLA | byen, &tmp, sizeof(tmp));
	if (ret < 0)
		goto out;

	ret = __le32_to_cpu(tmp);
	ret >>= (shift * 8);
	ret &= 0xffff;

out:
	return ret;
}

static int pla_write_word(struct usbnet *dev, u16 index, u32 data)
{
	u32 mask = 0xffff;
	u16 byen = BYTE_EN_WORD;
	u8 shift = index & 2;
	__le32 tmp;
	int ret;

	data &= mask;

	if (shift) {
		byen <<= shift;
		mask <<= (shift * 8);
		data <<= (shift * 8);
	}

	index &= ~3;

	ret = usbnet_read_cmd(dev, RTL8152_REQ_GET_REGS, RTL8152_REQT_READ, index,
			      MCU_TYPE_PLA | byen, &tmp, sizeof(tmp));

	if (ret < 0)
		goto out;

	data |= __le32_to_cpu(tmp) & ~mask;
	tmp = __cpu_to_le32(data);

	ret = usbnet_write_cmd(dev, RTL8152_REQ_SET_REGS, RTL8152_REQT_WRITE, index,
			       MCU_TYPE_PLA | byen, &tmp, sizeof(tmp));

out:
	return ret;
}

static int r8153_ecm_mdio_read(struct net_device *netdev, int phy_id, int reg)
{
	struct usbnet *dev = netdev_priv(netdev);
	int ret;

	ret = pla_write_word(dev, OCP_BASE, 0xa000);
	if (ret < 0)
		goto out;

	ret = pla_read_word(dev, 0xb400 + reg * 2);

out:
	return ret;
}

static void r8153_ecm_mdio_write(struct net_device *netdev, int phy_id, int reg, int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	int ret;

	ret = pla_write_word(dev, OCP_BASE, 0xa000);
	if (ret < 0)
		return;

	ret = pla_write_word(dev, 0xb400 + reg * 2, val);
}

static int r8153_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int status;

	status = usbnet_cdc_bind(dev, intf);
	if (status < 0)
		return status;

	dev->mii.dev = dev->net;
	dev->mii.mdio_read = r8153_ecm_mdio_read;
	dev->mii.mdio_write = r8153_ecm_mdio_write;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.supports_gmii = 1;

	return status;
}

static const struct driver_info r8153_info = {
	.description =	"RTL8153 ECM Device",
	.flags =	FLAG_ETHER,
	.bind =		r8153_bind,
	.unbind =	usbnet_cdc_unbind,
	.status =	usbnet_cdc_status,
	.manage_power =	usbnet_manage_power,
};

static const struct usb_device_id products[] = {
/* Realtek RTL8153 Based USB 3.0 Ethernet Adapters */
{
	USB_DEVICE_AND_INTERFACE_INFO(VENDOR_ID_REALTEK, 0x8153, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&r8153_info,
},

/* Lenovo Powered USB-C Travel Hub (4X90S92381, based on Realtek RTL8153) */
{
	USB_DEVICE_AND_INTERFACE_INFO(VENDOR_ID_LENOVO, 0x721e, USB_CLASS_COMM,
				      USB_CDC_SUBCLASS_ETHERNET, USB_CDC_PROTO_NONE),
	.driver_info = (unsigned long)&r8153_info,
},

	{ },		/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static int rtl8153_ecm_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
#if IS_REACHABLE(CONFIG_USB_RTL8152)
	if (rtl8152_get_version(intf))
		return -ENODEV;
#endif

	return usbnet_probe(intf, id);
}

static struct usb_driver r8153_ecm_driver = {
	.name =		"r8153_ecm",
	.id_table =	products,
	.probe =	rtl8153_ecm_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
	.reset_resume =	usbnet_resume,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(r8153_ecm_driver);

MODULE_AUTHOR("Hayes Wang");
MODULE_DESCRIPTION("Realtek USB ECM device");
MODULE_LICENSE("GPL");
