/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_enc.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>

#include "s5p_mfc_common.h"

#include "s5p_mfc_intr.h"
#include "s5p_mfc_mem.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_reg.h"
#include "s5p_mfc_enc.h"

#define DEF_SRC_FMT	1
#define DEF_DST_FMT	2

static struct s5p_mfc_fmt formats[] = {
	{
		.name = "4:2:0 2 Planes 16x16 Tiles",
		.fourcc = V4L2_PIX_FMT_NV12MT_16X16,
		.codec_mode = MFC_FORMATS_NO_CODEC,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "4:2:0 2 Planes 64x32 Tiles",
		.fourcc = V4L2_PIX_FMT_NV12MT,
		.codec_mode = MFC_FORMATS_NO_CODEC,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "4:2:0 2 Planes",
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = MFC_FORMATS_NO_CODEC,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "4:2:0 2 Planes Y/CrCb",
		.fourcc = V4L2_PIX_FMT_NV21M,
		.codec_mode = MFC_FORMATS_NO_CODEC,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H264,
		.codec_mode = S5P_FIMV_CODEC_H264_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
	{
		.name = "MPEG4 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_MPEG4,
		.codec_mode = S5P_FIMV_CODEC_MPEG4_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H263,
		.codec_mode = S5P_FIMV_CODEC_H263_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

static struct s5p_mfc_fmt *find_format(struct v4l2_format *f, unsigned int t)
{
	unsigned int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == f->fmt.pix_mp.pixelformat &&
		    formats[i].type == t)
			return (struct s5p_mfc_fmt *)&formats[i];
	}

