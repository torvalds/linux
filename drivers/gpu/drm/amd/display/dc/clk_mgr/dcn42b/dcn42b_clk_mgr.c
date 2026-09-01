/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dcn42b_clk_mgr.h"

#include "dccg.h"
#include "clk_mgr_internal.h"

// For dce12_get_dp_ref_freq_khz
#include "dce100/dce_clk_mgr.h"

// For dcn20_update_clocks_update_dpp_dto
#include "dcn20/dcn20_clk_mgr.h"

#include "reg_helper.h"
#include "core_types.h"
#include "dcn42/dcn42_smu.h"
#include "dcn42/dcn42_clk_mgr.h"
#include "dm_helpers.h"

/* TODO: remove this include once we ported over remaining clk mgr functions*/
#include "dcn30/dcn30_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"
#include "dcn35/dcn35_clk_mgr.h"

#include "dc_dmub_srv.h"
#include "link_service.h"
#include "logger_types.h"

#include "clk/clk_15_0_5_offset.h"
#include "clk/clk_15_0_5_sh_mask.h"
#include "dcn/dcn_4_2_1_offset.h"
#include "dcn/dcn_4_2_1_sh_mask.h"

#define DCN_BASE__INST0_SEG0                       0x00000012
#define DCN_BASE__INST0_SEG1                       0x000000C0


#undef DC_LOGGER
#define DC_LOGGER \
	dc_logger
#define DC_LOGGER_INIT(logger) \
	struct dal_logger *dc_logger = logger


#define mmCLK5_CLK_TICK_CNT_CONFIG_REG                  0x1B229
#define mmCLK5_CLK0_CURRENT_CNT                         0x1B22B //DISPCLK
#define mmCLK5_CLK1_CURRENT_CNT                         0x1B22C //DPPCLK
#define mmCLK5_CLK2_CURRENT_CNT                         0x1B22D //DPREFCLK
#define mmCLK5_CLK3_CURRENT_CNT                         0x1B22E //DCFCLK

#define mmCLK5_CLK0_DS_CNTL                             0x1B204
#define mmCLK5_CLK1_DS_CNTL                             0x1B20C
#define mmCLK5_CLK2_DS_CNTL                             0x1B214
#define mmCLK5_CLK3_DS_CNTL                             0x1B21C

