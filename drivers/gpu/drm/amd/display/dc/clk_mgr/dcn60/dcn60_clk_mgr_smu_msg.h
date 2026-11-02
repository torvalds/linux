// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DCN60_CLK_MGR_SMU_MSG_H_
#define __DCN60_CLK_MGR_SMU_MSG_H_

#include "os_types.h"
#include "core_types.h"
#include "dalsmc.h"

struct clk_mgr_internal;

/** Generic register overlay — four 32-bit C2PMSG argument registers. */
typedef struct {
	uint32_t Reg0;
	uint32_t Reg1;
	uint32_t Reg2;
	uint32_t Reg3;
} DALSMC_args_t;


/**
 * DALSMC_MSG_SetDispclkFreq - Set the DISPCLK frequency (MHz).
 * DALSMC_MSG_SetDppclkFreq  - Set the DPPCLK frequency (MHz).
 * DALSMC_MSG_SetDtbClk      - Set the DTBCLK frequency (MHz).
 *
 * Directly programs the target display-domain clock instead of requesting an
 * arbiter hard-min. PMFW returns the actual configured frequency (kHz) in Reg0.
 * Poll the matching DALSMC_MSG_Get*Freq message to confirm the clock has
 * reached the requested target.
 *
 * Request:  FreqMhz — target frequency in MHz
 * Response: Reg0    — actual configured frequency in kHz
 */
