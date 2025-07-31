// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_pipe.c  --  R-Car VSP1 Pipeline
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_brx.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"
#include "vsp1_hgo.h"
#include "vsp1_hgt.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_uds.h"

/* -----------------------------------------------------------------------------
 * Helper Functions
 */

static const struct vsp1_format_info vsp1_video_formats[] = {
	{ V4L2_PIX_FMT_RGB332, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_332, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 8, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGBA444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_RGBX444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBX_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ABGR444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ABGR_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XBGR444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ABGR_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_BGRA444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGRA_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_BGRX444, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGRA_4444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_XRGB_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGBA555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_5551, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_RGBX555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBX_5551, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ABGR555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ABGR_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XBGR555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ABGR_1555, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_BGRA555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGRA_5551, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_BGRX555, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGRA_5551, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGB565, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_565, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS,
	  1, { 16, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_BGR24, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_BGR_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGB24, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ABGR32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XBGR32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_BGRA32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_BGRX32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGBA32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_RGBX32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGBA_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, true },
	{ V4L2_PIX_FMT_XRGB32, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGBX1010102, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB10_RGB10A2_A2RGB10,
	  VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_RGBA1010102, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB10_RGB10A2_A2RGB10,
	  VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_ARGB2101010, MEDIA_BUS_FMT_ARGB8888_1X32,
	  VI6_FMT_RGB10_RGB10A2_A2RGB10,
	  VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_UYVY, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_YUYV, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, false, 2, 1, false },
	{ V4L2_PIX_FMT_YVYU, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, true, true, 2, 1, false },
	{ V4L2_PIX_FMT_NV12M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 2, false },
	{ V4L2_PIX_FMT_NV21M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 2, false },
	{ V4L2_PIX_FMT_NV16M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_NV61M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_UV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  2, { 8, 16, 0 }, false, true, 2, 1, false },
	{ V4L2_PIX_FMT_YUV420M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 2, 2, false },
	{ V4L2_PIX_FMT_YVU420M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_420, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 2, 2, false },
	{ V4L2_PIX_FMT_YUV422M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_YVU422M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 2, 1, false },
	{ V4L2_PIX_FMT_YUV444M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_YVU444M, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_Y_U_V_444, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  3, { 8, 8, 8 }, false, true, 1, 1, false },
	{ V4L2_PIX_FMT_Y210, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 2, 1, false },
	{ V4L2_PIX_FMT_Y212, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS,
	  1, { 32, 0, 0 }, false, false, 2, 1, false },
};

static const struct vsp1_format_info vsp1_video_gen2_formats[] = {
	{ V4L2_PIX_FMT_VYUY, MEDIA_BUS_FMT_AYUV8_1X32,
	  VI6_FMT_YUYV_422, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 16, 0, 0 }, false, true, 2, 1, false },
};

static const struct vsp1_format_info vsp1_video_hsit_formats[] = {
	{ V4L2_PIX_FMT_HSV24, MEDIA_BUS_FMT_AHSV8888_1X32,
	  VI6_FMT_RGB_888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 24, 0, 0 }, false, false, 1, 1, false },
	{ V4L2_PIX_FMT_HSV32, MEDIA_BUS_FMT_AHSV8888_1X32,
	  VI6_FMT_ARGB_8888, VI6_RPF_DSWAP_P_LLS | VI6_RPF_DSWAP_P_LWS |
	  VI6_RPF_DSWAP_P_WDS | VI6_RPF_DSWAP_P_BTS,
	  1, { 32, 0, 0 }, false, false, 1, 1, false },
};

/**
 * vsp1_get_format_info - Retrieve format information for a 4CC
 * @vsp1: the VSP1 device
 * @fourcc: the format 4CC
 *
 * Return a pointer to the format information structure corresponding to the
 * given V4L2 format 4CC, or NULL if no corresponding format can be found.
 */
const struct vsp1_format_info *vsp1_get_format_info(struct vsp1_device *vsp1,
						    u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vsp1_video_formats); ++i) {
		const struct vsp1_format_info *info = &vsp1_video_formats[i];

		if (info->fourcc == fourcc)
			return info;
	}

	if (vsp1->info->gen == 2) {
		for (i = 0; i < ARRAY_SIZE(vsp1_video_gen2_formats); ++i) {
			const struct vsp1_format_info *info =
				&vsp1_video_gen2_formats[i];

			if (info->fourcc == fourcc)
				return info;
		}
	}

	if (vsp1_feature(vsp1, VSP1_HAS_HSIT)) {
		for (i = 0; i < ARRAY_SIZE(vsp1_video_hsit_formats); ++i) {
			const struct vsp1_format_info *info =
				&vsp1_video_hsit_formats[i];

			if (info->fourcc == fourcc)
				return info;
		}
	}

	return NULL;
}

