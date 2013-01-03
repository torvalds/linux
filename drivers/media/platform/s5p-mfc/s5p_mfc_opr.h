/*
 * drivers/media/platform/s5p-mfc/s5p_mfc_opr.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Kamil Debski, Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_OPR_H_
#define S5P_MFC_OPR_H_

#include "s5p_mfc_common.h"

struct s5p_mfc_hw_ops {
	int (*alloc_dec_temp_buffers)(struct s5p_mfc_ctx *ctx);
	void (*release_dec_desc_buffer)(struct s5p_mfc_ctx *ctx);
	int (*alloc_codec_buffers)(struct s5p_mfc_ctx *ctx);
	void (*release_codec_buffers)(struct s5p_mfc_ctx *ctx);
	int (*alloc_instance_buffer)(struct s5p_mfc_ctx *ctx);
	void (*release_instance_buffer)(struct s5p_mfc_ctx *ctx);
	int (*alloc_dev_context_buffer)(struct s5p_mfc_dev *dev);
	void (*release_dev_context_buffer)(struct s5p_mfc_dev *dev);
	void (*dec_calc_dpb_size)(struct s5p_mfc_ctx *ctx);
	void (*enc_calc_src_size)(struct s5p_mfc_ctx *ctx);
	int (*set_dec_stream_buffer)(struct s5p_mfc_ctx *ctx,
			int buf_addr, unsigned int start_num_byte,
			unsigned int buf_size);
	int (*set_dec_frame_buffer)(struct s5p_mfc_ctx *ctx);
	int (*set_enc_stream_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long addr, unsigned int size);
	void (*set_enc_frame_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long y_addr, unsigned long c_addr);
	void (*get_enc_frame_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long *y_addr, unsigned long *c_addr);
	int (*set_enc_ref_buffer)(struct s5p_mfc_ctx *ctx);
	int (*init_decode)(struct s5p_mfc_ctx *ctx);
	int (*init_encode)(struct s5p_mfc_ctx *ctx);
	int (*encode_one_frame)(struct s5p_mfc_ctx *ctx);
	void (*try_run)(struct s5p_mfc_dev *dev);
	void (*cleanup_queue)(struct list_head *lh,
			struct vb2_queue *vq);
	void (*clear_int_flags)(struct s5p_mfc_dev *dev);
	void (*write_info)(struct s5p_mfc_ctx *ctx, unsigned int data,
			unsigned int ofs);
	unsigned int (*read_info)(struct s5p_mfc_ctx *ctx,
			unsigned int ofs);
	int (*get_dspl_y_adr)(struct s5p_mfc_dev *dev);
	int (*get_dec_y_adr)(struct s5p_mfc_dev *dev);
	int (*get_dspl_status)(struct s5p_mfc_dev *dev);
	int (*get_dec_status)(struct s5p_mfc_dev *dev);
	int (*get_dec_frame_type)(struct s5p_mfc_dev *dev);
	int (*get_disp_frame_type)(struct s5p_mfc_ctx *ctx);
	int (*get_consumed_stream)(struct s5p_mfc_dev *dev);
	int (*get_int_reason)(struct s5p_mfc_dev *dev);
	int (*get_int_err)(struct s5p_mfc_dev *dev);
	int (*err_dec)(unsigned int err);
	int (*err_dspl)(unsigned int err);
	int (*get_img_width)(struct s5p_mfc_dev *dev);
	int (*get_img_height)(struct s5p_mfc_dev *dev);
	int (*get_dpb_count)(struct s5p_mfc_dev *dev);
	int (*get_mv_count)(struct s5p_mfc_dev *dev);
	int (*get_inst_no)(struct s5p_mfc_dev *dev);
	int (*get_enc_strm_size)(struct s5p_mfc_dev *dev);
	int (*get_enc_slice_type)(struct s5p_mfc_dev *dev);
	int (*get_enc_dpb_count)(struct s5p_mfc_dev *dev);
	int (*get_enc_pic_count)(struct s5p_mfc_dev *dev);
	int (*get_sei_avail_status)(struct s5p_mfc_ctx *ctx);
	int (*get_mvc_num_views)(struct s5p_mfc_dev *dev);
	int (*get_mvc_view_id)(struct s5p_mfc_dev *dev);
	unsigned int (*get_pic_type_top)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_pic_type_bot)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_crop_info_h)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_crop_info_v)(struct s5p_mfc_ctx *ctx);
};

void s5p_mfc_init_hw_ops(struct s5p_mfc_dev *dev);
int s5p_mfc_alloc_priv_buf(struct device *dev,
					struct s5p_mfc_priv_buf *b);
void s5p_mfc_release_priv_buf(struct device *dev,
					struct s5p_mfc_priv_buf *b);


#endif /* S5P_MFC_OPR_H_ */
