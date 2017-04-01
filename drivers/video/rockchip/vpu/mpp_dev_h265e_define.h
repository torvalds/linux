 /*
  * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
  * author: hehua,hh@rock-chips.com
  * lixinhuang, buluess.li@rock-chips.com
  *
  * This software is licensed under the terms of the GNU General Public
  * License version 2, as published by the Free Software Foundation, and
  * may be copied, distributed, and modified under those terms.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  */
#ifndef __MPP_DEV_H265E_DEFINE_H__
#define __MPP_DEV_H265E_DEFINE_H__

#include <linux/bitops.h>

#define H265E_MVCOL_BUF_SIZE(w, h) \
	((((w) + 63) / 64) * (((h) + 63) / 64) * 128)
#define H265E_FBC_LUMA_TABLE_SIZE(w, h) \
	((((h) + 15) / 16) * (((w) + 255) / 256) * 128)
#define H265E_FBC_CHROMA_TABLE_SIZE(w, h) \
	((((h) + 15) / 16) * (((w) / 2 + 255) / 256) * 128)
#define H265E_SUBSAMPLED_ONE_SIZE(w, h) \
	(((((w) / 4) + 15) & ~15) * ((((h) / 4) + 7) & ~7))

#define H265E_PIC_TYPE_I 0
#define H265E_PIC_TYPE_P 1

enum H265E_VPU_COMMAND {
	H265E_CMD_INIT_VPU        = 0x0001,
	H265E_CMD_SET_PARAM       = 0x0002,
	H265E_CMD_FINI_SEQ        = 0x0004,
	H265E_CMD_ENC_PIC         = 0x0008,
	H265E_CMD_SET_FRAMEBUF    = 0x0010,
	H265E_CMD_FLUSH_DECODER   = 0x0020,
	H265E_CMD_GET_FW_VERSION  = 0x0100,
	H265E_CMD_QUERY_DECODER   = 0x0200,
	H265E_CMD_SLEEP_VPU       = 0x0400,
	H265E_CMD_WAKEUP_VPU      = 0x0800,
	H265E_CMD_CREATE_INSTANCE = 0x4000,
	H265E_CMD_RESET_VPU	  = 0x10000,
	H265E_CMD_MAX_VPU_COMD	  = 0x10000,
};

enum H265E_PIC_CODE_OPTION {
	CODEOPT_ENC_HEADER_IMPLICIT = BIT(0),
	CODEOPT_ENC_VCL             = BIT(1),
	CODEOPT_ENC_VPS             = BIT(2),
	CODEOPT_ENC_SPS             = BIT(3),
	CODEOPT_ENC_PPS             = BIT(4),
	CODEOPT_ENC_AUD             = BIT(5),
	CODEOPT_ENC_EOS             = BIT(6),
	CODEOPT_ENC_EOB             = BIT(7),
	CODEOPT_ENC_RESERVED        = BIT(8),
	CODEOPT_ENC_VUI             = BIT(9),
};

enum H265E_TILED_MAP_TYPE {
	LINEAR_FRAME_MAP            = 0,
	TILED_FRAME_V_MAP           = 1,
	TILED_FRAME_H_MAP           = 2,
	TILED_FIELD_V_MAP           = 3,
	TILED_MIXED_V_MAP           = 4,
	TILED_FRAME_MB_RASTER_MAP   = 5,
	TILED_FIELD_MB_RASTER_MAP   = 6,
	TILED_FRAME_NO_BANK_MAP     = 7,
	TILED_FIELD_NO_BANK_MAP     = 8,
	LINEAR_FIELD_MAP            = 9,
	CODA_TILED_MAP_TYPE_MAX     = 10,
	COMPRESSED_FRAME_MAP        = 10,
	TILED_SUB_CTU_MAP           = 11,
	ARM_COMPRESSED_FRAME_MAP      = 12,
};

