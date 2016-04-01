/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/firmware.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include <drm/radeon_drm.h>
#include "rv770d.h"
#include "atom.h"
#include "avivod.h"

#define R700_PFP_UCODE_SIZE 848
#define R700_PM4_UCODE_SIZE 1360

static void rv770_gpu_init(struct radeon_device *rdev);
void rv770_fini(struct radeon_device *rdev);
static void rv770_pcie_gen2_enable(struct radeon_device *rdev);
int evergreen_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);

int rv770_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk)
{
	unsigned fb_div = 0, vclk_div = 0, dclk_div = 0;
	int r;

	/* RV740 uses evergreen uvd clk programming */
	if (rdev->family == CHIP_RV740)
		return evergreen_set_uvd_clocks(rdev, vclk, dclk);

	/* bypass vclk and dclk with bclk */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		 VCLK_SRC_SEL(1) | DCLK_SRC_SEL(1),
		 ~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	if (!vclk || !dclk) {
		/* keep the Bypass mode, put PLL to sleep */
		WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_SLEEP_MASK, ~UPLL_SLEEP_MASK);
		return 0;
	}

	r = radeon_uvd_calc_upll_dividers(rdev, vclk, dclk, 50000, 160000,
					  43663, 0x03FFFFFE, 1, 30, ~0,
					  &fb_div, &vclk_div, &dclk_div);
	if (r)
		return r;

	fb_div |= 1;
	vclk_div -= 1;
	dclk_div -= 1;

	/* set UPLL_FB_DIV to 0x50000 */
	WREG32_P(CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(0x50000), ~UPLL_FB_DIV_MASK);

	/* deassert UPLL_RESET and UPLL_SLEEP */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~(UPLL_RESET_MASK | UPLL_SLEEP_MASK));

	/* assert BYPASS EN and FB_DIV[0] <- ??? why? */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_BYPASS_EN_MASK, ~UPLL_BYPASS_EN_MASK);
	WREG32_P(CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(1), ~UPLL_FB_DIV(1));

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* assert PLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_RESET_MASK, ~UPLL_RESET_MASK);

	/* set the required FB_DIV, REF_DIV, Post divder values */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_REF_DIV(1), ~UPLL_REF_DIV_MASK);
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		 UPLL_SW_HILEN(vclk_div >> 1) |
		 UPLL_SW_LOLEN((vclk_div >> 1) + (vclk_div & 1)) |
		 UPLL_SW_HILEN2(dclk_div >> 1) |
		 UPLL_SW_LOLEN2((dclk_div >> 1) + (dclk_div & 1)),
		 ~UPLL_SW_MASK);

	WREG32_P(CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(fb_div),
		 ~UPLL_FB_DIV_MASK);

	/* give the PLL some time to settle */
	mdelay(15);

	/* deassert PLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);

	mdelay(15);

	/* deassert BYPASS EN and FB_DIV[0] <- ??? why? */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_BYPASS_EN_MASK);
	WREG32_P(CG_UPLL_FUNC_CNTL_3, 0, ~UPLL_FB_DIV(1));

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* switch VCLK and DCLK selection */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		 VCLK_SRC_SEL(2) | DCLK_SRC_SEL(2),
		 ~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	mdelay(100);

	return 0;
}

static const u32 r7xx_golden_registers[] =
{
	0x8d00, 0xffffffff, 0x0e0e0074,
	0x8d04, 0xffffffff, 0x013a2b34,
	0x9508, 0xffffffff, 0x00000002,
	0x8b20, 0xffffffff, 0,
	0x88c4, 0xffffffff, 0x000000c2,
	0x28350, 0xffffffff, 0,
	0x9058, 0xffffffff, 0x0fffc40f,
	0x240c, 0xffffffff, 0x00000380,
	0x733c, 0xffffffff, 0x00000002,
	0x2650, 0x00040000, 0,
	0x20bc, 0x00040000, 0,
	0x7300, 0xffffffff, 0x001000f0
};

static const u32 r7xx_golden_dyn_gpr_registers[] =
{
	0x8db0, 0xffffffff, 0x98989898,
	0x8db4, 0xffffffff, 0x98989898,
	0x8db8, 0xffffffff, 0x98989898,
	0x8dbc, 0xffffffff, 0x98989898,
	0x8dc0, 0xffffffff, 0x98989898,
	0x8dc4, 0xffffffff, 0x98989898,
	0x8dc8, 0xffffffff, 0x98989898,
	0x8dcc, 0xffffffff, 0x98989898,
	0x88c4, 0xffffffff, 0x00000082
};

static const u32 rv770_golden_registers[] =
{
	0x562c, 0xffffffff, 0,
	0x3f90, 0xffffffff, 0,
	0x9148, 0xffffffff, 0,
	0x3f94, 0xffffffff, 0,
	0x914c, 0xffffffff, 0,
	0x9698, 0x18000000, 0x18000000
};

static const u32 rv770ce_golden_registers[] =
{
	0x562c, 0xffffffff, 0,
	0x3f90, 0xffffffff, 0x00cc0000,
	0x9148, 0xffffffff, 0x00cc0000,
	0x3f94, 0xffffffff, 0x00cc0000,
	0x914c, 0xffffffff, 0x00cc0000,
	0x9b7c, 0xffffffff, 0x00fa0000,
	0x3f8c, 0xffffffff, 0x00fa0000,
	0x9698, 0x18000000, 0x18000000
};

static const u32 rv770_mgcg_init[] =
{
	0x8bcc, 0xffffffff, 0x130300f9,
	0x5448, 0xffffffff, 0x100,
	0x55e4, 0xffffffff, 0x100,
	0x160c, 0xffffffff, 0x100,
	0x5644, 0xffffffff, 0x100,
	0xc164, 0xffffffff, 0x100,
	0x8a18, 0xffffffff, 0x100,
	0x897c, 0xffffffff, 0x8000100,
	0x8b28, 0xffffffff, 0x3c000100,
	0x9144, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10000,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10001,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10002,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10003,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x0,
	0x9870, 0xffffffff, 0x100,
	0x8d58, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x0,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x1,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x2,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x3,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x4,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x5,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x6,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x7,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x8,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x9,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x8000,
	0x9490, 0xffffffff, 0x0,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x1,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x2,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x3,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x4,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x5,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x6,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x7,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x8,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x9,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x8000,
	0x9604, 0xffffffff, 0x0,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x1,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x2,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x3,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x4,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x5,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x6,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x7,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x8,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x9,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x80000000,
	0x9030, 0xffffffff, 0x100,
	0x9034, 0xffffffff, 0x100,
	0x9038, 0xffffffff, 0x100,
	0x903c, 0xffffffff, 0x100,
	0x9040, 0xffffffff, 0x100,
	0xa200, 0xffffffff, 0x100,
	0xa204, 0xffffffff, 0x100,
	0xa208, 0xffffffff, 0x100,
	0xa20c, 0xffffffff, 0x100,
	0x971c, 0xffffffff, 0x100,
	0x915c, 0xffffffff, 0x00020001,
	0x9160, 0xffffffff, 0x00040003,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00080007,
	0x9174, 0xffffffff, 0x000a0009,
	0x9178, 0xffffffff, 0x000c000b,
	0x917c, 0xffffffff, 0x000e000d,
	0x9180, 0xffffffff, 0x0010000f,
	0x918c, 0xffffffff, 0x00120011,
	0x9190, 0xffffffff, 0x00140013,
	0x9194, 0xffffffff, 0x00020001,
	0x9198, 0xffffffff, 0x00040003,
	0x919c, 0xffffffff, 0x00060005,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000a0009,
	0x91b0, 0xffffffff, 0x000c000b,
	0x91b4, 0xffffffff, 0x000e000d,
	0x91b8, 0xffffffff, 0x0010000f,
	0x91c4, 0xffffffff, 0x00120011,
	0x91c8, 0xffffffff, 0x00140013,
	0x91cc, 0xffffffff, 0x00020001,
	0x91d0, 0xffffffff, 0x00040003,
	0x91d4, 0xffffffff, 0x00060005,
	0x91e0, 0xffffffff, 0x00080007,
	0x91e4, 0xffffffff, 0x000a0009,
	0x91e8, 0xffffffff, 0x000c000b,
	0x91ec, 0xffffffff, 0x00020001,
	0x91f0, 0xffffffff, 0x00040003,
	0x91f4, 0xffffffff, 0x00060005,
	0x9200, 0xffffffff, 0x00080007,
	0x9204, 0xffffffff, 0x000a0009,
	0x9208, 0xffffffff, 0x000c000b,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x0010000f,
	0x921c, 0xffffffff, 0x00120011,
	0x9220, 0xffffffff, 0x00140013,
	0x9224, 0xffffffff, 0x00020001,
	0x9228, 0xffffffff, 0x00040003,
	0x922c, 0xffffffff, 0x00060005,
	0x9238, 0xffffffff, 0x00080007,
	0x923c, 0xffffffff, 0x000a0009,
	0x9240, 0xffffffff, 0x000c000b,
	0x9244, 0xffffffff, 0x000e000d,
	0x9248, 0xffffffff, 0x0010000f,
	0x9254, 0xffffffff, 0x00120011,
	0x9258, 0xffffffff, 0x00140013,
	0x925c, 0xffffffff, 0x00020001,
	0x9260, 0xffffffff, 0x00040003,
	0x9264, 0xffffffff, 0x00060005,
	0x9270, 0xffffffff, 0x00080007,
	0x9274, 0xffffffff, 0x000a0009,
	0x9278, 0xffffffff, 0x000c000b,
	0x927c, 0xffffffff, 0x000e000d,
	0x9280, 0xffffffff, 0x0010000f,
	0x928c, 0xffffffff, 0x00120011,
	0x9290, 0xffffffff, 0x00140013,
	0x9294, 0xffffffff, 0x00020001,
	0x929c, 0xffffffff, 0x00040003,
	0x92a0, 0xffffffff, 0x00060005,
	0x92a4, 0xffffffff, 0x00080007
};

