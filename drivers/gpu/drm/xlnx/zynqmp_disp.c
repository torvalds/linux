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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_managed.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

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

#define ZYNQMP_DISP_NUM_LAYERS				2
#define ZYNQMP_DISP_MAX_NUM_SUB_PLANES			3

/**
 * struct zynqmp_disp_format - Display subsystem format information
 * @drm_fmt: DRM format (4CC)
 * @buf_fmt: AV buffer format
 * @bus_fmt: Media bus formats (live formats)
 * @swap: Flag to swap R & B for RGB formats, and U & V for YUV formats
 * @sf: Scaling factors for color components
 */
struct zynqmp_disp_format {
	u32 drm_fmt;
	u32 buf_fmt;
	u32 bus_fmt;
	bool swap;
	const u32 *sf;
};

/**
 * enum zynqmp_disp_id - Layer identifier
 * @ZYNQMP_DISP_LAYER_VID: Video layer
 * @ZYNQMP_DISP_LAYER_GFX: Graphics layer
 */
enum zynqmp_disp_layer_id {
	ZYNQMP_DISP_LAYER_VID,
	ZYNQMP_DISP_LAYER_GFX
};

/**
 * enum zynqmp_disp_layer_mode - Layer mode
 * @ZYNQMP_DISP_LAYER_NONLIVE: non-live (memory) mode
 * @ZYNQMP_DISP_LAYER_LIVE: live (stream) mode
 */
enum zynqmp_disp_layer_mode {
	ZYNQMP_DISP_LAYER_NONLIVE,
	ZYNQMP_DISP_LAYER_LIVE
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
 * struct zynqmp_disp_layer - Display layer (DRM plane)
 * @plane: DRM plane
 * @id: Layer ID
 * @disp: Back pointer to struct zynqmp_disp
 * @info: Static layer information
 * @dmas: DMA channels
 * @disp_fmt: Current format information
 * @drm_fmt: Current DRM format information
 * @mode: Current operation mode
 */
struct zynqmp_disp_layer {
	struct drm_plane plane;
	enum zynqmp_disp_layer_id id;
	struct zynqmp_disp *disp;
	const struct zynqmp_disp_layer_info *info;

	struct zynqmp_disp_layer_dma dmas[ZYNQMP_DISP_MAX_NUM_SUB_PLANES];

	const struct zynqmp_disp_format *disp_fmt;
	const struct drm_format_info *drm_fmt;
	enum zynqmp_disp_layer_mode mode;
};

/**
 * struct zynqmp_disp_blend - Blender
 * @base: Registers I/O base address
 */
struct zynqmp_disp_blend {
	void __iomem *base;
};

/**
 * struct zynqmp_disp_avbuf - Audio/video buffer manager
 * @base: Registers I/O base address
 */
struct zynqmp_disp_avbuf {
	void __iomem *base;
};

/**
 * struct zynqmp_disp_audio - Audio mixer
 * @base: Registers I/O base address
 * @clk: Audio clock
 * @clk_from_ps: True of the audio clock comes from PS, false from PL
 */
struct zynqmp_disp_audio {
	void __iomem *base;
	struct clk *clk;
	bool clk_from_ps;
};

/**
 * struct zynqmp_disp - Display controller
 * @dev: Device structure
 * @drm: DRM core
 * @dpsub: Display subsystem
 * @crtc: DRM CRTC
 * @blend: Blender (video rendering pipeline)
 * @avbuf: Audio/video buffer manager
 * @audio: Audio mixer
 * @layers: Layers (planes)
 * @event: Pending vblank event request
 * @pclk: Pixel clock
 * @pclk_from_ps: True of the video clock comes from PS, false from PL
 */
struct zynqmp_disp {
	struct device *dev;
	struct drm_device *drm;
	struct zynqmp_dpsub *dpsub;

	struct drm_crtc crtc;

	struct zynqmp_disp_blend blend;
	struct zynqmp_disp_avbuf avbuf;
	struct zynqmp_disp_audio audio;

	struct zynqmp_disp_layer layers[ZYNQMP_DISP_NUM_LAYERS];

	struct drm_pending_vblank_event *event;