	return NULL;
}

static struct v4l2_queryctrl controls[] = {
	{
		.id = V4L2_CID_CACHEABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Cacheable flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_GOP_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The period of intra frame",
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_MODE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The slice partitioning method",
		.minimum = 0,
		.maximum = 3,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_MB,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The number of MB in a slice",
		.minimum = 1,
		.maximum = ENC_MULTI_SLICE_MB_MAX,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_BIT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The maximum bits per slices",
		.minimum = ENC_MULTI_SLICE_BIT_MIN,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = ENC_MULTI_SLICE_BIT_MIN,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_INTRA_REFRESH_MB,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The number of intra refresh MBs",
		.minimum = 0,
		.maximum = ENC_INTRA_REFRESH_MB_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_PAD_CTRL_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Padding control enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_PAD_LUMA_VALUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Y image's padding value",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_PAD_CB_VALUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Cb image's padding value",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_PAD_CR_VALUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Cr image's padding value",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_RC_FRAME_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Frame level rate control enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_RC_BIT_RATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Target bit rate rate-control",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_RC_REACTION_COEFF,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Rate control reaction coeff.",
		.minimum = 1,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_STREAM_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Encoded stream size",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_FRAME_COUNT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Encoded frame count",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_FRAME_TYPE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Encoded frame type",
		.minimum = 0,
		.maximum = 5,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_FORCE_FRAME_TYPE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Force frame type",
		.minimum = 1,
		.maximum = 2,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_VBV_BUF_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "VBV buffer size (1Kbits)",
		.minimum = 0,
		.maximum = ENC_VBV_BUF_SIZE_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_SEQ_HDR_MODE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Sequence header mode",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_FRAME_SKIP_MODE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame skip enable",
		.minimum = 0,
		.maximum = 2,
		.step = 1,
		.default_value = 0,
	},
	{	/* MFC5.x Only */
		.id = V4L2_CID_CODEC_MFC5X_ENC_RC_FIXED_TARGET_BIT,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Fixed target bit enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{	/* MFC6.x Only for H.264 & H.263 */
		.id = V4L2_CID_CODEC_MFC5X_ENC_FRAME_DELTA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 frame delta",
		.minimum = 1,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_B_FRAMES,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The number of B frames",
		.minimum = 0,
		.maximum = 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_PROFILE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 profile",
		.minimum = 0,
		.maximum = ENC_H264_PROFILE_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LEVEL,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 level",
		.minimum = 9,
		.maximum = ENC_H264_LEVEL_MAX,
		.step = 1,
		.default_value = 9,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_INTERLACE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 interface mode",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_MODE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 loop filter mode",
		.minimum = 0,
		.maximum = 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_ALPHA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 loop filter alpha offset",
		.minimum = ENC_H264_LOOP_FILTER_AB_MIN,
		.maximum = ENC_H264_LOOP_FILTER_AB_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_BETA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 loop filter beta offset",
		.minimum = ENC_H264_LOOP_FILTER_AB_MIN,
		.maximum = ENC_H264_LOOP_FILTER_AB_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ENTROPY_MODE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 entorpy mode",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{	/* reserved */
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_MAX_REF_PIC,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The max number of ref. picture",
		.minimum = 1,
		.maximum = 2,
		.step = 1,
		.default_value = 2,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_NUM_REF_PIC_4P,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The number of ref. picture of P",
		.minimum = 1,
		.maximum = 2,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_8X8_TRANSFORM,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 8x8 transform enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 MB level rate control",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{	/* MFC6.x Only */
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MB_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 MB level rate control",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{	/* MFC6.x Only */
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MB_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 MB level rate control",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_FRAME_RATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame rate",
		.minimum = 1,
		.maximum = ENC_H264_RC_FRAME_RATE_MAX,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Minimum QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		/* FIXME: MAX_QP must be greater than or equal to MIN_QP */
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Maximum QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_DARK,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 dark region adaptive",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_SMOOTH,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 smooth region adaptive",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_STATIC,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 static region adaptive",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_ACTIVITY,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 MB activity adaptive",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "P frame QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_RC_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "B frame QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_AR_VUI_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Aspect ratio VUI enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_AR_VUI_IDC,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "VUI aspect ratio IDC",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_EXT_SAR_WIDTH,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Horizontal size of SAR",
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_EXT_SAR_HEIGHT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Vertical size of SAR",
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_OPEN_GOP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Open GOP enable (I-picture)",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_I_PERIOD,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H264 I period",
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_HIER_P_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Hierarchical P flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER0_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "QP value for hier P layer0",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER1_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "QP value for hier P layer1",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{	/* FIXME: maximum value */
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER2_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "QP value for hier P layer2",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_FRAME_PACK_SEI_GEN,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "frame pack sei generation flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_FRAME_PACK_FRM0_FLAG,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Current frame is frame 0 flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_FRAME_PACK_ARRGMENT_TYPE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame packing arrangement type",
		.minimum = 3,
		.maximum = 5,
		.step = 1,
		.default_value = 3,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "FMO flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_MAP_TYPE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Map type for FMO",
		.minimum = 0,
		.maximum = 5,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SLICE_NUM,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Number of slice groups for FMO",
		.minimum = 1,
		.maximum = 4,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN1,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Consecutive macroblocks No.1",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN2,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Consecutive macroblocks No.2",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN3,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Consecutive macroblocks No.3",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN4,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Consecutive macroblocks No.4",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SG_DIR,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Direction of the slice group",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SG_RATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Size of the first slice group",
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "ASO flag",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_0,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.1",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_1,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.2",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_2,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.3",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_3,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.4",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_4,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.5",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_5,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.6",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_6,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.7",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_7,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Slice order No.8",
		.minimum = 0,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_B_FRAMES,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The number of B frames",
		.minimum = 0,
		.maximum = 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_PROFILE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 profile",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{	/* FIXME: maximum value */
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_LEVEL,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 level",
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Minimum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Maximum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{	/* MFC5.x Only */
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_QUARTER_PIXEL,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Quarter pixel search enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "P frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "B frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_VOP_TIME_RES,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 vop time resolution",
		.minimum = 0,
		.maximum = ENC_MPEG4_VOP_TIME_RES_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_MPEG4_VOP_FRM_DELTA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 frame delta",
		.minimum = 1,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_FRAME_RATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame rate",
		.minimum = 1,
		.maximum = ENC_H263_RC_FRAME_RATE_MAX,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Minimum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Maximum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_MFC5X_ENC_H263_RC_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "P frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_CODEC_FRAME_TAG,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame Tag",
		.minimum = 0,
		.maximum = INT_MAX,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_CODEC_FRAME_INSERTION,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Frame Tag",
		.minimum = 0,
		.maximum = INT_MAX,
		.step = 1,
		.default_value = 0,
	},
#if defined(CONFIG_S5P_MFC_VB2_ION)
	{
		.id		= V4L2_CID_SET_SHAREABLE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "File descriptor for ION",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 1,
	},
#endif
};

#define NUM_CTRLS ARRAY_SIZE(controls)

static struct v4l2_queryctrl *get_ctrl(int id)
{
	int i;

	for (i = 0; i < NUM_CTRLS; ++i)
		if (id == controls[i].id)
			return &controls[i];
	return NULL;
}

static int check_ctrl_val(struct s5p_mfc_ctx *ctx, struct v4l2_control *ctrl)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct v4l2_queryctrl *c;

	c = get_ctrl(ctrl->id);
	if (!c)
		return -EINVAL;
	if (ctrl->value < c->minimum || ctrl->value > c->maximum
	    || (c->step != 0 && ctrl->value % c->step != 0)) {
		v4l2_err(&dev->v4l2_dev, "Invalid control value\n");
		return -ERANGE;
	}
	return 0;
}

static struct s5p_mfc_ctrl_cfg mfc_ctrl_list[] = {
	{	/* set frame tag */
		.type = MFC_CTRL_TYPE_SET,
		.id = V4L2_CID_CODEC_FRAME_TAG,
		.is_volatile = 1,
		.mode = MFC_CTRL_MODE_CUSTOM,
		.addr = S5P_FIMV_SHARED_SET_E_FRAME_TAG,
		.mask = 0xFFFFFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_NONE,
		.flag_addr = 0,
		.flag_shft = 0,
	},
	{	/* get frame tag */
		.type = MFC_CTRL_TYPE_GET_DST,
		.id = V4L2_CID_CODEC_FRAME_TAG,
		.is_volatile = 0,
		.mode = MFC_CTRL_MODE_CUSTOM,
		.addr = S5P_FIMV_SHARED_GET_E_FRAME_TAG,
		.mask = 0xFFFFFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_NONE,
		.flag_addr = 0,
		.flag_shft = 0,
	},
	{	/* encoded y physical addr */
		.type = MFC_CTRL_TYPE_GET_DST,
		.id = V4L2_CID_CODEC_ENCODED_LUMA_ADDR,
		.is_volatile = 0,
		.mode = MFC_CTRL_MODE_SFR,
		.addr = S5P_FIMV_ENCODED_LUMA_ADDR,
		.mask = 0xFFFFFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_NONE,
		.flag_addr = 0,
		.flag_shft = 0,
	},
	{	/* encoded c physical addr */
		.type = MFC_CTRL_TYPE_GET_DST,
		.id = V4L2_CID_CODEC_ENCODED_CHROMA_ADDR,
		.is_volatile = 0,
		.mode = MFC_CTRL_MODE_SFR,
		.addr = S5P_FIMV_ENCODED_CHROMA_ADDR,
		.mask = 0xFFFFFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_NONE,
		.flag_addr = 0,
		.flag_shft = 0,
	},
	{	/* I, not coded frame insertion */
		.type = MFC_CTRL_TYPE_SET,
		.id = V4L2_CID_CODEC_FRAME_INSERTION,
		.is_volatile = 1,
		.mode = MFC_CTRL_MODE_SFR,
		.addr = S5P_FIMV_FRAME_INSERTION,
		.mask = 0x3,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_NONE,
		.flag_addr = 0,
		.flag_shft = 0,
	},
	{	/* I period change */
		.type = MFC_CTRL_TYPE_SET,
		.id = V4L2_CID_CODEC_ENCODED_I_PERIOD_CH,
		.is_volatile = 1,
		.mode = MFC_CTRL_MODE_CUSTOM,
		.addr = S5P_FIMV_NEW_I_PERIOD,
		.mask = 0xFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_CUSTOM,
		.flag_addr = S5P_FIMV_PARAM_CHANGE_FLAG,
		.flag_shft = 0,
	},
	{	/* frame rate change */
		.type = MFC_CTRL_TYPE_SET,
		.id = V4L2_CID_CODEC_ENCODED_FRAME_RATE_CH,
		.is_volatile = 1,
		.mode = MFC_CTRL_MODE_CUSTOM,
		.addr = S5P_FIMV_NEW_RC_FRAME_RATE,
		.mask = 0xFFFF,
		.shft = 16,
		.flag_mode = MFC_CTRL_MODE_CUSTOM,
		.flag_addr = S5P_FIMV_PARAM_CHANGE_FLAG,
		.flag_shft = 1,
	},
	{	/* bit rate change */
		.type = MFC_CTRL_TYPE_SET,
		.id = V4L2_CID_CODEC_ENCODED_BIT_RATE_CH,
		.is_volatile = 1,
		.mode = MFC_CTRL_MODE_CUSTOM,
		.addr = S5P_FIMV_NEW_RC_BIT_RATE,
		.mask = 0xFFFFFFFF,
		.shft = 0,
		.flag_mode = MFC_CTRL_MODE_CUSTOM,
		.flag_addr = S5P_FIMV_PARAM_CHANGE_FLAG,
		.flag_shft = 2,
	},
};

#define NUM_CTRL_CFGS ARRAY_SIZE(mfc_ctrl_list)

static int s5p_mfc_ctx_ready(struct s5p_mfc_ctx *ctx)
{
	mfc_debug(2, "src=%d, dst=%d, state=%d\n",
		  ctx->src_queue_cnt, ctx->dst_queue_cnt, ctx->state);

	/* context is ready to make header */
	if (ctx->state == MFCINST_GOT_INST && ctx->dst_queue_cnt >= 1)
		return 1;
	/* context is ready to allocate DPB */
	if (ctx->dst_queue_cnt >= 1 && ctx->state == MFCINST_HEAD_PARSED)
		return 1;
	/* context is ready to encode a frame */
	if (ctx->state == MFCINST_RUNNING &&
		ctx->src_queue_cnt >= 1 && ctx->dst_queue_cnt >= 1)
		return 1;
	/* context is ready to encode a frame in case of B frame */
	if (ctx->state == MFCINST_RUNNING_NO_OUTPUT &&
		ctx->src_queue_cnt >= 1 && ctx->dst_queue_cnt >= 1)
		return 1;
	/* context is ready to encode remain frames */
	if (ctx->state == MFCINST_FINISHING &&
		ctx->src_queue_cnt >= 1 && ctx->dst_queue_cnt >= 1)
		return 1;

	mfc_debug(2, "ctx is not ready.\n");

	return 0;
}

static int enc_init_ctx_ctrls(struct s5p_mfc_ctx *ctx)
{
	int i;
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;

	INIT_LIST_HEAD(&ctx->ctrls);

	for (i = 0; i < NUM_CTRL_CFGS; i++) {
		ctx_ctrl = kzalloc(sizeof(struct s5p_mfc_ctx_ctrl), GFP_KERNEL);
		if (ctx_ctrl == NULL) {
			mfc_err("failed to allocate ctx_ctrl type: %d, id: 0x%08x\n",
				mfc_ctrl_list[i].type, mfc_ctrl_list[i].id);

			return -ENOMEM;
		}

		ctx_ctrl->type = mfc_ctrl_list[i].type;
		ctx_ctrl->id = mfc_ctrl_list[i].id;
		ctx_ctrl->has_new = 0;
		ctx_ctrl->val = 0;

		list_add_tail(&ctx_ctrl->list, &ctx->ctrls);

		mfc_debug(5, "add ctx ctrl id: 0x%08x\n", ctx_ctrl->id);
	}

	return 0;
}

static int enc_cleanup_ctx_ctrls(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;

	while (!list_empty(&ctx->ctrls)) {
		ctx_ctrl = list_entry((&ctx->ctrls)->next,
				      struct s5p_mfc_ctx_ctrl, list);

		mfc_debug(5, "del ctx ctrl id: 0x%08x\n", ctx_ctrl->id);

		list_del(&ctx_ctrl->list);
		kfree(ctx_ctrl);
	}

	INIT_LIST_HEAD(&ctx->ctrls);

	return 0;
}


static int enc_init_buf_ctrls(struct s5p_mfc_ctx *ctx,
	enum s5p_mfc_ctrl_type type, unsigned int index)
{
	int i;
	struct s5p_mfc_buf_ctrl *buf_ctrl;
	struct list_head *head;

	if ((type == MFC_CTRL_TYPE_SET) && (ctx->src_ctrls_flag[index])) {
		mfc_debug(5, "ctx->src_ctrls[%d] is initialized\n", index);
		return 0;
	}
	if ((type == MFC_CTRL_TYPE_GET_DST) && (ctx->dst_ctrls_flag[index])) {
		mfc_debug(5, "ctx->dst_ctrls[%d] is initialized\n", index);
		return 0;
	}

	if (type == MFC_CTRL_TYPE_SET) {
		head = &ctx->src_ctrls[index];
		ctx->src_ctrls_flag[index] = 1;
	}
	else if (type == MFC_CTRL_TYPE_GET_DST) {
		head = &ctx->dst_ctrls[index];
		ctx->dst_ctrls_flag[index] = 1;
	}
	else
		return -EINVAL;

	INIT_LIST_HEAD(head);

	for (i = 0; i < NUM_CTRL_CFGS; i++) {
		if (type != mfc_ctrl_list[i].type)
			continue;

		buf_ctrl = kzalloc(sizeof(struct s5p_mfc_buf_ctrl), GFP_KERNEL);
		if (buf_ctrl == NULL) {
			mfc_err("failed to allocate buf_ctrl type: %d, id: 0x%08x\n",
				mfc_ctrl_list[i].type, mfc_ctrl_list[i].id);

			return -ENOMEM;
		}

		buf_ctrl->id = mfc_ctrl_list[i].id;
		buf_ctrl->has_new = 0;
		buf_ctrl->val = 0;
		buf_ctrl->old_val = 0;
		buf_ctrl->is_volatile = mfc_ctrl_list[i].is_volatile;
		buf_ctrl->mode = mfc_ctrl_list[i].mode;
		buf_ctrl->addr = mfc_ctrl_list[i].addr;
		buf_ctrl->mask = mfc_ctrl_list[i].mask;
		buf_ctrl->shft = mfc_ctrl_list[i].shft;
		buf_ctrl->flag_mode = mfc_ctrl_list[i].flag_mode;
		buf_ctrl->flag_addr = mfc_ctrl_list[i].flag_addr;
		buf_ctrl->flag_shft = mfc_ctrl_list[i].flag_shft;

		list_add_tail(&buf_ctrl->list, head);

		mfc_debug(5, "add buf ctrl id: 0x%08x\n", buf_ctrl->id);
	}

	return 0;
}

static int enc_cleanup_buf_ctrls(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_buf_ctrl *buf_ctrl;

	while (!list_empty(head)) {
		buf_ctrl = list_entry(head->next,
				      struct s5p_mfc_buf_ctrl, list);

		mfc_debug(5, "del buf ctrl id: 0x%08x\n",  buf_ctrl->id);

		list_del(&buf_ctrl->list);
		kfree(buf_ctrl);
	}

	INIT_LIST_HEAD(head);

	return 0;
}

static int enc_to_buf_ctrls(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;
	struct s5p_mfc_buf_ctrl *buf_ctrl;

	list_for_each_entry(ctx_ctrl, &ctx->ctrls, list) {
		if ((ctx_ctrl->type != MFC_CTRL_TYPE_SET) || (!ctx_ctrl->has_new))
			continue;

		list_for_each_entry(buf_ctrl, head, list) {
			if (buf_ctrl->id == ctx_ctrl->id) {
				buf_ctrl->has_new = 1;
				buf_ctrl->val = ctx_ctrl->val;
				if (buf_ctrl->is_volatile)
					buf_ctrl->updated = 0;

				ctx_ctrl->has_new = 0;
				break;
			}
		}
	}

	list_for_each_entry(buf_ctrl, head, list) {
		if (buf_ctrl->has_new)
			mfc_debug(5, "id: 0x%08x val: %d\n",
				 buf_ctrl->id, buf_ctrl->val);
	}

	return 0;
}

static int enc_to_ctx_ctrls(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;
	struct s5p_mfc_buf_ctrl *buf_ctrl;

	list_for_each_entry(buf_ctrl, head, list) {
		if (!buf_ctrl->has_new)
			continue;

		list_for_each_entry(ctx_ctrl, &ctx->ctrls, list) {
			if (ctx_ctrl->type != MFC_CTRL_TYPE_GET_DST)
				continue;

			if (ctx_ctrl->id == buf_ctrl->id) {
				mfc_debug(!ctx_ctrl->has_new, "overwrite ctx ctrl value\n");

				ctx_ctrl->has_new = 1;
				ctx_ctrl->val = buf_ctrl->val;

				buf_ctrl->has_new = 0;
			}
		}
	}

	list_for_each_entry(ctx_ctrl, &ctx->ctrls, list) {
		if (ctx_ctrl->has_new)
			mfc_debug(5, "id: 0x%08x val: %d\n",
				  ctx_ctrl->id, ctx_ctrl->val);
	}

	return 0;
}

static int enc_set_buf_ctrls_val(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_buf_ctrl *buf_ctrl;
	unsigned int value = 0;

	mfc_debug_enter();

	list_for_each_entry(buf_ctrl, head, list) {
		if (!buf_ctrl->has_new)
			continue;

		/* read old vlaue */
		if (buf_ctrl->mode == MFC_CTRL_MODE_SFR)
			value = s5p_mfc_read_reg(buf_ctrl->addr);
		else if (buf_ctrl->mode == MFC_CTRL_MODE_SHM)
			value = s5p_mfc_read_info(ctx, buf_ctrl->addr);

		/* save old value for recovery */
		if (buf_ctrl->is_volatile)
			buf_ctrl->old_val = (value >> buf_ctrl->shft) & buf_ctrl->mask;

		/* write new value */
		value &= ~(buf_ctrl->mask << buf_ctrl->shft);
		value |= ((buf_ctrl->val & buf_ctrl->mask) << buf_ctrl->shft);

		if (buf_ctrl->mode == MFC_CTRL_MODE_SFR)
			s5p_mfc_write_reg(value, buf_ctrl->addr);
		else if (buf_ctrl->mode == MFC_CTRL_MODE_SHM)
			s5p_mfc_write_info(ctx, value, buf_ctrl->addr);

		/* set change flag bit */
		if (buf_ctrl->flag_mode == MFC_CTRL_MODE_SFR) {
			value = s5p_mfc_read_reg(buf_ctrl->flag_addr);
			value |= (1 << buf_ctrl->flag_shft);
			s5p_mfc_write_reg(value, buf_ctrl->flag_addr);
		} else if (buf_ctrl->flag_mode == MFC_CTRL_MODE_SHM) {
			value = s5p_mfc_read_info(ctx, buf_ctrl->flag_addr);
			value |= (1 << buf_ctrl->flag_shft);
			s5p_mfc_write_info(ctx, value, buf_ctrl->flag_addr);
		}

		buf_ctrl->has_new = 0;
		buf_ctrl->updated = 1;

		mfc_debug(5, "id: 0x%08x val: %d\n", buf_ctrl->id,
			  buf_ctrl->val);
	}

	mfc_debug_leave();

	return 0;
}

static int enc_get_buf_ctrls_val(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_buf_ctrl *buf_ctrl;
	unsigned int value = 0;

	list_for_each_entry(buf_ctrl, head, list) {
		if (buf_ctrl->mode == MFC_CTRL_MODE_SFR)
			value = s5p_mfc_read_reg(buf_ctrl->addr);
		else if (buf_ctrl->mode == MFC_CTRL_MODE_SHM)
			value = s5p_mfc_read_info(ctx, buf_ctrl->addr);

		value = (value >> buf_ctrl->shft) & buf_ctrl->mask;

		buf_ctrl->val = value;
		buf_ctrl->has_new = 1;

		mfc_debug(5, "id: 0x%08x val: %d\n", buf_ctrl->id,
			  buf_ctrl->val);
	}

	return 0;
}

static void cleanup_ref_queue(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_buf *mb_entry;
	unsigned long mb_y_addr, mb_c_addr;

	/* move buffers in ref queue to src queue */
	while (!list_empty(&enc->ref_queue)) {
		mb_entry = list_entry((&enc->ref_queue)->next, struct s5p_mfc_buf, list);

		mb_y_addr = mfc_plane_cookie(&mb_entry->vb, 0);
		mb_c_addr = mfc_plane_cookie(&mb_entry->vb, 1);

		mfc_debug(2, "enc ref y addr: 0x%08lx", mb_y_addr);
		mfc_debug(2, "enc ref c addr: 0x%08lx", mb_c_addr);

		list_del(&mb_entry->list);
		enc->ref_queue_cnt--;

		list_add_tail(&mb_entry->list, &ctx->src_queue);
		ctx->src_queue_cnt++;
	}

	mfc_debug(2, "enc src count: %d, enc ref count: %d\n",
		  ctx->src_queue_cnt, enc->ref_queue_cnt);

	INIT_LIST_HEAD(&enc->ref_queue);
	enc->ref_queue_cnt = 0;
}

static int enc_pre_seq_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	unsigned long dst_addr;
	unsigned int dst_size;
	unsigned long flags;

	spin_lock_irqsave(&dev->irqlock, flags);

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = mfc_plane_cookie(&dst_mb->vb, 0);
	dst_size = vb2_plane_size(&dst_mb->vb, 0);
	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	return 0;
}

static int enc_post_seq_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	struct s5p_mfc_buf *dst_mb;
	unsigned long flags;

