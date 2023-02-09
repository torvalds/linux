/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - helper definitions
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */

#ifndef VPUAPI_H_INCLUDED
#define VPUAPI_H_INCLUDED

#include <linux/kfifo.h>
#include <linux/idr.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>
#include "wave5-vpuerror.h"
#include "wave5-vpuconfig.h"
#include "wave5-vdi.h"

enum product_id {
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

#define WAVE5_MAX_FBS 32

#define MAX_REG_FRAME (WAVE5_MAX_FBS * 2)

#define WAVE5_DEC_HEVC_BUF_SIZE(_w, _h) (DIV_ROUND_UP(_w, 64) * DIV_ROUND_UP(_h, 64) * 256 + 64)
#define WAVE5_DEC_AVC_BUF_SIZE(_w, _h) ((((ALIGN(_w, 256) / 16) * (ALIGN(_h, 16) / 16)) + 16) * 80)
#define WAVE5_DEC_VP9_BUF_SIZE(_w, _h) (((ALIGN(_w, 64) * ALIGN(_h, 64)) >> 2))
#define WAVE5_DEC_AVS2_BUF_SIZE(_w, _h) (((ALIGN(_w, 64) * ALIGN(_h, 64)) >> 5))
// AV1 BUF SIZE : MFMV + segment ID + CDF probs table + film grain param Y+ film graim param C
#define WAVE5_DEC_AV1_BUF_SZ_1(_w, _h)	\
	(((ALIGN(_w, 64) / 64) * (ALIGN(_h, 64) / 64) * 512) + 41984 + 8192 + 4864)
#define WAVE5_DEC_AV1_BUF_SZ_2(_w1, _w2, _h)	\
	(((ALIGN(_w1, 64) / 64) * 256 + (ALIGN(_w2, 256) / 64) * 128) * (ALIGN(_h, 64) / 64))

#define WAVE5_FBC_LUMA_TABLE_SIZE(_w, _h) (ALIGN(_h, 64) * ALIGN(_w, 256) / 32)
#define WAVE5_FBC_CHROMA_TABLE_SIZE(_w, _h) (ALIGN((_h), 64) * ALIGN((_w) / 2, 256) / 32)
#define WAVE5_ENC_AVC_BUF_SIZE(_w, _h) (ALIGN(_w, 64) * ALIGN(_h, 64) / 32)
#define WAVE5_ENC_HEVC_BUF_SIZE(_w, _h) (ALIGN(_w, 64) / 64 * ALIGN(_h, 64) / 64 * 128)

/*
 * common struct and definition
 */
enum cod_std {
	STD_AVC = 0,
	STD_VC1 = 1,
	STD_MPEG2 = 2,
	STD_MPEG4 = 3,
	STD_H263 = 4,
	STD_DIV3 = 5,
	STD_RV = 6,
	STD_AVS = 7,
	STD_THO = 9,
	STD_VP3 = 10,
	STD_VP8 = 11,
	STD_HEVC = 12,
	STD_VP9 = 13,
	STD_AVS2 = 14,
	STD_AV1 = 16,
	STD_MAX
};

enum wave_std {
	W_HEVC_DEC = 0x00,
	W_HEVC_ENC = 0x01,
	W_AVC_DEC = 0x02,
	W_AVC_ENC = 0x03,
	W_VP9_DEC = 0x16,
	W_AVS2_DEC = 0x18,
	W_AV1_DEC = 0x1A,
	STD_UNKNOWN = 0xFF
};

enum SET_PARAM_OPTION {
	OPT_COMMON = 0, /* SET_PARAM command option for encoding sequence */
	OPT_CUSTOM_GOP = 1, /* SET_PARAM command option for setting custom GOP */
	OPT_CUSTOM_HEADER = 2, /* SET_PARAM command option for setting custom VPS/SPS/PPS */
	OPT_VUI = 3, /* SET_PARAM command option for encoding VUI */
	OPT_CHANGE_PARAM = 0x10,
};

enum DEC_PIC_HDR_OPTION {
	INIT_SEQ_NORMAL = 0x01,
	INIT_SEQ_W_THUMBNAIL = 0x11,
};

enum DEC_PIC_OPTION {
	DEC_PIC_NORMAL = 0x00, /* it is normal mode of DEC_PIC command */
	DEC_PIC_W_THUMBNAIL = 0x10, /* thumbnail mode (skip non-IRAP without reference reg) */
	SKIP_NON_IRAP = 0x11, /* it skips to decode non-IRAP pictures */
	SKIP_NON_REF_PIC = 0x13
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

/* Bitstream buffer option: Explicit End
 * When set to 1 the VPU assumes that the bitstream has at least one frame and
 * will read until the end of the bitstream buffer.
 * When set to 0 the VPU will not read the last few bytes.
 * This option can be set anytime but cannot be cleared during processing.
 * It can be set to force finish decoding even though there is not enough
 * bitstream data for a full frame.
 */
#define BS_EXPLICIT_END_MODE_ON			1

#define BUFFER_MARGIN				4096

/************************************************************************/
/* */
/************************************************************************/
/**
 * \brief parameters of DEC_SET_SEQ_CHANGE_MASK
 */
#define SEQ_CHANGE_ENABLE_PROFILE BIT(5)
#define SEQ_CHANGE_CHROMA_FORMAT_IDC BIT(15) /* AV1 */
#define SEQ_CHANGE_ENABLE_SIZE BIT(16)
#define SEQ_CHANGE_INTER_RES_CHANGE BIT(17) /* VP9 */
#define SEQ_CHANGE_ENABLE_BITDEPTH BIT(18)
#define SEQ_CHANGE_ENABLE_DPB_COUNT BIT(19)

#define SEQ_CHANGE_ENABLE_ALL_VP9 (SEQ_CHANGE_ENABLE_PROFILE | \
		SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_INTER_RES_CHANGE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

#define SEQ_CHANGE_ENABLE_ALL_HEVC (SEQ_CHANGE_ENABLE_PROFILE | \
		SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

#define SEQ_CHANGE_ENABLE_ALL_AVS2 (SEQ_CHANGE_ENABLE_PROFILE | \
		SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

#define SEQ_CHANGE_ENABLE_ALL_AVC (SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

#define SEQ_CHANGE_ENABLE_ALL_AV1 (SEQ_CHANGE_ENABLE_PROFILE | \
		SEQ_CHANGE_CHROMA_FORMAT_IDC | \
		SEQ_CHANGE_ENABLE_SIZE | \
		SEQ_CHANGE_ENABLE_BITDEPTH | \
		SEQ_CHANGE_ENABLE_DPB_COUNT)

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
	ENABLE_DEC_THUMBNAIL_MODE,
	DEC_GET_QUEUE_STATUS,
	ENC_GET_QUEUE_STATUS,
	DEC_RESET_FRAMEBUF_INFO,
	DEC_GET_SEQ_INFO,
};