#define mmCLK5_CLK0_BYPASS_CNTL                         0x1B20A
#define mmCLK5_CLK1_BYPASS_CNTL                         0x1B212
#define mmCLK5_CLK2_BYPASS_CNTL                         0x1B21A
#define mmCLK5_CLK3_BYPASS_CNTL                         0x1B222

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define CLK_SR_DCN42B(reg_name)\
	.reg_name = mm ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn42b = {
	CLK_REG_LIST_DCN42B()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn42b = {
	CLK_COMMON_MASK_SH_LIST_DCN42B(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn42b = {
	CLK_COMMON_MASK_SH_LIST_DCN42B(_MASK)
};



#define TO_CLK_MGR_DCN42B(clk_mgr_int)\
	container_of(clk_mgr_int, struct clk_mgr_dcn42, base)

static void dcn42b_dump_clk_registers_internal(struct dcn42b_clk_internal *internal, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	REG_GET(CLK5_CLK_TICK_CNT_CONFIG_REG, TIMER_THRESHOLD, &internal->CLK5_CLK_TICK_CNT__TIMER_THRESHOLD);

	// read dcf deep sleep divider
	internal->CLK5_CLK0_DS_CNTL = REG_READ(CLK5_CLK0_DS_CNTL);
	internal->CLK5_CLK3_DS_CNTL = REG_READ(CLK5_CLK3_DS_CNTL);
	// read dispclk
	internal->CLK5_CLK0_CURRENT_CNT = dcn42b_get_clock_freq_from_clkip(clk_mgr_base, clock_type_dispclk);
	internal->CLK5_CLK0_BYPASS_CNTL = REG_READ(CLK5_CLK0_BYPASS_CNTL);
	// read dppclk
	internal->CLK5_CLK1_CURRENT_CNT = dcn42b_get_clock_freq_from_clkip(clk_mgr_base, clock_type_dppclk);
	internal->CLK5_CLK1_BYPASS_CNTL = REG_READ(CLK5_CLK1_BYPASS_CNTL);
	// read dprefclk
	internal->CLK5_CLK2_CURRENT_CNT = dcn42b_get_clock_freq_from_clkip(clk_mgr_base, clock_type_dprefclk);
	internal->CLK5_CLK2_BYPASS_CNTL = REG_READ(CLK5_CLK2_BYPASS_CNTL);
	// read dcfclk
	internal->CLK5_CLK3_CURRENT_CNT = dcn42b_get_clock_freq_from_clkip(clk_mgr_base, clock_type_dcfclk);
	internal->CLK5_CLK3_BYPASS_CNTL = REG_READ(CLK5_CLK3_BYPASS_CNTL);
	/* read dtbclk - DTBCLK tied off in DCN42B
	* internal->CLK5_CLK4_CURRENT_CNT = REG_READ(CLK5_CLK4_CURRENT_CNT) / ratio;
	* internal->CLK5_CLK4_BYPASS_CNTL = REG_READ(CLK5_CLK4_BYPASS_CNTL);
	*/
}

static void dcn42b_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr_dcn42 *clk_mgr)
{
	struct dcn42b_clk_internal internal = {0};
	char *bypass_clks[5] = {"0x0 DFS", "0x1 REFCLK", "0x2 ERROR", "0x3 400 FCH", "0x4 600 FCH"};

	DC_LOGGER_INIT(clk_mgr->base.base.ctx->logger);
	(void)dc_logger;

	dcn42b_dump_clk_registers_internal(&internal, &clk_mgr->base.base);
	regs_and_bypass->timer_threshold = internal.CLK5_CLK_TICK_CNT__TIMER_THRESHOLD;
	regs_and_bypass->dcfclk = internal.CLK5_CLK3_CURRENT_CNT / 10;
	regs_and_bypass->dcf_deep_sleep_divider = internal.CLK5_CLK3_DS_CNTL / 10;
	regs_and_bypass->dcf_deep_sleep_allow = internal.CLK5_CLK3_DS_CNTL & 0x10; /*bit 4: CLK0_ALLOW_DS*/
	regs_and_bypass->dprefclk = internal.CLK5_CLK2_CURRENT_CNT / 10;
	regs_and_bypass->dispclk = internal.CLK5_CLK0_CURRENT_CNT / 10;
	regs_and_bypass->dppclk = internal.CLK5_CLK1_CURRENT_CNT / 10;
	/* regs_and_bypass->dtbclk = internal.CLK5_CLK4_CURRENT_CNT / 10; */ /* DTBCLK tied off in DCN42B */

	regs_and_bypass->dispclk_bypass = get_reg_field_value(internal.CLK5_CLK0_BYPASS_CNTL, CLK5_CLK0_BYPASS_CNTL, CLK0_BYPASS_SEL);
	regs_and_bypass->dppclk_bypass = get_reg_field_value(internal.CLK5_CLK1_BYPASS_CNTL, CLK5_CLK1_BYPASS_CNTL, CLK1_BYPASS_SEL);
	regs_and_bypass->dprefclk_bypass = get_reg_field_value(internal.CLK5_CLK2_BYPASS_CNTL, CLK5_CLK2_BYPASS_CNTL, CLK2_BYPASS_SEL);
	regs_and_bypass->dcfclk_bypass = get_reg_field_value(internal.CLK5_CLK3_BYPASS_CNTL, CLK5_CLK3_BYPASS_CNTL, CLK3_BYPASS_SEL);

	if (clk_mgr->base.base.ctx->dc->debug.pstate_enabled) {
		DC_LOG_SMU("clk_type,clk_value,deepsleep_cntl,deepsleep_allow,bypass\n");

		DC_LOG_SMU("dcfclk,%d,%d,%d,%s\n",
				   regs_and_bypass->dcfclk,
				   regs_and_bypass->dcf_deep_sleep_divider,
				   regs_and_bypass->dcf_deep_sleep_allow,
				   bypass_clks[(int) regs_and_bypass->dcfclk_bypass]);

		DC_LOG_SMU("dprefclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dprefclk,
			bypass_clks[(int) regs_and_bypass->dprefclk_bypass]);

		DC_LOG_SMU("dispclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dispclk,
			bypass_clks[(int) regs_and_bypass->dispclk_bypass]);

		//split
		DC_LOG_SMU("SPLIT\n");

		// REGISTER VALUES
		DC_LOG_SMU("reg_name,value,clk_type\n");

		DC_LOG_SMU("CLK5_CLK3_CURRENT_CNT,%d,dcfclk\n",
				internal.CLK5_CLK3_CURRENT_CNT);

		DC_LOG_SMU("CLK5_CLK3_DS_CNTL,%d,dcf_deep_sleep_divider\n",
					internal.CLK5_CLK3_DS_CNTL);

		DC_LOG_SMU("CLK5_CLK3_ALLOW_DS,%d,dcf_deep_sleep_allow\n",
					(internal.CLK5_CLK3_DS_CNTL & 0x10));

		DC_LOG_SMU("CLK5_CLK2_CURRENT_CNT,%d,dprefclk\n",
					internal.CLK5_CLK2_CURRENT_CNT);

		DC_LOG_SMU("CLK5_CLK0_CURRENT_CNT,%d,dispclk\n",
					internal.CLK5_CLK0_CURRENT_CNT);

		DC_LOG_SMU("CLK5_CLK1_CURRENT_CNT,%d,dppclk\n",
					internal.CLK5_CLK1_CURRENT_CNT);

		DC_LOG_SMU("CLK5_CLK3_BYPASS_CNTL,%d,dcfclk_bypass\n",
					internal.CLK5_CLK3_BYPASS_CNTL);

		DC_LOG_SMU("CLK5_CLK2_BYPASS_CNTL,%d,dprefclk_bypass\n",
					internal.CLK5_CLK2_BYPASS_CNTL);

		DC_LOG_SMU("CLK5_CLK0_BYPASS_CNTL,%d,dispclk_bypass\n",
					internal.CLK5_CLK0_BYPASS_CNTL);

		DC_LOG_SMU("CLK5_CLK1_BYPASS_CNTL,%d,dppclk_bypass\n",
					internal.CLK5_CLK1_BYPASS_CNTL);
	}
}

static void init_clk_states(struct clk_mgr *clk_mgr)
{
	/*
	 * DTBCLK is tied off in DCN42B - no save/restore needed
	 * uint32_t ref_dtbclk = clk_mgr->clks.ref_dtbclk_khz;
	 */

	memset(&(clk_mgr->clks), 0, sizeof(struct dc_clocks));
	clk_mgr->clks.dtbclk_en = false; /* DTBCLK tied off in DCN42B */


	clk_mgr->clks.ref_dtbclk_khz = 0;
	clk_mgr->clks.p_state_change_support = true;
	clk_mgr->clks.prev_p_state_change_support = true;
	clk_mgr->clks.pwr_state = DCN_PWR_STATE_UNKNOWN;
	clk_mgr->clks.zstate_support = DCN_ZSTATE_SUPPORT_UNKNOWN;
}

void dcn42b_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr_int = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_mgr_dcn42 *clk_mgr = TO_CLK_MGR_DCN42B(clk_mgr_int);

	DC_LOGGER_INIT(clk_mgr_base->ctx->logger);
	(void)dc_logger;

	init_clk_states(clk_mgr_base);

	// to adjust dp_dto reference clock if ssc is enable otherwise to apply dprefclk
	if (dcn42_is_spll_ssc_enabled(clk_mgr_base))
		clk_mgr_base->dp_dto_source_clock_in_khz =
			dce_adjust_dp_ref_freq_for_ss(clk_mgr_int, clk_mgr_base->dprefclk_khz);
	else
		clk_mgr_base->dp_dto_source_clock_in_khz = clk_mgr_base->dprefclk_khz;

	DC_LOG_SMU("dp_dto_source_clock %d, dprefclk %d\n", clk_mgr_base->dp_dto_source_clock_in_khz, clk_mgr_base->dprefclk_khz);
	dcn42b_dump_clk_registers(&clk_mgr_base->boot_snapshot, clk_mgr);
}

static struct clk_bw_params dcn42b_bw_params = {
	.vram_type = Ddr4MemType,
	.num_channels = 1,
	.clk_table = {
		.num_entries = 4,
	},

};

static struct wm_table ddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.72,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
	}
};