	mfc_debug(2, "seq header size: %d", s5p_mfc_get_enc_strm_size());

	if (p->seq_hdr_mode == V4L2_CODEC_MFC5X_ENC_SEQ_HDR_MODE_SEQ) {
		spin_lock_irqsave(&dev->irqlock, flags);

		dst_mb = list_entry(ctx->dst_queue.next,
				struct s5p_mfc_buf, list);
		list_del(&dst_mb->list);
		ctx->dst_queue_cnt--;

		vb2_set_plane_payload(&dst_mb->vb, 0, s5p_mfc_get_enc_strm_size());
		vb2_buffer_done(&dst_mb->vb, VB2_BUF_STATE_DONE);

		spin_unlock_irqrestore(&dev->irqlock, flags);
	}

	if (IS_MFCV6(dev))
		ctx->state = MFCINST_HEAD_PARSED; /* for INIT_BUFFER cmd */
	else {
		ctx->state = MFCINST_RUNNING;

		if (s5p_mfc_ctx_ready(ctx)) {
			spin_lock_irqsave(&dev->condlock, flags);
			set_bit(ctx->num, &dev->ctx_work_bits);
			spin_unlock_irqrestore(&dev->condlock, flags);
		}
		s5p_mfc_try_run(dev);
	}
	if (IS_MFCV6(dev))
		ctx->dpb_count = s5p_mfc_get_enc_dpb_count();

	return 0;
}

static int enc_pre_frame_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	struct s5p_mfc_buf *src_mb;
	unsigned long flags;
	unsigned long src_y_addr, src_c_addr, dst_addr;
	unsigned int dst_size;

	spin_lock_irqsave(&dev->irqlock, flags);

	src_mb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	src_y_addr = mfc_plane_cookie(&src_mb->vb, 0);
	src_c_addr = mfc_plane_cookie(&src_mb->vb, 1);
	s5p_mfc_set_enc_frame_buffer(ctx, src_y_addr, src_c_addr);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	mfc_debug(2, "enc src y addr: 0x%08lx", src_y_addr);
	mfc_debug(2, "enc src c addr: 0x%08lx", src_c_addr);

	spin_lock_irqsave(&dev->irqlock, flags);

	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = mfc_plane_cookie(&dst_mb->vb, 0);
	dst_size = vb2_plane_size(&dst_mb->vb, 0);
	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	mfc_debug(2, "enc dst addr: 0x%08lx", dst_addr);

	return 0;
}

