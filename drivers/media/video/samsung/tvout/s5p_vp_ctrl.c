/* linux/drivers/media/video/samsung/tvout/s5p_vp_ctrl.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Control class functions for S5P video processor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "hw_if/hw_if.h"
#include "s5p_tvout_ctrl.h"

#if defined(CONFIG_BUSFREQ)
#include <mach/cpufreq.h>
#endif

#define INTERLACED	0
#define PROGRESSIVE	1

struct s5p_vp_ctrl_op_mode {
	bool	ipc;
	bool	line_skip;
	bool	auto_toggling;
};

struct s5p_vp_ctrl_bc_line_eq {
	enum s5p_vp_line_eq	eq_num;
	u32			intc;
	u32			slope;
};

struct s5p_vp_ctrl_rect {
	u32 x;
	u32 y;
	u32 w;
	u32 h;
};

struct s5p_vp_ctrl_plane {
	u32			top_y_addr;
	u32			top_c_addr;
	u32			w;
	u32			h;

	enum s5p_vp_src_color	color_t;
	enum s5p_vp_field	field_id;
	enum s5p_vp_mem_type	mem_type;
	enum s5p_vp_mem_mode	mem_mode;
};

struct s5p_vp_ctrl_pp_param {
	bool				bypass;

	bool csc_en;
	enum s5p_vp_csc_type		csc_t;
	bool				csc_default_coef;
	bool				csc_sub_y_offset_en;

	u32				saturation;
	u8				contrast;
	bool				brightness;
	u32				bright_offset;
	struct s5p_vp_ctrl_bc_line_eq	bc_line_eq[8];

	/* sharpness */
	u32				th_hnoise;
	enum s5p_vp_sharpness_control	sharpness;


	bool				default_poly_filter;

	enum s5p_vp_chroma_expansion	chroma_exp;
};

struct s5p_vp_ctrl_mixer_param {
	bool	blend;
	u32	alpha;
	u32	prio;
};

struct s5p_vp_ctrl_private_data {
	struct s5p_vp_ctrl_plane	src_plane;
	struct s5p_vp_ctrl_rect		src_win;

	struct s5p_vp_ctrl_rect		dst_win;
	struct s5p_vp_ctrl_op_mode	op_mode;

	struct s5p_vp_ctrl_pp_param	pp_param;
	struct s5p_vp_ctrl_mixer_param	mixer_param;

	bool				running;

	struct reg_mem_info		reg_mem;

	struct s5p_tvout_clk_info	clk;
	char				*pow_name;

	struct device *dev;
};

static struct s5p_vp_ctrl_private_data s5p_vp_ctrl_private = {
	.reg_mem = {
		.name			= "s5p-vp",
		.res			= NULL,
		.base			= NULL
	},

	.clk = {
		.name			= "vp",
		.ptr			= NULL
	},

	.pow_name			= "vp_pd",

	.src_plane = {
		.field_id		= VP_TOP_FIELD,
	},

	.pp_param = {
		.default_poly_filter	= true,
		.bypass			= false,

		.saturation		= 0x80,
		.brightness		= 0x00,
		.bright_offset		= 0x00,
		.contrast		= 0x80,

		.th_hnoise		= 0,
		.sharpness		= VP_SHARPNESS_NO,

		.chroma_exp		= 0,

		.csc_en			= false,
		.csc_default_coef	= true,
		.csc_sub_y_offset_en	= false,
	},

	.running			= false
};

extern int s5p_vp_get_top_field_address(u32* top_y_addr, u32* top_c_addr);

static u8 s5p_vp_ctrl_get_src_scan_mode(void)
{
	struct s5p_vp_ctrl_plane *src_plane = &s5p_vp_ctrl_private.src_plane;
	u8 ret = PROGRESSIVE;

	if (src_plane->color_t == VP_SRC_COLOR_NV12IW ||
		src_plane->color_t == VP_SRC_COLOR_TILE_NV12IW ||
		src_plane->color_t == VP_SRC_COLOR_NV21IW ||
		src_plane->color_t == VP_SRC_COLOR_TILE_NV21IW)
		ret = INTERLACED;

	return ret;
}