static struct wm_table lpddr5_wm_table = {
	.entries = {
		{
			.wm_inst = WM_A,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_B,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_C,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
		{
			.wm_inst = WM_D,
			.wm_type = WM_TYPE_PSTATE_CHG,
			.pstate_latency_us = 11.65333,
			.sr_exit_time_us = 28.0,
			.sr_enter_plus_exit_time_us = 30.0,
			.valid = true,
		},
	}
};


static void dcn42b_read_ss_info_from_lut(struct clk_mgr_internal *clk_mgr)
{
	uint32_t clock_source;

	clock_source = (REG_READ(CLK5_CLK2_BYPASS_CNTL) & CLK5_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK);
	// If it's DFS mode, clock_source is 0.
	if (dcn42_is_spll_ssc_enabled(&clk_mgr->base) && (clock_source < ARRAY_SIZE(dcn42_ss_info_table.ss_percentage))) {
		clk_mgr->dprefclk_ss_percentage = dcn42_ss_info_table.ss_percentage[clock_source];

		if (clk_mgr->dprefclk_ss_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = dcn42_ss_info_table.ss_divider;
		}
	}
}

uint32_t dcn42b_get_clock_freq_from_clkip(struct clk_mgr *clk_mgr_base, enum clock_type clock)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint64_t clock_freq_mhz = 0;
	uint32_t timer_threshold = 0;