static int enc_post_frame_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_buf *mb_entry;
	unsigned long enc_y_addr, enc_c_addr;
	unsigned long mb_y_addr, mb_c_addr;
	int slice_type;
	unsigned int strm_size;
	unsigned int pic_count;
	unsigned long flags;
	unsigned int index;

	slice_type = s5p_mfc_get_enc_slice_type();
	strm_size = s5p_mfc_get_enc_strm_size();
	pic_count = s5p_mfc_get_enc_pic_count();

	mfc_debug(2, "encoded slice type: %d", slice_type);
	mfc_debug(2, "encoded stream size: %d", strm_size);
	mfc_debug(2, "display order: %d", pic_count);

	/* FIXME: set it to dest buffer not context */
	/* set encoded frame type */
	enc->frame_type = slice_type;

	spin_lock_irqsave(&dev->irqlock, flags);

	if (slice_type >= 0) {
		if (ctx->state == MFCINST_RUNNING_NO_OUTPUT)
			ctx->state = MFCINST_RUNNING;

		s5p_mfc_get_enc_frame_buffer(ctx, &enc_y_addr, &enc_c_addr);

		mfc_debug(2, "encoded y addr: 0x%08lx", enc_y_addr);
		mfc_debug(2, "encoded c addr: 0x%08lx", enc_c_addr);

		list_for_each_entry(mb_entry, &ctx->src_queue, list) {
			mb_y_addr = mfc_plane_cookie(&mb_entry->vb, 0);
			mb_c_addr = mfc_plane_cookie(&mb_entry->vb, 1);

			mfc_debug(2, "enc src y addr: 0x%08lx", mb_y_addr);
			mfc_debug(2, "enc src c addr: 0x%08lx", mb_c_addr);

			mb_entry = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
			index = mb_entry->vb.v4l2_buf.index;
			if (call_cop(ctx, recover_buf_ctrls_val, ctx, &ctx->src_ctrls[index]) < 0)
				mfc_err("failed in recover_buf_ctrls_val\n");

			if ((enc_y_addr == mb_y_addr) && (enc_c_addr == mb_c_addr)) {
				list_del(&mb_entry->list);
				ctx->src_queue_cnt--;

				vb2_buffer_done(&mb_entry->vb, VB2_BUF_STATE_DONE);
				break;
			}
		}

		list_for_each_entry(mb_entry, &enc->ref_queue, list) {
			mb_y_addr = mfc_plane_cookie(&mb_entry->vb, 0);
			mb_c_addr = mfc_plane_cookie(&mb_entry->vb, 1);

			mfc_debug(2, "enc ref y addr: 0x%08lx", mb_y_addr);
			mfc_debug(2, "enc ref c addr: 0x%08lx", mb_c_addr);

			if ((enc_y_addr == mb_y_addr) && (enc_c_addr == mb_c_addr)) {
				list_del(&mb_entry->list);
				enc->ref_queue_cnt--;

				vb2_buffer_done(&mb_entry->vb, VB2_BUF_STATE_DONE);
				break;
			}
		}
	}

	if ((ctx->src_queue_cnt > 0) &&
		((ctx->state == MFCINST_RUNNING) ||
		 (ctx->state == MFCINST_RUNNING_NO_OUTPUT))) {
		mb_entry = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);

		if (mb_entry->used) {
			list_del(&mb_entry->list);
			ctx->src_queue_cnt--;

			list_add_tail(&mb_entry->list, &enc->ref_queue);
			enc->ref_queue_cnt++;
		}
		/* FIXME: slice_type = 4 && strm_size = 0, skipped enable
		   should be considered */
		if ((slice_type == -1) && (strm_size == 0))
			ctx->state = MFCINST_RUNNING_NO_OUTPUT;

		mfc_debug(2, "slice_type: %d, ctx->state: %d \n", slice_type, ctx->state);
		mfc_debug(2, "enc src count: %d, enc ref count: %d\n",
			  ctx->src_queue_cnt, enc->ref_queue_cnt);
	}

	if (strm_size > 0) {
		/* at least one more dest. buffers exist always  */
		mb_entry = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);

		mb_entry->vb.v4l2_buf.flags &=
			(V4L2_BUF_FLAG_KEYFRAME &
			 V4L2_BUF_FLAG_PFRAME &
			 V4L2_BUF_FLAG_BFRAME);

		switch (slice_type) {
		case S5P_FIMV_DECODE_FRAME_I_FRAME:
			mb_entry->vb.v4l2_buf.flags |=
				V4L2_BUF_FLAG_KEYFRAME;
			break;
		case S5P_FIMV_DECODE_FRAME_P_FRAME:
			mb_entry->vb.v4l2_buf.flags |=
				V4L2_BUF_FLAG_PFRAME;
			break;
		case S5P_FIMV_DECODE_FRAME_B_FRAME:
			mb_entry->vb.v4l2_buf.flags |=
				V4L2_BUF_FLAG_BFRAME;
			break;
		default:
			mb_entry->vb.v4l2_buf.flags |=
				V4L2_BUF_FLAG_KEYFRAME;
			break;
		}
		mfc_debug(2, "Slice type : %d\n", mb_entry->vb.v4l2_buf.flags);

		list_del(&mb_entry->list);
		ctx->dst_queue_cnt--;
		vb2_set_plane_payload(&mb_entry->vb, 0, strm_size);
		vb2_buffer_done(&mb_entry->vb, VB2_BUF_STATE_DONE);

		index = mb_entry->vb.v4l2_buf.index;
		if (call_cop(ctx, get_buf_ctrls_val, ctx, &ctx->dst_ctrls[index]) < 0)
			mfc_err("failed in get_buf_ctrls_val\n");
	}

	spin_unlock_irqrestore(&dev->irqlock, flags);

	if ((ctx->src_queue_cnt == 0) || (ctx->dst_queue_cnt == 0))
	/*
		clear_work_bit(ctx);
	*/
	{
		spin_lock(&dev->condlock);
		clear_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock(&dev->condlock);
	}

	return 0;
}

static int enc_recover_buf_ctrls_val(struct s5p_mfc_ctx *ctx, struct list_head *head)
{
	struct s5p_mfc_buf_ctrl *buf_ctrl;
	unsigned int value = 0;

	list_for_each_entry(buf_ctrl, head, list) {
		if ((!buf_ctrl->is_volatile) || (!buf_ctrl->updated))
			continue;

		if (buf_ctrl->mode == MFC_CTRL_MODE_SFR)
			value = s5p_mfc_read_reg(buf_ctrl->addr);
		else if (buf_ctrl->mode == MFC_CTRL_MODE_SHM)
			value = s5p_mfc_read_info(ctx, buf_ctrl->addr);

		value &= ~(buf_ctrl->mask << buf_ctrl->shft);
		value |= ((buf_ctrl->old_val & buf_ctrl->mask) << buf_ctrl->shft);

		if (buf_ctrl->mode == MFC_CTRL_MODE_SFR)
			s5p_mfc_write_reg(value, buf_ctrl->addr);
		else if (buf_ctrl->mode == MFC_CTRL_MODE_SHM)
			s5p_mfc_write_info(ctx, value, buf_ctrl->addr);

		/* clear change flag bit */
		if (buf_ctrl->flag_mode == MFC_CTRL_MODE_SFR) {
			value = s5p_mfc_read_reg(buf_ctrl->flag_addr);
			value &= ~(1 << buf_ctrl->flag_shft);
			s5p_mfc_write_reg(value, buf_ctrl->flag_addr);
		} else if (buf_ctrl->flag_mode == MFC_CTRL_MODE_SHM) {
			value = s5p_mfc_read_info(ctx, buf_ctrl->flag_addr);
			value &= ~(1 << buf_ctrl->flag_shft);
			s5p_mfc_write_info(ctx, value, buf_ctrl->flag_addr);
		}

		mfc_debug(5, "id: 0x%08x old_val: %d\n", buf_ctrl->id,
			  buf_ctrl->old_val);
	}

	return 0;
}

static struct s5p_mfc_codec_ops encoder_codec_ops = {
	.init_ctx_ctrls		= enc_init_ctx_ctrls,
	.cleanup_ctx_ctrls	= enc_cleanup_ctx_ctrls,
	.init_buf_ctrls		= enc_init_buf_ctrls,
	.cleanup_buf_ctrls	= enc_cleanup_buf_ctrls,
	.to_buf_ctrls		= enc_to_buf_ctrls,
	.to_ctx_ctrls		= enc_to_ctx_ctrls,
	.set_buf_ctrls_val	= enc_set_buf_ctrls_val,
	.get_buf_ctrls_val	= enc_get_buf_ctrls_val,
	.recover_buf_ctrls_val	= enc_recover_buf_ctrls_val,
	.pre_seq_start		= enc_pre_seq_start,
	.post_seq_start		= enc_post_seq_start,
	.pre_frame_start	= enc_pre_frame_start,
	.post_frame_start	= enc_post_frame_start,
};

/* Query capabilities of the device */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s5p_mfc_dev *dev = video_drvdata(file);

	strncpy(cap->driver, dev->plat_dev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, dev->plat_dev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE
			  | V4L2_CAP_VIDEO_OUTPUT
			  | V4L2_CAP_STREAMING;

	return 0;
}

static int vidioc_enum_fmt(struct v4l2_fmtdesc *f, bool mplane, bool out)
{
	struct s5p_mfc_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (mplane && formats[i].num_planes == 1)
			continue;
		else if (!mplane && formats[i].num_planes > 1)
			continue;
		if (out && formats[i].type != MFC_FMT_RAW)
			continue;
		else if (!out && formats[i].type != MFC_FMT_ENC)
			continue;

		if (j == f->index) {
			fmt = &formats[i];
			strlcpy(f->description, fmt->name,
				sizeof(f->description));
			f->pixelformat = fmt->fourcc;

			return 0;
		}

		++j;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *pirv,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, false, false);
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *pirv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, true, false);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *prov,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, false, true);
}