#define H265E_MAX_NUM_TEMPORAL_LAYER          7
#define H265E_MAX_GOP_NUM                     8
#define H265E_MIN_PIC_WIDTH            256
#define H265E_MIN_PIC_HEIGHT           128
#define H265E_MAX_PIC_WIDTH            1920
#define H265E_MAX_PIC_HEIGHT           1080
#define MAX_ROI_NUMBER  64

enum H265E_GOP_PRESET_IDX {
	PRESET_IDX_CUSTOM_GOP       = 0,
	PRESET_IDX_ALL_I            = 1,
	PRESET_IDX_IPP              = 2,
	PRESET_IDX_IPPPP            = 6,
};

enum H265E_SET_PARAM_OPTION {
	H265E_OPT_COMMON          = 0,
	H265E_OPT_CUSTOM_GOP      = 1,
	H265E_OPT_CUSTOM_HEADER   = 2,
	H265E_OPT_VUI             = 3,
	H265E_OPT_ALL_PARAM       = 0xffffffff
};

enum H265E_PARAM_CHANEGED {
	H265E_PARAM_CHANEGED_COMMON          = 1,
	H265E_PARAM_CHANEGED_CUSTOM_GOP      = 2,
	H265E_PARAM_CHANEGED_VUI             = 4,
	H265E_PARAM_CHANEGED_REGISTER_BUFFER = 8,
};

enum H265E_COMON_CFG_MASK {
	/* COMMON parameters*/
	H265E_CFG_SEQ_SRC_SIZE_CHANGE             = BIT(0),
	H265E_CFG_SEQ_PARAM_CHANGE                = BIT(1),
	H265E_CFG_GOP_PARAM_CHANGE                = BIT(2),
	H265E_CFG_INTRA_PARAM_CHANGE              = BIT(3),
	H265E_CFG_CONF_WIN_TOP_BOT_CHANGE         = BIT(4),
	H265E_CFG_CONF_WIN_LEFT_RIGHT_CHANGE      = BIT(5),
	H265E_CFG_FRAME_RATE_CHANGE               = BIT(6),
	H265E_CFG_INDEPENDENT_SLICE_CHANGE        = BIT(7),
	H265E_CFG_DEPENDENT_SLICE_CHANGE          = BIT(8),
	H265E_CFG_INTRA_REFRESH_CHANGE            = BIT(9),
	H265E_CFG_PARAM_CHANGE                    = BIT(10),
	H265E_CFG_CHANGE_RESERVED                 = BIT(11),
	H265E_CFG_RC_PARAM_CHANGE                 = BIT(12),
	H265E_CFG_RC_MIN_MAX_QP_CHANGE            = BIT(13),
	H265E_CFG_RC_TARGET_RATE_LAYER_0_3_CHANGE = BIT(14),
	H265E_CFG_RC_TARGET_RATE_LAYER_4_7_CHANGE = BIT(15),

	H265E_CFG_SET_NUM_UNITS_IN_TICK		  = BIT(18),
	H265E_CFG_SET_TIME_SCALE		  = BIT(19),
	H265E_CFG_SET_NUM_TICKS_POC_DIFF_ONE	  = BIT(20),
	H265E_CFG_RC_TRANS_RATE_CHANGE            = BIT(21),
	H265E_CFG_RC_TARGET_RATE_CHANGE           = BIT(22),
	H265E_CFG_ROT_PARAM_CHANGE                = BIT(23),
	H265E_CFG_NR_PARAM_CHANGE                 = BIT(24),
	H265E_CFG_NR_WEIGHT_CHANGE                = BIT(25),

	H265E_CFG_SET_VCORE_LIMIT                 = BIT(27),
	H265E_CFG_CHANGE_SET_PARAM_ALL            = (0xFFFFFFFF),
};

/**
 * @brief    This is a data structure for setting
 * CTU level options (ROI, CTU mode, CTU QP) in HEVC encoder.
 */
struct h265e_ctu {
	u32 roi_enable;
	u32 roi_delta_qp;
	u32 map_endian;

