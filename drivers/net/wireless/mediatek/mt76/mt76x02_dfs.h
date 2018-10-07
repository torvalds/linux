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

#ifndef __MT76x02_DFS_H
#define __MT76x02_DFS_H

#include <linux/types.h>
#include <linux/nl80211.h>

#define MT_DFS_GP_INTERVAL		(10 << 4) /* 64 us unit */
#define MT_DFS_NUM_ENGINES		4

/* bbp params */
#define MT_DFS_SYM_ROUND		0
#define MT_DFS_DELTA_DELAY		2
#define MT_DFS_VGA_MASK			0
#define MT_DFS_PWR_GAIN_OFFSET		3
#define MT_DFS_PWR_DOWN_TIME		0xf
#define MT_DFS_RX_PE_MASK		0xff
#define MT_DFS_PKT_END_MASK		0
#define MT_DFS_CH_EN			0xf

/* sw detector params */
#define MT_DFS_EVENT_LOOP		64
#define MT_DFS_SW_TIMEOUT		(HZ / 20)
#define MT_DFS_EVENT_WINDOW		(HZ / 5)
#define MT_DFS_SEQUENCE_WINDOW		(200 * (1 << 20))
#define MT_DFS_EVENT_TIME_MARGIN	2000
#define MT_DFS_PRI_MARGIN		4
#define MT_DFS_SEQUENCE_TH		6

#define MT_DFS_FCC_MAX_PRI		((28570 << 1) + 1000)
#define MT_DFS_FCC_MIN_PRI		(3000 - 2)
#define MT_DFS_JP_MAX_PRI		((80000 << 1) + 1000)
#define MT_DFS_JP_MIN_PRI		(28500 - 2)
#define MT_DFS_ETSI_MAX_PRI		(133333 + 125000 + 117647 + 1000)
#define MT_DFS_ETSI_MIN_PRI		(4500 - 20)

struct mt76x02_radar_specs {
	u8 mode;
	u16 avg_len;
	u16 e_low;
	u16 e_high;
	u16 w_low;
	u16 w_high;
	u16 w_margin;
	u32 t_low;
	u32 t_high;
	u16 t_margin;
	u32 b_low;
	u32 b_high;
	u32 event_expiration;
	u16 pwr_jmp;
};

#define MT_DFS_CHECK_EVENT(x)		((x) != GENMASK(31, 0))
#define MT_DFS_EVENT_ENGINE(x)		(((x) & BIT(31)) ? 2 : 0)
#define MT_DFS_EVENT_TIMESTAMP(x)	((x) & GENMASK(21, 0))
#define MT_DFS_EVENT_WIDTH(x)		((x) & GENMASK(11, 0))
struct mt76x02_dfs_event {
	unsigned long fetch_ts;
	u32 ts;
	u16 width;
	u8 engine;
};

#define MT_DFS_EVENT_BUFLEN		256
struct mt76x02_dfs_event_rb {
	struct mt76x02_dfs_event data[MT_DFS_EVENT_BUFLEN];
	int h_rb, t_rb;
};

struct mt76x02_dfs_sequence {
	struct list_head head;
	u32 first_ts;
	u32 last_ts;
	u32 pri;
	u16 count;
	u8 engine;
};

struct mt76x02_dfs_hw_pulse {
	u8 engine;
	u32 period;
	u32 w1;
	u32 w2;
	u32 burst;
};

struct mt76x02_dfs_sw_detector_params {
	u32 min_pri;
	u32 max_pri;
	u32 pri_margin;
};

struct mt76x02_dfs_engine_stats {
	u32 hw_pattern;
	u32 hw_pulse_discarded;
	u32 sw_pattern;
};

struct mt76x02_dfs_seq_stats {
	u32 seq_pool_len;
	u32 seq_len;
};

struct mt76x02_dfs_pattern_detector {
	enum nl80211_dfs_regions region;

	u8 chirp_pulse_cnt;
	u32 chirp_pulse_ts;

	struct mt76x02_dfs_sw_detector_params sw_dpd_params;
	struct mt76x02_dfs_event_rb event_rb[2];

	struct list_head sequences;
	struct list_head seq_pool;
	struct mt76x02_dfs_seq_stats seq_stats;

	unsigned long last_sw_check;
	u32 last_event_ts;

	struct mt76x02_dfs_engine_stats stats[MT_DFS_NUM_ENGINES];
	struct tasklet_struct dfs_tasklet;
};

#endif /* __MT76x02_DFS_H */
