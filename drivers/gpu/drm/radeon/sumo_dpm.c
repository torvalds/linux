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
#include "sumod.h"
#include "r600_dpm.h"
#include "cypress_dpm.h"
#include "sumo_dpm.h"

#define SUMO_MAX_DEEPSLEEP_DIVIDER_ID 5
#define SUMO_MINIMUM_ENGINE_CLOCK 800
#define BOOST_DPM_LEVEL 7

static const u32 sumo_utc[SUMO_PM_NUMBER_OF_TC] =
{
	SUMO_UTC_DFLT_00,
	SUMO_UTC_DFLT_01,
	SUMO_UTC_DFLT_02,
	SUMO_UTC_DFLT_03,
	SUMO_UTC_DFLT_04,
	SUMO_UTC_DFLT_05,
	SUMO_UTC_DFLT_06,
	SUMO_UTC_DFLT_07,
	SUMO_UTC_DFLT_08,
	SUMO_UTC_DFLT_09,
	SUMO_UTC_DFLT_10,
	SUMO_UTC_DFLT_11,
	SUMO_UTC_DFLT_12,
	SUMO_UTC_DFLT_13,
	SUMO_UTC_DFLT_14,
};

static const u32 sumo_dtc[SUMO_PM_NUMBER_OF_TC] =
{
	SUMO_DTC_DFLT_00,
	SUMO_DTC_DFLT_01,
	SUMO_DTC_DFLT_02,
	SUMO_DTC_DFLT_03,
	SUMO_DTC_DFLT_04,
	SUMO_DTC_DFLT_05,
	SUMO_DTC_DFLT_06,
	SUMO_DTC_DFLT_07,
	SUMO_DTC_DFLT_08,
	SUMO_DTC_DFLT_09,
	SUMO_DTC_DFLT_10,
	SUMO_DTC_DFLT_11,
	SUMO_DTC_DFLT_12,
	SUMO_DTC_DFLT_13,
	SUMO_DTC_DFLT_14,
};

struct sumo_ps *sumo_get_ps(struct radeon_ps *rps)
{
	struct sumo_ps *ps = rps->ps_priv;

	return ps;
}

struct sumo_power_info *sumo_get_pi(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = rdev->pm.dpm.priv;

	return pi;
}

u32 sumo_get_xclk(struct radeon_device *rdev)
{
	return rdev->clock.spll.reference_freq;
}

static void sumo_gfx_clockgating_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(SCLK_PWRMGT_CNTL, DYN_GFX_CLK_OFF_EN, ~DYN_GFX_CLK_OFF_EN);
	else {
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~DYN_GFX_CLK_OFF_EN);
		WREG32_P(SCLK_PWRMGT_CNTL, GFX_CLK_FORCE_ON, ~GFX_CLK_FORCE_ON);
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~GFX_CLK_FORCE_ON);
		RREG32(GB_ADDR_CONFIG);
	}
}

#define CGCG_CGTT_LOCAL0_MASK 0xE5BFFFFF
#define CGCG_CGTT_LOCAL1_MASK 0xEFFF07FF

static void sumo_mg_clockgating_enable(struct radeon_device *rdev, bool enable)
{
	u32 local0;
	u32 local1;

	local0 = RREG32(CG_CGTT_LOCAL_0);
	local1 = RREG32(CG_CGTT_LOCAL_1);

	if (enable) {
		WREG32(CG_CGTT_LOCAL_0, (0 & CGCG_CGTT_LOCAL0_MASK) | (local0 & ~CGCG_CGTT_LOCAL0_MASK) );
		WREG32(CG_CGTT_LOCAL_1, (0 & CGCG_CGTT_LOCAL1_MASK) | (local1 & ~CGCG_CGTT_LOCAL1_MASK) );
	} else {
		WREG32(CG_CGTT_LOCAL_0, (0xFFFFFFFF & CGCG_CGTT_LOCAL0_MASK) | (local0 & ~CGCG_CGTT_LOCAL0_MASK) );
		WREG32(CG_CGTT_LOCAL_1, (0xFFFFCFFF & CGCG_CGTT_LOCAL1_MASK) | (local1 & ~CGCG_CGTT_LOCAL1_MASK) );
	}
}

static void sumo_program_git(struct radeon_device *rdev)
{
	u32 p, u;
	u32 xclk = sumo_get_xclk(rdev);

	r600_calculate_u_and_p(SUMO_GICST_DFLT,
			       xclk, 16, &p, &u);

	WREG32_P(CG_GIT, CG_GICST(p), ~CG_GICST_MASK);
}

static void sumo_program_grsd(struct radeon_device *rdev)
{
	u32 p, u;
	u32 xclk = sumo_get_xclk(rdev);
	u32 grs = 256 * 25 / 100;

	r600_calculate_u_and_p(1, xclk, 14, &p, &u);

	WREG32(CG_GCOOR, PHC(grs) | SDC(p) | SU(u));
}

void sumo_gfx_clockgating_initialize(struct radeon_device *rdev)
{
	sumo_program_git(rdev);
	sumo_program_grsd(rdev);
}

