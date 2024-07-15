// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP Display Controller Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>

#include <linux/clk.h>
#include <linux/dma/xilinx_dpdma.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "zynqmp_disp.h"
#include "zynqmp_disp_regs.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"

/*
 * Overview
 * --------
 *
 * The display controller part of ZynqMP DP subsystem, made of the Audio/Video
 * Buffer Manager, the Video Rendering Pipeline (blender) and the Audio Mixer.
 *
 *              +------------------------------------------------------------+
 * +--------+   | +----------------+     +-----------+                       |
 * | DPDMA  | --->|                | --> |   Video   | Video +-------------+ |
 * | 4x vid |   | |                |     | Rendering | -+--> |             | |   +------+
 * | 2x aud |   | |  Audio/Video   | --> | Pipeline  |  |    | DisplayPort |---> | PHY0 |
 * +--------+   | | Buffer Manager |     +-----------+  |    |   Source    | |   +------+
 *              | |    and STC     |     +-----------+  |    | Controller  | |   +------+
 * Live Video --->|                | --> |   Audio   | Audio |             |---> | PHY1 |
 *              | |                |     |   Mixer   | --+-> |             | |   +------+
 * Live Audio --->|                | --> |           |  ||   +-------------+ |
 *              | +----------------+     +-----------+  ||                   |
 *              +---------------------------------------||-------------------+
 *                                                      vv
 *                                                Blended Video and
 *                                                Mixed Audio to PL
 *
 * Only non-live input from the DPDMA and output to the DisplayPort Source
 * Controller are currently supported. Interface with the programmable logic
 * for live streams is not implemented.
 *
 * The display controller code creates planes for the DPDMA video and graphics
 * layers, and a CRTC for the Video Rendering Pipeline.
 */

#define ZYNQMP_DISP_AV_BUF_NUM_VID_GFX_BUFFERS		4
#define ZYNQMP_DISP_AV_BUF_NUM_BUFFERS			6

#define ZYNQMP_DISP_MAX_NUM_SUB_PLANES			3

/**
 * struct zynqmp_disp_format - Display subsystem format information
 * @drm_fmt: DRM format (4CC)
 * @buf_fmt: AV buffer format
 * @swap: Flag to swap R & B for RGB formats, and U & V for YUV formats
 * @sf: Scaling factors for color components
 */
struct zynqmp_disp_format {
	u32 drm_fmt;
	u32 buf_fmt;
	bool swap;
	const u32 *sf;
};

/**
 * struct zynqmp_disp_layer_dma - DMA channel for one data plane of a layer
 * @chan: DMA channel
 * @xt: Interleaved DMA descriptor template
 * @sgl: Data chunk for dma_interleaved_template
 */
struct zynqmp_disp_layer_dma {
	struct dma_chan *chan;
	struct dma_interleaved_template xt;
	struct data_chunk sgl;
};

/**
 * struct zynqmp_disp_layer_info - Static layer information
 * @formats: Array of supported formats
 * @num_formats: Number of formats in @formats array
 * @num_channels: Number of DMA channels
 */
struct zynqmp_disp_layer_info {
	const struct zynqmp_disp_format *formats;
	unsigned int num_formats;
	unsigned int num_channels;
};

/**
 * struct zynqmp_disp_layer - Display layer
 * @id: Layer ID
 * @disp: Back pointer to struct zynqmp_disp
 * @info: Static layer information
 * @dmas: DMA channels
 * @disp_fmt: Current format information
 * @drm_fmt: Current DRM format information
 * @mode: Current operation mode
 */
struct zynqmp_disp_layer {
	enum zynqmp_dpsub_layer_id id;
	struct zynqmp_disp *disp;
	const struct zynqmp_disp_layer_info *info;

	struct zynqmp_disp_layer_dma dmas[ZYNQMP_DISP_MAX_NUM_SUB_PLANES];

	const struct zynqmp_disp_format *disp_fmt;
	const struct drm_format_info *drm_fmt;
	enum zynqmp_dpsub_layer_mode mode;
};

/**
 * struct zynqmp_disp - Display controller
 * @dev: Device structure
 * @dpsub: Display subsystem
 * @blend.base: Register I/O base address for the blender
 * @avbuf.base: Register I/O base address for the audio/video buffer manager
 * @audio.base: Registers I/O base address for the audio mixer
 * @layers: Layers (planes)
 */
struct zynqmp_disp {
	struct device *dev;
	struct zynqmp_dpsub *dpsub;

	struct {
		void __iomem *base;
	} blend;
	struct {
		void __iomem *base;
	} avbuf;
	struct {
		void __iomem *base;
	} audio;

