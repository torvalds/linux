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

#include "reg_helper.h"
#include "clk_mgr_internal.h"
#include "rv1_clk_mgr_clk.h"

#include "ip/Discovery/hwid.h"
#include "ip/Discovery/v1/ip_offset_1.h"
#include "ip/CLK/clk_10_0_default.h"
#include "ip/CLK/clk_10_0_offset.h"
#include "ip/CLK/clk_10_0_reg.h"
#include "ip/CLK/clk_10_0_sh_mask.h"

#include "dce100/dce_clk_mgr.h"

#define CLK_BASE_INNER(inst) \
	CLK_BASE__INST ## inst ## _SEG0


#define CLK_REG(reg_name, block, inst)\
	CLK_BASE(mm ## block ## _ ## inst ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## _ ## inst ## _ ## reg_name

#define REG(reg_name) \
	CLK_REG(reg_name, CLK0, 0)


/* Only used by testing framework*/
void rv1_dump_clk_registers(struct clk_state_registers *regs, struct clk_bypass *bypass, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

		regs->CLK0_CLK8_CURRENT_CNT = REG_READ(CLK0_CLK8_CURRENT_CNT) / 10; //dcf clk

		bypass->dcfclk_bypass = REG_READ(CLK0_CLK8_BYPASS_CNTL) & 0x0007;
		if (bypass->dcfclk_bypass < 0 || bypass->dcfclk_bypass > 4)
			bypass->dcfclk_bypass = 0;


		regs->CLK0_CLK8_DS_CNTL = REG_READ(CLK0_CLK8_DS_CNTL) / 10;	//dcf deep sleep divider

		regs->CLK0_CLK8_ALLOW_DS = REG_READ(CLK0_CLK8_ALLOW_DS); //dcf deep sleep allow

		regs->CLK0_CLK10_CURRENT_CNT = REG_READ(CLK0_CLK10_CURRENT_CNT) / 10; //dpref clk

		bypass->dispclk_pypass = REG_READ(CLK0_CLK10_BYPASS_CNTL) & 0x0007;
		if (bypass->dispclk_pypass < 0 || bypass->dispclk_pypass > 4)
			bypass->dispclk_pypass = 0;

		regs->CLK0_CLK11_CURRENT_CNT = REG_READ(CLK0_CLK11_CURRENT_CNT) / 10; //disp clk

		bypass->dprefclk_bypass = REG_READ(CLK0_CLK11_BYPASS_CNTL) & 0x0007;
		if (bypass->dprefclk_bypass < 0 || bypass->dprefclk_bypass > 4)
			bypass->dprefclk_bypass = 0;

}