static void sumo_gfx_powergating_initialize(struct radeon_device *rdev)
{
	u32 rcu_pwr_gating_cntl;
	u32 p, u;
	u32 p_c, p_p, d_p;
	u32 r_t, i_t;
	u32 xclk = sumo_get_xclk(rdev);

	if (rdev->family == CHIP_PALM) {
		p_c = 4;
		d_p = 10;
		r_t = 10;
		i_t = 4;
		p_p = 50 + 1000/200 + 6 * 32;
	} else {
		p_c = 16;
		d_p = 50;
		r_t = 50;
		i_t  = 50;
		p_p = 113;
	}

	WREG32(CG_SCRATCH2, 0x01B60A17);

	r600_calculate_u_and_p(SUMO_GFXPOWERGATINGT_DFLT,
			       xclk, 16, &p, &u);

	WREG32_P(CG_PWR_GATING_CNTL, PGP(p) | PGU(u),
		 ~(PGP_MASK | PGU_MASK));

	r600_calculate_u_and_p(SUMO_VOLTAGEDROPT_DFLT,
			       xclk, 16, &p, &u);

	WREG32_P(CG_CG_VOLTAGE_CNTL, PGP(p) | PGU(u),
		 ~(PGP_MASK | PGU_MASK));

	if (rdev->family == CHIP_PALM) {
		WREG32_RCU(RCU_PWR_GATING_SEQ0, 0x10103210);
		WREG32_RCU(RCU_PWR_GATING_SEQ1, 0x10101010);
	} else {
		WREG32_RCU(RCU_PWR_GATING_SEQ0, 0x76543210);
		WREG32_RCU(RCU_PWR_GATING_SEQ1, 0xFEDCBA98);
	}

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL);
	rcu_pwr_gating_cntl &=
		~(RSVD_MASK | PCV_MASK | PGS_MASK);
	rcu_pwr_gating_cntl |= PCV(p_c) | PGS(1) | PWR_GATING_EN;
	if (rdev->family == CHIP_PALM) {
		rcu_pwr_gating_cntl &= ~PCP_MASK;
		rcu_pwr_gating_cntl |= PCP(0x77);
	}
	WREG32_RCU(RCU_PWR_GATING_CNTL, rcu_pwr_gating_cntl);

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_2);
	rcu_pwr_gating_cntl &= ~(MPPU_MASK | MPPD_MASK);
	rcu_pwr_gating_cntl |= MPPU(p_p) | MPPD(50);
	WREG32_RCU(RCU_PWR_GATING_CNTL_2, rcu_pwr_gating_cntl);

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_3);
	rcu_pwr_gating_cntl &= ~(DPPU_MASK | DPPD_MASK);
	rcu_pwr_gating_cntl |= DPPU(d_p) | DPPD(50);
	WREG32_RCU(RCU_PWR_GATING_CNTL_3, rcu_pwr_gating_cntl);

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_4);
	rcu_pwr_gating_cntl &= ~(RT_MASK | IT_MASK);
	rcu_pwr_gating_cntl |= RT(r_t) | IT(i_t);
	WREG32_RCU(RCU_PWR_GATING_CNTL_4, rcu_pwr_gating_cntl);

	if (rdev->family == CHIP_PALM)
		WREG32_RCU(RCU_PWR_GATING_CNTL_5, 0xA02);

	sumo_smu_pg_init(rdev);

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL);
	rcu_pwr_gating_cntl &=
		~(RSVD_MASK | PCV_MASK | PGS_MASK);
	rcu_pwr_gating_cntl |= PCV(p_c) | PGS(4) | PWR_GATING_EN;
	if (rdev->family == CHIP_PALM) {
		rcu_pwr_gating_cntl &= ~PCP_MASK;
		rcu_pwr_gating_cntl |= PCP(0x77);
	}
	WREG32_RCU(RCU_PWR_GATING_CNTL, rcu_pwr_gating_cntl);

	if (rdev->family == CHIP_PALM) {
		rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_2);
		rcu_pwr_gating_cntl &= ~(MPPU_MASK | MPPD_MASK);
		rcu_pwr_gating_cntl |= MPPU(113) | MPPD(50);
		WREG32_RCU(RCU_PWR_GATING_CNTL_2, rcu_pwr_gating_cntl);

		rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_3);
		rcu_pwr_gating_cntl &= ~(DPPU_MASK | DPPD_MASK);
		rcu_pwr_gating_cntl |= DPPU(16) | DPPD(50);
		WREG32_RCU(RCU_PWR_GATING_CNTL_3, rcu_pwr_gating_cntl);
	}

	sumo_smu_pg_init(rdev);

	rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL);
	rcu_pwr_gating_cntl &=
		~(RSVD_MASK | PCV_MASK | PGS_MASK);
	rcu_pwr_gating_cntl |= PGS(5) | PWR_GATING_EN;

	if (rdev->family == CHIP_PALM) {
		rcu_pwr_gating_cntl |= PCV(4);
		rcu_pwr_gating_cntl &= ~PCP_MASK;
		rcu_pwr_gating_cntl |= PCP(0x77);
	} else
		rcu_pwr_gating_cntl |= PCV(11);
	WREG32_RCU(RCU_PWR_GATING_CNTL, rcu_pwr_gating_cntl);

	if (rdev->family == CHIP_PALM) {
		rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_2);
		rcu_pwr_gating_cntl &= ~(MPPU_MASK | MPPD_MASK);
		rcu_pwr_gating_cntl |= MPPU(113) | MPPD(50);
		WREG32_RCU(RCU_PWR_GATING_CNTL_2, rcu_pwr_gating_cntl);

		rcu_pwr_gating_cntl = RREG32_RCU(RCU_PWR_GATING_CNTL_3);
		rcu_pwr_gating_cntl &= ~(DPPU_MASK | DPPD_MASK);
		rcu_pwr_gating_cntl |= DPPU(22) | DPPD(50);
		WREG32_RCU(RCU_PWR_GATING_CNTL_3, rcu_pwr_gating_cntl);
	}

	sumo_smu_pg_init(rdev);
}

static void sumo_gfx_powergating_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(CG_PWR_GATING_CNTL, DYN_PWR_DOWN_EN, ~DYN_PWR_DOWN_EN);
	else {
		WREG32_P(CG_PWR_GATING_CNTL, 0, ~DYN_PWR_DOWN_EN);
		RREG32(GB_ADDR_CONFIG);
	}
}

static int sumo_enable_clock_power_gating(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (pi->enable_gfx_clock_gating)
		sumo_gfx_clockgating_initialize(rdev);
	if (pi->enable_gfx_power_gating)
		sumo_gfx_powergating_initialize(rdev);
	if (pi->enable_mg_clock_gating)
		sumo_mg_clockgating_enable(rdev, true);
	if (pi->enable_gfx_clock_gating)
		sumo_gfx_clockgating_enable(rdev, true);
	if (pi->enable_gfx_power_gating)
		sumo_gfx_powergating_enable(rdev, true);

	return 0;
}

static void sumo_disable_clock_power_gating(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (pi->enable_gfx_clock_gating)
		sumo_gfx_clockgating_enable(rdev, false);
	if (pi->enable_gfx_power_gating)
		sumo_gfx_powergating_enable(rdev, false);
	if (pi->enable_mg_clock_gating)
		sumo_mg_clockgating_enable(rdev, false);
}

static void sumo_calculate_bsp(struct radeon_device *rdev,
			       u32 high_clk)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 xclk = sumo_get_xclk(rdev);

	pi->pasi = 65535 * 100 / high_clk;
	pi->asi = 65535 * 100 / high_clk;

	r600_calculate_u_and_p(pi->asi,
			       xclk, 16, &pi->bsp, &pi->bsu);

	r600_calculate_u_and_p(pi->pasi,
			       xclk, 16, &pi->pbsp, &pi->pbsu);

	pi->dsp = BSP(pi->bsp) | BSU(pi->bsu);
	pi->psp = BSP(pi->pbsp) | BSU(pi->pbsu);
}

static void sumo_init_bsp(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	WREG32(CG_BSP_0, pi->psp);
}


static void sumo_program_bsp(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	u32 i;
	u32 highest_engine_clock = ps->levels[ps->num_levels - 1].sclk;

	if (ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE)
		highest_engine_clock = pi->boost_pl.sclk;

	sumo_calculate_bsp(rdev, highest_engine_clock);

	for (i = 0; i < ps->num_levels - 1; i++)
		WREG32(CG_BSP_0 + (i * 4), pi->dsp);

	WREG32(CG_BSP_0 + (i * 4), pi->psp);

	if (ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE)
		WREG32(CG_BSP_0 + (BOOST_DPM_LEVEL * 4), pi->psp);
}

static void sumo_write_at(struct radeon_device *rdev,
			  u32 index, u32 value)
{
	if (index == 0)
		WREG32(CG_AT_0, value);
	else if (index == 1)
		WREG32(CG_AT_1, value);
	else if (index == 2)
		WREG32(CG_AT_2, value);
	else if (index == 3)
		WREG32(CG_AT_3, value);
	else if (index == 4)
		WREG32(CG_AT_4, value);
	else if (index == 5)
		WREG32(CG_AT_5, value);
	else if (index == 6)
		WREG32(CG_AT_6, value);
	else if (index == 7)
		WREG32(CG_AT_7, value);
}

