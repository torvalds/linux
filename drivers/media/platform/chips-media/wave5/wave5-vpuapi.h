/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - helper definitions
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#ifndef VPUAPI_H_INCLUDED
#define VPUAPI_H_INCLUDED

#include <linux/idr.h>
#include <linux/genalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>
#include "wave5-vpuerror.h"
#include "wave5-vpuconfig.h"
#include "wave5-vdi.h"

enum product_id {
	PRODUCT_ID_515,
	PRODUCT_ID_521,
	PRODUCT_ID_511,
	PRODUCT_ID_517,
	PRODUCT_ID_NONE,
};

struct vpu_attr;

enum vpu_instance_type {
	VPU_INST_TYPE_DEC = 0,
	VPU_INST_TYPE_ENC = 1
};

enum vpu_instance_state {
	VPU_INST_STATE_NONE = 0,
	VPU_INST_STATE_OPEN = 1,
	VPU_INST_STATE_INIT_SEQ = 2,
	VPU_INST_STATE_PIC_RUN = 3,
	VPU_INST_STATE_STOP = 4
};

/* Maximum available on hardware. */
#define WAVE5_MAX_FBS 32

#define MAX_REG_FRAME (WAVE5_MAX_FBS * 2)

#define WAVE5_DEC_HEVC_BUF_SIZE(_w, _h) (DIV_ROUND_UP(_w, 64) * DIV_ROUND_UP(_h, 64) * 256 + 64)
#define WAVE5_DEC_AVC_BUF_SIZE(_w, _h) ((((ALIGN(_w, 256) / 16) * (ALIGN(_h, 16) / 16)) + 16) * 80)

#define WAVE5_FBC_LUMA_TABLE_SIZE(_w, _h) (ALIGN(_h, 64) * ALIGN(_w, 256) / 32)
#define WAVE5_FBC_CHROMA_TABLE_SIZE(_w, _h) (ALIGN((_h), 64) * ALIGN((_w) / 2, 256) / 32)
#define WAVE5_ENC_AVC_BUF_SIZE(_w, _h) (ALIGN(_w, 64) * ALIGN(_h, 64) / 32)
#define WAVE5_ENC_HEVC_BUF_SIZE(_w, _h) (ALIGN(_w, 64) / 64 * ALIGN(_h, 64) / 64 * 128)

/*
 * common struct and definition
 */
enum cod_std {
	STD_AVC = 0,
	STD_HEVC = 12,
	STD_MAX
};

enum wave_std {
	W_HEVC_DEC = 0x00,
	W_HEVC_ENC = 0x01,
	W_AVC_DEC = 0x02,
	W_AVC_ENC = 0x03,
	STD_UNKNOWN = 0xFF
};

enum set_param_option {
	OPT_COMMON = 0, /* SET_PARAM command option for encoding sequence */
	OPT_CUSTOM_GOP = 1, /* SET_PARAM command option for setting custom GOP */
	OPT_CUSTOM_HEADER = 2, /* SET_PARAM command option for setting custom VPS/SPS/PPS */
	OPT_VUI = 3, /* SET_PARAM command option for encoding VUI */
	OPT_CHANGE_PARAM = 0x10,
};

/************************************************************************/
/* PROFILE & LEVEL */
/************************************************************************/
/* HEVC */
#define HEVC_PROFILE_MAIN 1
#define HEVC_PROFILE_MAIN10 2
#define HEVC_PROFILE_STILLPICTURE 3
#define HEVC_PROFILE_MAIN10_STILLPICTURE 2

/* H.264 profile for encoder*/
#define H264_PROFILE_BP 1
#define H264_PROFILE_MP 2
#define H264_PROFILE_EXTENDED 3
#define H264_PROFILE_HP 4
#define H264_PROFILE_HIGH10 5
#define H264_PROFILE_HIGH422 6
#define H264_PROFILE_HIGH444 7

/************************************************************************/
/* error codes */
/************************************************************************/

/************************************************************************/
/* utility macros */
/************************************************************************/

/* Initialize sequence firmware command mode */
#define INIT_SEQ_NORMAL				1

/* Decode firmware command mode */
#define DEC_PIC_NORMAL				0

/* bit_alloc_mode */
#define BIT_ALLOC_MODE_FIXED_RATIO		2

/* bit_rate */
#define MAX_BIT_RATE				700000000

/* decoding_refresh_type */
#define DEC_REFRESH_TYPE_NON_IRAP		0
#define DEC_REFRESH_TYPE_CRA			1
#define DEC_REFRESH_TYPE_IDR			2

/* depend_slice_mode */
#define DEPEND_SLICE_MODE_RECOMMENDED		1
#define DEPEND_SLICE_MODE_BOOST			2
#define DEPEND_SLICE_MODE_FAST			3

/* hvs_max_delta_qp */
#define MAX_HVS_MAX_DELTA_QP			51

