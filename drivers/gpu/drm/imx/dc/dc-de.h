/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_DISPLAY_ENGINE_H__
#define __DC_DISPLAY_ENGINE_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <drm/drm_modes.h>

#define DC_DISPLAYS	2

#define DC_FRAMEGEN_MAX_FRAME_INDEX	0x3ffff
#define DC_FRAMEGEN_MAX_CLOCK_KHZ	300000

struct dc_fg {
	struct device *dev;
	struct regmap *reg;
	struct clk *clk_disp;
};

struct dc_tc {
	struct device *dev;
	struct regmap *reg;
};

struct dc_de {
	struct device *dev;
	struct regmap *reg_top;
	struct dc_fg *fg;
	struct dc_tc *tc;
	int irq_shdload;
	int irq_framecomplete;
	int irq_seqcomplete;
};

/* Frame Generator Unit */
void dc_fg_cfg_videomode(struct dc_fg *fg, struct drm_display_mode *m);
void dc_fg_enable(struct dc_fg *fg);
void dc_fg_disable(struct dc_fg *fg);
void dc_fg_shdtokgen(struct dc_fg *fg);
u32 dc_fg_get_frame_index(struct dc_fg *fg);
u32 dc_fg_get_line_index(struct dc_fg *fg);
bool dc_fg_wait_for_frame_index_moving(struct dc_fg *fg);
bool dc_fg_secondary_requests_to_read_empty_fifo(struct dc_fg *fg);
void dc_fg_secondary_clear_channel_status(struct dc_fg *fg);
int dc_fg_wait_for_secondary_syncup(struct dc_fg *fg);
void dc_fg_enable_clock(struct dc_fg *fg);
void dc_fg_disable_clock(struct dc_fg *fg);
enum drm_mode_status dc_fg_check_clock(struct dc_fg *fg, int clk_khz);
void dc_fg_init(struct dc_fg *fg);

/* Timing Controller Unit */
void dc_tc_init(struct dc_tc *tc);

#endif /* __DC_DISPLAY_ENGINE_H__ */
