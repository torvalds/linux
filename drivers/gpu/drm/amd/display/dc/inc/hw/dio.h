// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DC_DIO_H__
#define __DC_DIO_H__

#include "dc_types.h"

struct dc_context;
struct dio;

struct dio_funcs {
	void (*mem_pwr_ctrl)(struct dio *dio, bool enable_i2c_light_sleep);
};

struct dio {
	const struct dio_funcs *funcs;
	struct dc_context *ctx;
};

#endif /* __DC_DIO_H__ */
