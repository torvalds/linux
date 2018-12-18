/*
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

#include <linux/delay.h>
#include "mt76x2.h"
#include "mt76x2_mcu.h"
#include "mt76x2_eeprom.h"

static bool
mt76x2_phy_tssi_init_cal(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	u32 flag = 0;

	if (!mt76x2_tssi_enabled(dev))
		return false;

	if (mt76x2_channel_silent(dev))
		return false;

	if (chan->band == NL80211_BAND_5GHZ)
		flag |= BIT(0);

	if (mt76x2_ext_pa_enabled(dev, chan->band))
		flag |= BIT(8);

	mt76x2_mcu_calibrate(dev, MCU_CAL_TSSI, flag);
	dev->cal.tssi_cal_done = true;
	return true;
}

static void
mt76x2_phy_channel_calibrate(struct mt76x2_dev *dev, bool mac_stopped)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	bool is_5ghz = chan->band == NL80211_BAND_5GHZ;

	if (dev->cal.channel_cal_done)
		return;

	if (mt76x2_channel_silent(dev))
		return;

	if (!dev->cal.tssi_cal_done)
		mt76x2_phy_tssi_init_cal(dev);

	if (!mac_stopped)
		mt76x2_mac_stop(dev, false);

	if (is_5ghz)
		mt76x2_mcu_calibrate(dev, MCU_CAL_LC, 0);

	mt76x2_mcu_calibrate(dev, MCU_CAL_TX_LOFT, is_5ghz);
	mt76x2_mcu_calibrate(dev, MCU_CAL_TXIQ, is_5ghz);
	mt76x2_mcu_calibrate(dev, MCU_CAL_RXIQC_FI, is_5ghz);
	mt76x2_mcu_calibrate(dev, MCU_CAL_TEMP_SENSOR, 0);
	mt76x2_mcu_calibrate(dev, MCU_CAL_TX_SHAPING, 0);

	if (!mac_stopped)
		mt76x2_mac_resume(dev);

	mt76x2_apply_gain_adj(dev);

	dev->cal.channel_cal_done = true;
}

void mt76x2_phy_set_antenna(struct mt76x2_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_BBP(AGC, 0));
	val &= ~(BIT(4) | BIT(1));
	switch (dev->mt76.antenna_mask) {
	case 1:
		/* disable mac DAC control */
		mt76_clear(dev, MT_BBP(IBI, 9), BIT(11));
		mt76_clear(dev, MT_BBP(TXBE, 5), 3);
		mt76_rmw_field(dev, MT_TX_PIN_CFG, MT_TX_PIN_CFG_TXANT, 0x3);
		mt76_rmw_field(dev, MT_BBP(CORE, 32), GENMASK(21, 20), 2);
		/* disable DAC 1 */
		mt76_rmw_field(dev, MT_BBP(CORE, 33), GENMASK(12, 9), 4);

		val &= ~(BIT(3) | BIT(0));
		break;
	case 2:
		/* disable mac DAC control */
		mt76_clear(dev, MT_BBP(IBI, 9), BIT(11));
		mt76_rmw_field(dev, MT_BBP(TXBE, 5), 3, 1);
		mt76_rmw_field(dev, MT_TX_PIN_CFG, MT_TX_PIN_CFG_TXANT, 0xc);
		mt76_rmw_field(dev, MT_BBP(CORE, 32), GENMASK(21, 20), 1);
		/* disable DAC 0 */
		mt76_rmw_field(dev, MT_BBP(CORE, 33), GENMASK(12, 9), 1);

		val &= ~BIT(3);
		val |= BIT(0);
		break;
	case 3:
	default:
		/* enable mac DAC control */
		mt76_set(dev, MT_BBP(IBI, 9), BIT(11));
		mt76_set(dev, MT_BBP(TXBE, 5), 3);
		mt76_rmw_field(dev, MT_TX_PIN_CFG, MT_TX_PIN_CFG_TXANT, 0xf);
		mt76_clear(dev, MT_BBP(CORE, 32), GENMASK(21, 20));
		mt76_clear(dev, MT_BBP(CORE, 33), GENMASK(12, 9));

		val &= ~BIT(0);
		val |= BIT(3);
		break;
	}
	mt76_wr(dev, MT_BBP(AGC, 0), val);
}

