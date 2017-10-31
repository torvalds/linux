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

/**
 * DOC: Display PLLs
 *
 * Display PLLs used for driving outputs vary by platform. While some have
 * per-pipe or per-encoder dedicated PLLs, others allow the use of any PLL
 * from a pool. In the latter scenario, it is possible that multiple pipes
 * share a PLL if their configurations match.
 *
 * This file provides an abstraction over display PLLs. The function
 * intel_shared_dpll_init() initializes the PLLs for the given platform.  The
 * users of a PLL are tracked and that tracking is integrated with the atomic
 * modest interface. During an atomic operation, a PLL can be requested for a
 * given CRTC and encoder configuration by calling intel_get_shared_dpll() and
 * a previously used PLL can be released with intel_release_shared_dpll().
 * Changes to the users are first staged in the atomic state, and then made
 * effective by calling intel_shared_dpll_swap_state() during the atomic
 * commit phase.
 */

static void
intel_atomic_duplicate_dpll_state(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll_state *shared_dpll)
{
	enum intel_dpll_id i;

	/* Copy shared dpll state */
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		shared_dpll[i] = pll->state;
	}
}

static struct intel_shared_dpll_state *
intel_atomic_get_shared_dpll_state(struct drm_atomic_state *s)
{
	struct intel_atomic_state *state = to_intel_atomic_state(s);

	WARN_ON(!drm_modeset_is_locked(&s->dev->mode_config.connection_mutex));

	if (!state->dpll_set) {
		state->dpll_set = true;

		intel_atomic_duplicate_dpll_state(to_i915(s->dev),
						  state->shared_dpll);
	}

	return state->shared_dpll;
}

/**
 * intel_get_shared_dpll_by_id - get a DPLL given its id
 * @dev_priv: i915 device instance
 * @id: pll id
 *
 * Returns:
 * A pointer to the DPLL with @id
 */
struct intel_shared_dpll *
intel_get_shared_dpll_by_id(struct drm_i915_private *dev_priv,
			    enum intel_dpll_id id)
{
	return &dev_priv->shared_dplls[id];
}

/**
 * intel_get_shared_dpll_id - get the id of a DPLL
 * @dev_priv: i915 device instance
 * @pll: the DPLL
 *
 * Returns:
 * The id of @pll
 */
enum intel_dpll_id
intel_get_shared_dpll_id(struct drm_i915_private *dev_priv,
			 struct intel_shared_dpll *pll)
{
	if (WARN_ON(pll < dev_priv->shared_dplls||
		    pll > &dev_priv->shared_dplls[dev_priv->num_shared_dpll]))
		return -1;

	return (enum intel_dpll_id) (pll - dev_priv->shared_dplls);
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

	cur_state = pll->funcs.get_hw_state(dev_priv, pll, &hw_state);
	I915_STATE_WARN(cur_state != state,
	     "%s assertion failure (expected %s, current %s)\n",
			pll->name, onoff(state), onoff(cur_state));
}

/**
 * intel_prepare_shared_dpll - call a dpll's prepare hook
 * @crtc: CRTC which has a shared dpll
 *
 * This calls the PLL's prepare hook if it has one and if the PLL is not
 * already enabled. The prepare hook is platform specific.
 */
void intel_prepare_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;

	if (WARN_ON(pll == NULL))
		return;

	mutex_lock(&dev_priv->dpll_lock);
	WARN_ON(!pll->state.crtc_mask);
	if (!pll->active_mask) {
		DRM_DEBUG_DRIVER("setting up %s\n", pll->name);
		WARN_ON(pll->on);
		assert_shared_dpll_disabled(dev_priv, pll);

		pll->funcs.prepare(dev_priv, pll);
	}
	mutex_unlock(&dev_priv->dpll_lock);
}

/**
 * intel_enable_shared_dpll - enable a CRTC's shared DPLL
 * @crtc: CRTC which has a shared DPLL
 *
 * Enable the shared DPLL used by @crtc.
 */
void intel_enable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;
	unsigned crtc_mask = 1 << drm_crtc_index(&crtc->base);
	unsigned old_mask;

	if (WARN_ON(pll == NULL))
		return;

	mutex_lock(&dev_priv->dpll_lock);
	old_mask = pll->active_mask;

	if (WARN_ON(!(pll->state.crtc_mask & crtc_mask)) ||
	    WARN_ON(pll->active_mask & crtc_mask))
		goto out;

	pll->active_mask |= crtc_mask;

	DRM_DEBUG_KMS("enable %s (active %x, on? %d) for crtc %d\n",
		      pll->name, pll->active_mask, pll->on,
		      crtc->base.base.id);

	if (old_mask) {
		WARN_ON(!pll->on);
		assert_shared_dpll_enabled(dev_priv, pll);
		goto out;
	}
	WARN_ON(pll->on);

	DRM_DEBUG_KMS("enabling %s\n", pll->name);
	pll->funcs.enable(dev_priv, pll);
	pll->on = true;

out:
	mutex_unlock(&dev_priv->dpll_lock);
}

/**
 * intel_disable_shared_dpll - disable a CRTC's shared DPLL
 * @crtc: CRTC which has a shared DPLL
 *
 * Disable the shared DPLL used by @crtc.
 */
void intel_disable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;
	unsigned crtc_mask = 1 << drm_crtc_index(&crtc->base);

	/* PCH only available on ILK+ */
	if (INTEL_GEN(dev_priv) < 5)
		return;

	if (pll == NULL)
		return;

	mutex_lock(&dev_priv->dpll_lock);
	if (WARN_ON(!(pll->active_mask & crtc_mask)))
		goto out;

	DRM_DEBUG_KMS("disable %s (active %x, on? %d) for crtc %d\n",
		      pll->name, pll->active_mask, pll->on,
		      crtc->base.base.id);

	assert_shared_dpll_enabled(dev_priv, pll);
	WARN_ON(!pll->on);

	pll->active_mask &= ~crtc_mask;
	if (pll->active_mask)
		goto out;

	DRM_DEBUG_KMS("disabling %s\n", pll->name);
	pll->funcs.disable(dev_priv, pll);
	pll->on = false;

out:
	mutex_unlock(&dev_priv->dpll_lock);
}

static struct intel_shared_dpll *
intel_find_shared_dpll(struct intel_crtc *crtc,
		       struct intel_crtc_state *crtc_state,
		       enum intel_dpll_id range_min,
		       enum intel_dpll_id range_max)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	struct intel_shared_dpll_state *shared_dpll;
	enum intel_dpll_id i;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	for (i = range_min; i <= range_max; i++) {
		pll = &dev_priv->shared_dplls[i];

		/* Only want to check enabled timings first */
		if (shared_dpll[i].crtc_mask == 0)
			continue;

		if (memcmp(&crtc_state->dpll_hw_state,
			   &shared_dpll[i].hw_state,
			   sizeof(crtc_state->dpll_hw_state)) == 0) {
			DRM_DEBUG_KMS("[CRTC:%d:%s] sharing existing %s (crtc mask 0x%08x, active %x)\n",
				      crtc->base.base.id, crtc->base.name, pll->name,
				      shared_dpll[i].crtc_mask,
				      pll->active_mask);
			return pll;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = range_min; i <= range_max; i++) {
		pll = &dev_priv->shared_dplls[i];
		if (shared_dpll[i].crtc_mask == 0) {
			DRM_DEBUG_KMS("[CRTC:%d:%s] allocated %s\n",
				      crtc->base.base.id, crtc->base.name, pll->name);
			return pll;
		}
	}

	return NULL;
}

static void
intel_reference_shared_dpll(struct intel_shared_dpll *pll,
			    struct intel_crtc_state *crtc_state)
{
	struct intel_shared_dpll_state *shared_dpll;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	enum intel_dpll_id i = pll->id;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	if (shared_dpll[i].crtc_mask == 0)
		shared_dpll[i].hw_state =
			crtc_state->dpll_hw_state;

	crtc_state->shared_dpll = pll;
	DRM_DEBUG_DRIVER("using %s for pipe %c\n", pll->name,
			 pipe_name(crtc->pipe));

	shared_dpll[pll->id].crtc_mask |= 1 << crtc->pipe;
}

/**
 * intel_shared_dpll_swap_state - make atomic DPLL configuration effective
 * @state: atomic state
 *
 * This is the dpll version of drm_atomic_helper_swap_state() since the
 * helper does not handle driver-specific global state.
 *
 * For consistency with atomic helpers this function does a complete swap,
 * i.e. it also puts the current state into @state, even though there is no
 * need for that at this moment.
 */