static int vidioc_enum_fmt_vid_out_mplane(struct file *file, void *prov,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, true, true);
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;

	mfc_debug_enter();

	mfc_debug(2, "f->type = %d ctx->state = %d\n", f->type, ctx->state);

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		/* This is run on output (encoder dest) */
		pix_fmt_mp->width = 0;
		pix_fmt_mp->height = 0;
		pix_fmt_mp->field = V4L2_FIELD_NONE;
		pix_fmt_mp->pixelformat = ctx->dst_fmt->fourcc;
		pix_fmt_mp->num_planes = ctx->dst_fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].bytesperline = enc->dst_buf_size;
		pix_fmt_mp->plane_fmt[0].sizeimage = enc->dst_buf_size;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* This is run on capture (encoder src) */
		pix_fmt_mp->width = ctx->img_width;
		pix_fmt_mp->height = ctx->img_height;
		/* FIXME: interlace */
		pix_fmt_mp->field = V4L2_FIELD_NONE;
		pix_fmt_mp->pixelformat = ctx->src_fmt->fourcc;
		pix_fmt_mp->num_planes = ctx->src_fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[0].sizeimage = ctx->luma_size;
		pix_fmt_mp->plane_fmt[1].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[1].sizeimage = ctx->chroma_size;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}

	mfc_debug_leave();

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt = find_format(f, MFC_FMT_ENC);
		if (!fmt) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}

		if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
			mfc_err("must be set encoding output size\n");
			return -EINVAL;
		}

		pix_fmt_mp->plane_fmt[0].bytesperline =
			pix_fmt_mp->plane_fmt[0].sizeimage;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fmt = find_format(f, MFC_FMT_RAW);
		if (!fmt) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}

		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}

		/* FIXME: check below items */
		/*
		pix_fmt_mp->height;
		pix_fmt_mp->width;

		pix_fmt_mp->plane_fmt[0].bytesperline;  - buf_width
		pix_fmt_mp->plane_fmt[0].sizeimage;	- luma
		pix_fmt_mp->plane_fmt[1].bytesperline;  - buf_width
		pix_fmt_mp->plane_fmt[1].sizeimage;	- chroma
		*/
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_dev *dev = video_drvdata(file);
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	unsigned long flags;
	int ret = 0;

	mfc_debug_enter();

	ret = vidioc_try_fmt(file, priv, f);
	if (ret)
		return ret;

	if (ctx->vq_src.streaming || ctx->vq_dst.streaming) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt = find_format(f, MFC_FMT_ENC);
		if (!fmt) {
			mfc_err("failed to set capture format\n");
			return -EINVAL;
		}
		ctx->state = MFCINST_INIT;

		ctx->dst_fmt = fmt;
		ctx->codec_mode = ctx->dst_fmt->codec_mode;
		mfc_debug(2, "codec number: %d\n", ctx->dst_fmt->codec_mode);

		/* CHKME: 2KB aligned, multiple of 4KB - it may be ok with SDVMM */
		enc->dst_buf_size = pix_fmt_mp->plane_fmt[0].sizeimage;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;

		ctx->capture_state = QUEUE_FREE;

		s5p_mfc_alloc_instance_buffer(ctx);

		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);

		s5p_mfc_clean_ctx_int_flags(ctx);
		s5p_mfc_try_run(dev);
		if (s5p_mfc_wait_for_done_ctx(ctx, \
				S5P_FIMV_R2H_CMD_OPEN_INSTANCE_RET, 1)) {
			/* Error or timeout */
			mfc_err("Error getting instance from hardware.\n");
			s5p_mfc_release_instance_buffer(ctx);
			ret = -EIO;
			goto out;
		}
		mfc_debug(2, "Got instance number: %d\n", ctx->inst_no);
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fmt = find_format(f, MFC_FMT_RAW);
		if (!fmt) {
			mfc_err("failed to set output format\n");
			return -EINVAL;
		}
		if (!IS_MFCV6(dev)) {
			if (fmt->fourcc == V4L2_PIX_FMT_NV12MT_16X16) {
				mfc_err("Not supported format.\n");
				return -EINVAL;
			}
		} else if (IS_MFCV6(dev)) {
			if (fmt->fourcc == V4L2_PIX_FMT_NV12MT) {
				mfc_err("Not supported format.\n");
				return -EINVAL;
			}
		}

		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			mfc_err("failed to set output format\n");
			ret = -EINVAL;
			goto out;
		}

		/* FIXME: Can be change source format in encoding? */
		ctx->src_fmt = fmt;
		ctx->img_width = pix_fmt_mp->width;
		ctx->img_height = pix_fmt_mp->height;

		mfc_debug(2, "codec number: %d\n", ctx->src_fmt->codec_mode);
		mfc_debug(2, "fmt - w: %d, h: %d, ctx - w: %d, h: %d\n",
			pix_fmt_mp->width, pix_fmt_mp->height,
			ctx->img_width, ctx->img_height);

		s5p_mfc_enc_calc_src_size(ctx);

		ctx->output_state = QUEUE_FREE;

		pix_fmt_mp->plane_fmt[0].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[0].sizeimage = ctx->luma_size;
		pix_fmt_mp->plane_fmt[1].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[1].sizeimage = ctx->chroma_size;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
out:
	mfc_debug_leave();
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
					  struct v4l2_requestbuffers *reqbufs)
{
	struct s5p_mfc_dev *dev = video_drvdata(file);
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret = 0;

	mfc_debug_enter();

	mfc_debug(2, "type: %d\n", reqbufs->memory);

	/* if memory is not mmp or userptr return error */
	if ((reqbufs->memory != V4L2_MEMORY_MMAP) &&
		(reqbufs->memory != V4L2_MEMORY_USERPTR))
		return -EINVAL;

#if defined(CONFIG_S5P_MFC_VB2_ION)
	if (ctx->fd_ion < 0) {
		mfc_err("ION file descriptor is not set.\n");
		return -EINVAL;
	}
	mfc_debug(2, "ION fd from Driver : %d\n", ctx->fd_ion);
	vb2_ion_set_sharable(ctx->dev->alloc_ctx[MFC_CMA_BANK2_ALLOC_CTX], (bool)ctx->fd_ion);
#endif

	if (reqbufs->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		/* RMVME: s5p_mfc_buf_negotiate() ctx state checked */
		/*
		if (ctx->state != MFCINST_GOT_INST) {
			mfc_err("invalid context state: %d\n", ctx->state);
			return -EINVAL;
		}
		*/
		/* FIXME: check it out in the MFC6.1 */
		s5p_mfc_mem_set_cacheable(ctx->dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX],true);
		if (ctx->capture_state != QUEUE_FREE) {
			mfc_err("invalid capture state: %d\n", ctx->capture_state);
			return -EINVAL;
		}

		ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
		if (ret != 0) {
			mfc_err("error in vb2_reqbufs() for E(D)\n");
			return ret;
		}
		ctx->capture_state = QUEUE_BUFS_REQUESTED;

		if (!IS_MFCV6(dev)) {
		ret = s5p_mfc_alloc_codec_buffers(ctx);
			if (ret) {
				mfc_err("Failed to allocate encoding buffers.\n");
				reqbufs->count = 0;
				ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
				return -ENOMEM;
			}
		}
	} else if (reqbufs->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* cacheable setting */
		if (!IS_MFCV6(dev))
			s5p_mfc_mem_set_cacheable(ctx->dev->alloc_ctx[MFC_CMA_BANK2_ALLOC_CTX],ctx->cacheable);
		else
			s5p_mfc_mem_set_cacheable(ctx->dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX],ctx->cacheable);
		if (ctx->output_state != QUEUE_FREE) {
			mfc_err("invalid output state: %d\n", ctx->output_state);
			return -EINVAL;
		}

		ret = vb2_reqbufs(&ctx->vq_src, reqbufs);
		if (ret != 0) {
			mfc_err("error in vb2_reqbufs() for E(S)\n");
			return ret;
		}
		ctx->output_state = QUEUE_BUFS_REQUESTED;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}

	mfc_debug(2, "--\n");

	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
						   struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret = 0;

	mfc_debug_enter();

	mfc_debug(2, "type: %d\n", buf->memory);

	/* if memory is not mmp or userptr return error */
	if ((buf->memory != V4L2_MEMORY_MMAP) &&
		(buf->memory != V4L2_MEMORY_USERPTR))
		return -EINVAL;

	mfc_debug(2, "state: %d, buf->type: %d\n", ctx->state, buf->type);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->state != MFCINST_GOT_INST) {
			mfc_err("invalid context state: %d\n", ctx->state);
			return -EINVAL;
		}

		/*
		if (ctx->capture_state != QUEUE_BUFS_REQUESTED) {
			mfc_err("invalid capture state: %d\n", ctx->capture_state);
			return -EINVAL;
		}
		*/

		ret = vb2_querybuf(&ctx->vq_dst, buf);
		if (ret != 0) {
			mfc_err("error in vb2_querybuf() for E(D)\n");
			return ret;
		}
		buf->m.planes[0].m.mem_offset += DST_QUEUE_OFF_BASE;

		/*
		ctx->capture_state = QUEUE_BUFS_QUERIED;
		*/
	} else if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* FIXME: check context state */
		/*
		if (ctx->output_state != QUEUE_BUFS_REQUESTED) {
			mfc_err("invalid output state: %d\n", ctx->output_state);
			return -EINVAL;
		}
		*/

		ret = vb2_querybuf(&ctx->vq_src, buf);
		if (ret != 0) {
			mfc_err("error in vb2_querybuf() for E(S)\n");
			return ret;
		}

		/*
		ctx->output_state = QUEUE_BUFS_QUERIED;
		*/
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}

	mfc_debug_leave();

	return ret;
}

/* Queue a buffer */
static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);

	mfc_debug_enter();
	mfc_debug(2, "Enqueued buf: %d (type = %d)\n", buf->index, buf->type);
	if (ctx->state == MFCINST_ERROR) {
		mfc_err("Call on QBUF after unrecoverable error.\n");
		return -EIO;
	}
	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return vb2_qbuf(&ctx->vq_src, buf);
	else
		return vb2_qbuf(&ctx->vq_dst, buf);
	mfc_debug_leave();
	return -EINVAL;
}

/* Dequeue a buffer */
static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret;

	mfc_debug_enter();
	mfc_debug(2, "Addr: %p %p %p Type: %d\n", &ctx->vq_src, buf, buf->m.planes,
								buf->type);
	if (ctx->state == MFCINST_ERROR) {
		mfc_err("Call on DQBUF after unrecoverable error.\n");
		return -EIO;
	}
	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		ret = vb2_dqbuf(&ctx->vq_src, buf, file->f_flags & O_NONBLOCK);
	else
		ret = vb2_dqbuf(&ctx->vq_dst, buf, file->f_flags & O_NONBLOCK);
	mfc_debug_leave();
	return ret;
}

/* Stream on */
static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret = -EINVAL;

	mfc_debug_enter();
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		ret = vb2_streamon(&ctx->vq_src, type);
	else
		ret = vb2_streamon(&ctx->vq_dst, type);
	mfc_debug(2, "ctx->src_queue_cnt = %d ctx->state = %d "
		  "ctx->dst_queue_cnt = %d ctx->dpb_count = %d\n",
		  ctx->src_queue_cnt, ctx->state, ctx->dst_queue_cnt,
		  ctx->dpb_count);
	mfc_debug_leave();
	return ret;
}