static void
mt76x2_get_agc_gain(struct mt76x2_dev *dev, u8 *dest)
{
	dest[0] = mt76_get_field(dev, MT_BBP(AGC, 8), MT_BBP_AGC_GAIN);
	dest[1] = mt76_get_field(dev, MT_BBP(AGC, 9), MT_BBP_AGC_GAIN);
}

static int
mt76x2_get_rssi_gain_thresh(struct mt76x2_dev *dev)
{
	switch (dev->mt76.chandef.width) {
	case NL80211_CHAN_WIDTH_80:
		return -62;
	case NL80211_CHAN_WIDTH_40:
		return -65;
	default:
		return -68;
	}
}

static int
mt76x2_get_low_rssi_gain_thresh(struct mt76x2_dev *dev)
{
	switch (dev->mt76.chandef.width) {
	case NL80211_CHAN_WIDTH_80:
		return -76;
	case NL80211_CHAN_WIDTH_40:
		return -79;
	default:
		return -82;
	}
}

static void
mt76x2_phy_set_gain_val(struct mt76x2_dev *dev)
{
	u32 val;
	u8 gain_val[2];

	gain_val[0] = dev->cal.agc_gain_cur[0] - dev->cal.agc_gain_adjust;
	gain_val[1] = dev->cal.agc_gain_cur[1] - dev->cal.agc_gain_adjust;

	if (dev->mt76.chandef.width >= NL80211_CHAN_WIDTH_40)
		val = 0x1e42 << 16;
	else
		val = 0x1836 << 16;

	val |= 0xf8;

	mt76_wr(dev, MT_BBP(AGC, 8),
		val | FIELD_PREP(MT_BBP_AGC_GAIN, gain_val[0]));
	mt76_wr(dev, MT_BBP(AGC, 9),
		val | FIELD_PREP(MT_BBP_AGC_GAIN, gain_val[1]));

	if (dev->mt76.chandef.chan->flags & IEEE80211_CHAN_RADAR)
		mt76x2_dfs_adjust_agc(dev);
}

static void
mt76x2_phy_adjust_vga_gain(struct mt76x2_dev *dev)
{
	u32 false_cca;
	u8 limit = dev->cal.low_gain > 0 ? 16 : 4;

	false_cca = FIELD_GET(MT_RX_STAT_1_CCA_ERRORS, mt76_rr(dev, MT_RX_STAT_1));
	dev->cal.false_cca = false_cca;
	if (false_cca > 800 && dev->cal.agc_gain_adjust < limit)
		dev->cal.agc_gain_adjust += 2;
	else if ((false_cca < 10 && dev->cal.agc_gain_adjust > 0) ||
		 (dev->cal.agc_gain_adjust >= limit && false_cca < 500))
		dev->cal.agc_gain_adjust -= 2;
	else
		return;

	mt76x2_phy_set_gain_val(dev);
}