void intel_shared_dpll_swap_state(struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct intel_shared_dpll_state *shared_dpll;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (!to_intel_atomic_state(state)->dpll_set)
		return;

	shared_dpll = to_intel_atomic_state(state)->shared_dpll;
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll_state tmp;

		pll = &dev_priv->shared_dplls[i];

		tmp = pll->state;
		pll->state = shared_dpll[i];
		shared_dpll[i] = tmp;
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

static void ibx_pch_dpll_prepare(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	I915_WRITE(PCH_FP0(pll->id), pll->state.hw_state.fp0);
	I915_WRITE(PCH_FP1(pll->id), pll->state.hw_state.fp1);
}

static void ibx_assert_pch_refclk_enabled(struct drm_i915_private *dev_priv)
{
	u32 val;
	bool enabled;

	I915_STATE_WARN_ON(!(HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)));

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

	I915_WRITE(PCH_DPLL(pll->id), pll->state.hw_state.dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(150);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	I915_WRITE(PCH_DPLL(pll->id), pll->state.hw_state.dpll);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static void ibx_pch_dpll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;

	/* Make sure no transcoder isn't still depending on us. */
	for_each_intel_crtc(dev, crtc) {
		if (crtc->config->shared_dpll == pll)
			assert_pch_transcoder_disabled(dev_priv, crtc->pipe);
	}

	I915_WRITE(PCH_DPLL(pll->id), 0);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static struct intel_shared_dpll *
ibx_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (HAS_PCH_IBX(dev_priv)) {
		/* Ironlake PCH has a fixed PLL->PCH pipe mapping. */
		i = (enum intel_dpll_id) crtc->pipe;
		pll = &dev_priv->shared_dplls[i];

		DRM_DEBUG_KMS("[CRTC:%d:%s] using pre-allocated %s\n",
			      crtc->base.base.id, crtc->base.name, pll->name);
	} else {
		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_PCH_PLL_A,
					     DPLL_ID_PCH_PLL_B);
	}

	if (!pll)
		return NULL;

	/* reference the pll */
	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static void ibx_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	DRM_DEBUG_KMS("dpll_hw_state: dpll: 0x%x, dpll_md: 0x%x, "
		      "fp0: 0x%x, fp1: 0x%x\n",
		      hw_state->dpll,
		      hw_state->dpll_md,
		      hw_state->fp0,
		      hw_state->fp1);
}

static const struct intel_shared_dpll_funcs ibx_pch_dpll_funcs = {
	.prepare = ibx_pch_dpll_prepare,
	.enable = ibx_pch_dpll_enable,
	.disable = ibx_pch_dpll_disable,
	.get_hw_state = ibx_pch_dpll_get_hw_state,
};

static void hsw_ddi_wrpll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	I915_WRITE(WRPLL_CTL(pll->id), pll->state.hw_state.wrpll);
	POSTING_READ(WRPLL_CTL(pll->id));
	udelay(20);
}

static void hsw_ddi_spll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	I915_WRITE(SPLL_CTL, pll->state.hw_state.spll);
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

#define LC_FREQ 2700
#define LC_FREQ_2K U64_C(LC_FREQ * 2000)

#define P_MIN 2
#define P_MAX 64
#define P_INC 2

/* Constraints for PLL good behavior */
#define REF_MIN 48
#define REF_MAX 400
#define VCO_MIN 2400
#define VCO_MAX 4800

struct hsw_wrpll_rnp {
	unsigned p, n2, r2;
};

static unsigned hsw_wrpll_get_budget_for_freq(int clock)
{
	unsigned budget;

	switch (clock) {
	case 25175000:
	case 25200000:
	case 27000000:
	case 27027000:
	case 37762500:
	case 37800000:
	case 40500000:
	case 40541000:
	case 54000000:
	case 54054000:
	case 59341000:
	case 59400000:
	case 72000000:
	case 74176000:
	case 74250000:
	case 81000000:
	case 81081000:
	case 89012000:
	case 89100000:
	case 108000000:
	case 108108000:
	case 111264000:
	case 111375000:
	case 148352000:
	case 148500000:
	case 162000000:
	case 162162000:
	case 222525000:
	case 222750000:
	case 296703000:
	case 297000000:
		budget = 0;
		break;
	case 233500000:
	case 245250000:
	case 247750000:
	case 253250000:
	case 298000000:
		budget = 1500;
		break;
	case 169128000:
	case 169500000:
	case 179500000:
	case 202000000:
		budget = 2000;
		break;
	case 256250000:
	case 262500000:
	case 270000000:
	case 272500000:
	case 273750000:
	case 280750000:
	case 281250000:
	case 286000000:
	case 291750000:
		budget = 4000;
		break;
	case 267250000:
	case 268500000:
		budget = 5000;
		break;
	default:
		budget = 1000;
		break;
	}

	return budget;
}

static void hsw_wrpll_update_rnp(uint64_t freq2k, unsigned budget,
				 unsigned r2, unsigned n2, unsigned p,
				 struct hsw_wrpll_rnp *best)
{
	uint64_t a, b, c, d, diff, diff_best;

	/* No best (r,n,p) yet */
	if (best->p == 0) {
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
		return;
	}

	/*
	 * Output clock is (LC_FREQ_2K / 2000) * N / (P * R), which compares to
	 * freq2k.
	 *
	 * delta = 1e6 *
	 *	   abs(freq2k - (LC_FREQ_2K * n2/(p * r2))) /
	 *	   freq2k;
	 *
	 * and we would like delta <= budget.
	 *
	 * If the discrepancy is above the PPM-based budget, always prefer to
	 * improve upon the previous solution.  However, if you're within the
	 * budget, try to maximize Ref * VCO, that is N / (P * R^2).
	 */
	a = freq2k * budget * p * r2;
	b = freq2k * budget * best->p * best->r2;
	diff = abs_diff(freq2k * p * r2, LC_FREQ_2K * n2);
	diff_best = abs_diff(freq2k * best->p * best->r2,
			     LC_FREQ_2K * best->n2);
	c = 1000000 * diff;
	d = 1000000 * diff_best;

	if (a < c && b < d) {
		/* If both are above the budget, pick the closer */
		if (best->p * best->r2 * diff < p * r2 * diff_best) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	} else if (a >= c && b < d) {
		/* If A is below the threshold but B is above it?  Update. */
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
	} else if (a >= c && b >= d) {
		/* Both are below the limit, so pick the higher n2/(r2*r2) */
		if (n2 * best->r2 * best->r2 > best->n2 * r2 * r2) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	}
	/* Otherwise a < c && b >= d, do nothing */
}

static void
hsw_ddi_calculate_wrpll(int clock /* in Hz */,
			unsigned *r2_out, unsigned *n2_out, unsigned *p_out)
{
	uint64_t freq2k;
	unsigned p, n2, r2;
	struct hsw_wrpll_rnp best = { 0, 0, 0 };
	unsigned budget;

	freq2k = clock / 100;

	budget = hsw_wrpll_get_budget_for_freq(clock);

	/* Special case handling for 540 pixel clock: bypass WR PLL entirely
	 * and directly pass the LC PLL to it. */
	if (freq2k == 5400000) {
		*n2_out = 2;
		*p_out = 1;
		*r2_out = 2;
		return;
	}

	/*
	 * Ref = LC_FREQ / R, where Ref is the actual reference input seen by
	 * the WR PLL.
	 *
	 * We want R so that REF_MIN <= Ref <= REF_MAX.
	 * Injecting R2 = 2 * R gives:
	 *   REF_MAX * r2 > LC_FREQ * 2 and
	 *   REF_MIN * r2 < LC_FREQ * 2
	 *
	 * Which means the desired boundaries for r2 are:
	 *  LC_FREQ * 2 / REF_MAX < r2 < LC_FREQ * 2 / REF_MIN
	 *
	 */
	for (r2 = LC_FREQ * 2 / REF_MAX + 1;
	     r2 <= LC_FREQ * 2 / REF_MIN;
	     r2++) {

		/*
		 * VCO = N * Ref, that is: VCO = N * LC_FREQ / R
		 *
		 * Once again we want VCO_MIN <= VCO <= VCO_MAX.
		 * Injecting R2 = 2 * R and N2 = 2 * N, we get:
		 *   VCO_MAX * r2 > n2 * LC_FREQ and
		 *   VCO_MIN * r2 < n2 * LC_FREQ)
		 *
		 * Which means the desired boundaries for n2 are:
		 * VCO_MIN * r2 / LC_FREQ < n2 < VCO_MAX * r2 / LC_FREQ
		 */
		for (n2 = VCO_MIN * r2 / LC_FREQ + 1;
		     n2 <= VCO_MAX * r2 / LC_FREQ;
		     n2++) {

			for (p = P_MIN; p <= P_MAX; p += P_INC)
				hsw_wrpll_update_rnp(freq2k, budget,
						     r2, n2, p, &best);
		}
	}

	*n2_out = best.n2;
	*p_out = best.p;
	*r2_out = best.r2;
}

static struct intel_shared_dpll *hsw_ddi_hdmi_get_dpll(int clock,
						       struct intel_crtc *crtc,
						       struct intel_crtc_state *crtc_state)
{
	struct intel_shared_dpll *pll;
	uint32_t val;
	unsigned int p, n2, r2;

	hsw_ddi_calculate_wrpll(clock * 1000, &r2, &n2, &p);

	val = WRPLL_PLL_ENABLE | WRPLL_PLL_LCPLL |
	      WRPLL_DIVIDER_REFERENCE(r2) | WRPLL_DIVIDER_FEEDBACK(n2) |
	      WRPLL_DIVIDER_POST(p);

	crtc_state->dpll_hw_state.wrpll = val;

	pll = intel_find_shared_dpll(crtc, crtc_state,
				     DPLL_ID_WRPLL1, DPLL_ID_WRPLL2);

