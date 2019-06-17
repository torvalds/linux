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

#define MAX_INSTANCE	5
#define MAX_SEGMENT		5

struct IP_BASE_INSTANCE {
	unsigned int segment[MAX_SEGMENT];
};

struct IP_BASE {
	struct IP_BASE_INSTANCE instance[MAX_INSTANCE];
};


static const struct IP_BASE MP1_BASE  = { { { { 0x00016000, 0, 0, 0, 0 } },
											 { { 0, 0, 0, 0, 0 } },
											 { { 0, 0, 0, 0, 0 } },
											 { { 0, 0, 0, 0, 0 } },
											 { { 0, 0, 0, 0, 0 } } } };

#define mmMP1_SMN_C2PMSG_91            0x29B
#define mmMP1_SMN_C2PMSG_83            0x293
#define mmMP1_SMN_C2PMSG_67            0x283
#define mmMP1_SMN_C2PMSG_91_BASE_IDX   0
#define mmMP1_SMN_C2PMSG_83_BASE_IDX   0
#define mmMP1_SMN_C2PMSG_67_BASE_IDX   0

#define MP1_SMN_C2PMSG_91__CONTENT_MASK                    0xffffffffL
#define MP1_SMN_C2PMSG_83__CONTENT_MASK                    0xffffffffL
#define MP1_SMN_C2PMSG_67__CONTENT_MASK                    0xffffffffL
#define MP1_SMN_C2PMSG_91__CONTENT__SHIFT                  0x00000000
#define MP1_SMN_C2PMSG_83__CONTENT__SHIFT                  0x00000000
#define MP1_SMN_C2PMSG_67__CONTENT__SHIFT                  0x00000000

#define REG(reg_name) \
	(MP1_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)

#define FN(reg_name, field) \
	FD(reg_name##__##field)

#define VBIOSSMC_MSG_SetDispclkFreq           0x4
#define VBIOSSMC_MSG_SetDprefclkFreq          0x5

int rv1_vbios_smu_send_msg_with_param(struct clk_mgr_internal *clk_mgr, unsigned int msg_id, unsigned int param)
{
	/* First clear response register */
	REG_WRITE(MP1_SMN_C2PMSG_91, 0);

	/* Set the parameter register for the SMU message, unit is Mhz */
	REG_WRITE(MP1_SMN_C2PMSG_83, param);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(MP1_SMN_C2PMSG_67, msg_id);

	REG_WAIT(MP1_SMN_C2PMSG_91, CONTENT, 1, 10, 200000);

	/* Actual dispclk set is returned in the parameter register */
	return REG_READ(MP1_SMN_C2PMSG_83);
}

int rv1_vbios_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz)
{
	int actual_dispclk_set_mhz = -1;
	struct dc *core_dc = clk_mgr->base.ctx->dc;
	struct dmcu *dmcu = core_dc->res_pool->dmcu;

	/*  Unit of SMU msg parameter is Mhz */
	actual_dispclk_set_mhz = rv1_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDispclkFreq,
			requested_dispclk_khz / 1000);

	/* Actual dispclk set is returned in the parameter register */
	actual_dispclk_set_mhz = REG_READ(MP1_SMN_C2PMSG_83) * 1000;

	if (!IS_FPGA_MAXIMUS_DC(core_dc->ctx->dce_environment)) {
		if (dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
			if (clk_mgr->dfs_bypass_disp_clk != actual_dispclk_set_mhz)
				dmcu->funcs->set_psr_wait_loop(dmcu,
						actual_dispclk_set_mhz / 7);
		}
	}

	return actual_dispclk_set_mhz * 1000;
}

int rv1_vbios_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr)
{
	int actual_dprefclk_set_mhz = -1;

	actual_dprefclk_set_mhz = rv1_vbios_smu_send_msg_with_param(
			clk_mgr,
			VBIOSSMC_MSG_SetDprefclkFreq,
			clk_mgr->base.dprefclk_khz / 1000);

	/* TODO: add code for programing DP DTO, currently this is down by command table */

	return actual_dprefclk_set_mhz * 1000;
}
