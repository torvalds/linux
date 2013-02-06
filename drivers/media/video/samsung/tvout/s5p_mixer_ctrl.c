/* linux/drivers/media/video/samsung/tvout/s5p_mixer_ctrl.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Functions of mixer ctrl class for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include <plat/clock.h>

#include "hw_if/hw_if.h"
#include "s5p_tvout_ctrl.h"

enum {
	ACLK = 0,
	MUX,
	NO_OF_CLK
};

struct s5p_bg_color {
	u32 color_y;
	u32 color_cb;
	u32 color_cr;
};

struct s5p_mixer_video_layer_info {
	bool layer_blend;
	u32 alpha;
	u32 priority;
	u32 y_min;
	u32 y_max;
	u32 c_min;
	u32 c_max;

	bool use_video_layer;
};

struct s5p_mixer_grp_layer_info {
	bool pixel_blend;
	bool layer_blend;
	u32 alpha;

	bool chroma_enable;
	u32 chroma_key;

	bool pre_mul_mode;

	u32 src_x;
	u32 src_y;
	u32 dst_x;
	u32 dst_y;
	u32 width;
	u32 height;
	dma_addr_t fb_addr;

	bool use_grp_layer;

	u32 priority;
	enum s5p_mixer_color_fmt format;

	enum s5ptvfb_ver_scaling_t ver;
	enum s5ptvfb_hor_scaling_t hor;
};

struct s5p_mixer_ctrl_private_data {
	char			*pow_name;
	struct s5p_tvout_clk_info	clk[NO_OF_CLK];
	struct irq_info		irq;
	struct reg_mem_info	reg_mem;

	enum s5p_mixer_burst_mode	burst;
	enum s5p_tvout_endian		endian;
	struct s5p_bg_color		bg_color[3];

	struct s5p_mixer_video_layer_info	v_layer;
	struct s5p_mixer_grp_layer_info		layer[S5PTV_FB_CNT];

	bool running;
	bool vsync_interrupt_enable;
};

static struct s5p_mixer_ctrl_private_data s5p_mixer_ctrl_private = {
	.pow_name		= "mixer_pd",
	.clk[ACLK] = {
		.name		= "mixer",
		.ptr		= NULL
	},
	.clk[MUX] = {
		.name		= "sclk_mixer",
		.ptr		= NULL
	},
	.irq = {
		.name		= "s5p-mixer",
		.handler	= s5p_mixer_irq,
		.no		= -1
	},
	.reg_mem = {
		.name = "s5p-mixer",
		.res = NULL,
		.base = NULL
	},

	.burst = MIXER_BURST_16,
	.endian = TVOUT_LITTLE_ENDIAN,
	.bg_color[0].color_y = 16,
	.bg_color[0].color_cb = 128,
	.bg_color[0].color_cr = 128,
	.bg_color[1].color_y = 16,
	.bg_color[1].color_cb = 128,
	.bg_color[1].color_cr = 128,
	.bg_color[2].color_y = 16,
	.bg_color[2].color_cb = 128,
	.bg_color[2].color_cr = 128,

	.v_layer = {
		.layer_blend = false,
		.alpha = 0xff,
		.priority = 10,
		.y_min = 0x10,
		.y_max = 0xeb,
		.c_min = 0x10,
		.c_max = 0xf0,
	},
	.layer[MIXER_GPR0_LAYER] = {
		.pixel_blend = false,
		.layer_blend = false,
		.alpha = 0xff,
		.chroma_enable = false,
		.chroma_key = 0x0,
		.pre_mul_mode = false,
		.src_x = 0,
		.src_y = 0,
		.dst_x = 0,
		.dst_y = 0,
		.width = 0,
		.height = 0,
		.priority = 10,
		.format = MIXER_RGB8888,
		.ver = VERTICAL_X1,
		.hor = HORIZONTAL_X1
	},
	.layer[MIXER_GPR1_LAYER] = {
		.pixel_blend = false,
		.layer_blend = false,
		.alpha = 0xff,
		.chroma_enable = false,
		.chroma_key = 0x0,
		.pre_mul_mode = false,
		.src_x = 0,
		.src_y = 0,
		.dst_x = 0,
		.dst_y = 0,
		.width = 0,
		.height = 0,
		.priority = 10,
		.format = MIXER_RGB8888,
		.ver = VERTICAL_X1,
		.hor = HORIZONTAL_X1
	},

	.running = false,
	.vsync_interrupt_enable = false,
};

static int s5p_mixer_ctrl_set_reg(enum s5p_mixer_layer layer)
{
	bool layer_blend;
	u32 alpha;
	u32 priority;
	struct s5ptvfb_user_scaling scaling;

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		layer_blend = s5p_mixer_ctrl_private.v_layer.layer_blend;
		alpha = s5p_mixer_ctrl_private.v_layer.alpha;
		priority = s5p_mixer_ctrl_private.v_layer.priority;
		break;
	case MIXER_GPR0_LAYER:
	case MIXER_GPR1_LAYER:
		layer_blend = s5p_mixer_ctrl_private.layer[layer].layer_blend;
		alpha = s5p_mixer_ctrl_private.layer[layer].alpha;
		priority = s5p_mixer_ctrl_private.layer[layer].priority;

		s5p_mixer_set_pre_mul_mode(layer,
			s5p_mixer_ctrl_private.layer[layer].pre_mul_mode);
		s5p_mixer_set_chroma_key(layer,
			s5p_mixer_ctrl_private.layer[layer].chroma_enable,
			s5p_mixer_ctrl_private.layer[layer].chroma_key);
		s5p_mixer_set_grp_layer_dst_pos(layer,
			s5p_mixer_ctrl_private.layer[layer].dst_x,
			s5p_mixer_ctrl_private.layer[layer].dst_y);

		scaling.ver = s5p_mixer_ctrl_private.layer[layer].ver;
		scaling.hor = s5p_mixer_ctrl_private.layer[layer].hor;
		s5p_mixer_scaling(layer, scaling);
		s5p_mixer_set_grp_base_address(layer,
			s5p_mixer_ctrl_private.layer[layer].fb_addr);

		s5p_mixer_set_color_format(layer,
			s5p_mixer_ctrl_private.layer[layer].format);

		s5p_mixer_set_grp_layer_src_pos(layer,
			s5p_mixer_ctrl_private.layer[layer].src_x,
			s5p_mixer_ctrl_private.layer[layer].src_y,
			s5p_mixer_ctrl_private.layer[layer].width,
			s5p_mixer_ctrl_private.layer[layer].width,
			s5p_mixer_ctrl_private.layer[layer].height);

		s5p_mixer_set_pixel_blend(layer,
			s5p_mixer_ctrl_private.layer[layer].pixel_blend);
		break;
	default:
		tvout_err("invalid layer\n");
		return -1;
	}

	s5p_mixer_set_layer_blend(layer, layer_blend);
	s5p_mixer_set_alpha(layer, alpha);
	s5p_mixer_set_priority(layer, priority);

	return 0;
}

static void s5p_mixer_ctrl_clock(bool on)
{
	/* power control function is not implemented yet */
	if (on) {
		clk_enable(s5p_mixer_ctrl_private.clk[MUX].ptr);
#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_get();
#endif

		clk_enable(s5p_mixer_ctrl_private.clk[ACLK].ptr);

		/* Restore mixer_base address */
		s5p_mixer_init(s5p_mixer_ctrl_private.reg_mem.base);
	} else {
		clk_disable(s5p_mixer_ctrl_private.clk[ACLK].ptr);

#ifdef CONFIG_ARCH_EXYNOS4
		s5p_tvout_pm_runtime_put();
#endif

		clk_disable(s5p_mixer_ctrl_private.clk[MUX].ptr);

		/* Set mixer_base address to NULL */
		s5p_mixer_init(NULL);
	}
}