	struct clk *pclk;
	bool pclk_from_ps;
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

static u32 zynqmp_disp_avbuf_read(struct zynqmp_disp_avbuf *avbuf, int reg)
{
	return readl(avbuf->base + reg);
}

static void zynqmp_disp_avbuf_write(struct zynqmp_disp_avbuf *avbuf,
				    int reg, u32 val)
{
	writel(val, avbuf->base + reg);
}

/**
 * zynqmp_disp_avbuf_set_format - Set the input format for a layer
 * @avbuf: Audio/video buffer manager
 * @layer: The layer ID
 * @fmt: The format information
 *
 * Set the video buffer manager format for @layer to @fmt.
 */
static void zynqmp_disp_avbuf_set_format(struct zynqmp_disp_avbuf *avbuf,
					 enum zynqmp_disp_layer_id layer,
					 const struct zynqmp_disp_format *fmt)
{
	unsigned int i;
	u32 val;

	val = zynqmp_disp_avbuf_read(avbuf, ZYNQMP_DISP_AV_BUF_FMT);
	val &= layer == ZYNQMP_DISP_LAYER_VID
	    ? ~ZYNQMP_DISP_AV_BUF_FMT_NL_VID_MASK
	    : ~ZYNQMP_DISP_AV_BUF_FMT_NL_GFX_MASK;
	val |= fmt->buf_fmt;
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_FMT, val);

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_SF; i++) {
		unsigned int reg = layer == ZYNQMP_DISP_LAYER_VID
				 ? ZYNQMP_DISP_AV_BUF_VID_COMP_SF(i)
				 : ZYNQMP_DISP_AV_BUF_GFX_COMP_SF(i);

		zynqmp_disp_avbuf_write(avbuf, reg, fmt->sf[i]);
	}
}

/**
 * zynqmp_disp_avbuf_set_clocks_sources - Set the clocks sources
 * @avbuf: Audio/video buffer manager
 * @video_from_ps: True if the video clock originates from the PS
 * @audio_from_ps: True if the audio clock originates from the PS
 * @timings_internal: True if video timings are generated internally
 *
 * Set the source for the video and audio clocks, as well as for the video
 * timings. Clocks can originate from the PS or PL, and timings can be
 * generated internally or externally.
 */
static void
zynqmp_disp_avbuf_set_clocks_sources(struct zynqmp_disp_avbuf *avbuf,
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

	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_CLK_SRC, val);
}

/**
 * zynqmp_disp_avbuf_enable_channels - Enable buffer channels
 * @avbuf: Audio/video buffer manager
 *
 * Enable all (video and audio) buffer channels.
 */
static void zynqmp_disp_avbuf_enable_channels(struct zynqmp_disp_avbuf *avbuf)
{
	unsigned int i;
	u32 val;

	val = ZYNQMP_DISP_AV_BUF_CHBUF_EN |
	      (ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT);

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_VID_GFX_BUFFERS; i++)
		zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					val);

	val = ZYNQMP_DISP_AV_BUF_CHBUF_EN |
	      (ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_AUD_MAX <<
	       ZYNQMP_DISP_AV_BUF_CHBUF_BURST_LEN_SHIFT);

	for (; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					val);
}

/**
 * zynqmp_disp_avbuf_disable_channels - Disable buffer channels
 * @avbuf: Audio/video buffer manager
 *
 * Disable all (video and audio) buffer channels.
 */
static void zynqmp_disp_avbuf_disable_channels(struct zynqmp_disp_avbuf *avbuf)
{
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_AV_BUF_NUM_BUFFERS; i++)
		zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_CHBUF(i),
					ZYNQMP_DISP_AV_BUF_CHBUF_FLUSH);
}

/**
 * zynqmp_disp_avbuf_enable_audio - Enable audio
 * @avbuf: Audio/video buffer manager
 *
 * Enable all audio buffers with a non-live (memory) source.
 */
static void zynqmp_disp_avbuf_enable_audio(struct zynqmp_disp_avbuf *avbuf)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT);
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MEM;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_disable_audio - Disable audio
 * @avbuf: Audio/video buffer manager
 *
 * Disable all audio buffers.
 */
static void zynqmp_disp_avbuf_disable_audio(struct zynqmp_disp_avbuf *avbuf)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT);
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_MASK;
	val |= ZYNQMP_DISP_AV_BUF_OUTPUT_AUD1_DISABLE;
	val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_AUD2_EN;
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_enable_video - Enable a video layer
 * @avbuf: Audio/video buffer manager
 * @layer: The layer ID
 * @mode: Operating mode of layer
 *
 * Enable the video/graphics buffer for @layer.
 */
static void zynqmp_disp_avbuf_enable_video(struct zynqmp_disp_avbuf *avbuf,
					   enum zynqmp_disp_layer_id layer,
					   enum zynqmp_disp_layer_mode mode)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (layer == ZYNQMP_DISP_LAYER_VID) {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		if (mode == ZYNQMP_DISP_LAYER_NONLIVE)
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MEM;
		else
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_LIVE;
	} else {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		if (mode == ZYNQMP_DISP_LAYER_NONLIVE)
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MEM;
		else
			val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_LIVE;
	}
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_disable_video - Disable a video layer
 * @avbuf: Audio/video buffer manager
 * @layer: The layer ID
 *
 * Disable the video/graphics buffer for @layer.
 */
