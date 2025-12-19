// SPDX-License-Identifier: BSD-3-Clause-Clear
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

int mt7615_register_device(struct mt7615_dev *dev)
{
	int ret;

	mt7615_init_device(dev);
	INIT_WORK(&dev->reset_work, mt7615_mac_reset_work);

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		dev->mphy.leds.cdev.brightness_set = mt7615_led_set_brightness;
		dev->mphy.leds.cdev.blink_set = mt7615_led_set_blink;
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
	tasklet_disable(&dev->mt76.irq_tasklet);

	mt76_free_device(&dev->mt76);
}