static void sumo_program_at(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	u32 asi;
	u32 i;
	u32 m_a;
	u32 a_t;
	u32 r[SUMO_MAX_HARDWARE_POWERLEVELS];
	u32 l[SUMO_MAX_HARDWARE_POWERLEVELS];

	r[0] = SUMO_R_DFLT0;
	r[1] = SUMO_R_DFLT1;
	r[2] = SUMO_R_DFLT2;
	r[3] = SUMO_R_DFLT3;
	r[4] = SUMO_R_DFLT4;

	l[0] = SUMO_L_DFLT0;
	l[1] = SUMO_L_DFLT1;
	l[2] = SUMO_L_DFLT2;
	l[3] = SUMO_L_DFLT3;
	l[4] = SUMO_L_DFLT4;

	for (i = 0; i < ps->num_levels; i++) {
		asi = (i == ps->num_levels - 1) ? pi->pasi : pi->asi;

		m_a = asi * ps->levels[i].sclk / 100;

		a_t = CG_R(m_a * r[i] / 100) | CG_L(m_a * l[i] / 100);

		sumo_write_at(rdev, i, a_t);
	}

	if (ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE) {
		asi = pi->pasi;

		m_a = asi * pi->boost_pl.sclk / 100;

		a_t = CG_R(m_a * r[ps->num_levels - 1] / 100) |
			CG_L(m_a * l[ps->num_levels - 1] / 100);

		sumo_write_at(rdev, BOOST_DPM_LEVEL, a_t);
	}
}

static void sumo_program_tp(struct radeon_device *rdev)
{
	int i;
	enum r600_td td = R600_TD_DFLT;

	for (i = 0; i < SUMO_PM_NUMBER_OF_TC; i++) {
		WREG32_P(CG_FFCT_0 + (i * 4), UTC_0(sumo_utc[i]), ~UTC_0_MASK);
		WREG32_P(CG_FFCT_0 + (i * 4), DTC_0(sumo_dtc[i]), ~DTC_0_MASK);
	}

	if (td == R600_TD_AUTO)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_FORCE_TREND_SEL);
	else
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_FORCE_TREND_SEL, ~FIR_FORCE_TREND_SEL);

	if (td == R600_TD_UP)
		WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_TREND_MODE);

	if (td == R600_TD_DOWN)
		WREG32_P(SCLK_PWRMGT_CNTL, FIR_TREND_MODE, ~FIR_TREND_MODE);
}

void sumo_program_vc(struct radeon_device *rdev, u32 vrc)
{
	WREG32(CG_FTV, vrc);
}

void sumo_clear_vc(struct radeon_device *rdev)
{
	WREG32(CG_FTV, 0);
}

void sumo_program_sstp(struct radeon_device *rdev)
{
	u32 p, u;
	u32 xclk = sumo_get_xclk(rdev);

	r600_calculate_u_and_p(SUMO_SST_DFLT,
			       xclk, 16, &p, &u);

	WREG32(CG_SSP, SSTU(u) | SST(p));
}

static void sumo_set_divider_value(struct radeon_device *rdev,
				   u32 index, u32 divider)
{
	u32 reg_index = index / 4;
	u32 field_index = index % 4;

	if (field_index == 0)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 SCLK_FSTATE_0_DIV(divider), ~SCLK_FSTATE_0_DIV_MASK);
	else if (field_index == 1)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 SCLK_FSTATE_1_DIV(divider), ~SCLK_FSTATE_1_DIV_MASK);
	else if (field_index == 2)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 SCLK_FSTATE_2_DIV(divider), ~SCLK_FSTATE_2_DIV_MASK);
	else if (field_index == 3)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 SCLK_FSTATE_3_DIV(divider), ~SCLK_FSTATE_3_DIV_MASK);
}

static void sumo_set_ds_dividers(struct radeon_device *rdev,
				 u32 index, u32 divider)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (pi->enable_sclk_ds) {
		u32 dpm_ctrl = RREG32(CG_SCLK_DPM_CTRL_6);

		dpm_ctrl &= ~(0x7 << (index * 3));
		dpm_ctrl |= (divider << (index * 3));
		WREG32(CG_SCLK_DPM_CTRL_6, dpm_ctrl);
	}
}

static void sumo_set_ss_dividers(struct radeon_device *rdev,
				 u32 index, u32 divider)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (pi->enable_sclk_ds) {
		u32 dpm_ctrl = RREG32(CG_SCLK_DPM_CTRL_11);

		dpm_ctrl &= ~(0x7 << (index * 3));
		dpm_ctrl |= (divider << (index * 3));
		WREG32(CG_SCLK_DPM_CTRL_11, dpm_ctrl);
	}
}

static void sumo_set_vid(struct radeon_device *rdev, u32 index, u32 vid)
{
	u32 voltage_cntl = RREG32(CG_DPM_VOLTAGE_CNTL);

	voltage_cntl &= ~(DPM_STATE0_LEVEL_MASK << (index * 2));
	voltage_cntl |= (vid << (DPM_STATE0_LEVEL_SHIFT + index * 2));
	WREG32(CG_DPM_VOLTAGE_CNTL, voltage_cntl);
}

static void sumo_set_allos_gnb_slow(struct radeon_device *rdev, u32 index, u32 gnb_slow)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 temp = gnb_slow;
	u32 cg_sclk_dpm_ctrl_3;

	if (pi->driver_nbps_policy_disable)
		temp = 1;

	cg_sclk_dpm_ctrl_3 = RREG32(CG_SCLK_DPM_CTRL_3);
	cg_sclk_dpm_ctrl_3 &= ~(GNB_SLOW_FSTATE_0_MASK << index);
	cg_sclk_dpm_ctrl_3 |= (temp << (GNB_SLOW_FSTATE_0_SHIFT + index));

	WREG32(CG_SCLK_DPM_CTRL_3, cg_sclk_dpm_ctrl_3);
}

static void sumo_program_power_level(struct radeon_device *rdev,
				     struct sumo_pl *pl, u32 index)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	int ret;
	struct atom_clock_dividers dividers;
	u32 ds_en = RREG32(DEEP_SLEEP_CNTL) & ENABLE_DS;

	ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
					     pl->sclk, false, &dividers);
	if (ret)
		return;

	sumo_set_divider_value(rdev, index, dividers.post_div);

	sumo_set_vid(rdev, index, pl->vddc_index);

	if (pl->ss_divider_index == 0 || pl->ds_divider_index == 0) {
		if (ds_en)
			WREG32_P(DEEP_SLEEP_CNTL, 0, ~ENABLE_DS);
	} else {
		sumo_set_ss_dividers(rdev, index, pl->ss_divider_index);
		sumo_set_ds_dividers(rdev, index, pl->ds_divider_index);

		if (!ds_en)
			WREG32_P(DEEP_SLEEP_CNTL, ENABLE_DS, ~ENABLE_DS);
	}

	sumo_set_allos_gnb_slow(rdev, index, pl->allow_gnb_slow);

	if (pi->enable_boost)
		sumo_set_tdp_limit(rdev, index, pl->sclk_dpm_tdp_limit);
}

static void sumo_power_level_enable(struct radeon_device *rdev, u32 index, bool enable)
{
	u32 reg_index = index / 4;
	u32 field_index = index % 4;

	if (field_index == 0)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 enable ? SCLK_FSTATE_0_VLD : 0, ~SCLK_FSTATE_0_VLD);
	else if (field_index == 1)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 enable ? SCLK_FSTATE_1_VLD : 0, ~SCLK_FSTATE_1_VLD);
	else if (field_index == 2)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 enable ? SCLK_FSTATE_2_VLD : 0, ~SCLK_FSTATE_2_VLD);
	else if (field_index == 3)
		WREG32_P(CG_SCLK_DPM_CTRL + (reg_index * 4),
			 enable ? SCLK_FSTATE_3_VLD : 0, ~SCLK_FSTATE_3_VLD);
}

