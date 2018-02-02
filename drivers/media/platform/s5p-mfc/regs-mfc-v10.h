/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *     http://www.samsung.com/
 *
 * Register definition file for Samsung MFC V10.x Interface (FIMV) driver
 *
 */

#ifndef _REGS_MFC_V10_H
#define _REGS_MFC_V10_H

#include <linux/sizes.h>
#include "regs-mfc-v8.h"

/* MFCv10 register definitions*/
#define S5P_FIMV_MFC_CLOCK_OFF_V10			0x7120
#define S5P_FIMV_MFC_STATE_V10				0x7124

/* MFCv10 Context buffer sizes */
#define MFC_CTX_BUF_SIZE_V10		(30 * SZ_1K)
#define MFC_H264_DEC_CTX_BUF_SIZE_V10	(2 * SZ_1M)
#define MFC_OTHER_DEC_CTX_BUF_SIZE_V10	(20 * SZ_1K)
#define MFC_H264_ENC_CTX_BUF_SIZE_V10	(100 * SZ_1K)
#define MFC_OTHER_ENC_CTX_BUF_SIZE_V10	(15 * SZ_1K)

/* MFCv10 variant defines */
#define MAX_FW_SIZE_V10		(SZ_1M)
#define MAX_CPB_SIZE_V10	(3 * SZ_1M)
#define MFC_VERSION_V10		0xA0
#define MFC_NUM_PORTS_V10	1

/* MFCv10 codec defines*/
#define S5P_FIMV_CODEC_HEVC_ENC         26

/* Encoder buffer size for MFC v10.0 */
#define ENC_V100_BASE_SIZE(x, y) \
	(((x + 3) * (y + 3) * 8) \
	+  ((y * 64) + 1280) * DIV_ROUND_UP(x, 8))

#define ENC_V100_H264_ME_SIZE(x, y) \
	(ENC_V100_BASE_SIZE(x, y) \
	+ (DIV_ROUND_UP(x * y, 64) * 32))

#define ENC_V100_MPEG4_ME_SIZE(x, y) \
	(ENC_V100_BASE_SIZE(x, y) \
	+ (DIV_ROUND_UP(x * y, 128) * 16))

#define ENC_V100_VP8_ME_SIZE(x, y) \
	ENC_V100_BASE_SIZE(x, y)

#endif /*_REGS_MFC_V10_H*/

