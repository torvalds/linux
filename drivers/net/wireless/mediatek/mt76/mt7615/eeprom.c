// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 */

#include "mt7615.h"
#include "eeprom.h"

static int mt7615_efuse_read(struct mt7615_dev *dev, u32 base,
			     u16 addr, u8 *data)
{
	u32 val;
	int i;

	val = mt76_rr(dev, base + MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN | MT_EFUSE_CTRL_MODE);
	val |= FIELD_PREP(MT_EFUSE_CTRL_AIN, addr & ~0xf);
	val |= MT_EFUSE_CTRL_KICK;
	mt76_wr(dev, base + MT_EFUSE_CTRL, val);

	if (!mt76_poll(dev, base + MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK, 0, 1000))
		return -ETIMEDOUT;

	udelay(2);

	val = mt76_rr(dev, base + MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT ||
	    WARN_ON_ONCE(!(val & MT_EFUSE_CTRL_VALID))) {
		memset(data, 0x0, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		val = mt76_rr(dev, base + MT_EFUSE_RDATA(i));
		put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

static int mt7615_efuse_init(struct mt7615_dev *dev)
{
	u32 base = mt7615_reg_map(dev, MT_EFUSE_BASE);
	int len = MT7615_EEPROM_SIZE;
	int ret, i;
	void *buf;

	if (mt76_rr(dev, base + MT_EFUSE_BASE_CTRL) & MT_EFUSE_BASE_CTRL_EMPTY)
		return -EINVAL;

	dev->mt76.otp.data = devm_kzalloc(dev->mt76.dev, len, GFP_KERNEL);
	dev->mt76.otp.size = len;
	if (!dev->mt76.otp.data)
		return -ENOMEM;

	buf = dev->mt76.otp.data;
	for (i = 0; i + 16 <= len; i += 16) {
		ret = mt7615_efuse_read(dev, base, i, buf + i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mt7615_eeprom_load(struct mt7615_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7615_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	return mt7615_efuse_init(dev);
}

int mt7615_eeprom_init(struct mt7615_dev *dev)
{
	int ret;

	ret = mt7615_eeprom_load(dev);
	if (ret < 0)
		return ret;

	memcpy(dev->mt76.eeprom.data, dev->mt76.otp.data, MT7615_EEPROM_SIZE);

	dev->mt76.cap.has_2ghz = true;
	dev->mt76.cap.has_5ghz = true;

	memcpy(dev->mt76.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mt76);

	return 0;
}
