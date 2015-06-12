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

struct s5p_mfc_regs {

	/* codec common registers */
	volatile void __iomem *risc_on;
	volatile void __iomem *risc2host_int;
	volatile void __iomem *host2risc_int;
	volatile void __iomem *risc_base_address;
	volatile void __iomem *mfc_reset;
	volatile void __iomem *host2risc_command;
	volatile void __iomem *risc2host_command;
	volatile void __iomem *mfc_bus_reset_ctrl;
	volatile void __iomem *firmware_version;
	volatile void __iomem *instance_id;
	volatile void __iomem *codec_type;
	volatile void __iomem *context_mem_addr;
	volatile void __iomem *context_mem_size;
	volatile void __iomem *pixel_format;
	volatile void __iomem *metadata_enable;
	volatile void __iomem *mfc_version;
	volatile void __iomem *dbg_info_enable;
	volatile void __iomem *dbg_buffer_addr;
	volatile void __iomem *dbg_buffer_size;
	volatile void __iomem *hed_control;
	volatile void __iomem *mfc_timeout_value;
	volatile void __iomem *hed_shared_mem_addr;
	volatile void __iomem *dis_shared_mem_addr;/* only v7 */
	volatile void __iomem *ret_instance_id;
	volatile void __iomem *error_code;
	volatile void __iomem *dbg_buffer_output_size;
	volatile void __iomem *metadata_status;
	volatile void __iomem *metadata_addr_mb_info;
	volatile void __iomem *metadata_size_mb_info;
	volatile void __iomem *dbg_info_stage_counter;

