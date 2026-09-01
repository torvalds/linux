// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DCN60_CLK_MGR_H_
#define __DCN60_CLK_MGR_H_

#ifdef CONFIG_DRM_AMD_DC_FP
#include "bounding_boxes/utm_qos_model_types.h"
#include "bounding_boxes/utm_qos_model_dchub_v3.h"
#endif

union dcn60_clk_mgr_block_sequence_params {
	struct {
		/* inputs */
		uint32_t ppclk;
		uint16_t freq_mhz;
		/* outputs */
		int *response;
	} update_hardmin_params;
	struct {
		/* inputs */
		uint32_t ppclk;
		int freq_khz;
		/* outputs */
		int *response;
	} update_hardmin_optimized_params;
	struct {
		/* inputs */
		uint16_t freq_mhz;
	} update_deep_sleep_dcfclk_params;
	struct {
		/* inputs */
		bool allow_fclk;
		bool allow_uclk;
		bool wait_resp;
		bool drr_enable;
		bool alt_ch_enable;
	} indicate_pstate_status_params;
	struct {
		/* inputs */
		struct dc_state *context;
		int *ref_dppclk_khz;
		bool safe_to_lower;
	} update_dppclk_dto_params;
	struct {
		/* inputs */
		struct dc_state *context;
		int *ref_dtbclk_khz;
	} update_dtbclk_dto_params;
	struct {
		/* inputs */
		struct dc_state *context;
	} update_dentist_params;
	struct {
		/* inputs */
		struct dmcu *dmcu;
		unsigned int wait;
	} update_psr_wait_loop_params;
	struct {
		/* inputs */
		uint8_t base_efficiency;
		uint8_t low_power_efficiency;
	} update_stutter_efficiency_params;
	struct {
		unsigned int utm_urgent_bandwidth_lb_KBps;
		unsigned int utm_nominal_bandwidth_lb_KBps;
		unsigned int utm_lsdma_bandwidth_lb_KBps;
		unsigned int utm_latency_ub_index;
	} update_utm_qos_request_params;
};

enum dcn60_clk_mgr_block_sequence_func {
	CLK_MGR60_READ_CLOCKS_FROM_DENTIST,
	CLK_MGR60_UPDATE_HARDMIN_PPCLK,
	CLK_MGR60_UPDATE_HARDMIN_PPCLK_OPTIMIZED,
	CLK_MGR60_UPDATE_DEEP_SLEEP_DCFCLK,
	CLK_MGR60_INDICATE_PSTATE_STATUS,
	CLK_MGR60_UPDATE_DPPCLK_DTO,
	CLK_MGR60_UPDATE_DTBCLK_DTO,
	CLK_MGR60_UPDATE_DENTIST,
	CLK_MGR60_UPDATE_PSR_WAIT_LOOP,
	CLK_MGR60_UPDATE_STUTTER_EFFICIENCY,
	CLK_MGR60_UPDATE_UTM_QOS_REQUEST
};

struct dcn60_clk_mgr_block_sequence {
	union dcn60_clk_mgr_block_sequence_params params;
	enum dcn60_clk_mgr_block_sequence_func func;
};

struct dcn60_update_action {
	/** clk_mgr state needs updating */
	bool update;
	/** SMU message required */
	bool send_message;
};

struct dcn60_enablement_action {
	/** feature transitioning to enabled */
	bool enable;
	/** feature transitioning to disabled */
	bool disable;
	/** SMU message required for this transition */
	bool send_message;
};

/**
 * struct dcn60_bandwidth_clocks_update_action - captures what clock and
 * p-state changes are needed for a bandwidth update, separating the action
 * logic from block sequence construction and state mutation.
 */
struct dcn60_bandwidth_clocks_update_action {
	struct dcn60_update_action dcfclk;
	struct dcn60_update_action deep_sleep_dcfclk;
	struct dcn60_update_action socclk;
	struct dcn60_update_action stutter;
	struct dcn60_update_action utm_qos;
	struct dcn60_enablement_action uclk_pstate;
	struct dcn60_enablement_action fclk_pstate;
	struct dcn60_enablement_action fams;
	struct dcn60_enablement_action alt_ch;
};

struct dcn60_clk_mgr {
	struct clk_mgr_internal base;
	struct dcn60_clk_mgr_block_sequence block_sequence[DCN401_CLK_MGR_MAX_SEQUENCE_SIZE];
#ifdef CONFIG_DRM_AMD_DC_FP
	struct utm_qos_model utm_qos_model;
	struct utm_qos_model_dchub_v3 dchub_v3;
#endif
	unsigned int num_block_sequence_steps;
};

void dcn60_init_clocks(struct clk_mgr *clk_mgr_base);

struct block_sequence_state;

void dcn60_build_clock_update_for_bls(
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		bool safe_to_lower,
		struct block_sequence_state *seq_state);

struct clk_mgr_internal *dcn60_clk_mgr_construct(struct dc_context *ctx,
		struct dccg *dccg);

void dcn60_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr);

#endif /* __DCN60_CLK_MGR_H_ */