enum error_conceal_mode {
	ERROR_CONCEAL_MODE_OFF = 0, /* conceal off */
	ERROR_CONCEAL_MODE_INTRA_ONLY = 1, /* intra conceal in intra-picture, inter-picture */
	ERROR_CONCEAL_MODE_INTRA_INTER = 2
};

enum error_conceal_unit {
	ERROR_CONCEAL_UNIT_PICTURE = 0, /* picture-level error conceal */
	ERROR_CONCEAL_UNIT_SLICE_TILE = 1, /* slice/tile-level error conceal */
	ERROR_CONCEAL_UNIT_BLOCK_ROW = 2, /* block-row-level error conceal */
	ERROR_CONCEAL_UNIT_BLOCK = 3 /* block-level conceal */
};

enum cb_cr_order {
	CBCR_ORDER_NORMAL,
	CBCR_ORDER_REVERSED
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
	PIC_TYPE_I = 0, /* I picture */
	PIC_TYPE_KEY = 0, /* KEY frame for AV1*/
	PIC_TYPE_P = 1, /* P picture */
	PIC_TYPE_INTER = 1, /* inter frame for AV1*/
	PIC_TYPE_B = 2, /* B picture (except VC1) */
	PIC_TYPE_REPEAT = 2, /* repeat frame (VP9 only) */
	PIC_TYPE_AV1_INTRA = 2, /* intra only frame (AV1 only) */
	PIC_TYPE_VC1_BI = 2, /* VC1 BI picture (VC1 only) */
	PIC_TYPE_VC1_B = 3, /* VC1 B picture (VC1 only) */
	PIC_TYPE_D = 3,
	PIC_TYPE_S = 3,
	PIC_TYPE_AVS2_F = 3, /* F picture in AVS2 */
	PIC_TYPE_AV1_SWITCH = 3, /* switch frame (AV1 only) */
	PIC_TYPE_VC1_P_SKIP = 4, /* VC1 P skip picture (VC1 only) */
	PIC_TYPE_MP4_P_SKIP_NOT_CODED = 4, /* not coded P picture in MPEG4 packed mode */
	PIC_TYPE_AVS2_S = 4, /* S picture in AVS2 */
	PIC_TYPE_IDR = 5, /* H.264/H.265 IDR picture */
	PIC_TYPE_AVS2_G = 5, /* G picture in AVS2 */
	PIC_TYPE_AVS2_GB = 6, /* GB picture in AVS2 */
	PIC_TYPE_MAX /* no meaning */
};

