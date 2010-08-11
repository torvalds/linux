/*
 * arch/arm/mach-tegra/include/mach/dc.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_DC_H
#define __MACH_TEGRA_DC_H


#define TEGRA_MAX_DC		2
#define DC_N_WINDOWS		3

struct tegra_dc_blend {
	u32	nokey;
	u32	one_win;
	u32	two_win_x;
	u32	two_win_y;
	u32	three_win_xy;
};

#define BLEND(key, control, weight0, weight1)				\
	(CKEY_ ## key | BLEND_CONTROL_ ## control |			\
	 BLEND_WEIGHT0(weight0) | BLEND_WEIGHT0(weight1))

struct tegra_dc_mode {
	int	pclk;
	int	h_ref_to_sync;
	int	v_ref_to_sync;
	int	h_sync_width;
	int	v_sync_width;
	int	h_back_porch;
	int	v_back_porch;
	int	h_active;
	int	v_active;
	int	h_front_porch;
	int	v_front_porch;
};

enum {
	TEGRA_DC_OUT_RGB,
};

struct tegra_dc_out {
	int			type;

	unsigned		order;
	unsigned		align;

	struct tegra_dc_mode	*modes;
	int			n_modes;
};

#define TEGRA_DC_ALIGN_MSB		0
#define TEGRA_DC_ALIGN_LSB		1

#define TEGRA_DC_ORDER_RED_BLUE		0
#define TEGRA_DC_ORDER_BLUE_RED		1

struct tegra_dc;

struct tegra_dc_win {
	u8			idx;
	u8			fmt;
	u32			flags;

	void			*virt_addr;
	dma_addr_t		phys_addr;
	unsigned		x;
	unsigned		y;
	unsigned		w;
	unsigned		h;
	unsigned		out_w;
	unsigned		out_h;

	int			dirty;
	struct tegra_dc		*dc;
};

#define TEGRA_WIN_FLAG_ENABLED		(1 << 0)
#define TEGRA_WIN_FLAG_COLOR_EXPAND	(1 << 1)

/* Note: These are the actual values written to the DC_WIN_COLOR_DEPTH register
 * and may change in new tegra architectures.
 */
#define TEGRA_WIN_FMT_P1		0
#define TEGRA_WIN_FMT_P2		1
#define TEGRA_WIN_FMT_P4		2
#define TEGRA_WIN_FMT_P8		3
#define TEGRA_WIN_FMT_B4G4R4A4		4
#define TEGRA_WIN_FMT_B5G5R5A		5
#define TEGRA_WIN_FMT_B5G6R5		6
#define TEGRA_WIN_FMT_AB5G5R5		7
#define TEGRA_WIN_FMT_B8G8R8A8		12
#define TEGRA_WIN_FMT_R8G8B8A8		13
#define TEGRA_WIN_FMT_B6x2G6x2R6x2A8	14
#define TEGRA_WIN_FMT_R6x2G6x2B6x2A8	15
#define TEGRA_WIN_FMT_YCbCr422		16
#define TEGRA_WIN_FMT_YUV422		17
#define TEGRA_WIN_FMT_YCbCr420P		18
#define TEGRA_WIN_FMT_YUV420P		19
#define TEGRA_WIN_FMT_YCbCr422P		20
#define TEGRA_WIN_FMT_YUV422P		21
#define TEGRA_WIN_FMT_YCbCr422R		22
#define TEGRA_WIN_FMT_YUV422R		23
#define TEGRA_WIN_FMT_YCbCr422RA	24
#define TEGRA_WIN_FMT_YUV422RA		25

struct tegra_fb_data {
	int		win;

	int		xres;
	int		yres;
	int		bits_per_pixel;
};

struct tegra_dc_platform_data {
	unsigned long		flags;
	struct tegra_dc_out	*default_out;
	struct tegra_fb_data	*fb;
};

#define TEGRA_DC_FLAG_ENABLED		(1 << 0)

struct tegra_dc *tegra_dc_get_dc(unsigned idx);
struct tegra_dc_win *tegra_dc_get_window(struct tegra_dc *dc, unsigned win);

/* tegra_dc_update_windows and tegra_dc_sync_windows do not support windows
 * with differenct dcs in one call
 */
int tegra_dc_update_windows(struct tegra_dc_win *windows[], int n);
int tegra_dc_sync_windows(struct tegra_dc_win *windows[], int n);

/* will probably be replaced with an interface describing the window order */
void tegra_dc_set_blending(struct tegra_dc *dc, struct tegra_dc_blend *blend);

int tegra_dc_set_mode(struct tegra_dc *dc, struct tegra_dc_mode *mode);

#endif
