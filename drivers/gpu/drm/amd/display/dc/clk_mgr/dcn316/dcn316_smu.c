/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dcn316_smu.h"
#include "mp/mp_13_0_8_offset.h"
#include "mp/mp_13_0_8_sh_mask.h"

#define MAX_INSTANCE                                        7
#define MAX_SEGMENT                                         6

struct IP_BASE_INSTANCE {
    unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE {
    struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};

static const struct IP_BASE MP0_BASE = { { { { 0x00016000, 0x00DC0000, 0x00E00000, 0x00E40000, 0x0243FC00, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } },
					{ { 0, 0, 0, 0, 0, 0 } } } };

#define REG(reg_name) \
	(MP0_BASE.instance[0].segment[reg ## reg_name ## _BASE_IDX] + reg ## reg_name)

#define FN(reg_name, field) \
	FD(reg_name##__##field)

#include "logger_types.h"
#undef DC_LOGGER
#define DC_LOGGER \
	CTX->logger
#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }

#define VBIOSSMC_MSG_TestMessage                  0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team
#define VBIOSSMC_MSG_GetPmfwVersion               0x02 ///< Get PMFW version
#define VBIOSSMC_MSG_Spare0                       0x03 ///< Spare0
#define VBIOSSMC_MSG_SetDispclkFreq               0x04 ///< Set display clock frequency in MHZ
#define VBIOSSMC_MSG_Spare1                       0x05 ///< Spare1
#define VBIOSSMC_MSG_SetDppclkFreq                0x06 ///< Set DPP clock frequency in MHZ
#define VBIOSSMC_MSG_SetHardMinDcfclkByFreq       0x07 ///< Set DCF clock frequency hard min in MHZ
#define VBIOSSMC_MSG_SetMinDeepSleepDcfclk        0x08 ///< Set DCF clock minimum frequency in deep sleep in MHZ
#define VBIOSSMC_MSG_SetPhyclkVoltageByFreq       0x09 ///< Set display phy clock frequency in MHZ in case VMIN does not support phy frequency
#define VBIOSSMC_MSG_GetFclkFrequency             0x0A ///< Get FCLK frequency, return frequemcy in MHZ
#define VBIOSSMC_MSG_SetDisplayCount              0x0B ///< Inform PMFW of number of display connected
#define VBIOSSMC_MSG_SPARE						  0x0C ///< SPARE
#define VBIOSSMC_MSG_UpdatePmeRestore             0x0D ///< To ask PMFW to write into Azalia for PME wake up event
#define VBIOSSMC_MSG_SetVbiosDramAddrHigh         0x0E ///< Set DRAM address high 32 bits for WM table transfer
#define VBIOSSMC_MSG_SetVbiosDramAddrLow          0x0F ///< Set DRAM address low 32 bits for WM table transfer
#define VBIOSSMC_MSG_TransferTableSmu2Dram        0x10 ///< Transfer table from PMFW SRAM to system DRAM
#define VBIOSSMC_MSG_TransferTableDram2Smu        0x11 ///< Transfer table from system DRAM to PMFW
#define VBIOSSMC_MSG_SetDisplayIdleOptimizations  0x12 ///< Set Idle state optimization for display off
#define VBIOSSMC_MSG_GetDprefclkFreq              0x13 ///< Get DPREF clock frequency. Return in MHZ
#define VBIOSSMC_MSG_GetDtbclkFreq                0x14 ///< Get DPREF clock frequency. Return in MHZ
#define VBIOSSMC_MSG_SetDtbclkFreq                0x15 ///< Inform PMFW to turn on/off DTB clock arg = 1, turn DTB clock on 600MHz/ arg = 0 turn DTB clock off
#define VBIOSSMC_Message_Count                    0x16 ///< Total number of VBIS and DAL messages

#define VBIOSSMC_Status_BUSY                      0x0
#define VBIOSSMC_Result_OK                        0x01 ///< Message Response OK
#define VBIOSSMC_Result_Failed                    0xFF ///< Message Response Failed
#define VBIOSSMC_Result_UnknownCmd                0xFE ///< Message Response Unknown Command
#define VBIOSSMC_Result_CmdRejectedPrereq         0xFD ///< Message Response Command Failed Prerequisite
#define VBIOSSMC_Result_CmdRejectedBusy           0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn316_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
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
	} while (max_retries--);

	return res_val;
}

static int dcn316_smu_send_msg_with_param(
		struct clk_mgr_internal *clk_mgr,
		unsigned int msg_id, unsigned int param)
{
	uint32_t result;

	result = dcn316_smu_wait_for_response(clk_mgr, 10, 200000);

	if (result != VBIOSSMC_Result_OK)
		smu_print("SMU Response was not OK. SMU response after wait received is: %d\n", result);

	if (result == VBIOSSMC_Status_BUSY) {
		return -1;
	}

	/* First clear response register */
	REG_WRITE(MP1_SMN_C2PMSG_91, VBIOSSMC_Status_BUSY);

	/* Set the parameter register for the SMU message, unit is Mhz */
	REG_WRITE(MP1_SMN_C2PMSG_83, param);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(MP1_SMN_C2PMSG_67, msg_id);

	result = dcn316_smu_wait_for_response(clk_mgr, 10, 200000);

	if (result == VBIOSSMC_Status_BUSY) {
		ASSERT(0);
		dm_helpers_smu_timeout(CTX, msg_id, param, 10 * 200000);
	}

	return REG_READ(MP1_SMN_C2PMSG_83);
}

int dcn316_smu_get_smu_version(struct clk_mgr_internal *clk_mgr)
{
	return dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_GetPmfwVersion,
			0);
}


int dcn316_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz)
{
	int actual_dispclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_dispclk_khz;

	/*  Unit of SMU msg parameter is Mhz */
	actual_dispclk_set_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDispclkFreq,
			khz_to_mhz_ceil(requested_dispclk_khz));

	return actual_dispclk_set_mhz * 1000;
}