	// always safer to read the timer threshold instead of using cached value
	REG_GET(CLK5_CLK_TICK_CNT_CONFIG_REG, TIMER_THRESHOLD, &timer_threshold);

	if (timer_threshold == 0) {
		BREAK_TO_DEBUGGER();
		return 0;
	}

	switch (clock) {
	case clock_type_dispclk:
		clock_freq_mhz = REG_READ(CLK5_CLK0_CURRENT_CNT);
		break;
	case clock_type_dppclk:
		clock_freq_mhz = REG_READ(CLK5_CLK1_CURRENT_CNT);
		break;
	case clock_type_dprefclk:
		clock_freq_mhz = REG_READ(CLK5_CLK2_CURRENT_CNT);
		break;
	case clock_type_dcfclk:
		clock_freq_mhz = REG_READ(CLK5_CLK3_CURRENT_CNT);
		break;
	case clock_type_dtbclk:
		/* DTBCLK tied off in DCN42B - CLK5_CLK4 register doesn't exist.
		 * Should never be called since dtbclk_en is always false.
		 */
		ASSERT(false);
		clock_freq_mhz = 0;
		break;
	default:
		break;
	}

	clock_freq_mhz *= DCN42_CLKIP_REFCLK;
	clock_freq_mhz = div_u64(clock_freq_mhz, timer_threshold);

	// there are no DCN clocks over 0xFFFFFFFF MHz
	ASSERT(clock_freq_mhz <= 0xFFFFFFFF);

	return (uint32_t)clock_freq_mhz;
}

/* dcn42b_get_dispclk_from_dentist removed: reuse dcn42_get_dispclk_from_dentist.
 * DENTIST_DISPCLK_CNTL is a DCN register with the same offset on both dcn42 and dcn42b.
 */

static struct clk_mgr_funcs dcn42b_funcs = {
	.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
	.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
	.update_clocks = dcn42_update_clocks,
	.init_clocks = dcn42b_init_clocks,
	.enable_pme_wa = dcn42_enable_pme_wa,
	.are_clock_states_equal = dcn42_are_clock_states_equal,
	.notify_wm_ranges = NULL,
	.set_low_power_state = dcn42_set_low_power_state,
	.exit_low_power_state = dcn42_exit_low_power_state,
	.get_max_clock_khz = dcn42_get_max_clock_khz,
	.get_dispclk_from_dentist = dcn42_get_dispclk_from_dentist,
	.is_smu_present = dcn42_is_smu_present,
};

