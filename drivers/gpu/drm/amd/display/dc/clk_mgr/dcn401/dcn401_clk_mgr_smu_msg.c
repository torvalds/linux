// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dcn401_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"

#include "dalsmc.h"
#include "dcn401_smu14_driver_if.h"

#define mmDAL_MSG_REG  0x1628A
#define mmDAL_ARG_REG  0x16273
#define mmDAL_RESP_REG 0x16274

#define REG(reg_name) \
	mm ## reg_name

#include "logger_types.h"

#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }

/* temporary define */
#ifndef DALSMC_MSG_SubvpUclkFclk
#define DALSMC_MSG_SubvpUclkFclk 0x1B
#endif
#ifndef DALSMC_MSG_GetNumUmcChannels
#define DALSMC_MSG_GetNumUmcChannels 0x1C
#endif

/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn401_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
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

	return reg;
}

static bool dcn401_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out)
{
	/* Wait for response register to be ready */
	dcn401_smu_wait_for_response(clk_mgr, 10, 200000);

	TRACE_SMU_MSG_ENTER(msg_id, param_in, clk_mgr->base.ctx);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	/* Wait for response */
	if (dcn401_smu_wait_for_response(clk_mgr, 10, 200000) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		TRACE_SMU_MSG_EXIT(true, param_out ? *param_out : 0, clk_mgr->base.ctx);
		return true;
	}

	TRACE_SMU_MSG_EXIT(false, 0, clk_mgr->base.ctx);
	return false;
}

/*
 * Use these functions to return back delay information so we can aggregate the total
 *  delay when requesting hardmin clk
 *
 * dcn401_smu_wait_for_response_delay
 * dcn401_smu_send_msg_with_param_delay
 *
 */
static uint32_t dcn401_smu_wait_for_response_delay(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries, unsigned int *total_delay_us)
{
	uint32_t reg = 0;
	*total_delay_us = 0;

	do {
		reg = REG_READ(DAL_RESP_REG);
		if (reg)
			break;

		if (delay_us >= 1000)
			msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);
		*total_delay_us += delay_us;
	} while (max_retries--);

	return reg;
}

static bool dcn401_smu_send_msg_with_param_delay(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out, unsigned int *total_delay_us)
{
	unsigned int delay1_us, delay2_us;
	*total_delay_us = 0;

	/* Wait for response register to be ready */
	dcn401_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay1_us);

	TRACE_SMU_MSG_ENTER(msg_id, param_in, clk_mgr->base.ctx);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	/* Wait for response */
	if (dcn401_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay2_us) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		*total_delay_us = delay1_us + delay2_us;
		TRACE_SMU_MSG_EXIT(true, param_out ? *param_out : 0, clk_mgr->base.ctx);
		return true;
	}

	*total_delay_us = delay1_us + 2000000;
	TRACE_SMU_MSG_EXIT(false, 0, clk_mgr->base.ctx);
	return false;
}

bool dcn401_smu_get_smu_version(struct clk_mgr_internal *clk_mgr, unsigned int *version)
{
	smu_print("SMU Get SMU version\n");

	if (dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetSmuVersion, 0, version)) {

		smu_print("SMU version: %d\n", *version);

		return true;
	}

	return false;
}

/* Message output should match SMU11_DRIVER_IF_VERSION in smu11_driver_if.h */
bool dcn401_smu_check_driver_if_version(struct clk_mgr_internal *clk_mgr)
{
	uint32_t response = 0;

	smu_print("SMU Check driver if version\n");

	if (dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDriverIfVersion, 0, &response)) {

		smu_print("SMU driver if version: %d\n", response);

		if (response == SMU14_DRIVER_IF_VERSION)
			return true;
	}

	return false;
}

/* Message output should match DALSMC_VERSION in dalsmc.h */
bool dcn401_smu_check_msg_header_version(struct clk_mgr_internal *clk_mgr)
{
	uint32_t response = 0;

	smu_print("SMU Check msg header version\n");

	if (dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetMsgHeaderVersion, 0, &response)) {

		smu_print("SMU msg header version: %d\n", response);

		if (response == DALSMC_VERSION)
			return true;
	}

	return false;
}