	struct zynqmp_disp_layer layers[ZYNQMP_DPSUB_NUM_LAYERS];
};

/* -----------------------------------------------------------------------------
 * Audio/Video Buffer Manager
 */

static const u32 scaling_factors_444[] = {
	ZYNQMP_DISP_AV_BUF_4BIT_SF,
	ZYNQMP_DISP_AV_BUF_4BIT_SF,
	ZYNQMP_DISP_AV_BUF_4BIT_SF,
};

static const u32 scaling_factors_555[] = {
	ZYNQMP_DISP_AV_BUF_5BIT_SF,
	ZYNQMP_DISP_AV_BUF_5BIT_SF,
	ZYNQMP_DISP_AV_BUF_5BIT_SF,
};

static const u32 scaling_factors_565[] = {
	ZYNQMP_DISP_AV_BUF_5BIT_SF,
	ZYNQMP_DISP_AV_BUF_6BIT_SF,
	ZYNQMP_DISP_AV_BUF_5BIT_SF,
};

static const u32 scaling_factors_888[] = {
	ZYNQMP_DISP_AV_BUF_8BIT_SF,
	ZYNQMP_DISP_AV_BUF_8BIT_SF,
	ZYNQMP_DISP_AV_BUF_8BIT_SF,
};

static const u32 scaling_factors_101010[] = {
	ZYNQMP_DISP_AV_BUF_10BIT_SF,
	ZYNQMP_DISP_AV_BUF_10BIT_SF,
	ZYNQMP_DISP_AV_BUF_10BIT_SF,
};

/* List of video layer formats */
static const struct zynqmp_disp_format avbuf_vid_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_VYUY,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_VYUY,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_UYVY,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_VYUY,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YUYV,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YUYV,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YVYU,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YUYV,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV422,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU422,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV444,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV24,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU444,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV24,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_NV16,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_NV61,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_XBGR8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGBA8880,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_XRGB8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGBA8880,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_XBGR2101010,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888_10,
		.swap		= false,
		.sf		= scaling_factors_101010,
	}, {
		.drm_fmt	= DRM_FORMAT_XRGB2101010,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_RGB888_10,
		.swap		= true,
		.sf		= scaling_factors_101010,
	}, {
		.drm_fmt	= DRM_FORMAT_YUV420,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16_420,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_YVU420,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16_420,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_NV12,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_420,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_NV21,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_VID_YV16CI_420,
		.swap		= true,
		.sf		= scaling_factors_888,
	},
};

/* List of graphics layer formats */
static const struct zynqmp_disp_format avbuf_gfx_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_ABGR8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA8888,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_ARGB8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA8888,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_ABGR8888,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA8888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_ABGR8888,
		.swap		= true,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB888,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB888,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_BGR888,
		.swap		= false,
		.sf		= scaling_factors_888,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA5551,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA5551,
		.swap		= false,
		.sf		= scaling_factors_555,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA5551,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA5551,
		.swap		= true,
		.sf		= scaling_factors_555,
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA4444,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA4444,
		.swap		= false,
		.sf		= scaling_factors_444,
	}, {
		.drm_fmt	= DRM_FORMAT_BGRA4444,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGBA4444,
		.swap		= true,
		.sf		= scaling_factors_444,
	}, {
		.drm_fmt	= DRM_FORMAT_RGB565,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB565,
		.swap		= false,
		.sf		= scaling_factors_565,
	}, {
		.drm_fmt	= DRM_FORMAT_BGR565,
		.buf_fmt	= ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_RGB565,
		.swap		= true,
		.sf		= scaling_factors_565,
	},
};

static u32 zynqmp_disp_avbuf_read(struct zynqmp_disp *disp, int reg)
{
	return readl(disp->avbuf.base + reg);
}

static void zynqmp_disp_avbuf_write(struct zynqmp_disp *disp, int reg, u32 val)
{
	writel(val, disp->avbuf.base + reg);
}

static bool zynqmp_disp_layer_is_video(const struct zynqmp_disp_layer *layer)
{
	return layer->id == ZYNQMP_DPSUB_LAYER_VID;
}

/**
 * zynqmp_disp_avbuf_set_format - Set the input format for a layer
 * @disp: Display controller
 * @layer: The layer
 * @fmt: The format information
 *
 * Set the video buffer manager format for @layer to @fmt.
 */