	if (!pll)
		return NULL;

	return pll;
}

static struct intel_shared_dpll *
hsw_ddi_dp_get_dpll(struct intel_encoder *encoder, int clock)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_shared_dpll *pll;
	enum intel_dpll_id pll_id;

	switch (clock / 2) {
	case 81000:
		pll_id = DPLL_ID_LCPLL_810;
		break;
	case 135000:
		pll_id = DPLL_ID_LCPLL_1350;
		break;
	case 270000:
		pll_id = DPLL_ID_LCPLL_2700;
		break;
	default:
		DRM_DEBUG_KMS("Invalid clock for DP: %d\n", clock);
		return NULL;
	}

	pll = intel_get_shared_dpll_by_id(dev_priv, pll_id);

	if (!pll)
		return NULL;

	return pll;
}

static struct intel_shared_dpll *
hsw_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct intel_shared_dpll *pll;
	int clock = crtc_state->port_clock;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		pll = hsw_ddi_hdmi_get_dpll(clock, crtc, crtc_state);
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		pll = hsw_ddi_dp_get_dpll(encoder, clock);
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		if (WARN_ON(crtc_state->port_clock / 2 != 135000))
			return NULL;

		crtc_state->dpll_hw_state.spll =
			SPLL_PLL_ENABLE | SPLL_PLL_FREQ_1350MHz | SPLL_PLL_SSC;

		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_SPLL, DPLL_ID_SPLL);
	} else {
		return NULL;
	}

	if (!pll)
		return NULL;

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static void hsw_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	DRM_DEBUG_KMS("dpll_hw_state: wrpll: 0x%x spll: 0x%x\n",
		      hw_state->wrpll, hw_state->spll);
}

static const struct intel_shared_dpll_funcs hsw_ddi_wrpll_funcs = {
	.enable = hsw_ddi_wrpll_enable,
	.disable = hsw_ddi_wrpll_disable,
	.get_hw_state = hsw_ddi_wrpll_get_hw_state,
};

static const struct intel_shared_dpll_funcs hsw_ddi_spll_funcs = {
	.enable = hsw_ddi_spll_enable,
	.disable = hsw_ddi_spll_disable,
	.get_hw_state = hsw_ddi_spll_get_hw_state,
};

static void hsw_ddi_lcpll_enable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
}

static void hsw_ddi_lcpll_disable(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
}

static bool hsw_ddi_lcpll_get_hw_state(struct drm_i915_private *dev_priv,
				       struct intel_shared_dpll *pll,
				       struct intel_dpll_hw_state *hw_state)
{
	return true;
}

static const struct intel_shared_dpll_funcs hsw_ddi_lcpll_funcs = {
	.enable = hsw_ddi_lcpll_enable,
	.disable = hsw_ddi_lcpll_disable,
	.get_hw_state = hsw_ddi_lcpll_get_hw_state,
};

struct skl_dpll_regs {
	i915_reg_t ctl, cfgcr1, cfgcr2;
};

/* this array is indexed by the *shared* pll id */
static const struct skl_dpll_regs skl_dpll_regs[4] = {
	{
		/* DPLL 0 */
		.ctl = LCPLL1_CTL,
		/* DPLL 0 doesn't support HDMI mode */
	},
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

static void skl_ddi_pll_write_ctrl1(struct drm_i915_private *dev_priv,
				    struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(DPLL_CTRL1);

	val &= ~(DPLL_CTRL1_HDMI_MODE(pll->id) | DPLL_CTRL1_SSC(pll->id) |
		 DPLL_CTRL1_LINK_RATE_MASK(pll->id));
	val |= pll->state.hw_state.ctrl1 << (pll->id * 6);

	I915_WRITE(DPLL_CTRL1, val);
	POSTING_READ(DPLL_CTRL1);
}

static void skl_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	skl_ddi_pll_write_ctrl1(dev_priv, pll);

	I915_WRITE(regs[pll->id].cfgcr1, pll->state.hw_state.cfgcr1);
	I915_WRITE(regs[pll->id].cfgcr2, pll->state.hw_state.cfgcr2);
	POSTING_READ(regs[pll->id].cfgcr1);
	POSTING_READ(regs[pll->id].cfgcr2);

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) | LCPLL_PLL_ENABLE);

	if (intel_wait_for_register(dev_priv,
				    DPLL_STATUS,
				    DPLL_LOCK(pll->id),
				    DPLL_LOCK(pll->id),
				    5))
		DRM_ERROR("DPLL %d not locked\n", pll->id);
}

static void skl_ddi_dpll0_enable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	skl_ddi_pll_write_ctrl1(dev_priv, pll);
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

static void skl_ddi_dpll0_disable(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
}

static bool skl_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	const struct skl_dpll_regs *regs = skl_dpll_regs;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	val = I915_READ(regs[pll->id].ctl);
	if (!(val & LCPLL_PLL_ENABLE))
		goto out;

	val = I915_READ(DPLL_CTRL1);
	hw_state->ctrl1 = (val >> (pll->id * 6)) & 0x3f;

	/* avoid reading back stale values if HDMI mode is not enabled */
	if (val & DPLL_CTRL1_HDMI_MODE(pll->id)) {
		hw_state->cfgcr1 = I915_READ(regs[pll->id].cfgcr1);
		hw_state->cfgcr2 = I915_READ(regs[pll->id].cfgcr2);
	}
	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static bool skl_ddi_dpll0_get_hw_state(struct drm_i915_private *dev_priv,
				       struct intel_shared_dpll *pll,
				       struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	const struct skl_dpll_regs *regs = skl_dpll_regs;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	/* DPLL0 is always enabled since it drives CDCLK */
	val = I915_READ(regs[pll->id].ctl);
	if (WARN_ON(!(val & LCPLL_PLL_ENABLE)))
		goto out;

	val = I915_READ(DPLL_CTRL1);
	hw_state->ctrl1 = (val >> (pll->id * 6)) & 0x3f;

	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

struct skl_wrpll_context {
	uint64_t min_deviation;		/* current minimal deviation */
	uint64_t central_freq;		/* chosen central freq */
	uint64_t dco_freq;		/* chosen dco freq */
	unsigned int p;			/* chosen divider */
};

static void skl_wrpll_context_init(struct skl_wrpll_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->min_deviation = U64_MAX;
}

/* DCO freq must be within +1%/-6%  of the DCO central freq */
#define SKL_DCO_MAX_PDEVIATION	100
#define SKL_DCO_MAX_NDEVIATION	600

static void skl_wrpll_try_divider(struct skl_wrpll_context *ctx,
				  uint64_t central_freq,
				  uint64_t dco_freq,
				  unsigned int divider)
{
	uint64_t deviation;

	deviation = div64_u64(10000 * abs_diff(dco_freq, central_freq),
			      central_freq);

	/* positive deviation */
	if (dco_freq >= central_freq) {
		if (deviation < SKL_DCO_MAX_PDEVIATION &&
		    deviation < ctx->min_deviation) {
			ctx->min_deviation = deviation;
			ctx->central_freq = central_freq;
			ctx->dco_freq = dco_freq;
			ctx->p = divider;
		}
	/* negative deviation */
	} else if (deviation < SKL_DCO_MAX_NDEVIATION &&
		   deviation < ctx->min_deviation) {
		ctx->min_deviation = deviation;
		ctx->central_freq = central_freq;
		ctx->dco_freq = dco_freq;
		ctx->p = divider;
	}
}

static void skl_wrpll_get_multipliers(unsigned int p,
				      unsigned int *p0 /* out */,
				      unsigned int *p1 /* out */,
				      unsigned int *p2 /* out */)
{
	/* even dividers */
	if (p % 2 == 0) {
		unsigned int half = p / 2;

		if (half == 1 || half == 2 || half == 3 || half == 5) {
			*p0 = 2;
			*p1 = 1;
			*p2 = half;
		} else if (half % 2 == 0) {
			*p0 = 2;
			*p1 = half / 2;
			*p2 = 2;
		} else if (half % 3 == 0) {
			*p0 = 3;
			*p1 = half / 3;
			*p2 = 2;
		} else if (half % 7 == 0) {
			*p0 = 7;
			*p1 = half / 7;
			*p2 = 2;
		}
	} else if (p == 3 || p == 9) {  /* 3, 5, 7, 9, 15, 21, 35 */
		*p0 = 3;
		*p1 = 1;
		*p2 = p / 3;
	} else if (p == 5 || p == 7) {
		*p0 = p;
		*p1 = 1;
		*p2 = 1;
	} else if (p == 15) {
		*p0 = 3;
		*p1 = 1;
		*p2 = 5;
	} else if (p == 21) {
		*p0 = 7;
		*p1 = 1;
		*p2 = 3;
	} else if (p == 35) {
		*p0 = 7;
		*p1 = 1;
		*p2 = 5;
	}
}

struct skl_wrpll_params {
	uint32_t        dco_fraction;
	uint32_t        dco_integer;
	uint32_t        qdiv_ratio;
	uint32_t        qdiv_mode;
	uint32_t        kdiv;
	uint32_t        pdiv;
	uint32_t        central_freq;
};

static void skl_wrpll_params_populate(struct skl_wrpll_params *params,
				      uint64_t afe_clock,
				      uint64_t central_freq,
				      uint32_t p0, uint32_t p1, uint32_t p2)
{
	uint64_t dco_freq;

	switch (central_freq) {
	case 9600000000ULL:
		params->central_freq = 0;
		break;
	case 9000000000ULL:
		params->central_freq = 1;
		break;
	case 8400000000ULL:
		params->central_freq = 3;
	}

	switch (p0) {
	case 1:
		params->pdiv = 0;
		break;
	case 2:
		params->pdiv = 1;
		break;
	case 3:
		params->pdiv = 2;
		break;
	case 7:
		params->pdiv = 4;
		break;
	default:
		WARN(1, "Incorrect PDiv\n");
	}

	switch (p2) {
	case 5:
		params->kdiv = 0;
		break;
	case 2:
		params->kdiv = 1;
		break;
	case 3:
		params->kdiv = 2;
		break;
	case 1:
		params->kdiv = 3;
		break;
	default:
		WARN(1, "Incorrect KDiv\n");
	}

	params->qdiv_ratio = p1;
	params->qdiv_mode = (params->qdiv_ratio == 1) ? 0 : 1;

	dco_freq = p0 * p1 * p2 * afe_clock;

	/*
	 * Intermediate values are in Hz.
	 * Divide by MHz to match bsepc
	 */
	params->dco_integer = div_u64(dco_freq, 24 * MHz(1));
	params->dco_fraction =
		div_u64((div_u64(dco_freq, 24) -
			 params->dco_integer * MHz(1)) * 0x8000, MHz(1));
}

static bool
skl_ddi_calculate_wrpll(int clock /* in Hz */,
			struct skl_wrpll_params *wrpll_params)
{
	uint64_t afe_clock = clock * 5; /* AFE Clock is 5x Pixel clock */
	uint64_t dco_central_freq[3] = {8400000000ULL,
					9000000000ULL,
					9600000000ULL};
	static const int even_dividers[] = {  4,  6,  8, 10, 12, 14, 16, 18, 20,
					     24, 28, 30, 32, 36, 40, 42, 44,
					     48, 52, 54, 56, 60, 64, 66, 68,
					     70, 72, 76, 78, 80, 84, 88, 90,
					     92, 96, 98 };
	static const int odd_dividers[] = { 3, 5, 7, 9, 15, 21, 35 };
	static const struct {
		const int *list;
		int n_dividers;
	} dividers[] = {
		{ even_dividers, ARRAY_SIZE(even_dividers) },
		{ odd_dividers, ARRAY_SIZE(odd_dividers) },
	};
	struct skl_wrpll_context ctx;
	unsigned int dco, d, i;
	unsigned int p0, p1, p2;

	skl_wrpll_context_init(&ctx);

	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		for (dco = 0; dco < ARRAY_SIZE(dco_central_freq); dco++) {
			for (i = 0; i < dividers[d].n_dividers; i++) {
				unsigned int p = dividers[d].list[i];
				uint64_t dco_freq = p * afe_clock;

				skl_wrpll_try_divider(&ctx,
						      dco_central_freq[dco],
						      dco_freq,
						      p);
				/*
				 * Skip the remaining dividers if we're sure to
				 * have found the definitive divider, we can't
				 * improve a 0 deviation.
				 */
				if (ctx.min_deviation == 0)
					goto skip_remaining_dividers;
			}
		}

skip_remaining_dividers:
		/*
		 * If a solution is found with an even divider, prefer
		 * this one.
		 */
		if (d == 0 && ctx.p)
			break;
	}

	if (!ctx.p) {
		DRM_DEBUG_DRIVER("No valid divider found for %dHz\n", clock);
		return false;
	}

	/*
	 * gcc incorrectly analyses that these can be used without being
	 * initialized. To be fair, it's hard to guess.
	 */
	p0 = p1 = p2 = 0;
	skl_wrpll_get_multipliers(ctx.p, &p0, &p1, &p2);
	skl_wrpll_params_populate(wrpll_params, afe_clock, ctx.central_freq,
				  p0, p1, p2);

	return true;
}

