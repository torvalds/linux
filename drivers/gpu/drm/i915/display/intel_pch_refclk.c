// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_panel.h"
#include "intel_pch_refclk.h"
#include "intel_sbi.h"

static void lpt_fdi_reset_mphy(struct drm_i915_private *dev_priv)
{
	intel_de_rmw(dev_priv, SOUTH_CHICKEN2, 0, FDI_MPHY_IOSFSB_RESET_CTL);

	if (wait_for_us(intel_de_read(dev_priv, SOUTH_CHICKEN2) &
			FDI_MPHY_IOSFSB_RESET_STATUS, 100))
		drm_err(&dev_priv->drm, "FDI mPHY reset assert timeout\n");

	intel_de_rmw(dev_priv, SOUTH_CHICKEN2, FDI_MPHY_IOSFSB_RESET_CTL, 0);

	if (wait_for_us((intel_de_read(dev_priv, SOUTH_CHICKEN2) &
			 FDI_MPHY_IOSFSB_RESET_STATUS) == 0, 100))
		drm_err(&dev_priv->drm, "FDI mPHY reset de-assert timeout\n");
}

/* WaMPhyProgramming:hsw */
static void lpt_fdi_program_mphy(struct drm_i915_private *dev_priv)
{
	u32 tmp;

	lpt_fdi_reset_mphy(dev_priv);

	tmp = intel_sbi_read(dev_priv, 0x8008, SBI_MPHY);
	tmp &= ~(0xFF << 24);
	tmp |= (0x12 << 24);
	intel_sbi_write(dev_priv, 0x8008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2008, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(dev_priv, 0x2008, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2108, SBI_MPHY);
	tmp |= (1 << 11);
	intel_sbi_write(dev_priv, 0x2108, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x206C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(dev_priv, 0x206C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x216C, SBI_MPHY);
	tmp |= (1 << 24) | (1 << 21) | (1 << 18);
	intel_sbi_write(dev_priv, 0x216C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2080, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(dev_priv, 0x2080, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2180, SBI_MPHY);
	tmp &= ~(7 << 13);
	tmp |= (5 << 13);
	intel_sbi_write(dev_priv, 0x2180, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x208C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(dev_priv, 0x208C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x218C, SBI_MPHY);
	tmp &= ~0xFF;
	tmp |= 0x1C;
	intel_sbi_write(dev_priv, 0x218C, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2098, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(dev_priv, 0x2098, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x2198, SBI_MPHY);
	tmp &= ~(0xFF << 16);
	tmp |= (0x1C << 16);
	intel_sbi_write(dev_priv, 0x2198, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x20C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(dev_priv, 0x20C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x21C4, SBI_MPHY);
	tmp |= (1 << 27);
	intel_sbi_write(dev_priv, 0x21C4, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x20EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(dev_priv, 0x20EC, tmp, SBI_MPHY);

	tmp = intel_sbi_read(dev_priv, 0x21EC, SBI_MPHY);
	tmp &= ~(0xF << 28);
	tmp |= (4 << 28);
	intel_sbi_write(dev_priv, 0x21EC, tmp, SBI_MPHY);
}

void lpt_disable_iclkip(struct drm_i915_private *dev_priv)
{
	u32 temp;

	intel_de_write(dev_priv, PIXCLK_GATE, PIXCLK_GATE_GATE);

	mutex_lock(&dev_priv->sb_lock);

	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	temp |= SBI_SSCCTL_DISABLE;
	intel_sbi_write(dev_priv, SBI_SSCCTL6, temp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
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
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int clock = crtc_state->hw.adjusted_mode.crtc_clock;
	struct iclkip_params p;
	u32 temp;

	lpt_disable_iclkip(dev_priv);

	lpt_compute_iclkip(&p, clock);
	drm_WARN_ON(&dev_priv->drm, lpt_iclkip_freq(&p) != clock);

	/* This should not happen with any sane values */
	drm_WARN_ON(&dev_priv->drm, SBI_SSCDIVINTPHASE_DIVSEL(p.divsel) &
		    ~SBI_SSCDIVINTPHASE_DIVSEL_MASK);
	drm_WARN_ON(&dev_priv->drm, SBI_SSCDIVINTPHASE_DIR(p.phasedir) &
		    ~SBI_SSCDIVINTPHASE_INCVAL_MASK);

	drm_dbg_kms(&dev_priv->drm,
		    "iCLKIP clock: found settings for %dKHz refresh rate: auxdiv=%x, divsel=%x, phasedir=%x, phaseinc=%x\n",
		    clock, p.auxdiv, p.divsel, p.phasedir, p.phaseinc);

	mutex_lock(&dev_priv->sb_lock);

	/* Program SSCDIVINTPHASE6 */
	temp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	temp &= ~SBI_SSCDIVINTPHASE_DIVSEL_MASK;
	temp |= SBI_SSCDIVINTPHASE_DIVSEL(p.divsel);
	temp &= ~SBI_SSCDIVINTPHASE_INCVAL_MASK;
	temp |= SBI_SSCDIVINTPHASE_INCVAL(p.phaseinc);
	temp |= SBI_SSCDIVINTPHASE_DIR(p.phasedir);
	temp |= SBI_SSCDIVINTPHASE_PROPAGATE;
	intel_sbi_write(dev_priv, SBI_SSCDIVINTPHASE6, temp, SBI_ICLK);

	/* Program SSCAUXDIV */
	temp = intel_sbi_read(dev_priv, SBI_SSCAUXDIV6, SBI_ICLK);
	temp &= ~SBI_SSCAUXDIV_FINALDIV2SEL(1);
	temp |= SBI_SSCAUXDIV_FINALDIV2SEL(p.auxdiv);
	intel_sbi_write(dev_priv, SBI_SSCAUXDIV6, temp, SBI_ICLK);

	/* Enable modulator and associated divider */
	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	temp &= ~SBI_SSCCTL_DISABLE;
	intel_sbi_write(dev_priv, SBI_SSCCTL6, temp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);

	/* Wait for initialization time */
	udelay(24);

	intel_de_write(dev_priv, PIXCLK_GATE, PIXCLK_GATE_UNGATE);
}

int lpt_get_iclkip(struct drm_i915_private *dev_priv)
{
	struct iclkip_params p;
	u32 temp;

	if ((intel_de_read(dev_priv, PIXCLK_GATE) & PIXCLK_GATE_UNGATE) == 0)
		return 0;

	iclkip_params_init(&p);

	mutex_lock(&dev_priv->sb_lock);

	temp = intel_sbi_read(dev_priv, SBI_SSCCTL6, SBI_ICLK);
	if (temp & SBI_SSCCTL_DISABLE) {
		mutex_unlock(&dev_priv->sb_lock);
		return 0;
	}

	temp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE6, SBI_ICLK);
	p.divsel = (temp & SBI_SSCDIVINTPHASE_DIVSEL_MASK) >>
		SBI_SSCDIVINTPHASE_DIVSEL_SHIFT;
	p.phaseinc = (temp & SBI_SSCDIVINTPHASE_INCVAL_MASK) >>
		SBI_SSCDIVINTPHASE_INCVAL_SHIFT;

	temp = intel_sbi_read(dev_priv, SBI_SSCAUXDIV6, SBI_ICLK);
	p.auxdiv = (temp & SBI_SSCAUXDIV_FINALDIV2SEL_MASK) >>
		SBI_SSCAUXDIV_FINALDIV2SEL_SHIFT;

	mutex_unlock(&dev_priv->sb_lock);

	p.desired_divisor = (p.divsel + 2) * p.iclk_pi_range + p.phaseinc;

	return lpt_iclkip_freq(&p);
}

/* Implements 3 different sequences from BSpec chapter "Display iCLK
 * Programming" based on the parameters passed:
 * - Sequence to enable CLKOUT_DP
 * - Sequence to enable CLKOUT_DP without spread
 * - Sequence to enable CLKOUT_DP for FDI usage and configure PCH FDI I/O
 */
static void lpt_enable_clkout_dp(struct drm_i915_private *dev_priv,
				 bool with_spread, bool with_fdi)
{
	u32 reg, tmp;

	if (drm_WARN(&dev_priv->drm, with_fdi && !with_spread,
		     "FDI requires downspread\n"))
		with_spread = true;
	if (drm_WARN(&dev_priv->drm, HAS_PCH_LPT_LP(dev_priv) &&
		     with_fdi, "LP PCH doesn't have FDI\n"))
		with_fdi = false;

	mutex_lock(&dev_priv->sb_lock);

	tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
	tmp &= ~SBI_SSCCTL_DISABLE;
	tmp |= SBI_SSCCTL_PATHALT;
	intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);

	udelay(24);

	if (with_spread) {
		tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
		tmp &= ~SBI_SSCCTL_PATHALT;
		intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);

		if (with_fdi)
			lpt_fdi_program_mphy(dev_priv);
	}

	reg = HAS_PCH_LPT_LP(dev_priv) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp |= SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

/* Sequence to disable CLKOUT_DP */
void lpt_disable_clkout_dp(struct drm_i915_private *dev_priv)
{
	u32 reg, tmp;

	mutex_lock(&dev_priv->sb_lock);

	reg = HAS_PCH_LPT_LP(dev_priv) ? SBI_GEN0 : SBI_DBUFF0;
	tmp = intel_sbi_read(dev_priv, reg, SBI_ICLK);
	tmp &= ~SBI_GEN0_CFG_BUFFENABLE_DISABLE;
	intel_sbi_write(dev_priv, reg, tmp, SBI_ICLK);

	tmp = intel_sbi_read(dev_priv, SBI_SSCCTL, SBI_ICLK);
	if (!(tmp & SBI_SSCCTL_DISABLE)) {
		if (!(tmp & SBI_SSCCTL_PATHALT)) {
			tmp |= SBI_SSCCTL_PATHALT;
			intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);
			udelay(32);
		}
		tmp |= SBI_SSCCTL_DISABLE;
		intel_sbi_write(dev_priv, SBI_SSCCTL, tmp, SBI_ICLK);
	}

	mutex_unlock(&dev_priv->sb_lock);
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
static void lpt_bend_clkout_dp(struct drm_i915_private *dev_priv, int steps)
{
	u32 tmp;
	int idx = BEND_IDX(steps);

	if (drm_WARN_ON(&dev_priv->drm, steps % 5 != 0))
		return;

	if (drm_WARN_ON(&dev_priv->drm, idx >= ARRAY_SIZE(sscdivintphase)))
		return;

	mutex_lock(&dev_priv->sb_lock);

	if (steps % 10 != 0)
		tmp = 0xAAAAAAAB;
	else
		tmp = 0x00000000;
	intel_sbi_write(dev_priv, SBI_SSCDITHPHASE, tmp, SBI_ICLK);

	tmp = intel_sbi_read(dev_priv, SBI_SSCDIVINTPHASE, SBI_ICLK);
	tmp &= 0xffff0000;
	tmp |= sscdivintphase[idx];
	intel_sbi_write(dev_priv, SBI_SSCDIVINTPHASE, tmp, SBI_ICLK);

	mutex_unlock(&dev_priv->sb_lock);
}

#undef BEND_IDX

static bool spll_uses_pch_ssc(struct drm_i915_private *dev_priv)
{
	u32 fuse_strap = intel_de_read(dev_priv, FUSE_STRAP);
	u32 ctl = intel_de_read(dev_priv, SPLL_CTL);

	if ((ctl & SPLL_PLL_ENABLE) == 0)
		return false;

	if ((ctl & SPLL_REF_MASK) == SPLL_REF_MUXED_SSC &&
	    (fuse_strap & HSW_CPU_SSC_ENABLE) == 0)
		return true;

	if (IS_BROADWELL(dev_priv) &&
	    (ctl & SPLL_REF_MASK) == SPLL_REF_PCH_SSC_BDW)
		return true;

	return false;
}

static bool wrpll_uses_pch_ssc(struct drm_i915_private *dev_priv,
			       enum intel_dpll_id id)
{
	u32 fuse_strap = intel_de_read(dev_priv, FUSE_STRAP);
	u32 ctl = intel_de_read(dev_priv, WRPLL_CTL(id));

	if ((ctl & WRPLL_PLL_ENABLE) == 0)
		return false;

	if ((ctl & WRPLL_REF_MASK) == WRPLL_REF_PCH_SSC)
		return true;

	if ((IS_BROADWELL(dev_priv) || IS_HASWELL_ULT(dev_priv)) &&
	    (ctl & WRPLL_REF_MASK) == WRPLL_REF_MUXED_SSC_BDW &&
	    (fuse_strap & HSW_CPU_SSC_ENABLE) == 0)
		return true;

	return false;
}

static void lpt_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	bool has_fdi = false;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
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
	dev_priv->display.dpll.pch_ssc_use = 0;

	if (spll_uses_pch_ssc(dev_priv)) {
		drm_dbg_kms(&dev_priv->drm, "SPLL using PCH SSC\n");
		dev_priv->display.dpll.pch_ssc_use |= BIT(DPLL_ID_SPLL);
	}

	if (wrpll_uses_pch_ssc(dev_priv, DPLL_ID_WRPLL1)) {
		drm_dbg_kms(&dev_priv->drm, "WRPLL1 using PCH SSC\n");
		dev_priv->display.dpll.pch_ssc_use |= BIT(DPLL_ID_WRPLL1);
	}

	if (wrpll_uses_pch_ssc(dev_priv, DPLL_ID_WRPLL2)) {
		drm_dbg_kms(&dev_priv->drm, "WRPLL2 using PCH SSC\n");
		dev_priv->display.dpll.pch_ssc_use |= BIT(DPLL_ID_WRPLL2);
	}

	if (dev_priv->display.dpll.pch_ssc_use)
		return;

	if (has_fdi) {
		lpt_bend_clkout_dp(dev_priv, 0);
		lpt_enable_clkout_dp(dev_priv, true, true);
	} else {
		lpt_disable_clkout_dp(dev_priv);
	}
}

static void ilk_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	int i;
	u32 val, final;
	bool has_lvds = false;
	bool has_cpu_edp = false;
	bool has_panel = false;
	bool has_ck505 = false;
	bool can_ssc = false;
	bool using_ssc_source = false;

	/* We need to take the global config into account */
	for_each_intel_encoder(&dev_priv->drm, encoder) {
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

	if (HAS_PCH_IBX(dev_priv)) {
		has_ck505 = dev_priv->display.vbt.display_clock_mode;
		can_ssc = has_ck505;
	} else {
		has_ck505 = false;
		can_ssc = true;
	}

	/* Check if any DPLLs are using the SSC source */
	for (i = 0; i < dev_priv->display.dpll.num_shared_dpll; i++) {
		u32 temp = intel_de_read(dev_priv, PCH_DPLL(i));

		if (!(temp & DPLL_VCO_ENABLE))
			continue;

		if ((temp & PLL_REF_INPUT_MASK) ==
		    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
			using_ssc_source = true;
			break;
		}
	}

	drm_dbg_kms(&dev_priv->drm,
		    "has_panel %d has_lvds %d has_ck505 %d using_ssc_source %d\n",
		    has_panel, has_lvds, has_ck505, using_ssc_source);

	/* Ironlake: try to setup display ref clock before DPLL
	 * enabling. This is only under driver's control after
	 * PCH B stepping, previous chipset stepping should be
	 * ignoring this setting.
	 */
	val = intel_de_read(dev_priv, PCH_DREF_CONTROL);

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

		if (intel_panel_use_ssc(dev_priv) && can_ssc)
			final |= DREF_SSC1_ENABLE;

		if (has_cpu_edp) {
			if (intel_panel_use_ssc(dev_priv) && can_ssc)
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
		if (intel_panel_use_ssc(dev_priv) && can_ssc) {
			drm_dbg_kms(&dev_priv->drm, "Using SSC on panel\n");
			val |= DREF_SSC1_ENABLE;
		} else {
			val &= ~DREF_SSC1_ENABLE;
		}

		/* Get SSC going before enabling the outputs */
		intel_de_write(dev_priv, PCH_DREF_CONTROL, val);
		intel_de_posting_read(dev_priv, PCH_DREF_CONTROL);
		udelay(200);

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Enable CPU source on CPU attached eDP */
		if (has_cpu_edp) {
			if (intel_panel_use_ssc(dev_priv) && can_ssc) {
				drm_dbg_kms(&dev_priv->drm,
					    "Using SSC on eDP\n");
				val |= DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD;
			} else {
				val |= DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
			}
		} else {
			val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
		}

		intel_de_write(dev_priv, PCH_DREF_CONTROL, val);
		intel_de_posting_read(dev_priv, PCH_DREF_CONTROL);
		udelay(200);
	} else {
		drm_dbg_kms(&dev_priv->drm, "Disabling CPU source output\n");

		val &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		/* Turn off CPU output */
		val |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		intel_de_write(dev_priv, PCH_DREF_CONTROL, val);
		intel_de_posting_read(dev_priv, PCH_DREF_CONTROL);
		udelay(200);

		if (!using_ssc_source) {
			drm_dbg_kms(&dev_priv->drm, "Disabling SSC source\n");

			/* Turn off the SSC source */
			val &= ~DREF_SSC_SOURCE_MASK;
			val |= DREF_SSC_SOURCE_DISABLE;

			/* Turn off SSC1 */
			val &= ~DREF_SSC1_ENABLE;

			intel_de_write(dev_priv, PCH_DREF_CONTROL, val);
			intel_de_posting_read(dev_priv, PCH_DREF_CONTROL);
			udelay(200);
		}
	}

	drm_WARN_ON(&dev_priv->drm, val != final);
}

/*
 * Initialize reference clocks when the driver loads
 */
void intel_init_pch_refclk(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv))
		ilk_init_pch_refclk(dev_priv);
	else if (HAS_PCH_LPT(dev_priv))
		lpt_init_pch_refclk(dev_priv);
}
