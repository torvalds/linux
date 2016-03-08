/*
 * Copyright © 2006-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "intel_drv.h"

struct intel_shared_dpll *
intel_crtc_to_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;

	if (crtc->config->shared_dpll < 0)
		return NULL;

	return &dev_priv->shared_dplls[crtc->config->shared_dpll];
}

/* For ILK+ */
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state)
{
	bool cur_state;
	struct intel_dpll_hw_state hw_state;

	if (WARN(!pll, "asserting DPLL %s with no DPLL\n", onoff(state)))
		return;

	cur_state = pll->get_hw_state(dev_priv, pll, &hw_state);
	I915_STATE_WARN(cur_state != state,
	     "%s assertion failure (expected %s, current %s)\n",
			pll->name, onoff(state), onoff(cur_state));
}

void intel_prepare_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	if (WARN_ON(pll == NULL))
		return;

	WARN_ON(!pll->config.crtc_mask);
	if (pll->active == 0) {
		DRM_DEBUG_DRIVER("setting up %s\n", pll->name);
		WARN_ON(pll->on);
		assert_shared_dpll_disabled(dev_priv, pll);

		pll->mode_set(dev_priv, pll);
	}
}

/**
 * intel_enable_shared_dpll - enable PCH PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 *
 * The PCH PLL needs to be enabled before the PCH transcoder, since it
 * drives the transcoder clock.
 */
void intel_enable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	if (WARN_ON(pll == NULL))
		return;

	if (WARN_ON(pll->config.crtc_mask == 0))
		return;

	DRM_DEBUG_KMS("enable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (pll->active++) {
		WARN_ON(!pll->on);
		assert_shared_dpll_enabled(dev_priv, pll);
		return;
	}
	WARN_ON(pll->on);

	intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);

	DRM_DEBUG_KMS("enabling %s\n", pll->name);
	pll->enable(dev_priv, pll);
	pll->on = true;
}

void intel_disable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = intel_crtc_to_shared_dpll(crtc);

	/* PCH only available on ILK+ */
	if (INTEL_INFO(dev)->gen < 5)
		return;

	if (pll == NULL)
		return;

	if (WARN_ON(!(pll->config.crtc_mask & (1 << drm_crtc_index(&crtc->base)))))
		return;

	DRM_DEBUG_KMS("disable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (WARN_ON(pll->active == 0)) {
		assert_shared_dpll_disabled(dev_priv, pll);
		return;
	}

	assert_shared_dpll_enabled(dev_priv, pll);
	WARN_ON(!pll->on);
	if (--pll->active)
		return;

	DRM_DEBUG_KMS("disabling %s\n", pll->name);
	pll->disable(dev_priv, pll);
	pll->on = false;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);
}

static enum intel_dpll_id
ibx_get_fixed_dpll(struct intel_crtc *crtc,
		   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	/* Ironlake PCH has a fixed PLL->PCH pipe mapping. */
	i = (enum intel_dpll_id) crtc->pipe;
	pll = &dev_priv->shared_dplls[i];

	DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
		      crtc->base.base.id, pll->name);

	return i;
}

static enum intel_dpll_id
bxt_get_fixed_dpll(struct intel_crtc *crtc,
		   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_encoder *encoder;
	struct intel_digital_port *intel_dig_port;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	/* PLL is attached to port in bxt */
	encoder = intel_ddi_get_crtc_new_encoder(crtc_state);
	if (WARN_ON(!encoder))
		return DPLL_ID_PRIVATE;

	intel_dig_port = enc_to_dig_port(&encoder->base);
	/* 1:1 mapping between ports and PLLs */
	i = (enum intel_dpll_id)intel_dig_port->port;
	pll = &dev_priv->shared_dplls[i];
	DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
		crtc->base.base.id, pll->name);

	return i;
}

