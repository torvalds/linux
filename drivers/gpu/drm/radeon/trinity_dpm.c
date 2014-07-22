/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "drmP.h"
#include "radeon.h"
#include "trinityd.h"
#include "r600_dpm.h"
#include "trinity_dpm.h"
#include <linux/seq_file.h>

#define TRINITY_MAX_DEEPSLEEP_DIVIDER_ID 5
#define TRINITY_MINIMUM_ENGINE_CLOCK 800
#define SCLK_MIN_DIV_INTV_SHIFT     12
#define TRINITY_DISPCLK_BYPASS_THRESHOLD 10000

#ifndef TRINITY_MGCG_SEQUENCE
#define TRINITY_MGCG_SEQUENCE  100

static const u32 trinity_mgcg_shls_default[] =
{
	/* Register, Value, Mask */
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00003fc4, 0xc0000000, 0xffffffff,
	0x00005448, 0x00000100, 0xffffffff,
	0x000055e4, 0x00000100, 0xffffffff,
	0x0000160c, 0x00000100, 0xffffffff,
	0x00008984, 0x06000100, 0xffffffff,
	0x0000c164, 0x00000100, 0xffffffff,
	0x00008a18, 0x00000100, 0xffffffff,
	0x0000897c, 0x06000100, 0xffffffff,
	0x00008b28, 0x00000100, 0xffffffff,
	0x00009144, 0x00800200, 0xffffffff,
	0x00009a60, 0x00000100, 0xffffffff,
	0x00009868, 0x00000100, 0xffffffff,
	0x00008d58, 0x00000100, 0xffffffff,
	0x00009510, 0x00000100, 0xffffffff,
	0x0000949c, 0x00000100, 0xffffffff,
	0x00009654, 0x00000100, 0xffffffff,
	0x00009030, 0x00000100, 0xffffffff,
	0x00009034, 0x00000100, 0xffffffff,
	0x00009038, 0x00000100, 0xffffffff,
	0x0000903c, 0x00000100, 0xffffffff,
	0x00009040, 0x00000100, 0xffffffff,
	0x0000a200, 0x00000100, 0xffffffff,
	0x0000a204, 0x00000100, 0xffffffff,
	0x0000a208, 0x00000100, 0xffffffff,
	0x0000a20c, 0x00000100, 0xffffffff,
	0x00009744, 0x00000100, 0xffffffff,
	0x00003f80, 0x00000100, 0xffffffff,
	0x0000a210, 0x00000100, 0xffffffff,
	0x0000a214, 0x00000100, 0xffffffff,
	0x000004d8, 0x00000100, 0xffffffff,
	0x00009664, 0x00000100, 0xffffffff,
	0x00009698, 0x00000100, 0xffffffff,
	0x000004d4, 0x00000200, 0xffffffff,
	0x000004d0, 0x00000000, 0xffffffff,
	0x000030cc, 0x00000104, 0xffffffff,
	0x0000d0c0, 0x00000100, 0xffffffff,
	0x0000d8c0, 0x00000100, 0xffffffff,
	0x0000951c, 0x00010000, 0xffffffff,
	0x00009160, 0x00030002, 0xffffffff,
	0x00009164, 0x00050004, 0xffffffff,
	0x00009168, 0x00070006, 0xffffffff,
	0x00009178, 0x00070000, 0xffffffff,
	0x0000917c, 0x00030002, 0xffffffff,
	0x00009180, 0x00050004, 0xffffffff,
	0x0000918c, 0x00010006, 0xffffffff,
	0x00009190, 0x00090008, 0xffffffff,
	0x00009194, 0x00070000, 0xffffffff,
	0x00009198, 0x00030002, 0xffffffff,
	0x0000919c, 0x00050004, 0xffffffff,
	0x000091a8, 0x00010006, 0xffffffff,
	0x000091ac, 0x00090008, 0xffffffff,
	0x000091b0, 0x00070000, 0xffffffff,
	0x000091b4, 0x00030002, 0xffffffff,
	0x000091b8, 0x00050004, 0xffffffff,
	0x000091c4, 0x00010006, 0xffffffff,
	0x000091c8, 0x00090008, 0xffffffff,
	0x000091cc, 0x00070000, 0xffffffff,
	0x000091d0, 0x00030002, 0xffffffff,
	0x000091d4, 0x00050004, 0xffffffff,
	0x000091e0, 0x00010006, 0xffffffff,
	0x000091e4, 0x00090008, 0xffffffff,
	0x000091e8, 0x00000000, 0xffffffff,
	0x000091ec, 0x00070000, 0xffffffff,
	0x000091f0, 0x00030002, 0xffffffff,
	0x000091f4, 0x00050004, 0xffffffff,
	0x00009200, 0x00010006, 0xffffffff,
	0x00009204, 0x00090008, 0xffffffff,
	0x00009208, 0x00070000, 0xffffffff,
	0x0000920c, 0x00030002, 0xffffffff,
	0x00009210, 0x00050004, 0xffffffff,
	0x0000921c, 0x00010006, 0xffffffff,
	0x00009220, 0x00090008, 0xffffffff,
	0x00009294, 0x00000000, 0xffffffff
};

static const u32 trinity_mgcg_shls_enable[] =
{
	/* Register, Value, Mask */
	0x0000802c, 0xc0000000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0x00000000, 0x000133FF,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0x00000000, 0xE00B03FC,
	0x00009150, 0x96944200, 0xffffffff
};

static const u32 trinity_mgcg_shls_disable[] =
{
	/* Register, Value, Mask */
	0x0000802c, 0xc0000000, 0xffffffff,
	0x00009150, 0x00600000, 0xffffffff,
	0x000008f8, 0x00000000, 0xffffffff,
	0x000008fc, 0xffffffff, 0x000133FF,
	0x000008f8, 0x00000001, 0xffffffff,
	0x000008fc, 0xffffffff, 0xE00B03FC
};
#endif

#ifndef TRINITY_SYSLS_SEQUENCE
#define TRINITY_SYSLS_SEQUENCE  100

static const u32 trinity_sysls_default[] =
{
	/* Register, Value, Mask */
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x0000d8bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x00002f50, 0x00000404, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x0000641c, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};

static const u32 trinity_sysls_disable[] =
{
	/* Register, Value, Mask */
	0x0000d0c0, 0x00000000, 0xffffffff,
	0x0000d8c0, 0x00000000, 0xffffffff,
	0x000055e8, 0x00000000, 0xffffffff,
	0x0000d0bc, 0x00000000, 0xffffffff,
	0x0000d8bc, 0x00000000, 0xffffffff,
	0x000015c0, 0x00041401, 0xffffffff,
	0x0000264c, 0x00040400, 0xffffffff,
	0x00002648, 0x00040400, 0xffffffff,
	0x00002650, 0x00040400, 0xffffffff,
	0x000020b8, 0x00040400, 0xffffffff,
	0x000020bc, 0x00040400, 0xffffffff,
	0x000020c0, 0x00040c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680000, 0xffffffff,
	0x00002f50, 0x00000404, 0xffffffff,
	0x000004c8, 0x00000001, 0xffffffff,
	0x0000641c, 0x00007ffd, 0xffffffff,
	0x00000c7c, 0x0000ff00, 0xffffffff,
	0x00006dfc, 0x0000007f, 0xffffffff
};

static const u32 trinity_sysls_enable[] =
{
	/* Register, Value, Mask */
	0x000055e8, 0x00000001, 0xffffffff,
	0x0000d0bc, 0x00000100, 0xffffffff,
	0x0000d8bc, 0x00000100, 0xffffffff,
	0x000015c0, 0x000c1401, 0xffffffff,
	0x0000264c, 0x000c0400, 0xffffffff,
	0x00002648, 0x000c0400, 0xffffffff,
	0x00002650, 0x000c0400, 0xffffffff,
	0x000020b8, 0x000c0400, 0xffffffff,
	0x000020bc, 0x000c0400, 0xffffffff,
	0x000020c0, 0x000c0c80, 0xffffffff,
	0x0000f4a0, 0x000000c0, 0xffffffff,
	0x0000f4a4, 0x00680fff, 0xffffffff,
	0x00002f50, 0x00000903, 0xffffffff,
	0x000004c8, 0x00000000, 0xffffffff,
	0x0000641c, 0x00000000, 0xffffffff,
	0x00000c7c, 0x00000000, 0xffffffff,
	0x00006dfc, 0x00000000, 0xffffffff
};
#endif

