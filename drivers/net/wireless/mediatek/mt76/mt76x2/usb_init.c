// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/delay.h>

#include "mt76x2u.h"
#include "eeprom.h"
#include "../mt76x02_phy.h"
#include "../mt76x02_usb.h"

static void mt76x2u_init_dma(struct mt76x02_dev *dev)
{
	u32 val = mt76_rr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG));

	val |= MT_USB_DMA_CFG_RX_DROP_OR_PAD |
	       MT_USB_DMA_CFG_RX_BULK_EN |
	       MT_USB_DMA_CFG_TX_BULK_EN;

	/* disable AGGR_BULK_RX in order to receive one
	 * frame in each rx urb and avoid copies
	 */
	val &= ~MT_USB_DMA_CFG_RX_BULK_AGG_EN;
	mt76_wr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG), val);
}

static void mt76x2u_power_on_rf_patch(struct mt76x02_dev *dev)
{
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x130), BIT(0) | BIT(16));
	udelay(1);

	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x1c), 0xff);
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x1c), 0x30);

	mt76_wr(dev, MT_VEND_ADDR(CFG, 0x14), 0x484f);
	udelay(1);

	mt76_set(dev, MT_VEND_ADDR(CFG, 0x130), BIT(17));
	usleep_range(150, 200);

	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x130), BIT(16));
	usleep_range(50, 100);

	mt76_set(dev, MT_VEND_ADDR(CFG, 0x14c), BIT(19) | BIT(20));
}

static void mt76x2u_power_on_rf(struct mt76x02_dev *dev, int unit)
{
	int shift = unit ? 8 : 0;
	u32 val = (BIT(1) | BIT(3) | BIT(4) | BIT(5)) << shift;

	/* Enable RF BG */
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x130), BIT(0) << shift);
	usleep_range(10, 20);

	/* Enable RFDIG LDO/AFE/ABB/ADDA */
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x130), val);
	usleep_range(10, 20);

	/* Switch RFDIG power to internal LDO */
	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x130), BIT(2) << shift);
	usleep_range(10, 20);

	mt76x2u_power_on_rf_patch(dev);

	mt76_set(dev, 0x530, 0xf);
}

static void mt76x2u_power_on(struct mt76x02_dev *dev)
{
	u32 val;

	/* Turn on WL MTCMOS */
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x148),
		 MT_WLAN_MTC_CTRL_MTCMOS_PWR_UP);

	val = MT_WLAN_MTC_CTRL_STATE_UP |
	      MT_WLAN_MTC_CTRL_PWR_ACK |
	      MT_WLAN_MTC_CTRL_PWR_ACK_S;

	mt76_poll(dev, MT_VEND_ADDR(CFG, 0x148), val, val, 1000);

	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x148), 0x7f << 16);
	usleep_range(10, 20);

	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x148), 0xf << 24);
	usleep_range(10, 20);

	mt76_set(dev, MT_VEND_ADDR(CFG, 0x148), 0xf << 24);
	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x148), 0xfff);

	/* Turn on AD/DA power down */
	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x1204), BIT(3));

	/* WLAN function enable */
	mt76_set(dev, MT_VEND_ADDR(CFG, 0x80), BIT(0));

	/* Release BBP software reset */
	mt76_clear(dev, MT_VEND_ADDR(CFG, 0x64), BIT(18));

	mt76x2u_power_on_rf(dev, 0);
	mt76x2u_power_on_rf(dev, 1);
}

static int mt76x2u_init_eeprom(struct mt76x02_dev *dev)
{
	u32 val, i;

	dev->mt76.eeprom.data = devm_kzalloc(dev->mt76.dev,
					     MT7612U_EEPROM_SIZE,
					     GFP_KERNEL);
	dev->mt76.eeprom.size = MT7612U_EEPROM_SIZE;
	if (!dev->mt76.eeprom.data)
		return -ENOMEM;

	for (i = 0; i + 4 <= MT7612U_EEPROM_SIZE; i += 4) {
		val = mt76_rr(dev, MT_VEND_ADDR(EEPROM, i));
		put_unaligned_le32(val, dev->mt76.eeprom.data + i);
	}

	mt76x02_eeprom_parse_hw_cap(dev);
	return 0;
}