/* Stream off, which equals to a pause */
static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret;

	mfc_debug_enter();
	ret = -EINVAL;
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		ret = vb2_streamoff(&ctx->vq_src, type);
	else
		ret = vb2_streamoff(&ctx->vq_dst, type);
	mfc_debug_leave();
	return ret;
}

/* Query a ctrl */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	struct v4l2_queryctrl *c;

	c = get_ctrl(qc->id);
	if (!c)
		return -EINVAL;
	*qc = *c;
	return 0;
}

static int get_ctrl_val(struct s5p_mfc_ctx *ctx, struct v4l2_control *ctrl)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;
	int ret = 0;
	int check = 0;

	switch (ctrl->id) {
	case V4L2_CID_CACHEABLE:
		ctrl->value = ctx->cacheable;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_STREAM_SIZE:
		ctrl->value = enc->dst_buf_size;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_FRAME_COUNT:
		ctrl->value = enc->frame_count;
		break;
	/* FIXME: it doesn't need to return using GetConfig */
	case V4L2_CID_CODEC_MFC5X_ENC_FRAME_TYPE:
		ctrl->value = enc->frame_type;
		break;
	case V4L2_CID_CODEC_CHECK_STATE:
		if (ctx->state == MFCINST_RUNNING_NO_OUTPUT)
			ctrl->value = MFCSTATE_ENC_NO_OUTPUT;
		else
			ctrl->value = MFCSTATE_PROCESSING;
		break;
	case V4L2_CID_CODEC_FRAME_TAG:
	case V4L2_CID_CODEC_ENCODED_LUMA_ADDR:
	case V4L2_CID_CODEC_ENCODED_CHROMA_ADDR:
		list_for_each_entry(ctx_ctrl, &ctx->ctrls, list) {
			if (ctx_ctrl->type != MFC_CTRL_TYPE_GET_DST)
				continue;

			if (ctx_ctrl->id == ctrl->id) {
				if (ctx_ctrl->has_new) {
					ctx_ctrl->has_new = 0;
					ctrl->value = ctx_ctrl->val;
				} else {
					ctrl->value = 0;
				}
				check = 1;
				break;
			}
		}
		if (!check) {
			v4l2_err(&dev->v4l2_dev, "invalid control 0x%08x\n", ctrl->id);
			return -EINVAL;
		}
		break;
#if defined(CONFIG_S5P_MFC_VB2_ION)
	case V4L2_CID_SET_SHAREABLE:
		ctrl->value = ctx->fd_ion;
		break;
#endif
	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control\n");
		ret = -EINVAL;
	}

	return ret;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret = 0;

	mfc_debug_enter();
	ret = get_ctrl_val(ctx, ctrl);
	mfc_debug_leave();

	return ret;
}

static int set_enc_param(struct s5p_mfc_ctx *ctx, struct v4l2_control *ctrl)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_enc_params *p = &enc->params;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_CODEC_MFC5X_ENC_GOP_SIZE:
		p->gop_size = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_MODE:
		p->slice_mode = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_MB:
		p->slice_mb = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MULTI_SLICE_BIT:
		p->slice_bit = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_INTRA_REFRESH_MB:
		p->intra_refresh_mb = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_PAD_CTRL_ENABLE:
		p->pad = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_PAD_LUMA_VALUE:
		p->pad_luma = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_PAD_CB_VALUE:
		p->pad_cb = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_PAD_CR_VALUE:
		p->pad_cr = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_RC_FRAME_ENABLE:
		p->rc_frame = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_RC_BIT_RATE:
		p->rc_bitrate = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_RC_REACTION_COEFF:
		p->rc_reaction_coeff = ctrl->value;
		break;
	/* FIXME: why is it used ? */
	case V4L2_CID_CODEC_MFC5X_ENC_FORCE_FRAME_TYPE:
		enc->force_frame_type = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_VBV_BUF_SIZE:
		p->vbv_buf_size = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_SEQ_HDR_MODE:
		p->seq_hdr_mode = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_FRAME_SKIP_MODE:
		p->frame_skip_mode = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_RC_FIXED_TARGET_BIT: /* MFC5.x Only */
		p->fixed_target_bit = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_B_FRAMES:
		p->codec.h264.num_b_frame = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_PROFILE:
		p->codec.h264.profile = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LEVEL:
		p->codec.h264.level = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_INTERLACE:
		p->codec.h264.interlace = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_MODE:
		p->codec.h264.loop_filter_mode = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_ALPHA:
		p->codec.h264.loop_filter_alpha = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LOOP_FILTER_BETA:
		p->codec.h264.loop_filter_beta = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ENTROPY_MODE:
		p->codec.h264.entropy_mode = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_MAX_REF_PIC: /* reserved */
		p->codec.h264.max_ref_pic = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_NUM_REF_PIC_4P:
		p->codec.h264.num_ref_pic_4p = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_8X8_TRANSFORM:
		p->codec.h264._8x8_transform = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_ENABLE:
		p->codec.h264.rc_mb = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_FRAME_RATE:
		p->codec.h264.rc_framerate = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_FRAME_QP:
		p->codec.h264.rc_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MIN_QP:
		p->codec.h264.rc_min_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MAX_QP:
		p->codec.h264.rc_max_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_DARK:
		p->codec.h264.rc_mb_dark = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_SMOOTH:
		p->codec.h264.rc_mb_smooth = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_STATIC:
		p->codec.h264.rc_mb_static = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_MB_ACTIVITY:
		p->codec.h264.rc_mb_activity = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_P_FRAME_QP:
		p->codec.h264.rc_p_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_RC_B_FRAME_QP:
		p->codec.h264.rc_b_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_AR_VUI_ENABLE:
		p->codec.h264.ar_vui = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_AR_VUI_IDC:
		p->codec.h264.ar_vui_idc = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_EXT_SAR_WIDTH:
		p->codec.h264.ext_sar_width = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_EXT_SAR_HEIGHT:
		p->codec.h264.ext_sar_height = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_OPEN_GOP:
		p->codec.h264.open_gop = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_I_PERIOD:
		p->codec.h264.open_gop_size = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_HIER_P_ENABLE:
		p->codec.h264.hier_p_enable = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER0_QP:
		p->codec.h264.hier_layer0_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER1_QP:
		p->codec.h264.hier_layer1_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_LAYER2_QP:
		p->codec.h264.hier_layer2_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_FRAME_PACK_SEI_GEN:
		p->codec.h264.sei_gen_enable = ctrl->value;
		break;
	case V4L2_CID_CODEC_FRAME_PACK_FRM0_FLAG:
		p->codec.h264.curr_frame_frm0_flag = ctrl->value;
		break;
	case V4L2_CID_CODEC_FRAME_PACK_ARRGMENT_TYPE:
		p->codec.h264.frame_pack_arrgment_type = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_ENABLE:
		p->codec.h264.fmo_enable = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_MAP_TYPE:
		p->codec.h264.fmo_slice_map_type = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SLICE_NUM:
		p->codec.h264.fmo_slice_num_grp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN1:
		p->codec.h264.fmo_run_length[0] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN2:
		p->codec.h264.fmo_run_length[1] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN3:
		p->codec.h264.fmo_run_length[2] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_RUN_LEN4:
		p->codec.h264.fmo_run_length[3] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SG_DIR:
		p->codec.h264.fmo_sg_dir = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_FMO_SG_RATE:
		p->codec.h264.fmo_sg_rate = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_ENABLE:
		p->codec.h264.aso_enable = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_0:
		p->codec.h264.aso_slice_order[0] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_1:
		p->codec.h264.aso_slice_order[1] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_2:
		p->codec.h264.aso_slice_order[2] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_3:
		p->codec.h264.aso_slice_order[3] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_4:
		p->codec.h264.aso_slice_order[4] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_5:
		p->codec.h264.aso_slice_order[5] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_6:
		p->codec.h264.aso_slice_order[6] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H264_ASO_SL_ORDER_7:
		p->codec.h264.aso_slice_order[7] = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_B_FRAMES:
		p->codec.mpeg4.num_b_frame = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_PROFILE:
		p->codec.mpeg4.profile = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_LEVEL:
		p->codec.mpeg4.level = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_FRAME_QP:
		p->codec.mpeg4.rc_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MIN_QP:
		p->codec.mpeg4.rc_min_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MAX_QP:
		p->codec.mpeg4.rc_max_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_QUARTER_PIXEL:
		p->codec.mpeg4.quarter_pixel = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_P_FRAME_QP:
		p->codec.mpeg4.rc_p_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_B_FRAME_QP:
		p->codec.mpeg4.rc_b_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_VOP_TIME_RES:
		p->codec.mpeg4.vop_time_res = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_VOP_FRM_DELTA:
		p->codec.mpeg4.vop_frm_delta = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_MPEG4_RC_MB_ENABLE:
		p->codec.mpeg4.rc_mb = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_FRAME_RATE:
		p->codec.mpeg4.rc_framerate = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_FRAME_QP:
		p->codec.mpeg4.rc_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MIN_QP:
		p->codec.mpeg4.rc_min_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MAX_QP:
		p->codec.mpeg4.rc_max_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_P_FRAME_QP:
		p->codec.mpeg4.rc_p_frame_qp = ctrl->value;
		break;
	case V4L2_CID_CODEC_MFC5X_ENC_H263_RC_MB_ENABLE:
		p->codec.mpeg4.rc_mb = ctrl->value;
		break;
	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control\n");
		ret = -EINVAL;
	}

	return ret;
}