static bool skl_ddi_hdmi_pll_dividers(struct intel_crtc *crtc,
				      struct intel_crtc_state *crtc_state,
				      int clock)
{
	uint32_t ctrl1, cfgcr1, cfgcr2;
	struct skl_wrpll_params wrpll_params = { 0, };

	/*
	 * See comment in intel_dpll_hw_state to understand why we always use 0
	 * as the DPLL id in this function.
	 */
	ctrl1 = DPLL_CTRL1_OVERRIDE(0);

	ctrl1 |= DPLL_CTRL1_HDMI_MODE(0);

	if (!skl_ddi_calculate_wrpll(clock * 1000, &wrpll_params))
		return false;

	cfgcr1 = DPLL_CFGCR1_FREQ_ENABLE |
		DPLL_CFGCR1_DCO_FRACTION(wrpll_params.dco_fraction) |
		wrpll_params.dco_integer;

	cfgcr2 = DPLL_CFGCR2_QDIV_RATIO(wrpll_params.qdiv_ratio) |
		DPLL_CFGCR2_QDIV_MODE(wrpll_params.qdiv_mode) |
		DPLL_CFGCR2_KDIV(wrpll_params.kdiv) |
		DPLL_CFGCR2_PDIV(wrpll_params.pdiv) |
		wrpll_params.central_freq;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	crtc_state->dpll_hw_state.ctrl1 = ctrl1;
	crtc_state->dpll_hw_state.cfgcr1 = cfgcr1;
	crtc_state->dpll_hw_state.cfgcr2 = cfgcr2;
	return true;
}

static bool
skl_ddi_dp_set_dpll_hw_state(int clock,
			     struct intel_dpll_hw_state *dpll_hw_state)
{
	uint32_t ctrl1;

	/*
	 * See comment in intel_dpll_hw_state to understand why we always use 0
	 * as the DPLL id in this function.
	 */
	ctrl1 = DPLL_CTRL1_OVERRIDE(0);
	switch (clock / 2) {
	case 81000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_810, 0);
		break;
	case 135000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1350, 0);
		break;
	case 270000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_2700, 0);
		break;
		/* eDP 1.4 rates */
	case 162000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1620, 0);
		break;
	case 108000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1080, 0);
		break;
	case 216000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_2160, 0);
		break;
	}

	dpll_hw_state->ctrl1 = ctrl1;
	return true;
}

static struct intel_shared_dpll *
skl_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct intel_shared_dpll *pll;
	int clock = crtc_state->port_clock;
	bool bret;
	struct intel_dpll_hw_state dpll_hw_state;

	memset(&dpll_hw_state, 0, sizeof(dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		bret = skl_ddi_hdmi_pll_dividers(crtc, crtc_state, clock);
		if (!bret) {
			DRM_DEBUG_KMS("Could not get HDMI pll dividers.\n");
			return NULL;
		}
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		bret = skl_ddi_dp_set_dpll_hw_state(clock, &dpll_hw_state);
		if (!bret) {
			DRM_DEBUG_KMS("Could not set DP dpll HW state.\n");
			return NULL;
		}
		crtc_state->dpll_hw_state = dpll_hw_state;
	} else {
		return NULL;
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_SKL_DPLL0,
					     DPLL_ID_SKL_DPLL0);
	else
		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_SKL_DPLL1,
					     DPLL_ID_SKL_DPLL3);
	if (!pll)
		return NULL;

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static void skl_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	DRM_DEBUG_KMS("dpll_hw_state: "
		      "ctrl1: 0x%x, cfgcr1: 0x%x, cfgcr2: 0x%x\n",
		      hw_state->ctrl1,
		      hw_state->cfgcr1,
		      hw_state->cfgcr2);
}

static const struct intel_shared_dpll_funcs skl_ddi_pll_funcs = {
	.enable = skl_ddi_pll_enable,
	.disable = skl_ddi_pll_disable,
	.get_hw_state = skl_ddi_pll_get_hw_state,
};

static const struct intel_shared_dpll_funcs skl_ddi_dpll0_funcs = {
	.enable = skl_ddi_dpll0_enable,
	.disable = skl_ddi_dpll0_disable,
	.get_hw_state = skl_ddi_dpll0_get_hw_state,
};

