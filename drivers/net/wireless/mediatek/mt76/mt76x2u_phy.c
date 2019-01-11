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
#include "mt76x2_eeprom.h"

void mt76x2u_phy_set_rxpath(struct mt76x2_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_BBP(AGC, 0));
	val &= ~BIT(4);

	switch (dev->chainmask & 0xf) {
	case 2:
		val |= BIT(3);
		break;
	default:
		val &= ~BIT(3);
		break;
	}
	mt76_wr(dev, MT_BBP(AGC, 0), val);
}

void mt76x2u_phy_set_txdac(struct mt76x2_dev *dev)
{
	int txpath;

	txpath = (dev->chainmask >> 8) & 0xf;
	switch (txpath) {
	case 2:
		mt76_set(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	default:
		mt76_clear(dev, MT_BBP(TXBE, 5), 0x3);
		break;
	}
}

void mt76x2u_phy_channel_calibrate(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	bool is_5ghz = chan->band == NL80211_BAND_5GHZ;

	if (mt76x2_channel_silent(dev))
		return;

	mt76x2u_mac_stop(dev);

	if (is_5ghz)
		mt76x2u_mcu_calibrate(dev, MCU_CAL_LC, 0);

	mt76x2u_mcu_calibrate(dev, MCU_CAL_TX_LOFT, is_5ghz);
	mt76x2u_mcu_calibrate(dev, MCU_CAL_TXIQ, is_5ghz);
	mt76x2u_mcu_calibrate(dev, MCU_CAL_RXIQC_FI, is_5ghz);
	mt76x2u_mcu_calibrate(dev, MCU_CAL_TEMP_SENSOR, 0);

	mt76x2u_mac_resume(dev);
}

static void
mt76x2u_phy_tssi_compensate(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	struct mt76x2_tx_power_info txp;
	struct mt76x2_tssi_comp t = {};

	if (!dev->cal.tssi_cal_done)
		return;

	if (!dev->cal.tssi_comp_pending) {
		/* TSSI trigger */
		t.cal_mode = BIT(0);
		mt76x2u_mcu_tssi_comp(dev, &t);
		dev->cal.tssi_comp_pending = true;
	} else {
		if (mt76_rr(dev, MT_BBP(CORE, 34)) & BIT(4))
			return;

		dev->cal.tssi_comp_pending = false;
		mt76x2_get_power_info(dev, &txp, chan);

		if (mt76x2_ext_pa_enabled(dev, chan->band))
			t.pa_mode = 1;

		t.cal_mode = BIT(1);
		t.slope0 = txp.chain[0].tssi_slope;
		t.offset0 = txp.chain[0].tssi_offset;
		t.slope1 = txp.chain[1].tssi_slope;
		t.offset1 = txp.chain[1].tssi_offset;
		mt76x2u_mcu_tssi_comp(dev, &t);

		if (t.pa_mode || dev->cal.dpd_cal_done)
			return;

		usleep_range(10000, 20000);
		mt76x2u_mcu_calibrate(dev, MCU_CAL_DPD, chan->hw_value);
		dev->cal.dpd_cal_done = true;
	}
}

static void
mt76x2u_phy_update_channel_gain(struct mt76x2_dev *dev)
{
	u8 channel = dev->mt76.chandef.chan->hw_value;
	int freq, freq1;
	u32 false_cca;

	freq = dev->mt76.chandef.chan->center_freq;
	freq1 = dev->mt76.chandef.center_freq1;

	switch (dev->mt76.chandef.width) {
	case NL80211_CHAN_WIDTH_80: {
		int ch_group_index;

		ch_group_index = (freq - freq1 + 30) / 20;
		if (WARN_ON(ch_group_index < 0 || ch_group_index > 3))
			ch_group_index = 0;
		channel += 6 - ch_group_index * 4;
		break;
	}
	case NL80211_CHAN_WIDTH_40:
		if (freq1 > freq)
			channel += 2;
		else
			channel -= 2;
		break;
	default:
		break;
	}

	dev->cal.avg_rssi_all = mt76x2_phy_get_min_avg_rssi(dev);
	false_cca = FIELD_GET(MT_RX_STAT_1_CCA_ERRORS,
			      mt76_rr(dev, MT_RX_STAT_1));

	mt76x2u_mcu_set_dynamic_vga(dev, channel, false, false,
				    dev->cal.avg_rssi_all, false_cca);
}

void mt76x2u_phy_calibrate(struct work_struct *work)
{
	struct mt76x2_dev *dev;

	dev = container_of(work, struct mt76x2_dev, cal_work.work);
	mt76x2u_phy_tssi_compensate(dev);
	mt76x2u_phy_update_channel_gain(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
}

int mt76x2u_phy_set_channel(struct mt76x2_dev *dev,
			    struct cfg80211_chan_def *chandef)
{
	u32 ext_cca_chan[4] = {
		[0] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(0)),
		[1] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(1)),
		[2] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(2)),
		[3] = FIELD_PREP(MT_EXT_CCA_CFG_CCA0, 3) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA1, 2) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA2, 1) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA3, 0) |
		      FIELD_PREP(MT_EXT_CCA_CFG_CCA_MASK, BIT(3)),
	};
	bool scan = test_bit(MT76_SCANNING, &dev->mt76.state);
	struct ieee80211_channel *chan = chandef->chan;
	u8 channel = chan->hw_value, bw, bw_index;
	int ch_group_index, freq, freq1, ret;

	dev->cal.channel_cal_done = false;
	freq = chandef->chan->center_freq;
	freq1 = chandef->center_freq1;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		bw = 1;
		if (freq1 > freq) {
			bw_index = 1;
			ch_group_index = 0;
		} else {
			bw_index = 3;
			ch_group_index = 1;
		}
		channel += 2 - ch_group_index * 4;
		break;
	case NL80211_CHAN_WIDTH_80:
		ch_group_index = (freq - freq1 + 30) / 20;
		if (WARN_ON(ch_group_index < 0 || ch_group_index > 3))
			ch_group_index = 0;
		bw = 2;
		bw_index = ch_group_index;
		channel += 6 - ch_group_index * 4;
		break;
	default:
		bw = 0;
		bw_index = 0;
		ch_group_index = 0;
		break;
	}

	mt76x2_read_rx_gain(dev);
	mt76x2_phy_set_txpower_regs(dev, chan->band);
	mt76x2_configure_tx_delay(dev, chan->band, bw);
	mt76x2_phy_set_txpower(dev);

	mt76x2_phy_set_band(dev, chan->band, ch_group_index & 1);
	mt76x2_phy_set_bw(dev, chandef->width, ch_group_index);

	mt76_rmw(dev, MT_EXT_CCA_CFG,
		 (MT_EXT_CCA_CFG_CCA0 |
		  MT_EXT_CCA_CFG_CCA1 |
		  MT_EXT_CCA_CFG_CCA2 |
		  MT_EXT_CCA_CFG_CCA3 |
		  MT_EXT_CCA_CFG_CCA_MASK),
		 ext_cca_chan[ch_group_index]);

	ret = mt76x2u_mcu_set_channel(dev, channel, bw, bw_index, scan);
	if (ret)
		return ret;

	mt76x2u_mcu_init_gain(dev, channel, dev->cal.rx.mcu_gain, true);

	/* Enable LDPC Rx */
	if (mt76xx_rev(dev) >= MT76XX_REV_E3)
		mt76_set(dev, MT_BBP(RXO, 13), BIT(10));

	if (!dev->cal.init_cal_done) {
		u8 val = mt76x2_eeprom_get(dev, MT_EE_BT_RCAL_RESULT);

		if (val != 0xff)
			mt76x2u_mcu_calibrate(dev, MCU_CAL_R, 0);
	}

	mt76x2u_mcu_calibrate(dev, MCU_CAL_RXDCOC, channel);

	/* Rx LPF calibration */
	if (!dev->cal.init_cal_done)
		mt76x2u_mcu_calibrate(dev, MCU_CAL_RC, 0);
	dev->cal.init_cal_done = true;

	mt76_wr(dev, MT_BBP(AGC, 61), 0xff64a4e2);
	mt76_wr(dev, MT_BBP(AGC, 7), 0x08081010);
	mt76_wr(dev, MT_BBP(AGC, 11), 0x00000404);
	mt76_wr(dev, MT_BBP(AGC, 2), 0x00007070);
	mt76_wr(dev, MT_TXOP_CTRL_CFG, 0X04101b3f);

	mt76_set(dev, MT_BBP(TXO, 4), BIT(25));
	mt76_set(dev, MT_BBP(RXO, 13), BIT(8));

	if (scan)
		return 0;

	if (mt76x2_tssi_enabled(dev)) {
		/* init default values for temp compensation */
		mt76_rmw_field(dev, MT_TX_ALC_CFG_1, MT_TX_ALC_CFG_1_TEMP_COMP,
			       0x38);
		mt76_rmw_field(dev, MT_TX_ALC_CFG_2, MT_TX_ALC_CFG_2_TEMP_COMP,
			       0x38);

		/* init tssi calibration */
		if (!mt76x2_channel_silent(dev)) {
			struct ieee80211_channel *chan;
			u32 flag = 0;

			chan = dev->mt76.chandef.chan;
			if (chan->band == NL80211_BAND_5GHZ)
				flag |= BIT(0);
			if (mt76x2_ext_pa_enabled(dev, chan->band))
				flag |= BIT(8);
			mt76x2u_mcu_calibrate(dev, MCU_CAL_TSSI, flag);
			dev->cal.tssi_cal_done = true;
		}
	}

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
	return 0;
}
