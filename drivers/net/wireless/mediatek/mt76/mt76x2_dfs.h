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

#ifndef __MT76x2_DFS_H
#define __MT76x2_DFS_H

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

struct mt76x2_radar_specs {
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

struct mt76x2_dfs_hw_pulse {
	u8 engine;
	u32 period;
	u32 w1;
	u32 w2;
	u32 burst;
};

struct mt76x2_dfs_engine_stats {
	u32 hw_pattern;
	u32 hw_pulse_discarded;
};

struct mt76x2_dfs_pattern_detector {
	enum nl80211_dfs_regions region;

	u8 chirp_pulse_cnt;
	u32 chirp_pulse_ts;

	struct mt76x2_dfs_engine_stats stats[MT_DFS_NUM_ENGINES];
	struct tasklet_struct dfs_tasklet;
};

void mt76x2_dfs_init_params(struct mt76x2_dev *dev);
void mt76x2_dfs_init_detector(struct mt76x2_dev *dev);
void mt76x2_dfs_adjust_agc(struct mt76x2_dev *dev);
void mt76x2_dfs_set_domain(struct mt76x2_dev *dev,
			   enum nl80211_dfs_regions region);

#endif /* __MT76x2_DFS_H */