static void zynqmp_disp_avbuf_disable_video(struct zynqmp_disp_avbuf *avbuf,
					    enum zynqmp_disp_layer_id layer)
{
	u32 val;

	val = zynqmp_disp_avbuf_read(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT);
	if (layer == ZYNQMP_DISP_LAYER_VID) {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID1_NONE;
	} else {
		val &= ~ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_MASK;
		val |= ZYNQMP_DISP_AV_BUF_OUTPUT_VID2_DISABLE;
	}
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_OUTPUT, val);
}

/**
 * zynqmp_disp_avbuf_enable - Enable the video pipe
 * @avbuf: Audio/video buffer manager
 *
 * De-assert the video pipe reset.
 */
static void zynqmp_disp_avbuf_enable(struct zynqmp_disp_avbuf *avbuf)
{
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_SRST_REG, 0);
}

/**
 * zynqmp_disp_avbuf_disable - Disable the video pipe
 * @avbuf: Audio/video buffer manager
 *
 * Assert the video pipe reset.
 */
static void zynqmp_disp_avbuf_disable(struct zynqmp_disp_avbuf *avbuf)
{
	zynqmp_disp_avbuf_write(avbuf, ZYNQMP_DISP_AV_BUF_SRST_REG,
				ZYNQMP_DISP_AV_BUF_SRST_REG_VID_RST);
}

/* -----------------------------------------------------------------------------
 * Blender (Video Pipeline)
 */

static void zynqmp_disp_blend_write(struct zynqmp_disp_blend *blend,
				    int reg, u32 val)
{
	writel(val, blend->base + reg);
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
 * @blend: Blender object
 * @format: Output format
 *
 * Set the output format of the blender to @format.
 */
static void zynqmp_disp_blend_set_output_format(struct zynqmp_disp_blend *blend,
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

	zynqmp_disp_blend_write(blend, ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT, fmt);
	if (fmt == ZYNQMP_DISP_V_BLEND_OUTPUT_VID_FMT_RGB) {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	} else {
		coeffs = csc_rgb_to_sdtv_matrix;
		offsets = csc_rgb_to_sdtv_offsets;
	}

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i++)
		zynqmp_disp_blend_write(blend,
					ZYNQMP_DISP_V_BLEND_RGB2YCBCR_COEFF(i),
					coeffs[i]);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_blend_write(blend,
					ZYNQMP_DISP_V_BLEND_OUTCSC_OFFSET(i),
					offsets[i]);
}

/**
 * zynqmp_disp_blend_set_bg_color - Set the background color
 * @blend: Blender object
 * @rcr: Red/Cr color component
 * @gy: Green/Y color component
 * @bcb: Blue/Cb color component
 *
 * Set the background color to (@rcr, @gy, @bcb), corresponding to the R, G and
 * B or Cr, Y and Cb components respectively depending on the selected output
 * format.
 */
static void zynqmp_disp_blend_set_bg_color(struct zynqmp_disp_blend *blend,
					   u32 rcr, u32 gy, u32 bcb)
{
	zynqmp_disp_blend_write(blend, ZYNQMP_DISP_V_BLEND_BG_CLR_0, rcr);
	zynqmp_disp_blend_write(blend, ZYNQMP_DISP_V_BLEND_BG_CLR_1, gy);
	zynqmp_disp_blend_write(blend, ZYNQMP_DISP_V_BLEND_BG_CLR_2, bcb);
}

/**
 * zynqmp_disp_blend_set_global_alpha - Configure global alpha blending
 * @blend: Blender object
 * @enable: True to enable global alpha blending
 * @alpha: Global alpha value (ignored if @enabled is false)
 */
static void zynqmp_disp_blend_set_global_alpha(struct zynqmp_disp_blend *blend,
					       bool enable, u32 alpha)
{
	zynqmp_disp_blend_write(blend, ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA,
				ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_VALUE(alpha) |
				(enable ? ZYNQMP_DISP_V_BLEND_SET_GLOBAL_ALPHA_EN : 0));
}

/**
 * zynqmp_disp_blend_layer_set_csc - Configure colorspace conversion for layer
 * @blend: Blender object
 * @layer: The layer
 * @coeffs: Colorspace conversion matrix
 * @offsets: Colorspace conversion offsets
 *
 * Configure the input colorspace conversion matrix and offsets for the @layer.
 * Columns of the matrix are automatically swapped based on the input format to
 * handle RGB and YCrCb components permutations.
 */