static enum intel_dpll_id
intel_find_shared_dpll(struct intel_crtc *crtc,
		       struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	struct intel_shared_dpll *pll;
	struct intel_shared_dpll_config *shared_dpll;
	enum intel_dpll_id i;
	int max = dev_priv->num_shared_dpll;

	if (INTEL_INFO(dev_priv)->gen < 9 && HAS_DDI(dev_priv))
		/* Do not consider SPLL */
		max = 2;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	for (i = 0; i < max; i++) {
		pll = &dev_priv->shared_dplls[i];

		/* Only want to check enabled timings first */
		if (shared_dpll[i].crtc_mask == 0)
			continue;

		if (memcmp(&crtc_state->dpll_hw_state,
			   &shared_dpll[i].hw_state,
			   sizeof(crtc_state->dpll_hw_state)) == 0) {
			DRM_DEBUG_KMS("CRTC:%d sharing existing %s (crtc mask 0x%08x, ative %d)\n",
				      crtc->base.base.id, pll->name,
				      shared_dpll[i].crtc_mask,
				      pll->active);
			return i;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		if (shared_dpll[i].crtc_mask == 0) {
			DRM_DEBUG_KMS("CRTC:%d allocated %s\n",
				      crtc->base.base.id, pll->name);
			return i;
		}
	}

	return DPLL_ID_PRIVATE;
}

struct intel_shared_dpll *
intel_get_shared_dpll(struct intel_crtc *crtc,
		      struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	struct intel_shared_dpll *pll;
	struct intel_shared_dpll_config *shared_dpll;
	enum intel_dpll_id i;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	if (HAS_PCH_IBX(dev_priv->dev)) {
		i = ibx_get_fixed_dpll(crtc, crtc_state);
		WARN_ON(shared_dpll[i].crtc_mask);
	} else if (IS_BROXTON(dev_priv->dev)) {
		i = bxt_get_fixed_dpll(crtc, crtc_state);
		WARN_ON(shared_dpll[i].crtc_mask);
	} else {
		i = intel_find_shared_dpll(crtc, crtc_state);
	}

	if (i < 0)
		return NULL;

	pll = &dev_priv->shared_dplls[i];

	if (shared_dpll[i].crtc_mask == 0)
		shared_dpll[i].hw_state =
			crtc_state->dpll_hw_state;

	crtc_state->shared_dpll = i;
	DRM_DEBUG_DRIVER("using %s for pipe %c\n", pll->name,
			 pipe_name(crtc->pipe));

	shared_dpll[i].crtc_mask |= 1 << crtc->pipe;

	return pll;
}

void intel_shared_dpll_commit(struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct intel_shared_dpll_config *shared_dpll;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (!to_intel_atomic_state(state)->dpll_set)
		return;

	shared_dpll = to_intel_atomic_state(state)->shared_dpll;
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		pll->config = shared_dpll[i];
	}
}

static bool ibx_pch_dpll_get_hw_state(struct drm_i915_private *dev_priv,
				      struct intel_shared_dpll *pll,
				      struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(PCH_DPLL(pll->id));
	hw_state->dpll = val;
	hw_state->fp0 = I915_READ(PCH_FP0(pll->id));
	hw_state->fp1 = I915_READ(PCH_FP1(pll->id));

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & DPLL_VCO_ENABLE;
}

static void ibx_pch_dpll_mode_set(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
	I915_WRITE(PCH_FP0(pll->id), pll->config.hw_state.fp0);
	I915_WRITE(PCH_FP1(pll->id), pll->config.hw_state.fp1);
}

static void ibx_assert_pch_refclk_enabled(struct drm_i915_private *dev_priv)
{
	u32 val;
	bool enabled;

	I915_STATE_WARN_ON(!(HAS_PCH_IBX(dev_priv->dev) || HAS_PCH_CPT(dev_priv->dev)));

	val = I915_READ(PCH_DREF_CONTROL);
	enabled = !!(val & (DREF_SSC_SOURCE_MASK | DREF_NONSPREAD_SOURCE_MASK |
			    DREF_SUPERSPREAD_SOURCE_MASK));
	I915_STATE_WARN(!enabled, "PCH refclk assertion failure, should be active but is disabled\n");
}

static void ibx_pch_dpll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	/* PCH refclock must be enabled first */
	ibx_assert_pch_refclk_enabled(dev_priv);

	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(150);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static void ibx_pch_dpll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_crtc *crtc;

	/* Make sure no transcoder isn't still depending on us. */
	for_each_intel_crtc(dev, crtc) {
		if (intel_crtc_to_shared_dpll(crtc) == pll)
			assert_pch_transcoder_disabled(dev_priv, crtc->pipe);
	}

	I915_WRITE(PCH_DPLL(pll->id), 0);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static char *ibx_pch_dpll_names[] = {
	"PCH DPLL A",
	"PCH DPLL B",
};

static void ibx_pch_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	dev_priv->num_shared_dpll = 2;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = ibx_pch_dpll_names[i];
		dev_priv->shared_dplls[i].mode_set = ibx_pch_dpll_mode_set;
		dev_priv->shared_dplls[i].enable = ibx_pch_dpll_enable;
		dev_priv->shared_dplls[i].disable = ibx_pch_dpll_disable;
		dev_priv->shared_dplls[i].get_hw_state =
			ibx_pch_dpll_get_hw_state;
	}
}

