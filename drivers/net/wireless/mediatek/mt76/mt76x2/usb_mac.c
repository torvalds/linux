/*
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

#include "mt76x2u.h"
#include "eeprom.h"

static void mt76x2u_mac_reset_counters(struct mt76x02_dev *dev)
{
	mt76_rr(dev, MT_RX_STAT_0);
	mt76_rr(dev, MT_RX_STAT_1);
	mt76_rr(dev, MT_RX_STAT_2);
	mt76_rr(dev, MT_TX_STA_0);
	mt76_rr(dev, MT_TX_STA_1);
	mt76_rr(dev, MT_TX_STA_2);
}

static void mt76x2u_mac_fixup_xtal(struct mt76x02_dev *dev)
{
	s8 offset = 0;
	u16 eep_val;

	eep_val = mt76x02_eeprom_get(&dev->mt76, MT_EE_XTAL_TRIM_2);

	offset = eep_val & 0x7f;
	if ((eep_val & 0xff) == 0xff)
		offset = 0;
	else if (eep_val & 0x80)
		offset = 0 - offset;

	eep_val >>= 8;
	if (eep_val == 0x00 || eep_val == 0xff) {
		eep_val = mt76x02_eeprom_get(&dev->mt76, MT_EE_XTAL_TRIM_1);
		eep_val &= 0xff;

		if (eep_val == 0x00 || eep_val == 0xff)
			eep_val = 0x14;
	}

	eep_val &= 0x7f;
	mt76_rmw_field(dev, MT_VEND_ADDR(CFG, MT_XO_CTRL5),
		       MT_XO_CTRL5_C2_VAL, eep_val + offset);
	mt76_set(dev, MT_VEND_ADDR(CFG, MT_XO_CTRL6), MT_XO_CTRL6_C2_CTRL);

	mt76_wr(dev, 0x504, 0x06000000);
	mt76_wr(dev, 0x50c, 0x08800000);
	mdelay(5);
	mt76_wr(dev, 0x504, 0x0);

	/* decrease SIFS from 16us to 13us */
	mt76_rmw_field(dev, MT_XIFS_TIME_CFG,
		       MT_XIFS_TIME_CFG_OFDM_SIFS, 0xd);
	mt76_rmw_field(dev, MT_BKOFF_SLOT_CFG, MT_BKOFF_SLOT_CFG_CC_DELAY, 1);

	/* init fce */
	mt76_clear(dev, MT_FCE_L2_STUFF, MT_FCE_L2_STUFF_WR_MPDU_LEN_EN);

	eep_val = mt76x02_eeprom_get(&dev->mt76, MT_EE_NIC_CONF_2);
	switch (FIELD_GET(MT_EE_NIC_CONF_2_XTAL_OPTION, eep_val)) {
	case 0:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1fee80);
		break;
	case 1:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1feed0);
		break;
	default:
		break;
	}
}

int mt76x2u_mac_reset(struct mt76x02_dev *dev)
{
	mt76_wr(dev, MT_WPDMA_GLO_CFG, BIT(4) | BIT(5));

	/* init pbf regs */
	mt76_wr(dev, MT_PBF_TX_MAX_PCNT, 0xefef3f1f);
	mt76_wr(dev, MT_PBF_RX_MAX_PCNT, 0xfebf);

	mt76_write_mac_initvals(dev);

	mt76_wr(dev, MT_TX_LINK_CFG, 0x1020);
	mt76_wr(dev, MT_AUTO_RSP_CFG, 0x13);
	mt76_wr(dev, MT_MAX_LEN_CFG, 0x2f00);
	mt76_wr(dev, MT_TX_RTS_CFG, 0x92b20);

	mt76_wr(dev, MT_WMM_AIFSN, 0x2273);
	mt76_wr(dev, MT_WMM_CWMIN, 0x2344);
	mt76_wr(dev, MT_WMM_CWMAX, 0x34aa);

	mt76_clear(dev, MT_MAC_SYS_CTRL,
		   MT_MAC_SYS_CTRL_RESET_CSR |
		   MT_MAC_SYS_CTRL_RESET_BBP);

	if (is_mt7612(dev))
		mt76_clear(dev, MT_COEXCFG0, MT_COEXCFG0_COEX_EN);

	mt76_set(dev, MT_EXT_CCA_CFG, 0xf000);
	mt76_clear(dev, MT_TX_ALC_CFG_4, BIT(31));

	mt76x2u_mac_fixup_xtal(dev);

	return 0;
}

