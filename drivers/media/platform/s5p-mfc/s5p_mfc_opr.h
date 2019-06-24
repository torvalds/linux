/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/platform/s5p-mfc/s5p_mfc_opr.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * Contains declarations of hw related functions.
 *
 * Kamil Debski, Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 */

#ifndef S5P_MFC_OPR_H_
#define S5P_MFC_OPR_H_

#include "s5p_mfc_common.h"

struct s5p_mfc_regs {

	/* codec common registers */
	void __iomem *risc_on;
	void __iomem *risc2host_int;
	void __iomem *host2risc_int;
	void __iomem *risc_base_address;
	void __iomem *mfc_reset;
	void __iomem *host2risc_command;
	void __iomem *risc2host_command;
	void __iomem *mfc_bus_reset_ctrl;
	void __iomem *firmware_version;
	void __iomem *instance_id;
	void __iomem *codec_type;
	void __iomem *context_mem_addr;
	void __iomem *context_mem_size;
	void __iomem *pixel_format;
	void __iomem *metadata_enable;
	void __iomem *mfc_version;
	void __iomem *dbg_info_enable;
	void __iomem *dbg_buffer_addr;
	void __iomem *dbg_buffer_size;
	void __iomem *hed_control;
	void __iomem *mfc_timeout_value;
	void __iomem *hed_shared_mem_addr;
	void __iomem *dis_shared_mem_addr;/* only v7 */
	void __iomem *ret_instance_id;
	void __iomem *error_code;
	void __iomem *dbg_buffer_output_size;
	void __iomem *metadata_status;
	void __iomem *metadata_addr_mb_info;
	void __iomem *metadata_size_mb_info;
	void __iomem *dbg_info_stage_counter;

