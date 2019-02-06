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

#include "mt76x02.h"

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

static const struct mt76x02_radar_specs etsi_radar_specs[] = {
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

static const struct mt76x02_radar_specs fcc_radar_specs[] = {
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

static const struct mt76x02_radar_specs jp_w56_radar_specs[] = {
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

static const struct mt76x02_radar_specs jp_w53_radar_specs[] = {
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

static void
mt76x02_dfs_set_capture_mode_ctrl(struct mt76x02_dev *dev, u8 enable)
{
	u32 data;

	data = (1 << 1) | enable;
	mt76_wr(dev, MT_BBP(DFS, 36), data);
}

static void mt76x02_dfs_seq_pool_put(struct mt76x02_dev *dev,
				     struct mt76x02_dfs_sequence *seq)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	list_add(&seq->head, &dfs_pd->seq_pool);

	dfs_pd->seq_stats.seq_pool_len++;
	dfs_pd->seq_stats.seq_len--;
}

static struct mt76x02_dfs_sequence *
mt76x02_dfs_seq_pool_get(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_sequence *seq;

	if (list_empty(&dfs_pd->seq_pool)) {
		seq = devm_kzalloc(dev->mt76.dev, sizeof(*seq), GFP_ATOMIC);
	} else {
		seq = list_first_entry(&dfs_pd->seq_pool,
				       struct mt76x02_dfs_sequence,
				       head);
		list_del(&seq->head);
		dfs_pd->seq_stats.seq_pool_len--;
	}
	if (seq)
		dfs_pd->seq_stats.seq_len++;

	return seq;
}

static int mt76x02_dfs_get_multiple(int val, int frac, int margin)
{
	int remainder, factor;

	if (!frac)
		return 0;

	if (abs(val - frac) <= margin)
		return 1;

	factor = val / frac;
	remainder = val % frac;

	if (remainder > margin) {
		if ((frac - remainder) <= margin)
			factor++;
		else
			factor = 0;
	}
	return factor;
}

static void mt76x02_dfs_detector_reset(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_sequence *seq, *tmp_seq;
	int i;

	/* reset hw detector */
	mt76_wr(dev, MT_BBP(DFS, 1), 0xf);

	/* reset sw detector */
	for (i = 0; i < ARRAY_SIZE(dfs_pd->event_rb); i++) {
		dfs_pd->event_rb[i].h_rb = 0;
		dfs_pd->event_rb[i].t_rb = 0;
	}

	list_for_each_entry_safe(seq, tmp_seq, &dfs_pd->sequences, head) {
		list_del_init(&seq->head);
		mt76x02_dfs_seq_pool_put(dev, seq);
	}
}

static bool mt76x02_dfs_check_chirp(struct mt76x02_dev *dev)
{
	bool ret = false;
	u32 current_ts, delta_ts;
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

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

static void mt76x02_dfs_get_hw_pulse(struct mt76x02_dev *dev,
				     struct mt76x02_dfs_hw_pulse *pulse)
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

static bool mt76x02_dfs_check_hw_pulse(struct mt76x02_dev *dev,
				       struct mt76x02_dfs_hw_pulse *pulse)
{
	bool ret = false;

	if (!pulse->period || !pulse->w1)
		return false;

	switch (dev->dfs_pd.region) {
	case NL80211_DFS_FCC:
		if (pulse->engine > 3)
			break;

		if (pulse->engine == 3) {
			ret = mt76x02_dfs_check_chirp(dev);
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
			ret = mt76x02_dfs_check_chirp(dev);
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

static bool mt76x02_dfs_fetch_event(struct mt76x02_dev *dev,
				    struct mt76x02_dfs_event *event)
{
	u32 data;

	/* 1st: DFS_R37[31]: 0 (engine 0) - 1 (engine 2)
	 * 2nd: DFS_R37[21:0]: pulse time
	 * 3rd: DFS_R37[11:0]: pulse width
	 * 3rd: DFS_R37[25:16]: phase
	 * 4th: DFS_R37[12:0]: current pwr
	 * 4th: DFS_R37[21:16]: pwr stable counter
	 *
	 * 1st: DFS_R37[31:0] set to 0xffffffff means no event detected
	 */
	data = mt76_rr(dev, MT_BBP(DFS, 37));
	if (!MT_DFS_CHECK_EVENT(data))
		return false;

	event->engine = MT_DFS_EVENT_ENGINE(data);
	data = mt76_rr(dev, MT_BBP(DFS, 37));
	event->ts = MT_DFS_EVENT_TIMESTAMP(data);
	data = mt76_rr(dev, MT_BBP(DFS, 37));
	event->width = MT_DFS_EVENT_WIDTH(data);

	return true;
}

static bool mt76x02_dfs_check_event(struct mt76x02_dev *dev,
				    struct mt76x02_dfs_event *event)
{
	if (event->engine == 2) {
		struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
		struct mt76x02_dfs_event_rb *event_buff = &dfs_pd->event_rb[1];
		u16 last_event_idx;
		u32 delta_ts;

		last_event_idx = mt76_decr(event_buff->t_rb,
					   MT_DFS_EVENT_BUFLEN);
		delta_ts = event->ts - event_buff->data[last_event_idx].ts;
		if (delta_ts < MT_DFS_EVENT_TIME_MARGIN &&
		    event_buff->data[last_event_idx].width >= 200)
			return false;
	}
	return true;
}

static void mt76x02_dfs_queue_event(struct mt76x02_dev *dev,
				    struct mt76x02_dfs_event *event)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_event_rb *event_buff;

	/* add radar event to ring buffer */
	event_buff = event->engine == 2 ? &dfs_pd->event_rb[1]
					: &dfs_pd->event_rb[0];
	event_buff->data[event_buff->t_rb] = *event;
	event_buff->data[event_buff->t_rb].fetch_ts = jiffies;

	event_buff->t_rb = mt76_incr(event_buff->t_rb, MT_DFS_EVENT_BUFLEN);
	if (event_buff->t_rb == event_buff->h_rb)
		event_buff->h_rb = mt76_incr(event_buff->h_rb,
					     MT_DFS_EVENT_BUFLEN);
}

static int mt76x02_dfs_create_sequence(struct mt76x02_dev *dev,
				       struct mt76x02_dfs_event *event,
				       u16 cur_len)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_sw_detector_params *sw_params;
	u32 width_delta, with_sum, factor, cur_pri;
	struct mt76x02_dfs_sequence seq, *seq_p;
	struct mt76x02_dfs_event_rb *event_rb;
	struct mt76x02_dfs_event *cur_event;
	int i, j, end, pri;

	event_rb = event->engine == 2 ? &dfs_pd->event_rb[1]
				      : &dfs_pd->event_rb[0];

	i = mt76_decr(event_rb->t_rb, MT_DFS_EVENT_BUFLEN);
	end = mt76_decr(event_rb->h_rb, MT_DFS_EVENT_BUFLEN);

	while (i != end) {
		cur_event = &event_rb->data[i];
		with_sum = event->width + cur_event->width;

		sw_params = &dfs_pd->sw_dpd_params;
		switch (dev->dfs_pd.region) {
		case NL80211_DFS_FCC:
		case NL80211_DFS_JP:
			if (with_sum < 600)
				width_delta = 8;
			else
				width_delta = with_sum >> 3;
			break;
		case NL80211_DFS_ETSI:
			if (event->engine == 2)
				width_delta = with_sum >> 6;
			else if (with_sum < 620)
				width_delta = 24;
			else
				width_delta = 8;
			break;
		case NL80211_DFS_UNSET:
		default:
			return -EINVAL;
		}

		pri = event->ts - cur_event->ts;
		if (abs(event->width - cur_event->width) > width_delta ||
		    pri < sw_params->min_pri)
			goto next;

		if (pri > sw_params->max_pri)
			break;

		seq.pri = event->ts - cur_event->ts;
		seq.first_ts = cur_event->ts;
		seq.last_ts = event->ts;
		seq.engine = event->engine;
		seq.count = 2;

		j = mt76_decr(i, MT_DFS_EVENT_BUFLEN);
		while (j != end) {
			cur_event = &event_rb->data[j];
			cur_pri = event->ts - cur_event->ts;
			factor = mt76x02_dfs_get_multiple(cur_pri, seq.pri,
						sw_params->pri_margin);
			if (factor > 0) {
				seq.first_ts = cur_event->ts;
				seq.count++;
			}

			j = mt76_decr(j, MT_DFS_EVENT_BUFLEN);
		}
		if (seq.count <= cur_len)
			goto next;

		seq_p = mt76x02_dfs_seq_pool_get(dev);
		if (!seq_p)
			return -ENOMEM;

		*seq_p = seq;
		INIT_LIST_HEAD(&seq_p->head);
		list_add(&seq_p->head, &dfs_pd->sequences);
next:
		i = mt76_decr(i, MT_DFS_EVENT_BUFLEN);
	}
	return 0;
}

static u16 mt76x02_dfs_add_event_to_sequence(struct mt76x02_dev *dev,
					     struct mt76x02_dfs_event *event)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_sw_detector_params *sw_params;
	struct mt76x02_dfs_sequence *seq, *tmp_seq;
	u16 max_seq_len = 0;
	u32 factor, pri;

	sw_params = &dfs_pd->sw_dpd_params;
	list_for_each_entry_safe(seq, tmp_seq, &dfs_pd->sequences, head) {
		if (event->ts > seq->first_ts + MT_DFS_SEQUENCE_WINDOW) {
			list_del_init(&seq->head);
			mt76x02_dfs_seq_pool_put(dev, seq);
			continue;
		}

		if (event->engine != seq->engine)
			continue;

		pri = event->ts - seq->last_ts;
		factor = mt76x02_dfs_get_multiple(pri, seq->pri,
						  sw_params->pri_margin);
		if (factor > 0) {
			seq->last_ts = event->ts;
			seq->count++;
			max_seq_len = max_t(u16, max_seq_len, seq->count);
		}
	}
	return max_seq_len;
}

static bool mt76x02_dfs_check_detection(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_sequence *seq;

	if (list_empty(&dfs_pd->sequences))
		return false;

	list_for_each_entry(seq, &dfs_pd->sequences, head) {
		if (seq->count > MT_DFS_SEQUENCE_TH) {
			dfs_pd->stats[seq->engine].sw_pattern++;
			return true;
		}
	}
	return false;
}

static void mt76x02_dfs_add_events(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_event event;
	int i, seq_len;

	/* disable debug mode */
	mt76x02_dfs_set_capture_mode_ctrl(dev, false);
	for (i = 0; i < MT_DFS_EVENT_LOOP; i++) {
		if (!mt76x02_dfs_fetch_event(dev, &event))
			break;

		if (dfs_pd->last_event_ts > event.ts)
			mt76x02_dfs_detector_reset(dev);
		dfs_pd->last_event_ts = event.ts;

		if (!mt76x02_dfs_check_event(dev, &event))
			continue;

		seq_len = mt76x02_dfs_add_event_to_sequence(dev, &event);
		mt76x02_dfs_create_sequence(dev, &event, seq_len);

		mt76x02_dfs_queue_event(dev, &event);
	}
	mt76x02_dfs_set_capture_mode_ctrl(dev, true);
}

static void mt76x02_dfs_check_event_window(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	struct mt76x02_dfs_event_rb *event_buff;
	struct mt76x02_dfs_event *event;
	int i;

	for (i = 0; i < ARRAY_SIZE(dfs_pd->event_rb); i++) {
		event_buff = &dfs_pd->event_rb[i];

		while (event_buff->h_rb != event_buff->t_rb) {
			event = &event_buff->data[event_buff->h_rb];

			/* sorted list */
			if (time_is_after_jiffies(event->fetch_ts +
						  MT_DFS_EVENT_WINDOW))
				break;
			event_buff->h_rb = mt76_incr(event_buff->h_rb,
						     MT_DFS_EVENT_BUFLEN);
		}
	}
}

static void mt76x02_dfs_tasklet(unsigned long arg)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)arg;
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;
	u32 engine_mask;
	int i;

	if (test_bit(MT76_SCANNING, &dev->mt76.state))
		goto out;

	if (time_is_before_jiffies(dfs_pd->last_sw_check +
				   MT_DFS_SW_TIMEOUT)) {
		bool radar_detected;

		dfs_pd->last_sw_check = jiffies;

		mt76x02_dfs_add_events(dev);
		radar_detected = mt76x02_dfs_check_detection(dev);
		if (radar_detected) {
			/* sw detector rx radar pattern */
			ieee80211_radar_detected(dev->mt76.hw);
			mt76x02_dfs_detector_reset(dev);

			return;
		}
		mt76x02_dfs_check_event_window(dev);
	}

	engine_mask = mt76_rr(dev, MT_BBP(DFS, 1));
	if (!(engine_mask & 0xf))
		goto out;

	for (i = 0; i < MT_DFS_NUM_ENGINES; i++) {
		struct mt76x02_dfs_hw_pulse pulse;

		if (!(engine_mask & (1 << i)))
			continue;

		pulse.engine = i;
		mt76x02_dfs_get_hw_pulse(dev, &pulse);

		if (!mt76x02_dfs_check_hw_pulse(dev, &pulse)) {
			dfs_pd->stats[i].hw_pulse_discarded++;
			continue;
		}

		/* hw detector rx radar pattern */
		dfs_pd->stats[i].hw_pattern++;
		ieee80211_radar_detected(dev->mt76.hw);
		mt76x02_dfs_detector_reset(dev);

		return;
	}

	/* reset hw detector */
	mt76_wr(dev, MT_BBP(DFS, 1), 0xf);

out:
	mt76x02_irq_enable(dev, MT_INT_GPTIMER);
}

static void mt76x02_dfs_init_sw_detector(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	switch (dev->dfs_pd.region) {
	case NL80211_DFS_FCC:
		dfs_pd->sw_dpd_params.max_pri = MT_DFS_FCC_MAX_PRI;
		dfs_pd->sw_dpd_params.min_pri = MT_DFS_FCC_MIN_PRI;
		dfs_pd->sw_dpd_params.pri_margin = MT_DFS_PRI_MARGIN;
		break;
	case NL80211_DFS_ETSI:
		dfs_pd->sw_dpd_params.max_pri = MT_DFS_ETSI_MAX_PRI;
		dfs_pd->sw_dpd_params.min_pri = MT_DFS_ETSI_MIN_PRI;
		dfs_pd->sw_dpd_params.pri_margin = MT_DFS_PRI_MARGIN << 2;
		break;
	case NL80211_DFS_JP:
		dfs_pd->sw_dpd_params.max_pri = MT_DFS_JP_MAX_PRI;
		dfs_pd->sw_dpd_params.min_pri = MT_DFS_JP_MIN_PRI;
		dfs_pd->sw_dpd_params.pri_margin = MT_DFS_PRI_MARGIN;
		break;
	case NL80211_DFS_UNSET:
	default:
		break;
	}
}

static void mt76x02_dfs_set_bbp_params(struct mt76x02_dev *dev)
{
	const struct mt76x02_radar_specs *radar_specs;
	u8 i, shift;
	u32 data;

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
	mt76_wr(dev, MT_BBP(IBI, 11), 0x0c350001);
}

void mt76x02_phy_dfs_adjust_agc(struct mt76x02_dev *dev)
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

	if (is_mt76x2(dev)) {
		mt76_wr(dev, MT_BBP(DFS, 32), 0x00040071);
	} else {
		/* disable hw detector */
		mt76_wr(dev, MT_BBP(DFS, 0), 0);
		/* enable hw detector */
		mt76_wr(dev, MT_BBP(DFS, 0), MT_DFS_CH_EN << 16);
	}
}
EXPORT_SYMBOL_GPL(mt76x02_phy_dfs_adjust_agc);

void mt76x02_dfs_init_params(struct mt76x02_dev *dev)
{
	struct cfg80211_chan_def *chandef = &dev->mt76.chandef;

	if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
	    dev->dfs_pd.region != NL80211_DFS_UNSET) {
		mt76x02_dfs_init_sw_detector(dev);
		mt76x02_dfs_set_bbp_params(dev);
		/* enable debug mode */
		mt76x02_dfs_set_capture_mode_ctrl(dev, true);

		mt76x02_irq_enable(dev, MT_INT_GPTIMER);
		mt76_rmw_field(dev, MT_INT_TIMER_EN,
			       MT_INT_TIMER_EN_GP_TIMER_EN, 1);
	} else {
		/* disable hw detector */
		mt76_wr(dev, MT_BBP(DFS, 0), 0);
		/* clear detector status */
		mt76_wr(dev, MT_BBP(DFS, 1), 0xf);
		if (mt76_chip(&dev->mt76) == 0x7610 ||
		    mt76_chip(&dev->mt76) == 0x7630)
			mt76_wr(dev, MT_BBP(IBI, 11), 0xfde8081);
		else
			mt76_wr(dev, MT_BBP(IBI, 11), 0);

		mt76x02_irq_disable(dev, MT_INT_GPTIMER);
		mt76_rmw_field(dev, MT_INT_TIMER_EN,
			       MT_INT_TIMER_EN_GP_TIMER_EN, 0);
	}
}
EXPORT_SYMBOL_GPL(mt76x02_dfs_init_params);