/* intra_refresh_mode */
#define REFRESH_MODE_CTU_ROWS			1
#define REFRESH_MODE_CTU_COLUMNS		2
#define REFRESH_MODE_CTU_STEP_SIZE		3
#define REFRESH_MODE_CTUS			4

/* intra_mb_refresh_mode */
#define REFRESH_MB_MODE_NONE			0
#define REFRESH_MB_MODE_CTU_ROWS		1
#define REFRESH_MB_MODE_CTU_COLUMNS		2
#define REFRESH_MB_MODE_CTU_STEP_SIZE		3

/* intra_qp */
#define MAX_INTRA_QP				63

/* nr_inter_weight_* */
#define MAX_INTER_WEIGHT			31

/* nr_intra_weight_* */
#define MAX_INTRA_WEIGHT			31

/* nr_noise_sigma_* */
#define MAX_NOISE_SIGMA				255

/* bitstream_buffer_size */
#define MIN_BITSTREAM_BUFFER_SIZE		1024
#define MIN_BITSTREAM_BUFFER_SIZE_WAVE521	(1024 * 64)

/* vbv_buffer_size */
#define MIN_VBV_BUFFER_SIZE			10
#define MAX_VBV_BUFFER_SIZE			3000

#define BUFFER_MARGIN				4096

#define MAX_FIRMWARE_CALL_RETRY			10

#define VDI_LITTLE_ENDIAN	0x0

/*
 * Parameters of DEC_SET_SEQ_CHANGE_MASK
 */
#define SEQ_CHANGE_ENABLE_PROFILE BIT(5)
#define SEQ_CHANGE_ENABLE_SIZE BIT(16)
#define SEQ_CHANGE_ENABLE_BITDEPTH BIT(18)
#define SEQ_CHANGE_ENABLE_DPB_COUNT BIT(19)
#define SEQ_CHANGE_ENABLE_ASPECT_RATIO BIT(21)
#define SEQ_CHANGE_ENABLE_VIDEO_SIGNAL BIT(23)
#define SEQ_CHANGE_ENABLE_VUI_TIMING_INFO BIT(29)

#define SEQ_CHANGE_ENABLE_ALL_HEVC (SEQ_CHANGE_ENABLE_PROFILE | \
		SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

#define SEQ_CHANGE_ENABLE_ALL_AVC (SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT | \
		SEQ_CHANGE_ENABLE_ASPECT_RATIO | \
		SEQ_CHANGE_ENABLE_VIDEO_SIGNAL | \
		SEQ_CHANGE_ENABLE_VUI_TIMING_INFO)

#define DISPLAY_IDX_FLAG_SEQ_END -1
#define DISPLAY_IDX_FLAG_NO_FB -3
#define DECODED_IDX_FLAG_NO_FB -1
#define DECODED_IDX_FLAG_SKIP -2

#define RECON_IDX_FLAG_ENC_END -1
#define RECON_IDX_FLAG_ENC_DELAY -2
#define RECON_IDX_FLAG_HEADER_ONLY -3
#define RECON_IDX_FLAG_CHANGE_PARAM -4

enum codec_command {
	ENABLE_ROTATION,
	ENABLE_MIRRORING,
	SET_MIRROR_DIRECTION,
	SET_ROTATION_ANGLE,
	DEC_GET_QUEUE_STATUS,
	ENC_GET_QUEUE_STATUS,
	DEC_RESET_FRAMEBUF_INFO,
	DEC_GET_SEQ_INFO,
};

enum mirror_direction {
	MIRDIR_NONE, /* no mirroring */
	MIRDIR_VER, /* vertical mirroring */
	MIRDIR_HOR, /* horizontal mirroring */
	MIRDIR_HOR_VER /* horizontal and vertical mirroring */
};

enum frame_buffer_format {
	FORMAT_ERR = -1,
	FORMAT_420 = 0, /* 8bit */
	FORMAT_422, /* 8bit */
	FORMAT_224, /* 8bit */
	FORMAT_444, /* 8bit */
	FORMAT_400, /* 8bit */

	/* little endian perspective */
	/* | addr 0 | addr 1 | */
	FORMAT_420_P10_16BIT_MSB = 5, /* lsb |000000xx|xxxxxxxx | msb */
	FORMAT_420_P10_16BIT_LSB, /* lsb |xxxxxxx |xx000000 | msb */
	FORMAT_420_P10_32BIT_MSB, /* lsb |00xxxxxxxxxxxxxxxxxxxxxxxxxxx| msb */
	FORMAT_420_P10_32BIT_LSB, /* lsb |xxxxxxxxxxxxxxxxxxxxxxxxxxx00| msb */

