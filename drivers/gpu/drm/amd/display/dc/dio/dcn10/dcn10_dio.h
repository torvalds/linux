// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DCN10_DIO_H__
#define __DCN10_DIO_H__

#include "dio.h"

#define TO_DCN10_DIO(dio_base) \
	container_of(dio_base, struct dcn10_dio, base)

#define DIO_REG_LIST_DCN10()\
	SR(DIO_MEM_PWR_CTRL)

struct dcn_dio_registers {
	uint32_t DIO_MEM_PWR_CTRL;
};

struct dcn_dio_shift {
	uint8_t I2C_LIGHT_SLEEP_FORCE;
};

struct dcn_dio_mask {
	uint32_t I2C_LIGHT_SLEEP_FORCE;
};

struct dcn10_dio {
	struct dio base;
	const struct dcn_dio_registers *regs;
	const struct dcn_dio_shift *shifts;
	const struct dcn_dio_mask *masks;
};

void dcn10_dio_construct(
	struct dcn10_dio *dio10,
	struct dc_context *ctx,
	const struct dcn_dio_registers *regs,
	const struct dcn_dio_shift *shifts,
	const struct dcn_dio_mask *masks);

#endif /* __DCN10_DIO_H__ */