void dcn401_smu_send_fclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support)
{
	smu_print("FCLK P-state support value is : %d\n", support);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetFclkSwitchAllow, support, NULL);
}

void dcn401_smu_send_uclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool support)
{
	smu_print("UCLK P-state support value is : %d\n", support);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetUclkPstateAllow, support, NULL);
}

void dcn401_smu_send_cab_for_uclk_message(struct clk_mgr_internal *clk_mgr, unsigned int num_ways)
{
	uint32_t param = (num_ways << 1) | (num_ways > 0);

	dcn401_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_SetCabForUclkPstate, param, NULL);
	smu_print("Numways for SubVP : %d\n", num_ways);
}

void dcn401_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high)
{
	smu_print("SMU Set DRAM addr high: %d\n", addr_high);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDalDramAddrHigh, addr_high, NULL);
}

void dcn401_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low)
{
	smu_print("SMU Set DRAM addr low: %d\n", addr_low);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetDalDramAddrLow, addr_low, NULL);
}

void dcn401_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Transfer WM table DRAM 2 SMU\n");

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS, NULL);
}

void dcn401_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Set PME workaround\n");

	dcn401_smu_send_msg_with_param(clk_mgr,
		DALSMC_MSG_BacoAudioD3PME, 0, NULL);
}

static unsigned int dcn401_smu_get_hard_min_status(struct clk_mgr_internal *clk_mgr, bool *no_timeout, unsigned int *total_delay_us)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = 0;

	*no_timeout = dcn401_smu_send_msg_with_param_delay(clk_mgr,
			DALSMC_MSG_ReturnHardMinStatus, param, &response, total_delay_us);

	smu_print("SMU Get hard min status: no_timeout %d delay %d us clk bits %x\n",
		*no_timeout, *total_delay_us, response);

	return response;
}

static bool dcn401_smu_wait_hard_min_status(struct clk_mgr_internal *clk_mgr, uint32_t ppclk)
{
	const unsigned int max_delay_us = 1000000;

	unsigned int hardmin_status_mask = (1 << ppclk);
	unsigned int total_delay_us = 0;
	bool hardmin_done = false;

	while (!hardmin_done && total_delay_us < max_delay_us) {
		unsigned int hardmin_status;
		unsigned int read_total_delay_us;
		bool no_timeout;

		if (!hardmin_done && total_delay_us > 0) {
			/* hardmin not yet fulfilled, wait 500us and retry*/
			udelay(500);
			total_delay_us += 500;

			smu_print("SMU Wait hard min status for %d us\n", total_delay_us);
		}

		hardmin_status = dcn401_smu_get_hard_min_status(clk_mgr, &no_timeout, &read_total_delay_us);
		total_delay_us += read_total_delay_us;
		hardmin_done = hardmin_status & hardmin_status_mask;
	}

	return hardmin_done;
}

/* Returns the actual frequency that was set in MHz, 0 on failure */
unsigned int dcn401_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz)
{
	uint32_t response = 0;
	bool hard_min_done = false;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = (clk << 16) | freq_mhz;

	smu_print("SMU Set hard min by freq: clk = %d, freq_mhz = %d MHz\n", clk, freq_mhz);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetHardMinByFreq, param, &response);

	/* wait until hardmin acknowledged */
	hard_min_done = dcn401_smu_wait_hard_min_status(clk_mgr, clk);
	smu_print("SMU Frequency set = %d KHz hard_min_done %d\n", response, hard_min_done);

	return response;
}

void dcn401_smu_wait_for_dmub_ack_mclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	smu_print("SMU to wait for DMCUB ack for MCLK : %d\n", enable);

	dcn401_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_SetAlwaysWaitDmcubResp, enable ? 1 : 0, NULL);
}