static bool sumo_dpm_enabled(struct radeon_device *rdev)
{
	if (RREG32(CG_SCLK_DPM_CTRL_3) & DPM_SCLK_ENABLE)
		return true;
	else
		return false;
}

static void sumo_start_dpm(struct radeon_device *rdev)
{
	WREG32_P(CG_SCLK_DPM_CTRL_3, DPM_SCLK_ENABLE, ~DPM_SCLK_ENABLE);
}

static void sumo_stop_dpm(struct radeon_device *rdev)
{
	WREG32_P(CG_SCLK_DPM_CTRL_3, 0, ~DPM_SCLK_ENABLE);
}

static void sumo_set_forced_mode(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32_P(CG_SCLK_DPM_CTRL_3, FORCE_SCLK_STATE_EN, ~FORCE_SCLK_STATE_EN);
	else
		WREG32_P(CG_SCLK_DPM_CTRL_3, 0, ~FORCE_SCLK_STATE_EN);
}

static void sumo_set_forced_mode_enabled(struct radeon_device *rdev)
{
	int i;

	sumo_set_forced_mode(rdev, true);
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(CG_SCLK_STATUS) & SCLK_OVERCLK_DETECT)
			break;
		udelay(1);
	}
}

static void sumo_wait_for_level_0(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURR_SCLK_INDEX_MASK) == 0)
			break;
		udelay(1);
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(TARGET_AND_CURRENT_PROFILE_INDEX) & CURR_INDEX_MASK) == 0)
			break;
		udelay(1);
	}
}

static void sumo_set_forced_mode_disabled(struct radeon_device *rdev)
{
	sumo_set_forced_mode(rdev, false);
}

static void sumo_enable_power_level_0(struct radeon_device *rdev)
{
	sumo_power_level_enable(rdev, 0, true);
}

static void sumo_patch_boost_state(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);

	if (new_ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE) {
		pi->boost_pl = new_ps->levels[new_ps->num_levels - 1];
		pi->boost_pl.sclk = pi->sys_info.boost_sclk;
		pi->boost_pl.vddc_index = pi->sys_info.boost_vid_2bit;
		pi->boost_pl.sclk_dpm_tdp_limit = pi->sys_info.sclk_dpm_tdp_limit_boost;
	}
}

static void sumo_pre_notify_alt_vddnb_change(struct radeon_device *rdev)
{
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	struct sumo_ps *old_ps = sumo_get_ps(rdev->pm.dpm.current_ps);
	u32 nbps1_old = 0;
	u32 nbps1_new = 0;

	if (old_ps != NULL)
		nbps1_old = (old_ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE) ? 1 : 0;

	nbps1_new = (new_ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE) ? 1 : 0;

	if (nbps1_old == 1 && nbps1_new == 0)
		sumo_smu_notify_alt_vddnb_change(rdev, 0, 0);
}

static void sumo_post_notify_alt_vddnb_change(struct radeon_device *rdev)
{
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	struct sumo_ps *old_ps = sumo_get_ps(rdev->pm.dpm.current_ps);
	u32 nbps1_old = 0;
	u32 nbps1_new = 0;

	if (old_ps != NULL)
		nbps1_old = (old_ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE)? 1 : 0;

	nbps1_new = (new_ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE)? 1 : 0;

	if (nbps1_old == 0 && nbps1_new == 1)
		sumo_smu_notify_alt_vddnb_change(rdev, 1, 1);
}

static void sumo_enable_boost(struct radeon_device *rdev, bool enable)
{
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);

	if (enable) {
		if (new_ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE)
			sumo_boost_state_enable(rdev, true);
	} else
		sumo_boost_state_enable(rdev, false);
}

static void sumo_update_current_power_levels(struct radeon_device *rdev)
{
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	pi->current_ps = *new_ps;
}

static void sumo_set_forced_level(struct radeon_device *rdev, u32 index)
{
	WREG32_P(CG_SCLK_DPM_CTRL_3, FORCE_SCLK_STATE(index), ~FORCE_SCLK_STATE_MASK);
}

static void sumo_set_forced_level_0(struct radeon_device *rdev)
{
	sumo_set_forced_level(rdev, 0);
}

static void sumo_program_wl(struct radeon_device *rdev)
{
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	u32 dpm_ctrl4 = RREG32(CG_SCLK_DPM_CTRL_4);

	dpm_ctrl4 &= 0xFFFFFF00;
	dpm_ctrl4 |= (1 << (new_ps->num_levels - 1));

	if (new_ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE)
		dpm_ctrl4 |= (1 << BOOST_DPM_LEVEL);

	WREG32(CG_SCLK_DPM_CTRL_4, dpm_ctrl4);
}

static void sumo_program_power_levels_0_to_n(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);
	struct sumo_ps *old_ps = sumo_get_ps(rdev->pm.dpm.current_ps);
	u32 i;
	u32 n_current_state_levels = (old_ps == NULL) ? 1 : old_ps->num_levels;

	for (i = 0; i < new_ps->num_levels; i++) {
		sumo_program_power_level(rdev, &new_ps->levels[i], i);
		sumo_power_level_enable(rdev, i, true);
	}

	for (i = new_ps->num_levels; i < n_current_state_levels; i++)
		sumo_power_level_enable(rdev, i, false);

	if (new_ps->flags & SUMO_POWERSTATE_FLAGS_BOOST_STATE)
		sumo_program_power_level(rdev, &pi->boost_pl, BOOST_DPM_LEVEL);
}

static void sumo_enable_acpi_pm(struct radeon_device *rdev)
{
	WREG32_P(GENERAL_PWRMGT, STATIC_PM_EN, ~STATIC_PM_EN);
}

static void sumo_program_power_level_enter_state(struct radeon_device *rdev)
{
	WREG32_P(CG_SCLK_DPM_CTRL_5, SCLK_FSTATE_BOOTUP(0), ~SCLK_FSTATE_BOOTUP_MASK);
}

static void sumo_program_acpi_power_level(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct atom_clock_dividers dividers;
	int ret;

        ret = radeon_atom_get_clock_dividers(rdev, COMPUTE_ENGINE_PLL_PARAM,
                                             pi->acpi_pl.sclk,
					     false, &dividers);
	if (ret)
		return;

	WREG32_P(CG_ACPI_CNTL, SCLK_ACPI_DIV(dividers.post_div), ~SCLK_ACPI_DIV_MASK);
	WREG32_P(CG_ACPI_VOLTAGE_CNTL, 0, ~ACPI_VOLTAGE_EN);
}

static void sumo_program_bootup_state(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 dpm_ctrl4 = RREG32(CG_SCLK_DPM_CTRL_4);
	u32 i;

	sumo_program_power_level(rdev, &pi->boot_pl, 0);

	dpm_ctrl4 &= 0xFFFFFF00;
	WREG32(CG_SCLK_DPM_CTRL_4, dpm_ctrl4);

	for (i = 1; i < 8; i++)
		sumo_power_level_enable(rdev, i, false);
}

void sumo_take_smu_control(struct radeon_device *rdev, bool enable)
{
/* This bit selects who handles display phy powergating.
 * Clear the bit to let atom handle it.
 * Set it to let the driver handle it.
 * For now we just let atom handle it.
 */
#if 0
	u32 v = RREG32(DOUT_SCRATCH3);

	if (enable)
		v |= 0x4;
	else
		v &= 0xFFFFFFFB;

	WREG32(DOUT_SCRATCH3, v);
#endif
}