enum bit_stream_mode {
	BS_MODE_INTERRUPT,
	BS_MODE_RESERVED, /* reserved for the future */
	BS_MODE_PIC_END,
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
	u32 product_id; /* the product ID */
	char product_name[8]; /* the product name in ascii code */
	u32 product_version; /* the product version number */
	u32 fw_version; /* the F/W version */
	u32 customer_id; /* customer ID number */
	u32 support_decoders; /* bitmask: see <<vpuapi_h_cod_std>> */
	u32 support_encoders; /* bitmask: see <<vpuapi_h_cod_std>> */
	u32 support_endian_mask; /* A variable of supported endian mode in product */
	u32 support_bitstream_mode;
	u32 support_backbone: 1;
	u32 support_avc10bit_enc: 1;
	u32 support_hevc10bit_enc: 1;
	u32 support_dual_core: 1; /* this indicates whether a product has two vcores */
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
	unsigned int endian;
	enum tiled_map_type map_type;
	unsigned int stride; /* A horizontal stride for given frame buffer */
	unsigned int width; /* A width for given frame buffer */
	unsigned int height; /* A height for given frame buffer */
	size_t size; /* A size for given frame buffer */
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
	enum cb_cr_order cbcr_order;
	unsigned int frame_endian;
	unsigned int stream_endian;
	enum bit_stream_mode bitstream_mode;
	u32 av1_format;
	enum error_conceal_unit error_conceal_unit;
	enum error_conceal_mode error_conceal_mode;
	u32 pri_ext_addr;
	u32 pri_axprot;
	u32 pri_axcache;
	u32 enable_non_ref_fbc_write: 1;
};

struct dec_initial_info {
	u32 pic_width;
	u32 pic_height;
	s32 f_rate_numerator; /* the numerator part of frame rate fraction */
	s32 f_rate_denominator; /* the denominator part of frame rate fraction */
	u64 ns_per_frame;
	struct vpu_rect pic_crop_rect;
	u32 min_frame_buffer_count; /* between 1 to 16 */
	u32 frame_buf_delay;

	u32 max_temporal_layers; /* it indicates the max number of temporal sub-layers */
	u32 profile;
	u32 level;
	u32 tier;
	bool is_ext_sar;
	u32 aspect_rate_info;
	u32 bit_rate;
	u32 user_data_header;
	u32 user_data_size;
	bool user_data_buf_full;
	u32 chroma_format_idc;/* A chroma format indicator */
	u32 luma_bitdepth; /* A bit-depth of luma sample */
	u32 chroma_bitdepth; /* A bit-depth of chroma sample */
	u32 seq_init_err_reason;
	u32 warn_info;
	dma_addr_t rd_ptr; /* A read pointer of bitstream buffer */
	dma_addr_t wr_ptr; /* A write pointer of bitstream buffer */
	u32 sequence_no;
	u32 output_bit_depth;
	u32 vlc_buf_size; /* the size of vlc buffer */
	u32 param_buf_size; /* the size of param buffer */
};

#define WAVE_SKIPMODE_WAVE_NONE 0
#define WAVE_SKIPMODE_NON_IRAP 1
#define WAVE_SKIPMODE_NON_REF 2

struct dec_param {
	u32 skipframe_mode: 2;
	u32 cra_as_bla_flag: 1;
	u32 disable_film_grain: 1;
};

struct avs2_info {
	s32 decoded_poi;
	int display_poi;
};