static u8 s5p_vp_ctrl_get_dest_scan_mode(
		enum s5p_tvout_disp_mode display, enum s5p_tvout_o_mode out)
{
	u8 ret = PROGRESSIVE;

	switch (out) {
	case TVOUT_COMPOSITE:
		ret = INTERLACED;
		break;

	case TVOUT_HDMI_RGB:
	case TVOUT_HDMI:
	case TVOUT_DVI:
		if (display == TVOUT_1080I_60 ||
		   display == TVOUT_1080I_59 ||
		   display == TVOUT_1080I_50)
			ret = INTERLACED;
		break;

	default:
		break;
	}

	return ret;
}

static void s5p_vp_ctrl_set_src_dst_win(
		struct s5p_vp_ctrl_rect	src_win,
		struct s5p_vp_ctrl_rect	dst_win,
		enum s5p_tvout_disp_mode	disp,
		enum s5p_tvout_o_mode	out,
		enum s5p_vp_src_color	color_t,
		bool ipc)
{
	struct s5p_vp_ctrl_op_mode *op_mode = &s5p_vp_ctrl_private.op_mode;

	if (s5p_vp_ctrl_get_dest_scan_mode(disp, out) == INTERLACED) {
		if (op_mode->line_skip) {
			src_win.y /= 2;
			src_win.h /= 2;
		}

		dst_win.y /= 2;
		dst_win.h /= 2;
	} else if (s5p_vp_ctrl_get_src_scan_mode() == INTERLACED) {
		src_win.y /= 2;
		src_win.h /= 2;
	}

	s5p_vp_set_src_position(src_win.x, 0, src_win.y);
	s5p_vp_set_dest_position(dst_win.x, dst_win.y);
	s5p_vp_set_src_dest_size(
		src_win.w, src_win.h, dst_win.w, dst_win.h, ipc);
}

int s5p_vp_ctrl_get_src_addr(u32* top_y_addr, u32* top_c_addr)
{
	if (s5p_vp_ctrl_private.running)
		s5p_vp_get_top_field_address(top_y_addr, top_c_addr);
	else {
		*top_y_addr = 0;
		*top_c_addr = 0;
	}

	return 0;
}

static int s5p_vp_ctrl_set_src_addr(
		u32 top_y_addr, u32 top_c_addr,
		u32 img_w, enum s5p_vp_src_color color_t)
{
	if (s5p_vp_set_top_field_address(top_y_addr, top_c_addr))
		return -1;

	if (s5p_vp_ctrl_get_src_scan_mode() == INTERLACED) {
		u32	bot_y = 0;
		u32	bot_c = 0;

		if (color_t == VP_SRC_COLOR_NV12IW ||
				color_t == VP_SRC_COLOR_NV21IW) {
			bot_y = top_y_addr + img_w;
			bot_c = top_c_addr + img_w;
		} else if (color_t == VP_SRC_COLOR_TILE_NV12IW ||
				color_t == VP_SRC_COLOR_TILE_NV21IW) {
			bot_y = top_y_addr + 0x40;
			bot_c = top_c_addr + 0x40;
		}

		if (s5p_vp_set_bottom_field_address(bot_y, bot_c))
			return -1;
	}

	return 0;
}

static void s5p_vp_ctrl_init_private(void)
{
	int i;
	struct s5p_vp_ctrl_pp_param *pp_param = &s5p_vp_ctrl_private.pp_param;

	for (i = 0; i < 8; i++)
		pp_param->bc_line_eq[i].eq_num = VP_LINE_EQ_DEFAULT;
}

