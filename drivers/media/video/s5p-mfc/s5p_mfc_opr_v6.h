/*
 * drivers/media/video/samsung/mfc5/s5p_mfc_opr.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Kamil Debski, Copyright (c) 2010 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_OPR_V6_H_
#define S5P_MFC_OPR_V6_H_

#include "s5p_mfc_common.h"
#include "s5p_mfc_mem.h"

#define MFC_CTRL_MODE_CUSTOM	MFC_CTRL_MODE_SFR

int s5p_mfc_init_decode(struct s5p_mfc_ctx *ctx);
int s5p_mfc_init_encode(struct s5p_mfc_ctx *mfc_ctx);

int s5p_mfc_set_dec_frame_buffer(struct s5p_mfc_ctx *ctx);
int s5p_mfc_set_dec_stream_buffer(struct s5p_mfc_ctx *ctx, int buf_addr,
						  unsigned int start_num_byte,
						  unsigned int buf_size);

void s5p_mfc_set_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long y_addr, unsigned long c_addr);
int s5p_mfc_set_enc_stream_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long addr, unsigned int size);
void s5p_mfc_get_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		unsigned long *y_addr, unsigned long *c_addr);
int s5p_mfc_set_enc_ref_buffer(struct s5p_mfc_ctx *mfc_ctx);

int s5p_mfc_decode_one_frame(struct s5p_mfc_ctx *ctx, int last_frame);
int s5p_mfc_encode_one_frame(struct s5p_mfc_ctx *mfc_ctx);

/* Memory allocation */
int s5p_mfc_alloc_dec_temp_buffers(struct s5p_mfc_ctx *ctx);
void s5p_mfc_set_dec_desc_buffer(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_dec_desc_buffer(struct s5p_mfc_ctx *ctx);

int s5p_mfc_alloc_codec_buffers(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_codec_buffers(struct s5p_mfc_ctx *ctx);

int s5p_mfc_alloc_instance_buffer(struct s5p_mfc_ctx *ctx);
void s5p_mfc_release_instance_buffer(struct s5p_mfc_ctx *ctx);
int s5p_mfc_alloc_dev_context_buffer(struct s5p_mfc_dev *dev);
void s5p_mfc_release_dev_context_buffer(struct s5p_mfc_dev *dev);

void s5p_mfc_dec_calc_dpb_size(struct s5p_mfc_ctx *ctx);
void s5p_mfc_enc_calc_src_size(struct s5p_mfc_ctx *ctx);

#define s5p_mfc_get_dspl_y_adr()	(readl(dev->regs_base + \
					S5P_FIMV_SI_DISPLAY_Y_ADR) << 11)
#define s5p_mfc_get_dspl_status()	readl(dev->regs_base + \
						S5P_FIMV_D_DISPLAY_STATUS)
#define s5p_mfc_get_decoded_status()	readl(dev->regs_base + \
						S5P_FIMV_D_DECODED_STATUS)
#define s5p_mfc_get_dec_frame_type()	(readl(dev->regs_base + \
						S5P_FIMV_D_DECODED_FRAME_TYPE) \
						& S5P_FIMV_DECODE_FRAME_MASK)
#define s5p_mfc_get_disp_frame_type()	(readl(ctx->dev->regs_base + \
						S5P_FIMV_D_DISPLAY_FRAME_TYPE) \
						& S5P_FIMV_DECODE_FRAME_MASK)
#define s5p_mfc_get_consumed_stream()	readl(dev->regs_base + \
						S5P_FIMV_D_DECODED_NAL_SIZE)
#define s5p_mfc_get_int_reason()	(readl(dev->regs_base + \
					S5P_FIMV_RISC2HOST_CMD) & \
					S5P_FIMV_RISC2HOST_CMD_MASK)
#define s5p_mfc_get_int_err()		readl(dev->regs_base + \
						S5P_FIMV_ERROR_CODE)
#define s5p_mfc_err_dec(x)		(((x) & S5P_FIMV_ERR_DEC_MASK) >> \
						S5P_FIMV_ERR_DEC_SHIFT)
#define s5p_mfc_err_dspl(x)		(((x) & S5P_FIMV_ERR_DSPL_MASK) >> \
						S5P_FIMV_ERR_DSPL_SHIFT)