static const u32 rv710_golden_registers[] =
{
	0x3f90, 0x00ff0000, 0x00fc0000,
	0x9148, 0x00ff0000, 0x00fc0000,
	0x3f94, 0x00ff0000, 0x00fc0000,
	0x914c, 0x00ff0000, 0x00fc0000,
	0xb4c, 0x00000020, 0x00000020,
	0xa180, 0xffffffff, 0x00003f3f
};

static const u32 rv710_mgcg_init[] =
{
	0x8bcc, 0xffffffff, 0x13030040,
	0x5448, 0xffffffff, 0x100,
	0x55e4, 0xffffffff, 0x100,
	0x160c, 0xffffffff, 0x100,
	0x5644, 0xffffffff, 0x100,
	0xc164, 0xffffffff, 0x100,
	0x8a18, 0xffffffff, 0x100,
	0x897c, 0xffffffff, 0x8000100,
	0x8b28, 0xffffffff, 0x3c000100,
	0x9144, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10000,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x0,
	0x9870, 0xffffffff, 0x100,
	0x8d58, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x0,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x1,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x8000,
	0x9490, 0xffffffff, 0x0,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x1,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x8000,
	0x9604, 0xffffffff, 0x0,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x1,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x80000000,
	0x9030, 0xffffffff, 0x100,
	0x9034, 0xffffffff, 0x100,
	0x9038, 0xffffffff, 0x100,
	0x903c, 0xffffffff, 0x100,
	0x9040, 0xffffffff, 0x100,
	0xa200, 0xffffffff, 0x100,
	0xa204, 0xffffffff, 0x100,
	0xa208, 0xffffffff, 0x100,
	0xa20c, 0xffffffff, 0x100,
	0x971c, 0xffffffff, 0x100,
	0x915c, 0xffffffff, 0x00020001,
	0x9174, 0xffffffff, 0x00000003,
	0x9178, 0xffffffff, 0x00050001,
	0x917c, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00000004,
	0x9190, 0xffffffff, 0x00070006,
	0x9194, 0xffffffff, 0x00050001,
	0x9198, 0xffffffff, 0x00030002,
	0x91a8, 0xffffffff, 0x00000004,
	0x91ac, 0xffffffff, 0x00070006,
	0x91e8, 0xffffffff, 0x00000001,
	0x9294, 0xffffffff, 0x00000001,
	0x929c, 0xffffffff, 0x00000002,
	0x92a0, 0xffffffff, 0x00040003,
	0x9150, 0xffffffff, 0x4d940000
};

static const u32 rv730_golden_registers[] =
{
	0x3f90, 0x00ff0000, 0x00f00000,
	0x9148, 0x00ff0000, 0x00f00000,
	0x3f94, 0x00ff0000, 0x00f00000,
	0x914c, 0x00ff0000, 0x00f00000,
	0x900c, 0xffffffff, 0x003b033f,
	0xb4c, 0x00000020, 0x00000020,
	0xa180, 0xffffffff, 0x00003f3f
};

static const u32 rv730_mgcg_init[] =
{
	0x8bcc, 0xffffffff, 0x130300f9,
	0x5448, 0xffffffff, 0x100,
	0x55e4, 0xffffffff, 0x100,
	0x160c, 0xffffffff, 0x100,
	0x5644, 0xffffffff, 0x100,
	0xc164, 0xffffffff, 0x100,
	0x8a18, 0xffffffff, 0x100,
	0x897c, 0xffffffff, 0x8000100,
	0x8b28, 0xffffffff, 0x3c000100,
	0x9144, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10000,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10001,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x0,
	0x9870, 0xffffffff, 0x100,
	0x8d58, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x0,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x1,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x2,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x3,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x4,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x5,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x6,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x7,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x8000,
	0x9490, 0xffffffff, 0x0,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x1,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x2,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x3,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x4,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x5,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x6,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x7,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x8000,
	0x9604, 0xffffffff, 0x0,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x1,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x2,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x3,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x4,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x5,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x6,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x7,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x80000000,
	0x9030, 0xffffffff, 0x100,
	0x9034, 0xffffffff, 0x100,
	0x9038, 0xffffffff, 0x100,
	0x903c, 0xffffffff, 0x100,
	0x9040, 0xffffffff, 0x100,
	0xa200, 0xffffffff, 0x100,
	0xa204, 0xffffffff, 0x100,
	0xa208, 0xffffffff, 0x100,
	0xa20c, 0xffffffff, 0x100,
	0x971c, 0xffffffff, 0x100,
	0x915c, 0xffffffff, 0x00020001,
	0x916c, 0xffffffff, 0x00040003,
	0x9170, 0xffffffff, 0x00000005,
	0x9178, 0xffffffff, 0x00050001,
	0x917c, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00000004,
	0x9190, 0xffffffff, 0x00070006,
	0x9194, 0xffffffff, 0x00050001,
	0x9198, 0xffffffff, 0x00030002,
	0x91a8, 0xffffffff, 0x00000004,
	0x91ac, 0xffffffff, 0x00070006,
	0x91b0, 0xffffffff, 0x00050001,
	0x91b4, 0xffffffff, 0x00030002,
	0x91c4, 0xffffffff, 0x00000004,
	0x91c8, 0xffffffff, 0x00070006,
	0x91cc, 0xffffffff, 0x00050001,
	0x91d0, 0xffffffff, 0x00030002,
	0x91e0, 0xffffffff, 0x00000004,
	0x91e4, 0xffffffff, 0x00070006,
	0x91e8, 0xffffffff, 0x00000001,
	0x91ec, 0xffffffff, 0x00050001,
	0x91f0, 0xffffffff, 0x00030002,
	0x9200, 0xffffffff, 0x00000004,
	0x9204, 0xffffffff, 0x00070006,
	0x9208, 0xffffffff, 0x00050001,
	0x920c, 0xffffffff, 0x00030002,
	0x921c, 0xffffffff, 0x00000004,
	0x9220, 0xffffffff, 0x00070006,
	0x9224, 0xffffffff, 0x00050001,
	0x9228, 0xffffffff, 0x00030002,
	0x9238, 0xffffffff, 0x00000004,
	0x923c, 0xffffffff, 0x00070006,
	0x9240, 0xffffffff, 0x00050001,
	0x9244, 0xffffffff, 0x00030002,
	0x9254, 0xffffffff, 0x00000004,
	0x9258, 0xffffffff, 0x00070006,
	0x9294, 0xffffffff, 0x00000001,
	0x929c, 0xffffffff, 0x00000002,
	0x92a0, 0xffffffff, 0x00040003,
	0x92a4, 0xffffffff, 0x00000005
};

