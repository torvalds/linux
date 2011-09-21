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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

/* Features list */
#define PREV_LUMA_ENHANCE		OMAP3ISP_PREV_LUMAENH
#define PREV_INVERSE_ALAW		OMAP3ISP_PREV_INVALAW
#define PREV_HORZ_MEDIAN_FILTER		OMAP3ISP_PREV_HRZ_MED
#define PREV_CFA			OMAP3ISP_PREV_CFA
#define PREV_CHROMA_SUPPRESS		OMAP3ISP_PREV_CHROMA_SUPP
#define PREV_WB				OMAP3ISP_PREV_WB
#define PREV_BLKADJ			OMAP3ISP_PREV_BLKADJ
#define PREV_RGB2RGB			OMAP3ISP_PREV_RGB2RGB
#define PREV_COLOR_CONV			OMAP3ISP_PREV_COLOR_CONV
#define PREV_YCLIMITS			OMAP3ISP_PREV_YC_LIMIT
#define PREV_DEFECT_COR			OMAP3ISP_PREV_DEFECT_COR
#define PREV_GAMMA_BYPASS		OMAP3ISP_PREV_GAMMABYPASS
#define PREV_DARK_FRAME_CAPTURE		OMAP3ISP_PREV_DRK_FRM_CAPTURE
#define PREV_DARK_FRAME_SUBTRACT	OMAP3ISP_PREV_DRK_FRM_SUBTRACT
#define PREV_LENS_SHADING		OMAP3ISP_PREV_LENS_SHADING
#define PREV_NOISE_FILTER		OMAP3ISP_PREV_NF
#define PREV_GAMMA			OMAP3ISP_PREV_GAMMA

#define PREV_CONTRAST			(1 << 17)
#define PREV_BRIGHTNESS			(1 << 18)
#define PREV_AVERAGER			(1 << 19)
#define PREV_FEATURES_END		(1 << 20)

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
 * @features: Set of features enabled.
 * @cfa: CFA coefficients.
 * @csup: Chroma suppression coefficients.
 * @luma: Luma enhancement coefficients.
 * @nf: Noise filter coefficients.
 * @dcor: Noise filter coefficients.
 * @gamma: Gamma coefficients.
 * @wbal: White Balance parameters.
 * @blk_adj: Black adjustment parameters.
 * @rgb2rgb: RGB blending parameters.
 * @rgb2ycbcr: RGB to ycbcr parameters.
 * @hmed: Horizontal median filter.
 * @yclimit: YC limits parameters.
 * @contrast: Contrast.
 * @brightness: Brightness.
 */
struct prev_params {
	u32 features;
	struct omap3isp_prev_cfa cfa;
	struct omap3isp_prev_csup csup;
	struct omap3isp_prev_luma luma;
	struct omap3isp_prev_nf nf;
	struct omap3isp_prev_dcor dcor;
	struct omap3isp_prev_gtables gamma;
	struct omap3isp_prev_wbal wbal;
	struct omap3isp_prev_blkadj blk_adj;
	struct omap3isp_prev_rgbtorgb rgb2rgb;
	struct omap3isp_prev_csc rgb2ycbcr;
	struct omap3isp_prev_hmed hmed;
	struct omap3isp_prev_yclimit yclimit;
	u8 contrast;
	u8 brightness;
};

/*
 * struct isptables_update - Structure for Table Configuration.
 * @update: Specifies which tables should be updated.
 * @flag: Specifies which tables should be enabled.
 * @nf: Pointer to structure for Noise Filter
 * @lsc: Pointer to LSC gain table. (currently not used)
 * @gamma: Pointer to gamma correction tables.
 * @cfa: Pointer to color filter array configuration.
 * @wbal: Pointer to colour and digital gain configuration.
 */
struct isptables_update {
	u32 update;
	u32 flag;
	struct omap3isp_prev_nf *nf;
	u32 *lsc;
	struct omap3isp_prev_gtables *gamma;
	struct omap3isp_prev_cfa *cfa;
	struct omap3isp_prev_wbal *wbal;
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
 * @input: Module currently connected to the input pad
 * @output: Bitmask of the active output
 * @video_in: Input video entity
 * @video_out: Output video entity
 * @error: A hardware error occurred during capture
 * @params: Module configuration data
 * @shadow_update: If set, update the hardware configured in the next interrupt
 * @underrun: Whether the preview entity has queued buffers on the output
 * @state: Current preview pipeline state
 * @lock: Shadow update lock
 * @update: Bitmask of the parameters to be updated
 *
 * This structure is used to store the OMAP ISP Preview module Information.
 */
struct isp_prev_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[PREV_PADS_NUM];
	struct v4l2_mbus_framefmt formats[PREV_PADS_NUM];

	struct v4l2_ctrl_handler ctrls;

	enum preview_input_entity input;
	unsigned int output;
	struct isp_video video_in;
	struct isp_video video_out;
	unsigned int error;

	struct prev_params params;
	unsigned int shadow_update:1;
	enum isp_pipeline_stream_state state;
	wait_queue_head_t wait;
	atomic_t stopping;
	spinlock_t lock;
	u32 update;
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