static const u32 trinity_override_mgpg_sequences[] =
{
	/* Register, Value */
	0x00000200, 0xE030032C,
	0x00000204, 0x00000FFF,
	0x00000200, 0xE0300058,
	0x00000204, 0x00030301,
	0x00000200, 0xE0300054,
	0x00000204, 0x500010FF,
	0x00000200, 0xE0300074,
	0x00000204, 0x00030301,
	0x00000200, 0xE0300070,
	0x00000204, 0x500010FF,
	0x00000200, 0xE0300090,
	0x00000204, 0x00030301,
	0x00000200, 0xE030008C,
	0x00000204, 0x500010FF,
	0x00000200, 0xE03000AC,
	0x00000204, 0x00030301,
	0x00000200, 0xE03000A8,
	0x00000204, 0x500010FF,
	0x00000200, 0xE03000C8,
	0x00000204, 0x00030301,
	0x00000200, 0xE03000C4,
	0x00000204, 0x500010FF,
	0x00000200, 0xE03000E4,
	0x00000204, 0x00030301,
	0x00000200, 0xE03000E0,
	0x00000204, 0x500010FF,
	0x00000200, 0xE0300100,
	0x00000204, 0x00030301,
	0x00000200, 0xE03000FC,
	0x00000204, 0x500010FF,
	0x00000200, 0xE0300058,
	0x00000204, 0x00030303,
	0x00000200, 0xE0300054,
	0x00000204, 0x600010FF,
	0x00000200, 0xE0300074,
	0x00000204, 0x00030303,
	0x00000200, 0xE0300070,
	0x00000204, 0x600010FF,
	0x00000200, 0xE0300090,
	0x00000204, 0x00030303,
	0x00000200, 0xE030008C,
	0x00000204, 0x600010FF,
	0x00000200, 0xE03000AC,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000A8,
	0x00000204, 0x600010FF,
	0x00000200, 0xE03000C8,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000C4,
	0x00000204, 0x600010FF,
	0x00000200, 0xE03000E4,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000E0,
	0x00000204, 0x600010FF,
	0x00000200, 0xE0300100,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000FC,
	0x00000204, 0x600010FF,
	0x00000200, 0xE0300058,
	0x00000204, 0x00030303,
	0x00000200, 0xE0300054,
	0x00000204, 0x700010FF,
	0x00000200, 0xE0300074,
	0x00000204, 0x00030303,
	0x00000200, 0xE0300070,
	0x00000204, 0x700010FF,
	0x00000200, 0xE0300090,
	0x00000204, 0x00030303,
	0x00000200, 0xE030008C,
	0x00000204, 0x700010FF,
	0x00000200, 0xE03000AC,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000A8,
	0x00000204, 0x700010FF,
	0x00000200, 0xE03000C8,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000C4,
	0x00000204, 0x700010FF,
	0x00000200, 0xE03000E4,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000E0,
	0x00000204, 0x700010FF,
	0x00000200, 0xE0300100,
	0x00000204, 0x00030303,
	0x00000200, 0xE03000FC,
	0x00000204, 0x700010FF,
	0x00000200, 0xE0300058,
	0x00000204, 0x00010303,
	0x00000200, 0xE0300054,
	0x00000204, 0x800010FF,
	0x00000200, 0xE0300074,
	0x00000204, 0x00010303,
	0x00000200, 0xE0300070,
	0x00000204, 0x800010FF,
	0x00000200, 0xE0300090,
	0x00000204, 0x00010303,
	0x00000200, 0xE030008C,
	0x00000204, 0x800010FF,
	0x00000200, 0xE03000AC,
	0x00000204, 0x00010303,
	0x00000200, 0xE03000A8,
	0x00000204, 0x800010FF,
	0x00000200, 0xE03000C4,
	0x00000204, 0x800010FF,
	0x00000200, 0xE03000C8,
	0x00000204, 0x00010303,
	0x00000200, 0xE03000E4,
	0x00000204, 0x00010303,
	0x00000200, 0xE03000E0,
	0x00000204, 0x800010FF,
	0x00000200, 0xE0300100,
	0x00000204, 0x00010303,
	0x00000200, 0xE03000FC,
	0x00000204, 0x800010FF,
	0x00000200, 0x0001f198,
	0x00000204, 0x0003ffff,
	0x00000200, 0x0001f19C,
	0x00000204, 0x3fffffff,
	0x00000200, 0xE030032C,
	0x00000204, 0x00000000,
};

static void trinity_program_clk_gating_hw_sequence(struct radeon_device *rdev,
						   const u32 *seq, u32 count);
static void trinity_override_dynamic_mg_powergating(struct radeon_device *rdev);
static void trinity_apply_state_adjust_rules(struct radeon_device *rdev,
					     struct radeon_ps *new_rps,
					     struct radeon_ps *old_rps);

static struct trinity_ps *trinity_get_ps(struct radeon_ps *rps)
{
	struct trinity_ps *ps = rps->ps_priv;

	return ps;
}

static struct trinity_power_info *trinity_get_pi(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

static void trinity_gfx_powergating_initialize(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 p, u;
	u32 value;
	struct atom_clock_dividers dividers;
	u32 xclk = radeon_get_xclk(rdev);
	u32 sssd = 1;
	int ret;
	u32 hw_rev = (RREG32(HW_REV) & ATI_REV_ID_MASK) >> ATI_REV_ID_SHIFT;

        ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
                                             25000, false, &dividers);
	if (ret)
		return;

	value = RREG32_SMC(GFX_POWER_GATING_CNTL);
	value &= ~(SSSD_MASK | PDS_DIV_MASK);
	if (sssd)
		value |= SSSD(1);
	value |= PDS_DIV(dividers.post_div);
	WREG32_SMC(GFX_POWER_GATING_CNTL, value);

	r600_calculate_u_and_p(500, xclk, 16, &p, &u);

	WREG32(CG_PG_CTRL, SP(p) | SU(u));

	WREG32_P(CG_GIPOTS, CG_GIPOT(p), ~CG_GIPOT_MASK);

	/* XXX double check hw_rev */
	if (pi->override_dynamic_mgpg && (hw_rev == 0))
		trinity_override_dynamic_mg_powergating(rdev);

}

#define CGCG_CGTT_LOCAL0_MASK       0xFFFF33FF
#define CGCG_CGTT_LOCAL1_MASK       0xFFFB0FFE
#define CGTS_SM_CTRL_REG_DISABLE    0x00600000
#define CGTS_SM_CTRL_REG_ENABLE     0x96944200

static void trinity_mg_clockgating_enable(struct radeon_device *rdev,
					  bool enable)
{
	u32 local0;
	u32 local1;

	if (enable) {
		local0 = RREG32_CG(CG_CGTT_LOCAL_0);
		local1 = RREG32_CG(CG_CGTT_LOCAL_1);

		WREG32_CG(CG_CGTT_LOCAL_0,
			  (0x00380000 & CGCG_CGTT_LOCAL0_MASK) | (local0 & ~CGCG_CGTT_LOCAL0_MASK) );
		WREG32_CG(CG_CGTT_LOCAL_1,
			  (0x0E000000 & CGCG_CGTT_LOCAL1_MASK) | (local1 & ~CGCG_CGTT_LOCAL1_MASK) );

		WREG32(CGTS_SM_CTRL_REG, CGTS_SM_CTRL_REG_ENABLE);
	} else {
		WREG32(CGTS_SM_CTRL_REG, CGTS_SM_CTRL_REG_DISABLE);

		local0 = RREG32_CG(CG_CGTT_LOCAL_0);
		local1 = RREG32_CG(CG_CGTT_LOCAL_1);

		WREG32_CG(CG_CGTT_LOCAL_0,
			  CGCG_CGTT_LOCAL0_MASK | (local0 & ~CGCG_CGTT_LOCAL0_MASK) );
		WREG32_CG(CG_CGTT_LOCAL_1,
			  CGCG_CGTT_LOCAL1_MASK | (local1 & ~CGCG_CGTT_LOCAL1_MASK) );
	}
}

static void trinity_mg_clockgating_initialize(struct radeon_device *rdev)
{
	u32 count;
	const u32 *seq = NULL;

	seq = &trinity_mgcg_shls_default[0];
	count = sizeof(trinity_mgcg_shls_default) / (3 * sizeof(u32));

	trinity_program_clk_gating_hw_sequence(rdev, seq, count);
}