static void hsw_ddi_wrpll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	I915_WRITE(WRPLL_CTL(pll->id), pll->config.hw_state.wrpll);
	POSTING_READ(WRPLL_CTL(pll->id));
	udelay(20);
}

static void hsw_ddi_spll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	I915_WRITE(SPLL_CTL, pll->config.hw_state.spll);
	POSTING_READ(SPLL_CTL);
	udelay(20);
}

static void hsw_ddi_wrpll_disable(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(WRPLL_CTL(pll->id));
	I915_WRITE(WRPLL_CTL(pll->id), val & ~WRPLL_PLL_ENABLE);
	POSTING_READ(WRPLL_CTL(pll->id));
}

static void hsw_ddi_spll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(SPLL_CTL);
	I915_WRITE(SPLL_CTL, val & ~SPLL_PLL_ENABLE);
	POSTING_READ(SPLL_CTL);
}

static bool hsw_ddi_wrpll_get_hw_state(struct drm_i915_private *dev_priv,
				       struct intel_shared_dpll *pll,
				       struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(WRPLL_CTL(pll->id));
	hw_state->wrpll = val;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & WRPLL_PLL_ENABLE;
}

static bool hsw_ddi_spll_get_hw_state(struct drm_i915_private *dev_priv,
				      struct intel_shared_dpll *pll,
				      struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(SPLL_CTL);
	hw_state->spll = val;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & SPLL_PLL_ENABLE;
}


static const char * const hsw_ddi_pll_names[] = {
	"WRPLL 1",
	"WRPLL 2",
	"SPLL"
};

static void hsw_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 3;

	for (i = 0; i < 2; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = hsw_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = hsw_ddi_wrpll_disable;
		dev_priv->shared_dplls[i].enable = hsw_ddi_wrpll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			hsw_ddi_wrpll_get_hw_state;
	}

	/* SPLL is special, but needs to be initialized anyway.. */
	dev_priv->shared_dplls[i].id = i;
	dev_priv->shared_dplls[i].name = hsw_ddi_pll_names[i];
	dev_priv->shared_dplls[i].disable = hsw_ddi_spll_disable;
	dev_priv->shared_dplls[i].enable = hsw_ddi_spll_enable;
	dev_priv->shared_dplls[i].get_hw_state = hsw_ddi_spll_get_hw_state;

}

static const char * const skl_ddi_pll_names[] = {
	"DPLL 1",
	"DPLL 2",
	"DPLL 3",
};

struct skl_dpll_regs {
	i915_reg_t ctl, cfgcr1, cfgcr2;
};

/* this array is indexed by the *shared* pll id */
static const struct skl_dpll_regs skl_dpll_regs[3] = {
	{
		/* DPLL 1 */
		.ctl = LCPLL2_CTL,
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL1),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL1),
	},
	{
		/* DPLL 2 */
		.ctl = WRPLL_CTL(0),
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL2),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL2),
	},
	{
		/* DPLL 3 */
		.ctl = WRPLL_CTL(1),
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL3),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL3),
	},
};

static void skl_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(DPLL_CTRL1);

	val &= ~(DPLL_CTRL1_HDMI_MODE(dpll) | DPLL_CTRL1_SSC(dpll) |
		 DPLL_CTRL1_LINK_RATE_MASK(dpll));
	val |= pll->config.hw_state.ctrl1 << (dpll * 6);

	I915_WRITE(DPLL_CTRL1, val);
	POSTING_READ(DPLL_CTRL1);

	I915_WRITE(regs[pll->id].cfgcr1, pll->config.hw_state.cfgcr1);
	I915_WRITE(regs[pll->id].cfgcr2, pll->config.hw_state.cfgcr2);
	POSTING_READ(regs[pll->id].cfgcr1);
	POSTING_READ(regs[pll->id].cfgcr2);

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) | LCPLL_PLL_ENABLE);

	if (wait_for(I915_READ(DPLL_STATUS) & DPLL_LOCK(dpll), 5))
		DRM_ERROR("DPLL %d not locked\n", dpll);
}

static void skl_ddi_pll_disable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) & ~LCPLL_PLL_ENABLE);
	POSTING_READ(regs[pll->id].ctl);
}