static void zynqmp_disp_avbuf_set_format(struct zynqmp_disp *disp,
					 struct zynqmp_disp_layer *layer,
					 const struct zynqmp_disp_format *fmt)
{
	unsigned int i;
	u32 val;

	val = zynqmp_disp_avbuf_read(disp, ZYNQMP_DISP_AV_BUF_FMT);
	val &= zynqmp_disp_layer_is_video(layer)
	    ? ~ZYNQMP_DISP_AV_BUF_FMT_NL_VID_MASK
	    : ~ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_MASK;
	val |= fmt->buf_fmt;
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_FMT, val);

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_SF; i++) {
		unsigned int reg = zynqmp_disp_layer_is_video(layer)
				 ? ZYNQMP_DISP_AV_BUF_VID_COMP_SF(i)
				 : ZYNQMP_DISP_AV_BUF_GFX_COMP_SF(i);

		zynqmp_disp_avbuf_write(disp, reg, fmt->sf[i]);
	}
}

/**
 * zynqmp_disp_avbuf_set_clocks_sources - Set the clocks sources
 * @disp: Display controller
 * @video_from_ps: True if the video clock originates from the PS
 * @audio_from_ps: True if the audio clock originates from the PS
 * @timings_internal: True if video timings are generated internally
 *
 * Set the source for the video and audio clocks, as well as for the video
 * timings. Clocks can originate from the PS or PL, and timings can be
 * generated internally or externally.
 */
static void
zynqmp_disp_avbuf_set_clocks_sources(struct zynqmp_disp *disp,
				     bool video_from_ps, bool audio_from_ps,
				     bool timings_internal)
{
	u32 val = 0;

	if (video_from_ps)
		val |= ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_FROM_PS;
	if (audio_from_ps)
		val |= ZYNQMP_DISP_AV_BUF_CLK_SRC_AUD_FROM_PS;
	if (timings_internal)
		val |= ZYNQMP_DISP_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING;

	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_CLK_SRC, val);
}

/**
 * zynqmp_disp_avbuf_enable_channels - Enable buffer channels
 * @disp: Display controller
 *
 * Enable all (video and audio) buffer channels.
 */
static void zynqmp_disp_avbuf_enable_channels(struct zynqmp_disp *disp)
{
	unsigned int i;
	u32 val;

	val = ZYNQMP_DISP_AV_BUF_CHBUF_EN |
	      (ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT);

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_VID_GFX_BUFFERS; i++)
		zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					val);

	val = ZYNQMP_DISP_AV_BUF_CHBUF_EN |
	      (ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_AUD_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT);

	for (; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					val);
}

/**
 * zynqmp_disp_avbuf_disable_channels - Disable buffer channels
 * @disp: Display controller
 *
 * Disable all (video and audio) buffer channels.
 */
static void zynqmp_disp_avbuf_disable_channels(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					ZYNQMP_DISP_AV_BUF_CHBUF_FLUSH);
}

/**
 * zynqmp_disp_avbuf_enable_audio - Enable audio
 * @disp: Display controller
 *
 * Enable all audio buffers with a non-live (memory) source.
 */
static void zynqmp_disp_avbuf_enable_audio(struct zynqmp_disp *disp)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(disp, ZYNQMP_DISP_AV_BUF_OUTPUT);
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MEM;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_disable_audio - Disable audio
 * @disp: Display controller
 *
 * Disable all audio buffers.
 */
static void zynqmp_disp_avbuf_disable_audio(struct zynqmp_disp *disp)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(disp, ZYNQMP_DISP_AV_BUF_OUTPUT);
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_DISABLE;
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_enable_video - Enable a video layer
 * @disp: Display controller
 * @layer: The layer
 *
 * Enable the video/graphics buffer for @layer.
 */