static void zynqmp_disp_blend_layer_set_csc(struct zynqmp_disp_blend *blend,
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

	if (layer->id == ZYNQMP_DISP_LAYER_VID)
		reg = ZYNQMP_DISP_V_BLEND_IN1CSC_COEFF(0);
	else
		reg = ZYNQMP_DISP_V_BLEND_IN2CSC_COEFF(0);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_COEFF; i += 3, reg += 12) {
		zynqmp_disp_blend_write(blend, reg + 0, coeffs[i + swap[0]]);
		zynqmp_disp_blend_write(blend, reg + 4, coeffs[i + swap[1]]);
		zynqmp_disp_blend_write(blend, reg + 8, coeffs[i + swap[2]]);
	}

	if (layer->id == ZYNQMP_DISP_LAYER_VID)
		reg = ZYNQMP_DISP_V_BLEND_IN1CSC_OFFSET(0);
	else
		reg = ZYNQMP_DISP_V_BLEND_IN2CSC_OFFSET(0);

	for (i = 0; i < ZYNQMP_DISP_V_BLEND_NUM_OFFSET; i++)
		zynqmp_disp_blend_write(blend, reg + i * 4, offsets[i]);
}

/**
 * zynqmp_disp_blend_layer_enable - Enable a layer
 * @blend: Blender object
 * @layer: The layer
 */
static void zynqmp_disp_blend_layer_enable(struct zynqmp_disp_blend *blend,
					   struct zynqmp_disp_layer *layer)
{
	const u16 *coeffs;
	const u32 *offsets;
	u32 val;

	val = (layer->drm_fmt->is_yuv ?
	       0 : ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_RGB) |
	      (layer->drm_fmt->hsub > 1 ?
	       ZYNQMP_DISP_V_BLEND_LAYER_CONTROL_EN_US : 0);

	zynqmp_disp_blend_write(blend,
				ZYNQMP_DISP_V_BLEND_LAYER_CONTROL(layer->id),
				val);

	if (layer->drm_fmt->is_yuv) {
		coeffs = csc_sdtv_to_rgb_matrix;
		offsets = csc_sdtv_to_rgb_offsets;
	} else {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	}

	zynqmp_disp_blend_layer_set_csc(blend, layer, coeffs, offsets);
}

/**
 * zynqmp_disp_blend_layer_disable - Disable a layer
 * @blend: Blender object
 * @layer: The layer
 */
static void zynqmp_disp_blend_layer_disable(struct zynqmp_disp_blend *blend,
					    struct zynqmp_disp_layer *layer)
{
	zynqmp_disp_blend_write(blend,
				ZYNQMP_DISP_V_BLEND_LAYER_CONTROL(layer->id),
				0);

	zynqmp_disp_blend_layer_set_csc(blend, layer, csc_zero_matrix,
					csc_zero_offsets);
}

/* -----------------------------------------------------------------------------
 * Audio Mixer
 */

static void zynqmp_disp_audio_write(struct zynqmp_disp_audio *audio,
				  int reg, u32 val)
{
	writel(val, audio->base + reg);
}

/**
 * zynqmp_disp_audio_enable - Enable the audio mixer
 * @audio: Audio mixer
 *
 * Enable the audio mixer by de-asserting the soft reset. The audio state is set to
 * default values by the reset, set the default mixer volume explicitly.
 */
static void zynqmp_disp_audio_enable(struct zynqmp_disp_audio *audio)
{
	/* Clear the audio soft reset register as it's an non-reset flop. */
	zynqmp_disp_audio_write(audio, ZYNQMP_DISP_AUD_SOFT_RESET, 0);
	zynqmp_disp_audio_write(audio, ZYNQMP_DISP_AUD_MIXER_VOLUME,
				ZYNQMP_DISP_AUD_MIXER_VOLUME_NO_SCALE);
}

/**
 * zynqmp_disp_audio_disable - Disable the audio mixer
 * @audio: Audio mixer
 *
 * Disable the audio mixer by asserting its soft reset.
 */
static void zynqmp_disp_audio_disable(struct zynqmp_disp_audio *audio)
{
	zynqmp_disp_audio_write(audio, ZYNQMP_DISP_AUD_SOFT_RESET,
				ZYNQMP_DISP_AUD_SOFT_RESET_AUD_SRST);
}

static void zynqmp_disp_audio_init(struct device *dev,
				   struct zynqmp_disp_audio *audio)
{
	/* Try the live PL audio clock. */
	audio->clk = devm_clk_get(dev, "dp_live_audio_aclk");
	if (!IS_ERR(audio->clk)) {
		audio->clk_from_ps = false;
		return;
	}