static bool skl_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(regs[pll->id].ctl);
	if (!(val & LCPLL_PLL_ENABLE))
		goto out;

	val = I915_READ(DPLL_CTRL1);
	hw_state->ctrl1 = (val >> (dpll * 6)) & 0x3f;

	/* avoid reading back stale values if HDMI mode is not enabled */
	if (val & DPLL_CTRL1_HDMI_MODE(dpll)) {
		hw_state->cfgcr1 = I915_READ(regs[pll->id].cfgcr1);
		hw_state->cfgcr2 = I915_READ(regs[pll->id].cfgcr2);
	}
	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static void skl_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 3;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = skl_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = skl_ddi_pll_disable;
		dev_priv->shared_dplls[i].enable = skl_ddi_pll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			skl_ddi_pll_get_hw_state;
	}
}

static const char * const bxt_ddi_pll_names[] = {
	"PORT PLL A",
	"PORT PLL B",
	"PORT PLL C",
};

static void bxt_ddi_pll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t temp;
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_REF_SEL;
	/* Non-SSC reference */
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

	/* Disable 10 bit clock */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Write P1 & P2 */
	temp = I915_READ(BXT_PORT_PLL_EBB_0(port));
	temp &= ~(PORT_PLL_P1_MASK | PORT_PLL_P2_MASK);
	temp |= pll->config.hw_state.ebb0;
	I915_WRITE(BXT_PORT_PLL_EBB_0(port), temp);

	/* Write M2 integer */
	temp = I915_READ(BXT_PORT_PLL(port, 0));
	temp &= ~PORT_PLL_M2_MASK;
	temp |= pll->config.hw_state.pll0;
	I915_WRITE(BXT_PORT_PLL(port, 0), temp);

	/* Write N */
	temp = I915_READ(BXT_PORT_PLL(port, 1));
	temp &= ~PORT_PLL_N_MASK;
	temp |= pll->config.hw_state.pll1;
	I915_WRITE(BXT_PORT_PLL(port, 1), temp);

	/* Write M2 fraction */
	temp = I915_READ(BXT_PORT_PLL(port, 2));
	temp &= ~PORT_PLL_M2_FRAC_MASK;
	temp |= pll->config.hw_state.pll2;
	I915_WRITE(BXT_PORT_PLL(port, 2), temp);

	/* Write M2 fraction enable */
	temp = I915_READ(BXT_PORT_PLL(port, 3));
	temp &= ~PORT_PLL_M2_FRAC_ENABLE;
	temp |= pll->config.hw_state.pll3;
	I915_WRITE(BXT_PORT_PLL(port, 3), temp);

	/* Write coeff */
	temp = I915_READ(BXT_PORT_PLL(port, 6));
	temp &= ~PORT_PLL_PROP_COEFF_MASK;
	temp &= ~PORT_PLL_INT_COEFF_MASK;
	temp &= ~PORT_PLL_GAIN_CTL_MASK;
	temp |= pll->config.hw_state.pll6;
	I915_WRITE(BXT_PORT_PLL(port, 6), temp);

	/* Write calibration val */
	temp = I915_READ(BXT_PORT_PLL(port, 8));
	temp &= ~PORT_PLL_TARGET_CNT_MASK;
	temp |= pll->config.hw_state.pll8;
	I915_WRITE(BXT_PORT_PLL(port, 8), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 9));
	temp &= ~PORT_PLL_LOCK_THRESHOLD_MASK;
	temp |= pll->config.hw_state.pll9;
	I915_WRITE(BXT_PORT_PLL(port, 9), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 10));
	temp &= ~PORT_PLL_DCO_AMP_OVR_EN_H;
	temp &= ~PORT_PLL_DCO_AMP_MASK;
	temp |= pll->config.hw_state.pll10;
	I915_WRITE(BXT_PORT_PLL(port, 10), temp);

	/* Recalibrate with new settings */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp |= PORT_PLL_RECALIBRATE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	temp |= pll->config.hw_state.ebb4;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Enable PLL */
	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp |= PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));

	if (wait_for_atomic_us((I915_READ(BXT_PORT_PLL_ENABLE(port)) &
			PORT_PLL_LOCK), 200))
		DRM_ERROR("PLL %d not locked\n", port);

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	temp = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	temp &= ~LANE_STAGGER_MASK;
	temp &= ~LANESTAGGER_STRAP_OVRD;
	temp |= pll->config.hw_state.pcsdw12;
	I915_WRITE(BXT_PORT_PCS_DW12_GRP(port), temp);
}

static void bxt_ddi_pll_disable(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t temp;

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));
}

