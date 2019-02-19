/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "../mt76x02_usb.h"
#include "mt76x2u.h"

static const struct usb_device_id mt76x2u_device_table[] = {
	{ USB_DEVICE(0x0e8d, 0x7612) }, /* Alfa AWUS036ACM */
	{ USB_DEVICE(0x0b05, 0x1833) },	/* Asus USB-AC54 */
	{ USB_DEVICE(0x0b05, 0x17eb) },	/* Asus USB-AC55 */
	{ USB_DEVICE(0x0b05, 0x180b) },	/* Asus USB-N53 B1 */
	{ USB_DEVICE(0x0e8d, 0x7612) },	/* Aukey USB-AC1200 */
	{ USB_DEVICE(0x057c, 0x8503) },	/* Avm FRITZ!WLAN AC860 */
	{ USB_DEVICE(0x7392, 0xb711) },	/* Edimax EW 7722 UAC */
	{ USB_DEVICE(0x0846, 0x9053) },	/* Netgear A6210 */
	{ USB_DEVICE(0x045e, 0x02e6) },	/* XBox One Wireless Adapter */
	{ },
};

static int mt76x2u_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.tx_prepare_skb = mt76x02u_tx_prepare_skb,
		.tx_complete_skb = mt76x02u_tx_complete_skb,
		.tx_status_data = mt76x02_tx_status_data,
		.rx_skb = mt76x02_queue_rx_skb,
		.sta_add = mt76x02_sta_add,
		.sta_remove = mt76x02_sta_remove,
	};
	struct usb_device *udev = interface_to_usbdev(intf);
	struct mt76x02_dev *dev;
	struct mt76_dev *mdev;
	int err;

	mdev = mt76_alloc_device(&intf->dev, sizeof(*dev), &mt76x2u_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt76x02_dev, mt76);

	udev = usb_get_dev(udev);
	usb_reset_device(udev);

	mt76x02u_init_mcu(mdev);
	err = mt76u_init(mdev, intf);
	if (err < 0)
		goto err;

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(mdev->dev, "ASIC revision: %08x\n", mdev->rev);

	err = mt76x2u_register_device(dev);
	if (err < 0)
		goto err;

	return 0;

err:
	ieee80211_free_hw(mt76_hw(dev));
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);

	return err;
}

static void mt76x2u_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct mt76x02_dev *dev = usb_get_intfdata(intf);
	struct ieee80211_hw *hw = mt76_hw(dev);

	set_bit(MT76_REMOVED, &dev->mt76.state);
	ieee80211_unregister_hw(hw);
	mt76x2u_cleanup(dev);

	ieee80211_free_hw(hw);
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);
}

static int __maybe_unused mt76x2u_suspend(struct usb_interface *intf,
					  pm_message_t state)
{
	struct mt76x02_dev *dev = usb_get_intfdata(intf);
	struct mt76_usb *usb = &dev->mt76.usb;

	mt76u_stop_queues(&dev->mt76);
	mt76x2u_stop_hw(dev);
	usb_kill_urb(usb->mcu.res.urb);

	return 0;
}

static int __maybe_unused mt76x2u_resume(struct usb_interface *intf)
{
	struct mt76x02_dev *dev = usb_get_intfdata(intf);
	struct mt76_usb *usb = &dev->mt76.usb;
	int err;

	reinit_completion(&usb->mcu.cmpl);
	err = mt76u_submit_buf(&dev->mt76, USB_DIR_IN,
			       MT_EP_IN_CMD_RESP,
			       &usb->mcu.res, GFP_KERNEL,
			       mt76u_mcu_complete_urb,
			       &usb->mcu.cmpl);
	if (err < 0)
		goto err;

	err = mt76u_submit_rx_buffers(&dev->mt76);
	if (err < 0)
		goto err;

	tasklet_enable(&usb->rx_tasklet);
	tasklet_enable(&usb->tx_tasklet);

	err = mt76x2u_init_hardware(dev);
	if (err < 0)
		goto err;

	return 0;

err:
	mt76x2u_cleanup(dev);
	return err;
}

MODULE_DEVICE_TABLE(usb, mt76x2u_device_table);
MODULE_FIRMWARE(MT7662_FIRMWARE);
MODULE_FIRMWARE(MT7662_ROM_PATCH);

static struct usb_driver mt76x2u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x2u_device_table,
	.probe		= mt76x2u_probe,
	.disconnect	= mt76x2u_disconnect,
#ifdef CONFIG_PM
	.suspend	= mt76x2u_suspend,
	.resume		= mt76x2u_resume,
	.reset_resume	= mt76x2u_resume,
#endif /* CONFIG_PM */
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt76x2u_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