void dcn401_smu_indicate_drr_status(struct clk_mgr_internal *clk_mgr, bool mod_drr_for_pstate)
{
	smu_print("SMU Set indicate drr status = %d\n", mod_drr_for_pstate);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_IndicateDrrStatus, mod_drr_for_pstate ? 1 : 0, NULL);
}

bool dcn401_smu_set_idle_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz)
{
	uint32_t response = 0;
	bool success;

	/* 15:0 for uclk, 32:16 for fclk */
	uint32_t param = (fclk_freq_mhz << 16) | uclk_freq_mhz;

	smu_print("SMU Set idle hardmin by freq: uclk_freq_mhz = %d MHz, fclk_freq_mhz = %d MHz\n", uclk_freq_mhz, fclk_freq_mhz);

	success = dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_IdleUclkFclk, param, &response);

	/* wait until hardmin acknowledged */
	success &= dcn401_smu_wait_hard_min_status(clk_mgr, PPCLK_UCLK);
	smu_print("SMU hard_min_done %d\n", success);

	return success;
}

bool dcn401_smu_set_active_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz)
{
	uint32_t response = 0;
	bool success;

	/* 15:0 for uclk, 32:16 for fclk */
	uint32_t param = (fclk_freq_mhz << 16) | uclk_freq_mhz;

	smu_print("SMU Set active hardmin by freq: uclk_freq_mhz = %d MHz, fclk_freq_mhz = %d MHz\n", uclk_freq_mhz, fclk_freq_mhz);

	success = dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_ActiveUclkFclk, param, &response);

	/* wait until hardmin acknowledged */
	success &= dcn401_smu_wait_hard_min_status(clk_mgr, PPCLK_UCLK);
	smu_print("SMU hard_min_done %d\n", success);

	return success;
}

bool dcn401_smu_set_subvp_uclk_fclk_hardmin(struct clk_mgr_internal *clk_mgr,
		uint16_t uclk_freq_mhz,
		uint16_t fclk_freq_mhz)
{
	uint32_t response = 0;
	bool success;

	/* 15:0 for uclk, 32:16 for fclk */
	uint32_t param = (fclk_freq_mhz << 16) | uclk_freq_mhz;

	smu_print("SMU Set active hardmin by freq: uclk_freq_mhz = %d MHz, fclk_freq_mhz = %d MHz\n", uclk_freq_mhz, fclk_freq_mhz);

	success = dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SubvpUclkFclk, param, &response);

	return success;
}

void dcn401_smu_set_min_deep_sleep_dcef_clk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz)
{
	smu_print("SMU Set min deep sleep dcef clk: freq_mhz = %d MHz\n", freq_mhz);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetMinDeepSleepDcfclk, freq_mhz, NULL);
}

void dcn401_smu_set_num_of_displays(struct clk_mgr_internal *clk_mgr, uint32_t num_displays)
{
	smu_print("SMU Set num of displays: num_displays = %d\n", num_displays);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_NumOfDisplays, num_displays, NULL);
}

unsigned int dcn401_smu_get_num_of_umc_channels(struct clk_mgr_internal *clk_mgr)
{
	unsigned int response = 0;

	dcn401_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_GetNumUmcChannels, 0, &response);

	smu_print("SMU Get Num UMC Channels: num_umc_channels = %d\n", response);

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
unsigned int dcn401_smu_get_dpm_freq_by_index(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint8_t dpm_level)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 8 bits for DPM level */
	uint32_t param = (clk << 16) | dpm_level;

	smu_print("SMU Get dpm freq by index: clk = %d, dpm_level = %d\n", clk, dpm_level);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDpmFreqByIndex, param, &response);

	smu_print("SMU dpm freq: %d MHz\n", response);

	return response;
}

/* Returns the max DPM frequency in DC mode in MHz, 0 on failure */
unsigned int dcn401_smu_get_dc_mode_max_dpm_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type */
	uint32_t param = clk << 16;

	smu_print("SMU Get DC mode max DPM freq: clk = %d\n", clk);

	dcn401_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_GetDcModeMaxDpmFreq, param, &response);

	smu_print("SMU DC mode max DMP freq: %d MHz\n", response);

	return response;
}
