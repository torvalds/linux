// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dc_hw_types.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "dcn10_dio.h"

#define CTX \
	dio10->base.ctx
#define REG(reg)\
	dio10->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dio10->shifts->field_name, dio10->masks->field_name

static void dcn10_dio_mem_pwr_ctrl(struct dio *dio, bool enable_i2c_light_sleep)
{
	struct dcn10_dio *dio10 = TO_DCN10_DIO(dio);

	/* power AFMT HDMI memory */
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	if (enable_i2c_light_sleep)
		REG_UPDATE(DIO_MEM_PWR_CTRL, I2C_LIGHT_SLEEP_FORCE, 1);
}

static const struct dio_funcs dcn10_dio_funcs = {
	.mem_pwr_ctrl = dcn10_dio_mem_pwr_ctrl,
};

void dcn10_dio_construct(
	struct dcn10_dio *dio10,
	struct dc_context *ctx,
	const struct dcn_dio_registers *regs,
	const struct dcn_dio_shift *shifts,
	const struct dcn_dio_mask *masks)
{
	dio10->base.ctx = ctx;
	dio10->base.funcs = &dcn10_dio_funcs;

	dio10->regs = regs;
	dio10->shifts = shifts;
	dio10->masks = masks;
}