	/* If the live PL audio clock is not valid, fall back to PS clock. */
	audio->clk = devm_clk_get(dev, "dp_aud_clk");
	if (!IS_ERR(audio->clk)) {
		audio->clk_from_ps = true;
		return;
	}

	dev_err(dev, "audio disabled due to missing clock\n");
}

/* -----------------------------------------------------------------------------
 * ZynqMP Display external functions for zynqmp_dp
 */

/**
 * zynqmp_disp_handle_vblank - Handle the vblank event
 * @disp: Display controller
 *
 * This function handles the vblank interrupt, and sends an event to
 * CRTC object. This will be called by the DP vblank interrupt handler.
 */
void zynqmp_disp_handle_vblank(struct zynqmp_disp *disp)
{
	struct drm_crtc *crtc = &disp->crtc;

	drm_crtc_handle_vblank(crtc);
}

/**
 * zynqmp_disp_audio_enabled - If the audio is enabled
 * @disp: Display controller
 *
 * Return if the audio is enabled depending on the audio clock.
 *
 * Return: true if audio is enabled, or false.
 */
bool zynqmp_disp_audio_enabled(struct zynqmp_disp *disp)
{
	return !!disp->audio.clk;
}

/**
 * zynqmp_disp_get_audio_clk_rate - Get the current audio clock rate
 * @disp: Display controller
 *
 * Return: the current audio clock rate.
 */
unsigned int zynqmp_disp_get_audio_clk_rate(struct zynqmp_disp *disp)
{
	if (zynqmp_disp_audio_enabled(disp))
		return 0;
	return clk_get_rate(disp->audio.clk);
}

/**
 * zynqmp_disp_get_crtc_mask - Return the CRTC bit mask
 * @disp: Display controller
 *
 * Return: the crtc mask of the zyqnmp_disp CRTC.
 */