static void bxt_ddi_pll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t temp;
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	enum dpio_phy phy;
	enum dpio_channel ch;

	bxt_port_to_phy_channel(dev_priv, port, &phy, &ch);

	/* Non-SSC reference */
	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp |= PORT_PLL_REF_SEL;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

	if (IS_GEMINILAKE(dev_priv)) {
		temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
		temp |= PORT_PLL_POWER_ENABLE;
		I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

		if (wait_for_us((I915_READ(BXT_PORT_PLL_ENABLE(port)) &
				 PORT_PLL_POWER_STATE), 200))
			DRM_ERROR("Power state not set for PLL:%d\n", port);
	}

	/* Disable 10 bit clock */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(phy, ch));
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(phy, ch), temp);

	/* Write P1 & P2 */
	temp = I915_READ(BXT_PORT_PLL_EBB_0(phy, ch));
	temp &= ~(PORT_PLL_P1_MASK | PORT_PLL_P2_MASK);
	temp |= pll->state.hw_state.ebb0;
	I915_WRITE(BXT_PORT_PLL_EBB_0(phy, ch), temp);

	/* Write M2 integer */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 0));
	temp &= ~PORT_PLL_M2_MASK;
	temp |= pll->state.hw_state.pll0;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 0), temp);

	/* Write N */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 1));
	temp &= ~PORT_PLL_N_MASK;
	temp |= pll->state.hw_state.pll1;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 1), temp);

	/* Write M2 fraction */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 2));
	temp &= ~PORT_PLL_M2_FRAC_MASK;
	temp |= pll->state.hw_state.pll2;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 2), temp);

	/* Write M2 fraction enable */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 3));
	temp &= ~PORT_PLL_M2_FRAC_ENABLE;
	temp |= pll->state.hw_state.pll3;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 3), temp);

	/* Write coeff */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 6));
	temp &= ~PORT_PLL_PROP_COEFF_MASK;
	temp &= ~PORT_PLL_INT_COEFF_MASK;
	temp &= ~PORT_PLL_GAIN_CTL_MASK;
	temp |= pll->state.hw_state.pll6;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 6), temp);

	/* Write calibration val */
	temp = I915_READ(BXT_PORT_PLL(phy, ch, 8));
	temp &= ~PORT_PLL_TARGET_CNT_MASK;
	temp |= pll->state.hw_state.pll8;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 8), temp);

	temp = I915_READ(BXT_PORT_PLL(phy, ch, 9));
	temp &= ~PORT_PLL_LOCK_THRESHOLD_MASK;
	temp |= pll->state.hw_state.pll9;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 9), temp);

	temp = I915_READ(BXT_PORT_PLL(phy, ch, 10));
	temp &= ~PORT_PLL_DCO_AMP_OVR_EN_H;
	temp &= ~PORT_PLL_DCO_AMP_MASK;
	temp |= pll->state.hw_state.pll10;
	I915_WRITE(BXT_PORT_PLL(phy, ch, 10), temp);

	/* Recalibrate with new settings */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(phy, ch));
	temp |= PORT_PLL_RECALIBRATE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(phy, ch), temp);
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	temp |= pll->state.hw_state.ebb4;
	I915_WRITE(BXT_PORT_PLL_EBB_4(phy, ch), temp);

	/* Enable PLL */
	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp |= PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));

	if (wait_for_us((I915_READ(BXT_PORT_PLL_ENABLE(port)) & PORT_PLL_LOCK),
			200))
		DRM_ERROR("PLL %d not locked\n", port);

	if (IS_GEMINILAKE(dev_priv)) {
		temp = I915_READ(BXT_PORT_TX_DW5_LN0(phy, ch));
		temp |= DCC_DELAY_RANGE_2;
		I915_WRITE(BXT_PORT_TX_DW5_GRP(phy, ch), temp);
	}

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	temp = I915_READ(BXT_PORT_PCS_DW12_LN01(phy, ch));
	temp &= ~LANE_STAGGER_MASK;
	temp &= ~LANESTAGGER_STRAP_OVRD;
	temp |= pll->state.hw_state.pcsdw12;
	I915_WRITE(BXT_PORT_PCS_DW12_GRP(phy, ch), temp);
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

	if (IS_GEMINILAKE(dev_priv)) {
		temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
		temp &= ~PORT_PLL_POWER_ENABLE;
		I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

		if (wait_for_us(!(I915_READ(BXT_PORT_PLL_ENABLE(port)) &
				PORT_PLL_POWER_STATE), 200))
			DRM_ERROR("Power state not reset for PLL:%d\n", port);
	}
}

static bool bxt_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll,
					struct intel_dpll_hw_state *hw_state)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t val;
	bool ret;
	enum dpio_phy phy;
	enum dpio_channel ch;

	bxt_port_to_phy_channel(dev_priv, port, &phy, &ch);

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	val = I915_READ(BXT_PORT_PLL_ENABLE(port));
	if (!(val & PORT_PLL_ENABLE))
		goto out;

	hw_state->ebb0 = I915_READ(BXT_PORT_PLL_EBB_0(phy, ch));
	hw_state->ebb0 &= PORT_PLL_P1_MASK | PORT_PLL_P2_MASK;

	hw_state->ebb4 = I915_READ(BXT_PORT_PLL_EBB_4(phy, ch));
	hw_state->ebb4 &= PORT_PLL_10BIT_CLK_ENABLE;

	hw_state->pll0 = I915_READ(BXT_PORT_PLL(phy, ch, 0));
	hw_state->pll0 &= PORT_PLL_M2_MASK;

	hw_state->pll1 = I915_READ(BXT_PORT_PLL(phy, ch, 1));
	hw_state->pll1 &= PORT_PLL_N_MASK;

	hw_state->pll2 = I915_READ(BXT_PORT_PLL(phy, ch, 2));
	hw_state->pll2 &= PORT_PLL_M2_FRAC_MASK;

	hw_state->pll3 = I915_READ(BXT_PORT_PLL(phy, ch, 3));
	hw_state->pll3 &= PORT_PLL_M2_FRAC_ENABLE;

	hw_state->pll6 = I915_READ(BXT_PORT_PLL(phy, ch, 6));
	hw_state->pll6 &= PORT_PLL_PROP_COEFF_MASK |
			  PORT_PLL_INT_COEFF_MASK |
			  PORT_PLL_GAIN_CTL_MASK;

	hw_state->pll8 = I915_READ(BXT_PORT_PLL(phy, ch, 8));
	hw_state->pll8 &= PORT_PLL_TARGET_CNT_MASK;

	hw_state->pll9 = I915_READ(BXT_PORT_PLL(phy, ch, 9));
	hw_state->pll9 &= PORT_PLL_LOCK_THRESHOLD_MASK;

	hw_state->pll10 = I915_READ(BXT_PORT_PLL(phy, ch, 10));
	hw_state->pll10 &= PORT_PLL_DCO_AMP_OVR_EN_H |
			   PORT_PLL_DCO_AMP_MASK;

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers. We configure all lanes the same way, so
	 * here just read out lanes 0/1 and output a note if lanes 2/3 differ.
	 */
	hw_state->pcsdw12 = I915_READ(BXT_PORT_PCS_DW12_LN01(phy, ch));
	if (I915_READ(BXT_PORT_PCS_DW12_LN23(phy, ch)) != hw_state->pcsdw12)
		DRM_DEBUG_DRIVER("lane stagger config different for lane 01 (%08x) and 23 (%08x)\n",
				 hw_state->pcsdw12,
				 I915_READ(BXT_PORT_PCS_DW12_LN23(phy, ch)));
	hw_state->pcsdw12 &= LANE_STAGGER_MASK | LANESTAGGER_STRAP_OVRD;

	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

/* bxt clock parameters */
struct bxt_clk_div {
	int clock;
	uint32_t p1;
	uint32_t p2;
	uint32_t m2_int;
	uint32_t m2_frac;
	bool m2_frac_en;
	uint32_t n;

	int vco;
};

/* pre-calculated values for DP linkrates */
static const struct bxt_clk_div bxt_dp_clk_val[] = {
	{162000, 4, 2, 32, 1677722, 1, 1},
	{270000, 4, 1, 27,       0, 0, 1},
	{540000, 2, 1, 27,       0, 0, 1},
	{216000, 3, 2, 32, 1677722, 1, 1},
	{243000, 4, 1, 24, 1258291, 1, 1},
	{324000, 4, 1, 32, 1677722, 1, 1},
	{432000, 3, 1, 32, 1677722, 1, 1}
};

static bool
bxt_ddi_hdmi_pll_dividers(struct intel_crtc *intel_crtc,
			  struct intel_crtc_state *crtc_state, int clock,
			  struct bxt_clk_div *clk_div)
{
	struct dpll best_clock;

	/* Calculate HDMI div */
	/*
	 * FIXME: tie the following calculation into
	 * i9xx_crtc_compute_clock
	 */
	if (!bxt_find_best_dpll(crtc_state, clock, &best_clock)) {
		DRM_DEBUG_DRIVER("no PLL dividers found for clock %d pipe %c\n",
				 clock, pipe_name(intel_crtc->pipe));
		return false;
	}

	clk_div->p1 = best_clock.p1;
	clk_div->p2 = best_clock.p2;
	WARN_ON(best_clock.m1 != 2);
	clk_div->n = best_clock.n;
	clk_div->m2_int = best_clock.m2 >> 22;
	clk_div->m2_frac = best_clock.m2 & ((1 << 22) - 1);
	clk_div->m2_frac_en = clk_div->m2_frac != 0;

	clk_div->vco = best_clock.vco;

	return true;
}

static void bxt_ddi_dp_pll_dividers(int clock, struct bxt_clk_div *clk_div)
{
	int i;

	*clk_div = bxt_dp_clk_val[0];
	for (i = 0; i < ARRAY_SIZE(bxt_dp_clk_val); ++i) {
		if (bxt_dp_clk_val[i].clock == clock) {
			*clk_div = bxt_dp_clk_val[i];
			break;
		}
	}

	clk_div->vco = clock * 10 / 2 * clk_div->p1 * clk_div->p2;
}

