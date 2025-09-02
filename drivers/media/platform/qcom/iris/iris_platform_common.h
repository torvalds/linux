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

#define IRIS_PAS_ID				9
#define HW_RESPONSE_TIMEOUT_VALUE               (1000) /* milliseconds */
#define AUTOSUSPEND_DELAY_VALUE			(HW_RESPONSE_TIMEOUT_VALUE + 500) /* milliseconds */

#define REGISTER_BIT_DEPTH(luma, chroma)	((luma) << 16 | (chroma))
#define BIT_DEPTH_8				REGISTER_BIT_DEPTH(8, 8)
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

enum stage_type {
	STAGE_1 = 1,
	STAGE_2 = 2,
};

enum pipe_type {
	PIPE_1 = 1,
	PIPE_2 = 2,
	PIPE_4 = 4,
};

extern struct iris_platform_data qcs8300_data;
extern struct iris_platform_data sm8250_data;
extern struct iris_platform_data sm8550_data;
extern struct iris_platform_data sm8650_data;
extern struct iris_platform_data sm8750_data;

enum platform_clk_type {
	IRIS_AXI_CLK, /* AXI0 in case of platforms with multiple AXI clocks */
	IRIS_CTRL_CLK,
	IRIS_HW_CLK,
	IRIS_AXI1_CLK,
	IRIS_CTRL_FREERUN_CLK,
	IRIS_HW_FREERUN_CLK,
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

struct ubwc_config_data {
	u32	max_channels;
	u32	mal_length;
	u32	highest_bank_bit;
	u32	bank_swzl_level;
	u32	bank_swz2_level;
	u32	bank_swz3_level;
	u32	bank_spreading;
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
	u32 num_comv;
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
};

struct iris_platform_data {
	void (*init_hfi_command_ops)(struct iris_core *core);
	void (*init_hfi_response_ops)(struct iris_core *core);
	struct iris_inst *(*get_instance)(void);
	u32 (*get_vpu_buffer_size)(struct iris_inst *inst, enum iris_buffer_type buffer_type);
	const struct vpu_ops *vpu_ops;
	void (*set_preset_registers)(struct iris_core *core);
	const struct icc_info *icc_tbl;
	unsigned int icc_tbl_size;
	const struct bw_info *bw_tbl_dec;
	unsigned int bw_tbl_dec_size;
	const char * const *pmdomain_tbl;
	unsigned int pmdomain_tbl_size;
	const char * const *opp_pd_tbl;
	unsigned int opp_pd_tbl_size;
	const struct platform_clk_data *clk_tbl;
	unsigned int clk_tbl_size;
	const char * const *clk_rst_tbl;
	unsigned int clk_rst_tbl_size;
	const char * const *controller_rst_tbl;
	unsigned int controller_rst_tbl_size;
	u64 dma_mask;
	const char *fwname;
	u32 pas_id;
	struct platform_inst_caps *inst_caps;
	struct platform_inst_fw_cap *inst_fw_caps_dec;
	u32 inst_fw_caps_dec_size;
	struct platform_inst_fw_cap *inst_fw_caps_enc;
	u32 inst_fw_caps_enc_size;
	struct tz_cp_config *tz_cp_config_data;
	u32 core_arch;
	u32 hw_response_timeout;
	struct ubwc_config_data *ubwc_config;
	u32 num_vpp_pipe;
	u32 max_session_count;
	/* max number of macroblocks per frame supported */
	u32 max_core_mbpf;
	/* max number of macroblocks per second supported */
	u32 max_core_mbps;
	const u32 *dec_input_config_params_default;
	unsigned int dec_input_config_params_default_size;
	const u32 *dec_input_config_params_hevc;
	unsigned int dec_input_config_params_hevc_size;
	const u32 *dec_input_config_params_vp9;
	unsigned int dec_input_config_params_vp9_size;
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
	const u32 *dec_ip_int_buf_tbl;
	unsigned int dec_ip_int_buf_tbl_size;
	const u32 *dec_op_int_buf_tbl;
	unsigned int dec_op_int_buf_tbl_size;
	const u32 *enc_ip_int_buf_tbl;
	unsigned int enc_ip_int_buf_tbl_size;
	const u32 *enc_op_int_buf_tbl;
	unsigned int enc_op_int_buf_tbl_size;
};

#endif