struct dec_output_info {
	/**
	 * this is a frame buffer index for the picture to be displayed at the moment among
	 * frame buffers which are registered using vpu_dec_register_frame_buffer(). frame
	 * data to be displayed are stored into the frame buffer with this index
	 * when there is no display delay, this index is always
	 * the same with index_frame_decoded, however, if display delay does exist for display
	 * reordering in AVC
	 * or B-frames in VC1), this index might be different with index_frame_decoded.
	 * by checking this index, HOST application can easily know whether sequence decoding
	 * has been finished or not.
	 *
	 * -3(0xFFFD) or -2(0xFFFE) : it is when a display output cannot be given due to picture
	 * reordering or skip option
	 * -1(0xFFFF) : it is when there is no more output for display at the end of sequence
	 * decoding
	 */
	s32 index_frame_display;
	/**
	 * this is a frame buffer index of decoded picture among frame buffers which were
	 * registered using vpu_dec_register_frame_buffer(). the currently decoded frame is stored
	 * into the frame buffer specified by
	 * this index.
	 *
	 * -2 : it indicates that no decoded output is generated because decoder meets EOS
	 * (end of sequence) or skip
	 * -1 : it indicates that decoder fails to decode a picture because there is no available
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
	struct avs2_info avs2_info;
	s32 decoded_poc;
	int temporal_id; /* A temporal ID of the picture */
	dma_addr_t rd_ptr; /* A stream buffer read pointer for the current decoder instance */
	dma_addr_t wr_ptr; /* A stream buffer write pointer for the current decoder instance */
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
	u32 pic_type; /* A picture type of nth picture in the custom GOP */
	u32 poc_offset; /* A POC of nth picture in the custom GOP */
	u32 pic_qp; /* A quantization parameter of nth picture in the custom GOP */
	u32 use_multi_ref_p; /* use multiref pic for P picture. valid only if PIC_TYPE is P */
	u32 ref_poc_l0; /* A POC of reference L0 of nth picture in the custom GOP */
	u32 ref_poc_l1; /* A POC of reference L1 of nth picture in the custom GOP */
	s32 temporal_id; /* A temporal ID of nth picture in the custom GOP */
};

struct custom_gop_param {
	u32 custom_gop_size; /* the size of custom GOP (0~8) */
	struct custom_gop_pic_param pic_param[MAX_GOP_NUM];
};

struct wave_custom_map_opt {
	u32 roi_avg_qp; /* it sets an average QP of ROI map */
	u32 addr_custom_map;
	u32 custom_roi_map_enable: 1; /* it enables ROI map */
	u32 custom_lambda_map_enable: 1; /* it enables custom lambda map */
	u32 custom_mode_map_enable: 1;
	u32 custom_coef_drop_enable: 1;
};

