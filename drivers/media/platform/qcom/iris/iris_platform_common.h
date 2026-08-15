/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_COMMON_H__
#define __IRIS_PLATFORM_COMMON_H__

#include <linux/bits.h>
#include "iris_buffer.h"

struct iris_core;
struct iris_inst;

#define HW_RESPONSE_TIMEOUT_VALUE               (1000) /* milliseconds */
#define AUTOSUSPEND_DELAY_VALUE			(HW_RESPONSE_TIMEOUT_VALUE + 500) /* milliseconds */

#define REGISTER_BIT_DEPTH(luma, chroma)	((luma) << 16 | (chroma))
#define BIT_DEPTH_8				REGISTER_BIT_DEPTH(8, 8)
#define BIT_DEPTH_10				REGISTER_BIT_DEPTH(10, 10)
#define CODED_FRAMES_PROGRESSIVE		0x0
#define DEFAULT_MAX_HOST_BUF_COUNT		64
#define DEFAULT_MAX_HOST_BURST_BUF_COUNT	256
#define DEFAULT_FPS				30
#define MAXIMUM_FPS				480
#define NUM_MBS_8K                             ((8192 * 4352) / 256)
#define MIN_QP_8BIT				1
#define MAX_QP					51
#define MAX_QP_HEVC				63
#define DEFAULT_QP				20
#define BITRATE_DEFAULT			20000000
#define INVALID_DEFAULT_MARK_OR_USE_LTR		-1
#define MAX_LTR_FRAME_COUNT_GEN1		4
#define MAX_LTR_FRAME_COUNT_GEN2		2
#define MAX_LAYER_HB				3
#define MAX_AVC_LAYER_HP_HYBRID_LTR		5
#define MAX_AVC_LAYER_HP_SLIDING_WINDOW		3
#define MAX_HEVC_LAYER_HP_SLIDING_WINDOW	3
#define MAX_HEVC_VBR_LAYER_HP_SLIDING_WINDOW	5
#define MAX_HIER_CODING_LAYER_GEN1		6

enum stage_type {
	STAGE_1 = 1,
	STAGE_2 = 2,
};

enum pipe_type {
	PIPE_1 = 1,
	PIPE_2 = 2,
	PIPE_4 = 4,
};

extern const struct iris_firmware_data iris_hfi_gen1_data;
extern const struct iris_firmware_data iris_hfi_gen2_data;

extern const struct iris_platform_data qcs8300_data;
extern const struct iris_platform_data sc7280_data;
extern const struct iris_platform_data sm8250_data;
extern const struct iris_platform_data sm8550_data;
extern const struct iris_platform_data sm8650_data;
extern const struct iris_platform_data sm8750_data;
extern const struct iris_platform_data x1p42100_data;

enum platform_clk_type {
	IRIS_AXI_CLK, /* AXI0 in case of platforms with multiple AXI clocks */
	IRIS_CTRL_CLK,
	IRIS_AHB_CLK,
	IRIS_HW_CLK,
	IRIS_HW_AHB_CLK,
	IRIS_AXI1_CLK,
	IRIS_CTRL_FREERUN_CLK,
	IRIS_HW_FREERUN_CLK,
	IRIS_BSE_HW_CLK,
	IRIS_VPP0_HW_CLK,
	IRIS_VPP1_HW_CLK,
	IRIS_APV_HW_CLK,
};

struct platform_clk_data {
	enum platform_clk_type clk_type;
	const char *clk_name;
};

struct tz_cp_config {
	u32 cp_start;
	u32 cp_size;
	u32 cp_nonpixel_start;
	u32 cp_nonpixel_size;
};

struct platform_inst_caps {
	u32 min_frame_width;
	u32 max_frame_width;
	u32 min_frame_height;
	u32 max_frame_height;
	u32 max_mbpf;
	u32 mb_cycles_vsp;
	u32 mb_cycles_vpp;
	u32 mb_cycles_fw;
	u32 mb_cycles_fw_vpp;
	u32 max_frame_rate;
	u32 max_operating_rate;
};

