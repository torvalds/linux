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

static const u32 mt7663u_reg_map[] = {
	[MT_TOP_CFG_BASE]	= 0x80020000,
	[MT_HW_BASE]		= 0x80000000,
	[MT_DMA_SHDL_BASE]	= 0x5000a000,
	[MT_HIF_BASE]		= 0x50000000,
	[MT_CSR_BASE]		= 0x40000000,
	[MT_EFUSE_ADDR_BASE]	= 0x78011000,
	[MT_TOP_MISC_BASE]	= 0x81020000,
	[MT_PLE_BASE]		= 0x82060000,
	[MT_PSE_BASE]		= 0x82068000,
	[MT_PHY_BASE]		= 0x82070000,
	[MT_WTBL_BASE_ADDR]	= 0x820e0000,
	[MT_CFG_BASE]		= 0x820f0000,
	[MT_AGG_BASE]		= 0x820f2000,
	[MT_ARB_BASE]		= 0x820f3000,
	[MT_TMAC_BASE]		= 0x820f4000,
	[MT_RMAC_BASE]		= 0x820f5000,
	[MT_DMA_BASE]		= 0x820f7000,
	[MT_PF_BASE]		= 0x820f8000,
	[MT_WTBL_BASE_ON]	= 0x820f9000,
	[MT_WTBL_BASE_OFF]	= 0x820f9800,
	[MT_LPON_BASE]		= 0x820fb000,
	[MT_MIB_BASE]		= 0x820fd000,
};

static const struct usb_device_id mt7615_device_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e8d, 0x7663, 0xff, 0xff, 0xff) },
	{ },
};

static void mt7663u_stop(struct ieee80211_hw *hw)
{
	struct mt7615_phy *phy = mt7615_hw_phy(hw);
	struct mt7615_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);
	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mac_work);
	mt76u_stop_tx(&dev->mt76);
}

static void mt7663u_cleanup(struct mt7615_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	mt76u_queues_deinit(&dev->mt76);
}

static void
mt7663u_mac_write_txwi(struct mt7615_dev *dev, struct mt76_wcid *wcid,
		       enum mt76_txq_id qid, struct ieee80211_sta *sta,
		       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	__le32 *txwi;
	int pid;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	pid = mt76_tx_status_skb_add(&dev->mt76, wcid, skb);

	txwi = (__le32 *)(skb->data - MT_USB_TXD_SIZE);
	memset(txwi, 0, MT_USB_TXD_SIZE);
	mt7615_mac_write_txwi(dev, txwi, skb, wcid, sta, pid, key, false);
	skb_push(skb, MT_USB_TXD_SIZE);
}

static int
__mt7663u_mac_set_rates(struct mt7615_dev *dev,
			struct mt7615_wtbl_desc *wd)
{
	struct mt7615_rate_desc *rate = &wd->rate;
	struct mt7615_sta *sta = wd->sta;
	u32 w5, w27, addr, val;

	lockdep_assert_held(&dev->mt76.mutex);

	if (!sta)
		return -EINVAL;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return -ETIMEDOUT;

	addr = mt7615_mac_wtbl_addr(dev, sta->wcid.idx);

	w27 = mt76_rr(dev, addr + 27 * 4);
	w27 &= ~MT_WTBL_W27_CC_BW_SEL;
	w27 |= FIELD_PREP(MT_WTBL_W27_CC_BW_SEL, rate->bw);

	w5 = mt76_rr(dev, addr + 5 * 4);
	w5 &= ~(MT_WTBL_W5_BW_CAP | MT_WTBL_W5_CHANGE_BW_RATE |
		MT_WTBL_W5_MPDU_OK_COUNT |
		MT_WTBL_W5_MPDU_FAIL_COUNT |
		MT_WTBL_W5_RATE_IDX);
	w5 |= FIELD_PREP(MT_WTBL_W5_BW_CAP, rate->bw) |
	      FIELD_PREP(MT_WTBL_W5_CHANGE_BW_RATE,
			 rate->bw_idx ? rate->bw_idx - 1 : 7);

