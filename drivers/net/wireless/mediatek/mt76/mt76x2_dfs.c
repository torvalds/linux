/*
 * Copyright (C) 2016 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
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

#define RADAR_SPEC(m, len, el, eh, wl, wh,		\
		   w_tolerance, tl, th, t_tolerance,	\
		   bl, bh, event_exp, power_jmp)	\
{							\
	.mode = m,					\
	.avg_len = len,					\
	.e_low = el,					\
	.e_high = eh,					\
	.w_low = wl,					\
	.w_high = wh,					\
	.w_margin = w_tolerance,			\
	.t_low = tl,					\
	.t_high = th,					\
	.t_margin = t_tolerance,			\
	.b_low = bl,					\
	.b_high = bh,					\
	.event_expiration = event_exp,			\
	.pwr_jmp = power_jmp				\
}

static const struct mt76x2_radar_specs etsi_radar_specs[] = {
	/* 20MHz */
	RADAR_SPEC(0, 8, 2, 15, 106, 150, 10, 4900, 100096, 10, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(0, 40, 4, 59, 96, 380, 150, 4900, 100096, 40, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(3, 60, 20, 46, 300, 640, 80, 4900, 10100, 80, 0,
		   0x7fffffff, 0x155cc0, 0x19dd),
	RADAR_SPEC(8, 8, 2, 9, 106, 150, 32, 4900, 296704, 32, 0,
		   0x7fffffff, 0x2191c0, 0x15cc),
	/* 40MHz */
	RADAR_SPEC(0, 8, 2, 15, 106, 150, 10, 4900, 100096, 10, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(0, 40, 4, 59, 96, 380, 150, 4900, 100096, 40, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(3, 60, 20, 46, 300, 640, 80, 4900, 10100, 80, 0,
		   0x7fffffff, 0x155cc0, 0x19dd),
	RADAR_SPEC(8, 8, 2, 9, 106, 150, 32, 4900, 296704, 32, 0,
		   0x7fffffff, 0x2191c0, 0x15cc),
	/* 80MHz */
	RADAR_SPEC(0, 8, 2, 15, 106, 150, 10, 4900, 100096, 10, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(0, 40, 4, 59, 96, 380, 150, 4900, 100096, 40, 0,
		   0x7fffffff, 0x155cc0, 0x19cc),
	RADAR_SPEC(3, 60, 20, 46, 300, 640, 80, 4900, 10100, 80, 0,
		   0x7fffffff, 0x155cc0, 0x19dd),
	RADAR_SPEC(8, 8, 2, 9, 106, 150, 32, 4900, 296704, 32, 0,
		   0x7fffffff, 0x2191c0, 0x15cc)
};

static const struct mt76x2_radar_specs fcc_radar_specs[] = {
	/* 20MHz */
	RADAR_SPEC(0, 8, 2, 12, 106, 150, 5, 2900, 80100, 5, 0,
		   0x7fffffff, 0xfe808, 0x13dc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0xfe808, 0x19dd),
	RADAR_SPEC(0, 40, 4, 54, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0xfe808, 0x12cc),
	RADAR_SPEC(2, 60, 15, 63, 640, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0x57bcf00, 0x1289),
	/* 40MHz */
	RADAR_SPEC(0, 8, 2, 12, 106, 150, 5, 2900, 80100, 5, 0,
		   0x7fffffff, 0xfe808, 0x13dc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0xfe808, 0x19dd),
	RADAR_SPEC(0, 40, 4, 54, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0xfe808, 0x12cc),
	RADAR_SPEC(2, 60, 15, 63, 640, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0x57bcf00, 0x1289),
	/* 80MHz */
	RADAR_SPEC(0, 8, 2, 14, 106, 150, 15, 2900, 80100, 15, 0,
		   0x7fffffff, 0xfe808, 0x16cc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0xfe808, 0x19dd),
	RADAR_SPEC(0, 40, 4, 54, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0xfe808, 0x12cc),
	RADAR_SPEC(2, 60, 15, 63, 640, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0x57bcf00, 0x1289)
};

static const struct mt76x2_radar_specs jp_w56_radar_specs[] = {
	/* 20MHz */
	RADAR_SPEC(0, 8, 2, 7, 106, 150, 5, 2900, 80100, 5, 0,
		   0x7fffffff, 0x14c080, 0x13dc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0x14c080, 0x19dd),
	RADAR_SPEC(0, 40, 4, 44, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0x14c080, 0x12cc),
	RADAR_SPEC(2, 60, 15, 48, 940, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0X57bcf00, 0x1289),
	/* 40MHz */
	RADAR_SPEC(0, 8, 2, 7, 106, 150, 5, 2900, 80100, 5, 0,
		   0x7fffffff, 0x14c080, 0x13dc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0x14c080, 0x19dd),
	RADAR_SPEC(0, 40, 4, 44, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0x14c080, 0x12cc),
	RADAR_SPEC(2, 60, 15, 48, 940, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0X57bcf00, 0x1289),
	/* 80MHz */
	RADAR_SPEC(0, 8, 2, 9, 106, 150, 15, 2900, 80100, 15, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	RADAR_SPEC(0, 8, 2, 7, 106, 140, 5, 27600, 27900, 5, 0,
		   0x7fffffff, 0x14c080, 0x19dd),
	RADAR_SPEC(0, 40, 4, 44, 96, 480, 150, 2900, 80100, 40, 0,
		   0x7fffffff, 0x14c080, 0x12cc),
	RADAR_SPEC(2, 60, 15, 48, 940, 2080, 32, 19600, 40200, 32, 0,
		   0x3938700, 0X57bcf00, 0x1289)
};

static const struct mt76x2_radar_specs jp_w53_radar_specs[] = {
	/* 20MHz */
	RADAR_SPEC(0, 8, 2, 9, 106, 150, 20, 28400, 77000, 20, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 },
	RADAR_SPEC(0, 40, 4, 44, 96, 200, 150, 28400, 77000, 60, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 },
	/* 40MHz */
	RADAR_SPEC(0, 8, 2, 9, 106, 150, 20, 28400, 77000, 20, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 },
	RADAR_SPEC(0, 40, 4, 44, 96, 200, 150, 28400, 77000, 60, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 },
	/* 80MHz */
	RADAR_SPEC(0, 8, 2, 9, 106, 150, 20, 28400, 77000, 20, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 },
	RADAR_SPEC(0, 40, 4, 44, 96, 200, 150, 28400, 77000, 60, 0,
		   0x7fffffff, 0x14c080, 0x16cc),
	{ 0 }
};

static void mt76x2_dfs_set_capture_mode_ctrl(struct mt76x2_dev *dev,
					     u8 enable)
{
	u32 data;

	data = (1 << 1) | enable;
	mt76_wr(dev, MT_BBP(DFS, 36), data);
}

static bool mt76x2_dfs_check_chirp(struct mt76x2_dev *dev)
{
	bool ret = false;
	u32 current_ts, delta_ts;
	struct mt76x2_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	current_ts = mt76_rr(dev, MT_PBF_LIFE_TIMER);
	delta_ts = current_ts - dfs_pd->chirp_pulse_ts;
	dfs_pd->chirp_pulse_ts = current_ts;

	/* 12 sec */
	if (delta_ts <= (12 * (1 << 20))) {
		if (++dfs_pd->chirp_pulse_cnt > 8)
			ret = true;
	} else {
		dfs_pd->chirp_pulse_cnt = 1;
	}

	return ret;
}

static void mt76x2_dfs_get_hw_pulse(struct mt76x2_dev *dev,
				    struct mt76x2_dfs_hw_pulse *pulse)
{
	u32 data;

	/* select channel */
	data = (MT_DFS_CH_EN << 16) | pulse->engine;
	mt76_wr(dev, MT_BBP(DFS, 0), data);

	/* reported period */
	pulse->period = mt76_rr(dev, MT_BBP(DFS, 19));

	/* reported width */
	pulse->w1 = mt76_rr(dev, MT_BBP(DFS, 20));
	pulse->w2 = mt76_rr(dev, MT_BBP(DFS, 23));

	/* reported burst number */
	pulse->burst = mt76_rr(dev, MT_BBP(DFS, 22));
}

static bool mt76x2_dfs_check_hw_pulse(struct mt76x2_dev *dev,
				      struct mt76x2_dfs_hw_pulse *pulse)
{
	bool ret = false;

	if (!pulse->period || !pulse->w1)
		return false;

	switch (dev->dfs_pd.region) {
	case NL80211_DFS_FCC:
		if (pulse->engine > 3)
			break;

		if (pulse->engine == 3) {
			ret = mt76x2_dfs_check_chirp(dev);
			break;
		}

		/* check short pulse*/
		if (pulse->w1 < 120)
			ret = (pulse->period >= 2900 &&
			       (pulse->period <= 4700 ||
				pulse->period >= 6400) &&
			       (pulse->period <= 6800 ||
				pulse->period >= 10200) &&
			       pulse->period <= 61600);
		else if (pulse->w1 < 130) /* 120 - 130 */
			ret = (pulse->period >= 2900 &&
			       pulse->period <= 61600);
		else
			ret = (pulse->period >= 3500 &&
			       pulse->period <= 10100);
		break;
	case NL80211_DFS_ETSI:
		if (pulse->engine >= 3)
			break;

		ret = (pulse->period >= 4900 &&
		       (pulse->period <= 10200 ||
			pulse->period >= 12400) &&
		       pulse->period <= 100100);
		break;
	case NL80211_DFS_JP:
		if (dev->mt76.chandef.chan->center_freq >= 5250 &&
		    dev->mt76.chandef.chan->center_freq <= 5350) {
			/* JPW53 */
			if (pulse->w1 <= 130)
				ret = (pulse->period >= 28360 &&
				       (pulse->period <= 28700 ||
					pulse->period >= 76900) &&
				       pulse->period <= 76940);
			break;
		}

		if (pulse->engine > 3)
			break;

		if (pulse->engine == 3) {
			ret = mt76x2_dfs_check_chirp(dev);
			break;
		}

		/* check short pulse*/
		if (pulse->w1 < 120)
			ret = (pulse->period >= 2900 &&
			       (pulse->period <= 4700 ||
				pulse->period >= 6400) &&
			       (pulse->period <= 6800 ||
				pulse->period >= 27560) &&
			       (pulse->period <= 27960 ||
				pulse->period >= 28360) &&
			       (pulse->period <= 28700 ||
				pulse->period >= 79900) &&
			       pulse->period <= 80100);
		else if (pulse->w1 < 130) /* 120 - 130 */
			ret = (pulse->period >= 2900 &&
			       (pulse->period <= 10100 ||
				pulse->period >= 27560) &&
			       (pulse->period <= 27960 ||
				pulse->period >= 28360) &&
			       (pulse->period <= 28700 ||
				pulse->period >= 79900) &&
			       pulse->period <= 80100);
		else
			ret = (pulse->period >= 3900 &&
			       pulse->period <= 10100);
		break;
	case NL80211_DFS_UNSET:
	default:
		return false;
	}

	return ret;
}

static void mt76x2_dfs_tasklet(unsigned long arg)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *)arg;
	struct mt76x2_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	u32 engine_mask;
	int i;

	if (test_bit(MT76_SCANNING, &dev->mt76.state))
		goto out;

	engine_mask = mt76_rr(dev, MT_BBP(DFS, 1));
	if (!(engine_mask & 0xf))
		goto out;

	for (i = 0; i < MT_DFS_NUM_ENGINES; i++) {
		struct mt76x2_dfs_hw_pulse pulse;

		if (!(engine_mask & (1 << i)))
			continue;

		pulse.engine = i;
		mt76x2_dfs_get_hw_pulse(dev, &pulse);

		if (!mt76x2_dfs_check_hw_pulse(dev, &pulse)) {
			dfs_pd->stats[i].hw_pulse_discarded++;
			continue;
		}

		/* hw detector rx radar pattern */
		dfs_pd->stats[i].hw_pattern++;
		ieee80211_radar_detected(dev->mt76.hw);

		/* reset hw detector */
		mt76_wr(dev, MT_BBP(DFS, 1), 0xf);

		return;
	}

	/* reset hw detector */
	mt76_wr(dev, MT_BBP(DFS, 1), 0xf);

out:
	mt76x2_irq_enable(dev, MT_INT_GPTIMER);
}

static void mt76x2_dfs_set_bbp_params(struct mt76x2_dev *dev)
{
	u32 data;
	u8 i, shift;
	const struct mt76x2_radar_specs *radar_specs;

	switch (dev->mt76.chandef.width) {
	case NL80211_CHAN_WIDTH_40:
		shift = MT_DFS_NUM_ENGINES;
		break;
	case NL80211_CHAN_WIDTH_80:
		shift = 2 * MT_DFS_NUM_ENGINES;
		break;
	default:
		shift = 0;
		break;
	}

	switch (dev->dfs_pd.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs[shift];
		break;
	case NL80211_DFS_ETSI:
		radar_specs = &etsi_radar_specs[shift];
		break;
	case NL80211_DFS_JP:
		if (dev->mt76.chandef.chan->center_freq >= 5250 &&
		    dev->mt76.chandef.chan->center_freq <= 5350)
			radar_specs = &jp_w53_radar_specs[shift];
		else
			radar_specs = &jp_w56_radar_specs[shift];
		break;
	case NL80211_DFS_UNSET:
	default:
		return;
	}

	data = (MT_DFS_VGA_MASK << 16) |
	       (MT_DFS_PWR_GAIN_OFFSET << 12) |
	       (MT_DFS_PWR_DOWN_TIME << 8) |
	       (MT_DFS_SYM_ROUND << 4) |
	       (MT_DFS_DELTA_DELAY & 0xf);
	mt76_wr(dev, MT_BBP(DFS, 2), data);

	data = (MT_DFS_RX_PE_MASK << 16) | MT_DFS_PKT_END_MASK;
	mt76_wr(dev, MT_BBP(DFS, 3), data);

	for (i = 0; i < MT_DFS_NUM_ENGINES; i++) {
		/* configure engine */
		mt76_wr(dev, MT_BBP(DFS, 0), i);

		/* detection mode + avg_len */
		data = ((radar_specs[i].avg_len & 0x1ff) << 16) |
		       (radar_specs[i].mode & 0xf);
		mt76_wr(dev, MT_BBP(DFS, 4), data);

		/* dfs energy */
		data = ((radar_specs[i].e_high & 0x0fff) << 16) |
		       (radar_specs[i].e_low & 0x0fff);
		mt76_wr(dev, MT_BBP(DFS, 5), data);

		/* dfs period */
		mt76_wr(dev, MT_BBP(DFS, 7), radar_specs[i].t_low);
		mt76_wr(dev, MT_BBP(DFS, 9), radar_specs[i].t_high);

		/* dfs burst */
		mt76_wr(dev, MT_BBP(DFS, 11), radar_specs[i].b_low);
		mt76_wr(dev, MT_BBP(DFS, 13), radar_specs[i].b_high);

		/* dfs width */
		data = ((radar_specs[i].w_high & 0x0fff) << 16) |
		       (radar_specs[i].w_low & 0x0fff);
		mt76_wr(dev, MT_BBP(DFS, 14), data);

		/* dfs margins */
		data = (radar_specs[i].w_margin << 16) |
		       radar_specs[i].t_margin;
		mt76_wr(dev, MT_BBP(DFS, 15), data);

		/* dfs event expiration */
		mt76_wr(dev, MT_BBP(DFS, 17), radar_specs[i].event_expiration);

		/* dfs pwr adj */
		mt76_wr(dev, MT_BBP(DFS, 30), radar_specs[i].pwr_jmp);
	}

	/* reset status */
	mt76_wr(dev, MT_BBP(DFS, 1), 0xf);
	mt76_wr(dev, MT_BBP(DFS, 36), 0x3);

	/* enable detection*/
	mt76_wr(dev, MT_BBP(DFS, 0), MT_DFS_CH_EN << 16);
	mt76_wr(dev, 0x212c, 0x0c350001);
}

void mt76x2_dfs_adjust_agc(struct mt76x2_dev *dev)
{
	u32 agc_r8, agc_r4, val_r8, val_r4, dfs_r31;

	agc_r8 = mt76_rr(dev, MT_BBP(AGC, 8));
	agc_r4 = mt76_rr(dev, MT_BBP(AGC, 4));

	val_r8 = (agc_r8 & 0x00007e00) >> 9;
	val_r4 = agc_r4 & ~0x1f000000;
	val_r4 += (((val_r8 + 1) >> 1) << 24);
	mt76_wr(dev, MT_BBP(AGC, 4), val_r4);

	dfs_r31 = FIELD_GET(MT_BBP_AGC_LNA_HIGH_GAIN, val_r4);
	dfs_r31 += val_r8;
	dfs_r31 -= (agc_r8 & 0x00000038) >> 3;
	dfs_r31 = (dfs_r31 << 16) | 0x00000307;
	mt76_wr(dev, MT_BBP(DFS, 31), dfs_r31);

	mt76_wr(dev, MT_BBP(DFS, 32), 0x00040071);
}

void mt76x2_dfs_init_params(struct mt76x2_dev *dev)
{
	struct cfg80211_chan_def *chandef = &dev->mt76.chandef;

	if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
	    dev->dfs_pd.region != NL80211_DFS_UNSET) {
		mt76x2_dfs_set_bbp_params(dev);
		/* enable debug mode */
		mt76x2_dfs_set_capture_mode_ctrl(dev, true);

		mt76x2_irq_enable(dev, MT_INT_GPTIMER);
		mt76_rmw_field(dev, MT_INT_TIMER_EN,
			       MT_INT_TIMER_EN_GP_TIMER_EN, 1);
	} else {
		/* disable hw detector */
		mt76_wr(dev, MT_BBP(DFS, 0), 0);
		/* clear detector status */
		mt76_wr(dev, MT_BBP(DFS, 1), 0xf);
		mt76_wr(dev, 0x212c, 0);

		mt76x2_irq_disable(dev, MT_INT_GPTIMER);
		mt76_rmw_field(dev, MT_INT_TIMER_EN,
			       MT_INT_TIMER_EN_GP_TIMER_EN, 0);
	}
}

void mt76x2_dfs_init_detector(struct mt76x2_dev *dev)
{
	struct mt76x2_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	dfs_pd->region = NL80211_DFS_UNSET;
	tasklet_init(&dfs_pd->dfs_tasklet, mt76x2_dfs_tasklet,
		     (unsigned long)dev);
}

void mt76x2_dfs_set_domain(struct mt76x2_dev *dev,
			   enum nl80211_dfs_regions region)
{
	struct mt76x2_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	if (dfs_pd->region != region) {
		tasklet_disable(&dfs_pd->dfs_tasklet);
		dfs_pd->region = region;
		mt76x2_dfs_init_params(dev);
		tasklet_enable(&dfs_pd->dfs_tasklet);
	}
}