void s5p_mixer_ctrl_init_fb_addr_phy(enum s5p_mixer_layer layer,
				dma_addr_t fb_addr)
{
	s5p_mixer_ctrl_private.layer[layer].fb_addr = fb_addr;
}

void s5p_mixer_ctrl_init_grp_layer(enum s5p_mixer_layer layer)
{
	struct s5ptvfb_user_scaling scaling;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return;
	} else
#endif
	{
		if (s5p_mixer_ctrl_private.running) {
			s5p_mixer_set_priority(layer,
				s5p_mixer_ctrl_private.layer[layer].priority);
			s5p_mixer_set_pre_mul_mode(layer,
				s5p_mixer_ctrl_private.layer[layer].
				pre_mul_mode);
			s5p_mixer_set_chroma_key(layer,
				s5p_mixer_ctrl_private.layer[layer].
				chroma_enable,
				s5p_mixer_ctrl_private.layer[layer].
				chroma_key);
			s5p_mixer_set_layer_blend(layer,
				s5p_mixer_ctrl_private.layer[layer].
				layer_blend);
			s5p_mixer_set_alpha(layer,
				s5p_mixer_ctrl_private.layer[layer].alpha);
			s5p_mixer_set_grp_layer_dst_pos(layer,
				s5p_mixer_ctrl_private.layer[layer].dst_x,
				s5p_mixer_ctrl_private.layer[layer].dst_y);

			scaling.ver = s5p_mixer_ctrl_private.layer[layer].ver;
			scaling.hor = s5p_mixer_ctrl_private.layer[layer].hor;
			s5p_mixer_scaling(layer, scaling);
			s5p_mixer_set_grp_base_address(layer,
				s5p_mixer_ctrl_private.layer[layer].fb_addr);
		}
	}
}