static int s5p_vp_ctrl_set_reg(void)
{
	int i;
	int ret = 0;

	enum s5p_tvout_disp_mode	tv_std;
	enum s5p_tvout_o_mode		tv_if;

	struct s5p_vp_ctrl_plane *src_plane = &s5p_vp_ctrl_private.src_plane;
	struct s5p_vp_ctrl_pp_param *pp_param = &s5p_vp_ctrl_private.pp_param;
	struct s5p_vp_ctrl_op_mode *op_mode = &s5p_vp_ctrl_private.op_mode;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
	} else
#endif
	{
		s5p_tvif_ctrl_get_std_if(&tv_std, &tv_if);

		s5p_vp_sw_reset();

		s5p_vp_set_endian(TVOUT_BIG_ENDIAN);

		s5p_vp_set_op_mode(
			op_mode->line_skip, src_plane->mem_type,
			src_plane->mem_mode, pp_param->chroma_exp,
			op_mode->auto_toggling);

		s5p_vp_set_field_id(src_plane->field_id);

		s5p_vp_set_img_size(src_plane->w, src_plane->h);

		s5p_vp_ctrl_set_src_addr(
			src_plane->top_y_addr, src_plane->top_c_addr,
			src_plane->w, src_plane->color_t);

		s5p_vp_ctrl_set_src_dst_win(
			s5p_vp_ctrl_private.src_win,
			s5p_vp_ctrl_private.dst_win,
			tv_std,
			tv_if,
			s5p_vp_ctrl_private.src_plane.color_t,
			op_mode->ipc);

		if (pp_param->default_poly_filter)
			s5p_vp_set_poly_filter_coef_default(
				s5p_vp_ctrl_private.src_win.w,
				s5p_vp_ctrl_private.src_win.h,
				s5p_vp_ctrl_private.dst_win.w,
				s5p_vp_ctrl_private.dst_win.h,
				op_mode->ipc);

		s5p_vp_set_bypass_post_process(pp_param->bypass);
		s5p_vp_set_sharpness(pp_param->th_hnoise, pp_param->sharpness);
		s5p_vp_set_saturation(pp_param->saturation);
		s5p_vp_set_brightness_contrast(
				pp_param->brightness, pp_param->contrast);

		for (i = VP_LINE_EQ_0; i <= VP_LINE_EQ_7; i++) {
			if (pp_param->bc_line_eq[i].eq_num == i)
				ret = s5p_vp_set_brightness_contrast_control(
					pp_param->bc_line_eq[i].eq_num,
					pp_param->bc_line_eq[i].intc,
					pp_param->bc_line_eq[i].slope);

			if (ret != 0)
				return -1;
		}

		s5p_vp_set_brightness_offset(pp_param->bright_offset);

		s5p_vp_set_csc_control(
				pp_param->csc_sub_y_offset_en,
				pp_param->csc_en);

		if (pp_param->csc_en && pp_param->csc_default_coef) {
			if (s5p_vp_set_csc_coef_default(pp_param->csc_t))
				return -1;
		}

		if (s5p_vp_start())
			return -1;

	}

	s5p_mixer_ctrl_enable_layer(MIXER_VIDEO_LAYER);

	mdelay(50);

	return 0;
}

static void s5p_vp_ctrl_internal_stop(void)
{
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
	} else
#endif
		s5p_vp_stop();

	s5p_mixer_ctrl_disable_layer(MIXER_VIDEO_LAYER);
}

static void s5p_vp_ctrl_clock(bool on)
{
	if (on) {
#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_get();
#endif
		clk_enable(s5p_vp_ctrl_private.clk.ptr);
		// Restore vp_base address
		s5p_vp_init(s5p_vp_ctrl_private.reg_mem.base);

	} else {
		clk_disable(s5p_vp_ctrl_private.clk.ptr);
#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_put();
#endif
		// Set vp_base to NULL
		s5p_vp_init(NULL);
	}
}