	/* 4:2:2 packed format */
	/* little endian perspective */
	/* | addr 0 | addr 1 | */
	FORMAT_422_P10_16BIT_MSB, /* lsb |000000xx |xxxxxxxx | msb */
	FORMAT_422_P10_16BIT_LSB, /* lsb |xxxxxxxx |xx000000 | msb */
	FORMAT_422_P10_32BIT_MSB, /* lsb |00xxxxxxxxxxxxxxxxxxxxxxxxxxx| msb */
	FORMAT_422_P10_32BIT_LSB, /* lsb |xxxxxxxxxxxxxxxxxxxxxxxxxxx00| msb */

	FORMAT_YUYV, /* 8bit packed format : Y0U0Y1V0 Y2U1Y3V1 ... */
	FORMAT_YUYV_P10_16BIT_MSB,
	FORMAT_YUYV_P10_16BIT_LSB,
	FORMAT_YUYV_P10_32BIT_MSB,
	FORMAT_YUYV_P10_32BIT_LSB,

	FORMAT_YVYU, /* 8bit packed format : Y0V0Y1U0 Y2V1Y3U1 ... */
	FORMAT_YVYU_P10_16BIT_MSB,
	FORMAT_YVYU_P10_16BIT_LSB,
	FORMAT_YVYU_P10_32BIT_MSB,
	FORMAT_YVYU_P10_32BIT_LSB,

	FORMAT_UYVY, /* 8bit packed format : U0Y0V0Y1 U1Y2V1Y3 ... */
	FORMAT_UYVY_P10_16BIT_MSB,
	FORMAT_UYVY_P10_16BIT_LSB,
	FORMAT_UYVY_P10_32BIT_MSB,
	FORMAT_UYVY_P10_32BIT_LSB,

	FORMAT_VYUY, /* 8bit packed format : V0Y0U0Y1 V1Y2U1Y3 ... */
	FORMAT_VYUY_P10_16BIT_MSB,
	FORMAT_VYUY_P10_16BIT_LSB,
	FORMAT_VYUY_P10_32BIT_MSB,
	FORMAT_VYUY_P10_32BIT_LSB,

	FORMAT_MAX,
};

enum packed_format_num {
	NOT_PACKED = 0,
	PACKED_YUYV,
	PACKED_YVYU,
	PACKED_UYVY,
	PACKED_VYUY,
};

enum wave5_interrupt_bit {
	INT_WAVE5_INIT_VPU = 0,
	INT_WAVE5_WAKEUP_VPU = 1,
	INT_WAVE5_SLEEP_VPU = 2,
	INT_WAVE5_CREATE_INSTANCE = 3,
	INT_WAVE5_FLUSH_INSTANCE = 4,
	INT_WAVE5_DESTROY_INSTANCE = 5,
	INT_WAVE5_INIT_SEQ = 6,
	INT_WAVE5_SET_FRAMEBUF = 7,
	INT_WAVE5_DEC_PIC = 8,
	INT_WAVE5_ENC_PIC = 8,
	INT_WAVE5_ENC_SET_PARAM = 9,
	INT_WAVE5_DEC_QUERY = 14,
	INT_WAVE5_BSBUF_EMPTY = 15,
	INT_WAVE5_BSBUF_FULL = 15,
};

enum pic_type {
	PIC_TYPE_I = 0,
	PIC_TYPE_P = 1,
	PIC_TYPE_B = 2,
	PIC_TYPE_IDR = 5, /* H.264/H.265 IDR (Instantaneous Decoder Refresh) picture */
	PIC_TYPE_MAX /* no meaning */
};

enum sw_reset_mode {
	SW_RESET_SAFETY,
	SW_RESET_FORCE,
	SW_RESET_ON_BOOT
};

enum tiled_map_type {
	LINEAR_FRAME_MAP = 0, /* linear frame map type */
	COMPRESSED_FRAME_MAP = 17, /* compressed frame map type*/
};

enum temporal_id_mode {
	TEMPORAL_ID_MODE_ABSOLUTE,
	TEMPORAL_ID_MODE_RELATIVE,
};

struct vpu_attr {
	u32 product_id;
	char product_name[8]; /* product name in ascii code */
	u32 product_version;
	u32 fw_version;
	u32 customer_id;
	u32 support_decoders; /* bitmask */
	u32 support_encoders; /* bitmask */
	u32 support_backbone: 1;
	u32 support_avc10bit_enc: 1;
	u32 support_hevc10bit_enc: 1;
	u32 support_hevc10bit_dec: 1;
	u32 support_vcore_backbone: 1;
	u32 support_vcpu_backbone: 1;
};

struct frame_buffer {
	dma_addr_t buf_y;
	dma_addr_t buf_cb;
	dma_addr_t buf_cr;
	unsigned int buf_y_size;
	unsigned int buf_cb_size;
	unsigned int buf_cr_size;
	enum tiled_map_type map_type;
	unsigned int stride; /* horizontal stride for the given frame buffer */
	unsigned int width; /* width of the given frame buffer */
	unsigned int height; /* height of the given frame buffer */
	size_t size; /* size of the given frame buffer */
	unsigned int sequence_no;
	bool update_fb_info;
};

