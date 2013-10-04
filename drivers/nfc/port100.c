/*
 * Sony NFC Port-100 Series driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * Partly based/Inspired by Stephen Tiedemann's nfcpy
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <net/nfc/digital.h>

#define VERSION "0.1"

#define SONY_VENDOR_ID    0x054c
#define RCS380_PRODUCT_ID 0x06c1

#define PORT100_PROTOCOLS (NFC_PROTO_JEWEL_MASK    | \
			   NFC_PROTO_MIFARE_MASK   | \
			   NFC_PROTO_FELICA_MASK   | \
			   NFC_PROTO_NFC_DEP_MASK)

#define PORT100_CAPABILITIES (NFC_DIGITAL_DRV_CAPS_IN_CRC | \
			      NFC_DIGITAL_DRV_CAPS_TG_CRC)

struct port100 {
	struct nfc_digital_dev *nfc_digital_dev;

	int skb_headroom;
	int skb_tailroom;

	struct usb_device *udev;
	struct usb_interface *interface;
};

static void port100_abort_cmd(struct nfc_digital_dev *ddev)
{
}

static int port100_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	return -EOPNOTSUPP;
}

static int port100_in_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	return -EOPNOTSUPP;
}

static int port100_in_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 _timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_tg_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	return -EOPNOTSUPP;
}

static int port100_tg_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_listen_mdaa(struct nfc_digital_dev *ddev,
			       struct digital_tg_mdaa_params *params,
			       u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static int port100_listen(struct nfc_digital_dev *ddev, u16 timeout,
			  nfc_digital_cmd_complete_t cb, void *arg)
{
	return -EOPNOTSUPP;
}

static struct nfc_digital_ops port100_digital_ops = {
	.in_configure_hw = port100_in_configure_hw,
	.in_send_cmd = port100_in_send_cmd,

	.tg_listen_mdaa = port100_listen_mdaa,
	.tg_listen = port100_listen,
	.tg_configure_hw = port100_tg_configure_hw,
	.tg_send_cmd = port100_tg_send_cmd,

	.switch_rf = port100_switch_rf,
	.abort_cmd = port100_abort_cmd,
};

static const struct usb_device_id port100_table[] = {
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= SONY_VENDOR_ID,
	  .idProduct		= RCS380_PRODUCT_ID,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, port100_table);

static int port100_probe(struct usb_interface *interface,
			 const struct usb_device_id *id)
{
	struct port100 *dev;
	int rc;

	dev = devm_kzalloc(&interface->dev, sizeof(struct port100), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	usb_set_intfdata(interface, dev);

	nfc_info(&interface->dev, "Sony NFC Port-100 Series attached\n");

	dev->nfc_digital_dev = nfc_digital_allocate_device(&port100_digital_ops,
							   PORT100_PROTOCOLS,
							   PORT100_CAPABILITIES,
							   dev->skb_headroom,
							   dev->skb_tailroom);
	if (!dev->nfc_digital_dev) {
		nfc_err(&interface->dev,
			"Could not allocate nfc_digital_dev.\n");
		rc = -ENOMEM;
		goto error;
	}

	nfc_digital_set_parent_dev(dev->nfc_digital_dev, &interface->dev);
	nfc_digital_set_drvdata(dev->nfc_digital_dev, dev);

	rc = nfc_digital_register_device(dev->nfc_digital_dev);
	if (rc) {
		nfc_err(&interface->dev,
			"Could not register digital device.\n");
		goto free_nfc_dev;
	}

	return 0;

free_nfc_dev:
	nfc_digital_free_device(dev->nfc_digital_dev);

error:
	return rc;
}

static void port100_disconnect(struct usb_interface *interface)
{
	struct port100 *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	nfc_digital_unregister_device(dev->nfc_digital_dev);
	nfc_digital_free_device(dev->nfc_digital_dev);

	nfc_info(&interface->dev, "Sony Port-100 NFC device disconnected");
}

static struct usb_driver port100_driver = {
	.name =		"port100",
	.probe =	port100_probe,
	.disconnect =	port100_disconnect,
	.id_table =	port100_table,
};

module_usb_driver(port100_driver);

MODULE_DESCRIPTION("NFC Port-100 series usb driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