static void sumo_enable_sclk_ds(struct radeon_device *rdev, bool enable)
{
	if (enable) {
		u32 deep_sleep_cntl = RREG32(DEEP_SLEEP_CNTL);
		u32 deep_sleep_cntl2 = RREG32(DEEP_SLEEP_CNTL2);
		u32 t = 1;

		deep_sleep_cntl &= ~R_DIS;
		deep_sleep_cntl &= ~HS_MASK;
		deep_sleep_cntl |= HS(t > 4095 ? 4095 : t);

		deep_sleep_cntl2 |= LB_UFP_EN;
		deep_sleep_cntl2 &= INOUT_C_MASK;
		deep_sleep_cntl2 |= INOUT_C(0xf);

		WREG32(DEEP_SLEEP_CNTL2, deep_sleep_cntl2);
		WREG32(DEEP_SLEEP_CNTL, deep_sleep_cntl);
	} else
		WREG32_P(DEEP_SLEEP_CNTL, 0, ~ENABLE_DS);
}

static void sumo_program_bootup_at(struct radeon_device *rdev)
{
	WREG32_P(CG_AT_0, CG_R(0xffff), ~CG_R_MASK);
	WREG32_P(CG_AT_0, CG_L(0), ~CG_L_MASK);
}

static void sumo_reset_am(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, FIR_RESET, ~FIR_RESET);
}

static void sumo_start_am(struct radeon_device *rdev)
{
	WREG32_P(SCLK_PWRMGT_CNTL, 0, ~FIR_RESET);
}

static void sumo_program_ttp(struct radeon_device *rdev)
{
	u32 xclk = sumo_get_xclk(rdev);
	u32 p, u;
	u32 cg_sclk_dpm_ctrl_5 = RREG32(CG_SCLK_DPM_CTRL_5);

	r600_calculate_u_and_p(1000,
			       xclk, 16, &p, &u);

	cg_sclk_dpm_ctrl_5 &= ~(TT_TP_MASK | TT_TU_MASK);
	cg_sclk_dpm_ctrl_5 |= TT_TP(p) | TT_TU(u);

	WREG32(CG_SCLK_DPM_CTRL_5, cg_sclk_dpm_ctrl_5);
}

static void sumo_program_ttt(struct radeon_device *rdev)
{
	u32 cg_sclk_dpm_ctrl_3 = RREG32(CG_SCLK_DPM_CTRL_3);
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	cg_sclk_dpm_ctrl_3 &= ~(GNB_TT_MASK | GNB_THERMTHRO_MASK);
	cg_sclk_dpm_ctrl_3 |= GNB_TT(pi->thermal_auto_throttling + 49);

	WREG32(CG_SCLK_DPM_CTRL_3, cg_sclk_dpm_ctrl_3);
}


static void sumo_enable_voltage_scaling(struct radeon_device *rdev, bool enable)
{
	if (enable) {
		WREG32_P(CG_DPM_VOLTAGE_CNTL, DPM_VOLTAGE_EN, ~DPM_VOLTAGE_EN);
		WREG32_P(CG_CG_VOLTAGE_CNTL, 0, ~CG_VOLTAGE_EN);
	} else {
		WREG32_P(CG_CG_VOLTAGE_CNTL, CG_VOLTAGE_EN, ~CG_VOLTAGE_EN);
		WREG32_P(CG_DPM_VOLTAGE_CNTL, 0, ~DPM_VOLTAGE_EN);
	}
}

static void sumo_override_cnb_thermal_events(struct radeon_device *rdev)
{
	WREG32_P(CG_SCLK_DPM_CTRL_3, CNB_THERMTHRO_MASK_SCLK,
		 ~CNB_THERMTHRO_MASK_SCLK);
}

static void sumo_program_dc_hto(struct radeon_device *rdev)
{
	u32 cg_sclk_dpm_ctrl_4 = RREG32(CG_SCLK_DPM_CTRL_4);
	u32 p, u;
	u32 xclk = sumo_get_xclk(rdev);

	r600_calculate_u_and_p(100000,
			       xclk, 14, &p, &u);

	cg_sclk_dpm_ctrl_4 &= ~(DC_HDC_MASK | DC_HU_MASK);
	cg_sclk_dpm_ctrl_4 |= DC_HDC(p) | DC_HU(u);

	WREG32(CG_SCLK_DPM_CTRL_4, cg_sclk_dpm_ctrl_4);
}

static void sumo_force_nbp_state(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *new_ps = sumo_get_ps(rdev->pm.dpm.requested_ps);

	if (!pi->driver_nbps_policy_disable) {
		if (new_ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE)
			WREG32_P(CG_SCLK_DPM_CTRL_3, FORCE_NB_PSTATE_1, ~FORCE_NB_PSTATE_1);
		else
			WREG32_P(CG_SCLK_DPM_CTRL_3, 0, ~FORCE_NB_PSTATE_1);
	}
}

u32 sumo_get_sleep_divider_from_id(u32 id)
{
	return 1 << id;
}

u32 sumo_get_sleep_divider_id_from_clock(struct radeon_device *rdev,
					 u32 sclk,
					 u32 min_sclk_in_sr)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 i;
	u32 temp;
	u32 min = (min_sclk_in_sr > SUMO_MINIMUM_ENGINE_CLOCK) ?
		min_sclk_in_sr : SUMO_MINIMUM_ENGINE_CLOCK;

	if (sclk < min)
		return 0;

	if (!pi->enable_sclk_ds)
		return 0;

	for (i = SUMO_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		temp = sclk / sumo_get_sleep_divider_from_id(i);

		if (temp >= min || i == 0)
			break;
	}
	return i;
}

static u32 sumo_get_valid_engine_clock(struct radeon_device *rdev,
				       u32 lower_limit)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 i;

	for (i = 0; i < pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries; i++) {
		if (pi->sys_info.sclk_voltage_mapping_table.entries[i].sclk_frequency >= lower_limit)
			return pi->sys_info.sclk_voltage_mapping_table.entries[i].sclk_frequency;
	}

	return pi->sys_info.sclk_voltage_mapping_table.entries[pi->sys_info.sclk_voltage_mapping_table.num_max_dpm_entries - 1].sclk_frequency;
}

static void sumo_patch_thermal_state(struct radeon_device *rdev,
				     struct sumo_ps *ps,
				     struct sumo_ps *current_ps)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
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

	ps->levels[0].ss_divider_index =
		sumo_get_sleep_divider_id_from_clock(rdev, ps->levels[0].sclk, sclk_in_sr);

	ps->levels[0].ds_divider_index =
		sumo_get_sleep_divider_id_from_clock(rdev, ps->levels[0].sclk, SUMO_MINIMUM_ENGINE_CLOCK);

	if (ps->levels[0].ds_divider_index > ps->levels[0].ss_divider_index + 1)
		ps->levels[0].ds_divider_index = ps->levels[0].ss_divider_index + 1;

	if (ps->levels[0].ss_divider_index == ps->levels[0].ds_divider_index) {
		if (ps->levels[0].ss_divider_index > 1)
			ps->levels[0].ss_divider_index = ps->levels[0].ss_divider_index - 1;
	}

	if (ps->levels[0].ss_divider_index == 0)
		ps->levels[0].ds_divider_index = 0;

	if (ps->levels[0].ds_divider_index == 0)
		ps->levels[0].ss_divider_index = 0;
}