int s5p_mixer_ctrl_set_pixel_format(
	enum s5p_mixer_layer layer, u32 bpp, u32 trans_len)
{
	enum s5p_mixer_color_fmt format;

	switch (bpp) {
	case 16:
		if (trans_len == 1)
			format = MIXER_RGB1555;
		else if (trans_len == 4)
			format = MIXER_RGB4444;
		else
			format = MIXER_RGB565;
		break;
	case 32:
		format = MIXER_RGB8888;
		break;
	default:
		tvout_err("invalid bits per pixel\n");
		return -1;
	}

	s5p_mixer_ctrl_private.layer[layer].format = format;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	} else
#endif
	{
		if (s5p_mixer_ctrl_private.running)
			s5p_mixer_set_color_format(layer, format);

	}

	return 0;
}

int s5p_mixer_ctrl_enable_layer(enum s5p_mixer_layer layer)
{
	switch (layer) {
	case MIXER_VIDEO_LAYER:
		s5p_mixer_ctrl_private.v_layer.use_video_layer = true;
		break;
	case MIXER_GPR0_LAYER:
	case MIXER_GPR1_LAYER:
		s5p_mixer_ctrl_private.layer[layer].use_grp_layer = true;
		break;
	default:
		tvout_err("invalid layer\n");
		return -1;
	}
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif

	if (s5p_mixer_ctrl_private.running) {
		s5p_mixer_ctrl_set_reg(layer);

		s5p_mixer_set_show(layer, true);
	}

	return 0;
}

int s5p_mixer_ctrl_disable_layer(enum s5p_mixer_layer layer)
{
	bool use_vid, use_grp0, use_grp1;

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		s5p_mixer_ctrl_private.v_layer.use_video_layer = false;
		break;
	case MIXER_GPR0_LAYER:
	case MIXER_GPR1_LAYER:
		s5p_mixer_ctrl_private.layer[layer].use_grp_layer = false;
		break;
	default:
		tvout_err("invalid layer\n");
		return -1;
	}

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif

	use_vid = s5p_mixer_ctrl_private.v_layer.use_video_layer;
	use_grp0 = s5p_mixer_ctrl_private.layer[MIXER_GPR0_LAYER].use_grp_layer;
	use_grp1 = s5p_mixer_ctrl_private.layer[MIXER_GPR1_LAYER].use_grp_layer;

	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_show(layer, false);

	return 0;
}

int s5p_mixer_ctrl_set_priority(enum s5p_mixer_layer layer, u32 prio)
{
	if (prio > 15) {
		tvout_err("layer priority range : 0 - 15\n");
		return -1;
	}

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		s5p_mixer_ctrl_private.v_layer.priority = prio;
		break;
	case MIXER_GPR0_LAYER:
	case MIXER_GPR1_LAYER:
		s5p_mixer_ctrl_private.layer[layer].priority = prio;
		break;
	default:
		tvout_err("invalid layer\n");
		return -1;
	}

	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_priority(layer, prio);

	return 0;
}

