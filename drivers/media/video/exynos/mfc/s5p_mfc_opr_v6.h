/*
 * drivers/media/video/exynos/mfc/s5p_mfc_opr_v6.h
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

#include "s5p_mfc_mem.h"

#define MFC_CTRL_MODE_CUSTOM	MFC_CTRL_MODE_SFR

int s5p_mfc_init_decode(struct s5p_mfc_ctx *ctx);
int s5p_mfc_init_encode(struct s5p_mfc_ctx *mfc_ctx);

int s5p_mfc_set_dec_frame_buffer(struct s5p_mfc_ctx *ctx);
int s5p_mfc_set_dec_stream_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t buf_addr,
		unsigned int start_num_byte,
		unsigned int buf_size);

void s5p_mfc_set_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t y_addr, dma_addr_t c_addr);
int s5p_mfc_set_enc_stream_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t addr, unsigned int size);
void s5p_mfc_get_enc_frame_buffer(struct s5p_mfc_ctx *ctx,
		dma_addr_t *y_addr, dma_addr_t *c_addr);
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
						& S5P_FIMV_DECODED_FRAME_MASK)
#define s5p_mfc_get_disp_frame_type()	(readl(ctx->dev->regs_base + \
						S5P_FIMV_D_DISPLAY_FRAME_TYPE) \
						& S5P_FIMV_DISPLAY_FRAME_MASK)
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
#define s5p_mfc_get_mv_count()		readl(dev->regs_base + \
						S5P_FIMV_D_MIN_NUM_MV)
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
#define s5p_mfc_get_mvc_num_views()	readl(dev->regs_base + \
						S5P_FIMV_D_MVC_NUM_VIEWS)
#define s5p_mfc_get_mvc_disp_view_id()	(readl(dev->regs_base +		\
					S5P_FIMV_D_MVC_VIEW_ID)		\
					& S5P_FIMV_D_MVC_VIEW_ID_DISP_MASK)

#define s5p_mfc_is_interlace_picture()	((readl(dev->regs_base + \
					S5P_FIMV_D_DECODED_STATUS) & \
					S5P_FIMV_DEC_STATUS_INTERLACE_MASK) == \
					S5P_FIMV_DEC_STATUS_INTERLACE)

#define s5p_mfc_get_dec_status()	(readl(dev->regs_base + \
						S5P_FIMV_D_DECODED_STATUS) \
						& S5P_FIMV_DECODED_FRAME_MASK)

#define s5p_mfc_get_dec_frame()		(readl(dev->regs_base + \
						S5P_FIMV_D_DECODED_FRAME_TYPE) \
						& S5P_FIMV_DECODED_FRAME_MASK)

#define mb_width(x_size)		((x_size + 15) / 16)
#define mb_height(y_size)		((y_size + 15) / 16)
#define s5p_mfc_dec_mv_size(x, y)	(mb_width(x) * (((mb_height(y)+1)/2)*2) * 64 + 128)

#define s5p_mfc_clear_int_flags()				\
	do {							\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC2HOST_CMD);	\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC2HOST_INT);	\
	} while (0)

/* Definition */
#define ENC_MULTI_SLICE_MB_MAX		((1 << 30) - 1)
#define ENC_MULTI_SLICE_BIT_MIN		2800
#define ENC_MULTI_SLICE_BYTE_MIN	350
#define ENC_INTRA_REFRESH_MB_MAX	((1 << 18) - 1)
#define ENC_VBV_BUF_SIZE_MAX		((1 << 30) - 1)
#define ENC_H264_LOOP_FILTER_AB_MIN	-12
#define ENC_H264_LOOP_FILTER_AB_MAX	12
#define ENC_H264_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H263_RC_FRAME_RATE_MAX	((1 << 16) - 1)
#define ENC_H264_PROFILE_MAX		3
#define ENC_H264_LEVEL_MAX		42
#define ENC_MPEG4_VOP_TIME_RES_MAX	((1 << 16) - 1)
#define FRAME_DELTA_DEFAULT		1
#define TIGHT_CBR_MAX			10