struct enc_wave_param {
	/*
	 * A profile indicator (HEVC only)
	 *
	 * 0 : the firmware determines a profile according to internalbitdepth
	 * 1 : main profile
	 * 2 : main10 profile
	 * 3 : main still picture profile
	 * in AVC encoder, a profile cannot be set by host application. the firmware decides it
	 * based on internalbitdepth. it is HIGH profile for bitdepth of 8 and HIGH10 profile for
	 * bitdepth of 10.
	 */
	u32 profile;
	u32 level; /* A level indicator (level * 10) */
	u32 internal_bit_depth: 4; /* 8/10 */
	u32 gop_preset_idx: 4; /* 0 - 9 */
	u32 decoding_refresh_type: 2; /* 0=non-IRAP, 1=CRA, 2=IDR */
	u32 intra_qp; /* A quantization parameter of intra picture */
	u32 intra_period; /* A period of intra picture in GOP size */
	u32 forced_idr_header_enable: 2;
	u32 conf_win_top; /* A top offset of conformance window */
	u32 conf_win_bot; /* A bottom offset of conformance window */
	u32 conf_win_left; /* A left offset of conformance window */
	u32 conf_win_right; /* A right offset of conformance window */
	u32 independ_slice_mode_arg;
	u32 depend_slice_mode_arg;
	u32 intra_refresh_mode: 3;
	/*
	 * it specifies an intra CTU refresh interval. depending on intra_refresh_mode,
	 * it can mean one of the following.
	 *
	 * the number of consecutive CTU rows for intra_ctu_refresh_mode of 1
	 * the number of consecutive CTU columns for intra_ctu_refresh_mode of 2
	 * A step size in CTU for intra_ctu_refresh_mode of 3
	 * the number of intra ct_us to be encoded in a picture for intra_ctu_refresh_mode of 4
	 */
	u32 intra_refresh_arg;
	/*
	 * 0 : custom setting
	 * 1 : recommended encoder parameters (slow encoding speed, highest picture quality)
	 * 2 : boost mode (normal encoding speed, moderate picture quality)
	 * 3 : fast mode (fast encoding speed, low picture quality)
	 */
	u32 depend_slice_mode : 2;
	u32 use_recommend_enc_param: 2;
	u32 max_num_merge: 2;
	u32 scaling_list_enable: 2;
	u32 bit_alloc_mode: 2; /* 0=ref-pic-priority, 1=uniform, 2=fixed_bit_ratio */
	s32 beta_offset_div2: 4; /* it sets beta_offset_div2 for deblocking filter */
	s32 tc_offset_div2: 4; /* it sets tc_offset_div3 for deblocking filter */
	u32 hvs_qp_scale: 4; /* QP scaling factor for CU QP adjust if hvs_qp_scale_enable is 1 */
	u32 hvs_max_delta_qp; /* A maximum delta QP for HVS */
	/*
	 * A fixed bit ratio (1 ~ 255) for each picture of GOP's bit
	 * allocation
	 *
	 * N = 0 ~ (MAX_GOP_SIZE - 1)
	 * MAX_GOP_SIZE = 8
	 *
	 * for instance when MAX_GOP_SIZE is 3, fixed_bit_ratio0, fixed_bit_ratio1, and
	 * fixed_bit_ratio2 can be set as 2, 1, and 1 respectively for
	 * the fixed bit ratio 2:1:1. this is only valid when bit_alloc_mode is 2.
	 */
	u8 fixed_bit_ratio[MAX_GOP_NUM];
	struct custom_gop_param gop_param; /* <<vpuapi_h_custom_gop_param>> */
	u32 num_units_in_tick;
	u32 time_scale;
	u32 num_ticks_poc_diff_one;
	s32 chroma_cb_qp_offset; /* the value of chroma(cb) QP offset */
	s32 chroma_cr_qp_offset; /* the value of chroma(cr) QP offset */
	s32 initial_rc_qp;
	u32 nr_intra_weight_y;
	u32 nr_intra_weight_cb; /* A weight to cb noise level for intra picture (0 ~ 31) */
	u32 nr_intra_weight_cr; /* A weight to cr noise level for intra picture (0 ~ 31) */
	u32 nr_inter_weight_y;
	u32 nr_inter_weight_cb; /* A weight to cb noise level for inter picture (0 ~ 31) */
	u32 nr_inter_weight_cr; /* A weight to cr noise level for inter picture (0 ~ 31) */
	u32 nr_noise_sigma_y; /* Y noise standard deviation if nr_noise_est_enable is 0 */
	u32 nr_noise_sigma_cb;/* cb noise standard deviation if nr_noise_est_enable is 0 */
	u32 nr_noise_sigma_cr;/* cr noise standard deviation if nr_noise_est_enable is 0 */
	u32 bg_thr_diff;
	u32 bg_thr_mean_diff;
	u32 bg_lambda_qp;
	u32 bg_delta_qp;
	u32 pu04_delta_rate: 8; /* added to the total cost of 4x4 blocks */
	u32 pu08_delta_rate: 8; /* added to the total cost of 8x8 blocks */
	u32 pu16_delta_rate: 8; /* added to the total cost of 16x16 blocks */
	u32 pu32_delta_rate: 8; /* added to the total cost of 32x32 blocks */
	u32 pu04_intra_planar_delta_rate: 8;
	u32 pu04_intra_dc_delta_rate: 8;
	u32 pu04_intra_angle_delta_rate: 8;
	u32 pu08_intra_planar_delta_rate: 8;
	u32 pu08_intra_dc_delta_rate: 8;
	u32 pu08_intra_angle_delta_rate: 8;
	u32 pu16_intra_planar_delta_rate: 8;
	u32 pu16_intra_dc_delta_rate: 8;
	u32 pu16_intra_angle_delta_rate: 8;
	u32 pu32_intra_planar_delta_rate: 8;
	u32 pu32_intra_dc_delta_rate: 8;
	u32 pu32_intra_angle_delta_rate: 8;
	u32 cu08_intra_delta_rate: 8;
	u32 cu08_inter_delta_rate: 8;
	u32 cu08_merge_delta_rate: 8;
	u32 cu16_intra_delta_rate: 8;
	u32 cu16_inter_delta_rate: 8;
	u32 cu16_merge_delta_rate: 8;
	u32 cu32_intra_delta_rate: 8;
	u32 cu32_inter_delta_rate: 8;
	u32 cu32_merge_delta_rate: 8;
	u32 coef_clear_disable: 8;
	u32 min_qp_i; /* A minimum QP of I picture for rate control */
	u32 max_qp_i; /* A maximum QP of I picture for rate control */
	u32 min_qp_p; /* A minimum QP of P picture for rate control */
	u32 max_qp_p; /* A maximum QP of P picture for rate control */
	u32 min_qp_b; /* A minimum QP of B picture for rate control */
	u32 max_qp_b; /* A maximum QP of B picture for rate control */
	u32 custom_lambda_addr; /* it specifies the address of custom lambda map */
	u32 user_scaling_list_addr; /* it specifies the address of user scaling list file */
	u32 avc_idr_period;/* A period of IDR picture (0 ~ 1024). 0 - implies an infinite period */
	u32 avc_slice_arg;	/* the number of MB for a slice when avc_slice_mode is set with 1 */
	u32 intra_mb_refresh_mode: 2; /* 0=none, 1=row, 2=column, 3=step-size-in-mb */
	/**
	 * it specifies an intra MB refresh interval. depending on intra_mb_refresh_mode,
	 * it can mean one of the following.
	 *
	 * the number of consecutive MB rows for intra_mb_refresh_mode of 1
	 * the number of consecutive MB columns for intra_mb_refresh_mode of 2
	 * A step size in MB for intra_mb_refresh_mode of 3
	 */
	u32 intra_mb_refresh_arg;
	u32 rc_weight_param;
	u32 rc_weight_buf;

