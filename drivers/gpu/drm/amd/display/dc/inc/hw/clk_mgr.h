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

#define DCN_MINIMUM_DISPCLK_Khz 100000
#define DCN_MINIMUM_DPPCLK_Khz 100000

#ifdef CONFIG_DRM_AMD_DC_DCN2_1
/* Constants */
#define DDR4_DRAM_WIDTH   64
#define WM_A 0
#define WM_B 1
#define WM_C 2
#define WM_D 3
#define WM_SET_COUNT 4
#endif

#define DCN_MINIMUM_DISPCLK_Khz 100000
#define DCN_MINIMUM_DPPCLK_Khz 100000

#ifdef CONFIG_DRM_AMD_DC_DCN2_1
/* Will these bw structures be ASIC specific? */

#define MAX_NUM_DPM_LVL		4
#define WM_SET_COUNT 		4


struct clk_limit_table_entry {
	unsigned int voltage; /* milivolts withh 2 fractional bits */
	unsigned int dcfclk_mhz;
	unsigned int fclk_mhz;
	unsigned int memclk_mhz;
	unsigned int socclk_mhz;
};

/* This table is contiguous */
struct clk_limit_table {
	struct clk_limit_table_entry entries[MAX_NUM_DPM_LVL];
	unsigned int num_entries;
};

struct wm_range_table_entry {
	unsigned int wm_inst;
	unsigned int wm_type;
	double pstate_latency_us;
	bool valid;
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
	struct wm_range_table_entry entries[WM_SET_COUNT];
};

struct clk_bw_params {
	unsigned int vram_type;
	unsigned int num_channels;
	struct clk_limit_table clk_table;
	struct wm_table wm_table;
};
#endif
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

	void (*init_clocks)(struct clk_mgr *clk_mgr);

	void (*enable_pme_wa) (struct clk_mgr *clk_mgr);
	void (*get_clock)(struct clk_mgr *clk_mgr,
			struct dc_state *context,
			enum dc_clock_type clock_type,
			struct dc_clock_config *clock_cfg);
};

struct clk_mgr {
	struct dc_context *ctx;
	struct clk_mgr_funcs *funcs;
	struct dc_clocks clks;
	int dprefclk_khz; // Used by program pixel clock in clock source funcs, need to figureout where this goes
#ifdef CONFIG_DRM_AMD_DC_DCN2_1
	struct clk_bw_params *bw_params;
#endif
};

/* forward declarations */
struct dccg;

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg);

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr);

#endif /* __DAL_CLK_MGR_H__ */