static void trinity_gfx_clockgating_enable(struct radeon_device *rdev,
					   bool enable)
{
	if (enable) {
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
	} else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_ADDR_CONFIG);
	}
}

static void trinity_program_clk_gating_hw_sequence(struct radeon_device *rdev,
						   const u32 *seq, u32 count)
{
	u32 i, length = count * 3;

	for (i = 0; i < length; i += 3)
		WREG32_P(seq[i], seq[i+1], ~seq[i+2]);
}

static void trinity_program_override_mgpg_sequences(struct radeon_device *rdev,
						    const u32 *seq, u32 count)
{
	u32  i, length = count * 2;

	for (i = 0; i < length; i += 2)
		WREG32(seq[i], seq[i+1]);

}

static void trinity_override_dynamic_mg_powergating(struct radeon_device *rdev)
{
	u32 count;
	const u32 *seq = NULL;

	seq = &trinity_override_mgpg_sequences[0];
	count = sizeof(trinity_override_mgpg_sequences) / (2 * sizeof(u32));

	trinity_program_override_mgpg_sequences(rdev, seq, count);
}

static void trinity_ls_clockgating_enable(struct radeon_device *rdev,
					  bool enable)
{
	u32 count;
	const u32 *seq = NULL;

	if (enable) {
		seq = &trinity_sysls_enable[0];
		count = sizeof(trinity_sysls_enable) / (3 * sizeof(u32));
	} else {
		seq = &trinity_sysls_disable[0];
		count = sizeof(trinity_sysls_disable) / (3 * sizeof(u32));
	}

	trinity_program_clk_gating_hw_sequence(rdev, seq, count);
}

static void trinity_gfx_powergating_enable(struct radeon_device *rdev,
					   bool enable)
{
	if (enable) {
		if (RREG32_SMC(CC_SMU_TST_EFUSE1_MISC) & RB_BACKEND_DISABLE_MASK)
			WREG32_SMC(SMU_SCRATCH_A, (RREG32_SMC(SMU_SCRATCH_A) | 0x01));

		WREG32_P(SCLK_PWRMGT_CNTL, DYN_PWR_DOWN_EN, ~DYN_PWR_DOWN_EN);
	} else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_PWR_DOWN_EN);
		RREG32(GB_ADDR_CONFIG);
	}
}

static void trinity_gfx_dynamic_mgpg_enable(struct radeon_device *rdev,
					    bool enable)
{
	u32 value;

	if (enable) {
		value = RREG32_SMC(PM_I_CNTL_1);
		value &= ~DS_PG_CNTL_MASK;
		value |= DS_PG_CNTL(1);
		WREG32_SMC(PM_I_CNTL_1, value);

		value = RREG32_SMC(SMU_S_PG_CNTL);
		value &= ~DS_PG_EN_MASK;
		value |= DS_PG_EN(1);
		WREG32_SMC(SMU_S_PG_CNTL, value);
	} else {
		value = RREG32_SMC(SMU_S_PG_CNTL);
		value &= ~DS_PG_EN_MASK;
		WREG32_SMC(SMU_S_PG_CNTL, value);

		value = RREG32_SMC(PM_I_CNTL_1);
		value &= ~DS_PG_CNTL_MASK;
		WREG32_SMC(PM_I_CNTL_1, value);
	}

	trinity_gfx_dynamic_mgpg_config(rdev);

}

static void trinity_enable_clock_power_gating(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->enable_gfx_clock_gating)
		sumo_gfx_clockgating_initialize(rdev);
	if (pi->enable_mg_clock_gating)
		trinity_mg_clockgating_initialize(rdev);
	if (pi->enable_gfx_power_gating)
		trinity_gfx_powergating_initialize(rdev);
	if (pi->enable_mg_clock_gating) {
		trinity_ls_clockgating_enable(rdev, true);
		trinity_mg_clockgating_enable(rdev, true);
	}
	if (pi->enable_gfx_clock_gating)
		trinity_gfx_clockgating_enable(rdev, true);
	if (pi->enable_gfx_dynamic_mgpg)
		trinity_gfx_dynamic_mgpg_enable(rdev, true);
	if (pi->enable_gfx_power_gating)
		trinity_gfx_powergating_enable(rdev, true);
}

static void trinity_disable_clock_power_gating(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->enable_gfx_power_gating)
		trinity_gfx_powergating_enable(rdev, false);
	if (pi->enable_gfx_dynamic_mgpg)
		trinity_gfx_dynamic_mgpg_enable(rdev, false);
	if (pi->enable_gfx_clock_gating)
		trinity_gfx_clockgating_enable(rdev, false);
	if (pi->enable_mg_clock_gating) {
		trinity_mg_clockgating_enable(rdev, false);
		trinity_ls_clockgating_enable(rdev, false);
	}
}

static void trinity_set_divider_value(struct radeon_device *rdev,
				      u32 index, u32 sclk)
{
	struct atom_clock_dividers  dividers;
	int ret;
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

        ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
                                             sclk, false, &dividers);
	if (ret)
		return;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix);
	value &= ~CLK_DIVIDER_MASK;
	value |= CLK_DIVIDER(dividers.post_div);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix, value);

        ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
                                             sclk/2, false, &dividers);
	if (ret)
		return;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_PG_CNTL + ix);
	value &= ~PD_SCLK_DIVIDER_MASK;
	value |= PD_SCLK_DIVIDER(dividers.post_div);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_PG_CNTL + ix, value);
}

static void trinity_set_ds_dividers(struct radeon_device *rdev,
				    u32 index, u32 divider)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix);
	value &= ~DS_DIV_MASK;
	value |= DS_DIV(divider);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix, value);
}

static void trinity_set_ss_dividers(struct radeon_device *rdev,
				    u32 index, u32 divider)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix);
	value &= ~DS_SH_DIV_MASK;
	value |= DS_SH_DIV(divider);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix, value);
}

static void trinity_set_vid(struct radeon_device *rdev, u32 index, u32 vid)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 vid_7bit = sumo_convert_vid2_to_vid7(rdev, &pi->sys_info.vid_mapping_table, vid);
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix);
	value &= ~VID_MASK;
	value |= VID(vid_7bit);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix, value);

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix);
	value &= ~LVRT_MASK;
	value |= LVRT(0);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix, value);
}

static void trinity_set_allos_gnb_slow(struct radeon_device *rdev,
				       u32 index, u32 gnb_slow)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_3 + ix);
	value &= ~GNB_SLOW_MASK;
	value |= GNB_SLOW(gnb_slow);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_3 + ix, value);
}

static void trinity_set_force_nbp_state(struct radeon_device *rdev,
					u32 index, u32 force_nbp_state)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_3 + ix);
	value &= ~FORCE_NBPS1_MASK;
	value |= FORCE_NBPS1(force_nbp_state);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_3 + ix, value);
}

static void trinity_set_display_wm(struct radeon_device *rdev,
				   u32 index, u32 wm)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix);
	value &= ~DISPLAY_WM_MASK;
	value |= DISPLAY_WM(wm);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix, value);
}

static void trinity_set_vce_wm(struct radeon_device *rdev,
			       u32 index, u32 wm)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix);
	value &= ~VCE_WM_MASK;
	value |= VCE_WM(wm);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_1 + ix, value);
}

static void trinity_set_at(struct radeon_device *rdev,
			   u32 index, u32 at)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_AT + ix);
	value &= ~AT_MASK;
	value |= AT(at);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_AT + ix, value);
}

static void trinity_program_power_level(struct radeon_device *rdev,
					struct trinity_pl *pl, u32 index)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (index >= SUMO_MAX_HARDWARE_POWERLEVELS)
		return;

	trinity_set_divider_value(rdev, index, pl->sclk);
	trinity_set_vid(rdev, index, pl->vddc_index);
	trinity_set_ss_dividers(rdev, index, pl->ss_divider_index);
	trinity_set_ds_dividers(rdev, index, pl->ds_divider_index);
	trinity_set_allos_gnb_slow(rdev, index, pl->allow_gnb_slow);
	trinity_set_force_nbp_state(rdev, index, pl->force_nbp_state);
	trinity_set_display_wm(rdev, index, pl->display_wm);
	trinity_set_vce_wm(rdev, index, pl->vce_wm);
	trinity_set_at(rdev, index, pi->at[index]);
}