	mt76_wr(dev, MT_WTBL_RIUCR0, w5);

	mt76_wr(dev, MT_WTBL_RIUCR1,
		FIELD_PREP(MT_WTBL_RIUCR1_RATE0, rate->probe_val) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE1, rate->val[0]) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE2_LO, rate->val[1]));

	mt76_wr(dev, MT_WTBL_RIUCR2,
		FIELD_PREP(MT_WTBL_RIUCR2_RATE2_HI, rate->val[1] >> 8) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE3, rate->val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE4, rate->val[2]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE5_LO, rate->val[2]));

	mt76_wr(dev, MT_WTBL_RIUCR3,
		FIELD_PREP(MT_WTBL_RIUCR3_RATE5_HI, rate->val[2] >> 4) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE6, rate->val[3]) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE7, rate->val[3]));

	mt76_wr(dev, MT_WTBL_UPDATE,
		FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, sta->wcid.idx) |
		MT_WTBL_UPDATE_RATE_UPDATE |
		MT_WTBL_UPDATE_TX_COUNT_CLEAR);

	mt76_wr(dev, addr + 27 * 4, w27);

	mt76_set(dev, MT_LPON_T0CR, MT_LPON_T0CR_MODE); /* TSF read */
	val = mt76_rr(dev, MT_LPON_UTTR0);
	sta->rate_set_tsf = (val & ~BIT(0)) | rate->rateset;

	if (!(sta->wcid.tx_info & MT_WCID_TX_INFO_SET))
		mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	sta->rate_count = 2 * MT7615_RATE_RETRY * sta->n_rates;
	sta->wcid.tx_info |= MT_WCID_TX_INFO_SET;

	return 0;
}

static int
__mt7663u_mac_set_key(struct mt7615_dev *dev,
		      struct mt7615_wtbl_desc *wd)
{
	struct mt7615_key_desc *key = &wd->key;
	struct mt7615_sta *sta = wd->sta;
	enum mt7615_cipher_type cipher;
	struct mt76_wcid *wcid;
	int err;

	lockdep_assert_held(&dev->mt76.mutex);

	if (!sta)
		return -EINVAL;

	cipher = mt7615_mac_get_cipher(key->cipher);
	if (cipher == MT_CIPHER_NONE)
		return -EOPNOTSUPP;

	wcid = &wd->sta->wcid;

	mt7615_mac_wtbl_update_cipher(dev, wcid, cipher, key->cmd);
	err = mt7615_mac_wtbl_update_key(dev, wcid, key->key, key->keylen,
					 cipher, key->cmd);
	if (err < 0)
		return err;

	err = mt7615_mac_wtbl_update_pk(dev, wcid, cipher, key->keyidx,
					key->cmd);
	if (err < 0)
		return err;

	if (key->cmd == SET_KEY)
		wcid->cipher |= BIT(cipher);
	else
		wcid->cipher &= ~BIT(cipher);

	return 0;
}

void mt7663u_wtbl_work(struct work_struct *work)
{
	struct mt7615_wtbl_desc *wd, *wd_next;
	struct mt7615_dev *dev;

	dev = (struct mt7615_dev *)container_of(work, struct mt7615_dev,
						wtbl_work);

	list_for_each_entry_safe(wd, wd_next, &dev->wd_head, node) {
		spin_lock_bh(&dev->mt76.lock);
		list_del(&wd->node);
		spin_unlock_bh(&dev->mt76.lock);

		mutex_lock(&dev->mt76.mutex);
		switch (wd->type) {
		case MT7615_WTBL_RATE_DESC:
			__mt7663u_mac_set_rates(dev, wd);
			break;
		case MT7615_WTBL_KEY_DESC:
			__mt7663u_mac_set_key(dev, wd);
			break;
		}
		mutex_unlock(&dev->mt76.mutex);

		kfree(wd);
	}
}