void s5p_vp_ctrl_set_src_plane(
		u32 base_y, u32 base_c,	u32 width, u32 height,
		enum s5p_vp_src_color color, enum s5p_vp_field field)
{
	struct s5p_vp_ctrl_plane *src_plane = &s5p_vp_ctrl_private.src_plane;

	src_plane->color_t	= color;
	src_plane->field_id	= field;

	src_plane->top_y_addr	= base_y;
	src_plane->top_c_addr	= base_c;

	src_plane->w		= width;
	src_plane->h		= height;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	if (s5p_vp_ctrl_private.running) {
		s5p_vp_set_img_size(width, height);

		s5p_vp_set_field_id(field);
		s5p_vp_ctrl_set_src_addr(base_y, base_c, width, color);

		s5p_vp_update();
	}
}

void s5p_vp_ctrl_set_src_win(u32 left, u32 top, u32 width, u32 height)
{
	struct s5p_vp_ctrl_rect *src_win = &s5p_vp_ctrl_private.src_win;

	src_win->x = left;
	src_win->y = top;
	src_win->w = width;
	src_win->h = height;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	if (s5p_vp_ctrl_private.running) {
		enum s5p_tvout_disp_mode	tv_std;
		enum s5p_tvout_o_mode		tv_if;

		s5p_tvif_ctrl_get_std_if(&tv_std, &tv_if);

		s5p_vp_ctrl_set_src_dst_win(
			*src_win,
			s5p_vp_ctrl_private.dst_win,
			tv_std,
			tv_if,
			s5p_vp_ctrl_private.src_plane.color_t,
			s5p_vp_ctrl_private.op_mode.ipc);

		s5p_vp_update();
	}
}

void s5p_vp_ctrl_set_dest_win(u32 left, u32 top, u32 width, u32 height)
{
	struct s5p_vp_ctrl_rect *dst_win = &s5p_vp_ctrl_private.dst_win;

	dst_win->x = left;
	dst_win->y = top;
	dst_win->w = width;
	dst_win->h = height;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	if (s5p_vp_ctrl_private.running) {
		enum s5p_tvout_disp_mode	tv_std;
		enum s5p_tvout_o_mode		tv_if;

		s5p_tvif_ctrl_get_std_if(&tv_std, &tv_if);

		s5p_vp_ctrl_set_src_dst_win(
			s5p_vp_ctrl_private.src_win,
			*dst_win,
			tv_std,
			tv_if,
			s5p_vp_ctrl_private.src_plane.color_t,
			s5p_vp_ctrl_private.op_mode.ipc);

		s5p_vp_update();
	}
}

void s5p_vp_ctrl_set_dest_win_alpha_val(u32 alpha)
{
	s5p_vp_ctrl_private.mixer_param.alpha = alpha;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	s5p_mixer_ctrl_set_alpha(MIXER_VIDEO_LAYER, alpha);
}

void s5p_vp_ctrl_set_dest_win_blend(bool enable)
{
	s5p_vp_ctrl_private.mixer_param.blend = enable;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	s5p_mixer_ctrl_set_blend_mode(MIXER_VIDEO_LAYER,
			LAYER_BLENDING);
}

void s5p_vp_ctrl_set_dest_win_priority(u32 prio)
{
	s5p_vp_ctrl_private.mixer_param.prio = prio;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	}
#endif
	s5p_mixer_ctrl_set_priority(MIXER_VIDEO_LAYER, prio);
}

void s5p_vp_ctrl_stop(void)
{
	if (s5p_vp_ctrl_private.running) {
		s5p_vp_ctrl_internal_stop();
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
		} else
#endif
		{
			s5p_vp_ctrl_clock(0);
		}

		s5p_vp_ctrl_private.running = false;
#if defined(CONFIG_BUSFREQ) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
		exynos4_busfreq_lock_free(DVFS_LOCK_ID_TV);
#endif
	}
}

