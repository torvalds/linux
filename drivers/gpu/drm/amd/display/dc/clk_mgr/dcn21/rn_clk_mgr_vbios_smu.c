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

#include "core_types.h"
#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include <linux/delay.h>

#include "renoir_ip_offset.h"

#include "mp/mp_12_0_0_offset.h"
#include "mp/mp_12_0_0_sh_mask.h"

#include "rn_clk_mgr_vbios_smu.h"

#define REG(reg_name) \
	(MP0_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)

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
#define VBIOSSMC_MSG_SetDprefclkFreq              0x5
#define VBIOSSMC_MSG_PowerDownGfx                 0x6
#define VBIOSSMC_MSG_SetDppclkFreq                0x7
#define VBIOSSMC_MSG_SetHardMinDcfclkByFreq       0x8
#define VBIOSSMC_MSG_SetMinDeepSleepDcfclk        0x9
#define VBIOSSMC_MSG_SetPhyclkVoltageByFreq       0xA
#define VBIOSSMC_MSG_GetFclkFrequency             0xB
#define VBIOSSMC_MSG_SetDisplayCount              0xC
#define VBIOSSMC_MSG_EnableTmdp48MHzRefclkPwrDown 0xD
#define VBIOSSMC_MSG_UpdatePmeRestore             0xE
#define VBIOSSMC_MSG_IsPeriodicRetrainingDisabled 0xF

#define VBIOSSMC_Status_BUSY                      0x0
#define VBIOSSMC_Result_OK                        0x1
#define VBIOSSMC_Result_Failed                    0xFF
#define VBIOSSMC_Result_UnknownCmd                0xFE
#define VBIOSSMC_Result_CmdRejectedPrereq         0xFD
#define VBIOSSMC_Result_CmdRejectedBusy           0xFC

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t rn_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
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


static int rn_vbios_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr,
					    unsigned int msg_id,
					    unsigned int param)
{
	uint32_t result;

	result = rn_smu_wait_for_response(clk_mgr, 10, 200000);

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

	result = rn_smu_wait_for_response(clk_mgr, 10, 200000);

	ASSERT(result == VBIOSSMC_Result_OK || result == VBIOSSMC_Result_UnknownCmd);

	/* Actual dispclk set is returned in the parameter register */
	return REG_READ(MP1_SMN_C2PMSG_83);
}

int rn_vbios_smu_get_smu_version(struct clk_mgr_internal *clk_mgr)
{
	return rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_GetSmuVersion,
			0);
}


int rn_vbios_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz)
{
	int actual_dispclk_set_mhz = -1;
	struct dc *dc = clk_mgr->base.ctx->dc;
	struct dmcu *dmcu = dc->res_pool->dmcu;

	/*  Unit of SMU msg parameter is Mhz */
	actual_dispclk_set_mhz = rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDispclkFreq,
			khz_to_mhz_ceil(requested_dispclk_khz));

	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment)) {
		if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
			if (clk_mgr->dfs_bypass_disp_clk != actual_dispclk_set_mhz)
				dmcu->funcs->set_psr_wait_loop(dmcu,
						actual_dispclk_set_mhz / 7);
		}
	}

	// pmfw always set clock more than or equal requested clock
	if (!IS_DIAG_DC(dc->ctx->dce_environment))
		ASSERT(actual_dispclk_set_mhz >= khz_to_mhz_ceil(requested_dispclk_khz));

	return actual_dispclk_set_mhz * 1000;
}

int rn_vbios_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	int actual_dprefclk_set_mhz = -1;

	actual_dprefclk_set_mhz = rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDprefclkFreq,
			khz_to_mhz_ceil(clk_mgr->base.dprefclk_khz));

	/* TODO: add code for programing DP DTO, currently this is down by command table */

	return actual_dprefclk_set_mhz * 1000;
}

int rn_vbios_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz)
{
	int actual_dcfclk_set_mhz = -1;

	if (clk_mgr->smu_ver < 0x370c00)
		return actual_dcfclk_set_mhz;

	actual_dcfclk_set_mhz = rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetHardMinDcfclkByFreq,
			khz_to_mhz_ceil(requested_dcfclk_khz));

#ifdef DBG
	smu_print("actual_dcfclk_set_mhz %d is set to : %d\n", actual_dcfclk_set_mhz, actual_dcfclk_set_mhz * 1000);
#endif

	return actual_dcfclk_set_mhz * 1000;
}

int rn_vbios_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz)
{
	int actual_min_ds_dcfclk_mhz = -1;

	if (clk_mgr->smu_ver < 0x370c00)
		return actual_min_ds_dcfclk_mhz;

	actual_min_ds_dcfclk_mhz = rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetMinDeepSleepDcfclk,
			khz_to_mhz_ceil(requested_min_ds_dcfclk_khz));

	return actual_min_ds_dcfclk_mhz * 1000;
}

void rn_vbios_smu_set_phyclk(struct clk_mgr_internal *clk_mgr, int requested_phyclk_khz)
{
	rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetPhyclkVoltageByFreq,
			khz_to_mhz_ceil(requested_phyclk_khz));
}

int rn_vbios_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz)
{
	int actual_dppclk_set_mhz = -1;
	struct dc *dc = clk_mgr->base.ctx->dc;

	actual_dppclk_set_mhz = rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDppclkFreq,
			khz_to_mhz_ceil(requested_dpp_khz));

	if (!IS_DIAG_DC(dc->ctx->dce_environment))
		ASSERT(actual_dppclk_set_mhz >= khz_to_mhz_ceil(requested_dpp_khz));

	return actual_dppclk_set_mhz * 1000;
}

void rn_vbios_smu_set_dcn_low_power_state(struct clk_mgr_internal *clk_mgr, enum dcn_pwr_state state)
{
	int disp_count;

	if (state == DCN_PWR_STATE_LOW_POWER)
		disp_count = 0;
	else
		disp_count = 1;

	rn_vbios_smu_send_msg_with_param(
		clk_mgr,
		VBIOSSMC_MSG_SetDisplayCount,
		disp_count);
}

void rn_vbios_smu_enable_48mhz_tmdp_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable)
{
	rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_EnableTmdp48MHzRefclkPwrDown,
			enable);
}

void rn_vbios_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr)
{
	rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_UpdatePmeRestore,
			0);
}

int rn_vbios_smu_is_periodic_retraining_disabled(struct clk_mgr_internal *clk_mgr)
{
	return rn_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_IsPeriodicRetrainingDisabled,
			1);	// if PMFW doesn't support this message, assume retraining is disabled
				// so we only use most optimal watermark if we know retraining is enabled.
}