	/* decoder registers */
	void __iomem *d_crc_ctrl;
	void __iomem *d_dec_options;
	void __iomem *d_display_delay;
	void __iomem *d_set_frame_width;
	void __iomem *d_set_frame_height;
	void __iomem *d_sei_enable;
	void __iomem *d_min_num_dpb;
	void __iomem *d_min_first_plane_dpb_size;
	void __iomem *d_min_second_plane_dpb_size;
	void __iomem *d_min_third_plane_dpb_size;/* only v8 */
	void __iomem *d_min_num_mv;
	void __iomem *d_mvc_num_views;
	void __iomem *d_min_num_dis;/* only v7 */
	void __iomem *d_min_first_dis_size;/* only v7 */
	void __iomem *d_min_second_dis_size;/* only v7 */
	void __iomem *d_min_third_dis_size;/* only v7 */
	void __iomem *d_post_filter_luma_dpb0;/*  v7 and v8 */
	void __iomem *d_post_filter_luma_dpb1;/* v7 and v8 */
	void __iomem *d_post_filter_luma_dpb2;/* only v7 */
	void __iomem *d_post_filter_chroma_dpb0;/* v7 and v8 */
	void __iomem *d_post_filter_chroma_dpb1;/* v7 and v8 */
	void __iomem *d_post_filter_chroma_dpb2;/* only v7 */
	void __iomem *d_num_dpb;
	void __iomem *d_num_mv;
	void __iomem *d_init_buffer_options;
	void __iomem *d_first_plane_dpb_stride_size;/* only v8 */
	void __iomem *d_second_plane_dpb_stride_size;/* only v8 */
	void __iomem *d_third_plane_dpb_stride_size;/* only v8 */
	void __iomem *d_first_plane_dpb_size;
	void __iomem *d_second_plane_dpb_size;
	void __iomem *d_third_plane_dpb_size;/* only v8 */
	void __iomem *d_mv_buffer_size;
	void __iomem *d_first_plane_dpb;
	void __iomem *d_second_plane_dpb;
	void __iomem *d_third_plane_dpb;
	void __iomem *d_mv_buffer;
	void __iomem *d_scratch_buffer_addr;
	void __iomem *d_scratch_buffer_size;
	void __iomem *d_metadata_buffer_addr;
	void __iomem *d_metadata_buffer_size;
	void __iomem *d_nal_start_options;/* v7 and v8 */
	void __iomem *d_cpb_buffer_addr;
	void __iomem *d_cpb_buffer_size;
	void __iomem *d_available_dpb_flag_upper;
	void __iomem *d_available_dpb_flag_lower;
	void __iomem *d_cpb_buffer_offset;
	void __iomem *d_slice_if_enable;
	void __iomem *d_picture_tag;
	void __iomem *d_stream_data_size;
	void __iomem *d_dynamic_dpb_flag_upper;/* v7 and v8 */
	void __iomem *d_dynamic_dpb_flag_lower;/* v7 and v8 */
	void __iomem *d_display_frame_width;
	void __iomem *d_display_frame_height;
	void __iomem *d_display_status;
	void __iomem *d_display_first_plane_addr;
	void __iomem *d_display_second_plane_addr;
	void __iomem *d_display_third_plane_addr;/* only v8 */
	void __iomem *d_display_frame_type;
	void __iomem *d_display_crop_info1;
	void __iomem *d_display_crop_info2;
	void __iomem *d_display_picture_profile;
	void __iomem *d_display_luma_crc;/* v7 and v8 */
	void __iomem *d_display_chroma0_crc;/* v7 and v8 */
	void __iomem *d_display_chroma1_crc;/* only v8 */
	void __iomem *d_display_luma_crc_top;/* only v6 */
	void __iomem *d_display_chroma_crc_top;/* only v6 */
	void __iomem *d_display_luma_crc_bot;/* only v6 */
	void __iomem *d_display_chroma_crc_bot;/* only v6 */
	void __iomem *d_display_aspect_ratio;
	void __iomem *d_display_extended_ar;
	void __iomem *d_decoded_frame_width;
	void __iomem *d_decoded_frame_height;
	void __iomem *d_decoded_status;
	void __iomem *d_decoded_first_plane_addr;
	void __iomem *d_decoded_second_plane_addr;
	void __iomem *d_decoded_third_plane_addr;/* only v8 */
	void __iomem *d_decoded_frame_type;
	void __iomem *d_decoded_crop_info1;
	void __iomem *d_decoded_crop_info2;
	void __iomem *d_decoded_picture_profile;
	void __iomem *d_decoded_nal_size;
	void __iomem *d_decoded_luma_crc;
	void __iomem *d_decoded_chroma0_crc;
	void __iomem *d_decoded_chroma1_crc;/* only v8 */
	void __iomem *d_ret_picture_tag_top;
	void __iomem *d_ret_picture_tag_bot;
	void __iomem *d_ret_picture_time_top;
	void __iomem *d_ret_picture_time_bot;
	void __iomem *d_chroma_format;
	void __iomem *d_vc1_info;/* v7 and v8 */
	void __iomem *d_mpeg4_info;
	void __iomem *d_h264_info;
	void __iomem *d_metadata_addr_concealed_mb;
	void __iomem *d_metadata_size_concealed_mb;
	void __iomem *d_metadata_addr_vc1_param;
	void __iomem *d_metadata_size_vc1_param;
	void __iomem *d_metadata_addr_sei_nal;
	void __iomem *d_metadata_size_sei_nal;
	void __iomem *d_metadata_addr_vui;
	void __iomem *d_metadata_size_vui;
	void __iomem *d_metadata_addr_mvcvui;/* v7 and v8 */
	void __iomem *d_metadata_size_mvcvui;/* v7 and v8 */
	void __iomem *d_mvc_view_id;
	void __iomem *d_frame_pack_sei_avail;
	void __iomem *d_frame_pack_arrgment_id;
	void __iomem *d_frame_pack_sei_info;
	void __iomem *d_frame_pack_grid_pos;
	void __iomem *d_display_recovery_sei_info;/* v7 and v8 */
	void __iomem *d_decoded_recovery_sei_info;/* v7 and v8 */
	void __iomem *d_display_first_addr;/* only v7 */
	void __iomem *d_display_second_addr;/* only v7 */
	void __iomem *d_display_third_addr;/* only v7 */
	void __iomem *d_decoded_first_addr;/* only v7 */
	void __iomem *d_decoded_second_addr;/* only v7 */
	void __iomem *d_decoded_third_addr;/* only v7 */
	void __iomem *d_used_dpb_flag_upper;/* v7 and v8 */
	void __iomem *d_used_dpb_flag_lower;/* v7 and v8 */
	void __iomem *d_min_scratch_buffer_size; /* v10 */
	void __iomem *d_static_buffer_addr; /* v10 */
	void __iomem *d_static_buffer_size; /* v10 */

