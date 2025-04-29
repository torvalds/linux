// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "mt76x0.h"
#include "mcu.h"
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
	{ USB_DEVICE(0x2357, 0x0123) }, /* TP-LINK T2UHP_US_v1 */
	{ USB_DEVICE(0x2357, 0x010b) }, /* TP-LINK T2UHP_UN_v1 */
	/* TP-LINK Archer T1U */
	{ USB_DEVICE(0x2357, 0x0105), .driver_info = 1, },
	/* MT7630U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7630, 0xff, 0x2, 0xff)},
	/* MT7650U */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0E8D, 0x7650, 0xff, 0x2, 0xff)},
	{ 0, }
};

static void mt76x0_init_usb_dma(struct mt76x02_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_USB_DMA_CFG);

	val |= MT_USB_DMA_CFG_RX_BULK_EN |
	       MT_USB_DMA_CFG_TX_BULK_EN;

	/* disable AGGR_BULK_RX in order to receive one
	 * frame in each rx urb and avoid copies
	 */
	val &= ~MT_USB_DMA_CFG_RX_BULK_AGG_EN;
	mt76_wr(dev, MT_USB_DMA_CFG, val);

	val = mt76_rr(dev, MT_COM_REG0);
	if (val & 1)
		dev_dbg(dev->mt76.dev, "MCU not ready\n");

	val = mt76_rr(dev, MT_USB_DMA_CFG);

	val |= MT_USB_DMA_CFG_RX_DROP_OR_PAD;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
	val &= ~MT_USB_DMA_CFG_RX_DROP_OR_PAD;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
}

static void mt76x0u_cleanup(struct mt76x02_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	mt76x0_chip_onoff(dev, false, false);
	mt76u_queues_deinit(&dev->mt76);
}

static void mt76x0u_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt76x02_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	mt76u_stop_tx(&dev->mt76);
	mt76x02u_exit_beacon_config(dev);

	if (test_bit(MT76_REMOVED, &dev->mphy.state))
		return;

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_TX_BUSY, 0, 1000))
		dev_warn(dev->mt76.dev, "TX DMA did not stop\n");

	mt76x0_mac_stop(dev);

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_RX_BUSY, 0, 1000))
		dev_warn(dev->mt76.dev, "RX DMA did not stop\n");
}

static int mt76x0u_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret;

	ret = mt76x02u_mac_start(dev);
	if (ret)
		return ret;

	mt76x0_phy_calibrate(dev, true);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->mphy.mac_work,
				     MT_MAC_WORK_INTERVAL);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
	set_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	return 0;
}

static const struct ieee80211_ops mt76x0u_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx = mt76x02_tx,
	.start = mt76x0u_start,
	.stop = mt76x0u_stop,
	.add_interface = mt76x02_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.config = mt76x0_config,
	.configure_filter = mt76x02_configure_filter,
	.bss_info_changed = mt76x02_bss_info_changed,
	.sta_state = mt76_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt76x02_set_key,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76x02_sw_scan_complete,
	.ampdu_action = mt76x02_ampdu_action,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
	.set_rts_threshold = mt76x02_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.get_txpower = mt76_get_txpower,
	.get_survey = mt76_get_survey,
	.set_tim = mt76_set_tim,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_antenna = mt76_get_antenna,
	.set_sar_specs = mt76x0_set_sar_specs,
};

static int mt76x0u_init_hardware(struct mt76x02_dev *dev, bool reset)
{
	int err;

	mt76x0_chip_onoff(dev, true, reset);

	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	err = mt76x0u_mcu_init(dev);
	if (err < 0)
		return err;

	mt76x0_init_usb_dma(dev);
	err = mt76x0_init_hardware(dev);
	if (err < 0)
		return err;

	mt76x02u_init_beacon_config(dev);

	mt76_rmw(dev, MT_US_CYC_CFG, MT_US_CYC_CNT, 0x1e);
	mt76_wr(dev, MT_TXOP_CTRL_CFG,
		FIELD_PREP(MT_TXOP_TRUN_EN, 0x3f) |
		FIELD_PREP(MT_TXOP_EXT_CCA_DLY, 0x58));

	return 0;
}