struct vpu_rect {
	unsigned int left; /* horizontal pixel offset from left edge */
	unsigned int top; /* vertical pixel offset from top edge */
	unsigned int right; /* horizontal pixel offset from right edge */
	unsigned int bottom; /* vertical pixel offset from bottom edge */
};

/*
 * decode struct and definition
 */

struct dec_open_param {
	dma_addr_t bitstream_buffer;
	size_t bitstream_buffer_size;
};

struct dec_initial_info {
	u32 pic_width;
	u32 pic_height;
	struct vpu_rect pic_crop_rect;
	u32 min_frame_buffer_count; /* between 1 to 16 */

	u32 profile;
	u32 luma_bitdepth; /* bit-depth of the luma sample */
	u32 chroma_bitdepth; /* bit-depth of the chroma sample */
	u32 seq_init_err_reason;
	dma_addr_t rd_ptr; /* read pointer of bitstream buffer */
	dma_addr_t wr_ptr; /* write pointer of bitstream buffer */
	u32 sequence_no;
	u32 vlc_buf_size;
	u32 param_buf_size;
};

struct dec_output_info {
	/**
	 * This is a frame buffer index for the picture to be displayed at the moment
	 * among frame buffers which are registered using vpu_dec_register_frame_buffer().
	 * Frame data that will be displayed is stored in the frame buffer with this index
	 * When there is no display delay, this index is always the equal to
	 * index_frame_decoded, however, if displaying is delayed (for display
	 * reordering in AVC or B-frames in VC1), this index might be different to
	 * index_frame_decoded. By checking this index, HOST applications can easily figure
	 * out whether sequence decoding has been finished or not.
	 *
	 * -3(0xFFFD) or -2(0xFFFE) : when a display output cannot be given due to picture
	 * reordering or skip option
	 * -1(0xFFFF) : when there is no more output for display at the end of sequence
	 * decoding
	 */
	s32 index_frame_display;
	/**
	 * This is the frame buffer index of the decoded picture among the frame buffers which were
	 * registered using vpu_dec_register_frame_buffer(). The currently decoded frame is stored
	 * into the frame buffer specified by this index.
	 *
	 * -2 : indicates that no decoded output is generated because decoder meets EOS
	 * (end of sequence) or skip
	 * -1 : indicates that the decoder fails to decode a picture because there is no available
	 * frame buffer
	 */
	s32 index_frame_decoded;
	s32 index_frame_decoded_for_tiled;
	u32 nal_type;
	unsigned int pic_type;
	struct vpu_rect rc_display;
	unsigned int disp_pic_width;
	unsigned int disp_pic_height;
	struct vpu_rect rc_decoded;
	u32 dec_pic_width;
	u32 dec_pic_height;
	s32 decoded_poc;
	int temporal_id; /* temporal ID of the picture */
	dma_addr_t rd_ptr; /* stream buffer read pointer for the current decoder instance */
	dma_addr_t wr_ptr; /* stream buffer write pointer for the current decoder instance */
	struct frame_buffer disp_frame;
	u32 frame_display_flag; /* it reports a frame buffer flag to be displayed */
	/**
	 * this variable reports that sequence has been changed while H.264/AVC stream decoding.
	 * if it is 1, HOST application can get the new sequence information by calling
	 * vpu_dec_get_initial_info() or wave5_vpu_dec_issue_seq_init().
	 *
	 * for H.265/HEVC decoder, each bit has a different meaning as follows.
	 *
	 * sequence_changed[5] : it indicates that the profile_idc has been changed
	 * sequence_changed[16] : it indicates that the resolution has been changed
	 * sequence_changed[19] : it indicates that the required number of frame buffer has
	 * been changed.
	 */
	unsigned int frame_cycle; /* reports the number of cycles for processing a frame */
	u32 sequence_no;

	u32 dec_host_cmd_tick; /* tick of DEC_PIC command for the picture */
	u32 dec_decode_end_tick; /* end tick of decoding slices of the picture */

	u32 sequence_changed;
};

struct queue_status_info {
	u32 instance_queue_count;
	u32 report_queue_count;
};

/*
 * encode struct and definition
 */

#define MAX_NUM_TEMPORAL_LAYER 7
#define MAX_NUM_SPATIAL_LAYER 3
#define MAX_GOP_NUM 8

struct custom_gop_pic_param {
	u32 pic_type; /* picture type of nth picture in the custom GOP */
	u32 poc_offset; /* POC of nth picture in the custom GOP */
	u32 pic_qp; /* quantization parameter of nth picture in the custom GOP */
	u32 use_multi_ref_p; /* use multiref pic for P picture. valid only if PIC_TYPE is P */
	u32 ref_poc_l0; /* POC of reference L0 of nth picture in the custom GOP */
	u32 ref_poc_l1; /* POC of reference L1 of nth picture in the custom GOP */
	s32 temporal_id; /* temporal ID of nth picture in the custom GOP */
};