static void trinity_power_level_enable_disable(struct radeon_device *rdev,
					       u32 index, bool enable)
{
	u32 value;
	u32 ix = index * TRINITY_SIZEOF_DPM_STATE_TABLE;

	value = RREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix);
	value &= ~STATE_VALID_MASK;
	if (enable)
		value |= STATE_VALID(1);
	WREG32_SMC(SMU_SCLK_DPM_STATE_0_CNTL_0 + ix, value);
}

static bool trinity_dpm_enabled(struct radeon_device *rdev)
{
	if (RREG32_SMC(SMU_SCLK_DPM_CNTL) & SCLK_DPM_EN(1))
		return true;
	else
		return false;
}

static void trinity_start_dpm(struct radeon_device *rdev)
{
	u32 value = RREG32_SMC(SMU_SCLK_DPM_CNTL);

	value &= ~(SCLK_DPM_EN_MASK | SCLK_DPM_BOOT_STATE_MASK | VOLTAGE_CHG_EN_MASK);
	value |= SCLK_DPM_EN(1) | SCLK_DPM_BOOT_STATE(0) | VOLTAGE_CHG_EN(1);
	WREG32_SMC(SMU_SCLK_DPM_CNTL, value);

	WREG32_P(GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, ~GLOBAL_PWRMGT_EN);
	WREG32_P(CG_CG_VOLTAGE_CNTL, 0, ~EN);

	trinity_dpm_config(rdev, true);
}

static void trinity_wait_for_dpm_enabled(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(SCLK_PWRMGT_CNTL) & DYNAMIC_PM_EN)
			break;
		udelay(1);
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & TARGET_STATE_MASK) == 0)
			break;
		udelay(1);
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURRENT_STATE_MASK) == 0)
			break;
		udelay(1);
	}
}

static void trinity_stop_dpm(struct radeon_device *rdev)
{
	u32 sclk_dpm_cntl;

	WREG32_P(CG_CG_VOLTAGE_CNTL, EN, ~EN);

	sclk_dpm_cntl = RREG32_SMC(SMU_SCLK_DPM_CNTL);
	sclk_dpm_cntl &= ~(SCLK_DPM_EN_MASK | VOLTAGE_CHG_EN_MASK);
	WREG32_SMC(SMU_SCLK_DPM_CNTL, sclk_dpm_cntl);

	trinity_dpm_config(rdev, false);
}

static void trinity_start_am(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, 0, ~(RESET_SCLK_CNT | RESET_BUSY_CNT));
}

static void trinity_reset_am(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, RESET_SCLK_CNT | RESET_BUSY_CNT,
		 ~(RESET_SCLK_CNT | RESET_BUSY_CNT));
}

static void trinity_wait_for_level_0(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURRENT_STATE_MASK) == 0)
			break;
		udelay(1);
	}
}

static void trinity_enable_power_level_0(struct radeon_device *rdev)
{
	trinity_power_level_enable_disable(rdev, 0, true);
}

static void trinity_force_level_0(struct radeon_device *rdev)
{
	trinity_dpm_force_state(rdev, 0);
}

static void trinity_unforce_levels(struct radeon_device *rdev)
{
	trinity_dpm_no_forced_level(rdev);
}

static void trinity_program_power_levels_0_to_n(struct radeon_device *rdev,
						struct radeon_ps *new_rps,
						struct radeon_ps *old_rps)
{
	struct trinity_ps *new_ps = trinity_get_ps(new_rps);
	struct trinity_ps *old_ps = trinity_get_ps(old_rps);
	u32 i;
	u32 n_current_state_levels = (old_ps == NULL) ? 1 : old_ps->num_levels;

	for (i = 0; i < new_ps->num_levels; i++) {
		trinity_program_power_level(rdev, &new_ps->levels[i], i);
		trinity_power_level_enable_disable(rdev, i, true);
	}

	for (i = new_ps->num_levels; i < n_current_state_levels; i++)
		trinity_power_level_enable_disable(rdev, i, false);
}

static void trinity_program_bootup_state(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 i;

	trinity_program_power_level(rdev, &pi->boot_pl, 0);
	trinity_power_level_enable_disable(rdev, 0, true);

	for (i = 1; i < 8; i++)
		trinity_power_level_enable_disable(rdev, i, false);
}

static void trinity_setup_uvd_clock_table(struct radeon_device *rdev,
					  struct radeon_ps *rps)
{
	struct trinity_ps *ps = trinity_get_ps(rps);
	u32 uvdstates = (ps->vclk_low_divider |
			 ps->vclk_high_divider << 8 |
			 ps->dclk_low_divider << 16 |
			 ps->dclk_high_divider << 24);

	WREG32_SMC(SMU_UVD_DPM_STATES, uvdstates);
}

static void trinity_setup_uvd_dpm_interval(struct radeon_device *rdev,
					   u32 interval)
{
	u32 p, u;
	u32 tp = RREG32_SMC(PM_TP);
	u32 val;
	u32 xclk = radeon_get_xclk(rdev);

	r600_calculate_u_and_p(interval, xclk, 16, &p, &u);

	val = (p + tp - 1) / tp;

	WREG32_SMC(SMU_UVD_DPM_CNTL, val);
}

static bool trinity_uvd_clocks_zero(struct radeon_ps *rps)
{
	if ((rps->vclk == 0) && (rps->dclk == 0))
		return true;
	else
		return false;
}

static bool trinity_uvd_clocks_equal(struct radeon_ps *rps1,
				     struct radeon_ps *rps2)
{
	struct trinity_ps *ps1 = trinity_get_ps(rps1);
	struct trinity_ps *ps2 = trinity_get_ps(rps2);

	if ((rps1->vclk == rps2->vclk) &&
	    (rps1->dclk == rps2->dclk) &&
	    (ps1->vclk_low_divider == ps2->vclk_low_divider) &&
	    (ps1->vclk_high_divider == ps2->vclk_high_divider) &&
	    (ps1->dclk_low_divider == ps2->dclk_low_divider) &&
	    (ps1->dclk_high_divider == ps2->dclk_high_divider))
		return true;
	else
		return false;
}

static void trinity_setup_uvd_clocks(struct radeon_device *rdev,
				     struct radeon_ps *new_rps,
				     struct radeon_ps *old_rps)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->enable_gfx_power_gating) {
		trinity_gfx_powergating_enable(rdev, false);
	}

	if (pi->uvd_dpm) {
		if (trinity_uvd_clocks_zero(new_rps) &&
		    !trinity_uvd_clocks_zero(old_rps)) {
			trinity_setup_uvd_dpm_interval(rdev, 0);
		} else if (!trinity_uvd_clocks_zero(new_rps)) {
			trinity_setup_uvd_clock_table(rdev, new_rps);

			if (trinity_uvd_clocks_zero(old_rps)) {
				u32 tmp = RREG32(CG_MISC_REG);
				tmp &= 0xfffffffd;
				WREG32(CG_MISC_REG, tmp);

				radeon_set_uvd_clocks(rdev, new_rps->vclk, new_rps->dclk);

				trinity_setup_uvd_dpm_interval(rdev, 3000);
			}
		}
		trinity_uvd_dpm_config(rdev);
	} else {
		if (trinity_uvd_clocks_zero(new_rps) ||
		    trinity_uvd_clocks_equal(new_rps, old_rps))
			return;

		radeon_set_uvd_clocks(rdev, new_rps->vclk, new_rps->dclk);
	}

	if (pi->enable_gfx_power_gating) {
		trinity_gfx_powergating_enable(rdev, true);
	}
}

static void trinity_set_uvd_clock_before_set_eng_clock(struct radeon_device *rdev,
						       struct radeon_ps *new_rps,
						       struct radeon_ps *old_rps)
{
	struct trinity_ps *new_ps = trinity_get_ps(new_rps);
	struct trinity_ps *current_ps = trinity_get_ps(new_rps);

	if (new_ps->levels[new_ps->num_levels - 1].sclk >=
	    current_ps->levels[current_ps->num_levels - 1].sclk)
		return;

	trinity_setup_uvd_clocks(rdev, new_rps, old_rps);
}

static void trinity_set_uvd_clock_after_set_eng_clock(struct radeon_device *rdev,
						      struct radeon_ps *new_rps,
						      struct radeon_ps *old_rps)
{
	struct trinity_ps *new_ps = trinity_get_ps(new_rps);
	struct trinity_ps *current_ps = trinity_get_ps(old_rps);

	if (new_ps->levels[new_ps->num_levels - 1].sclk <
	    current_ps->levels[current_ps->num_levels - 1].sclk)
		return;

