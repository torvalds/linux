/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_shm.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Shared memory interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_SHM_H
#define __MFC_SHM_H __FILE__

enum MFC_SHM_OFS
{
	EXTENEDED_DECODE_STATUS		= 0x0000, /* D */
	SET_FRAME_TAG			= 0x0004, /* D */
	GET_FRAME_TAG_TOP		= 0x0008, /* D */
	GET_FRAME_TAG_BOT		= 0x000C, /* D */
	PIC_TIME_TOP			= 0x0010, /* D */
	PIC_TIME_BOT			= 0x0014, /* D */
	START_BYTE_NUM			= 0x0018, /* D */
	CROP_INFO1			= 0x0020, /* D, H.264 */
	CROP_INFO2			= 0x0024, /* D, H.264 */
	EXT_ENC_CONTROL			= 0x0028, /* E */
	ENC_PARAM_CHANGE		= 0x002C, /* E */
	VOP_TIMING			= 0x0030, /* E, MPEG4 */
	HEC_PERIOD			= 0x0034, /* E, MPEG4 */
	METADATA_ENABLE			= 0x0038, /* C */
	METADATA_STATUS			= 0x003C, /* C */
	METADATA_DISPLAY_INDEX		= 0x0040, /* C */
	EXT_METADATA_START_ADDR		= 0x0044, /* C */
	PUT_EXTRADATA			= 0x0048, /* C */
	EXTRADATA_ADDR			= 0x004C, /* C */
	ALLOCATED_LUMA_DPB_SIZE		= 0x0064, /* D */
	ALLOCATED_CHROMA_DPB_SIZE	= 0x0068, /* D */
	ALLOCATED_MV_SIZE		= 0x006C, /* D */
	P_B_FRAME_QP			= 0x0070, /* E */
	ASPECT_RATIO_IDC		= 0x0074, /* E, H.264, depend on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	EXTENDED_SAR			= 0x0078, /* E, H.264, depned on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	DISP_PIC_PROFILE		= 0x007C, /* D */
	FLUSH_CMD_TYPE			= 0x0080, /* C */
	FLUSH_CMD_INBUF1		= 0x0084, /* C */
	FLUSH_CMD_INBUF2		= 0x0088, /* C */
	FLUSH_CMD_OUTBUF		= 0x008C, /* E */
	NEW_RC_BIT_RATE			= 0x0090, /* E, format as RC_BIT_RATE(0xC5A8) depend on RC_BIT_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_RC_FRAME_RATE		= 0x0094, /* E, format as RC_FRAME_RATE(0xD0D0) depend on RC_FRAME_RATE_CHANGE in ENC_PARAM_CHANGE */
	NEW_I_PERIOD			= 0x0098, /* E, format as I_FRM_CTRL(0xC504) depend on I_PERIOD_CHANGE in ENC_PARAM_CHANGE */
	H264_I_PERIOD			= 0x009C, /* E, H.264, open GOP */
	RC_CONTROL_CONFIG		= 0x00A0, /* E */
	BATCH_INPUT_ADDR		= 0x00A4, /* E */
	BATCH_OUTPUT_ADDR		= 0x00A8, /* E */
	BATCH_OUTPUT_SIZE		= 0x00AC, /* E */
	MIN_LUMA_DPB_SIZE		= 0x00B0, /* D */
	DEVICE_FORMAT_ID		= 0x00B4, /* C */
	H264_POC_TYPE			= 0x00B8, /* D */
	MIN_CHROMA_DPB_SIZE		= 0x00BC, /* D */
	DISP_PIC_FRAME_TYPE		= 0x00C0, /* D */
	FREE_LUMA_DPB			= 0x00C4, /* D, VC1 MPEG4 */
	ASPECT_RATIO_INFO		= 0x00C8, /* D, MPEG4 */
	EXTENDED_PAR			= 0x00CC, /* D, MPEG4 */
	DBG_HISTORY_INPUT0		= 0x00D0, /* C */
	DBG_HISTORY_INPUT1		= 0x00D4, /* C */
	DBG_HISTORY_OUTPUT		= 0x00D8, /* C */
	HIERARCHICAL_P_QP		= 0x00E0, /* E, H.264 */
	SEI_ENABLE			= 0x0168, /* C, H.264 */
	FRAME_PACK_SEI_AVAIL		= 0x016C, /* D, H.264 */
	FRAME_PACK_ARRGMENT_ID		= 0x0170, /* D, H.264 */
	FRAME_PACK_DEC_INFO		= 0x0174, /* D, H.264 */
	FRAME_PACK_GRID_POS		= 0x0178, /* D, H.264 */
	FRAME_PACK_ENC_INFO		= 0x017C, /* E, H.264 */
};

int init_shm(struct mfc_inst_ctx *ctx);
void write_shm(struct mfc_inst_ctx *ctx, unsigned int data, unsigned int offset);
unsigned int read_shm(struct mfc_inst_ctx *ctx, unsigned int offset);

#endif /* __MFC_SHM_H */