	/* flags */
	u32 en_still_picture: 1; /* still picture profile */
	u32 tier: 1; /* 0=main, 1=high */
	u32 independ_slice_mode : 1; /* 0=no-multi-slice, 1=slice-in-ctu-number*/
	u32 avc_slice_mode: 1; /* 0=none, 1=slice-in-mb-number */
	u32 entropy_coding_mode: 1; /* 0=CAVLC, 1=CABAC */
	u32 lossless_enable: 1; /* enables lossless coding */
	u32 const_intra_pred_flag: 1; /* enables constrained intra prediction */
	u32 tmvp_enable: 1; /* enables temporal motion vector prediction */
	u32 wpp_enable: 1;
	u32 disable_deblk: 1; /* it disables in-loop deblocking filtering */
	u32 lf_cross_slice_boundary_enable: 1;
	u32 skip_intra_trans: 1;
	u32 sao_enable: 1; /* it enables SAO (sample adaptive offset) */
	u32 intra_nx_n_enable: 1; /* it enables intra nx_n p_us */
	u32 cu_level_rc_enable: 1; /* it enable CU level rate control */
	u32 hvs_qp_enable: 1; /* enable CU QP adjustment for subjective quality enhancement */
	u32 roi_enable: 1; /* it enables ROI map. NOTE: it is valid when rate control is on */
	u32 nr_y_enable: 1; /* it enables noise reduction algorithm to Y component */
	u32 nr_noise_est_enable: 1;
	u32 nr_cb_enable: 1; /* it enables noise reduction algorithm to cb component */
	u32 nr_cr_enable: 1; /* it enables noise reduction algorithm to cr component */
	u32 use_long_term: 1; /* it enables long-term reference function */
	u32 monochrome_enable: 1; /* it enables monochrom encoding mode */
	u32 strong_intra_smooth_enable: 1; /* it enables strong intra smoothing */
	u32 weight_pred_enable: 1; /* it enables to use weighted prediction*/
	u32 bg_detect_enable: 1; /* it enables background detection */
	u32 custom_lambda_enable: 1; /* it enables custom lambda table */
	u32 custom_md_enable: 1; /* it enables custom mode decision */
	u32 rdo_skip: 1; /* it skips RDO(rate distortion optimization) */
	u32 lambda_scaling_enable: 1; /* it enables lambda scaling using custom GOP */
	u32 transform8x8_enable: 1; /* it enables 8x8 intra prediction and 8x8 transform */
	u32 mb_level_rc_enable: 1; /* it enables MB-level rate control */
	u32 s2fme_disable: 1; /* it disables s2me_fme (only for AVC encoder) */
};

struct enc_sub_frame_sync_config {
	u32 sub_frame_sync_mode; /* 0=wire-based, 1=register-based */
	u32 sub_frame_sync_on;
};

struct enc_open_param {
	dma_addr_t bitstream_buffer;
	unsigned int bitstream_buffer_size;
	u32 pic_width; /* the width of a picture to be encoded in unit of sample */
	u32 pic_height; /* the height of a picture to be encoded in unit of sample */
	u32 frame_rate_info;/* desired fps */
	u32 vbv_buffer_size;
	u32 bit_rate; /* target bitrate in bps */
	struct enc_wave_param wave_param;
	enum cb_cr_order cbcr_order;
	unsigned int stream_endian;
	unsigned int source_endian;
	enum packed_format_num packed_format; /* <<vpuapi_h_packed_format_num>> */
	enum frame_buffer_format src_format;
	/* enum frame_buffer_format output_format; not used yet */
	u32 enc_hrd_rbsp_in_vps; /* it encodes the HRD syntax rbsp into VPS */
	u32 hrd_rbsp_data_size; /* the bit size of the HRD rbsp data */
	u32 hrd_rbsp_data_addr; /* the address of the HRD rbsp data */
	u32 encode_vui_rbsp;
	u32 vui_rbsp_data_size; /* the bit size of the VUI rbsp data */
	u32 vui_rbsp_data_addr; /* the address of the VUI rbsp data */
	u32 pri_ext_addr;
	u32 pri_axprot;
	u32 pri_axcache;
	bool ring_buffer_enable;
	bool line_buf_int_en;
	bool enable_pts; /* an enable flag to report PTS(presentation timestamp) */
	u32 rc_enable : 1; /* rate control */
	u32 enable_non_ref_fbc_write: 1;
	u32 sub_frame_sync_enable: 1;
	u32 sub_frame_sync_mode: 1;
};