	/*
	 * Stride of CTU-level ROI/mode/QP map
	 * Set this with (Width  + CTB_SIZE - 1) / CTB_SIZE
	 */
	u32 map_stride;
	/*
	 * It enables CTU QP map that allows
	 * CTUs to be encoded with the given QPs.
	 * NOTE: rcEnable should be turned off for this,
	 * encoding with the given CTU QPs.
	 */
	u32 ctu_qp_enable;
};

struct h265e_sei {
	u8 prefix_sei_nal_enable;

	/*
	 * A flag whether to encode PREFIX_SEI_DATA
	 * with a picture of this command or with a source
	 * picture of the buffer at the moment
	 * 0 : encode PREFIX_SEI_DATA when a source picture is encoded.
	 * 1 : encode PREFIX_SEI_DATA at this command.
	 */
	u8 prefix_sei_data_order;

	/*
	 * enables to encode the suffix SEI NAL which is given by host.
	 */
	u8 suffix_sei_nal_enable;

	/*
	 * A flag whether to encode SUFFIX_SEI_DATA
	 * with a picture of this command or with a source
	 * picture of the buffer at the moment
	 * 0 : encode SUFFIX_SEI_DATA when a source picture is encoded.
	 * 1 : encode SUFFIX_SEI_DATA at this command.
	 */
	u8 suffix_sei_data_enc_order;

	/*
	 * The total byte size of the prefix SEI
	 */
	u32 prefix_sei_data_size;

	/*
	 * The start address of the total prefix SEI NALs to be encoded
	 */
	u32 prefix_sei_nal_addr;

	/*
	 * The total byte size of the suffix SEI
	 */
	u32 suffix_sei_data_size;

	/*
	 * The start address of the total suffix SEI NALs to be encoded
	 */
	u32 suffix_sei_nal_addr;
};

/**
 * @brief    This is a data structure for setting
 * VUI parameters in HEVC encoder.
 */
struct h265e_vui {
	/*
	 * VUI parameter flag
	 */
	u32 flags;
	/**< aspect_ratio_idc */
	u32 aspect_ratio_idc;
	/**< sar_width, sar_height
	 * (only valid when aspect_ratio_idc is equal to 255)
	 */
	u32 sar_size;
	/**< overscan_appropriate_flag */
	u32 over_scan_appropriate;
	/**< VUI parameter flag */
	u32 signal;
	/**< chroma_sample_loc_type_top_field,
	 *chroma_sample_loc_type_bottom_field
	 */
	u32 chroma_sample_loc;
	/**< def_disp_win_left_offset, def_disp_win_right_offset */
	u32 disp_win_left_right;
	/**< def_disp_win_top_offset, def_disp_win_bottom_offset */
	u32 disp_win_top_bottom;
};

/**
 * @brief    This is a data structure for
 *custom GOP parameters of the given picture.
 */
struct h265e_custom_gop_pic {
	/**< A picture type of #th picture in the custom GOP */
	u32 type;
	/**< A POC offset of #th picture in the custom GOP */
	u32 offset;
	/**< A quantization parameter of #th picture in the custom GOP */
	u32 qp;
	/**< POC offset of reference L0 of #th picture in the custom GOP */
	u32 ref_poc_l0;
	/**< POC offset of reference L1 of #th picture in the custom GOP */
	u32 ref_poc_l1;
	/**< A temporal ID of #th picture in the custom GOP */
	u32 temporal_id;
};

/**
 * @brief    This is a data structure for custom GOP parameters.
 */
struct h265e_custom_gop {
	/**< Size of the custom GOP (0~8) */
	u32 custom_gop_size;
	/**< It derives a lamda weight internally
	 * instead of using lamda weight specified.
	 */
	u32 use_derive_lambda_weight;
	/**< picture parameters of #th picture in the custom gop */
	struct h265e_custom_gop_pic pic[H265E_MAX_GOP_NUM];
	/**< a lamda weight of #th picture in the custom gop */
	u32 gop_pic_lambda[H265E_MAX_GOP_NUM];
};

