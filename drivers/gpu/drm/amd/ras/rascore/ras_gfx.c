// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#include "ras.h"
#include "ras_gfx_v9_0.h"
#include "ras_gfx.h"
#include "ras_core_status.h"

static const struct ras_gfx_ip_func *ras_gfx_get_ip_funcs(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
	case IP_VERSION(9, 5, 0):
		return &gfx_ras_func_v9_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"GFX ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_gfx_get_ta_subblock(struct ras_core_context *ras_core,
		uint32_t error_type, uint32_t subblock, uint32_t *ta_subblock)
{
	struct ras_gfx *gfx = &ras_core->ras_gfx;

	return gfx->ip_func->get_ta_subblock(ras_core,
					error_type, subblock, ta_subblock);
}

int ras_gfx_hw_init(struct ras_core_context *ras_core)
{
	struct ras_gfx *gfx = &ras_core->ras_gfx;

	gfx->gfx_ip_version = ras_core->config->gfx_ip_version;

	gfx->ip_func = ras_gfx_get_ip_funcs(ras_core, gfx->gfx_ip_version);

	return gfx->ip_func ? RAS_CORE_OK : -EINVAL;
}

int ras_gfx_hw_fini(struct ras_core_context *ras_core)
{
	return 0;
}