static void zynqmp_disp_avbuf_enable_video(struct zynqmp_disp *disp,
					   struct zynqmp_disp_layer *layer)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(disp, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (zynqmp_disp_layer_is_video(layer)) {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		if (layer->mode == ZYNQMP_DPSUB_LAYER_NONLIVE)
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MEM;
		else
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_LIVE;
	} else {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		if (layer->mode == ZYNQMP_DPSUB_LAYER_NONLIVE)
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		else
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_LIVE;
	}
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_disable_video - Disable a video layer
 * @disp: Display controller
 * @layer: The layer
 *
 * Disable the video/graphics buffer for @layer.
 */
static void zynqmp_disp_avbuf_disable_video(struct zynqmp_disp *disp,
					    struct zynqmp_disp_layer *layer)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(disp, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (zynqmp_disp_layer_is_video(layer)) {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_NONE;
	} else {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_DISABLE;
	}
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_enable - Enable the video pipe
 * @disp: Display controller
 *
 * De-assert the video pipe reset.
 */
static void zynqmp_disp_avbuf_enable(struct zynqmp_disp *disp)
{
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_SRST_REG, 0);
}

/**
 * zynqmp_disp_avbuf_disable - Disable the video pipe
 * @disp: Display controller
 *
 * Assert the video pipe reset.
 */
static void zynqmp_disp_avbuf_disable(struct zynqmp_disp *disp)
{
	zynqmp_disp_avbuf_write(disp, ZYNQMP_DISP_AV_BUF_SRST_REG,
				ZYNQMP_DISP_AV_BUF_SRST_REG_VID_RST);
}

/* -----------------------------------------------------------------------------
 * Blender (Video Pipeline)
 */

static void zynqmp_disp_blend_write(struct zynqmp_disp *disp, int reg, u32 val)
{
	writel(val, disp->blend.base + reg);
}

/*
 * Colorspace conversion matrices.
 *
 * Hardcode RGB <-> YUV conversion to full-range SDTV for now.
 */
static const u16 csc_zero_matrix[] = {
	0x0,    0x0,    0x0,
	0x0,    0x0,    0x0,
	0x0,    0x0,    0x0
};

static const u16 csc_identity_matrix[] = {
	0x1000, 0x0,    0x0,
	0x0,    0x1000, 0x0,
	0x0,    0x0,    0x1000
};

static const u32 csc_zero_offsets[] = {
	0, 0, 0
};

static const u16 csc_rgb_to_sdtv_matrix[] = {
	0x4c9,  0x864,  0x1d3,
	0x7d4d, 0x7ab3, 0x800,
	0x800,  0x794d, 0x7eb3
};

static const u32 csc_rgb_to_sdtv_offsets[] = {
	0x0, 0x8000000, 0x8000000
};

static const u16 csc_sdtv_to_rgb_matrix[] = {
	0x1000, 0x166f, 0x0,
	0x1000, 0x7483, 0x7a7f,
	0x1000, 0x0,    0x1c5a
};

static const u32 csc_sdtv_to_rgb_offsets[] = {
	0x0, 0x1800, 0x1800
};

/**
 * zynqmp_disp_blend_set_output_format - Set the output format of the blender
 * @disp: Display controller
 * @format: Output format
 *
 * Set the output format of the blender to @format.
 */
static void zynqmp_disp_blend_set_output_format(struct zynqmp_disp *disp,
						enum zynqmp_dpsub_format format)
{
	static const unsigned int blend_output_fmts[] = {
		[ZYNQMP_DPSUB_FORMAT_RGB] = ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_RGB,
		[ZYNQMP_DPSUB_FORMAT_YCRCB444] = ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YCBCR444,
		[ZYNQMP_DPSUB_FORMAT_YCRCB422] = ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YCBCR422
					       | ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_EN_DOWNSAMPLE,
		[ZYNQMP_DPSUB_FORMAT_YONLY] = ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_YONLY,
	};

	u32 fmt = blend_output_fmts[format];
	const u16 *coeffs;
	const u32 *offsets;
	unsigned int i;

	zynqmp_disp_blend_write(disp, ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT, fmt);
	if (fmt == ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_RGB) {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	} else {
		coeffs = csc_rgb_to_sdtv_matrix;
		offsets = csc_rgb_to_sdtv_offsets;
	}

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i++)
		zynqmp_disp_blend_write(disp,
					ZYNQMP_DISP_V_BLEND_RGB2YCBCR_COEFF(i),
					coeffs[i]);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_blend_write(disp,
					ZYNQMP_DISP_V_BLEND_OUTCSC_OFFSET(i),
					offsets[i]);
}

/**
 * zynqmp_disp_blend_set_bg_color - Set the background color
 * @disp: Display controller
 * @rcr: Red/Cr color component
 * @gy: Green/Y color component
 * @bcb: Blue/Cb color component
 *
 * Set the background color to (@rcr, @gy, @bcb), corresponding to the R, G and
 * B or Cr, Y and Cb components respectively depending on the selected output
 * format.
 */
static void zynqmp_disp_blend_set_bg_color(struct zynqmp_disp *disp,
					   u32 rcr, u32 gy, u32 bcb)
{
	zynqmp_disp_blend_write(disp, ZYNQMP_DISP_V_BLEND_BG_CLR_0, rcr);
	zynqmp_disp_blend_write(disp, ZYNQMP_DISP_V_BLEND_BG_CLR_1, gy);
	zynqmp_disp_blend_write(disp, ZYNQMP_DISP_V_BLEND_BG_CLR_2, bcb);
}

/**
 * zynqmp_disp_blend_set_global_alpha - Configure global alpha blending
 * @disp: Display controller
 * @enable: True to enable global alpha blending
 * @alpha: Global alpha value (ignored if @enabled is false)
 */
