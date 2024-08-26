/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _VDEC_H264_REQ_COMMON_H_
#define _VDEC_H264_REQ_COMMON_H_

#include <linux/module.h>
#include <linux/slab.h>
#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "../mtk_vcodec_dec_drv.h"

#define NAL_NON_IDR_SLICE			0x01
#define NAL_IDR_SLICE				0x05
#define NAL_TYPE(value)				((value) & 0x1F)

#define BUF_PREDICTION_SZ			(64 * 4096)
#define MB_UNIT_LEN				16

/* motion vector size (bytes) for every macro block */
#define HW_MB_STORE_SZ				64

#define H264_MAX_MV_NUM				32

/**
 * struct mtk_h264_dpb_info  - h264 dpb information
 *
 * @y_dma_addr:	Y bitstream physical address
 * @c_dma_addr:	CbCr bitstream physical address
 * @reference_flag:	reference picture flag (short/long term reference picture)
 * @field:		field picture flag
 */
struct mtk_h264_dpb_info {
	dma_addr_t y_dma_addr;
	dma_addr_t c_dma_addr;
	int reference_flag;
	int field;
};

/*
 * struct mtk_h264_sps_param  - parameters for sps
 */
struct mtk_h264_sps_param {
	unsigned char chroma_format_idc;
	unsigned char bit_depth_luma_minus8;
	unsigned char bit_depth_chroma_minus8;
	unsigned char log2_max_frame_num_minus4;
	unsigned char pic_order_cnt_type;
	unsigned char log2_max_pic_order_cnt_lsb_minus4;
	unsigned char max_num_ref_frames;
	unsigned char separate_colour_plane_flag;
	unsigned short pic_width_in_mbs_minus1;
	unsigned short pic_height_in_map_units_minus1;
	unsigned int max_frame_nums;
	unsigned char qpprime_y_zero_transform_bypass_flag;
	unsigned char delta_pic_order_always_zero_flag;
	unsigned char frame_mbs_only_flag;
	unsigned char mb_adaptive_frame_field_flag;
	unsigned char direct_8x8_inference_flag;
	unsigned char reserved[3];
};

/*
 * struct mtk_h264_pps_param  - parameters for pps
 */
struct mtk_h264_pps_param {
	unsigned char num_ref_idx_l0_default_active_minus1;
	unsigned char num_ref_idx_l1_default_active_minus1;
	unsigned char weighted_bipred_idc;
	char pic_init_qp_minus26;
	char chroma_qp_index_offset;
	char second_chroma_qp_index_offset;
	unsigned char entropy_coding_mode_flag;
	unsigned char pic_order_present_flag;
	unsigned char deblocking_filter_control_present_flag;
	unsigned char constrained_intra_pred_flag;
	unsigned char weighted_pred_flag;
	unsigned char redundant_pic_cnt_present_flag;
	unsigned char transform_8x8_mode_flag;
	unsigned char scaling_matrix_present_flag;
	unsigned char reserved[2];
};

/*
 * struct mtk_h264_slice_hd_param  - parameters for slice header
 */
struct mtk_h264_slice_hd_param {
	unsigned int first_mb_in_slice;
	unsigned int field_pic_flag;
	unsigned int slice_type;
	unsigned int frame_num;
	int pic_order_cnt_lsb;
	int delta_pic_order_cnt_bottom;
	unsigned int bottom_field_flag;
	unsigned int direct_spatial_mv_pred_flag;
	int delta_pic_order_cnt0;
	int delta_pic_order_cnt1;
	unsigned int cabac_init_idc;
	int slice_qp_delta;
	unsigned int disable_deblocking_filter_idc;
	int slice_alpha_c0_offset_div2;
	int slice_beta_offset_div2;
	unsigned int num_ref_idx_l0_active_minus1;
	unsigned int num_ref_idx_l1_active_minus1;
	unsigned int reserved;
};

/*
 * struct slice_api_h264_scaling_matrix  - parameters for scaling list
 */
struct slice_api_h264_scaling_matrix {
	unsigned char scaling_list_4x4[6][16];
	unsigned char scaling_list_8x8[6][64];
};

/*
 * struct slice_h264_dpb_entry  - each dpb information
 */
struct slice_h264_dpb_entry {
	unsigned long long reference_ts;
	unsigned short frame_num;
	unsigned short pic_num;
	/* Note that field is indicated by v4l2_buffer.field */
	int top_field_order_cnt;
	int bottom_field_order_cnt;
	unsigned int flags;
};

/*
 * struct slice_api_h264_decode_param - parameters for decode.
 */
struct slice_api_h264_decode_param {
	struct slice_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES];
	unsigned short num_slices;
	unsigned short nal_ref_idc;
	unsigned char ref_pic_list_p0[32];
	unsigned char ref_pic_list_b0[32];
	unsigned char ref_pic_list_b1[32];
	int top_field_order_cnt;
	int bottom_field_order_cnt;
	unsigned int flags;
};