static void sumo_apply_state_adjust_rules(struct radeon_device *rdev)
{
	struct radeon_ps *rps = rdev->pm.dpm.requested_ps;
	struct sumo_ps *ps = sumo_get_ps(rps);
	struct sumo_ps *current_ps = sumo_get_ps(rdev->pm.dpm.current_ps);
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 min_voltage = 0; /* ??? */
	u32 min_sclk = pi->sys_info.min_sclk; /* XXX check against disp reqs */
	u32 sclk_in_sr = pi->sys_info.min_sclk; /* ??? */
	u32 i;

	if (rps->class & ATOM_PPLIB_CLASSIFICATION_THERMAL)
		return sumo_patch_thermal_state(rdev, ps, current_ps);

	if (pi->enable_boost) {
		if (rps->class & ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE)
			ps->flags |= SUMO_POWERSTATE_FLAGS_BOOST_STATE;
	}

	if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UI_BATTERY) ||
	    (rps->class & ATOM_PPLIB_CLASSIFICATION_SDSTATE) ||
	    (rps->class & ATOM_PPLIB_CLASSIFICATION_HDSTATE))
		ps->flags |= SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE;

	for (i = 0; i < ps->num_levels; i++) {
		if (ps->levels[i].vddc_index < min_voltage)
			ps->levels[i].vddc_index = min_voltage;

		if (ps->levels[i].sclk < min_sclk)
			ps->levels[i].sclk =
				sumo_get_valid_engine_clock(rdev, min_sclk);

		ps->levels[i].ss_divider_index =
			sumo_get_sleep_divider_id_from_clock(rdev, ps->levels[i].sclk, sclk_in_sr);

		ps->levels[i].ds_divider_index =
			sumo_get_sleep_divider_id_from_clock(rdev, ps->levels[i].sclk, SUMO_MINIMUM_ENGINE_CLOCK);

		if (ps->levels[i].ds_divider_index > ps->levels[i].ss_divider_index + 1)
			ps->levels[i].ds_divider_index = ps->levels[i].ss_divider_index + 1;

		if (ps->levels[i].ss_divider_index == ps->levels[i].ds_divider_index) {
			if (ps->levels[i].ss_divider_index > 1)
				ps->levels[i].ss_divider_index = ps->levels[i].ss_divider_index - 1;
		}

		if (ps->levels[i].ss_divider_index == 0)
			ps->levels[i].ds_divider_index = 0;

		if (ps->levels[i].ds_divider_index == 0)
			ps->levels[i].ss_divider_index = 0;

		if (ps->flags & SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE)
			ps->levels[i].allow_gnb_slow = 1;
		else if ((rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE) ||
			 (rps->class2 & ATOM_PPLIB_CLASSIFICATION2_MVC))
			ps->levels[i].allow_gnb_slow = 0;
		else if (i == ps->num_levels - 1)
			ps->levels[i].allow_gnb_slow = 0;
		else
			ps->levels[i].allow_gnb_slow = 1;
	}
}

static void sumo_cleanup_asic(struct radeon_device *rdev)
{
	sumo_take_smu_control(rdev, false);
}

static int sumo_set_thermal_temperature_range(struct radeon_device *rdev,
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

	WREG32_P(CG_THERMAL_INT, DIG_THERM_INTH(49 + (high_temp / 1000)), ~DIG_THERM_INTH_MASK);
	WREG32_P(CG_THERMAL_INT, DIG_THERM_INTL(49 + (low_temp / 1000)), ~DIG_THERM_INTL_MASK);

	rdev->pm.dpm.thermal.min_temp = low_temp;
	rdev->pm.dpm.thermal.max_temp = high_temp;

	return 0;
}

int sumo_dpm_enable(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (sumo_dpm_enabled(rdev))
		return -EINVAL;

	sumo_enable_clock_power_gating(rdev);
	sumo_program_bootup_state(rdev);
	sumo_init_bsp(rdev);
	sumo_reset_am(rdev);
	sumo_program_tp(rdev);
	sumo_program_bootup_at(rdev);
	sumo_start_am(rdev);
	if (pi->enable_auto_thermal_throttling) {
		sumo_program_ttp(rdev);
		sumo_program_ttt(rdev);
	}
	sumo_program_dc_hto(rdev);
	sumo_program_power_level_enter_state(rdev);
	sumo_enable_voltage_scaling(rdev, true);
	sumo_program_sstp(rdev);
	sumo_program_vc(rdev, SUMO_VRC_DFLT);
	sumo_override_cnb_thermal_events(rdev);
	sumo_start_dpm(rdev);
	sumo_wait_for_level_0(rdev);
	if (pi->enable_sclk_ds)
		sumo_enable_sclk_ds(rdev, true);
	if (pi->enable_boost)
		sumo_enable_boost_timer(rdev);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		sumo_set_thermal_temperature_range(rdev, R600_TEMP_RANGE_MIN, R600_TEMP_RANGE_MAX);
		rdev->irq.dpm_thermal = true;
		radeon_irq_set(rdev);
	}

	return 0;
}

void sumo_dpm_disable(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (!sumo_dpm_enabled(rdev))
		return;
	sumo_disable_clock_power_gating(rdev);
	if (pi->enable_sclk_ds)
		sumo_enable_sclk_ds(rdev, false);
	sumo_clear_vc(rdev);
	sumo_wait_for_level_0(rdev);
	sumo_stop_dpm(rdev);
	sumo_enable_voltage_scaling(rdev, false);

	if (rdev->irq.installed &&
	    r600_is_internal_thermal_sensor(rdev->pm.int_thermal_type)) {
		rdev->irq.dpm_thermal = false;
		radeon_irq_set(rdev);
	}
}

int sumo_dpm_set_power_state(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	if (pi->enable_dynamic_patch_ps)
		sumo_apply_state_adjust_rules(rdev);
	sumo_update_current_power_levels(rdev);
	if (pi->enable_boost) {
		sumo_enable_boost(rdev, false);
		sumo_patch_boost_state(rdev);
	}
	if (pi->enable_dpm) {
		sumo_pre_notify_alt_vddnb_change(rdev);
		sumo_enable_power_level_0(rdev);
		sumo_set_forced_level_0(rdev);
		sumo_set_forced_mode_enabled(rdev);
		sumo_wait_for_level_0(rdev);
		sumo_program_power_levels_0_to_n(rdev);
		sumo_program_wl(rdev);
		sumo_program_bsp(rdev);
		sumo_program_at(rdev);
		sumo_force_nbp_state(rdev);
		sumo_set_forced_mode_disabled(rdev);
		sumo_set_forced_mode_enabled(rdev);
		sumo_set_forced_mode_disabled(rdev);
		sumo_post_notify_alt_vddnb_change(rdev);
	}
	if (pi->enable_boost)
		sumo_enable_boost(rdev, true);

	return 0;
}

void sumo_dpm_reset_asic(struct radeon_device *rdev)
{
	sumo_program_bootup_state(rdev);
	sumo_enable_power_level_0(rdev);
	sumo_set_forced_level_0(rdev);
	sumo_set_forced_mode_enabled(rdev);
	sumo_wait_for_level_0(rdev);
	sumo_set_forced_mode_disabled(rdev);
	sumo_set_forced_mode_enabled(rdev);
	sumo_set_forced_mode_disabled(rdev);
}

