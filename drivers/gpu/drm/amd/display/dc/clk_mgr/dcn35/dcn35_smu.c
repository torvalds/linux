/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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



#include "core_types.h"
#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include "dm_helpers.h"
#include "dcn35_smu.h"

#include "mp/mp_14_0_0_offset.h"
#include "mp/mp_14_0_0_sh_mask.h"

/* TODO: Use the real headers when they're correct */
#define MP1_BASE__INST0_SEG0                       0x00016000
#define MP1_BASE__INST0_SEG1                       0x0243FC00
#define MP1_BASE__INST0_SEG2                       0x00DC0000
#define MP1_BASE__INST0_SEG3                       0x00E00000
#define MP1_BASE__INST0_SEG4                       0x00E40000
#define MP1_BASE__INST0_SEG5                       0

#ifdef BASE_INNER
#undef BASE_INNER
#endif

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

#define VBIOSSMC_MSG_TestMessage                  0x1
#define VBIOSSMC_MSG_GetSmuVersion                0x2
#define VBIOSSMC_MSG_PowerUpGfx                   0x3
#define VBIOSSMC_MSG_SetDispclkFreq               0x4
#define VBIOSSMC_MSG_SetDprefclkFreq              0x5   //Not used. DPRef is constant
#define VBIOSSMC_MSG_SetDppclkFreq                0x6
#define VBIOSSMC_MSG_SetHardMinDcfclkByFreq       0x7
#define VBIOSSMC_MSG_SetMinDeepSleepDcfclk        0x8
#define VBIOSSMC_MSG_SetPhyclkVoltageByFreq       0x9	//Keep it in case VMIN dees not support phy clk
#define VBIOSSMC_MSG_GetFclkFrequency             0xA
#define VBIOSSMC_MSG_SetDisplayCount              0xB   //Not used anymore
#define VBIOSSMC_MSG_EnableTmdp48MHzRefclkPwrDown 0xC   //To ask PMFW turn off TMDP 48MHz refclk during display off to save power
#define VBIOSSMC_MSG_UpdatePmeRestore             0xD
#define VBIOSSMC_MSG_SetVbiosDramAddrHigh         0xE   //Used for WM table txfr
#define VBIOSSMC_MSG_SetVbiosDramAddrLow          0xF
#define VBIOSSMC_MSG_TransferTableSmu2Dram        0x10
#define VBIOSSMC_MSG_TransferTableDram2Smu        0x11
#define VBIOSSMC_MSG_SetDisplayIdleOptimizations  0x12
#define VBIOSSMC_MSG_GetDprefclkFreq              0x13
#define VBIOSSMC_MSG_GetDtbclkFreq                0x14
#define VBIOSSMC_MSG_AllowZstatesEntry            0x15
#define VBIOSSMC_MSG_DisallowZstatesEntry     	  0x16
#define VBIOSSMC_MSG_SetDtbClk                    0x17
#define VBIOSSMC_MSG_DispPsrEntry                 0x18 ///< Display PSR entry, DMU
#define VBIOSSMC_MSG_DispPsrExit                  0x19 ///< Display PSR exit, DMU
#define VBIOSSMC_MSG_DisableLSdma                 0x1A ///< Disable LSDMA; only sent by VBIOS
#define VBIOSSMC_MSG_DpControllerPhyStatus        0x1B ///< Inform PMFW about the pre conditions for turning SLDO2 on/off . bit[0]==1 precondition is met, bit[1-2] are for DPPHY number
#define VBIOSSMC_MSG_QueryIPS2Support             0x1C ///< Return 1: support; else not supported
#define VBIOSSMC_Message_Count                    0x1D