enum platform_inst_fw_cap_type {
	PROFILE_H264 = 1,
	PROFILE_HEVC,
	PROFILE_VP9,
	LEVEL_H264,
	LEVEL_HEVC,
	LEVEL_VP9,
	PROFILE_AV1,
	LEVEL_AV1,
	TIER_AV1,
	DRAP,
	FILM_GRAIN,
	SUPER_BLOCK,
	ENH_LAYER_COUNT,
	INPUT_BUF_HOST_MAX_COUNT,
	OUTPUT_BUF_HOST_MAX_COUNT,
	STAGE,
	PIPE,
	POC,
	CODED_FRAMES,
	BIT_DEPTH,
	RAP_FRAME,
	TIER,
	HEADER_MODE,
	PREPEND_SPSPPS_TO_IDR,
	BITRATE,
	BITRATE_PEAK,
	BITRATE_MODE,
	FRAME_SKIP_MODE,
	FRAME_RC_ENABLE,
	GOP_SIZE,
	ENTROPY_MODE,
	MIN_FRAME_QP_H264,
	MIN_FRAME_QP_HEVC,
	MAX_FRAME_QP_H264,
	MAX_FRAME_QP_HEVC,
	I_FRAME_MIN_QP_H264,
	I_FRAME_MIN_QP_HEVC,
	P_FRAME_MIN_QP_H264,
	P_FRAME_MIN_QP_HEVC,
	B_FRAME_MIN_QP_H264,
	B_FRAME_MIN_QP_HEVC,
	I_FRAME_MAX_QP_H264,
	I_FRAME_MAX_QP_HEVC,
	P_FRAME_MAX_QP_H264,
	P_FRAME_MAX_QP_HEVC,
	B_FRAME_MAX_QP_H264,
	B_FRAME_MAX_QP_HEVC,
	I_FRAME_QP_H264,
	I_FRAME_QP_HEVC,
	P_FRAME_QP_H264,
	P_FRAME_QP_HEVC,
	B_FRAME_QP_H264,
	B_FRAME_QP_HEVC,
	ROTATION,
	HFLIP,
	VFLIP,
	IR_TYPE,
	IR_PERIOD,
	LTR_COUNT,
	USE_LTR,
	MARK_LTR,
	B_FRAME,
	INTRA_PERIOD,
	LAYER_ENABLE,
	LAYER_TYPE_H264,
	LAYER_TYPE_HEVC,
	LAYER_COUNT_H264,
	LAYER_COUNT_HEVC,
	LAYER0_BITRATE_H264,
	LAYER1_BITRATE_H264,
	LAYER2_BITRATE_H264,
	LAYER3_BITRATE_H264,
	LAYER4_BITRATE_H264,
	LAYER5_BITRATE_H264,
	LAYER0_BITRATE_HEVC,
	LAYER1_BITRATE_HEVC,
	LAYER2_BITRATE_HEVC,
	LAYER3_BITRATE_HEVC,
	LAYER4_BITRATE_HEVC,
	LAYER5_BITRATE_HEVC,
	INST_FW_CAP_MAX,
};

enum platform_inst_fw_cap_flags {
	CAP_FLAG_DYNAMIC_ALLOWED	= BIT(0),
	CAP_FLAG_MENU			= BIT(1),
	CAP_FLAG_INPUT_PORT		= BIT(2),
	CAP_FLAG_OUTPUT_PORT		= BIT(3),
	CAP_FLAG_CLIENT_SET		= BIT(4),
	CAP_FLAG_BITMASK		= BIT(5),
	CAP_FLAG_VOLATILE		= BIT(6),
};

struct platform_inst_fw_cap {
	enum platform_inst_fw_cap_type cap_id;
	s64 min;
	s64 max;
	s64 step_or_mask;
	s64 value;
	u32 hfi_id;
	enum platform_inst_fw_cap_flags flags;
	int (*set)(struct iris_inst *inst,
		   enum platform_inst_fw_cap_type cap_id);
};

struct bw_info {
	u32 mbs_per_sec;
	u32 bw_ddr;
};

struct iris_core_power {
	u64 clk_freq;
	u64 icc_bw;
};

