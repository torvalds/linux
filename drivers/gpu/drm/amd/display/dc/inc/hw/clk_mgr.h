/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_CLK_MGR_H__
#define __DAL_CLK_MGR_H__

#include "dc.h"
#include "dm_pp_smu.h"

/* Constants */
#define DDR4_DRAM_WIDTH   64
#define WM_A 0
#define WM_B 1
#define WM_C 2
#define WM_D 3
#define WM_SET_COUNT 4
#define WM_1A 2
#define WM_1B 3

#define DCN_MINIMUM_DISPCLK_Khz 100000
#define DCN_MINIMUM_DPPCLK_Khz 100000

struct dcn3_clk_internal {
	int dummy;
//	TODO:
	uint32_t CLK1_CLK0_CURRENT_CNT; //dispclk
	uint32_t CLK1_CLK1_CURRENT_CNT; //dppclk
	uint32_t CLK1_CLK2_CURRENT_CNT; //dprefclk
	uint32_t CLK1_CLK3_CURRENT_CNT; //dcfclk
	uint32_t CLK1_CLK4_CURRENT_CNT;
	uint32_t CLK1_CLK3_DS_CNTL;	//dcf_deep_sleep_divider
	uint32_t CLK1_CLK3_ALLOW_DS;	//dcf_deep_sleep_allow

	uint32_t CLK1_CLK0_BYPASS_CNTL; //dispclk bypass
	uint32_t CLK1_CLK1_BYPASS_CNTL; //dppclk bypass
	uint32_t CLK1_CLK2_BYPASS_CNTL; //dprefclk bypass
	uint32_t CLK1_CLK3_BYPASS_CNTL; //dcfclk bypass

	uint32_t CLK4_CLK0_CURRENT_CNT; //fclk
};

struct dcn35_clk_internal {
	int dummy;
	uint32_t CLK1_CLK0_CURRENT_CNT; //dispclk
	uint32_t CLK1_CLK1_CURRENT_CNT; //dppclk
	uint32_t CLK1_CLK2_CURRENT_CNT; //dprefclk
	uint32_t CLK1_CLK3_CURRENT_CNT; //dcfclk
	uint32_t CLK1_CLK4_CURRENT_CNT; //dtbclk
	//uint32_t CLK1_CLK5_CURRENT_CNT; //dpiaclk
	//uint32_t CLK1_CLK6_CURRENT_CNT; //srdbgclk
	uint32_t CLK1_CLK3_DS_CNTL;	    //dcf_deep_sleep_divider
	uint32_t CLK1_CLK3_ALLOW_DS;	//dcf_deep_sleep_allow

	uint32_t CLK1_CLK0_BYPASS_CNTL; //dispclk bypass
	uint32_t CLK1_CLK1_BYPASS_CNTL; //dppclk bypass
	uint32_t CLK1_CLK2_BYPASS_CNTL; //dprefclk bypass
	uint32_t CLK1_CLK3_BYPASS_CNTL; //dcfclk bypass
	uint32_t CLK1_CLK4_BYPASS_CNTL; //dtbclk bypass
};

struct dcn301_clk_internal {
	int dummy;
	uint32_t CLK1_CLK0_CURRENT_CNT; //dispclk
	uint32_t CLK1_CLK1_CURRENT_CNT; //dppclk
	uint32_t CLK1_CLK2_CURRENT_CNT; //dprefclk
	uint32_t CLK1_CLK3_CURRENT_CNT; //dcfclk
	uint32_t CLK1_CLK3_DS_CNTL;	//dcf_deep_sleep_divider
	uint32_t CLK1_CLK3_ALLOW_DS;	//dcf_deep_sleep_allow

	uint32_t CLK1_CLK0_BYPASS_CNTL; //dispclk bypass
	uint32_t CLK1_CLK1_BYPASS_CNTL; //dppclk bypass
	uint32_t CLK1_CLK2_BYPASS_CNTL; //dprefclk bypass
	uint32_t CLK1_CLK3_BYPASS_CNTL; //dcfclk bypass
};

/* Will these bw structures be ASIC specific? */

#define MAX_NUM_DPM_LVL		8
#define WM_SET_COUNT 		4


struct clk_limit_table_entry {
	unsigned int voltage; /* milivolts withh 2 fractional bits */
	unsigned int dcfclk_mhz;
	unsigned int fclk_mhz;
	unsigned int memclk_mhz;
	unsigned int socclk_mhz;
	unsigned int dtbclk_mhz;
	unsigned int dispclk_mhz;
	unsigned int dppclk_mhz;
	unsigned int phyclk_mhz;
	unsigned int phyclk_d18_mhz;
	unsigned int wck_ratio;
};