void sumo_dpm_setup_asic(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	sumo_initialize_m3_arb(rdev);
	pi->fw_version = sumo_get_running_fw_version(rdev);
	DRM_INFO("Found smc ucode version: 0x%08x\n", pi->fw_version);
	sumo_program_acpi_power_level(rdev);
	sumo_enable_acpi_pm(rdev);
	sumo_take_smu_control(rdev, true);
}

void sumo_dpm_display_configuration_changed(struct radeon_device *rdev)
{

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

static void sumo_patch_boot_state(struct radeon_device *rdev,
				  struct sumo_ps *ps)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	ps->num_levels = 1;
	ps->flags = 0;
	ps->levels[0] = pi->boot_pl;
}

static void sumo_parse_pplib_non_clock_info(struct radeon_device *rdev,
					    struct radeon_ps *rps,
					    struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info,
					    u8 table_rev)
{
	struct sumo_ps *ps = sumo_get_ps(rps);

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
		sumo_patch_boot_state(rdev, ps);
	}
	if (rps->class & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		rdev->pm.dpm.uvd_ps = rps;
}

static void sumo_parse_pplib_clock_info(struct radeon_device *rdev,
					struct radeon_ps *rps, int index,
					union pplib_clock_info *clock_info)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	struct sumo_ps *ps = sumo_get_ps(rps);
	struct sumo_pl *pl = &ps->levels[index];
	u32 sclk;

	sclk = le16_to_cpu(clock_info->sumo.usEngineClockLow);
	sclk |= clock_info->sumo.ucEngineClockHigh << 16;
	pl->sclk = sclk;
	pl->vddc_index = clock_info->sumo.vddcIndex;
	pl->sclk_dpm_tdp_limit = clock_info->sumo.tdpLimit;

	ps->num_levels = index + 1;

	if (pi->enable_sclk_ds) {
		pl->ds_divider_index = 5;
		pl->ss_divider_index = 4;
	}
}

static int sumo_parse_power_table(struct radeon_device *rdev)
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
	rdev->pm.dpm.platform_caps = le32_to_cpu(power_info->pplib.ulPlatformCaps);
	rdev->pm.dpm.backbias_response_time = le16_to_cpu(power_info->pplib.usBackbiasTime);
	rdev->pm.dpm.voltage_response_time = le16_to_cpu(power_info->pplib.usVoltageTime);
	for (i = 0; i < state_array->ucNumEntries; i++) {
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
		for (j = 0; j < power_state->v2.ucNumDPMLevels; j++) {
			clock_array_index = power_state->v2.clockInfoIndex[j];
			if (k >= SUMO_MAX_HARDWARE_POWERLEVELS)
				break;
			clock_info = (union pplib_clock_info *)
				&clock_info_array->clockInfo[clock_array_index * clock_info_array->ucEntrySize];
			sumo_parse_pplib_clock_info(rdev,
						    &rdev->pm.dpm.ps[i], k,
						    clock_info);
			k++;
		}
		sumo_parse_pplib_non_clock_info(rdev, &rdev->pm.dpm.ps[i],
						non_clock_info,
						non_clock_info_array->ucEntrySize);
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
	}
	rdev->pm.dpm.num_ps = state_array->ucNumEntries;
	return 0;
}

u32 sumo_convert_vid2_to_vid7(struct radeon_device *rdev,
			      struct sumo_vid_mapping_table *vid_mapping_table,
			      u32 vid_2bit)
{
	u32 i;

	for (i = 0; i < vid_mapping_table->num_entries; i++) {
		if (vid_mapping_table->entries[i].vid_2bit == vid_2bit)
			return vid_mapping_table->entries[i].vid_7bit;
	}

	return vid_mapping_table->entries[vid_mapping_table->num_entries - 1].vid_7bit;
}

static u16 sumo_convert_voltage_index_to_value(struct radeon_device *rdev,
					       u32 vid_2bit)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
	u32 vid_7bit = sumo_convert_vid2_to_vid7(rdev, &pi->sys_info.vid_mapping_table, vid_2bit);

	if (vid_7bit > 0x7C)
		return 0;

	return (15500 - vid_7bit * 125 + 5) / 10;
}

static void sumo_construct_display_voltage_mapping_table(struct radeon_device *rdev,
							 struct sumo_disp_clock_voltage_mapping_table *disp_clk_voltage_mapping_table,
							 ATOM_CLK_VOLT_CAPABILITY *table)
{
	u32 i;

	for (i = 0; i < SUMO_MAX_NUMBER_VOLTAGES; i++) {
		if (table[i].ulMaximumSupportedCLK == 0)
			break;

		disp_clk_voltage_mapping_table->display_clock_frequency[i] =
			table[i].ulMaximumSupportedCLK;
	}

	disp_clk_voltage_mapping_table->num_max_voltage_levels = i;

	if (disp_clk_voltage_mapping_table->num_max_voltage_levels == 0) {
		disp_clk_voltage_mapping_table->display_clock_frequency[0] = 80000;
		disp_clk_voltage_mapping_table->num_max_voltage_levels = 1;
	}
}

void sumo_construct_sclk_voltage_mapping_table(struct radeon_device *rdev,
					       struct sumo_sclk_voltage_mapping_table *sclk_voltage_mapping_table,
					       ATOM_AVAILABLE_SCLK_LIST *table)
{
	u32 i;
	u32 n = 0;
	u32 prev_sclk = 0;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++) {
		if (table[i].ulSupportedSCLK > prev_sclk) {
			sclk_voltage_mapping_table->entries[n].sclk_frequency =
				table[i].ulSupportedSCLK;
			sclk_voltage_mapping_table->entries[n].vid_2bit =
				table[i].usVoltageIndex;
			prev_sclk = table[i].ulSupportedSCLK;
			n++;
		}
	}

	sclk_voltage_mapping_table->num_max_dpm_entries = n;
}

void sumo_construct_vid_mapping_table(struct radeon_device *rdev,
				      struct sumo_vid_mapping_table *vid_mapping_table,
				      ATOM_AVAILABLE_SCLK_LIST *table)
{
	u32 i, j;

	for (i = 0; i < SUMO_MAX_HARDWARE_POWERLEVELS; i++) {
		if (table[i].ulSupportedSCLK != 0) {
			vid_mapping_table->entries[table[i].usVoltageIndex].vid_7bit =
				table[i].usVoltageID;
			vid_mapping_table->entries[table[i].usVoltageIndex].vid_2bit =
				table[i].usVoltageIndex;
		}
	}

	for (i = 0; i < SUMO_MAX_NUMBER_VOLTAGES; i++) {
		if (vid_mapping_table->entries[i].vid_7bit == 0) {
			for (j = i + 1; j < SUMO_MAX_NUMBER_VOLTAGES; j++) {
				if (vid_mapping_table->entries[j].vid_7bit != 0) {
					vid_mapping_table->entries[i] =
						vid_mapping_table->entries[j];
					vid_mapping_table->entries[j].vid_7bit = 0;
					break;
				}
			}

			if (j == SUMO_MAX_NUMBER_VOLTAGES)
				break;
		}
	}

	vid_mapping_table->num_entries = i;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V5 info_5;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V6 info_6;
};