struct iris_inst_power {
	u64 min_freq;
	u32 icc_bw;
};

struct icc_vote_data {
	u32 height, width;
	u32 fps;
};

enum platform_pm_domain_type {
	IRIS_CTRL_POWER_DOMAIN,
	IRIS_HW_POWER_DOMAIN,
	IRIS_VPP0_HW_POWER_DOMAIN,
	IRIS_VPP1_HW_POWER_DOMAIN,
	IRIS_APV_HW_POWER_DOMAIN,
};

struct iris_firmware_data {
	void (*init_hfi_ops)(struct iris_core *core);

	u32 core_arch;

	const struct platform_inst_fw_cap *inst_fw_caps_dec;
	u32 inst_fw_caps_dec_size;
	const struct platform_inst_fw_cap *inst_fw_caps_enc;
	u32 inst_fw_caps_enc_size;

	const u32 *dec_input_config_params_default;
	unsigned int dec_input_config_params_default_size;
	const u32 *dec_input_config_params_hevc;
	unsigned int dec_input_config_params_hevc_size;
	const u32 *dec_input_config_params_vp9;
	unsigned int dec_input_config_params_vp9_size;
	const u32 *dec_input_config_params_av1;
	unsigned int dec_input_config_params_av1_size;
	const u32 *dec_output_config_params;
	unsigned int dec_output_config_params_size;
	const u32 *enc_input_config_params;
	unsigned int enc_input_config_params_size;
	const u32 *enc_output_config_params;
	unsigned int enc_output_config_params_size;

	const u32 *dec_input_prop;
	unsigned int dec_input_prop_size;
	const u32 *dec_output_prop_avc;
	unsigned int dec_output_prop_avc_size;
	const u32 *dec_output_prop_hevc;
	unsigned int dec_output_prop_hevc_size;
	const u32 *dec_output_prop_vp9;
	unsigned int dec_output_prop_vp9_size;
	const u32 *dec_output_prop_av1;
	unsigned int dec_output_prop_av1_size;

	const u32 *dec_ip_int_buf_tbl;
	unsigned int dec_ip_int_buf_tbl_size;
	const u32 *dec_op_int_buf_tbl;
	unsigned int dec_op_int_buf_tbl_size;
	const u32 *enc_ip_int_buf_tbl;
	unsigned int enc_ip_int_buf_tbl_size;
	const u32 *enc_op_int_buf_tbl;
	unsigned int enc_op_int_buf_tbl_size;
};

struct iris_firmware_desc {
	const struct iris_firmware_data *firmware_data;
	u32 (*get_vpu_buffer_size)(struct iris_inst *inst, enum iris_buffer_type buffer_type);
	const char *fwname;
};

struct iris_platform_data {
	/*
	 * XXX: replace with gen1 / gen2 pointers once we have platforms
	 * supporting both firmware kinds.
	 */
	const struct iris_firmware_desc *firmware_desc;

	const struct vpu_ops *vpu_ops;
	const struct icc_info *icc_tbl;
	unsigned int icc_tbl_size;
	const struct bw_info *bw_tbl_dec;
	unsigned int bw_tbl_dec_size;
	const char * const *pmdomain_tbl;
	unsigned int pmdomain_tbl_size;
	const char * const *opp_pd_tbl;
	unsigned int opp_pd_tbl_size;
	const struct platform_clk_data *clk_tbl;
	const char * const *opp_clk_tbl;
	unsigned int clk_tbl_size;
	const char * const *clk_rst_tbl;
	unsigned int clk_rst_tbl_size;
	const char * const *controller_rst_tbl;
	unsigned int controller_rst_tbl_size;
	u64 dma_mask;
	const u32 *inst_iris_fmts;
	u32 inst_iris_fmts_size;
	struct platform_inst_caps *inst_caps;
	const struct tz_cp_config *tz_cp_config_data;
	u32 tz_cp_config_data_size;
	u32 num_vpp_pipe;
	bool no_aon;
	u32 max_session_count;
	/* max number of macroblocks per frame supported */
	u32 max_core_mbpf;
	/* max number of macroblocks per second supported */
	u32 max_core_mbps;
};

#endif