struct enc_wave_param {
	/*
	 * profile indicator (HEVC only)
	 *
	 * 0 : the firmware determines a profile according to the internal_bit_depth
	 * 1 : main profile
	 * 2 : main10 profile
	 * 3 : main still picture profile
	 * In the AVC encoder, a profile cannot be set by the host application.
	 * The firmware decides it based on internal_bit_depth.
	 * profile = HIGH (bitdepth 8) profile = HIGH10 (bitdepth 10)
	 */
	u32 profile;
	u32 level; /* level indicator (level * 10) */
	u32 internal_bit_depth: 4; /* 8/10 */
	u32 gop_preset_idx: 4; /* 0 - 9 */
	u32 decoding_refresh_type: 2; /* 0=non-IRAP, 1=CRA, 2=IDR */
	u32 intra_qp; /* quantization parameter of intra picture */
	u32 intra_period; /* period of intra picture in GOP size */
	u32 conf_win_top; /* top offset of conformance window */
	u32 conf_win_bot; /* bottom offset of conformance window */
	u32 conf_win_left; /* left offset of conformance window */
	u32 conf_win_right; /* right offset of conformance window */
	u32 intra_refresh_mode: 3;
	/*
	 * Argument for intra_ctu_refresh_mode.
	 *
	 * Depending on intra_refresh_mode, it can mean one of the following:
	 * - intra_ctu_refresh_mode (1) -> number of consecutive CTU rows
	 * - intra_ctu_refresh_mode (2) -> the number of consecutive CTU columns
	 * - intra_ctu_refresh_mode (3) -> step size in CTU
	 * - intra_ctu_refresh_mode (4) -> number of intra ct_us to be encoded in a picture
	 */
	u32 intra_refresh_arg;
	/*
	 * 0 : custom setting
	 * 1 : recommended encoder parameters (slow encoding speed, highest picture quality)
	 * 2 : boost mode (normal encoding speed, moderate picture quality)
	 * 3 : fast mode (fast encoding speed, low picture quality)
	 */
	u32 depend_slice_mode : 2;
	u32 depend_slice_mode_arg;
	u32 independ_slice_mode : 1; /* 0=no-multi-slice, 1=slice-in-ctu-number*/
	u32 independ_slice_mode_arg;
	u32 max_num_merge: 2;
	s32 beta_offset_div2: 4; /* sets beta_offset_div2 for deblocking filter */
	s32 tc_offset_div2: 4; /* sets tc_offset_div3 for deblocking filter */
	u32 hvs_qp_scale: 4; /* QP scaling factor for CU QP adjust if hvs_qp_scale_enable is 1 */
	u32 hvs_max_delta_qp; /* maximum delta QP for HVS */
	s32 chroma_cb_qp_offset; /* the value of chroma(cb) QP offset */
	s32 chroma_cr_qp_offset; /* the value of chroma(cr) QP offset */
	s32 initial_rc_qp;
	u32 nr_intra_weight_y;
	u32 nr_intra_weight_cb; /* weight to cb noise level for intra picture (0 ~ 31) */
	u32 nr_intra_weight_cr; /* weight to cr noise level for intra picture (0 ~ 31) */
	u32 nr_inter_weight_y;
	u32 nr_inter_weight_cb; /* weight to cb noise level for inter picture (0 ~ 31) */
	u32 nr_inter_weight_cr; /* weight to cr noise level for inter picture (0 ~ 31) */
	u32 min_qp_i; /* minimum QP of I picture for rate control */
	u32 max_qp_i; /* maximum QP of I picture for rate control */
	u32 min_qp_p; /* minimum QP of P picture for rate control */
	u32 max_qp_p; /* maximum QP of P picture for rate control */
	u32 min_qp_b; /* minimum QP of B picture for rate control */
	u32 max_qp_b; /* maximum QP of B picture for rate control */
	u32 avc_idr_period; /* period of IDR picture (0 ~ 1024). 0 - implies an infinite period */
	u32 avc_slice_arg; /* the number of MB for a slice when avc_slice_mode is set with 1 */
	u32 intra_mb_refresh_mode: 2; /* 0=none, 1=row, 2=column, 3=step-size-in-mb */
	/**
	 * Argument for intra_mb_refresh_mode.
	 *
	 * intra_mb_refresh_mode (1) -> number of consecutive MB rows
	 * intra_mb_refresh_mode (2) ->the number of consecutive MB columns
	 * intra_mb_refresh_mode (3) -> step size in MB
	 */
	u32 intra_mb_refresh_arg;
	u32 rc_weight_param;
	u32 rc_weight_buf;