#define VBIOSSMC_Status_BUSY                      0x0
#define VBIOSSMC_Result_OK                        0x1
#define VBIOSSMC_Result_Failed                    0xFF
#define VBIOSSMC_Result_UnknownCmd                0xFE
#define VBIOSSMC_Result_CmdRejectedPrereq         0xFD
#define VBIOSSMC_Result_CmdRejectedBusy           0xFC

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because `the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn35_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
{
	uint32_t res_val = VBIOSSMC_Status_BUSY;

	do {
		res_val = REG_READ(MP1_SMN_C2PMSG_91);
		if (res_val != VBIOSSMC_Status_BUSY)
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

static int dcn35_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr,
					 unsigned int msg_id,
					 unsigned int param)
{
	uint32_t result;

	result = dcn35_smu_wait_for_response(clk_mgr, 10, 2000000);
	ASSERT(result == VBIOSSMC_Result_OK);

	if (result != VBIOSSMC_Result_OK) {
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);

		if (result == VBIOSSMC_Status_BUSY)
			return -1;
	}

	/* First clear response register */
	REG_WRITE(MP1_SMN_C2PMSG_91, VBIOSSMC_Status_BUSY);

	/* Set the parameter register for the SMU message, unit is Mhz */
	REG_WRITE(MP1_SMN_C2PMSG_83, param);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(MP1_SMN_C2PMSG_67, msg_id);

	result = dcn35_smu_wait_for_response(clk_mgr, 10, 2000000);

	if (result == VBIOSSMC_Result_Failed) {
		if (msg_id == VBIOSSMC_MSG_TransferTableDram2Smu &&
		    param == TABLE_WATERMARKS)
			DC_LOG_WARNING("Watermarks table not configured properly by SMU");
		else
			ASSERT(0);
		REG_WRITE(MP1_SMN_C2PMSG_91, VBIOSSMC_Result_OK);
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);
		return -1;
	}

	if (IS_SMU_TIMEOUT(result)) {
		ASSERT(0);
		result = dcn35_smu_wait_for_response(clk_mgr, 10, 2000000);
		//dm_helpers_smu_timeout(CTX, msg_id, param, 10 * 200000);
		DC_LOG_WARNING("SMU response after wait: %d, msg id = %d\n", result, msg_id);
	}

	return REG_READ(MP1_SMN_C2PMSG_83);
}

int dcn35_smu_get_smu_version(struct clk_mgr_internal *clk_mgr)
{
	return dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_GetSmuVersion,
			0);
}


int dcn35_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz)
{
	int actual_dispclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_dispclk_khz;

	/*  Unit of SMU msg parameter is Mhz */
	actual_dispclk_set_mhz = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDispclkFreq,
			khz_to_mhz_ceil(requested_dispclk_khz));

	smu_print("requested_dispclk_khz = %d, actual_dispclk_set_mhz: %d\n", requested_dispclk_khz, actual_dispclk_set_mhz);
	return actual_dispclk_set_mhz * 1000;
}

int dcn35_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	int actual_dprefclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return clk_mgr->base.dprefclk_khz;

	actual_dprefclk_set_mhz = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDprefclkFreq,
			khz_to_mhz_ceil(clk_mgr->base.dprefclk_khz));

	/* TODO: add code for programing DP DTO, currently this is down by command table */

	return actual_dprefclk_set_mhz * 1000;
}

int dcn35_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz)
{
	int actual_dcfclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_dcfclk_khz;

	actual_dcfclk_set_mhz = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetHardMinDcfclkByFreq,
			khz_to_mhz_ceil(requested_dcfclk_khz));

	smu_print("requested_dcfclk_khz = %d, actual_dcfclk_set_mhz: %d\n", requested_dcfclk_khz, actual_dcfclk_set_mhz);

	return actual_dcfclk_set_mhz * 1000;
}

int dcn35_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz)
{
	int actual_min_ds_dcfclk_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_min_ds_dcfclk_khz;

	actual_min_ds_dcfclk_mhz = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetMinDeepSleepDcfclk,
			khz_to_mhz_ceil(requested_min_ds_dcfclk_khz));

	smu_print("requested_min_ds_dcfclk_khz = %d, actual_min_ds_dcfclk_mhz: %d\n", requested_min_ds_dcfclk_khz, actual_min_ds_dcfclk_mhz);

	return actual_min_ds_dcfclk_mhz * 1000;
}

int dcn35_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz)
{
	int actual_dppclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_dpp_khz;

	actual_dppclk_set_mhz = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDppclkFreq,
			khz_to_mhz_ceil(requested_dpp_khz));

	smu_print("requested_dpp_khz = %d, actual_dppclk_set_mhz: %d\n", requested_dpp_khz, actual_dppclk_set_mhz);

	return actual_dppclk_set_mhz * 1000;
}

void dcn35_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info)
{
	if (!clk_mgr->base.ctx->dc->debug.pstate_enabled)
		return;

	if (!clk_mgr->smu_present)
		return;

	//TODO: Work with smu team to define optimization options.
	dcn35_smu_send_msg_with_param(
		clk_mgr,
		VBIOSSMC_MSG_SetDisplayIdleOptimizations,
		idle_info);
	smu_print("%s: VBIOSSMC_MSG_SetDisplayIdleOptimizations idle_info  = %x\n", __func__, idle_info);
}

void dcn35_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable)
{
	union display_idle_optimization_u idle_info = { 0 };

	if (!clk_mgr->smu_present)
		return;

	if (enable) {
		idle_info.idle_info.df_request_disabled = 1;
		idle_info.idle_info.phy_ref_clk_off = 1;
	}

	dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDisplayIdleOptimizations,
			idle_info.data);
	smu_print("%s smu_enable_phy_refclk_pwrdwn  = %d\n", __func__, enable ? 1 : 0);
}

void dcn35_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_UpdatePmeRestore,
			0);
	smu_print("%s: SMC_MSG_UpdatePmeRestore\n", __func__);
}

void dcn35_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_SetVbiosDramAddrHigh, addr_high);
}

void dcn35_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_SetVbiosDramAddrLow, addr_low);
}

void dcn35_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_TransferTableSmu2Dram, TABLE_DPMCLOCKS);
}

void dcn35_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS);
}

void dcn35_smu_set_zstate_support(struct clk_mgr_internal *clk_mgr, enum dcn_zstate_support_state support)
{
	unsigned int msg_id, param, retv;

	if (!clk_mgr->smu_present)
		return;

	switch (support) {

	case DCN_ZSTATE_SUPPORT_ALLOW:
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = (1 << 10) | (1 << 9) | (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW, param = %d\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_DISALLOW:
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = 0;
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg_id = DISALLOW, param = %d\n",  __func__, param);
		break;


	case DCN_ZSTATE_SUPPORT_ALLOW_Z10_ONLY:
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = (1 << 10);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z10_ONLY, param = %d\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_ALLOW_Z8_Z10_ONLY:
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = (1 << 10) | (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z8_Z10_ONLY, param = %d\n", __func__, param);
		break;

	case DCN_ZSTATE_SUPPORT_ALLOW_Z8_ONLY:
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = (1 << 8);
		smu_print("%s: SMC_MSG_AllowZstatesEntry msg = ALLOW_Z8_ONLY, param = %d\n", __func__, param);
		break;

	default: //DCN_ZSTATE_SUPPORT_UNKNOWN
		msg_id = VBIOSSMC_MSG_AllowZstatesEntry;
		param = 0;
		break;
	}


	retv = dcn35_smu_send_msg_with_param(
		clk_mgr,
		msg_id,
		param);
	smu_print("%s:  msg_id = %d, param = 0x%x, return = %d\n", __func__, msg_id, param, retv);
}

int dcn35_smu_get_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	int dprefclk;

	if (!clk_mgr->smu_present)
		return 0;

	dprefclk = dcn35_smu_send_msg_with_param(clk_mgr,
						 VBIOSSMC_MSG_GetDprefclkFreq,
						 0);

	smu_print("%s:  SMU DPREF clk  = %d mhz\n",  __func__, dprefclk);
	return dprefclk * 1000;
}

int dcn35_smu_get_dtbclk(struct clk_mgr_internal *clk_mgr)
{
	int dtbclk;

	if (!clk_mgr->smu_present)
		return 0;

	dtbclk = dcn35_smu_send_msg_with_param(clk_mgr,
					       VBIOSSMC_MSG_GetDtbclkFreq,
					       0);

	smu_print("%s: get_dtbclk  = %dmhz\n", __func__, dtbclk);
	return dtbclk * 1000;
}
/* Arg = 1: Turn DTB on; 0: Turn DTB CLK OFF. when it is on, it is 600MHZ */
void dcn35_smu_set_dtbclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	if (!clk_mgr->smu_present)
		return;

	dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDtbClk,
			enable);
	smu_print("%s: smu_set_dtbclk = %d\n", __func__, enable ? 1 : 0);
}

void dcn35_vbios_smu_enable_48mhz_tmdp_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable)
{
	dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_EnableTmdp48MHzRefclkPwrDown,
			enable);
	smu_print("%s: smu_enable_48mhz_tmdp_refclk_pwrdwn = %d\n", __func__, enable ? 1 : 0);
}

int dcn35_smu_exit_low_power_state(struct clk_mgr_internal *clk_mgr)
{
	int retv;

	retv = dcn35_smu_send_msg_with_param(
		clk_mgr,
		VBIOSSMC_MSG_DispPsrExit,
		0);
	smu_print("%s: smu_exit_low_power_state return = %d\n", __func__, retv);
	return retv;
}

int dcn35_smu_get_ips_supported(struct clk_mgr_internal *clk_mgr)
{
	int retv;

	retv = dcn35_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_QueryIPS2Support,
			0);

	//smu_print("%s: VBIOSSMC_MSG_QueryIPS2Support return = %x\n", __func__, retv);
	return retv;
}

void dcn35_smu_write_ips_scratch(struct clk_mgr_internal *clk_mgr, uint32_t param)
{
	REG_WRITE(MP1_SMN_C2PMSG_71, param);
	//smu_print("%s: write_ips_scratch = %x\n", __func__, param);
}

uint32_t dcn35_smu_read_ips_scratch(struct clk_mgr_internal *clk_mgr)
{
	uint32_t retv;

	retv = REG_READ(MP1_SMN_C2PMSG_71);
	//smu_print("%s: dcn35_smu_read_ips_scratch = %x\n",  __func__, retv);
	return retv;
}