static bool bxt_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll,
					struct intel_dpll_hw_state *hw_state)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t val;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	val = I915_READ(BXT_PORT_PLL_ENABLE(port));
	if (!(val & PORT_PLL_ENABLE))
		goto out;

	hw_state->ebb0 = I915_READ(BXT_PORT_PLL_EBB_0(port));
	hw_state->ebb0 &= PORT_PLL_P1_MASK | PORT_PLL_P2_MASK;

	hw_state->ebb4 = I915_READ(BXT_PORT_PLL_EBB_4(port));
	hw_state->ebb4 &= PORT_PLL_10BIT_CLK_ENABLE;

	hw_state->pll0 = I915_READ(BXT_PORT_PLL(port, 0));
	hw_state->pll0 &= PORT_PLL_M2_MASK;

	hw_state->pll1 = I915_READ(BXT_PORT_PLL(port, 1));
	hw_state->pll1 &= PORT_PLL_N_MASK;

	hw_state->pll2 = I915_READ(BXT_PORT_PLL(port, 2));
	hw_state->pll2 &= PORT_PLL_M2_FRAC_MASK;

	hw_state->pll3 = I915_READ(BXT_PORT_PLL(port, 3));
	hw_state->pll3 &= PORT_PLL_M2_FRAC_ENABLE;

	hw_state->pll6 = I915_READ(BXT_PORT_PLL(port, 6));
	hw_state->pll6 &= PORT_PLL_PROP_COEFF_MASK |
			  PORT_PLL_INT_COEFF_MASK |
			  PORT_PLL_GAIN_CTL_MASK;

	hw_state->pll8 = I915_READ(BXT_PORT_PLL(port, 8));
	hw_state->pll8 &= PORT_PLL_TARGET_CNT_MASK;

	hw_state->pll9 = I915_READ(BXT_PORT_PLL(port, 9));
	hw_state->pll9 &= PORT_PLL_LOCK_THRESHOLD_MASK;

	hw_state->pll10 = I915_READ(BXT_PORT_PLL(port, 10));
	hw_state->pll10 &= PORT_PLL_DCO_AMP_OVR_EN_H |
			   PORT_PLL_DCO_AMP_MASK;

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers. We configure all lanes the same way, so
	 * here just read out lanes 0/1 and output a note if lanes 2/3 differ.
	 */
	hw_state->pcsdw12 = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	if (I915_READ(BXT_PORT_PCS_DW12_LN23(port)) != hw_state->pcsdw12)
		DRM_DEBUG_DRIVER("lane stagger config different for lane 01 (%08x) and 23 (%08x)\n",
				 hw_state->pcsdw12,
				 I915_READ(BXT_PORT_PCS_DW12_LN23(port)));
	hw_state->pcsdw12 &= LANE_STAGGER_MASK | LANESTAGGER_STRAP_OVRD;

	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static void bxt_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 3;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = bxt_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = bxt_ddi_pll_disable;
		dev_priv->shared_dplls[i].enable = bxt_ddi_pll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			bxt_ddi_pll_get_hw_state;
	}
}

static void intel_ddi_pll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val = I915_READ(LCPLL_CTL);

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		skl_shared_dplls_init(dev_priv);
	else if (IS_BROXTON(dev))
		bxt_shared_dplls_init(dev_priv);
	else
		hsw_shared_dplls_init(dev_priv);

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) {
		int cdclk_freq;

		cdclk_freq = dev_priv->display.get_display_clock_speed(dev);
		dev_priv->skl_boot_cdclk = cdclk_freq;
		if (skl_sanitize_cdclk(dev_priv))
			DRM_DEBUG_KMS("Sanitized cdclk programmed by pre-os\n");
		if (!(I915_READ(LCPLL1_CTL) & LCPLL_PLL_ENABLE))
			DRM_ERROR("LCPLL1 is disabled\n");
	} else if (IS_BROXTON(dev)) {
		broxton_init_cdclk(dev);
		broxton_ddi_phy_init(dev);
	} else {
		/*
		 * The LCPLL register should be turned on by the BIOS. For now
		 * let's just check its state and print errors in case
		 * something is wrong.  Don't even try to turn it on.
		 */

		if (val & LCPLL_CD_SOURCE_FCLK)
			DRM_ERROR("CDCLK source is not LCPLL\n");

		if (val & LCPLL_PLL_DISABLE)
			DRM_ERROR("LCPLL is disabled\n");
	}
}

void intel_shared_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_DDI(dev))
		intel_ddi_pll_init(dev);
	else if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		ibx_pch_dpll_init(dev);
	else
		dev_priv->num_shared_dpll = 0;

	BUG_ON(dev_priv->num_shared_dpll > I915_NUM_PLLS);
}