struct enc_initial_info {
	u32 min_frame_buffer_count; /* minimum number of frame buffer */
	u32 min_src_frame_count; /* minimum number of source buffer */
	u32 max_latency_pictures; /* maximum number of picture latency */
	u32 seq_init_err_reason; /* error information */
	u32 warn_info; /* warn information */
	u32 vlc_buf_size; /* the size of task buffer */
	u32 param_buf_size; /* the size of task buffer */
};

struct enc_code_opt {
	u32 implicit_header_encode: 1;
	u32 encode_vcl: 1; /* A flag to encode VCL nal unit explicitly */
	u32 encode_vps: 1; /* A flag to encode VPS nal unit explicitly */
	u32 encode_sps: 1; /* A flag to encode SPS nal unit explicitly */
	u32 encode_pps: 1; /* A flag to encode PPS nal unit explicitly */
	u32 encode_aud: 1; /* A flag to encode AUD nal unit explicitly */
	u32 encode_eos: 1;
	u32 encode_eob: 1;
	u32 encode_vui: 1; /* A flag to encode VUI nal unit explicitly */
};

struct enc_param {
	struct frame_buffer *source_frame;
	u32 pic_stream_buffer_addr;
	u64 pic_stream_buffer_size;
	u32 force_pic_qp_i;
	u32 force_pic_qp_p;
	u32 force_pic_qp_b;
	u32 force_pic_type: 2;
	u32 src_idx; /* A source frame buffer index */
	struct enc_code_opt code_option;
	u32 use_cur_src_as_longterm_pic;
	u32 use_longterm_ref;
	u64 pts; /* the presentation timestamp (PTS) of input source */
	struct wave_custom_map_opt custom_map_opt;
	u32 wp_pix_sigma_y; /* pixel variance of Y component for weighted prediction */
	u32 wp_pix_sigma_cb; /* pixel variance of cb component for weighted prediction */
	u32 wp_pix_sigma_cr; /* pixel variance of cr component for weighted prediction */
	u32 wp_pix_mean_y; /* pixel mean value of Y component for weighted prediction */
	u32 wp_pix_mean_cb; /* pixel mean value of cb component for weighted prediction */
	u32 wp_pix_mean_cr; /* pixel mean value of cr component for weighted prediction */
	bool src_end_flag;
	u32 skip_picture: 1;
	u32 force_pic_qp_enable: 1; /* flag used to force picture quantization parameter */
	u32 force_pic_type_enable: 1; /* A flag to use a force picture type */
	u32 force_all_ctu_coef_drop_enable: 1; /* forces all coefficients to be zero after TQ */
};

struct enc_output_info {
	u32 bitstream_buffer;
	u32 bitstream_size; /* the byte size of encoded bitstream */
	u32 pic_type: 2; /* <<vpuapi_h_pic_type>> */
	s32 recon_frame_index;
	dma_addr_t rd_ptr;
	dma_addr_t wr_ptr;
	u32 enc_pic_byte; /* the number of encoded picture bytes */
	s32 enc_src_idx; /* the source buffer index of the currently encoded picture */
	u32 enc_vcl_nut;
	u32 error_reason; /* the error reason of the currently encoded picture */
	u32 warn_info; /* the warning information of the currently encoded picture */
	unsigned int frame_cycle; /* param for reporting the cycle number of encoding one frame*/
	u64 pts;
	u32 enc_host_cmd_tick; /* tick of ENC_PIC command for the picture */
	u32 enc_encode_end_tick; /* end tick of encoding slices of the picture */
};

enum ENC_PIC_CODE_OPTION {
	CODEOPT_ENC_HEADER_IMPLICIT = BIT(0),
	CODEOPT_ENC_VCL = BIT(1), /* A flag to encode VCL nal unit explicitly */
};