static void
mt76x2_phy_update_channel_gain(struct mt76x2_dev *dev)
{
	u8 *gain = dev->cal.agc_gain_init;
	u8 low_gain_delta, gain_delta;
	bool gain_change;
	int low_gain;
	u32 val;

	dev->cal.avg_rssi_all = mt76x2_phy_get_min_avg_rssi(dev);

	low_gain = (dev->cal.avg_rssi_all > mt76x2_get_rssi_gain_thresh(dev)) +
		   (dev->cal.avg_rssi_all > mt76x2_get_low_rssi_gain_thresh(dev));

	gain_change = (dev->cal.low_gain & 2) ^ (low_gain & 2);
	dev->cal.low_gain = low_gain;

	if (!gain_change) {
		mt76x2_phy_adjust_vga_gain(dev);
		return;
	}

	if (dev->mt76.chandef.width == NL80211_CHAN_WIDTH_80) {
		mt76_wr(dev, MT_BBP(RXO, 14), 0x00560211);
		val = mt76_rr(dev, MT_BBP(AGC, 26)) & ~0xf;
		if (low_gain == 2)
			val |= 0x3;
		else
			val |= 0x5;
		mt76_wr(dev, MT_BBP(AGC, 26), val);
	} else {
		mt76_wr(dev, MT_BBP(RXO, 14), 0x00560423);
	}

	if (mt76x2_has_ext_lna(dev))
		low_gain_delta = 10;
	else
		low_gain_delta = 14;

	if (low_gain == 2) {
		mt76_wr(dev, MT_BBP(RXO, 18), 0xf000a990);
		mt76_wr(dev, MT_BBP(AGC, 35), 0x08080808);
		mt76_wr(dev, MT_BBP(AGC, 37), 0x08080808);
		gain_delta = low_gain_delta;
		dev->cal.agc_gain_adjust = 0;
	} else {
		mt76_wr(dev, MT_BBP(RXO, 18), 0xf000a991);
		if (dev->mt76.chandef.width == NL80211_CHAN_WIDTH_80)
			mt76_wr(dev, MT_BBP(AGC, 35), 0x10101014);
		else
			mt76_wr(dev, MT_BBP(AGC, 35), 0x11111116);
		mt76_wr(dev, MT_BBP(AGC, 37), 0x2121262C);
		gain_delta = 0;
		dev->cal.agc_gain_adjust = low_gain_delta;
	}

	dev->cal.agc_gain_cur[0] = gain[0] - gain_delta;
	dev->cal.agc_gain_cur[1] = gain[1] - gain_delta;
	mt76x2_phy_set_gain_val(dev);

	/* clear false CCA counters */
	mt76_rr(dev, MT_RX_STAT_1);
}

int mt76x2_phy_set_channel(struct mt76x2_dev *dev,
			   struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *chan = chandef->chan;
	bool scan = test_bit(MT76_SCANNING, &dev->mt76.state);
	enum nl80211_band band = chan->band;
	u8 channel;

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
	int ch_group_index;
	u8 bw, bw_index;
	int freq, freq1;
	int ret;

	dev->cal.channel_cal_done = false;
	freq = chandef->chan->center_freq;
	freq1 = chandef->center_freq1;
	channel = chan->hw_value;

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
	mt76x2_phy_set_txpower_regs(dev, band);
	mt76x2_configure_tx_delay(dev, band, bw);
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

	ret = mt76x2_mcu_set_channel(dev, channel, bw, bw_index, scan);
	if (ret)
		return ret;

	mt76x2_mcu_init_gain(dev, channel, dev->cal.rx.mcu_gain, true);

	mt76x2_phy_set_antenna(dev);

	/* Enable LDPC Rx */
	if (mt76xx_rev(dev) >= MT76XX_REV_E3)
		mt76_set(dev, MT_BBP(RXO, 13), BIT(10));

	if (!dev->cal.init_cal_done) {
		u8 val = mt76x2_eeprom_get(dev, MT_EE_BT_RCAL_RESULT);

		if (val != 0xff)
			mt76x2_mcu_calibrate(dev, MCU_CAL_R, 0);
	}

	mt76x2_mcu_calibrate(dev, MCU_CAL_RXDCOC, channel);

	/* Rx LPF calibration */
	if (!dev->cal.init_cal_done)
		mt76x2_mcu_calibrate(dev, MCU_CAL_RC, 0);

	dev->cal.init_cal_done = true;

	mt76_wr(dev, MT_BBP(AGC, 61), 0xFF64A4E2);
	mt76_wr(dev, MT_BBP(AGC, 7), 0x08081010);
	mt76_wr(dev, MT_BBP(AGC, 11), 0x00000404);
	mt76_wr(dev, MT_BBP(AGC, 2), 0x00007070);
	mt76_wr(dev, MT_TXOP_CTRL_CFG, 0x04101B3F);

	if (scan)
		return 0;

	dev->cal.low_gain = -1;
	mt76x2_phy_channel_calibrate(dev, true);
	mt76x2_get_agc_gain(dev, dev->cal.agc_gain_init);
	memcpy(dev->cal.agc_gain_cur, dev->cal.agc_gain_init,
	       sizeof(dev->cal.agc_gain_cur));

	/* init default values for temp compensation */
	if (mt76x2_tssi_enabled(dev)) {
		mt76_rmw_field(dev, MT_TX_ALC_CFG_1, MT_TX_ALC_CFG_1_TEMP_COMP,
			       0x38);
		mt76_rmw_field(dev, MT_TX_ALC_CFG_2, MT_TX_ALC_CFG_2_TEMP_COMP,
			       0x38);
	}

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);

	return 0;
}