	/* flags */
	u32 en_still_picture: 1; /* still picture profile */
	u32 tier: 1; /* 0=main, 1=high */
	u32 avc_slice_mode: 1; /* 0=none, 1=slice-in-mb-number */
	u32 entropy_coding_mode: 1; /* 0=CAVLC, 1=CABAC */
	u32 lossless_enable: 1; /* enable lossless encoding */
	u32 const_intra_pred_flag: 1; /* enable constrained intra prediction */
	u32 tmvp_enable: 1; /* enable temporal motion vector prediction */
	u32 wpp_enable: 1;
	u32 disable_deblk: 1; /* disable in-loop deblocking filtering */
	u32 lf_cross_slice_boundary_enable: 1;
	u32 skip_intra_trans: 1;
	u32 sao_enable: 1; /* enable SAO (sample adaptive offset) */
	u32 intra_nx_n_enable: 1; /* enables intra nx_n p_us */
	u32 cu_level_rc_enable: 1; /* enable CU level rate control */
	u32 hvs_qp_enable: 1; /* enable CU QP adjustment for subjective quality enhancement */
	u32 strong_intra_smooth_enable: 1; /* enable strong intra smoothing */
	u32 rdo_skip: 1; /* skip RDO (rate distortion optimization) */
	u32 lambda_scaling_enable: 1; /* enable lambda scaling using custom GOP */
	u32 transform8x8_enable: 1; /* enable 8x8 intra prediction and 8x8 transform */
	u32 mb_level_rc_enable: 1; /* enable MB-level rate control */
};

struct enc_open_param {
	dma_addr_t bitstream_buffer;
	unsigned int bitstream_buffer_size;
	u32 pic_width; /* width of a picture to be encoded in unit of sample */
	u32 pic_height; /* height of a picture to be encoded in unit of sample */
	u32 frame_rate_info;/* desired fps */
	u32 vbv_buffer_size;
	u32 bit_rate; /* target bitrate in bps */
	struct enc_wave_param wave_param;
	enum packed_format_num packed_format; /* <<vpuapi_h_packed_format_num>> */
	enum frame_buffer_format src_format;
	bool line_buf_int_en;
	u32 rc_enable : 1; /* rate control */
};

struct enc_initial_info {
	u32 min_frame_buffer_count; /* minimum number of frame buffers */
	u32 min_src_frame_count; /* minimum number of source buffers */
	u32 seq_init_err_reason;
	u32 warn_info;
	u32 vlc_buf_size; /* size of task buffer */
	u32 param_buf_size; /* size of task buffer */
};

/*
 * Flags to encode NAL units explicitly
 */
struct enc_code_opt {
	u32 implicit_header_encode: 1;
	u32 encode_vcl: 1;
	u32 encode_vps: 1;
	u32 encode_sps: 1;
	u32 encode_pps: 1;
	u32 encode_aud: 1;
	u32 encode_eos: 1;
	u32 encode_eob: 1;
	u32 encode_vui: 1;
};

struct enc_param {
	struct frame_buffer *source_frame;
	u32 pic_stream_buffer_addr;
	u64 pic_stream_buffer_size;
	u32 src_idx; /* source frame buffer index */
	struct enc_code_opt code_option;
	u64 pts; /* presentation timestamp (PTS) of the input source */
	bool src_end_flag;
};

struct enc_output_info {
	u32 bitstream_buffer;
	u32 bitstream_size; /* byte size of encoded bitstream */
	u32 pic_type: 2; /* <<vpuapi_h_pic_type>> */
	s32 recon_frame_index;
	dma_addr_t rd_ptr;
	dma_addr_t wr_ptr;
	u32 enc_pic_byte; /* number of encoded picture bytes */
	s32 enc_src_idx; /* source buffer index of the currently encoded picture */
	u32 enc_vcl_nut;
	u32 error_reason; /* error reason of the currently encoded picture */
	u32 warn_info; /* warning information on the currently encoded picture */
	unsigned int frame_cycle; /* param for reporting the cycle number of encoding one frame*/
	u64 pts;
	u32 enc_host_cmd_tick; /* tick of ENC_PIC command for the picture */
	u32 enc_encode_end_tick; /* end tick of encoding slices of the picture */
};

enum enc_pic_code_option {
	CODEOPT_ENC_HEADER_IMPLICIT = BIT(0),
	CODEOPT_ENC_VCL = BIT(1), /* flag to encode VCL nal unit explicitly */
};

enum gop_preset_idx {
	PRESET_IDX_CUSTOM_GOP = 0, /* user defined GOP structure */
	PRESET_IDX_ALL_I = 1, /* all intra, gopsize = 1 */
	PRESET_IDX_IPP = 2, /* consecutive P, cyclic gopsize = 1 */
	PRESET_IDX_IBBB = 3, /* consecutive B, cyclic gopsize = 1 */
	PRESET_IDX_IBPBP = 4, /* gopsize = 2 */
	PRESET_IDX_IBBBP = 5, /* gopsize = 4 */
	PRESET_IDX_IPPPP = 6, /* consecutive P, cyclic gopsize = 4 */
	PRESET_IDX_IBBBB = 7, /* consecutive B, cyclic gopsize = 4 */
	PRESET_IDX_RA_IB = 8, /* random access, cyclic gopsize = 8 */
	PRESET_IDX_IPP_SINGLE = 9, /* consecutive P, cyclic gopsize = 1, with single ref */
};

