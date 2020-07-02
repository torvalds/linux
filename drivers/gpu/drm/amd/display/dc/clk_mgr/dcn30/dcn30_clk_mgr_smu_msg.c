/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include <linux/delay.h>
#include "dcn30_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include "dalsmc.h"

#define mmDAL_MSG_REG  0x1628A
#define mmDAL_ARG_REG  0x16273
#define mmDAL_RESP_REG 0x16274

#define REG(reg_name) \
	mm ## reg_name

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn30_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
{
	uint32_t reg = 0;

	do {
		reg = REG_READ(DAL_RESP_REG);
		if (reg)
			break;

		if (delay_us >= 1000)
			msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);
	} while (max_retries--);

	/* handle DALSMC_Result_CmdRejectedBusy? */

	/* Log? */

	return reg;
}

static bool dcn30_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out)
{
	/* Wait for response register to be ready */
	dcn30_smu_wait_for_response(clk_mgr, 10, 200000);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	/* Wait for response */
	if (dcn30_smu_wait_for_response(clk_mgr, 10, 200000) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		return true;
	}

	return false;
}

/* Test message should return input + 1 */
bool dcn30_smu_test_message(struct clk_mgr_internal *clk_mgr, uint32_t input)
{
	uint32_t response = 0;

	if (dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TestMessage, input, &response))
		if (response == input + 1)
			return true;

	return false;
}

bool dcn30_smu_get_smu_version(struct clk_mgr_internal *clk_mgr, unsigned int *version)
{
	if (dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetSmuVersion, 0, version))
		return true;

	return false;
}

/* Message output should match SMU11_DRIVER_IF_VERSION in smu11_driver_if.h */
bool dcn30_smu_check_driver_if_version(struct clk_mgr_internal *clk_mgr)
{
	uint32_t response = 0;

	if (dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDriverIfVersion, 0, &response))
		if (response == SMU11_DRIVER_IF_VERSION)
			return true;

	return false;
}

/* Message output should match DALSMC_VERSION in dalsmc.h */
bool dcn30_smu_check_msg_header_version(struct clk_mgr_internal *clk_mgr)
{
	uint32_t response = 0;

	if (dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetMsgHeaderVersion, 0, &response))
		if (response == DALSMC_VERSION)
			return true;

	return false;
}

void dcn30_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDalDramAddrHigh, addr_high, NULL);
}

void dcn30_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDalDramAddrLow, addr_low, NULL);
}

void dcn30_smu_transfer_wm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableSmu2Dram, TABLE_WATERMARKS, NULL);
}

void dcn30_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS, NULL);
}

/* Returns the actual frequency that was set in MHz, 0 on failure */
unsigned int dcn30_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint16_t freq_mhz)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = (clk << 16) | freq_mhz;

	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetHardMinByFreq, param, &response);

	return response;
}

/* Returns the actual frequency that was set in MHz, 0 on failure */
unsigned int dcn30_smu_set_hard_max_by_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint16_t freq_mhz)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = (clk << 16) | freq_mhz;

	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetHardMaxByFreq, param, &response);

	return response;
}

/*
 * Frequency in MHz returned in lower 16 bits for valid DPM level
 *
 * Call with dpm_level = 0xFF to query features, return value will be:
 *     Bits 7:0 - number of DPM levels
 *     Bit   28 - 1 = auto DPM on
 *     Bit   29 - 1 = sweep DPM on
 *     Bit   30 - 1 = forced DPM on
 *     Bit   31 - 0 = discrete, 1 = fine-grained
 *
 * With fine-grained DPM, only min and max frequencies will be reported
 *
 * Returns 0 on failure
 */
unsigned int dcn30_smu_get_dpm_freq_by_index(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, uint8_t dpm_level)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 8 bits for DPM level */
	uint32_t param = (clk << 16) | dpm_level;

	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDpmFreqByIndex, param, &response);

	return response;
}

/* Returns the max DPM frequency in DC mode in MHz, 0 on failure */
unsigned int dcn30_smu_get_dc_mode_max_dpm_freq(struct clk_mgr_internal *clk_mgr, PPCLK_e clk)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type */
	uint32_t param = clk << 16;

	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDcModeMaxDpmFreq, param, &response);

	return response;
}

void dcn30_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetMinDeepSleepDcefclk, freq_mhz, NULL);
}

void dcn30_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_NumOfDisplays, num_displays, NULL);
}

void dcn30_smu_set_external_client_df_cstate_allow(struct clk_mgr_internal *clk_mgr, bool enable)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetExternalClientDfCstateAllow, enable ? 1 : 0, NULL);
}

void dcn30_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr)
{
	dcn30_smu_send_msg_with_param(clk_mgr,
	DALSMC_MSG_BacoAudioD3PME, 0, NULL);
}