uint32_t zynqmp_disp_get_crtc_mask(struct zynqmp_disp *disp)
{
	return drm_crtc_mask(&disp->crtc);
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
 * zynqmp_disp_layer_enable - Enable a layer
 * @layer: The layer
 *
 * Enable the @layer in the audio/video buffer manager and the blender. DMA
 * channels are started separately by zynqmp_disp_layer_update().
 */
static void zynqmp_disp_layer_enable(struct zynqmp_disp_layer *layer)
{
	zynqmp_disp_avbuf_enable_video(&layer->disp->avbuf, layer->id,
				       ZYNQMP_DISP_LAYER_NONLIVE);
	zynqmp_disp_blend_layer_enable(&layer->disp->blend, layer);

	layer->mode = ZYNQMP_DISP_LAYER_NONLIVE;
}

/**
 * zynqmp_disp_layer_disable - Disable the layer
 * @layer: The layer
 *
 * Disable the layer by stopping its DMA channels and disabling it in the
 * audio/video buffer manager and the blender.
 */
static void zynqmp_disp_layer_disable(struct zynqmp_disp_layer *layer)
{
	unsigned int i;

	for (i = 0; i < layer->drm_fmt->num_planes; i++)
		dmaengine_terminate_sync(layer->dmas[i].chan);

	zynqmp_disp_avbuf_disable_video(&layer->disp->avbuf, layer->id);
	zynqmp_disp_blend_layer_disable(&layer->disp->blend, layer);
}

/**
 * zynqmp_disp_layer_set_format - Set the layer format
 * @layer: The layer
 * @state: The plane state
 *
 * Set the format for @layer based on @state->fb->format. The layer must be
 * disabled.
 */
static void zynqmp_disp_layer_set_format(struct zynqmp_disp_layer *layer,
					 struct drm_plane_state *state)
{
	const struct drm_format_info *info = state->fb->format;
	unsigned int i;

	layer->disp_fmt = zynqmp_disp_layer_find_format(layer, info->format);
	layer->drm_fmt = info;

	zynqmp_disp_avbuf_set_format(&layer->disp->avbuf, layer->id,
				     layer->disp_fmt);

	/*
	 * Set slave_id for each DMA channel to indicate they're part of a
	 * video group.
	 */
	for (i = 0; i < info->num_planes; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		struct dma_slave_config config = {
			.direction = DMA_MEM_TO_DEV,
			.slave_id = 1,
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
static int zynqmp_disp_layer_update(struct zynqmp_disp_layer *layer,
				    struct drm_plane_state *state)
{
	const struct drm_format_info *info = layer->drm_fmt;
	unsigned int i;

	for (i = 0; i < layer->drm_fmt->num_planes; i++) {
		unsigned int width = state->crtc_w / (i ? info->hsub : 1);
		unsigned int height = state->crtc_h / (i ? info->vsub : 1);
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		struct dma_async_tx_descriptor *desc;
		dma_addr_t paddr;

		paddr = drm_fb_cma_get_gem_addr(state->fb, state, i);

		dma->xt.numf = height;
		dma->sgl.size = width * info->cpp[i];
		dma->sgl.icg = state->fb->pitches[i] - dma->sgl.size;
		dma->xt.src_start = paddr;
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

static inline struct zynqmp_disp_layer *plane_to_layer(struct drm_plane *plane)
{
	return container_of(plane, struct zynqmp_disp_layer, plane);
}

static int
zynqmp_disp_plane_atomic_check(struct drm_plane *plane,
			       struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;

	if (!state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(state, crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, false);
}

static void
zynqmp_disp_plane_atomic_disable(struct drm_plane *plane,
				 struct drm_plane_state *old_state)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);

	if (!old_state->fb)
		return;

	zynqmp_disp_layer_disable(layer);
}

static void
zynqmp_disp_plane_atomic_update(struct drm_plane *plane,
				struct drm_plane_state *old_state)
{
	struct zynqmp_disp_layer *layer = plane_to_layer(plane);
	bool format_changed = false;

	if (!old_state->fb ||
	    old_state->fb->format->format != plane->state->fb->format->format)
		format_changed = true;

	/*
	 * If the format has changed (including going from a previously
	 * disabled state to any format), reconfigure the format. Disable the
	 * plane first if needed.
	 */
	if (format_changed) {
		if (old_state->fb)
			zynqmp_disp_layer_disable(layer);

		zynqmp_disp_layer_set_format(layer, plane->state);
	}

	zynqmp_disp_layer_update(layer, plane->state);

	/* Enable or re-enable the plane is the format has changed. */
	if (format_changed)
		zynqmp_disp_layer_enable(layer);
}

static const struct drm_plane_helper_funcs zynqmp_disp_plane_helper_funcs = {
	.atomic_check		= zynqmp_disp_plane_atomic_check,
	.atomic_update		= zynqmp_disp_plane_atomic_update,
	.atomic_disable		= zynqmp_disp_plane_atomic_disable,
};

static const struct drm_plane_funcs zynqmp_disp_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int zynqmp_disp_create_planes(struct zynqmp_disp *disp)
{
	unsigned int i, j;
	int ret;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		struct zynqmp_disp_layer *layer = &disp->layers[i];
		enum drm_plane_type type;
		u32 *drm_formats;

		drm_formats = drmm_kcalloc(disp->drm, sizeof(*drm_formats),
					   layer->info->num_formats,
					   GFP_KERNEL);
		if (!drm_formats)
			return -ENOMEM;

		for (j = 0; j < layer->info->num_formats; ++j)
			drm_formats[j] = layer->info->formats[j].drm_fmt;

		/* Graphics layer is primary, and video layer is overlay. */
		type = i == ZYNQMP_DISP_LAYER_GFX
		     ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
		ret = drm_universal_plane_init(disp->drm, &layer->plane, 0,
					       &zynqmp_disp_plane_funcs,
					       drm_formats,
					       layer->info->num_formats,
					       NULL, type, NULL);
		if (ret)
			return ret;

		drm_plane_helper_add(&layer->plane,
				     &zynqmp_disp_plane_helper_funcs);
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

	if (!layer->info)
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

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
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

	for (i = 0; i < layer->info->num_channels; i++) {
		struct zynqmp_disp_layer_dma *dma = &layer->dmas[i];
		char dma_channel_name[16];

		snprintf(dma_channel_name, sizeof(dma_channel_name),
			 "%s%u", dma_names[layer->id], i);
		dma->chan = of_dma_request_slave_channel(disp->dev->of_node,
							 dma_channel_name);
		if (IS_ERR(dma->chan)) {
			dev_err(disp->dev, "failed to request dma channel\n");
			ret = PTR_ERR(dma->chan);
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
		[ZYNQMP_DISP_LAYER_VID] = {
			.formats = avbuf_vid_fmts,
			.num_formats = ARRAY_SIZE(avbuf_vid_fmts),
			.num_channels = 3,
		},
		[ZYNQMP_DISP_LAYER_GFX] = {
			.formats = avbuf_gfx_fmts,
			.num_formats = ARRAY_SIZE(avbuf_gfx_fmts),
			.num_channels = 1,
		},
	};

	unsigned int i;
	int ret;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++) {
		struct zynqmp_disp_layer *layer = &disp->layers[i];

		layer->id = i;
		layer->disp = disp;
		layer->info = &layer_info[i];

		ret = zynqmp_disp_layer_request_dma(disp, layer);
		if (ret)
			goto err;
	}

	return 0;

err:
	zynqmp_disp_destroy_layers(disp);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ZynqMP Display & DRM CRTC
 */

/**
 * zynqmp_disp_enable - Enable the display controller
 * @disp: Display controller
 */
static void zynqmp_disp_enable(struct zynqmp_disp *disp)
{
	zynqmp_disp_avbuf_enable(&disp->avbuf);
	/* Choose clock source based on the DT clock handle. */
	zynqmp_disp_avbuf_set_clocks_sources(&disp->avbuf, disp->pclk_from_ps,
					     disp->audio.clk_from_ps, true);
	zynqmp_disp_avbuf_enable_channels(&disp->avbuf);
	zynqmp_disp_avbuf_enable_audio(&disp->avbuf);

	zynqmp_disp_audio_enable(&disp->audio);
}

/**
 * zynqmp_disp_disable - Disable the display controller
 * @disp: Display controller
 */
static void zynqmp_disp_disable(struct zynqmp_disp *disp)
{
	zynqmp_disp_audio_disable(&disp->audio);

	zynqmp_disp_avbuf_disable_audio(&disp->avbuf);
	zynqmp_disp_avbuf_disable_channels(&disp->avbuf);
	zynqmp_disp_avbuf_disable(&disp->avbuf);
}

static inline struct zynqmp_disp *crtc_to_disp(struct drm_crtc *crtc)
{
	return container_of(crtc, struct zynqmp_disp, crtc);
}

static int zynqmp_disp_crtc_setup_clock(struct drm_crtc *crtc,
					struct drm_display_mode *adjusted_mode)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);
	unsigned long mode_clock = adjusted_mode->clock * 1000;
	unsigned long rate;
	long diff;
	int ret;

	ret = clk_set_rate(disp->pclk, mode_clock);
	if (ret) {
		dev_err(disp->dev, "failed to set a pixel clock\n");
		return ret;
	}

	rate = clk_get_rate(disp->pclk);
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

static void
zynqmp_disp_crtc_atomic_enable(struct drm_crtc *crtc,
			       struct drm_crtc_state *old_crtc_state)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int ret, vrefresh;

	zynqmp_disp_crtc_setup_clock(crtc, adjusted_mode);

	pm_runtime_get_sync(disp->dev);
	ret = clk_prepare_enable(disp->pclk);
	if (ret) {
		dev_err(disp->dev, "failed to enable a pixel clock\n");
		pm_runtime_put_sync(disp->dev);
		return;
	}

	zynqmp_disp_blend_set_output_format(&disp->blend,
					    ZYNQMP_DPSUB_FORMAT_RGB);
	zynqmp_disp_blend_set_bg_color(&disp->blend, 0, 0, 0);
	zynqmp_disp_blend_set_global_alpha(&disp->blend, false, 0);

	zynqmp_disp_enable(disp);

	/* Delay of 3 vblank intervals for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(3 * 1000 / vrefresh);
}

static void
zynqmp_disp_crtc_atomic_disable(struct drm_crtc *crtc,
				struct drm_crtc_state *old_crtc_state)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);
	struct drm_plane_state *old_plane_state;

	/*
	 * Disable the plane if active. The old plane state can be NULL in the
	 * .shutdown() path if the plane is already disabled, skip
	 * zynqmp_disp_plane_atomic_disable() in that case.
	 */
	old_plane_state = drm_atomic_get_old_plane_state(old_crtc_state->state,
							 crtc->primary);
	if (old_plane_state)
		zynqmp_disp_plane_atomic_disable(crtc->primary, old_plane_state);

	zynqmp_disp_disable(disp);

	drm_crtc_vblank_off(&disp->crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	clk_disable_unprepare(disp->pclk);
	pm_runtime_put_sync(disp->dev);
}

static int zynqmp_disp_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void
zynqmp_disp_crtc_atomic_begin(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state)
{
	drm_crtc_vblank_on(crtc);
}

static void
zynqmp_disp_crtc_atomic_flush(struct drm_crtc *crtc,
			      struct drm_crtc_state *old_crtc_state)
{
	if (crtc->state->event) {
		struct drm_pending_vblank_event *event;

		/* Consume the flip_done event from atomic helper. */
		event = crtc->state->event;
		crtc->state->event = NULL;

		event->pipe = drm_crtc_index(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_arm_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs zynqmp_disp_crtc_helper_funcs = {
	.atomic_enable	= zynqmp_disp_crtc_atomic_enable,
	.atomic_disable	= zynqmp_disp_crtc_atomic_disable,
	.atomic_check	= zynqmp_disp_crtc_atomic_check,
	.atomic_begin	= zynqmp_disp_crtc_atomic_begin,
	.atomic_flush	= zynqmp_disp_crtc_atomic_flush,
};

static int zynqmp_disp_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	zynqmp_dp_enable_vblank(disp->dpsub->dp);

	return 0;
}

static void zynqmp_disp_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_disp *disp = crtc_to_disp(crtc);

	zynqmp_dp_disable_vblank(disp->dpsub->dp);
}

static const struct drm_crtc_funcs zynqmp_disp_crtc_funcs = {
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= zynqmp_disp_crtc_enable_vblank,
	.disable_vblank		= zynqmp_disp_crtc_disable_vblank,
};

static int zynqmp_disp_create_crtc(struct zynqmp_disp *disp)
{
	struct drm_plane *plane = &disp->layers[ZYNQMP_DISP_LAYER_GFX].plane;
	int ret;

	ret = drm_crtc_init_with_planes(disp->drm, &disp->crtc, plane,
					NULL, &zynqmp_disp_crtc_funcs, NULL);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(&disp->crtc, &zynqmp_disp_crtc_helper_funcs);

	/* Start with vertical blanking interrupt reporting disabled. */
	drm_crtc_vblank_off(&disp->crtc);

	return 0;
}

static void zynqmp_disp_map_crtc_to_plane(struct zynqmp_disp *disp)
{
	u32 possible_crtcs = drm_crtc_mask(&disp->crtc);
	unsigned int i;

	for (i = 0; i < ZYNQMP_DISP_NUM_LAYERS; i++)
		disp->layers[i].plane.possible_crtcs = possible_crtcs;
}

/* -----------------------------------------------------------------------------
 * Initialization & Cleanup
 */

int zynqmp_disp_drm_init(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_disp *disp = dpsub->disp;
	int ret;

	ret = zynqmp_disp_create_planes(disp);
	if (ret)
		return ret;

	ret = zynqmp_disp_create_crtc(disp);
	if (ret < 0)
		return ret;

	zynqmp_disp_map_crtc_to_plane(disp);

	return 0;
}

int zynqmp_disp_probe(struct zynqmp_dpsub *dpsub, struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(dpsub->dev);
	struct zynqmp_disp *disp;
	struct zynqmp_disp_layer *layer;
	struct resource *res;
	int ret;

	disp = drmm_kzalloc(drm, sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	disp->dev = &pdev->dev;
	disp->dpsub = dpsub;
	disp->drm = drm;

	dpsub->disp = disp;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "blend");
	disp->blend.base = devm_ioremap_resource(disp->dev, res);
	if (IS_ERR(disp->blend.base))
		return PTR_ERR(disp->blend.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "av_buf");
	disp->avbuf.base = devm_ioremap_resource(disp->dev, res);
	if (IS_ERR(disp->avbuf.base))
		return PTR_ERR(disp->avbuf.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aud");
	disp->audio.base = devm_ioremap_resource(disp->dev, res);
	if (IS_ERR(disp->audio.base))
		return PTR_ERR(disp->audio.base);

	/* Try the live PL video clock */
	disp->pclk = devm_clk_get(disp->dev, "dp_live_video_in_clk");
	if (!IS_ERR(disp->pclk))
		disp->pclk_from_ps = false;
	else if (PTR_ERR(disp->pclk) == -EPROBE_DEFER)
		return PTR_ERR(disp->pclk);

	/* If the live PL video clock is not valid, fall back to PS clock */
	if (IS_ERR_OR_NULL(disp->pclk)) {
		disp->pclk = devm_clk_get(disp->dev, "dp_vtc_pixel_clk_in");
		if (IS_ERR(disp->pclk)) {
			dev_err(disp->dev, "failed to init any video clock\n");
			return PTR_ERR(disp->pclk);
		}
		disp->pclk_from_ps = true;
	}

	zynqmp_disp_audio_init(disp->dev, &disp->audio);

	ret = zynqmp_disp_create_layers(disp);
	if (ret)
		return ret;

	layer = &disp->layers[ZYNQMP_DISP_LAYER_VID];
	dpsub->dma_align = 1 << layer->dmas[0].chan->device->copy_align;

	return 0;
}

void zynqmp_disp_remove(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_disp *disp = dpsub->disp;

	zynqmp_disp_destroy_layers(disp);
}