/**
 * vsp1_get_format_info_by_index - Enumerate format information
 * @vsp1: the VSP1 device
 * @index: the format index
 * @code: media bus code to limit enumeration
 *
 * Return a pointer to the format information structure corresponding to the
 * given index, or NULL if the index exceeds the supported formats list. If the
 * @code parameter is not zero, only formats compatible with the media bus code
 * will be enumerated.
 */
const struct vsp1_format_info *
vsp1_get_format_info_by_index(struct vsp1_device *vsp1, unsigned int index,
			      u32 code)
{
	unsigned int i;

	if (!code) {
		if (index < ARRAY_SIZE(vsp1_video_formats))
			return &vsp1_video_formats[index];

		if (vsp1->info->gen == 2) {
			index -= ARRAY_SIZE(vsp1_video_formats);
			if (index < ARRAY_SIZE(vsp1_video_gen2_formats))
				return &vsp1_video_gen2_formats[index];
		}

		if (vsp1_feature(vsp1, VSP1_HAS_HSIT)) {
			index -= ARRAY_SIZE(vsp1_video_gen2_formats);
			if (index < ARRAY_SIZE(vsp1_video_hsit_formats))
				return &vsp1_video_hsit_formats[index];
		}

		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(vsp1_video_formats); ++i) {
		const struct vsp1_format_info *info = &vsp1_video_formats[i];

		if (info->mbus == code) {
			if (!index)
				return info;
			index--;
		}
	}

	if (vsp1->info->gen == 2) {
		for (i = 0; i < ARRAY_SIZE(vsp1_video_gen2_formats); ++i) {
			const struct vsp1_format_info *info =
				&vsp1_video_gen2_formats[i];

			if (info->mbus == code) {
				if (!index)
					return info;
				index--;
			}
		}
	}

	if (vsp1_feature(vsp1, VSP1_HAS_HSIT)) {
		for (i = 0; i < ARRAY_SIZE(vsp1_video_hsit_formats); ++i) {
			const struct vsp1_format_info *info =
				&vsp1_video_hsit_formats[i];

			if (info->mbus == code) {
				if (!index)
					return info;
				index--;
			}
		}
	}

	return NULL;
}

/**
 * vsp1_adjust_color_space - Adjust color space fields in a format
 * @code: the media bus code
 * @colorspace: the colorspace
 * @xfer_func: the transfer function
 * @encoding: the encoding
 * @quantization: the quantization
 *
 * This function adjusts all color space fields of a video device of subdev
 * format structure, taking into account the requested format, requested color
 * space and limitations of the VSP1. It should be used in the video device and
 * subdev set format handlers.
 *
 * The colorspace and xfer_func fields are freely configurable, as they are out
 * of scope for VSP processing. The encoding and quantization is hardcoded for
 * non-YUV formats, and can be configured for YUV formats.
 */