struct enc_code_opt {
	/**< whether host encode a header implicitly or not.
	 * if this value is 1, below encode options will be ignored
	 */
	int implicit_header_encode;
	int encode_vcl;/**< a flag to encode vcl nal unit explicitly*/
	int encode_vps;/**< a flag to encode vps nal unit explicitly*/
	int encode_sps;/**< a flag to encode sps nal unit explicitly*/
	int encode_pps;/**< a flag to encode pps nal unit explicitly*/
	int encode_aud;/**< a flag to encode aud nal unit explicitly*/
	int encode_eos;/**< a flag to encode eos nal unit explicitly*/
	int encode_eob;/**< a flag to encode eob nal unit explicitly*/
	int encode_vui;/**< a flag to encode vui nal unit explicitly*/
};

enum H265E_SRC_FORMAT {
	H265E_SRC_YUV_420 = 0,
	H265E_SRC_YUV_420_YU12 = 0, /*  3Plane 1.Y, 2.U, 3.V*/
	H265E_SRC_YUV_420_YV12, /*  3Plane 1.Y, 2.V, 3.U*/
	H265E_SRC_YUV_420_NV12, /* 2 Plane 1.Y 2. UV*/
	H265E_SRC_YUV_420_NV21, /* 2 Plane 1.Y 2. VU*/
	H265E_SRC_YUV_420_MAX,
};

struct hal_h265e_header {
	u32         buf;
	u32         size;
};

struct mpp_h265e_cfg {
	/*
	 * A profile indicator
	 * 1 : main
	 * 2 : main10
	 */
	u8 profile;

	/*
	 * only support to level 4.1
	 */
	u8 level; /**< A level indicator (level * 10) */

	/*
	 * A tier indicator
	 * 0 : main
	 * 1 : high
	 */
	u8 tier;

	/*
	 * A chroma format indecator, only support YUV420
	 */
	u8 chroma_idc;

	/*
	 * the source's width and height
	 */
	u16 width;
	u16 height;
	u16 width_stride;
	u16 height_stride;

	/*
	 * bitdepth,only support 8 bits(only support 8 bits)
	 */
	u8 bit_depth;

	/*
	 * source yuv's format. The value is defined
	 * in H265E_FrameBufferFormat(only support YUV420)
	 * the value could be YU12,YV12,NV12,NV21
	 */
	u8 src_format;

	u8 src_endian;
	u8 bs_endian;
	u8 fb_endian;
	u8 frame_rate;
	u8 frame_skip;
	u32 bit_rate;

	u32 map_type;
	u32 line_buf_int_en;
	u32 slice_int_enable;
	u32 ring_buffer_enable;

	struct enc_code_opt code_option;
	/*
	 * A chroma format indecator, only support YUV420
	 */
	int lossless_enable;/**< It enables lossless coding */
	/**< It enables constrained intra prediction */
	int const_intra_pred_flag;
	/**< The value of chroma(cb) qp offset (only for WAVE420L) */
	int chroma_cb_qp_offset;
	/**< The value of chroma(cr) qp offset  (only for WAVE420L) */
	int chroma_cr_qp_offset;
	/**
	 * A GOP structure option
	 * 0: Custom GOP
	 * 1 : I-I-I-I,..I (all intra, gop_size=1)
	 * 2 : I-P-P-P,... P (consecutive P, gop_size=1)
	 * 6 : I-P-P-P-P (consecutive P, gop_size=4)
	 */
	u32 gop_idx;

	/**
	 * An intra picture refresh mode
	 * 0 : Non-IRAP
	 * 1 : CRA
	 * 2 : IDR
	 */
	u32 decoding_refresh_type;

	/*
	 * A quantization parameter of intra picture
	 */
	u32 intra_qp;

	/*
	 * A period of intra picture in GOP size
	 */
	u32 intra_period;