struct sec_axi_info {
	u32 use_ip_enable;
	u32 use_bit_enable;
	u32 use_lf_row_enable: 1;
	u32 use_enc_rdo_enable: 1;
	u32 use_enc_lf_enable: 1;
};

struct dec_info {
	struct dec_open_param open_param;
	struct dec_initial_info initial_info;
	struct dec_initial_info new_seq_info; /* temporal new sequence information */
	u32 stream_wr_ptr;
	u32 stream_rd_ptr;
	u32 frame_display_flag;
	dma_addr_t stream_buf_start_addr;
	dma_addr_t stream_buf_end_addr;
	u32 stream_buf_size;
	struct vpu_buf vb_mv[MAX_REG_FRAME];
	struct vpu_buf vb_fbc_y_tbl[MAX_REG_FRAME];
	struct vpu_buf vb_fbc_c_tbl[MAX_REG_FRAME];
	unsigned int num_of_decoding_fbs: 7;
	unsigned int num_of_display_fbs: 7;
	unsigned int stride;
	struct sec_axi_info sec_axi_info;
	dma_addr_t user_data_buf_addr;
	u32 user_data_enable;
	u32 user_data_buf_size;
	struct vpu_buf vb_work;
	struct vpu_buf vb_task;
	struct dec_output_info dec_out_info[WAVE5_MAX_FBS];
	u32 seq_change_mask;
	enum temporal_id_mode temp_id_select_mode;
	u32 target_temp_id;
	u32 target_spatial_id;
	u32 instance_queue_count;
	u32 report_queue_count;
	u32 cycle_per_tick;
	u32 product_code;
	u32 vlc_buf_size;
	u32 param_buf_size;
	bool initial_info_obtained;
	bool reorder_enable;
	bool first_cycle_check;
	u32 stream_endflag: 1;
};

struct enc_info {
	struct enc_open_param open_param;
	struct enc_initial_info initial_info;
	u32 stream_rd_ptr;
	u32 stream_wr_ptr;
	dma_addr_t stream_buf_start_addr;
	dma_addr_t stream_buf_end_addr;
	u32 stream_buf_size;
	unsigned int num_frame_buffers;
	unsigned int stride;
	bool rotation_enable;
	bool mirror_enable;
	enum mirror_direction mirror_direction;
	unsigned int rotation_angle;
	bool initial_info_obtained;
	struct sec_axi_info sec_axi_info;
	bool line_buf_int_en;
	struct vpu_buf vb_work;
	struct vpu_buf vb_mv; /* col_mv buffer */
	struct vpu_buf vb_fbc_y_tbl; /* FBC luma table buffer */
	struct vpu_buf vb_fbc_c_tbl; /* FBC chroma table buffer */
	struct vpu_buf vb_sub_sam_buf; /* sub-sampled buffer for ME */
	struct vpu_buf vb_task;
	u64 cur_pts; /* current timestamp in 90_k_hz */
	u64 pts_map[32]; /* PTS mapped with source frame index */
	u32 instance_queue_count;
	u32 report_queue_count;
	bool first_cycle_check;
	u32 cycle_per_tick;
	u32 product_code;
	u32 vlc_buf_size;
	u32 param_buf_size;
};

struct vpu_device {
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *v4l2_m2m_dec_dev;
	struct v4l2_m2m_dev *v4l2_m2m_enc_dev;
	struct list_head instances;
	struct video_device *video_dev_dec;
	struct video_device *video_dev_enc;
	struct mutex dev_lock; /* lock for the src, dst v4l2 queues */
	struct mutex hw_lock; /* lock hw configurations */
	int irq;
	enum product_id product;
	struct vpu_attr attr;
	struct vpu_buf common_mem;
	u32 last_performance_cycles;
	u32 sram_size;
	struct gen_pool *sram_pool;
	struct vpu_buf sram_buf;
	void __iomem *vdb_register;
	u32 product_code;
	struct ida inst_ida;
	struct clk_bulk_data *clks;
	struct hrtimer hrtimer;
	struct kthread_work work;
	struct kthread_worker *worker;
	int vpu_poll_interval;
	int num_clks;
	struct reset_control *resets;
};

struct vpu_instance;

struct vpu_instance_ops {
	void (*finish_process)(struct vpu_instance *inst);
};

struct vpu_instance {
	struct list_head list;
	struct v4l2_fh v4l2_fh;
	struct v4l2_m2m_dev *v4l2_m2m_dev;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl;
	struct vpu_device *dev;
	struct completion irq_done;

	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;

