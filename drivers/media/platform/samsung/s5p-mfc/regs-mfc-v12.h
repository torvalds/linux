/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register definition file for Samsung MFC V12.x Interface (FIMV) driver
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *     http://www.samsung.com/
 */

#ifndef _REGS_MFC_V12_H
#define _REGS_MFC_V12_H

#include <linux/sizes.h>
#include "regs-mfc-v10.h"

/* MFCv12 Context buffer sizes */
#define MFC_CTX_BUF_SIZE_V12		(30 * SZ_1K)
#define MFC_H264_DEC_CTX_BUF_SIZE_V12	(2 * SZ_1M)
#define MFC_OTHER_DEC_CTX_BUF_SIZE_V12	(30 * SZ_1K)
#define MFC_H264_ENC_CTX_BUF_SIZE_V12	(100 * SZ_1K)
#define MFC_HEVC_ENC_CTX_BUF_SIZE_V12	(40 * SZ_1K)
#define MFC_OTHER_ENC_CTX_BUF_SIZE_V12	(25 * SZ_1K)

/* MFCv12 variant defines */
#define MAX_FW_SIZE_V12			(SZ_1M)
#define MAX_CPB_SIZE_V12		(7 * SZ_1M)
#define MFC_VERSION_V12			0xC0
#define MFC_NUM_PORTS_V12		1
#define S5P_FIMV_CODEC_VP9_ENC		27
#define MFC_CHROMA_PAD_BYTES_V12        256
#define S5P_FIMV_D_ALIGN_PLANE_SIZE_V12 256

/* Encoder buffer size for MFCv12 */
#define ENC_V120_BASE_SIZE(x, y) \
	((((x) + 3) * ((y) + 3) * 8) \
	+ ((((y) * 64) + 2304) * ((x) + 7) / 8))

#define ENC_V120_H264_ME_SIZE(x, y) \
	ALIGN((ENC_V120_BASE_SIZE(x, y) \
	+ (DIV_ROUND_UP((x) * (y), 64) * 32)), 256)

#define ENC_V120_MPEG4_ME_SIZE(x, y) \
	ALIGN((ENC_V120_BASE_SIZE(x, y) \
	+ (DIV_ROUND_UP((x) * (y), 128) * 16)), 256)

#define ENC_V120_VP8_ME_SIZE(x, y) \
	ALIGN(ENC_V120_BASE_SIZE((x), (y)), 256)

#define ENC_V120_HEVC_ME_SIZE(x, y)     \
	ALIGN(((((x) + 3) * ((y) + 3) * 32)       \
	+ ((((y) * 128) + 2304) * ((x) + 3) / 4)), 256)

#endif /*_REGS_MFC_V12_H*/