int s5p_mixer_ctrl_set_dst_win_pos(enum s5p_mixer_layer layer,
				int dst_x, int dst_y, u32 w, u32 h)
{
	u32 w_t, h_t;
	enum s5p_tvout_disp_mode std;
	enum s5p_tvout_o_mode inf;

	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	s5p_tvif_ctrl_get_std_if(&std, &inf);
	tvout_dbg("standard no = %d, output mode no = %d\n", std, inf);

	/*
	 * When tvout resolution was overscanned, there is no
	 * adjust method in H/W. So, framebuffer should be resized.
	 * In this case - TV w/h is greater than FB w/h, grp layer's
	 * dst offset must be changed to fix tv screen.
	 */

	switch (std) {
	case TVOUT_NTSC_M:
	case TVOUT_480P_60_16_9:
	case TVOUT_480P_60_4_3:
	case TVOUT_480P_59:
		w_t = 720;
		h_t = 480;
		break;

	case TVOUT_576P_50_16_9:
	case TVOUT_576P_50_4_3:
		w_t = 720;
		h_t = 576;
		break;

	case TVOUT_720P_60:
	case TVOUT_720P_59:
	case TVOUT_720P_50:
		w_t = 1280;
		h_t = 720;
		break;

	case TVOUT_1080I_60:
	case TVOUT_1080I_59:
	case TVOUT_1080I_50:
	case TVOUT_1080P_60:
	case TVOUT_1080P_59:
	case TVOUT_1080P_50:
	case TVOUT_1080P_30:
		w_t = 1920;
		h_t = 1080;
		break;

#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_720P_60_SBS_HALF:
	case TVOUT_720P_59_SBS_HALF:
	case TVOUT_720P_50_TB:
		w_t = 1280;
		h_t = 720;
		break;

	case TVOUT_1080P_24_TB:
	case TVOUT_1080P_23_TB:
		w_t = 1920;
		h_t = 1080;
		break;

#endif
	default:
		w_t = 0;
		h_t = 0;
		break;
	}

	if (dst_x < 0)
		dst_x = 0;

	if (dst_y < 0)
		dst_y = 0;

	if (dst_x + w > w_t)
		dst_x = w_t - w;

	if (dst_y + h > h_t)
		dst_y = h_t - h;

	tvout_dbg("destination coordinates : x = %d, y = %d\n",
			dst_x, dst_y);
	tvout_dbg("output device screen size : width = %d, height = %d",
			w_t, h_t);

	s5p_mixer_ctrl_private.layer[layer].dst_x = (u32)dst_x;
	s5p_mixer_ctrl_private.layer[layer].dst_y = (u32)dst_y;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_grp_layer_dst_pos(layer, (u32)dst_x, (u32)dst_y);

	return 0;
}

int s5p_mixer_ctrl_set_src_win_pos(enum s5p_mixer_layer layer,
				u32 src_x, u32 src_y, u32 w, u32 h)
{
	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	tvout_dbg("source coordinates : x = %d, y = %d\n", src_x, src_y);
	tvout_dbg("source size : width = %d, height = %d\n", w, h);

	s5p_mixer_ctrl_private.layer[layer].src_x = src_x;
	s5p_mixer_ctrl_private.layer[layer].src_y = src_y;
	s5p_mixer_ctrl_private.layer[layer].width = w;
	s5p_mixer_ctrl_private.layer[layer].height = h;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	} else
#endif
	{
		if (s5p_mixer_ctrl_private.running)
			s5p_mixer_set_grp_layer_src_pos(
				layer, src_x, src_y, w, w, h);
	}

	return 0;
}

int s5p_mixer_ctrl_set_buffer_address(enum s5p_mixer_layer layer,
				dma_addr_t start_addr)
{
	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	tvout_dbg("TV frame buffer base address = 0x%x\n", start_addr);

	s5p_mixer_ctrl_private.layer[layer].fb_addr = start_addr;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_grp_base_address(layer, start_addr);

	return 0;
}

int s5p_mixer_ctrl_set_chroma_key(enum s5p_mixer_layer layer,
				struct s5ptvfb_chroma chroma)
{
	bool enabled = (chroma.enabled) ? true : false;

	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	s5p_mixer_ctrl_private.layer[layer].chroma_enable = enabled;
	s5p_mixer_ctrl_private.layer[layer].chroma_key = chroma.key;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_chroma_key(layer, enabled, chroma.key);

	return 0;
}

int s5p_mixer_ctrl_set_alpha(enum s5p_mixer_layer layer, u32 alpha)
{
	switch (layer) {
	case MIXER_VIDEO_LAYER:
		s5p_mixer_ctrl_private.v_layer.alpha = alpha;
		break;
	case MIXER_GPR0_LAYER:
	case MIXER_GPR1_LAYER:
		s5p_mixer_ctrl_private.layer[layer].alpha = alpha;
		break;
	default:
		tvout_err("invalid layer\n");
		return -1;
	}

	tvout_dbg("alpha value = 0x%x\n", alpha);

	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_alpha(layer, alpha);

	return 0;
}