static const u32 rv740_golden_registers[] =
{
	0x88c4, 0xffffffff, 0x00000082,
	0x28a50, 0xfffffffc, 0x00000004,
	0x2650, 0x00040000, 0,
	0x20bc, 0x00040000, 0,
	0x733c, 0xffffffff, 0x00000002,
	0x7300, 0xffffffff, 0x001000f0,
	0x3f90, 0x00ff0000, 0,
	0x9148, 0x00ff0000, 0,
	0x3f94, 0x00ff0000, 0,
	0x914c, 0x00ff0000, 0,
	0x240c, 0xffffffff, 0x00000380,
	0x8a14, 0x00000007, 0x00000007,
	0x8b24, 0xffffffff, 0x00ff0fff,
	0x28a4c, 0xffffffff, 0x00004000,
	0xa180, 0xffffffff, 0x00003f3f,
	0x8d00, 0xffffffff, 0x0e0e003a,
	0x8d04, 0xffffffff, 0x013a0e2a,
	0x8c00, 0xffffffff, 0xe400000f,
	0x8db0, 0xffffffff, 0x98989898,
	0x8db4, 0xffffffff, 0x98989898,
	0x8db8, 0xffffffff, 0x98989898,
	0x8dbc, 0xffffffff, 0x98989898,
	0x8dc0, 0xffffffff, 0x98989898,
	0x8dc4, 0xffffffff, 0x98989898,
	0x8dc8, 0xffffffff, 0x98989898,
	0x8dcc, 0xffffffff, 0x98989898,
	0x9058, 0xffffffff, 0x0fffc40f,
	0x900c, 0xffffffff, 0x003b033f,
	0x28350, 0xffffffff, 0,
	0x8cf0, 0x1fffffff, 0x08e00420,
	0x9508, 0xffffffff, 0x00000002,
	0x88c4, 0xffffffff, 0x000000c2,
	0x9698, 0x18000000, 0x18000000
};

static const u32 rv740_mgcg_init[] =
{
	0x8bcc, 0xffffffff, 0x13030100,
	0x5448, 0xffffffff, 0x100,
	0x55e4, 0xffffffff, 0x100,
	0x160c, 0xffffffff, 0x100,
	0x5644, 0xffffffff, 0x100,
	0xc164, 0xffffffff, 0x100,
	0x8a18, 0xffffffff, 0x100,
	0x897c, 0xffffffff, 0x100,
	0x8b28, 0xffffffff, 0x100,
	0x9144, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10000,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10001,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10002,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x10003,
	0x9a50, 0xffffffff, 0x100,
	0x9a1c, 0xffffffff, 0x0,
	0x9870, 0xffffffff, 0x100,
	0x8d58, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x0,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x1,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x2,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x3,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x4,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x5,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x6,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x7,
	0x9510, 0xffffffff, 0x100,
	0x9500, 0xffffffff, 0x8000,
	0x9490, 0xffffffff, 0x0,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x1,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x2,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x3,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x4,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x5,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x6,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x7,
	0x949c, 0xffffffff, 0x100,
	0x9490, 0xffffffff, 0x8000,
	0x9604, 0xffffffff, 0x0,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x1,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x2,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x3,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x4,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x5,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x6,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x7,
	0x9654, 0xffffffff, 0x100,
	0x9604, 0xffffffff, 0x80000000,
	0x9030, 0xffffffff, 0x100,
	0x9034, 0xffffffff, 0x100,
	0x9038, 0xffffffff, 0x100,
	0x903c, 0xffffffff, 0x100,
	0x9040, 0xffffffff, 0x100,
	0xa200, 0xffffffff, 0x100,
	0xa204, 0xffffffff, 0x100,
	0xa208, 0xffffffff, 0x100,
	0xa20c, 0xffffffff, 0x100,
	0x971c, 0xffffffff, 0x100,
	0x915c, 0xffffffff, 0x00020001,
	0x9160, 0xffffffff, 0x00040003,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00080007,
	0x9174, 0xffffffff, 0x000a0009,
	0x9178, 0xffffffff, 0x000c000b,
	0x917c, 0xffffffff, 0x000e000d,
	0x9180, 0xffffffff, 0x0010000f,
	0x918c, 0xffffffff, 0x00120011,
	0x9190, 0xffffffff, 0x00140013,
	0x9194, 0xffffffff, 0x00020001,
	0x9198, 0xffffffff, 0x00040003,
	0x919c, 0xffffffff, 0x00060005,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000a0009,
	0x91b0, 0xffffffff, 0x000c000b,
	0x91b4, 0xffffffff, 0x000e000d,
	0x91b8, 0xffffffff, 0x0010000f,
	0x91c4, 0xffffffff, 0x00120011,
	0x91c8, 0xffffffff, 0x00140013,
	0x91cc, 0xffffffff, 0x00020001,
	0x91d0, 0xffffffff, 0x00040003,
	0x91d4, 0xffffffff, 0x00060005,
	0x91e0, 0xffffffff, 0x00080007,
	0x91e4, 0xffffffff, 0x000a0009,
	0x91e8, 0xffffffff, 0x000c000b,
	0x91ec, 0xffffffff, 0x00020001,
	0x91f0, 0xffffffff, 0x00040003,
	0x91f4, 0xffffffff, 0x00060005,
	0x9200, 0xffffffff, 0x00080007,
	0x9204, 0xffffffff, 0x000a0009,
	0x9208, 0xffffffff, 0x000c000b,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x0010000f,
	0x921c, 0xffffffff, 0x00120011,
	0x9220, 0xffffffff, 0x00140013,
	0x9224, 0xffffffff, 0x00020001,
	0x9228, 0xffffffff, 0x00040003,
	0x922c, 0xffffffff, 0x00060005,
	0x9238, 0xffffffff, 0x00080007,
	0x923c, 0xffffffff, 0x000a0009,
	0x9240, 0xffffffff, 0x000c000b,
	0x9244, 0xffffffff, 0x000e000d,
	0x9248, 0xffffffff, 0x0010000f,
	0x9254, 0xffffffff, 0x00120011,
	0x9258, 0xffffffff, 0x00140013,
	0x9294, 0xffffffff, 0x00020001,
	0x929c, 0xffffffff, 0x00040003,
	0x92a0, 0xffffffff, 0x00060005,
	0x92a4, 0xffffffff, 0x00080007
};

static void rv770_init_golden_registers(struct radeon_device *rdev)
{
	switch (rdev->family) {
	case CHIP_RV770:
		radeon_program_register_sequence(rdev,
						 r7xx_golden_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_registers));
		radeon_program_register_sequence(rdev,
						 r7xx_golden_dyn_gpr_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_dyn_gpr_registers));
		if (rdev->pdev->device == 0x994e)
			radeon_program_register_sequence(rdev,
							 rv770ce_golden_registers,
							 (const u32)ARRAY_SIZE(rv770ce_golden_registers));
		else
			radeon_program_register_sequence(rdev,
							 rv770_golden_registers,
							 (const u32)ARRAY_SIZE(rv770_golden_registers));
		radeon_program_register_sequence(rdev,
						 rv770_mgcg_init,
						 (const u32)ARRAY_SIZE(rv770_mgcg_init));
		break;
	case CHIP_RV730:
		radeon_program_register_sequence(rdev,
						 r7xx_golden_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_registers));
		radeon_program_register_sequence(rdev,
						 r7xx_golden_dyn_gpr_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_dyn_gpr_registers));
		radeon_program_register_sequence(rdev,
						 rv730_golden_registers,
						 (const u32)ARRAY_SIZE(rv730_golden_registers));
		radeon_program_register_sequence(rdev,
						 rv730_mgcg_init,
						 (const u32)ARRAY_SIZE(rv730_mgcg_init));
		break;
	case CHIP_RV710:
		radeon_program_register_sequence(rdev,
						 r7xx_golden_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_registers));
		radeon_program_register_sequence(rdev,
						 r7xx_golden_dyn_gpr_registers,
						 (const u32)ARRAY_SIZE(r7xx_golden_dyn_gpr_registers));
		radeon_program_register_sequence(rdev,
						 rv710_golden_registers,
						 (const u32)ARRAY_SIZE(rv710_golden_registers));
		radeon_program_register_sequence(rdev,
						 rv710_mgcg_init,
						 (const u32)ARRAY_SIZE(rv710_mgcg_init));
		break;
	case CHIP_RV740:
		radeon_program_register_sequence(rdev,
						 rv740_golden_registers,
						 (const u32)ARRAY_SIZE(rv740_golden_registers));
		radeon_program_register_sequence(rdev,
						 rv740_mgcg_init,
						 (const u32)ARRAY_SIZE(rv740_mgcg_init));
		break;
	default:
		break;
	}
}

