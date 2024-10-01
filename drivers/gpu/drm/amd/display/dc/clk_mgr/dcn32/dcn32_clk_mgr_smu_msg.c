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

#include "dcn32_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include "dalsmc.h"
#include "dcn32_smu13_driver_if.h"

#define mmDAL_MSG_REG  0x1628A
#define mmDAL_ARG_REG  0x16273
#define mmDAL_RESP_REG 0x16274

#define REG(reg_name) \
	mm ## reg_name

#include "logger_types.h"

#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }


/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn32_smu_wait_for_response(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries)
{
	const uint32_t initial_max_retries = max_retries;
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

	TRACE_SMU_DELAY(delay_us * (initial_max_retries - max_retries), clk_mgr->base.ctx);

	return reg;
}

static bool dcn32_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out)
{
	/* Wait for response register to be ready */
	dcn32_smu_wait_for_response(clk_mgr, 10, 200000);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	TRACE_SMU_MSG(msg_id, param_in, clk_mgr->base.ctx);

	/* Wait for response */
	if (dcn32_smu_wait_for_response(clk_mgr, 10, 200000) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		return true;
	}

	return false;
}

/*
 * Use these functions to return back delay information so we can aggregate the total
 *  delay when requesting hardmin clk
 *
 * dcn32_smu_wait_for_response_delay
 * dcn32_smu_send_msg_with_param_delay
 *
 */
static uint32_t dcn32_smu_wait_for_response_delay(struct clk_mgr_internal *clk_mgr, unsigned int delay_us, unsigned int max_retries, unsigned int *total_delay_us)
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

	TRACE_SMU_DELAY(*total_delay_us, clk_mgr->base.ctx);

	return reg;
}

static bool dcn32_smu_send_msg_with_param_delay(struct clk_mgr_internal *clk_mgr, uint32_t msg_id, uint32_t param_in, uint32_t *param_out, unsigned int *total_delay_us)
{
	unsigned int delay1_us, delay2_us;
	*total_delay_us = 0;

	/* Wait for response register to be ready */
	dcn32_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay1_us);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	TRACE_SMU_MSG(msg_id, param_in, clk_mgr->base.ctx);

	/* Wait for response */
	if (dcn32_smu_wait_for_response_delay(clk_mgr, 10, 200000, &delay2_us) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		*total_delay_us = delay1_us + delay2_us;
		return true;
	}

	*total_delay_us = delay1_us + 2000000;
	return false;
}

void dcn32_smu_send_fclk_pstate_message(struct clk_mgr_internal *clk_mgr, bool enable)
{
	smu_print("FCLK P-state support value is : %d\n", enable);

	dcn32_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SetFclkSwitchAllow, enable ? FCLK_PSTATE_SUPPORTED : FCLK_PSTATE_NOTSUPPORTED, NULL);
}

void dcn32_smu_send_cab_for_uclk_message(struct clk_mgr_internal *clk_mgr, unsigned int num_ways)
{
	uint32_t param = (num_ways << 1) | (num_ways > 0);

	dcn32_smu_send_msg_with_param(clk_mgr, DALSMC_MSG_SetCabForUclkPstate, param, NULL);
	smu_print("Numways for SubVP : %d\n", num_ways);
}

void dcn32_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Transfer WM table DRAM 2 SMU\n");

	dcn32_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, TABLE_WATERMARKS, NULL);
}

void dcn32_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr)
{
	smu_print("SMU Set PME workaround\n");

	dcn32_smu_send_msg_with_param(clk_mgr,
		DALSMC_MSG_BacoAudioD3PME, 0, NULL);
}

/* Check PMFW version if it supports ReturnHardMinStatus message */
static bool dcn32_get_hard_min_status_supported(struct clk_mgr_internal *clk_mgr)
{
	if (ASICREV_IS_GC_11_0_0(clk_mgr->base.ctx->asic_id.hw_internal_rev)) {
		if (clk_mgr->smu_ver >= 0x4e6a00)
			return true;
	} else if (ASICREV_IS_GC_11_0_2(clk_mgr->base.ctx->asic_id.hw_internal_rev)) {
		if (clk_mgr->smu_ver >= 0x524e00)
			return true;
	} else { /* ASICREV_IS_GC_11_0_3 */
		if (clk_mgr->smu_ver >= 0x503900)
			return true;
	}
	return false;
}