int s5p_mixer_ctrl_set_blend_mode(enum s5p_mixer_layer layer,
				enum s5ptvfb_alpha_t mode)
{
	if ((layer != MIXER_VIDEO_LAYER) && (layer != MIXER_GPR0_LAYER) &&
		(layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	if ((layer == MIXER_VIDEO_LAYER) && (mode == PIXEL_BLENDING)) {
		tvout_err("video layer doesn't support pixel blending\n");
		return -1;
	}

	switch (mode) {
	case PIXEL_BLENDING:
		tvout_dbg("pixel blending\n");
		s5p_mixer_ctrl_private.layer[layer].pixel_blend = true;

		if (s5p_mixer_ctrl_private.running)
			s5p_mixer_set_pixel_blend(layer, true);
		break;

	case LAYER_BLENDING:
		tvout_dbg("layer blending\n");
		if (layer == MIXER_VIDEO_LAYER)
			s5p_mixer_ctrl_private.v_layer.layer_blend = true;
		else /* graphic layer */
			s5p_mixer_ctrl_private.layer[layer].layer_blend = true;

		if (s5p_mixer_ctrl_private.running)
			s5p_mixer_set_layer_blend(layer, true);
		break;

	case NONE_BLENDING:
		tvout_dbg("alpha blending off\n");
		if (layer == MIXER_VIDEO_LAYER) {
			s5p_mixer_ctrl_private.v_layer.layer_blend = false;

			if (s5p_mixer_ctrl_private.running)
				s5p_mixer_set_layer_blend(layer, false);
		} else { /* graphic layer */
			s5p_mixer_ctrl_private.layer[layer].pixel_blend = false;
			s5p_mixer_ctrl_private.layer[layer].layer_blend = false;

			if (s5p_mixer_ctrl_private.running) {
				s5p_mixer_set_layer_blend(layer, false);
				s5p_mixer_set_pixel_blend(layer, false);
			}
		}
		break;

	default:
		tvout_err("invalid blending mode\n");
		return -1;
	}

	return 0;
}

int s5p_mixer_ctrl_set_alpha_blending(enum s5p_mixer_layer layer,
			enum s5ptvfb_alpha_t blend_mode, unsigned int alpha)
{
	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	switch (blend_mode) {
	case PIXEL_BLENDING:
		tvout_dbg("pixel blending\n");
		s5p_mixer_ctrl_private.layer[layer].pixel_blend = true;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
			return 0;
		}
#endif
		if (s5p_mixer_ctrl_private.running)
			s5p_mixer_set_pixel_blend(layer, true);
		break;

	case LAYER_BLENDING:
		tvout_dbg("layer blending : alpha value = 0x%x\n", alpha);
		s5p_mixer_ctrl_private.layer[layer].layer_blend = true;
		s5p_mixer_ctrl_private.layer[layer].alpha = alpha;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
			return 0;
		}
#endif
		if (s5p_mixer_ctrl_private.running) {
			s5p_mixer_set_layer_blend(layer, true);
			s5p_mixer_set_alpha(layer, alpha);
		}
		break;

	case NONE_BLENDING:
		tvout_dbg("alpha blending off\n");
		s5p_mixer_ctrl_private.layer[layer].pixel_blend = false;
		s5p_mixer_ctrl_private.layer[layer].layer_blend = false;
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
			return 0;
		}
#endif
		if (s5p_mixer_ctrl_private.running) {
			s5p_mixer_set_pixel_blend(layer, false);
			s5p_mixer_set_layer_blend(layer, false);
		}
		break;

	default:
		tvout_err("invalid blending mode\n");
		return -1;
	}

	return 0;
}

int s5p_mixer_ctrl_scaling(enum s5p_mixer_layer layer,
			struct s5ptvfb_user_scaling scaling)
{
	if ((layer != MIXER_GPR0_LAYER) && (layer != MIXER_GPR1_LAYER)) {
		tvout_err("invalid layer\n");
		return -1;
	}

	if ((scaling.ver != VERTICAL_X1) && (scaling.ver != VERTICAL_X2)) {
		tvout_err("invalid vertical size\n");
		return -1;
	}

	if ((scaling.hor != HORIZONTAL_X1) && (scaling.hor != HORIZONTAL_X2)) {
		tvout_err("invalid horizontal size\n");
		return -1;
	}

	s5p_mixer_ctrl_private.layer[layer].ver = scaling.ver;
	s5p_mixer_ctrl_private.layer[layer].hor = scaling.hor;

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	if (suspend_status) {
		tvout_dbg("driver is suspend_status\n");
		return 0;
	}
#endif
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_scaling(layer, scaling);

	return 0;
}

int s5p_mixer_ctrl_mux_clk(struct clk *ptr)
{
	if (clk_set_parent(s5p_mixer_ctrl_private.clk[MUX].ptr, ptr)) {
		tvout_err("unable to set parent %s of clock %s.\n",
				ptr->name,
				s5p_mixer_ctrl_private.clk[MUX].ptr->name);
		return -1;
	}

	return 0;
}