int mt76x2u_init_hardware(struct mt76x02_dev *dev)
{
	int i, k, err;

	mt76x2_reset_wlan(dev, true);
	mt76x2u_power_on(dev);

	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	err = mt76x2u_mcu_fw_init(dev);
	if (err < 0)
		return err;

	if (!mt76_poll_msec(dev, MT_WPDMA_GLO_CFG,
			    MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			    MT_WPDMA_GLO_CFG_RX_DMA_BUSY, 0, 100))
		return -EIO;

	/* wait for asic ready after fw load. */
	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	mt76x2u_init_dma(dev);

	err = mt76x2u_mcu_init(dev);
	if (err < 0)
		return err;

	err = mt76x2u_mac_reset(dev);
	if (err < 0)
		return err;

	mt76x02_mac_setaddr(dev, dev->mt76.eeprom.data + MT_EE_MAC_ADDR);
	dev->mt76.rxfilter = mt76_rr(dev, MT_RX_FILTR_CFG);

	if (!mt76x02_wait_for_txrx_idle(&dev->mt76))
		return -ETIMEDOUT;

	/* reset wcid table */
	for (i = 0; i < 256; i++)
		mt76x02_mac_wcid_setup(dev, i, 0, NULL);

	/* reset shared key table and pairwise key table */
	for (i = 0; i < 16; i++) {
		for (k = 0; k < 4; k++)
			mt76x02_mac_shared_key_setup(dev, i, k, NULL);
	}

	mt76x02u_init_beacon_config(dev);

	mt76_rmw(dev, MT_US_CYC_CFG, MT_US_CYC_CNT, 0x1e);
	mt76_wr(dev, MT_TXOP_CTRL_CFG, 0x583f);

	err = mt76x2_mcu_load_cr(dev, MT_RF_BBP_CR, 0, 0);
	if (err < 0)
		return err;

	mt76x02_phy_set_rxpath(dev);
	mt76x02_phy_set_txdac(dev);

	return mt76x2u_mac_stop(dev);
}

int mt76x2u_register_device(struct mt76x02_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_usb *usb = &dev->mt76.usb;
	bool vht;
	int err;

	INIT_DELAYED_WORK(&dev->cal_work, mt76x2u_phy_calibrate);
	err = mt76x02_init_device(dev);
	if (err)
		return err;

	err = mt76x2u_init_eeprom(dev);
	if (err < 0)
		return err;

	usb->mcu.data = devm_kmalloc(dev->mt76.dev, MCU_RESP_URB_SIZE,
				     GFP_KERNEL);
	if (!usb->mcu.data)
		return -ENOMEM;

	err = mt76u_alloc_queues(&dev->mt76);
	if (err < 0)
		goto fail;

	err = mt76x2u_init_hardware(dev);
	if (err < 0)
		goto fail;

	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = dev->mt76.usb.sg_en ? MT_TX_SG_MAX_SIZE : 1;
	switch (dev->mt76.rev) {
	case 0x76320044:
		/* these ASIC revisions do not support VHT */
		vht = false;
		break;
	default:
		vht = true;
		break;
	}

	err = mt76_register_device(&dev->mt76, vht, mt76x02_rates,
				   ARRAY_SIZE(mt76x02_rates));
	if (err)
		goto fail;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	mt76x02_init_debugfs(dev);
	mt76x2_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt76x2_init_txpower(dev, &dev->mphy.sband_5g.sband);

	return 0;

fail:
	mt76x2u_cleanup(dev);
	return err;
}

void mt76x2u_stop_hw(struct mt76x02_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	mt76x2u_mac_stop(dev);
}

void mt76x2u_cleanup(struct mt76x02_dev *dev)
{
	mt76x02_mcu_set_radio_state(dev, false);
	mt76x2u_stop_hw(dev);
	mt76u_queues_deinit(&dev->mt76);
}
