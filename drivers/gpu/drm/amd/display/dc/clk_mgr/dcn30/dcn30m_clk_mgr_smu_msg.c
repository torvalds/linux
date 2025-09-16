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

#include "dcn30m_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"
#include "dm_helpers.h"

#include "dalsmc.h"

#define mmDAL_MSG_REG  0x1628A
#define mmDAL_ARG_REG  0x16273
#define mmDAL_RESP_REG 0x16274

#define REG(reg_name) \
	mm ## reg_name

#include "logger_types.h"
#undef DC_LOGGER
#define DC_LOGGER \
	CTX->logger
#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }


/*
 * Function to be used instead of REG_WAIT macro because the wait ends when
 * the register is NOT EQUAL to zero, and because the translation in msg_if.h
 * won't work with REG_WAIT.
 */
static uint32_t dcn30m_smu_wait_for_response(struct clk_mgr_internal *clk_mgr,
	unsigned int delay_us, unsigned int max_retries)
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

static bool dcn30m_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr,
	uint32_t msg_id, uint32_t param_in, uint32_t *param_out)
{
	uint32_t result;
	/* Wait for response register to be ready */
	dcn30m_smu_wait_for_response(clk_mgr, 10, 200000);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Set the parameter register for the SMU message */
	REG_WRITE(DAL_ARG_REG, param_in);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	result = dcn30m_smu_wait_for_response(clk_mgr, 10, 200000);

	if (IS_SMU_TIMEOUT(result))
		dm_helpers_smu_timeout(CTX, msg_id, param_in, 10 * 200000);

	/* Wait for response */
	if (result == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG);

		return true;
	}

	return false;
}

uint32_t dcn30m_smu_set_smart_mux_switch(struct clk_mgr_internal *clk_mgr, uint32_t pins_to_set)
{
	uint32_t response = 0;

	smu_print("SMU Set SmartMux Switch: switch_dgpu = %d\n", pins_to_set);

	dcn30m_smu_send_msg_with_param(clk_mgr,
			DALSMC_MSG_SmartAccess, pins_to_set, &response);

	return response;
}