typedef struct {
	uint32_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
	uint32_t DispClocks[NUM_DISPCLK_DPM_LEVELS];
	uint32_t DppClocks[NUM_DPPCLK_DPM_LEVELS];
	uint32_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
	uint32_t VPEClocks[NUM_VPE_DPM_LEVELS];
	uint32_t FclkClocks_Freq[NUM_FCLK_DPM_LEVELS];
	uint32_t FclkClocks_Voltage[NUM_FCLK_DPM_LEVELS];
	uint32_t SocVoltage[NUM_SOC_VOLTAGE_LEVELS];
	MemPstateTable_t MemPstateTable[NUM_MEM_PSTATE_LEVELS];

	uint8_t  NumDcfClkLevelsEnabled;
	uint8_t  NumDispClkLevelsEnabled; //Applies to both Dispclk and Dppclk
	uint8_t  NumSocClkLevelsEnabled;
	uint8_t  VpeClkLevelsEnabled;

	uint8_t  NumMemPstatesEnabled;
	uint8_t  NumFclkLevelsEnabled;
	uint8_t  Spare1;
	uint8_t  Spare2;

	uint8_t  Spare3;
	uint8_t  Spare4;
	uint8_t  Spare5[2];

	uint32_t MinGfxClk;
	uint32_t MaxGfxClk;

	uint32_t Spare6[8];
	uint32_t Spare7[8];
	uint32_t Spare8[8];
	uint32_t Spare9[8];
} DpmClocks_t_dcn42b;
struct dcn42b_smu_dpm_clks {
	DpmClocks_t_dcn42b *dpm_clks;
	union large_integer mc_address;
};

static void dcn42b_get_dpm_table_from_smu(struct clk_mgr_internal *clk_mgr,
		struct dcn42b_smu_dpm_clks *smu_dpm_clks)
{
	DpmClocks_t_dcn42b *table = smu_dpm_clks->dpm_clks;

	if (!clk_mgr->smu_ver)
		return;

	if (!table || smu_dpm_clks->mc_address.quad_part == 0)
		return;

	memset(table, 0, sizeof(*table));

	dcn42_smu_set_dram_addr_high(clk_mgr,
			smu_dpm_clks->mc_address.high_part);
	dcn42_smu_set_dram_addr_low(clk_mgr,
			smu_dpm_clks->mc_address.low_part);
	dcn42_smu_transfer_dpm_table_smu_2_dram(clk_mgr);
}