void zynqmp_disp_blend_set_global_alpha(struct zynqmp_disp *disp,
					bool enable, u32 alpha)
{
	zynqmp_disp_blend_write(disp, ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA,
				ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_VALUE(alpha) |
				(enable ? ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_EN : 0));
}

/**
 * zynqmp_disp_blend_layer_set_csc - Configure colorspace conversion for layer
 * @disp: Display controller
 * @layer: The layer
 * @coeffs: Colorspace conversion matrix
 * @offsets: Colorspace conversion offsets
 *
 * Configure the input colorspace conversion matrix and offsets for the @layer.
 * Columns of the matrix are automatically swapped based on the input format to
 * handle RGB and YCrCb components permutations.
 */
static void zynqmp_disp_blend_layer_set_csc(struct zynqmp_disp *disp,
					    struct zynqmp_disp_layer *layer,
					    const u16 *coeffs,
					    const u32 *offsets)
{
	unsigned int swap[3] = { 0, 1, 2 };
	unsigned int reg;
	unsigned int i;

	if (layer->disp_fmt->swap) {
		if (layer->drm_fmt->is_yuv) {
			/* Swap U and V. */
			swap[1] = 2;
			swap[2] = 1;
		} else {
			/* Swap R and B. */
			swap[0] = 2;
			swap[2] = 0;
		}
	}

	if (zynqmp_disp_layer_is_video(layer))
		reg = ZYNQMP_DISP_V_BLEND_IN1CSC_COEFF(0);
	else
		reg = ZYNQMP_DISP_V_BLEND_IN2CSC_COEFF(0);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i += 3, reg += 12) {
		zynqmp_disp_blend_write(disp, reg + 0, coeffs[i + swap[0]]);
		zynqmp_disp_blend_write(disp, reg + 4, coeffs[i + swap[1]]);
		zynqmp_disp_blend_write(disp, reg + 8, coeffs[i + swap[2]]);
	}

	if (zynqmp_disp_layer_is_video(layer))
		reg = ZYNQMP_DISP_V_BLEND_IN1CSC_OFFSET(0);
	else
		reg = ZYNQMP_DISP_V_BLEND_IN2CSC_OFFSET(0);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_blend_write(disp, reg + i * 4, offsets[i]);
}

/**
 * zynqmp_disp_blend_layer_enable - Enable a layer
 * @disp: Display controller
 * @layer: The layer
 */
static void zynqmp_disp_blend_layer_enable(struct zynqmp_disp *disp,
					   struct zynqmp_disp_layer *layer)
{
	const u16 *coeffs;
	const u32 *offsets;
	u32 val;

	val = (layer->drm_fmt->is_yuv ?
	       0 : ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_RGB) |
	      (layer->drm_fmt->hsub > 1 ?
	       ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_EN_US : 0);

	zynqmp_disp_blend_write(disp,
				ZYNQMP_DISP_V_BLEND_LAYER_CONTROL(layer->id),
				val);

	if (layer->drm_fmt->is_yuv) {
		coeffs = csc_sdtv_to_rgb_matrix;
		offsets = csc_sdtv_to_rgb_offsets;
	} else {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	}

	zynqmp_disp_blend_layer_set_csc(disp, layer, coeffs, offsets);
}

/**
 * zynqmp_disp_blend_layer_disable - Disable a layer
 * @disp: Display controller
 * @layer: The layer
 */
static void zynqmp_disp_blend_layer_disable(struct zynqmp_disp *disp,
					    struct zynqmp_disp_layer *layer)
{
	zynqmp_disp_blend_write(disp,
				ZYNQMP_DISP_V_BLEND_LAYER_CONTROL(layer->id),
				0);

	zynqmp_disp_blend_layer_set_csc(disp, layer, csc_zero_matrix,
					csc_zero_offsets);
}

/* -----------------------------------------------------------------------------
 * Audio Mixer
 */

static void zynqmp_disp_audio_write(struct zynqmp_disp *disp, int reg, u32 val)
{
	writel(val, disp->audio.base + reg);
}

/**
 * zynqmp_disp_audio_enable - Enable the audio mixer
 * @disp: Display controller
 *
 * Enable the audio mixer by de-asserting the soft reset. The audio state is set to
 * default values by the reset, set the default mixer volume explicitly.
 */
static void zynqmp_disp_audio_enable(struct zynqmp_disp *disp)
{
	/* Clear the audio soft reset register as it's an non-reset flop. */
	zynqmp_disp_audio_write(disp, ZYNQMP_DISP_AUD_SOFT_RESET, 0);
	zynqmp_disp_audio_write(disp, ZYNQMP_DISP_AUD_MIXER_VOLUME,
				ZYNQMP_DISP_AUD_MIXER_VOLUME_NO_SCALE);
}