	trinity_setup_uvd_clocks(rdev, new_rps, old_rps);
}

static void trinity_program_ttt(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 value = RREG32_SMC(SMU_SCLK_DPM_TTT);

	value &= ~(HT_MASK | LT_MASK);
	value |= HT((pi->thermal_auto_throttling + 49) * 8);
	value |= LT((pi->thermal_auto_throttling + 49 - pi->sys_info.htc_hyst_lmt) * 8);
	WREG32_SMC(SMU_SCLK_DPM_TTT, value);
}

static void trinity_enable_att(struct radeon_device *rdev)
{
	u32 value = RREG32_SMC(SMU_SCLK_DPM_TT_CNTL);

	value &= ~SCLK_TT_EN_MASK;
	value |= SCLK_TT_EN(1);
	WREG32_SMC(SMU_SCLK_DPM_TT_CNTL, value);
}

static void trinity_program_sclk_dpm(struct radeon_device *rdev)
{
	u32 p, u;
	u32 tp = RREG32_SMC(PM_TP);
	u32 ni;
	u32 xclk = radeon_get_xclk(rdev);
	u32 value;

	r600_calculate_u_and_p(400, xclk, 16, &p, &u);

	ni = (p + tp - 1) / tp;

	value = RREG32_SMC(PM_I_CNTL_1);
	value &= ~SCLK_DPM_MASK;
	value |= SCLK_DPM(ni);
	WREG32_SMC(PM_I_CNTL_1, value);
}

static int trinity_set_thermal_temperature_range(struct radeon_device *rdev,
						 int min_temp, int max_temp)
{
	int low_temp = 0 * 1000;
	int high_temp = 255 * 1000;

        if (low_temp < min_temp)
		low_temp = min_temp;
        if (high_temp > max_temp)
		high_temp = max_temp;
        if (high_temp < low_temp) {
		DRM_ERROR("invalid thermal range: %d - %d\n", low_temp, high_temp);
                return -EINVAL;
        }

	WREG32_P(CG_THERMAL_INT_CTRL, DIG_THERM_INTH(49 + (high_temp / 1000)), ~DIG_THERM_INTH_MASK);
	WREG32_P(CG_THERMAL_INT_CTRL, DIG_THERM_INTL(49 + (low_temp / 1000)), ~DIG_THERM_INTL_MASK);

	rdev->pm.dpm.thermal.min_temp = low_temp;
	rdev->pm.dpm.thermal.max_temp = high_temp;

	return 0;
}

static void trinity_update_current_ps(struct radeon_device *rdev,
				      struct radeon_ps *rps)
{
	struct trinity_ps *new_ps = trinity_get_ps(rps);
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	pi->current_rps = *rps;
	pi->current_ps = *new_ps;
	pi->current_rps.ps_priv = &pi->current_ps;
}

static void trinity_update_requested_ps(struct radeon_device *rdev,
					struct radeon_ps *rps)
{
	struct trinity_ps *new_ps = trinity_get_ps(rps);
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	pi->requested_rps = *rps;
	pi->requested_ps = *new_ps;
	pi->requested_rps.ps_priv = &pi->requested_ps;
}

void trinity_dpm_enable_bapm(struct radeon_device *rdev, bool enable)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->enable_bapm) {
		trinity_acquire_mutex(rdev);
		trinity_dpm_bapm_enable(rdev, enable);
		trinity_release_mutex(rdev);
	}
}

int trinity_dpm_enable(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	trinity_acquire_mutex(rdev);

	if (trinity_dpm_enabled(rdev)) {
		trinity_release_mutex(rdev);
		return -EINVAL;
	}

	trinity_program_bootup_state(rdev);
	sumo_program_vc(rdev, 0x00C00033);
	trinity_start_am(rdev);
	if (pi->enable_auto_thermal_throttling) {
		trinity_program_ttt(rdev);
		trinity_enable_att(rdev);
	}
	trinity_program_sclk_dpm(rdev);
	trinity_start_dpm(rdev);
	trinity_wait_for_dpm_enabled(rdev);
	trinity_dpm_bapm_enable(rdev, false);
	trinity_release_mutex(rdev);

	trinity_update_current_ps(rdev, rdev->pm.dpm.boot_ps);

	return 0;
}

int trinity_dpm_late_enable(struct radeon_device *rdev)
{
	int ret;

	trinity_acquire_mutex(rdev);
	trinity_enable_clock_power_gating(rdev);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		ret = trinity_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		if (ret) {
			trinity_release_mutex(rdev);
			return ret;
		}
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
	}
	trinity_release_mutex(rdev);

	return 0;
}

void trinity_dpm_disable(struct radeon_device *rdev)
{
	trinity_acquire_mutex(rdev);
	if (!trinity_dpm_enabled(rdev)) {
		trinity_release_mutex(rdev);
		return;
	}
	trinity_dpm_bapm_enable(rdev, false);
	trinity_disable_clock_power_gating(rdev);
	sumo_clear_vc(rdev);
	trinity_wait_for_level_0(rdev);
	trinity_stop_dpm(rdev);
	trinity_reset_am(rdev);
	trinity_release_mutex(rdev);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}

	trinity_update_current_ps(rdev, rdev->pm.dpm.boot_ps);
}

static void trinity_get_min_sclk_divider(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	pi->min_sclk_did =
		(RREG32_SMC(CC_SMU_MISC_FUSES) & MinSClkDid_MASK) >> MinSClkDid_SHIFT;
}

static void trinity_setup_nbp_sim(struct radeon_device *rdev,
				  struct radeon_ps *rps)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct trinity_ps *new_ps = trinity_get_ps(rps);
	u32 nbpsconfig;

	if (pi->sys_info.nb_dpm_enable) {
		nbpsconfig = RREG32_SMC(NB_PSTATE_CONFIG);
		nbpsconfig &= ~(Dpm0PgNbPsLo_MASK | Dpm0PgNbPsHi_MASK | DpmXNbPsLo_MASK | DpmXNbPsHi_MASK);
		nbpsconfig |= (Dpm0PgNbPsLo(new_ps->Dpm0PgNbPsLo) |
			       Dpm0PgNbPsHi(new_ps->Dpm0PgNbPsHi) |
			       DpmXNbPsLo(new_ps->DpmXNbPsLo) |
			       DpmXNbPsHi(new_ps->DpmXNbPsHi));
		WREG32_SMC(NB_PSTATE_CONFIG, nbpsconfig);
	}
}

int trinity_dpm_force_performance_level(struct radeon_device *rdev,
					enum radeon_dpm_forced_level level)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_ps *rps = &pi->current_rps;
	struct trinity_ps *ps = trinity_get_ps(rps);
	int i, ret;

	if (ps->num_levels <= 1)
		return 0;

	if (level == RADEON_DPM_FORCED_LEVEL_HIGH) {
		/* not supported by the hw */
		return -EINVAL;
	} else if (level == RADEON_DPM_FORCED_LEVEL_LOW) {
		ret = trinity_dpm_n_levels_disabled(rdev, ps->num_levels - 1);
		if (ret)
			return ret;
	} else {
		for (i = 0; i < ps->num_levels; i++) {
			ret = trinity_dpm_n_levels_disabled(rdev, 0);
			if (ret)
				return ret;
		}
	}

	rdev->pm.dpm.forced_level = level;

	return 0;
}

int trinity_dpm_pre_set_power_state(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_ps requested_ps = *rdev->pm.dpm.requested_ps;
	struct radeon_ps *new_ps = &requested_ps;

	trinity_update_requested_ps(rdev, new_ps);

	trinity_apply_state_adjust_rules(rdev,
					 &pi->requested_rps,
					 &pi->current_rps);

	return 0;
}

int trinity_dpm_set_power_state(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;
	struct radeon_ps *old_ps = &pi->current_rps;

	trinity_acquire_mutex(rdev);
	if (pi->enable_dpm) {
		if (pi->enable_bapm)
			trinity_dpm_bapm_enable(rdev, rdev->pm.dpm.ac_power);
		trinity_set_uvd_clock_before_set_eng_clock(rdev, new_ps, old_ps);
		trinity_enable_power_level_0(rdev);
		trinity_force_level_0(rdev);
		trinity_wait_for_level_0(rdev);
		trinity_setup_nbp_sim(rdev, new_ps);
		trinity_program_power_levels_0_to_n(rdev, new_ps, old_ps);
		trinity_force_level_0(rdev);
		trinity_unforce_levels(rdev);
		trinity_set_uvd_clock_after_set_eng_clock(rdev, new_ps, old_ps);
	}
	trinity_release_mutex(rdev);

	return 0;
}

