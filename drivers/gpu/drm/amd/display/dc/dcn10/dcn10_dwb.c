/*
 * Copyright 2012-17 Advanced Micro Devices, Inc.
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

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)

#include "reg_helper.h"
#include "resource.h"
#include "dwb.h"
#include "dcn10_dwb.h"


#define REG(reg)\
	dwbc10->dwbc_regs->reg

#define CTX \
	dwbc10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dwbc10->dwbc_shift->field_name, dwbc10->dwbc_mask->field_name

#define TO_DCN10_DWBC(dwbc_base) \
	container_of(dwbc_base, struct dcn10_dwbc, base)

static bool dwb1_get_caps(struct dwbc *dwbc, struct dwb_caps *caps)
{
	if (caps) {
		caps->adapter_id = 0;	/* we only support 1 adapter currently */
		caps->hw_version = DCN_VERSION_1_0;
		caps->num_pipes = 2;
		memset(&caps->reserved, 0, sizeof(caps->reserved));
		memset(&caps->reserved2, 0, sizeof(caps->reserved2));
		caps->sw_version = dwb_ver_1_0;
		caps->caps.support_dwb = true;
		caps->caps.support_ogam = false;
		caps->caps.support_wbscl = true;
		caps->caps.support_ocsc = false;
		return true;
	} else {
		return false;
	}
}

static bool dwb1_enable(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn10_dwbc *dwbc10 = TO_DCN10_DWBC(dwbc);

	/* disable first. */
	dwbc->funcs->disable(dwbc);

	/* disable power gating */
	REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 1,
		 DISPCLK_G_WB_GATE_DIS, 1, DISPCLK_G_WBSCL_GATE_DIS, 1,
		 WB_LB_LS_DIS, 1, WB_LUT_LS_DIS, 1);

	REG_UPDATE(WB_ENABLE, WB_ENABLE, 1);

	return true;
}

static bool dwb1_disable(struct dwbc *dwbc)
{
	struct dcn10_dwbc *dwbc10 = TO_DCN10_DWBC(dwbc);

	/* disable CNV */
	REG_UPDATE(CNV_MODE, CNV_FRAME_CAPTURE_EN, 0);

	/* disable WB */
	REG_UPDATE(WB_ENABLE, WB_ENABLE, 0);

	/* soft reset */
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 1);
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 0);

	/* enable power gating */
	REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 0,
		 DISPCLK_G_WB_GATE_DIS, 0, DISPCLK_G_WBSCL_GATE_DIS, 0,
		 WB_LB_LS_DIS, 0, WB_LUT_LS_DIS, 0);

	return true;
}

const struct dwbc_funcs dcn10_dwbc_funcs = {
	.get_caps			= dwb1_get_caps,
	.enable				= dwb1_enable,
	.disable			= dwb1_disable,
	.update				= NULL,
	.set_stereo			= NULL,
	.set_new_content		= NULL,
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	.set_warmup			= NULL,
#endif
	.dwb_set_scaler			= NULL,
};

void dcn10_dwbc_construct(struct dcn10_dwbc *dwbc10,
		struct dc_context *ctx,
		const struct dcn10_dwbc_registers *dwbc_regs,
		const struct dcn10_dwbc_shift *dwbc_shift,
		const struct dcn10_dwbc_mask *dwbc_mask,
		int inst)
{
	dwbc10->base.ctx = ctx;

	dwbc10->base.inst = inst;
	dwbc10->base.funcs = &dcn10_dwbc_funcs;

	dwbc10->dwbc_regs = dwbc_regs;
	dwbc10->dwbc_shift = dwbc_shift;
	dwbc10->dwbc_mask = dwbc_mask;
}


#endif
