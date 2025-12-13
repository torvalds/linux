// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_panel.h"
#include "intel_pch_refclk.h"
#include "intel_sbi.h"
#include "intel_sbi_regs.h"

static void lpt_fdi_reset_mphy(struct intel_display *display)
{
	int ret;

	intel_de_rmw(display, SOUTH_CHICKEN2, 0, FDI_MPHY_IOSFSB_RESET_CTL);

	ret = intel_de_wait_custom(display, SOUTH_CHICKEN2,
				   FDI_MPHY_IOSFSB_RESET_STATUS, FDI_MPHY_IOSFSB_RESET_STATUS,
				   100, 0, NULL);
	if (ret)
		drm_err(display->drm, "FDI mPHY reset assert timeout\n");

	intel_de_rmw(display, SOUTH_CHICKEN2, FDI_MPHY_IOSFSB_RESET_CTL, 0);

	ret = intel_de_wait_custom(display, SOUTH_CHICKEN2,
				   FDI_MPHY_IOSFSB_RESET_STATUS, 0,
				   100, 0, NULL);
	if (ret)
		drm_err(display->drm, "FDI mPHY reset de-assert timeout\n");
}

/* WaMPhyProgramming:hsw */
static void lpt_fdi_program_mphy(struct intel_display *display)
{
	u32 tmp;

	lpt_fdi_reset_mphy(display);

	tmp = intel_sbi_read(display, 0x8008, SBI_MPHY);
	tmp &= ~(0xFF << 24);
	tmp |= (0x12 << 24);
	intel_sbi_write(display, 0x8008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2008, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(display, 0x2008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2108, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(display, 0x2108, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x206C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(display, 0x206C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x216C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(display, 0x216C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2080, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(display, 0x2080, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2180, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(display, 0x2180, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x208C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(display, 0x208C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x218C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(display, 0x218C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2098, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(display, 0x2098, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x2198, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(display, 0x2198, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x20C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(display, 0x20C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x21C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(display, 0x21C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x20EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(display, 0x20EC, tmp, SBI_MPHY);

	tmp = intel_sbi_read(display, 0x21EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(display, 0x21EC, tmp, SBI_MPHY);
}

void lpt_disable_iclkip(struct intel_display *display)
{
	u32 temp;

	intel_de_write(display, PIXCLK_GATE, PIXCLK_GATE_GATE);

	intel_sbi_lock(display);

	temp = intel_sbi_read(display, SBI_SSCCTL6, SBI_ICLK);
	temp |= SBI_SSCCTL_DISABLE;
	intel_sbi_write(display, SBI_SSCCTL6, temp, SBI_ICLK);

	intel_sbi_unlock(display);
}

struct iclkip_params {
	u32 iclk_virtual_root_freq;
	u32 iclk_pi_range;
	u32 divsel, phaseinc, auxdiv, phasedir, desired_divisor;
};

static void iclkip_params_init(struct iclkip_params *p)
{
	memset(p, 0, sizeof(*p));

	p->iclk_virtual_root_freq = 172800 * 1000;
	p->iclk_pi_range = 64;
}

static int lpt_iclkip_freq(struct iclkip_params *p)
{
	return DIV_ROUND_CLOSEST(p->iclk_virtual_root_freq,
				 p->desired_divisor << p->auxdiv);
}

static void lpt_compute_iclkip(struct iclkip_params *p, int clock)
{
	iclkip_params_init(p);

	/* The iCLK virtual clock root frequency is in MHz,
	 * but the adjusted_mode->crtc_clock in KHz. To get the
	 * divisors, it is necessary to divide one by another, so we
	 * convert the virtual clock precision to KHz here for higher
	 * precision.
	 */
	for (p->auxdiv = 0; p->auxdiv < 2; p->auxdiv++) {
		p->desired_divisor = DIV_ROUND_CLOSEST(p->iclk_virtual_root_freq,
						       clock << p->auxdiv);
		p->divsel = (p->desired_divisor / p->iclk_pi_range) - 2;
		p->phaseinc = p->desired_divisor % p->iclk_pi_range;

		/*
		 * Near 20MHz is a corner case which is
		 * out of range for the 7-bit divisor
		 */
		if (p->divsel <= 0x7f)
			break;
	}
}

int lpt_iclkip(const struct intel_crtc_state *crtc_state)
{
	struct iclkip_params p;

	lpt_compute_iclkip(&p, crtc_state->hw.adjusted_mode.crtc_clock);

	return lpt_iclkip_freq(&p);
}

/* Program iCLKIP clock to the desired frequency */
void lpt_program_iclkip(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	int clock = crtc_state->hw.adjusted_mode.crtc_clock;
	struct iclkip_params p;
	u32 temp;

	lpt_disable_iclkip(display);

	lpt_compute_iclkip(&p, clock);
	drm_WARN_ON(display->drm, lpt_iclkip_freq(&p) != clock);

	/* This should not happen with any sane values */
	drm_WARN_ON(display->drm, SBI_SSCDIVINTPHASE_DIVSEL(p.divsel) &
		    ~SBI_SSCDIVINTPHASE_DIVSEL_MASK);
	drm_WARN_ON(display->drm, SBI_SSCDIVINTPHASE_DIR(p.phasedir) &
		    ~SBI_SSCDIVINTPHASE_INCVAL_MASK);

	drm_dbg_kms(display->drm,
		    "iCLKIP clock: found settings for %dKHz refresh rate: auxdiv=%x, divsel=%x, phasedir=%x, phaseinc=%x\n",
		    clock, p.auxdiv, p.divsel, p.phasedir, p.phaseinc);

	intel_sbi_lock(display);

	/* Program SSCDIVINTPHASE6 */
	temp = intel_sbi_read(display, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	temp &= ~SBI_SSCDIVINTPHASE_DIVSEL_MASK;
	temp |= SBI_SSCDIVINTPHASE_DIVSEL(p.divsel);
	temp &= ~SBI_SSCDIVINTPHASE_INCVAL_MASK;
	temp |= SBI_SSCDIVINTPHASE_INCVAL(p.phaseinc);
	temp |= SBI_SSCDIVINTPHASE_DIR(p.phasedir);
	temp |= SBI_SSCDIVINTPHASE_PROPAGATE;
	intel_sbi_write(display, SBI_SSCDIVINTPHASE6, temp, SBI_ICLK);

	/* Program SSCAUXDIV */
	temp = intel_sbi_read(display, SBI_SSCAUXDIV6, SBI_ICLK);
	temp &= ~SBI_SSCAUXDIV_FINALDIV2SEL(1);
	temp |= SBI_SSCAUXDIV_FINALDIV2SEL(p.auxdiv);
	intel_sbi_write(display, SBI_SSCAUXDIV6, temp, SBI_ICLK);

	/* Enable modulator and associated divider */
	temp = intel_sbi_read(display, SBI_SSCCTL6, SBI_ICLK);
	temp &= ~SBI_SSCCTL_DISABLE;
	intel_sbi_write(display, SBI_SSCCTL6, temp, SBI_ICLK);

	intel_sbi_unlock(display);

	/* Wait for initialization time */
	udelay(24);

	intel_de_write(display, PIXCLK_GATE, PIXCLK_GATE_UNGATE);
}

int lpt_get_iclkip(struct intel_display *display)
{
	struct iclkip_params p;
	u32 temp;

	if ((intel_de_read(display, PIXCLK_GATE) & PIXCLK_GATE_UNGATE) == 0)
		return 0;

	iclkip_params_init(&p);

	intel_sbi_lock(display);

	temp = intel_sbi_read(display, SBI_SSCCTL6, SBI_ICLK);
	if (temp & SBI_SSCCTL_DISABLE) {
		intel_sbi_unlock(display);
		return 0;
	}

	temp = intel_sbi_read(display, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	p.divsel = (temp & SBI_SSCDIVINTPHASE_DIVSEL_MASK) >>
		SBI_SSCDIVINTPHASE_DIVSEL_SHIFT;
	p.phaseinc = (temp & SBI_SSCDIVINTPHASE_INCVAL_MASK) >>
		SBI_SSCDIVINTPHASE_INCVAL_SHIFT;

	temp = intel_sbi_read(display, SBI_SSCAUXDIV6, SBI_ICLK);
	p.auxdiv = (temp & SBI_SSCAUXDIV_FINALDIV2SEL_MASK) >>
		SBI_SSCAUXDIV_FINALDIV2SEL_SHIFT;

	intel_sbi_unlock(display);

	p.desired_divisor = (p.divsel + 2) * p.iclk_pi_range + p.phaseinc;

	return lpt_iclkip_freq(&p);
}

/* Implements 3 different sequences from BSpec chapter "Display iCLK
 * Programming" based on the parameters passed:
 * - Sequence to enable CLKOUT_DP
 * - Sequence to enable CLKOUT_DP without spread
 * - Sequence to enable CLKOUT_DP for FDI usage and configure PCH FDI I/O
 */
static void lpt_enable_clkout_dp(struct intel_display *display,
				 bool with_spread, bool with_fdi)
{
	u32 reg, tmp;

	if (drm_WARN(display->drm, with_fdi && !with_spread,
		     "FDI requires downspread\n"))
		with_spread = true;
	if (drm_WARN(display->drm, HAS_PCH_LPT_LP(display) &&
		     with_fdi, "LP PCH doesn't have FDI\n"))
		with_fdi = false;

	intel_sbi_lock(display);

	tmp = intel_sbi_read(display, SBI_SSCCTL, SBI_ICLK);
	tmp &= ~SBI_SSCCTL_DISABLE;
	tmp |= SBI_SSCCTL_PATHALT;
	intel_sbi_write(display, SBI_SSCCTL, tmp, SBI_ICLK);

	udelay(24);

	if (with_spread) {
		tmp = intel_sbi_read(display, SBI_SSCCTL, SBI_ICLK);
		tmp &= ~SBI_SSCCTL_PATHALT;
		intel_sbi_write(display, SBI_SSCCTL, tmp, SBI_ICLK);

		if (with_fdi)
			lpt_fdi_program_mphy(display);
	}

	reg = HAS_PCH_LPT_LP(display) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(display, reg, SBI_ICLK);
	tmp |= SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(display, reg, tmp, SBI_ICLK);

	intel_sbi_unlock(display);
}

/* Sequence to disable CLKOUT_DP */
void lpt_disable_clkout_dp(struct intel_display *display)
{
	u32 reg, tmp;

	intel_sbi_lock(display);

	reg = HAS_PCH_LPT_LP(display) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(display, reg, SBI_ICLK);
	tmp &= ~SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(display, reg, tmp, SBI_ICLK);

	tmp = intel_sbi_read(display, SBI_SSCCTL, SBI_ICLK);
	if (!(tmp & SBI_SSCCTL_DISABLE)) {
		if (!(tmp & SBI_SSCCTL_PATHALT)) {
			tmp |= SBI_SSCCTL_PATHALT;
			intel_sbi_write(display, SBI_SSCCTL, tmp, SBI_ICLK);
			udelay(32);
		}
		tmp |= SBI_SSCCTL_DISABLE;
		intel_sbi_write(display, SBI_SSCCTL, tmp, SBI_ICLK);
	}

	intel_sbi_unlock(display);
}

#define BEND_IDX(steps) ((50 + (steps)) / 5)

static const u16 sscdivintphase[] = {
	[BEND_IDX( 50)] = 0x3B23,
	[BEND_IDX( 45)] = 0x3B23,
	[BEND_IDX( 40)] = 0x3C23,
	[BEND_IDX( 35)] = 0x3C23,
	[BEND_IDX( 30)] = 0x3D23,
	[BEND_IDX( 25)] = 0x3D23,
	[BEND_IDX( 20)] = 0x3E23,
	[BEND_IDX( 15)] = 0x3E23,
	[BEND_IDX( 10)] = 0x3F23,
	[BEND_IDX(  5)] = 0x3F23,
	[BEND_IDX(  0)] = 0x0025,
	[BEND_IDX( -5)] = 0x0025,
	[BEND_IDX(-10)] = 0x0125,
	[BEND_IDX(-15)] = 0x0125,
	[BEND_IDX(-20)] = 0x0225,
	[BEND_IDX(-25)] = 0x0225,
	[BEND_IDX(-30)] = 0x0325,
	[BEND_IDX(-35)] = 0x0325,
	[BEND_IDX(-40)] = 0x0425,
	[BEND_IDX(-45)] = 0x0425,
	[BEND_IDX(-50)] = 0x0525,
};

/*
 * Bend CLKOUT_DP
 * steps -50 to 50 inclusive, in steps of 5
 * < 0 slow down the clock, > 0 speed up the clock, 0 == no bend (135MHz)
 * change in clock period = -(steps / 10) * 5.787 ps
 */
static void lpt_bend_clkout_dp(struct intel_display *display, int steps)
{
	u32 tmp;
	int idx = BEND_IDX(steps);

	if (drm_WARN_ON(display->drm, steps % 5 != 0))
		return;

	if (drm_WARN_ON(display->drm, idx >= ARRAY_SIZE(sscdivintphase)))
		return;

	intel_sbi_lock(display);

	if (steps % 10 != 0)
		tmp = 0xAAAAAAAB;
	else
		tmp = 0x00000000;
	intel_sbi_write(display, SBI_SSCDITHPHASE, tmp, SBI_ICLK);

	tmp = intel_sbi_read(display, SBI_SSCDIVINTPHASE, SBI_ICLK);
	tmp &= 0xffff0000;
	tmp |= sscdivintphase[idx];
	intel_sbi_write(display, SBI_SSCDIVINTPHASE, tmp, SBI_ICLK);

	intel_sbi_unlock(display);
}

#undef BEND_IDX

static bool spll_uses_pch_ssc(struct intel_display *display)
{
	u32 fuse_strap = intel_de_read(display, FUSE_STRAP);
	u32 ctl = intel_de_read(display, SPLL_CTL);

	if ((ctl & SPLL_PLL_ENABLE) == 0)
		return false;

	if ((ctl & SPLL_REF_MASK) == SPLL_REF_MUXED_SSC &&
	    (fuse_strap & HSW_CPU_SSC_ENABLE) == 0)
		return true;

	if (display->platform.broadwell &&
	    (ctl & SPLL_REF_MASK) == SPLL_REF_PCH_SSC_BDW)
		return true;

	return false;
}

static bool wrpll_uses_pch_ssc(struct intel_display *display, enum intel_dpll_id id)
{
	u32 fuse_strap = intel_de_read(display, FUSE_STRAP);
	u32 ctl = intel_de_read(display, WRPLL_CTL(id));

	if ((ctl & WRPLL_PLL_ENABLE) == 0)
		return false;

	if ((ctl & WRPLL_REF_MASK) == WRPLL_REF_PCH_SSC)
		return true;

	if ((display->platform.broadwell || display->platform.haswell_ult) &&
	    (ctl & WRPLL_REF_MASK) == WRPLL_REF_MUXED_SSC_BDW &&
	    (fuse_strap & HSW_CPU_SSC_ENABLE) == 0)
		return true;

	return false;
}

static void lpt_init_pch_refclk(struct intel_display *display)
{
	struct intel_encoder *encoder;
	bool has_fdi = false;

	for_each_intel_encoder(display->drm, encoder) {
		switch (encoder->type) {
		case INTEL_OUTPUT_ANALOG:
			has_fdi = true;
			break;
		default:
			break;
		}
	}

	/*
	 * The BIOS may have decided to use the PCH SSC
	 * reference so we must not disable it until the
	 * relevant PLLs have stopped relying on it. We'll
	 * just leave the PCH SSC reference enabled in case
	 * any active PLL is using it. It will get disabled
	 * after runtime suspend if we don't have FDI.
	 *
	 * TODO: Move the whole reference clock handling
	 * to the modeset sequence proper so that we can
	 * actually enable/disable/reconfigure these things
	 * safely. To do that we need to introduce a real
	 * clock hierarchy. That would also allow us to do
	 * clock bending finally.
	 */
	display->dpll.pch_ssc_use = 0;

	if (spll_uses_pch_ssc(display)) {
		drm_dbg_kms(display->drm, "SPLL using PCH SSC\n");
		display->dpll.pch_ssc_use |= BIT(DPLL_ID_SPLL);
	}

	if (wrpll_uses_pch_ssc(display, DPLL_ID_WRPLL1)) {
		drm_dbg_kms(display->drm, "WRPLL1 using PCH SSC\n");
		display->dpll.pch_ssc_use |= BIT(DPLL_ID_WRPLL1);
	}

	if (wrpll_uses_pch_ssc(display, DPLL_ID_WRPLL2)) {
		drm_dbg_kms(display->drm, "WRPLL2 using PCH SSC\n");
		display->dpll.pch_ssc_use |= BIT(DPLL_ID_WRPLL2);
	}

	if (display->dpll.pch_ssc_use)
		return;

	if (has_fdi) {
		lpt_bend_clkout_dp(display, 0);
		lpt_enable_clkout_dp(display, true, true);
	} else {
		lpt_disable_clkout_dp(display);
	}
}

static void ilk_init_pch_refclk(struct intel_display *display)
{
	struct intel_encoder *encoder;
	struct intel_dpll *pll;
	int i;
	u32 val, final;
	bool has_lvds = false;
	bool has_cpu_edp = false;
	bool has_panel = false;
	bool has_ck505 = false;
	bool can_ssc = false;
	bool using_ssc_source = false;

	/* We need to take the global config into account */
	for_each_intel_encoder(display->drm, encoder) {
		switch (encoder->type) {
		case INTEL_OUTPUT_LVDS:
			has_panel = true;
			has_lvds = true;
			break;
		case INTEL_OUTPUT_EDP:
			has_panel = true;
			if (encoder->port == PORT_A)
				has_cpu_edp = true;
			break;
		default:
			break;
		}
	}

	if (HAS_PCH_IBX(display)) {
		has_ck505 = display->vbt.display_clock_mode;
		can_ssc = has_ck505;
	} else {
		has_ck505 = false;
		can_ssc = true;
	}

	/* Check if any DPLLs are using the SSC source */
	for_each_dpll(display, pll, i) {
		u32 temp;

		temp = intel_de_read(display, PCH_DPLL(pll->info->id));

		if (!(temp & DPLL_VCO_ENABLE))
			continue;

		if ((temp & PLL_REF_INPUT_MASK) ==
		    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
			using_ssc_source = true;
			break;
		}
	}

	drm_dbg_kms(display->drm,
		    "has_panel %d has_lvds %d has_ck505 %d using_ssc_source %d\n",
		    has_panel, has_lvds, has_ck505, using_ssc_source);

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	val = intel_de_read(display, PCH_DREF_CONTROL);

	/* As we must carefully and slowly disable/enable each source in turn,
	 * compute the final state we want first and check if we need to
	 * make any changes at all.
	 */
	final = val;
	final &= ~DREF_NONSPREAD_SOURCE_MASK;
	if (has_ck505)
		final |= DREF_NONSPREAD_CK505_ENABLE;
	else
		final |= DREF_NONSPREAD_SOURCE_ENABLE;

	final &= ~DREF_SSC_SOURCE_MASK;
	final &= ~DREF_CPU_SOURCE_OUTPUT_MASK;
	final &= ~DREF_SSC1_ENABLE;

	if (has_panel) {
		final |= DREF_SSC_SOURCE_ENABLE;

		if (intel_panel_use_ssc(display) && can_ssc)
			final |= DREF_SSC1_ENABLE;

		if (has_cpu_edp) {
			if (intel_panel_use_ssc(display) && can_ssc)
				final |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			else
				final |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
		} else {
			final |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
		}
	} else if (using_ssc_source) {
		final |= DREF_SSC_SOURCE_ENABLE;
		final |= DREF_SSC1_ENABLE;
	}

	if (final == val)
		return;

	/* Always enable nonspread source */
	val &= ~DREF_NONSPREAD_SOURCE_MASK;

	if (has_ck505)
		val |= DREF_NONSPREAD_CK505_ENABLE;
	else
		val |= DREF_NONSPREAD_SOURCE_ENABLE;

	if (has_panel) {
		val &= ~DREF_SSC_SOURCE_MASK;
		val |= DREF_SSC_SOURCE_ENABLE;

		/* SSC must be turned on before enabling the CPU output  */
		if (intel_panel_use_ssc(display) && can_ssc) {
			drm_dbg_kms(display->drm, "Using SSC on panel\n");
			val |= DREF_SSC1_ENABLE;
		} else {
			val &= ~DREF_SSC1_ENABLE;
		}

		/* Get SSC going before enabling the outputs */
		intel_de_write(display, PCH_DREF_CONTROL, val);
		intel_de_posting_read(display, PCH_DREF_CONTROL);
		udelay(200);

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Enable CPU source on CPU attached eDP */
		if (has_cpu_edp) {
			if (intel_panel_use_ssc(display) && can_ssc) {
				drm_dbg_kms(display->drm,
					    "Using SSC on eDP\n");
				val |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			} else {
				val |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
			}
		} else {
			val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
		}

		intel_de_write(display, PCH_DREF_CONTROL, val);
		intel_de_posting_read(display, PCH_DREF_CONTROL);
		udelay(200);
	} else {
		drm_dbg_kms(display->drm, "Disabling CPU source output\n");

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Turn off CPU output */
		val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		intel_de_write(display, PCH_DREF_CONTROL, val);
		intel_de_posting_read(display, PCH_DREF_CONTROL);
		udelay(200);

		if (!using_ssc_source) {
			drm_dbg_kms(display->drm, "Disabling SSC source\n");

			/* Turn off the SSC source */
			val &= ~DREF_SSC_SOURCE_MASK;
			val |= DREF_SSC_SOURCE_DISABLE;

			/* Turn off SSC1 */
			val &= ~DREF_SSC1_ENABLE;

			intel_de_write(display, PCH_DREF_CONTROL, val);
			intel_de_posting_read(display, PCH_DREF_CONTROL);
			udelay(200);
		}
	}

	drm_WARN_ON(display->drm, val != final);
}

/*
 * Initialize reference clocks when the driver loads
 */
void intel_init_pch_refclk(struct intel_display *display)
{
	if (HAS_PCH_IBX(display) || HAS_PCH_CPT(display))
		ilk_init_pch_refclk(display);
	else if (HAS_PCH_LPT(display))
		lpt_init_pch_refclk(display);
}