#define s5p_mfc_get_img_width()		readl(dev->regs_base + \
						S5P_FIMV_D_DISPLAY_FRAME_WIDTH)
#define s5p_mfc_get_img_height()	readl(dev->regs_base + \
						S5P_FIMV_D_DISPLAY_FRAME_HEIGHT)
#define s5p_mfc_get_dpb_count()		readl(dev->regs_base + \
						S5P_FIMV_D_MIN_NUM_DPB)
#define s5p_mfc_get_inst_no()		readl(dev->regs_base + \
						S5P_FIMV_RET_INSTANCE_ID)
#define s5p_mfc_get_enc_dpb_count()	readl(dev->regs_base + \
						S5P_FIMV_E_NUM_DPB)
#define s5p_mfc_get_enc_strm_size()	readl(dev->regs_base + \
						S5P_FIMV_E_STREAM_SIZE)
#define s5p_mfc_get_enc_slice_type()	readl(dev->regs_base + \
						S5P_FIMV_E_SLICE_TYPE)
#define s5p_mfc_get_enc_pic_count()	readl(dev->regs_base + \
						S5P_FIMV_E_PICTURE_COUNT)
#define s5p_mfc_get_sei_avail_status()	readl(dev->regs_base + \
						S5P_FIMV_D_FRAME_PACK_SEI_AVAIL)

#define mb_width(x_size)		((x_size + 15) / 16)
#define mb_height(y_size)		((y_size + 15) / 16)
#define s5p_mfc_dec_mv_size(x, y)	(mb_width(x) * (((mb_height(y)+1)/2)*2) * 64 + 128)

#define s5p_mfc_clear_int_flags()				\
	do {							\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC2HOST_CMD);	\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC2HOST_INT);	\
	} while (0)

/* Definition */
#define ENC_MULTI_SLICE_MB_MAX		(1 << 30) - 1
#define ENC_MULTI_SLICE_BIT_MIN		2800
#define ENC_INTRA_REFRESH_MB_MAX	(1 << 18) - 1
#define ENC_VBV_BUF_SIZE_MAX		(1 << 30) - 1
#define ENC_H264_LOOP_FILTER_AB_MIN	-12
#define ENC_H264_LOOP_FILTER_AB_MAX	12
#define ENC_H264_RC_FRAME_RATE_MAX	(1 << 16) - 1
#define ENC_H263_RC_FRAME_RATE_MAX	(1 << 16) - 1
#define ENC_H264_PROFILE_MAX		3
#define ENC_H264_LEVEL_MAX		42
#define ENC_MPEG4_VOP_TIME_RES_MAX	(1 << 16) - 1
#define FRAME_DELTA_H264_H263		1
#define TIGHT_CBR_MAX			10

/* Definitions for shared memory compatibility */
#define PIC_TIME_TOP		S5P_FIMV_D_RET_PICTURE_TAG_TOP
#define PIC_TIME_BOT		S5P_FIMV_D_RET_PICTURE_TAG_BOT
#define CROP_INFO_H		S5P_FIMV_D_DISPLAY_CROP_INFO1
#define CROP_INFO_V		S5P_FIMV_D_DISPLAY_CROP_INFO2

/* FIXME: temporal definition to avoid compile error */
enum MFC_SHM_OFS
{
	EXT_ENC_CONTROL		= 0x28,	/* E */
	RC_VOP_TIMING		= 0x30,	/* E, MPEG4 */

	P_B_FRAME_QP		= 0x70,	/* E */
	ASPECT_RATIO_IDC	= 0x74, /* E, H.264, depend on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */
	EXTENDED_SAR		= 0x78, /* E, H.264, depned on ASPECT_RATIO_VUI_ENABLE in EXT_ENC_CONTROL */

	H264_I_PERIOD		= 0x9C, /* E, H.264, open GOP */
	RC_CONTROL_CONFIG	= 0xA0, /* E */
};

void s5p_mfc_try_run(struct s5p_mfc_dev *dev);

void s5p_mfc_cleanup_queue(struct list_head *lh, struct vb2_queue *vq);

void s5p_mfc_write_info(struct s5p_mfc_ctx *ctx, unsigned int data, unsigned int ofs);
unsigned int s5p_mfc_read_info(struct s5p_mfc_ctx *ctx, unsigned int ofs);

#endif /* S5P_MFC_OPR_V6_H_ */