void s5p_mixer_ctrl_set_int_enable(bool en)
{
	tvout_dbg("mixer layers' underflow interrupts are %s, running %d\n",
				en ? "enabled" : "disabled",
				s5p_mixer_ctrl_private.running);

	if (s5p_mixer_ctrl_private.running) {
		s5p_mixer_set_underflow_int_enable(MIXER_VIDEO_LAYER, en);
		s5p_mixer_set_underflow_int_enable(MIXER_GPR0_LAYER, en);
		s5p_mixer_set_underflow_int_enable(MIXER_GPR1_LAYER, en);
	}
}

void s5p_mixer_ctrl_set_vsync_interrupt(bool en)
{
	s5p_mixer_ctrl_private.vsync_interrupt_enable = en;
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_vsync_interrupt(en);
}

bool s5p_mixer_ctrl_get_vsync_interrupt()
{
	return s5p_mixer_ctrl_private.vsync_interrupt_enable;
}

void s5p_mixer_ctrl_disable_vsync_interrupt()
{
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_set_vsync_interrupt(false);
}

void s5p_mixer_ctrl_clear_pend_all(void)
{
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_clear_pend_all();
}

void s5p_mixer_ctrl_stop(void)
{
	int i;

	tvout_dbg("running(%d)\n", s5p_mixer_ctrl_private.running);
	if (s5p_mixer_ctrl_private.running) {
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
		if (suspend_status) {
			tvout_dbg("driver is suspend_status\n");
		} else
#endif
		{
			s5p_mixer_set_vsync_interrupt(false);

			for (i = 0; i < S5PTV_VP_BUFF_CNT-1; i++)
				s5ptv_vp_buff.copy_buff_idxs[i] = i;

			s5ptv_vp_buff.curr_copy_idx = 0;
			s5ptv_vp_buff.vp_access_buff_idx =
				S5PTV_VP_BUFF_CNT - 1;

			s5p_mixer_stop();
			s5p_mixer_ctrl_clock(0);
		}
		s5p_mixer_ctrl_private.running = false;
	}
}

void s5p_mixer_ctrl_internal_start(void)
{
	tvout_dbg("running(%d)\n", s5p_mixer_ctrl_private.running);
	if (s5p_mixer_ctrl_private.running)
		s5p_mixer_start();
}

int s5p_mixer_ctrl_start(
	enum s5p_tvout_disp_mode disp, enum s5p_tvout_o_mode out)
{
	int i;

	int csc = MIXER_RGB601_16_235;
	int csc_for_coeff = MIXER_RGB601_0_255;
	enum s5p_mixer_burst_mode burst = s5p_mixer_ctrl_private.burst;
	enum s5p_tvout_endian endian = s5p_mixer_ctrl_private.endian;
	struct clk *sclk_mixer = s5p_mixer_ctrl_private.clk[MUX].ptr;
	bool mixer_video_limiter = true;

	/*
	 * Getting mega struct member variable will be replaced another tvout
	 * interface
	 */
	struct s5p_tvout_status *st = &s5ptv_status;

	tvout_dbg("running(%d)\n", s5p_mixer_ctrl_private.running);

