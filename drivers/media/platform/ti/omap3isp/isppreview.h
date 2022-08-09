/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * isppreview.h
 *
 * TI OMAP3 ISP - Preview module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef OMAP3_ISP_PREVIEW_H
#define OMAP3_ISP_PREVIEW_H

#include <linux/omap3isp.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>

#include "ispvideo.h"

#define ISPPRV_BRIGHT_STEP		0x1
#define ISPPRV_BRIGHT_DEF		0x0
#define ISPPRV_BRIGHT_LOW		0x0
#define ISPPRV_BRIGHT_HIGH		0xFF
#define ISPPRV_BRIGHT_UNITS		0x1

#define ISPPRV_CONTRAST_STEP		0x1
#define ISPPRV_CONTRAST_DEF		0x10
#define ISPPRV_CONTRAST_LOW		0x0
#define ISPPRV_CONTRAST_HIGH		0xFF
#define ISPPRV_CONTRAST_UNITS		0x1

/* Additional features not listed in linux/omap3isp.h */
#define OMAP3ISP_PREV_CONTRAST		(1 << 17)
#define OMAP3ISP_PREV_BRIGHTNESS	(1 << 18)
#define OMAP3ISP_PREV_FEATURES_END	(1 << 19)

enum preview_input_entity {
	PREVIEW_INPUT_NONE,
	PREVIEW_INPUT_CCDC,
	PREVIEW_INPUT_MEMORY,
};

#define PREVIEW_OUTPUT_RESIZER		(1 << 1)
#define PREVIEW_OUTPUT_MEMORY		(1 << 2)

/* Configure byte layout of YUV image */
enum preview_ycpos_mode {
	YCPOS_YCrYCb = 0,
	YCPOS_YCbYCr = 1,
	YCPOS_CbYCrY = 2,
	YCPOS_CrYCbY = 3
};

/*
 * struct prev_params - Structure for all configuration
 * @busy: Bitmask of busy parameters (being updated or used)
 * @update: Bitmask of the parameters to be updated
 * @features: Set of features enabled.
 * @cfa: CFA coefficients.
 * @csup: Chroma suppression coefficients.
 * @luma: Luma enhancement coefficients.
 * @nf: Noise filter coefficients.
 * @dcor: Noise filter coefficients.
 * @gamma: Gamma coefficients.
 * @wbal: White Balance parameters.
 * @blkadj: Black adjustment parameters.
 * @rgb2rgb: RGB blending parameters.
 * @csc: Color space conversion (RGB to YCbCr) parameters.
 * @hmed: Horizontal median filter.
 * @yclimit: YC limits parameters.
 * @contrast: Contrast.
 * @brightness: Brightness.
 */
struct prev_params {
	u32 busy;
	u32 update;
	u32 features;
	struct omap3isp_prev_cfa cfa;
	struct omap3isp_prev_csup csup;
	struct omap3isp_prev_luma luma;
	struct omap3isp_prev_nf nf;
	struct omap3isp_prev_dcor dcor;
	struct omap3isp_prev_gtables gamma;
	struct omap3isp_prev_wbal wbal;
	struct omap3isp_prev_blkadj blkadj;
	struct omap3isp_prev_rgbtorgb rgb2rgb;
	struct omap3isp_prev_csc csc;
	struct omap3isp_prev_hmed hmed;
	struct omap3isp_prev_yclimit yclimit;
	u8 contrast;
	u8 brightness;
};

/* Sink and source previewer pads */
#define PREV_PAD_SINK			0
#define PREV_PAD_SOURCE			1
#define PREV_PADS_NUM			2

/*
 * struct isp_prev_device - Structure for storing ISP Preview module information
 * @subdev: V4L2 subdevice
 * @pads: Media entity pads
 * @formats: Active formats at the subdev pad
 * @crop: Active crop rectangle
 * @input: Module currently connected to the input pad
 * @output: Bitmask of the active output
 * @video_in: Input video entity
 * @video_out: Output video entity
 * @params.params : Active and shadow parameters sets
 * @params.active: Bitmask of parameters active in set 0
 * @params.lock: Parameters lock, protects params.active and params.shadow
 * @underrun: Whether the preview entity has queued buffers on the output
 * @state: Current preview pipeline state
 *
 * This structure is used to store the OMAP ISP Preview module Information.
 */
struct isp_prev_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[PREV_PADS_NUM];
	struct v4l2_mbus_framefmt formats[PREV_PADS_NUM];
	struct v4l2_rect crop;

	struct v4l2_ctrl_handler ctrls;

	enum preview_input_entity input;
	unsigned int output;
	struct isp_video video_in;
	struct isp_video video_out;

	struct {
		unsigned int cfa_order;
		struct prev_params params[2];
		u32 active;
		spinlock_t lock;
	} params;

	enum isp_pipeline_stream_state state;
	wait_queue_head_t wait;
	atomic_t stopping;
};

struct isp_device;

int omap3isp_preview_init(struct isp_device *isp);
void omap3isp_preview_cleanup(struct isp_device *isp);

int omap3isp_preview_register_entities(struct isp_prev_device *prv,
				       struct v4l2_device *vdev);
void omap3isp_preview_unregister_entities(struct isp_prev_device *prv);

void omap3isp_preview_isr_frame_sync(struct isp_prev_device *prev);
void omap3isp_preview_isr(struct isp_prev_device *prev);

int omap3isp_preview_busy(struct isp_prev_device *isp_prev);

void omap3isp_preview_restore_context(struct isp_device *isp);

#endif	/* OMAP3_ISP_PREVIEW_H */
