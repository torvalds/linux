/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_shm.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5P_MFC_SHM_H_
#define __S5P_MFC_SHM_H_ __FILE__

#include <linux/io.h>

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
	ASPECT_RATIO_IDC	= 0x74, /* E, H.264, depend on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	EXTENDED_SAR		= 0x78, /* E, H.264, depned on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	DISP_PIC_PROFILE	= 0x7C, /* D */
	FLUSH_CMD_TYPE		= 0x80, /* C */
	FLUSH_CMD_INBUF1	= 0x84, /* C */
	FLUSH_CMD_INBUF2	= 0x88, /* C */
	FLUSH_CMD_OUTBUF	= 0x8C, /* E */
	NEW_RC_BIT_RATE		= 0x90, /* E, format as RC_BIT_RATE(0xC5A8) depend on RC_BIT_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_RC_FRAME_RATE	= 0x94, /* E, format as RC_FRAME_RATE(0xD0D0) depend on RC_FRAME_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_I_PERIOD		= 0x98, /* E, format as I_FRM_CTRL(0xC504) depend on I_PERIOD_CHANGE in ENC_PARAM_CHANGE */
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

#define S5P_FIMV_DISPLAY_FRAME_NOT_CODED	0
#define S5P_FIMV_DISPLAY_FRAME_I		1
#define S5P_FIMV_DISPLAY_FRAME_P		2
#define S5P_FIMV_DISPLAY_FRAME_B		3

int s5p_mfc_init_shm(struct s5p_mfc_ctx *ctx);

static inline void s5p_mfc_write_shm(struct s5p_mfc_ctx *ctx, unsigned int data, unsigned int ofs)
{
	writel(data, (ctx->shm.virt + ofs));
	s5p_mfc_mem_clean_priv(ctx->shm.alloc, ctx->shm.virt, ofs, 4);
}

static inline u32 s5p_mfc_read_shm(struct s5p_mfc_ctx *ctx, unsigned int ofs)
{
	s5p_mfc_mem_inv_priv(ctx->shm.alloc, ctx->shm.virt, ofs, 4);
	return readl(ctx->shm.virt + ofs);
}

#endif /* __S5P_MFC_SHM_H_ */
