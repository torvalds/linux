// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.



#include "core_types.h"
#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include "dm_helpers.h"
#include "dcn42_smu.h"

#include "mp/mp_15_0_0_offset.h"
#include "mp/mp_15_0_0_sh_mask.h"

#ifdef BASE_INNER
#undef BASE_INNER
#endif

#define MP1_BASE__INST0_SEG0                       0x00016000
#define MP1_BASE__INST0_SEG1                       0x00016200
#define MP1_BASE__INST0_SEG2                       0x00E00000
#define MP1_BASE__INST0_SEG3                       0x00E80000
#define MP1_BASE__INST0_SEG4                       0x00EC0000
#define MP1_BASE__INST0_SEG5                       0x00F00000
#define MP1_BASE__INST0_SEG6                       0x02400400
#define MP1_BASE__INST0_SEG7                       0x0243F400
#define MP1_BASE__INST0_SEG8                       0x3C004000
#define MP1_BASE__INST0_SEG9                       0x3C3F4000

#define BASE_INNER(seg) MP1_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define REG(reg_name) (BASE(reg##reg_name##_BASE_IDX) + reg##reg_name)

#define FN(reg_name, field) \
	FD(reg_name##__##field)

#include "logger_types.h"
#undef DC_LOGGER
#define DC_LOGGER \
	CTX->logger
#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }

// VBIOS and DAL to PMFW Interface

// DAL to PMFW interface registers
#define DAL_MSG_REG                             MP1_SMN_C2PMSG_71
#define DAL_RESP_REG                            MP1_SMN_C2PMSG_72
#define DAL_ARG_REG                             MP1_SMN_C2PMSG_73

/** @defgroup ResponseCodes PMFW Response Codes
*  @{
*/
// SMU Response Codes:
#define DALSMC_Result_OK                        0x01 ///< Message Response OK
#define DALSMC_Result_Failed                    0xFF ///< Message Response Failed
#define DALSMC_Result_UnknownCmd                0xFE ///< Message Response Unknown Command
#define DALSMC_Result_CmdRejectedPrereq         0xFD ///< Message Response Command Failed Prerequisite
#define DALSMC_Result_CmdRejectedBusy           0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message
/** @}*/

// Message Definitions:
/** @defgroup definitions Message definitions
*  @{
*/
#define DALSMC_MSG_TestMessage                  0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team
#define DALSMC_MSG_GetPmfwVersion               0x02 ///< Get version
#define DALSMC_MSG_SetDispclkFreq               0x03 ///< Set display clock frequency in MHZ
#define DALSMC_MSG_SetDppclkFreq                0x04 ///< Set DPP clock frequency in MHZ
#define DALSMC_MSG_SetHardMinDcfclkByFreq       0x05 ///< Set DCF clock frequency hard min in MHZ
#define DALSMC_MSG_SetMinDeepSleepDcfclk        0x06 ///< Set DCF clock minimum frequency in deep sleep in MHZ
#define DALSMC_MSG_UpdatePmeRestore             0x07 ///< To ask PMFW to write into Azalia for PME wake up event
#define DALSMC_MSG_SetDramAddrHigh              0x08 ///< Set DRAM address high 32 bits for WM table transfer
#define DALSMC_MSG_SetDramAddrLow               0x09 ///< Set DRAM address low 32 bits for WM table transfer

#define DALSMC_MSG_TransferTableSmu2Dram        0x0A ///< Transfer table from PMFW SRAM to system DRAM
#define DALSMC_MSG_TransferTableDram2Smu        0x0B ///< Transfer table from system DRAM to PMFW
#define DALSMC_MSG_SetDisplayIdleOptimizations  0x0C ///< Set Idle state optimization for display off
#define DALSMC_MSG_GetDprefclkFreq              0x0D ///< Get DPREF clock frequency. Return in MHZ
#define DALSMC_MSG_GetDtbclkFreq                0x0E ///< Get DTB clock frequency. Return in MHZ
#define DALSMC_MSG_AllowZstatesEntry            0x0F ///< Inform PMFW of display allowing Zstate entry
#define DALSMC_MSG_SetDtbClk                    0x10 ///< Inform PMFW to turn of/off DTB cl0ck. arg = 1: turn DTB on with 600MHZ; arg = 0: turn DTB clk off
#define DALSMC_MSG_DispIPS2Exit                 0x11 ///< Display IPS2 exit

#define DALSMC_MSG_QueryIPS2Support             0x12 ///< Return 1: support; else not supported
#define DALSMC_Message_Count                    0x13 ///< Total number of VBIS and DAL messages
/** @}*/


union dcn42_dpia_host_router_bw {
	struct {
		uint32_t hr_id : 16;
		uint32_t bw_mbps : 16;
	} bits;
	uint32_t all;
};

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because `the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn42_smu_wait_for_response(struct clk_mgr_internal *clk_mgr,
		unsigned int delay_us, unsigned int max_retries)
{
	uint32_t res_val;

	do {
		res_val = REG_READ(DAL_RESP_REG);
		if (res_val != DALSMC_Result_CmdRejectedBusy)
			break;

		if (delay_us >= 1000)
			msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);

		if (clk_mgr->base.ctx->dc->debug.disable_timeout)
			max_retries++;
	} while (max_retries--);

	return res_val;
}

static int dcn42_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr,
					 unsigned int msg_id,
					 unsigned int param)
{
	uint32_t result;

	result = dcn42_smu_wait_for_response(clk_mgr, 10, 2000000);

	if (result != DALSMC_Result_OK) {
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);

		if (result == DALSMC_Result_CmdRejectedBusy)
			return -1;
	}

	/* First clear response register */
	REG_WRITE(DAL_RESP_REG, DALSMC_Result_CmdRejectedBusy);

	/* Set the parameter register for the SMU message, unit is Mhz */
	REG_WRITE(DAL_ARG_REG, param);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	result = dcn42_smu_wait_for_response(clk_mgr, 10, 2000000);

	if (result == DALSMC_Result_Failed) {
		if (msg_id == DALSMC_MSG_TransferTableDram2Smu &&
		    param == TABLE_WATERMARKS)
			DC_LOG_WARNING("Watermarks table not configured properly by SMU");
		REG_WRITE(DAL_RESP_REG, DALSMC_Result_OK);
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);
		return -1;
	}

	if (IS_SMU_TIMEOUT(result)) {
		ASSERT(0);
		result = dcn42_smu_wait_for_response(clk_mgr, 10, 2000000);
		//dm_helpers_smu_timeout(CTX, msg_id, param, 10 * 200000);
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);
	}

	return REG_READ(DAL_ARG_REG);
}

int dcn42_smu_get_pmfw_version(struct clk_mgr_internal *clk_mgr)
{
	return dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_GetPmfwVersion,
			0);
}


int dcn42_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz)
{
	int actual_dispclk_set_mhz;

	if (!clk_mgr->smu_present)
		return requested_dispclk_khz;

	/*  Unit of SMU msg parameter is Mhz */
	actual_dispclk_set_mhz = dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetDispclkFreq,
			khz_to_mhz_ceil(requested_dispclk_khz));

	smu_print("requested_dispclk_khz = %d, actual_dispclk_set_mhz: %d\n",
		requested_dispclk_khz, actual_dispclk_set_mhz);
	return (int)((long long)actual_dispclk_set_mhz * 1000);
}


int dcn42_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz)
{
	int actual_dcfclk_set_mhz;

	if (!clk_mgr->smu_present)
		return requested_dcfclk_khz;

	actual_dcfclk_set_mhz = dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetHardMinDcfclkByFreq,
			khz_to_mhz_ceil(requested_dcfclk_khz));

	smu_print("requested_dcfclk_khz = %d, actual_dcfclk_set_mhz: %d\n",
		requested_dcfclk_khz, actual_dcfclk_set_mhz);

	return (int)((long long)actual_dcfclk_set_mhz * 1000);
}

int dcn42_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz)
{
	int actual_min_ds_dcfclk_mhz;

	if (!clk_mgr->smu_present)
		return requested_min_ds_dcfclk_khz;

	actual_min_ds_dcfclk_mhz = dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetMinDeepSleepDcfclk,
			khz_to_mhz_ceil(requested_min_ds_dcfclk_khz));

	smu_print("requested_min_ds_dcfclk_khz = %d, actual_min_ds_dcfclk_mhz: %d\n",
		requested_min_ds_dcfclk_khz, actual_min_ds_dcfclk_mhz);

	return (int)((long long)actual_min_ds_dcfclk_mhz * 1000);
}

int dcn42_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz)
{
	int actual_dppclk_set_mhz;

	if (!clk_mgr->smu_present)
		return requested_dpp_khz;

	actual_dppclk_set_mhz = dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetDppclkFreq,
			khz_to_mhz_ceil(requested_dpp_khz));

	smu_print("requested_dpp_khz = %d, actual_dppclk_set_mhz: %d\n",
		requested_dpp_khz, actual_dppclk_set_mhz);

	return (int)((long long)actual_dppclk_set_mhz * 1000);
}

void dcn42_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info)
{
	if (!clk_mgr->base.ctx->dc->debug.pstate_enabled)
		return;

	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(
		clk_mgr,
		DALSMC_MSG_SetDisplayIdleOptimizations,
		idle_info);
	smu_print("%s: SMC_MSG_SetDisplayIdleOptimizations idle_info  = %x\n", __func__, idle_info);
}

void dcn42_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable)
{
	union display_idle_optimization_u idle_info = { 0 };

	if (!clk_mgr->smu_present)
		return;

	if (enable) {
		idle_info.idle_info.df_request_disabled = 1;
		idle_info.idle_info.phy_ref_clk_off = 1;
	}

	dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetDisplayIdleOptimizations,
			idle_info.data);
	smu_print("%s smu_enable_phy_refclk_pwrdwn  = %d\n", __func__, enable ? 1 : 0);
}

void dcn42_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_UpdatePmeRestore,
			0);
	smu_print("%s: SMC_MSG_UpdatePmeRestore\n", __func__);
}

void dcn42_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDramAddrHigh, addr_high);
}

void dcn42_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDramAddrLow, addr_low);
}

void dcn42_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableSmu2Dram, TABLE_DPMCLOCKS);
}

void dcn42_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS);
}

void dcn42_smu_set_zstate_support(struct clk_mgr_internal *clk_mgr, enum dcn_zstate_support_state support)
{
	unsigned int msg_id, param, retv;

	if (!clk_mgr->smu_present)
		return;

	switch (support) {

	case DCN_ZSTATE_SUPPORT_ALLOW:
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = (1 << 10) | (1 << 9) | (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW, param = 0x%x\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_DISALLOW:
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = 0;
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg_id = DISALLOW, param = 0x%x\n",  __func__, param);
		break;


	case DCN_ZSTATE_SUPPORT_ALLOW_Z10_ONLY:
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = (1 << 10);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z10_ONLY, param = 0x%x\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_ALLOW_Z8_Z10_ONLY:
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = (1 << 10) | (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z8_Z10_ONLY, param = 0x%x\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY:
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z8_ONLY, param = 0x%x\n", __func__, param);
		break;

	default: //DCN_ZSTATE_SUPPORT_UNKNOWN
		msg_id = DALSMC_MSG_AllowZstatesEntry;
		param = 0;
		break;
	}


	retv = dcn42_smu_send_msg_with_param(
		clk_mgr,
		msg_id,
		param);
	smu_print("%s:  msg_id = %d, param = 0x%x, return = 0x%x\n", __func__, msg_id, param, retv);
}

int dcn42_smu_get_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	int dprefclk;

	if (!clk_mgr->smu_present)
		return 0;

	dprefclk = dcn42_smu_send_msg_with_param(clk_mgr,
						 DALSMC_MSG_GetDprefclkFreq,
						 0);

	smu_print("%s:  SMU DPREF clk  = %d mhz\n",  __func__, dprefclk);
	return (int)((long long)dprefclk * 1000);
}

int dcn42_smu_get_dtbclk(struct clk_mgr_internal *clk_mgr)
{
	int dtbclk;

	if (!clk_mgr->smu_present)
		return 0;

	dtbclk = dcn42_smu_send_msg_with_param(clk_mgr,
					       DALSMC_MSG_GetDtbclkFreq,
					       0);

	smu_print("%s: get_dtbclk  = %dmhz\n", __func__, dtbclk);
	return (int)((long long)dtbclk * 1000);
}
/* Arg = 1: Turn DTB on; 0: Turn DTB CLK OFF. when it is on, it is 600MHZ */
void dcn42_smu_set_dtbclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	if (!clk_mgr->smu_present)
		return;

	dcn42_smu_send_msg_with_param(
			clk_mgr,
			DALSMC_MSG_SetDtbClk,
			enable);
	smu_print("%s: smu_set_dtbclk = %d\n", __func__, enable ? 1 : 0);
}