/* Returns the clocks which were fulfilled by the DAL hard min arbiter in PMFW */
static unsigned int dcn32_smu_get_hard_min_status(struct clk_mgr_internal *clk_mgr, bool *no_timeout, unsigned int *total_delay_us)
{
	uint32_t response = 0;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = 0;

	*no_timeout = dcn32_smu_send_msg_with_param_delay(clk_mgr,
			DALSMC_MSG_ReturnHardMinStatus, param, &response, total_delay_us);

	smu_print("SMU Get hard min status: no_timeout %d delay %d us clk bits %x\n",
		*no_timeout, *total_delay_us, response);

	return response;
}

static bool dcn32_smu_wait_get_hard_min_status(struct clk_mgr_internal *clk_mgr,
	uint32_t clk)
{
	int readDalHardMinClkBits, checkDalHardMinClkBits;
	unsigned int total_delay_us, read_total_delay_us;
	bool no_timeout, hard_min_done;

	static unsigned int cur_wait_get_hard_min_max_us;
	static unsigned int cur_wait_get_hard_min_max_timeouts;

	checkDalHardMinClkBits = CHECK_HARD_MIN_CLK_DPREFCLK;
	if (clk == PPCLK_DISPCLK)
		checkDalHardMinClkBits |= CHECK_HARD_MIN_CLK_DISPCLK;
	if (clk == PPCLK_DPPCLK)
		checkDalHardMinClkBits |= CHECK_HARD_MIN_CLK_DPPCLK;
	if (clk == PPCLK_DCFCLK)
		checkDalHardMinClkBits |= CHECK_HARD_MIN_CLK_DCFCLK;
	if (clk == PPCLK_DTBCLK)
		checkDalHardMinClkBits |= CHECK_HARD_MIN_CLK_DTBCLK;
	if (clk == PPCLK_UCLK)
		checkDalHardMinClkBits |= CHECK_HARD_MIN_CLK_UCLK;

	if (checkDalHardMinClkBits == CHECK_HARD_MIN_CLK_DPREFCLK)
		return 0;

	total_delay_us = 0;
	hard_min_done = false;
	while (1) {
		readDalHardMinClkBits = dcn32_smu_get_hard_min_status(clk_mgr, &no_timeout, &read_total_delay_us);
		total_delay_us += read_total_delay_us;
		if (checkDalHardMinClkBits == (readDalHardMinClkBits & checkDalHardMinClkBits)) {
			hard_min_done = true;
			break;
		}


		if (total_delay_us >= 2000000) {
			cur_wait_get_hard_min_max_timeouts++;
			smu_print("SMU Wait get hard min status: %d timeouts\n", cur_wait_get_hard_min_max_timeouts);
			break;
		}
		msleep(1);
		total_delay_us += 1000;
	}

	if (total_delay_us > cur_wait_get_hard_min_max_us)
		cur_wait_get_hard_min_max_us = total_delay_us;

	smu_print("SMU Wait get hard min status: no_timeout %d, delay %d us, max %d us, read %x, check %x\n",
		no_timeout, total_delay_us, cur_wait_get_hard_min_max_us, readDalHardMinClkBits, checkDalHardMinClkBits);

	return hard_min_done;
}

/* Returns the actual frequency that was set in MHz, 0 on failure */
unsigned int dcn32_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz)
{
	uint32_t response = 0;
	bool hard_min_done = false;

	/* bits 23:16 for clock type, lower 16 bits for frequency in MHz */
	uint32_t param = (clk << 16) | freq_mhz;

	smu_print("SMU Set hard min by freq: clk = %d, freq_mhz = %d MHz\n", clk, freq_mhz);

	dcn32_smu_send_msg_with_param(clk_mgr,
		DALSMC_MSG_SetHardMinByFreq, param, &response);

	if (dcn32_get_hard_min_status_supported(clk_mgr)) {
		hard_min_done = dcn32_smu_wait_get_hard_min_status(clk_mgr, clk);
		smu_print("SMU Frequency set = %d KHz hard_min_done %d\n", response, hard_min_done);
	} else
		smu_print("SMU Frequency set = %d KHz\n", response);

	return response;
}

void dcn32_smu_wait_for_dmub_ack_mclk(struct clk_mgr_internal *clk_mgr, bool enable)
{
	smu_print("PMFW to wait for DMCUB ack for MCLK : %d\n", enable);

	dcn32_smu_send_msg_with_param(clk_mgr, 0x14, enable ? 1 : 0, NULL);
}