struct clk_limit_num_entries {
	unsigned int num_dcfclk_levels;
	unsigned int num_fclk_levels;
	unsigned int num_memclk_levels;
	unsigned int num_socclk_levels;
	unsigned int num_dtbclk_levels;
	unsigned int num_dispclk_levels;
	unsigned int num_dppclk_levels;
	unsigned int num_phyclk_levels;
	unsigned int num_phyclk_d18_levels;
};

/* This table is contiguous */
struct clk_limit_table {
	struct clk_limit_table_entry entries[MAX_NUM_DPM_LVL];
	struct clk_limit_num_entries num_entries_per_clk;
	unsigned int num_entries; /* highest populated dpm level for back compatibility */
};

struct wm_range_table_entry {
	unsigned int wm_inst;
	unsigned int wm_type;
	double pstate_latency_us;
	double sr_exit_time_us;
	double sr_enter_plus_exit_time_us;
	bool valid;
};

struct nv_wm_range_entry {
	bool valid;

	struct {
		uint8_t wm_type;
		uint16_t min_dcfclk;
		uint16_t max_dcfclk;
		uint16_t min_uclk;
		uint16_t max_uclk;
	} pmfw_breakdown;

	struct {
		double pstate_latency_us;
		double sr_exit_time_us;
		double sr_enter_plus_exit_time_us;
		double fclk_change_latency_us;
	} dml_input;
};

struct clk_log_info {
	bool enabled;
	char *pBuf;
	unsigned int bufSize;
	unsigned int *sum_chars_printed;
};

struct clk_state_registers_and_bypass {
	uint32_t dcfclk;
	uint32_t dcf_deep_sleep_divider;
	uint32_t dcf_deep_sleep_allow;
	uint32_t dprefclk;
	uint32_t dispclk;
	uint32_t dppclk;
	uint32_t dtbclk;
	uint32_t fclk;

	uint32_t dppclk_bypass;
	uint32_t dcfclk_bypass;
	uint32_t dprefclk_bypass;
	uint32_t dispclk_bypass;
};

struct rv1_clk_internal {
	uint32_t CLK0_CLK8_CURRENT_CNT;  //dcfclk
	uint32_t CLK0_CLK8_DS_CNTL;		 //dcf_deep_sleep_divider
	uint32_t CLK0_CLK8_ALLOW_DS;	 //dcf_deep_sleep_allow
	uint32_t CLK0_CLK10_CURRENT_CNT; //dprefclk
	uint32_t CLK0_CLK11_CURRENT_CNT; //dispclk

	uint32_t CLK0_CLK8_BYPASS_CNTL;  //dcfclk bypass
	uint32_t CLK0_CLK10_BYPASS_CNTL; //dprefclk bypass
	uint32_t CLK0_CLK11_BYPASS_CNTL; //dispclk bypass
};

struct rn_clk_internal {
	uint32_t CLK1_CLK0_CURRENT_CNT; //dispclk
	uint32_t CLK1_CLK1_CURRENT_CNT; //dppclk
	uint32_t CLK1_CLK2_CURRENT_CNT; //dprefclk
	uint32_t CLK1_CLK3_CURRENT_CNT; //dcfclk
	uint32_t CLK1_CLK3_DS_CNTL;		//dcf_deep_sleep_divider
	uint32_t CLK1_CLK3_ALLOW_DS;	//dcf_deep_sleep_allow

	uint32_t CLK1_CLK0_BYPASS_CNTL; //dispclk bypass
	uint32_t CLK1_CLK1_BYPASS_CNTL; //dppclk bypass
	uint32_t CLK1_CLK2_BYPASS_CNTL; //dprefclk bypass
	uint32_t CLK1_CLK3_BYPASS_CNTL; //dcfclk bypass

};

/* For dtn logging and debugging */
struct clk_state_registers {
		uint32_t CLK0_CLK8_CURRENT_CNT;  //dcfclk
		uint32_t CLK0_CLK8_DS_CNTL;		 //dcf_deep_sleep_divider
		uint32_t CLK0_CLK8_ALLOW_DS;	 //dcf_deep_sleep_allow
		uint32_t CLK0_CLK10_CURRENT_CNT; //dprefclk
		uint32_t CLK0_CLK11_CURRENT_CNT; //dispclk
};

/* TODO: combine this with the above */
struct clk_bypass {
	uint32_t dcfclk_bypass;
	uint32_t dispclk_pypass;
	uint32_t dprefclk_bypass;
};
/*
 * This table is not contiguous, can have holes, each
 * entry correspond to one set of WM. For example if
 * we have 2 DPM and LPDDR, we will WM set A, B and
 * D occupied, C will be emptry.
 */