static void
mt76x2_phy_tssi_compensate(struct mt76x2_dev *dev)
{
	struct ieee80211_channel *chan = dev->mt76.chandef.chan;
	struct mt76x2_tx_power_info txp;
	struct mt76x2_tssi_comp t = {};

	if (!dev->cal.tssi_cal_done)
		return;

	if (!dev->cal.tssi_comp_pending) {
		/* TSSI trigger */
		t.cal_mode = BIT(0);
		mt76x2_mcu_tssi_comp(dev, &t);
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
		mt76x2_mcu_tssi_comp(dev, &t);

		if (t.pa_mode || dev->cal.dpd_cal_done)
			return;

		usleep_range(10000, 20000);
		mt76x2_mcu_calibrate(dev, MCU_CAL_DPD, chan->hw_value);
		dev->cal.dpd_cal_done = true;
	}
}

static void
mt76x2_phy_temp_compensate(struct mt76x2_dev *dev)
{
	struct mt76x2_temp_comp t;
	int temp, db_diff;

	if (mt76x2_get_temp_comp(dev, &t))
		return;

	temp = mt76_get_field(dev, MT_TEMP_SENSOR, MT_TEMP_SENSOR_VAL);
	temp -= t.temp_25_ref;
	temp = (temp * 1789) / 1000 + 25;
	dev->cal.temp = temp;

	if (temp > 25)
		db_diff = (temp - 25) / t.high_slope;
	else
		db_diff = (25 - temp) / t.low_slope;

	db_diff = min(db_diff, t.upper_bound);
	db_diff = max(db_diff, t.lower_bound);

	mt76_rmw_field(dev, MT_TX_ALC_CFG_1, MT_TX_ALC_CFG_1_TEMP_COMP,
		       db_diff * 2);
	mt76_rmw_field(dev, MT_TX_ALC_CFG_2, MT_TX_ALC_CFG_2_TEMP_COMP,
		       db_diff * 2);
}

void mt76x2_phy_calibrate(struct work_struct *work)
{
	struct mt76x2_dev *dev;

	dev = container_of(work, struct mt76x2_dev, cal_work.work);
	mt76x2_phy_channel_calibrate(dev, false);
	mt76x2_phy_tssi_compensate(dev);
	mt76x2_phy_temp_compensate(dev);
	mt76x2_phy_update_channel_gain(dev);
	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
}

int mt76x2_phy_start(struct mt76x2_dev *dev)
{
	int ret;

	ret = mt76x2_mcu_set_radio_state(dev, true);
	if (ret)
		return ret;

	mt76x2_mcu_load_cr(dev, MT_RF_BBP_CR, 0, 0);

	return ret;
}
