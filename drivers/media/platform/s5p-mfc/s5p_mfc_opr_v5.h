/*
 * drivers/media/platform/samsung/mfc5/s5p_mfc_opr.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Kamil Debski, Copyright (C) 2011 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_OPR_H_
#define S5P_MFC_OPR_H_

#include "s5p_mfc_common.h"

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

int s5p_mfc_init_decode(struct s5p_mfc_ctx *ctx);
int s5p_mfc_init_encode(struct s5p_mfc_ctx *mfc_ctx);

/* Decoding functions */
int s5p_mfc_set_dec_frame_buffer(struct s5p_mfc_ctx *ctx);
int s5p_mfc_set_dec_stream_buffer(struct s5p_mfc_ctx *ctx, int buf_addr,
						  unsigned int start_num_byte,
						  unsigned int buf_size);

/* Encoding functions */
void s5p_mfc_set_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long y_addr, unsigned long c_addr);
int s5p_mfc_set_enc_stream_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long addr, unsigned int size);
void s5p_mfc_get_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long *y_addr, unsigned long *c_addr);
int s5p_mfc_set_enc_ref_buffer(struct s5p_mfc_ctx *mfc_ctx);

int s5p_mfc_decode_one_frame(struct s5p_mfc_ctx *ctx,
					enum s5p_mfc_decode_arg last_frame);
int s5p_mfc_encode_one_frame(struct s5p_mfc_ctx *mfc_ctx);

/* Memory allocation */
int s5p_mfc_alloc_dec_temp_buffers(struct s5p_mfc_ctx *ctx);
void s5p_mfc_set_dec_desc_buffer(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_dec_desc_buffer(struct s5p_mfc_ctx *ctx);

int s5p_mfc_alloc_codec_buffers(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_codec_buffers(struct s5p_mfc_ctx *ctx);

int s5p_mfc_alloc_instance_buffer(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_instance_buffer(struct s5p_mfc_ctx *ctx);

void s5p_mfc_try_run(struct s5p_mfc_dev *dev);
void s5p_mfc_cleanup_queue(struct list_head *lh, struct vb2_queue *vq);

/* Shared memory ops */
void s5p_mfc_write_info_v5(struct s5p_mfc_ctx *ctx, unsigned int data,
			unsigned int ofs);

unsigned int s5p_mfc_read_info_v5(struct s5p_mfc_ctx *ctx,
				unsigned int ofs);

#define s5p_mfc_get_dspl_y_adr()	(readl(dev->regs_base + \
					S5P_FIMV_SI_DISPLAY_Y_ADR) << \
					MFC_OFFSET_SHIFT)
#define s5p_mfc_get_dec_y_adr()		(readl(dev->regs_base + \
					S5P_FIMV_SI_DECODE_Y_ADR) << \
					MFC_OFFSET_SHIFT)
#define s5p_mfc_get_dspl_status()	readl(dev->regs_base + \
						S5P_FIMV_SI_DISPLAY_STATUS)
#define s5p_mfc_get_dec_status()	readl(dev->regs_base + \
						S5P_FIMV_SI_DECODE_STATUS)
#define s5p_mfc_get_frame_type()	(readl(dev->regs_base + \
						S5P_FIMV_DECODE_FRAME_TYPE) \
					& S5P_FIMV_DECODE_FRAME_MASK)
#define s5p_mfc_get_consumed_stream()	readl(dev->regs_base + \
						S5P_FIMV_SI_CONSUMED_BYTES)
#define s5p_mfc_get_int_reason()	(readl(dev->regs_base + \
					S5P_FIMV_RISC2HOST_CMD) & \
					S5P_FIMV_RISC2HOST_CMD_MASK)
#define s5p_mfc_get_int_err()		readl(dev->regs_base + \
						S5P_FIMV_RISC2HOST_ARG2)
#define s5p_mfc_err_dec(x)		(((x) & S5P_FIMV_ERR_DEC_MASK) >> \
							S5P_FIMV_ERR_DEC_SHIFT)
#define s5p_mfc_err_dspl(x)		(((x) & S5P_FIMV_ERR_DSPL_MASK) >> \
							S5P_FIMV_ERR_DSPL_SHIFT)
#define s5p_mfc_get_img_width()		readl(dev->regs_base + \
						S5P_FIMV_SI_HRESOL)
#define s5p_mfc_get_img_height()	readl(dev->regs_base + \
						S5P_FIMV_SI_VRESOL)
#define s5p_mfc_get_dpb_count()		readl(dev->regs_base + \
						S5P_FIMV_SI_BUF_NUMBER)
#define s5p_mfc_get_inst_no()		readl(dev->regs_base + \
						S5P_FIMV_RISC2HOST_ARG1)
#define s5p_mfc_get_enc_strm_size()	readl(dev->regs_base + \
						S5P_FIMV_ENC_SI_STRM_SIZE)
#define s5p_mfc_get_enc_slice_type()	readl(dev->regs_base + \
						S5P_FIMV_ENC_SI_SLICE_TYPE)

#endif /* S5P_MFC_OPR_H_ */