	switch (out) {
	case TVOUT_COMPOSITE:
		if (clk_set_parent(sclk_mixer, st->sclk_dac)) {
			tvout_err("unable to set parent %s of clock %s.\n",
				   st->sclk_dac->name, sclk_mixer->name);
			return -1;
		}

		if (!s5p_mixer_ctrl_private.running) {
			s5p_mixer_ctrl_clock(true);
			s5p_mixer_ctrl_private.running = true;
		}

		csc = MIXER_RGB601_0_255;
		csc_for_coeff = MIXER_RGB601_0_255;
		break;

	case TVOUT_HDMI_RGB:
	case TVOUT_HDMI:
	case TVOUT_DVI:
		if (clk_set_parent(sclk_mixer, st->sclk_hdmi)) {
			tvout_err("unable to set parent %s of clock %s.\n",
				   st->sclk_hdmi->name, sclk_mixer->name);
			return -1;
		}

		if (clk_set_parent(st->sclk_hdmi, st->sclk_hdmiphy)) {
			tvout_err("unable to set parent %s of clock %s.\n",
				   st->sclk_hdmiphy->name, st->sclk_hdmi->name);
			return -1;
		}

		if (!s5p_mixer_ctrl_private.running) {
			s5p_mixer_ctrl_clock(true);
			s5p_mixer_ctrl_private.running = true;
		}

		switch (disp) {

		case TVOUT_480P_60_4_3:
			if (s5p_tvif_get_q_range() || out == TVOUT_HDMI_RGB)
				csc = MIXER_RGB601_0_255;
			else
				csc = MIXER_RGB601_16_235;
			csc_for_coeff = MIXER_RGB601_0_255;
			break;
		case TVOUT_480P_60_16_9:
		case TVOUT_480P_59:
		case TVOUT_576P_50_16_9:
		case TVOUT_576P_50_4_3:
			if (s5p_tvif_get_q_range() && out != TVOUT_HDMI_RGB)
				csc = MIXER_RGB601_0_255;
			else
				csc = MIXER_RGB601_16_235;
			csc_for_coeff = MIXER_RGB601_0_255;
			break;
		case TVOUT_720P_60:
		case TVOUT_720P_50:
		case TVOUT_720P_59:
		case TVOUT_1080I_60:
		case TVOUT_1080I_59:
		case TVOUT_1080I_50:
		case TVOUT_1080P_60:
		case TVOUT_1080P_30:
		case TVOUT_1080P_59:
		case TVOUT_1080P_50:
			if (!s5p_tvif_get_q_range() || out == TVOUT_HDMI_RGB)
				csc = MIXER_RGB709_16_235;
			else
				csc = MIXER_RGB709_0_255;
			csc_for_coeff = MIXER_RGB709_0_255;
			break;
#ifdef CONFIG_HDMI_14A_3D
		case TVOUT_720P_60_SBS_HALF:
		case TVOUT_720P_59_SBS_HALF:
		case TVOUT_720P_50_TB:
		case TVOUT_1080P_24_TB:
		case TVOUT_1080P_23_TB:
			if (!s5p_tvif_get_q_range() || out == TVOUT_HDMI_RGB)
				csc = MIXER_RGB709_16_235;
			else
				csc = MIXER_RGB709_0_255;
			csc_for_coeff = MIXER_RGB709_0_255;
			break;

#endif
		default:
			break;
		}
		break;

	default:
		tvout_err("invalid tvout output mode = %d\n", out);
		return -1;
	}

	tvout_dbg("%s burst mode\n", burst ? "16" : "8");
	tvout_dbg("%s endian\n", endian ? "big" : "little");

	if ((burst != MIXER_BURST_8) && (burst != MIXER_BURST_16)) {
		tvout_err("invalid burst mode\n");
		return -1;
	}

	if ((endian != TVOUT_BIG_ENDIAN) && (endian != TVOUT_LITTLE_ENDIAN)) {
		tvout_err("invalid endian\n");
		return -1;
	}

	s5p_mixer_init_status_reg(burst, endian);

	tvout_dbg("tvout standard = 0x%X, output mode = %d\n", disp, out);
	/* error handling will be implemented */
	tvout_dbg(KERN_INFO "Color range mode set : %d\n",
		s5p_tvif_get_q_range());
	s5p_mixer_init_csc_coef_default(csc_for_coeff);
	s5p_mixer_init_display_mode(disp, out, csc);

#ifndef	__CONFIG_HDMI_SUPPORT_FULL_RANGE__
	if (!s5p_tvif_get_q_range() || out == TVOUT_HDMI_RGB)
		mixer_video_limiter = true;
	else
		mixer_video_limiter = false;
#else
	/* full range */
	if ((out == TVOUT_HDMI_RGB && disp == TVOUT_480P_60_4_3) ||
		(out != TVOUT_HDMI_RGB && s5p_tvif_get_q_range())) {
		mixer_video_limiter = false;
		for (i = MIXER_BG_COLOR_0; i <= MIXER_BG_COLOR_2; i++)
			s5p_mixer_ctrl_private.bg_color[i].color_y = 0;
	} else { /* limited range */
		mixer_video_limiter = true;
		for (i = MIXER_BG_COLOR_0; i <= MIXER_BG_COLOR_2; i++)
			s5p_mixer_ctrl_private.bg_color[i].color_y = 16;
	}
#endif

	s5p_mixer_set_video_limiter(s5p_mixer_ctrl_private.v_layer.y_min,
			s5p_mixer_ctrl_private.v_layer.y_max,
			s5p_mixer_ctrl_private.v_layer.c_min,
			s5p_mixer_ctrl_private.v_layer.c_max,
			mixer_video_limiter);

	for (i = MIXER_BG_COLOR_0; i <= MIXER_BG_COLOR_2; i++) {
		s5p_mixer_set_bg_color(i,
			s5p_mixer_ctrl_private.bg_color[i].color_y,
			s5p_mixer_ctrl_private.bg_color[i].color_cb,
			s5p_mixer_ctrl_private.bg_color[i].color_cr);
	}