static void dcn42b_get_smu_clocks(struct clk_mgr_internal *clk_mgr_int)
{
	struct clk_mgr *clk_mgr_base = &clk_mgr_int->base;
	struct dcn42b_smu_dpm_clks smu_dpm_clks = { 0 };

	DC_LOGGER_INIT(clk_mgr_base->ctx->logger);
	(void)dc_logger;

	smu_dpm_clks.dpm_clks = (DpmClocks_t_dcn42b *)dm_helpers_allocate_gpu_mem(
				clk_mgr_base->ctx,
				DC_MEM_ALLOC_TYPE_GART,
				sizeof(DpmClocks_t_dcn42b),
				&smu_dpm_clks.mc_address.quad_part);

	ASSERT(smu_dpm_clks.dpm_clks);
	if (clk_mgr_base->ctx->dc->debug.pstate_enabled && smu_dpm_clks.mc_address.quad_part != 0) {
		int i;
		DpmClocks_t_dcn42b *dpm_clks = smu_dpm_clks.dpm_clks;

		dcn42b_get_dpm_table_from_smu(clk_mgr_int, &smu_dpm_clks);
		DC_LOG_SMU("NumDcfClkLevelsEnabled: %d\n"
				   "NumDispClkLevelsEnabled: %d\n"
				   "NumSocClkLevelsEnabled: %d\n"
				   "VpeClkLevelsEnabled: %d\n"
				   "FClkLevelsEnabled: %d\n"
				   "NumMemPstatesEnabled: %d\n"
				   "MinGfxClk: %d\n"
				   "MaxGfxClk: %d\n",
				   dpm_clks->NumDcfClkLevelsEnabled,
				   dpm_clks->NumDispClkLevelsEnabled,
				   dpm_clks->NumSocClkLevelsEnabled,
				   dpm_clks->VpeClkLevelsEnabled,
				   dpm_clks->NumFclkLevelsEnabled,
				   dpm_clks->NumMemPstatesEnabled,
				   dpm_clks->MinGfxClk,
				   dpm_clks->MaxGfxClk);

		for (i = 0; i < NUM_DCFCLK_DPM_LEVELS; i++)
			DC_LOG_SMU("dpm_clks->DcfClocks[%d] = %d\n", i, dpm_clks->DcfClocks[i]);
		for (i = 0; i < NUM_DISPCLK_DPM_LEVELS; i++)
			DC_LOG_SMU("dpm_clks->DispClocks[%d] = %d\n", i, dpm_clks->DispClocks[i]);
		for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
			DC_LOG_SMU("dpm_clks->SocClocks[%d] = %d\n", i, dpm_clks->SocClocks[i]);
		for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++) {
			DC_LOG_SMU("dpm_clks->FclkClocks_Freq[%d] = %d\n", i, dpm_clks->FclkClocks_Freq[i]);
			DC_LOG_SMU("dpm_clks->FclkClocks_Voltage[%d] = %d\n", i, dpm_clks->FclkClocks_Voltage[i]);
		}
		for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
			DC_LOG_SMU("dpm_clks->SocVoltage[%d] = %d\n", i, dpm_clks->SocVoltage[i]);

		for (i = 0; i < NUM_MEM_PSTATE_LEVELS; i++)
			DC_LOG_SMU("dpm_clks.MemPstateTable[%d].UClk = %d MemClk = %d Voltage = %d\n",
					i, dpm_clks->MemPstateTable[i].UClk, dpm_clks->MemPstateTable[i].MemClk, dpm_clks->MemPstateTable[i].Voltage);

		if (clk_mgr_base->ctx->dc_bios->integrated_info && clk_mgr_base->ctx->dc->config.use_default_clock_table == false) {
			/* DCFCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
					dpm_clks->DcfClocks,
					dpm_clks->NumDcfClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels = dpm_clks->NumDcfClkLevelsEnabled;

			/* SOCCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					dpm_clks->SocClocks,
					dpm_clks->NumSocClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_socclk_levels = dpm_clks->NumSocClkLevelsEnabled;

			/* DISPCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
					dpm_clks->DispClocks,
					dpm_clks->NumDispClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels = dpm_clks->NumDispClkLevelsEnabled;

			/* DPPCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].dppclk_mhz,
					dpm_clks->DppClocks,
					dpm_clks->NumDispClkLevelsEnabled);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dppclk_levels = dpm_clks->NumDispClkLevelsEnabled;

			/* FCLK */
			dcn42_init_single_clock(&clk_mgr_base->bw_params->clk_table.entries[0].fclk_mhz,
					dpm_clks->FclkClocks_Freq,
					NUM_FCLK_DPM_LEVELS);
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_fclk_levels = dpm_clks->NumFclkLevelsEnabled;
			clk_mgr_base->bw_params->clk_table.num_entries = dpm_clks->NumFclkLevelsEnabled;

			/* Memory Pstate table is in reverse order for dcn42. Proper way to map this is for pmfw to provide an fclk indexed uclk key since we consume fclk matched pairs.*/
			ASSERT(dpm_clks->NumMemPstatesEnabled <= NUM_MEM_PSTATE_LEVELS);
			if (dpm_clks->NumMemPstatesEnabled > NUM_MEM_PSTATE_LEVELS)
				dpm_clks->NumMemPstatesEnabled = NUM_MEM_PSTATE_LEVELS;
			for (i = 0; i < dpm_clks->NumDcfClkLevelsEnabled; i++) {
				if (i < dpm_clks->NumMemPstatesEnabled) {
					clk_mgr_base->bw_params->clk_table.entries[dpm_clks->NumMemPstatesEnabled - 1 - i].memclk_mhz = dpm_clks->MemPstateTable[i].MemClk;
					clk_mgr_base->bw_params->clk_table.entries[dpm_clks->NumMemPstatesEnabled - 1 - i].wck_ratio = dcn42_convert_wck_ratio(dpm_clks->MemPstateTable[i].WckRatio);
				} else {
					clk_mgr_base->bw_params->clk_table.entries[i].memclk_mhz = dpm_clks->MemPstateTable[0].MemClk;
					clk_mgr_base->bw_params->clk_table.entries[i].wck_ratio = dcn42_convert_wck_ratio(dpm_clks->MemPstateTable[0].WckRatio);
				}
			}

			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_memclk_levels = dpm_clks->NumDcfClkLevelsEnabled;

			/* DTBCLK tied off in DCN42B */
			clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz = 0;
			clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels = 0;
		}
	}
	if (smu_dpm_clks.dpm_clks && smu_dpm_clks.mc_address.quad_part != 0)
		dm_helpers_free_gpu_mem(clk_mgr_base->ctx, DC_MEM_ALLOC_TYPE_GART,
				smu_dpm_clks.dpm_clks);
}