void trinity_dpm_post_set_power_state(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_ps *new_ps = &pi->requested_rps;

	trinity_update_current_ps(rdev, new_ps);
}

void trinity_dpm_setup_asic(struct radeon_device *rdev)
{
	trinity_acquire_mutex(rdev);
	sumo_program_sstp(rdev);
	sumo_take_smu_control(rdev, true);
	trinity_get_min_sclk_divider(rdev);
	trinity_release_mutex(rdev);
}

void trinity_dpm_reset_asic(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	trinity_acquire_mutex(rdev);
	if (pi->enable_dpm) {
		trinity_enable_power_level_0(rdev);
		trinity_force_level_0(rdev);
		trinity_wait_for_level_0(rdev);
		trinity_program_bootup_state(rdev);
		trinity_force_level_0(rdev);
		trinity_unforce_levels(rdev);
	}
	trinity_release_mutex(rdev);
}

static u16 trinity_convert_voltage_index_to_value(struct radeon_device *rdev,
						  u32 vid_2bit)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 vid_7bit = sumo_convert_vid2_to_vid7(rdev, &pi->sys_info.vid_mapping_table, vid_2bit);
	u32 svi_mode = (RREG32_SMC(PM_CONFIG) & SVI_Mode) ? 1 : 0;
	u32 step = (svi_mode == 0) ? 1250 : 625;
	u32 delta = vid_7bit * step + 50;

	if (delta > 155000)
		return 0;

	return (155000 - delta) / 100;
}

static void trinity_patch_boot_state(struct radeon_device *rdev,
				     struct trinity_ps *ps)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	ps->num_levels = 1;
	ps->nbps_flags = 0;
	ps->bapm_flags = 0;
	ps->levels[0] = pi->boot_pl;
}

static u8 trinity_calculate_vce_wm(struct radeon_device *rdev, u32 sclk)
{
	if (sclk < 20000)
		return 1;
	return 0;
}

static void trinity_construct_boot_state(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	pi->boot_pl.sclk = pi->sys_info.bootup_sclk;
	pi->boot_pl.vddc_index = pi->sys_info.bootup_nb_voltage_index;
	pi->boot_pl.ds_divider_index = 0;
	pi->boot_pl.ss_divider_index = 0;
	pi->boot_pl.allow_gnb_slow = 1;
	pi->boot_pl.force_nbp_state = 0;
	pi->boot_pl.display_wm = 0;
	pi->boot_pl.vce_wm = 0;
	pi->current_ps.num_levels = 1;
	pi->current_ps.levels[0] = pi->boot_pl;
}

static u8 trinity_get_sleep_divider_id_from_clock(struct radeon_device *rdev,
						  u32 sclk, u32 min_sclk_in_sr)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 i;
	u32 temp;
	u32 min = (min_sclk_in_sr > TRINITY_MINIMUM_ENGINE_CLOCK) ?
		min_sclk_in_sr : TRINITY_MINIMUM_ENGINE_CLOCK;

	if (sclk < min)
		return 0;

	if (!pi->enable_sclk_ds)
		return 0;

	for (i = TRINITY_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		temp = sclk / sumo_get_sleep_divider_from_id(i);
		if (temp >= min || i == 0)
			break;
	}

	return (u8)i;
}

static u32 trinity_get_valid_engine_clock(struct radeon_device *rdev,
					  u32 lower_limit)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 i;

	for (i = 0; i < pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries; i++) {
		if (pi->sys_info.sclk_voltage_mapping_table.entries[i].sclk_frequency >= lower_limit)
			return pi->sys_info.sclk_voltage_mapping_table.entries[i].sclk_frequency;
	}

	if (i == pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries)
		DRM_ERROR("engine clock out of range!");

	return 0;
}

static void trinity_patch_thermal_state(struct radeon_device *rdev,
					struct trinity_ps *ps,
					struct trinity_ps *current_ps)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 sclk_in_sr = pi->sys_info.min_sclk; /* ??? */
	u32 current_vddc;
	u32 current_sclk;
	u32 current_index = 0;

	if (current_ps) {
		current_vddc = current_ps->levels[current_index].vddc_index;
		current_sclk = current_ps->levels[current_index].sclk;
	} else {
		current_vddc = pi->boot_pl.vddc_index;
		current_sclk = pi->boot_pl.sclk;
	}

	ps->levels[0].vddc_index = current_vddc;

	if (ps->levels[0].sclk > current_sclk)
		ps->levels[0].sclk = current_sclk;

	ps->levels[0].ds_divider_index =
		trinity_get_sleep_divider_id_from_clock(rdev, ps->levels[0].sclk, sclk_in_sr);
	ps->levels[0].ss_divider_index = ps->levels[0].ds_divider_index;
	ps->levels[0].allow_gnb_slow = 1;
	ps->levels[0].force_nbp_state = 0;
	ps->levels[0].display_wm = 0;
	ps->levels[0].vce_wm =
		trinity_calculate_vce_wm(rdev, ps->levels[0].sclk);
}

static u8 trinity_calculate_display_wm(struct radeon_device *rdev,
				       struct trinity_ps *ps, u32 index)
{
	if (ps == NULL || ps->num_levels <= 1)
		return 0;
	else if (ps->num_levels == 2) {
		if (index == 0)
			return 0;
		else
			return 1;
	} else {
		if (index == 0)
			return 0;
		else if (ps->levels[index].sclk < 30000)
			return 0;
		else
			return 1;
	}
}

static u32 trinity_get_uvd_clock_index(struct radeon_device *rdev,
				       struct radeon_ps *rps)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 i = 0;

	for (i = 0; i < 4; i++) {
		if ((rps->vclk == pi->sys_info.uvd_clock_table_entries[i].vclk) &&
		    (rps->dclk == pi->sys_info.uvd_clock_table_entries[i].dclk))
		    break;
	}

	if (i >= 4) {
		DRM_ERROR("UVD clock index not found!\n");
		i = 3;
	}
	return i;
}

static void trinity_adjust_uvd_state(struct radeon_device *rdev,
				     struct radeon_ps *rps)
{
	struct trinity_ps *ps = trinity_get_ps(rps);
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 high_index = 0;
	u32 low_index = 0;

	if (pi->uvd_dpm && r600_is_uvd_state(rps->class, rps->class2)) {
		high_index = trinity_get_uvd_clock_index(rdev, rps);

		switch(high_index) {
		case 3:
		case 2:
			low_index = 1;
			break;
		case 1:
		case 0:
		default:
			low_index = 0;
			break;
		}

		ps->vclk_low_divider =
			pi->sys_info.uvd_clock_table_entries[high_index].vclk_did;
		ps->dclk_low_divider =
			pi->sys_info.uvd_clock_table_entries[high_index].dclk_did;
		ps->vclk_high_divider =
			pi->sys_info.uvd_clock_table_entries[low_index].vclk_did;
		ps->dclk_high_divider =
			pi->sys_info.uvd_clock_table_entries[low_index].dclk_did;
	}
}



static void trinity_apply_state_adjust_rules(struct radeon_device *rdev,
					     struct radeon_ps *new_rps,
					     struct radeon_ps *old_rps)
{
	struct trinity_ps *ps = trinity_get_ps(new_rps);
	struct trinity_ps *current_ps = trinity_get_ps(old_rps);
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 min_voltage = 0; /* ??? */
	u32 min_sclk = pi->sys_info.min_sclk; /* XXX check against disp reqs */
	u32 sclk_in_sr = pi->sys_info.min_sclk; /* ??? */
	u32 i;
	bool force_high;
	u32 num_active_displays = rdev->pm.dpm.new_active_crtc_count;