/**
 * zynqmp_disp_audio_disable - Disable the audio mixer
 * @disp: Display controller
 *
 * Disable the audio mixer by asserting its soft reset.
 */
static void zynqmp_disp_audio_disable(struct zynqmp_disp *disp)
{
	zynqmp_disp_audio_write(disp, ZYNQMP_DISP_AUD_SOFT_RESET,
				ZYNQMP_DISP_AUD_SOFT_RESET_AUD_SRST);
}

/* -----------------------------------------------------------------------------
 * ZynqMP Display Layer & DRM Plane
 */

/**
 * zynqmp_disp_layer_find_format - Find format information for a DRM format
 * @layer: The layer
 * @drm_fmt: DRM format to search
 *
 * Search display subsystem format information corresponding to the given DRM
 * format @drm_fmt for the @layer, and return a pointer to the format
 * descriptor.
 *
 * Return: A pointer to the format descriptor if found, NULL otherwise
 */
static const struct zynqmp_disp_format *
zynqmp_disp_layer_find_format(struct zynqmp_disp_layer *layer,
			      u32 drm_fmt)
{
	unsigned int i;

	for (i = 0; i < layer->info->num_formats; i++) {
		if (layer->info->formats[i].drm_fmt == drm_fmt)
			return &layer->info->formats[i];
	}

	return NULL;
}

/**
 * zynqmp_disp_layer_drm_formats - Return the DRM formats supported by the layer
 * @layer: The layer
 * @num_formats: Pointer to the returned number of formats
 *
 * Return: A newly allocated u32 array that stores all the DRM formats
 * supported by the layer. The number of formats in the array is returned
 * through the num_formats argument.
 */
u32 *zynqmp_disp_layer_drm_formats(struct zynqmp_disp_layer *layer,
				   unsigned int *num_formats)
{
	unsigned int i;
	u32 *formats;

	formats = kcalloc(layer->info->num_formats, sizeof(*formats),
			  GFP_KERNEL);
	if (!formats)
		return NULL;

	for (i = 0; i < layer->info->num_formats; ++i)
		formats[i] = layer->info->formats[i].drm_fmt;

	*num_formats = layer->info->num_formats;
	return formats;
}

/**
 * zynqmp_disp_layer_enable - Enable a layer
 * @layer: The layer
 * @mode: Operating mode of layer
 *
 * Enable the @layer in the audio/video buffer manager and the blender. DMA
 * channels are started separately by zynqmp_disp_layer_update().
 */
void zynqmp_disp_layer_enable(struct zynqmp_disp_layer *layer,
			      enum zynqmp_dpsub_layer_mode mode)
{
	layer->mode = mode;
	zynqmp_disp_avbuf_enable_video(layer->disp, layer);
	zynqmp_disp_blend_layer_enable(layer->disp, layer);
}

/**
 * zynqmp_disp_layer_disable - Disable the layer
 * @layer: The layer
 *
 * Disable the layer by stopping its DMA channels and disabling it in the
 * audio/video buffer manager and the blender.
 */
void zynqmp_disp_layer_disable(struct zynqmp_disp_layer *layer)
{
	unsigned int i;

	if (layer->disp->dpsub->dma_enabled) {
		for (i = 0; i < layer->drm_fmt->num_planes; i++)
			dmaengine_terminate_sync(layer->dmas[i].chan);
	}

	zynqmp_disp_avbuf_disable_video(layer->disp, layer);
	zynqmp_disp_blend_layer_disable(layer->disp, layer);
}

/**
 * zynqmp_disp_layer_set_format - Set the layer format
 * @layer: The layer
 * @info: The format info
 *
 * Set the format for @layer to @info. The layer must be disabled.
 */
void zynqmp_disp_layer_set_format(struct zynqmp_disp_layer *layer,
				  const struct drm_format_info *info)
{
	unsigned int i;

	layer->disp_fmt = zynqmp_disp_layer_find_format(layer, info->format);
	layer->drm_fmt = info;

	zynqmp_disp_avbuf_set_format(layer->disp, layer, layer->disp_fmt);

	if (!layer->disp->dpsub->dma_enabled)
		return;

	/*
	 * Set pconfig for each DMA channel to indicate they're part of a
	 * video group.
	 */
	for (i = 0; i < info->num_planes; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		struct xilinx_dpdma_peripheral_config pconfig = {
			.video_group = true,
		};
		struct dma_slave_config config = {
			.direction = DMA_MEM_TO_DEV,
			.peripheral_config = &pconfig,
			.peripheral_size = sizeof(pconfig),
		};

		dmaengine_slave_config(dma->chan, &config);
	}
}