int s5p_vp_ctrl_start(void)
{
	struct s5p_vp_ctrl_plane *src_plane = &s5p_vp_ctrl_private.src_plane;
	enum s5p_tvout_disp_mode disp;
	enum s5p_tvout_o_mode out;

	struct s5p_vp_ctrl_rect *src_win = &s5p_vp_ctrl_private.src_win;
	struct s5p_vp_ctrl_rect *dst_win = &s5p_vp_ctrl_private.dst_win;

	bool i_mode, o_mode; /* 0 for interlaced, 1 for progressive */

	s5p_tvif_ctrl_get_std_if(&disp, &out);

	switch (disp) {
	case TVOUT_480P_60_16_9:
	case TVOUT_480P_60_4_3:
	case TVOUT_576P_50_16_9:
	case TVOUT_576P_50_4_3:
	case TVOUT_480P_59:
		s5p_vp_ctrl_private.pp_param.csc_t = VP_CSC_SD_HD;
		break;

	case TVOUT_1080I_50:
	case TVOUT_1080I_60:
	case TVOUT_1080P_50:
	case TVOUT_1080P_30:
	case TVOUT_1080P_60:
	case TVOUT_720P_59:
	case TVOUT_1080I_59:
	case TVOUT_1080P_59:
	case TVOUT_720P_50:
	case TVOUT_720P_60:
		s5p_vp_ctrl_private.pp_param.csc_t = VP_CSC_HD_SD;
		break;
#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_720P_60_SBS_HALF:
	case TVOUT_720P_59_SBS_HALF:
	case TVOUT_720P_50_TB:
	case TVOUT_1080P_24_TB:
	case TVOUT_1080P_23_TB:
		s5p_vp_ctrl_private.pp_param.csc_t = VP_CSC_HD_SD;
		break;

#endif

	default:
		break;
	}

	i_mode = s5p_vp_ctrl_get_src_scan_mode();
	o_mode = s5p_vp_ctrl_get_dest_scan_mode(disp, out);

	/* check o_mode */
	if (i_mode == INTERLACED) {
		if (o_mode == INTERLACED) {
			/* i to i : line skip 1, ipc 0, auto toggle 0 */
			s5p_vp_ctrl_private.op_mode.line_skip		= true;
			s5p_vp_ctrl_private.op_mode.ipc			= false;
			s5p_vp_ctrl_private.op_mode.auto_toggling	= false;
		} else {
			/* i to p : line skip 1, ipc 1, auto toggle 0 */
			s5p_vp_ctrl_private.op_mode.line_skip		= true;
			s5p_vp_ctrl_private.op_mode.ipc			= true;
			s5p_vp_ctrl_private.op_mode.auto_toggling	= false;
		}
	} else {
		if (o_mode == INTERLACED) {
			/* p to i : line skip 0, ipc 0, auto toggle 0 */
			if (dst_win->h > src_win->h &&
			   ((dst_win->h << 16)/src_win->h < 0x100000))
				s5p_vp_ctrl_private.op_mode.line_skip	= false;
			/* p to i : line skip 1, ipc 0, auto toggle 0 */
			else
				s5p_vp_ctrl_private.op_mode.line_skip	= true;
			s5p_vp_ctrl_private.op_mode.ipc			= false;
			s5p_vp_ctrl_private.op_mode.auto_toggling	= false;
		} else {
			/* p to p : line skip 0, ipc 0, auto toggle 0 */
			s5p_vp_ctrl_private.op_mode.line_skip		= false;
			s5p_vp_ctrl_private.op_mode.ipc			= false;
			s5p_vp_ctrl_private.op_mode.auto_toggling	= false;
		}
	}
	src_plane->mem_type
		= ((src_plane->color_t == VP_SRC_COLOR_NV12) ||
			(src_plane->color_t == VP_SRC_COLOR_NV12IW) ||
			(src_plane->color_t == VP_SRC_COLOR_TILE_NV12) ||
			(src_plane->color_t == VP_SRC_COLOR_TILE_NV12IW)) ?
			VP_YUV420_NV12 : VP_YUV420_NV21;

	src_plane->mem_mode
		= ((src_plane->color_t == VP_SRC_COLOR_NV12) ||
			(src_plane->color_t == VP_SRC_COLOR_NV12IW) ||
			(src_plane->color_t == VP_SRC_COLOR_NV21) ||
			(src_plane->color_t == VP_SRC_COLOR_NV21IW)) ?
			VP_LINEAR_MODE : VP_2D_TILE_MODE;

	if (s5p_vp_ctrl_private.running)
		s5p_vp_ctrl_internal_stop();
	else {
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
		} else
#endif
		{
#if defined(CONFIG_BUSFREQ) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
			if ((disp == TVOUT_1080P_60) || (disp == TVOUT_1080P_59)
					|| (disp == TVOUT_1080P_50)) {
				if (exynos4_busfreq_lock(DVFS_LOCK_ID_TV, BUS_L0))
					printk(KERN_ERR "%s: failed lock DVFS\n", __func__);
			}
#endif
			s5p_vp_ctrl_clock(1);
		}
		s5p_vp_ctrl_private.running = true;
	}
	s5p_vp_ctrl_set_reg();

	return 0;
}