	enum vpu_instance_state state;
	enum vpu_instance_type type;
	const struct vpu_instance_ops *ops;
	spinlock_t state_spinlock; /* This protects the instance state */

	enum wave_std std;
	s32 id;
	union {
		struct enc_info enc_info;
		struct dec_info dec_info;
	} *codec_info;
	struct frame_buffer frame_buf[MAX_REG_FRAME];
	struct vpu_buf frame_vbuf[MAX_REG_FRAME];
	u32 fbc_buf_count;
	u32 queued_src_buf_num;
	u32 queued_dst_buf_num;
	struct list_head avail_src_bufs;
	struct list_head avail_dst_bufs;
	struct v4l2_rect conf_win;
	u64 timestamp;
	enum frame_buffer_format output_format;
	bool cbcr_interleave;
	bool nv21;
	bool eos;
	struct vpu_buf bitstream_vbuf;
	dma_addr_t last_rd_ptr;
	size_t remaining_consumed_bytes;
	bool needs_reallocation;

	unsigned int min_src_buf_count;
	unsigned int rot_angle;
	unsigned int mirror_direction;
	unsigned int bit_depth;
	unsigned int frame_rate;
	unsigned int vbv_buf_size;
	unsigned int rc_mode;
	unsigned int rc_enable;
	unsigned int bit_rate;
	unsigned int encode_aud;
	struct enc_wave_param enc_param;
};

void wave5_vdi_write_register(struct vpu_device *vpu_dev, u32 addr, u32 data);
u32 wave5_vdi_read_register(struct vpu_device *vpu_dev, u32 addr);
int wave5_vdi_clear_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);
int wave5_vdi_allocate_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);
int wave5_vdi_allocate_array(struct vpu_device *vpu_dev, struct vpu_buf *array, unsigned int count,
			     size_t size);
int wave5_vdi_write_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb, size_t offset,
			   u8 *data, size_t len);
int wave5_vdi_free_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);
void wave5_vdi_allocate_sram(struct vpu_device *vpu_dev);
void wave5_vdi_free_sram(struct vpu_device *vpu_dev);

int wave5_vpu_init_with_bitcode(struct device *dev, u8 *bitcode, size_t size);
int wave5_vpu_flush_instance(struct vpu_instance *inst);
int wave5_vpu_get_version_info(struct device *dev, u32 *revision, unsigned int *product_id);
int wave5_vpu_dec_open(struct vpu_instance *inst, struct dec_open_param *open_param);
int wave5_vpu_dec_close(struct vpu_instance *inst, u32 *fail_res);
int wave5_vpu_dec_issue_seq_init(struct vpu_instance *inst);
int wave5_vpu_dec_complete_seq_init(struct vpu_instance *inst, struct dec_initial_info *info);
int wave5_vpu_dec_register_frame_buffer_ex(struct vpu_instance *inst, int num_of_decoding_fbs,
					   int num_of_display_fbs, int stride, int height);
int wave5_vpu_dec_start_one_frame(struct vpu_instance *inst, u32 *res_fail);
int wave5_vpu_dec_get_output_info(struct vpu_instance *inst, struct dec_output_info *info);
int wave5_vpu_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr, int update_wr_ptr);
dma_addr_t wave5_vpu_dec_get_rd_ptr(struct vpu_instance *inst);
int wave5_vpu_dec_reset_framebuffer(struct vpu_instance *inst, unsigned int index);
int wave5_vpu_dec_give_command(struct vpu_instance *inst, enum codec_command cmd, void *parameter);
int wave5_vpu_dec_get_bitstream_buffer(struct vpu_instance *inst, dma_addr_t *prd_ptr,
				       dma_addr_t *pwr_ptr, size_t *size);
int wave5_vpu_dec_update_bitstream_buffer(struct vpu_instance *inst, size_t size);
int wave5_vpu_dec_clr_disp_flag(struct vpu_instance *inst, int index);
int wave5_vpu_dec_set_disp_flag(struct vpu_instance *inst, int index);

int wave5_vpu_enc_open(struct vpu_instance *inst, struct enc_open_param *open_param);
int wave5_vpu_enc_close(struct vpu_instance *inst, u32 *fail_res);
int wave5_vpu_enc_issue_seq_init(struct vpu_instance *inst);
int wave5_vpu_enc_complete_seq_init(struct vpu_instance *inst, struct enc_initial_info *info);
int wave5_vpu_enc_register_frame_buffer(struct vpu_instance *inst, unsigned int num,
					unsigned int stride, int height,
					enum tiled_map_type map_type);
int wave5_vpu_enc_start_one_frame(struct vpu_instance *inst, struct enc_param *param,
				  u32 *fail_res);
int wave5_vpu_enc_get_output_info(struct vpu_instance *inst, struct enc_output_info *info);
int wave5_vpu_enc_give_command(struct vpu_instance *inst, enum codec_command cmd, void *parameter);

#endif