#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)

/**
 * rv770_get_xclk - get the xclk
 *
 * @rdev: radeon_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (r7xx-cayman).
 */
u32 rv770_get_xclk(struct radeon_device *rdev)
{
	u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 tmp = RREG32(CG_CLKPIN_CNTL);

	if (tmp & MUX_TCLK_TO_XCLK)
		return TCLK;

	if (tmp & XTALIN_DIVIDE)
		return reference_clock / 4;

	return reference_clock;
}

void rv770_page_flip(struct radeon_device *rdev, int crtc_id, u64 crtc_base, bool async)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];
	u32 tmp = RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset);
	int i;

	/* Lock the graphics update lock */
	tmp |= AVIVO_D1GRPH_UPDATE_LOCK;
	WREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset, tmp);

	/* update the scanout addresses */
	WREG32(AVIVO_D1GRPH_FLIP_CONTROL + radeon_crtc->crtc_offset,
	       async ? AVIVO_D1GRPH_SURFACE_UPDATE_H_RETRACE_EN : 0);
	if (radeon_crtc->crtc_id) {
		WREG32(D2GRPH_SECONDARY_SURFACE_ADDRESS_HIGH, upper_32_bits(crtc_base));
		WREG32(D2GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, upper_32_bits(crtc_base));
	} else {
		WREG32(D1GRPH_SECONDARY_SURFACE_ADDRESS_HIGH, upper_32_bits(crtc_base));
		WREG32(D1GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, upper_32_bits(crtc_base));
	}
	WREG32(D1GRPH_SECONDARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32)crtc_base);
	WREG32(D1GRPH_PRIMARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32)crtc_base);

	/* Wait for update_pending to go high. */
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset) & AVIVO_D1GRPH_SURFACE_UPDATE_PENDING)
			break;
		udelay(1);
	}
	DRM_DEBUG("Update pending now high. Unlocking vupdate_lock.\n");

	/* Unlock the lock, so double-buffering can take place inside vblank */
	tmp &= ~AVIVO_D1GRPH_UPDATE_LOCK;
	WREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset, tmp);
}

bool rv770_page_flip_pending(struct radeon_device *rdev, int crtc_id)
{
	struct radeon_crtc *radeon_crtc = rdev->mode_info.crtcs[crtc_id];

	/* Return current update_pending status: */
	return !!(RREG32(AVIVO_D1GRPH_UPDATE + radeon_crtc->crtc_offset) &
		AVIVO_D1GRPH_SURFACE_UPDATE_PENDING);
}

/* get temperature in millidegrees */
int rv770_get_temp(struct radeon_device *rdev)
{
	u32 temp = (RREG32(CG_MULT_THERMAL_STATUS) & ASIC_T_MASK) >>
		ASIC_T_SHIFT;
	int actual_temp;

	if (temp & 0x400)
		actual_temp = -256;
	else if (temp & 0x200)
		actual_temp = 255;
	else if (temp & 0x100) {
		actual_temp = temp & 0x1ff;
		actual_temp |= ~0x1ff;
	} else
		actual_temp = temp & 0xff;

	return (actual_temp * 1000) / 2;
}

void rv770_pm_misc(struct radeon_device *rdev)
{
	int req_ps_idx = rdev->pm.requested_power_state_index;
	int req_cm_idx = rdev->pm.requested_clock_mode_index;
	struct radeon_power_state *ps = &rdev->pm.power_state[req_ps_idx];
	struct radeon_voltage *voltage = &ps->clock_info[req_cm_idx].voltage;

	if ((voltage->type == VOLTAGE_SW) && voltage->voltage) {
		/* 0xff01 is a flag rather then an actual voltage */
		if (voltage->voltage == 0xff01)
			return;
		if (voltage->voltage != rdev->pm.current_vddc) {
			radeon_atom_set_voltage(rdev, voltage->voltage, SET_VOLTAGE_TYPE_ASIC_VDDC);
			rdev->pm.current_vddc = voltage->voltage;
			DRM_DEBUG("Setting: v: %d\n", voltage->voltage);
		}
	}
}

/*
 * GART
 */
static int rv770_pcie_gart_enable(struct radeon_device *rdev)
{
	u32 tmp;
	int r, i;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE | ENABLE_L2_FRAGMENT_PROCESSING |
				ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, BANK_SELECT(0) | CACHE_UPDATE_MODE(2));
	/* Setup TLB control */
	tmp = ENABLE_L1_TLB | ENABLE_L1_FRAGMENT_PROCESSING |
		SYSTEM_ACCESS_MODE_NOT_IN_SYS |
		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU |
		EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5);
	WREG32(MC_VM_MD_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB2_CNTL, tmp);
	if (rdev->family == CHIP_RV740)
		WREG32(MC_VM_MD_L1_TLB3_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB2_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB3_CNTL, tmp);
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	for (i = 1; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);

	r600_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

static void rv770_pcie_gart_disable(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	/* Disable all tables */
	for (i = 0; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);

	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, BANK_SELECT(0) | CACHE_UPDATE_MODE(2));
	/* Setup TLB control */
	tmp = EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5);
	WREG32(MC_VM_MD_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB2_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB2_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB3_CNTL, tmp);
	radeon_gart_table_vram_unpin(rdev);
}

static void rv770_pcie_gart_fini(struct radeon_device *rdev)
{
	radeon_gart_fini(rdev);
	rv770_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
}


static void rv770_agp_enable(struct radeon_device *rdev)
{
	u32 tmp;
	int i;

	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE | ENABLE_L2_FRAGMENT_PROCESSING |
				ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
				EFFECTIVE_L2_QUEUE_SIZE(7));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, BANK_SELECT(0) | CACHE_UPDATE_MODE(2));
	/* Setup TLB control */
	tmp = ENABLE_L1_TLB | ENABLE_L1_FRAGMENT_PROCESSING |
		SYSTEM_ACCESS_MODE_NOT_IN_SYS |
		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU |
		EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5);
	WREG32(MC_VM_MD_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MD_L1_TLB2_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB0_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB1_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB2_CNTL, tmp);
	WREG32(MC_VM_MB_L1_TLB3_CNTL, tmp);
	for (i = 0; i < 7; i++)
		WREG32(VM_CONTEXT0_CNTL + (i * 4), 0);
}

