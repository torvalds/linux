// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7921.h"
#include "eeprom.h"

static u32 mt7921_eeprom_read(struct mt7921_dev *dev, u32 offset)
{
	u8 *data = dev->mt76.eeprom.data;

	if (data[offset] == 0xff)
		mt7921_mcu_get_eeprom(dev, offset);

	return data[offset];
}

static int mt7921_eeprom_load(struct mt7921_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7921_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	memset(dev->mt76.eeprom.data, -1, MT7921_EEPROM_SIZE);

	return 0;
}

static int mt7921_check_eeprom(struct mt7921_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 val;

	mt7921_eeprom_read(dev, MT_EE_CHIP_ID);
	val = get_unaligned_le16(eeprom);

	switch (val) {
	case 0x7922:
	case 0x7961:
		return 0;
	default:
		return -EINVAL;
	}
}

void mt7921_eeprom_parse_band_config(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	u32 val;

	val = mt7921_eeprom_read(dev, MT_EE_WIFI_CONF);
	val = FIELD_GET(MT_EE_WIFI_CONF_BAND_SEL, val);

	switch (val) {
	case MT_EE_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
	case MT_EE_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	default:
		phy->mt76->cap.has_2ghz = true;
		phy->mt76->cap.has_5ghz = true;
		break;
	}
}

static void mt7921_eeprom_parse_hw_cap(struct mt7921_dev *dev)
{
	u8 tx_mask;

	mt7921_eeprom_parse_band_config(&dev->phy);

	/* TODO: read NSS with MCU_CMD_NIC_CAPV2 */
	tx_mask = 2;
	dev->chainmask = BIT(tx_mask) - 1;
	dev->mphy.antenna_mask = dev->chainmask;
	dev->mphy.chainmask = dev->mphy.antenna_mask;
}

int mt7921_eeprom_init(struct mt7921_dev *dev)
{
	int ret;

	ret = mt7921_eeprom_load(dev);
	if (ret < 0)
		return ret;

	ret = mt7921_check_eeprom(dev);
	if (ret)
		return ret;

	mt7921_eeprom_parse_hw_cap(dev);
	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mphy);

	return 0;
}
