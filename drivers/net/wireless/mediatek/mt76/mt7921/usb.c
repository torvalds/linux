// SPDX-License-Identifier: ISC
/* Copyright (C) 2022 MediaTek Inc.
 *
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt7921.h"
#include "mcu.h"
#include "mac.h"

static const struct usb_device_id mt7921u_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7961, 0xff, 0xff, 0xff) },
	{ },
};

static u32 mt7921u_rr(struct mt76_dev *dev, u32 addr)
{
	u32 ret;

	mutex_lock(&dev->usb.usb_ctrl_mtx);
	ret = ___mt76u_rr(dev, MT_VEND_READ_EXT,
			  USB_DIR_IN | MT_USB_TYPE_VENDOR, addr);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return ret;
}

static void mt7921u_wr(struct mt76_dev *dev, u32 addr, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);
}

static u32 mt7921u_rmw(struct mt76_dev *dev, u32 addr,
		       u32 mask, u32 val)
{
	mutex_lock(&dev->usb.usb_ctrl_mtx);
	val |= ___mt76u_rr(dev, MT_VEND_READ_EXT,
			   USB_DIR_IN | MT_USB_TYPE_VENDOR, addr) & ~mask;
	___mt76u_wr(dev, MT_VEND_WRITE_EXT,
		    USB_DIR_OUT | MT_USB_TYPE_VENDOR, addr, val);
	mutex_unlock(&dev->usb.usb_ctrl_mtx);

	return val;
}

static void mt7921u_copy(struct mt76_dev *dev, u32 offset,
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
					     USB_DIR_OUT | MT_USB_TYPE_VENDOR,
					     (offset + i) >> 16, offset + i,
					     usb->data, batch_len);
		if (ret < 0)
			break;

		i += batch_len;
	}
	mutex_unlock(&usb->usb_ctrl_mtx);
}

int mt7921u_mcu_power_on(struct mt7921_dev *dev)
{
	int ret;

	ret = mt76u_vendor_request(&dev->mt76, MT_VEND_POWER_ON,
				   USB_DIR_OUT | MT_USB_TYPE_VENDOR,
				   0x0, 0x1, NULL, 0);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			    MT_TOP_MISC2_FW_PWR_ON, 500)) {
		dev_err(dev->mt76.dev, "Timeout for power on\n");
		ret = -EIO;
	}

	return ret;
}

static int
mt7921u_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			 int cmd, int *seq)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	u32 pad, ep;
	int ret;

	ret = mt76_connac2_mcu_fill_message(mdev, skb, cmd, seq);
	if (ret)
		return ret;

	if (cmd == MCU_UNI_CMD(HIF_CTRL) ||
	    cmd == MCU_UNI_CMD(SUSPEND) ||
	    cmd == MCU_UNI_CMD(OFFLOAD))
		mdev->mcu.timeout = HZ;
	else
		mdev->mcu.timeout = 3 * HZ;

	if (cmd != MCU_CMD(FW_SCATTER))
		ep = MT_EP_OUT_INBAND_CMD;
	else
		ep = MT_EP_OUT_AC_BE;

	mt7921_skb_add_usb_sdio_hdr(dev, skb, 0);
	pad = round_up(skb->len, 4) + 4 - skb->len;
	__skb_put_zero(skb, pad);

	ret = mt76u_bulk_msg(&dev->mt76, skb->data, skb->len, NULL,
			     1000, ep);
	dev_kfree_skb(skb);

	return ret;
}

static int mt7921u_mcu_init(struct mt7921_dev *dev)
{
	static const struct mt76_mcu_ops mcu_ops = {
		.headroom = MT_SDIO_HDR_SIZE +
			    sizeof(struct mt76_connac2_mcu_txd),
		.tailroom = MT_USB_TAIL_SIZE,
		.mcu_skb_send_msg = mt7921u_mcu_send_message,
		.mcu_parse_response = mt7921_mcu_parse_response,
		.mcu_restart = mt76_connac_mcu_restart,
	};
	int ret;

	dev->mt76.mcu_ops = &mcu_ops;

	mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);
	ret = mt7921_run_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	return 0;
}

static void mt7921u_stop(struct ieee80211_hw *hw)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mt76u_stop_tx(&dev->mt76);
	mt7921_stop(hw);
}

static void mt7921u_cleanup(struct mt7921_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	mt7921u_wfsys_reset(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);
	mt76u_queues_deinit(&dev->mt76);
}

static int mt7921u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_SDIO_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR | MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = mt7921_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = mt7921_usb_sdio_tx_complete_skb,
		.tx_status_data = mt7921_usb_sdio_tx_status_data,
		.rx_skb = mt7921_queue_rx_skb,
		.sta_ps = mt7921_sta_ps,
		.sta_add = mt7921_mac_sta_add,
		.sta_assoc = mt7921_mac_sta_assoc,
		.sta_remove = mt7921_mac_sta_remove,
		.update_survey = mt7921_update_channel,
	};
	static const struct mt7921_hif_ops hif_ops = {
		.mcu_init = mt7921u_mcu_init,
		.init_reset = mt7921u_init_reset,
		.reset = mt7921u_mac_reset,
	};
	static struct mt76_bus_ops bus_ops = {
		.rr = mt7921u_rr,
		.wr = mt7921u_wr,
		.rmw = mt7921u_rmw,
		.read_copy = mt76u_read_copy,
		.write_copy = mt7921u_copy,
		.type = MT76_BUS_USB,
	};
	struct usb_device *udev = interface_to_usbdev(usb_intf);
	struct ieee80211_ops *ops;
	struct ieee80211_hw *hw;
	struct mt7921_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ops = devm_kmemdup(&usb_intf->dev, &mt7921_ops, sizeof(mt7921_ops),
			   GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	ops->stop = mt7921u_stop;

	mdev = mt76_alloc_device(&usb_intf->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7921_dev, mt76);
	dev->hif_ops = &hif_ops;

	udev = usb_get_dev(udev);
	usb_reset_device(udev);

	usb_set_intfdata(usb_intf, dev);

	ret = __mt76u_init(mdev, usb_intf, &bus_ops);
	if (ret < 0)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	if (mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY)) {
		ret = mt7921u_wfsys_reset(dev);
		if (ret)
			goto error;
	}

	ret = mt7921u_mcu_power_on(dev);
	if (ret)
		goto error;

	ret = mt76u_alloc_mcu_queue(&dev->mt76);
	if (ret)
		goto error;

	ret = mt76u_alloc_queues(&dev->mt76);
	if (ret)
		goto error;

	ret = mt7921u_dma_init(dev, false);
	if (ret)
		return ret;

	hw = mt76_hw(dev);
	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = mdev->usb.sg_en ? MT_HW_TXP_MAX_BUF_NUM : 1;

	ret = mt7921_register_device(dev);
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

static void mt7921u_disconnect(struct usb_interface *usb_intf)
{
	struct mt7921_dev *dev = usb_get_intfdata(usb_intf);

	cancel_work_sync(&dev->init_work);
	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return;

	mt76_unregister_device(&dev->mt76);
	mt7921u_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	mt76_free_device(&dev->mt76);
}

#ifdef CONFIG_PM
static int mt7921u_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct mt7921_dev *dev = usb_get_intfdata(intf);
	int err;

	err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, true);
	if (err)
		return err;

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	set_bit(MT76_STATE_SUSPEND, &dev->mphy.state);

	return 0;
}

static int mt7921u_resume(struct usb_interface *intf)
{
	struct mt7921_dev *dev = usb_get_intfdata(intf);
	bool reinit = true;
	int err, i;

	for (i = 0; i < 10; i++) {
		u32 val = mt76_rr(dev, MT_WF_SW_DEF_CR_USB_MCU_EVENT);

		if (!(val & MT_WF_SW_SER_TRIGGER_SUSPEND)) {
			reinit = false;
			break;
		}
		if (val & MT_WF_SW_SER_DONE_SUSPEND) {
			mt76_wr(dev, MT_WF_SW_DEF_CR_USB_MCU_EVENT, 0);
			break;
		}

		msleep(20);
	}

	if (reinit || mt7921_dma_need_reinit(dev)) {
		err = mt7921u_dma_init(dev, true);
		if (err)
			return err;
	}

	clear_bit(MT76_STATE_SUSPEND, &dev->mphy.state);

	err = mt76u_resume_rx(&dev->mt76);
	if (err < 0)
		return err;

	return mt76_connac_mcu_set_hif_suspend(&dev->mt76, false);
}
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(usb, mt7921u_device_table);
MODULE_FIRMWARE(MT7921_FIRMWARE_WM);
MODULE_FIRMWARE(MT7921_ROM_PATCH);

static struct usb_driver mt7921u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7921u_device_table,
	.probe		= mt7921u_probe,
	.disconnect	= mt7921u_disconnect,
#ifdef CONFIG_PM
	.suspend	= mt7921u_suspend,
	.resume		= mt7921u_resume,
	.reset_resume	= mt7921u_resume,
#endif /* CONFIG_PM */
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt7921u_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