void mt76x02_dfs_init_detector(struct mt76x02_dev *dev)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	INIT_LIST_HEAD(&dfs_pd->sequences);
	INIT_LIST_HEAD(&dfs_pd->seq_pool);
	dfs_pd->region = NL80211_DFS_UNSET;
	dfs_pd->last_sw_check = jiffies;
	tasklet_init(&dfs_pd->dfs_tasklet, mt76x02_dfs_tasklet,
		     (unsigned long)dev);
}

static void
mt76x02_dfs_set_domain(struct mt76x02_dev *dev,
		       enum nl80211_dfs_regions region)
{
	struct mt76x02_dfs_pattern_detector *dfs_pd = &dev->dfs_pd;

	mutex_lock(&dev->mt76.mutex);
	if (dfs_pd->region != region) {
		tasklet_disable(&dfs_pd->dfs_tasklet);

		dev->ed_monitor = region == NL80211_DFS_ETSI;
		mt76x02_edcca_init(dev);

		dfs_pd->region = region;
		mt76x02_dfs_init_params(dev);
		tasklet_enable(&dfs_pd->dfs_tasklet);
	}
	mutex_unlock(&dev->mt76.mutex);
}

void mt76x02_regd_notifier(struct wiphy *wiphy,
			   struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt76x02_dev *dev = hw->priv;

	mt76x02_dfs_set_domain(dev, request->dfs_region);
}