	if (s5p_mixer_ctrl_private.v_layer.use_video_layer) {
		s5p_mixer_ctrl_set_reg(MIXER_VIDEO_LAYER);
		s5p_mixer_set_show(MIXER_VIDEO_LAYER, true);
	}
	if (s5p_mixer_ctrl_private.layer[MIXER_GPR0_LAYER].use_grp_layer) {
		s5p_mixer_ctrl_set_reg(MIXER_GPR0_LAYER);
		s5p_mixer_set_show(MIXER_GPR0_LAYER, true);
	}
	if (s5p_mixer_ctrl_private.layer[MIXER_GPR1_LAYER].use_grp_layer) {
		s5p_mixer_ctrl_set_reg(MIXER_GPR1_LAYER);
		s5p_mixer_set_show(MIXER_GPR1_LAYER, true);
	}

	s5p_mixer_start();
	if (s5p_mixer_ctrl_private.vsync_interrupt_enable)
		s5p_mixer_set_vsync_interrupt(true);

	return 0;
}

wait_queue_head_t s5ptv_wq;

int s5p_mixer_ctrl_constructor(struct platform_device *pdev)
{
	int ret = 0, i;

	ret = s5p_tvout_map_resource_mem(
		pdev,
		s5p_mixer_ctrl_private.reg_mem.name,
		&(s5p_mixer_ctrl_private.reg_mem.base),
		&(s5p_mixer_ctrl_private.reg_mem.res));

	if (ret)
		goto err_on_res;

	for (i = ACLK; i < NO_OF_CLK; i++) {
		s5p_mixer_ctrl_private.clk[i].ptr =
			clk_get(&pdev->dev, s5p_mixer_ctrl_private.clk[i].name);

		if (IS_ERR(s5p_mixer_ctrl_private.clk[i].ptr)) {
			printk(KERN_ERR "Failed to find clock %s\n",
				s5p_mixer_ctrl_private.clk[i].name);
			ret = -ENOENT;
			goto err_on_clk;
		}
	}

	s5p_mixer_ctrl_private.irq.no =
		platform_get_irq_byname(pdev, s5p_mixer_ctrl_private.irq.name);

	if (s5p_mixer_ctrl_private.irq.no < 0) {
		tvout_err("Failed to call platform_get_irq_byname() for %s\n",
			s5p_mixer_ctrl_private.irq.name);
		ret = s5p_mixer_ctrl_private.irq.no;
		goto err_on_irq;
	}

	/* Initializing wait queue for mixer vsync interrupt */
	init_waitqueue_head(&s5ptv_wq);

	s5p_mixer_init(s5p_mixer_ctrl_private.reg_mem.base);

	ret = request_irq(
			s5p_mixer_ctrl_private.irq.no,
			s5p_mixer_ctrl_private.irq.handler,
			IRQF_DISABLED,
			s5p_mixer_ctrl_private.irq.name,
			NULL);
	if (ret) {
		tvout_err("Failed to call request_irq() for %s\n",
			s5p_mixer_ctrl_private.irq.name);
		goto err_on_irq;
	}

	return 0;

err_on_irq:
err_on_clk:
	iounmap(s5p_mixer_ctrl_private.reg_mem.base);
	release_resource(s5p_mixer_ctrl_private.reg_mem.res);
	kfree(s5p_mixer_ctrl_private.reg_mem.res);

err_on_res:
	return ret;
}

void s5p_mixer_ctrl_destructor(void)
{
	int i;
	int irq_no = s5p_mixer_ctrl_private.irq.no;

	if (irq_no >= 0)
		free_irq(irq_no, NULL);

	s5p_tvout_unmap_resource_mem(
		s5p_mixer_ctrl_private.reg_mem.base,
		s5p_mixer_ctrl_private.reg_mem.res);

	for (i = ACLK; i < NO_OF_CLK; i++) {
		if (s5p_mixer_ctrl_private.clk[i].ptr) {
			clk_disable(s5p_mixer_ctrl_private.clk[i].ptr);
			clk_put(s5p_mixer_ctrl_private.clk[i].ptr);
			s5p_mixer_init(NULL);
		}
	}
}

bool pm_running;

void s5p_mixer_ctrl_suspend(void)
{
	tvout_dbg("running(%d)\n", s5p_mixer_ctrl_private.running);
	/* Mixer clock will be gated by tvif_ctrl */
}

void s5p_mixer_ctrl_resume(void)
{
	tvout_dbg("running(%d)\n", s5p_mixer_ctrl_private.running);
	/* Mixer clock will be gated by tvif_ctrl */
}