int mt76x2u_mac_start(struct mt76x02_dev *dev)
{
	mt76x2u_mac_reset_counters(dev);

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	mt76x02_wait_for_wpdma(&dev->mt76, 1000);
	usleep_range(50, 100);

	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);

	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);

	return 0;
}

int mt76x2u_mac_stop(struct mt76x02_dev *dev)
{
	int i, count = 0, val;
	bool stopped = false;
	u32 rts_cfg;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return -EIO;

	rts_cfg = mt76_rr(dev, MT_TX_RTS_CFG);
	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg & ~MT_TX_RTS_CFG_RETRY_LIMIT);

	mt76_clear(dev, MT_TXOP_CTRL_CFG, BIT(20));
	mt76_clear(dev, MT_TXOP_HLDR_ET, BIT(1));

	/* wait tx dma to stop */
	for (i = 0; i < 2000; i++) {
		val = mt76_rr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG));
		if (!(val & MT_USB_DMA_CFG_TX_BUSY) && i > 10)
			break;
		usleep_range(50, 100);
	}

	/* page count on TxQ */
	for (i = 0; i < 200; i++) {
		if (!(mt76_rr(dev, 0x0438) & 0xffffffff) &&
		    !(mt76_rr(dev, 0x0a30) & 0x000000ff) &&
		    !(mt76_rr(dev, 0x0a34) & 0xff00ff00))
			break;
		usleep_range(10, 20);
	}

	/* disable tx-rx */
	mt76_clear(dev, MT_MAC_SYS_CTRL,
		   MT_MAC_SYS_CTRL_ENABLE_RX |
		   MT_MAC_SYS_CTRL_ENABLE_TX);

	/* Wait for MAC to become idle */
	for (i = 0; i < 1000; i++) {
		if (!(mt76_rr(dev, MT_MAC_STATUS) & MT_MAC_STATUS_TX) &&
		    !mt76_rr(dev, MT_BBP(IBI, 12))) {
			stopped = true;
			break;
		}
		usleep_range(10, 20);
	}

	if (!stopped) {
		mt76_set(dev, MT_BBP(CORE, 4), BIT(1));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(1));

		mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));
	}

	/* page count on RxQ */
	for (i = 0; i < 200; i++) {
		if (!(mt76_rr(dev, 0x0430) & 0x00ff0000) &&
		    !(mt76_rr(dev, 0x0a30) & 0xffffffff) &&
		    !(mt76_rr(dev, 0x0a34) & 0xffffffff) &&
		    ++count > 10)
			break;
		msleep(50);
	}

	if (!mt76_poll(dev, MT_MAC_STATUS, MT_MAC_STATUS_RX, 0, 2000))
		dev_warn(dev->mt76.dev, "MAC RX failed to stop\n");

	/* wait rx dma to stop */
	for (i = 0; i < 2000; i++) {
		val = mt76_rr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG));
		if (!(val & MT_USB_DMA_CFG_RX_BUSY) && i > 10)
			break;
		usleep_range(50, 100);
	}

	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg);

	return 0;
}

void mt76x2u_mac_resume(struct mt76x02_dev *dev)
{
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
	mt76_set(dev, MT_TXOP_CTRL_CFG, BIT(20));
	mt76_set(dev, MT_TXOP_HLDR_ET, BIT(1));
}