static void rv770_mc_program(struct radeon_device *rdev)
{
	struct rv515_mc_save save;
	u32 tmp;
	int i, j;

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}
	/* r7xx hw bug.  Read from HDP_DEBUG1 rather
	 * than writing to HDP_REG_COHERENCY_FLUSH_CNTL
	 */
	tmp = RREG32(HDP_DEBUG1);

	rv515_mc_stop(rdev, &save);
	if (r600_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	/* Lockout access through VGA aperture*/
	WREG32(VGA_HDP_CONTROL, VGA_MEMORY_DISABLE);
	/* Update configuration */
	if (rdev->flags & RADEON_IS_AGP) {
		if (rdev->mc.vram_start < rdev->mc.gtt_start) {
			/* VRAM before AGP */
			WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
				rdev->mc.vram_start >> 12);
			WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
				rdev->mc.gtt_end >> 12);
		} else {
			/* VRAM after AGP */
			WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
				rdev->mc.gtt_start >> 12);
			WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
				rdev->mc.vram_end >> 12);
		}
	} else {
		WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
			rdev->mc.vram_start >> 12);
		WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			rdev->mc.vram_end >> 12);
	}
	WREG32(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, rdev->vram_scratch.gpu_addr >> 12);
	tmp = ((rdev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((rdev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(MC_VM_FB_LOCATION, tmp);
	WREG32(HDP_NONSURFACE_BASE, (rdev->mc.vram_start >> 8));
	WREG32(HDP_NONSURFACE_INFO, (2 << 7));
	WREG32(HDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	if (rdev->flags & RADEON_IS_AGP) {
		WREG32(MC_VM_AGP_TOP, rdev->mc.gtt_end >> 16);
		WREG32(MC_VM_AGP_BOT, rdev->mc.gtt_start >> 16);
		WREG32(MC_VM_AGP_BASE, rdev->mc.agp_base >> 22);
	} else {
		WREG32(MC_VM_AGP_BASE, 0);
		WREG32(MC_VM_AGP_TOP, 0x0FFFFFFF);
		WREG32(MC_VM_AGP_BOT, 0x0FFFFFFF);
	}
	if (r600_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	rv515_mc_resume(rdev, &save);
	/* we need to own VRAM, so turn off the VGA renderer here
	 * to stop it overwriting our objects */
	rv515_vga_render_disable(rdev);
}


/*
 * CP.
 */
void r700_cp_stop(struct radeon_device *rdev)
{
	if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
	WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT));
	WREG32(SCRATCH_UMSK, 0);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
}

static int rv770_cp_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data;
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw)
		return -EINVAL;

	r700_cp_stop(rdev);
	WREG32(CP_RB_CNTL,
#ifdef __BIG_ENDIAN
	       BUF_SWAP_32BIT |
#endif
	       RB_NO_UPDATE | RB_BLKSZ(15) | RB_BUFSZ(3));

	/* Reset cp */
	WREG32(GRBM_SOFT_RESET, SOFT_RESET_CP);
	RREG32(GRBM_SOFT_RESET);
	mdelay(15);
	WREG32(GRBM_SOFT_RESET, 0);

	fw_data = (const __be32 *)rdev->pfp_fw->data;
	WREG32(CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < R700_PFP_UCODE_SIZE; i++)
		WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
	WREG32(CP_PFP_UCODE_ADDR, 0);

	fw_data = (const __be32 *)rdev->me_fw->data;
	WREG32(CP_ME_RAM_WADDR, 0);
	for (i = 0; i < R700_PM4_UCODE_SIZE; i++)
		WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

void r700_cp_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r700_cp_stop(rdev);
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);
}

void rv770_set_clk_bypass_mode(struct radeon_device *rdev)
{
	u32 tmp, i;

	if (rdev->flags & RADEON_IS_IGP)
		return;

	tmp = RREG32(CG_SPLL_FUNC_CNTL_2);
	tmp &= SCLK_MUX_SEL_MASK;
	tmp |= SCLK_MUX_SEL(1) | SCLK_MUX_UPDATE;
	WREG32(CG_SPLL_FUNC_CNTL_2, tmp);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(CG_SPLL_STATUS) & SPLL_CHG_STATUS)
			break;
		udelay(1);
	}

	tmp &= ~SCLK_MUX_UPDATE;
	WREG32(CG_SPLL_FUNC_CNTL_2, tmp);

	tmp = RREG32(MPLL_CNTL_MODE);
	if ((rdev->family == CHIP_RV710) || (rdev->family == CHIP_RV730))
		tmp &= ~RV730_MPLL_MCLK_SEL;
	else
		tmp &= ~MPLL_MCLK_SEL;
	WREG32(MPLL_CNTL_MODE, tmp);
}

/*
 * Core functions
 */