/**
 * zynqmp_disp_layer_update - Update the layer framebuffer
 * @layer: The layer
 * @state: The plane state
 *
 * Update the framebuffer for the layer by issuing a new DMA engine transaction
 * for the new framebuffer.
 *
 * Return: 0 on success, or the DMA descriptor failure error otherwise
 */
int zynqmp_disp_layer_update(struct zynqmp_disp_layer *layer,
			     struct drm_plane_state *state)
{
	const struct drm_format_info *info = layer->drm_fmt;
	unsigned int i;

	if (!layer->disp->dpsub->dma_enabled)
		return 0;

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = state->crtc_w / (i ? info->hsub : 1);
		unsigned int height = state->crtc_h / (i ? info->vsub : 1);
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		struct dma_async_tx_descriptor *desc;
		dma_addr_t dma_addr;

		dma_addr = drm_fb_dma_get_gem_addr(state->fb, state, i);

		dma->xt.numf = height;
		dma->sgl.size = width * info->cpp[i];
		dma->sgl.icg = state->fb->pitches[i] - dma->sgl.size;
		dma->xt.src_start = dma_addr;
		dma->xt.frame_size = 1;
		dma->xt.dir = DMA_MEM_TO_DEV;
		dma->xt.src_sgl = true;
		dma->xt.dst_sgl = false;

		desc = dmaengine_prep_interleaved_dma(dma->chan, &dma->xt,
						      DMA_CTRL_ACK |
						      DMA_PREP_REPEAT |
						      DMA_PREP_LOAD_EOT);
		if (!desc) {
			dev_err(layer->disp->dev,
				"failed to prepare DMA descriptor\n");
			return -ENOMEM;
		}

		dmaengine_submit(desc);
		dma_async_issue_pending(dma->chan);
	}

	return 0;
}

/**
 * zynqmp_disp_layer_release_dma - Release DMA channels for a layer
 * @disp: Display controller
 * @layer: The layer
 *
 * Release the DMA channels associated with @layer.
 */
static void zynqmp_disp_layer_release_dma(struct zynqmp_disp *disp,
					  struct zynqmp_disp_layer *layer)
{
	unsigned int i;

	if (!layer->info || !disp->dpsub->dma_enabled)
		return;

	for (i = 0; i < layer->info->num_channels; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];

		if (!dma->chan)
			continue;

		/* Make sure the channel is terminated before release. */
		dmaengine_terminate_sync(dma->chan);
		dma_release_channel(dma->chan);
	}
}

/**
 * zynqmp_disp_destroy_layers - Destroy all layers
 * @disp: Display controller
 */
static void zynqmp_disp_destroy_layers(struct zynqmp_disp *disp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(disp->layers); i++)
		zynqmp_disp_layer_release_dma(disp, &disp->layers[i]);
}

/**
 * zynqmp_disp_layer_request_dma - Request DMA channels for a layer
 * @disp: Display controller
 * @layer: The layer
 *
 * Request all DMA engine channels needed by @layer.
 *
 * Return: 0 on success, or the DMA channel request error otherwise
 */
static int zynqmp_disp_layer_request_dma(struct zynqmp_disp *disp,
					 struct zynqmp_disp_layer *layer)
{
	static const char * const dma_names[] = { "vid", "gfx" };
	unsigned int i;
	int ret;

	if (!disp->dpsub->dma_enabled)
		return 0;

	for (i = 0; i < layer->info->num_channels; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		char dma_channel_name[16];

		snprintf(dma_channel_name, sizeof(dma_channel_name),
			 "%s%u", dma_names[layer->id], i);
		dma->chan = dma_request_chan(disp->dev, dma_channel_name);
		if (IS_ERR(dma->chan)) {
			ret = dev_err_probe(disp->dev, PTR_ERR(dma->chan),
					    "failed to request dma channel\n");
			dma->chan = NULL;
			return ret;
		}
	}

	return 0;
}

/**
 * zynqmp_disp_create_layers - Create and initialize all layers
 * @disp: Display controller
 *
 * Return: 0 on success, or the DMA channel request error otherwise
 */