static bool bxt_ddi_set_dpll_hw_state(int clock,
			  struct bxt_clk_div *clk_div,
			  struct intel_dpll_hw_state *dpll_hw_state)
{
	int vco = clk_div->vco;
	uint32_t prop_coef, int_coef, gain_ctl, targ_cnt;
	uint32_t lanestagger;

	if (vco >= 6200000 && vco <= 6700000) {
		prop_coef = 4;
		int_coef = 9;
		gain_ctl = 3;
		targ_cnt = 8;
	} else if ((vco > 5400000 && vco < 6200000) ||
			(vco >= 4800000 && vco < 5400000)) {
		prop_coef = 5;
		int_coef = 11;
		gain_ctl = 3;
		targ_cnt = 9;
	} else if (vco == 5400000) {
		prop_coef = 3;
		int_coef = 8;
		gain_ctl = 1;
		targ_cnt = 9;
	} else {
		DRM_ERROR("Invalid VCO\n");
		return false;
	}

	if (clock > 270000)
		lanestagger = 0x18;
	else if (clock > 135000)
		lanestagger = 0x0d;
	else if (clock > 67000)
		lanestagger = 0x07;
	else if (clock > 33000)
		lanestagger = 0x04;
	else
		lanestagger = 0x02;

	dpll_hw_state->ebb0 = PORT_PLL_P1(clk_div->p1) | PORT_PLL_P2(clk_div->p2);
	dpll_hw_state->pll0 = clk_div->m2_int;
	dpll_hw_state->pll1 = PORT_PLL_N(clk_div->n);
	dpll_hw_state->pll2 = clk_div->m2_frac;

	if (clk_div->m2_frac_en)
		dpll_hw_state->pll3 = PORT_PLL_M2_FRAC_ENABLE;

	dpll_hw_state->pll6 = prop_coef | PORT_PLL_INT_COEFF(int_coef);
	dpll_hw_state->pll6 |= PORT_PLL_GAIN_CTL(gain_ctl);

	dpll_hw_state->pll8 = targ_cnt;

	dpll_hw_state->pll9 = 5 << PORT_PLL_LOCK_THRESHOLD_SHIFT;

	dpll_hw_state->pll10 =
		PORT_PLL_DCO_AMP(PORT_PLL_DCO_AMP_DEFAULT)
		| PORT_PLL_DCO_AMP_OVR_EN_H;

	dpll_hw_state->ebb4 = PORT_PLL_10BIT_CLK_ENABLE;

	dpll_hw_state->pcsdw12 = LANESTAGGER_STRAP_OVRD | lanestagger;

	return true;
}

static bool
bxt_ddi_dp_set_dpll_hw_state(int clock,
			     struct intel_dpll_hw_state *dpll_hw_state)
{
	struct bxt_clk_div clk_div = {0};

	bxt_ddi_dp_pll_dividers(clock, &clk_div);

	return bxt_ddi_set_dpll_hw_state(clock, &clk_div, dpll_hw_state);
}

static bool
bxt_ddi_hdmi_set_dpll_hw_state(struct intel_crtc *intel_crtc,
			       struct intel_crtc_state *crtc_state, int clock,
			       struct intel_dpll_hw_state *dpll_hw_state)
{
	struct bxt_clk_div clk_div = { };

	bxt_ddi_hdmi_pll_dividers(intel_crtc, crtc_state, clock, &clk_div);

	return bxt_ddi_set_dpll_hw_state(clock, &clk_div, dpll_hw_state);
}

static struct intel_shared_dpll *
bxt_get_dpll(struct intel_crtc *crtc,
		struct intel_crtc_state *crtc_state,
		struct intel_encoder *encoder)
{
	struct intel_dpll_hw_state dpll_hw_state = { };
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	int i, clock = crtc_state->port_clock;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) &&
	    !bxt_ddi_hdmi_set_dpll_hw_state(crtc, crtc_state, clock,
					    &dpll_hw_state))
		return NULL;

	if (intel_crtc_has_dp_encoder(crtc_state) &&
	    !bxt_ddi_dp_set_dpll_hw_state(clock, &dpll_hw_state))
		return NULL;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	crtc_state->dpll_hw_state = dpll_hw_state;

	/* 1:1 mapping between ports and PLLs */
	i = (enum intel_dpll_id) encoder->port;
	pll = intel_get_shared_dpll_by_id(dev_priv, i);

	DRM_DEBUG_KMS("[CRTC:%d:%s] using pre-allocated %s\n",
		      crtc->base.base.id, crtc->base.name, pll->name);

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static void bxt_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	DRM_DEBUG_KMS("dpll_hw_state: ebb0: 0x%x, ebb4: 0x%x,"
		      "pll0: 0x%x, pll1: 0x%x, pll2: 0x%x, pll3: 0x%x, "
		      "pll6: 0x%x, pll8: 0x%x, pll9: 0x%x, pll10: 0x%x, pcsdw12: 0x%x\n",
		      hw_state->ebb0,
		      hw_state->ebb4,
		      hw_state->pll0,
		      hw_state->pll1,
		      hw_state->pll2,
		      hw_state->pll3,
		      hw_state->pll6,
		      hw_state->pll8,
		      hw_state->pll9,
		      hw_state->pll10,
		      hw_state->pcsdw12);
}

static const struct intel_shared_dpll_funcs bxt_ddi_pll_funcs = {
	.enable = bxt_ddi_pll_enable,
	.disable = bxt_ddi_pll_disable,
	.get_hw_state = bxt_ddi_pll_get_hw_state,
};

