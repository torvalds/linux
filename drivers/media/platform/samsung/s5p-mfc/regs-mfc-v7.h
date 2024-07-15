/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register definition file for Samsung MFC V7.x Interface (FIMV) driver
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#ifndef _REGS_MFC_V7_H
#define _REGS_MFC_V7_H

#include "regs-mfc-v6.h"

/* Additional features of v7 */
#define S5P_FIMV_CODEC_VP8_ENC_V7	25

/* Additional registers for v7 */
#define S5P_FIMV_E_SOURCE_FIRST_ADDR_V7			0xf9e0
#define S5P_FIMV_E_SOURCE_SECOND_ADDR_V7		0xf9e4
#define S5P_FIMV_E_SOURCE_THIRD_ADDR_V7			0xf9e8
#define S5P_FIMV_E_SOURCE_FIRST_STRIDE_V7		0xf9ec
#define S5P_FIMV_E_SOURCE_SECOND_STRIDE_V7		0xf9f0
#define S5P_FIMV_E_SOURCE_THIRD_STRIDE_V7		0xf9f4

#define S5P_FIMV_E_ENCODED_SOURCE_FIRST_ADDR_V7		0xfa70
#define S5P_FIMV_E_ENCODED_SOURCE_SECOND_ADDR_V7	0xfa74
#define S5P_FIMV_E_ENCODED_SOURCE_THIRD_ADDR_V7		0xfa78

#define S5P_FIMV_E_VP8_OPTIONS_V7			0xfdb0
#define S5P_FIMV_E_VP8_FILTER_OPTIONS_V7		0xfdb4
#define S5P_FIMV_E_VP8_GOLDEN_FRAME_OPTION_V7		0xfdb8
#define S5P_FIMV_E_VP8_NUM_T_LAYER_V7			0xfdc4

/* MFCv7 variant defines */
#define MAX_FW_SIZE_V7			(SZ_512K)	/* 512KB */
#define MAX_CPB_SIZE_V7			(3 * SZ_1M)	/* 3MB */
#define MFC_VERSION_V7			0x72
#define MFC_NUM_PORTS_V7		1

#define MFC_LUMA_PAD_BYTES_V7		256
#define MFC_CHROMA_PAD_BYTES_V7		128

/* MFCv7 Context buffer sizes */
#define MFC_CTX_BUF_SIZE_V7		(30 * SZ_1K)	/*  30KB */
#define MFC_H264_DEC_CTX_BUF_SIZE_V7	(2 * SZ_1M)	/*  2MB */
#define MFC_OTHER_DEC_CTX_BUF_SIZE_V7	(20 * SZ_1K)	/*  20KB */
#define MFC_H264_ENC_CTX_BUF_SIZE_V7	(100 * SZ_1K)	/* 100KB */
#define MFC_OTHER_ENC_CTX_BUF_SIZE_V7	(10 * SZ_1K)	/*  10KB */

/* Buffer size defines */
#define S5P_FIMV_SCRATCH_BUF_SIZE_MPEG4_DEC_V7(w, h) \
			(SZ_1M + ((w) * 144) + (8192 * (h)) + 49216)

#define S5P_FIMV_SCRATCH_BUF_SIZE_VP8_ENC_V7(w, h) \
			(((w) * 48) + 8192 + ((((w) + 1) / 2) * 128) + 144 + \
			((((((w) * 16) * ((h) * 16)) * 3) / 2) * 4))

#endif /*_REGS_MFC_V7_H*/
