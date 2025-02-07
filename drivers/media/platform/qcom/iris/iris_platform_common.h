/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_COMMON_H__
#define __IRIS_PLATFORM_COMMON_H__

struct iris_core;

#define IRIS_PAS_ID				9
#define HW_RESPONSE_TIMEOUT_VALUE               (1000) /* milliseconds */
#define AUTOSUSPEND_DELAY_VALUE			(HW_RESPONSE_TIMEOUT_VALUE + 500) /* milliseconds */

extern struct iris_platform_data sm8550_data;

enum platform_clk_type {
	IRIS_AXI_CLK,
	IRIS_CTRL_CLK,
	IRIS_HW_CLK,
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
};

enum platform_inst_fw_cap_type {
	PROFILE = 1,
	LEVEL,
	DEBLOCK,
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
};

struct iris_core_power {
	u64 clk_freq;
	u64 icc_bw;
};

enum platform_pm_domain_type {
	IRIS_CTRL_POWER_DOMAIN,
	IRIS_HW_POWER_DOMAIN,
};

struct iris_platform_data {
	void (*init_hfi_command_ops)(struct iris_core *core);
	void (*init_hfi_response_ops)(struct iris_core *core);
	struct iris_inst *(*get_instance)(void);
	const struct vpu_ops *vpu_ops;
	void (*set_preset_registers)(struct iris_core *core);
	const struct icc_info *icc_tbl;
	unsigned int icc_tbl_size;
	const char * const *pmdomain_tbl;
	unsigned int pmdomain_tbl_size;
	const char * const *opp_pd_tbl;
	unsigned int opp_pd_tbl_size;
	const struct platform_clk_data *clk_tbl;
	unsigned int clk_tbl_size;
	const char * const *clk_rst_tbl;
	unsigned int clk_rst_tbl_size;
	u64 dma_mask;
	const char *fwname;
	u32 pas_id;
	struct platform_inst_caps *inst_caps;
	struct platform_inst_fw_cap *inst_fw_caps;
	u32 inst_fw_caps_size;
	struct tz_cp_config *tz_cp_config_data;
	u32 core_arch;
	u32 hw_response_timeout;
	struct ubwc_config_data *ubwc_config;
	u32 num_vpp_pipe;
	u32 max_session_count;
};

#endif