int dcn316_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz)
{
	int actual_dcfclk_set_mhz = -1;

	if (!clk_mgr->base.ctx->dc->debug.pstate_enabled)
		return -1;

	if (!clk_mgr->smu_present)
		return requested_dcfclk_khz;

	actual_dcfclk_set_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetHardMinDcfclkByFreq,
			khz_to_mhz_ceil(requested_dcfclk_khz));

#ifdef DBG
	smu_print("actual_dcfclk_set_mhz %d is set to : %d\n", actual_dcfclk_set_mhz, actual_dcfclk_set_mhz * 1000);
#endif

	return actual_dcfclk_set_mhz * 1000;
}

int dcn316_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz)
{
	int actual_min_ds_dcfclk_mhz = -1;

	if (!clk_mgr->base.ctx->dc->debug.pstate_enabled)
		return -1;

	if (!clk_mgr->smu_present)
		return requested_min_ds_dcfclk_khz;

	actual_min_ds_dcfclk_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetMinDeepSleepDcfclk,
			khz_to_mhz_ceil(requested_min_ds_dcfclk_khz));

	return actual_min_ds_dcfclk_mhz * 1000;
}

int dcn316_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz)
{
	int actual_dppclk_set_mhz = -1;

	if (!clk_mgr->smu_present)
		return requested_dpp_khz;

	actual_dppclk_set_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDppclkFreq,
			khz_to_mhz_ceil(requested_dpp_khz));

	return actual_dppclk_set_mhz * 1000;
}

void dcn316_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info)
{
	if (!clk_mgr->base.ctx->dc->debug.pstate_enabled)
		return;

	if (!clk_mgr->smu_present)
		return;

	//TODO: Work with smu team to define optimization options.
	dcn316_smu_send_msg_with_param(
		clk_mgr,
		VBIOSSMC_MSG_SetDisplayIdleOptimizations,
		idle_info);
}

void dcn316_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable)
{
	union display_idle_optimization_u idle_info = { 0 };

	if (!clk_mgr->smu_present)
		return;

	if (enable) {
		idle_info.idle_info.df_request_disabled = 1;
		idle_info.idle_info.phy_ref_clk_off = 1;
	}

	dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDisplayIdleOptimizations,
			idle_info.data);
}

void dcn316_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_SetVbiosDramAddrHigh, addr_high);
}

void dcn316_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_SetVbiosDramAddrLow, addr_low);
}

void dcn316_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_TransferTableSmu2Dram, TABLE_DPMCLOCKS);
}

void dcn316_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(clk_mgr,
			VBIOSSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS);
}

void dcn316_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_UpdatePmeRestore,
			0);
}

/* Arg = 1: Turn DTB on; 0: Turn DTB CLK OFF. when it is on, it is 600MHZ */
void dcn316_smu_set_dtbclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	if (!clk_mgr->smu_present)
		return;

	dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDtbclkFreq,
			enable);
}

int dcn316_smu_get_dpref_clk(struct clk_mgr_internal *clk_mgr)
{
	int dprefclk_get_mhz = -1;

	if (clk_mgr->smu_present) {
		dprefclk_get_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_GetDprefclkFreq,
			0);
	}
	return (dprefclk_get_mhz * 1000);
}

int dcn316_smu_get_smu_fclk(struct clk_mgr_internal *clk_mgr)
{
	int fclk_get_mhz = -1;

	if (clk_mgr->smu_present) {
		fclk_get_mhz = dcn316_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_GetFclkFrequency,
			0);
	}
	return (fclk_get_mhz * 1000);
}
