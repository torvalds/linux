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

#ifndef S5P_MFC_OPR_V5_H_
#define S5P_MFC_OPR_V5_H_

#include "s5p_mfc_common.h"
#include "s5p_mfc_mem.h"

#define MFC_CTRL_MODE_CUSTOM	MFC_CTRL_MODE_SHM

/*
int s5p_mfc_release_firmware(struct s5p_mfc_dev *dev);
int s5p_mfc_alloc_firmware(struct s5p_mfc_dev *dev);
int s5p_mfc_load_firmware(struct s5p_mfc_dev *dev);
int s5p_mfc_init_hw(struct s5p_mfc_dev *dev);
*/

int s5p_mfc_init_decode(struct s5p_mfc_ctx *ctx);
int s5p_mfc_init_encode(struct s5p_mfc_ctx *mfc_ctx);
/*
void s5p_mfc_deinit_hw(struct s5p_mfc_dev *dev);
int s5p_mfc_set_sleep(struct s5p_mfc_ctx *ctx);
int s5p_mfc_set_wakeup(struct s5p_mfc_ctx *ctx);
*/

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

/* Instance handling */
/*
int s5p_mfc_open_inst(struct s5p_mfc_ctx *ctx);
int s5p_mfc_return_inst_no(struct s5p_mfc_ctx *ctx);
*/

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
						S5P_FIMV_SI_DISPLAY_STATUS)
#define s5p_mfc_get_dec_frame_type()	(readl(dev->regs_base + \
						S5P_FIMV_DECODE_FRAME_TYPE) \
					& S5P_FIMV_DECODE_FRAME_MASK)
#define s5p_mfc_get_disp_frame_type()	((s5p_mfc_read_shm(ctx, DISP_PIC_FRAME_TYPE) \
						>> S5P_FIMV_SHARED_DISP_FRAME_TYPE_SHIFT) \
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
#define s5p_mfc_get_enc_dpb_count()	-1
#define s5p_mfc_get_enc_strm_size()	readl(dev->regs_base + \
						S5P_FIMV_ENC_SI_STRM_SIZE)
#define s5p_mfc_get_enc_slice_type()	readl(dev->regs_base + \
						S5P_FIMV_ENC_SI_SLICE_TYPE)
#define s5p_mfc_get_enc_pic_count()	readl(dev->regs_base + \
						S5P_FIMV_ENC_SI_PIC_CNT)
#define s5p_mfc_get_sei_avail_status()	s5p_mfc_read_shm(ctx, FRAME_PACK_SEI_AVAIL)

#define s5p_mfc_clear_int_flags()				\
	do {							\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC_HOST_INT);	\
		s5p_mfc_write_reg(0, S5P_FIMV_RISC2HOST_CMD);	\
		s5p_mfc_write_reg(0xffff, S5P_FIMV_SI_RTN_CHID);\
	} while (0)

/* Definition */
#define ENC_MULTI_SLICE_MB_MAX 		(1 << 16) - 1
#define ENC_MULTI_SLICE_BIT_MIN		1900
#define ENC_INTRA_REFRESH_MB_MAX	(1 << 16) - 1
#define ENC_VBV_BUF_SIZE_MAX		(1 << 16) - 1
#define ENC_H264_LOOP_FILTER_AB_MIN	-6
#define ENC_H264_LOOP_FILTER_AB_MAX	6
#define ENC_H264_RC_FRAME_RATE_MAX	(1 << 30) -1
#define ENC_H263_RC_FRAME_RATE_MAX	(1 << 30) -1
#define ENC_H264_PROFILE_MAX		2
#define ENC_H264_LEVEL_MAX		40
#define ENC_MPEG4_VOP_TIME_RES_MAX	(1 << 15) - 1

void s5p_mfc_try_run(struct s5p_mfc_dev *dev);

void s5p_mfc_cleanup_queue(struct list_head *lh, struct vb2_queue *vq);

void s5p_mfc_write_info(struct s5p_mfc_ctx *ctx, unsigned int data, unsigned int ofs);
unsigned int s5p_mfc_read_info(struct s5p_mfc_ctx *ctx, unsigned int ofs);

#endif /* S5P_MFC_OPR_V5_H_ */