	if (new_rps->class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
		return trinity_patch_thermal_state(rdev, ps, current_ps);

	trinity_adjust_uvd_state(rdev, new_rps);

	for (i = 0; i < ps->num_levels; i++) {
		if (ps->levels[i].vddc_index < min_voltage)
			ps->levels[i].vddc_index = min_voltage;

		if (ps->levels[i].sclk < min_sclk)
			ps->levels[i].sclk =
				trinity_get_valid_engine_clock(rdev, min_sclk);

		ps->levels[i].ds_divider_index =
			sumo_get_sleep_divider_id_from_clock(rdev, ps->levels[i].sclk, sclk_in_sr);

		ps->levels[i].ss_divider_index = ps->levels[i].ds_divider_index;

		ps->levels[i].allow_gnb_slow = 1;
		ps->levels[i].force_nbp_state = 0;
		ps->levels[i].display_wm =
			trinity_calculate_display_wm(rdev, ps, i);
		ps->levels[i].vce_wm =
			trinity_calculate_vce_wm(rdev, ps->levels[0].sclk);
	}

	if ((new_rps->class & (ATOM_PPLIB_CLASSIFICATION_HDSTATE | ATOM_PPLIB_CLASSIFICATION_SDSTATE)) ||
	    ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY))
		ps->bapm_flags |= TRINITY_POWERSTATE_FLAGS_BAPM_DISABLE;

	if (pi->sys_info.nb_dpm_enable) {
		ps->Dpm0PgNbPsLo = 0x1;
		ps->Dpm0PgNbPsHi = 0x0;
		ps->DpmXNbPsLo = 0x2;
		ps->DpmXNbPsHi = 0x1;

		if ((new_rps->class & (ATOM_PPLIB_CLASSIFICATION_HDSTATE | ATOM_PPLIB_CLASSIFICATION_SDSTATE)) ||
		    ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_UI_MASK) == ATOM_PPLIB_CLASSIFICATION_UI_BATTERY)) {
			force_high = ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_HDSTATE) ||
				      ((new_rps->class & ATOM_PPLIB_CLASSIFICATION_SDSTATE) &&
				       (pi->sys_info.uma_channel_number == 1)));
			force_high = (num_active_displays >= 3) || force_high;
			ps->Dpm0PgNbPsLo = force_high ? 0x2 : 0x3;
			ps->Dpm0PgNbPsHi = 0x1;
			ps->DpmXNbPsLo = force_high ? 0x2 : 0x3;
			ps->DpmXNbPsHi = 0x2;
			ps->levels[ps->num_levels - 1].allow_gnb_slow = 0;
		}
	}
}

static void trinity_cleanup_asic(struct radeon_device *rdev)
{
	sumo_take_smu_control(rdev, false);
}

#if 0
static void trinity_pre_display_configuration_change(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->voltage_drop_in_dce)
		trinity_dce_enable_voltage_adjustment(rdev, false);
}
#endif

static void trinity_add_dccac_value(struct radeon_device *rdev)
{
	u32 gpu_cac_avrg_cntl_window_size;
	u32 num_active_displays = rdev->pm.dpm.new_active_crtc_count;
	u64 disp_clk = rdev->clock.default_dispclk / 100;
	u32 dc_cac_value;

	gpu_cac_avrg_cntl_window_size =
		(RREG32_SMC(GPU_CAC_AVRG_CNTL) & WINDOW_SIZE_MASK) >> WINDOW_SIZE_SHIFT;

	dc_cac_value = (u32)((14213 * disp_clk * disp_clk * (u64)num_active_displays) >>
			     (32 - gpu_cac_avrg_cntl_window_size));

	WREG32_SMC(DC_CAC_VALUE, dc_cac_value);
}

void trinity_dpm_display_configuration_changed(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	if (pi->voltage_drop_in_dce)
		trinity_dce_enable_voltage_adjustment(rdev, true);
	trinity_add_dccac_value(rdev);
}

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
};

union pplib_clock_info {
	struct _ATOM_PPLIB_R600_CLOCK_INFO r600;
	struct _ATOM_PPLIB_RS780_CLOCK_INFO rs780;
	struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO evergreen;
	struct _ATOM_PPLIB_SUMO_CLOCK_INFO sumo;
};

union pplib_power_state {
	struct _ATOM_PPLIB_STATE v1;
	struct _ATOM_PPLIB_STATE_V2 v2;
};

static void trinity_parse_pplib_non_clock_info(struct radeon_device *rdev,
					       struct radeon_ps *rps,
					       struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info,
					       u8 table_rev)
{
	struct trinity_ps *ps = trinity_get_ps(rps);

	rps->caps = le32_to_cpu(non_clock_info->ulCapsAndSettings);
	rps->class = le16_to_cpu(non_clock_info->usClassification);
	rps->class2 = le16_to_cpu(non_clock_info->usClassification2);

	if (ATOM_PPLIB_NONCLOCKINFO_VER1 < table_rev) {
		rps->vclk = le32_to_cpu(non_clock_info->ulVCLK);
		rps->dclk = le32_to_cpu(non_clock_info->ulDCLK);
	} else {
		rps->vclk = 0;
		rps->dclk = 0;
	}

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_BOOT) {
		rdev->pm.dpm.boot_ps = rps;
		trinity_patch_boot_state(rdev, ps);
	}
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void trinity_parse_pplib_clock_info(struct radeon_device *rdev,
					   struct radeon_ps *rps, int index,
					   union pplib_clock_info *clock_info)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct trinity_ps *ps = trinity_get_ps(rps);
	struct trinity_pl *pl = &ps->levels[index];
	u32 sclk;

	sclk = le16_to_cpu(clock_info->sumo.usEngineClockLow);
	sclk |= clock_info->sumo.ucEngineClockHigh << 16;
	pl->sclk = sclk;
	pl->vddc_index = clock_info->sumo.vddcIndex;

	ps->num_levels = index + 1;

	if (pi->enable_sclk_ds) {
		pl->ds_divider_index = 5;
		pl->ss_divider_index = 5;
	}
}

static int trinity_parse_power_table(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j, k, non_clock_array_index, clock_array_index;
	union pplib_clock_info *clock_info;
	struct _StateArray *state_array;
	struct _ClockInfoArray *clock_info_array;
	struct _NonClockInfoArray *non_clock_info_array;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	u8 *power_state_offset;
	struct sumo_ps *ps;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return -EINVAL;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	state_array = (struct _StateArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usStateArrayOffset));
	clock_info_array = (struct _ClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset));
	non_clock_info_array = (struct _NonClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset));

	rdev->pm.dpm.ps = kzalloc(sizeof(struct radeon_ps) *
				  state_array->ucNumEntries, GFP_KERNEL);
	if (!rdev->pm.dpm.ps)
		return -ENOMEM;
	power_state_offset = (u8 *)state_array->states;
	for (i = 0; i < state_array->ucNumEntries; i++) {
		u8 *idx;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		if (!rdev->pm.power_state[i].clock_info)
			return -EINVAL;
		ps = kzalloc(sizeof(struct sumo_ps), GFP_KERNEL);
		if (ps == NULL) {
			kfree(rdev->pm.dpm.ps);
			return -ENOMEM;
		}
		rdev->pm.dpm.ps[i].ps_priv = ps;
		k = 0;
		idx = (u8 *)&power_state->v2.clockInfoIndex[0];
		for (j = 0; j < power_state->v2.ucNumDPMLevels; j++) {
			clock_array_index = idx[j];
			if (clock_array_index >= clock_info_array->ucNumEntries)
				continue;
			if (k >= SUMO_MAX_HARDWARE_POWERLEVELS)
				break;
			clock_info = (union pplib_clock_info *)
				((u8 *)&clock_info_array->clockInfo[0] +
				 (clock_array_index * clock_info_array->ucEntrySize));
			trinity_parse_pplib_clock_info(rdev,
						       &rdev->pm.dpm.ps[i], k,
						       clock_info);
			k++;
		}
		trinity_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
						   non_clock_info,
						   non_clock_info_array->ucEntrySize);
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
	}
	rdev->pm.dpm.num_ps = state_array->ucNumEntries;
	return 0;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V5 info_5;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V6 info_6;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_7 info_7;
};

static u32 trinity_convert_did_to_freq(struct radeon_device *rdev, u8 did)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	u32 divider;

	if (did >= 8 && did <= 0x3f)
		divider = did * 25;
	else if (did > 0x3f && did <= 0x5f)
		divider = (did - 64) * 50 + 1600;
	else if (did > 0x5f && did <= 0x7e)
		divider = (did - 96) * 100 + 3200;
	else if (did == 0x7f)
		divider = 128 * 100;
	else
		return 10000;

	return ((pi->sys_info.dentist_vco_freq * 100) + (divider - 1)) / divider;
}

