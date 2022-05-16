// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Roy Luo <royluo@google.com>
 *         Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/etherdevice.h>
#include "mt7615.h"
#include "mac.h"
#include "eeprom.h"

static void mt7615_pci_init_work(struct work_struct *work)
{
	struct mt7615_dev *dev = container_of(work, struct mt7615_dev,
					      mcu_work);
	int i, ret;

	ret = mt7615_mcu_init(dev);
	for (i = 0; (ret == -EAGAIN) && (i < 10); i++) {
		msleep(200);
		ret = mt7615_mcu_init(dev);
	}

	if (ret)
		return;

	mt7615_init_work(dev);
}

static int mt7615_init_hardware(struct mt7615_dev *dev)
{
	u32 addr = mt7615_reg_map(dev, MT_EFUSE_BASE);
	int ret, idx;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	INIT_WORK(&dev->mcu_work, mt7615_pci_init_work);
	ret = mt7615_eeprom_init(dev, addr);
	if (ret < 0)
		return ret;

	if (is_mt7663(&dev->mt76)) {
		/* Reset RGU */
		mt76_clear(dev, MT_MCU_CIRQ_IRQ_SEL(4), BIT(1));
		mt76_set(dev, MT_MCU_CIRQ_IRQ_SEL(4), BIT(1));
	}

	ret = mt7615_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7615_WTBL_STA - 1);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

static void
mt7615_led_set_config(struct led_classdev *led_cdev,
		      u8 delay_on, u8 delay_off)
{
	struct mt7615_dev *dev;
	struct mt76_dev *mt76;
	u32 val, addr;

	mt76 = container_of(led_cdev, struct mt76_dev, led_cdev);
	dev = container_of(mt76, struct mt7615_dev, mt76);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm))
		return;

	val = FIELD_PREP(MT_LED_STATUS_DURATION, 0xffff) |
	      FIELD_PREP(MT_LED_STATUS_OFF, delay_off) |
	      FIELD_PREP(MT_LED_STATUS_ON, delay_on);

	addr = mt7615_reg_map(dev, MT_LED_STATUS_0(mt76->led_pin));
	mt76_wr(dev, addr, val);
	addr = mt7615_reg_map(dev, MT_LED_STATUS_1(mt76->led_pin));
	mt76_wr(dev, addr, val);

	val = MT_LED_CTRL_REPLAY(mt76->led_pin) |
	      MT_LED_CTRL_KICK(mt76->led_pin);
	if (mt76->led_al)
		val |= MT_LED_CTRL_POLARITY(mt76->led_pin);
	addr = mt7615_reg_map(dev, MT_LED_CTRL);
	mt76_wr(dev, addr, val);

	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}

static int
mt7615_led_set_blink(struct led_classdev *led_cdev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	u8 delta_on, delta_off;

	delta_off = max_t(u8, *delay_off / 10, 1);
	delta_on = max_t(u8, *delay_on / 10, 1);

	mt7615_led_set_config(led_cdev, delta_on, delta_off);

	return 0;
}

static void
mt7615_led_set_brightness(struct led_classdev *led_cdev,
			  enum led_brightness brightness)
{
	if (!brightness)
		mt7615_led_set_config(led_cdev, 0, 0xff);
	else
		mt7615_led_set_config(led_cdev, 0xff, 0);
}

int mt7615_register_device(struct mt7615_dev *dev)
{
	int ret;

	mt7615_init_device(dev);
	INIT_WORK(&dev->reset_work, mt7615_mac_reset_work);

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		dev->mt76.led_cdev.brightness_set = mt7615_led_set_brightness;
		dev->mt76.led_cdev.blink_set = mt7615_led_set_blink;
	}

	ret = mt7622_wmac_init(dev);
	if (ret)
		return ret;

	ret = mt7615_init_hardware(dev);
	if (ret)
		return ret;

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	ret = mt7615_thermal_init(dev);
	if (ret)
		return ret;

	ieee80211_queue_work(mt76_hw(dev), &dev->mcu_work);
	mt7615_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt7615_init_txpower(dev, &dev->mphy.sband_5g.sband);

	if (dev->dbdc_support) {
		ret = mt7615_register_ext_phy(dev);
		if (ret)
			return ret;
	}

	return mt7615_init_debugfs(dev);
}

void mt7615_unregister_device(struct mt7615_dev *dev)
{
	bool mcu_running;

	mcu_running = mt7615_wait_for_mcu_init(dev);

	mt7615_unregister_ext_phy(dev);
	mt76_unregister_device(&dev->mt76);
	if (mcu_running)
		mt7615_mcu_exit(dev);

	mt7615_tx_token_put(dev);
	mt7615_dma_cleanup(dev);
	tasklet_disable(&dev->irq_tasklet);

	mt76_free_device(&dev->mt76);
}