static int set_ctrl_val(struct s5p_mfc_ctx *ctx, struct v4l2_control *ctrl)
{

	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_ctx_ctrl *ctx_ctrl;
	int ret = 0;
	int check = 0;

	switch (ctrl->id) {
	case V4L2_CID_CACHEABLE:
		/*if (stream_on)
			return -EBUSY; */
		if(ctrl->value == 0 || ctrl->value ==1)
			ctx->cacheable = ctrl->value;
		else
			ctx->cacheable = 0;
		break;
	case V4L2_CID_CODEC_FRAME_TAG:
	case V4L2_CID_CODEC_FRAME_INSERTION:
	case V4L2_CID_CODEC_ENCODED_I_PERIOD_CH:
	case V4L2_CID_CODEC_ENCODED_FRAME_RATE_CH:
	case V4L2_CID_CODEC_ENCODED_BIT_RATE_CH:
		list_for_each_entry(ctx_ctrl, &ctx->ctrls, list) {
			if (ctx_ctrl->type != MFC_CTRL_TYPE_SET)
				continue;
			if (ctx_ctrl->id == ctrl->id) {
				ctx_ctrl->has_new = 1;
				ctx_ctrl->val = ctrl->value;
				if (ctx_ctrl->id == V4L2_CID_CODEC_ENCODED_FRAME_RATE_CH)
					ctx_ctrl->val *= 1000;
				check = 1;
				break;
			}
		}
		if (!check) {
			v4l2_err(&dev->v4l2_dev,
				"invalid control 0x%08x\n", ctrl->id);
			return -EINVAL;
		}
		break;
#if defined(CONFIG_S5P_MFC_VB2_ION)
	case V4L2_CID_SET_SHAREABLE:
		ctx->fd_ion = ctrl->value;
		mfc_debug(2, "fd_ion : %d\n", ctx->fd_ion);
		break;
#endif
	default:
		ret = set_enc_param(ctx, ctrl);
		break;
	}

	return ret;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	int ret = 0;

	mfc_debug_enter();
	/*
	int stream_on;
	*/

	/*
	if (s5p_mfc_get_node_type(file) != MFCNODE_ENCODER)
		return -EINVAL;
	*/

	/* FIXME:
	stream_on = ctx->vq_src.streaming || ctx->vq_dst.streaming;
	*/

	ret = check_ctrl_val(ctx, ctrl);
	if (ret != 0)
		return ret;

	ret = set_ctrl_val(ctx, ctrl);

	mfc_debug_leave();

	return ret;
}

static int vidioc_g_ext_ctrls(struct file *file, void *priv,
			      struct v4l2_ext_controls *f)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	struct v4l2_ext_control *ext_ctrl;
	struct v4l2_control ctrl;
	int i;
	int ret = 0;

	/*
	if (s5p_mfc_get_node_type(file) != MFCNODE_ENCODER)
		return -EINVAL;
	*/

	if (f->ctrl_class != V4L2_CTRL_CLASS_CODEC)
		return -EINVAL;

	for (i = 0; i < f->count; i++) {
		ext_ctrl = (f->controls + i);

		ctrl.id = ext_ctrl->id;

		ret = get_ctrl_val(ctx, &ctrl);
		if (ret == 0) {
			ext_ctrl->value = ctrl.value;
		} else {
			f->error_idx = i;
			break;
		}

		mfc_debug(2, "[%d] id: 0x%08x, value: %d", i, ext_ctrl->id, ext_ctrl->value);
	}

	return ret;
}

static int vidioc_s_ext_ctrls(struct file *file, void *priv,
				struct v4l2_ext_controls *f)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	struct v4l2_ext_control *ext_ctrl;
	struct v4l2_control ctrl;
	int i;
	int ret = 0;

	/*
	if (s5p_mfc_get_node_type(file) != MFCNODE_ENCODER)
		return -EINVAL;
	*/

	if (f->ctrl_class != V4L2_CTRL_CLASS_CODEC)
		return -EINVAL;

	for (i = 0; i < f->count; i++) {
		ext_ctrl = (f->controls + i);

		ctrl.id = ext_ctrl->id;
		ctrl.value = ext_ctrl->value;

		ret = check_ctrl_val(ctx, &ctrl);
		if (ret != 0) {
			f->error_idx = i;
			break;
		}

		ret = set_enc_param(ctx, &ctrl);
		if (ret != 0) {
			f->error_idx = i;
			break;
		}

		mfc_debug(2, "[%d] id: 0x%08x, value: %d", i, ext_ctrl->id, ext_ctrl->value);
	}

	return ret;
}

static int vidioc_try_ext_ctrls(struct file *file, void *priv,
				struct v4l2_ext_controls *f)
{
	struct s5p_mfc_ctx *ctx = fh_to_mfc_ctx(file->private_data);
	struct v4l2_ext_control *ext_ctrl;
	struct v4l2_control ctrl;
	int i;
	int ret = 0;

	/*
	if (s5p_mfc_get_node_type(file) != MFCNODE_ENCODER)
		return -EINVAL;
	*/

	if (f->ctrl_class != V4L2_CTRL_CLASS_CODEC)
		return -EINVAL;

	for (i = 0; i < f->count; i++) {
		ext_ctrl = (f->controls + i);

		ctrl.id = ext_ctrl->id;
		ctrl.value = ext_ctrl->value;

		ret = check_ctrl_val(ctx, &ctrl);
		if (ret != 0) {
			f->error_idx = i;
			break;
		}

		mfc_debug(2, "[%d] id: 0x%08x, value: %d", i, ext_ctrl->id, ext_ctrl->value);
	}

	return ret;
}

static const struct v4l2_ioctl_ops s5p_mfc_enc_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_enum_fmt_vid_out_mplane = vidioc_enum_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_cap_mplane = vidioc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = vidioc_s_fmt,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_queryctrl = vidioc_queryctrl,
	.vidioc_g_ctrl = vidioc_g_ctrl,
	.vidioc_s_ctrl = vidioc_s_ctrl,
	.vidioc_g_ext_ctrls = vidioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls = vidioc_s_ext_ctrls,
	.vidioc_try_ext_ctrls = vidioc_try_ext_ctrls,
};

static int check_vb_with_fmt(struct s5p_mfc_fmt *fmt, struct vb2_buffer *vb)
{
	int i;

	if (!fmt)
		return -EINVAL;

	if (fmt->num_planes != vb->num_planes) {
		mfc_err("invalid plane number for the format\n");
		return -EINVAL;
	}

	for (i = 0; i < fmt->num_planes; i++) {
		if (!mfc_plane_cookie(vb, i)) {
			mfc_err("failed to get plane cookie\n");
			return -EINVAL;
		}

		mfc_debug(2, "index: %d, plane[%d] cookie: 0x%08lx",
				vb->v4l2_buf.index, i,
				mfc_plane_cookie(vb, i));
	}

	return 0;
}

static int s5p_mfc_queue_setup(struct vb2_queue *vq,
			       unsigned int *buf_count, unsigned int *plane_count,
			       unsigned long psize[], void *allocators[])
{
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	struct s5p_mfc_dev *dev = ctx->dev;
	int i;

	mfc_debug_enter();

	if (ctx->state != MFCINST_GOT_INST &&
	    vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		mfc_err("invalid state: %d\n", ctx->state);
		return -EINVAL;
	}
	if (ctx->state >= MFCINST_FINISHING &&
	    vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		mfc_err("invalid state: %d\n", ctx->state);
		return -EINVAL;
	}

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->dst_fmt)
			*plane_count = ctx->dst_fmt->num_planes;
		else
			*plane_count = MFC_ENC_CAP_PLANE_COUNT;

		if (*buf_count < 1)
			*buf_count = 1;
		if (*buf_count > MFC_MAX_BUFFERS)
			*buf_count = MFC_MAX_BUFFERS;

		psize[0] = enc->dst_buf_size;
		allocators[0] = ctx->dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->src_fmt)
			*plane_count = ctx->src_fmt->num_planes;
		else
			*plane_count = MFC_ENC_OUT_PLANE_COUNT;

		if (*buf_count < 1)
			*buf_count = 1;
		if (*buf_count > MFC_MAX_BUFFERS)
			*buf_count = MFC_MAX_BUFFERS;

		psize[0] = ctx->luma_size;
		psize[1] = ctx->chroma_size;
		if (IS_MFCV6(dev)) {
			allocators[0] = ctx->dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];
			allocators[1] = ctx->dev->alloc_ctx[MFC_CMA_BANK1_ALLOC_CTX];
		}
		else {
			allocators[0] = ctx->dev->alloc_ctx[MFC_CMA_BANK2_ALLOC_CTX];
			allocators[1] = ctx->dev->alloc_ctx[MFC_CMA_BANK2_ALLOC_CTX];
		}

	} else {
		mfc_err("invalid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	mfc_debug(2, "buf_count: %d, plane_count: %d\n", *buf_count, *plane_count);
	for (i = 0; i < *plane_count; i++)
		mfc_debug(2, "plane[%d] size=%lu\n", i, psize[i]);

	mfc_debug_leave();

	return 0;
}

static void s5p_mfc_unlock(struct vb2_queue *q)
{
	struct s5p_mfc_ctx *ctx = q->drv_priv;
	struct s5p_mfc_dev *dev = ctx->dev;

	mutex_unlock(&dev->mfc_mutex);
}

static void s5p_mfc_lock(struct vb2_queue *q)
{
	struct s5p_mfc_ctx *ctx = q->drv_priv;
	struct s5p_mfc_dev *dev = ctx->dev;

	mutex_lock(&dev->mfc_mutex);
}

static int s5p_mfc_buf_init(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	struct s5p_mfc_buf *buf = vb_to_mfc_buf(vb);
	int ret;

	mfc_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = check_vb_with_fmt(ctx->dst_fmt, vb);
		if (ret < 0)
			return ret;

		buf->cookie.stream = mfc_plane_cookie(vb, 0);

		if (call_cop(ctx, init_buf_ctrls, ctx, MFC_CTRL_TYPE_GET_DST, vb->v4l2_buf.index) < 0)
			mfc_err("failed in init_buf_ctrls\n");

	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = check_vb_with_fmt(ctx->src_fmt, vb);
		if (ret < 0)
			return ret;

		buf->cookie.raw.luma = mfc_plane_cookie(vb, 0);
		buf->cookie.raw.chroma = mfc_plane_cookie(vb, 1);

		if (call_cop(ctx, init_buf_ctrls, ctx, MFC_CTRL_TYPE_SET, vb->v4l2_buf.index) < 0)
			mfc_err("failed in init_buf_ctrls\n");

	} else {
		mfc_err("inavlid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	struct s5p_mfc_enc *enc = ctx->enc_priv;
	unsigned int index = vb->v4l2_buf.index;
	int ret;

	mfc_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = check_vb_with_fmt(ctx->dst_fmt, vb);
		if (ret < 0)
			return ret;

		mfc_debug(2, "plane size: %ld, dst size: %d\n",
			vb2_plane_size(vb, 0), enc->dst_buf_size);

		if (vb2_plane_size(vb, 0) < enc->dst_buf_size) {
			mfc_err("plane size is too small for capture\n");
			return -EINVAL;
		}
		/* FIXME: 'cacheable' should be tested */
		s5p_mfc_mem_cache_flush(vb, 1);
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = check_vb_with_fmt(ctx->src_fmt, vb);
		if (ret < 0)
			return ret;

		mfc_debug(2, "plane size: %ld, luma size: %d\n",
			vb2_plane_size(vb, 0), ctx->luma_size);
		mfc_debug(2, "plane size: %ld, chroma size: %d\n",
			vb2_plane_size(vb, 1), ctx->chroma_size);

		if (vb2_plane_size(vb, 0) < ctx->luma_size ||
		    vb2_plane_size(vb, 1) < ctx->chroma_size) {
			mfc_err("plane size is too small for output\n");
			return -EINVAL;
		}
		if(ctx->cacheable) {
			s5p_mfc_mem_cache_flush(vb, 2);
		}
		if (call_cop(ctx, to_buf_ctrls, ctx, &ctx->src_ctrls[index]) < 0)
			mfc_err("failed in to_buf_ctrls\n");

	} else {
		mfc_err("inavlid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	mfc_debug_leave();

	return 0;
}

static int s5p_mfc_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	unsigned int index = vb->v4l2_buf.index;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (call_cop(ctx, to_ctx_ctrls, ctx, &ctx->dst_ctrls[index]) < 0)
			mfc_err("failed in to_ctx_ctrls\n");
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (call_cop(ctx, to_ctx_ctrls, ctx, &ctx->src_ctrls[index]) < 0)
			mfc_err("failed in to_buf_ctrls\n");
		#if 0
		/* if there are not-handled mfc_ctrl, remove all */
		while (!list_empty(&ctx->src_ctrls[index])) {
			mfc_ctrl = list_entry((&ctx->src_ctrls[index])->next,
					      struct s5p_mfc_ctrl, list);
			mfc_debug(2, "not handled ctrl id: 0x%08x val: %d\n",
				  mfc_ctrl->id, mfc_ctrl->val);
			list_del(&mfc_ctrl->list);
			kfree(mfc_ctrl);
		}
		#endif
	}

	return 0;
}

static void s5p_mfc_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	unsigned int index = vb->v4l2_buf.index;

	mfc_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (call_cop(ctx, cleanup_buf_ctrls, ctx, &ctx->dst_ctrls[index]) < 0)
			mfc_err("failed in cleanup_buf_ctrls\n");
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->src_ctrls_flag[index]) {
			if (call_cop(ctx, cleanup_buf_ctrls, ctx, &ctx->src_ctrls[index]) < 0)
				mfc_err("failed in cleanup_buf_ctrls\n");
		}
	} else {
		mfc_err("s5p_mfc_buf_cleanup: unknown queue type.\n");
	}

	mfc_debug_leave();
}

