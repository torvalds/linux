// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt7615.h"
#include "mac.h"
#include "mcu.h"
#include "regs.h"

static const struct usb_device_id mt7615_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7663, 0xff, 0xff, 0xff) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x043e, 0x310c, 0xff, 0xff, 0xff) },
	{ },
};

static u32 mt7663u_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_READ_EXT,
			  USB_DIR_IN | USB_TYPE_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void mt7663u_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static u32 mt7663u_rmw(struct mt76_dev *dev, u32 addr,
		       u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= ___mt76u_rr(dev, MT_VEND_READ_EXT,
			   USB_DIR_IN | USB_TYPE_VENDOR, addr) & ~mask;
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}

static void mt7663u_copy(struct mt76_dev *dev, u32 offset,
			 const void *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	int ret, i = 0, batch_len;
	const u8 *val = data;

	len = round_up(len, 4);

	mutex_lock(&usb->usb_ctrl_mtx);
	while (i < len) {
		batch_len = min_t(int, usb->data_len, len - i);
		memcpy(usb->data, val + i, batch_len);
		ret = __mt76u_vendor_request(dev, MT_VEND_WRITE_EXT,
					     USB_DIR_OUT | USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}

static void mt7663u_stop(struct ieee80211_hw *hw)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);
	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mt76->mac_work);
	mt76u_stop_tx(&dev->mt76);
}

static void mt7663u_cleanup(struct mt7615_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	mt76u_queues_deinit(&dev->mt76);
}

static void mt7663u_init_work(struct work_struct *work)
{
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, mcu_work);
	if (mt7663u_mcu_init(dev))
		return;

	mt7615_init_work(dev);
}

static int mt7663u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_USB_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR | MT_DRV_HW_MGMT_TXQ,
		.tx_prepare_skb = mt7663_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = mt7663_usb_sdio_tx_complete_skb,
		.tx_status_data = mt7663_usb_sdio_tx_status_data,
		.rx_skb = mt7615_queue_rx_skb,
		.rx_check = mt7615_rx_check,
		.sta_add = mt7615_mac_sta_add,
		.sta_remove = mt7615_mac_sta_remove,
		.update_survey = mt7615_update_channel,
	};
	static struct mt76_bus_ops bus_ops = {
		.rr = mt7663u_rr,
		.wr = mt7663u_wr,
		.rmw = mt7663u_rmw,
		.read_copy = mt76u_read_copy,
		.write_copy = mt7663u_copy,
		.type = MT76_BUS_USB,
	};
	struct usb_device *udev = interface_to_usbdev(usb_intf);
	struct ieee80211_ops *ops;
	struct mt7615_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ops = devm_kmemdup(&usb_intf->dev, &mt7615_ops, sizeof(mt7615_ops),
			   GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	ops->stop = mt7663u_stop;

	mdev = mt76_alloc_device(&usb_intf->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7615_dev, mt76);
	udev = usb_get_dev(udev);
	usb_reset_device(udev);

	usb_set_intfdata(usb_intf, dev);

	INIT_WORK(&dev->mcu_work, mt7663u_init_work);
	dev->reg_map = mt7663_usb_sdio_reg_map;
	dev->ops = ops;
	ret = __mt76u_init(mdev, usb_intf, &bus_ops);
	if (ret < 0)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			    FW_STATE_PWR_ON << 1, 500)) {
		ret = mt7663u_mcu_power_on(dev);
		if (ret)
			goto error;
	} else {
		set_bit(MT76_STATE_POWER_OFF, &dev->mphy.state);
	}

	ret = mt76u_alloc_mcu_queue(&dev->mt76);
	if (ret)
		goto error;

	ret = mt76u_alloc_queues(&dev->mt76);
	if (ret)
		goto error;

	ret = mt7663_usb_sdio_register_device(dev);
	if (ret)
		goto error;

	return 0;

error:
	mt76u_queues_deinit(&dev->mt76);
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7663u_disconnect(struct usb_interface *usb_intf)
{
	struct mt7615_dev *dev = usb_get_intfdata(usb_intf);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return;

	ieee80211_unregister_hw(dev->mt76.hw);
	mt7663u_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	mt76_free_device(&dev->mt76);
}

#ifdef CONFIG_PM
static int mt7663u_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct mt7615_dev *dev = usb_get_intfdata(intf);

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
	    mt7615_firmware_offload(dev)) {
		int err;

		err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, true);
		if (err < 0)
			return err;
	}

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	return 0;
}

static int mt7663u_resume(struct usb_interface *intf)
{
	struct mt7615_dev *dev = usb_get_intfdata(intf);
	int err;

	err = mt76u_vendor_request(&dev->mt76, MT_VEND_FEATURE_SET,
				   USB_DIR_OUT | USB_TYPE_VENDOR,
				   0x5, 0x0, NULL, 0);
	if (err)
		return err;

	err = mt76u_resume_rx(&dev->mt76);
	if (err < 0)
		return err;

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
	    mt7615_firmware_offload(dev))
		err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, false);

	return err;
}
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(usb, mt7615_device_table);
MODULE_FIRMWARE(MT7663_OFFLOAD_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_OFFLOAD_ROM_PATCH);
MODULE_FIRMWARE(MT7663_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_ROM_PATCH);

static struct usb_driver mt7663u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7615_device_table,
	.probe		= mt7663u_probe,
	.disconnect	= mt7663u_disconnect,
#ifdef CONFIG_PM
	.suspend	= mt7663u_suspend,
	.resume		= mt7663u_resume,
	.reset_resume	= mt7663u_resume,
#endif /* CONFIG_PM */
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt7663u_driver);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("MediaTek MT7663U (USB) wireless driver");
MODULE_LICENSE("Dual BSD/GPL");