static void rv770_gpu_init(struct radeon_device *rdev)
{
	int i, j, num_qd_pipes;
	u32 ta_aux_cntl;
	u32 sx_debug_1;
	u32 smx_dc_ctl0;
	u32 db_debug3;
	u32 num_gs_verts_per_thread;
	u32 vgt_gs_per_es;
	u32 gs_prim_buffer_depth = 0;
	u32 sq_ms_fifo_sizes;
	u32 sq_config;
	u32 sq_thread_resource_mgmt;
	u32 hdp_host_path_cntl;
	u32 sq_dyn_gpr_size_simd_ab_0;
	u32 gb_tiling_config = 0;
	u32 cc_gc_shader_pipe_config = 0;
	u32 mc_arb_ramcfg;
	u32 db_debug4, tmp;
	u32 inactive_pipes, shader_pipe_config;
	u32 disabled_rb_mask;
	unsigned active_number;

	/* setup chip specs */
	rdev->config.rv770.tiling_group_size = 256;
	switch (rdev->family) {
	case CHIP_RV770:
		rdev->config.rv770.max_pipes = 4;
		rdev->config.rv770.max_tile_pipes = 8;
		rdev->config.rv770.max_simds = 10;
		rdev->config.rv770.max_backends = 4;
		rdev->config.rv770.max_gprs = 256;
		rdev->config.rv770.max_threads = 248;
		rdev->config.rv770.max_stack_entries = 512;
		rdev->config.rv770.max_hw_contexts = 8;
		rdev->config.rv770.max_gs_threads = 16 * 2;
		rdev->config.rv770.sx_max_export_size = 128;
		rdev->config.rv770.sx_max_export_pos_size = 16;
		rdev->config.rv770.sx_max_export_smx_size = 112;
		rdev->config.rv770.sq_num_cf_insts = 2;

		rdev->config.rv770.sx_num_of_sets = 7;
		rdev->config.rv770.sc_prim_fifo_size = 0xF9;
		rdev->config.rv770.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.rv770.sc_earlyz_tile_fifo_fize = 0x130;
		break;
	case CHIP_RV730:
		rdev->config.rv770.max_pipes = 2;
		rdev->config.rv770.max_tile_pipes = 4;
		rdev->config.rv770.max_simds = 8;
		rdev->config.rv770.max_backends = 2;
		rdev->config.rv770.max_gprs = 128;
		rdev->config.rv770.max_threads = 248;
		rdev->config.rv770.max_stack_entries = 256;
		rdev->config.rv770.max_hw_contexts = 8;
		rdev->config.rv770.max_gs_threads = 16 * 2;
		rdev->config.rv770.sx_max_export_size = 256;
		rdev->config.rv770.sx_max_export_pos_size = 32;
		rdev->config.rv770.sx_max_export_smx_size = 224;
		rdev->config.rv770.sq_num_cf_insts = 2;

		rdev->config.rv770.sx_num_of_sets = 7;
		rdev->config.rv770.sc_prim_fifo_size = 0xf9;
		rdev->config.rv770.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.rv770.sc_earlyz_tile_fifo_fize = 0x130;
		if (rdev->config.rv770.sx_max_export_pos_size > 16) {
			rdev->config.rv770.sx_max_export_pos_size -= 16;
			rdev->config.rv770.sx_max_export_smx_size += 16;
		}
		break;
	case CHIP_RV710:
		rdev->config.rv770.max_pipes = 2;
		rdev->config.rv770.max_tile_pipes = 2;
		rdev->config.rv770.max_simds = 2;
		rdev->config.rv770.max_backends = 1;
		rdev->config.rv770.max_gprs = 256;
		rdev->config.rv770.max_threads = 192;
		rdev->config.rv770.max_stack_entries = 256;
		rdev->config.rv770.max_hw_contexts = 4;
		rdev->config.rv770.max_gs_threads = 8 * 2;
		rdev->config.rv770.sx_max_export_size = 128;
		rdev->config.rv770.sx_max_export_pos_size = 16;
		rdev->config.rv770.sx_max_export_smx_size = 112;
		rdev->config.rv770.sq_num_cf_insts = 1;

		rdev->config.rv770.sx_num_of_sets = 7;
		rdev->config.rv770.sc_prim_fifo_size = 0x40;
		rdev->config.rv770.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.rv770.sc_earlyz_tile_fifo_fize = 0x130;
		break;
	case CHIP_RV740:
		rdev->config.rv770.max_pipes = 4;
		rdev->config.rv770.max_tile_pipes = 4;
		rdev->config.rv770.max_simds = 8;
		rdev->config.rv770.max_backends = 4;
		rdev->config.rv770.max_gprs = 256;
		rdev->config.rv770.max_threads = 248;
		rdev->config.rv770.max_stack_entries = 512;
		rdev->config.rv770.max_hw_contexts = 8;
		rdev->config.rv770.max_gs_threads = 16 * 2;
		rdev->config.rv770.sx_max_export_size = 256;
		rdev->config.rv770.sx_max_export_pos_size = 32;
		rdev->config.rv770.sx_max_export_smx_size = 224;
		rdev->config.rv770.sq_num_cf_insts = 2;

		rdev->config.rv770.sx_num_of_sets = 7;
		rdev->config.rv770.sc_prim_fifo_size = 0x100;
		rdev->config.rv770.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.rv770.sc_earlyz_tile_fifo_fize = 0x130;

		if (rdev->config.rv770.sx_max_export_pos_size > 16) {
			rdev->config.rv770.sx_max_export_pos_size -= 16;
			rdev->config.rv770.sx_max_export_smx_size += 16;
		}
		break;
	default:
		break;
	}

	/* Initialize HDP */
	j = 0;
	for (i = 0; i < 32; i++) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
		j += 0x18;
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));

	/* setup tiling, simd, pipe config */
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	shader_pipe_config = RREG32(CC_GC_SHADER_PIPE_CONFIG);
	inactive_pipes = (shader_pipe_config & INACTIVE_QD_PIPES_MASK) >> INACTIVE_QD_PIPES_SHIFT;
	for (i = 0, tmp = 1, active_number = 0; i < R7XX_MAX_PIPES; i++) {
		if (!(inactive_pipes & tmp)) {
			active_number++;
		}
		tmp <<= 1;
	}
	if (active_number == 1) {
		WREG32(SPI_CONFIG_CNTL, DISABLE_INTERP_1);
	} else {
		WREG32(SPI_CONFIG_CNTL, 0);
	}

	cc_gc_shader_pipe_config = RREG32(CC_GC_SHADER_PIPE_CONFIG) & 0xffffff00;
	tmp = rdev->config.rv770.max_simds -
		r600_count_pipe_bits((cc_gc_shader_pipe_config >> 16) & R7XX_MAX_SIMDS_MASK);
	rdev->config.rv770.active_simds = tmp;

	switch (rdev->config.rv770.max_tile_pipes) {
	case 1:
	default:
		gb_tiling_config = PIPE_TILING(0);
		break;
	case 2:
		gb_tiling_config = PIPE_TILING(1);
		break;
	case 4:
		gb_tiling_config = PIPE_TILING(2);
		break;
	case 8:
		gb_tiling_config = PIPE_TILING(3);
		break;
	}
	rdev->config.rv770.tiling_npipes = rdev->config.rv770.max_tile_pipes;

	disabled_rb_mask = (RREG32(CC_RB_BACKEND_DISABLE) >> 16) & R7XX_MAX_BACKENDS_MASK;
	tmp = 0;
	for (i = 0; i < rdev->config.rv770.max_backends; i++)
		tmp |= (1 << i);
	/* if all the backends are disabled, fix it up here */
	if ((disabled_rb_mask & tmp) == tmp) {
		for (i = 0; i < rdev->config.rv770.max_backends; i++)
			disabled_rb_mask &= ~(1 << i);
	}
	tmp = (gb_tiling_config & PIPE_TILING__MASK) >> PIPE_TILING__SHIFT;
	tmp = r6xx_remap_render_backend(rdev, tmp, rdev->config.rv770.max_backends,
					R7XX_MAX_BACKENDS, disabled_rb_mask);
	gb_tiling_config |= tmp << 16;
	rdev->config.rv770.backend_map = tmp;

	if (rdev->family == CHIP_RV770)
		gb_tiling_config |= BANK_TILING(1);
	else {
		if ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT)
			gb_tiling_config |= BANK_TILING(1);
		else
			gb_tiling_config |= BANK_TILING(0);
	}
	rdev->config.rv770.tiling_nbanks = 4 << ((gb_tiling_config >> 4) & 0x3);
	gb_tiling_config |= GROUP_SIZE((mc_arb_ramcfg & BURSTLENGTH_MASK) >> BURSTLENGTH_SHIFT);
	if (((mc_arb_ramcfg & NOOFROWS_MASK) >> NOOFROWS_SHIFT) > 3) {
		gb_tiling_config |= ROW_TILING(3);
		gb_tiling_config |= SAMPLE_SPLIT(3);
	} else {
		gb_tiling_config |=
			ROW_TILING(((mc_arb_ramcfg & NOOFROWS_MASK) >> NOOFROWS_SHIFT));
		gb_tiling_config |=
			SAMPLE_SPLIT(((mc_arb_ramcfg & NOOFROWS_MASK) >> NOOFROWS_SHIFT));
	}

	gb_tiling_config |= BANK_SWAPS(1);
	rdev->config.rv770.tile_config = gb_tiling_config;

	WREG32(GB_TILING_CONFIG, gb_tiling_config);
	WREG32(DCP_TILING_CONFIG, (gb_tiling_config & 0xffff));
	WREG32(HDP_TILING_CONFIG, (gb_tiling_config & 0xffff));
	WREG32(DMA_TILING_CONFIG, (gb_tiling_config & 0xffff));
	WREG32(DMA_TILING_CONFIG2, (gb_tiling_config & 0xffff));
	if (rdev->family == CHIP_RV730) {
		WREG32(UVD_UDEC_DB_TILING_CONFIG, (gb_tiling_config & 0xffff));
		WREG32(UVD_UDEC_DBW_TILING_CONFIG, (gb_tiling_config & 0xffff));
		WREG32(UVD_UDEC_TILING_CONFIG, (gb_tiling_config & 0xffff));
	}

	WREG32(CGTS_SYS_TCC_DISABLE, 0);
	WREG32(CGTS_TCC_DISABLE, 0);
	WREG32(CGTS_USER_SYS_TCC_DISABLE, 0);
	WREG32(CGTS_USER_TCC_DISABLE, 0);


	num_qd_pipes = R7XX_MAX_PIPES - r600_count_pipe_bits((cc_gc_shader_pipe_config & INACTIVE_QD_PIPES_MASK) >> 8);
	WREG32(VGT_OUT_DEALLOC_CNTL, (num_qd_pipes * 4) & DEALLOC_DIST_MASK);
	WREG32(VGT_VERTEX_REUSE_BLOCK_CNTL, ((num_qd_pipes * 4) - 2) & VTX_REUSE_DEPTH_MASK);

	/* set HW defaults for 3D engine */
	WREG32(CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) |
				     ROQ_IB2_START(0x2b)));

	WREG32(CP_MEQ_THRESHOLDS, STQ_SPLIT(0x30));

	ta_aux_cntl = RREG32(TA_CNTL_AUX);
	WREG32(TA_CNTL_AUX, ta_aux_cntl | DISABLE_CUBE_ANISO);

	sx_debug_1 = RREG32(SX_DEBUG_1);
	sx_debug_1 |= ENABLE_NEW_SMX_ADDRESS;
	WREG32(SX_DEBUG_1, sx_debug_1);

	smx_dc_ctl0 = RREG32(SMX_DC_CTL0);
	smx_dc_ctl0 &= ~CACHE_DEPTH(0x1ff);
	smx_dc_ctl0 |= CACHE_DEPTH((rdev->config.rv770.sx_num_of_sets * 64) - 1);
	WREG32(SMX_DC_CTL0, smx_dc_ctl0);

	if (rdev->family != CHIP_RV740)
		WREG32(SMX_EVENT_CTL, (ES_FLUSH_CTL(4) |
				       GS_FLUSH_CTL(4) |
				       ACK_FLUSH_CTL(3) |
				       SYNC_FLUSH_CTL));

	if (rdev->family != CHIP_RV770)
		WREG32(SMX_SAR_CTL0, 0x00003f3f);

	db_debug3 = RREG32(DB_DEBUG3);
	db_debug3 &= ~DB_CLK_OFF_DELAY(0x1f);
	switch (rdev->family) {
	case CHIP_RV770:
	case CHIP_RV740:
		db_debug3 |= DB_CLK_OFF_DELAY(0x1f);
		break;
	case CHIP_RV710:
	case CHIP_RV730:
	default:
		db_debug3 |= DB_CLK_OFF_DELAY(2);
		break;
	}
	WREG32(DB_DEBUG3, db_debug3);

	if (rdev->family != CHIP_RV770) {
		db_debug4 = RREG32(DB_DEBUG4);
		db_debug4 |= DISABLE_TILE_COVERED_FOR_PS_ITER;
		WREG32(DB_DEBUG4, db_debug4);
	}

	WREG32(SX_EXPORT_BUFFER_SIZES, (COLOR_BUFFER_SIZE((rdev->config.rv770.sx_max_export_size / 4) - 1) |
					POSITION_BUFFER_SIZE((rdev->config.rv770.sx_max_export_pos_size / 4) - 1) |
					SMX_BUFFER_SIZE((rdev->config.rv770.sx_max_export_smx_size / 4) - 1)));

	WREG32(PA_SC_FIFO_SIZE, (SC_PRIM_FIFO_SIZE(rdev->config.rv770.sc_prim_fifo_size) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.rv770.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.rv770.sc_earlyz_tile_fifo_fize)));

	WREG32(PA_SC_MULTI_CHIP_CNTL, 0);

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(CP_PERFMON_CNTL, 0);

	sq_ms_fifo_sizes = (CACHE_FIFO_SIZE(16 * rdev->config.rv770.sq_num_cf_insts) |
			    DONE_FIFO_HIWATER(0xe0) |
			    ALU_UPDATE_FIFO_HIWATER(0x8));
	switch (rdev->family) {
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
		sq_ms_fifo_sizes |= FETCH_FIFO_HIWATER(0x1);
		break;
	case CHIP_RV740:
	default:
		sq_ms_fifo_sizes |= FETCH_FIFO_HIWATER(0x4);
		break;
	}
	WREG32(SQ_MS_FIFO_SIZES, sq_ms_fifo_sizes);

	/* SQ_CONFIG, SQ_GPR_RESOURCE_MGMT, SQ_THREAD_RESOURCE_MGMT, SQ_STACK_RESOURCE_MGMT
	 * should be adjusted as needed by the 2D/3D drivers.  This just sets default values
	 */
	sq_config = RREG32(SQ_CONFIG);
	sq_config &= ~(PS_PRIO(3) |
		       VS_PRIO(3) |
		       GS_PRIO(3) |
		       ES_PRIO(3));
	sq_config |= (DX9_CONSTS |
		      VC_ENABLE |
		      EXPORT_SRC_C |
		      PS_PRIO(0) |
		      VS_PRIO(1) |
		      GS_PRIO(2) |
		      ES_PRIO(3));
	if (rdev->family == CHIP_RV710)
		/* no vertex cache */
		sq_config &= ~VC_ENABLE;

	WREG32(SQ_CONFIG, sq_config);

	WREG32(SQ_GPR_RESOURCE_MGMT_1,  (NUM_PS_GPRS((rdev->config.rv770.max_gprs * 24)/64) |
					 NUM_VS_GPRS((rdev->config.rv770.max_gprs * 24)/64) |
					 NUM_CLAUSE_TEMP_GPRS(((rdev->config.rv770.max_gprs * 24)/64)/2)));

	WREG32(SQ_GPR_RESOURCE_MGMT_2,  (NUM_GS_GPRS((rdev->config.rv770.max_gprs * 7)/64) |
					 NUM_ES_GPRS((rdev->config.rv770.max_gprs * 7)/64)));

	sq_thread_resource_mgmt = (NUM_PS_THREADS((rdev->config.rv770.max_threads * 4)/8) |
				   NUM_VS_THREADS((rdev->config.rv770.max_threads * 2)/8) |
				   NUM_ES_THREADS((rdev->config.rv770.max_threads * 1)/8));
	if (((rdev->config.rv770.max_threads * 1) / 8) > rdev->config.rv770.max_gs_threads)
		sq_thread_resource_mgmt |= NUM_GS_THREADS(rdev->config.rv770.max_gs_threads);
	else
		sq_thread_resource_mgmt |= NUM_GS_THREADS((rdev->config.rv770.max_gs_threads * 1)/8);
	WREG32(SQ_THREAD_RESOURCE_MGMT, sq_thread_resource_mgmt);

	WREG32(SQ_STACK_RESOURCE_MGMT_1, (NUM_PS_STACK_ENTRIES((rdev->config.rv770.max_stack_entries * 1)/4) |
						     NUM_VS_STACK_ENTRIES((rdev->config.rv770.max_stack_entries * 1)/4)));

	WREG32(SQ_STACK_RESOURCE_MGMT_2, (NUM_GS_STACK_ENTRIES((rdev->config.rv770.max_stack_entries * 1)/4) |
						     NUM_ES_STACK_ENTRIES((rdev->config.rv770.max_stack_entries * 1)/4)));

	sq_dyn_gpr_size_simd_ab_0 = (SIMDA_RING0((rdev->config.rv770.max_gprs * 38)/64) |
				     SIMDA_RING1((rdev->config.rv770.max_gprs * 38)/64) |
				     SIMDB_RING0((rdev->config.rv770.max_gprs * 38)/64) |
				     SIMDB_RING1((rdev->config.rv770.max_gprs * 38)/64));

	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_0, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_1, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_2, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_3, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_4, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_5, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_6, sq_dyn_gpr_size_simd_ab_0);
	WREG32(SQ_DYN_GPR_SIZE_SIMD_AB_7, sq_dyn_gpr_size_simd_ab_0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	if (rdev->family == CHIP_RV710)
		WREG32(VGT_CACHE_INVALIDATION, (CACHE_INVALIDATION(TC_ONLY) |
						AUTO_INVLD_EN(ES_AND_GS_AUTO)));
	else
		WREG32(VGT_CACHE_INVALIDATION, (CACHE_INVALIDATION(VC_AND_TC) |
						AUTO_INVLD_EN(ES_AND_GS_AUTO)));

	switch (rdev->family) {
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV740:
		gs_prim_buffer_depth = 384;
		break;
	case CHIP_RV710:
		gs_prim_buffer_depth = 128;
		break;
	default:
		break;
	}

	num_gs_verts_per_thread = rdev->config.rv770.max_pipes * 16;
	vgt_gs_per_es = gs_prim_buffer_depth + num_gs_verts_per_thread;
	/* Max value for this is 256 */
	if (vgt_gs_per_es > 256)
		vgt_gs_per_es = 256;

	WREG32(VGT_ES_PER_GS, 128);
	WREG32(VGT_GS_PER_ES, vgt_gs_per_es);
	WREG32(VGT_GS_PER_VS, 2);

	/* more default values. 2D/3D driver should adjust as needed */
	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);
	WREG32(VGT_STRMOUT_EN, 0);
	WREG32(SX_MISC, 0);
	WREG32(PA_SC_MODE_CNTL, 0);
	WREG32(PA_SC_EDGERULE, 0xaaaaaaaa);
	WREG32(PA_SC_AA_CONFIG, 0);
	WREG32(PA_SC_CLIPRECT_RULE, 0xffff);
	WREG32(PA_SC_LINE_STIPPLE, 0);
	WREG32(SPI_INPUT_Z, 0);
	WREG32(SPI_PS_IN_CONTROL_0, NUM_INTERP(2));
	WREG32(CB_COLOR7_FRAG, 0);

	/* clear render buffer base addresses */
	WREG32(CB_COLOR0_BASE, 0);
	WREG32(CB_COLOR1_BASE, 0);
	WREG32(CB_COLOR2_BASE, 0);
	WREG32(CB_COLOR3_BASE, 0);
	WREG32(CB_COLOR4_BASE, 0);
	WREG32(CB_COLOR5_BASE, 0);
	WREG32(CB_COLOR6_BASE, 0);
	WREG32(CB_COLOR7_BASE, 0);

	WREG32(TCP_CNTL, 0);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_SC_MULTI_CHIP_CNTL, 0);

	WREG32(PA_CL_ENHANCE, (CLIP_VTX_REORDER_ENA |
					  NUM_CLIP_SEQ(3)));
	WREG32(VC_ENHANCE, 0);
}

