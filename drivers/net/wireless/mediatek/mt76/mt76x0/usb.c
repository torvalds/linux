/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt76x0.h"
#include "mcu.h"
#include "trace.h"
#include "../mt76x02_util.h"
#include "../mt76x02_usb.h"

static struct usb_device_id mt76x0_device_table[] = {
	{ USB_DEVICE(0x148F, 0x7610) },	/* MT7610U */
	{ USB_DEVICE(0x13B1, 0x003E) },	/* Linksys AE6000 */
	{ USB_DEVICE(0x0E8D, 0x7610) },	/* Sabrent NTWLAC */
	{ USB_DEVICE(0x7392, 0xa711) },	/* Edimax 7711mac */
	{ USB_DEVICE(0x7392, 0xb711) },	/* Edimax / Elecom  */
	{ USB_DEVICE(0x148f, 0x761a) },	/* TP-Link TL-WDN5200 */
	{ USB_DEVICE(0x148f, 0x760a) },	/* TP-Link unknown */
	{ USB_DEVICE(0x0b05, 0x17d1) },	/* Asus USB-AC51 */
	{ USB_DEVICE(0x0b05, 0x17db) },	/* Asus USB-AC50 */
	{ USB_DEVICE(0x0df6, 0x0075) },	/* Sitecom WLA-3100 */
	{ USB_DEVICE(0x2019, 0xab31) },	/* Planex GW-450D */
	{ USB_DEVICE(0x2001, 0x3d02) },	/* D-LINK DWA-171 rev B1 */
	{ USB_DEVICE(0x0586, 0x3425) },	/* Zyxel NWD6505 */
	{ USB_DEVICE(0x07b8, 0x7610) },	/* AboCom AU7212 */
	{ USB_DEVICE(0x04bb, 0x0951) },	/* I-O DATA WN-AC433UK */
	{ USB_DEVICE(0x057c, 0x8502) },	/* AVM FRITZ!WLAN USB Stick AC 430 */
	{ USB_DEVICE(0x293c, 0x5702) },	/* Comcast Xfinity KXW02AAA  */
	{ USB_DEVICE(0x20f4, 0x806b) },	/* TRENDnet TEW-806UBH  */
	{ USB_DEVICE(0x7392, 0xc711) }, /* Devolo Wifi ac Stick */
	{ USB_DEVICE(0x0df6, 0x0079) }, /* Sitecom Europe B.V. ac  Stick */
	{ USB_DEVICE(0x2357, 0x0105),
	  .driver_info = 1,	     }, /* TP-LINK Archer T1U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7630, 0xff, 0x2, 0xff)}, /* MT7630U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7650, 0xff, 0x2, 0xff)}, /* MT7650U */
	{ 0, }
};

static int mt76x0u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.tx_prepare_skb = mt76x0_tx_prepare_skb,
		.tx_complete_skb = mt76x02_tx_complete_skb,
		.tx_status_data = mt76x02_tx_status_data,
		.rx_skb = mt76x0_queue_rx_skb,
	};
	struct usb_device *usb_dev = interface_to_usbdev(usb_intf);
	struct mt76x0_dev *dev;
	u32 asic_rev, mac_rev;
	int ret;

	dev = mt76x0_alloc_device(&usb_intf->dev, &drv_ops);
	if (!dev)
		return -ENOMEM;

	/* Quirk for Archer T1U */
	if (id->driver_info)
		dev->no_2ghz = true;

	usb_dev = usb_get_dev(usb_dev);
	usb_reset_device(usb_dev);

	usb_set_intfdata(usb_intf, dev);

	mt76x02u_init_mcu(&dev->mt76);
	ret = mt76u_init(&dev->mt76, usb_intf);
	if (ret)
		goto err;

	/* Disable the HW, otherwise MCU fail to initalize on hot reboot */
	mt76x0_chip_onoff(dev, false, false);

	if (!mt76x02_wait_for_mac(&dev->mt76)) {
		ret = -ETIMEDOUT;
		goto err;
	}

	asic_rev = mt76_rr(dev, MT_ASIC_VERSION);
	mac_rev = mt76_rr(dev, MT_MAC_CSR0);
	dev_info(dev->mt76.dev, "ASIC revision: %08x MAC revision: %08x\n",
		 asic_rev, mac_rev);

	/* Note: vendor driver skips this check for MT76X0U */
	if (!(mt76_rr(dev, MT_EFUSE_CTRL) & MT_EFUSE_CTRL_SEL))
		dev_warn(dev->mt76.dev, "Warning: eFUSE not present\n");

	ret = mt76u_alloc_queues(&dev->mt76);
	if (ret < 0)
		goto err;

	ret = mt76u_mcu_init_rx(&dev->mt76);
	if (ret < 0)
		goto err;

	mt76x0_chip_onoff(dev, true, true);

	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	ret = mt76x0u_mcu_init(dev);
	if (ret)
		goto err_hw;

	ret = mt76x0_register_device(dev);
	if (ret)
		goto err_hw;

	set_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	return 0;
err_hw:
	mt76x0_cleanup(dev);
err:
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	ieee80211_free_hw(dev->mt76.hw);
	return ret;
}

static void mt76x0_disconnect(struct usb_interface *usb_intf)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);
	bool initalized = test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	if (!initalized)
		return;

	ieee80211_unregister_hw(dev->mt76.hw);
	mt76x0_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	ieee80211_free_hw(dev->mt76.hw);
}

static int __maybe_unused mt76x0_suspend(struct usb_interface *usb_intf,
					 pm_message_t state)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);
	struct mt76_usb *usb = &dev->mt76.usb;

	mt76u_stop_queues(&dev->mt76);
	mt76x0_mac_stop(dev);
	usb_kill_urb(usb->mcu.res.urb);

	return 0;
}

static int __maybe_unused mt76x0_resume(struct usb_interface *usb_intf)
{
	struct mt76x0_dev *dev = usb_get_intfdata(usb_intf);
	struct mt76_usb *usb = &dev->mt76.usb;
	int ret;

	reinit_completion(&usb->mcu.cmpl);
	ret = mt76u_submit_buf(&dev->mt76, USB_DIR_IN,
			       MT_EP_IN_CMD_RESP,
			       &usb->mcu.res, GFP_KERNEL,
			       mt76u_mcu_complete_urb,
			       &usb->mcu.cmpl);
	if (ret < 0)
		goto err;

	ret = mt76u_submit_rx_buffers(&dev->mt76);
	if (ret < 0)
		goto err;

	tasklet_enable(&usb->rx_tasklet);
	tasklet_enable(&usb->tx_tasklet);

	ret = mt76x0_init_hardware(dev);
	if (ret)
		goto err;

	return 0;
err:
	mt76x0_cleanup(dev);
	return ret;
}

MODULE_DEVICE_TABLE(usb, mt76x0_device_table);
MODULE_LICENSE("GPL");

static struct usb_driver mt76x0_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x0_device_table,
	.probe		= mt76x0u_probe,
	.disconnect	= mt76x0_disconnect,
	.suspend	= mt76x0_suspend,
	.resume		= mt76x0_resume,
	.reset_resume	= mt76x0_resume,
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt76x0_driver);