void dcn42b_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_dcn42 *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	clk_mgr->base.base.ctx = ctx;
	clk_mgr->base.base.funcs = &dcn42b_funcs;
	clk_mgr->base.regs = &clk_mgr_regs_dcn42b;
	clk_mgr->base.clk_mgr_shift = &clk_mgr_shift_dcn42b;
	clk_mgr->base.clk_mgr_mask = &clk_mgr_mask_dcn42b;

	clk_mgr->base.pp_smu = pp_smu;

	clk_mgr->base.dccg = dccg;
	clk_mgr->base.dfs_bypass_disp_clk = 0;

	clk_mgr->base.dprefclk_ss_percentage = 0;
	clk_mgr->base.dprefclk_ss_divider = 1000;
	clk_mgr->base.ss_on_dprefclk = false;
	clk_mgr->base.dfs_ref_freq_khz = 48000; /*sync with pmfw*/
	clk_mgr->base.base.clks.ref_dtbclk_khz = 0;

	/* Changed from DCN3.2_clock_frequency doc to match
	 * dcn32_dump_clk_registers from 4 * dentist_vco_freq_khz /
	 * dprefclk DID divider
	 */
	clk_mgr->base.base.dprefclk_khz = 600000;

		clk_mgr->base.smu_present = false;
		clk_mgr->base.smu_ver = dcn42_smu_get_pmfw_version(&clk_mgr->base);
		if (clk_mgr->base.smu_ver && clk_mgr->base.smu_ver != -1)
			clk_mgr->base.smu_present = true;

		if (ctx->dc_bios->integrated_info) {
			clk_mgr->base.base.dentist_vco_freq_khz = ctx->dc_bios->integrated_info->dentist_vco_freq;

			if (ctx->dc_bios->integrated_info->memory_type == LpDdr5MemType)
				dcn42b_bw_params.wm_table = lpddr5_wm_table;
			else
				dcn42b_bw_params.wm_table = ddr5_wm_table;
			dcn42b_bw_params.vram_type = ctx->dc_bios->integrated_info->memory_type;
			dcn42b_bw_params.dram_channel_width_bytes = ctx->dc_bios->integrated_info->memory_type == 0x22 ? 8 : 4;
			dcn42b_bw_params.num_channels = ctx->dc_bios->integrated_info->ma_channel_number ? ctx->dc_bios->integrated_info->ma_channel_number : 2;
			if (clk_mgr->base.smu_present)
				clk_mgr->base.base.dprefclk_khz = dcn42_smu_get_dprefclk(&clk_mgr->base);

		}
		/* in case we don't get a value from the BIOS, use default */
		if (clk_mgr->base.base.dentist_vco_freq_khz == 0)
			clk_mgr->base.base.dentist_vco_freq_khz = 3000000; /* 3000MHz */

		/* Saved clocks configured at boot for debug purposes */
		dcn42b_dump_clk_registers(&clk_mgr->base.base.boot_snapshot, clk_mgr);

	dce_clock_read_ss_info(&clk_mgr->base);
	/*when clk src is from FCH, it could have ss, same clock src as DPREF clk*/

	dcn42b_read_ss_info_from_lut(&clk_mgr->base);

	clk_mgr->base.base.bw_params = &dcn42b_bw_params;
	if (clk_mgr->base.smu_present)
		dcn42b_get_smu_clocks(&clk_mgr->base);
}