static int zynqmp_disp_create_layers(struct zynqmp_disp *disp)
{
	static const struct zynqmp_disp_layer_info layer_info[] = {
		[ZYNQMP_DPSUB_LAYER_VID] = {
			.formats = avbuf_vid_fmts,
			.num_formats = ARRAY_SIZE(avbuf_vid_fmts),
			.num_channels = 3,
		},
		[ZYNQMP_DPSUB_LAYER_GFX] = {
			.formats = avbuf_gfx_fmts,
			.num_formats = ARRAY_SIZE(avbuf_gfx_fmts),
			.num_channels = 1,
		},
	};

	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(disp->layers); i++) {
		struct zynqmp_disp_layer *layer = &disp->layers[i];

		layer->id = i;
		layer->disp = disp;
		layer->info = &layer_info[i];

		ret = zynqmp_disp_layer_request_dma(disp, layer);
		if (ret)
			goto err;

		disp->dpsub->layers[i] = layer;
	}

	return 0;

err:
	zynqmp_disp_destroy_layers(disp);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ZynqMP Display
 */

/**
 * zynqmp_disp_enable - Enable the display controller
 * @disp: Display controller
 */
void zynqmp_disp_enable(struct zynqmp_disp *disp)
{
	zynqmp_disp_blend_set_output_format(disp, ZYNQMP_DPSUB_FORMAT_RGB);
	zynqmp_disp_blend_set_bg_color(disp, 0, 0, 0);

	zynqmp_disp_avbuf_enable(disp);
	/* Choose clock source based on the DT clock handle. */
	zynqmp_disp_avbuf_set_clocks_sources(disp, disp->dpsub->vid_clk_from_ps,
					     disp->dpsub->aud_clk_from_ps,
					     disp->dpsub->vid_clk_from_ps);
	zynqmp_disp_avbuf_enable_channels(disp);
	zynqmp_disp_avbuf_enable_audio(disp);

	zynqmp_disp_audio_enable(disp);
}

/**
 * zynqmp_disp_disable - Disable the display controller
 * @disp: Display controller
 */
void zynqmp_disp_disable(struct zynqmp_disp *disp)
{
	zynqmp_disp_audio_disable(disp);

	zynqmp_disp_avbuf_disable_audio(disp);
	zynqmp_disp_avbuf_disable_channels(disp);
	zynqmp_disp_avbuf_disable(disp);
}

/**
 * zynqmp_disp_setup_clock - Configure the display controller pixel clock rate
 * @disp: Display controller
 * @mode_clock: The pixel clock rate, in Hz
 *
 * Return: 0 on success, or a negative error clock otherwise
 */
int zynqmp_disp_setup_clock(struct zynqmp_disp *disp,
			    unsigned long mode_clock)
{
	unsigned long rate;
	long diff;
	int ret;

	ret = clk_set_rate(disp->dpsub->vid_clk, mode_clock);
	if (ret) {
		dev_err(disp->dev, "failed to set the video clock\n");
		return ret;
	}

	rate = clk_get_rate(disp->dpsub->vid_clk);
	diff = rate - mode_clock;
	if (abs(diff) > mode_clock / 20)
		dev_info(disp->dev,
			 "requested pixel rate: %lu actual rate: %lu\n",
			 mode_clock, rate);
	else
		dev_dbg(disp->dev,
			"requested pixel rate: %lu actual rate: %lu\n",
			mode_clock, rate);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Initialization & Cleanup
 */

int zynqmp_disp_probe(struct zynqmp_dpsub *dpsub)
{
	struct platform_device *pdev = to_platform_device(dpsub->dev);
	struct zynqmp_disp *disp;
	int ret;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	disp->dev = &pdev->dev;
	disp->dpsub = dpsub;

	disp->blend.base = devm_platform_ioremap_resource_byname(pdev, "blend");
	if (IS_ERR(disp->blend.base)) {
		ret = PTR_ERR(disp->blend.base);
		goto error;
	}

	disp->avbuf.base = devm_platform_ioremap_resource_byname(pdev, "av_buf");
	if (IS_ERR(disp->avbuf.base)) {
		ret = PTR_ERR(disp->avbuf.base);
		goto error;
	}

	disp->audio.base = devm_platform_ioremap_resource_byname(pdev, "aud");
	if (IS_ERR(disp->audio.base)) {
		ret = PTR_ERR(disp->audio.base);
		goto error;
	}

	ret = zynqmp_disp_create_layers(disp);
	if (ret)
		goto error;

	if (disp->dpsub->dma_enabled) {
		struct zynqmp_disp_layer *layer;

		layer = &disp->layers[ZYNQMP_DPSUB_LAYER_VID];
		dpsub->dma_align = 1 << layer->dmas[0].chan->device->copy_align;
	}

	dpsub->disp = disp;

	return 0;

error:
	kfree(disp);
	return ret;
}

void zynqmp_disp_remove(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_disp *disp = dpsub->disp;

	zynqmp_disp_destroy_layers(disp);
}
