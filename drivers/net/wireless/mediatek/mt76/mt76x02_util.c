/*
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include "mt76.h"
#include "mt76x02_regs.h"

void mt76x02_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags, u64 multicast)
{
	struct mt76_dev *dev = hw->priv;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		dev->rxfilter &= ~(_hw);				\
		dev->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mutex);

	dev->rxfilter &= ~MT_RX_FILTR_CFG_OTHER_BSS;

	MT76_FILTER(FCSFAIL, MT_RX_FILTR_CFG_CRC_ERR);
	MT76_FILTER(PLCPFAIL, MT_RX_FILTR_CFG_PHY_ERR);
	MT76_FILTER(CONTROL, MT_RX_FILTR_CFG_ACK |
			     MT_RX_FILTR_CFG_CTS |
			     MT_RX_FILTR_CFG_CFEND |
			     MT_RX_FILTR_CFG_CFACK |
			     MT_RX_FILTR_CFG_BA |
			     MT_RX_FILTR_CFG_CTRL_RSV);
	MT76_FILTER(PSPOLL, MT_RX_FILTR_CFG_PSPOLL);

	*total_flags = flags;
	dev->bus->wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76x02_configure_filter);

MODULE_LICENSE("Dual BSD/GPL");