/**
 * struct h264_fb - h264 decode frame buffer information
 *
 * @vdec_fb_va:	virtual address of struct vdec_fb
 * @y_fb_dma:		dma address of Y frame buffer (luma)
 * @c_fb_dma:		dma address of C frame buffer (chroma)
 * @poc:		picture order count of frame buffer
 * @reserved:		for 8 bytes alignment
 */
struct h264_fb {
	u64 vdec_fb_va;
	u64 y_fb_dma;
	u64 c_fb_dma;
	s32 poc;
	u32 reserved;
};

/**
 * mtk_vdec_h264_get_ref_list - translate V4L2 reference list
 *
 * @ref_list:		Mediatek reference picture list
 * @v4l2_ref_list:	V4L2 reference picture list
 * @num_valid:		used reference number
 */
void mtk_vdec_h264_get_ref_list(u8 *ref_list,
				const struct v4l2_h264_reference *v4l2_ref_list,
				int num_valid);

/**
 * mtk_vdec_h264_get_ctrl_ptr - get each CID control address.
 *
 * @ctx:	v4l2 ctx
 * @id:	CID control ID
 *
 * Return: returns CID ctrl address.
 */
void *mtk_vdec_h264_get_ctrl_ptr(struct mtk_vcodec_dec_ctx *ctx, int id);

/**
 * mtk_vdec_h264_fill_dpb_info - Fill the decoded picture buffer info
 *
 * @ctx:		v4l2 ctx
 * @decode_params:	slice decode params
 * @h264_dpb_info:	dpb buffer information
 */
void mtk_vdec_h264_fill_dpb_info(struct mtk_vcodec_dec_ctx *ctx,
				 struct slice_api_h264_decode_param *decode_params,
				 struct mtk_h264_dpb_info *h264_dpb_info);

/**
 * mtk_vdec_h264_copy_sps_params - get sps params.
 *
 * @dst_param:	sps params for hw decoder
 * @src_param:	sps params from user driver
 */
void mtk_vdec_h264_copy_sps_params(struct mtk_h264_sps_param *dst_param,
				   const struct v4l2_ctrl_h264_sps *src_param);

/**
 * mtk_vdec_h264_copy_pps_params - get pps params.
 *
 * @dst_param:	pps params for hw decoder
 * @src_param:	pps params from user driver
 */
void mtk_vdec_h264_copy_pps_params(struct mtk_h264_pps_param *dst_param,
				   const struct v4l2_ctrl_h264_pps *src_param);

/**
 * mtk_vdec_h264_copy_slice_hd_params - get slice header params.
 *
 * @dst_param:	slice params for hw decoder
 * @src_param:	slice params from user driver
 * @dec_param:	decode params from user driver
 */
void mtk_vdec_h264_copy_slice_hd_params(struct mtk_h264_slice_hd_param *dst_param,
					const struct v4l2_ctrl_h264_slice_params *src_param,
					const struct v4l2_ctrl_h264_decode_params *dec_param);

/**
 * mtk_vdec_h264_copy_scaling_matrix - Copy scaling matrix from a control to the driver
 *
 * @dst_matrix:	scaling list params for the HW decoder
 * @src_matrix:	scaling list params from a V4L2 control
 *
 * This function is used to copy the scaling matrix from a
 * v4l2 control into the slice parameters for a decode.
 */
void mtk_vdec_h264_copy_scaling_matrix(struct slice_api_h264_scaling_matrix *dst_matrix,
				       const struct v4l2_ctrl_h264_scaling_matrix *src_matrix);

/**
 * mtk_vdec_h264_copy_decode_params - get decode params.
 *
 * @dst_params:	dst params for hw decoder
 * @src_params:	decode params from user driver
 * @dpb:		dpb information
 */
void
mtk_vdec_h264_copy_decode_params(struct slice_api_h264_decode_param *dst_params,
				 const struct v4l2_ctrl_h264_decode_params *src_params,
				 const struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES]);

/**
 * mtk_vdec_h264_update_dpb - update dpb list.
 *
 * @dec_param:	v4l2 control decode params
 * @dpb:	dpb entry informaton
 */
void mtk_vdec_h264_update_dpb(const struct v4l2_ctrl_h264_decode_params *dec_param,
			      struct v4l2_h264_dpb_entry *dpb);

/**
 * mtk_vdec_h264_find_start_code - find h264 start code using sofeware.
 *
 * @data:	input buffer address
 * @data_sz:	input buffer size
 *
 * Return: returns start code position.
 */
int mtk_vdec_h264_find_start_code(unsigned char *data, unsigned int data_sz);

/**
 * mtk_vdec_h264_get_mv_buf_size - get mv buffer size.
 *
 * @width:	picture width
 * @height:	picture height
 *
 * Return: returns mv buffer size.
 */
unsigned int mtk_vdec_h264_get_mv_buf_size(unsigned int width, unsigned int height);

#endif