	/* decoder registers */
	volatile void __iomem *d_crc_ctrl;
	volatile void __iomem *d_dec_options;
	volatile void __iomem *d_display_delay;
	volatile void __iomem *d_set_frame_width;
	volatile void __iomem *d_set_frame_height;
	volatile void __iomem *d_sei_enable;
	volatile void __iomem *d_min_num_dpb;
	volatile void __iomem *d_min_first_plane_dpb_size;
	volatile void __iomem *d_min_second_plane_dpb_size;
	volatile void __iomem *d_min_third_plane_dpb_size;/* only v8 */
	volatile void __iomem *d_min_num_mv;
	volatile void __iomem *d_mvc_num_views;
	volatile void __iomem *d_min_num_dis;/* only v7 */
	volatile void __iomem *d_min_first_dis_size;/* only v7 */
	volatile void __iomem *d_min_second_dis_size;/* only v7 */
	volatile void __iomem *d_min_third_dis_size;/* only v7 */
	volatile void __iomem *d_post_filter_luma_dpb0;/*  v7 and v8 */
	volatile void __iomem *d_post_filter_luma_dpb1;/* v7 and v8 */
	volatile void __iomem *d_post_filter_luma_dpb2;/* only v7 */
	volatile void __iomem *d_post_filter_chroma_dpb0;/* v7 and v8 */
	volatile void __iomem *d_post_filter_chroma_dpb1;/* v7 and v8 */
	volatile void __iomem *d_post_filter_chroma_dpb2;/* only v7 */
	volatile void __iomem *d_num_dpb;
	volatile void __iomem *d_num_mv;
	volatile void __iomem *d_init_buffer_options;
	volatile void __iomem *d_first_plane_dpb_stride_size;/* only v8 */
	volatile void __iomem *d_second_plane_dpb_stride_size;/* only v8 */
	volatile void __iomem *d_third_plane_dpb_stride_size;/* only v8 */
	volatile void __iomem *d_first_plane_dpb_size;
	volatile void __iomem *d_second_plane_dpb_size;
	volatile void __iomem *d_third_plane_dpb_size;/* only v8 */
	volatile void __iomem *d_mv_buffer_size;
	volatile void __iomem *d_first_plane_dpb;
	volatile void __iomem *d_second_plane_dpb;
	volatile void __iomem *d_third_plane_dpb;
	volatile void __iomem *d_mv_buffer;
	volatile void __iomem *d_scratch_buffer_addr;
	volatile void __iomem *d_scratch_buffer_size;
	volatile void __iomem *d_metadata_buffer_addr;
	volatile void __iomem *d_metadata_buffer_size;
	volatile void __iomem *d_nal_start_options;/* v7 and v8 */
	volatile void __iomem *d_cpb_buffer_addr;
	volatile void __iomem *d_cpb_buffer_size;
	volatile void __iomem *d_available_dpb_flag_upper;
	volatile void __iomem *d_available_dpb_flag_lower;
	volatile void __iomem *d_cpb_buffer_offset;
	volatile void __iomem *d_slice_if_enable;
	volatile void __iomem *d_picture_tag;
	volatile void __iomem *d_stream_data_size;
	volatile void __iomem *d_dynamic_dpb_flag_upper;/* v7 and v8 */
	volatile void __iomem *d_dynamic_dpb_flag_lower;/* v7 and v8 */
	volatile void __iomem *d_display_frame_width;
	volatile void __iomem *d_display_frame_height;
	volatile void __iomem *d_display_status;
	volatile void __iomem *d_display_first_plane_addr;
	volatile void __iomem *d_display_second_plane_addr;
	volatile void __iomem *d_display_third_plane_addr;/* only v8 */
	volatile void __iomem *d_display_frame_type;
	volatile void __iomem *d_display_crop_info1;
	volatile void __iomem *d_display_crop_info2;
	volatile void __iomem *d_display_picture_profile;
	volatile void __iomem *d_display_luma_crc;/* v7 and v8 */
	volatile void __iomem *d_display_chroma0_crc;/* v7 and v8 */
	volatile void __iomem *d_display_chroma1_crc;/* only v8 */
	volatile void __iomem *d_display_luma_crc_top;/* only v6 */
	volatile void __iomem *d_display_chroma_crc_top;/* only v6 */
	volatile void __iomem *d_display_luma_crc_bot;/* only v6 */
	volatile void __iomem *d_display_chroma_crc_bot;/* only v6 */
	volatile void __iomem *d_display_aspect_ratio;
	volatile void __iomem *d_display_extended_ar;
	volatile void __iomem *d_decoded_frame_width;
	volatile void __iomem *d_decoded_frame_height;
	volatile void __iomem *d_decoded_status;
	volatile void __iomem *d_decoded_first_plane_addr;
	volatile void __iomem *d_decoded_second_plane_addr;
	volatile void __iomem *d_decoded_third_plane_addr;/* only v8 */
	volatile void __iomem *d_decoded_frame_type;
	volatile void __iomem *d_decoded_crop_info1;
	volatile void __iomem *d_decoded_crop_info2;
	volatile void __iomem *d_decoded_picture_profile;
	volatile void __iomem *d_decoded_nal_size;
	volatile void __iomem *d_decoded_luma_crc;
	volatile void __iomem *d_decoded_chroma0_crc;
	volatile void __iomem *d_decoded_chroma1_crc;/* only v8 */
	volatile void __iomem *d_ret_picture_tag_top;
	volatile void __iomem *d_ret_picture_tag_bot;
	volatile void __iomem *d_ret_picture_time_top;
	volatile void __iomem *d_ret_picture_time_bot;
	volatile void __iomem *d_chroma_format;
	volatile void __iomem *d_vc1_info;/* v7 and v8 */
	volatile void __iomem *d_mpeg4_info;
	volatile void __iomem *d_h264_info;
	volatile void __iomem *d_metadata_addr_concealed_mb;
	volatile void __iomem *d_metadata_size_concealed_mb;
	volatile void __iomem *d_metadata_addr_vc1_param;
	volatile void __iomem *d_metadata_size_vc1_param;
	volatile void __iomem *d_metadata_addr_sei_nal;
	volatile void __iomem *d_metadata_size_sei_nal;
	volatile void __iomem *d_metadata_addr_vui;
	volatile void __iomem *d_metadata_size_vui;
	volatile void __iomem *d_metadata_addr_mvcvui;/* v7 and v8 */
	volatile void __iomem *d_metadata_size_mvcvui;/* v7 and v8 */
	volatile void __iomem *d_mvc_view_id;
	volatile void __iomem *d_frame_pack_sei_avail;
	volatile void __iomem *d_frame_pack_arrgment_id;
	volatile void __iomem *d_frame_pack_sei_info;
	volatile void __iomem *d_frame_pack_grid_pos;
	volatile void __iomem *d_display_recovery_sei_info;/* v7 and v8 */
	volatile void __iomem *d_decoded_recovery_sei_info;/* v7 and v8 */
	volatile void __iomem *d_display_first_addr;/* only v7 */
	volatile void __iomem *d_display_second_addr;/* only v7 */
	volatile void __iomem *d_display_third_addr;/* only v7 */
	volatile void __iomem *d_decoded_first_addr;/* only v7 */
	volatile void __iomem *d_decoded_second_addr;/* only v7 */
	volatile void __iomem *d_decoded_third_addr;/* only v7 */
	volatile void __iomem *d_used_dpb_flag_upper;/* v7 and v8 */
	volatile void __iomem *d_used_dpb_flag_lower;/* v7 and v8 */

