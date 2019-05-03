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

#include "mt76x2.h"

void mt76x2_mac_stop(struct mt76x02_dev *dev, bool force)
{
	bool stopped = false;
	u32 rts_cfg;
	int i;

	mt76_clear(dev, MT_TXOP_CTRL_CFG, MT_TXOP_ED_CCA_EN);
	mt76_clear(dev, MT_TXOP_HLDR_ET, MT_TXOP_HLDR_TX40M_BLK_EN);

	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);

	rts_cfg = mt76_rr(dev, MT_TX_RTS_CFG);
	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg & ~MT_TX_RTS_CFG_RETRY_LIMIT);

	/* Wait for MAC to become idle */
	for (i = 0; i < 300; i++) {
		if ((mt76_rr(dev, MT_MAC_STATUS) &
		     (MT_MAC_STATUS_RX | MT_MAC_STATUS_TX)) ||
		    mt76_rr(dev, MT_BBP(IBI, 12))) {
			udelay(1);
			continue;
		}

		stopped = true;
		break;
	}

	if (force && !stopped) {
		mt76_set(dev, MT_BBP(CORE, 4), BIT(1));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(1));

		mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));
	}

	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg);
}
EXPORT_SYMBOL_GPL(mt76x2_mac_stop);