	/** A conformance window size of TOP,BUTTOM,LEFT,RIGHT */
	u16 conf_win_top;
	u16 conf_win_bot;
	u16 conf_win_left;
	u16 conf_win_right;

	/*
	 * A slice mode for independent slice
	 * 0 : no multi-slice
	 * 1 : Slice in CTU number
	 * 2 : Slice in number of byte
	 */
	u32 independ_slice_mode;

	/*
	 * The number of CTU or bytes for a slice
	 * when independ_slice_mode is set with 1 or 2.
	 */
	u32 independ_slice_mode_arg;

	/**
	 *A slice mode for dependent slice
	 * 0 : no multi-slice
	 * 1 : Slice in CTU number
	 * 2 : Slice in number of byte
	 */
	u32 depend_slice_mode;

	/*
	 * The number of CTU or bytes for a slice
	 * when depend_slice_mode is set with 1 or 2.
	 */
	u32 depend_slice_mode_arg;

	/*
	 * An intra refresh mode
	 * 0 : No intra refresh
	 * 1 : Row
	 * 2 : Column
	 * 3 : Step size in CTU
	 */
	u32 intra_refresh_mode;

	/*
	 * The number of CTU (only valid when intraRefreshMode is 3.)
	 */
	u32 intra_refresh_arg;

	/*
	 * It uses one of the recommended encoder parameter presets.
	 * 0 : Custom
	 * 1 : Recommend enc params
	 * (slow encoding speed, highest picture quality)
	 * 2 : Boost mode (normal encoding speed, normal picture quality)
	 * 3 : Fast mode (high encoding speed, low picture quality)
	 */
	u8 use_recommend_param;
	u8 scaling_list_enable; /**< It enables a scaling list */

	/*
	 * It specifies CU size.
	 * 3'b001: 8x8
	 * 3'b010: 16x16
	 * 3'b100 : 32x32
	 */
	u8 cu_size_mode;
	u8 tmvp_enable;
	u8 wpp_enable; /**< It enables wave-front parallel processing. */
	u8 max_num_merge; /**< Maximum number of merge candidates (0~2) */
	u8 dynamic_merge_8x8_enable;
	u8 dynamic_merge_16x16_enable;
	u8 dynamic_merge_32x32_enable;
	u8 disable_deblk; /**< It disables in-loop deblocking filtering. */
	/**< it enables filtering across slice
	 * boundaries for in-loop deblocking.
	 */
	u8 lf_cross_slice_boundary_enable;
	/**< BetaOffsetDiv2 for deblocking filter */
	u8 beta_offset_div2;
	/**< TcOffsetDiv3 for deblocking filter */
	u8 tc_offset_div2;
	/**< It enables transform skip for an intra CU. */
	u8 skip_intra_trans;
	/**< It enables SAO (sample adaptive offset). */
	u8 sao_enable;
	/**< It enables to make intra CUs in an inter slice. */
	u8 intra_in_inter_slice_enable;
	/**< It enables intra NxN PUs. */
	u8 intra_nxn_enable;

	/*
	 * specifies intra QP offset relative
	 * to inter QP (Only available when rc_enable is enabled)
	 */
	s8 intra_qp_offset;

	/*
	 * It specifies encoder initial delay,
	 * Only available when RateControl is enabled
	 * (encoder initial delay = initial_delay * init_buf_levelx8 / 8)
	 */
	int init_buf_levelx8;

	/*
	 * specifies picture bits allocation mode.
	 * Only available when RateControl is enabled
	 * and GOP size is larger than 1
	 * 0: More referenced pictures have
	 * better quality than less referenced pictures
	 * 1: All pictures in a GOP have similar image quality
	 * 2: Each picture bits in a GOP is allocated according to FixedRatioN
	 */
	u8 bit_alloc_mode;