enum GOP_PRESET_IDX {
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
	struct {
		u32 use_ip_enable;
		u32 use_bit_enable;
		u32 use_lf_row_enable: 1;
		u32 use_enc_rdo_enable: 1;
		u32 use_enc_lf_enable: 1;
	} wave;
	unsigned int buf_size;
	dma_addr_t buf_base;
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
	unsigned int num_of_decoding_fbs;
	unsigned int num_of_display_fbs;
	unsigned int stride;
	enum mirror_direction mirror_direction;
	unsigned int rotation_angle;
	struct frame_buffer rotator_output;
	unsigned int rotator_stride;
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
	bool rotation_enable;
	bool mirror_enable;
	bool dering_enable;
	bool initial_info_obtained;
	bool reorder_enable;
	bool thumbnail_mode;
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
	bool ring_buffer_enable;
	struct sec_axi_info sec_axi_info;
	struct enc_sub_frame_sync_config sub_frame_sync_config;
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
	struct list_head instances;
	struct video_device *video_dev_dec;
	struct video_device *video_dev_enc;
	struct mutex dev_lock; /* the lock for the src,dst v4l2 queues */
	struct mutex hw_lock; /* lock hw configurations */
	int irq;
	enum product_id	 product;
	struct vpu_attr	 attr;
	struct vpu_buf common_mem;
	u32 last_performance_cycles;
	struct dma_vpu_buf sram_buf;
	void __iomem *vdb_register;
	u32 product_code;
	struct ida inst_ida;
	struct clk_bulk_data *clks;
	struct reset_control *resets;
	int num_clks;
};

struct vpu_instance;

struct vpu_instance_ops {
	void (*start_process)(struct vpu_instance *inst);
	void (*stop_process)(struct vpu_instance *inst);
	void (*finish_process)(struct vpu_instance *inst);
};

struct vpu_instance {
	struct list_head list;
	struct v4l2_fh v4l2_fh;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl;
	struct vpu_device *dev;
	struct v4l2_m2m_dev *v4l2_m2m_dev;
	struct kfifo irq_status;
	struct completion irq_done;

	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	struct v4l2_pix_format_mplane display_fmt;
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_hsv_encoding hsv_enc;

	enum vpu_instance_state state;
	enum vpu_instance_type type;
	const struct vpu_instance_ops *ops;
	struct vpu_rect crop_rect;

	enum wave_std		 std;
	s32			 id;
	union {
		struct enc_info enc_info;
		struct dec_info dec_info;
	} *codec_info;
	struct frame_buffer frame_buf[MAX_REG_FRAME];
	struct vpu_buf frame_vbuf[MAX_REG_FRAME];
	u32 min_dst_buf_count;
	u32 dst_buf_count;
	u32 queued_src_buf_num;
	u32 queued_dst_buf_num;
	u32 conf_win_width;
	u32 conf_win_height;
	u64 timestamp;
	u64 timestamp_cnt;
	bool cbcr_interleave;
	bool nv21;
	bool eos;

	struct vpu_buf bitstream_vbuf;
	bool thumbnail_mode;

	unsigned int min_src_buf_count;
	unsigned int src_buf_count;
	unsigned int rot_angle;
	unsigned int mirror_direction;
	unsigned int bit_depth;
	unsigned int frame_rate;
	unsigned int vbv_buf_size;
	unsigned int rc_mode;
	unsigned int rc_enable;
	unsigned int bit_rate;
	struct enc_wave_param enc_param;
};

void wave5_vdi_write_register(struct vpu_device *vpu_dev, u32 addr, u32 data);
u32 wave5_vdi_readl(struct vpu_device *vpu_dev, u32 addr);
int wave5_vdi_clear_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);
int wave5_vdi_allocate_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);
int wave5_vdi_write_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb, size_t offset,
			   u8 *data, size_t len, unsigned int endian);
unsigned int wave5_vdi_convert_endian(struct vpu_device *vpu_dev, unsigned int endian);
void wave5_vdi_free_dma_memory(struct vpu_device *vpu_dev, struct vpu_buf *vb);

int wave5_vpu_init_with_bitcode(struct device *dev, u8 *bitcode, size_t size);
void wave5_vpu_clear_interrupt_ex(struct vpu_instance *inst, u32 intr_flag);
int wave5_vpu_get_version_info(struct device *dev, u32 *revision, unsigned int *product_id);
int wave5_vpu_dec_open(struct vpu_instance *inst, struct dec_open_param *open_param);
int wave5_vpu_dec_close(struct vpu_instance *inst, u32 *fail_res);
int wave5_vpu_dec_issue_seq_init(struct vpu_instance *inst);
int wave5_vpu_dec_complete_seq_init(struct vpu_instance *inst, struct dec_initial_info *info);
int wave5_vpu_dec_register_frame_buffer_ex(struct vpu_instance *inst, int num_of_decoding_fbs,
					   int num_of_display_fbs, int stride, int height,
					   int map_type);
int wave5_vpu_dec_start_one_frame(struct vpu_instance *inst, struct dec_param *param,
				  u32 *res_fail);
int wave5_vpu_dec_get_output_info(struct vpu_instance *inst, struct dec_output_info *info);
int wave5_vpu_dec_set_rd_ptr(struct vpu_instance *inst, dma_addr_t addr, int update_wr_ptr);
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