static int s5p_mfc_start_streaming(struct vb2_queue *q)
{
	struct s5p_mfc_ctx *ctx = q->drv_priv;
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;

	/* If context is ready then dev = work->data;schedule it to run */
	if (s5p_mfc_ctx_ready(ctx)) {
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
	}

	s5p_mfc_try_run(dev);

	return 0;
}

static int s5p_mfc_stop_streaming(struct vb2_queue *q)
{
	unsigned long flags;
	struct s5p_mfc_ctx *ctx = q->drv_priv;
	struct s5p_mfc_dev *dev = ctx->dev;

	if ((ctx->state == MFCINST_FINISHING ||
		ctx->state ==  MFCINST_RUNNING) &&
		dev->curr_ctx == ctx->num && dev->hw_lock) {
		ctx->state = MFCINST_ABORT;
		s5p_mfc_wait_for_done_ctx(ctx, S5P_FIMV_R2H_CMD_FRAME_DONE_RET,
					  0);
	}

	ctx->state = MFCINST_FINISHED;

	spin_lock_irqsave(&dev->irqlock, flags);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		s5p_mfc_cleanup_queue(&ctx->dst_queue, &ctx->vq_dst);
		INIT_LIST_HEAD(&ctx->dst_queue);
		ctx->dst_queue_cnt = 0;
	}

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		cleanup_ref_queue(ctx);

		s5p_mfc_cleanup_queue(&ctx->src_queue, &ctx->vq_src);
		INIT_LIST_HEAD(&ctx->src_queue);
		ctx->src_queue_cnt = 0;
	}

	spin_unlock_irqrestore(&dev->irqlock, flags);

	return 0;
}

static void s5p_mfc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = vq->drv_priv;
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;
	struct s5p_mfc_buf *buf = vb_to_mfc_buf(vb);

	mfc_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		buf->used = 0;
		mfc_debug(2, "dst queue: %p\n", &ctx->dst_queue);
		mfc_debug(2, "adding to dst: %p (%08lx, %08x)\n", vb,
			mfc_plane_cookie(vb, 0),
			buf->cookie.stream);

		/* Mark destination as available for use by MFC */
		spin_lock_irqsave(&dev->irqlock, flags);
		list_add_tail(&buf->list, &ctx->dst_queue);
		ctx->dst_queue_cnt++;
		spin_unlock_irqrestore(&dev->irqlock, flags);
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		buf->used = 0;
		mfc_debug(2, "src queue: %p\n", &ctx->src_queue);
		mfc_debug(2, "adding to src: %p (%08lx, %08lx, %08x, %08x)\n", vb,
			mfc_plane_cookie(vb, 0),
			mfc_plane_cookie(vb, 1),
			buf->cookie.raw.luma,
			buf->cookie.raw.chroma);

		spin_lock_irqsave(&dev->irqlock, flags);

		if (vb->v4l2_planes[0].bytesused == 0) {
			mfc_debug(1, "change state to FINISHING\n");
			ctx->state = MFCINST_FINISHING;

			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

			cleanup_ref_queue(ctx);
		} else {
			list_add_tail(&buf->list, &ctx->src_queue);
			ctx->src_queue_cnt++;
		}

		spin_unlock_irqrestore(&dev->irqlock, flags);
	} else {
		mfc_err("unsupported buffer type (%d)\n", vq->type);
	}

	if (s5p_mfc_ctx_ready(ctx)) {
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
	}
	s5p_mfc_try_run(dev);

	mfc_debug_leave();
}

static struct vb2_ops s5p_mfc_enc_qops = {
	.queue_setup	= s5p_mfc_queue_setup,
	.wait_prepare	= s5p_mfc_unlock,
	.wait_finish	= s5p_mfc_lock,
	.buf_init	= s5p_mfc_buf_init,
	.buf_prepare	= s5p_mfc_buf_prepare,
	.buf_finish	= s5p_mfc_buf_finish,
	.buf_cleanup	= s5p_mfc_buf_cleanup,
	.start_streaming= s5p_mfc_start_streaming,
	.stop_streaming = s5p_mfc_stop_streaming,
	.buf_queue	= s5p_mfc_buf_queue,
};

const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void)
{
	return &s5p_mfc_enc_ioctl_ops;
}

int s5p_mfc_init_enc_ctx(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_enc *enc;
	int ret = 0;

	enc = kzalloc(sizeof(struct s5p_mfc_enc), GFP_KERNEL);
	if (!enc) {
		mfc_err("failed to allocate encoder private data\n");
		return -ENOMEM;
	}
	ctx->enc_priv = enc;

	ctx->inst_no = MFC_NO_INSTANCE_SET;

	INIT_LIST_HEAD(&ctx->src_queue);
	INIT_LIST_HEAD(&ctx->dst_queue);
	ctx->src_queue_cnt = 0;
	ctx->dst_queue_cnt = 0;

	ctx->type = MFCINST_ENCODER;
	ctx->c_ops = &encoder_codec_ops;
	ctx->src_fmt = &formats[DEF_SRC_FMT];
	ctx->dst_fmt = &formats[DEF_DST_FMT];

	INIT_LIST_HEAD(&enc->ref_queue);
	enc->ref_queue_cnt = 0;

	/* Init videobuf2 queue for OUTPUT */
	ctx->vq_src.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ctx->vq_src.drv_priv = ctx;
	ctx->vq_src.buf_struct_size = sizeof(struct s5p_mfc_buf);
	ctx->vq_src.io_modes = VB2_MMAP | VB2_USERPTR;
	ctx->vq_src.ops = &s5p_mfc_enc_qops;
	ctx->vq_src.mem_ops = s5p_mfc_mem_ops();
	ret = vb2_queue_init(&ctx->vq_src);
	if (ret) {
		mfc_err("Failed to initialize videobuf2 queue(output)\n");
		return ret;
	}

	/* Init videobuf2 queue for CAPTURE */
	ctx->vq_dst.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ctx->vq_dst.drv_priv = ctx;
	ctx->vq_dst.buf_struct_size = sizeof(struct s5p_mfc_buf);
	ctx->vq_dst.io_modes = VB2_MMAP | VB2_USERPTR;
	ctx->vq_dst.ops = &s5p_mfc_enc_qops;
	ctx->vq_dst.mem_ops = s5p_mfc_mem_ops();
	ret = vb2_queue_init(&ctx->vq_dst);
	if (ret) {
		mfc_err("Failed to initialize videobuf2 queue(capture)\n");
		return ret;
	}

	return 0;
}
