/*
 * Register definition file for Samsung MFC V8.x Interface (FIMV) driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _REGS_MFC_V8_H
#define _REGS_MFC_V8_H

#include <linux/sizes.h>
#include "regs-mfc-v7.h"

/* Additional registers for v8 */
#define S5P_FIMV_D_MVC_NUM_VIEWS_V8		0xf104
#define S5P_FIMV_D_FIRST_PLANE_DPB_SIZE_V8	0xf144
#define S5P_FIMV_D_SECOND_PLANE_DPB_SIZE_V8	0xf148
#define S5P_FIMV_D_MV_BUFFER_SIZE_V8		0xf150

#define S5P_FIMV_D_FIRST_PLANE_DPB_STRIDE_SIZE_V8	0xf138
#define S5P_FIMV_D_SECOND_PLANE_DPB_STRIDE_SIZE_V8	0xf13c

#define S5P_FIMV_D_FIRST_PLANE_DPB_V8		0xf160
#define S5P_FIMV_D_SECOND_PLANE_DPB_V8		0xf260
#define S5P_FIMV_D_MV_BUFFER_V8			0xf460

#define S5P_FIMV_D_NUM_MV_V8			0xf134
#define S5P_FIMV_D_INIT_BUFFER_OPTIONS_V8	0xf154

#define S5P_FIMV_D_SCRATCH_BUFFER_ADDR_V8	0xf560
#define S5P_FIMV_D_SCRATCH_BUFFER_SIZE_V8	0xf564

#define S5P_FIMV_D_CPB_BUFFER_ADDR_V8		0xf5b0
#define S5P_FIMV_D_CPB_BUFFER_SIZE_V8		0xf5b4
#define S5P_FIMV_D_AVAILABLE_DPB_FLAG_LOWER_V8	0xf5bc
#define S5P_FIMV_D_CPB_BUFFER_OFFSET_V8		0xf5c0
#define S5P_FIMV_D_SLICE_IF_ENABLE_V8		0xf5c4
#define S5P_FIMV_D_STREAM_DATA_SIZE_V8		0xf5d0

/* Display information register */
#define S5P_FIMV_D_DISPLAY_FRAME_WIDTH_V8	0xf600
#define S5P_FIMV_D_DISPLAY_FRAME_HEIGHT_V8	0xf604

/* Display status */
#define S5P_FIMV_D_DISPLAY_STATUS_V8		0xf608

#define S5P_FIMV_D_DISPLAY_FIRST_PLANE_ADDR_V8	0xf60c
#define S5P_FIMV_D_DISPLAY_SECOND_PLANE_ADDR_V8	0xf610

#define S5P_FIMV_D_DISPLAY_FRAME_TYPE_V8	0xf618
#define S5P_FIMV_D_DISPLAY_CROP_INFO1_V8	0xf61c
#define S5P_FIMV_D_DISPLAY_CROP_INFO2_V8	0xf620
#define S5P_FIMV_D_DISPLAY_PICTURE_PROFILE_V8	0xf624

/* Decoded picture information register */
#define S5P_FIMV_D_DECODED_STATUS_V8		0xf644
#define S5P_FIMV_D_DECODED_FIRST_PLANE_ADDR_V8	0xf648
#define S5P_FIMV_D_DECODED_SECOND_PLANE_ADDR_V8	0xf64c
#define S5P_FIMV_D_DECODED_THIRD_PLANE_ADDR_V8	0xf650
#define S5P_FIMV_D_DECODED_FRAME_TYPE_V8	0xf654
#define S5P_FIMV_D_DECODED_NAL_SIZE_V8          0xf664

/* Returned value register for specific setting */
#define S5P_FIMV_D_RET_PICTURE_TAG_TOP_V8	0xf674
#define S5P_FIMV_D_RET_PICTURE_TAG_BOT_V8	0xf678
#define S5P_FIMV_D_MVC_VIEW_ID_V8		0xf6d8

/* SEI related information */
#define S5P_FIMV_D_FRAME_PACK_SEI_AVAIL_V8	0xf6dc

/* Encoder Registers */
#define S5P_FIMV_E_FIXED_PICTURE_QP_V8		0xf794
#define S5P_FIMV_E_RC_CONFIG_V8			0xf798
#define S5P_FIMV_E_RC_QP_BOUND_V8		0xf79c
#define S5P_FIMV_E_RC_RPARAM_V8			0xf7a4
#define S5P_FIMV_E_MB_RC_CONFIG_V8		0xf7a8
#define S5P_FIMV_E_PADDING_CTRL_V8		0xf7ac
#define S5P_FIMV_E_MV_HOR_RANGE_V8		0xf7b4
#define S5P_FIMV_E_MV_VER_RANGE_V8		0xf7b8

#define S5P_FIMV_E_VBV_BUFFER_SIZE_V8		0xf78c
#define S5P_FIMV_E_VBV_INIT_DELAY_V8		0xf790

#define S5P_FIMV_E_ASPECT_RATIO_V8		0xfb4c
#define S5P_FIMV_E_EXTENDED_SAR_V8		0xfb50
#define S5P_FIMV_E_H264_OPTIONS_V8		0xfb54

/* MFCv8 Context buffer sizes */
#define MFC_CTX_BUF_SIZE_V8		(30 * SZ_1K)	/*  30KB */
#define MFC_H264_DEC_CTX_BUF_SIZE_V8	(2 * SZ_1M)	/*  2MB */
#define MFC_OTHER_DEC_CTX_BUF_SIZE_V8	(20 * SZ_1K)	/*  20KB */
#define MFC_H264_ENC_CTX_BUF_SIZE_V8	(100 * SZ_1K)	/* 100KB */
#define MFC_OTHER_ENC_CTX_BUF_SIZE_V8	(10 * SZ_1K)	/*  10KB */

/* Buffer size defines */
#define S5P_FIMV_TMV_BUFFER_SIZE_V8(w, h)	(((w) + 1) * ((h) + 1) * 8)

#define S5P_FIMV_SCRATCH_BUF_SIZE_H264_DEC_V8(w, h)	(((w) * 704) + 2176)
#define S5P_FIMV_SCRATCH_BUF_SIZE_VP8_DEC_V8(w, h) \
		(((w) * 576 + (h) * 128)  + 4128)

#define S5P_FIMV_SCRATCH_BUF_SIZE_H264_ENC_V8(w, h) \
			(((w) * 592) + 2336)
#define S5P_FIMV_SCRATCH_BUF_SIZE_VP8_ENC_V8(w, h) \
			(((w) * 576) + 10512 + \
			((((((w) * 16) * ((h) * 16)) * 3) / 2) * 4))
#define S5P_FIMV_ME_BUFFER_SIZE_V8(imw, imh, mbw, mbh) \
	((DIV_ROUND_UP((mbw * 16), 64) *  DIV_ROUND_UP((mbh * 16), 64) * 256) \
	 + (DIV_ROUND_UP((mbw) * (mbh), 32) * 16))

/* BUffer alignment defines */
#define S5P_FIMV_D_ALIGN_PLANE_SIZE_V8	64

/* MFCv8 variant defines */
#define MAX_FW_SIZE_V8			(SZ_1M)		/* 1MB */
#define MAX_CPB_SIZE_V8			(3 * SZ_1M)	/* 3MB */
#define MFC_VERSION_V8			0x80
#define MFC_NUM_PORTS_V8		1

#endif /*_REGS_MFC_V8_H*/