	/*
	 * A fixed bit ratio (1 ~ 255) for each picture of GOP's bitallocation
	 * N = 0 ~ (MAX_GOP_SIZE - 1)
	 * MAX_GOP_SIZE = 8
	 * For instance when MAX_GOP_SIZE is 3, FixedBitRaio0
	 * to FixedBitRaio2 can be set as 2, 1, and 1 respectively for
	 * the fixed bit ratio 2:1:1. This is only valid when BitAllocMode is 2.
	 */
	u8 fixed_bit_ratio[H265E_MAX_GOP_NUM];

	/*
	 * enable rate control
	 */
	u32 rc_enable;

	/*
	 * enable CU level rate control
	 */
	u8 cu_level_rc_enable;

	/*
	 * enable CU QP adjustment for subjective quality enhancement
	 */
	u8 hvs_qp_enable;

	/*
	 * enable QP scaling factor for CU QP adjustment when hvs_qp_enable = 1
	 */
	u8 hvs_qp_scale_enable;

	/*
	 * A QP scaling factor for CU QP adjustment when hvs_qp_enable = 1
	 */
	s8 hvs_qp_scale;

	/*
	 * A minimum QP for rate control
	 */
	u8 min_qp;

	/*
	 * A maximum QP for rate control
	 */
	u8 max_qp;

	/*
	 * A maximum delta QP for rate control
	 */
	u8 max_delta_qp;

	/*
	 * A peak transmission bitrate in bps
	 */
	u32 trans_rate;
	/*< It specifies the number of time units of
	 * a clock operating at the frequency time_scale Hz
	 */
	u32 num_units_in_tick;
	/**< It specifies the number of time units that pass in one second */
	u32 time_scale;
	/**< It specifies the number of clock ticks corresponding to a
	 * difference of picture order count values equal to 1
	 */
	u32 num_ticks_poc_diff_one;

	/*< The value of initial QP by host.
	 * This value is meaningless if INITIAL_RC_QP == 63
	 */
	int initial_rc_qp;

	/*
	 * enables noise reduction algorithm to Y/Cb/Cr component.
	 */
	u8 nr_y_enable;
	u8 nr_cb_enable;
	u8 nr_cr_enable;

	/*
	 * enables noise estimation for reduction. When this is disabled,
	 * noise estimation is carried out ouside VPU.
	 */
	u8 nr_noise_est_enable;
	/*
	 * It specifies Y/Cb/Cr noise standard deviation
	 * if no use of noise estimation (nr_noise_est_enable=0)
	 */
	u8 nr_noise_sigma_y;
	u8 nr_noise_sigma_cb;
	u8 nr_noise_sigma_cr;
	/* ENC_NR_WEIGHT*/
	/*< A weight to Y noise level for intra picture (0 ~ 31).
	 * nr_intra_weight_y/4 is multiplied to the noise
	 * level that has been estimated.
	 * This weight is put for intra frame to be filtered more strongly or
	 * more weakly than just with the estimated noise level.
	 */
	u8 nr_intra_weight_y;
	/**< A weight to Cb noise level for intra picture (0 ~ 31). */
	u8 nr_intra_weight_cb;
	/**< A weight to Cr noise level for intra picture (0 ~ 31). */
	u8 nr_intra_weight_cr;
	/*< A weight to Y noise level for inter picture (0 ~ 31).
	 * nr_inter_weight_y/4 is multiplied to the noise
	 * level that has been estimated.
	 * This weight is put for inter frame to be filtered more strongly or
	 * more weakly than just with the estimated noise level.
	 */
	u8 nr_inter_weight_y;
	/**< A weight to Cb noise level for inter picture (0 ~ 31). */
	u8 nr_inter_weight_cb;
	/**< A weight to Cr noise level for inter picture (0 ~ 31). */
	u8 nr_inter_weight_cr;
	/*
	 * a minimum QP for intra picture (0 ~ 51).
	 * It is only available when rc_enable is 1.
	 */
	u8 intra_min_qp;

