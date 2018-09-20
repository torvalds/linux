/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <asm/unaligned.h>

#include "mt76.h"
#include "mt76x02_eeprom.h"
#include "mt76x02_regs.h"

static int
mt76x02_efuse_read(struct mt76_dev *dev, u16 addr, u8 *data,
		   enum mt76x02_eeprom_modes mode)
{
	u32 val;
	int i;

	val = __mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= FIELD_PREP(MT_EFUSE_CTRL_AIN, addr & ~0xf);
	val |= FIELD_PREP(MT_EFUSE_CTRL_MODE, mode);
	val |= MT_EFUSE_CTRL_KICK;
	__mt76_wr(dev, MT_EFUSE_CTRL, val);

	if (!__mt76_poll_msec(dev, MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK,
			      0, 1000))
		return -ETIMEDOUT;

	udelay(2);

	val = __mt76_rr(dev, MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT) {
		memset(data, 0xff, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		val = __mt76_rr(dev, MT_EFUSE_DATA(i));
		put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

int mt76x02_get_efuse_data(struct mt76_dev *dev, u16 base, void *buf,
			   int len, enum mt76x02_eeprom_modes mode)
{
	int ret, i;

	for (i = 0; i + 16 <= len; i += 16) {
		ret = mt76x02_efuse_read(dev, base + i, buf + i, mode);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_get_efuse_data);

bool mt76x02_ext_pa_enabled(struct mt76_dev *dev, enum nl80211_band band)
{
	u16 conf0 = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);

	if (band == NL80211_BAND_5GHZ)
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_5G);
	else
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_2G);
}
EXPORT_SYMBOL_GPL(mt76x02_ext_pa_enabled);
