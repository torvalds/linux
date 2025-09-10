// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DCN401_CLK_MGR_H_
#define __DCN401_CLK_MGR_H_

#define DCN401_CLK_MGR_MAX_SEQUENCE_SIZE 30

union dcn401_clk_mgr_block_sequence_params {
	struct {
		/* inputs */
		uint32_t num_displays;
	} update_num_displays_params;
	struct {
		/* inputs */
		uint32_t ppclk;
		uint16_t freq_mhz;
		/* outputs */
		uint32_t *response;
	} update_hardmin_params;
	struct {
		/* inputs */
		uint32_t ppclk;
		int freq_khz;
		/* outputs */
		uint32_t *response;
	} update_hardmin_optimized_params;
	struct {
		/* inputs */
		uint16_t uclk_mhz;
		uint16_t fclk_mhz;
	} update_idle_hardmin_params;
	struct {
		/* inputs */
		uint16_t freq_mhz;
	} update_deep_sleep_dcfclk_params;
	struct {
		/* inputs */
		bool support;
	} update_pstate_support_params;
	struct {
		/* inputs */
		unsigned int num_ways;
	} update_cab_for_uclk_params;
	struct {
		/* inputs */
		bool enable;
	} update_wait_for_dmub_ack_params;
	struct {
		/* inputs */
		bool mod_drr_for_pstate;
	} indicate_drr_status_params;
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
};

enum dcn401_clk_mgr_block_sequence_func {
	CLK_MGR401_READ_CLOCKS_FROM_DENTIST,
	CLK_MGR401_UPDATE_NUM_DISPLAYS,
	CLK_MGR401_UPDATE_HARDMIN_PPCLK,
	CLK_MGR401_UPDATE_HARDMIN_PPCLK_OPTIMIZED,
	CLK_MGR401_UPDATE_ACTIVE_HARDMINS,
	CLK_MGR401_UPDATE_IDLE_HARDMINS,
	CLK_MGR401_UPDATE_DEEP_SLEEP_DCFCLK,
	CLK_MGR401_UPDATE_FCLK_PSTATE_SUPPORT,
	CLK_MGR401_UPDATE_UCLK_PSTATE_SUPPORT,
	CLK_MGR401_UPDATE_CAB_FOR_UCLK,
	CLK_MGR401_UPDATE_WAIT_FOR_DMUB_ACK,
	CLK_MGR401_INDICATE_DRR_STATUS,
	CLK_MGR401_UPDATE_DPPCLK_DTO,
	CLK_MGR401_UPDATE_DTBCLK_DTO,
	CLK_MGR401_UPDATE_DENTIST,
	CLK_MGR401_UPDATE_PSR_WAIT_LOOP,
	CLK_MGR401_UPDATE_SUBVP_HARDMINS,
};

struct dcn401_clk_mgr_block_sequence {
	union dcn401_clk_mgr_block_sequence_params params;
	enum dcn401_clk_mgr_block_sequence_func func;
};

struct dcn401_clk_mgr {
	struct clk_mgr_internal base;

	struct dcn401_clk_mgr_block_sequence block_sequence[DCN401_CLK_MGR_MAX_SEQUENCE_SIZE];
};

void dcn401_init_clocks(struct clk_mgr *clk_mgr_base);
bool dcn401_is_dc_mode_present(struct clk_mgr *clk_mgr_base);

struct clk_mgr_internal *dcn401_clk_mgr_construct(struct dc_context *ctx,
		struct dccg *dccg);

void dcn401_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr);

unsigned int dcn401_get_max_clock_khz(struct clk_mgr *clk_mgr_base, enum clk_type clk_type);

#endif /* __DCN401_CLK_MGR_H_ */