static int sumo_parse_sys_info_table(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);
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

		if (crev != 6) {
			DRM_ERROR("Unsupported IGP table: %d %d\n", frev, crev);
			return -EINVAL;
		}
		pi->sys_info.bootup_sclk = le32_to_cpu(igp_info->info_6.ulBootUpEngineClock);
		pi->sys_info.min_sclk = le32_to_cpu(igp_info->info_6.ulMinEngineClock);
		pi->sys_info.bootup_uma_clk = le32_to_cpu(igp_info->info_6.ulBootUpUMAClock);
		pi->sys_info.bootup_nb_voltage_index =
			le16_to_cpu(igp_info->info_6.usBootUpNBVoltage);
		if (igp_info->info_6.ucHtcTmpLmt == 0)
			pi->sys_info.htc_tmp_lmt = 203;
		else
			pi->sys_info.htc_tmp_lmt = igp_info->info_6.ucHtcTmpLmt;
		if (igp_info->info_6.ucHtcHystLmt == 0)
			pi->sys_info.htc_hyst_lmt = 5;
		else
			pi->sys_info.htc_hyst_lmt = igp_info->info_6.ucHtcHystLmt;
		if (pi->sys_info.htc_tmp_lmt <= pi->sys_info.htc_hyst_lmt) {
			DRM_ERROR("The htcTmpLmt should be larger than htcHystLmt.\n");
		}
		for (i = 0; i < NUMBER_OF_M3ARB_PARAM_SETS; i++) {
			pi->sys_info.csr_m3_arb_cntl_default[i] =
				le32_to_cpu(igp_info->info_6.ulCSR_M3_ARB_CNTL_DEFAULT[i]);
			pi->sys_info.csr_m3_arb_cntl_uvd[i] =
				le32_to_cpu(igp_info->info_6.ulCSR_M3_ARB_CNTL_UVD[i]);
			pi->sys_info.csr_m3_arb_cntl_fs3d[i] =
				le32_to_cpu(igp_info->info_6.ulCSR_M3_ARB_CNTL_FS3D[i]);
		}
		pi->sys_info.sclk_dpm_boost_margin =
			le32_to_cpu(igp_info->info_6.SclkDpmBoostMargin);
		pi->sys_info.sclk_dpm_throttle_margin =
			le32_to_cpu(igp_info->info_6.SclkDpmThrottleMargin);
		pi->sys_info.sclk_dpm_tdp_limit_pg =
			le16_to_cpu(igp_info->info_6.SclkDpmTdpLimitPG);
		pi->sys_info.gnb_tdp_limit = le16_to_cpu(igp_info->info_6.GnbTdpLimit);
		pi->sys_info.sclk_dpm_tdp_limit_boost =
			le16_to_cpu(igp_info->info_6.SclkDpmTdpLimitBoost);
		pi->sys_info.boost_sclk = le32_to_cpu(igp_info->info_6.ulBoostEngineCLock);
		pi->sys_info.boost_vid_2bit = igp_info->info_6.ulBoostVid_2bit;
		if (igp_info->info_6.EnableBoost)
			pi->sys_info.enable_boost = true;
		else
			pi->sys_info.enable_boost = false;
		sumo_construct_display_voltage_mapping_table(rdev,
							     &pi->sys_info.disp_clk_voltage_mapping_table,
							     igp_info->info_6.sDISPCLK_Voltage);
		sumo_construct_sclk_voltage_mapping_table(rdev,
							  &pi->sys_info.sclk_voltage_mapping_table,
							  igp_info->info_6.sAvail_SCLK);
		sumo_construct_vid_mapping_table(rdev, &pi->sys_info.vid_mapping_table,
						 igp_info->info_6.sAvail_SCLK);

	}
	return 0;
}

static void sumo_construct_boot_and_acpi_state(struct radeon_device *rdev)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	pi->boot_pl.sclk = pi->sys_info.bootup_sclk;
	pi->boot_pl.vddc_index = pi->sys_info.bootup_nb_voltage_index;
	pi->boot_pl.ds_divider_index = 0;
	pi->boot_pl.ss_divider_index = 0;
	pi->boot_pl.allow_gnb_slow = 1;
	pi->acpi_pl = pi->boot_pl;
	pi->current_ps.num_levels = 1;
	pi->current_ps.levels[0] = pi->boot_pl;
}

int sumo_dpm_init(struct radeon_device *rdev)
{
	struct sumo_power_info *pi;
	u32 hw_rev = (RREG32(HW_REV) & ATI_REV_ID_MASK) >> ATI_REV_ID_SHIFT;
	int ret;

	pi = kzalloc(sizeof(struct sumo_power_info), GFP_KERNEL);
	if (pi == NULL)
		return -ENOMEM;
	rdev->pm.dpm.priv = pi;

	pi->driver_nbps_policy_disable = false;
	if ((rdev->family == CHIP_PALM) && (hw_rev < 3))
		pi->disable_gfx_power_gating_in_uvd = true;
	else
		pi->disable_gfx_power_gating_in_uvd = false;
	pi->enable_alt_vddnb = true;
	pi->enable_sclk_ds = true;
	pi->enable_dynamic_m3_arbiter = false;
	pi->enable_dynamic_patch_ps = true;
	pi->enable_gfx_power_gating = true;
	pi->enable_gfx_clock_gating = true;
	pi->enable_mg_clock_gating = true;
	pi->enable_auto_thermal_throttling = true;

	ret = sumo_parse_sys_info_table(rdev);
	if (ret)
		return ret;

	sumo_construct_boot_and_acpi_state(rdev);

	ret = sumo_parse_power_table(rdev);
	if (ret)
		return ret;

	pi->pasi = CYPRESS_HASI_DFLT;
	pi->asi = RV770_ASI_DFLT;
	pi->thermal_auto_throttling = pi->sys_info.htc_tmp_lmt;
	pi->enable_boost = pi->sys_info.enable_boost;
	pi->enable_dpm = true;

	return 0;
}

void sumo_dpm_print_power_state(struct radeon_device *rdev,
				struct radeon_ps *rps)
{
	int i;
	struct sumo_ps *ps = sumo_get_ps(rps);

	r600_dpm_print_class_info(rps->class, rps->class2);
	r600_dpm_print_cap_info(rps->caps);
	printk("\tuvd    vclk: %d dclk: %d\n", rps->vclk, rps->dclk);
	for (i = 0; i < ps->num_levels; i++) {
		struct sumo_pl *pl = &ps->levels[i];
		printk("\t\tpower level %d    sclk: %u vddc: %u\n",
		       i, pl->sclk,
		       sumo_convert_voltage_index_to_value(rdev, pl->vddc_index));
	}
	r600_dpm_print_ps_status(rdev, rps);
}

void sumo_dpm_fini(struct radeon_device *rdev)
{
	int i;

	sumo_cleanup_asic(rdev); /* ??? */

	for (i = 0; i < rdev->pm.dpm.num_ps; i++) {
		kfree(rdev->pm.dpm.ps[i].ps_priv);
	}
	kfree(rdev->pm.dpm.ps);
	kfree(rdev->pm.dpm.priv);
}

u32 sumo_dpm_get_sclk(struct radeon_device *rdev, bool low)
{
	struct sumo_ps *requested_state = sumo_get_ps(rdev->pm.dpm.requested_ps);

	if (low)
		return requested_state->levels[0].sclk;
	else
		return requested_state->levels[requested_state->num_levels - 1].sclk;
}

u32 sumo_dpm_get_mclk(struct radeon_device *rdev, bool low)
{
	struct sumo_power_info *pi = sumo_get_pi(rdev);

	return pi->sys_info.bootup_uma_clk;
}
