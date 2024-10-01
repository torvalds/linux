// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt7925.h"
#include "mcu.h"
#include "mac.h"

static const struct usb_device_id mt7925u_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7925, 0xff, 0xff, 0xff),
		.driver_info = (kernel_ulong_t)MT7925_FIRMWARE_WM },
	{ },
};

static int
mt7925u_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			 int cmd, int *seq)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	u32 pad, ep;
	int ret;

	ret = mt7925_mcu_fill_message(mdev, skb, cmd, seq);
	if (ret)
		return ret;

	mdev->mcu.timeout = 3 * HZ;

	if (cmd != MCU_CMD(FW_SCATTER))
		ep = MT_EP_OUT_INBAND_CMD;
	else
		ep = MT_EP_OUT_AC_BE;

	mt792x_skb_add_usb_sdio_hdr(dev, skb, 0);
	pad = round_up(skb->len, 4) + 4 - skb->len;
	__skb_put_zero(skb, pad);

	ret = mt76u_bulk_msg(&dev->mt76, skb->data, skb->len, NULL,
			     1000, ep);
	dev_kfree_skb(skb);

	return ret;
}

static int mt7925u_mcu_init(struct mt792x_dev *dev)
{
	static const struct mt76_mcu_ops mcu_ops = {
		.headroom = MT_SDIO_HDR_SIZE +
			    sizeof(struct mt76_connac2_mcu_txd),
		.tailroom = MT_USB_TAIL_SIZE,
		.mcu_skb_send_msg = mt7925u_mcu_send_message,
		.mcu_parse_response = mt7925_mcu_parse_response,
	};
	int ret;

	dev->mt76.mcu_ops = &mcu_ops;

	mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);
	ret = mt7925_run_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	return 0;
}

static int mt7925u_mac_reset(struct mt792x_dev *dev)
{
	int err;

	mt76_txq_schedule_all(&dev->mphy);
	mt76_worker_disable(&dev->mt76.tx_worker);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);

	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	mt792xu_wfsys_reset(dev);

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	err = mt76u_resume_rx(&dev->mt76);
	if (err)
		goto out;

	err = mt792xu_mcu_power_on(dev);
	if (err)
		goto out;

	err = mt792xu_dma_init(dev, false);
	if (err)
		goto out;

	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
	mt76_set(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = mt7925_run_firmware(dev);
	if (err)
		goto out;

	mt76_clear(dev, MT_UDMA_TX_QSEL, MT_FW_DL_EN);

	err = mt7925_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = mt7925_mac_init(dev);
	if (err)
		goto out;

	err = __mt7925_start(&dev->phy);
out:
	clear_bit(MT76_RESET, &dev->mphy.state);

	mt76_worker_enable(&dev->mt76.tx_worker);

	return err;
}

static int mt7925u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_SDIO_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR | MT_DRV_HW_MGMT_TXQ |
			     MT_DRV_AMSDU_OFFLOAD,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = mt7925_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = mt7925_usb_sdio_tx_complete_skb,
		.tx_status_data = mt7925_usb_sdio_tx_status_data,
		.rx_skb = mt7925_queue_rx_skb,
		.rx_check = mt7925_rx_check,
		.sta_add = mt7925_mac_sta_add,
		.sta_event = mt7925_mac_sta_event,
		.sta_remove = mt7925_mac_sta_remove,
		.update_survey = mt792x_update_channel,
	};
	static const struct mt792x_hif_ops hif_ops = {
		.mcu_init = mt7925u_mcu_init,
		.init_reset = mt792xu_init_reset,
		.reset = mt7925u_mac_reset,
	};
	static struct mt76_bus_ops bus_ops = {
		.rr = mt792xu_rr,
		.wr = mt792xu_wr,
		.rmw = mt792xu_rmw,
		.read_copy = mt76u_read_copy,
		.write_copy = mt792xu_copy,
		.type = MT76_BUS_USB,
	};
	struct usb_device *udev = interface_to_usbdev(usb_intf);
	struct ieee80211_ops *ops;
	struct ieee80211_hw *hw;
	struct mt792x_dev *dev;
	struct mt76_dev *mdev;
	u8 features;
	int ret;

	ops = mt792x_get_mac80211_ops(&usb_intf->dev, &mt7925_ops,
				      (void *)id->driver_info, &features);
	if (!ops)
		return -ENOMEM;

	ops->stop = mt792xu_stop;

	mdev = mt76_alloc_device(&usb_intf->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt792x_dev, mt76);
	dev->fw_features = features;
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
		ret = mt792xu_wfsys_reset(dev);
		if (ret)
			goto error;
	}

	ret = mt792xu_mcu_power_on(dev);
	if (ret)
		goto error;

	ret = mt76u_alloc_mcu_queue(&dev->mt76);
	if (ret)
		goto error;

	ret = mt76u_alloc_queues(&dev->mt76);
	if (ret)
		goto error;

	ret = mt792xu_dma_init(dev, false);
	if (ret)
		goto error;

	hw = mt76_hw(dev);
	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = mdev->usb.sg_en ? MT_HW_TXP_MAX_BUF_NUM : 1;

	ret = mt7925_register_device(dev);
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

#ifdef CONFIG_PM
static int mt7925u_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct mt792x_dev *dev = usb_get_intfdata(intf);
	struct mt76_connac_pm *pm = &dev->pm;
	int err;

	pm->suspended = true;
	flush_work(&dev->reset_work);

	err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, true);
	if (err)
		goto failed;

	mt76u_stop_rx(&dev->mt76);
	mt76u_stop_tx(&dev->mt76);

	return 0;

failed:
	pm->suspended = false;

	if (err < 0)
		mt792x_reset(&dev->mt76);

	return err;
}

static int mt7925u_resume(struct usb_interface *intf)
{
	struct mt792x_dev *dev = usb_get_intfdata(intf);
	struct mt76_connac_pm *pm = &dev->pm;
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

	if (reinit || mt792x_dma_need_reinit(dev)) {
		err = mt792xu_dma_init(dev, true);
		if (err)
			goto failed;
	}

	err = mt76u_resume_rx(&dev->mt76);
	if (err < 0)
		goto failed;

	err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, false);
failed:
	pm->suspended = false;

	if (err < 0)
		mt792x_reset(&dev->mt76);

	return err;
}
#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(usb, mt7925u_device_table);
MODULE_FIRMWARE(MT7925_FIRMWARE_WM);
MODULE_FIRMWARE(MT7925_ROM_PATCH);

static struct usb_driver mt7925u_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7925u_device_table,
	.probe		= mt7925u_probe,
	.disconnect	= mt792xu_disconnect,
#ifdef CONFIG_PM
	.suspend	= mt7925u_suspend,
	.resume		= mt7925u_resume,
	.reset_resume	= mt7925u_resume,
#endif /* CONFIG_PM */
	.soft_unbind	= 1,
	.disable_hub_initiated_lpm = 1,
};
module_usb_driver(mt7925u_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("MediaTek MT7925U (USB) wireless driver");
MODULE_LICENSE("Dual BSD/GPL");