/* Definitions for shared memory compatibility */
#define PIC_TIME_TOP		S5P_FIMV_D_RET_PICTURE_TAG_TOP
#define PIC_TIME_BOT		S5P_FIMV_D_RET_PICTURE_TAG_BOT
#define CROP_INFO_H		S5P_FIMV_D_DISPLAY_CROP_INFO1
#define CROP_INFO_V		S5P_FIMV_D_DISPLAY_CROP_INFO2

/* Scratch buffer size for MFC v6.1 */
#define DEC_V61_H264_SCRATCH_SIZE(x, y)				\
		((x * 128) + 65536)
#define DEC_V61_MPEG4_SCRATCH_SIZE(x, y)			\
		((x) * ((y) * 64 + 144) +			\
		 ((2048 + 15) / 16 * (y) * 64) +		\
		 ((2048 + 15) / 16 * 256 + 8320))
#define DEC_V61_VC1_SCRATCH_SIZE(x, y)				\
		(2096 * ((x) + (y) + 1))
#define DEC_V61_MPEG2_SCRATCH_SIZE(x, y)	0
#define DEC_V61_H263_SCRATCH_SIZE(x, y)				\
		((x) * 400)
#define DEC_V61_VP8_SCRATCH_SIZE(x, y)				\
		((x) * 32 + (y) * 128 + 34816)
#define ENC_V61_H264_SCRATCH_SIZE(x, y)				\
		(((x) * 64) + (((x) + 1) * 16) + (4096 * 16))
#define ENC_V61_MPEG4_SCRATCH_SIZE(x, y)			\
		(((x) * 16) + (((x) + 1) * 16))

/* Scratch buffer size for MFC v6.5 */
#define DEC_V65_H264_SCRATCH_SIZE(x, y)				\
		((x * 192) + 64)
#define DEC_V65_MPEG4_SCRATCH_SIZE(x, y)			\
		(((x) * 144) + ((y) * 8192) + 49216 + 1048576)
#define DEC_V65_VC1_SCRATCH_SIZE(x, y)				\
		(2096 * ((x) + (y) + 1))
#define DEC_V65_MPEG2_SCRATCH_SIZE(x, y)	0
#define DEC_V65_H263_SCRATCH_SIZE(x, y)				\
		((x) * 400)
#define DEC_V65_VP8_SCRATCH_SIZE(x, y)				\
		((x) * 32 + (y) * 128 +				\
		 (((x) + 1) / 2) * 64 + 2112)
#define ENC_V65_H264_SCRATCH_SIZE(x, y)				\
		(((x) * 48) + (((x) + 1) / 2 * 128) + 144)
#define ENC_V65_MPEG4_SCRATCH_SIZE(x, y)			\
		(((x) * 32) + 16)

/* Encoder buffer size for common */
#define ENC_TMV_SIZE(x, y)					\
		(((x) + 1) * ((y) + 3) * 8)
#define ENC_ME_SIZE(f_x, f_y, mb_x, mb_y)			\
		((((((f_x) + 127) / 64) * 16) *			\
		((((f_y) + 63) / 64) * 16)) +			\
		((((mb_x) * (mb_y) + 31) / 32) * 16))

/* MV range is [16,256] for v6.1, [16,128] for v6.5 */
#define ENC_V61_MV_RANGE		256
#define ENC_V65_MV_RANGE		128

#define NUM_MPEG4_LF_BUF		2

void s5p_mfc_try_run(struct s5p_mfc_dev *dev);

void s5p_mfc_cleanup_queue(struct list_head *lh, struct vb2_queue *vq);

void s5p_mfc_write_info(struct s5p_mfc_ctx *ctx, unsigned int data, unsigned int ofs);
unsigned int s5p_mfc_read_info(struct s5p_mfc_ctx *ctx, unsigned int ofs);

#endif /* S5P_MFC_OPR_V6_H_ */
