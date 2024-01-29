/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/platform/samsung/s5p-mfc/s5p_mfc_opr_v6.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#ifndef S5P_MFC_OPR_V6_H_
#define S5P_MFC_OPR_V6_H_

#include "s5p_mfc_common.h"
#include "s5p_mfc_opr.h"

#define MFC_CTRL_MODE_CUSTOM	MFC_CTRL_MODE_SFR

#define MB_WIDTH(x_size)		DIV_ROUND_UP(x_size, 16)
#define MB_HEIGHT(y_size)		DIV_ROUND_UP(y_size, 16)
#define S5P_MFC_DEC_MV_SIZE(x, y, offset)	(MB_WIDTH(x) * \
					(((MB_HEIGHT(y) + 1) / 2) * 2) * 64 + (offset))
#define S5P_MFC_LCU_WIDTH(x_size)	DIV_ROUND_UP(x_size, 32)
#define S5P_MFC_LCU_HEIGHT(y_size)	DIV_ROUND_UP(y_size, 32)

#define s5p_mfc_dec_hevc_mv_size(x, y) \
	(DIV_ROUND_UP(x, 64) * DIV_ROUND_UP(y, 64) * 256 + 512)

/* Definition */
#define ENC_MULTI_SLICE_MB_MAX		((1 << 30) - 1)
#define ENC_MULTI_SLICE_BIT_MIN		2800
#define ENC_INTRA_REFRESH_MB_MAX	((1 << 18) - 1)
#define ENC_VBV_BUF_SIZE_MAX		((1 << 30) - 1)
#define ENC_H264_LOOP_FILTER_AB_MIN	-12
#define ENC_H264_LOOP_FILTER_AB_MAX	12
#define ENC_H264_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H263_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H264_PROFILE_MAX		3
#define ENC_H264_LEVEL_MAX		42
#define ENC_MPEG4_VOP_TIME_RES_MAX	((1 << 16) - 1)
#define FRAME_DELTA_H264_H263		1
#define LOOSE_CBR_MAX			5
#define TIGHT_CBR_MAX			10
#define ENC_HEVC_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_HEVC_QP_INDEX_MIN		-12
#define ENC_HEVC_QP_INDEX_MAX		12
#define ENC_HEVC_LOOP_FILTER_MIN	-12
#define ENC_HEVC_LOOP_FILTER_MAX	12
#define ENC_HEVC_LEVEL_MAX		62

#define FRAME_DELTA_DEFAULT		1

struct s5p_mfc_hw_ops *s5p_mfc_init_hw_ops_v6(void);
const struct s5p_mfc_regs *s5p_mfc_init_regs_v6_plus(struct s5p_mfc_dev *dev);
#endif /* S5P_MFC_OPR_V6_H_ */