static int mt76x0u_register_device(struct mt76x02_dev *dev)
{
	struct ieee80211_hw *hw = dev->mt76.hw;
	struct mt76_usb *usb = &dev->mt76.usb;
	int err;

	usb->mcu.data = devm_kmalloc(dev->mt76.dev, MCU_RESP_URB_SIZE,
				     GFP_KERNEL);
	if (!usb->mcu.data)
		return -ENOMEM;

	err = mt76u_alloc_queues(&dev->mt76);
	if (err < 0)
		goto out_err;

	err = mt76x0u_init_hardware(dev, true);
	if (err < 0)
		goto out_err;

	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = dev->mt76.usb.sg_en ? MT_TX_SG_MAX_SIZE : 1;
	err = mt76x0_register_device(dev);
	if (err < 0)
		goto out_err;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	return 0;

out_err:
	mt76x0u_cleanup(dev);
	return err;
}

static int mt76x0u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.drv_flags = MT_DRV_SW_RX_AIRTIME |
			     MT_DRV_IGNORE_TXS_FAILED,
		.survey_flags = SURVEY_INFO_TIME_TX,
		.update_survey = mt76x02_update_channel,
		.set_channel = mt76x0_set_channel,
		.tx_prepare_skb = mt76x02u_tx_prepare_skb,
		.tx_complete_skb = mt76x02u_tx_complete_skb,
		.tx_status_data = mt76x02_tx_status_data,
		.rx_skb = mt76x02_queue_rx_skb,
		.sta_ps = mt76x02_sta_ps,
		.sta_add = mt76x02_sta_add,
		.sta_remove = mt76x02_sta_remove,
	};
	struct usb_device *usb_dev = interface_to_usbdev(usb_intf);
	struct mt76x02_dev *dev;
	struct mt76_dev *mdev;
	u32 mac_rev;
	int ret;

	mdev = mt76_alloc_device(&usb_intf->dev, sizeof(*dev), &mt76x0u_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt76x02_dev, mt76);
	mutex_init(&dev->phy_mutex);

	/* Quirk for Archer T1U */
	if (id->driver_info)
		dev->no_2ghz = true;

	usb_dev = usb_get_dev(usb_dev);
	usb_reset_device(usb_dev);

	usb_set_intfdata(usb_intf, dev);

	mt76x02u_init_mcu(mdev);
	ret = mt76u_init(mdev, usb_intf);
	if (ret)
		goto err;

	/* Disable the HW, otherwise MCU fail to initialize on hot reboot */
	mt76x0_chip_onoff(dev, false, false);

	if (!mt76x02_wait_for_mac(mdev)) {
		ret = -ETIMEDOUT;
		goto err;
	}

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	mac_rev = mt76_rr(dev, MT_MAC_CSR0);
	dev_info(mdev->dev, "ASIC revision: %08x MAC revision: %08x\n",
		 mdev->rev, mac_rev);
	if (!is_mt76x0(dev)) {
		ret = -ENODEV;
		goto err;
	}

	/* Note: vendor driver skips this check for MT76X0U */
	if (!(mt76_rr(dev, MT_EFUSE_CTRL) & MT_EFUSE_CTRL_SEL))
		dev_warn(mdev->dev, "Warning: eFUSE not present\n");

	ret = mt76x0u_register_device(dev);
	if (ret < 0)
		goto err;

	return 0;

err:
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));
	mt76u_queues_deinit(&dev->mt76);
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt76x0_disconnect(struct usb_interface *usb_intf)
{
	struct mt76x02_dev *dev = usb_get_intfdata(usb_intf);
	bool initialized = test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	if (!initialized)
		return;

	ieee80211_unregister_hw(dev->mt76.hw);
	mt76x0u_cleanup(dev);

	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	mt76_free_device(&dev->mt76);
}

static int __maybe_unused mt76x0_suspend(struct usb_interface *usb_intf,
					 pm_message_t state)
{
	struct mt76x02_dev *dev = usb_get_intfdata(usb_intf);

	mt76u_stop_rx(&dev->mt76);
	clear_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	mt76x0_chip_onoff(dev, false, false);

	return 0;
}

static int __maybe_unused mt76x0_resume(struct usb_interface *usb_intf)
{
	struct mt76x02_dev *dev = usb_get_intfdata(usb_intf);
	int ret;

	ret = mt76u_resume_rx(&dev->mt76);
	if (ret < 0)
		goto err;

	ret = mt76x0u_init_hardware(dev, false);
	if (ret)
		goto err;

	return 0;
err:
	mt76x0u_cleanup(dev);
	return ret;
}

MODULE_DEVICE_TABLE(usb, mt76x0_device_table);
MODULE_FIRMWARE(MT7610E_FIRMWARE);
MODULE_FIRMWARE(MT7610U_FIRMWARE);
MODULE_DESCRIPTION("MediaTek MT76x0U (USB) wireless driver");
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
