/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#include "dc_bios_types.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn31_vpg.h"
#include "reg_helper.h"
#include "dc/dc.h"

#define DC_LOGGER \
		vpg31->base.ctx->logger

#define REG(reg)\
	(vpg31->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	vpg31->vpg_shift->field_name, vpg31->vpg_mask->field_name


#define CTX \
	vpg31->base.ctx

static struct vpg_funcs dcn31_vpg_funcs = {
	.update_generic_info_packet	= vpg3_update_generic_info_packet,
	.vpg_poweron = vpg31_poweron,
	.vpg_powerdown = vpg31_powerdown,
};

void vpg31_powerdown(struct vpg *vpg)
{
	struct dcn31_vpg *vpg31 = DCN31_VPG_FROM_VPG(vpg);

	if (vpg->ctx->dc->debug.enable_mem_low_power.bits.vpg == false)
		return;

	REG_UPDATE_2(VPG_MEM_PWR, VPG_GSP_MEM_LIGHT_SLEEP_DIS, 0, VPG_GSP_LIGHT_SLEEP_FORCE, 1);
}

void vpg31_poweron(struct vpg *vpg)
{
	struct dcn31_vpg *vpg31 = DCN31_VPG_FROM_VPG(vpg);

	if (vpg->ctx->dc->debug.enable_mem_low_power.bits.vpg == false)
		return;

	REG_UPDATE_2(VPG_MEM_PWR, VPG_GSP_MEM_LIGHT_SLEEP_DIS, 1, VPG_GSP_LIGHT_SLEEP_FORCE, 0);
}

void vpg31_construct(struct dcn31_vpg *vpg31,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn31_vpg_registers *vpg_regs,
	const struct dcn31_vpg_shift *vpg_shift,
	const struct dcn31_vpg_mask *vpg_mask)
{
	vpg31->base.ctx = ctx;

	vpg31->base.inst = inst;
	vpg31->base.funcs = &dcn31_vpg_funcs;

	vpg31->regs = vpg_regs;
	vpg31->vpg_shift = vpg_shift;
	vpg31->vpg_mask = vpg_mask;
}