	/* encoder registers */
	void __iomem *e_frame_width;
	void __iomem *e_frame_height;
	void __iomem *e_cropped_frame_width;
	void __iomem *e_cropped_frame_height;
	void __iomem *e_frame_crop_offset;
	void __iomem *e_enc_options;
	void __iomem *e_picture_profile;
	void __iomem *e_vbv_buffer_size;
	void __iomem *e_vbv_init_delay;
	void __iomem *e_fixed_picture_qp;
	void __iomem *e_rc_config;
	void __iomem *e_rc_qp_bound;
	void __iomem *e_rc_qp_bound_pb;/* v7 and v8 */
	void __iomem *e_rc_mode;
	void __iomem *e_mb_rc_config;
	void __iomem *e_padding_ctrl;
	void __iomem *e_air_threshold;
	void __iomem *e_mv_hor_range;
	void __iomem *e_mv_ver_range;
	void __iomem *e_num_dpb;
	void __iomem *e_luma_dpb;
	void __iomem *e_chroma_dpb;
	void __iomem *e_me_buffer;
	void __iomem *e_scratch_buffer_addr;
	void __iomem *e_scratch_buffer_size;
	void __iomem *e_tmv_buffer0;
	void __iomem *e_tmv_buffer1;
	void __iomem *e_ir_buffer_addr;/* v7 and v8 */
	void __iomem *e_source_first_plane_addr;
	void __iomem *e_source_second_plane_addr;
	void __iomem *e_source_third_plane_addr;/* v7 and v8 */
	void __iomem *e_source_first_plane_stride;/* v7 and v8 */
	void __iomem *e_source_second_plane_stride;/* v7 and v8 */
	void __iomem *e_source_third_plane_stride;/* v7 and v8 */
	void __iomem *e_stream_buffer_addr;
	void __iomem *e_stream_buffer_size;
	void __iomem *e_roi_buffer_addr;
	void __iomem *e_param_change;
	void __iomem *e_ir_size;
	void __iomem *e_gop_config;
	void __iomem *e_mslice_mode;
	void __iomem *e_mslice_size_mb;
	void __iomem *e_mslice_size_bits;
	void __iomem *e_frame_insertion;
	void __iomem *e_rc_frame_rate;
	void __iomem *e_rc_bit_rate;
	void __iomem *e_rc_roi_ctrl;
	void __iomem *e_picture_tag;
	void __iomem *e_bit_count_enable;
	void __iomem *e_max_bit_count;
	void __iomem *e_min_bit_count;
	void __iomem *e_metadata_buffer_addr;
	void __iomem *e_metadata_buffer_size;
	void __iomem *e_encoded_source_first_plane_addr;
	void __iomem *e_encoded_source_second_plane_addr;
	void __iomem *e_encoded_source_third_plane_addr;/* v7 and v8 */
	void __iomem *e_stream_size;
	void __iomem *e_slice_type;
	void __iomem *e_picture_count;
	void __iomem *e_ret_picture_tag;
	void __iomem *e_stream_buffer_write_pointer; /*  only v6 */
	void __iomem *e_recon_luma_dpb_addr;
	void __iomem *e_recon_chroma_dpb_addr;
	void __iomem *e_metadata_addr_enc_slice;
	void __iomem *e_metadata_size_enc_slice;
	void __iomem *e_mpeg4_options;
	void __iomem *e_mpeg4_hec_period;
	void __iomem *e_aspect_ratio;
	void __iomem *e_extended_sar;
	void __iomem *e_h264_options;
	void __iomem *e_h264_options_2;/* v7 and v8 */
	void __iomem *e_h264_lf_alpha_offset;
	void __iomem *e_h264_lf_beta_offset;
	void __iomem *e_h264_i_period;
	void __iomem *e_h264_fmo_slice_grp_map_type;
	void __iomem *e_h264_fmo_num_slice_grp_minus1;
	void __iomem *e_h264_fmo_slice_grp_change_dir;
	void __iomem *e_h264_fmo_slice_grp_change_rate_minus1;
	void __iomem *e_h264_fmo_run_length_minus1_0;
	void __iomem *e_h264_aso_slice_order_0;
	void __iomem *e_h264_chroma_qp_offset;
	void __iomem *e_h264_num_t_layer;
	void __iomem *e_h264_hierarchical_qp_layer0;
	void __iomem *e_h264_frame_packing_sei_info;
	void __iomem *e_h264_nal_control;/* v7 and v8 */
	void __iomem *e_mvc_frame_qp_view1;
	void __iomem *e_mvc_rc_bit_rate_view1;
	void __iomem *e_mvc_rc_qbound_view1;
	void __iomem *e_mvc_rc_mode_view1;
	void __iomem *e_mvc_inter_view_prediction_on;
	void __iomem *e_vp8_options;/* v7 and v8 */
	void __iomem *e_vp8_filter_options;/* v7 and v8 */
	void __iomem *e_vp8_golden_frame_option;/* v7 and v8 */
	void __iomem *e_vp8_num_t_layer;/* v7 and v8 */
	void __iomem *e_vp8_hierarchical_qp_layer0;/* v7 and v8 */
	void __iomem *e_vp8_hierarchical_qp_layer1;/* v7 and v8 */
	void __iomem *e_vp8_hierarchical_qp_layer2;/* v7 and v8 */
	void __iomem *e_min_scratch_buffer_size; /* v10 */
	void __iomem *e_num_t_layer; /* v10 */
	void __iomem *e_hier_qp_layer0; /* v10 */
	void __iomem *e_hier_bit_rate_layer0; /* v10 */
	void __iomem *e_hevc_options; /* v10 */
	void __iomem *e_hevc_refresh_period; /* v10 */
	void __iomem *e_hevc_lf_beta_offset_div2; /* v10 */
	void __iomem *e_hevc_lf_tc_offset_div2; /* v10 */
	void __iomem *e_hevc_nal_control; /* v10 */
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
	int (*set_enc_stream_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long addr, unsigned int size);
	void (*set_enc_frame_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long y_addr, unsigned long c_addr);
	void (*get_enc_frame_buffer)(struct s5p_mfc_ctx *ctx,
			unsigned long *y_addr, unsigned long *c_addr);
	void (*try_run)(struct s5p_mfc_dev *dev);
	void (*clear_int_flags)(struct s5p_mfc_dev *dev);
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
	int (*get_img_width)(struct s5p_mfc_dev *dev);
	int (*get_img_height)(struct s5p_mfc_dev *dev);
	int (*get_dpb_count)(struct s5p_mfc_dev *dev);
	int (*get_mv_count)(struct s5p_mfc_dev *dev);
	int (*get_inst_no)(struct s5p_mfc_dev *dev);
	int (*get_enc_strm_size)(struct s5p_mfc_dev *dev);
	int (*get_enc_slice_type)(struct s5p_mfc_dev *dev);
	int (*get_enc_dpb_count)(struct s5p_mfc_dev *dev);
	unsigned int (*get_pic_type_top)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_pic_type_bot)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_crop_info_h)(struct s5p_mfc_ctx *ctx);
	unsigned int (*get_crop_info_v)(struct s5p_mfc_ctx *ctx);
	int (*get_min_scratch_buf_size)(struct s5p_mfc_dev *dev);
	int (*get_e_min_scratch_buf_size)(struct s5p_mfc_dev *dev);
};

void s5p_mfc_init_hw_ops(struct s5p_mfc_dev *dev);
void s5p_mfc_init_regs(struct s5p_mfc_dev *dev);
int s5p_mfc_alloc_priv_buf(struct s5p_mfc_dev *dev, unsigned int mem_ctx,
			   struct s5p_mfc_priv_buf *b);
void s5p_mfc_release_priv_buf(struct s5p_mfc_dev *dev,
			      struct s5p_mfc_priv_buf *b);
int s5p_mfc_alloc_generic_buf(struct s5p_mfc_dev *dev, unsigned int mem_ctx,
			   struct s5p_mfc_priv_buf *b);
void s5p_mfc_release_generic_buf(struct s5p_mfc_dev *dev,
			      struct s5p_mfc_priv_buf *b);


#endif /* S5P_MFC_OPR_H_ */