	/* encoder registers */
	volatile void __iomem *e_frame_width;
	volatile void __iomem *e_frame_height;
	volatile void __iomem *e_cropped_frame_width;
	volatile void __iomem *e_cropped_frame_height;
	volatile void __iomem *e_frame_crop_offset;
	volatile void __iomem *e_enc_options;
	volatile void __iomem *e_picture_profile;
	volatile void __iomem *e_vbv_buffer_size;
	volatile void __iomem *e_vbv_init_delay;
	volatile void __iomem *e_fixed_picture_qp;
	volatile void __iomem *e_rc_config;
	volatile void __iomem *e_rc_qp_bound;
	volatile void __iomem *e_rc_qp_bound_pb;/* v7 and v8 */
	volatile void __iomem *e_rc_mode;
	volatile void __iomem *e_mb_rc_config;
	volatile void __iomem *e_padding_ctrl;
	volatile void __iomem *e_air_threshold;
	volatile void __iomem *e_mv_hor_range;
	volatile void __iomem *e_mv_ver_range;
	volatile void __iomem *e_num_dpb;
	volatile void __iomem *e_luma_dpb;
	volatile void __iomem *e_chroma_dpb;
	volatile void __iomem *e_me_buffer;
	volatile void __iomem *e_scratch_buffer_addr;
	volatile void __iomem *e_scratch_buffer_size;
	volatile void __iomem *e_tmv_buffer0;
	volatile void __iomem *e_tmv_buffer1;
	volatile void __iomem *e_ir_buffer_addr;/* v7 and v8 */
	volatile void __iomem *e_source_first_plane_addr;
	volatile void __iomem *e_source_second_plane_addr;
	volatile void __iomem *e_source_third_plane_addr;/* v7 and v8 */
	volatile void __iomem *e_source_first_plane_stride;/* v7 and v8 */
	volatile void __iomem *e_source_second_plane_stride;/* v7 and v8 */
	volatile void __iomem *e_source_third_plane_stride;/* v7 and v8 */
	volatile void __iomem *e_stream_buffer_addr;
	volatile void __iomem *e_stream_buffer_size;
	volatile void __iomem *e_roi_buffer_addr;
	volatile void __iomem *e_param_change;
	volatile void __iomem *e_ir_size;
	volatile void __iomem *e_gop_config;
	volatile void __iomem *e_mslice_mode;
	volatile void __iomem *e_mslice_size_mb;
	volatile void __iomem *e_mslice_size_bits;
	volatile void __iomem *e_frame_insertion;
	volatile void __iomem *e_rc_frame_rate;
	volatile void __iomem *e_rc_bit_rate;
	volatile void __iomem *e_rc_roi_ctrl;
	volatile void __iomem *e_picture_tag;
	volatile void __iomem *e_bit_count_enable;
	volatile void __iomem *e_max_bit_count;
	volatile void __iomem *e_min_bit_count;
	volatile void __iomem *e_metadata_buffer_addr;
	volatile void __iomem *e_metadata_buffer_size;
	volatile void __iomem *e_encoded_source_first_plane_addr;
	volatile void __iomem *e_encoded_source_second_plane_addr;
	volatile void __iomem *e_encoded_source_third_plane_addr;/* v7 and v8 */
	volatile void __iomem *e_stream_size;
	volatile void __iomem *e_slice_type;
	volatile void __iomem *e_picture_count;
	volatile void __iomem *e_ret_picture_tag;
	volatile void __iomem *e_stream_buffer_write_pointer; /*  only v6 */
	volatile void __iomem *e_recon_luma_dpb_addr;
	volatile void __iomem *e_recon_chroma_dpb_addr;
	volatile void __iomem *e_metadata_addr_enc_slice;
	volatile void __iomem *e_metadata_size_enc_slice;
	volatile void __iomem *e_mpeg4_options;
	volatile void __iomem *e_mpeg4_hec_period;
	volatile void __iomem *e_aspect_ratio;
	volatile void __iomem *e_extended_sar;
	volatile void __iomem *e_h264_options;
	volatile void __iomem *e_h264_options_2;/* v7 and v8 */
	volatile void __iomem *e_h264_lf_alpha_offset;
	volatile void __iomem *e_h264_lf_beta_offset;
	volatile void __iomem *e_h264_i_period;
	volatile void __iomem *e_h264_fmo_slice_grp_map_type;
	volatile void __iomem *e_h264_fmo_num_slice_grp_minus1;
	volatile void __iomem *e_h264_fmo_slice_grp_change_dir;
	volatile void __iomem *e_h264_fmo_slice_grp_change_rate_minus1;
	volatile void __iomem *e_h264_fmo_run_length_minus1_0;
	volatile void __iomem *e_h264_aso_slice_order_0;
	volatile void __iomem *e_h264_chroma_qp_offset;
	volatile void __iomem *e_h264_num_t_layer;
	volatile void __iomem *e_h264_hierarchical_qp_layer0;
	volatile void __iomem *e_h264_frame_packing_sei_info;
	volatile void __iomem *e_h264_nal_control;/* v7 and v8 */
	volatile void __iomem *e_mvc_frame_qp_view1;
	volatile void __iomem *e_mvc_rc_bit_rate_view1;
	volatile void __iomem *e_mvc_rc_qbound_view1;
	volatile void __iomem *e_mvc_rc_mode_view1;
	volatile void __iomem *e_mvc_inter_view_prediction_on;
	volatile void __iomem *e_vp8_options;/* v7 and v8 */
	volatile void __iomem *e_vp8_filter_options;/* v7 and v8 */
	volatile void __iomem *e_vp8_golden_frame_option;/* v7 and v8 */
	volatile void __iomem *e_vp8_num_t_layer;/* v7 and v8 */
	volatile void __iomem *e_vp8_hierarchical_qp_layer0;/* v7 and v8 */
	volatile void __iomem *e_vp8_hierarchical_qp_layer1;/* v7 and v8 */
	volatile void __iomem *e_vp8_hierarchical_qp_layer2;/* v7 and v8 */
};

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
			unsigned long ofs);
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
void s5p_mfc_init_regs(struct s5p_mfc_dev *dev);
int s5p_mfc_alloc_priv_buf(struct device *dev,
					struct s5p_mfc_priv_buf *b);
void s5p_mfc_release_priv_buf(struct device *dev,
					struct s5p_mfc_priv_buf *b);


#endif /* S5P_MFC_OPR_H_ */