struct wm_table {
	union {
		struct nv_wm_range_entry nv_entries[WM_SET_COUNT];
		struct wm_range_table_entry entries[WM_SET_COUNT];
	};
};

struct dummy_pstate_entry {
	unsigned int dram_speed_mts;
	unsigned int dummy_pstate_latency_us;
};

struct clk_bw_params {
	unsigned int vram_type;
	unsigned int num_channels;
	unsigned int dram_channel_width_bytes;
	unsigned int dispclk_vco_khz;
	unsigned int dc_mode_softmax_memclk;
	unsigned int max_memclk_mhz;
	struct clk_limit_table clk_table;
	struct wm_table wm_table;
	struct dummy_pstate_entry dummy_pstate_table[4];
	struct clk_limit_table_entry dc_mode_limit;
};
/* Public interfaces */

struct clk_states {
	uint32_t dprefclk_khz;
};

struct clk_mgr_funcs {
	/*
	 * This function should set new clocks based on the input "safe_to_lower".
	 * If safe_to_lower == false, then only clocks which are to be increased
	 * should changed.
	 * If safe_to_lower == true, then only clocks which are to be decreased
	 * should be changed.
	 */
	void (*update_clocks)(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			bool safe_to_lower);

	int (*get_dp_ref_clk_frequency)(struct clk_mgr *clk_mgr);
	int (*get_dtb_ref_clk_frequency)(struct clk_mgr *clk_mgr);

	void (*set_low_power_state)(struct clk_mgr *clk_mgr);
	void (*exit_low_power_state)(struct clk_mgr *clk_mgr);
	bool (*is_ips_supported)(struct clk_mgr *clk_mgr);

	void (*init_clocks)(struct clk_mgr *clk_mgr);

	void (*dump_clk_registers)(struct clk_state_registers_and_bypass *regs_and_bypass,
			struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info);

	void (*enable_pme_wa) (struct clk_mgr *clk_mgr);
	void (*get_clock)(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg);

	bool (*are_clock_states_equal) (struct dc_clocks *a,
			struct dc_clocks *b);
	void (*notify_wm_ranges)(struct clk_mgr *clk_mgr);

	/* Notify clk_mgr of a change in link rate, update phyclk frequency if necessary */
	void (*notify_link_rate_change)(struct clk_mgr *clk_mgr, struct dc_link *link);
	/*
	 * Send message to PMFW to set hard min memclk frequency
	 * When current_mode = false, set DPM0
	 * When current_mode = true, set required clock for current mode
	 */
	void (*set_hard_min_memclk)(struct clk_mgr *clk_mgr, bool current_mode);

	int (*get_hard_min_memclk)(struct clk_mgr *clk_mgr);
	int (*get_hard_min_fclk)(struct clk_mgr *clk_mgr);

	/* Send message to PMFW to set hard max memclk frequency to highest DPM */
	void (*set_hard_max_memclk)(struct clk_mgr *clk_mgr);

	/* Custom set a memclk freq range*/
	void (*set_max_memclk)(struct clk_mgr *clk_mgr, unsigned int memclk_mhz);
	void (*set_min_memclk)(struct clk_mgr *clk_mgr, unsigned int memclk_mhz);

	/* Get current memclk states from PMFW, update relevant structures */
	void (*get_memclk_states_from_smu)(struct clk_mgr *clk_mgr);

	/* Get SMU present */
	bool (*is_smu_present)(struct clk_mgr *clk_mgr);

	int (*get_dispclk_from_dentist)(struct clk_mgr *clk_mgr_base);

};

struct clk_mgr {
	struct dc_context *ctx;
	struct clk_mgr_funcs *funcs;
	struct dc_clocks clks;
	bool psr_allow_active_cache;
	bool force_smu_not_present;
	bool dc_mode_softmax_enabled;
	int dprefclk_khz; // Used by program pixel clock in clock source funcs, need to figureout where this goes
	int dp_dto_source_clock_in_khz; // Used to program DP DTO with ss adjustment on DCN314
	int dentist_vco_freq_khz;
	struct clk_state_registers_and_bypass boot_snapshot;
	struct clk_bw_params *bw_params;
	struct pp_smu_wm_range_sets ranges;
};

/* forward declarations */
struct dccg;

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg);

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr);

void clk_mgr_exit_optimized_pwr_state(const struct dc *dc, struct clk_mgr *clk_mgr);

void clk_mgr_optimize_pwr_state(const struct dc *dc, struct clk_mgr *clk_mgr);

#endif /* __DAL_CLK_MGR_H__ */
