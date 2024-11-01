/*
* Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef __DC_RESOURCE_DCN201_H__
#define __DC_RESOURCE_DCN201_H__

#include "core_types.h"

#define RRDPCS_PHY_DP_TX_PSTATE_POWER_UP    0x00000000
#define RRDPCS_PHY_DP_TX_PSTATE_HOLD        0x00000001
#define RRDPCS_PHY_DP_TX_PSTATE_HOLD_OFF    0x00000002
#define RRDPCS_PHY_DP_TX_PSTATE_POWER_DOWN  0x00000003

#define TO_DCN201_RES_POOL(pool)\
	container_of(pool, struct dcn201_resource_pool, base)

struct dc;
struct resource_pool;
struct _vcs_dpi_display_pipe_params_st;

struct dcn201_resource_pool {
	struct resource_pool base;
};
struct resource_pool *dcn201_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

#endif /* __DC_RESOURCE_DCN201_H__ */