int s5p_vp_ctrl_constructor(struct platform_device *pdev)
{
	int ret = 0;

	ret = s5p_tvout_map_resource_mem(
		pdev,
		s5p_vp_ctrl_private.reg_mem.name,
		&(s5p_vp_ctrl_private.reg_mem.base),
		&(s5p_vp_ctrl_private.reg_mem.res));

	if (ret)
		goto err_on_res;

	s5p_vp_ctrl_private.clk.ptr =
		clk_get(&pdev->dev, s5p_vp_ctrl_private.clk.name);

	if (IS_ERR(s5p_vp_ctrl_private.clk.ptr)) {
		tvout_err("Failed to find clock %s\n",
			s5p_vp_ctrl_private.clk.name);
		ret = -ENOENT;
		goto err_on_clk;
	}

	s5p_vp_init(s5p_vp_ctrl_private.reg_mem.base);
	s5p_vp_ctrl_init_private();

	return 0;

err_on_clk:
	iounmap(s5p_vp_ctrl_private.reg_mem.base);
	release_resource(s5p_vp_ctrl_private.reg_mem.res);
	kfree(s5p_vp_ctrl_private.reg_mem.res);

err_on_res:
	return ret;
}

void s5p_vp_ctrl_destructor(void)
{
	if (s5p_vp_ctrl_private.reg_mem.base)
		iounmap(s5p_vp_ctrl_private.reg_mem.base);

	if (s5p_vp_ctrl_private.reg_mem.res) {
		release_resource(s5p_vp_ctrl_private.reg_mem.res);
		kfree(s5p_vp_ctrl_private.reg_mem.res);
	}

	if (s5p_vp_ctrl_private.clk.ptr) {
		if (s5p_vp_ctrl_private.running)
			clk_disable(s5p_vp_ctrl_private.clk.ptr);
		clk_put(s5p_vp_ctrl_private.clk.ptr);
	}
}

void s5p_vp_ctrl_suspend(void)
{
	tvout_dbg("running(%d)\n", s5p_vp_ctrl_private.running);
	if (s5p_vp_ctrl_private.running) {
		s5p_vp_stop();
		s5p_vp_ctrl_clock(0);
	}
}

void s5p_vp_ctrl_resume(void)
{
	tvout_dbg("running(%d)\n", s5p_vp_ctrl_private.running);
	if (s5p_vp_ctrl_private.running) {
		s5p_vp_ctrl_clock(1);
		s5p_vp_ctrl_set_reg();
	}
}