static void
mt7663u_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			struct mt76_queue_entry *e)
{
	skb_pull(e->skb, MT_USB_HDR_SIZE + MT_USB_TXD_SIZE);
	mt76_tx_complete_skb(mdev, e->skb);
}

static int
mt7663u_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
		       enum mt76_txq_id qid, struct mt76_wcid *wcid,
		       struct ieee80211_sta *sta,
		       struct mt76_tx_info *tx_info)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
		struct mt7615_sta *msta;

		msta = container_of(wcid, struct mt7615_sta, wcid);
		spin_lock_bh(&dev->mt76.lock);
		mt7615_mac_set_rates(&dev->phy, msta, &info->control.rates[0],
				     msta->rates);
		msta->rate_probe = true;
		spin_unlock_bh(&dev->mt76.lock);
	}
	mt7663u_mac_write_txwi(dev, wcid, qid, sta, tx_info->skb);

	return mt76u_skb_dma_info(tx_info->skb, tx_info->skb->len);
}

static bool mt7663u_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mutex_lock(&dev->mt76.mutex);
	mt7615_mac_sta_poll(dev);
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int mt7663u_probe(struct usb_interface *usb_intf,
			 const struct usb_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_USB_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR | MT_DRV_HW_MGMT_TXQ,
		.tx_prepare_skb = mt7663u_tx_prepare_skb,
		.tx_complete_skb = mt7663u_tx_complete_skb,
		.tx_status_data = mt7663u_tx_status_data,
		.rx_skb = mt7615_queue_rx_skb,
		.sta_ps = mt7615_sta_ps,
		.sta_add = mt7615_mac_sta_add,
		.sta_remove = mt7615_mac_sta_remove,
		.update_survey = mt7615_update_channel,
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

	dev->reg_map = mt7663u_reg_map;
	dev->ops = ops;
	ret = mt76u_init(mdev, usb_intf, true);
	if (ret < 0)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	if (mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			   FW_STATE_PWR_ON << 1, 500)) {
		dev_dbg(dev->mt76.dev, "Usb device already powered on\n");
		set_bit(MT76_STATE_POWER_OFF, &dev->mphy.state);
		goto alloc_queues;
	}

	ret = mt76u_vendor_request(&dev->mt76, MT_VEND_POWER_ON,
				   USB_DIR_OUT | USB_TYPE_VENDOR,
				   0x0, 0x1, NULL, 0);
	if (ret)
		goto error;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_PWR_ON,
			    FW_STATE_PWR_ON << 1, 500)) {
		dev_err(dev->mt76.dev, "Timeout for power on\n");
		ret = -EIO;
		goto error;
	}

alloc_queues:
	ret = mt76u_alloc_mcu_queue(&dev->mt76);
	if (ret)
		goto error_free_q;

	ret = mt76u_alloc_queues(&dev->mt76);
	if (ret)
		goto error_free_q;

	ret = mt7663u_register_device(dev);
	if (ret)
		goto error_free_q;

	return 0;

error_free_q:
	mt76u_queues_deinit(&dev->mt76);
error:
	mt76u_deinit(&dev->mt76);
	usb_set_intfdata(usb_intf, NULL);
	usb_put_dev(interface_to_usbdev(usb_intf));

	ieee80211_free_hw(mdev->hw);

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

	mt76u_deinit(&dev->mt76);
	ieee80211_free_hw(dev->mt76.hw);
}

#ifdef CONFIG_PM
static int mt7663u_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct mt7615_dev *dev = usb_get_intfdata(intf);

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
	    mt7615_firmware_offload(dev)) {
		int err;

		err = mt7615_mcu_set_hif_suspend(dev, true);
		if (err < 0)
			return err;
	}

	mt76u_stop_rx(&dev->mt76);

	mt76u_stop_tx(&dev->mt76);
	tasklet_kill(&dev->mt76.tx_tasklet);

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
		err = mt7615_mcu_set_hif_suspend(dev, false);

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
MODULE_LICENSE("Dual BSD/GPL");