static void intel_ddi_pll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (INTEL_GEN(dev_priv) < 9) {
		uint32_t val = I915_READ(LCPLL_CTL);

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

struct dpll_info {
	const char *name;
	const int id;
	const struct intel_shared_dpll_funcs *funcs;
	uint32_t flags;
};

struct intel_dpll_mgr {
	const struct dpll_info *dpll_info;

	struct intel_shared_dpll *(*get_dpll)(struct intel_crtc *crtc,
					      struct intel_crtc_state *crtc_state,
					      struct intel_encoder *encoder);

	void (*dump_hw_state)(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state);
};

static const struct dpll_info pch_plls[] = {
	{ "PCH DPLL A", DPLL_ID_PCH_PLL_A, &ibx_pch_dpll_funcs, 0 },
	{ "PCH DPLL B", DPLL_ID_PCH_PLL_B, &ibx_pch_dpll_funcs, 0 },
	{ NULL, -1, NULL, 0 },
};

static const struct intel_dpll_mgr pch_pll_mgr = {
	.dpll_info = pch_plls,
	.get_dpll = ibx_get_dpll,
	.dump_hw_state = ibx_dump_hw_state,
};

static const struct dpll_info hsw_plls[] = {
	{ "WRPLL 1",    DPLL_ID_WRPLL1,     &hsw_ddi_wrpll_funcs, 0 },
	{ "WRPLL 2",    DPLL_ID_WRPLL2,     &hsw_ddi_wrpll_funcs, 0 },
	{ "SPLL",       DPLL_ID_SPLL,       &hsw_ddi_spll_funcs,  0 },
	{ "LCPLL 810",  DPLL_ID_LCPLL_810,  &hsw_ddi_lcpll_funcs, INTEL_DPLL_ALWAYS_ON },
	{ "LCPLL 1350", DPLL_ID_LCPLL_1350, &hsw_ddi_lcpll_funcs, INTEL_DPLL_ALWAYS_ON },
	{ "LCPLL 2700", DPLL_ID_LCPLL_2700, &hsw_ddi_lcpll_funcs, INTEL_DPLL_ALWAYS_ON },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr hsw_pll_mgr = {
	.dpll_info = hsw_plls,
	.get_dpll = hsw_get_dpll,
	.dump_hw_state = hsw_dump_hw_state,
};

static const struct dpll_info skl_plls[] = {
	{ "DPLL 0", DPLL_ID_SKL_DPLL0, &skl_ddi_dpll0_funcs, INTEL_DPLL_ALWAYS_ON },
	{ "DPLL 1", DPLL_ID_SKL_DPLL1, &skl_ddi_pll_funcs,   0 },
	{ "DPLL 2", DPLL_ID_SKL_DPLL2, &skl_ddi_pll_funcs,   0 },
	{ "DPLL 3", DPLL_ID_SKL_DPLL3, &skl_ddi_pll_funcs,   0 },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr skl_pll_mgr = {
	.dpll_info = skl_plls,
	.get_dpll = skl_get_dpll,
	.dump_hw_state = skl_dump_hw_state,
};

static const struct dpll_info bxt_plls[] = {
	{ "PORT PLL A", DPLL_ID_SKL_DPLL0, &bxt_ddi_pll_funcs, 0 },
	{ "PORT PLL B", DPLL_ID_SKL_DPLL1, &bxt_ddi_pll_funcs, 0 },
	{ "PORT PLL C", DPLL_ID_SKL_DPLL2, &bxt_ddi_pll_funcs, 0 },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr bxt_pll_mgr = {
	.dpll_info = bxt_plls,
	.get_dpll = bxt_get_dpll,
	.dump_hw_state = bxt_dump_hw_state,
};

static void cnl_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	uint32_t val;

	/* 1. Enable DPLL power in DPLL_ENABLE. */
	val = I915_READ(CNL_DPLL_ENABLE(pll->id));
	val |= PLL_POWER_ENABLE;
	I915_WRITE(CNL_DPLL_ENABLE(pll->id), val);

	/* 2. Wait for DPLL power state enabled in DPLL_ENABLE. */
	if (intel_wait_for_register(dev_priv,
				    CNL_DPLL_ENABLE(pll->id),
				    PLL_POWER_STATE,
				    PLL_POWER_STATE,
				    5))
		DRM_ERROR("PLL %d Power not enabled\n", pll->id);

	/*
	 * 3. Configure DPLL_CFGCR0 to set SSC enable/disable,
	 * select DP mode, and set DP link rate.
	 */
	val = pll->state.hw_state.cfgcr0;
	I915_WRITE(CNL_DPLL_CFGCR0(pll->id), val);

	/* 4. Reab back to ensure writes completed */
	POSTING_READ(CNL_DPLL_CFGCR0(pll->id));

	/* 3. Configure DPLL_CFGCR0 */
	/* Avoid touch CFGCR1 if HDMI mode is not enabled */
	if (pll->state.hw_state.cfgcr0 & DPLL_CFGCR0_HDMI_MODE) {
		val = pll->state.hw_state.cfgcr1;
		I915_WRITE(CNL_DPLL_CFGCR1(pll->id), val);
		/* 4. Reab back to ensure writes completed */
		POSTING_READ(CNL_DPLL_CFGCR1(pll->id));
	}

	/*
	 * 5. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching
	 * Sequence Before Frequency Change
	 *
	 * Note: DVFS is actually handled via the cdclk code paths,
	 * hence we do nothing here.
	 */

	/* 6. Enable DPLL in DPLL_ENABLE. */
	val = I915_READ(CNL_DPLL_ENABLE(pll->id));
	val |= PLL_ENABLE;
	I915_WRITE(CNL_DPLL_ENABLE(pll->id), val);

	/* 7. Wait for PLL lock status in DPLL_ENABLE. */
	if (intel_wait_for_register(dev_priv,
				    CNL_DPLL_ENABLE(pll->id),
				    PLL_LOCK,
				    PLL_LOCK,
				    5))
		DRM_ERROR("PLL %d not locked\n", pll->id);

	/*
	 * 8. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching
	 * Sequence After Frequency Change
	 *
	 * Note: DVFS is actually handled via the cdclk code paths,
	 * hence we do nothing here.
	 */

	/*
	 * 9. turn on the clock for the DDI and map the DPLL to the DDI
	 * Done at intel_ddi_clk_select
	 */
}

static void cnl_ddi_pll_disable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t val;

	/*
	 * 1. Configure DPCLKA_CFGCR0 to turn off the clock for the DDI.
	 * Done at intel_ddi_post_disable
	 */

	/*
	 * 2. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching
	 * Sequence Before Frequency Change
	 *
	 * Note: DVFS is actually handled via the cdclk code paths,
	 * hence we do nothing here.
	 */

	/* 3. Disable DPLL through DPLL_ENABLE. */
	val = I915_READ(CNL_DPLL_ENABLE(pll->id));
	val &= ~PLL_ENABLE;
	I915_WRITE(CNL_DPLL_ENABLE(pll->id), val);

	/* 4. Wait for PLL not locked status in DPLL_ENABLE. */
	if (intel_wait_for_register(dev_priv,
				    CNL_DPLL_ENABLE(pll->id),
				    PLL_LOCK,
				    0,
				    5))
		DRM_ERROR("PLL %d locked\n", pll->id);

	/*
	 * 5. If the frequency will result in a change to the voltage
	 * requirement, follow the Display Voltage Frequency Switching
	 * Sequence After Frequency Change
	 *
	 * Note: DVFS is actually handled via the cdclk code paths,
	 * hence we do nothing here.
	 */

	/* 6. Disable DPLL power in DPLL_ENABLE. */
	val = I915_READ(CNL_DPLL_ENABLE(pll->id));
	val &= ~PLL_POWER_ENABLE;
	I915_WRITE(CNL_DPLL_ENABLE(pll->id), val);

	/* 7. Wait for DPLL power state disabled in DPLL_ENABLE. */
	if (intel_wait_for_register(dev_priv,
				    CNL_DPLL_ENABLE(pll->id),
				    PLL_POWER_STATE,
				    0,
				    5))
		DRM_ERROR("PLL %d Power not disabled\n", pll->id);
}

static bool cnl_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	val = I915_READ(CNL_DPLL_ENABLE(pll->id));
	if (!(val & PLL_ENABLE))
		goto out;

	val = I915_READ(CNL_DPLL_CFGCR0(pll->id));
	hw_state->cfgcr0 = val;

	/* avoid reading back stale values if HDMI mode is not enabled */
	if (val & DPLL_CFGCR0_HDMI_MODE) {
		hw_state->cfgcr1 = I915_READ(CNL_DPLL_CFGCR1(pll->id));
	}
	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static void cnl_wrpll_get_multipliers(unsigned int bestdiv,
				      unsigned int *pdiv,
				      unsigned int *qdiv,
				      unsigned int *kdiv)
{
	/* even dividers */
	if (bestdiv % 2 == 0) {
		if (bestdiv == 2) {
			*pdiv = 2;
			*qdiv = 1;
			*kdiv = 1;
		} else if (bestdiv % 4 == 0) {
			*pdiv = 2;
			*qdiv = bestdiv / 4;
			*kdiv = 2;
		} else if (bestdiv % 6 == 0) {
			*pdiv = 3;
			*qdiv = bestdiv / 6;
			*kdiv = 2;
		} else if (bestdiv % 5 == 0) {
			*pdiv = 5;
			*qdiv = bestdiv / 10;
			*kdiv = 2;
		} else if (bestdiv % 14 == 0) {
			*pdiv = 7;
			*qdiv = bestdiv / 14;
			*kdiv = 2;
		}
	} else {
		if (bestdiv == 3 || bestdiv == 5 || bestdiv == 7) {
			*pdiv = bestdiv;
			*qdiv = 1;
			*kdiv = 1;
		} else { /* 9, 15, 21 */
			*pdiv = bestdiv / 3;
			*qdiv = 1;
			*kdiv = 3;
		}
	}
}

static void cnl_wrpll_params_populate(struct skl_wrpll_params *params, uint32_t dco_freq,
				      uint32_t ref_freq, uint32_t pdiv, uint32_t qdiv,
				      uint32_t kdiv)
{
	switch (kdiv) {
	case 1:
		params->kdiv = 1;
		break;
	case 2:
		params->kdiv = 2;
		break;
	case 3:
		params->kdiv = 4;
		break;
	default:
		WARN(1, "Incorrect KDiv\n");
	}

	switch (pdiv) {
	case 2:
		params->pdiv = 1;
		break;
	case 3:
		params->pdiv = 2;
		break;
	case 5:
		params->pdiv = 4;
		break;
	case 7:
		params->pdiv = 8;
		break;
	default:
		WARN(1, "Incorrect PDiv\n");
	}

	if (kdiv != 2)
		qdiv = 1;

	params->qdiv_ratio = qdiv;
	params->qdiv_mode = (qdiv == 1) ? 0 : 1;

	params->dco_integer = div_u64(dco_freq, ref_freq);
	params->dco_fraction = div_u64((div_u64((uint64_t)dco_freq<<15, (uint64_t)ref_freq) -
					((uint64_t)params->dco_integer<<15)) * 0x8000, 0x8000);
}

static bool
cnl_ddi_calculate_wrpll(int clock /* in Hz */,
			struct drm_i915_private *dev_priv,
			struct skl_wrpll_params *wrpll_params)
{
	uint64_t afe_clock = clock * 5 / KHz(1); /* clocks in kHz */
	unsigned int dco_min = 7998 * KHz(1);
	unsigned int dco_max = 10000 * KHz(1);
	unsigned int dco_mid = (dco_min + dco_max) / 2;

	static const int dividers[] = {  2,  4,  6,  8, 10, 12,  14,  16,
					 18, 20, 24, 28, 30, 32,  36,  40,
					 42, 44, 48, 50, 52, 54,  56,  60,
					 64, 66, 68, 70, 72, 76,  78,  80,
					 84, 88, 90, 92, 96, 98, 100, 102,
					  3,  5,  7,  9, 15, 21 };
	unsigned int d, dco;
	unsigned int dco_centrality = 0;
	unsigned int best_dco_centrality = 999999;
	unsigned int best_div = 0;
	unsigned int best_dco = 0;
	unsigned int pdiv = 0, qdiv = 0, kdiv = 0;

	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		dco = afe_clock * dividers[d];

		if ((dco <= dco_max) && (dco >= dco_min)) {
			dco_centrality = abs(dco - dco_mid);

			if (dco_centrality < best_dco_centrality) {
				best_dco_centrality = dco_centrality;
				best_div = dividers[d];
				best_dco = dco;
			}
		}
	}

	if (best_div == 0)
		return false;

	cnl_wrpll_get_multipliers(best_div, &pdiv, &qdiv, &kdiv);

	cnl_wrpll_params_populate(wrpll_params, best_dco,
				  dev_priv->cdclk.hw.ref, pdiv, qdiv, kdiv);

	return true;
}

static bool cnl_ddi_hdmi_pll_dividers(struct intel_crtc *crtc,
				      struct intel_crtc_state *crtc_state,
				      int clock)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	uint32_t cfgcr0, cfgcr1;
	struct skl_wrpll_params wrpll_params = { 0, };

	cfgcr0 = DPLL_CFGCR0_HDMI_MODE;

	if (!cnl_ddi_calculate_wrpll(clock * 1000, dev_priv, &wrpll_params))
		return false;

	cfgcr0 |= DPLL_CFGCR0_DCO_FRACTION(wrpll_params.dco_fraction) |
		wrpll_params.dco_integer;

	cfgcr1 = DPLL_CFGCR1_QDIV_RATIO(wrpll_params.qdiv_ratio) |
		DPLL_CFGCR1_QDIV_MODE(wrpll_params.qdiv_mode) |
		DPLL_CFGCR1_KDIV(wrpll_params.kdiv) |
		DPLL_CFGCR1_PDIV(wrpll_params.pdiv) |
		wrpll_params.central_freq |
		DPLL_CFGCR1_CENTRAL_FREQ;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	crtc_state->dpll_hw_state.cfgcr0 = cfgcr0;
	crtc_state->dpll_hw_state.cfgcr1 = cfgcr1;
	return true;
}