void vsp1_adjust_color_space(u32 code, u32 *colorspace, u8 *xfer_func,
			     u8 *encoding, u8 *quantization)
{
	if (*colorspace == V4L2_COLORSPACE_DEFAULT ||
	    *colorspace >= V4L2_COLORSPACE_LAST)
		*colorspace = code == MEDIA_BUS_FMT_AYUV8_1X32
			    ? V4L2_COLORSPACE_SMPTE170M
			    : V4L2_COLORSPACE_SRGB;

	if (*xfer_func == V4L2_XFER_FUNC_DEFAULT ||
	    *xfer_func >= V4L2_XFER_FUNC_LAST)
		*xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(*colorspace);

	switch (code) {
	case MEDIA_BUS_FMT_ARGB8888_1X32:
	default:
		*encoding = V4L2_YCBCR_ENC_601;
		*quantization = V4L2_QUANTIZATION_FULL_RANGE;
		break;

	case MEDIA_BUS_FMT_AHSV8888_1X32:
		*encoding = V4L2_HSV_ENC_256;
		*quantization = V4L2_QUANTIZATION_FULL_RANGE;
		break;

	case MEDIA_BUS_FMT_AYUV8_1X32:
		if (*encoding != V4L2_YCBCR_ENC_601 &&
		    *encoding != V4L2_YCBCR_ENC_709)
			*encoding = V4L2_YCBCR_ENC_601;
		if (*quantization != V4L2_QUANTIZATION_FULL_RANGE &&
		    *quantization != V4L2_QUANTIZATION_LIM_RANGE)
			*quantization = V4L2_QUANTIZATION_LIM_RANGE;
		break;
	}
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

void vsp1_pipeline_reset(struct vsp1_pipeline *pipe)
{
	struct vsp1_entity *entity;
	unsigned int i;

	if (pipe->brx) {
		struct vsp1_brx *brx = to_brx(&pipe->brx->subdev);

		for (i = 0; i < ARRAY_SIZE(brx->inputs); ++i)
			brx->inputs[i].rpf = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(pipe->inputs); ++i)
		pipe->inputs[i] = NULL;

	pipe->output = NULL;

	list_for_each_entry(entity, &pipe->entities, list_pipe)
		entity->pipe = NULL;

	INIT_LIST_HEAD(&pipe->entities);
	pipe->state = VSP1_PIPELINE_STOPPED;
	pipe->buffers_ready = 0;
	pipe->num_inputs = 0;
	pipe->brx = NULL;
	pipe->hgo = NULL;
	pipe->hgt = NULL;
	pipe->iif = NULL;
	pipe->lif = NULL;
	pipe->uds = NULL;
}

void vsp1_pipeline_init(struct vsp1_pipeline *pipe)
{
	mutex_init(&pipe->lock);
	spin_lock_init(&pipe->irqlock);
	init_waitqueue_head(&pipe->wq);
	kref_init(&pipe->kref);

	INIT_LIST_HEAD(&pipe->entities);
	pipe->state = VSP1_PIPELINE_STOPPED;
}

void __vsp1_pipeline_dump(struct _ddebug *dbg, struct vsp1_pipeline *pipe,
			  const char *msg)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	struct vsp1_entity *entity;
	bool first = true;

	printk(KERN_DEBUG "%s: %s: pipe: ", dev_name(vsp1->dev), msg);

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		const char *name;

		name = strchrnul(entity->subdev.name, ' ');
		name = name ? name + 1 : entity->subdev.name;

		pr_cont("%s%s", first ? "" : ", ", name);
		first = false;
	}

	pr_cont("\n");
}

/* Must be called with the pipe irqlock held. */
void vsp1_pipeline_run(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;

	lockdep_assert_held(&pipe->irqlock);

	if (pipe->state == VSP1_PIPELINE_STOPPED) {
		vsp1_write(vsp1, VI6_CMD(pipe->output->entity.index),
			   VI6_CMD_STRCMD);
		pipe->state = VSP1_PIPELINE_RUNNING;
	}

	pipe->buffers_ready = 0;
}

bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe)
{
	unsigned long flags;
	bool stopped;

	spin_lock_irqsave(&pipe->irqlock, flags);
	stopped = pipe->state == VSP1_PIPELINE_STOPPED;
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return stopped;
}

int vsp1_pipeline_stop(struct vsp1_pipeline *pipe)
{
	struct vsp1_device *vsp1 = pipe->output->entity.vsp1;
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	if (pipe->lif) {
		/*
		 * When using display lists in continuous frame mode the only
		 * way to stop the pipeline is to reset the hardware.
		 */
		ret = vsp1_reset_wpf(vsp1, pipe->output->entity.index);
		if (ret == 0) {
			spin_lock_irqsave(&pipe->irqlock, flags);
			pipe->state = VSP1_PIPELINE_STOPPED;
			spin_unlock_irqrestore(&pipe->irqlock, flags);
		}
	} else {
		/* Otherwise just request a stop and wait. */
		spin_lock_irqsave(&pipe->irqlock, flags);
		if (pipe->state == VSP1_PIPELINE_RUNNING)
			pipe->state = VSP1_PIPELINE_STOPPING;
		spin_unlock_irqrestore(&pipe->irqlock, flags);

		ret = wait_event_timeout(pipe->wq, vsp1_pipeline_stopped(pipe),
					 msecs_to_jiffies(500));
		ret = ret == 0 ? -ETIMEDOUT : 0;
	}

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		if (entity->route && entity->route->reg)
			vsp1_write(vsp1, entity->route->reg,
				   VI6_DPR_NODE_UNUSED);
	}

	if (pipe->hgo)
		vsp1_write(vsp1, VI6_DPR_HGO_SMPPT,
			   (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
			   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));

	if (pipe->hgt)
		vsp1_write(vsp1, VI6_DPR_HGT_SMPPT,
			   (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
			   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));

	vsp1_wpf_stop(pipe->output);

	return ret;
}

bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe)
{
	unsigned int mask;

	mask = ((1 << pipe->num_inputs) - 1) << 1;
	if (!pipe->lif)
		mask |= 1 << 0;

	return pipe->buffers_ready == mask;
}

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe)
{
	unsigned int flags;

	if (pipe == NULL)
		return;

	/*
	 * If the DL commit raced with the frame end interrupt, the commit ends
	 * up being postponed by one frame. The returned flags tell whether the
	 * active frame was finished or postponed.
	 */
	flags = vsp1_dlm_irq_frame_end(pipe->output->dlm);

	if (pipe->hgo)
		vsp1_hgo_frame_end(pipe->hgo);

	if (pipe->hgt)
		vsp1_hgt_frame_end(pipe->hgt);

	/*
	 * Regardless of frame completion we still need to notify the pipe
	 * frame_end to account for vblank events.
	 */
	if (pipe->frame_end)
		pipe->frame_end(pipe, flags);

	pipe->sequence++;
}

/*
 * Propagate the alpha value through the pipeline.
 *
 * As the UDS has restricted scaling capabilities when the alpha component needs
 * to be scaled, we disable alpha scaling when the UDS input has a fixed alpha
 * value. The UDS then outputs a fixed alpha value which needs to be programmed
 * from the input RPF alpha.
 */
void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_dl_body *dlb, unsigned int alpha)
{
	if (!pipe->uds)
		return;

	/*
	 * The BRU and BRS background color has a fixed alpha value set to 255,
	 * the output alpha value is thus always equal to 255.
	 */
	if (pipe->uds_input->type == VSP1_ENTITY_BRU ||
	    pipe->uds_input->type == VSP1_ENTITY_BRS)
		alpha = 255;

	vsp1_uds_set_alpha(pipe->uds, dlb, alpha);
}

/* -----------------------------------------------------------------------------
 * VSP1 Partition Algorithm support
 */

/*
 * Propagate the partition calculations through the pipeline
 *
 * Work backwards through the pipe, allowing each entity to update the partition
 * parameters based on its configuration, and the entity connected to its
 * source. Each entity must produce the partition required for the previous
 * entity in the pipeline.
 */
static void vsp1_pipeline_propagate_partition(struct vsp1_pipeline *pipe,
					      struct vsp1_partition *partition,
					      unsigned int index,
					      struct v4l2_rect *window)
{
	struct vsp1_entity *entity;

	list_for_each_entry_reverse(entity, &pipe->entities, list_pipe) {
		if (entity->ops->partition)
			entity->ops->partition(entity, entity->state, pipe,
					       partition, index, window);
	}
}

/*
 * vsp1_pipeline_calculate_partition - Calculate pipeline configuration for a
 * partition
 *
 * @pipe: the pipeline
 * @partition: partition that will hold the calculated values
 * @div_size: pre-determined maximum partition division size
 * @index: partition index
 */
void vsp1_pipeline_calculate_partition(struct vsp1_pipeline *pipe,
				       struct vsp1_partition *partition,
				       unsigned int div_size,
				       unsigned int index)
{
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_rect window;
	unsigned int modulus;

	/*
	 * Partitions are computed on the size before rotation, use the format
	 * at the WPF sink.
	 */
	format = v4l2_subdev_state_get_format(pipe->output->entity.state,
					      RWPF_PAD_SINK);

	/* Initialise the partition with sane starting conditions. */
	window.left = index * div_size;
	window.width = div_size;
	window.top = 0;
	window.height = format->height;

	modulus = format->width % div_size;

	/*
	 * We need to prevent the last partition from being smaller than the
	 * *minimum* width of the hardware capabilities.
	 *
	 * If the modulus is less than half of the partition size,
	 * the penultimate partition is reduced to half, which is added
	 * to the final partition: |1234|1234|1234|12|341|
	 * to prevent this:        |1234|1234|1234|1234|1|.
	 */
	if (modulus) {
		/*
		 * pipe->partitions is 1 based, whilst index is a 0 based index.
		 * Normalise this locally.
		 */
		unsigned int partitions = pipe->partitions - 1;

		if (modulus < div_size / 2) {
			if (index == partitions - 1) {
				/* Halve the penultimate partition. */
				window.width = div_size / 2;
			} else if (index == partitions) {
				/* Increase the final partition. */
				window.width = (div_size / 2) + modulus;
				window.left -= div_size / 2;
			}
		} else if (index == partitions) {
			window.width = modulus;
		}
	}

	vsp1_pipeline_propagate_partition(pipe, partition, index, &window);
}