static int trinity_parse_sys_info_table(struct radeon_device *rdev)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *igp_info;
	u8 frev, crev;
	u16 data_offset;
	int i;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		igp_info = (union igp_info *)(mode_info->atom_context->bios +
					      data_offset);

		if (crev != 7) {
			DRM_ERROR("Unsupported IGP table: %d %d\n", frev, crev);
			return -EINVAL;
		}
		pi->sys_info.bootup_sclk = le32_to_cpu(igp_info->info_7.ulBootUpEngineClock);
		pi->sys_info.min_sclk = le32_to_cpu(igp_info->info_7.ulMinEngineClock);
		pi->sys_info.bootup_uma_clk = le32_to_cpu(igp_info->info_7.ulBootUpUMAClock);
		pi->sys_info.dentist_vco_freq = le32_to_cpu(igp_info->info_7.ulDentistVCOFreq);
		pi->sys_info.bootup_nb_voltage_index =
			le16_to_cpu(igp_info->info_7.usBootUpNBVoltage);
		if (igp_info->info_7.ucHtcTmpLmt == 0)
			pi->sys_info.htc_tmp_lmt = 203;
		else
			pi->sys_info.htc_tmp_lmt = igp_info->info_7.ucHtcTmpLmt;
		if (igp_info->info_7.ucHtcHystLmt == 0)
			pi->sys_info.htc_hyst_lmt = 5;
		else
			pi->sys_info.htc_hyst_lmt = igp_info->info_7.ucHtcHystLmt;
		if (pi->sys_info.htc_tmp_lmt <= pi->sys_info.htc_hyst_lmt) {
			DRM_ERROR("The htcTmpLmt should be larger than htcHystLmt.\n");
		}

		if (pi->enable_nbps_policy)
			pi->sys_info.nb_dpm_enable = igp_info->info_7.ucNBDPMEnable;
		else
			pi->sys_info.nb_dpm_enable = 0;

		for (i = 0; i < TRINITY_NUM_NBPSTATES; i++) {
			pi->sys_info.nbp_mclk[i] = le32_to_cpu(igp_info->info_7.ulNbpStateMemclkFreq[i]);
			pi->sys_info.nbp_nclk[i] = le32_to_cpu(igp_info->info_7.ulNbpStateNClkFreq[i]);
		}

		pi->sys_info.nbp_voltage_index[0] = le16_to_cpu(igp_info->info_7.usNBP0Voltage);
		pi->sys_info.nbp_voltage_index[1] = le16_to_cpu(igp_info->info_7.usNBP1Voltage);
		pi->sys_info.nbp_voltage_index[2] = le16_to_cpu(igp_info->info_7.usNBP2Voltage);
		pi->sys_info.nbp_voltage_index[3] = le16_to_cpu(igp_info->info_7.usNBP3Voltage);

		if (!pi->sys_info.nb_dpm_enable) {
			for (i = 1; i < TRINITY_NUM_NBPSTATES; i++) {
				pi->sys_info.nbp_mclk[i] = pi->sys_info.nbp_mclk[0];
				pi->sys_info.nbp_nclk[i] = pi->sys_info.nbp_nclk[0];
				pi->sys_info.nbp_voltage_index[i] = pi->sys_info.nbp_voltage_index[0];
			}
		}

		pi->sys_info.uma_channel_number = igp_info->info_7.ucUMAChannelNumber;

		sumo_construct_sclk_voltage_mapping_table(rdev,
							  &pi->sys_info.sclk_voltage_mapping_table,
							  igp_info->info_7.sAvail_SCLK);
		sumo_construct_vid_mapping_table(rdev, &pi->sys_info.vid_mapping_table,
						 igp_info->info_7.sAvail_SCLK);

		pi->sys_info.uvd_clock_table_entries[0].vclk_did =
			igp_info->info_7.ucDPMState0VclkFid;
		pi->sys_info.uvd_clock_table_entries[1].vclk_did =
			igp_info->info_7.ucDPMState1VclkFid;
		pi->sys_info.uvd_clock_table_entries[2].vclk_did =
			igp_info->info_7.ucDPMState2VclkFid;
		pi->sys_info.uvd_clock_table_entries[3].vclk_did =
			igp_info->info_7.ucDPMState3VclkFid;

		pi->sys_info.uvd_clock_table_entries[0].dclk_did =
			igp_info->info_7.ucDPMState0DclkFid;
		pi->sys_info.uvd_clock_table_entries[1].dclk_did =
			igp_info->info_7.ucDPMState1DclkFid;
		pi->sys_info.uvd_clock_table_entries[2].dclk_did =
			igp_info->info_7.ucDPMState2DclkFid;
		pi->sys_info.uvd_clock_table_entries[3].dclk_did =
			igp_info->info_7.ucDPMState3DclkFid;

		for (i = 0; i < 4; i++) {
			pi->sys_info.uvd_clock_table_entries[i].vclk =
				trinity_convert_did_to_freq(rdev,
							    pi->sys_info.uvd_clock_table_entries[i].vclk_did);
			pi->sys_info.uvd_clock_table_entries[i].dclk =
				trinity_convert_did_to_freq(rdev,
							    pi->sys_info.uvd_clock_table_entries[i].dclk_did);
		}



	}
	return 0;
}

int trinity_dpm_init(struct radeon_device *rdev)
{
	struct trinity_power_info *pi;
	int ret, i;

	pi = kzalloc(sizeof(struct trinity_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++)
		pi->at[i] = TRINITY_AT_DFLT;

	/* There are stability issues reported on with
	 * bapm enabled when switching between AC and battery
	 * power.  At the same time, some MSI boards hang
	 * if it's not enabled and dpm is enabled.  Just enable
	 * it for MSI boards right now.
	 */
	if (rdev->pdev->subsystem_vendor == 0x1462)
		pi->enable_bapm = true;
	else
		pi->enable_bapm = false;
	pi->enable_nbps_policy = true;
	pi->enable_sclk_ds = true;
	pi->enable_gfx_power_gating = true;
	pi->enable_gfx_clock_gating = true;
	pi->enable_mg_clock_gating = false;
	pi->enable_gfx_dynamic_mgpg = false;
	pi->override_dynamic_mgpg = false;
	pi->enable_auto_thermal_throttling = true;
	pi->voltage_drop_in_dce = false; /* need to restructure dpm/modeset interaction */
	pi->uvd_dpm = true; /* ??? */

	ret = trinity_parse_sys_info_table(rdev);
	if (ret)
		return ret;

	trinity_construct_boot_state(rdev);

	ret = r600_get_platform_caps(rdev);
	if (ret)
		return ret;

	ret = trinity_parse_power_table(rdev);
	if (ret)
		return ret;

	pi->thermal_auto_throttling = pi->sys_info.htc_tmp_lmt;
	pi->enable_dpm = true;

	return 0;
}

void trinity_dpm_print_power_state(struct radeon_device *rdev,
				   struct radeon_ps *rps)
{
	int i;
	struct trinity_ps *ps = trinity_get_ps(rps);

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->num_levels; i++) {
		struct trinity_pl *pl = &ps->levels[i];
		printk("\t\tpower level %d    sclk: %u vddc: %u\n",
		       i, pl->sclk,
		       trinity_convert_voltage_index_to_value(rdev, pl->vddc_index));
	}
	r600_dpm_print_ps_status(rdev, rps);
}

void trinity_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
							 struct seq_file *m)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct radeon_ps *rps = &pi->current_rps;
	struct trinity_ps *ps = trinity_get_ps(rps);
	struct trinity_pl *pl;
	u32 current_index =
		(RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURRENT_STATE_MASK) >>
		CURRENT_STATE_SHIFT;

	if (current_index >= ps->num_levels) {
		seq_printf(m, "invalid dpm profile %d\n", current_index);
	} else {
		pl = &ps->levels[current_index];
		seq_printf(m, "uvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
		seq_printf(m, "power level %d    sclk: %u vddc: %u\n",
			   current_index, pl->sclk,
			   trinity_convert_voltage_index_to_value(rdev, pl->vddc_index));
	}
}

void trinity_dpm_fini(struct radeon_device *rdev)
{
	int i;

	trinity_cleanup_asic(rdev); /* ??? */

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}

u32 trinity_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);
	struct trinity_ps *requested_state = trinity_get_ps(&pi->requested_rps);

	if (low)
		return requested_state->levels[0].sclk;
	else
		return requested_state->levels[requested_state->num_levels - 1].sclk;
}

u32 trinity_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct trinity_power_info *pi = trinity_get_pi(rdev);

	return pi->sys_info.bootup_uma_clk;
}
