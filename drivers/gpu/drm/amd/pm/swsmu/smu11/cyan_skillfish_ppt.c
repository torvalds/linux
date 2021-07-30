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
 */

#define SWSMU_CODE_LAYER_L2

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_cyan_skillfish.h"
#include "cyan_skillfish_ppt.h"
#include "smu_v11_8_ppsmc.h"
#include "smu_v11_8_pmfw.h"
#include "smu_cmn.h"
#include "soc15_common.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */

#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

static struct cmn2asic_msg_mapping cyan_skillfish_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,                    PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,                  PPSMC_MSG_GetSmuVersion,		0),
	MSG_MAP(GetDriverIfVersion,             PPSMC_MSG_GetDriverIfVersion,		0),
	MSG_MAP(SetDriverDramAddrHigh,          PPSMC_MSG_SetDriverTableDramAddrHigh,	0),
	MSG_MAP(SetDriverDramAddrLow,           PPSMC_MSG_SetDriverTableDramAddrLow,	0),
	MSG_MAP(TransferTableSmu2Dram,          PPSMC_MSG_TransferTableSmu2Dram,	0),
	MSG_MAP(TransferTableDram2Smu,          PPSMC_MSG_TransferTableDram2Smu,	0),
};

static const struct pptable_funcs cyan_skillfish_ppt_funcs = {

	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.interrupt_work = smu_v11_0_interrupt_work,
};

void cyan_skillfish_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &cyan_skillfish_ppt_funcs;
	smu->message_map = cyan_skillfish_message_map;
	smu->is_apu = true;
}