void r700_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc)
{
	u64 size_bf, size_af;

	if (mc->mc_vram_size > 0xE0000000) {
		/* leave room for at least 512M GTT */
		dev_warn(rdev->dev, "limiting VRAM\n");
		mc->real_vram_size = 0xE0000000;
		mc->mc_vram_size = 0xE0000000;
	}
	if (rdev->flags & RADEON_IS_AGP) {
		size_bf = mc->gtt_start;
		size_af = mc->mc_mask - mc->gtt_end;
		if (size_bf > size_af) {
			if (mc->mc_vram_size > size_bf) {
				dev_warn(rdev->dev, "limiting VRAM\n");
				mc->real_vram_size = size_bf;
				mc->mc_vram_size = size_bf;
			}
			mc->vram_start = mc->gtt_start - mc->mc_vram_size;
		} else {
			if (mc->mc_vram_size > size_af) {
				dev_warn(rdev->dev, "limiting VRAM\n");
				mc->real_vram_size = size_af;
				mc->mc_vram_size = size_af;
			}
			mc->vram_start = mc->gtt_end + 1;
		}
		mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
		dev_info(rdev->dev, "VRAM: %lluM 0x%08llX - 0x%08llX (%lluM used)\n",
				mc->mc_vram_size >> 20, mc->vram_start,
				mc->vram_end, mc->real_vram_size >> 20);
	} else {
		radeon_vram_location(rdev, &rdev->mc, 0);
		rdev->mc.gtt_base_align = 0;
		radeon_gtt_location(rdev, mc);
	}
}