typedef union {
	struct {
		uint32_t FreqMhz;
		uint32_t Reserved[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetClockFreq_arg_t;

/**
 * DALSMC_MSG_StutterEfficiency - Report DCN stutter efficiency to PMFW.
 *
 * Informs PMFW of the current stutter utilisation for base and low-power stutter
 * modes so PMFW can adjust memory power policy accordingly.
 *
 * Base mode    — lower enter+exit latency (PHY LP1, no UCIE LP).
 * Low-power mode — higher enter+exit latency (PHY LP2, UCIE LP1).
 *
 * Request:  BaseEfficiencyPct[7:0]     — stutter efficiency % in base mode
 *           LowPowerEfficiencyPct[15:8] — stutter efficiency % in low-power mode
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t BaseEfficiencyPct     : 8;
		uint32_t LowPowerEfficiencyPct : 8;
		uint32_t Reserved              : 16;
		uint32_t Reserved1[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetStutterEfficiency_arg_t;

/**
 * DALSMC_MSG_SetMinDeepSleepDcfclk - Set the minimum DCFCLK frequency in deep sleep.
 *
 * Request:  MinDcfclkMhz — minimum DCFCLK frequency in MHz during deep sleep
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t MinDcfclkMhz;
		uint32_t Reserved[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetMinDeepSleepDcfclk_arg_t;

/**
 * DALSMC_MSG_IndicatePstateStatus - Indicate to PMFW various DMU behaviors required
 * to support UCLK P-state, for example whether or not DMU needs to modulate refresh
 * rate to perform UCLK switches.
 *
 * Request:  WaitResp[0]   — DAL requires a synchronous response before proceeding
 *           DrrEnable[1]  — DRR (dynamic refresh rate modulation) is active
 *           AltCh[2]      — alternate-channel mode is active
 *           AllowUclk[16] — DCN can tolerate UCLK P-state switches
 *           AllowFclk[17] — DCN can tolerate FCLK P-state switches
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t WaitResp  : 1;
		uint32_t DrrEnable : 1;
		uint32_t AltCh     : 1;
		uint32_t Reserved  : 13;
		uint32_t AllowUclk : 1;
		uint32_t AllowFclk : 1;
		uint32_t Reserved1 : 14;
		uint32_t Reserved2[3];
	};
	DALSMC_args_t Args;
} DALSMC_IndicatePstateStatus_arg_t;

/**
 * DALSMC_MSG_TransferTableSmu2Dram - Transfer a PMFW table into DRAM.
 * DALSMC_MSG_TransferTableDram2Smu - Transfer a DRAM buffer into PMFW.
 *
 * Both directions use the same argument layout. The DRAM address must be set
 * beforehand (AddrLow / AddrHigh are the GPU MC address bits [31:0] / [63:32]).
 *
 * Smu2Dram supported tables: TABLE_DAL_INIT (DPM clocks + UTM QoS + memory config)
 * Dram2Smu supported tables: TABLE_SOC_UTM  (debug override of UTM QoS parameters)
 *
 * Request:  TableId  — table identifier (TABLE_* defines below)
 *           AddrLow  — GPU MC address bits [31:0]  of destination/source buffer
 *           AddrHigh — GPU MC address bits [63:32] of destination/source buffer
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t TableId;
		uint32_t AddrLow;
		uint32_t AddrHigh;
		uint32_t Reserved;
	};
	DALSMC_args_t Args;
} DALSMC_TransferTable_arg_t;

/**
 * DALSMC_MSG_UpdateUTMQoSRequest - Update the active UTM QoS bandwidth/latency request.
 *
 * Passes the current display bandwidth and latency requirements to PMFW so it
 * can select the appropriate SoC operating point (UCLK/FCLK level) from the
 * UTM table. Called whenever the display configuration changes.
 *
 * The QoS requirement must take effect before PMFW sends its response.
 *
 * Request:  LatencySopIndex      — index into the UTM SOP table that satisfies latency
 *           NominalBandwidthKBps — required nominal (average) bandwidth in KB/s
 *           UrgentBandwidthKBps  — required urgent bandwidth in KB/s
 *           LsdmaBandwidthKBps   — required LSDMA bandwidth in KB/s
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t LatencySopIndex;
		uint32_t NominalBandwidthKBps;
		uint32_t UrgentBandwidthKBps;
		uint32_t LsdmaBandwidthKBps;
	};
	DALSMC_args_t Args;
} DALSMC_UpdateUTMQoSRequest_arg_t;

/**
 * DALSMC_MSG_SetDisplayIdleOptimizations - Notify PMFW of DCN idle-state conditions.
 *
 * Indicates which display-side power optimizations are currently safe to apply.
 * PMFW uses these flags to gate deeper SoC power states such as S0i2.
 *
 * Request:  DfRequestDisabled[0] — DF (data fabric) requests from DCN are disabled
 *           PhyRefClkOff[1]      — PHY reference clock has been gated off
 *           S0i2Rdy[2]           — DCN is ready for the system to enter S0i2
 * Response: (none beyond result code)
 */
typedef union {
	struct {
		uint32_t DfRequestDisabled : 1;
		uint32_t PhyRefClkOff      : 1;
		uint32_t S0i2Rdy           : 1;
		uint32_t Reserved          : 29;
		uint32_t Reserved1[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetDisplayIdleOptimizations_arg_t;
unsigned int dcn60_smu_set_hard_min_dcfclk_by_freq(struct clk_mgr_internal *clk_mgr, uint16_t freq_mhz);
unsigned int dcn60_smu_set_clock_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz);
void dcn60_smu_set_stutter_efficiency(struct clk_mgr_internal *clk_mgr,
		uint8_t base_efficiency, uint8_t low_power_efficiency);
void dcn60_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz);
void dcn60_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr);
void dcn60_smu_indicate_pstate_status(struct clk_mgr_internal *clk_mgr,
		bool allow_fclk, bool allow_uclk,
		bool wait_resp, bool drr_enable, bool alt_ch_enable);
bool dcn60_smu_update_utm_qos_request(struct clk_mgr_internal *clk_mgr,
		uint32_t latency_sop_index,
		uint32_t nominal_bandwidth_KBps,
		uint32_t urgent_bandwidth_KBps,
		uint32_t lsdma_bandwidth_KBps);
bool dcn60_smu_set_soc_utm_table(struct clk_mgr_internal *clk_mgr,
		long long dram_addr);
bool dcn60_smu_get_dal_init_table(struct clk_mgr_internal *clk_mgr,
		const DalInitTable_t **init_table);
bool dcn60_smu_get_msg_header_version(struct clk_mgr_internal *clk_mgr,
		uint32_t *version);
void dcn60_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, bool is_idle);

#endif /* __DCN60_CLK_MGR_SMU_MSG_H_ */
