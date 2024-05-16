/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/platform/samsung/mfc5/s5p_mfc_opr_v5.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Kamil Debski, Copyright (C) 2011 Samsung Electronics
 * http://www.samsung.com/
 */

#ifndef S5P_MFC_OPR_V5_H_
#define S5P_MFC_OPR_V5_H_

#include "s5p_mfc_common.h"
#include "s5p_mfc_opr.h"

enum MFC_SHM_OFS {
	EXTENEDED_DECODE_STATUS	= 0x00,	/* D */
	SET_FRAME_TAG		= 0x04, /* D */
	GET_FRAME_TAG_TOP	= 0x08, /* D */
	GET_FRAME_TAG_BOT	= 0x0C, /* D */
	PIC_TIME_TOP		= 0x10, /* D */
	PIC_TIME_BOT		= 0x14, /* D */
	START_BYTE_NUM		= 0x18, /* D */

	CROP_INFO_H		= 0x20, /* D */
	CROP_INFO_V		= 0x24, /* D */
	EXT_ENC_CONTROL		= 0x28,	/* E */
	ENC_PARAM_CHANGE	= 0x2C,	/* E */
	RC_VOP_TIMING		= 0x30,	/* E, MPEG4 */
	HEC_PERIOD		= 0x34,	/* E, MPEG4 */
	METADATA_ENABLE		= 0x38, /* C */
	METADATA_STATUS		= 0x3C, /* C */
	METADATA_DISPLAY_INDEX	= 0x40,	/* C */
	EXT_METADATA_START_ADDR	= 0x44, /* C */
	PUT_EXTRADATA		= 0x48, /* C */
	EXTRADATA_ADDR		= 0x4C, /* C */

	ALLOC_LUMA_DPB_SIZE	= 0x64,	/* D */
	ALLOC_CHROMA_DPB_SIZE	= 0x68,	/* D */
	ALLOC_MV_SIZE		= 0x6C,	/* D */
	P_B_FRAME_QP		= 0x70,	/* E */
	SAMPLE_ASPECT_RATIO_IDC	= 0x74, /* E, H.264, depend on
				ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	EXTENDED_SAR		= 0x78, /* E, H.264, depned on
				ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	DISP_PIC_PROFILE	= 0x7C, /* D */
	FLUSH_CMD_TYPE		= 0x80, /* C */
	FLUSH_CMD_INBUF1	= 0x84, /* C */
	FLUSH_CMD_INBUF2	= 0x88, /* C */
	FLUSH_CMD_OUTBUF	= 0x8C, /* E */
	NEW_RC_BIT_RATE		= 0x90, /* E, format as RC_BIT_RATE(0xC5A8)
			depend on RC_BIT_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_RC_FRAME_RATE	= 0x94, /* E, format as RC_FRAME_RATE(0xD0D0)
			depend on RC_FRAME_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_I_PERIOD		= 0x98, /* E, format as I_FRM_CTRL(0xC504)
			depend on I_PERIOD_CHANGE in ENC_PARAM_CHANGE */
	H264_I_PERIOD		= 0x9C, /* E, H.264, open GOP */
	RC_CONTROL_CONFIG	= 0xA0, /* E */
	BATCH_INPUT_ADDR	= 0xA4, /* E */
	BATCH_OUTPUT_ADDR	= 0xA8, /* E */
	BATCH_OUTPUT_SIZE	= 0xAC, /* E */
	MIN_LUMA_DPB_SIZE	= 0xB0, /* D */
	DEVICE_FORMAT_ID	= 0xB4, /* C */
	H264_POC_TYPE		= 0xB8, /* D */
	MIN_CHROMA_DPB_SIZE	= 0xBC, /* D */
	DISP_PIC_FRAME_TYPE	= 0xC0, /* D */
	FREE_LUMA_DPB		= 0xC4, /* D, VC1 MPEG4 */
	ASPECT_RATIO_INFO	= 0xC8, /* D, MPEG4 */
	EXTENDED_PAR		= 0xCC, /* D, MPEG4 */
	DBG_HISTORY_INPUT0	= 0xD0, /* C */
	DBG_HISTORY_INPUT1	= 0xD4,	/* C */
	DBG_HISTORY_OUTPUT	= 0xD8,	/* C */
	HIERARCHICAL_P_QP	= 0xE0, /* E, H.264 */
	FRAME_PACK_SEI_ENABLE	= 0x168, /* C */
	FRAME_PACK_SEI_AVAIL	= 0x16c, /* D */
	FRAME_PACK_SEI_INFO	= 0x17c, /* E */
};

const struct s5p_mfc_hw_ops *s5p_mfc_init_hw_ops_v5(void);
#endif /* S5P_MFC_OPR_H_ */
