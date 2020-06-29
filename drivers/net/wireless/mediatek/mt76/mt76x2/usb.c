// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "../mt76x02_usb.h"
#include "mt76x2u.h"

static const struct usb_device_id mt76x2u_device_table[] = {
	{ USB_DEVICE(0x0b05, 0x1833) },	/* Asus USB-AC54 */
	{ USB_DEVICE(0x0b05, 0x17eb) },	/* Asus USB-AC55 */
	{ USB_DEVICE(0x0b05, 0x180b) },	/* Asus USB-N53 B1 */
	{ USB_DEVICE(0x0e8d, 0x7612) },	/* Aukey USBAC1200 - Alfa AWUS036ACM */
	{ USB_DEVICE(0x057c, 0x8503) },	/* Avm FRITZ!WLAN AC860 */
	{ USB_DEVICE(0x7392, 0xb711) },	/* Edimax EW 7722 UAC */
	{ USB_DEVICE(0x2c4e, 0x0103) },	/* Mercury UD13 */
	{ USB_DEVICE(0x0846, 0x9053) },	/* Netgear A6210 */
	{ USB_DEVICE(0x045e, 0x02e6) },	/* XBox One Wireless Adapter */
	{ USB_DEVICE(0x045e, 0x02fe) },	/* XBox One Wireless Adapter */
	{ },
};

static int mt76x2u_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.drv_flags = MT_DRV_SW_RX_AIRTIME,
		.survey_flags = SURVEY_INFO_TIME_TX,
		.update_survey = mt76x02_update_channel,
		.tx_prepare_skb = mt76x02u_tx_prepare_skb,
		.tx_complete_skb = mt76x02u_tx_complete_skb,
		.tx_status_data = mt76x02_tx_status_data,
		.rx_skb = mt76x02_queue_rx_skb,
		.sta_ps = mt76x02_sta_ps,
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

	usb_set_intfdata(intf, dev);

	mt76x02u_init_mcu(mdev);
	err = mt76u_init(mdev, intf, false);
	if (err < 0)
		goto err;

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(mdev->dev, "ASIC revision: %08x\n", mdev->rev);
	if (!is_mt76x2(dev)) {
		err = -ENODEV;
		goto err;
	}

	err = mt76x2u_register_device(dev);
	if (err < 0)
		goto err;

	return 0;

err:
	ieee80211_free_hw(mt76_hw(dev));
	mt76u_deinit(&dev->mt76);
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);

	return err;
}

static void mt76x2u_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct mt76x02_dev *dev = usb_get_intfdata(intf);
	struct ieee80211_hw *hw = mt76_hw(dev);

	set_bit(MT76_REMOVED, &dev->mphy.state);
	ieee80211_unregister_hw(hw);
	mt76x2u_cleanup(dev);
	mt76u_deinit(&dev->mt76);

	ieee80211_free_hw(hw);
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);
}

static int __maybe_unused mt76x2u_suspend(struct usb_interface *intf,
					  pm_message_t state)
{
	struct mt76x02_dev *dev = usb_get_intfdata(intf);

	mt76u_stop_rx(&dev->mt76);

	return 0;
}

static int __maybe_unused mt76x2u_resume(struct usb_interface *intf)
{
	struct mt76x02_dev *dev = usb_get_intfdata(intf);
	int err;

	err = mt76u_resume_rx(&dev->mt76);
	if (err < 0)
		goto err;

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