static bool
cnl_ddi_dp_set_dpll_hw_state(int clock,
			     struct intel_dpll_hw_state *dpll_hw_state)
{
	uint32_t cfgcr0;

	cfgcr0 = DPLL_CFGCR0_SSC_ENABLE;

	switch (clock / 2) {
	case 81000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_810;
		break;
	case 135000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_1350;
		break;
	case 270000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_2700;
		break;
		/* eDP 1.4 rates */
	case 162000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_1620;
		break;
	case 108000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_1080;
		break;
	case 216000:
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_2160;
		break;
	case 324000:
		/* Some SKUs may require elevated I/O voltage to support this */
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_3240;
		break;
	case 405000:
		/* Some SKUs may require elevated I/O voltage to support this */
		cfgcr0 |= DPLL_CFGCR0_LINK_RATE_4050;
		break;
	}

	dpll_hw_state->cfgcr0 = cfgcr0;
	return true;
}

static struct intel_shared_dpll *
cnl_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct intel_shared_dpll *pll;
	int clock = crtc_state->port_clock;
	bool bret;
	struct intel_dpll_hw_state dpll_hw_state;

	memset(&dpll_hw_state, 0, sizeof(dpll_hw_state));

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		bret = cnl_ddi_hdmi_pll_dividers(crtc, crtc_state, clock);
		if (!bret) {
			DRM_DEBUG_KMS("Could not get HDMI pll dividers.\n");
			return NULL;
		}
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		bret = cnl_ddi_dp_set_dpll_hw_state(clock, &dpll_hw_state);
		if (!bret) {
			DRM_DEBUG_KMS("Could not set DP dpll HW state.\n");
			return NULL;
		}
		crtc_state->dpll_hw_state = dpll_hw_state;
	} else {
		DRM_DEBUG_KMS("Skip DPLL setup for output_types 0x%x\n",
			      crtc_state->output_types);
		return NULL;
	}

	pll = intel_find_shared_dpll(crtc, crtc_state,
				     DPLL_ID_SKL_DPLL0,
				     DPLL_ID_SKL_DPLL2);
	if (!pll) {
		DRM_DEBUG_KMS("No PLL selected\n");
		return NULL;
	}

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static void cnl_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	DRM_DEBUG_KMS("dpll_hw_state: "
		      "cfgcr0: 0x%x, cfgcr1: 0x%x\n",
		      hw_state->cfgcr0,
		      hw_state->cfgcr1);
}

static const struct intel_shared_dpll_funcs cnl_ddi_pll_funcs = {
	.enable = cnl_ddi_pll_enable,
	.disable = cnl_ddi_pll_disable,
	.get_hw_state = cnl_ddi_pll_get_hw_state,
};

static const struct dpll_info cnl_plls[] = {
	{ "DPLL 0", DPLL_ID_SKL_DPLL0, &cnl_ddi_pll_funcs, 0 },
	{ "DPLL 1", DPLL_ID_SKL_DPLL1, &cnl_ddi_pll_funcs, 0 },
	{ "DPLL 2", DPLL_ID_SKL_DPLL2, &cnl_ddi_pll_funcs, 0 },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr cnl_pll_mgr = {
	.dpll_info = cnl_plls,
	.get_dpll = cnl_get_dpll,
	.dump_hw_state = cnl_dump_hw_state,
};

/**
 * intel_shared_dpll_init - Initialize shared DPLLs
 * @dev: drm device
 *
 * Initialize shared DPLLs for @dev.
 */
void intel_shared_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_dpll_mgr *dpll_mgr = NULL;
	const struct dpll_info *dpll_info;
	int i;

	if (IS_CANNONLAKE(dev_priv))
		dpll_mgr = &cnl_pll_mgr;
	else if (IS_GEN9_BC(dev_priv))
		dpll_mgr = &skl_pll_mgr;
	else if (IS_GEN9_LP(dev_priv))
		dpll_mgr = &bxt_pll_mgr;
	else if (HAS_DDI(dev_priv))
		dpll_mgr = &hsw_pll_mgr;
	else if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv))
		dpll_mgr = &pch_pll_mgr;

	if (!dpll_mgr) {
		dev_priv->num_shared_dpll = 0;
		return;
	}

	dpll_info = dpll_mgr->dpll_info;

	for (i = 0; dpll_info[i].id >= 0; i++) {
		WARN_ON(i != dpll_info[i].id);

		dev_priv->shared_dplls[i].id = dpll_info[i].id;
		dev_priv->shared_dplls[i].name = dpll_info[i].name;
		dev_priv->shared_dplls[i].funcs = *dpll_info[i].funcs;
		dev_priv->shared_dplls[i].flags = dpll_info[i].flags;
	}

	dev_priv->dpll_mgr = dpll_mgr;
	dev_priv->num_shared_dpll = i;
	mutex_init(&dev_priv->dpll_lock);

	BUG_ON(dev_priv->num_shared_dpll > I915_NUM_PLLS);

	/* FIXME: Move this to a more suitable place */
	if (HAS_DDI(dev_priv))
		intel_ddi_pll_init(dev);
}

/**
 * intel_get_shared_dpll - get a shared DPLL for CRTC and encoder combination
 * @crtc: CRTC
 * @crtc_state: atomic state for @crtc
 * @encoder: encoder
 *
 * Find an appropriate DPLL for the given CRTC and encoder combination. A
 * reference from the @crtc to the returned pll is registered in the atomic
 * state. That configuration is made effective by calling
 * intel_shared_dpll_swap_state(). The reference should be released by calling
 * intel_release_shared_dpll().
 *
 * Returns:
 * A shared DPLL to be used by @crtc and @encoder with the given @crtc_state.
 */
struct intel_shared_dpll *
intel_get_shared_dpll(struct intel_crtc *crtc,
		      struct intel_crtc_state *crtc_state,
		      struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_dpll_mgr *dpll_mgr = dev_priv->dpll_mgr;

	if (WARN_ON(!dpll_mgr))
		return NULL;

	return dpll_mgr->get_dpll(crtc, crtc_state, encoder);
}

/**
 * intel_release_shared_dpll - end use of DPLL by CRTC in atomic state
 * @dpll: dpll in use by @crtc
 * @crtc: crtc
 * @state: atomic state
 *
 * This function releases the reference from @crtc to @dpll from the
 * atomic @state. The new configuration is made effective by calling
 * intel_shared_dpll_swap_state().
 */
void intel_release_shared_dpll(struct intel_shared_dpll *dpll,
			       struct intel_crtc *crtc,
			       struct drm_atomic_state *state)
{
	struct intel_shared_dpll_state *shared_dpll_state;

	shared_dpll_state = intel_atomic_get_shared_dpll_state(state);
	shared_dpll_state[dpll->id].crtc_mask &= ~(1 << crtc->pipe);
}

/**
 * intel_shared_dpll_dump_hw_state - write hw_state to dmesg
 * @dev_priv: i915 drm device
 * @hw_state: hw state to be written to the log
 *
 * Write the relevant values in @hw_state to dmesg using DRM_DEBUG_KMS.
 */
void intel_dpll_dump_hw_state(struct drm_i915_private *dev_priv,
			      struct intel_dpll_hw_state *hw_state)
{
	if (dev_priv->dpll_mgr) {
		dev_priv->dpll_mgr->dump_hw_state(dev_priv, hw_state);
	} else {
		/* fallback for platforms that don't use the shared dpll
		 * infrastructure
		 */
		DRM_DEBUG_KMS("dpll_hw_state: dpll: 0x%x, dpll_md: 0x%x, "
			      "fp0: 0x%x, fp1: 0x%x\n",
			      hw_state->dpll,
			      hw_state->dpll_md,
			      hw_state->fp0,
			      hw_state->fp1);
	}
}