	/*
	 * a maximum QP for intra picture (0 ~ 51).
	 * It is only available when rc_enable is 1.
	 */
	u8 intra_max_qp;

	u32 initial_delay;

	u8 hrd_rbsp_in_vps;
	u8 hrd_rbsp_in_vui;
	u32 vui_rbsp;

	u32 hrd_rbsp_data_size; /**< The size of the HRD rbsp data */
	u32 hrd_rbsp_data_addr;  /**< The address of the HRD rbsp data */

	u32 vui_rbsp_data_size;   /**< The size of the VUI rbsp data */
	u32 vui_rbsp_data_addr;   /**< The address of the VUI rbsp data */

	u8 use_long_term;
	u8 use_cur_as_longterm_pic;
	u8 use_longterm_ref;

	struct h265e_custom_gop gop;
	struct h265e_ctu ctu;
	struct h265e_vui vui;
	struct h265e_sei sei;

	/*
	 * define which type of parameters are changed,
	 * only support common parameter chanegd now,
	 * see H265eCommonCfgMask
	 */
	u32 cfg_option;

	/*
	 * define which parameters are changed,see H265E_SET_PARAM_OPTION
	 */
	u32 cfg_mask;
};

struct mpp_h265e_encode_info {
	/*
	 * the address of source(yuv) data for encoding
	 */
	u32 src_fd;

	/*
	 * the size of source(yuv) data for encoding
	 */
	u32 src_size;

	/*
	 * the address of bitstream buffer
	 */
	u32 bs_fd;

	/*
	 * the size of bitstream buffer
	 */
	u32 bs_size;
	u32 roi_fd;
	u32 ctu_qp_fd;
	u32 stream_end;

	/*
	 * skip current frame
	 */
	u32 skip_pic;

	/*
	 * A flag to use a force picture quantization parameter
	 */
	u32 force_qp_enable;

	/*
	 *Force picture quantization parameter for I picture
	 */
	u32 force_qp_i;

	/*
	 * Force picture quantization parameter for P picture
	 */
	u32 force_qp_p;

	/*
	 * A flag to use a force picture type
	 */
	u32 force_frame_type_enable;

	/*
	 * A force picture type (I, P, B, IDR, CRA)
	 */
	u32 force_frame_type;
};

enum INTERRUPT_BIT {
	INT_BIT_INIT            = 0,
	INT_BIT_SEQ_INIT        = 1,
	INT_BIT_SEQ_END         = 2,
	INT_BIT_PIC_RUN         = 3,
	INT_BIT_FRAMEBUF_SET    = 4,
	INT_BIT_ENC_HEADER      = 5,
	INT_BIT_DEC_PARA_SET    = 7,
	INT_BIT_DEC_BUF_FLUSH   = 8,
	INT_BIT_USERDATA        = 9,
	INT_BIT_DEC_FIELD       = 10,
	INT_BIT_DEC_MB_ROWS     = 13,
	INT_BIT_BIT_BUF_EMPTY   = 14,
	INT_BIT_BIT_BUF_FULL    = 15
};

enum H265E_INTERRUPT_BIT {
	INT_H265E_INIT            = 0,
	INT_H265E_DEC_PIC_HDR     = 1,
	INT_H265E_FINI_SEQ        = 2,
	INT_H265E_ENC_PIC         = 3,
	INT_H265E_SET_FRAMEBUF    = 4,
	INT_H265E_FLUSH_DECODER   = 5,
	INT_H265E_GET_FW_VERSION  = 8,
	INT_H265E_QUERY_DECODER   = 9,
	INT_H265E_SLEEP_VPU       = 10,
	INT_H265E_WAKEUP_VPU      = 11,
	INT_H265E_CHANGE_INST     = 12,
	INT_H265E_CREATE_INSTANCE = 14,
	INT_H265E_BIT_BUF_EMPTY   = 15,
	INT_H265E_BIT_BUF_FULL    = 15,   /* Encoder */
};

#endif