static int rv770_mc_init(struct radeon_device *rdev)
{
	u32 tmp;
	int chansize, numchan;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(MC_ARB_RAMCFG);
	if (tmp & CHANSIZE_OVERRIDE) {
		chansize = 16;
	} else if (tmp & CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(MC_SHARED_CHMAP);
	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		numchan = 1;
		break;
	case 1:
		numchan = 2;
		break;
	case 2:
		numchan = 4;
		break;
	case 3:
		numchan = 8;
		break;
	}
	rdev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	rdev->mc.aper_base = pci_resource_start(rdev->pdev, 0);
	rdev->mc.aper_size = pci_resource_len(rdev->pdev, 0);
	/* Setup GPU memory space */
	rdev->mc.mc_vram_size = RREG32(CONFIG_MEMSIZE);
	rdev->mc.real_vram_size = RREG32(CONFIG_MEMSIZE);
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	r700_vram_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);

	return 0;
}

static void rv770_uvd_init(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = radeon_uvd_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD (%d) init.\n", r);
		/*
		 * At this point rdev->uvd.vcpu_bo is NULL which trickles down
		 * to early fails uvd_v2_2_resume() and thus nothing happens
		 * there. So it is pointless to try to go through that code
		 * hence why we disable uvd here.
		 */
		rdev->has_uvd = 0;
		return;
	}
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[R600_RING_TYPE_UVD_INDEX], 4096);
}

static void rv770_uvd_start(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = uvd_v2_2_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD resume (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_UVD_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD fences (%d).\n", r);
		goto error;
	}
	return;

error:
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size = 0;
}

static void rv770_uvd_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->has_uvd || !rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size)
		return;

	ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, RADEON_CP_PACKET2);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD ring (%d).\n", r);
		return;
	}
	r = uvd_v1_0_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD (%d).\n", r);
		return;
	}
}

static int rv770_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	/* enable pcie gen2 link */
	rv770_pcie_gen2_enable(rdev);

	/* scratch needs to be initialized before MC */
	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	rv770_mc_program(rdev);

	if (rdev->flags & RADEON_IS_AGP) {
		rv770_agp_enable(rdev);
	} else {
		r = rv770_pcie_gart_enable(rdev);
		if (r)
			return r;
	}

	rv770_gpu_init(rdev);

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_DMA_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	rv770_uvd_start(rdev);

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = r600_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	r600_irq_set(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0));
	if (r)
		return r;

	r = rv770_cp_load_microcode(rdev);
	if (r)
		return r;
	r = r600_cp_resume(rdev);
	if (r)
		return r;

	r = r600_dma_resume(rdev);
	if (r)
		return r;

	rv770_uvd_resume(rdev);

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_audio_init(rdev);
	if (r) {
		DRM_ERROR("radeon: audio init failed\n");
		return r;
	}

	return 0;
}

int rv770_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on rv770 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	/* init golden registers */
	rv770_init_golden_registers(rdev);

	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_resume(rdev);

	rdev->accel_working = true;
	r = rv770_startup(rdev);
	if (r) {
		DRM_ERROR("r600 startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}

	return r;

}

int rv770_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	radeon_audio_fini(rdev);
	if (rdev->has_uvd) {
		uvd_v1_0_fini(rdev);
		radeon_uvd_suspend(rdev);
	}
	r700_cp_stop(rdev);
	r600_dma_stop(rdev);
	r600_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	rv770_pcie_gart_disable(rdev);

	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int rv770_init(struct radeon_device *rdev)
{
	int r;

	/* Read BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	/* Must be an ATOMBIOS */
	if (!rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting atombios for R600 GPU\n");
		return -EINVAL;
	}
	r = radeon_atombios_init(rdev);
	if (r)
		return r;
	/* Post card if necessary */
	if (!radeon_card_posted(rdev)) {
		if (!rdev->bios) {
			dev_err(rdev->dev, "Card not posted and no BIOS - ignoring\n");
			return -EINVAL;
		}
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* init golden registers */
	rv770_init_golden_registers(rdev);
	/* Initialize scratch registers */
	r600_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev->ddev);
	/* Fence driver */
	r = radeon_fence_driver_init(rdev);
	if (r)
		return r;
	/* initialize AGP */
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r)
			radeon_agp_disable(rdev);
	}
	r = rv770_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->rlc_fw) {
		r = r600_init_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load firmware!\n");
			return r;
		}
	}

	/* Initialize power management */
	radeon_pm_init(rdev);

	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX], 1024 * 1024);

	rdev->ring[R600_RING_TYPE_DMA_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[R600_RING_TYPE_DMA_INDEX], 64 * 1024);

	rv770_uvd_init(rdev);

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = rv770_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		r700_cp_fini(rdev);
		r600_dma_fini(rdev);
		r600_irq_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_irq_kms_fini(rdev);
		rv770_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	return 0;
}

void rv770_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	r700_cp_fini(rdev);
	r600_dma_fini(rdev);
	r600_irq_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	uvd_v1_0_fini(rdev);
	radeon_uvd_fini(rdev);
	rv770_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_agp_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

static void rv770_pcie_gen2_enable(struct radeon_device *rdev)
{
	u32 link_width_cntl, lanes, speed_cntl, tmp;
	u16 link_cntl2;

	if (radeon_pcie_gen2 == 0)
		return;

	if (rdev->flags & RADEON_IS_IGP)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	/* x2 cards have a special sequence */
	if (ASIC_IS_X2(rdev))
		return;

	if ((rdev->pdev->bus->max_bus_speed != PCIE_SPEED_5_0GT) &&
		(rdev->pdev->bus->max_bus_speed != PCIE_SPEED_8_0GT))
		return;

	DRM_INFO("enabling PCIE gen 2 link speeds, disable with radeon.pcie_gen2=0\n");

	/* advertise upconfig capability */
	link_width_cntl = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
	link_width_cntl &= ~LC_UPCONFIGURE_DIS;
	WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
	link_width_cntl = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
	if (link_width_cntl & LC_RENEGOTIATION_SUPPORT) {
		lanes = (link_width_cntl & LC_LINK_WIDTH_RD_MASK) >> LC_LINK_WIDTH_RD_SHIFT;
		link_width_cntl &= ~(LC_LINK_WIDTH_MASK |
				     LC_RECONFIG_ARC_MISSING_ESCAPE);
		link_width_cntl |= lanes | LC_RECONFIG_NOW |
			LC_RENEGOTIATE_EN | LC_UPCONFIGURE_SUPPORT;
		WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
	} else {
		link_width_cntl |= LC_UPCONFIGURE_DIS;
		WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
	}

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	if ((speed_cntl & LC_OTHER_SIDE_EVER_SENT_GEN2) &&
	    (speed_cntl & LC_OTHER_SIDE_SUPPORTS_GEN2)) {

		tmp = RREG32(0x541c);
		WREG32(0x541c, tmp | 0x8);
		WREG32(MM_CFGREGS_CNTL, MM_WR_TO_CFG_EN);
		link_cntl2 = RREG16(0x4088);
		link_cntl2 &= ~TARGET_LINK_SPEED_MASK;
		link_cntl2 |= 0x2;
		WREG16(0x4088, link_cntl2);
		WREG32(MM_CFGREGS_CNTL, 0);

		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		speed_cntl &= ~LC_TARGET_LINK_SPEED_OVERRIDE_EN;
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		speed_cntl |= LC_CLR_FAILED_SPD_CHANGE_CNT;
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		speed_cntl &= ~LC_CLR_FAILED_SPD_CHANGE_CNT;
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		speed_cntl |= LC_GEN2_EN_STRAP;
		WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	} else {
		link_width_cntl = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
		/* XXX: only disable it if gen1 bridge vendor == 0x111d or 0x1106 */
		if (1)
			link_width_cntl |= LC_UPCONFIGURE_DIS;
		else
			link_width_cntl &= ~LC_UPCONFIGURE_DIS;
		WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
	}
}
